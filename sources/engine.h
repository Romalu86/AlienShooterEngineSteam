#pragma once

#include "unit.h"

namespace as1
{
    class ENGINE : public UNIT
    {
    public:
        ENGINE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~ENGINE() override;
        void MoveTact() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        void DrawDebugOverlay() override; // +0x18 -> drawEngineDebugState
        void DrawRelationDebugOverlay() override; // +0x1C -> drawEnginePathDebug
        void clearEngineSpriteReference(SPRITE* sprite) noexcept;

        ENGINE* scalarDeletingDestructorEngine(unsigned char flags) noexcept;
        void destroyEngineStateNoFree() noexcept;
        void drawEngineDebugState() noexcept;
        int drawEnginePathDebug() noexcept;
        int applyRepairPulseTo(SPRITE* target) noexcept;
        void updateRepairTarget() noexcept;
        void updateMaintenanceTarget() noexcept;
        int dispatchEngineActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept;
        SPRITE* findEngineChainSpecialWeaponNode() noexcept;
        int engineChainContainsArmy(int bucket) noexcept;
        int updateCombatDecision() noexcept;
        int needsMaintenanceService() noexcept;
        void stopEngineChain() noexcept;
        void setEnginePathGoalFromCoordinates(float x, float y, float z, int unused, int project2D) noexcept;

        SPRITE* chainPrevious() const noexcept { return engineChainPrevious(); }
        SPRITE* chainNext() const noexcept { return engineChainNext(); }
        int secondaryStateFlag() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::SecondaryStateFlag);
#else
            return m_secondaryStateFlag;
#endif
        }
        void setSecondaryStateFlag(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SecondaryStateFlag) = value;
#else
            m_secondaryStateFlag = value;
#endif
        }
        int productionBatchCompletionPending() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::ProductionBatchCompletionPending);
#else
            return m_productionBatchCompletionPending;
#endif
        }
        int productionSequenceId() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::ProductionSequenceId);
#else
            return m_productionSequenceId;
#endif
        }

        void setChainPrevious(SPRITE* value) noexcept { setEngineChainPrevious(value); }
        void setChainNext(SPRITE* value) noexcept { setEngineChainNext(value); }
        void setProductionBatchCompletionPending(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::ProductionBatchCompletionPending) = value;
#else
            m_productionBatchCompletionPending = value;
#endif
        }
        void setProductionSequenceId(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::ProductionSequenceId) = value;
#else
            m_productionSequenceId = value;
#endif
        }

    private:
        friend struct EngineRetailLayoutProbe;
        void releaseEngineCommandReference() noexcept;
        void releaseEngineRuntimeState() noexcept;

#if UINTPTR_MAX == 0xFFFFFFFFu
        std::array<std::uint8_t, 0xA30> m_reservedPhysicalTail94;
#else
        int m_secondaryStateFlag = 1;
        int m_productionBatchCompletionPending = 0;
        int m_productionSequenceId = -1;
#endif
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    struct EngineRetailLayoutProbe
    {
        static constexpr std::size_t tail94 = offsetof(ENGINE, m_reservedPhysicalTail94);
    };
                                                                                                              
                                                                                                    
                                                                                                  
#endif
}
