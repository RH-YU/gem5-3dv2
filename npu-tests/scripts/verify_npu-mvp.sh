#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux ]]; then
    echo "Linux is required for this validation script." >&2
    exit 1
fi

if [[ -r /etc/os-release ]]; then
    source /etc/os-release
    if [[ ${ID:-} != ubuntu || ${VERSION_ID:-} != 24.04 ]]; then
        echo "Warning: validated on Ubuntu 24.04; continuing on ${PRETTY_NAME:-unknown Linux}." >&2
    fi
fi

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
gem5_root="$project_root/gem5"
gem5_binary="$gem5_root/build/RISCV/gem5.opt"
systemc_config="$project_root/npu-tests/systemc/npu-mvp/sc-main/sc_main.py"
systemc_test_log="$project_root/npu-tests/build/npu-mvp/vector_add_test.log"
gem5_outdir="$project_root/npu-tests/build/npu-mvp/m5out"
build_jobs=${NPU_BUILD_JOBS:-"$(nproc)"}
linker=${NPU_LINKER:-gold}
ruby=${NPU_BUILD_RUBY:-False}
kvm=${NPU_BUILD_KVM:-False}
gpu=${NPU_BUILD_GPU:-False}

echo "+ scons build/RISCV/gem5.opt USE_SYSTEMC=1 RUBY=$ruby USE_KVM=$kvm BUILD_GPU=$gpu --linker=$linker -j$build_jobs"
(
    cd "$gem5_root"
    scons build/RISCV/gem5.opt USE_SYSTEMC=1 RUBY="$ruby" USE_KVM="$kvm" \
        BUILD_GPU="$gpu" --linker="$linker" -j"$build_jobs"
)

if [[ ! -x $gem5_binary ]]; then
    echo "Expected gem5 binary was not produced: $gem5_binary" >&2
    exit 1
fi

mkdir -p "$(dirname "$systemc_test_log")" "$gem5_outdir"
echo "+ $gem5_binary --outdir $gem5_outdir $systemc_config"
"$gem5_binary" --outdir "$gem5_outdir" "$systemc_config" 2>&1 | tee "$systemc_test_log"

if ! grep -q "PASS: NPU MVP vector add, CPU packet bridge, and physical-address checks" "$systemc_test_log"; then
    echo "Expected vector_add_test PASS marker was not found in: $systemc_test_log" >&2
    exit 1
fi

echo "PASS: gem5 SystemC build includes src/dev/npu/npu_top.cc"
