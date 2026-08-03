#include "dev/npu/npu_top.hh"

#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace npu_mvp
{

namespace
{

struct RegionRange
{
    const char *name;
    uint64_t base;
    uint64_t size;
};

bool
range_fits(uint64_t address, uint64_t byte_count, uint64_t base, uint64_t size)
{
    if (byte_count == 0 || address < base || byte_count > size)
        return false;
    const uint64_t offset = address - base;
    return offset <= size - byte_count;
}

bool
range_is_valid(uint64_t base, uint64_t size)
{
    return size != 0 && base <= UINT64_MAX - size;
}

bool
ranges_overlap(uint64_t first_base, uint64_t first_size,
               uint64_t second_base, uint64_t second_size)
{
    const uint64_t first_end = first_base + first_size;
    const uint64_t second_end = second_base + second_size;
    return first_base < second_end && second_base < first_end;
}

void
require_non_overlapping(uint64_t first_base, uint64_t first_size,
                        uint64_t second_base, uint64_t second_size,
                        const char *message)
{
    if (ranges_overlap(first_base, first_size, second_base, second_size))
        throw std::invalid_argument(message);
}

std::vector<RegionRange>
configured_regions(const NpuConfig &config)
{
    return {
            {"GM", config.gm_phys_base, config.gm_size},
            {"UB", config.ub_phys_base, config.ub_size},
            {"L1", config.l1_phys_base, config.l1_size},
            {"L0A", config.l0a_phys_base, config.l0a_size},
            {"L0B", config.l0b_phys_base, config.l0b_size},
            {"L0C", config.l0c_phys_base, config.l0c_size},
    };
}

void
require_valid_npu_regions(const NpuConfig &config)
{
    for (const RegionRange &region : configured_regions(config)) {
        if (!range_is_valid(region.base, region.size))
            throw std::invalid_argument("NPU physical address map overflows");
    }
}

void
require_non_overlapping_npu_regions(const NpuConfig &config)
{
    const std::vector<RegionRange> regions = configured_regions(config);
    for (auto first = regions.begin(); first != regions.end(); ++first) {
        for (auto second = std::next(first); second != regions.end(); ++second) {
            const std::string message = std::string(first->name) + " and " +
                    second->name + " physical regions overlap";
            require_non_overlapping(first->base, first->size, second->base,
                                    second->size, message.c_str());
        }
    }
}

} // anonymous namespace

NpuTop::NpuTop(sc_core::sc_module_name name, NpuConfig config)
    : sc_core::sc_module(name), npu_clk("npu_clk"), config(std::move(config)),
      gm(this->config.gm_size, this->config.gm_page_size), ub(this->config.ub_size),
      l1(this->config.l1_size), l0a(this->config.l0a_size),
      l0b(this->config.l0b_size), l0c(this->config.l0c_size),
      vcu(this->config.vector_register_count, this->config.vector_register_bytes)
{
    require_valid_npu_regions(this->config);
    require_non_overlapping_npu_regions(this->config);

    SC_METHOD(dispatch_ingress);
    sensitive << scheduler.dispatch_event;
    dont_initialize();
    SC_METHOD(clear_niu_packet_sent_trace);
    sensitive << niu.packet_sent_clear_event;
    dont_initialize();
    SC_METHOD(clear_niu_packet_received_trace);
    sensitive << niu.packet_received_clear_event;
    dont_initialize();
    SC_THREAD(mte4_thread);
    SC_THREAD(mte1_thread);
    SC_THREAD(mte2_thread);
    SC_THREAD(vcu_thread);
    SC_THREAD(cube_thread);
    SC_THREAD(fixpipe_thread);
    SC_THREAD(file_io_thread);
    SC_THREAD(niu_tx_thread);
    SC_THREAD(niu_rx_thread);
}

uint64_t
NpuTop::delay_to_npu_cycles(const sc_core::sc_time &delay) const
{
    const uint64_t cycle_ticks =
            active_cpu_cycle_ticks(config.vcd_trace_cycle_ticks);
    const uint64_t delay_ticks = delay.value();
    if (delay_ticks == 0)
        return 0;
    return (delay_ticks + cycle_ticks - 1) / cycle_ticks;
}

void
NpuTop::wait_npu_cycles(uint64_t cycles)
{
    for (uint64_t cycle = 0; cycle < cycles; ++cycle)
        wait(npu_clk.posedge_event());
}

bool
NpuTop::can_accept(const NpuCommand &command) const
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return true;

    if (scheduler.ingress_queue.size() >= config.scheduler_queue_depth)
        return false;

    return engine_has_space(route_engine(command));
}

SubmitResult
NpuTop::submit(const NpuCommand &command)
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return SubmitResult::Accepted;

    if (!can_accept(command))
        return SubmitResult::Backpressured;
    scheduler.ingress_queue.push_back(command);
    trace_scheduler_queue_size();
    trace_ingress();
    scheduler.dispatch_event.notify(sc_core::SC_ZERO_TIME);
    return SubmitResult::Accepted;
}

void
NpuTop::bind_noc(NocState &noc_state)
{
    noc = &noc_state;
}

void
NpuTop::register_trace(sc_core::sc_trace_file *tf, const std::string &scope)
{
    trace_file = tf;
    register_npu_trace_signals(trace_file, trace_signals, scope);
    mte4.trace.register_trace(trace_file, scope + ".mte4");
    mte1.trace.register_trace(trace_file, scope + ".mte1");
    mte2.trace.register_trace(trace_file, scope + ".mte2");
    vcu.trace.register_trace(trace_file, scope + ".vcu");
    cube.trace.register_trace(trace_file, scope + ".cube");
    fixpipe.trace.register_trace(trace_file, scope + ".fixpipe");
    file_io.trace.register_trace(trace_file, scope + ".file_io");
    niu.trace.register_trace(trace_file, scope + ".niu");
}

uint64_t
NpuTop::fault_count() const
{
    return scheduler.faults;
}

const std::string &
NpuTop::last_fault() const
{
    return scheduler.latest_fault;
}

void
NpuTop::trace_ingress()
{
    if (trace_file == nullptr)
        return;

    trace_signals.ingress_event = !trace_signals.ingress_event;
}

void
NpuTop::trace_dispatch()
{
    if (trace_file == nullptr)
        return;

    trace_signals.dispatch_event = !trace_signals.dispatch_event;
}

void
NpuTop::trace_sync_start(const NpuCommand &command)
{
    if (trace_file == nullptr)
        return;

    trace_signals.sync_event = !trace_signals.sync_event;
}

void
NpuTop::trace_sync_done()
{
    if (trace_file == nullptr)
        return;

    trace_signals.sync_event = !trace_signals.sync_event;
}

void
NpuTop::trace_fault()
{
    if (trace_file == nullptr)
        return;

    trace_signals.fault_event = !trace_signals.fault_event;
}

void
NpuTop::trace_scheduler_queue_size()
{
    if (trace_file == nullptr)
        return;

    trace_signals.scheduler_queue_size =
            static_cast<uint32_t>(scheduler.ingress_queue.size());
}

void
NpuTop::write_gm_for_test(uint64_t address, const std::vector<uint8_t> &data)
{
    const auto decoded = decode(address, data.size(), Region::Gm);
    write(decoded.region, decoded.local_address, data);
}

std::vector<uint8_t>
NpuTop::read_gm_for_test(uint64_t address, uint64_t byte_count) const
{
    const auto decoded = decode(address, byte_count, Region::Gm);
    return read(decoded.region, decoded.local_address, byte_count);
}

NpuTop::DecodedAddress
NpuTop::decode(uint64_t address, uint64_t byte_count, Region expected) const
{
    uint64_t base = 0;
    uint64_t size = 0;
    switch (expected) {
      case Region::Gm:
        base = config.gm_phys_base;
        size = config.gm_size;
        break;
      case Region::Ub:
        base = config.ub_phys_base;
        size = config.ub_size;
        break;
      case Region::L1:
        base = config.l1_phys_base;
        size = config.l1_size;
        break;
      case Region::L0A:
        base = config.l0a_phys_base;
        size = config.l0a_size;
        break;
      case Region::L0B:
        base = config.l0b_phys_base;
        size = config.l0b_size;
        break;
      case Region::L0C:
        base = config.l0c_phys_base;
        size = config.l0c_size;
        break;
    }
    if (!range_fits(address, byte_count, base, size))
        throw std::out_of_range("NPU physical address does not fit expected region");
    return {expected, address - base};
}

NpuTop::DecodedAddress
NpuTop::decode_any(uint64_t address, uint64_t byte_count) const
{
    for (const Region region :
         {Region::Gm, Region::Ub, Region::L1, Region::L0A, Region::L0B,
          Region::L0C}) {
        try {
            return decode(address, byte_count, region);
        } catch (const std::out_of_range &) {
        }
    }
    throw std::out_of_range("NPU physical address does not fit any storage region");
}

const char *
NpuTop::region_name(Region region)
{
    switch (region) {
      case Region::Gm: return "gm";
      case Region::Ub: return "ub";
      case Region::L1: return "l1";
      case Region::L0A: return "l0a";
      case Region::L0B: return "l0b";
      case Region::L0C: return "l0c";
    }
    return "invalid";
}

const char *
NpuTop::storage_region_name(const NpuCommand &command) const
{
    if (command.opcode != Opcode::FileIo)
        return "";

    try {
        const uint64_t byte_count = command.file_byte_count == 0
                                            ? 1
                                            : command.file_byte_count;
        return region_name(
                decode_any(command.storage_physical_address, byte_count).region);
    } catch (const std::exception &) {
        return "invalid";
    }
}

bool
NpuTop::can_write_data_to_region(Region region)
{
    switch (region) {
      case Region::Gm:
      case Region::Ub:
      case Region::L1:
      case Region::L0A:
      case Region::L0B:
        return true;
      case Region::L0C:
        return false;
    }
    return false;
}

bool
NpuTop::can_load_data_from_region(Region region)
{
    switch (region) {
      case Region::Gm:
      case Region::Ub:
      case Region::L1:
      case Region::L0C:
        return true;
      case Region::L0A:
      case Region::L0B:
        return false;
    }
    return false;
}

std::vector<uint8_t>
NpuTop::read(Region region, uint64_t local_address, uint64_t byte_count) const
{
    std::vector<uint8_t> data(byte_count, 0);
    switch (region) {
      case Region::Gm:
        gm.read(local_address, data);
        break;
      case Region::Ub:
        ub.read(local_address, data);
        break;
      case Region::L1:
        l1.read(local_address, data);
        break;
      case Region::L0A:
        l0a.read(local_address, data);
        break;
      case Region::L0B:
        l0b.read(local_address, data);
        break;
      case Region::L0C:
        l0c.read(local_address, data);
        break;
    }
    return data;
}

void
NpuTop::write(Region region, uint64_t local_address, const std::vector<uint8_t> &data)
{
    switch (region) {
      case Region::Gm:
        gm.write(local_address, data);
        break;
      case Region::Ub:
        ub.write(local_address, data);
        break;
      case Region::L1:
        l1.write(local_address, data);
        break;
      case Region::L0A:
        l0a.write(local_address, data);
        break;
      case Region::L0B:
        l0b.write(local_address, data);
        break;
      case Region::L0C:
        l0c.write(local_address, data);
        break;
    }
}

} // namespace npu_mvp
