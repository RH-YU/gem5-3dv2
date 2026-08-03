#include "dev/npu/npu_cube.hh"

#include "dev/npu/npu_top.hh"

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace npu_mvp
{

void
CubeTraceState::register_trace(sc_core::sc_trace_file *tf,
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
CubeTraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
CubeTraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
CubeTraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

namespace
{

constexpr uint64_t cube_m = 8;
constexpr uint64_t cube_k = 16;
constexpr uint64_t cube_n = 16;
constexpr uint64_t fp32_bytes = sizeof(float);
constexpr uint64_t cube_c_bytes = cube_m * cube_n * fp32_bytes;
constexpr uint64_t cube_fma_count = cube_m * cube_k * cube_n;

uint16_t
read_u16(const std::vector<uint8_t> &data, uint64_t element)
{
    const uint64_t byte_offset = element * 2;
    return static_cast<uint16_t>(data[byte_offset]) |
           (static_cast<uint16_t>(data[byte_offset + 1]) << 8);
}

float
read_f32(const std::vector<uint8_t> &data, uint64_t element)
{
    float value = 0.0F;
    std::memcpy(&value, data.data() + element * fp32_bytes, fp32_bytes);
    return value;
}

float
read_f16(const std::vector<uint8_t> &data, uint64_t element)
{
    const uint16_t half = read_u16(data, element);
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000U) << 16;
    int32_t exponent = static_cast<int32_t>((half >> 10) & 0x1FU);
    uint32_t fraction = static_cast<uint32_t>(half & 0x03FFU);

    uint32_t bits = 0;
    if (exponent == 0) {
        if (fraction == 0) {
            bits = sign;
        } else {
            exponent = -14;
            while ((fraction & 0x0400U) == 0) {
                fraction <<= 1;
                --exponent;
            }
            fraction &= 0x03FFU;
            bits = sign | (static_cast<uint32_t>(exponent + 127) << 23) |
                   (fraction << 13);
        }
    } else if (exponent == 0x1FU) {
        bits = sign | 0x7F800000U | (fraction << 13);
    } else {
        bits = sign | (static_cast<uint32_t>(exponent + (127 - 15)) << 23) |
               (fraction << 13);
    }

    float value = 0.0F;
    std::memcpy(&value, &bits, fp32_bytes);
    return value;
}

void
write_f32(std::vector<uint8_t> &data, uint64_t element, float value)
{
    std::memcpy(data.data() + element * fp32_bytes, &value, fp32_bytes);
}

struct CubeShape
{
    uint64_t a_bytes = 0;
    uint64_t b_bytes = 0;
    uint64_t c_bytes = cube_c_bytes;
    uint64_t input_element_bytes = fp32_bytes;
};

CubeShape
cube_shape(CubeOpcode opcode)
{
    switch (opcode) {
      case CubeOpcode::MmaFp32_8x16x16:
        return {cube_m * cube_k * fp32_bytes, cube_k * cube_n * fp32_bytes};
      case CubeOpcode::MmaFp16Fp32_8x16x16:
        return {cube_m * cube_k * 2, cube_k * cube_n * 2, cube_c_bytes, 2};
    }
    throw std::invalid_argument("unsupported cube opcode");
}

float
read_cube_input(const std::vector<uint8_t> &data, uint64_t element,
                uint64_t input_element_bytes)
{
    if (input_element_bytes == fp32_bytes)
        return read_f32(data, element);
    if (input_element_bytes == 2)
        return read_f16(data, element);
    throw std::invalid_argument("unsupported cube input element size");
}

} // anonymous namespace

void
NpuTop::execute_cube(const ScheduledCommand &command)
{
    const CubeOpcode opcode = as_cube_opcode(command.command);
    const CubeShape shape = cube_shape(opcode);

    const auto a_address = decode(command.command.rs1_value, shape.a_bytes,
                                  Region::L0A);
    const auto b_address = decode(command.command.rs2_value, shape.b_bytes,
                                  Region::L0B);
    const auto c_address = decode(command.command.rd_value, cube_c_bytes,
                                  Region::L0C);
    const auto a = read(a_address.region, a_address.local_address, shape.a_bytes);
    const auto b = read(b_address.region, b_address.local_address, shape.b_bytes);
    std::vector<uint8_t> c(cube_c_bytes, 0);

    for (uint64_t row = 0; row < cube_m; ++row) {
        for (uint64_t col = 0; col < cube_n; ++col) {
            float sum = 0.0F;
            for (uint64_t inner = 0; inner < cube_k; ++inner) {
                sum += read_cube_input(a, row * cube_k + inner,
                                       shape.input_element_bytes) *
                       read_cube_input(b, inner * cube_n + col,
                                       shape.input_element_bytes);
            }
            write_f32(c, row * cube_n + col, sum);
        }
    }

    write(c_address.region, c_address.local_address, c);
}

void
NpuTop::cube_thread()
{
    while (true) {
        wait(cube.event);
        while (!cube.queue.empty()) {
            ScheduledCommand command = std::move(cube.queue.front());
            cube.queue.pop_front();
            cube.trace.trace_queue_size(cube.queue.size());
            cube.busy = true;
            cube.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(cube_fma_count, config.cube_fma_per_ns,
                                    config.cube_setup_delay));
                try {
                    execute_cube(command);
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            cube.busy = false;
            cube.trace.trace_done();
            complete(command, Engine::Cube);
        }
    }
}

} // namespace npu_mvp
