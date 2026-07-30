# NPU XiangShan Bare-Metal Runner

This directory contains the NPU-oriented bare-metal XiangShan runner.
The original gem5/XiangShan config files are not modified.

## Run An ELF

Build the RISC-V target first:

```sh
scons build/RISCV/gem5.opt --gold-linker -j4
```

Run a bare-metal ELF:

```sh
./build/RISCV/gem5.opt \
  configs/example/npu/baremetal_xiangshan.py \
  --baremetal-bin /path/to/program.elf \
  --mem-type SimpleMemory \
  --mem-size 256MB \
  --disable-difftest \
  -I 1000000
```

For vector experiments, add:

```sh
--enable-riscv-vector
```

`--generic-rv-cpt` is not needed. Difftest is forced off by this runner.

## ELF Requirements

Use a freestanding RISC-V ELF, not a Linux userspace program. The XiangShan
bare-metal memory range starts at `0x80000000`.

Recommended linker placement:

```ld
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;
  .text : { *(.text.init) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
```

For a flat binary instead of ELF:

```sh
./build/RISCV/gem5.opt \
  configs/example/npu/baremetal_xiangshan.py \
  --baremetal-bin /path/to/program.bin \
  --raw-binary \
  --reset-vect 0x80000000
```

## Exit Behavior

If the program has no m5 exit instruction, use `-I <insts>` or `--maxtime` to
stop the simulation. For custom vector ISA bring-up, starting with `-I 100000`
is usually enough to verify decode/execute progress.

## Optional NPU Attachment

Build with in-tree SystemC enabled:

```sh
scons build/RISCV/gem5.opt USE_SYSTEMC=1 RUBY=False USE_KVM=False BUILD_GPU=False --linker=gold -j$(nproc)
```

Run a bare-metal ELF with direct Xai instruction dispatch to the NPU:

```sh
./build/RISCV/gem5.opt \
  configs/example/npu/baremetal_xiangshan.py \
  --baremetal-bin /path/to/program.elf \
  --enable-npu \
  --npu-dispatch-id 1
```

The dispatch id binds Xai instructions executed by the CPU to the registered
SystemC NPU cluster. NPU GM and UB remain private SystemC storage and are not
added to the CPU memory map.
