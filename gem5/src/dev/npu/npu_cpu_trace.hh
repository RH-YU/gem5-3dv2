#pragma once

#include <cstdint>

#include "sim/probe/probe.hh"

namespace gem5
{

struct NpuCpuVcdProbeParams;
struct RetiredInstRecord;

} // namespace gem5

namespace npu_mvp
{

class NpuCluster;

class NpuCpuVcdProbe : public gem5::ProbeListenerObject
{
  public:
    explicit NpuCpuVcdProbe(const gem5::NpuCpuVcdProbeParams &params);

    void regProbeListeners() override;

  private:
    void trace_retired_inst(const gem5::RetiredInstRecord &record);

    NpuCluster *cluster = nullptr;
};

} // namespace npu_mvp
