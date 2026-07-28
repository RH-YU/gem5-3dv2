#pragma once

#include "dev/npu/npu_types.hh"

#include <cstdint>
#include <optional>
#include <vector>

namespace npu_mvp
{

enum class VcuWorkUnit : uint8_t {
    Bytes,
    Elements,
};

struct VcuPayload;
struct VcuExecutionContext;
using VcuHandler = void (*)(VcuExecutionContext &context, const VcuPayload &payload);

struct VcuOperationDescriptor
{
    VcuOpcode opcode;
    const char *name;
    VcuWorkUnit work_unit;
    double NpuConfig::*work_rate;
    VcuHandler handler;
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

using VcuUbRead = std::vector<uint8_t> (*)(void *owner, uint64_t address,
                                           uint64_t byte_count);
using VcuUbWrite = void (*)(void *owner, uint64_t address,
                            const std::vector<uint8_t> &data);

struct VcuUbPort
{
    void *owner = nullptr;
    VcuUbRead read = nullptr;
    VcuUbWrite write = nullptr;
};

struct VcuExecutionContext
{
    const NpuConfig &config;
    std::vector<std::vector<uint8_t>> &registers;
    VcuUbPort ub;

    uint64_t byte_count(const VcuPayload &payload) const;
    std::vector<uint8_t> read_ub(uint64_t address, uint64_t byte_count) const;
    void write_ub(uint64_t address, const std::vector<uint8_t> &data) const;
    std::vector<uint8_t> &register_at(uint8_t index, const char *operation,
                                      const char *role);
    const std::vector<uint8_t> &register_at(uint8_t index, const char *operation,
                                            const char *role) const;
};

const VcuOperationDescriptor *find_vcu_operation(VcuOpcode opcode);
std::optional<VcuPayload> make_vcu_payload(const NpuCommand &command,
                                           const VcuContext &context);
uint64_t vcu_payload_byte_count(const NpuConfig &config, const VcuPayload &payload);
uint64_t vcu_work_count(const NpuConfig &config, const VcuPayload &payload);
void execute_vcu_operation(VcuExecutionContext &context, const VcuPayload &payload);

} // namespace npu_mvp
