#include "dev/npu/npu_vcu_operation.hh"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace npu_mvp
{

uint64_t
VcuExecutionContext::byte_count(const VcuPayload &payload) const
{
    return vcu_payload_byte_count(config, payload);
}

std::vector<uint8_t>
VcuExecutionContext::read_ub(uint64_t address, uint64_t byte_count) const
{
    if (ub.read == nullptr)
        throw std::invalid_argument("VCU UB read port is not connected");
    return ub.read(ub.owner, address, byte_count);
}

void
VcuExecutionContext::write_ub(uint64_t address, const std::vector<uint8_t> &data) const
{
    if (ub.write == nullptr)
        throw std::invalid_argument("VCU UB write port is not connected");
    ub.write(ub.owner, address, data);
}

std::vector<uint8_t> &
VcuExecutionContext::register_at(uint8_t index, const char *operation, const char *role)
{
    if (index >= registers.size()) {
        throw std::out_of_range(std::string(operation) + " " + role +
                                " out of range");
    }
    return registers[index];
}

const std::vector<uint8_t> &
VcuExecutionContext::register_at(uint8_t index, const char *operation,
                                 const char *role) const
{
    if (index >= registers.size()) {
        throw std::out_of_range(std::string(operation) + " " + role +
                                " out of range");
    }
    return registers[index];
}

namespace
{

void
require_eew_bytes(const VcuPayload &payload, uint8_t expected, const char *operation)
{
    if (payload.eew_bytes != expected) {
        throw std::invalid_argument(std::string(operation) +
                                    " does not support this EEW");
    }
}

uint32_t
read_u32(const std::vector<uint8_t> &source, uint64_t byte_offset)
{
    uint32_t value = 0;
    std::copy_n(source.data() + byte_offset, sizeof(value),
                reinterpret_cast<uint8_t *>(&value));
    return value;
}

void
write_u32(std::vector<uint8_t> &destination, uint64_t byte_offset, uint32_t value)
{
    std::copy_n(reinterpret_cast<const uint8_t *>(&value), sizeof(value),
                destination.data() + byte_offset);
}

void
execute_vcu_load(VcuExecutionContext &context, const VcuPayload &payload)
{
    const uint64_t byte_count = context.byte_count(payload);
    auto &destination = context.register_at(payload.destination_register, "vload",
                                           "destination register");
    destination = context.read_ub(payload.ub_address, byte_count);
    destination.resize(context.config.vector_register_bytes, 0);
}

void
execute_vcu_store(VcuExecutionContext &context, const VcuPayload &payload)
{
    const uint64_t byte_count = context.byte_count(payload);
    const auto &source = context.register_at(payload.source_register_2, "vstore",
                                            "source register");
    std::vector<uint8_t> data(source.begin(), source.begin() + byte_count);
    context.write_ub(payload.ub_address, data);
}

void
execute_vcu_add(VcuExecutionContext &context, const VcuPayload &payload)
{
    require_eew_bytes(payload, sizeof(uint32_t), "vadd");
    context.byte_count(payload);
    auto &destination = context.register_at(payload.destination_register, "vadd",
                                           "destination register");
    const auto &left = context.register_at(payload.source_register_1, "vadd",
                                          "source register 1");
    const auto &right = context.register_at(payload.source_register_2, "vadd",
                                           "source register 2");
    for (uint64_t index = 0; index < payload.nvl; ++index) {
        const uint64_t byte_offset = index * sizeof(uint32_t);
        write_u32(destination, byte_offset, read_u32(left, byte_offset) +
                  read_u32(right, byte_offset));
    }
}

constexpr std::array<VcuOperationDescriptor, 3> vcu_operations = {{
        {Opcode::Vload, "vload", VcuWorkUnit::Bytes, &NpuConfig::vcu_bytes_per_ns,
         execute_vcu_load},
        {Opcode::Vstore, "vstore", VcuWorkUnit::Bytes, &NpuConfig::vcu_bytes_per_ns,
         execute_vcu_store},
        {Opcode::Vadd, "vadd", VcuWorkUnit::Elements, &NpuConfig::vadd_elements_per_ns,
         execute_vcu_add},
}};

} // anonymous namespace

const VcuOperationDescriptor *
find_vcu_operation(Opcode opcode)
{
    for (const auto &descriptor : vcu_operations) {
        if (descriptor.opcode == opcode)
            return &descriptor;
    }
    return nullptr;
}

std::optional<VcuPayload>
make_vcu_payload(const NpuCommand &command, const VcuContext &context)
{
    const auto *descriptor = find_vcu_operation(command.opcode);
    if (descriptor == nullptr)
        return std::nullopt;

    return VcuPayload{descriptor, command.rd, command.rs1, command.rs2,
                      command.rs1_value, context.nvl, context.eew_bytes};
}

uint64_t
vcu_payload_byte_count(const NpuConfig &config, const VcuPayload &payload)
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
vcu_work_count(const NpuConfig &config, const VcuPayload &payload)
{
    if (payload.operation == nullptr)
        throw std::invalid_argument("unsupported VCU operation");
    return payload.operation->work_unit == VcuWorkUnit::Bytes
            ? vcu_payload_byte_count(config, payload)
            : payload.nvl;
}

void
execute_vcu_operation(VcuExecutionContext &context, const VcuPayload &payload)
{
    if (payload.operation == nullptr || payload.operation->handler == nullptr)
        throw std::invalid_argument("unsupported VCU operation");
    payload.operation->handler(context, payload);
}

} // namespace npu_mvp
