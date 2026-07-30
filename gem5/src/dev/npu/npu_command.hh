#pragma once

#include "dev/npu/npu_types.hh"
#include "base/types.hh"

#include <cstdint>

namespace npu_mvp
{

class NpuCommandTarget
{
  public:
    virtual ~NpuCommandTarget() = default;
    virtual DispatchStatus submitNpuCommand(const NpuCommand &command) = 0;
};

void registerNpuCommandTarget(uint64_t dispatch_id,
                              NpuCommandTarget &target);
void unregisterNpuCommandTarget(uint64_t dispatch_id,
                                NpuCommandTarget &target);
DispatchStatus submitNpuCommandDirect(uint64_t dispatch_id,
                                      const NpuCommand &command);

} // namespace npu_mvp
