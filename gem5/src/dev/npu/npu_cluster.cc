#include "dev/npu/npu_cluster.hh"

#include "base/logging.hh"
#include "dev/npu/npu_command.hh"
#include "params/NpuCluster.hh"
#include "systemc/ext/utils/sc_trace_file.hh"

#include <filesystem>

namespace
{

sc_core::sc_time
to_sc_time(gem5::Tick ticks)
{
    return sc_core::sc_time::from_value(ticks);
}

npu_mvp::NpuConfig
make_config(const gem5::NpuClusterParams &params)
{
    npu_mvp::NpuConfig config;
    config.npu_dispatch_id = params.npu_dispatch_id;
    config.gm_phys_base = params.gm_phys_base;
    config.gm_size = params.gm_size;
    config.gm_page_size = params.gm_page_size;
    config.ub_phys_base = params.ub_phys_base;
    config.ub_size = params.ub_size;
    config.l1_phys_base = params.l1_phys_base;
    config.l1_size = params.l1_size;
    config.l0a_phys_base = params.l0a_phys_base;
    config.l0a_size = params.l0a_size;
    config.l0b_phys_base = params.l0b_phys_base;
    config.l0b_size = params.l0b_size;
    config.l0c_phys_base = params.l0c_phys_base;
    config.l0c_size = params.l0c_size;
    config.mte_max_transfer_bytes = params.mte_max_transfer_bytes;
    config.max_vl = params.max_vl;
    config.vector_register_count = params.vector_register_count;
    config.vector_register_bytes = params.vector_register_bytes;
    config.scheduler_queue_depth = params.scheduler_queue_depth;
    config.mte4_queue_depth = params.mte4_queue_depth;
    config.mte1_queue_depth = params.mte1_queue_depth;
    config.mte2_queue_depth = params.mte2_queue_depth;
    config.vcu_queue_depth = params.vcu_queue_depth;
    config.cube_queue_depth = params.cube_queue_depth;
    config.fixpipe_queue_depth = params.fixpipe_queue_depth;
    config.file_io_queue_depth = params.file_io_queue_depth;
    config.niu_queue_depth = params.niu_queue_depth;
    config.niu_tx_queue_depth = params.niu_tx_queue_depth;
    config.niu_rx_queue_depth = params.niu_rx_queue_depth;
    config.noc_link_queue_depth = params.noc_link_queue_depth;
    config.noc_packet_bytes = params.noc_packet_bytes;
    config.noc_link_latency_cycles = params.noc_link_latency_cycles;
    config.noc_bytes_per_cycle = params.noc_bytes_per_cycle;
    config.enable_sim_file_io = params.enable_sim_file_io;
    config.sim_file_io_root = params.sim_file_io_root;
    config.scheduler_dispatch_delay = to_sc_time(params.scheduler_dispatch_delay);
    config.mte4_setup_delay = to_sc_time(params.mte4_setup_delay);
    config.mte1_setup_delay = to_sc_time(params.mte1_setup_delay);
    config.mte2_setup_delay = to_sc_time(params.mte2_setup_delay);
    config.cube_setup_delay = to_sc_time(params.cube_setup_delay);
    config.fixpipe_setup_delay = to_sc_time(params.fixpipe_setup_delay);
    config.file_io_setup_delay = to_sc_time(params.file_io_setup_delay);
    config.mte4_bytes_per_ns = params.mte4_bytes_per_ns;
    config.mte1_bytes_per_ns = params.mte1_bytes_per_ns;
    config.mte2_bytes_per_ns = params.mte2_bytes_per_ns;
    config.cube_fma_per_ns = params.cube_fma_per_ns;
    config.fixpipe_bytes_per_ns = params.fixpipe_bytes_per_ns;
    config.file_io_bytes_per_ns = params.file_io_bytes_per_ns;
    config.vcu_bytes_per_ns = params.vcu_bytes_per_ns;
    config.vadd_elements_per_ns = params.vadd_elements_per_ns;
    config.vcd_trace_file = params.vcd_trace_file;
    config.vcd_trace_cycle_ticks = params.vcd_trace_cycle_ticks;
    return config;
}

} // anonymous namespace

namespace npu_mvp
{

NpuCluster::NpuCluster(sc_core::sc_module_name name, const NpuConfig &config,
                       uint8_t npu_count)
    : sc_core::sc_module(name),
      dispatch_id(config.npu_dispatch_id),
      dispatch_delay(config.scheduler_dispatch_delay),
      trace_cycle_ticks(active_cpu_cycle_ticks(config.vcd_trace_cycle_ticks)),
      npu_clock("npu_clock",
                sc_core::sc_time::from_value(trace_cycle_ticks),
                0.5, sc_core::SC_ZERO_TIME, true)
{
    SC_METHOD(clear_cpu_commit_trace);
    dont_initialize();
    sensitive << cpu_commit_clear_event;

    if (npu_count == 0 || npu_count > 4)
        fatal("NpuCluster npu_count must be in the range [1, 4].");

    noc.configure(config, npu_count);

    SC_METHOD(noc_tick);
    dont_initialize();
    sensitive << npu_clock.pos();

    if (dispatch_id != 0)
        registerNpuCommandTarget(dispatch_id, *this);

    const std::string trace_basename =
            normalize_vcd_trace_basename(config.vcd_trace_file);
    if (!trace_basename.empty()) {
        trace_file = sc_core::sc_create_vcd_trace_file(trace_basename.c_str());
        configure_vcd_trace_time_unit(trace_file, trace_cycle_ticks);
        register_cpu_trace_signals(trace_file, trace_signals, "cpu");
        noc.register_trace(trace_file, "cluster.noc");
        sc_core::sc_trace(trace_file, npu_clock, "cluster.npu_clock");
    }

    npus.reserve(npu_count);
    for (uint8_t npu_id = 0; npu_id < npu_count; ++npu_id) {
        const std::string npu_name = "npu" + std::to_string(npu_id);
        npus.push_back(std::make_unique<NpuTop>(
                npu_name.c_str(), config_for_npu(config, npu_id, npu_count)));
        npus.back()->npu_clk(npu_clock);
        npus.back()->bind_noc(noc);
        noc.register_endpoint(npu_id, *npus.back());
        npus.back()->register_trace(trace_file, npu_name);
    }
}

NpuCluster::~NpuCluster()
{
    if (dispatch_id != 0)
        unregisterNpuCommandTarget(dispatch_id, *this);
    if (trace_file != nullptr)
        sc_core::sc_close_vcd_trace_file(trace_file);
}

NpuConfig
NpuCluster::config_for_npu(const NpuConfig &base_config, uint8_t npu_id,
                           uint8_t npu_count)
{
    NpuConfig config = base_config;
    config.npu_id = npu_id;
    config.npu_count = npu_count;
    if (npu_count > 1 && !config.sim_file_io_root.empty()) {
        config.sim_file_io_root =
                (std::filesystem::path(config.sim_file_io_root) /
                 ("npu" + std::to_string(npu_id))).string();
    }
    return config;
}

void
NpuCluster::noc_tick()
{
    noc.tick();
}

void
NpuCluster::record_cpu_commit(uint32_t pc, uint32_t instruction)
{
    if (trace_file == nullptr)
        return;

    trace_signals.cpu_commit_event = !trace_signals.cpu_commit_event;
    trace_signals.cpu_commit_valid = true;
    trace_signals.cpu_commit_pc = pc;
    trace_signals.cpu_commit_insn = instruction;

    cpu_commit_clear_event.cancel();
    cpu_commit_clear_event.notify(sc_core::sc_time::from_value(trace_cycle_ticks));
}

void
NpuCluster::clear_cpu_commit_trace()
{
    trace_signals.cpu_commit_valid = false;
}

void
NpuCluster::trace_cpu_command()
{
    if (trace_file == nullptr)
        return;

    trace_signals.cpu_cmd_event = !trace_signals.cpu_cmd_event;
}

void
NpuCluster::trace_cpu_backpressure()
{
    if (trace_file == nullptr)
        return;

    trace_signals.cpu_backpressure_event = !trace_signals.cpu_backpressure_event;
}

DispatchStatus
NpuCluster::submit_cpu_sync(const NpuCommand &command)
{
    if (npus.empty())
        return DispatchStatus::Invalid;

    if (npus.front()->is_cpu_sync_set(command)) {
        for (const auto &npu : npus)
            npu->signal_cpu_sync(command);
        return DispatchStatus::Accepted;
    }

    if (npus.front()->is_cpu_sync_wait(command)) {
        for (const auto &npu : npus) {
            if (!npu->cpu_sync_ready(command))
                return DispatchStatus::Backpressured;
        }
        for (const auto &npu : npus)
            npu->consume_cpu_sync(command);
        return DispatchStatus::Accepted;
    }

    return DispatchStatus::Invalid;
}

DispatchStatus
NpuCluster::submitNpuCommand(const NpuCommand &command)
{
    trace_cpu_command();
    if (!npus.empty() && (npus.front()->is_cpu_sync_set(command) ||
                         npus.front()->is_cpu_sync_wait(command))) {
        const DispatchStatus status = submit_cpu_sync(command);
        if (status == DispatchStatus::Backpressured)
            trace_cpu_backpressure();
        return status;
    }

    bool backpressured = false;
    bool invalid = false;
    for (const auto &npu : npus) {
        if (!npu->can_accept(command)) {
            backpressured = true;
            break;
        }
    }

    if (backpressured) {
        trace_cpu_backpressure();
        return DispatchStatus::Backpressured;
    }

    for (const auto &npu : npus) {
        const SubmitResult result = npu->submit(command);
        invalid = invalid || result == SubmitResult::Invalid;
    }

    return invalid ? DispatchStatus::Invalid : DispatchStatus::Accepted;
}

} // namespace npu_mvp

namespace gem5
{

npu_mvp::NpuCluster *
NpuClusterParams::create() const
{
    return new npu_mvp::NpuCluster(name.c_str(), make_config(*this), npu_count);
}

} // namespace gem5
