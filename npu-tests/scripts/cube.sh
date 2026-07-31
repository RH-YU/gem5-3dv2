#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/../build/cube"
source "$script_dir/xai_common.sh"

usage()
{
    cat >&2 <<'EOF'
Usage: cube.sh
EOF
}

generate_cube_smoke_data()
{
    local cube_smoke_data_generator=$1
    local cube_smoke_file_io_root=$2
    local hart_id=$3
    local file_index=$4
    local cube_smoke_expected_result_bin=$5
    local cube_smoke_actual_result_bin=$6
    local cube_smoke_write_data_fixture="$cube_smoke_file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $cube_smoke_data_generator --file-io-root $cube_smoke_file_io_root --hart-id $hart_id --index $file_index --expected-bin $cube_smoke_expected_result_bin"
    python3 "$cube_smoke_data_generator" \
        --file-io-root "$cube_smoke_file_io_root" \
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
    local file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 60 "$log_file" "$m5out_dir" "$elf_file" \
        "$file_io_root" "$vcd_base"
}

check_cube_smoke_log()
{
    local log_file=$1
    local vcd_file=$2
    local expected_result_bin=$3
    local actual_result_bin=$4
    local op
    check_xai_log_common "cube smoke" "$log_file"

    for op in WriteDataToNpu mte4_gm_to_l1 mte1_l1_to_l0a \
        mte1_l1_to_l0b cube_mma_fp32 fixpipe_l0c_to_l1 \
        mte1_l1_to_ub mte2_ub_to_gm LoadDataFromNpu sync_set sync_wait; do
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
    local cube_smoke_src="$project_root/npu-tests/baremetal/cube/xai_cube_smoke.cc"
    local cube_smoke_elf="$build_dir/xai_cube_smoke.elf"
    local cube_smoke_asm_file="$build_dir/xai_cube_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local cube_smoke_log="$gem5_output_dir/xai_cube_smoke.log"
    local cube_smoke_m5out_dir="$gem5_output_dir/cube_smoke_m5out"
    local cube_smoke_vcd_base="xai_cube_smoke_npu_trace"
    local cube_smoke_vcd_file="$cube_smoke_m5out_dir/${cube_smoke_vcd_base}.vcd"
    local cube_smoke_file_io_root="$build_dir/file_io_cube_smoke"
    local cube_smoke_data_generator="$project_root/npu-tests/reference/cube/generate_cube_smoke_data.py"
    local file_hart_id=0
    local file_index=0
    local cube_smoke_expected_result_bin="$build_dir/xai_cube_smoke_expected.bin"
    local cube_smoke_actual_result_bin="$cube_smoke_file_io_root/GMOutputFile_${file_hart_id}_${file_index}.bin"

    require_single_npu_type "cube smoke"
    require_gem5
    generate_cube_smoke_data "$cube_smoke_data_generator" \
        "$cube_smoke_file_io_root" "$file_hart_id" "$file_index" \
        "$cube_smoke_expected_result_bin" "$cube_smoke_actual_result_bin"
    compile_xai_elf "$cube_smoke_src" "$cube_smoke_elf" \
        "$cube_smoke_asm_file"
    check_xai_elf "cube smoke" "$cube_smoke_elf"
    run_cube_smoke_sim "$cube_smoke_log" "$cube_smoke_m5out_dir" \
        "$cube_smoke_elf" "$cube_smoke_file_io_root" \
        "$cube_smoke_vcd_base"
    check_cube_smoke_log "$cube_smoke_log" "$cube_smoke_vcd_file" \
        "$cube_smoke_expected_result_bin" "$cube_smoke_actual_result_bin"
    echo "PASS: cube smoke completed GM/L1/L0/Cube/Fixpipe/UB/GM flow."
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
run_cube_smoke
