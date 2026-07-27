#pragma once

#include "dev/npu/npu_command.hh"
#include "dev/npu/npu_types.hh"
#include "dev/npu/npu_vcu_operation.hh"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>

#include "systemc/ext/core/sc_event.hh"

namespace npu_mvp
{

struct ScheduledCommand
{
    uint64_t sequence = 0;
    NpuCommand command;
    VcuContext context;
    std::optional<VcuPayload> vcu_payload;
};

struct CommandRecord
{
    Engine engine = Engine::Control;
    bool complete = false;
    bool faulted = false;
};

struct SchedulerState
{
    std::deque<NpuCommand> ingress_queue;
    std::map<uint64_t, CommandRecord> command_records;
    uint64_t next_sequence = 1;
    uint64_t faults = 0;
    std::string latest_fault;
    sc_core::sc_event dispatch_event;
};

} // namespace npu_mvp

#ifdef NPU_SCHEDULER_INCLUDE_TOP
#include "dev/npu/npu_top.hh"
#endif
