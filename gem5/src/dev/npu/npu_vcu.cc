#include "dev/npu/npu_vcu.hh"

#include <algorithm>
#include <stdexcept>

namespace npu_mvp
{

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
            trace_queue_sizes();
            vcu.busy = true;
            trace_engine_start(Engine::Vcu, command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else if (as_vcu_opcode(command.command) == VcuOpcode::Nsetvl) {
                wait(config.scheduler_dispatch_delay);
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
                    wait(transfer_delay(work, config.*(operation.work_rate),
                                        sc_core::SC_ZERO_TIME));
                    execute_vcu(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            vcu.busy = false;
            complete(command, Engine::Vcu);
        }
    }
}

} // namespace npu_mvp
