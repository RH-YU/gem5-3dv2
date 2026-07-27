#pragma once

#include "systemc/ext/core/sc_time.hh"

#include <cstdint>
#include <string>

namespace npu_mvp
{

enum class Opcode : uint8_t {
    Nsetvl = 0,
    Mte4 = 1,
    Mte2 = 2,
    Vload = 3,
    Vstore = 4,
    Vadd = 5,
    SyncSet = 6,
    SyncWait = 7,
    WriteDataToGm = 8,
    LoadDataFromGm = 9,
};

enum class Engine : uint8_t {
    Control,
    Mte4,
    Mte2,
    Vcu,
    GmFileIo,
};

enum class SyncScope : uint8_t {
    All = 0,
    MteAll = 1,
    Vcu = 2,
    Mte4 = 3,
    Mte2 = 4,
    GmFileIo = 5,
};

enum class SyncEndpoint : uint8_t {
    Mte4 = 0,
    Mte2 = 1,
    Vcu = 2,
    GmFileIo = 3,
};

enum class SubmitResult : uint8_t {
    Accepted,
    Backpressured,
    Invalid,
};

enum class DispatchStatus : uint8_t {
    Accepted,
    Backpressured,
    Invalid,
};

struct NpuCommand
{
    Opcode opcode = Opcode::Nsetvl;
    uint64_t pc = 0;
    uint32_t raw_instruction = 0;
    uint64_t rd_value = 0;
    uint64_t rs1_value = 0;
    uint64_t rs2_value = 0;
    uint8_t rd = 0;
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    uint8_t hart_id = 0;
    uint8_t npu_mask = 1;
    SyncEndpoint sync_src = SyncEndpoint::Mte4;
    SyncEndpoint sync_dst = SyncEndpoint::Mte4;
    uint8_t sync_id = 0;
    std::string sim_file_path;
    uint64_t gm_physical_address = 0;
    uint64_t file_byte_count = 0;
};

struct VcuContext
{
    uint64_t nvl = 0;
    uint8_t eew_bytes = 4;
};

struct NpuConfig
{
    uint8_t npu_id = 0;
    uint64_t gm_phys_base = 0x0000000000000000ULL;
    uint64_t gm_size = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    uint64_t gm_page_size = 4096;
    uint64_t ub_phys_base = 0x0000000100000000ULL;
    uint64_t ub_size = 512ULL * 1024ULL;
    uint64_t mte_max_transfer_bytes = 1024ULL * 1024ULL;
    uint32_t max_vl = 64;
    uint32_t vector_register_count = 32;
    uint32_t vector_register_bytes = 256;
    uint32_t scheduler_queue_depth = 32;
    uint32_t mte4_queue_depth = 32;
    uint32_t mte2_queue_depth = 32;
    uint32_t vcu_queue_depth = 32;
    uint32_t gm_file_io_queue_depth = 32;
    bool enable_sim_gm_file_io = false;
    std::string sim_gm_file_io_root;
    sc_core::sc_time scheduler_dispatch_delay = sc_core::sc_time(1, sc_core::SC_NS);
    sc_core::sc_time mte4_setup_delay = sc_core::sc_time(4, sc_core::SC_NS);
    sc_core::sc_time mte2_setup_delay = sc_core::sc_time(4, sc_core::SC_NS);
    sc_core::sc_time gm_file_io_setup_delay = sc_core::sc_time(1, sc_core::SC_NS);
    double mte4_bytes_per_ns = 16.0;
    double mte2_bytes_per_ns = 16.0;
    double gm_file_io_bytes_per_ns = 16.0;
    double vcu_bytes_per_ns = 16.0;
    double vadd_elements_per_ns = 16.0;
    std::string vcd_trace_file;
    uint64_t vcd_trace_cycle_ticks = 0;
};

} // namespace npu_mvp
