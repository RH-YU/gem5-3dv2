#pragma once

#include "dev/npu/npu_niu.hh"
#include "dev/npu/npu_types.hh"

#include <array>
#include <cstdint>
#include <deque>
#include <string>

#include "systemc/ext/dt/int/sc_uint.hh"
#include "systemc/ext/utils/sc_trace_file.hh"

namespace npu_mvp
{

enum class NocDirection : uint8_t {
    Clockwise = 0,
    CounterClockwise = 1,
};

class NocEndpoint
{
  public:
    virtual ~NocEndpoint() = default;

    virtual bool niu_has_tx_packet() const = 0;
    virtual const NiuPacket &peek_niu_tx_packet() const = 0;
    virtual void pop_niu_tx_packet() = 0;
    virtual bool can_receive_niu_packet() const = 0;
    virtual void receive_niu_packet(const NiuPacket &packet) = 0;
    virtual void receive_niu_ack(uint64_t sequence, uint32_t packet_id) = 0;
};

struct NocTraceSignals
{
    bool inject_event = false;
    bool forward_event = false;
    bool deliver_event = false;
    bool ack_event = false;
    bool block_event = false;
    sc_dt::sc_uint<32> cw0_queue_size = 0;
    sc_dt::sc_uint<32> cw1_queue_size = 0;
    sc_dt::sc_uint<32> cw2_queue_size = 0;
    sc_dt::sc_uint<32> cw3_queue_size = 0;
    sc_dt::sc_uint<32> ccw0_queue_size = 0;
    sc_dt::sc_uint<32> ccw1_queue_size = 0;
    sc_dt::sc_uint<32> ccw2_queue_size = 0;
    sc_dt::sc_uint<32> ccw3_queue_size = 0;
    sc_dt::sc_uint<8> last_source = 0;
    sc_dt::sc_uint<8> last_target = 0;
    sc_dt::sc_uint<8> last_direction = 0;
    sc_dt::sc_uint<8> last_hops = 0;
    sc_dt::sc_uint<32> last_packet_bytes = 0;
};

struct NocLinkEntry
{
    NiuPacket packet;
    uint8_t current_node = 0;
    uint64_t remaining_cycles = 0;
    bool moved_this_tick = false;
};

class NocState
{
  public:
    void configure(const NpuConfig &config, uint8_t npu_count);
    void register_endpoint(uint8_t npu_id, NocEndpoint &endpoint);
    void register_trace(sc_core::sc_trace_file *trace_file,
                        const std::string &scope);
    void tick();
    void ack_niu_packet(const NiuPacket &packet);

  private:
    using LinkQueues = std::array<std::deque<NocLinkEntry>, 4>;

    NocDirection route_direction(uint8_t source, uint8_t target) const;
    uint8_t hop_count(uint8_t source, uint8_t target,
                      NocDirection direction) const;
    uint8_t next_node(uint8_t node, NocDirection direction) const;
    uint64_t link_cycles(uint16_t payload_bytes) const;
    bool link_has_space(const LinkQueues &links, uint8_t link) const;
    void push_link(LinkQueues &links, uint8_t link,
                   const NiuPacket &packet);
    void advance_links(LinkQueues &links, NocDirection direction);
    void try_inject(NocDirection direction);
    void trace_link_sizes();
    void trace_packet(const NiuPacket &packet, NocDirection direction,
                      uint8_t hops, uint16_t bytes);

    uint8_t npu_count = 0;
    uint32_t link_queue_depth = 16;
    uint32_t packet_bytes = 128;
    uint32_t link_latency_cycles = 1;
    uint32_t bytes_per_cycle = 128;
    uint8_t last_grant_source = 0;
    std::array<NocEndpoint *, 4> endpoints = {nullptr, nullptr, nullptr,
                                              nullptr};
    LinkQueues clockwise_links;
    LinkQueues counter_clockwise_links;
    sc_core::sc_trace_file *trace_file = nullptr;
    NocTraceSignals trace_signals;
};

} // namespace npu_mvp
