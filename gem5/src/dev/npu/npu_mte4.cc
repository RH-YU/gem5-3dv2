#include "dev/npu/npu_mte.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

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
