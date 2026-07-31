#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/xai_common.sh"

usage()
{
    cat >&2 <<'EOF'
Usage: build_gem5.sh
EOF
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

build_gem5()
{
    local jobs
    jobs=${JOBS:-$(nproc)}
    echo "+ scons build/RISCV/gem5.opt --rvv-impl=$rvv_impl USE_SYSTEMC=1 RUBY=False USE_KVM=False BUILD_GPU=False --linker=lld -j$jobs"
    (
        cd "$gem5_root"
        scons build/RISCV/gem5.opt --rvv-impl="$rvv_impl" USE_SYSTEMC=1 \
            RUBY=False USE_KVM=False BUILD_GPU=False --linker=lld -j"$jobs"
    )
    test -x "$gem5_bin"
}

check_host
cd "$project_root"
mkdir -p "$build_dir"
build_gem5
