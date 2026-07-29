#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
gem5_root="$project_root/gem5"
build_dir="$project_root/npu-tests/build/xai-elf"
start_src="$project_root/npu-tests/baremetal/xai-elf/start.S"
linker_script="$project_root/npu-tests/baremetal/xai-elf/linker.ld"
xai_vcd_checker="$project_root/npu-tests/reference/xai-elf/check_xai_vcd.py"
gem5_bin="$gem5_root/build/RISCV/gem5.opt"
gem5_config="$gem5_root/configs/example/npu/baremetal_xiangshan.py"
riscv_toolchain_root="${RISCV_TOOLCHAIN_ROOT:-$project_root/riscv_bin}"
riscv_toolchain_bin="${RISCV_TOOLCHAIN_BIN:-}"
riscv_toolchain_prefix="${RISCV_TOOLCHAIN_PREFIX:-}"

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

usage()
{
    echo "Usage: $0 [all|build-gem5|run-smoke|run-multinpu|run-vcu-backpressure|run-vcu-backpressure-cache-debug|run-cube-smoke]" >&2
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

build_gem5()
{
    local jobs
    jobs=${JOBS:-$(nproc)}
    echo "+ scons build/RISCV/gem5.opt USE_SYSTEMC=1 RUBY=False USE_KVM=False BUILD_GPU=False --linker=lld -j$jobs"
    (
        cd "$gem5_root"
        scons build/RISCV/gem5.opt USE_SYSTEMC=1 RUBY=False USE_KVM=False \
            BUILD_GPU=False --linker=lld -j"$jobs"
    )
    test -x "$gem5_bin"
}

require_gem5()
{
    if [[ ! -x "$gem5_bin" ]]; then
        echo "FAIL: $gem5_bin is missing; run '$0 build-gem5' first." >&2
        exit 1
    fi
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

run_xai_sim()
{
    local timeout_seconds=$1
    local log_file=$2
    local m5out=$3
    local elf_file=$4
    local gm_root=$5
    local vcd_base=$6
    local npu_count=${7:-}
    local debug_file=${8:-}
    local dramsim3_output_dir="$build_dir/gem5_output/dramsim3"

    local gem5_args=("$gem5_bin")
    if [[ -n "$debug_file" ]]; then
        gem5_args+=(
            --debug-flags=SimpleCPU,Cache,CachePort,MSHR
            --debug-file="$debug_file"
        )
    fi
    local sim_args=(
        --outdir="$m5out"
        "$gem5_config"
        --baremetal-bin "$elf_file"
        --enable-npu
    )
    if [[ -n "$npu_count" ]]; then
        sim_args+=(--npu-count "$npu_count")
    fi
    sim_args+=(
        --npu-enable-sim-gm-file-io
        --npu-sim-gm-file-io-root "$gm_root"
        --dramsim3-output-dir "$dramsim3_output_dir"
        --npu-cmd-base 0x20000000
        --npu-cmd-size 0x1000
        --npu-vcd-trace-file "$vcd_base"
        --cacheline_size=1024
    )
    if [[ -n "$npu_count" ]]; then
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

generate_smoke_data()
{
    local data_generator=$1
    local gm_file_io_root=$2
    local hart_id=$3
    local file_index=$4
    local expected_result_bin=$5
    local actual_result_bin=$6
    local write_data_fixture="$gm_file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $data_generator --gm-file-io-root $gm_file_io_root --hart-id $hart_id --index $file_index --expected-bin $expected_result_bin"
    python3 "$data_generator" \
        --gm-file-io-root "$gm_file_io_root" \
        --hart-id "$hart_id" \
        --index "$file_index" \
        --expected-bin "$expected_result_bin"
    test -s "$write_data_fixture"
    test -s "$expected_result_bin"
    rm -f "$actual_result_bin"
    rm -f "$gm_file_io_root/xai_write_data_to_gm.bin"
    rm -f "$gm_file_io_root/xai_result.bin"
}

run_smoke_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local gm_file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 30 "$log_file" "$m5out_dir" "$elf_file" \
        "$gm_file_io_root" "$vcd_base"
}

check_smoke_log()
{
    local log_file=$1
    local vcd_file=$2
    local expected_result_bin=$3
    local actual_result_bin=$4
    local op
    check_xai_log_common "smoke" "$log_file"

    for op in mte4 mte2 vload vstore vadd nsetvl sync_set sync_wait \
        WriteDataToGm LoadDataFromGm; do
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=$op " "$log_file"; then
            cat "$log_file"
            echo "FAIL: Xai operation $op did not reach NPU target." >&2
            exit 1
        fi
    done

    local boundary
    local src
    local dst
    local sync_id
    for boundary in "3 0 0" "0 2 1" "2 1 2" "1 3 3"; do
        read -r src dst sync_id <<<"$boundary"
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_set .*opcode=4 subopcode=1 .*sync_src=$src sync_dst=$dst sync_id=$sync_id" "$log_file"; then
            cat "$log_file"
            echo "FAIL: expected sync_set commands at GM-to-MTE4, MTE4-to-VCU, VCU-to-MTE2, and MTE2-to-GM-file boundaries." >&2
            exit 1
        fi
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_wait .*opcode=4 subopcode=2 .*sync_src=$src sync_dst=$dst sync_id=$sync_id" "$log_file"; then
            cat "$log_file"
            echo "FAIL: expected sync_wait commands at GM-to-MTE4, MTE4-to-VCU, VCU-to-MTE2, and MTE2-to-GM-file boundaries." >&2
            exit 1
        fi
    done

    if ! grep -q "rd_value=16" "$log_file"; then
        cat "$log_file"
        echo "FAIL: MTE rlen rd source snapshot was not observed." >&2
        exit 1
    fi

    if [[ ! -s "$actual_result_bin" ]]; then
        cat "$log_file"
        echo "FAIL: NPU result bin was not generated: $actual_result_bin" >&2
        exit 1
    fi

    echo "+ cmp -s $expected_result_bin $actual_result_bin"
    if ! cmp -s "$expected_result_bin" "$actual_result_bin"; then
        cat "$log_file"
        echo "FAIL: NPU result bin differs from expected VADD output." >&2
        echo "+ cmp -l $expected_result_bin $actual_result_bin" >&2
        cmp -l "$expected_result_bin" "$actual_result_bin" >&2 || true
        exit 1
    fi

    check_xai_vcd "smoke" "$vcd_file" 1 "$log_file"
}

run_smoke()
{
    local smoke_src="$project_root/npu-tests/baremetal/xai-elf/xai_smoke.cc"
    local smoke_elf="$build_dir/xai_smoke.elf"
    local smoke_asm_file="$build_dir/xai_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local gem5_log="$gem5_output_dir/xai_smoke.log"
    local m5out_dir="$gem5_output_dir/m5out"
    local smoke_vcd_base="xai_smoke_npu_trace"
    local smoke_vcd_file="$m5out_dir/${smoke_vcd_base}.vcd"
    local gm_file_io_root="$build_dir/gm_file_io"
    local data_generator="$project_root/npu-tests/reference/xai-elf/generate_smoke_data.py"
    local file_hart_id=0
    local file_index=0
    local expected_result_bin="$build_dir/xai_expected.bin"
    local actual_result_bin="$gm_file_io_root/GMOutputFile_${file_hart_id}_${file_index}.bin"

    require_gem5
    generate_smoke_data "$data_generator" "$gm_file_io_root" "$file_hart_id" \
        "$file_index" "$expected_result_bin" "$actual_result_bin"
    compile_xai_elf "$smoke_src" "$smoke_elf" "$smoke_asm_file"
    check_xai_elf "smoke" "$smoke_elf"
    run_smoke_sim "$gem5_log" "$m5out_dir" "$smoke_elf" \
        "$gm_file_io_root" "$smoke_vcd_base"
    check_smoke_log "$gem5_log" "$smoke_vcd_file" "$expected_result_bin" \
        "$actual_result_bin"
    echo "PASS: Xai full-flow ELF completed NPU execution with expected VADD result."
}

generate_multinpu_data()
{
    local multinpu_data_generator=$1
    local multinpu_gm_file_io_root=$2
    local multinpu_expected_root=$3
    local npu_count=$4

    echo "+ python3 $multinpu_data_generator --gm-file-io-root $multinpu_gm_file_io_root --expected-root $multinpu_expected_root --npu-count $npu_count"
    python3 "$multinpu_data_generator" \
        --gm-file-io-root "$multinpu_gm_file_io_root" \
        --expected-root "$multinpu_expected_root" \
        --npu-count "$npu_count"
    local npu_id
    for ((npu_id = 0; npu_id < npu_count; ++npu_id)); do
        test -s "$multinpu_gm_file_io_root/npu${npu_id}/GMInputFile_0_0.bin"
        test -s "$multinpu_expected_root/xai_multinpu_expected_npu${npu_id}.bin"
        rm -f "$multinpu_gm_file_io_root/npu${npu_id}/GMOutputFile_0_0.bin"
    done
}

run_multinpu_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local gm_file_io_root=$4
    local vcd_base=$5
    local npu_count=$6
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 45 "$log_file" "$m5out_dir" "$elf_file" \
        "$gm_file_io_root" "$vcd_base" "$npu_count"
}

check_multinpu_log()
{
    local log_file=$1
    local vcd_file=$2
    local gm_file_io_root=$3
    local expected_root=$4
    local npu_count=$5

    check_xai_log_common "multi-NPU" "$log_file"

    local npu_id
    for ((npu_id = 0; npu_id < npu_count; ++npu_id)); do
        if ! grep -Fq "CPU[0]NPU[${npu_id}] : op=WriteDataToGm opcode=5 subopcode=0 mask=0xf" "$log_file"; then
            cat "$log_file"
            echo "FAIL: NPU${npu_id} did not execute its WriteDataToGm command." >&2
            exit 1
        fi

        local expected_bin
        local actual_bin
        expected_bin="$expected_root/xai_multinpu_expected_npu${npu_id}.bin"
        actual_bin="$gm_file_io_root/npu${npu_id}/GMOutputFile_0_0.bin"
        if [[ ! -s "$actual_bin" ]]; then
            cat "$log_file"
            echo "FAIL: NPU${npu_id} result bin was not generated: $actual_bin" >&2
            exit 1
        fi
        echo "+ cmp -s $expected_bin $actual_bin"
        if ! cmp -s "$expected_bin" "$actual_bin"; then
            cat "$log_file"
            echo "FAIL: NPU${npu_id} result bin differs from expected VADD output." >&2
            echo "+ cmp -l $expected_bin $actual_bin" >&2
            cmp -l "$expected_bin" "$actual_bin" >&2 || true
            exit 1
        fi
    done

    check_xai_vcd "multi-NPU" "$vcd_file" "$npu_count" "$log_file"
}

run_multinpu()
{
    local multinpu_src="$project_root/npu-tests/baremetal/xai-elf/xai_multinpu_smoke.cc"
    local multinpu_elf="$build_dir/xai_multinpu_smoke.elf"
    local multinpu_asm_file="$build_dir/xai_multinpu_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local multinpu_log="$gem5_output_dir/xai_multinpu_smoke.log"
    local multinpu_m5out_dir="$gem5_output_dir/multinpu_m5out"
    local multinpu_vcd_base="xai_multinpu_npu_trace"
    local multinpu_vcd_file="$multinpu_m5out_dir/${multinpu_vcd_base}.vcd"
    local multinpu_gm_file_io_root="$build_dir/gm_file_io_multinpu"
    local multinpu_expected_root="$build_dir/multinpu_expected"
    local multinpu_data_generator="$project_root/npu-tests/reference/xai-elf/generate_multinpu_smoke_data.py"
    local npu_count=4

    require_gem5
    generate_multinpu_data "$multinpu_data_generator" \
        "$multinpu_gm_file_io_root" "$multinpu_expected_root" "$npu_count"
    compile_xai_elf "$multinpu_src" "$multinpu_elf" "$multinpu_asm_file"
    check_xai_elf "multi-NPU" "$multinpu_elf"
    run_multinpu_sim "$multinpu_log" "$multinpu_m5out_dir" \
        "$multinpu_elf" "$multinpu_gm_file_io_root" "$multinpu_vcd_base" \
        "$npu_count"
    check_multinpu_log "$multinpu_log" "$multinpu_vcd_file" \
        "$multinpu_gm_file_io_root" "$multinpu_expected_root" "$npu_count"
    echo "PASS: single-CPU multi-NPU Xai flow completed with distinct per-NPU GM results."
}

generate_cube_smoke_data()
{
    local cube_smoke_data_generator=$1
    local cube_smoke_gm_file_io_root=$2
    local hart_id=$3
    local file_index=$4
    local cube_smoke_expected_result_bin=$5
    local cube_smoke_actual_result_bin=$6
    local cube_smoke_write_data_fixture="$cube_smoke_gm_file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $cube_smoke_data_generator --gm-file-io-root $cube_smoke_gm_file_io_root --hart-id $hart_id --index $file_index --expected-bin $cube_smoke_expected_result_bin"
    python3 "$cube_smoke_data_generator" \
        --gm-file-io-root "$cube_smoke_gm_file_io_root" \
        --hart-id "$hart_id" \
        --index "$file_index" \
        --expected-bin "$cube_smoke_expected_result_bin"
    test -s "$cube_smoke_write_data_fixture"
    test -s "$cube_smoke_expected_result_bin"
    rm -f "$cube_smoke_actual_result_bin"
}

run_cube_smoke_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local gm_file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 60 "$log_file" "$m5out_dir" "$elf_file" \
        "$gm_file_io_root" "$vcd_base"
}

check_cube_smoke_log()
{
    local log_file=$1
    local vcd_file=$2
    local expected_result_bin=$3
    local actual_result_bin=$4
    local op
    check_xai_log_common "cube smoke" "$log_file"

    for op in WriteDataToGm mte4_gm_to_l1 mte1_l1_to_l0a \
        mte1_l1_to_l0b cube_mma_fp32 fixpipe_l0c_to_l1 \
        mte1_l1_to_ub mte2_ub_to_gm LoadDataFromGm sync_set sync_wait; do
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=$op " "$log_file"; then
            cat "$log_file"
            echo "FAIL: cube smoke operation $op did not reach NPU target." >&2
            exit 1
        fi
    done

    if [[ ! -s "$actual_result_bin" ]]; then
        cat "$log_file"
        echo "FAIL: cube smoke result bin was not generated: $actual_result_bin" >&2
        exit 1
    fi

    echo "+ cmp -s $expected_result_bin $actual_result_bin"
    if ! cmp -s "$expected_result_bin" "$actual_result_bin"; then
        cat "$log_file"
        echo "FAIL: cube smoke result bin differs from expected fp32 matmul output." >&2
        echo "+ cmp -l $expected_result_bin $actual_result_bin" >&2
        cmp -l "$expected_result_bin" "$actual_result_bin" >&2 || true
        exit 1
    fi

    check_xai_vcd "cube smoke" "$vcd_file" 1 "$log_file"
}

run_cube_smoke()
{
    local cube_smoke_src="$project_root/npu-tests/baremetal/xai-elf/xai_cube_smoke.cc"
    local cube_smoke_elf="$build_dir/xai_cube_smoke.elf"
    local cube_smoke_asm_file="$build_dir/xai_cube_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local cube_smoke_log="$gem5_output_dir/xai_cube_smoke.log"
    local cube_smoke_m5out_dir="$gem5_output_dir/cube_smoke_m5out"
    local cube_smoke_vcd_base="xai_cube_smoke_npu_trace"
    local cube_smoke_vcd_file="$cube_smoke_m5out_dir/${cube_smoke_vcd_base}.vcd"
    local cube_smoke_gm_file_io_root="$build_dir/gm_file_io_cube_smoke"
    local cube_smoke_data_generator="$project_root/npu-tests/reference/xai-elf/generate_cube_smoke_data.py"
    local file_hart_id=0
    local file_index=0
    local cube_smoke_expected_result_bin="$build_dir/xai_cube_smoke_expected.bin"
    local cube_smoke_actual_result_bin="$cube_smoke_gm_file_io_root/GMOutputFile_${file_hart_id}_${file_index}.bin"

    require_gem5
    generate_cube_smoke_data "$cube_smoke_data_generator" \
        "$cube_smoke_gm_file_io_root" "$file_hart_id" "$file_index" \
        "$cube_smoke_expected_result_bin" "$cube_smoke_actual_result_bin"
    compile_xai_elf "$cube_smoke_src" "$cube_smoke_elf" \
        "$cube_smoke_asm_file"
    check_xai_elf "cube smoke" "$cube_smoke_elf"
    run_cube_smoke_sim "$cube_smoke_log" "$cube_smoke_m5out_dir" \
        "$cube_smoke_elf" "$cube_smoke_gm_file_io_root" \
        "$cube_smoke_vcd_base"
    check_cube_smoke_log "$cube_smoke_log" "$cube_smoke_vcd_file" \
        "$cube_smoke_expected_result_bin" "$cube_smoke_actual_result_bin"
    echo "PASS: cube smoke completed GM/L1/L0/Cube/Fixpipe/UB/GM flow."
}

generate_vcu_backpressure_data()
{
    local data_generator=$1
    local gm_file_io_root=$2
    local hart_id=$3
    local file_index=$4
    local recursive_add_count=$5
    local expected_result_bin=$6
    local actual_result_bin=$7
    local write_data_fixture="$gm_file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $data_generator --gm-file-io-root $gm_file_io_root --hart-id $hart_id --index $file_index --recursive-add-count $recursive_add_count --expected-bin $expected_result_bin"
    python3 "$data_generator" \
        --gm-file-io-root "$gm_file_io_root" \
        --hart-id "$hart_id" \
        --index "$file_index" \
        --recursive-add-count "$recursive_add_count" \
        --expected-bin "$expected_result_bin"
    test -s "$write_data_fixture"
    test -s "$expected_result_bin"
    rm -f "$actual_result_bin"
}

run_vcu_backpressure_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local gm_file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 60 "$log_file" "$m5out_dir" "$elf_file" \
        "$gm_file_io_root" "$vcd_base"
}

run_vcu_backpressure_cache_debug_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local gm_file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file" "$m5out_dir/icache_debug.log"
    run_xai_sim 60 "$log_file" "$m5out_dir" "$elf_file" \
        "$gm_file_io_root" "$vcd_base" "" "icache_debug.log"
}

check_vcu_backpressure_log()
{
    local log_file=$1
    local vcd_file=$2

    check_xai_log_common "VCU backpressure" "$log_file"

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_set .* sync_src=2 sync_dst=1 sync_id=2" "$log_file"; then
        cat "$log_file"
        echo "FAIL: VCU-to-MTE2 sync_set did not complete." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_wait .* sync_src=2 sync_dst=1 sync_id=2" "$log_file"; then
        cat "$log_file"
        echo "FAIL: VCU-to-MTE2 sync_wait did not complete." >&2
        exit 1
    fi

    check_xai_vcd "VCU backpressure" "$vcd_file" 1 "$log_file"
    check_vcd_signal_minimum "VCU backpressure" "$vcd_file" \
        "vcu_queue_size" 8 "$log_file"
}

run_vcu_backpressure()
{
    local vcu_backpressure_src="$project_root/npu-tests/baremetal/xai-elf/xai_vcu_backpressure.cc"
    local vcu_backpressure_elf="$build_dir/xai_vcu_backpressure.elf"
    local vcu_backpressure_asm_file="$build_dir/xai_vcu_backpressure.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local vcu_backpressure_log="$gem5_output_dir/xai_vcu_backpressure.log"
    local vcu_backpressure_m5out_dir="$gem5_output_dir/vcu_backpressure_m5out"
    local vcu_backpressure_vcd_base="xai_vcu_backpressure_npu_trace"
    local vcu_backpressure_vcd_file="$vcu_backpressure_m5out_dir/${vcu_backpressure_vcd_base}.vcd"
    local vcu_backpressure_gm_file_io_root="$build_dir/gm_file_io_vcu_backpressure"
    local vcu_backpressure_data_generator="$project_root/npu-tests/reference/xai-elf/generate_vcu_backpressure_data.py"
    local file_hart_id=0
    local vcu_backpressure_file_index=1
    local vcu_backpressure_recursive_add_count=16
    local vcu_backpressure_expected_result_bin="$build_dir/xai_vcu_backpressure_expected.bin"
    local vcu_backpressure_actual_result_bin="$vcu_backpressure_gm_file_io_root/GMOutputFile_${file_hart_id}_${vcu_backpressure_file_index}.bin"

    require_gem5
    generate_vcu_backpressure_data "$vcu_backpressure_data_generator" \
        "$vcu_backpressure_gm_file_io_root" "$file_hart_id" \
        "$vcu_backpressure_file_index" "$vcu_backpressure_recursive_add_count" \
        "$vcu_backpressure_expected_result_bin" \
        "$vcu_backpressure_actual_result_bin"
    compile_xai_elf "$vcu_backpressure_src" "$vcu_backpressure_elf" \
        "$vcu_backpressure_asm_file"
    check_xai_elf "VCU backpressure" "$vcu_backpressure_elf"
    run_vcu_backpressure_sim "$vcu_backpressure_log" \
        "$vcu_backpressure_m5out_dir" "$vcu_backpressure_elf" \
        "$vcu_backpressure_gm_file_io_root" "$vcu_backpressure_vcd_base"
    check_vcu_backpressure_log "$vcu_backpressure_log" \
        "$vcu_backpressure_vcd_file"
    echo "PASS: recursive VCU vadd flow grew the VCU FIFO."
}

run_vcu_backpressure_cache_debug()
{
    local vcu_backpressure_src="$project_root/npu-tests/baremetal/xai-elf/xai_vcu_backpressure.cc"
    local vcu_backpressure_elf="$build_dir/xai_vcu_backpressure.elf"
    local vcu_backpressure_asm_file="$build_dir/xai_vcu_backpressure.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local vcu_backpressure_cache_debug_log="$gem5_output_dir/xai_vcu_backpressure_cache_debug.log"
    local vcu_backpressure_cache_debug_m5out_dir="$gem5_output_dir/vcu_backpressure_cache_debug_m5out"
    local vcu_backpressure_vcd_base="xai_vcu_backpressure_npu_trace"
    local vcu_backpressure_gm_file_io_root="$build_dir/gm_file_io_vcu_backpressure"
    local vcu_backpressure_data_generator="$project_root/npu-tests/reference/xai-elf/generate_vcu_backpressure_data.py"
    local file_hart_id=0
    local vcu_backpressure_file_index=1
    local vcu_backpressure_recursive_add_count=16
    local vcu_backpressure_expected_result_bin="$build_dir/xai_vcu_backpressure_expected.bin"
    local vcu_backpressure_actual_result_bin="$vcu_backpressure_gm_file_io_root/GMOutputFile_${file_hart_id}_${vcu_backpressure_file_index}.bin"

    require_gem5
    generate_vcu_backpressure_data "$vcu_backpressure_data_generator" \
        "$vcu_backpressure_gm_file_io_root" "$file_hart_id" \
        "$vcu_backpressure_file_index" "$vcu_backpressure_recursive_add_count" \
        "$vcu_backpressure_expected_result_bin" \
        "$vcu_backpressure_actual_result_bin"
    compile_xai_elf "$vcu_backpressure_src" "$vcu_backpressure_elf" \
        "$vcu_backpressure_asm_file"
    check_xai_elf "VCU backpressure" "$vcu_backpressure_elf"
    run_vcu_backpressure_cache_debug_sim "$vcu_backpressure_cache_debug_log" \
        "$vcu_backpressure_cache_debug_m5out_dir" "$vcu_backpressure_elf" \
        "$vcu_backpressure_gm_file_io_root" "$vcu_backpressure_vcd_base"
    check_xai_log_common "VCU backpressure cache debug" \
        "$vcu_backpressure_cache_debug_log"
    echo "PASS: VCU backpressure cache debug log generated at $vcu_backpressure_cache_debug_m5out_dir/icache_debug.log"
}

phase=${1:-all}
if [[ $# -gt 1 ]]; then
    usage
    exit 2
fi

check_host
cd "$project_root"
mkdir -p "$build_dir"

case "$phase" in
    all)
        build_gem5
        run_smoke
        run_multinpu
        run_cube_smoke
        run_vcu_backpressure
        ;;
    build-gem5)
        build_gem5
        ;;
    run-smoke)
        run_smoke
        ;;
    run-multinpu)
        run_multinpu
        ;;
    run-cube-smoke)
        run_cube_smoke
        ;;
    run-vcu-backpressure)
        run_vcu_backpressure
        ;;
    run-vcu-backpressure-cache-debug)
        run_vcu_backpressure_cache_debug
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
