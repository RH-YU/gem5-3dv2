#include "dev/npu/npu_fixpipe.hh"

#include "dev/npu/npu_top.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

void
FixpipeTraceState::register_trace(sc_core::sc_trace_file *tf,
                                  const std::string &scope)
{
    trace_file = tf;
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.start_event, scope + ".start_event");
    sc_core::sc_trace(trace_file, signals.done_event, scope + ".done_event");
    sc_core::sc_trace(trace_file, signals.busy, scope + ".busy");
    sc_core::sc_trace(trace_file, signals.queue_size, scope + ".queue_size");
    sc_core::sc_trace(trace_file, signals.instruction, scope + ".instruction");
}

void
FixpipeTraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
FixpipeTraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
FixpipeTraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

void
NpuTop::execute_fixpipe(const ScheduledCommand &command)
{
    const Region destination =
            as_fixpipe_opcode(command.command) == FixpipeOpcode::L0CToUb
            ? Region::Ub
            : Region::L1;
    execute_mte(command, Region::L0C, destination);
}

void
NpuTop::fixpipe_thread()
{
    while (true) {
        wait(fixpipe.event);
        while (!fixpipe.queue.empty()) {
            ScheduledCommand command = std::move(fixpipe.queue.front());
            fixpipe.queue.pop_front();
            fixpipe.trace.trace_queue_size(fixpipe.queue.size());
            fixpipe.busy = true;
            fixpipe.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.rd_value,
                                    config.fixpipe_bytes_per_ns,
                                    config.fixpipe_setup_delay));
                try {
                    execute_fixpipe(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            fixpipe.busy = false;
            fixpipe.trace.trace_done();
            complete(command, Engine::Fixpipe);
        }
    }
}

} // namespace npu_mvp
