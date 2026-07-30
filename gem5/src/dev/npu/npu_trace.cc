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

uint64_t
vcd_trace_time_unit_ticks(uint64_t cycle_ticks)
{
    return cycle_ticks <= 1 ? 1 : cycle_ticks / 2;
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

    const uint64_t time_unit_ticks = vcd_trace_time_unit_ticks(cycle_ticks);
    const double seconds =
            static_cast<double>(time_unit_ticks) /
            gem5::sim_clock::as_float::s;
    trace_file->set_time_unit(seconds, sc_core::SC_SEC);
}

void
register_cpu_trace_signals(sc_core::sc_trace_file *trace_file,
                           NpuClusterTraceSignals &signals,
                           const std::string &scope)
{
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.cpu_cmd_event,
                      scope + ".npu_cmd_event");
    sc_core::sc_trace(trace_file, signals.cpu_backpressure_event,
                      scope + ".npu_backpressure_event");
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

    sc_core::sc_trace(trace_file, signals.ingress_event,
                      scope + ".scheduler.ingress_event");
    sc_core::sc_trace(trace_file, signals.dispatch_event,
                      scope + ".scheduler.dispatch_event");
    sc_core::sc_trace(trace_file, signals.scheduler_queue_size,
                      scope + ".scheduler.queue_size");
    sc_core::sc_trace(trace_file, signals.fault_event, scope + ".fault.event");
    sc_core::sc_trace(trace_file, signals.sync_event, scope + ".sync.event");

    sc_core::sc_trace(trace_file, signals.mte4_start_event,
                      scope + ".mte4.start_event");
    sc_core::sc_trace(trace_file, signals.mte4_done_event,
                      scope + ".mte4.done_event");
    sc_core::sc_trace(trace_file, signals.mte4_busy, scope + ".mte4.busy");
    sc_core::sc_trace(trace_file, signals.mte4_queue_size,
                      scope + ".mte4.queue_size");
    sc_core::sc_trace(trace_file, signals.mte4_instruction,
                      scope + ".mte4.instruction");

    sc_core::sc_trace(trace_file, signals.mte1_start_event,
                      scope + ".mte1.start_event");
    sc_core::sc_trace(trace_file, signals.mte1_done_event,
                      scope + ".mte1.done_event");
    sc_core::sc_trace(trace_file, signals.mte1_busy, scope + ".mte1.busy");
    sc_core::sc_trace(trace_file, signals.mte1_queue_size,
                      scope + ".mte1.queue_size");
    sc_core::sc_trace(trace_file, signals.mte1_instruction,
                      scope + ".mte1.instruction");

    sc_core::sc_trace(trace_file, signals.mte2_start_event,
                      scope + ".mte2.start_event");
    sc_core::sc_trace(trace_file, signals.mte2_done_event,
                      scope + ".mte2.done_event");
    sc_core::sc_trace(trace_file, signals.mte2_busy, scope + ".mte2.busy");
    sc_core::sc_trace(trace_file, signals.mte2_queue_size,
                      scope + ".mte2.queue_size");
    sc_core::sc_trace(trace_file, signals.mte2_instruction,
                      scope + ".mte2.instruction");

    sc_core::sc_trace(trace_file, signals.vcu_start_event,
                      scope + ".vcu.start_event");
    sc_core::sc_trace(trace_file, signals.vcu_done_event,
                      scope + ".vcu.done_event");
    sc_core::sc_trace(trace_file, signals.vcu_busy, scope + ".vcu.busy");
    sc_core::sc_trace(trace_file, signals.vcu_queue_size,
                      scope + ".vcu.queue_size");
    sc_core::sc_trace(trace_file, signals.vcu_instruction,
                      scope + ".vcu.instruction");

    sc_core::sc_trace(trace_file, signals.cube_start_event,
                      scope + ".cube.start_event");
    sc_core::sc_trace(trace_file, signals.cube_done_event,
                      scope + ".cube.done_event");
    sc_core::sc_trace(trace_file, signals.cube_busy, scope + ".cube.busy");
    sc_core::sc_trace(trace_file, signals.cube_queue_size,
                      scope + ".cube.queue_size");
    sc_core::sc_trace(trace_file, signals.cube_instruction,
                      scope + ".cube.instruction");

    sc_core::sc_trace(trace_file, signals.fixpipe_start_event,
                      scope + ".fixpipe.start_event");
    sc_core::sc_trace(trace_file, signals.fixpipe_done_event,
                      scope + ".fixpipe.done_event");
    sc_core::sc_trace(trace_file, signals.fixpipe_busy, scope + ".fixpipe.busy");
    sc_core::sc_trace(trace_file, signals.fixpipe_queue_size,
                      scope + ".fixpipe.queue_size");
    sc_core::sc_trace(trace_file, signals.fixpipe_instruction,
                      scope + ".fixpipe.instruction");

    sc_core::sc_trace(trace_file, signals.gm_file_io_start_event,
                      scope + ".gm_file_io.start_event");
    sc_core::sc_trace(trace_file, signals.gm_file_io_done_event,
                      scope + ".gm_file_io.done_event");
    sc_core::sc_trace(trace_file, signals.gm_file_io_busy,
                      scope + ".gm_file_io.busy");
    sc_core::sc_trace(trace_file, signals.gm_file_io_queue_size,
                      scope + ".gm_file_io.queue_size");
    sc_core::sc_trace(trace_file, signals.gm_file_io_instruction,
                      scope + ".gm_file_io.instruction");
}

} // namespace npu_mvp
