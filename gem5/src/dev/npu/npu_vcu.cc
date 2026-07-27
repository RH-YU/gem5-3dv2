#include "dev/npu/npu_vcu.hh"

#include <algorithm>
#include <stdexcept>

namespace npu_mvp
{

void
NpuTop::execute_vcu(const ScheduledCommand &command)
{
    if (!command.vcu_payload.has_value())
        throw std::invalid_argument("VCU command has no operation payload");

    const auto &payload = *command.vcu_payload;
    if (payload.operation == nullptr)
        throw std::invalid_argument("unsupported VCU operation");

    switch (payload.operation->opcode) {
      case Opcode::Vload:
        execute_vcu_load(payload);
        return;
      case Opcode::Vstore:
        execute_vcu_store(payload);
        return;
      case Opcode::Vadd:
        execute_vcu_add(payload);
        return;
      default:
        throw std::invalid_argument("unsupported VCU operation");
    }
}

uint64_t
NpuTop::vcu_byte_count(const VcuPayload &payload) const
{
    if (payload.nvl == 0 || payload.eew_bytes == 0 ||
        payload.nvl > UINT64_MAX / payload.eew_bytes) {
        throw std::invalid_argument("invalid VCU vector length");
    }
    const uint64_t byte_count = payload.nvl * payload.eew_bytes;
    if (byte_count > config.vector_register_bytes)
        throw std::invalid_argument("invalid VCU vector length");
    return byte_count;
}

uint64_t
NpuTop::vcu_work_count(const VcuPayload &payload) const
{
    if (payload.operation == nullptr)
        throw std::invalid_argument("unsupported VCU operation");
    return payload.operation->work_unit == VcuWorkUnit::Bytes ? vcu_byte_count(payload)
                                                             : payload.nvl;
}

void
NpuTop::execute_vcu_load(const VcuPayload &payload)
{
    const uint64_t byte_count = vcu_byte_count(payload);
    if (payload.destination_register >= vcu.registers.size())
        throw std::out_of_range("vload destination register out of range");
    const auto source = decode(payload.ub_address, byte_count, Region::Ub);
    vcu.registers[payload.destination_register] =
            read(source.region, source.local_address, byte_count);
    vcu.registers[payload.destination_register].resize(config.vector_register_bytes, 0);
}

void
NpuTop::execute_vcu_store(const VcuPayload &payload)
{
    const uint64_t byte_count = vcu_byte_count(payload);
    if (payload.source_register_2 >= vcu.registers.size())
        throw std::out_of_range("vstore source register out of range");
    const auto destination = decode(payload.ub_address, byte_count, Region::Ub);
    std::vector<uint8_t> data(vcu.registers[payload.source_register_2].begin(),
                              vcu.registers[payload.source_register_2].begin() + byte_count);
    write(destination.region, destination.local_address, data);
}

void
NpuTop::execute_vcu_add(const VcuPayload &payload)
{
    if (payload.destination_register >= vcu.registers.size() ||
        payload.source_register_1 >= vcu.registers.size() ||
        payload.source_register_2 >= vcu.registers.size()) {
        throw std::out_of_range("vadd register out of range");
    }
    if (payload.eew_bytes != 4)
        throw std::invalid_argument("MVP VCU only supports EEW=32");
    vcu_byte_count(payload);
    for (uint64_t index = 0; index < payload.nvl; ++index) {
        const uint64_t byte_offset = index * sizeof(uint32_t);
        uint32_t left = 0;
        uint32_t right = 0;
        std::copy_n(vcu.registers[payload.source_register_1].data() + byte_offset, sizeof(left),
                    reinterpret_cast<uint8_t *>(&left));
        std::copy_n(vcu.registers[payload.source_register_2].data() + byte_offset, sizeof(right),
                    reinterpret_cast<uint8_t *>(&right));
        const uint32_t result = left + right;
        std::copy_n(reinterpret_cast<const uint8_t *>(&result), sizeof(result),
                    vcu.registers[payload.destination_register].data() + byte_offset);
    }
}

void
NpuTop::vcu_thread()
{
    while (true) {
        wait(vcu.event);
        while (!vcu.queue.empty()) {
            ScheduledCommand command = std::move(vcu.queue.front());
            vcu.queue.pop_front();
            vcu.busy = true;
            trace_engine_start(Engine::Vcu, command.command.raw_instruction);
            if (!command.vcu_payload.has_value()) {
                execute_sync(command);
            } else {
                try {
                    const auto &payload = *command.vcu_payload;
                    const uint64_t work = vcu_work_count(payload);
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
