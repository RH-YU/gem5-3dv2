#pragma once

#include "dev/npu/npu_scheduler.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "systemc/ext/core/sc_event.hh"
#include "systemc/ext/core/sc_time.hh"
#include "systemc/ext/dt/bit/sc_bv.hh"
#include "systemc/ext/dt/int/sc_uint.hh"
#include "systemc/ext/utils/sc_trace_file.hh"

namespace npu_mvp
{

struct NiuPacket
{
    enum class Kind : uint8_t {
        Data = 0,
        Sync = 1,
    };

    Kind kind = Kind::Data;
    uint64_t sequence = 0;
    uint32_t packet_id = 0;
    uint32_t packet_count = 0;
    uint8_t source_npu_id = 0;
    uint8_t target_npu_id = 0;
    NiuOpcode opcode = NiuOpcode::UbToRemoteUb;
    uint64_t target_address = 0;
    uint16_t payload_bytes = 0;
    std::array<uint8_t, 128> payload = {};
    bool last = false;
    SyncEndpoint sync_src = SyncEndpoint::Mte4;
    SyncEndpoint sync_dst = SyncEndpoint::Mte4;
    uint8_t sync_id = 0;
};

struct NiuTransfer
{
    uint8_t source_npu_id = 0;
    uint8_t target_npu_id = 0;
    uint64_t byte_count = 0;
    uint64_t source_address = 0;
    uint64_t target_address = 0;
};

NiuTransfer decode_niu_transfer(const NpuCommand &command,
                                uint8_t local_npu_id);

struct NiuTraceSignals
{
    bool packet_sent_event = false;
    bool packet_received_event = false;
    bool ack_event = false;
    bool busy = false;
    sc_dt::sc_uint<32> command_queue_size = 0;
    sc_dt::sc_uint<32> tx_queue_size = 0;
    sc_dt::sc_uint<32> rx_queue_size = 0;
    sc_dt::sc_bv<32> instruction = sc_dt::sc_bv<32>(0);
};

struct NiuTraceState
{
    void register_trace(sc_core::sc_trace_file *trace_file,
                        const std::string &scope);
    void trace_start(uint32_t raw_instruction);
    void trace_done();
    void trace_packet_sent();
    void trace_packet_received();
    void clear_packet_sent();
    void clear_packet_received();
    void trace_ack();
    void trace_command_queue_size(std::size_t queue_size);
    void trace_tx_queue_size(std::size_t queue_size);
    void trace_rx_queue_size(std::size_t queue_size);

    sc_core::sc_trace_file *trace_file = nullptr;
    NiuTraceSignals signals;
};

struct NiuProgress
{
    uint64_t sequence = 0;
    uint32_t packet_count = 0;
    uint32_t completed_packets = 0;
};

struct NiuState
{
    void trace_packet_sent_pulse(const sc_core::sc_time &pulse_width);
    void trace_packet_received_pulse(const sc_core::sc_time &pulse_width);
    void clear_packet_sent_trace();
    void clear_packet_received_trace();

    std::deque<ScheduledCommand> queue;
    std::deque<NiuPacket> tx_queue;
    std::deque<NiuPacket> rx_queue;
    std::optional<NiuProgress> active_progress;
    bool busy = false;
    sc_core::sc_event event;
    sc_core::sc_event tx_space_event;
    sc_core::sc_event rx_event;
    sc_core::sc_event progress_event;
    sc_core::sc_event packet_sent_clear_event;
    sc_core::sc_event packet_received_clear_event;
    NiuTraceState trace;
};

} // namespace npu_mvp
