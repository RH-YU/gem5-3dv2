#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
gem5_root="$project_root/gem5"
build_dir="${build_dir:-$project_root/npu-tests/build}"
start_src="$project_root/npu-tests/baremetal/common/start.S"
linker_script="$project_root/npu-tests/baremetal/common/linker.ld"
xai_vcd_checker="$project_root/npu-tests/reference/common/check_xai_vcd.py"
gem5_bin="$gem5_root/build/RISCV/gem5.opt"
gem5_config="$gem5_root/configs/example/npu/baremetal_xiangshan.py"
riscv_toolchain_root="${RISCV_TOOLCHAIN_ROOT:-$project_root/riscv_bin}"
riscv_toolchain_bin="${RISCV_TOOLCHAIN_BIN:-}"
riscv_toolchain_prefix="${RISCV_TOOLCHAIN_PREFIX:-}"
l1i_hwp_type="${L1I_HWP_TYPE:-}"
cache_log_enabled="${CACHE_LOG:-0}"
cache_log_flags="${CACHE_LOG_FLAGS:-SimpleCPU,Cache,CachePort,MSHR}"
cache_log_file="${CACHE_LOG_FILE:-cache_debug.log}"
npu_type="${NPU_TYPE:-single}"
multi_npu_count="${MULTI_NPU_COUNT:-4}"
cacheline_size="${CACHELINE_SIZE:-64}"
rvv_impl="${RVV_IMPL:-npu}"

toolchain_bin_candidates()
{
    [[ -n "$riscv_toolchain_bin" ]] && printf '%s\n' "$riscv_toolchain_bin"
    [[ -d "$riscv_toolchain_root/bin" ]] && printf '%s\n' "$riscv_toolchain_root/bin"
    if [[ -d "$riscv_toolchain_root" ]]; then
        find "$riscv_toolchain_root" -mindepth 2 -maxdepth 2 -type d -name bin \
            2>/dev/null | sort
    fi
}

toolchain_prefix_candidates()
{
    [[ -n "$riscv_toolchain_prefix" ]] && printf '%s\n' "$riscv_toolchain_prefix"
    printf '%s\n' riscv-none-elf riscv64-unknown-elf riscv64-unknown
}

tool_exists()
{
    local tool=$1
    [[ -x "$tool" ]] || command -v "$tool" >/dev/null 2>&1
}

pick_prefixed_tool()
{
    local suffix=$1
    local bin_dir
    local prefix

    while IFS= read -r bin_dir; do
        [[ -n "$bin_dir" ]] || continue
        while IFS= read -r prefix; do
            [[ -n "$prefix" ]] || continue
            if [[ -x "$bin_dir/${prefix}-${suffix}" ]]; then
                printf '%s\n' "$bin_dir/${prefix}-${suffix}"
                return 0
            fi
        done < <(toolchain_prefix_candidates)
    done < <(toolchain_bin_candidates)

    while IFS= read -r prefix; do
        [[ -n "$prefix" ]] || continue
        if command -v "${prefix}-${suffix}" >/dev/null 2>&1; then
            printf '%s\n' "${prefix}-${suffix}"
            return 0
        fi
    done < <(toolchain_prefix_candidates)

    return 1
}

pick_riscv_cxx()
{
    local tool
    for tool in \
        "${RISCV64_UNKNOWN_ELF_CXX:-}" \
        "${RISCV_NONE_ELF_CXX:-}"
    do
        [[ -n "$tool" ]] || continue
        if tool_exists "$tool"; then
            printf '%s\n' "$tool"
            return 0
        fi
    done

    if tool=$(pick_prefixed_tool g++); then
        printf '%s\n' "$tool"
        return 0
    fi

    echo "SKIP: no bare-metal RISC-V C++ compiler found; set RISCV_TOOLCHAIN_BIN/RISCV_TOOLCHAIN_PREFIX or RISCV64_UNKNOWN_ELF_CXX/RISCV_NONE_ELF_CXX." >&2
    exit 0
}

pick_riscv_readelf()
{
    local tool
    for tool in \
        "${RISCV64_UNKNOWN_ELF_READELF:-}" \
        "${RISCV_NONE_ELF_READELF:-}"
    do
        [[ -n "$tool" ]] || continue
        if tool_exists "$tool"; then
            printf '%s\n' "$tool"
            return 0
        fi
    done

    if tool=$(pick_prefixed_tool readelf); then
        printf '%s\n' "$tool"
        return 0
    fi

    return 1
}

pick_riscv_objdump()
{
    local tool
    for tool in \
        "${RISCV_NONE_ELF_OBJDUMP:-}" \
        "${RISCV64_UNKNOWN_ELF_OBJDUMP:-}"
    do
        [[ -n "$tool" ]] || continue
        if tool_exists "$tool"; then
            printf '%s\n' "$tool"
            return 0
        fi
    done

    if tool=$(pick_prefixed_tool objdump); then
        printf '%s\n' "$tool"
        return 0
    fi

    echo "SKIP: no RISC-V objdump found; set RISCV_TOOLCHAIN_BIN/RISCV_TOOLCHAIN_PREFIX or RISCV_NONE_ELF_OBJDUMP/RISCV64_UNKNOWN_ELF_OBJDUMP." >&2
    exit 0
}

emit_asm()
{
    local elf_file=$1
    local asm_file=$2
    local objdump_tool
    objdump_tool=$(pick_riscv_objdump)

    echo "+ $objdump_tool -d $elf_file > $asm_file"
    "$objdump_tool" -d "$elf_file" >"$asm_file"
    test -s "$asm_file"
}

compile_xai_elf()
{
    local source_file=$1
    local elf_file=$2
    local asm_file=$3
    local compiler
    compiler=$(pick_riscv_cxx)

    local compile_flags=(
        -march=rv64g
        -mabi=lp64
        -mcmodel=medany
        -O2
        -fno-tree-vectorize
        -fno-tree-slp-vectorize
        -mno-relax
        -nostdlib
        -nostartfiles
        -ffreestanding
        -Wl,--no-relax
        -Wl,-T,"$linker_script"
    )

    mkdir -p "$(dirname "$elf_file")" "$(dirname "$asm_file")"
    echo "+ $compiler ${compile_flags[*]} -o $elf_file $start_src $source_file"
    "$compiler" "${compile_flags[@]}" -o "$elf_file" "$start_src" "$source_file"
    test -s "$elf_file"
    emit_asm "$elf_file" "$asm_file"
}

check_xai_elf()
{
    local label=$1
    local elf_file=$2
    local readelf_tool
    if ! readelf_tool=$(pick_riscv_readelf); then
        return
    fi

    echo "+ $readelf_tool -A $elf_file"
    local arch_attrs
    arch_attrs=$("$readelf_tool" -A "$elf_file")
    if grep -Eq "Tag_RISCV_arch:.*(^|_)c[0-9]" <<<"$arch_attrs"; then
        echo "$arch_attrs"
        echo "FAIL: $label ELF advertises the RISC-V compressed C extension." >&2
        exit 1
    fi
    if grep -Eq "Tag_RISCV_arch:.*(^|_)v[0-9]" <<<"$arch_attrs"; then
        echo "$arch_attrs"
        echo "FAIL: $label ELF advertises the RISC-V vector V extension." >&2
        exit 1
    fi
}

check_host()
{
    if [[ "$(uname -s)" != Linux ]]; then
        echo "Linux is required." >&2
        exit 1
    fi

    if [[ -r /etc/os-release ]]; then
        source /etc/os-release
        if [[ "${ID:-}" != ubuntu || "${VERSION_ID:-}" != 24.04 ]]; then
            echo "Warning: validated target is Ubuntu 24.04; found ${ID:-unknown} ${VERSION_ID:-unknown}." >&2
        fi
    else
        echo "Warning: /etc/os-release is unavailable; validated target is Ubuntu 24.04." >&2
    fi
}

is_multi_npu()
{
    [[ "$npu_type" == multi ]]
}

selected_npu_count()
{
    if is_multi_npu; then
        printf '%s\n' "$multi_npu_count"
    else
        printf '%s\n' 1
    fi
}

validate_npu_selection()
{
    case "$npu_type" in
        single|multi)
            ;;
        *)
            echo "FAIL: NPU_TYPE must be 'single' or 'multi'; got '$npu_type'." >&2
            exit 2
            ;;
    esac

    if is_multi_npu; then
        if ! [[ "$multi_npu_count" =~ ^[0-9]+$ ]]; then
            echo "FAIL: MULTI_NPU_COUNT must be an integer; got '$multi_npu_count'." >&2
            exit 2
        fi
        if (( multi_npu_count < 2 || multi_npu_count > 4 )); then
            echo "FAIL: MULTI_NPU_COUNT must be in the range [2, 4]." >&2
            exit 2
        fi
    fi
}

require_single_npu_type()
{
    local label=$1
    if is_multi_npu; then
        echo "FAIL: $label is a single-NPU case; use NPU_TYPE=single." >&2
        exit 2
    fi
}

require_multi_npu_type()
{
    local label=$1
    if ! is_multi_npu; then
        echo "FAIL: $label requires NPU_TYPE=multi." >&2
        exit 2
    fi
}

require_gem5()
{
    if [[ ! -x "$gem5_bin" ]]; then
        echo "FAIL: $gem5_bin is missing; run npu-tests/scripts/build_gem5.sh first." >&2
        exit 1
    fi
}

run_xai_sim()
{
    local timeout_seconds=$1
    local log_file=$2
    local m5out=$3
    local elf_file=$4
    local gm_root=$5
    local vcd_base=$6
    local npu_count
    npu_count=$(selected_npu_count)
    local dramsim3_output_dir="$build_dir/gem5_output/dramsim3"

    local gem5_args=("$gem5_bin")
    if [[ "$cache_log_enabled" == 1 || "$cache_log_enabled" == true ]]; then
        gem5_args+=(
            --debug-flags="$cache_log_flags"
            --debug-file="$cache_log_file"
        )
    fi
    local sim_args=(
        --outdir="$m5out"
        "$gem5_config"
        --baremetal-bin "$elf_file"
        --enable-npu
    )
    sim_args+=(--npu-count "$npu_count")
    if [[ -n "$l1i_hwp_type" ]]; then
        sim_args+=(--l1i-hwp-type "$l1i_hwp_type")
    fi
    sim_args+=(
        --npu-enable-sim-file-io
        --npu-sim-file-io-root "$gm_root"
        --dramsim3-output-dir "$dramsim3_output_dir"
        --npu-dispatch-id 1
        --npu-vcd-trace-file "$vcd_base"
        --cacheline_size "$cacheline_size"
    )
    if is_multi_npu; then
        sim_args+=(--num-cpus 1)
    fi
    sim_args+=(--mem-size 2GB)

    mkdir -p "$(dirname "$log_file")" "$m5out" "$gm_root" \
        "$dramsim3_output_dir"
    echo "+ timeout ${timeout_seconds}s ${gem5_args[*]} ${sim_args[*]}"
    set +e
    timeout "${timeout_seconds}s" "${gem5_args[@]}" "${sim_args[@]}" \
        >"$log_file" 2>&1
    local status=$?
    set -e

    if [[ $status -ne 0 && $status -ne 124 ]]; then
        cat "$log_file"
        exit "$status"
    fi
}

check_xai_log_common()
{
    local label=$1
    local log_file=$2
    if ! grep -q "m5_exit instruction encountered" "$log_file"; then
        cat "$log_file"
        echo "FAIL: $label ELF did not terminate through the agreed m5_exit instruction." >&2
        exit 1
    fi
    if grep -Fq "] : fault " "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU reported a fault during the $label Xai flow." >&2
        exit 1
    fi
    if grep -Eq "NPU_(ACCEPT|IGNORE|XAI_SUBMIT|COMPLETE|FAULT|RESULT|LOAD_DATA_FROM_GM)" "$log_file"; then
        cat "$log_file"
        echo "FAIL: legacy NPU log markers should not be emitted." >&2
        exit 1
    fi
}

check_xai_vcd()
{
    local label=$1
    local vcd_file=$2
    local npu_count=$3
    local log_file=$4

    if [[ ! -s "$vcd_file" ]]; then
        cat "$log_file"
        echo "FAIL: $label NPU VCD trace was not generated: $vcd_file" >&2
        exit 1
    fi
    if ! python3 "$xai_vcd_checker" structure \
        --vcd-file "$vcd_file" --npu-count "$npu_count"
    then
        echo "FAIL: $label VCD trace is malformed or missing expected scopes/signals." >&2
        exit 1
    fi
}

check_vcd_signal_asserted()
{
    local label=$1
    local vcd_file=$2
    local signal_name=$3
    local log_file=$4

    if ! python3 "$xai_vcd_checker" signal-asserted \
        --vcd-file "$vcd_file" --signal-name "$signal_name"
    then
        cat "$log_file"
        echo "FAIL: $label VCD signal $signal_name was not asserted." >&2
        exit 1
    fi
}

check_vcd_signal_minimum()
{
    local label=$1
    local vcd_file=$2
    local signal_name=$3
    local minimum_value=$4
    local log_file=$5

    if ! python3 "$xai_vcd_checker" signal-minimum \
        --vcd-file "$vcd_file" --signal-name "$signal_name" \
        --minimum-value "$minimum_value"
    then
        cat "$log_file"
        echo "FAIL: $label VCD signal $signal_name never reached at least $minimum_value." >&2
        exit 1
    fi
}
