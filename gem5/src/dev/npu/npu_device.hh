#pragma once

#include "dev/npu/npu_command.hh"
#include "dev/npu/npu_top.hh"

#include <string>

#include "systemc/tlm_port_wrapper.hh"

namespace npu_mvp
{

class NpuDevice : public sc_core::sc_module, public NpuCommandTarget
{
  public:
    NpuDevice(const char *name, const NpuConfig &config);
    ~NpuDevice() override;

    gem5::Port &gem5_getPort(const std::string &if_name, int idx=-1) override;
    DispatchStatus submitNpuCommand(const NpuCommand &command) override;

    NpuTop &top();
    const NpuTop &top() const;

  private:
    NpuTop npu;
    gem5::Addr command_base = 0;
    sc_gem5::TlmTargetWrapper<64, tlm::tlm_base_protocol_types, 1,
                              sc_core::SC_ZERO_OR_MORE_BOUND> tlm_wrapper;
};

} // namespace npu_mvp
