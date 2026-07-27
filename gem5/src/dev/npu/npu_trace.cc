#include "dev/npu/npu_trace.hh"

#include "sim/core.hh"
#include "sim/cur_tick.hh"

namespace npu_mvp
{

std::string
normalize_vcd_trace_basename(const std::string &trace_file)
{
    constexpr const char *suffix = ".vcd";
    constexpr std::size_t suffix_len = 4;
    if (trace_file.size() > suffix_len &&
        trace_file.compare(trace_file.size() - suffix_len, suffix_len,
                           suffix) == 0) {
        return trace_file.substr(0, trace_file.size() - suffix_len);
    }
    return trace_file;
}

uint64_t
active_cpu_cycle_ticks(uint64_t configured_cycle_ticks)
{
    if (configured_cycle_ticks != 0)
        return configured_cycle_ticks;
    return gem5::getCpuClockPeriod();
}

void
configure_vcd_trace_time_unit(sc_core::sc_trace_file *trace_file,
                              uint64_t configured_cycle_ticks)
{
    if (trace_file == nullptr)
        return;

    const uint64_t cycle_ticks = active_cpu_cycle_ticks(configured_cycle_ticks);
    if (cycle_ticks == 0)
        return;

    const double seconds =
            static_cast<double>(cycle_ticks) / gem5::sim_clock::as_float::s;
    trace_file->set_time_unit(seconds, sc_core::SC_SEC);
}

void
register_cluster_trace_signals(sc_core::sc_trace_file *trace_file,
                               NpuClusterTraceSignals &signals,
                               const std::string &scope)
{
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.cpu_cmd_event, scope + ".cpu_cmd_event");
    sc_core::sc_trace(trace_file, signals.cpu_backpressure_event,
                      scope + ".cpu_backpressure_event");
    sc_core::sc_trace(trace_file, signals.cpu_instruction_event,
                      scope + ".cpu_instruction_event");
}

void
register_cpu_trace_signals(sc_core::sc_trace_file *trace_file,
                           NpuClusterTraceSignals &signals,
                           const std::string &scope)
{
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.cpu_commit_event,
                      scope + ".commit_event");
    sc_core::sc_trace(trace_file, signals.cpu_commit_valid,
                      scope + ".commit_valid");
    sc_core::sc_trace(trace_file, signals.cpu_commit_pc,
                      scope + ".commit_pc");
    sc_core::sc_trace(trace_file, signals.cpu_commit_insn,
                      scope + ".commit_insn");
}

void
register_npu_trace_signals(sc_core::sc_trace_file *trace_file,
                           NpuTraceSignals &signals,
                           const std::string &scope)
{
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.ingress_event, scope + ".ingress_event");
    sc_core::sc_trace(trace_file, signals.dispatch_event, scope + ".dispatch_event");
    sc_core::sc_trace(trace_file, signals.engine_start_event,
                      scope + ".engine_start_event");
    sc_core::sc_trace(trace_file, signals.engine_done_event,
                      scope + ".engine_done_event");
    sc_core::sc_trace(trace_file, signals.fault_event, scope + ".fault_event");
    sc_core::sc_trace(trace_file, signals.sync_event, scope + ".sync_event");
    sc_core::sc_trace(trace_file, signals.mte4_busy, scope + ".mte4_busy");
    sc_core::sc_trace(trace_file, signals.mte2_busy, scope + ".mte2_busy");
    sc_core::sc_trace(trace_file, signals.vcu_busy, scope + ".vcu_busy");
    sc_core::sc_trace(trace_file, signals.gm_file_io_busy,
                      scope + ".gm_file_io_busy");
    sc_core::sc_trace(trace_file, signals.mte4_instruction,
                      scope + ".mte4_instruction");
    sc_core::sc_trace(trace_file, signals.mte2_instruction,
                      scope + ".mte2_instruction");
    sc_core::sc_trace(trace_file, signals.vcu_instruction,
                      scope + ".vcu_instruction");
    sc_core::sc_trace(trace_file, signals.gm_file_io_instruction,
                      scope + ".gm_file_io_instruction");
}

} // namespace npu_mvp
