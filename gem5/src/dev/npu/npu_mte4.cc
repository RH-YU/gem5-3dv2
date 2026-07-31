#include "dev/npu/npu_mte4.hh"

#include "dev/npu/npu_top.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

void
Mte4TraceState::register_trace(sc_core::sc_trace_file *tf,
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
Mte4TraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
Mte4TraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
Mte4TraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

void
NpuTop::execute_mte4(const ScheduledCommand &command)
{
    const Region destination = as_mte4_opcode(command.command) == Mte4Opcode::GmToL1
            ? Region::L1
            : Region::Ub;
    execute_mte(command, Region::Gm, destination);
}

void
NpuTop::mte4_thread()
{
    while (true) {
        wait(mte4.event);
        while (!mte4.queue.empty()) {
            ScheduledCommand command = std::move(mte4.queue.front());
            mte4.queue.pop_front();
            mte4.trace.trace_queue_size(mte4.queue.size());
            mte4.busy = true;
            mte4.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.rd_value,
                                    config.mte4_bytes_per_ns,
                                    config.mte4_setup_delay));
                try {
                    execute_mte4(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            mte4.busy = false;
            mte4.trace.trace_done();
            complete(command, Engine::Mte4);
        }
    }
}

} // namespace npu_mvp
