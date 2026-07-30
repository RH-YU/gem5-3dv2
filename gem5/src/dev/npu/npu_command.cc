#include "dev/npu/npu_command.hh"

#include "base/logging.hh"

#include <map>

namespace npu_mvp
{

namespace
{

std::map<uint64_t, NpuCommandTarget *> command_targets;

} // anonymous namespace

void
registerNpuCommandTarget(uint64_t dispatch_id, NpuCommandTarget &target)
{
    panic_if(dispatch_id == 0, "NPU command target requires a non-zero dispatch id.");
    auto [it, inserted] = command_targets.emplace(dispatch_id, &target);
    panic_if(!inserted && it->second != &target,
             "Duplicate NPU command target id %llu.",
             static_cast<unsigned long long>(dispatch_id));
}

void
unregisterNpuCommandTarget(uint64_t dispatch_id, NpuCommandTarget &target)
{
    auto it = command_targets.find(dispatch_id);
    if (it == command_targets.end())
        return;

    panic_if(it->second != &target,
             "NPU command target unregister mismatch at id %llu.",
             static_cast<unsigned long long>(dispatch_id));
    command_targets.erase(it);
}

DispatchStatus
submitNpuCommandDirect(uint64_t dispatch_id, const NpuCommand &command)
{
    auto it = command_targets.find(dispatch_id);
    if (it == command_targets.end())
        return DispatchStatus::Invalid;

    return it->second->submitNpuCommand(command);
}

} // namespace npu_mvp
