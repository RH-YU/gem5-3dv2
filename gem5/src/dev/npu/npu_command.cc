#include "dev/npu/npu_command.hh"

#include <utility>

#include "systemc/tlm_bridge/gem5_to_tlm.hh"

namespace npu_mvp
{

namespace
{

bool packet_conversion_registered = false;

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

} // namespace npu_mvp
