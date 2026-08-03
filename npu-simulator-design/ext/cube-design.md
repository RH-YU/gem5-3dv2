# Cube 设计说明

本文基于当前实现，说明 Cube 单元的功能、数据流、调度方式，以及新增 Cube 指令时需要修改的文件。

## 1. Cube 的定位

Cube 是 NPU 内的计算单元，负责矩阵乘计算。当前实现支持两条矩阵乘指令：

```text
L0A[8x16 fp32] * L0B[16x16 fp32] -> L0C[8x16 fp32]
L0A[8x16 fp16] * L0B[16x16 fp16] -> L0C[8x16 fp32]
```

Cube 本身只做计算，不负责把结果从 L0C 搬出去。结果搬运由 Fixpipe 完成。

当前完整数据流是：

```text
GM -> MTE4 -> L1 -> MTE1 -> L0A/L0B -> Cube -> L0C -> Fixpipe -> L1/UB
```

## 2. 当前实现结构

Cube 相关实现集中在以下位置：

| 文件 | 作用 |
|---|---|
| `gem5/src/dev/npu/npu_types.hh` | 定义 `CubeOpcode`、`Engine::Cube`、`SyncEndpoint::Cube` 等类型。 |
| `gem5/src/dev/npu/npu_cube.hh` | 定义 Cube trace 状态和 Cube 运行状态。 |
| `gem5/src/dev/npu/npu_cube.cc` | 实现 `execute_cube()` 和 `cube_thread()`。 |
| `gem5/src/dev/npu/npu_scheduler.cc` | 把 `Opcode::Cube` 路由到 Cube FIFO，并打印日志名字。 |
| `gem5/src/dev/npu/npu_top.hh` / `npu_top.cc` | 持有 Cube state，创建 Cube thread，注册 trace。 |
| `gem5/src/arch/riscv/isa/decoder.isa` | 定义 Cube 的 XAI 指令解码入口。 |
| `gem5/src/arch/riscv/isa/formats/xai.isa` | 构造 `NpuCommand`，把寄存器值和 `CubeOpcode` 填进去。 |
| `npu-tests/baremetal/cube/xai_cube_smoke.cc` | Cube smoke 测试程序。 |
| `npu-tests/scripts/cube.sh` | 编译、运行、校验 Cube smoke。 |

## 3. Cube 指令的当前语义

当前 Cube 指令的编解码和执行方式如下：

- `opcode = Opcode::Cube`
- `subopcode = CubeOpcode::MmaFp32_8x16x16` 或 `CubeOpcode::MmaFp16Fp32_8x16x16`
- `rd_value` 作为 L0C 目的地址
- `rs1_value` 作为 L0A 源地址
- `rs2_value` 作为 L0B 源地址

当前 fp32 bare-metal 中使用的指令编码是：

```text
.insn r 0x5b, 0x6, 0x00, rd, rs1, rs2
```

其中：

- `0x5b` 是 RISC-V `custom-2` opcode。
- `0x6` 是当前分给 Cube 的 `FUNCT3`。
- `0x00` 是当前 `cube_mma_fp32` 的 `FUNCT7`。

fp16 输入、fp32 输出使用相同的寄存器约定，只是 `FUNCT7=0x01`：

```text
.insn r 0x5b, 0x6, 0x01, rd, rs1, rs2
```

在 `execute_cube()` 中：

1. 先根据子 opcode 选择输入元素宽度。
2. 用 `decode()` 将三个地址分别解析到 `L0A`、`L0B`、`L0C`。
3. 从 `L0A` 和 `L0B` 读出 fp32 或 fp16 数据，并统一转换成 fp32 参与计算。
4. 执行 8x16 乘 16x16 的矩阵乘。
5. 将结果写回 `L0C`。

Cube 的固定尺寸在代码里是：

```text
M = 8, K = 16, N = 16
```

对应的字节数为：

- A：`8 * 16 * 4`
- B：`16 * 16 * 4`
- C：`8 * 16 * 4`

fp16 输入 case 的对应字节数为：

- A：`8 * 16 * 2`
- B：`16 * 16 * 2`
- C：`8 * 16 * 4`

## 4. 调度和执行

Cube 命令进入 NPU 后的路径是：

```text
decoder.isa
  -> buildXaiCommand()
  -> NpuTop::submit()
  -> Scheduler ingress FIFO
  -> route_engine(Opcode::Cube)
  -> Cube FIFO
  -> cube_thread()
  -> execute_cube()
```

`cube_thread()` 的行为和其他引擎一致：

- 从队列取出命令
- trace `start_event`
- 按 `cube_fma_per_ns` 和 `cube_setup_delay` 等待建模延迟
- 执行计算
- trace `done_event`
- 调用 `complete(command, Engine::Cube)`

也就是说，Cube 现在是**异步执行**的：CPU 只要成功提交命令就继续跑，后续依赖由软件通过 `sync_set` / `sync_wait` 自己表达。

## 5. 相关 trace

Cube 的 VCD trace 主要包括：

| 信号 | 含义 |
|---|---|
| `start_event` | Cube 开始处理一个命令。 |
| `done_event` | Cube 完成一个命令。 |
| `busy` | Cube 当前是否忙。 |
| `queue_size` | Cube FIFO 当前深度。 |
| `instruction` | 当前执行的原始指令编码。 |

## 6. Cube smoke 测试

当前 `npu-tests/baremetal/cube/xai_cube_smoke.cc` 和
`npu-tests/baremetal/cube/xai_cube_fp16_smoke.cc` 覆盖了完整链路：

```text
WriteDataToNpu
-> MTE4
-> MTE1 L1->L0A/L0B
-> Cube
-> Fixpipe
-> MTE1
-> MTE2
-> LoadDataFromNpu
```

`npu-tests/scripts/cube.sh` 会：

1. 生成输入和期望输出。
2. 编译 bare-metal ELF。
3. 运行 gem5。
4. 检查日志和 VCD。
5. 比较输出文件与期望结果。

默认执行：

```bash
npu-tests/scripts/cube.sh
```

也可以单独执行：

```bash
npu-tests/scripts/cube.sh fp32
npu-tests/scripts/cube.sh fp16
```

## 7. 如何新增一条 Cube 指令

### 7.1 如果只是新增一种 Cube 行为，但地址/操作数形式不变

这种情况最简单，通常只需要改三处：

1. 在 `gem5/src/dev/npu/npu_types.hh` 里给 `CubeOpcode` 新增枚举值。
2. 在 `gem5/src/arch/riscv/isa/decoder.isa` 的 Cube decode 块中新增一条 `FUNCT7` 映射，并复用现有 `XaiCubeOp`。
3. 在 `gem5/src/dev/npu/npu_cube.cc` 的 `execute_cube()` 里按 `CubeOpcode` 分支实现新行为。

如果只是执行逻辑不同，而寄存器约定仍然是：

- `rd_value` = 目标地址
- `rs1_value` = 左矩阵地址
- `rs2_value` = 右矩阵地址

那么通常不需要改 `xai.isa` 的 format，只需要加新的 decode 项和新的 `CubeOpcode`。

示例：

```text
0x6: decode FUNCT7 {
    format XaiCubeOp {
        0x00: cube_mma_fp32(MmaFp32_8x16x16);
        0x01: cube_mma_fp16_fp32(MmaFp16Fp32_8x16x16);
        0x02: cube_new_op(NewOp);
    }
}
```

### 7.2 如果新增 Cube 指令还需要不同的编码形式

如果新指令的寄存器个数、立即数编码或语义和当前 `cube_mma_fp32` 不一样，还需要额外修改：

1. `gem5/src/arch/riscv/isa/decoder.isa`
   - 为新指令增加新的 FUNCT7 / 子编码入口。
2. `gem5/src/arch/riscv/isa/formats/xai.isa`
   - 新增对应的 `XaiCubeOp` 或新的 format。
3. `npu-tests/baremetal/cube/`
   - 新增或调整测试程序，保证新指令能被触发。

### 7.3 推荐修改顺序

建议按下面顺序做：

1. 先在 `CubeOpcode` 里加枚举值。
2. 再在 `decoder.isa` 里把新指令绑到这个 opcode。
3. 然后在 `npu_cube.cc` 里补执行分支。
4. 最后补 `npu-tests/baremetal/cube/` 和 `npu-tests/scripts/cube.sh` 的验证。

这样可以先保证 decoder 和 scheduler 的路径是通的，再补功能和测试。

## 8. 代码层面的注意点

- `Opcode::Cube` 只做一级分类，真正的差异放在 `CubeOpcode`。
- Scheduler 不需要为每个 Cube 子指令新增独立队列，仍然统一进入 `cube.queue`。
- 如果新 Cube 指令会访问不同的存储区域，要同步检查 `NpuTop::decode()` 和 `Region` 相关逻辑。
- 如果新 Cube 指令需要不同的 trace 命名，记得同步改 `npu_scheduler.cc` 的 `opcode_name()`。

## 9. 相关文件

- `gem5/src/dev/npu/npu_cube.hh`
- `gem5/src/dev/npu/npu_cube.cc`
- `gem5/src/dev/npu/npu_types.hh`
- `gem5/src/dev/npu/npu_scheduler.cc`
- `gem5/src/dev/npu/npu_top.hh`
- `gem5/src/arch/riscv/isa/decoder.isa`
- `gem5/src/arch/riscv/isa/formats/xai.isa`
- `npu-tests/baremetal/cube/xai_cube_smoke.cc`
- `npu-tests/baremetal/cube/xai_cube_fp16_smoke.cc`
- `npu-tests/scripts/cube.sh`
