#include "dev/npu/npu_cpu_trace.hh"

#include "cpu/base.hh"
#include "dev/npu/npu_cluster.hh"
#include "params/NpuCpuVcdProbe.hh"

namespace npu_mvp
{

NpuCpuVcdProbe::NpuCpuVcdProbe(
        const gem5::NpuCpuVcdProbeParams &params)
    : gem5::ProbeListenerObject(params),
      cluster(params.cluster)
{
}

void
NpuCpuVcdProbe::regProbeListeners()
{
    listeners.push_back(new gem5::ProbeListenerArg<NpuCpuVcdProbe,
            gem5::RetiredInstRecord>(this, "RetiredInstRecord",
            &NpuCpuVcdProbe::trace_retired_inst));
}

void
NpuCpuVcdProbe::trace_retired_inst(const gem5::RetiredInstRecord &record)
{
    if (cluster == nullptr)
        return;

    cluster->record_cpu_commit(record.pc, record.instruction);
}

} // namespace npu_mvp
