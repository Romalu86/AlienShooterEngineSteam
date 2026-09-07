#pragma once

#include "avia.h"

namespace as1
{
    class BALLOON : public AVIA
    {
    public:
        BALLOON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~BALLOON() override = default;
        void MoveTact() override;
        void updateAltitudeState() noexcept override; // +24 -> updateAltitudeAndAttachmentState
        int updateFlightAuxiliaryBehavior() noexcept override;  // +28 -> updateBalloonBehavior
        int updateFlightCombatBehavior() noexcept override;  // +38 -> updateBalloonCombatBehavior
        int acquireNearestAttachmentTarget() noexcept;
        void attachToGoalTarget() noexcept;
        void updateAltitudeAndAttachmentState() noexcept;
        int isAtGoalOrPathComplete() const noexcept;
        void updateBalloonBehavior() noexcept;
        void updateBalloonCombatBehavior() noexcept;
        int setBalloonCommandTarget(int action, SPRITE* target) noexcept;
        void performBalloonMovementTact() noexcept;

        std::uint32_t attachmentTransitionPending() const noexcept { return m_attachmentTransitionPending; }
        std::uint8_t attachmentPhase() const noexcept { return m_attachmentPhase; }
        void setAttachmentPhase(std::uint8_t value) noexcept { m_attachmentPhase = value; }

    private:
        friend struct BalloonRetailLayoutProbe;

        std::uint32_t m_attachmentTransitionPending = 0;
        std::uint8_t m_attachmentPhase = 0;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct BalloonRetailLayoutProbe
    {
        static constexpr std::size_t slot94 = offsetof(BALLOON, m_attachmentTransitionPending);
        static constexpr std::size_t attachmentPhaseOffset = offsetof(BALLOON, m_attachmentPhase);
    };
                                                                                    
                                                                                                   
                                                                                                    
#endif
}
