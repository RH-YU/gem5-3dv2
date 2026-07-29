#include "dev/npu/npu_command.hh"

#include "base/logging.hh"

#include <map>

namespace npu_mvp
{

namespace
{

std::map<gem5::Addr, NpuCommandTarget *> command_targets;

} // anonymous namespace

void
registerNpuCommandTarget(gem5::Addr command_base, NpuCommandTarget &target)
{
    panic_if(command_base == 0, "NPU command target requires a non-zero base.");
    auto [it, inserted] = command_targets.emplace(command_base, &target);
    panic_if(!inserted && it->second != &target,
             "Duplicate NPU command target at %#x.", command_base);
}

void
unregisterNpuCommandTarget(gem5::Addr command_base, NpuCommandTarget &target)
{
    auto it = command_targets.find(command_base);
    if (it == command_targets.end())
        return;

    panic_if(it->second != &target,
             "NPU command target unregister mismatch at %#x.", command_base);
    command_targets.erase(it);
}

DispatchStatus
submitNpuCommandDirect(gem5::Addr command_base, const NpuCommand &command)
{
    auto it = command_targets.find(command_base);
    if (it == command_targets.end())
        return DispatchStatus::Invalid;

    return it->second->submitNpuCommand(command);
}

} // namespace npu_mvp
