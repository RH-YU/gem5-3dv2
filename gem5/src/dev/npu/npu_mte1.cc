#include "dev/npu/npu_mte1.hh"

#include "dev/npu/npu_top.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

void
Mte1TraceState::register_trace(sc_core::sc_trace_file *tf,
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
Mte1TraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
Mte1TraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
Mte1TraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

void
NpuTop::execute_mte1(const ScheduledCommand &command)
{
    Region destination = Region::Gm;
    switch (as_mte1_opcode(command.command)) {
      case Mte1Opcode::L1ToGm:
        destination = Region::Gm;
        break;
      case Mte1Opcode::L1ToUb:
        destination = Region::Ub;
        break;
      case Mte1Opcode::L1ToL0A:
        destination = Region::L0A;
        break;
      case Mte1Opcode::L1ToL0B:
        destination = Region::L0B;
        break;
    }
    execute_mte(command, Region::L1, destination);
}

void
NpuTop::mte1_thread()
{
    while (true) {
        wait(mte1.event);
        while (!mte1.queue.empty()) {
            ScheduledCommand command = std::move(mte1.queue.front());
            mte1.queue.pop_front();
            mte1.trace.trace_queue_size(mte1.queue.size());
            mte1.busy = true;
            mte1.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.rd_value,
                                    config.mte1_bytes_per_ns,
                                    config.mte1_setup_delay));
                try {
                    execute_mte1(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            mte1.busy = false;
            mte1.trace.trace_done();
            complete(command, Engine::Mte1);
        }
    }
}

} // namespace npu_mvp
