#pragma once

#include "dev/npu/npu_types.hh"
#include "mem/packet.hh"
#include "systemc/ext/tlm_core/2/generic_payload/gp.hh"

namespace npu_mvp
{

class NpuCommandSenderState : public gem5::Packet::SenderState
{
  public:
    explicit NpuCommandSenderState(NpuCommand command);

    NpuCommand command;
    DispatchStatus status = DispatchStatus::Accepted;
};

class NpuCommandExtension : public tlm::tlm_extension<NpuCommandExtension>
{
  public:
    NpuCommand command;
    NpuCommandSenderState *sender_state = nullptr;

    tlm::tlm_extension_base *clone() const override;
    void copy_from(const tlm::tlm_extension_base &extension) override;
};

void registerNpuPacketConversionStep();

} // namespace npu_mvp
