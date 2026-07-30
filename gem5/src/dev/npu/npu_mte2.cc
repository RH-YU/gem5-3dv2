#include "dev/npu/npu_mte.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

void
NpuTop::mte2_thread()
{
    while (true) {
        wait(mte2.event);
        while (!mte2.queue.empty()) {
            ScheduledCommand command = std::move(mte2.queue.front());
            mte2.queue.pop_front();
            mte2.trace.trace_queue_size(mte2.queue.size());
            mte2.busy = true;
            mte2.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.rd_value,
                                    config.mte2_bytes_per_ns,
                                    config.mte2_setup_delay));
                try {
                    execute_mte2(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            mte2.busy = false;
            mte2.trace.trace_done();
            complete(command, Engine::Mte2);
        }
    }
}

} // namespace npu_mvp
