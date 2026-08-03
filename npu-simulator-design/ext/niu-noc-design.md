# NIU / NOC 设计说明

本文说明当前实现中的 NIU 和 NOC 设计，以及相对早期实现的变化。

## 1. 设计目标

当前 NPU 侧新增了两个模块：

- **NIU**：负责把本 core 的 UB 数据打包成 packet，通过 NOC 发送到其他 core；也负责接收来自 NOC 的 packet，并把数据写回目标存储单元。
- **NOC**：负责四个 NPU core 之间的双向环形互联，按链路容量和 NPU 路由规则转发 packet。

NIU 和 NOC 共同支持两类 packet：

- **Data packet**：携带实际搬运的数据。
- **Sync packet**：携带 remote sync token。

## 2. 当前数据路径

### 2.1 NIU 发送路径

```text
NpuTop::execute_niu()
  -> enqueue_niu_packet()
  -> NIU TX queue
  -> NOC inject
  -> ring link forward
  -> NOC deliver
  -> destination NIU RX queue
  -> NpuTop::decode_niu_packet()
  -> write to UB / GM or signal sync token
```

### 2.2 remote sync 路径

remote sync 不再依赖本地 core 的 wait/set 直接广播，而是通过 NIU/NOC 传递：

```text
core0 remote_sync_set
  -> core0 NIU 封装 Sync packet
  -> NOC 发送到 core1
  -> core1 NIU 收到 Sync packet
  -> core1 写入 sync token
  -> core1 local remote_sync_wait 退出
```

## 3. NIU 结构

NIU 的状态定义在 `gem5/src/dev/npu/npu_niu.hh`，主要包含：

- `queue`：NIU command 队列
- `tx_queue`：等待 NOC 发送的 packet 队列
- `rx_queue`：来自 NOC 的 packet 队列
- `active_progress`：当前 NIU 命令的 packet 完成进度
- `trace`：NIU trace 状态

### 3.1 packet 类型

```cpp
struct NiuPacket
{
    enum class Kind : uint8_t {
        Data = 0,
        Sync = 1,
    };
    ...
};
```

#### 公共字段

| 字段 | 含义 |
|---|---|
| `kind` | packet 类型。`Data` 表示数据搬运，`Sync` 表示同步 token 传递。 |
| `sequence` | 由源 NIU 生成的命令级序号，用于把同一条 NIU 命令拆分出的多个 packet 归并到同一次传输中。 |
| `packet_id` | 当前命令内的 packet 编号，从 0 开始递增。 |
| `packet_count` | 当前命令总共拆分出的 packet 数量。 |
| `source_npu_id` | 源 NPU 的编号。 |
| `target_npu_id` | 目标 NPU 的编号。 |

#### Data packet

Data packet 主要用于 `UB -> remote UB/GM` 一类的数据搬运，除公共字段外，还包含：

| 字段 | 含义 |
|---|---|
| `opcode` | 原始 NIU 子 opcode，用于区分数据搬运方向，例如 `UbToRemoteUb` 或 `UbToRemoteGm`。 |
| `target_address` | 目标地址的起始位置，packet 到达目的 NIU 后据此写入目标存储单元。 |
| `payload_bytes` | 当前 packet 中有效 payload 的字节数。每个 packet 的 payload 上限为 128 byte。 |
| `payload` | 真正承载的数据内容，按字节存储。 |
| `last` | 是否为当前命令的最后一个 packet。用于推进命令完成进度。 |

#### Sync packet

Sync packet 主要用于 remote sync token 传递，除公共字段外，还包含：

| 字段 | 含义 |
|---|---|
| `sync_src` | 同步 token 的源端点。 |
| `sync_dst` | 同步 token 的目的端点。 |
| `sync_id` | 同步编号，用来区分同一对端点上的不同同步事件。 |

Sync packet 不携带数据 payload，因此 `opcode`、`target_address`、`payload_bytes`、`payload` 和 `last` 在当前实现中通常保持默认值。

## 4. NOC 结构

NOC 是双向环形总线，每个方向都有独立链路队列。

当前建模点：

- 每段链路有独立容量
- 每个 packet 的基本大小上限是 `128 bytes`
- 每段链路的传输开销由 `noc_packet_bytes` 和 `noc_bytes_per_cycle` 决定
- packet 在环上逐跳转发，直到到达目标 NPU

当前 trace 中可以看到的 NOC 事件包括：

- `inject_event`
- `forward_event`
- `deliver_event`
- `ack_event`
- `block_event`

## 5. trace 信号含义

### 5.1 NIU trace

| 信号 | 含义 |
|---|---|
| `packet_sent_event` | packet 被放入 NIU TX queue |
| `packet_received_event` | packet 被目的 NIU 收到并进入 RX 处理路径 |
| `ack_event` | NOC 对源 NIU 回确认 |
| `busy` | NIU 当前是否忙 |
| `command_queue_size` | NIU command 队列深度 |
| `tx_queue_size` | NIU TX packet 队列深度 |
| `rx_queue_size` | NIU RX packet 队列深度 |
| `instruction` | 当前 NIU 正在执行的 raw instruction |

### 5.2 NOC trace

| 信号 | 含义 |
|---|---|
| `inject_event` | NOC 从源 NIU 取走 packet 并注入链路 |
| `forward_event` | packet 在环上转发到下一段链路 |
| `deliver_event` | packet 到达目标 NIU |
| `ack_event` | NOC 向源 NIU 返回完成确认 |
| `block_event` | 注入/转发/投递因容量不足而阻塞 |

## 6. 相关文件

- `gem5/src/dev/npu/npu_niu.hh`
- `gem5/src/dev/npu/npu_niu.cc`
- `gem5/src/dev/npu/npu_noc.hh`
- `gem5/src/dev/npu/npu_noc.cc`
- `gem5/src/dev/npu/npu_top.cc`
- `npu-tests/baremetal/niu/xai_niu_smoke.cc`
- `npu-tests/scripts/niu.sh`
