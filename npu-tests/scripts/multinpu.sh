#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/../build/multinpu"
source "$script_dir/xai_common.sh"

usage()
{
    cat >&2 <<'EOF'
Usage: multinpu.sh
EOF
}

generate_multinpu_data()
{
    local multinpu_data_generator=$1
    local multinpu_file_io_root=$2
    local multinpu_expected_root=$3
    local npu_count=$4

    echo "+ python3 $multinpu_data_generator --file-io-root $multinpu_file_io_root --expected-root $multinpu_expected_root --npu-count $npu_count"
    python3 "$multinpu_data_generator" \
        --file-io-root "$multinpu_file_io_root" \
        --expected-root "$multinpu_expected_root" \
        --npu-count "$npu_count"
    local npu_id
    for ((npu_id = 0; npu_id < npu_count; ++npu_id)); do
        test -s "$multinpu_file_io_root/npu${npu_id}/GMInputFile_0_0.bin"
        test -s "$multinpu_expected_root/xai_multinpu_expected_npu${npu_id}.bin"
        rm -f "$multinpu_file_io_root/npu${npu_id}/GMOutputFile_0_0.bin"
    done
}

run_multinpu_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 45 "$log_file" "$m5out_dir" "$elf_file" \
        "$file_io_root" "$vcd_base"
}

check_multinpu_log()
{
    local log_file=$1
    local vcd_file=$2
    local file_io_root=$3
    local expected_root=$4
    local npu_count=$5

    check_xai_log_common "multi-NPU" "$log_file"

    local npu_id
    for ((npu_id = 0; npu_id < npu_count; ++npu_id)); do
        if ! grep -Eq "CPU\\[0\\]NPU\\[${npu_id}\\] : op=WriteDataToNpu .*opcode=5 subopcode=0 .*mask=0xf .*storage_region=gm" "$log_file"; then
            cat "$log_file"
            echo "FAIL: NPU${npu_id} did not execute its WriteDataToNpu command." >&2
            exit 1
        fi

        local expected_bin
        local actual_bin
        expected_bin="$expected_root/xai_multinpu_expected_npu${npu_id}.bin"
        actual_bin="$file_io_root/npu${npu_id}/GMOutputFile_0_0.bin"
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
    local multinpu_src="$project_root/npu-tests/baremetal/multinpu/xai_multinpu_smoke.cc"
    local multinpu_elf="$build_dir/xai_multinpu_smoke.elf"
    local multinpu_asm_file="$build_dir/xai_multinpu_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local multinpu_log="$gem5_output_dir/xai_multinpu_smoke.log"
    local multinpu_m5out_dir="$gem5_output_dir/multinpu_m5out"
    local multinpu_vcd_base="xai_multinpu_npu_trace"
    local multinpu_vcd_file="$multinpu_m5out_dir/${multinpu_vcd_base}.vcd"
    local multinpu_file_io_root="$build_dir/file_io_multinpu"
    local multinpu_expected_root="$build_dir/multinpu_expected"
    local multinpu_data_generator="$project_root/npu-tests/reference/multinpu/generate_multinpu_smoke_data.py"
    local npu_count
    npu_count=$(selected_npu_count)

    require_multi_npu_type "multinpu"
    require_gem5
    generate_multinpu_data "$multinpu_data_generator" \
        "$multinpu_file_io_root" "$multinpu_expected_root" "$npu_count"
    compile_xai_elf "$multinpu_src" "$multinpu_elf" "$multinpu_asm_file"
    check_xai_elf "multi-NPU" "$multinpu_elf"
    run_multinpu_sim "$multinpu_log" "$multinpu_m5out_dir" \
        "$multinpu_elf" "$multinpu_file_io_root" "$multinpu_vcd_base"
    check_multinpu_log "$multinpu_log" "$multinpu_vcd_file" \
        "$multinpu_file_io_root" "$multinpu_expected_root" "$npu_count"
    echo "PASS: single-CPU multi-NPU Xai flow completed with distinct per-NPU GM results."
}

if [[ $# -gt 0 ]]; then
    case "$1" in
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
fi

check_host
validate_npu_selection
cd "$project_root"
mkdir -p "$build_dir"
require_gem5
run_multinpu
