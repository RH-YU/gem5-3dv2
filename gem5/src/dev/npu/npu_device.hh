#pragma once

#include "dev/npu/npu_top.hh"

#include <string>

#include "systemc/tlm_port_wrapper.hh"

namespace npu_mvp
{

class NpuDevice : public sc_core::sc_module
{
  public:
    NpuDevice(const char *name, const NpuConfig &config);

    gem5::Port &gem5_getPort(const std::string &if_name, int idx=-1) override;

    NpuTop &top();
    const NpuTop &top() const;

  private:
    NpuTop npu;
    sc_gem5::TlmTargetWrapper<64, tlm::tlm_base_protocol_types, 1,
                              sc_core::SC_ZERO_OR_MORE_BOUND> tlm_wrapper;
};

} // namespace npu_mvp
