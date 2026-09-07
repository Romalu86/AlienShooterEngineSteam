#pragma once

#include "unit.h"

namespace as1
{
    class CREATURE : public UNIT
    {
    public:
        CREATURE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~CREATURE() override = default;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        void clearCreatureRegionReference(SPRITE* sprite) noexcept;
        void performCreatureMovementTact() noexcept;
        SPRITE* findContainingRegion(float x, float y) noexcept;

        std::uint32_t regionTrackingFlags() const noexcept { return m_regionTrackingFlags; }
        void setRegionTrackingFlags(std::uint32_t value) noexcept { m_regionTrackingFlags = value; }
        SPRITE* currentRegion() const noexcept { return m_currentRegion; }
        void setCurrentRegion(SPRITE* value) noexcept { m_currentRegion = value; }

    private:
        friend struct CreatureRetailLayoutProbe;
        std::uint32_t m_regionTrackingFlags = 0;
        SPRITE* m_currentRegion = nullptr;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct CreatureRetailLayoutProbe
    {
        static constexpr std::size_t slot94 = offsetof(CREATURE, m_regionTrackingFlags);
        static constexpr std::size_t slot98 = offsetof(CREATURE, m_currentRegion);
    };
                                                                                      
                                                                                      
                                                                                                      
#endif
}
