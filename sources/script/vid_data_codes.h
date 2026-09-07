#pragma once
#include <cstdint>

namespace as1 { namespace script
{
    enum class VidDataCode : std::int32_t
    {
        MaxHp = 1,
        Gamma0 = 18,
        Gamma1 = 19,
        Gamma2 = 20,
        Gamma3 = 21,
        Property = 22,
        BattleRange = 23,
        MaxUnit = 25,
        Ammo = 26,
        Name = 27,
        Count = 28,
        KilledUnit = 29,
        KilledUnitArmy0 = 30,
        KilledUnitArmy1 = 31,
        KilledUnitArmy2 = 32,
        KilledUnitArmy3 = 33,
        CountArmy0 = 34,
        CountArmy1 = 35,
        CountArmy2 = 36,
        CountArmy3 = 37,
        MaxHpArmy0 = 38,
        MaxHpArmy1 = 39,
        MaxHpArmy2 = 40,
        MaxHpArmy3 = 41,
        HpCoeffArmy0 = 42,
        HpCoeffArmy1 = 43,
        HpCoeffArmy2 = 44,
        HpCoeffArmy3 = 45,
        SpriteType = 46,
        Class = 47,
        Speed = 48,
        Lifetime = 49,
        DetectRange = 50,
        WeaponAim = 51,
        ExchangeVid = 52,
        DirectionCount = 53,
        MoveMask = 54,
        BuildTime = 55,
        Hide = 56,
        NotCreateAsChild = 57,
        FrameSpeed = 58,
        Link = 59,
        Child0 = 60,
        NoChild0 = 92,
        Damage = 124,
        RecolorUnit = 125,
        RecolorUnitArmy0 = 126,
        RecolorUnitArmy1 = 127,
        RecolorUnitArmy2 = 128,
        RecolorUnitArmy3 = 129,
    };

    constexpr int toInt(VidDataCode code) noexcept
    {
        return static_cast<int>(code);
    }

    constexpr int VidChildFirst = toInt(VidDataCode::Child0);
    constexpr int VidChildCount = 17;
    constexpr int VidChildEnd = VidChildFirst + VidChildCount;
    constexpr int VidNoChildFirst = toInt(VidDataCode::NoChild0);
    constexpr int VidNoChildCount = 17;
    constexpr int VidNoChildEnd = VidNoChildFirst + VidNoChildCount;
} }
