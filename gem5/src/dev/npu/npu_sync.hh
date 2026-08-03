#pragma once

#include "dev/npu/npu_scheduler.hh"
#include "dev/npu/npu_types.hh"

#include <cstdint>
#include <map>
#include <tuple>

#include "systemc/ext/core/sc_event.hh"

namespace npu_mvp
{

struct SyncToken
{
    SyncEndpoint src = SyncEndpoint::Mte4;
    SyncEndpoint dst = SyncEndpoint::Mte4;
    uint8_t id = 0;
    uint8_t peer_npu_id = 0xff;

    bool operator<(const SyncToken &other) const
    {
        return std::tie(src, dst, id, peer_npu_id) <
               std::tie(other.src, other.dst, other.id, other.peer_npu_id);
    }
};

struct RemoteSyncInfo
{
    uint8_t peer_npu_id = 0xff;
    SyncEndpoint endpoint = SyncEndpoint::Mte4;
};

RemoteSyncInfo decode_remote_sync_info(const NpuCommand &command);

struct SyncState
{
    std::map<SyncToken, uint64_t> token_counts;
    sc_core::sc_event token_event;
};

} // namespace npu_mvp

#include "dev/npu/npu_top.hh"
