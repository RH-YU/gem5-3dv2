#include "dev/npu/npu_sync.hh"

namespace npu_mvp
{

bool
NpuTop::is_cpu_sync_set(const NpuCommand &command) const
{
    return command.opcode == Opcode::Sync &&
           as_sync_opcode(command) == SyncOpcode::Set &&
           command.sync_src == SyncEndpoint::Cpu;
}

bool
NpuTop::is_cpu_sync_wait(const NpuCommand &command) const
{
    return command.opcode == Opcode::Sync &&
           as_sync_opcode(command) == SyncOpcode::Wait &&
           command.sync_dst == SyncEndpoint::Cpu;
}

void
NpuTop::signal_cpu_sync(const NpuCommand &command)
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return;

    signal_sync_token(command);
}

bool
NpuTop::cpu_sync_ready(const NpuCommand &command) const
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return true;

    const SyncToken token{command.sync_src, command.sync_dst, command.sync_id};
    const auto it = sync.token_counts.find(token);
    return it != sync.token_counts.end() && it->second > 0;
}

void
NpuTop::consume_cpu_sync(const NpuCommand &command)
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return;

    const SyncToken token{command.sync_src, command.sync_dst, command.sync_id};
    auto it = sync.token_counts.find(token);
    if (it == sync.token_counts.end() || it->second == 0)
        return;

    --it->second;
}

void
NpuTop::execute_sync(const ScheduledCommand &command)
{
    trace_sync_start(command.command);
    if (as_sync_opcode(command.command) == SyncOpcode::Set) {
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
