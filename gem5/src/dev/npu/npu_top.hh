#pragma once

#include "dev/npu/npu_command.hh"
#include "dev/npu/npu_gm_file_io.hh"
#include "dev/npu/npu_mte.hh"
#include "dev/npu/npu_scheduler.hh"
#include "dev/npu/npu_storage.hh"
#include "dev/npu/npu_sync.hh"
#include "dev/npu/npu_trace.hh"
#include "dev/npu/npu_types.hh"
#include "dev/npu/npu_vcu.hh"

#include <cstdint>
#include <string>
#include <vector>

#include "systemc/ext/core/sc_event.hh"
#include "systemc/ext/core/sc_module.hh"
#include "systemc/ext/core/sc_module_name.hh"
#include "systemc/ext/tlm_utils/simple_target_socket.h"

namespace npu_mvp
{

class NpuTop : public sc_core::sc_module
{
  public:
    SC_HAS_PROCESS(NpuTop);

    NpuTop(sc_core::sc_module_name name, NpuConfig config);

    tlm_utils::simple_target_socket_optional<NpuTop, 64> command_target;

    bool can_accept(const NpuCommand &command) const;
    SubmitResult submit(const NpuCommand &command);
    void register_trace(sc_core::sc_trace_file *trace_file,
                        const std::string &scope);
    uint64_t scope_watermark() const;
    bool scope_complete(SyncScope scope, uint64_t watermark) const;
    uint64_t fault_count() const;
    const std::string &last_fault() const;

    void write_gm_for_test(uint64_t address, const std::vector<uint8_t> &data);
    std::vector<uint8_t> read_gm_for_test(uint64_t address, uint64_t byte_count) const;

  private:
    enum class Region : uint8_t { Gm, Ub };

    struct DecodedAddress {
        Region region;
        uint64_t local_address;
    };

    // Command target and scheduler core.
    void dispatch_ingress();
    void b_transport(tlm::tlm_generic_payload &transaction, sc_core::sc_time &delay);
    bool dispatch_one(const NpuCommand &command);
    bool enqueue_scheduled(Engine engine, ScheduledCommand &&command);
    void trace_command(const NpuCommand &command) const;
    void trace_ingress();
    void trace_dispatch();
    void trace_sync_start(const NpuCommand &command);
    void trace_sync_done();
    void trace_engine_start(Engine engine, uint32_t raw_instruction);
    void trace_engine_done(Engine engine);
    void trace_fault();
    bool engine_has_space(Engine engine) const;
    Engine sync_route_engine(const NpuCommand &command) const;
    bool sync_complete_for_command(const NpuCommand &command) const;
    bool scope_includes(SyncScope scope, Engine engine) const;
    void execute_sync(const ScheduledCommand &command);
    void signal_sync_token(const NpuCommand &command);
    void wait_for_sync_token(const NpuCommand &command);
    void complete(const ScheduledCommand &command, Engine engine);
    void fault(const ScheduledCommand &command, const std::string &message);
    VcuContext &vcu_context_for(uint8_t hart_id);
    static uint8_t decode_eew_bytes(uint64_t encoding);

    // NPU private storage and address decoding.
    DecodedAddress decode(uint64_t address, uint64_t byte_count, Region expected) const;
    std::vector<uint8_t> read(Region region, uint64_t local_address, uint64_t byte_count) const;
    void write(Region region, uint64_t local_address, const std::vector<uint8_t> &data);

    // MTE4/MTE2 engines.
    void mte4_thread();
    void mte2_thread();
    void execute_mte(const ScheduledCommand &command, Region source, Region destination);

    // VCU engine.
    void vcu_thread();
    void execute_vcu(const ScheduledCommand &command);
    static std::vector<uint8_t> read_vcu_ub(void *owner, uint64_t address,
                                            uint64_t byte_count);
    static void write_vcu_ub(void *owner, uint64_t address,
                             const std::vector<uint8_t> &data);

    // Simulator-only GM file I/O engine.
    void gm_file_io_thread();
    void WriteDataToGm(const ScheduledCommand &command);
    void LoadDataFromGm(const ScheduledCommand &command);

    // Timing helpers.
    sc_core::sc_time transfer_delay(uint64_t byte_count, double bytes_per_ns,
                                    const sc_core::sc_time &setup) const;

    NpuConfig config;
    SparseMemory gm;
    FlatMemory ub;
    SchedulerState scheduler;
    MteEngineState mte4;
    MteEngineState mte2;
    VcuState vcu;
    GmFileIoState gm_file_io;
    SyncState sync;
    sc_core::sc_trace_file *trace_file = nullptr;
    NpuTraceSignals trace_signals;
};

} // namespace npu_mvp
