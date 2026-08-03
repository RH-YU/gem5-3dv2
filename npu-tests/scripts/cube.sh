#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/../build/cube"
source "$script_dir/xai_common.sh"

usage()
{
    cat >&2 <<'EOF'
Usage: cube.sh [all|fp32|fp16|smoke]
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
    local input_dtype=${7:-fp32}
    local cube_smoke_write_data_fixture="$cube_smoke_file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $cube_smoke_data_generator --file-io-root $cube_smoke_file_io_root --hart-id $hart_id --index $file_index --expected-bin $cube_smoke_expected_result_bin --input-dtype $input_dtype"
    python3 "$cube_smoke_data_generator" \
        --file-io-root "$cube_smoke_file_io_root" \
        --hart-id "$hart_id" \
        --index "$file_index" \
        --expected-bin "$cube_smoke_expected_result_bin" \
        --input-dtype "$input_dtype"
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
    local label=$1
    local log_file=$2
    local vcd_file=$3
    local expected_result_bin=$4
    local actual_result_bin=$5
    local cube_op=$6
    local input_dtype=$7

    check_xai_log_common "$label" "$log_file"

    local op
    for op in WriteDataToNpu mte4_gm_to_l1 mte1_l1_to_l0a \
        mte1_l1_to_l0b "$cube_op" fixpipe_l0c_to_l1 \
        mte1_l1_to_ub mte2_ub_to_gm LoadDataFromNpu sync_set sync_wait; do
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=$op " "$log_file"; then
            cat "$log_file"
            echo "FAIL: $label operation $op did not reach NPU target." >&2
            exit 1
        fi
    done

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=WriteDataToNpu .*storage_region=gm" "$log_file"; then
        cat "$log_file"
        echo "FAIL: $label input fixture was not written to GM." >&2
        exit 1
    fi

    if [[ "$input_dtype" == fp16 ]]; then
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=$cube_op .*subopcode=1" "$log_file"; then
            cat "$log_file"
            echo "FAIL: $label did not dispatch the fp16 cube subopcode." >&2
            exit 1
        fi
    fi

    if [[ ! -s "$actual_result_bin" ]]; then
        cat "$log_file"
        echo "FAIL: $label result bin was not generated: $actual_result_bin" >&2
        exit 1
    fi

    echo "+ cmp -s $expected_result_bin $actual_result_bin"
    if ! cmp -s "$expected_result_bin" "$actual_result_bin"; then
        cat "$log_file"
        echo "FAIL: $label result bin differs from expected matmul output." >&2
        echo "+ cmp -l $expected_result_bin $actual_result_bin" >&2
        cmp -l "$expected_result_bin" "$actual_result_bin" >&2 || true
        exit 1
    fi

    check_xai_vcd "$label" "$vcd_file" 1 "$log_file"
}

run_cube_fp32_smoke()
{
    local cube_smoke_src="$project_root/npu-tests/baremetal/cube/xai_cube_smoke.cc"
    local cube_smoke_elf="$build_dir/xai_cube_smoke.elf"
    local cube_smoke_asm_file="$build_dir/xai_cube_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local cube_smoke_log="$gem5_output_dir/xai_cube_smoke.log"
    local cube_smoke_m5out_dir="$gem5_output_dir/cube_smoke_m5out"
    local cube_smoke_vcd_base="xai_cube_smoke_npu_trace"
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
        "$cube_smoke_expected_result_bin" "$cube_smoke_actual_result_bin" \
        fp32
    compile_xai_elf "$cube_smoke_src" "$cube_smoke_elf" \
        "$cube_smoke_asm_file"
    check_xai_elf "cube smoke" "$cube_smoke_elf"
    run_cube_smoke_sim "$cube_smoke_log" "$cube_smoke_m5out_dir" \
        "$cube_smoke_elf" "$cube_smoke_file_io_root" \
        "$cube_smoke_vcd_base"
    check_cube_smoke_log "cube smoke" "$cube_smoke_log" \
        "$cube_smoke_m5out_dir/${cube_smoke_vcd_base}.vcd" \
        "$cube_smoke_expected_result_bin" \
        "$cube_smoke_actual_result_bin" \
        cube_mma_fp32 fp32
    echo "PASS: cube smoke completed GM/L1/L0/Cube/Fixpipe/UB/GM flow."
}

run_cube_fp16_smoke()
{
    local cube_smoke_src="$project_root/npu-tests/baremetal/cube/xai_cube_fp16_smoke.cc"
    local cube_smoke_elf="$build_dir/xai_cube_fp16_smoke.elf"
    local cube_smoke_asm_file="$build_dir/xai_cube_fp16_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local cube_smoke_log="$gem5_output_dir/xai_cube_fp16_smoke.log"
    local cube_smoke_m5out_dir="$gem5_output_dir/cube_fp16_smoke_m5out"
    local cube_smoke_vcd_base="xai_cube_fp16_smoke_npu_trace"
    local cube_smoke_file_io_root="$build_dir/file_io_cube_fp16_smoke"
    local cube_smoke_data_generator="$project_root/npu-tests/reference/cube/generate_cube_smoke_data.py"
    local file_hart_id=0
    local file_index=0
    local cube_smoke_expected_result_bin="$build_dir/xai_cube_fp16_smoke_expected.bin"
    local cube_smoke_actual_result_bin="$cube_smoke_file_io_root/GMOutputFile_${file_hart_id}_${file_index}.bin"

    require_single_npu_type "cube fp16 smoke"
    require_gem5
    generate_cube_smoke_data "$cube_smoke_data_generator" \
        "$cube_smoke_file_io_root" "$file_hart_id" "$file_index" \
        "$cube_smoke_expected_result_bin" "$cube_smoke_actual_result_bin" \
        fp16
    compile_xai_elf "$cube_smoke_src" "$cube_smoke_elf" \
        "$cube_smoke_asm_file"
    check_xai_elf "cube fp16 smoke" "$cube_smoke_elf"
    run_cube_smoke_sim "$cube_smoke_log" "$cube_smoke_m5out_dir" \
        "$cube_smoke_elf" "$cube_smoke_file_io_root" \
        "$cube_smoke_vcd_base"
    check_cube_smoke_log "cube fp16 smoke" "$cube_smoke_log" \
        "$cube_smoke_m5out_dir/${cube_smoke_vcd_base}.vcd" \
        "$cube_smoke_expected_result_bin" \
        "$cube_smoke_actual_result_bin" \
        cube_mma_fp16_fp32 fp16
    echo "PASS: cube fp16 smoke completed GM/L1/L0/Cube/Fixpipe/UB/GM flow."
}

run_cube_smoke()
{
    case "${1:-all}" in
        all)
            run_cube_fp32_smoke
            run_cube_fp16_smoke
            ;;
        smoke|fp32)
            run_cube_fp32_smoke
            ;;
        fp16)
            run_cube_fp16_smoke
            ;;
        *)
            usage
            exit 2
            ;;
    esac
}

if [[ $# -gt 1 ]]; then
    usage
    exit 2
fi

if [[ $# -eq 1 ]]; then
    case "$1" in
        -h|--help|help)
            usage
            exit 0
            ;;
    esac
fi

check_host
validate_npu_selection
cd "$project_root"
mkdir -p "$build_dir"
require_gem5
run_cube_smoke "${1:-all}"
