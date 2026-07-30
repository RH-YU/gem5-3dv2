#include "dev/npu/npu_cube.hh"

#include "dev/npu/npu_top.hh"

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace npu_mvp
{

namespace
{

constexpr uint64_t cube_m = 8;
constexpr uint64_t cube_k = 16;
constexpr uint64_t cube_n = 16;
constexpr uint64_t fp32_bytes = sizeof(float);
constexpr uint64_t cube_a_bytes = cube_m * cube_k * fp32_bytes;
constexpr uint64_t cube_b_bytes = cube_k * cube_n * fp32_bytes;
constexpr uint64_t cube_c_bytes = cube_m * cube_n * fp32_bytes;
constexpr uint64_t cube_fma_count = cube_m * cube_k * cube_n;

float
read_f32(const std::vector<uint8_t> &data, uint64_t element)
{
    float value = 0.0F;
    std::memcpy(&value, data.data() + element * fp32_bytes, fp32_bytes);
    return value;
}

void
write_f32(std::vector<uint8_t> &data, uint64_t element, float value)
{
    std::memcpy(data.data() + element * fp32_bytes, &value, fp32_bytes);
}

} // anonymous namespace

void
NpuTop::execute_cube(const ScheduledCommand &command)
{
    if (as_cube_opcode(command.command) != CubeOpcode::MmaFp32_8x16x16)
        throw std::invalid_argument("unsupported cube opcode");

    const auto a_address = decode(command.command.rs1_value, cube_a_bytes,
                                  Region::L0A);
    const auto b_address = decode(command.command.rs2_value, cube_b_bytes,
                                  Region::L0B);
    const auto c_address = decode(command.command.rd_value, cube_c_bytes,
                                  Region::L0C);
    const auto a = read(a_address.region, a_address.local_address, cube_a_bytes);
    const auto b = read(b_address.region, b_address.local_address, cube_b_bytes);
    std::vector<uint8_t> c(cube_c_bytes, 0);

    for (uint64_t row = 0; row < cube_m; ++row) {
        for (uint64_t col = 0; col < cube_n; ++col) {
            float sum = 0.0F;
            for (uint64_t inner = 0; inner < cube_k; ++inner) {
                sum += read_f32(a, row * cube_k + inner) *
                       read_f32(b, inner * cube_n + col);
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
            trace_queue_sizes();
            cube.busy = true;
            trace_engine_start(Engine::Cube, command.command.raw_instruction);
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
            complete(command, Engine::Cube);
        }
    }
}

} // namespace npu_mvp
