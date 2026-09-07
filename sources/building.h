#pragma once

#include "unit.h"

namespace as1
{
    class BUILDING : public UNIT
    {
    public:
        BUILDING(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~BUILDING() override = default;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void updateBuildingAnimationTact() noexcept;
        void serviceNearbyEngine() noexcept;

        std::uintptr_t pendingBuildVidHandle() const noexcept { return m_pendingBuildVidHandle; }
        std::uint32_t serviceActivityFlag() const noexcept { return m_serviceActivityFlag; }
        std::uint32_t lastServiceTargetHandle() const noexcept { return m_lastServiceTargetHandle; }

    private:
        friend struct BuildingRetailLayoutProbe;
        std::uintptr_t m_pendingBuildVidHandle = 0; // +0x94
        int m_buildPositionX;                       // +0x98
        int m_buildPositionY;                       // +0x9C
        std::uint32_t m_serviceActivityFlag = 0;        // +0xA0
        std::uint32_t m_lastServiceTargetHandle = 0;    // +0xA4
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct BuildingRetailLayoutProbe
    {
        static constexpr std::size_t slot94 = offsetof(BUILDING, m_pendingBuildVidHandle);
        static constexpr std::size_t slot98 = offsetof(BUILDING, m_buildPositionX);
        static constexpr std::size_t slot9C = offsetof(BUILDING, m_buildPositionY);
        static constexpr std::size_t slotA0 = offsetof(BUILDING, m_serviceActivityFlag);
        static constexpr std::size_t slotA4 = offsetof(BUILDING, m_lastServiceTargetHandle);
    };
                                                                                      
                                                                                      
                                                                                      
                                                                                      
                                                                                      
                                                                                                      
#endif
}
