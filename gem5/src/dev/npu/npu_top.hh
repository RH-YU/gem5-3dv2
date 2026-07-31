#pragma once

#include "dev/npu/npu_command.hh"
#include "dev/npu/npu_cube.hh"
#include "dev/npu/npu_fixpipe.hh"
#include "dev/npu/npu_file_io.hh"
#include "dev/npu/npu_mte1.hh"
#include "dev/npu/npu_mte2.hh"
#include "dev/npu/npu_mte4.hh"
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

namespace npu_mvp
{

class NpuTop : public sc_core::sc_module
{
  public:
    SC_HAS_PROCESS(NpuTop);

    NpuTop(sc_core::sc_module_name name, NpuConfig config);

    bool can_accept(const NpuCommand &command) const;
    SubmitResult submit(const NpuCommand &command);
    bool is_cpu_sync_set(const NpuCommand &command) const;
    bool is_cpu_sync_wait(const NpuCommand &command) const;
    void signal_cpu_sync(const NpuCommand &command);
    bool cpu_sync_ready(const NpuCommand &command) const;
    void consume_cpu_sync(const NpuCommand &command);
    void register_trace(sc_core::sc_trace_file *trace_file,
                        const std::string &scope);
    uint64_t scope_watermark() const;
    bool scope_complete(SyncScope scope, uint64_t watermark) const;
    uint64_t fault_count() const;
    const std::string &last_fault() const;

    void write_gm_for_test(uint64_t address, const std::vector<uint8_t> &data);
    std::vector<uint8_t> read_gm_for_test(uint64_t address, uint64_t byte_count) const;

  private:
    enum class Region : uint8_t { Gm, Ub, L1, L0A, L0B, L0C };

    struct DecodedAddress {
        Region region;
        uint64_t local_address;
    };

    // Command target and scheduler core.
    void dispatch_ingress();
    bool dispatch_one(const NpuCommand &command);
    bool enqueue_scheduled(Engine engine, ScheduledCommand &&command);
    Engine route_engine(const NpuCommand &command) const;
    void trace_command(const NpuCommand &command) const;
    void trace_ingress();
    void trace_dispatch();
    void trace_sync_start(const NpuCommand &command);
    void trace_sync_done();
    void trace_fault();
    void trace_scheduler_queue_size();
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
    DecodedAddress decode_any(uint64_t address, uint64_t byte_count) const;
    const char *storage_region_name(const NpuCommand &command) const;
    static const char *region_name(Region region);
    static bool can_write_data_to_region(Region region);
    static bool can_load_data_from_region(Region region);
    std::vector<uint8_t> read(Region region, uint64_t local_address, uint64_t byte_count) const;
    void write(Region region, uint64_t local_address, const std::vector<uint8_t> &data);

    // MTE4/MTE2 engines.
    void mte4_thread();
    void mte1_thread();
    void mte2_thread();
    void execute_mte(const ScheduledCommand &command, Region source, Region destination);
    void execute_mte4(const ScheduledCommand &command);
    void execute_mte1(const ScheduledCommand &command);
    void execute_mte2(const ScheduledCommand &command);

    // VCU engine.
    void vcu_thread();
    void execute_vcu_nsetvl(const ScheduledCommand &command);
    void execute_vcu(const ScheduledCommand &command);
    static std::vector<uint8_t> read_vcu_ub(void *owner, uint64_t address,
                                            uint64_t byte_count);
    static void write_vcu_ub(void *owner, uint64_t address,
                             const std::vector<uint8_t> &data);

    // Cube engine.
    void cube_thread();
    void execute_cube(const ScheduledCommand &command);

    // Fixpipe engine.
    void fixpipe_thread();
    void execute_fixpipe(const ScheduledCommand &command);

    // Simulator-only file I/O engine.
    void file_io_thread();
    void WriteDataToNpu(const ScheduledCommand &command);
    void LoadDataFromNpu(const ScheduledCommand &command);

    // Timing helpers.
    sc_core::sc_time transfer_delay(uint64_t byte_count, double bytes_per_ns,
                                    const sc_core::sc_time &setup) const;

    NpuConfig config;
    SparseMemory gm;
    FlatMemory ub;
    FlatMemory l1;
    FlatMemory l0a;
    FlatMemory l0b;
    FlatMemory l0c;
    SchedulerState scheduler;
    Mte4State mte4;
    Mte1State mte1;
    Mte2State mte2;
    VcuState vcu;
    CubeState cube;
    FixpipeState fixpipe;
    FileIoState file_io;
    SyncState sync;
    sc_core::sc_trace_file *trace_file = nullptr;
    NpuTraceSignals trace_signals;
};

} // namespace npu_mvp
