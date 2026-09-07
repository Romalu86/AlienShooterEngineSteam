#pragma once

#include "sprite.h"
#include "core/weak_controller.h"

namespace as1
{
    class RAIL : public TERRAIN
    {
    public:
        RAIL(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~RAIL() override;
        RAIL* railScalarDeletingDestructor(unsigned char flags) noexcept;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        int dispatchRailActionOpcode(int opcode, int actionArgument1, float actionArgument2, int actionArgument3);
        int handleRailDamageAction(int actionArgument1, float actionArgument2, int actionArgument3);
        int rebuildRailNodes(int direction, float moveUpZ);
        void destroyRailState() noexcept;
        void handleRailNodeReleased(std::uintptr_t ownerHandle) noexcept;
        void setRailNodes(core::WeakController* slot78, core::WeakController* slot7C) noexcept;
        core::WeakController* firstRailNode() const noexcept { return m_firstRailNode; }
        core::WeakController* secondRailNode() const noexcept { return m_secondRailNode; }

    private:
        // TERRAIN owns +0x74/+0x78; RAIL node owners begin at +0x7C.
        core::WeakController* m_firstRailNode = nullptr;
        core::WeakController* m_secondRailNode = nullptr;
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                      
#endif
}
