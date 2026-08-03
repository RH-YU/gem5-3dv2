#include "dev/npu/npu_vcu.hh"

#include <algorithm>
#include <stdexcept>

namespace npu_mvp
{

void
VcuTraceState::register_trace(sc_core::sc_trace_file *tf,
                              const std::string &scope)
{
    trace_file = tf;
    if (trace_file == nullptr)
        return;

    sc_core::sc_trace(trace_file, signals.start_event, scope + ".start_event");
    sc_core::sc_trace(trace_file, signals.done_event, scope + ".done_event");
    sc_core::sc_trace(trace_file, signals.busy, scope + ".busy");
    sc_core::sc_trace(trace_file, signals.queue_size, scope + ".queue_size");
    sc_core::sc_trace(trace_file, signals.instruction, scope + ".instruction");
}

void
VcuTraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
VcuTraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
VcuTraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

void
NpuTop::execute_vcu_nsetvl(const ScheduledCommand &command)
{
    auto &context = vcu_context_for(command.command.hart_id);
    context.eew_bytes = decode_eew_bytes(command.command.rs2_value);
    context.nvl = std::min<uint64_t>(command.command.rs1_value, config.max_vl);
}

void
NpuTop::execute_vcu(const ScheduledCommand &command)
{
    if (!command.vcu_payload.has_value())
        throw std::invalid_argument("VCU command has no operation payload");

    const auto &payload = *command.vcu_payload;
    if (payload.operation == nullptr)
        throw std::invalid_argument("unsupported VCU operation");

    VcuExecutionContext context{config, vcu.registers,
                                VcuUbPort{this, read_vcu_ub, write_vcu_ub}};
    execute_vcu_operation(context, payload);
}

std::vector<uint8_t>
NpuTop::read_vcu_ub(void *owner, uint64_t address, uint64_t byte_count)
{
    auto *top = static_cast<NpuTop *>(owner);
    const auto source = top->decode(address, byte_count, Region::Ub);
    return top->read(source.region, source.local_address, byte_count);
}

void
NpuTop::write_vcu_ub(void *owner, uint64_t address, const std::vector<uint8_t> &data)
{
    auto *top = static_cast<NpuTop *>(owner);
    const auto destination = top->decode(address, data.size(), Region::Ub);
    top->write(destination.region, destination.local_address, data);
}

void
NpuTop::vcu_thread()
{
    while (true) {
        wait(vcu.event);
        while (!vcu.queue.empty()) {
            ScheduledCommand command = std::move(vcu.queue.front());
            vcu.queue.pop_front();
            vcu.trace.trace_queue_size(vcu.queue.size());
            vcu.busy = true;
            vcu.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else if (as_vcu_opcode(command.command) == VcuOpcode::Nsetvl) {
                wait_npu_cycles(delay_to_npu_cycles(
                        config.scheduler_dispatch_delay));
                try {
                    execute_vcu_nsetvl(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            } else {
                try {
                    const auto &payload = *command.vcu_payload;
                    const uint64_t work = vcu_work_count(config, payload);
                    const auto &operation = *payload.operation;
                    wait_npu_cycles(delay_to_npu_cycles(
                            transfer_delay(work, config.*(operation.work_rate),
                                           sc_core::SC_ZERO_TIME)));
                    execute_vcu(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            vcu.busy = false;
            vcu.trace.trace_done();
            complete(command, Engine::Vcu);
        }
    }
}

} // namespace npu_mvp
