#pragma once

#include "dev/npu/npu_command.hh"
#include "dev/npu/npu_top.hh"
#include "dev/npu/npu_trace.hh"

#include <memory>
#include <string>
#include <vector>

#include "systemc/ext/channel/sc_clock.hh"
#include "systemc/ext/core/sc_event.hh"
#include "systemc/ext/core/sc_module.hh"
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

    void record_cpu_commit(uint32_t pc, uint32_t instruction);
    DispatchStatus submitNpuCommand(const NpuCommand &command) override;

  private:
    void clear_cpu_commit_trace();
    void trace_cpu_command();
    void trace_cpu_backpressure();
    static NpuConfig config_for_npu(const NpuConfig &base_config,
                                    uint8_t npu_id, uint8_t npu_count);
    DispatchStatus submit_cpu_sync(const NpuCommand &command);

    uint64_t dispatch_id = 0;
    sc_core::sc_time dispatch_delay;
    sc_core::sc_trace_file *trace_file = nullptr;
    uint64_t trace_cycle_ticks = 0;
    sc_core::sc_clock npu_clock;
    sc_core::sc_event cpu_commit_clear_event;
    NpuClusterTraceSignals trace_signals;
    std::vector<std::unique_ptr<NpuTop>> npus;
};

} // namespace npu_mvp
