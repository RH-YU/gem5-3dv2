#include "dev/npu/npu_noc.hh"

#include <algorithm>
#include <stdexcept>

#include "systemc/ext/utils/sc_trace_file.hh"

namespace npu_mvp
{

void
NocState::configure(const NpuConfig &config, uint8_t count)
{
    if (count == 0 || count > endpoints.size())
        throw std::invalid_argument("NOC npu_count must be in the range [1, 4]");
    if (config.noc_packet_bytes == 0 ||
        config.noc_packet_bytes > NiuPacket{}.payload.size())
        throw std::invalid_argument("NOC packet bytes must be in the range [1, 128]");
    if (config.noc_link_queue_depth == 0)
        throw std::invalid_argument("NOC link queue depth must be positive");
    if (config.noc_bytes_per_cycle == 0)
        throw std::invalid_argument("NOC bytes per cycle must be positive");

    npu_count = count;
    link_queue_depth = config.noc_link_queue_depth;
    packet_bytes = config.noc_packet_bytes;
    link_latency_cycles = config.noc_link_latency_cycles;
    bytes_per_cycle = config.noc_bytes_per_cycle;
}

void
NocState::register_endpoint(uint8_t npu_id, NocEndpoint &endpoint)
{
    if (npu_id >= npu_count)
        throw std::out_of_range("NOC endpoint id is outside npu_count");
    endpoints[npu_id] = &endpoint;
}

void
NocState::register_trace(sc_core::sc_trace_file *tf,
                         const std::string &scope)
{
    trace_file = tf;
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, trace_signals.inject_event,
                      scope + ".inject_event");
    sc_core::sc_trace(trace_file, trace_signals.forward_event,
                      scope + ".forward_event");
    sc_core::sc_trace(trace_file, trace_signals.deliver_event,
                      scope + ".deliver_event");
    sc_core::sc_trace(trace_file, trace_signals.ack_event,
                      scope + ".ack_event");
    sc_core::sc_trace(trace_file, trace_signals.block_event,
                      scope + ".block_event");
    sc_core::sc_trace(trace_file, trace_signals.cw0_queue_size,
                      scope + ".cw0_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.cw1_queue_size,
                      scope + ".cw1_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.cw2_queue_size,
                      scope + ".cw2_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.cw3_queue_size,
                      scope + ".cw3_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.ccw0_queue_size,
                      scope + ".ccw0_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.ccw1_queue_size,
                      scope + ".ccw1_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.ccw2_queue_size,
                      scope + ".ccw2_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.ccw3_queue_size,
                      scope + ".ccw3_queue_size");
    sc_core::sc_trace(trace_file, trace_signals.last_source,
                      scope + ".last_source");
    sc_core::sc_trace(trace_file, trace_signals.last_target,
                      scope + ".last_target");
    sc_core::sc_trace(trace_file, trace_signals.last_direction,
                      scope + ".last_direction");
    sc_core::sc_trace(trace_file, trace_signals.last_hops,
                      scope + ".last_hops");
    sc_core::sc_trace(trace_file, trace_signals.last_packet_bytes,
                      scope + ".last_packet_bytes");
}

void
NocState::tick()
{
    if (npu_count == 0)
        return;

    advance_links(clockwise_links, NocDirection::Clockwise);
    advance_links(counter_clockwise_links, NocDirection::CounterClockwise);
    try_inject(NocDirection::Clockwise);
    try_inject(NocDirection::CounterClockwise);
    trace_link_sizes();
}

void
NocState::ack_niu_packet(const NiuPacket &packet)
{
    if (packet.source_npu_id >= npu_count ||
        endpoints[packet.source_npu_id] == nullptr)
        return;

    endpoints[packet.source_npu_id]->receive_niu_ack(packet.sequence,
                                                     packet.packet_id);
    if (trace_file != nullptr)
        trace_signals.ack_event = !trace_signals.ack_event;
}

NocDirection
NocState::route_direction(uint8_t source, uint8_t target) const
{
    const uint8_t cw_hops =
            static_cast<uint8_t>((target + npu_count - source) % npu_count);
    const uint8_t ccw_hops =
            static_cast<uint8_t>((source + npu_count - target) % npu_count);
    return cw_hops <= ccw_hops ? NocDirection::Clockwise
                               : NocDirection::CounterClockwise;
}

uint8_t
NocState::hop_count(uint8_t source, uint8_t target,
                    NocDirection direction) const
{
    if (direction == NocDirection::Clockwise)
        return static_cast<uint8_t>((target + npu_count - source) % npu_count);
    return static_cast<uint8_t>((source + npu_count - target) % npu_count);
}

uint8_t
NocState::next_node(uint8_t node, NocDirection direction) const
{
    if (direction == NocDirection::Clockwise)
        return static_cast<uint8_t>((node + 1) % npu_count);
    return static_cast<uint8_t>((node + npu_count - 1) % npu_count);
}

uint64_t
NocState::link_cycles(uint16_t payload_bytes) const
{
    const uint64_t transfer_cycles =
            (static_cast<uint64_t>(payload_bytes) + bytes_per_cycle - 1) /
            bytes_per_cycle;
    return std::max<uint64_t>(1, link_latency_cycles + transfer_cycles);
}

bool
NocState::link_has_space(const LinkQueues &links, uint8_t link) const
{
    return links[link].size() < link_queue_depth;
}

void
NocState::push_link(LinkQueues &links, uint8_t link, const NiuPacket &packet)
{
    links[link].push_back(
            NocLinkEntry{packet, link, link_cycles(packet.payload_bytes),
                         true});
}

void
NocState::advance_links(LinkQueues &links, NocDirection direction)
{
    for (uint8_t link = 0; link < npu_count; ++link) {
        for (auto &entry : links[link]) {
            entry.moved_this_tick = false;
            if (entry.remaining_cycles > 0)
                --entry.remaining_cycles;
        }
    }

    for (uint8_t link = 0; link < npu_count; ++link) {
        if (links[link].empty())
            continue;

        auto &entry = links[link].front();
        if (entry.moved_this_tick || entry.remaining_cycles > 0)
            continue;

        const uint8_t node = next_node(entry.current_node, direction);
        if (node == entry.packet.target_npu_id) {
            auto *endpoint = endpoints[node];
            if (endpoint != nullptr && endpoint->can_receive_niu_packet()) {
                endpoint->receive_niu_packet(entry.packet);
                links[link].pop_front();
                if (trace_file != nullptr)
                    trace_signals.deliver_event =
                            !trace_signals.deliver_event;
            } else if (trace_file != nullptr) {
                trace_signals.block_event = !trace_signals.block_event;
            }
            continue;
        }

        if (link_has_space(links, node)) {
            NiuPacket packet = entry.packet;
            links[link].pop_front();
            push_link(links, node, packet);
            if (trace_file != nullptr)
                trace_signals.forward_event = !trace_signals.forward_event;
        } else if (trace_file != nullptr) {
            trace_signals.block_event = !trace_signals.block_event;
        }
    }
}

void
NocState::try_inject(NocDirection direction)
{
    for (uint8_t probe = 0; probe < npu_count; ++probe) {
        const uint8_t source =
                static_cast<uint8_t>((last_grant_source + 1 + probe) %
                                     npu_count);
        auto *endpoint = endpoints[source];
        if (endpoint == nullptr || !endpoint->niu_has_tx_packet())
            continue;

        const NiuPacket &packet = endpoint->peek_niu_tx_packet();
        if (packet.source_npu_id != source ||
            packet.target_npu_id >= npu_count ||
            route_direction(packet.source_npu_id, packet.target_npu_id) !=
                    direction) {
            continue;
        }

        const uint8_t hops = hop_count(packet.source_npu_id,
                                       packet.target_npu_id, direction);
        if (hops == 0) {
            if (endpoint->can_receive_niu_packet()) {
                endpoint->receive_niu_packet(packet);
                endpoint->pop_niu_tx_packet();
                last_grant_source = source;
                trace_packet(packet, direction, hops, packet.payload_bytes);
            }
            return;
        }

        LinkQueues &links = direction == NocDirection::Clockwise
                ? clockwise_links
                : counter_clockwise_links;
        const uint8_t first_link = packet.source_npu_id;
        if (!link_has_space(links, first_link)) {
            if (trace_file != nullptr)
                trace_signals.block_event = !trace_signals.block_event;
            return;
        }

        push_link(links, first_link, packet);
        endpoint->pop_niu_tx_packet();
        last_grant_source = source;
        trace_packet(packet, direction, hops, packet.payload_bytes);
        return;
    }
}

void
NocState::trace_link_sizes()
{
    if (trace_file == nullptr)
        return;

    trace_signals.cw0_queue_size = clockwise_links[0].size();
    trace_signals.cw1_queue_size = clockwise_links[1].size();
    trace_signals.cw2_queue_size = clockwise_links[2].size();
    trace_signals.cw3_queue_size = clockwise_links[3].size();
    trace_signals.ccw0_queue_size = counter_clockwise_links[0].size();
    trace_signals.ccw1_queue_size = counter_clockwise_links[1].size();
    trace_signals.ccw2_queue_size = counter_clockwise_links[2].size();
    trace_signals.ccw3_queue_size = counter_clockwise_links[3].size();
}

void
NocState::trace_packet(const NiuPacket &packet, NocDirection direction,
                       uint8_t hops, uint16_t bytes)
{
    if (trace_file == nullptr)
        return;

    trace_signals.inject_event = !trace_signals.inject_event;
    trace_signals.last_source = packet.source_npu_id;
    trace_signals.last_target = packet.target_npu_id;
    trace_signals.last_direction = static_cast<uint8_t>(direction);
    trace_signals.last_hops = hops;
    trace_signals.last_packet_bytes = bytes;
}

} // namespace npu_mvp
