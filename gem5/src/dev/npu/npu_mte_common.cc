#include "dev/npu/npu_mte.hh"

#include <iostream>
#include <stdexcept>

namespace npu_mvp
{

void
MteTraceState::register_trace(sc_core::sc_trace_file *tf,
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
MteTraceState::trace_start(uint32_t raw_instruction)
{
    if (trace_file == nullptr)
        return;

    signals.start_event = !signals.start_event;
    signals.busy = true;
    signals.instruction = raw_instruction;
}

void
MteTraceState::trace_done()
{
    if (trace_file == nullptr)
        return;

    signals.done_event = !signals.done_event;
    signals.busy = false;
    signals.instruction = 0;
}

void
MteTraceState::trace_queue_size(std::size_t queue_size)
{
    if (trace_file == nullptr)
        return;

    signals.queue_size = static_cast<uint32_t>(queue_size);
}

void
NpuTop::execute_mte(const ScheduledCommand &command, Region source, Region destination)
{
    const uint64_t byte_count = command.command.rd_value;
    if (byte_count == 0 || byte_count > config.mte_max_transfer_bytes)
        throw std::invalid_argument("invalid MTE byte_count");
    const auto source_address = decode(command.command.rs1_value, byte_count, source);
    const auto destination_address = decode(command.command.rs2_value, byte_count, destination);
    auto data = read(source_address.region, source_address.local_address, byte_count);
    write(destination_address.region, destination_address.local_address, data);
}

void
NpuTop::execute_mte4(const ScheduledCommand &command)
{
    const Region destination = as_mte4_opcode(command.command) == Mte4Opcode::GmToL1
            ? Region::L1
            : Region::Ub;
    execute_mte(command, Region::Gm, destination);
}

void
NpuTop::execute_mte1(const ScheduledCommand &command)
{
    Region destination = Region::Gm;
    switch (as_mte1_opcode(command.command)) {
      case Mte1Opcode::L1ToGm:
        destination = Region::Gm;
        break;
      case Mte1Opcode::L1ToUb:
        destination = Region::Ub;
        break;
      case Mte1Opcode::L1ToL0A:
        destination = Region::L0A;
        break;
      case Mte1Opcode::L1ToL0B:
        destination = Region::L0B;
        break;
    }
    execute_mte(command, Region::L1, destination);
}

void
NpuTop::execute_mte2(const ScheduledCommand &command)
{
    const Region destination = as_mte2_opcode(command.command) == Mte2Opcode::UbToL1
            ? Region::L1
            : Region::Gm;
    execute_mte(command, Region::Ub, destination);
}

} // namespace npu_mvp
