#include "dev/npu/npu_mte1.hh"

#include "dev/npu/npu_top.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

void
NpuTop::mte1_thread()
{
    while (true) {
        wait(mte1.event);
        while (!mte1.queue.empty()) {
            ScheduledCommand command = std::move(mte1.queue.front());
            mte1.queue.pop_front();
            trace_queue_sizes();
            mte1.busy = true;
            trace_engine_start(Engine::Mte1, command.command.raw_instruction);
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
            complete(command, Engine::Mte1);
        }
    }
}

} // namespace npu_mvp
