#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
gem5_root="$project_root/gem5"
build_dir="$project_root/npu-tests/build/xai-elf"
smoke_src="$project_root/npu-tests/baremetal/xai-elf/xai_smoke.cc"
multinpu_src="$project_root/npu-tests/baremetal/xai-elf/xai_multinpu_smoke.cc"
vcu_backpressure_src="$project_root/npu-tests/baremetal/xai-elf/xai_vcu_backpressure.cc"
start_src="$project_root/npu-tests/baremetal/xai-elf/start.S"
linker_script="$project_root/npu-tests/baremetal/xai-elf/linker.ld"
data_generator="$project_root/npu-tests/reference/xai-elf/generate_smoke_data.py"
multinpu_data_generator="$project_root/npu-tests/reference/xai-elf/generate_multinpu_smoke_data.py"
vcu_backpressure_data_generator="$project_root/npu-tests/reference/xai-elf/generate_vcu_backpressure_data.py"
smoke_elf="$build_dir/xai_smoke.elf"
multinpu_elf="$build_dir/xai_multinpu_smoke.elf"
vcu_backpressure_elf="$build_dir/xai_vcu_backpressure.elf"
smoke_asm_file="$build_dir/xai_smoke.asm"
multinpu_asm_file="$build_dir/xai_multinpu_smoke.asm"
vcu_backpressure_asm_file="$build_dir/xai_vcu_backpressure.asm"
gem5_bin="$gem5_root/build/RISCV/gem5.opt"
gem5_config="$gem5_root/configs/example/npu/baremetal_xiangshan.py"
gem5_output_dir="$build_dir/gem5_output"
gem5_log="$gem5_output_dir/xai_smoke.log"
multinpu_log="$gem5_output_dir/xai_multinpu_smoke.log"
vcu_backpressure_log="$gem5_output_dir/xai_vcu_backpressure.log"
m5out_dir="$gem5_output_dir/m5out"
multinpu_m5out_dir="$gem5_output_dir/multinpu_m5out"
vcu_backpressure_m5out_dir="$gem5_output_dir/vcu_backpressure_m5out"
smoke_vcd_base="xai_smoke_npu_trace"
smoke_vcd_file="$m5out_dir/${smoke_vcd_base}.vcd"
multinpu_vcd_base="xai_multinpu_npu_trace"
multinpu_vcd_file="$multinpu_m5out_dir/${multinpu_vcd_base}.vcd"
vcu_backpressure_vcd_base="xai_vcu_backpressure_npu_trace"
vcu_backpressure_vcd_file="$vcu_backpressure_m5out_dir/${vcu_backpressure_vcd_base}.vcd"
dramsim3_output_dir="$gem5_output_dir/dramsim3"
gm_file_io_root="$build_dir/gm_file_io"
multinpu_gm_file_io_root="$build_dir/gm_file_io_multinpu"
vcu_backpressure_gm_file_io_root="$build_dir/gm_file_io_vcu_backpressure"
multinpu_expected_root="$build_dir/multinpu_expected"
file_hart_id=0
file_index=0
vcu_backpressure_file_index=1
vcu_backpressure_recursive_add_count=16
write_data_fixture="$gm_file_io_root/GMInputFile_${file_hart_id}_${file_index}.bin"
expected_result_bin="$build_dir/xai_expected.bin"
actual_result_bin="$gm_file_io_root/GMOutputFile_${file_hart_id}_${file_index}.bin"
vcu_backpressure_write_data_fixture="$vcu_backpressure_gm_file_io_root/GMInputFile_${file_hart_id}_${vcu_backpressure_file_index}.bin"
vcu_backpressure_expected_result_bin="$build_dir/xai_vcu_backpressure_expected.bin"
vcu_backpressure_actual_result_bin="$vcu_backpressure_gm_file_io_root/GMOutputFile_${file_hart_id}_${vcu_backpressure_file_index}.bin"
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
    echo "Usage: $0 [all|build-gem5|run-smoke|run-multinpu|run-vcu-backpressure]" >&2
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
    )
    if [[ -n "$npu_count" ]]; then
        sim_args+=(--num-cpus 1)
    fi
    sim_args+=(--mem-size 2GB)

    echo "+ timeout ${timeout_seconds}s $gem5_bin ${sim_args[*]}"
    set +e
    timeout "${timeout_seconds}s" "$gem5_bin" "${sim_args[@]}" \
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
    if ! python3 - "$vcd_file" "$npu_count" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
npu_count = int(sys.argv[2])
data = path.read_bytes()
if any(byte > 0x7f for byte in data):
    raise SystemExit(1)
text = data.decode("ascii")
required = [
    "$timescale",
    "$scope module cluster $end",
    "$scope module cpu $end",
    "cmd_event",
    "backpressure_event",
    "npu_clock",
    "commit_event",
    "commit_valid",
    "commit_pc",
    "commit_insn",
]
for needle in required:
    if needle not in text:
        raise SystemExit(1)
for signal in ["commit_pc", "commit_insn"]:
    pattern = rf"\$var\s+wire\s+32\s+\S+\s+{signal}\s+\[31:0\]\s+\$end"
    if not re.search(pattern, text):
        raise SystemExit(1)
commit_insn = re.search(
    r"\$var\s+wire\s+32\s+(\S+)\s+commit_insn\s+\[31:0\]\s+\$end", text)
if not commit_insn or not re.search(
        rf"\bb[01]*1[01]*\s+{re.escape(commit_insn.group(1))}\b", text):
    raise SystemExit(1)
for npu_id in range(npu_count):
    scope = f"$scope module npu{npu_id} $end"
    if scope not in text:
        raise SystemExit(1)
    for signal in [
        "ingress_event",
        "dispatch_event",
        "engine_start_event",
        "engine_done_event",
        "fault_event",
        "sync_event",
        "mte4_busy",
        "mte2_busy",
        "vcu_busy",
        "gm_file_io_busy",
        "scheduler_queue_size",
        "mte4_queue_size",
        "mte2_queue_size",
        "vcu_queue_size",
        "gm_file_io_queue_size",
        "mte4_instruction",
        "mte2_instruction",
        "vcu_instruction",
        "gm_file_io_instruction",
    ]:
        if signal not in text:
            raise SystemExit(1)
PY
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

    if ! python3 - "$vcd_file" "$signal_name" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
signal_name = sys.argv[2]
text = path.read_text(encoding="ascii")
match = re.search(
    rf"\$var\s+\w+\s+\d+\s+(\S+)\s+{re.escape(signal_name)}\s+\$end",
    text,
)
if not match:
    raise SystemExit(1)
identifier = re.escape(match.group(1))
if not re.search(rf"(?m)^1{identifier}$", text):
    raise SystemExit(1)
PY
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

    if ! python3 - "$vcd_file" "$signal_name" "$minimum_value" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
signal_name = sys.argv[2]
minimum_value = int(sys.argv[3], 0)
text = path.read_text(encoding="ascii")
match = re.search(
    rf"\$var\s+\w+\s+32\s+(\S+)\s+{re.escape(signal_name)}\s+\[31:0\]\s+\$end",
    text,
)
if not match:
    raise SystemExit(1)
identifier = re.escape(match.group(1))
values = [
    int(bits, 2)
    for bits in re.findall(rf"(?m)^b([01]+)\s+{identifier}$", text)
]
if not values or max(values) < minimum_value:
    raise SystemExit(1)
PY
    then
        cat "$log_file"
        echo "FAIL: $label VCD signal $signal_name never reached at least $minimum_value." >&2
        exit 1
    fi
}

generate_smoke_data()
{
    echo "+ python3 $data_generator --gm-file-io-root $gm_file_io_root --hart-id $file_hart_id --index $file_index --expected-bin $expected_result_bin"
    python3 "$data_generator" \
        --gm-file-io-root "$gm_file_io_root" \
        --hart-id "$file_hart_id" \
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
    rm -f "$smoke_vcd_file"
    run_xai_sim 30 "$gem5_log" "$m5out_dir" "$smoke_elf" \
        "$gm_file_io_root" "$smoke_vcd_base"
}

check_smoke_log()
{
    local op
    check_xai_log_common "smoke" "$gem5_log"

    for op in mte4 mte2 vload vstore vadd nsetvl sync_set sync_wait \
        WriteDataToGm LoadDataFromGm; do
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=$op " "$gem5_log"; then
            cat "$gem5_log"
            echo "FAIL: Xai operation $op did not reach NPU target." >&2
            exit 1
        fi
    done

    if [[ $(grep -Ec "CPU\\[0\\]NPU\\[0\\] : op=sync_set opcode=4 subopcode=1 " "$gem5_log") -lt 4 ]]; then
        cat "$gem5_log"
        echo "FAIL: expected sync_set commands at GM-to-MTE4, MTE4-to-VCU, VCU-to-MTE2, and MTE2-to-GM-file boundaries." >&2
        exit 1
    fi

    if [[ $(grep -Ec "CPU\\[0\\]NPU\\[0\\] : op=sync_wait opcode=4 subopcode=2 " "$gem5_log") -lt 4 ]]; then
        cat "$gem5_log"
        echo "FAIL: expected sync_wait commands at GM-to-MTE4, MTE4-to-VCU, VCU-to-MTE2, and MTE2-to-GM-file boundaries." >&2
        exit 1
    fi

    if ! grep -q "rd_value=16" "$gem5_log"; then
        cat "$gem5_log"
        echo "FAIL: MTE rlen rd source snapshot was not observed." >&2
        exit 1
    fi

    if [[ ! -s "$actual_result_bin" ]]; then
        cat "$gem5_log"
        echo "FAIL: NPU result bin was not generated: $actual_result_bin" >&2
        exit 1
    fi

    echo "+ cmp -s $expected_result_bin $actual_result_bin"
    if ! cmp -s "$expected_result_bin" "$actual_result_bin"; then
        cat "$gem5_log"
        echo "FAIL: NPU result bin differs from expected VADD output." >&2
        echo "+ cmp -l $expected_result_bin $actual_result_bin" >&2
        cmp -l "$expected_result_bin" "$actual_result_bin" >&2 || true
        exit 1
    fi

    check_xai_vcd "smoke" "$smoke_vcd_file" 1 "$gem5_log"
}

run_smoke()
{
    require_gem5
    generate_smoke_data
    compile_xai_elf "$smoke_src" "$smoke_elf" "$smoke_asm_file"
    check_xai_elf "smoke" "$smoke_elf"
    run_smoke_sim
    check_smoke_log
    echo "PASS: Xai full-flow ELF completed NPU execution with expected VADD result."
}

generate_multinpu_data()
{
    echo "+ python3 $multinpu_data_generator --gm-file-io-root $multinpu_gm_file_io_root --expected-root $multinpu_expected_root --npu-count 4"
    python3 "$multinpu_data_generator" \
        --gm-file-io-root "$multinpu_gm_file_io_root" \
        --expected-root "$multinpu_expected_root" \
        --npu-count 4
    for npu_id in 0 1 2 3; do
        test -s "$multinpu_gm_file_io_root/npu${npu_id}/GMInputFile_0_0.bin"
        test -s "$multinpu_expected_root/xai_multinpu_expected_npu${npu_id}.bin"
        rm -f "$multinpu_gm_file_io_root/npu${npu_id}/GMOutputFile_0_0.bin"
    done
}

run_multinpu_sim()
{
    rm -f "$multinpu_vcd_file"
    run_xai_sim 45 "$multinpu_log" "$multinpu_m5out_dir" "$multinpu_elf" \
        "$multinpu_gm_file_io_root" "$multinpu_vcd_base" 4
}

check_multinpu_log()
{
    check_xai_log_common "multi-NPU" "$multinpu_log"

    for npu_id in 0 1 2 3; do
        if ! grep -Fq "CPU[0]NPU[${npu_id}] : op=WriteDataToGm opcode=5 subopcode=0 mask=0xf" "$multinpu_log"; then
            cat "$multinpu_log"
            echo "FAIL: NPU${npu_id} did not execute its WriteDataToGm command." >&2
            exit 1
        fi

        local expected_bin
        local actual_bin
        expected_bin="$multinpu_expected_root/xai_multinpu_expected_npu${npu_id}.bin"
        actual_bin="$multinpu_gm_file_io_root/npu${npu_id}/GMOutputFile_0_0.bin"
        if [[ ! -s "$actual_bin" ]]; then
            cat "$multinpu_log"
            echo "FAIL: NPU${npu_id} result bin was not generated: $actual_bin" >&2
            exit 1
        fi
        echo "+ cmp -s $expected_bin $actual_bin"
        if ! cmp -s "$expected_bin" "$actual_bin"; then
            cat "$multinpu_log"
            echo "FAIL: NPU${npu_id} result bin differs from expected VADD output." >&2
            echo "+ cmp -l $expected_bin $actual_bin" >&2
            cmp -l "$expected_bin" "$actual_bin" >&2 || true
            exit 1
        fi
    done

    check_xai_vcd "multi-NPU" "$multinpu_vcd_file" 4 "$multinpu_log"
}

run_multinpu()
{
    require_gem5
    generate_multinpu_data
    compile_xai_elf "$multinpu_src" "$multinpu_elf" "$multinpu_asm_file"
    check_xai_elf "multi-NPU" "$multinpu_elf"
    run_multinpu_sim
    check_multinpu_log
    echo "PASS: single-CPU multi-NPU Xai flow completed with distinct per-NPU GM results."
}

generate_vcu_backpressure_data()
{
    echo "+ python3 $vcu_backpressure_data_generator --gm-file-io-root $vcu_backpressure_gm_file_io_root --hart-id $file_hart_id --index $vcu_backpressure_file_index --recursive-add-count $vcu_backpressure_recursive_add_count --expected-bin $vcu_backpressure_expected_result_bin"
    python3 "$vcu_backpressure_data_generator" \
        --gm-file-io-root "$vcu_backpressure_gm_file_io_root" \
        --hart-id "$file_hart_id" \
        --index "$vcu_backpressure_file_index" \
        --recursive-add-count "$vcu_backpressure_recursive_add_count" \
        --expected-bin "$vcu_backpressure_expected_result_bin"
    test -s "$vcu_backpressure_write_data_fixture"
    test -s "$vcu_backpressure_expected_result_bin"
    rm -f "$vcu_backpressure_actual_result_bin"
}

run_vcu_backpressure_sim()
{
    rm -f "$vcu_backpressure_vcd_file"
    run_xai_sim 60 "$vcu_backpressure_log" "$vcu_backpressure_m5out_dir" \
        "$vcu_backpressure_elf" "$vcu_backpressure_gm_file_io_root" \
        "$vcu_backpressure_vcd_base"
}

check_vcu_backpressure_log()
{
    check_xai_log_common "VCU backpressure" "$vcu_backpressure_log"

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_set .* sync_src=2 sync_dst=1 sync_id=2" "$vcu_backpressure_log"; then
        cat "$vcu_backpressure_log"
        echo "FAIL: VCU-to-MTE2 sync_set did not complete." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_wait .* sync_src=2 sync_dst=1 sync_id=2" "$vcu_backpressure_log"; then
        cat "$vcu_backpressure_log"
        echo "FAIL: VCU-to-MTE2 sync_wait did not complete." >&2
        exit 1
    fi

    check_xai_vcd "VCU backpressure" "$vcu_backpressure_vcd_file" 1 "$vcu_backpressure_log"
    check_vcd_signal_minimum "VCU backpressure" "$vcu_backpressure_vcd_file" \
        "vcu_queue_size" 8 "$vcu_backpressure_log"
}

run_vcu_backpressure()
{
    require_gem5
    generate_vcu_backpressure_data
    compile_xai_elf "$vcu_backpressure_src" "$vcu_backpressure_elf" \
        "$vcu_backpressure_asm_file"
    check_xai_elf "VCU backpressure" "$vcu_backpressure_elf"
    run_vcu_backpressure_sim
    check_vcu_backpressure_log
    echo "PASS: recursive VCU vadd flow grew the VCU FIFO."
}

phase=${1:-all}
if [[ $# -gt 1 ]]; then
    usage
    exit 2
fi

check_host
cd "$project_root"
mkdir -p "$build_dir" "$m5out_dir" "$multinpu_m5out_dir" "$dramsim3_output_dir" \
    "$vcu_backpressure_m5out_dir" "$gm_file_io_root" \
    "$multinpu_gm_file_io_root" "$vcu_backpressure_gm_file_io_root" \
    "$multinpu_expected_root"

case "$phase" in
    all)
        build_gem5
        run_smoke
        run_multinpu
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
    run-vcu-backpressure)
        run_vcu_backpressure
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
