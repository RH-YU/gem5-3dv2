#pragma once

#include "dev/npu/npu_scheduler.hh"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include "systemc/ext/core/sc_event.hh"

namespace npu_mvp
{

struct VcuState
{
    VcuState(uint32_t register_count, uint32_t register_bytes)
        : registers(register_count, std::vector<uint8_t>(register_bytes, 0))
    {
    }

    std::deque<ScheduledCommand> queue;
    bool busy = false;
    sc_core::sc_event event;
    std::map<uint8_t, VcuContext> contexts;
    std::vector<std::vector<uint8_t>> registers;
};

} // namespace npu_mvp

#include "dev/npu/npu_top.hh"
