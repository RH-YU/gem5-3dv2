#include "dev/npu/npu_command.hh"

#include "base/logging.hh"

#include <map>
#include <utility>

#include "systemc/tlm_bridge/gem5_to_tlm.hh"

namespace npu_mvp
{

namespace
{

bool packet_conversion_registered = false;
std::map<gem5::Addr, NpuCommandTarget *> command_targets;

void
attach_npu_command_extension(gem5::PacketPtr packet,
                             tlm::tlm_generic_payload &transaction)
{
    auto *state = const_cast<NpuCommandSenderState *>(
            packet->findNextSenderState<NpuCommandSenderState>());
    if (state == nullptr)
        return;

    auto *extension = new NpuCommandExtension;
    extension->command = state->command;
    extension->sender_state = state;
    transaction.set_auto_extension(extension);
}

} // anonymous namespace

NpuCommandSenderState::NpuCommandSenderState(NpuCommand command)
    : command(std::move(command))
{}

tlm::tlm_extension_base *
NpuCommandExtension::clone() const
{
    auto *extension = new NpuCommandExtension;
    extension->command = command;
    extension->sender_state = sender_state;
    return extension;
}

void
NpuCommandExtension::copy_from(const tlm::tlm_extension_base &extension)
{
    const auto &typed_extension =
            static_cast<const NpuCommandExtension &>(extension);
    command = typed_extension.command;
    sender_state = typed_extension.sender_state;
}

void
registerNpuPacketConversionStep()
{
    if (packet_conversion_registered)
        return;

    sc_gem5::addPacketToPayloadConversionStep(attach_npu_command_extension);
    packet_conversion_registered = true;
}

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
