#pragma once

#include "sprite.h"

namespace as1
{
    class CANNON : public SPRITE
    {
    public:
        CANNON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~CANNON() override = default;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void drawBaseDebugOverlayThunk();
        void DeletePointerToSprite(SPRITE* sprite) override;
        void replaceDeletedGoalAndClearReference(SPRITE* sprite) noexcept;

    private:
        int m_cannonMotionFlags = 0;
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                          
#endif
}
