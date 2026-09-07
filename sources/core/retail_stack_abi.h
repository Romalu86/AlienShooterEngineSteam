#pragma once

#include <cstdint>

namespace as1 { namespace core
{
    inline std::uint32_t retailReadStackDword(const void* address) noexcept
    {
        // Non-x86 builds use a portability fallback. The release target is Win32/x86.
        (void)address;
        return 0u;
    }
} }
