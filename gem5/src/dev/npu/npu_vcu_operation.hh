#pragma once

#include "dev/npu/npu_types.hh"

#include <cstdint>
#include <optional>

namespace npu_mvp
{

enum class VcuWorkUnit : uint8_t {
    Bytes,
    Elements,
};

struct VcuOperationDescriptor
{
    Opcode opcode;
    const char *name;
    VcuWorkUnit work_unit;
    double NpuConfig::*work_rate;
};

// This is the decoded execution contract. VCU handlers do not inspect ISA opcodes.
struct VcuPayload
{
    const VcuOperationDescriptor *operation = nullptr;
    uint8_t destination_register = 0;
    uint8_t source_register_1 = 0;
    uint8_t source_register_2 = 0;
    uint64_t ub_address = 0;
    uint64_t nvl = 0;
    uint8_t eew_bytes = 0;
};

const VcuOperationDescriptor *find_vcu_operation(Opcode opcode);
std::optional<VcuPayload> make_vcu_payload(const NpuCommand &command,
                                           const VcuContext &context);

} // namespace npu_mvp
