#include "dev/npu/npu_storage.hh"

#include <algorithm>
#include <stdexcept>

namespace npu_mvp
{

SparseMemory::SparseMemory(uint64_t size, uint64_t page_size)
    : size(size), page_size(page_size)
{
    if (size == 0 || page_size == 0)
        throw std::invalid_argument("GM size and page size must be non-zero");
}

void
SparseMemory::read(uint64_t address, std::vector<uint8_t> &data) const
{
    if (data.size() > size || address > size - data.size())
        throw std::out_of_range("GM read exceeds storage capacity");

    for (size_t index = 0; index < data.size(); ++index) {
        const uint64_t current = address + index;
        const uint64_t page = current / page_size;
        const uint64_t offset = current % page_size;
        const auto found = pages.find(page);
        data[index] = found == pages.end() ? 0 : found->second[offset];
    }
}

void
SparseMemory::write(uint64_t address, const std::vector<uint8_t> &data)
{
    if (data.size() > size || address > size - data.size())
        throw std::out_of_range("GM write exceeds storage capacity");

    for (size_t index = 0; index < data.size(); ++index) {
        const uint64_t current = address + index;
        const uint64_t page = current / page_size;
        const uint64_t offset = current % page_size;
        auto &page_data = pages[page];
        if (page_data.empty())
            page_data.resize(page_size, 0);
        page_data[offset] = data[index];
    }
}

FlatMemory::FlatMemory(uint64_t size) : bytes(size, 0)
{
}

void
FlatMemory::read(uint64_t address, std::vector<uint8_t> &data) const
{
    if (data.size() > bytes.size() || address > bytes.size() - data.size())
        throw std::out_of_range("UB read exceeds storage capacity");
    std::copy_n(bytes.begin() + address, data.size(), data.begin());
}

void
FlatMemory::write(uint64_t address, const std::vector<uint8_t> &data)
{
    if (data.size() > bytes.size() || address > bytes.size() - data.size())
        throw std::out_of_range("UB write exceeds storage capacity");
    std::copy(data.begin(), data.end(), bytes.begin() + address);
}

} // namespace npu_mvp
