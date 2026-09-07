#pragma once

#include "unit.h"

namespace as1
{
    class AVIA : public UNIT
    {
    public:
        AVIA(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;

        virtual int updateFlightBehavior(int behaviorArgument1 = 0, int behaviorArgument2 = 0) noexcept; // +20
        virtual void updateAltitudeState() noexcept;                          // +24
        virtual int updateFlightAuxiliaryBehavior() noexcept;
        virtual int probeForwardFlightObstacle() noexcept;                           // +2C
        virtual void chooseFlightAvoidanceTurn() noexcept;                          // +30
        virtual int faceFlightTargetAndUpdateCombat() noexcept;                           // +34
        virtual int updateFlightCombatBehavior() noexcept;                           // +38
        virtual int updateFlightIdleBehavior() noexcept;                           // +3C
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                              
#endif
}
