#pragma once

#include "dev/npu/npu_types.hh"
#include "base/types.hh"

namespace npu_mvp
{

class NpuCommandTarget
{
  public:
    virtual ~NpuCommandTarget() = default;
    virtual DispatchStatus submitNpuCommand(const NpuCommand &command) = 0;
};

void registerNpuCommandTarget(gem5::Addr command_base,
                              NpuCommandTarget &target);
void unregisterNpuCommandTarget(gem5::Addr command_base,
                                NpuCommandTarget &target);
DispatchStatus submitNpuCommandDirect(gem5::Addr command_base,
                                      const NpuCommand &command);

} // namespace npu_mvp
