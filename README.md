# 构建与验证

## 环境依赖

构建不依赖固定 Ubuntu 版本，Linux 环境满足依赖即可。Ubuntu/Debian 可参考：

```bash
sudo apt update
sudo apt install -y build-essential scons python3 python3-dev python3-pip \
    pkg-config zlib1g-dev libprotobuf-dev protobuf-compiler \
    libgoogle-perftools-dev libboost-all-dev libzstd-dev cmake curl
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
    x86_64)
        archive="riscv64-elf-ubuntu-22.04-gcc.tar.xz"
        curl -L -o "/tmp/$archive" \
            "https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2026.07.15/$archive"
        ;;
    aarch64|arm64)
        archive="xpack-riscv-none-elf-gcc-15.2.0-1-linux-arm64.tar.gz"
        curl -L -o "/tmp/$archive" \
            "https://sourceforge.net/projects/riscv-none-elf-gcc-xpack/files/v15.2.0-1/$archive/download"
        ;;
    armv7l|armv6l)
        archive="xpack-riscv-none-elf-gcc-15.2.0-1-linux-arm.tar.gz"
        curl -L -o "/tmp/$archive" \
            "https://sourceforge.net/projects/riscv-none-elf-gcc-xpack/files/v15.2.0-1/$archive/download"
        ;;
    *) echo "unsupported host arch: $arch" >&2; exit 1 ;;
esac
tar -xf "/tmp/$archive" -C riscv_bin
find riscv_bin -mindepth 2 -maxdepth 2 -type d -name bin -print
find riscv_bin -path '*/bin/*-g++' -type f -perm -111 -print -quit
```

## 构建 DRAMsim3

使用 DRAMsim3 时，需要先构建 `gem5/ext/dramsim3/DRAMsim3/libdramsim3.so`，再构建 gem5：

```bash
cd gem5/ext/dramsim3
test -d DRAMsim3 || git clone https://github.com/umd-memsys/DRAMsim3.git DRAMsim3
cd DRAMsim3
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"
```

## 一键构建与验证

先构建 `gem5.opt`：

```bash
npu-tests/scripts/build_gem5.sh
```

然后按模块运行：

```text
npu-tests/scripts/vcu.sh
npu-tests/scripts/cube.sh
NPU_TYPE=multi npu-tests/scripts/multinpu.sh
```

如果只想观察 cache 访问日志，可以通过环境变量打开：

```bash
CACHE_LOG=1 npu-tests/scripts/vcu.sh smoke
```

如果需要调整多 NPU 数量，可以设置 `MULTI_NPU_COUNT`，当前支持 2 到 4：

```bash
NPU_TYPE=multi MULTI_NPU_COUNT=4 npu-tests/scripts/multinpu.sh
```

## 减少 cache miss 干扰

如果希望减少 icache miss 带来的时延干扰，可以把 cacheline 设置为 2048，表示一次 cache fill 可以向 icache 加载 2KB 指令数据。

通过脚本运行：

```bash
CACHELINE_SIZE=2048 npu-tests/scripts/cube.sh
```
