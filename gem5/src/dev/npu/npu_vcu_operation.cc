#include "dev/npu/npu_vcu_operation.hh"

#include <array>

namespace npu_mvp
{

namespace
{

constexpr std::array<VcuOperationDescriptor, 3> vcu_operations = {{
        {Opcode::Vload, "vload", VcuWorkUnit::Bytes, &NpuConfig::vcu_bytes_per_ns},
        {Opcode::Vstore, "vstore", VcuWorkUnit::Bytes, &NpuConfig::vcu_bytes_per_ns},
        {Opcode::Vadd, "vadd", VcuWorkUnit::Elements, &NpuConfig::vadd_elements_per_ns},
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

} // namespace npu_mvp
