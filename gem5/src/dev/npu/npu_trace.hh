#pragma once

#include <cstdint>
#include <string>

#include "systemc/ext/dt/bit/sc_bv.hh"
#include "systemc/ext/dt/bit/sc_uint.hh"
#include "systemc/ext/utils/sc_trace_file.hh"

namespace npu_mvp
{

struct NpuTraceSignals
{
    bool ingress_event = false;
    bool dispatch_event = false;
    bool engine_start_event = false;
    bool engine_done_event = false;
    bool fault_event = false;
    bool sync_event = false;
    bool mte4_busy = false;
    bool mte2_busy = false;
    bool vcu_busy = false;
    bool gm_file_io_busy = false;
    sc_dt::sc_uint<32> scheduler_queue_size = 0;
    sc_dt::sc_uint<32> mte4_queue_size = 0;
    sc_dt::sc_uint<32> mte2_queue_size = 0;
    sc_dt::sc_uint<32> vcu_queue_size = 0;
    sc_dt::sc_uint<32> gm_file_io_queue_size = 0;
    sc_dt::sc_bv<32> mte4_instruction = sc_dt::sc_bv<32>(0);
    sc_dt::sc_bv<32> mte2_instruction = sc_dt::sc_bv<32>(0);
    sc_dt::sc_bv<32> vcu_instruction = sc_dt::sc_bv<32>(0);
    sc_dt::sc_bv<32> gm_file_io_instruction = sc_dt::sc_bv<32>(0);
};

struct NpuClusterTraceSignals
{
    bool cpu_cmd_event = false;
    bool cpu_backpressure_event = false;
    bool cpu_instruction_event = false;
    bool cpu_commit_event = false;
    bool cpu_commit_valid = false;
    sc_dt::sc_bv<32> cpu_commit_pc = sc_dt::sc_bv<32>(0);
    sc_dt::sc_bv<32> cpu_commit_insn = sc_dt::sc_bv<32>(0);
};

std::string normalize_vcd_trace_basename(const std::string &trace_file);
uint64_t active_cpu_cycle_ticks(uint64_t configured_cycle_ticks);
void configure_vcd_trace_time_unit(sc_core::sc_trace_file *trace_file,
                                   uint64_t configured_cycle_ticks);
void register_cluster_trace_signals(sc_core::sc_trace_file *trace_file,
                                    NpuClusterTraceSignals &signals,
                                    const std::string &scope);
void register_cpu_trace_signals(sc_core::sc_trace_file *trace_file,
                                NpuClusterTraceSignals &signals,
                                const std::string &scope);
void register_npu_trace_signals(sc_core::sc_trace_file *trace_file,
                                NpuTraceSignals &signals,
                                const std::string &scope);

} // namespace npu_mvp
