# 新增 RVV NPU VCU 指令说明

本文说明在当前实现中，如何新增一条由 NPU 执行的 RVV 向量指令。

当前设计里，VCU 不再使用旧的 custom-2 专用编码；外部指令复用标准 RVV，通过 `--rvv-impl=npu` 选择 `arch/riscv/isa/vector/npu/decoder.isa`。MTE、Sync、Cube、Fixpipe、FileIo 仍然走 XAI 的 custom-2 通路。

## 当前执行路径

```text
arch/riscv/isa/vector/npu/decoder.isa
  -> XaiVcuVector* format in formats/xai.isa
  -> dispatchXaiVcuVectorCommand()
  -> buildXaiVcuVectorCommand()
  -> NpuCommand{opcode=Vcu, subopcode, rd/rs1/rs2, rd_value/rs1_value/rs2_value, ...}
  -> NpuTop::submit()
  -> scheduler.ingress_queue
  -> NpuTop::dispatch_one()
  -> make_vcu_payload()
  -> ScheduledCommand{sequence, command, context, vcu_payload}
  -> vcu.queue
  -> NpuTop::vcu_thread()
  -> execute_vcu_operation()
  -> concrete execute_vcu_xxx()
```

当前已经接入 NPU VCU 后端的 RVV 指令包括：

| RVV 指令 | 内部 opcode | 作用 |
|---|---|---|
| `vsetvli` / `vsetvl` | `VcuOpcode::Nsetvl` | 更新当前 hart 的 `VcuContext` |
| `vle32.v` | `VcuOpcode::Load` | 从 UB load 到 VCU 向量寄存器 |
| `vse32.v` | `VcuOpcode::Store` | 从 VCU 向量寄存器 store 到 UB |
| `vadd.vv` | `VcuOpcode::Add` | VCU 向量寄存器加法 |

`Nsetvl` 仍然进入 VCU FIFO，只是执行时更新 `nvl` 和 `eew_bytes`，不写回 CPU GPR。

## Opcode 设计

`NpuCommand` 使用一级 `opcode` 加统一二级 `subopcode`：

```cpp
struct NpuCommand
{
    Opcode opcode = Opcode::Vcu;
    uint8_t subopcode = static_cast<uint8_t>(VcuOpcode::Nsetvl);
    ...
};
```

含义是：

- `opcode`：一级模块类型，例如 `Vcu`、`Mte4`、`Sync`、`Cube`。
- `subopcode`：统一二级 opcode 存储，具体解释方式由 `opcode` 决定。

VCU 模块读取二级 opcode 时使用：

```cpp
as_vcu_opcode(command)
```

新增 VCU 指令时，不要再给 `NpuCommand` 增加单独的二级 opcode 字段；只需要扩展 `VcuOpcode`，并让 RVV NPU decoder 把对应枚举值写入 `subopcode`。

## 必改文件

### `gem5/src/dev/npu/npu_types.hh`

在 `VcuOpcode` 中增加新指令枚举。

```cpp
enum class VcuOpcode : uint8_t {
    Load = 0,
    Store = 1,
    Add = 2,
    Nsetvl = 3,
    Sub = 4,
};
```

枚举值是 NPU 内部二级 opcode，不要求等于 RVV 指令字段。

### `gem5/src/arch/riscv/isa/vector/npu/decoder.isa`

在 RVV NPU decoder 中，把目标标准 RVV 指令绑定到对应的 `XaiVcuVector*` format。

如果新增一条和 `vadd.vv` 同形的三寄存器向量指令，可以在 `OPIVV` 对应位置增加：

```text
0x02: XaiVcuVectorArithOp::vsub_vv(Sub);
```

如果新增 `vsetvl` 类指令，可以继续复用 `XaiVcuVectorSetvlImmOp` / `XaiVcuVectorSetvlRegOp`。

### `gem5/src/dev/npu/npu_vcu_operation.cc`

新增执行函数，并注册到 `vcu_operations` 表。

```cpp
void
execute_vcu_sub(VcuExecutionContext &context, const VcuPayload &payload)
{
    require_eew_bytes(payload, sizeof(uint32_t), "vsub");
    auto &destination = context.register_at(payload.destination_register,
                                            "vsub", "destination register");
    const auto &subtrahend = context.register_at(payload.source_register_1,
                                                 "vsub", "source register 1");
    const auto &minuend = context.register_at(payload.source_register_2,
                                              "vsub", "source register 2");
    for (uint64_t index = 0; index < payload.nvl; ++index) {
        const uint64_t byte_offset = index * payload.eew_bytes;
        write_u32(destination, byte_offset,
                  read_u32(minuend, byte_offset) -
                  read_u32(subtrahend, byte_offset));
    }
}
```

表项示例：

```cpp
{VcuOpcode::Sub, "vsub", VcuWorkUnit::Elements,
 &NpuConfig::vadd_elements_per_ns, execute_vcu_sub},
```

如果新指令和 `vadd` 使用相同吞吐，可以复用 `vadd_elements_per_ns`。如果需要单独吞吐参数，需要额外修改 `Npu.py`、`NpuConfig` 和 `npu_cluster.cc`。

## 通常不需要修改的文件

### `gem5/src/arch/riscv/isa/formats/xai.isa`

如果新指令仍是已有 RVV operand 格式，通常不需要修改。当前已经有这些 format：

- `XaiVcuVectorLoadOp`：用于 `vle*.v`。
- `XaiVcuVectorStoreOp`：用于 `vse*.v`。
- `XaiVcuVectorArithOp`：用于 `vadd.vv` 这类寄存器-寄存器计算。
- `XaiVcuVectorSetvlImmOp`：用于 `vsetvli`。
- `XaiVcuVectorSetvlRegOp`：用于 `vsetvl`。

只有当新指令需要不同 operand 格式、不同寄存器读取方式或特殊立即数字段时，才需要新增 format。

### `gem5/src/dev/npu/npu_scheduler.cc`

通常不需要修改。scheduler 会在 `dispatch_one()` 中统一调用：

```cpp
const auto vcu_payload = make_vcu_payload(command, context);
```

只要新指令已经注册到 `vcu_operations` 表，`make_vcu_payload()` 就能构造出对应 payload。日志里的 `op=` 名称来自 `VcuOperationDescriptor::name`。

### `gem5/src/dev/npu/npu_vcu.cc`

通常不需要修改。普通 VCU 计算指令都会走：

```cpp
execute_vcu_operation(context, payload);
```

只有新增类似 `Nsetvl` 这种改变 VCU 上下文、且不适合放入 `vcu_operations` 表的特殊指令时，才需要改 VCU thread。

## 测试相关文件

### `npu-tests/baremetal/vcu/*.cc`

测试程序可以直接写标准 RVV 编码。当前为了避免依赖编译器 RVV intrinsic，测试仍使用 `.word` 固定指令编码。

```cpp
inline void
rvv_vadd_v3_v1_v2()
{
    asm volatile(".word 0x021101d7" : : : "memory");
}
```

新增测试时，建议保持现有 baremetal 风格：准备 file input，XAI MTE 搬运到 UB，执行 RVV NPU VCU 指令，再通过 MTE 和 FileIo 导出结果。

### `npu-tests/scripts/vcu.sh`

如果测试需要检查日志，需要把新 op 名加入 VCU 对应用例的 check。

```bash
for op in ... vsub ...; do
    ...
done
```

日志名来自 `vcu_operations` 表项中的 `name` 字段。当前 RVV NPU VCU 相关用例包括：

```bash
npu-tests/scripts/vcu.sh
```

## 需要新性能参数时

如果新指令需要独立吞吐，例如 `vsub_elements_per_ns`，需要改三处。

### `gem5/src/dev/npu/Npu.py`

增加 Python SimObject 参数：

```python
vsub_elements_per_ns = Param.Float(16.0, "VCU vector-sub throughput")
```

### `gem5/src/dev/npu/npu_types.hh`

在 `NpuConfig` 中增加字段：

```cpp
double vsub_elements_per_ns = 16.0;
```

### `gem5/src/dev/npu/npu_cluster.cc`

在 `make_config()` 中从 params 复制到 `NpuConfig`：

```cpp
config.vsub_elements_per_ns = params.vsub_elements_per_ns;
```

然后在 `vcu_operations` 表中使用：

```cpp
&NpuConfig::vsub_elements_per_ns
```

## 核心结构说明

### `NpuCommand`

定义在 `gem5/src/dev/npu/npu_types.hh`。

`NpuCommand` 是 CPU decode 后提交给 NPU 的通用命令结构。它包含：

- `opcode`：一级模块类型。
- `subopcode`：统一二级 opcode，VCU 使用 `as_vcu_opcode(command)` 解释。
- `pc`、`raw_instruction`：用于 trace 和调试。
- `rd/rs1/rs2`：指令寄存器编号。
- `rd_value/rs1_value/rs2_value`：CPU 侧读出的寄存器值。
- `hart_id`：当前 hart，用于索引每个 hart 独立的 VCU context。
- `npu_mask`：多 NPU 场景下的目标 NPU mask。
- `sync_src/sync_dst/sync_id`：sync 指令使用。
- `sim_file_path/storage_physical_address/file_byte_count`：文件 I/O 指令使用。

`NpuCommand` 不直接表达某一条 VCU 指令的执行 contract，它只是统一 ISA command。

### `VcuOperationDescriptor`

定义在 `gem5/src/dev/npu/npu_vcu_operation.hh`。

```cpp
struct VcuOperationDescriptor
{
    VcuOpcode opcode;
    const char *name;
    VcuWorkUnit work_unit;
    double NpuConfig::*work_rate;
    VcuHandler handler;
};
```

字段含义：

- `opcode`：VCU 指令枚举，用于从 `as_vcu_opcode(command)` 找到对应表项。
- `name`：日志中显示的 op 名，例如 `vadd`。
- `work_unit`：计时单位，`Bytes` 用于 load/store，`Elements` 用于 `vadd/vsub`。
- `work_rate`：指向 `NpuConfig` 吞吐参数的成员指针。
- `handler`：真正执行该指令的函数。

### `VcuPayload`

`VcuPayload` 是 VCU 执行层使用的解码后数据。

```cpp
struct VcuPayload
{
    const VcuOperationDescriptor *operation = nullptr;
    uint8_t destination_register = 0;
    uint8_t source_register_1 = 0;
    uint8_t source_register_2 = 0;
    uint64_t ub_address = 0;
    uint64_t nvl = 0;
    uint8_t eew_bytes = 0;
};
```

字段含义：

- `operation`：指向 `vcu_operations` 表项。
- `destination_register`：目的向量寄存器编号，load 和算术指令来自 RVV `vd`。
- `source_register_1`：源寄存器 1，算术指令来自 `vs1`，load/store 通常不用。
- `source_register_2`：源寄存器 2，算术指令来自 `vs2`，store 指令来自待写回的向量寄存器字段。
- `ub_address`：UB 地址，主要给 load/store 使用，来自地址寄存器的 CPU GPR 值；算术指令通常为 0。
- `nvl`：当前 vector length，来自 `Nsetvl` 写入的 `VcuContext`。
- `eew_bytes`：元素字节数，来自 `Nsetvl` 写入的 `VcuContext`。

`VcuPayload` 的作用是把通用 `NpuCommand` 转换成 VCU handler 直接可用的执行参数，并冻结 dispatch 时刻的 `nvl/eew_bytes`。

### `ScheduledCommand`

定义在 `gem5/src/dev/npu/npu_scheduler.hh`。

```cpp
struct ScheduledCommand
{
    uint64_t sequence = 0;
    NpuCommand command;
    VcuContext context;
    std::optional<VcuPayload> vcu_payload;
};
```

字段含义：

- `sequence`：scheduler 分配的递增序号，用于完成状态、fault 状态和 sync watermark。
- `command`：原始 `NpuCommand`。
- `context`：dispatch 时捕获的 `VcuContext`。
- `vcu_payload`：普通 VCU 指令的预解析执行描述。非 VCU 指令和 `Nsetvl` 通常为空。

### `VcuExecutionContext`

`VcuExecutionContext` 是执行函数访问 VCU 状态和 UB 的接口。

```cpp
struct VcuExecutionContext
{
    const NpuConfig &config;
    std::vector<std::vector<uint8_t>> &registers;
    VcuUbPort ub;

    uint64_t byte_count(const VcuPayload &payload) const;
    std::vector<uint8_t> read_ub(uint64_t address, uint64_t byte_count) const;
    void write_ub(uint64_t address, const std::vector<uint8_t> &data) const;
    std::vector<uint8_t> &register_at(uint8_t index, const char *operation,
                                      const char *role);
    const std::vector<uint8_t> &register_at(uint8_t index, const char *operation,
                                            const char *role) const;
};
```

字段和函数含义：

- `config`：NPU 配置，例如寄存器大小和吞吐参数。
- `registers`：VCU 向量寄存器文件。
- `ub`：VCU 到 UB 的读写端口。
- `byte_count()`：根据 `nvl * eew_bytes` 计算当前指令处理的字节数，并检查是否超过向量寄存器容量。
- `read_ub()`：从 UB 读取数据。
- `write_ub()`：向 UB 写入数据。
- `register_at()`：按寄存器编号访问向量寄存器，并在越界时报错。

### `VcuContext`

定义在 `npu_types.hh`。

```cpp
struct VcuContext
{
    uint64_t nvl = 0;
    uint8_t eew_bytes = 4;
};
```

`Nsetvl` 会更新每个 hart 对应的 `VcuContext`。后续 VCU 指令在 dispatch 时从这里捕获 `nvl` 和 `eew_bytes`，并保存到 `VcuPayload`。

## 核心函数说明

### `find_vcu_operation(VcuOpcode opcode)`

根据 `VcuOpcode` 在 `vcu_operations` 表中查找描述项。新增指令没有注册到表里时，这里会返回 `nullptr`。

### `make_vcu_payload(const NpuCommand &command, const VcuContext &context)`

把通用 `NpuCommand` 转换成 VCU 执行层使用的 `VcuPayload`。

主要映射关系：

```text
as_vcu_opcode(command) -> operation
command.rd             -> destination_register
command.rs1            -> source_register_1
command.rs2            -> source_register_2
command.rs1_value      -> ub_address
context.nvl            -> nvl
context.eew_bytes      -> eew_bytes
```

### `vcu_payload_byte_count(const NpuConfig &config, const VcuPayload &payload)`

计算 `payload.nvl * payload.eew_bytes`，并检查结果不能超过 `config.vector_register_bytes`。

### `vcu_work_count(const NpuConfig &config, const VcuPayload &payload)`

返回计时使用的工作量：

- `Bytes`：返回 `vcu_payload_byte_count()`。
- `Elements`：返回 `payload.nvl`。

### `execute_vcu_operation(VcuExecutionContext &context, const VcuPayload &payload)`

调用 `payload.operation->handler(context, payload)` 执行实际指令。

### `execute_vcu_xxx()`

每条具体 VCU 指令的执行函数。它只应该使用 `VcuExecutionContext` 和 `VcuPayload`，不要直接解析 `NpuCommand`。

## 新增一条类似 `vadd.vv` 的指令检查清单

1. 在 `VcuOpcode` 中加入内部枚举值。
2. 在 `vector/npu/decoder.isa` 中把标准 RVV 编码映射到 `XaiVcuVector*` format。
3. 在 `npu_vcu_operation.cc` 中新增 `execute_vcu_xxx()`。
4. 在 `vcu_operations` 表中注册 descriptor。
5. 如果测试需要，新增 baremetal `.word` helper。
6. 如果测试需要检查日志，更新 VCU 对应的模块脚本或底层兼容脚本。
7. 增量编译相关 ISA 和 NPU 对象文件。

## 推荐验证命令

不需要全量构建时，可以先做脚本语法和对象级编译：

```bash
bash -n npu-tests/scripts/build_gem5.sh
bash -n npu-tests/scripts/vcu.sh

cd gem5
scons build/RISCV/dev/npu/npu_vcu_operation.o \
      build/RISCV/dev/npu/npu_vcu.o \
      build/RISCV/dev/npu/npu_scheduler.o \
      build/RISCV/arch/riscv/generated/decoder.o \
      build/RISCV/arch/riscv/isa.o \
      USE_SYSTEMC=1 RUBY=False USE_KVM=False BUILD_GPU=False --linker=lld -j$(nproc)
```
