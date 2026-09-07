#pragma once

#include <cstdint>
#include "script/action_constants.h"

namespace as1
{
    namespace SpriteActConst
    {

        constexpr std::int32_t ACT_RESTORE_OLD_MAP = 0xC8;
        constexpr std::int32_t ACT_RESTORE = static_cast<std::int32_t>(ActionCode::ACT_RESTORE);

        constexpr bool isRestoreActionOpcode(std::int32_t opcode)
        {
            return opcode == ACT_RESTORE_OLD_MAP || opcode == ACT_RESTORE;
        }

        constexpr const char* restoreActionOpcodeName(std::int32_t opcode)
        {
            return opcode == ACT_RESTORE_OLD_MAP ? "ACT_RESTORE_OLD_MAP" :
                   opcode == ACT_RESTORE ? "ACT_RESTORE" : nullptr;
        }
    }
}
