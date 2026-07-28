#include "dev/npu/npu_sync.hh"

namespace npu_mvp
{

void
NpuTop::execute_sync(const ScheduledCommand &command)
{
    trace_sync_start(command.command);
    if (command.command.sync_opcode == SyncOpcode::Set) {
        signal_sync_token(command.command);
    } else {
        wait_for_sync_token(command.command);
    }
    wait(config.scheduler_dispatch_delay);
    trace_sync_done();
}

void
NpuTop::signal_sync_token(const NpuCommand &command)
{
    const SyncToken token{command.sync_src, command.sync_dst, command.sync_id};
    ++sync.token_counts[token];
    sync.token_event.notify(sc_core::SC_ZERO_TIME);
}

void
NpuTop::wait_for_sync_token(const NpuCommand &command)
{
    const SyncToken token{command.sync_src, command.sync_dst, command.sync_id};
    while (sync.token_counts[token] == 0) {
        wait(sync.token_event);
    }
    --sync.token_counts[token];
}

} // namespace npu_mvp
