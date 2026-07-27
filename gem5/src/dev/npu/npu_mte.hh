#pragma once

#include "dev/npu/npu_scheduler.hh"

#include <deque>

#include "systemc/ext/core/sc_event.hh"

namespace npu_mvp
{

struct MteEngineState
{
    std::deque<ScheduledCommand> queue;
    bool busy = false;
    sc_core::sc_event event;
};

} // namespace npu_mvp

#include "dev/npu/npu_top.hh"
