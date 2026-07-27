#include "dev/npu/npu_gm_file_io.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace npu_mvp
{

namespace
{

std::filesystem::path
resolve_sim_gm_path(const NpuConfig &config, const std::string &path)
{
    if (!config.enable_sim_gm_file_io)
        throw std::invalid_argument("sim GM file I/O is disabled");
    if (config.sim_gm_file_io_root.empty())
        throw std::invalid_argument("sim GM file root is empty");

    const std::filesystem::path requested(path);
    if (requested.empty() || requested.is_absolute())
        throw std::invalid_argument("sim GM file path must be a relative path");

    const auto normalized = requested.lexically_normal();
    for (const auto &part : normalized) {
        if (part == "..")
            throw std::invalid_argument("sim GM file path escapes configured root");
    }

    const auto root = std::filesystem::weakly_canonical(config.sim_gm_file_io_root);
    return root / normalized;
}

void
write_sim_file(const std::filesystem::path &path, const std::vector<uint8_t> &data)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("LoadDataFromGm output file cannot be opened");
    output.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output)
        throw std::runtime_error("LoadDataFromGm output file write failed");
}

} // anonymous namespace

void
NpuTop::WriteDataToGm(const ScheduledCommand &command)
{
    const auto full_path = resolve_sim_gm_path(config, command.command.sim_file_path);

    std::ifstream input(full_path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("WriteDataToGm input file cannot be opened");
    const auto file_size = static_cast<uint64_t>(input.tellg());
    const uint64_t byte_count = command.command.file_byte_count == 0
                                    ? file_size
                                    : std::min(file_size, command.command.file_byte_count);
    const auto destination = decode(command.command.gm_physical_address, byte_count, Region::Gm);
    std::vector<uint8_t> data(byte_count, 0);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (input.gcount() != static_cast<std::streamsize>(data.size()))
        throw std::runtime_error("WriteDataToGm input file read failed");
    write(destination.region, destination.local_address, data);
}

void
NpuTop::LoadDataFromGm(const ScheduledCommand &command)
{
    const uint64_t byte_count = command.command.file_byte_count;
    if (byte_count == 0)
        throw std::invalid_argument("LoadDataFromGm byte_count must be non-zero");

    const auto full_path = resolve_sim_gm_path(config, command.command.sim_file_path);
    const auto source = decode(command.command.gm_physical_address,
                               byte_count, Region::Gm);
    auto data = read(source.region, source.local_address, byte_count);
    write_sim_file(full_path, data);
}

void
NpuTop::gm_file_io_thread()
{
    while (true) {
        wait(gm_file_io.event);
        while (!gm_file_io.queue.empty()) {
            ScheduledCommand command = std::move(gm_file_io.queue.front());
            gm_file_io.queue.pop_front();
            gm_file_io.busy = true;
            trace_engine_start(Engine::GmFileIo, command.command.raw_instruction);
            if (command.command.opcode == Opcode::SyncSet ||
                command.command.opcode == Opcode::SyncWait) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.file_byte_count,
                                    config.gm_file_io_bytes_per_ns,
                                    config.gm_file_io_setup_delay));
                try {
                    if (command.command.opcode == Opcode::LoadDataFromGm) {
                        LoadDataFromGm(command);
                    } else {
                        WriteDataToGm(command);
                    }
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            gm_file_io.busy = false;
            complete(command, Engine::GmFileIo);
        }
    }
}

} // namespace npu_mvp
