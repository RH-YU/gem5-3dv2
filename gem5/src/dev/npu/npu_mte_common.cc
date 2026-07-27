#include "dev/npu/npu_mte.hh"

#include <iostream>
#include <stdexcept>

namespace npu_mvp
{

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

} // namespace npu_mvp
