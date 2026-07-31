# gem5 + SystemC NPU 当前实现总览

本文档按当前代码实现描述 NPU MVP 的整体逻辑。实现入口主要位于 `gem5/src/arch/riscv/isa/`、`gem5/src/dev/npu/` 和 `npu-tests/`。

## 1. 总体目标

当前仿真器在 gem5 RV64 CPU 与 SystemC NPU 之间建立两类提交路径：MTE、Sync、Cube、Fixpipe、FileIo 仍通过 RISC-V `custom-2` XAI 编码提交；VCU 使用标准 RVV 指令编码，在 `--rvv-impl=npu` 时由 RVV NPU decoder 构造同一类 `NpuCommand`。NPU 在 SystemC 侧维护 GM、UB、L1、L0A/L0B/L0C、VCU context、各模块 FIFO 和同步 token。

当前支持的基础 VCU 数据流是：

```text
input file -> GM -> MTE4 -> UB -> RVV vle32/vadd/vse32 -> UB -> MTE2 -> GM -> output file
```

当前正在扩展的 AICore 数据流是：

```text
GM -> MTE4 -> L1 -> MTE1 -> L0A/L0B -> Cube -> L0C -> Fixpipe -> L1/UB
```

其中 FileIo 是仿真辅助能力，用于在 bin 文件和 NPU 存储单元之间导入/导出数据，不表示真实硬件文件系统。

## 2. 框架图

![gem5 + SystemC NPU architecture](images/gem5-systemc-npu-architecture.svg)

## 3. 代码模块分层

| 层级 | 主要文件 | 当前职责 |
|---|---|---|
| XAI ISA decode | `arch/riscv/isa/decoder.isa`、`formats/xai.isa` | 解码 custom-2 MTE/Sync/Cube/Fixpipe/FileIo 指令，读取 GPR 快照，构造 `NpuCommand`，通过 direct-submit 提交 |
| RVV NPU decode | `arch/riscv/isa/vector/npu/decoder.isa`、`formats/xai.isa` | 在 `--rvv-impl=npu` 下把 `vsetvli/vsetvl/vle32.v/vse32.v/vadd.vv` 映射为 VCU `NpuCommand` |
| gem5 NPU cluster | `dev/npu/npu_command.*`、`npu_cluster.*` | 注册 CPU 可见 NPU command aperture，聚合一个或多个 NPU，处理广播、背压和 CPU sync |
| SystemC NPU top | `dev/npu/npu_top.*` | 持有 GM、UB、L1、L0A/L0B/L0C、scheduler、MTE、VCU、Cube、Fixpipe、FileIo、sync 状态 |
| 调度器 | `dev/npu/npu_scheduler.*` | 接收 ingress FIFO，更新 VCU context，按 opcode 路由到目标 engine FIFO |
| 执行引擎 | `npu_mte*.cc`、`npu_vcu.cc`、`npu_cube.cc`、`npu_fixpipe.cc`、`npu_file_io.cc`、`npu_sync.cc` | 各模块独立 SystemC thread，按 FIFO 顺序执行命令 |
| VCU 操作表 | `npu_vcu_operation.*` | 将 VCU 内部子 opcode 映射到具体 handler，例如日志中的 `vload`、`vstore`、`vadd` |
| 测试入口 | `npu-tests/scripts/verify_xai-elf.sh` | 编译 bare-metal ELF，运行 gem5，比较 GM 输出 |

## 4. 指令分类

当前 `Opcode` 是模块级粗分类，不把每条具体指令都摊平成顶层 opcode：

```text
Mte4, Mte1, Mte2, Vcu, Cube, Fixpipe, Sync, FileIo
```

模块内部再用子 opcode 表示具体指令：

```text
Mte4Opcode: GmToUb, GmToL1
Mte1Opcode: L1ToGm, L1ToUb, L1ToL0A, L1ToL0B
Mte2Opcode: UbToGm, UbToL1
VcuOpcode: Nsetvl, Load, Store, Add
CubeOpcode: MmaFp32_8x16x16
FixpipeOpcode: L0CToL1, L0CToUb
SyncOpcode: Set, Wait
FileIoOpcode: WriteDataToNpu, LoadDataFromNpu
```

标准 RVV `vsetvli/vsetvl` 在 NPU decoder 下会映射为内部 `Opcode::Vcu + VcuOpcode::Nsetvl`。它进入 VCU FIFO，执行时只更新当前 hart 的 VCU context，包括 `nvl` 和 `eew_bytes`，不写回 CPU GPR。`vle32.v/vse32.v/vadd.vv` 分别映射为内部 `Load/Store/Add`。

### 4.1 MTE 数据通路

MTE 指令按数据源和目的地分工：

| 指令类 | 子 opcode | 数据通路 |
|---|---|---|
| MTE4 | `GmToUb` | `GM -> UB` |
| MTE4 | `GmToL1` | `GM -> L1` |
| MTE1 | `L1ToGm` | `L1 -> GM` |
| MTE1 | `L1ToUb` | `L1 -> UB` |
| MTE1 | `L1ToL0A` | `L1 -> L0A` |
| MTE1 | `L1ToL0B` | `L1 -> L0B` |
| MTE2 | `UbToGm` | `UB -> GM` |
| MTE2 | `UbToL1` | `UB -> L1` |

### 4.2 Cube 与 Fixpipe 指令

Cube 第一版只定义一个 fp32 矩阵乘指令：

```text
cube_mma_fp32: L0A[8x16 fp32] * L0B[16x16 fp32] -> L0C[8x16 fp32]
```

Fixpipe 负责将 Cube 输出搬出 L0C：

| 指令类 | 子 opcode | 数据通路 |
|---|---|---|
| Fixpipe | `L0CToL1` | `L0C -> L1` |
| Fixpipe | `L0CToUb` | `L0C -> UB` |

### 4.3 同步端点

同步指令仍使用 `sync_set(src, dst, id)` 和 `sync_wait(src, dst, id)`。当前端点定义为：

```text
Mte4, Mte2, Vcu, FileIo, Cpu, Mte1, Cube, Fixpipe
```

新增模块均拥有自己的 FIFO 和完成状态，因此可以独立作为 sync 的 source 或 destination。

## 5. CPU 到 NPU 的提交路径

1. custom-2 XAI 指令由 `decoder.isa` 选择对应 XAI format。
2. 标准 RVV VCU 指令由 `vector/npu/decoder.isa` 选择 `XaiVcuVector*` format。
3. `formats/xai.isa` 读取需要的 GPR 快照，填充 `NpuCommand`。
4. `sendXaiCommandDirect()` 调用 `submitNpuCommandDirect(command)`。
5. `NpuCluster::submitNpuCommand()` 先检查所有目标 NPU 是否可接收，再调用对应 `NpuTop::submit()`。
6. `NpuTop::submit()` 将命令放入 scheduler ingress FIFO，并通知 `dispatch_event`。

普通 XAI 指令和 RVV NPU VCU 指令都是异步提交：只要 NPU 接收命令，CPU 就继续执行，不等待 NPU 模块完成。需要等待 NPU 完成时，软件显式使用 `sync_set/sync_wait`，其中 `Cpu` 也可以作为同步端点。

## 6. 背压与 CPU 阻塞

NPU 侧有两级容量判断：

1. scheduler ingress FIFO 是否有空间。
2. 目标 engine FIFO 是否有空间。

`NpuTop::can_accept()` 会根据命令路由检查目标队列。RVV `vsetvli/vsetvl` 映射后的内部 `Nsetvl` 归入 VCU engine，因此同样受 VCU FIFO 容量影响。

当任一目标 NPU 无法接收命令时，`NpuCluster` 返回 `DispatchStatus::Backpressured`。XAI 指令侧收到该状态后返回 gem5 `ReExec` fault，CPU 保持 PC 不前进，在后续 tick 重新执行同一条 XAI 指令。这样可以用最小改动实现“队列满时阻塞 CPU 并重试”。

## 7. Scheduler 与执行顺序

`NpuTop::dispatch_ingress()` 按 ingress FIFO 顺序尝试派发命令：

```text
Nsetvl     -> VCU FIFO，执行时更新 VCU context
Mte4       -> MTE4 FIFO
Mte1       -> MTE1 FIFO
Mte2       -> MTE2 FIFO
Vcu op     -> VCU FIFO
Cube       -> Cube FIFO
Fixpipe    -> Fixpipe FIFO
Sync set   -> source engine FIFO
Sync wait  -> destination engine FIFO
FileIo     -> file I/O FIFO
```

进入执行 FIFO 的命令会冻结当时的 VCU context。后续 `vsetvli/vsetvl` 不会改变已经排队的 VCU 命令。

同一 engine 内部严格 FIFO 顺序执行，不同 engine 之间可并行推进。跨 engine 的数据依赖不做自动分析，需要软件使用 `sync_set` 和 `sync_wait` 建立 token 依赖。

## 8. 存储模型

当前每个 NPU 独自拥有如下存储域：

| 存储 | 实现 | 用途 |
|---|---|---|
| GM | `SparseMemory` | NPU 全局内存，默认按页稀疏分配 |
| UB | `FlatMemory` | VCU 使用的本地统一缓冲 |
| L1 | `FlatMemory` | AICore 私有 L1，默认 512KB |
| L0A | `FlatMemory` | Cube A 矩阵输入，默认 64KB |
| L0B | `FlatMemory` | Cube B 矩阵输入，默认 64KB |
| L0C | `FlatMemory` | Cube 计算输出，默认 64KB |

MTE4 负责 `GM -> UB/L1`，MTE1 负责 `L1 -> GM/UB/L0A/L0B`，MTE2 负责 `UB -> GM/L1`，Fixpipe 负责 `L0C -> L1/UB`。VCU 只通过 UB load/store 访问 UB，不直接访问 GM/L1/L0。Cube 只读取 L0A/L0B 并写 L0C。CPU 不通过普通 load/store 访问这些 NPU 私有存储。

## 9. VCU 扩展方式

VCU 后端指令扩展集中在 `npu_vcu_operation.*`，外部编码优先放在 RVV NPU decoder 中：

1. 在 `VcuOpcode` 中新增子 opcode。
2. 在 `arch/riscv/isa/vector/npu/decoder.isa` 中把目标 RVV 指令绑定到 `XaiVcuVector*` format。
3. 在 `npu_vcu_operation.cc` 中新增 execute handler。
4. 将 `{opcode, name, work_unit, work_rate, handler}` 加入 VCU operation descriptor 表。

这类扩展不需要修改 scheduler 的 opcode 分发逻辑，因为所有 VCU 指令都统一归入 `Opcode::Vcu`。

## 10. 当前验证流程

现有验证脚本为：

```bash
npu-tests/scripts/verify_xai-elf.sh run-rvv-npu-vcu-smoke
npu-tests/scripts/verify_xai-elf.sh run-rvv-npu-backpressure
npu-tests/scripts/verify_xai-elf.sh run-multinpu
npu-tests/scripts/verify_xai-elf.sh run-cube-smoke
```

`run-rvv-npu-vcu-smoke` 验证标准 RVV 编码进入 NPU VCU 后端后的 VADD 数据流，`run-rvv-npu-backpressure` 验证 RVV NPU VCU FIFO 背压观察，`run-multinpu` 验证单 CPU 向多个 NPU 广播命令并产生各自 GM 输出，`run-cube-smoke` 验证 GM/L1/L0/Cube/Fixpipe/UB/GM 数据通路。
