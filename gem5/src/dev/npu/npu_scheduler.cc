#define NPU_SCHEDULER_INCLUDE_TOP
#include "dev/npu/npu_scheduler.hh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace npu_mvp
{

namespace
{

const char *
opcode_name(Opcode opcode)
{
    if (const auto *operation = find_vcu_operation(opcode))
        return operation->name;

    switch (opcode) {
      case Opcode::Nsetvl: return "nsetvl";
      case Opcode::Mte4: return "mte4";
      case Opcode::Mte2: return "mte2";
      case Opcode::SyncSet: return "sync_set";
      case Opcode::SyncWait: return "sync_wait";
      case Opcode::WriteDataToGm: return "WriteDataToGm";
      case Opcode::LoadDataFromGm: return "LoadDataFromGm";
      default: return "unknown";
    }
}

} // anonymous namespace

void
NpuTop::dispatch_ingress()
{
    while (!scheduler.ingress_queue.empty()) {
        if (!dispatch_one(scheduler.ingress_queue.front()))
            return;
        scheduler.ingress_queue.pop_front();
    }
}

bool
NpuTop::dispatch_one(const NpuCommand &command)
{
    Engine engine = Engine::Control;
    const VcuContext context = vcu_context_for(command.hart_id);
    const auto vcu_payload = make_vcu_payload(command, context);
    if (vcu_payload.has_value()) {
        engine = Engine::Vcu;
    } else {
        switch (command.opcode) {
          case Opcode::Mte4: engine = Engine::Mte4; break;
          case Opcode::Mte2: engine = Engine::Mte2; break;
          case Opcode::SyncSet:
          case Opcode::SyncWait: engine = sync_route_engine(command); break;
          case Opcode::WriteDataToGm:
          case Opcode::LoadDataFromGm: engine = Engine::GmFileIo; break;
          case Opcode::Nsetvl: break;
          default:
            throw std::logic_error("opcode has no scheduler descriptor");
        }
    }

    if (!engine_has_space(engine))
        return false;

    ScheduledCommand scheduled{scheduler.next_sequence++, command, context, vcu_payload};
    scheduler.command_records.emplace(scheduled.sequence, CommandRecord{engine, false});

    if (command.opcode == Opcode::Nsetvl) {
        auto &context = vcu_context_for(command.hart_id);
        context.eew_bytes = decode_eew_bytes(command.rs2_value);
        context.nvl = std::min<uint64_t>(command.rs1_value, config.max_vl);
        trace_dispatch();
        complete(scheduled, Engine::Control);
        return true;
    }
    if (enqueue_scheduled(engine, std::move(scheduled))) {
        trace_dispatch();
        return true;
    }
    return false;
}

bool
NpuTop::enqueue_scheduled(Engine engine, ScheduledCommand &&scheduled)
{
    switch (engine) {
      case Engine::Mte4:
        mte4.queue.push_back(std::move(scheduled));
        mte4.event.notify(sc_core::SC_ZERO_TIME);
        return true;
      case Engine::Mte2:
        mte2.queue.push_back(std::move(scheduled));
        mte2.event.notify(sc_core::SC_ZERO_TIME);
        return true;
      case Engine::Vcu:
        vcu.queue.push_back(std::move(scheduled));
        vcu.event.notify(sc_core::SC_ZERO_TIME);
        return true;
      case Engine::GmFileIo:
        gm_file_io.queue.push_back(std::move(scheduled));
        gm_file_io.event.notify(sc_core::SC_ZERO_TIME);
        return true;
      case Engine::Control:
        return false;
    }
    return false;
}

void
NpuTop::trace_command(const NpuCommand &command) const
{
    std::cout << "CPU[" << static_cast<unsigned>(command.hart_id)
              << "]NPU[" << static_cast<unsigned>(config.npu_id)
              << "] : op=" << opcode_name(command.opcode)
              << " opcode=" << static_cast<unsigned>(command.opcode)
              << " mask=0x" << std::hex << static_cast<unsigned>(command.npu_mask)
              << " raw=0x" << command.raw_instruction
              << " pc=0x" << command.pc
              << std::dec
              << " rd=x" << static_cast<unsigned>(command.rd)
              << " rs1=x" << static_cast<unsigned>(command.rs1)
              << " rs2=x" << static_cast<unsigned>(command.rs2)
              << " rd_value=" << command.rd_value
              << " rs1_value=" << command.rs1_value
              << " rs2_value=" << command.rs2_value
              << " sync_src=" << static_cast<unsigned>(command.sync_src)
              << " sync_dst=" << static_cast<unsigned>(command.sync_dst)
              << " sync_id=" << static_cast<unsigned>(command.sync_id)
              << std::endl;
}

bool
NpuTop::engine_has_space(Engine engine) const
{
    switch (engine) {
      case Engine::Mte4: return mte4.queue.size() < config.mte4_queue_depth;
      case Engine::Mte2: return mte2.queue.size() < config.mte2_queue_depth;
      case Engine::Vcu: return vcu.queue.size() < config.vcu_queue_depth;
      case Engine::GmFileIo: return gm_file_io.queue.size() < config.gm_file_io_queue_depth;
      case Engine::Control: return true;
    }
    return false;
}

Engine
NpuTop::sync_route_engine(const NpuCommand &command) const
{
    const SyncEndpoint endpoint = command.opcode == Opcode::SyncSet
                                      ? command.sync_src
                                      : command.sync_dst;
    switch (endpoint) {
      case SyncEndpoint::Mte4: return Engine::Mte4;
      case SyncEndpoint::Mte2: return Engine::Mte2;
      case SyncEndpoint::Vcu: return Engine::Vcu;
      case SyncEndpoint::GmFileIo: return Engine::GmFileIo;
    }
    return Engine::Control;
}

uint64_t
NpuTop::scope_watermark() const
{
    return scheduler.next_sequence == 0 ? 0 : scheduler.next_sequence - 1;
}

bool
NpuTop::scope_complete(SyncScope scope, uint64_t watermark) const
{
    for (const auto &[sequence, record] : scheduler.command_records) {
        if (sequence <= watermark && scope_includes(scope, record.engine) && !record.complete)
            return false;
    }
    return true;
}

bool
NpuTop::sync_complete_for_command(const NpuCommand &command) const
{
    return scope_complete(static_cast<SyncScope>(command.rs1_value & 0x7), scope_watermark());
}

bool
NpuTop::scope_includes(SyncScope scope, Engine engine) const
{
    if (engine == Engine::Control)
        return false;
    switch (scope) {
      case SyncScope::All: return true;
      case SyncScope::MteAll: return engine == Engine::Mte4 || engine == Engine::Mte2;
      case SyncScope::Vcu: return engine == Engine::Vcu;
      case SyncScope::Mte4: return engine == Engine::Mte4;
      case SyncScope::Mte2: return engine == Engine::Mte2;
      case SyncScope::GmFileIo: return engine == Engine::GmFileIo;
    }
    return false;
}

void
NpuTop::complete(const ScheduledCommand &command, Engine engine)
{
    auto &record = scheduler.command_records.at(command.sequence);
    record.engine = engine;
    record.complete = true;
    trace_engine_done(engine);
    if (!record.faulted)
        trace_command(command.command);
    scheduler.dispatch_event.notify(sc_core::SC_ZERO_TIME);
}

void
NpuTop::fault(const ScheduledCommand &command, const std::string &message)
{
    ++scheduler.faults;
    scheduler.latest_fault =
            "sequence=" + std::to_string(command.sequence) + ": " + message;
    scheduler.command_records.at(command.sequence).faulted = true;
    trace_fault();
    std::cout << "CPU[" << static_cast<unsigned>(command.command.hart_id)
              << "]NPU[" << static_cast<unsigned>(config.npu_id)
              << "] : fault opcode="
              << static_cast<unsigned>(command.command.opcode)
              << " sequence=" << command.sequence
              << " message=\"" << message << "\""
              << std::endl;
}

sc_core::sc_time
NpuTop::transfer_delay(uint64_t byte_count, double bytes_per_ns, const sc_core::sc_time &setup) const
{
    if (bytes_per_ns <= 0.0)
        throw std::invalid_argument("bandwidth must be positive");
    const double transfer_ns = std::ceil(static_cast<double>(byte_count) / bytes_per_ns);
    return config.scheduler_dispatch_delay + setup + sc_core::sc_time(transfer_ns, sc_core::SC_NS);
}

VcuContext &
NpuTop::vcu_context_for(uint8_t hart_id)
{
    return vcu.contexts[hart_id];
}

uint8_t
NpuTop::decode_eew_bytes(uint64_t encoding)
{
    switch (encoding & 0x7) {
      case 0: return 1;
      case 1: return 2;
      case 2: return 4;
      case 3: return 8;
      default: throw std::invalid_argument("unsupported EEW encoding");
    }
}

} // namespace npu_mvp
