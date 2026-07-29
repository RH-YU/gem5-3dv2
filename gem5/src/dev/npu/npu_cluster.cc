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

NpuCluster::NpuCluster(sc_core::sc_module_name name, const NpuConfig &config,
                       uint8_t npu_count)
    : sc_core::sc_module(name),
      command_target("command_target"),
      tlm_wrapper(command_target, std::string(name) + ".tlm", gem5::InvalidPortID),
      command_base(config.npu_command_base),
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

    registerNpuPacketConversionStep();
    if (command_base != 0)
        registerNpuCommandTarget(command_base, *this);
    command_target.register_b_transport(this, &NpuCluster::b_transport);

    const std::string trace_basename =
            normalize_vcd_trace_basename(config.vcd_trace_file);
    if (!trace_basename.empty()) {
        trace_file = sc_core::sc_create_vcd_trace_file(trace_basename.c_str());
        configure_vcd_trace_time_unit(trace_file, trace_cycle_ticks);
        register_cpu_trace_signals(trace_file, trace_signals, "cpu");
        sc_core::sc_trace(trace_file, npu_clock, "cluster.npu_clock");
    }

    npus.reserve(npu_count);
    for (uint8_t npu_id = 0; npu_id < npu_count; ++npu_id) {
        const std::string npu_name = "npu" + std::to_string(npu_id);
        npus.push_back(std::make_unique<NpuTop>(
                npu_name.c_str(), config_for_npu(config, npu_id, npu_count)));
        npus.back()->register_trace(trace_file, npu_name);
    }
}

NpuCluster::~NpuCluster()
{
    if (command_base != 0)
        unregisterNpuCommandTarget(command_base, *this);
    if (trace_file != nullptr)
        sc_core::sc_close_vcd_trace_file(trace_file);
}

gem5::Port &
NpuCluster::gem5_getPort(const std::string &if_name, int idx)
{
    if (if_name == "tlm")
        return tlm_wrapper;

    fatal("NpuCluster has no port named %s[%d].", if_name, idx);
}

NpuConfig
NpuCluster::config_for_npu(const NpuConfig &base_config, uint8_t npu_id,
                           uint8_t npu_count)
{
    NpuConfig config = base_config;
    config.npu_id = npu_id;
    if (npu_count > 1 && !config.sim_gm_file_io_root.empty()) {
        config.sim_gm_file_io_root =
                (std::filesystem::path(config.sim_gm_file_io_root) /
                 ("npu" + std::to_string(npu_id))).string();
    }
    return config;
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

void
NpuCluster::b_transport(tlm::tlm_generic_payload &transaction,
                        sc_core::sc_time &delay)
{
    auto *extension = transaction.get_extension<NpuCommandExtension>();
    if (extension == nullptr) {
        transaction.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    const DispatchStatus status = submitNpuCommand(extension->command);
    if (extension->sender_state != nullptr)
        extension->sender_state->status = status;

    if (status == DispatchStatus::Backpressured) {
        transaction.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return;
    }
    if (status == DispatchStatus::Invalid) {
        transaction.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    transaction.set_response_status(tlm::TLM_OK_RESPONSE);
}

} // namespace npu_mvp

namespace gem5
{

npu_mvp::NpuCluster *
NpuClusterParams::create() const
{
    npu_mvp::registerNpuPacketConversionStep();
    return new npu_mvp::NpuCluster(name.c_str(), make_config(*this), npu_count);
}

} // namespace gem5
