#pragma once
#include <cstdint>
#include <string>

#ifndef AS_FOURCC
#define AS_FOURCC(ch0, ch1, ch2, ch3) \
  ((std::uint32_t)(std::uint8_t)(ch0) \
  | ((std::uint32_t)(std::uint8_t)(ch1) << 8) \
  | ((std::uint32_t)(std::uint8_t)(ch2) << 16) \
  | ((std::uint32_t)(std::uint8_t)(ch3) << 24))
#endif

namespace as1
{
    inline std::string fourccToString(std::uint32_t v)
    {
        char s[5] = {
            static_cast<char>(v & 0xff),
            static_cast<char>((v >> 8) & 0xff),
            static_cast<char>((v >> 16) & 0xff),
            static_cast<char>((v >> 24) & 0xff),
            0
        };
        return std::string(s, 4);
    }
}
