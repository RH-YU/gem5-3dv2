#include "dev/npu/npu_device.hh"

#include "base/logging.hh"
#include "dev/npu/npu_command.hh"
#include "params/NpuDevice.hh"

namespace
{

sc_core::sc_time
to_sc_time(gem5::Tick ticks)
{
    return sc_core::sc_time::from_value(ticks);
}

npu_mvp::NpuConfig
make_config(const gem5::NpuDeviceParams &params)
{
    npu_mvp::NpuConfig config;
    config.npu_id = params.npu_id;
    config.npu_command_base = params.npu_command_base;
    config.gm_phys_base = params.gm_phys_base;
    config.gm_size = params.gm_size;
    config.gm_page_size = params.gm_page_size;
    config.ub_phys_base = params.ub_phys_base;
    config.ub_size = params.ub_size;
    config.mte_max_transfer_bytes = params.mte_max_transfer_bytes;
    config.max_vl = params.max_vl;
    config.vector_register_count = params.vector_register_count;
    config.vector_register_bytes = params.vector_register_bytes;
    config.scheduler_queue_depth = params.scheduler_queue_depth;
    config.mte4_queue_depth = params.mte4_queue_depth;
    config.mte2_queue_depth = params.mte2_queue_depth;
    config.vcu_queue_depth = params.vcu_queue_depth;
    config.gm_file_io_queue_depth = params.gm_file_io_queue_depth;
    config.enable_sim_gm_file_io = params.enable_sim_gm_file_io;
    config.sim_gm_file_io_root = params.sim_gm_file_io_root;
    config.scheduler_dispatch_delay = to_sc_time(params.scheduler_dispatch_delay);
    config.mte4_setup_delay = to_sc_time(params.mte4_setup_delay);
    config.mte2_setup_delay = to_sc_time(params.mte2_setup_delay);
    config.gm_file_io_setup_delay = to_sc_time(params.gm_file_io_setup_delay);
    config.mte4_bytes_per_ns = params.mte4_bytes_per_ns;
    config.mte2_bytes_per_ns = params.mte2_bytes_per_ns;
    config.gm_file_io_bytes_per_ns = params.gm_file_io_bytes_per_ns;
    config.vcu_bytes_per_ns = params.vcu_bytes_per_ns;
    config.vadd_elements_per_ns = params.vadd_elements_per_ns;
    config.vcd_trace_file = params.vcd_trace_file;
    config.vcd_trace_cycle_ticks = params.vcd_trace_cycle_ticks;
    return config;
}

} // anonymous namespace

namespace npu_mvp
{

NpuDevice::NpuDevice(const char *name, const NpuConfig &config)
    : sc_core::sc_module(sc_core::sc_module_name(name)), npu("top", config),
      command_base(config.npu_command_base),
      tlm_wrapper(npu.command_target, std::string(name) + ".tlm",
                  gem5::InvalidPortID)
{
    registerNpuPacketConversionStep();
    if (command_base != 0)
        registerNpuCommandTarget(command_base, *this);
}

NpuDevice::~NpuDevice()
{
    if (command_base != 0)
        unregisterNpuCommandTarget(command_base, *this);
}

gem5::Port &
NpuDevice::gem5_getPort(const std::string &if_name, int idx)
{
    if (if_name == "tlm")
        return tlm_wrapper;

    fatal("NpuDevice has no port named %s[%d].", if_name, idx);
}

DispatchStatus
NpuDevice::submit_cpu_sync(const NpuCommand &command)
{
    if (npu.is_cpu_sync_set(command)) {
        npu.signal_cpu_sync(command);
        return DispatchStatus::Accepted;
    }

    if (npu.is_cpu_sync_wait(command)) {
        if (!npu.cpu_sync_ready(command))
            return DispatchStatus::Backpressured;
        npu.consume_cpu_sync(command);
        return DispatchStatus::Accepted;
    }

    return DispatchStatus::Invalid;
}

DispatchStatus
NpuDevice::submitNpuCommand(const NpuCommand &command)
{
    if (npu.is_cpu_sync_set(command) || npu.is_cpu_sync_wait(command))
        return submit_cpu_sync(command);

    if (!npu.can_accept(command))
        return DispatchStatus::Backpressured;

    const SubmitResult result = npu.submit(command);
    return result == SubmitResult::Invalid ? DispatchStatus::Invalid
                                           : DispatchStatus::Accepted;
}

NpuTop &
NpuDevice::top()
{
    return npu;
}

const NpuTop &
NpuDevice::top() const
{
    return npu;
}

} // namespace npu_mvp

namespace gem5
{

npu_mvp::NpuDevice *
NpuDeviceParams::create() const
{
    npu_mvp::registerNpuPacketConversionStep();
    return new npu_mvp::NpuDevice(name.c_str(), make_config(*this));
}

} // namespace gem5
