#include "input/control_actions.h"
#include "input.h"
#include "script/logic_runtime.h"

#include <cstdint>

namespace as1 { namespace input
{
    namespace
    {
        std::uint32_t signExtendedFirstByte(const STRING& text, std::uint32_t fallback) noexcept
        {
            const char* value = text.c_str();
            if (!value || !value[0])
                return fallback;
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(value[0])));
        }
    }

    void ApplyControlConfiguration(const as1::core::ControlConfiguration& config)
    {
        applyStartupControlProfileBlock(config);
    }

    void applyStartupControlProfileBlock(const as1::core::ControlConfiguration& config)
    {

        InputControlKeys& keys = inputControlKeys();
        keys.left1 = signExtendedFirstByte(config.left, keys.left1);
        keys.up1 = signExtendedFirstByte(config.up, keys.up1);
        keys.right1 = signExtendedFirstByte(config.right, keys.right1);
        keys.down1 = signExtendedFirstByte(config.down, keys.down1);
        relativeControlEnabled() = config.relative;
        keys.first0 = static_cast<std::uint32_t>(as1::script::decodeLogicKeyName(config.firstAction));
        keys.second0 = static_cast<std::uint32_t>(as1::script::decodeLogicKeyName(config.secondAction));
        keys.previousWeapon = static_cast<std::uint32_t>(as1::script::decodeLogicKeyName(config.previousWeapon));
        keys.nextWeapon = static_cast<std::uint32_t>(as1::script::decodeLogicKeyName(config.nextWeapon));
    }
} }
