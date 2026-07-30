#pragma once

#include "dev/npu/npu_scheduler.hh"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "systemc/ext/core/sc_event.hh"
#include "systemc/ext/dt/bit/sc_bv.hh"
#include "systemc/ext/dt/int/sc_uint.hh"
#include "systemc/ext/utils/sc_trace_file.hh"

namespace npu_mvp
{

struct VcuTraceSignals
{
    bool start_event = false;
    bool done_event = false;
    bool busy = false;
    sc_dt::sc_uint<32> queue_size = 0;
    sc_dt::sc_bv<32> instruction = sc_dt::sc_bv<32>(0);
};

struct VcuTraceState
{
    void register_trace(sc_core::sc_trace_file *trace_file,
                        const std::string &scope);
    void trace_start(uint32_t raw_instruction);
    void trace_done();
    void trace_queue_size(std::size_t queue_size);

    sc_core::sc_trace_file *trace_file = nullptr;
    VcuTraceSignals signals;
};

struct VcuState
{
    VcuState(uint32_t register_count, uint32_t register_bytes)
        : registers(register_count, std::vector<uint8_t>(register_bytes, 0))
    {
    }

    std::deque<ScheduledCommand> queue;
    bool busy = false;
    sc_core::sc_event event;
    VcuTraceState trace;
    std::map<uint8_t, VcuContext> contexts;
    std::vector<std::vector<uint8_t>> registers;
};

} // namespace npu_mvp

#include "dev/npu/npu_top.hh"
