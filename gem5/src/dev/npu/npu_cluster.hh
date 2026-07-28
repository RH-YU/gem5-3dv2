#pragma once

#include "dev/npu/npu_command.hh"
#include "dev/npu/npu_top.hh"
#include "dev/npu/npu_trace.hh"

#include <memory>
#include <string>
#include <vector>

#include "systemc/ext/channel/sc_clock.hh"
#include "systemc/tlm_port_wrapper.hh"
#include "systemc/ext/core/sc_module_name.hh"

namespace npu_mvp
{

class NpuCluster : public sc_core::sc_module, public NpuCommandTarget
{
  public:
    SC_HAS_PROCESS(NpuCluster);

    NpuCluster(sc_core::sc_module_name name, const NpuConfig &config,
               uint8_t npu_count);
    ~NpuCluster() override;

    gem5::Port &gem5_getPort(const std::string &if_name, int idx=-1) override;
    void record_cpu_commit(uint32_t pc, uint32_t instruction);
    DispatchStatus submitNpuCommand(const NpuCommand &command) override;

  private:
    void b_transport(tlm::tlm_generic_payload &transaction, sc_core::sc_time &delay);
    void trace_cpu_command();
    void trace_cpu_backpressure();
    static NpuConfig config_for_npu(const NpuConfig &base_config,
                                    uint8_t npu_id, uint8_t npu_count);

    tlm_utils::simple_target_socket<NpuCluster, 64> command_target;
    sc_gem5::TlmTargetWrapper<64> tlm_wrapper;
    gem5::Addr command_base = 0;
    sc_core::sc_time dispatch_delay;
    sc_core::sc_trace_file *trace_file = nullptr;
    uint64_t trace_cycle_ticks = 0;
    sc_core::sc_clock npu_clock;
    NpuClusterTraceSignals trace_signals;
    std::vector<std::unique_ptr<NpuTop>> npus;
};

} // namespace npu_mvp
