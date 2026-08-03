#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/../build/niu"
source "$script_dir/xai_common.sh"

usage()
{
    cat >&2 <<'EOF'
Usage: niu.sh
EOF
}

generate_niu_smoke_data()
{
    local data_generator=$1
    local file_io_root=$2
    local expected_root=$3
    local byte_count=$4

    echo "+ python3 $data_generator --file-io-root $file_io_root --expected-root $expected_root --byte-count $byte_count"
    python3 "$data_generator" \
        --file-io-root "$file_io_root" \
        --expected-root "$expected_root" \
        --byte-count "$byte_count"
    test -s "$file_io_root/npu0/GMInputFile_0_0.bin"
    test -s "$expected_root/xai_niu_expected_remote_ub.bin"
    test -s "$expected_root/xai_niu_expected_remote_gm.bin"
    rm -f "$file_io_root/npu1/GMOutputFile_0_0.bin"
    rm -f "$file_io_root/npu2/GMOutputFile_0_0.bin"
}

run_niu_smoke_sim()
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

check_niu_output()
{
    local label=$1
    local expected_bin=$2
    local actual_bin=$3
    local log_file=$4

    if [[ ! -s "$actual_bin" ]]; then
        cat "$log_file"
        echo "FAIL: $label result bin was not generated: $actual_bin" >&2
        exit 1
    fi

    echo "+ cmp -s $expected_bin $actual_bin"
    if ! cmp -s "$expected_bin" "$actual_bin"; then
        cat "$log_file"
        echo "FAIL: $label result bin differs from expected NIU payload." >&2
        echo "+ cmp -l $expected_bin $actual_bin" >&2
        cmp -l "$expected_bin" "$actual_bin" >&2 || true
        exit 1
    fi
}

check_niu_smoke_log()
{
    local log_file=$1
    local vcd_file=$2
    local file_io_root=$3
    local expected_root=$4
    local npu_count=$5

    check_xai_log_common "NIU/NOC smoke" "$log_file"

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=WriteDataToNpu .*storage_region=ub" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU0 input fixture was not written directly to UB." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=niu_ub_to_remote_ub .*source_npu=0 target_npu=1 niu_bytes=256" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NIU UB-to-remote-UB command did not complete on NPU0." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=niu_ub_to_remote_gm .*source_npu=0 target_npu=2 niu_bytes=256" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NIU UB-to-remote-GM command did not complete on NPU0." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=remote_sync_set .*sync_src=8 sync_dst=3 sync_id=10 remote_peer=1" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU0 did not send the NIU-to-FileIo remote sync_set to NPU1." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[0\\] : op=remote_sync_set .*sync_src=8 sync_dst=3 sync_id=11 remote_peer=2" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU0 did not send the NIU-to-FileIo remote sync_set to NPU2." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[1\\] : op=remote_sync_wait .*sync_src=8 sync_dst=3 sync_id=10 remote_peer=0" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU1 FileIo did not wait for the remote NIU sync token from NPU0." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[2\\] : op=remote_sync_wait .*sync_src=8 sync_dst=3 sync_id=11 remote_peer=0" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU2 FileIo did not wait for the remote NIU sync token from NPU0." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[1\\] : op=LoadDataFromNpu .*storage_region=ub" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU1 did not export remote UB data." >&2
        exit 1
    fi

    if ! grep -Eq "CPU\\[0\\]NPU\\[2\\] : op=LoadDataFromNpu .*storage_region=gm" "$log_file"; then
        cat "$log_file"
        echo "FAIL: NPU2 did not export remote GM data." >&2
        exit 1
    fi

    check_niu_output "NIU remote UB" \
        "$expected_root/xai_niu_expected_remote_ub.bin" \
        "$file_io_root/npu1/GMOutputFile_0_0.bin" \
        "$log_file"
    check_niu_output "NIU remote GM" \
        "$expected_root/xai_niu_expected_remote_gm.bin" \
        "$file_io_root/npu2/GMOutputFile_0_0.bin" \
        "$log_file"

    check_xai_vcd "NIU/NOC smoke" "$vcd_file" "$npu_count" "$log_file"
    check_vcd_signal_asserted "NIU/NOC smoke" "$vcd_file" \
        "npu0.niu.packet_sent_event" "$log_file"
    check_vcd_signal_asserted "NIU/NOC smoke" "$vcd_file" \
        "npu1.niu.packet_received_event" "$log_file"
    check_vcd_signal_asserted "NIU/NOC smoke" "$vcd_file" \
        "npu2.niu.packet_received_event" "$log_file"
    check_vcd_signal_asserted "NIU/NOC smoke" "$vcd_file" \
        "cluster.noc.inject_event" "$log_file"
    check_vcd_signal_asserted "NIU/NOC smoke" "$vcd_file" \
        "cluster.noc.deliver_event" "$log_file"
}

run_niu_smoke()
{
    local niu_src="$project_root/npu-tests/baremetal/niu/xai_niu_smoke.cc"
    local niu_elf="$build_dir/xai_niu_smoke.elf"
    local niu_asm_file="$build_dir/xai_niu_smoke.asm"
    local gem5_output_dir="$build_dir/gem5_output"
    local niu_log="$gem5_output_dir/xai_niu_smoke.log"
    local niu_m5out_dir="$gem5_output_dir/niu_smoke_m5out"
    local niu_vcd_base="xai_niu_smoke_npu_trace"
    local niu_vcd_file="$niu_m5out_dir/${niu_vcd_base}.vcd"
    local file_io_root="$build_dir/file_io_niu_smoke"
    local expected_root="$build_dir/niu_expected"
    local data_generator="$project_root/npu-tests/reference/niu/generate_niu_smoke_data.py"
    local byte_count=256
    local npu_count
    npu_count=$(selected_npu_count)

    require_multi_npu_type "NIU/NOC smoke"
    require_gem5
    generate_niu_smoke_data "$data_generator" "$file_io_root" \
        "$expected_root" "$byte_count"
    compile_xai_elf "$niu_src" "$niu_elf" "$niu_asm_file"
    check_xai_elf "NIU/NOC smoke" "$niu_elf"
    run_niu_smoke_sim "$niu_log" "$niu_m5out_dir" "$niu_elf" \
        "$file_io_root" "$niu_vcd_base"
    check_niu_smoke_log "$niu_log" "$niu_vcd_file" "$file_io_root" \
        "$expected_root" "$npu_count"
    echo "PASS: NIU/NOC smoke moved NPU0 UB payload to remote UB and GM."
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
run_niu_smoke
