# gem5 ISA 扩展修改说明

本文说明当前工程中扩展 gem5 RISC-V ISA 时常用文件、关键语句和含义。重点覆盖 NPU 当前使用的两条路径：

- `custom-2` XAI 指令：MTE、Sync、Cube、Fixpipe、FileIo。
- RVV NPU 指令：复用标准 RVV 编码，将部分 RVV 指令改派发到 NPU VCU。

## 1. 相关文件

| 文件 | 作用 |
|---|---|
| `gem5/src/arch/riscv/isa/decoder.isa` | RISC-V 主 decoder，决定某个编码落到哪个 format 和 mnemonic |
| `gem5/src/arch/riscv/isa/formats/xai.isa` | 当前 NPU/XAI 指令的 format、模板、execute 代码 |
| `gem5/src/arch/riscv/isa/vector/base/decoder.isa` | 标准 RVV base decoder |
| `gem5/src/arch/riscv/isa/vector/simple/decoder.isa` | 标准 RVV simple decoder |
| `gem5/src/arch/riscv/isa/vector/npu/decoder.isa` | NPU RVV decoder，当前用于把 `vle32.v/vse32.v/vadd.vv/vsetvl` 派发给 NPU |
| `gem5/src/dev/npu/npu_types.hh` | NPU 内部 opcode/subopcode 定义 |
| `gem5/src/dev/npu/npu_command.*` | CPU 到 NPU direct-submit 注册与入口 |

## 2. decoder.isa 的 decode 结构

gem5 的 `.isa` 文件使用嵌套 `decode` 描述指令匹配树。例如当前 XAI 指令放在 `custom-2` 区间：

```text
0x16: decode FUNCT3 {
    0x0: decode FUNCT7 {
        format XaiMte4Op {
            0x00: mte4(GmToUb);
            0x02: mte4_gm_to_l1(GmToL1);
        }
    }
}
```

含义：

| 语句 | 含义 |
|---|---|
| `0x16: decode FUNCT3` | opcode 匹配到 `0x16` 后，继续按 `FUNCT3` 分支 |
| `0x0: decode FUNCT7` | `FUNCT3 == 0` 后，继续按 `FUNCT7` 分支 |
| `format XaiMte4Op { ... }` | 这个块里的指令都使用 `XaiMte4Op` format 生成 C++ 静态指令类 |
| `0x00: mte4(GmToUb);` | `FUNCT7 == 0x00` 时生成 mnemonic 为 `mte4` 的指令，并把参数 `GmToUb` 传给 format |

`mte4(GmToUb)` 中：

- `mte4` 是生成的指令 mnemonic，日志/反汇编里会看到它。
- `GmToUb` 是 format 参数，不是 C++ 变量；它会在 `formats/xai.isa` 里拼成 `npu_mvp::Mte4Opcode::GmToUb`。

## 3. format 的作用

format 是“同类指令的生成模板”。当前 XAI MTE4 format 示例：

```python
def format XaiMte4Op(mte4_opcode, *flags) {{
    xai_flags = flags + ('IsInteger', 'IsNonSpeculative',)
    code = '''
    const uint64_t rd_value = xc->getRegOperand(this, 0);
    const uint64_t rs1_value = xc->getRegOperand(this, 1);
    const uint64_t rs2_value = xc->getRegOperand(this, 2);
    Fault fault = dispatchXaiCommand(
            xc, machInst, npu_mvp::Opcode::Mte4, rd_value, rs1_value,
            rs2_value, %(mte4_opcode)s);
    if (fault != NoFault)
        return fault;
    ''' % { 'mte4_opcode': 'npu_mvp::Mte4Opcode::' + mte4_opcode }
    iop = InstObjParams(name, Name, 'RiscvStaticInst', code, xai_flags)
    iop.padSrcRegIdx(3)
    header_output = XaiDeclare.subst(iop)
    decoder_output = XaiConstructor.subst(iop)
    decode_block = BasicDecode.subst(iop)
    exec_output = NoWBExecute.subst(iop) + XaiDisasm.subst(iop)
}};
```

关键语句含义：

| 语句 | 含义 |
|---|---|
| `def format XaiMte4Op(mte4_opcode, *flags)` | 定义一个 decoder 可调用的 format，`mte4_opcode` 来自 `decoder.isa` 中的 `GmToUb` |
| `xai_flags = flags + (...)` | 给静态指令添加 gem5 指令属性 |
| `code = ''' ... '''` | 生成该指令 `execute()` 函数里的 C++ 代码 |
| `%(mte4_opcode)s` | Python 字符串替换占位符，最终替换成 C++ 表达式 |
| `InstObjParams(...)` | gem5 ISA parser 用它保存类名、基类、execute 代码、flags 等生成参数 |
| `iop.padSrcRegIdx(3)` | 告诉模板这里至少会访问 3 个源寄存器索引 |
| `header_output = ...` | 生成静态指令类声明 |
| `decoder_output = ...` | 生成静态指令构造函数 |
| `decode_block = BasicDecode.subst(iop)` | 生成 decode 分支返回该静态指令对象的代码 |
| `exec_output = NoWBExecute.subst(iop)` | 生成无通用写回的 execute 函数 |
| `+ XaiDisasm.subst(iop)` | 追加反汇编字符串生成函数 |

## 4. constructor 模板和寄存器读取

format 中的 `xc->getRegOperand(this, index)` 不是直接读取 x1/x2/x3，而是读取该指令对象注册过的第 `index` 个 operand。

例如 `XaiConstructor` 会注册 `rd/rs1/rs2`：

```cpp
setSrcRegIdx(_numSrcRegs++,
        (machInst.rd == 0) ? RegId() : RegId(IntRegClass, machInst.rd));
setSrcRegIdx(_numSrcRegs++,
        (machInst.rs1 == 0) ? RegId() : RegId(IntRegClass, machInst.rs1));
setSrcRegIdx(_numSrcRegs++,
        (machInst.rs2 == 0) ? RegId() : RegId(IntRegClass, machInst.rs2));
```

因此在 execute 里：

```cpp
const uint64_t rd_value = xc->getRegOperand(this, 0);
const uint64_t rs1_value = xc->getRegOperand(this, 1);
const uint64_t rs2_value = xc->getRegOperand(this, 2);
```

实际含义是：

| execute 读取 | 对应指令字段 |
|---|---|
| `getRegOperand(this, 0)` | `rd` 字段对应的整数寄存器值 |
| `getRegOperand(this, 1)` | `rs1` 字段对应的整数寄存器值 |
| `getRegOperand(this, 2)` | `rs2` 字段对应的整数寄存器值 |

这也是当前 XAI 指令把 `rd` 当作普通输入寄存器使用的原因。XAI 指令不通过 CPU GPR 写回结果，只读取字段值构造 `NpuCommand`。

如果指令只需要 `rs1`，使用 `XaiRs1Constructor`。如果不需要 CPU 寄存器值，使用 `XaiNoRegConstructor`。

## 5. execute 代码如何提交到 NPU

当前 XAI 指令 execute 的关键路径是：

```cpp
Fault fault = dispatchXaiCommand(...);
if (fault != NoFault)
    return fault;
```

`dispatchXaiCommand()` 会构造 `NpuCommand`，再调用：

```cpp
sendXaiCommandDirect(xc, command)
```

其中：

```cpp
const auto dispatch_status =
        npu_mvp::submitNpuCommandDirect(isa->npuDispatchId(), command);
if (dispatch_status == npu_mvp::DispatchStatus::Backpressured)
    return std::make_shared<ReExec>();
if (dispatch_status == npu_mvp::DispatchStatus::Invalid)
    return std::make_shared<IllegalInstFault>(...);
return NoFault;
```

含义：

| 返回值 | CPU 侧行为 |
|---|---|
| `NoFault` | 指令正常退休，CPU 继续执行 |
| `ReExec` | 当前指令不退休，后续 tick 重新执行同一条指令，用于 NPU FIFO 满时反压 CPU |
| `IllegalInstFault` | 提交目标无效或 NPU dispatch 失败，作为非法指令处理 |

## 6. buildXaiCommand 的作用

`buildXaiCommand()` 是 custom-2 XAI 指令到 NPU 内部命令的统一转换点：

```cpp
command.opcode = opcode;
command.subopcode = static_cast<uint8_t>(mte4_opcode);
command.pc = pc_state.pc();
command.raw_instruction = mach_inst.instBits;
command.rd_value = rd_value;
command.rs1_value = rs1_value;
command.rs2_value = rs2_value;
command.rd = bits(mach_inst, 11, 7);
command.rs1 = bits(mach_inst, 19, 15);
command.rs2 = bits(mach_inst, 24, 20);
command.hart_id = xaiHartId(xc);
command.npu_mask = static_cast<uint8_t>(
        xc->readMiscReg(MISCREG_REG_NPU) & 0xF);
```

关键字段：

| 字段 | 用途 |
|---|---|
| `opcode` | NPU 一级模块类型，例如 `Mte4`、`Vcu`、`Cube` |
| `subopcode` | 模块内部二级 opcode，统一保存为 `uint8_t` |
| `pc` | CPU 提交该指令时的 PC，用于日志和 trace |
| `raw_instruction` | 原始 32-bit 指令，用于日志和 VCD |
| `rd/rs1/rs2` | 指令字段编号 |
| `rd_value/rs1_value/rs2_value` | CPU GPR 快照值 |
| `hart_id` | 提交该命令的 hart |
| `npu_mask` | 多 NPU 广播掩码 |

新增 XAI 指令时，优先复用这个统一结构，不建议给 `NpuCommand` 增加每条指令专属字段。

## 7. FileIo 的特殊处理

FileIo 指令不是普通数据搬运，而是仿真辅助文件读写。`buildXaiCommand()` 对 `Opcode::FileIo` 额外设置：

```cpp
command.sim_file_path = file_prefix + std::to_string(command.hart_id) +
        "_" + std::to_string(file_index) + ".bin";
command.storage_physical_address = rs1_value;
command.file_byte_count = rd_value;
```

约定：

| 字段 | 含义 |
|---|---|
| `rd_value` | 文件读写字节数 |
| `rs1_value` | NPU 存储单元地址，后端会自动识别 GM/UB/L1/L0A/L0B/L0C |
| `rs2_value` | 文件 index，用于拼接文件名 |
| `WriteDataToNpu` | 从 `GMInputFile_<hart>_<index>.bin` 写入 NPU 存储 |
| `LoadDataFromNpu` | 从 NPU 存储读出到 `GMOutputFile_<hart>_<index>.bin` |

虽然文件名前缀仍叫 `GMInputFile/GMOutputFile`，但当前接口已经可以访问多个 NPU 存储区域。

## 8. RVV NPU decoder

RVV NPU decoder 位于：

```text
gem5/src/arch/riscv/isa/vector/npu/decoder.isa
```

示例：

```text
0x00: XaiVcuVectorLoadOp::vle32_v(Load);
```

含义：

| 语句 | 含义 |
|---|---|
| `XaiVcuVectorLoadOp` | 使用 NPU VCU load format，而不是标准 RVV load format |
| `vle32_v` | 保持标准 RVV mnemonic |
| `Load` | 传给 format，最终变成 `npu_mvp::VcuOpcode::Load` |

当前 RVV NPU format 会调用：

```cpp
dispatchXaiVcuVectorCommand(...)
```

而不是标准 RVV 的内存访问或向量寄存器执行逻辑。因此当 `--rvv-impl=npu` 生效时，这些 RVV 指令会进入 NPU VCU FIFO。

## 9. InstObjParams 和输出模板

常见输出组合：

```python
header_output = XaiDeclare.subst(iop)
decoder_output = XaiConstructor.subst(iop)
decode_block = BasicDecode.subst(iop)
exec_output = NoWBExecute.subst(iop) + XaiDisasm.subst(iop)
```

含义：

| 输出变量 | 生成内容 |
|---|---|
| `header_output` | 静态指令类声明，进入 generated header |
| `decoder_output` | 静态指令构造函数，设置 mnemonic、flags、operand |
| `decode_block` | decoder 匹配到该指令时返回对象 |
| `exec_output` | execute 函数和反汇编函数 |

如果只改 `decoder.isa` 或 `formats/*.isa`，需要重新生成并编译 RISC-V decoder 相关目标。

## 10. 新增 custom-2 指令的基本步骤

以新增一个 `Mte1` 子指令为例：

1. 在 `npu_types.hh` 的 `Mte1Opcode` 增加枚举。
2. 在 `decoder.isa` 对应 `FUNCT3/FUNCT7` 下新增编码：

```text
format XaiMte1Op {
    0x04: mte1_l1_to_xxx(L1ToXxx);
}
```

3. 如果 operand 格式不变，复用 `XaiMte1Op`。
4. 如果 operand 格式变化，在 `formats/xai.isa` 新增 format 或 constructor 模板。
5. 在 NPU 后端 engine 中实现该 subopcode 的执行逻辑。
6. 增加或更新对应模块 verify 脚本。

## 11. 新增 RVV NPU VCU 指令的基本步骤

如果新增一条和 `vadd.vv` 类似的三寄存器 RVV 指令：

1. 在 `npu_types.hh` 的 `VcuOpcode` 增加枚举。
2. 在 `vector/npu/decoder.isa` 中绑定编码：

```text
0x02: XaiVcuVectorArithOp::vsub_vv(Sub);
```

3. 在 `npu_vcu_operation.cc` 中新增 handler，并注册到 `vcu_operations` 表。
4. 更新 VCU verify 脚本检查新的 `op=` 日志。

这种情况下通常不需要修改 `formats/xai.isa`，因为 `XaiVcuVectorArithOp` 已经能抽取 `vd/vs1/vs2` 并构造 VCU command。

## 12. 构建和语法检查

ISA 修改后，不需要每次全量构建。可以做目标级增量编译：

```bash
cd gem5
scons build/RISCV/arch/riscv/generated/decoder.o \
      build/RISCV/arch/riscv/isa.o \
      USE_SYSTEMC=1 RUBY=False USE_KVM=False BUILD_GPU=False \
      --linker=lld -j"$(nproc)"
```

如果同时修改了 NPU 后端 `.cc`，把对应对象也加到目标列表，例如：

```bash
scons build/RISCV/dev/npu/npu_vcu_operation.o \
      build/RISCV/arch/riscv/generated/decoder.o \
      USE_SYSTEMC=1 RUBY=False USE_KVM=False BUILD_GPU=False \
      --linker=lld -j"$(nproc)"
```

脚本语法检查：

```bash
bash -n npu-tests/scripts/verify_vcu.sh
```

## 13. 常见注意事项

- `decoder.isa` 里的参数名是传给 format 的字符串，不是 C++ 变量。
- `getRegOperand(this, index)` 的 index 取决于 constructor 注册 operand 的顺序。
- XAI 指令当前把 `rd` 当作输入寄存器使用，不做 CPU GPR 写回。
- `NoWBExecute` 表示 execute 不走普通写回路径。
- NPU FIFO 满时应返回 `ReExec`，不要让指令提前退休。
- 新增 RVV NPU 指令时要确认 `--rvv-impl=npu` 生效，否则可能走 `base/simple` 原始 RVV decoder。
- 修改 `.isa` 文件后一定要编译 `generated/decoder.o`，否则语法错误可能不会暴露。
