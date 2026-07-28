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
            trace_queue_sizes();
            mte2.busy = true;
            trace_engine_start(Engine::Mte2, command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.rd_value,
                                    config.mte2_bytes_per_ns,
                                    config.mte2_setup_delay));
                try {
                    execute_mte(command, Region::Ub, Region::Gm);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            mte2.busy = false;
            complete(command, Engine::Mte2);
        }
    }
}

} // namespace npu_mvp
