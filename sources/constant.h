#pragma once
#include "core/types.h"
#include <array>
#include <cstddef>

namespace as1
{
    class RESOURCE;

    struct BASE_CONSTANTS
    {
        static constexpr std::size_t AS1_DWORD_COUNT = 26;
        static constexpr std::size_t AS1_RECORD_SIZE = AS1_DWORD_COUNT * sizeof(DWORD);

        std::array<DWORD, AS1_DWORD_COUNT> raw;

        BASE_CONSTANTS* Load(RESOURCE* res);
    };

                                                                                                               

    inline BASE_CONSTANTS* loadBaseConstantsFromResource(BASE_CONSTANTS* owner, RESOURCE* res)
    {
        return owner ? owner->Load(res) : nullptr;
    }

    extern BASE_CONSTANTS* g_baseConstants;

    inline BASE_CONSTANTS* GlobalBaseConstants() noexcept { return g_baseConstants; }
    inline void BindGlobalBaseConstants(BASE_CONSTANTS* constants) noexcept { g_baseConstants = constants; }
}
