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

    bool operator<(const SyncToken &other) const
    {
        return std::tie(src, dst, id) <
               std::tie(other.src, other.dst, other.id);
    }
};

struct SyncState
{
    std::map<SyncToken, uint64_t> token_counts;
    sc_core::sc_event token_event;
};

} // namespace npu_mvp

#include "dev/npu/npu_top.hh"
