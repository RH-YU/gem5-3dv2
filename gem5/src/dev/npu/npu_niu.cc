#include "dev/npu/npu_niu.hh"

#include "dev/npu/npu_top.hh"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace npu_mvp
{

NiuTransfer
decode_niu_transfer(const NpuCommand &command, uint8_t local_npu_id)
{
    NiuTransfer transfer;
    transfer.source_npu_id = local_npu_id;
    transfer.target_npu_id =
            static_cast<uint8_t>((command.rs2_value >> 56) & 0xF);
    transfer.byte_count = command.rd_value;
    transfer.source_address = command.rs1_value;
    transfer.target_address = command.rs2_value & 0x00FFFFFFFFFFFFFFULL;
    return transfer;
}

void
NiuTraceState::register_trace(sc_core::sc_trace_file *tf,
                              const std::string &scope)
{
    trace_file = tf;
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.packet_sent_event,
                      scope + ".packet_sent_event");
    sc_core::sc_trace(trace_file, signals.packet_received_event,
                      scope + ".packet_received_event");
    sc_core::sc_trace(trace_file, signals.ack_event, scope + ".ack_event");
    sc_core::sc_trace(trace_file, signals.busy, scope + ".busy");
    sc_core::sc_trace(trace_file, signals.command_queue_size,
                      scope + ".command_queue_size");
    sc_core::sc_trace(trace_file, signals.tx_queue_size,
                      scope + ".tx_queue_size");
    sc_core::sc_trace(trace_file, signals.rx_queue_size,
                      scope + ".rx_queue_size");
    sc_core::sc_trace(trace_file, signals.instruction, scope + ".instruction");
}

void
NiuTraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
NiuTraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.busy = false;
    signals.instruction = 0;
}

void
NiuTraceState::trace_packet_sent()
{
    if (trace_file == nullptr)
        return;
    signals.packet_sent_event = true;
}

void
NiuTraceState::trace_packet_received()
{
    if (trace_file == nullptr)
        return;
    signals.packet_received_event = true;
}

void
NiuTraceState::clear_packet_sent()
{
    signals.packet_sent_event = false;
}

void
NiuTraceState::clear_packet_received()
{
    signals.packet_received_event = false;
}

void
NiuTraceState::trace_ack()
{
    if (trace_file == nullptr)
        return;
    signals.ack_event = !signals.ack_event;
}

void
NiuTraceState::trace_command_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;
    signals.command_queue_size = static_cast<uint32_t>(queue_size);
}

void
NiuTraceState::trace_tx_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;
    signals.tx_queue_size = static_cast<uint32_t>(queue_size);
}

void
NiuTraceState::trace_rx_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;
    signals.rx_queue_size = static_cast<uint32_t>(queue_size);
}

void
NiuState::trace_packet_sent_pulse(const sc_core::sc_time &pulse_width)
{
    trace.trace_packet_sent();
    packet_sent_clear_event.cancel();
    packet_sent_clear_event.notify(pulse_width);
}

void
NiuState::trace_packet_received_pulse(const sc_core::sc_time &pulse_width)
{
    trace.trace_packet_received();
    packet_received_clear_event.cancel();
    packet_received_clear_event.notify(pulse_width);
}

void
NiuState::clear_packet_sent_trace()
{
    trace.clear_packet_sent();
}

void
NiuState::clear_packet_received_trace()
{
    trace.clear_packet_received();
}

NpuTop::Region
NpuTop::niu_destination_region(const NpuCommand &command) const
{
    switch (as_niu_opcode(command)) {
      case NiuOpcode::UbToRemoteUb: return Region::Ub;
      case NiuOpcode::UbToRemoteGm: return Region::Gm;
    }
    throw std::invalid_argument("unsupported NIU opcode");
}

uint32_t
NpuTop::niu_packet_count(uint64_t byte_count) const
{
    if (byte_count == 0)
        throw std::invalid_argument("NIU byte count must be positive");
    if (config.noc_packet_bytes == 0 ||
        config.noc_packet_bytes > NiuPacket{}.payload.size())
        throw std::invalid_argument("NIU packet size must be in the range [1, 128]");

    return static_cast<uint32_t>(
            (byte_count + config.noc_packet_bytes - 1) /
            config.noc_packet_bytes);
}

NiuTransfer
decode_data_transfer(const NpuCommand &command, uint8_t local_npu_id)
{
    return decode_niu_transfer(command, local_npu_id);
}

NiuPacket
make_sync_packet(const NpuCommand &command, uint8_t local_npu_id,
                 uint64_t sequence)
{
    const RemoteSyncInfo remote = decode_remote_sync_info(command);
    NiuPacket packet;
    packet.kind = NiuPacket::Kind::Sync;
    packet.sequence = sequence;
    packet.packet_id = 0;
    packet.packet_count = 1;
    packet.source_npu_id = local_npu_id;
    packet.target_npu_id = remote.peer_npu_id;
    packet.sync_src = command.sync_src;
    packet.sync_dst = command.sync_dst;
    packet.sync_id = command.sync_id;
    packet.last = true;
    return packet;
}

void
NpuTop::enqueue_niu_packet(const NiuPacket &packet)
{
    while (niu.tx_queue.size() >= config.niu_tx_queue_depth)
        wait(niu.tx_space_event);

    niu.tx_queue.push_back(packet);
    niu.trace.trace_tx_queue_size(niu.tx_queue.size());
    niu.trace_packet_sent_pulse(sc_core::sc_time::from_value(
            active_cpu_cycle_ticks(config.vcd_trace_cycle_ticks)));
}

void
NpuTop::execute_niu(const ScheduledCommand &scheduled)
{
    if (noc == nullptr)
        throw std::runtime_error("NIU has no bound NOC");

    const NpuCommand &command = scheduled.command;
    if (as_sync_opcode(command) == SyncOpcode::RemoteSet) {
        const RemoteSyncInfo remote = decode_remote_sync_info(command);
        if (remote.peer_npu_id >= config.npu_count)
            throw std::out_of_range("remote sync target NPU id is outside cluster npu_count");

        niu.active_progress = NiuProgress{scheduled.sequence, 1, 0};
        enqueue_niu_packet(make_sync_packet(command, config.npu_id,
                                            scheduled.sequence));
        while (niu.active_progress.has_value() &&
               niu.active_progress->completed_packets < 1) {
            wait(niu.progress_event);
        }
        niu.active_progress.reset();
        return;
    }

    const NiuTransfer transfer = decode_data_transfer(command, config.npu_id);
    if (transfer.target_npu_id >= config.npu_count)
        throw std::out_of_range("NIU target NPU id is outside cluster npu_count");

    const uint32_t packet_count = niu_packet_count(transfer.byte_count);
    decode(transfer.source_address, transfer.byte_count, Region::Ub);
    decode(transfer.target_address, transfer.byte_count, niu_destination_region(command));

    niu.active_progress = NiuProgress{scheduled.sequence, packet_count, 0};

    for (uint32_t packet_id = 0; packet_id < packet_count; ++packet_id) {
        const uint64_t offset =
                static_cast<uint64_t>(packet_id) * config.noc_packet_bytes;
        const uint16_t bytes = static_cast<uint16_t>(std::min<uint64_t>(
                config.noc_packet_bytes, transfer.byte_count - offset));
        const auto decoded_source =
                decode(transfer.source_address + offset, bytes, Region::Ub);
        const std::vector<uint8_t> data =
                read(Region::Ub, decoded_source.local_address, bytes);

        NiuPacket packet;
        packet.kind = NiuPacket::Kind::Data;
        packet.sequence = scheduled.sequence;
        packet.packet_id = packet_id;
        packet.packet_count = packet_count;
        packet.source_npu_id = transfer.source_npu_id;
        packet.target_npu_id = transfer.target_npu_id;
        packet.opcode = as_niu_opcode(command);
        packet.target_address = transfer.target_address + offset;
        packet.payload_bytes = bytes;
        std::copy(data.begin(), data.end(), packet.payload.begin());
        packet.last = packet_id + 1 == packet_count;
        enqueue_niu_packet(packet);
    }

    while (niu.active_progress.has_value() &&
           niu.active_progress->completed_packets < packet_count) {
        wait(niu.progress_event);
    }
    niu.active_progress.reset();
}

void
NpuTop::niu_tx_thread()
{
    while (true) {
        wait(niu.event);
        while (!niu.queue.empty()) {
            ScheduledCommand command = std::move(niu.queue.front());
            niu.queue.pop_front();
            niu.trace.trace_command_queue_size(niu.queue.size());
            niu.busy = true;
            niu.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync &&
                as_sync_opcode(command.command) != SyncOpcode::RemoteSet) {
                execute_sync(command);
            } else {
                try {
                    execute_niu(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                    niu.active_progress.reset();
                }
            }
            niu.busy = false;
            niu.trace.trace_done();
            complete(command, Engine::Niu);
        }
    }
}

void
NpuTop::decode_niu_packet(const NiuPacket &packet)
{
    if (packet.kind == NiuPacket::Kind::Sync) {
        signal_sync_token(packet.sync_src, packet.sync_dst, packet.sync_id,
                          packet.source_npu_id);
        return;
    }

    const Region destination = packet.opcode == NiuOpcode::UbToRemoteGm
            ? Region::Gm
            : Region::Ub;
    const auto decoded =
            decode(packet.target_address, packet.payload_bytes, destination);
    const std::vector<uint8_t> data(packet.payload.begin(),
                                    packet.payload.begin() +
                                            packet.payload_bytes);
    write(destination, decoded.local_address, data);
}

void
NpuTop::niu_rx_thread()
{
    while (true) {
        wait(niu.rx_event);
        while (!niu.rx_queue.empty()) {
            NiuPacket packet = niu.rx_queue.front();
            niu.rx_queue.pop_front();
            niu.trace.trace_rx_queue_size(niu.rx_queue.size());
            bool wrote_packet = true;
            try {
                decode_niu_packet(packet);
            } catch (const std::exception &error) {
                wrote_packet = false;
                std::cout << "NPU[" << static_cast<unsigned>(config.npu_id)
                          << "] NIU RX fault sequence=" << packet.sequence
                          << " packet=" << packet.packet_id
                          << " message=\"" << error.what() << "\""
                          << std::endl;
            }
            niu.trace_packet_received_pulse(sc_core::sc_time::from_value(
                    active_cpu_cycle_ticks(config.vcd_trace_cycle_ticks)));
            if (wrote_packet && noc != nullptr)
                noc->ack_niu_packet(packet);
        }
    }
}

void
NpuTop::clear_niu_packet_sent_trace()
{
    niu.clear_packet_sent_trace();
}

void
NpuTop::clear_niu_packet_received_trace()
{
    niu.clear_packet_received_trace();
}

bool
NpuTop::niu_has_tx_packet() const
{
    return !niu.tx_queue.empty();
}

const NiuPacket &
NpuTop::peek_niu_tx_packet() const
{
    return niu.tx_queue.front();
}

void
NpuTop::pop_niu_tx_packet()
{
    niu.tx_queue.pop_front();
    niu.trace.trace_tx_queue_size(niu.tx_queue.size());
    niu.tx_space_event.notify(sc_core::SC_ZERO_TIME);
}

bool
NpuTop::can_receive_niu_packet() const
{
    return niu.rx_queue.size() < config.niu_rx_queue_depth;
}

void
NpuTop::receive_niu_packet(const NiuPacket &packet)
{
    niu.rx_queue.push_back(packet);
    niu.trace.trace_rx_queue_size(niu.rx_queue.size());
    niu.rx_event.notify(sc_core::SC_ZERO_TIME);
}

void
NpuTop::receive_niu_ack(uint64_t sequence, uint32_t packet_id)
{
    if (!niu.active_progress.has_value() ||
        niu.active_progress->sequence != sequence)
        return;

    ++niu.active_progress->completed_packets;
    niu.trace.trace_ack();
    niu.progress_event.notify(sc_core::SC_ZERO_TIME);
}

} // namespace npu_mvp
