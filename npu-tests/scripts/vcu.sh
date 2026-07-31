#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/../build/vcu"
source "$script_dir/xai_common.sh"

usage()
{
    cat >&2 <<'EOF'
Usage: vcu.sh [all|smoke|backpressure]
EOF
}

generate_vcu_smoke_data()
{
    local data_generator=$1
    local file_io_root=$2
    local hart_id=$3
    local file_index=$4
    local expected_result_bin=$5
    local actual_result_bin=$6
    local write_data_fixture="$file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $data_generator --file-io-root $file_io_root --hart-id $hart_id --index $file_index --expected-bin $expected_result_bin"
    python3 "$data_generator" \
        --file-io-root "$file_io_root" \
        --hart-id "$hart_id" \
        --index "$file_index" \
        --expected-bin "$expected_result_bin"
    test -s "$write_data_fixture"
    test -s "$expected_result_bin"
    rm -f "$actual_result_bin"
}

run_vcu_smoke_sim()
{
    local log_file=$1
    local m5out_dir=$2
    local elf_file=$3
    local file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 30 "$log_file" "$m5out_dir" "$elf_file" \
        "$file_io_root" "$vcd_base"
}

check_vcu_smoke_log()
{
    local log_file=$1
    local vcd_file=$2
    local expected_result_bin=$3
    local actual_result_bin=$4
    local op
    check_xai_log_common "RVV NPU VCU smoke" "$log_file"

    for op in mte4_gm_to_ub mte2_ub_to_gm vload vstore vadd nsetvl \
        sync_set sync_wait WriteDataToNpu LoadDataFromNpu; do
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
            echo "FAIL: expected sync_set commands at FileIo-to-MTE4, MTE4-to-VCU, VCU-to-MTE2, and MTE2-to-FileIo boundaries." >&2
            exit 1
        fi
        if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_wait .*opcode=4 subopcode=2 .*sync_src=$src sync_dst=$dst sync_id=$sync_id" "$log_file"; then
            cat "$log_file"
            echo "FAIL: expected sync_wait commands at FileIo-to-MTE4, MTE4-to-VCU, VCU-to-MTE2, and MTE2-to-FileIo boundaries." >&2
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

    check_xai_vcd "RVV NPU VCU smoke" "$vcd_file" 1 "$log_file"
}

run_vcu_smoke()
{
    local vcu_smoke_src="$project_root/npu-tests/baremetal/vcu/xai_rvv_npu_vcu_smoke.cc"
    local vcu_smoke_elf="$build_dir/xai_rvv_npu_vcu_smoke.elf"
    local vcu_smoke_asm_file="$build_dir/xai_rvv_npu_vcu_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local vcu_smoke_log="$gem5_output_dir/xai_rvv_npu_vcu_smoke.log"
    local vcu_smoke_m5out_dir="$gem5_output_dir/rvv_npu_vcu_m5out"
    local vcu_smoke_vcd_base="xai_rvv_npu_vcu_smoke_npu_trace"
    local vcu_smoke_vcd_file="$vcu_smoke_m5out_dir/${vcu_smoke_vcd_base}.vcd"
    local file_io_root="$build_dir/file_io_rvv_npu_vcu_smoke"
    local data_generator="$project_root/npu-tests/reference/vcu/generate_vcu_smoke_data.py"
    local file_hart_id=0
    local file_index=0
    local expected_result_bin="$build_dir/xai_rvv_npu_vcu_expected.bin"
    local actual_result_bin="$file_io_root/GMOutputFile_${file_hart_id}_${file_index}.bin"

    require_single_npu_type "vcu smoke"
    require_gem5
    generate_vcu_smoke_data "$data_generator" "$file_io_root" "$file_hart_id" \
        "$file_index" "$expected_result_bin" "$actual_result_bin"
    compile_xai_elf "$vcu_smoke_src" "$vcu_smoke_elf" \
        "$vcu_smoke_asm_file"
    check_xai_elf "RVV NPU VCU smoke" "$vcu_smoke_elf"
    run_vcu_smoke_sim "$vcu_smoke_log" "$vcu_smoke_m5out_dir" \
        "$vcu_smoke_elf" "$file_io_root" \
        "$vcu_smoke_vcd_base"
    check_vcu_smoke_log "$vcu_smoke_log" \
        "$vcu_smoke_vcd_file" "$expected_result_bin" \
        "$actual_result_bin"
    echo "PASS: standard RVV encodings dispatched to NPU VCU with expected VADD result."
}

generate_vcu_backpressure_data()
{
    local data_generator=$1
    local file_io_root=$2
    local hart_id=$3
    local file_index=$4
    local recursive_add_count=$5
    local expected_result_bin=$6
    local actual_result_bin=$7
    local write_data_fixture="$file_io_root/GMInputFile_${hart_id}_${file_index}.bin"

    echo "+ python3 $data_generator --file-io-root $file_io_root --hart-id $hart_id --index $file_index --recursive-add-count $recursive_add_count --expected-bin $expected_result_bin"
    python3 "$data_generator" \
        --file-io-root "$file_io_root" \
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
    local file_io_root=$4
    local vcd_base=$5
    local vcd_file="$m5out_dir/${vcd_base}.vcd"

    rm -f "$vcd_file"
    run_xai_sim 60 "$log_file" "$m5out_dir" "$elf_file" \
        "$file_io_root" "$vcd_base"
}

check_vcu_backpressure_log()
{
    local log_file=$1
    local vcd_file=$2

    check_xai_log_common "RVV NPU backpressure" "$log_file"

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_set .* sync_src=2 sync_dst=1 sync_id=2" "$log_file"; then
        cat "$log_file"
        echo "FAIL: RVV NPU VCU-to-MTE2 sync_set did not complete." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=sync_wait .* sync_src=2 sync_dst=1 sync_id=2" "$log_file"; then
        cat "$log_file"
        echo "FAIL: RVV NPU VCU-to-MTE2 sync_wait did not complete." >&2
        exit 1
    fi

    check_xai_vcd "RVV NPU backpressure" "$vcd_file" 1 "$log_file"
    check_vcd_signal_minimum "RVV NPU backpressure" "$vcd_file" \
        "npu0.vcu.queue_size" 8 "$log_file"
}

run_vcu_backpressure()
{
    local vcu_backpressure_src="$project_root/npu-tests/baremetal/vcu/xai_rvv_npu_backpressure.cc"
    local vcu_backpressure_elf="$build_dir/xai_rvv_npu_backpressure.elf"
    local vcu_backpressure_asm_file="$build_dir/xai_rvv_npu_backpressure.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local vcu_backpressure_log="$gem5_output_dir/xai_rvv_npu_backpressure.log"
    local vcu_backpressure_m5out_dir="$gem5_output_dir/rvv_npu_backpressure_m5out"
    local vcu_backpressure_vcd_base="xai_rvv_npu_backpressure_npu_trace"
    local vcu_backpressure_vcd_file="$vcu_backpressure_m5out_dir/${vcu_backpressure_vcd_base}.vcd"
    local vcu_backpressure_file_io_root="$build_dir/file_io_rvv_npu_backpressure"
    local vcu_backpressure_data_generator="$project_root/npu-tests/reference/vcu/generate_rvv_npu_backpressure_data.py"
    local file_hart_id=0
    local vcu_backpressure_file_index=1
    local vcu_backpressure_recursive_add_count=16
    local vcu_backpressure_expected_result_bin="$build_dir/xai_rvv_npu_backpressure_expected.bin"
    local vcu_backpressure_actual_result_bin="$vcu_backpressure_file_io_root/GMOutputFile_${file_hart_id}_${vcu_backpressure_file_index}.bin"

    require_single_npu_type "vcu backpressure"
    require_gem5
    generate_vcu_backpressure_data "$vcu_backpressure_data_generator" \
        "$vcu_backpressure_file_io_root" "$file_hart_id" \
        "$vcu_backpressure_file_index" "$vcu_backpressure_recursive_add_count" \
        "$vcu_backpressure_expected_result_bin" \
        "$vcu_backpressure_actual_result_bin"
    compile_xai_elf "$vcu_backpressure_src" "$vcu_backpressure_elf" \
        "$vcu_backpressure_asm_file"
    check_xai_elf "RVV NPU backpressure" "$vcu_backpressure_elf"
    run_vcu_backpressure_sim "$vcu_backpressure_log" \
        "$vcu_backpressure_m5out_dir" "$vcu_backpressure_elf" \
        "$vcu_backpressure_file_io_root" "$vcu_backpressure_vcd_base"
    check_vcu_backpressure_log "$vcu_backpressure_log" \
        "$vcu_backpressure_vcd_file"
    echo "PASS: recursive RVV NPU vadd flow grew the VCU FIFO."
}

phase=${1:-all}
if [[ $# -gt 1 ]]; then
    usage
    exit 2
fi

case "$phase" in
    -h|--help|help)
        usage
        exit 0
        ;;
esac

check_host
validate_npu_selection
cd "$project_root"
mkdir -p "$build_dir"
require_gem5

case "$phase" in
    all)
        run_vcu_smoke
        run_vcu_backpressure
        ;;
    smoke)
        run_vcu_smoke
        ;;
    backpressure)
        run_vcu_backpressure
        ;;
    *)
        usage
        exit 2
        ;;
esac
