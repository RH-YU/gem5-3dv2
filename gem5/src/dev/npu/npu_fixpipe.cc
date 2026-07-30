#include "dev/npu/npu_fixpipe.hh"

#include "dev/npu/npu_top.hh"

#include <stdexcept>
#include <utility>

namespace npu_mvp
{

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
            trace_queue_sizes();
            fixpipe.busy = true;
            trace_engine_start(Engine::Fixpipe, command.command.raw_instruction);
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
            complete(command, Engine::Fixpipe);
        }
    }
}

} // namespace npu_mvp
