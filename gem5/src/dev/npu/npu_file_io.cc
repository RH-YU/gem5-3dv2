#include "dev/npu/npu_file_io.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace npu_mvp
{

void
FileIoTraceState::register_trace(sc_core::sc_trace_file *tf,
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
FileIoTraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
FileIoTraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
FileIoTraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

namespace
{

std::filesystem::path
resolve_sim_file_path(const NpuConfig &config, const std::string &path)
{
    if (!config.enable_sim_file_io)
        throw std::invalid_argument("sim file I/O is disabled");
    if (config.sim_file_io_root.empty())
        throw std::invalid_argument("sim file I/O root is empty");

    const std::filesystem::path requested(path);
    if (requested.empty() || requested.is_absolute())
        throw std::invalid_argument("sim file path must be a relative path");

    const auto normalized = requested.lexically_normal();
    for (const auto &part : normalized) {
        if (part == "..")
            throw std::invalid_argument("sim file path escapes configured root");
    }

    const auto root = std::filesystem::weakly_canonical(config.sim_file_io_root);
    return root / normalized;
}

void
write_sim_file(const std::filesystem::path &path, const std::vector<uint8_t> &data)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("LoadDataFromNpu output file cannot be opened");
    output.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output)
        throw std::runtime_error("LoadDataFromNpu output file write failed");
}

} // anonymous namespace

void
NpuTop::WriteDataToNpu(const ScheduledCommand &command)
{
    const auto full_path = resolve_sim_file_path(config, command.command.sim_file_path);

    std::ifstream input(full_path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("WriteDataToNpu input file cannot be opened");
    const auto file_size = static_cast<uint64_t>(input.tellg());
    const uint64_t byte_count = command.command.file_byte_count == 0
                                    ? file_size
                                    : std::min(file_size, command.command.file_byte_count);
    const auto destination =
            decode_any(command.command.storage_physical_address, byte_count);
    if (!can_write_data_to_region(destination.region))
        throw std::invalid_argument(
                std::string("WriteDataToNpu cannot write region ") +
                region_name(destination.region));
    std::vector<uint8_t> data(byte_count, 0);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (input.gcount() != static_cast<std::streamsize>(data.size()))
        throw std::runtime_error("WriteDataToNpu input file read failed");
    write(destination.region, destination.local_address, data);
}

void
NpuTop::LoadDataFromNpu(const ScheduledCommand &command)
{
    const uint64_t byte_count = command.command.file_byte_count;
    if (byte_count == 0)
        throw std::invalid_argument("LoadDataFromNpu byte_count must be non-zero");

    const auto full_path = resolve_sim_file_path(config, command.command.sim_file_path);
    const auto source =
            decode_any(command.command.storage_physical_address, byte_count);
    if (!can_load_data_from_region(source.region))
        throw std::invalid_argument(
                std::string("LoadDataFromNpu cannot read region ") +
                region_name(source.region));
    auto data = read(source.region, source.local_address, byte_count);
    write_sim_file(full_path, data);
}

void
NpuTop::file_io_thread()
{
    while (true) {
        wait(file_io.event);
        while (!file_io.queue.empty()) {
            ScheduledCommand command = std::move(file_io.queue.front());
            file_io.queue.pop_front();
            file_io.trace.trace_queue_size(file_io.queue.size());
            file_io.busy = true;
            file_io.trace.trace_start(command.command.raw_instruction);
            if (command.command.opcode == Opcode::Sync) {
                execute_sync(command);
            } else {
                wait(transfer_delay(command.command.file_byte_count,
                                    config.file_io_bytes_per_ns,
                                    config.file_io_setup_delay));
                try {
                    if (as_file_io_opcode(command.command) ==
                        FileIoOpcode::LoadDataFromNpu) {
                        LoadDataFromNpu(command);
                    } else {
                        WriteDataToNpu(command);
                    }
                } catch (const std::exception &error) {
                    fault(command, error.what());
                }
            }
            file_io.busy = false;
            file_io.trace.trace_done();
            complete(command, Engine::FileIo);
        }
    }
}

} // namespace npu_mvp
