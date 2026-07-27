#include "dev/npu/npu_top.hh"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace npu_mvp
{

namespace
{

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

} // anonymous namespace

NpuTop::NpuTop(sc_core::sc_module_name name, NpuConfig config)
    : sc_core::sc_module(name), command_target("command_target"), config(std::move(config)),
      gm(this->config.gm_size, this->config.gm_page_size), ub(this->config.ub_size),
      vcu(this->config.vector_register_count, this->config.vector_register_bytes)
{
    if (!range_is_valid(this->config.gm_phys_base, this->config.gm_size) ||
        !range_is_valid(this->config.ub_phys_base, this->config.ub_size)) {
        throw std::invalid_argument("NPU physical address map overflows");
    }
    if (ranges_overlap(this->config.gm_phys_base, this->config.gm_size,
                       this->config.ub_phys_base, this->config.ub_size)) {
        throw std::invalid_argument("GM and UB physical regions overlap");
    }

    SC_METHOD(dispatch_ingress);
    sensitive << scheduler.dispatch_event;
    dont_initialize();
    SC_THREAD(mte4_thread);
    SC_THREAD(mte2_thread);
    SC_THREAD(vcu_thread);
    SC_THREAD(gm_file_io_thread);
    command_target.register_b_transport(this, &NpuTop::b_transport);
}

bool
NpuTop::can_accept(const NpuCommand &command) const
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return true;

    return scheduler.ingress_queue.size() < config.scheduler_queue_depth;
}

SubmitResult
NpuTop::submit(const NpuCommand &command)
{
    if ((command.npu_mask & (1U << config.npu_id)) == 0)
        return SubmitResult::Accepted;

    if (!can_accept(command))
        return SubmitResult::Backpressured;
    scheduler.ingress_queue.push_back(command);
    trace_ingress();
    scheduler.dispatch_event.notify(sc_core::SC_ZERO_TIME);
    return SubmitResult::Accepted;
}

void
NpuTop::register_trace(sc_core::sc_trace_file *tf, const std::string &scope)
{
    trace_file = tf;
    register_npu_trace_signals(trace_file, trace_signals, scope);
}

void
NpuTop::b_transport(tlm::tlm_generic_payload &transaction, sc_core::sc_time &delay)
{
    auto *extension = transaction.get_extension<NpuCommandExtension>();
    if (extension == nullptr) {
        transaction.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    const SubmitResult result = submit(extension->command);
    if (result == SubmitResult::Accepted) {
        if (extension->sender_state != nullptr)
            extension->sender_state->status = DispatchStatus::Accepted;
        delay += config.scheduler_dispatch_delay;
        transaction.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }
    if (extension->sender_state != nullptr) {
        extension->sender_state->status =
                result == SubmitResult::Backpressured
                ? DispatchStatus::Backpressured
                : DispatchStatus::Invalid;
    }
    transaction.set_response_status(result == SubmitResult::Backpressured
                                        ? tlm::TLM_INCOMPLETE_RESPONSE
                                        : tlm::TLM_COMMAND_ERROR_RESPONSE);
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
NpuTop::trace_engine_start(Engine engine, uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    trace_signals.engine_start_event = !trace_signals.engine_start_event;
    switch (engine) {
      case Engine::Mte4:
        trace_signals.mte4_busy = true;
        trace_signals.mte4_instruction = raw_instruction;
        break;
      case Engine::Mte2:
        trace_signals.mte2_busy = true;
        trace_signals.mte2_instruction = raw_instruction;
        break;
      case Engine::Vcu:
        trace_signals.vcu_busy = true;
        trace_signals.vcu_instruction = raw_instruction;
        break;
      case Engine::GmFileIo:
        trace_signals.gm_file_io_busy = true;
        trace_signals.gm_file_io_instruction = raw_instruction;
        break;
      case Engine::Control: break;
    }
}

void
NpuTop::trace_engine_done(Engine engine)
{
    if (trace_file == nullptr)
        return;

    trace_signals.engine_done_event = !trace_signals.engine_done_event;
    switch (engine) {
      case Engine::Mte4:
        trace_signals.mte4_busy = false;
        trace_signals.mte4_instruction = 0;
        break;
      case Engine::Mte2:
        trace_signals.mte2_busy = false;
        trace_signals.mte2_instruction = 0;
        break;
      case Engine::Vcu:
        trace_signals.vcu_busy = false;
        trace_signals.vcu_instruction = 0;
        break;
      case Engine::GmFileIo:
        trace_signals.gm_file_io_busy = false;
        trace_signals.gm_file_io_instruction = 0;
        break;
      case Engine::Control: break;
    }
}

void
NpuTop::trace_fault()
{
    if (trace_file == nullptr)
        return;

    trace_signals.fault_event = !trace_signals.fault_event;
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
    const uint64_t base = expected == Region::Gm ? config.gm_phys_base : config.ub_phys_base;
    const uint64_t size = expected == Region::Gm ? config.gm_size : config.ub_size;
    if (!range_fits(address, byte_count, base, size))
        throw std::out_of_range("NPU physical address does not fit expected region");
    return {expected, address - base};
}

std::vector<uint8_t>
NpuTop::read(Region region, uint64_t local_address, uint64_t byte_count) const
{
    std::vector<uint8_t> data(byte_count, 0);
    if (region == Region::Gm)
        gm.read(local_address, data);
    else
        ub.read(local_address, data);
    return data;
}

void
NpuTop::write(Region region, uint64_t local_address, const std::vector<uint8_t> &data)
{
    if (region == Region::Gm)
        gm.write(local_address, data);
    else
        ub.write(local_address, data);
}

} // namespace npu_mvp
