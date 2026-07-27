#include "dev/npu/npu_device.hh"
#include "dev/npu/npu_command.hh"

#include "mem/packet.hh"
#include "mem/request.hh"
#include "systemc/ext/systemc"
#include "systemc/tlm_bridge/gem5_to_tlm.hh"
#include "systemc/ext/tlm_utils/simple_initiator_socket.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace
{

constexpr uint64_t GmA = 0x0000000000000000ULL;
constexpr uint64_t GmB = 0x0000000000001000ULL;
constexpr uint64_t GmC = 0x0000000000002000ULL;
constexpr uint64_t UbA = 0x0000000100000000ULL;
constexpr uint64_t UbB = 0x0000000100000400ULL;
constexpr uint64_t UbC = 0x0000000100000800ULL;
constexpr uint64_t ElementCount = 64;
constexpr uint64_t ByteCount = ElementCount * sizeof(uint32_t);
constexpr gem5::Addr CommandAperture = 0x20000000ULL;

[[noreturn]] void
fail(const char *message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void
require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

std::vector<uint8_t>
encode(const std::vector<uint32_t> &values)
{
    std::vector<uint8_t> data(values.size() * sizeof(uint32_t));
    for (size_t index = 0; index < values.size(); ++index) {
        const uint32_t value = values[index];
        for (size_t byte = 0; byte < sizeof(value); ++byte)
            data[index * sizeof(value) + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
    return data;
}

std::vector<uint32_t>
decode(const std::vector<uint8_t> &data)
{
    std::vector<uint32_t> values(data.size() / sizeof(uint32_t));
    for (size_t index = 0; index < values.size(); ++index) {
        uint32_t value = 0;
        for (size_t byte = 0; byte < sizeof(value); ++byte)
            value |= static_cast<uint32_t>(data[index * sizeof(value) + byte]) << (byte * 8);
        values[index] = value;
    }
    return values;
}

npu_mvp::NpuCommand
make_command(npu_mvp::Opcode opcode)
{
    npu_mvp::NpuCommand command;
    command.opcode = opcode;
    return command;
}

void
submit(tlm_utils::simple_initiator_socket<class TestHarness, 64> &source,
       const npu_mvp::NpuCommand &command)
{
    std::array<uint8_t, sizeof(uint64_t)> data = {};
    auto request = std::make_shared<gem5::Request>(
        CommandAperture, data.size(),
        gem5::Request::UNCACHEABLE | gem5::Request::STRICT_ORDER,
        gem5::Request::funcRequestorId);
    gem5::Packet packet(request, gem5::MemCmd::WriteReq);
    packet.dataStatic(data.data());
    packet.pushSenderState(new npu_mvp::NpuCommandSenderState(command));

    auto *transaction = sc_gem5::packet2payload(&packet);
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    source->b_transport(*transaction, delay);
    require(transaction->is_response_ok(), "NPU TLM command was not accepted");
    transaction->release();

    delete packet.popSenderState();
}

class TestHarness : public sc_core::sc_module
{
  public:
    npu_mvp::NpuDevice npu;
    tlm_utils::simple_initiator_socket<TestHarness, 64> command_source;

    TestHarness(sc_core::sc_module_name name, npu_mvp::NpuConfig config)
        : sc_core::sc_module(name), npu("npu", config), command_source("command_source")
    {
        command_source.bind(npu.top().command_target);
    }
};

} // anonymous namespace

int
sc_main(int, char *[])
{
    npu_mvp::NpuConfig config;
    config.scheduler_queue_depth = 32;
    TestHarness harness("harness", config);

    std::vector<uint32_t> a(ElementCount);
    std::vector<uint32_t> b(ElementCount);
    std::vector<uint32_t> expected(ElementCount);
    for (uint32_t index = 0; index < ElementCount; ++index) {
        a[index] = index * 3;
        b[index] = 1000 + index;
        expected[index] = a[index] + b[index];
    }
    harness.npu.top().write_gm_for_test(GmA, encode(a));
    harness.npu.top().write_gm_for_test(GmB, encode(b));

    auto nsetvl = make_command(npu_mvp::Opcode::Nsetvl);
    nsetvl.rs1_value = ElementCount;
    nsetvl.rs2_value = 2;
    submit(harness.command_source, nsetvl);

    auto mte4_a = make_command(npu_mvp::Opcode::Mte4);
    mte4_a.rd_value = ByteCount;
    mte4_a.rs1_value = GmA;
    mte4_a.rs2_value = UbA;
    submit(harness.command_source, mte4_a);

    auto mte4_b = make_command(npu_mvp::Opcode::Mte4);
    mte4_b.rd_value = ByteCount;
    mte4_b.rs1_value = GmB;
    mte4_b.rs2_value = UbB;
    submit(harness.command_source, mte4_b);

    sc_core::sc_start(sc_core::sc_time(200, sc_core::SC_NS));
    require(harness.npu.top().scope_complete(npu_mvp::SyncScope::Mte4, harness.npu.top().scope_watermark()),
            "MTE4 commands did not complete");

    auto vload_a = make_command(npu_mvp::Opcode::Vload);
    vload_a.rd = 0;
    vload_a.rs1_value = UbA;
    submit(harness.command_source, vload_a);

    auto vload_b = make_command(npu_mvp::Opcode::Vload);
    vload_b.rd = 1;
    vload_b.rs1_value = UbB;
    submit(harness.command_source, vload_b);

    auto vadd = make_command(npu_mvp::Opcode::Vadd);
    vadd.rd = 2;
    vadd.rs1 = 0;
    vadd.rs2 = 1;
    submit(harness.command_source, vadd);

    auto vstore = make_command(npu_mvp::Opcode::Vstore);
    vstore.rs1_value = UbC;
    vstore.rs2 = 2;
    submit(harness.command_source, vstore);

    sc_core::sc_start(sc_core::sc_time(200, sc_core::SC_NS));
    require(harness.npu.top().scope_complete(npu_mvp::SyncScope::Vcu, harness.npu.top().scope_watermark()),
            "VCU commands did not complete");

    auto mte2 = make_command(npu_mvp::Opcode::Mte2);
    mte2.rd_value = ByteCount;
    mte2.rs1_value = UbC;
    mte2.rs2_value = GmC;
    submit(harness.command_source, mte2);

    sc_core::sc_start(sc_core::sc_time(200, sc_core::SC_NS));
    require(harness.npu.top().scope_complete(npu_mvp::SyncScope::Mte2, harness.npu.top().scope_watermark()),
            "MTE2 command did not complete");
    require(decode(harness.npu.top().read_gm_for_test(GmC, ByteCount)) == expected,
            "vector addition result differs from reference");

    auto invalid_mte4 = make_command(npu_mvp::Opcode::Mte4);
    invalid_mte4.rd_value = ByteCount;
    invalid_mte4.rs1_value = UbA;
    invalid_mte4.rs2_value = UbB;
    submit(harness.command_source, invalid_mte4);
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    require(harness.npu.top().fault_count() == 1, "invalid MTE4 physical region was not faulted");

    std::cout << "PASS: NPU MVP vector add, CPU packet bridge, and physical-address checks" << '\n';
    return 0;
}
