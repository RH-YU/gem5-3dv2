#pragma once

#include "systemc/ext/core/sc_time.hh"

#include <cstdint>
#include <string>

namespace npu_mvp
{

enum class Opcode : uint8_t {
    Mte4 = 1,
    Mte2 = 2,
    Vcu = 3,
    Sync = 4,
    FileIo = 5,
    Mte1 = 6,
    Cube = 7,
    Fixpipe = 8,
    Niu = 9,
};

enum class Mte4Opcode : uint8_t {
    GmToUb = 0,
    GmToL1 = 1,
};

enum class Mte2Opcode : uint8_t {
    UbToGm = 0,
    UbToL1 = 1,
};

enum class Mte1Opcode : uint8_t {
    L1ToGm = 0,
    L1ToUb = 1,
    L1ToL0A = 2,
    L1ToL0B = 3,
};

enum class VcuOpcode : uint8_t {
    Nsetvl = 3,
    Load = 0,
    Store = 1,
    Add = 2,
};

enum class CubeOpcode : uint8_t {
    MmaFp32_8x16x16 = 0,
};

enum class FixpipeOpcode : uint8_t {
    L0CToL1 = 0,
    L0CToUb = 1,
};

enum class SyncOpcode : uint8_t {
    Set = 1,
    Wait = 2,
};

enum class FileIoOpcode : uint8_t {
    WriteDataToNpu = 0,
    LoadDataFromNpu = 1,
};

enum class NiuOpcode : uint8_t {
    UbToRemoteUb = 0,
    UbToRemoteGm = 1,
};

enum class Engine : uint8_t {
    Mte4,
    Mte1,
    Mte2,
    Vcu,
    Cube,
    Fixpipe,
    FileIo,
    Niu,
};

enum class SyncScope : uint8_t {
    All = 0,
    MteAll = 1,
    Vcu = 2,
    Mte4 = 3,
    Mte2 = 4,
    FileIo = 5,
    Mte1 = 6,
    Cube = 7,
    Fixpipe = 8,
    Niu = 9,
};

enum class SyncEndpoint : uint8_t {
    Mte4 = 0,
    Mte2 = 1,
    Vcu = 2,
    FileIo = 3,
    Cpu = 4,
    Mte1 = 5,
    Cube = 6,
    Fixpipe = 7,
    Niu = 8,
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
    Opcode opcode = Opcode::Vcu;
    uint8_t subopcode = static_cast<uint8_t>(VcuOpcode::Nsetvl);
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
    uint64_t storage_physical_address = 0;
    uint64_t file_byte_count = 0;
};

inline Mte4Opcode
as_mte4_opcode(const NpuCommand &command)
{
    return static_cast<Mte4Opcode>(command.subopcode);
}

inline Mte2Opcode
as_mte2_opcode(const NpuCommand &command)
{
    return static_cast<Mte2Opcode>(command.subopcode);
}

inline Mte1Opcode
as_mte1_opcode(const NpuCommand &command)
{
    return static_cast<Mte1Opcode>(command.subopcode);
}

inline VcuOpcode
as_vcu_opcode(const NpuCommand &command)
{
    return static_cast<VcuOpcode>(command.subopcode);
}

inline CubeOpcode
as_cube_opcode(const NpuCommand &command)
{
    return static_cast<CubeOpcode>(command.subopcode);
}

inline FixpipeOpcode
as_fixpipe_opcode(const NpuCommand &command)
{
    return static_cast<FixpipeOpcode>(command.subopcode);
}

inline SyncOpcode
as_sync_opcode(const NpuCommand &command)
{
    return static_cast<SyncOpcode>(command.subopcode);
}

inline FileIoOpcode
as_file_io_opcode(const NpuCommand &command)
{
    return static_cast<FileIoOpcode>(command.subopcode);
}

inline NiuOpcode
as_niu_opcode(const NpuCommand &command)
{
    return static_cast<NiuOpcode>(command.subopcode);
}

struct VcuContext
{
    uint64_t nvl = 0;
    uint8_t eew_bytes = 4;
};

struct NpuConfig
{
    uint8_t npu_id = 0;
    uint8_t npu_count = 1;
    uint64_t npu_dispatch_id = 0;
    uint64_t gm_phys_base = 0x0000000000000000ULL;
    uint64_t gm_size = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    uint64_t gm_page_size = 4096;
    uint64_t ub_phys_base = 0x0000000100000000ULL;
    uint64_t ub_size = 512ULL * 1024ULL;
    uint64_t l1_phys_base = 0x0000000100080000ULL;
    uint64_t l1_size = 512ULL * 1024ULL;
    uint64_t l0a_phys_base = 0x0000000100100000ULL;
    uint64_t l0a_size = 64ULL * 1024ULL;
    uint64_t l0b_phys_base = 0x0000000100110000ULL;
    uint64_t l0b_size = 64ULL * 1024ULL;
    uint64_t l0c_phys_base = 0x0000000100120000ULL;
    uint64_t l0c_size = 64ULL * 1024ULL;
    uint64_t mte_max_transfer_bytes = 1024ULL * 1024ULL;
    uint32_t max_vl = 64;
    uint32_t vector_register_count = 32;
    uint32_t vector_register_bytes = 256;
    uint32_t scheduler_queue_depth = 32;
    uint32_t mte4_queue_depth = 32;
    uint32_t mte1_queue_depth = 32;
    uint32_t mte2_queue_depth = 32;
    uint32_t vcu_queue_depth = 32;
    uint32_t cube_queue_depth = 32;
    uint32_t fixpipe_queue_depth = 32;
    uint32_t file_io_queue_depth = 32;
    uint32_t niu_queue_depth = 32;
    uint32_t niu_tx_queue_depth = 64;
    uint32_t niu_rx_queue_depth = 64;
    uint32_t noc_link_queue_depth = 16;
    uint32_t noc_packet_bytes = 128;
    uint32_t noc_link_latency_cycles = 1;
    uint32_t noc_bytes_per_cycle = 128;
    bool enable_sim_file_io = false;
    std::string sim_file_io_root;
    sc_core::sc_time scheduler_dispatch_delay = sc_core::sc_time(1, sc_core::SC_NS);
    sc_core::sc_time mte4_setup_delay = sc_core::sc_time(4, sc_core::SC_NS);
    sc_core::sc_time mte1_setup_delay = sc_core::sc_time(4, sc_core::SC_NS);
    sc_core::sc_time mte2_setup_delay = sc_core::sc_time(4, sc_core::SC_NS);
    sc_core::sc_time cube_setup_delay = sc_core::sc_time(1, sc_core::SC_NS);
    sc_core::sc_time fixpipe_setup_delay = sc_core::sc_time(1, sc_core::SC_NS);
    sc_core::sc_time file_io_setup_delay = sc_core::sc_time(1, sc_core::SC_NS);
    double mte4_bytes_per_ns = 16.0;
    double mte1_bytes_per_ns = 16.0;
    double mte2_bytes_per_ns = 16.0;
    double cube_fma_per_ns = 128.0;
    double fixpipe_bytes_per_ns = 16.0;
    double file_io_bytes_per_ns = 16.0;
    double vcu_bytes_per_ns = 16.0;
    double vadd_elements_per_ns = 16.0;
    std::string vcd_trace_file;
    uint64_t vcd_trace_cycle_ticks = 0;
};

} // namespace npu_mvp
