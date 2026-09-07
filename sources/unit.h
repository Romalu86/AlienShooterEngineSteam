#pragma once

#include "sprite.h"

namespace as1
{
    class UNIT : public TERRAIN
    {
    public:
        UNIT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~UNIT() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void DrawDebugOverlay() override;
        void drawBaseDebugOverlayThunk();

    private:
        friend struct UnitRetailLayoutProbe;
        int m_legacyCommandState0;             // +0x7C = 0
        int m_legacyCommandState1;             // +0x80 = 0
        int m_ammoFixedPoint;                  // +0x84 fixed-point weapon counter
        int m_legacyCommandState2;             // +0x88 = -1
        int m_turnTimer;                       // +0x8C
        int m_behaviorFlags;                   // +0x90
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct UnitRetailLayoutProbe
    {
        static constexpr std::size_t slot7C = offsetof(UNIT, m_legacyCommandState0);
        static constexpr std::size_t slot80 = offsetof(UNIT, m_legacyCommandState1);
        static constexpr std::size_t slot84 = offsetof(UNIT, m_ammoFixedPoint);
        static constexpr std::size_t slot88 = offsetof(UNIT, m_legacyCommandState2);
        static constexpr std::size_t slot8C = offsetof(UNIT, m_turnTimer);
        static constexpr std::size_t slot90 = offsetof(UNIT, m_behaviorFlags);
    };
                                                                                         
                                                                              
                                                                              
                                                                              
                                                                              
                                                                              
                                                                              
                                                                                              
#endif
}
