#include "balloon.h"
#include "core/application.h"
#include "constant.h"
#include "graphics/angle.h"
#include "sprite_collector_hash.h"
#include "map.h"
#include "vid/vid.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace as1
{
    BALLOON::BALLOON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : AVIA(owner, vid, xyz, direction, parent)
    {

        m_attachmentPhase = 0;
        m_attachmentTransitionPending = 0;
    }
    void BALLOON::updateAltitudeState() noexcept
    {
        updateAltitudeAndAttachmentState();
    }

    int BALLOON::updateFlightAuxiliaryBehavior() noexcept
    {
        updateBalloonBehavior();
        return 0;
    }

    int BALLOON::updateFlightCombatBehavior() noexcept
    {
        updateBalloonCombatBehavior();
        return 0;
    }

    void BALLOON::MoveTact()
    {

        performBalloonMovementTact();
    }

    namespace
    {
        bool balloonTargetMatches(const BALLOON* self, const SPRITE* target) noexcept
        {
            if (!target)
                return false;
            VID* const targetVid = target->Vid();
            if (targetVid->linkedVid() != self->Vid())
                return false;
            return self->sameArmy(*target);
        }

        bool balloonTargetHasMatchingLinkChild(const SPRITE* target) noexcept
        {
            SPRITE* const child = target->childChain();
            return child && child->Vid() == target->Vid()->linkedVid();
        }

        bool balloonX87LessOrUnordered(long double lhs, long double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        bool balloonX87EqualOrUnordered(long double lhs, long double rhs) noexcept
        {

            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        bool balloonX87LessEqualOrUnordered(long double lhs, long double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs <= rhs;
        }

        bool balloonX87LessOrUnordered(double lhs, double rhs) noexcept
        {
            return balloonX87LessOrUnordered(static_cast<long double>(lhs), static_cast<long double>(rhs));
        }

        bool balloonX87EqualOrUnordered(double lhs, double rhs) noexcept
        {
            return balloonX87EqualOrUnordered(static_cast<long double>(lhs), static_cast<long double>(rhs));
        }

        bool balloonX87LessEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return balloonX87LessEqualOrUnordered(static_cast<long double>(lhs), static_cast<long double>(rhs));
        }

        bool balloonUcomissNotEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs != rhs;
        }

        bool balloonOrderedGreaterEqual(double lhs, double rhs) noexcept
        {
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs >= rhs;
        }

        double balloonTargetDistance(const BALLOON* self, const SPRITE* candidate) noexcept
        {
            const double dx = std::fabs(static_cast<double>(candidate->X()) - static_cast<double>(self->X()));
            const double dy = std::fabs(static_cast<double>(candidate->Y()) - static_cast<double>(self->Y()));
            return (std::isnan(dx) || std::isnan(dy) || dx <= dy)
                ? dx * 0.5 + dy
                : dx + dy * 0.5;
        }
    }

    int BALLOON::acquireNearestAttachmentTarget() noexcept
    {

        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE* best = nullptr;
        int cursor = hash->overflowCount() - 1;
        hash->setReverseCursor(cursor);
        while (cursor >= 0)
        {
            SPRITE* const candidate = hash->overflowSpriteAt(cursor);
            if (!candidate)
                break;

            if (balloonTargetMatches(this, candidate) &&
                !balloonTargetHasMatchingLinkChild(candidate) &&
                candidate->balloonTargetBusy() == 0)
            {
                if (!best)
                {
                    best = candidate;
                }
                else
                {
                    const double candidateDistance = balloonTargetDistance(this, candidate);
                    const double bestDistance = balloonTargetDistance(this, best);
                    // Final `test ah,1` accepts less-than and unordered.
                    if (std::isnan(candidateDistance) || std::isnan(bestDistance) ||
                        candidateDistance < bestDistance)
                        best = candidate;
                }
            }

            cursor = hash->reverseCursor() - 1;
            hash->setReverseCursor(cursor);
        }

        if (!best)
        {

            return static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hash)));
        }
        best->setBalloonTargetBusy(1);
        return Move(best);
    }

    void BALLOON::attachToGoalTarget() noexcept
    {

        SPRITE* const target = goalSprite();
        if (balloonTargetMatches(this, target))
        {
            if (balloonTargetHasMatchingLinkChild(target))
            {
                setBalloonCommandTarget(0, nullptr);
                setAttachmentPhase(1u);
                return;
            }

            setZSpeedDirect(0.0f);
            const VECTOR& link = target->Vid()->linkOffset();
            ChangeCoor(target->X() + link.x, target->Y() + link.y, target->Z() + link.z);

            setBehaviorFlags(target->Action(static_cast<int>(ActionCode::ACT_GET_BEHAVE), 0, 0, 0));
            target->insertChildChainHead(this);
            target->setBalloonTargetBusy(1);
            setBalloonCommandTarget(0, nullptr);
            return;
        }

        ChangeAnimation(15);
    }

    void BALLOON::updateAltitudeAndAttachmentState() noexcept
    {

        VID* const vid = Vid();
        const float ground = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()}, Direction());

        if (childBacklink())
        {
            setAttachmentPhase(0u);
        }
        else if (SPRITE* const target = goalSprite())
        {
            if (balloonTargetMatches(this, target))
                target->setBalloonTargetBusy(1);
        }

        if (attachmentPhase() != 0u)
        {
            if (attachmentPhase() == 1u)
            {
                DWORD flags = runtimeFlags() & ~SPRITE::MovementStartedFlag;
                setZSpeedDirect(vid->maximumZSpeed());
                setSpeedDirect(0.0f);
                if (balloonOrderedGreaterEqual(
                        static_cast<double>(Z()),
                        static_cast<double>(ground + vid->moveUpZ())))
                {
                    flags |= SPRITE::MovementStartedFlag;
                    setAttachmentPhase(0u);
                }
                setRuntimeFlags(flags);
                return;
            }

            if (attachmentPhase() == 2u)
            {
                setRuntimeFlags(runtimeFlags() & ~SPRITE::MovementStartedFlag);
                setSpeedDirect(0.0f);
                setZSpeedDirect(-vid->maximumZSpeed());
                SPRITE* const target = goalSprite();
                if (!balloonTargetMatches(this, target) || balloonTargetHasMatchingLinkChild(target))
                {
                    setAttachmentPhase(1u);
                    return;
                }

                ChangeCoor(target->X(), target->Y(), Z());
                const std::uint32_t delta = std::max(
                    core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds(),
                    static_cast<std::uint32_t>(vid->defaultFrameSpeed()));
                RotateTact(target->directionIndex(), delta);
                if (target->Z() + target->Vid()->linkOffset().z >= Z())
                    attachToGoalTarget();
                return;
            }
        }

        if (childBacklink())
        {
            setZSpeedDirect(0.0f);
            return;
        }

        const float center = ground + vid->moveUpZ();
        if (center - 10.0f > Z())
            setZSpeedDirect(vid->maximumZSpeed());
        else if (balloonX87LessEqualOrUnordered(
                     static_cast<double>(Z()),
                     static_cast<double>(center + 10.0f)))
            setZSpeedDirect(0.0f);
        else
            setZSpeedDirect(-vid->maximumZSpeed());
    }

    int BALLOON::isAtGoalOrPathComplete() const noexcept
    {

        SPRITE* const target = goalSprite();
        if (target)
        {
            const double dx = std::fabs(static_cast<double>(target->X()) - static_cast<double>(X()));
            if (dx < 10.0 || std::isnan(dx))
            {
                const double dy = std::fabs(static_cast<double>(target->Y()) - static_cast<double>(Y()));
                if (dy < 10.0 || std::isnan(dy))
                    return 1;
            }
        }
        const DWORD flags = runtimeFlags();
        return (flags & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask ? 1 : 0;
    }

    void BALLOON::updateBalloonBehavior() noexcept
    {

        SPRITE* const carrier = childBacklink();
        if (carrier)
        {
            if ((core::CurrentTimeMilliseconds() & 0xFFFFFC00u) > applicationBucketTime())
                refillAmmoByCapacityFraction(static_cast<int>(GlobalBaseConstants()->raw[19]));

            SPRITE* const target = goalSprite();
            bool rotateOnly = (target == nullptr);
            if (!rotateOnly)
            {
                if (carrier->goalSprite() == target)
                {
                    const DWORD carrierMode = carrier->runtimeFlags() & SPRITE::CommandBitsMask;
                    if (carrierMode == 0x6Cu || carrierMode == 0x68u)
                        rotateOnly = true;
                }
                if (!rotateOnly)
                {
                    const long double distance = approximatePlanarDistance(
                        target->X() - carrier->X(),
                        target->Y() - carrier->Y());
                    if (!balloonX87LessOrUnordered(
                            distance,
                            static_cast<long double>(carrier->Vid()->weaponBattleRange())))
                        rotateOnly = true;
                }
                if (!rotateOnly && actionTimer() != 0u)
                    rotateOnly = true;
            }

            if (rotateOnly)
            {
                const std::uint32_t delta = std::max(
                    core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds(),
                    static_cast<std::uint32_t>(Vid()->defaultFrameSpeed()));
                if (RotateTact(ANGLE(static_cast<unsigned char>(carrier->directionIndex())), delta).Int() == 0)
                    ChangeAnimation(0);
                return;
            }

            if ((runtimeFlags() & SPRITE::CommandBitsMask) == 0x20u)
            {
                setBalloonCommandTarget(3, target);
                m_attachmentTransitionPending = 1u;
            }

            (void)deleteChildByVid(carrier->Vid()->woundChildVid());
            carrier->setChildChain(nullptr);
            carrier->setBalloonTargetBusy(0);

            SPRITE* const best = bestTargetSprite();
            if (best && best != target)
            {
                const long double distance = approximatePlanarDistance(best->X() - X(), best->Y() - Y());
                if (balloonX87LessOrUnordered(
                        distance,
                        static_cast<long double>(Vid()->weaponBattleRange())))
                    setAttackCommandForTarget(best);
            }

            if (target && target->Vid() == MAP::NullVid())
            {
                carrier->SetCommand(0, nullptr);
                SPRITE* const helper = new (std::nothrow) SPRITE(
                    mapOwner(), MAP::NullVid(), target->xyz(), ANGLE(0), nullptr);
                setAttackCommandForTarget(helper);
            }

            ChangeAnimation(2);
            m_attachmentPhase = 1u;
            setRuntimeFlags(runtimeFlags() & ~SPRITE::MovementStartedFlag);
            setChildBacklink(nullptr);
            return;
        }

        SPRITE* const best = bestTargetSprite();
        SPRITE* target = goalSprite();
        if (best && best != target)
        {
            const double distance = balloonTargetDistance(this, best);
            if (balloonX87LessOrUnordered(
                    static_cast<long double>(distance),
                    static_cast<long double>(Vid()->weaponDetectRange())) &&
                ammoCount() > 0)
                setAttackCommandForTarget(best);
        }

        if (ammoCount() <= 0 || !target)
        {
            bool keepTarget = false;
            if (target)
            {
                VID* const linkVid = target->Vid()->linkedVid();
                const bool sameLink = linkVid == Vid();
                const bool sameArmy = this->sameArmy(*target);
                SPRITE* const targetChild = target->childChain();
                keepTarget = sameLink && sameArmy && (!targetChild || targetChild->Vid() != linkVid);
                if (!keepTarget)
                    setBalloonCommandTarget(0, nullptr);
            }
            if (!keepTarget)
                acquireNearestAttachmentTarget();
        }

        target = goalSprite();
        if (target)
        {
            VID* const linkVid = target->Vid()->linkedVid();
            if (linkVid == Vid() &&
                this->sameArmy(*target))
            {
                SPRITE* const targetChild = target->childChain();
                if (targetChild && targetChild->Vid() == linkVid)
                {
                    setBalloonCommandTarget(0, nullptr);
                    acquireNearestAttachmentTarget();
                }
            }
        }

        if (m_attachmentPhase != 2u && (runtimeFlags() & SPRITE::CommandBitsMask) == 0x04u && isAtGoalOrPathComplete())
        {
            target = goalSprite();
            if (target)
            {
                VID* const linkVid = target->Vid()->linkedVid();
                if (linkVid == Vid() &&
                    this->sameArmy(*target))
                {
                    SPRITE* const targetChild = target->childChain();
                    if (!targetChild || targetChild->Vid() != linkVid)
                    {
                        m_attachmentPhase = 2u;
                        updateAltitudeState();
                    }
                }
            }
        }
    }

    void BALLOON::updateBalloonCombatBehavior() noexcept
    {

        SPRITE* const target = goalSprite();
        if (!balloonTargetMatches(this, target))
        {
            const std::uint32_t delta = std::max(
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds(),
                static_cast<std::uint32_t>(Vid()->defaultFrameSpeed()));
            setAttackDecisionCode(computeAttackDecisionCode(delta));
        }

        SPRITE* const current = goalSprite();
        if (attachmentTransitionPending() != 0u && current &&
            balloonX87LessOrUnordered(std::fabs(static_cast<double>(current->X()) - static_cast<double>(X())), 10.0) &&
            balloonX87LessOrUnordered(std::fabs(static_cast<double>(current->Y()) - static_cast<double>(Y())), 10.0))
        {
            ChangeAnimation(15);
            return;
        }

        if (balloonTargetMatches(this, current) &&
            ammoCount() > 0 &&
            (behaviorFlags() & 1) != 0)
        {
            if (SPRITE* const candidate = SeekEnemy())
                setBalloonCommandTarget(4, candidate);
        }
    }

    int BALLOON::setBalloonCommandTarget(int action, SPRITE* target) noexcept
    {

        SPRITE* const previous = goalSprite();
        m_attachmentTransitionPending = 0;
        if (balloonTargetMatches(this, previous))
            previous->setBalloonTargetBusy(0);
        if (balloonTargetMatches(this, target))
            target->setBalloonTargetBusy(1);
        return SetCommand(action, target);
    }

    void BALLOON::performBalloonMovementTact() noexcept
    {

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        if (attachmentPhase() != 0u)
        {
            ChangeCoor(X(), Y(), candidate.z);
            return;
        }

        if (SPRITE* const target = goalSprite())
        {
            if (balloonUcomissNotEqualOrUnordered(Speed(), 0.0))
            {
                const int reverse = Speed() < 0.0f ? 0x80 : 0;
                const int desired = (RetailDirectionFromFloatXY(
                    target->X() - X(), target->Y() - Y()).Int() + reverse) & 0xFF;
                const int turn = GlideDirection(ANGLE(static_cast<unsigned char>(desired))).Int();
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                RotateTact(turn, deltaMs);
            }
        }

        if ((balloonUcomissNotEqualOrUnordered(X(), candidate.x) ||
             balloonUcomissNotEqualOrUnordered(Y(), candidate.y)) &&
            CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            steerAwayFromMapBoundary(candidate.x, candidate.y);
            ChangeCoor(candidate.x, candidate.y, candidate.z);
        }
    }

}
