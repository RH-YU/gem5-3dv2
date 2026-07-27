# 构建与验证

## 环境依赖

构建不依赖固定 Ubuntu 版本，Linux 环境满足依赖即可。Ubuntu/Debian 可参考：

```bash
sudo apt update
sudo apt install -y build-essential scons python3 python3-dev python3-pip \
    pkg-config zlib1g-dev libprotobuf-dev protobuf-compiler \
    libgoogle-perftools-dev libboost-all-dev libzstd-dev curl
```

可选依赖：

```bash
sudo apt install -y libpng-dev libhdf5-dev lld
```

## 下载 RISC-V 工具链

```bash
mkdir -p riscv_bin
arch=$(uname -m)
case "$arch" in
    x86_64) pkg=linux-x64 ;;
    aarch64|arm64) pkg=linux-arm64 ;;
    *) echo "unsupported host arch: $arch" >&2; exit 1 ;;
esac
archive="xpack-riscv-none-elf-gcc-15.2.0-1-${pkg}.tar.gz"
curl -L -o "/tmp/$archive" \
    "https://sourceforge.net/projects/riscv-none-elf-gcc-xpack/files/v15.2.0-1/$archive/download"
tar -xf "/tmp/$archive" -C riscv_bin
test -x riscv_bin/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-g++
```

## 一键构建与验证

构建 `gem5.opt` 并运行 SystemC NPU MVP 用例：

```bash
npu-tests/scripts/verify_npu-mvp.sh
```

构建 `gem5.opt` 并运行 XAI bare-metal smoke 和 multi-NPU 用例：

```bash
npu-tests/scripts/verify_xai-elf.sh all
```

只构建 `gem5.opt`：

```bash
npu-tests/scripts/verify_xai-elf.sh build-gem5
```
