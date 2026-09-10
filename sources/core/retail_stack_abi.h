#pragma once

#include <cstdint>

namespace as1 { namespace core
{
    inline std::uint32_t retailReadStackDword(const void* address) noexcept
    {
        // Non-32-bit builds use a portability fallback. The release target is Win32/32-bit.
        (void)address;
        return 0u;
    }
} }
