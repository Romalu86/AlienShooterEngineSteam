#pragma once

#include <cstddef>
#include <cstdint>

namespace as1
{
    class Crc32
    {
    public:
        Crc32() = default;
        Crc32(const void* data, unsigned int size);

        std::uint32_t Update(const void* data, unsigned int size);
        void Reset();
        std::uint32_t Value() const;
        operator unsigned int() const;

        static std::uint32_t UpdateValue(std::uint32_t crc, const void* data, unsigned int size);

    private:
        std::uint32_t m_crc = 0;
    };

    std::uint32_t updateCrc32Bytes(std::uint32_t& crc, const void* data, int size);
    std::uint32_t UpdateCrc32(std::uint32_t& crc, const void* data, unsigned int size);
}
