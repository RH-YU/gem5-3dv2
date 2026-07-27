#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace npu_mvp
{

class SparseMemory
{
  public:
    SparseMemory(uint64_t size, uint64_t page_size);
    void read(uint64_t address, std::vector<uint8_t> &data) const;
    void write(uint64_t address, const std::vector<uint8_t> &data);

  private:
    uint64_t size;
    uint64_t page_size;
    std::unordered_map<uint64_t, std::vector<uint8_t>> pages;
};

class FlatMemory
{
  public:
    explicit FlatMemory(uint64_t size);
    void read(uint64_t address, std::vector<uint8_t> &data) const;
    void write(uint64_t address, const std::vector<uint8_t> &data);

  private:
    std::vector<uint8_t> bytes;
};

} // namespace npu_mvp
