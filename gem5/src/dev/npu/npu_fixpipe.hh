#pragma once

#include "dev/npu/npu_scheduler.hh"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "systemc/ext/core/sc_event.hh"
#include "systemc/ext/dt/bit/sc_bv.hh"
#include "systemc/ext/dt/int/sc_uint.hh"
#include "systemc/ext/utils/sc_trace_file.hh"

namespace npu_mvp
{

struct FixpipeTraceSignals
{
    bool start_event = false;
    bool done_event = false;
    bool busy = false;
    sc_dt::sc_uint<32> queue_size = 0;
    sc_dt::sc_bv<32> instruction = sc_dt::sc_bv<32>(0);
};

struct FixpipeTraceState
{
    void register_trace(sc_core::sc_trace_file *trace_file,
                        const std::string &scope);
    void trace_start(uint32_t raw_instruction);
    void trace_done();
    void trace_queue_size(std::size_t queue_size);

    sc_core::sc_trace_file *trace_file = nullptr;
    FixpipeTraceSignals signals;
};

struct FixpipeState
{
    std::deque<ScheduledCommand> queue;
    bool busy = false;
    sc_core::sc_event event;
    FixpipeTraceState trace;
};

} // namespace npu_mvp
