#include "civ_robot.h"
#include "map.h"
#include "sprite_collector_hash.h"
#include "graphics/angle.h"
#include "vid/vid.h"
#include "core/log.h"
#include "core/application.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <limits>

namespace as1
{
    namespace
    {
        int civConvertFloatToInt32(long double value) noexcept
        {
            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(value)))));
        }

        int civSubtractAndConvertToInt32(float lhs, float rhs) noexcept
        {
            return civConvertFloatToInt32(static_cast<long double>(lhs) - static_cast<long double>(rhs));
        }

        int civSubtractRoundedFloatAndConvertToInt32(float lhs, float rhs) noexcept
        {
            const float rounded = static_cast<float>(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            return civConvertFloatToInt32(static_cast<long double>(rounded));
        }
    }
    CIV_ROBOT::CIV_ROBOT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : CREATURE(owner, vid, xyz, direction, parent)
    {
        m_retainedTargetSprite = nullptr;
        m_behaviorState = 0;
        m_damageReactionPending = 0;
    }

    CIV_ROBOT::~CIV_ROBOT()
    {
        if (m_retainedTargetSprite)
        {
            const VID* const vid = m_retainedTargetSprite->Vid();
            LOG::ResourceError("SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, vid ? vid->nVid : -1);
        }
    }

    int CIV_ROBOT::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int actionArgument1 = static_cast<int>(argument1Carrier);
        const int actionArgument2 = argument2Carrier;
        const int actionArgument3 = argument3Carrier;

        switch (opcode)
        {
        case 9:
        {
            if (currentAnimation() == 8)
                return 0;
            SPRITE* const child = childChain();
            VID* const vid = Vid();
            if (child && child->Vid() == vid->linkedVid())
                child->ChangeAnimation(9);
            else
                changeLinkedChildAnimationWhenIdle(9);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_MOVE_TO):
        {
            SPRITE* const source = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(actionArgument1)));
            if (!source)
                return 0;
            VID* const sourceVid = source->Vid();
            const int direction = source->directionIndex();
            const float x = source->X() + rawDirectionSin(direction) * 4.0f;
            const float y = source->Y() - rawDirectionCos(direction) * 4.0f;
            const float z = source->Z() + sourceVid->moveUpZ();
            SPRITE* const helper = new (std::nothrow) SPRITE(
                mapOwner(), MAP::NullVid(), VECTOR{x, y, z}, ANGLE(0), nullptr);
            Move(helper);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
            if (actionArgument1 > 0)
            {
                for (SPRITE* candidate = GlobalHashFirstInBoxAroundDot(
                         X() - 150.0f, Y() - 150.0f, X() + 150.0f, Y() + 300.0f);
                     candidate;
                     candidate = GlobalHashNextInBoxAroundDot())
                {
                    VID* const candidateVid = candidate->Vid();
                    if (candidateVid->spriteClassId() != B_CIV_ROBOT)
                        continue;
                    const float dx = candidate->X() - X();
                    const float dy = candidate->Y() - Y();
                    if (approximatePlanarDistance(dx, dy) < 150.0L)
                    {
                        auto* const robot = static_cast<CIV_ROBOT*>(candidate);
                        robot->m_damageReactionPending = 1u;
                        robot->m_targetRefreshPending = 1u;
                    }
                }
            }
            return dispatchBaseActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
            break;

        default:
            return CREATURE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
        }

        if (currentAnimation() >= 15)
            return 0;

        if ((regionTrackingFlags() & 1u) != 0u && currentRegion() == nullptr)
        {
            setRegionTrackingFlags(regionTrackingFlags() & ~1u);
            setCurrentRegion(findContainingRegion(X(), Y()));
        }

        if ((runtimeFlags() & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask)
            Stop();

        VID* const vid = Vid();
        const std::uint32_t elapsed = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const auto currentTurnDelta = [this, vid, elapsed]() noexcept -> std::uint32_t {
            return std::max(
                elapsed,
                static_cast<std::uint32_t>(vid->hostFrameSpeedStorage(currentAnimation())));
        };
        const std::uint32_t turnDelta = currentTurnDelta();

        int remainingTurnTicks = turnTimer();
        if (remainingTurnTicks != 0)
        {
            if (remainingTurnTicks > 0)
                RotateTact(directionIndex() - 32, turnDelta);
            else
                RotateTact(directionIndex() + 32, turnDelta);
            setTurnTimer(remainingTurnTicks >= 0 ? remainingTurnTicks - 1 : remainingTurnTicks + 1);
        }
        else if (SPRITE* const target = goalSprite())
        {
            if ((runtimeFlags() & SPRITE::CommandBitsMask) == 4u)
            {
                int desired = RetailDirectionFromFloatXY(
                    target->X() - X(), target->Y() - Y()).Int();
                if (Speed() < 0.0f)
                    desired = (desired - 128) & 0xFF;
                if (RotateTact(ANGLE(static_cast<unsigned char>(desired)), turnDelta).Int() == 0)
                    changeLinkedChildAnimationWhenIdle(2);
            }
        }

        if (currentAnimation() == 13)
            changeLinkedChildAnimationWhenIdle(0);

        const std::uint32_t actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
        if (actionBits == 4u || actionBits == 12u)
            return 0;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now - (now & 0x7FFu)) > applicationBucketTime())
            m_targetRefreshPending = 1u;

        if (m_targetRefreshPending != 0u)
        {
            m_targetRefreshPending = 0u;
            MAP* const map = mapOwner();
            SPRITE* const selected = core::Application::findNearestSpriteByFilter(
                *map, core::GlobalApplicationDrawDispatcherState(), 0x9015, X(), Y(), 350.0f);
            if (selected)
                selected->AddListReference();
            if (m_retainedTargetSprite)
                releaseSpriteListReference(m_retainedTargetSprite);
            m_retainedTargetSprite = selected;

            if (m_behaviorState == 14u)
            {
                if ((runtimeFlags() & SPRITE::MovementStartedFlag) == 0u &&
                    (commandRecordCount() == 0u || lastCommandOpcode() == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK)))
                    m_behaviorState = 15u;
                m_damageReactionPending = 0u;
            }
            else if (m_behaviorState == 15u && (std::rand() % 5) == 0)
            {
                m_behaviorState = 5u;
                SPRITE* const helper = new (std::nothrow) SPRITE(
                    mapOwner(), MAP::NullVid(), VECTOR{X(), Y() + 50.0f, Z()}, ANGLE(0), nullptr);
                Move(helper);
                m_damageReactionPending = 0u;
            }
            else if (m_behaviorState == 7u &&
                     (static_cast<unsigned>(std::rand()) & 0x80000001u) != 0u)
            {
                m_behaviorState = 7u;
                m_damageReactionPending = 0u;
            }
            else
            {
                if (m_damageReactionPending != 0u)
                {
                    if ((std::rand() % 5) != 0)
                        m_behaviorState = 7u;
                    else
                        m_behaviorState = (std::rand() % 3) != 0 ? 12u : 10u;
                }
                else
                {
                    bool selectedState = false;
                    if (m_retainedTargetSprite)
                    {
                        if ((m_behaviorState != 9u && (std::rand() % 3) == 0) ||
                            (std::rand() % 3) == 0)
                        {
                            m_behaviorState = 9u;
                            selectedState = true;
                        }
                        else if ((static_cast<unsigned>(std::rand()) & 0x80000001u) == 0u)
                        {
                            m_behaviorState = 11u;
                            selectedState = true;
                        }
                    }
                    if (!selectedState && m_behaviorState == 5u && (std::rand() % 3) != 0)
                        selectedState = true;
                    if (!selectedState && m_behaviorState == 1u && (std::rand() % 6) != 0)
                        selectedState = true;
                    if (!selectedState && m_behaviorState == 4u &&
                        (static_cast<unsigned>(std::rand()) & 0x80000001u) == 0u)
                        selectedState = true;
                    if (!selectedState && (std::rand() % 7) == 0)
                    {
                        m_behaviorState = 4u;
                        selectedState = true;
                    }
                    if (!selectedState && (std::rand() % 6) == 0)
                    {
                        playSfxAtWorldPosition(129);
                        m_behaviorState = 3u;
                        selectedState = true;
                    }
                    if (!selectedState && (std::rand() % 5) == 0)
                    {
                        playSfxAtWorldPosition(129);
                        m_behaviorState = 2u;
                        selectedState = true;
                    }
                    if (!selectedState)
                        m_behaviorState = (std::rand() % 5) != 0 ? 5u : 14u;
                }
                m_damageReactionPending = 0u;
            }
        }

        switch (m_behaviorState)
        {
        case 0:
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            rotateLinkedChildTowardDirection(static_cast<std::uint8_t>(directionIndex()));
            return 0;

        case 13:
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            ChangeDirection(directionIndex() - 32);
            rotateLinkedChildTowardDirection(static_cast<std::uint8_t>(directionIndex()));
            return 0;

        case 12:
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            changeLinkedChildAnimationWhenIdle(7);
            return 0;

        case 11:
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            if (m_retainedTargetSprite)
            {
                const int direction = RetailDirectionFromFloatXY(
                    m_retainedTargetSprite->X() - X(),
                    m_retainedTargetSprite->Y() - Y()).Int();
                rotateLinkedChildTowardDirection(static_cast<std::uint8_t>(direction));
            }
            return 0;

        case 2:
        case 3:
        {
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            SPRITE* const child = childChain();
            if (!child || child->Vid() != vid->linkedVid())
                return 0;
            const int offset = m_behaviorState == 2u ? -32 : 32;
            rotateLinkedChildTowardDirection(static_cast<std::uint8_t>(child->directionIndex() + offset));
            return 0;
        }

        case 4:
        {
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            SPRITE* const child = childChain();
            if (!child || child->Vid() != vid->linkedVid())
                return 0;
            child->RotateTact(directionIndex(), currentTurnDelta());
            changeLinkedChildAnimationWhenIdle(12);
            return 0;
        }

        case 1:
        {
            SPRITE* const target = goalSprite();
            if (!target)
                return 0;
            const float dx = std::fabs(target->X() - X());
            const float dy = std::fabs(target->Y() - Y());
            const float distance = dx <= dy ? dx * 0.5f + dy : dx + dy * 0.5f;
            if (!(distance < 150.0f))
            {
                Move(target);
                return 0;
            }
            SetCommand(0, nullptr);
            if ((std::rand() % 3) == 0)
                RotateTact(std::rand() & 0xFF, currentTurnDelta());
            if ((std::rand() % 10) == 0)
                changeLinkedChildAnimationWhenIdle(11);
            else if ((std::rand() % 10) == 0)
                changeLinkedChildAnimationWhenIdle(9);
            else if ((std::rand() % 10) != 0)
                changeLinkedChildAnimationWhenIdle(0);
            else
                changeLinkedChildAnimationWhenIdle(6);
            return 0;
        }

        case 5:
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) == 0u)
                StartMove();
            if (currentAnimation() == 2 && (std::rand() % 3) == 0)
                RotateTact(directionIndex() - 32, currentTurnDelta());
            else if (currentAnimation() == 2 && (std::rand() % 3) == 0)
                RotateTact(directionIndex() + 32, currentTurnDelta());
            else
                changeLinkedChildAnimationWhenIdle(2);
            rotateLinkedChildTowardDirection(static_cast<std::uint8_t>(directionIndex()));
            return 0;

        case 14:
        {
            const std::uint32_t commandCount = commandRecordCount();
            if (goalSprite() != nullptr ||
                (commandCount != 0u && lastCommandOpcode() != 73u))
            {
                if ((runtimeFlags() & SPRITE::MovementStartedFlag) == 0u &&
                    (commandCount == 0u || lastCommandOpcode() == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK)))
                    m_behaviorState = 15u;
                rotateLinkedChildTowardDirection(static_cast<std::uint8_t>(directionIndex()));
                return 0;
            }

            SPRITE* const candidate = findNearbyEligibleTarget();
            if (!candidate)
            {
                m_targetRefreshPending = 1u;
                return 0;
            }
            const int direction = candidate->directionIndex();
            const float x = candidate->X() - rawDirectionSin(direction) * 32.0f;
            const float y = candidate->Y() + rawDirectionCos(direction) * 32.0f;
            SPRITE* const helper = new (std::nothrow) SPRITE(
                mapOwner(), MAP::NullVid(), VECTOR{x, y, candidate->Z()}, ANGLE(0), nullptr);
            Move(helper);
            prependCommandRecord(buildCommandRecord(0x22u,
                                  static_cast<int>(reinterpret_cast<std::uintptr_t>(candidate) & 0xFFFFFFFFu),
                                  0, 0));
            return 0;
        }

        case 8:
        {
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) == 0u)
                StartMove();
            if (Speed() >= vid->maxSpeedValue())
                setSpeedDirect(vid->maxSpeedValue() * 2.0f);
            SPRITE* const child = childChain();
            if (!child || child->Vid() != vid->linkedVid())
                return 0;
            child->RotateTact(directionIndex(), currentTurnDelta());
            changeLinkedChildAnimationWhenIdle(11);
            return 0;
        }

        case 7:
        {
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) == 0u)
                StartMove();
            if (Speed() >= vid->maxSpeedValue())
                setSpeedDirect(vid->maxSpeedValue() * 2.0f);
            if (m_retainedTargetSprite)
            {
                const int direction = (RetailDirectionFromFloatXY(
                    m_retainedTargetSprite->X() - X(),
                    m_retainedTargetSprite->Y() - Y()).Int() - 128) & 0xFF;
                RotateTact(direction, currentTurnDelta());
            }
            SPRITE* const child = childChain();
            if (!child || child->Vid() != vid->linkedVid())
                return 0;
            child->RotateTact(directionIndex(), currentTurnDelta());
            changeLinkedChildAnimationWhenIdle(11);
            return 0;
        }

        case 9:
        {
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            SPRITE* const child = childChain();
            if (!child || child->Vid() != vid->linkedVid())
                return 0;
            const int direction = m_retainedTargetSprite
                ? RetailDirectionFromFloatXY(
                      m_retainedTargetSprite->X() - X(),
                      m_retainedTargetSprite->Y() - Y()).Int()
                : directionIndex();
            child->RotateTact(direction, currentTurnDelta());
            changeLinkedChildAnimationWhenIdle((std::rand() % 3) != 0 ? 9 : 11);
            return 0;
        }

        case 10:
        {
            if ((runtimeFlags() & SPRITE::MovementStartedFlag) != 0u)
                Stop();
            SPRITE* const child = childChain();
            if (!child || child->Vid() != vid->linkedVid())
                return 0;
            const int direction = m_retainedTargetSprite
                ? RetailDirectionFromFloatXY(
                      m_retainedTargetSprite->X() - X(),
                      m_retainedTargetSprite->Y() - Y()).Int()
                : directionIndex();
            child->RotateTact(direction, currentTurnDelta());
            changeLinkedChildAnimationWhenIdle(6);
            return 0;
        }

        default:
            return 0;
        }
    }

    void CIV_ROBOT::MoveTact()
    {
        performMovementTact();
    }

    void CIV_ROBOT::performMovementTact() noexcept
    {
        if ((regionTrackingFlags() & 1u) != 0u && currentRegion() == nullptr)
        {
            setRegionTrackingFlags(regionTrackingFlags() & ~1u);
            setCurrentRegion(findContainingRegion(X(), Y()));
        }

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        VID* const vid = Vid();
        MAP* const map = mapOwner();
        if (((vid->properties() >> 8) & 2u) != 0u)
            candidate.z = map->GetGroundZ(vid, VECTOR2{candidate.x, candidate.y}, Direction());

        if (!((candidate.z - Z()) <= vid->moveUpZ()) ||
            !((Z() - candidate.z) <= vid->moveDownZ()))
            goto blocked;

        if (SPRITE* const regionSprite = currentRegion())
        {
            const REGION* const region = static_cast<const REGION*>(regionSprite);
            const float halfX = region->regionWidth() * 0.5f;
            const float halfY = region->regionHeight() * 0.5f;
            if (candidate.x < region->X() - halfX || candidate.x > region->X() + halfX ||
                candidate.y < region->Y() - halfY || candidate.y > region->Y() + halfY)
            {
                SPRITE* const nextRegion = findContainingRegion(candidate.x, candidate.y);
                if (!nextRegion || !currentRegion() ||
                    nextRegion->Vid() != currentRegion()->Vid())
                    goto blocked;
                setCurrentRegion(nextRegion);
            }
        }

        if (!(candidate.x >= 0.0f) || !(candidate.x < map->SizeX()) ||
            !(candidate.y >= 0.0f) || !(candidate.y < map->SizeY()))
        {
            dispatchVirtualAction(ActionCode::ACT_PATH_LIMIT,
                                     civSubtractAndConvertToInt32(candidate.x, X()),
                                     civSubtractAndConvertToInt32(candidate.y, Y()),
                                     civSubtractAndConvertToInt32(candidate.z, Z()));
            return;
        }

        {
            SPRITE* const collision = CanPlaceWithCrush(candidate.x, candidate.y, Z());
            if (!collision)
            {
                ChangeCoor(candidate.x, candidate.y, candidate.z);
                return;
            }

            VID* const collisionVid = collision->Vid();
            {
                const bool overlapXY =
                    vid->halfSizeX() + collisionVid->halfSizeX() > std::fabs(X() - collision->X()) &&
                    vid->halfSizeY() + collisionVid->halfSizeY() > std::fabs(Y() - collision->Y());
                const bool overlapZ =
                    vid->sizeZ() + Z() >= collision->Z() &&
                    collision->Z() + collisionVid->sizeZ() >= Z();
                if (overlapXY && overlapZ)
                {
                    ChangeCoor(candidate.x, candidate.y, candidate.z);
                    return;
                }
            }

            if (SpriteHashDepoCanCreateUnitFilter(collision) && behaviorState() == 14u)
            {
                const std::uint8_t desired = static_cast<std::uint8_t>(
                    RetailDirectionFromFloatXY(
                        X() - collision->X(), Y() - collision->Y()).Int());
                const std::uint8_t opposite = static_cast<std::uint8_t>(collision->directionIndex() - 128);
                const std::uint8_t d1 = static_cast<std::uint8_t>(desired - opposite);
                const std::uint8_t d2 = static_cast<std::uint8_t>(opposite - desired);
                if (static_cast<int>(std::min(d1, d2)) < 30)
                {
                    ChangeCoor(candidate.x, candidate.y, candidate.z);
                    return;
                }
            }

            if (collisionVid == vid)
            {
                const CIV_ROBOT* const other = static_cast<const CIV_ROBOT*>(collision);
                if (behaviorState() == 15u || other->behaviorState() == 15u)
                {
                    ChangeCoor(candidate.x, candidate.y, candidate.z);
                    return;
                }
            }
        }

    blocked:
        // The game logic is CIV-specific.  It does not delegate to the
        // generic ACT_PATH_BLOCK axis-slide behavior.
        setSpeedDirect(0.0f);
        if (turnTimer() == 0)
            setTurnTimer((std::rand() % 2) != 0 ? 10 : -10);
        if ((runtimeFlags() & SPRITE::CommandBitsMask) == 4u &&
            (std::rand() % 9) == 0)
        {
            Stop();
            setBehaviorState(0u);
        }
    }

    SPRITE* CIV_ROBOT::findNearbyEligibleTarget() noexcept
    {
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE* selected = nullptr;
        for (SPRITE* candidate = hash->firstSpriteInBox(
                 X() - 200.0f, Y() - 133.0f,
                 X() + 200.0f, (Z() + Y()) + 133.0f);
             candidate;
             candidate = hash->nextSpriteInBox())
        {
            VID* const candidateVid = candidate->Vid();
            if ((candidateVid->spriteTypeId() & 2u) == 0u ||
                candidateVid->spriteClassId() != 1u ||
                !SpriteHashDepoCanCreateUnitFilter(candidate))
                continue;

            SPRITE* const activeRegion = currentRegion();
            if (activeRegion)
            {
                SPRITE* const candidateRegion = findContainingRegion(candidate->X(), candidate->Y());
                if (!candidateRegion || candidateRegion->Vid() != activeRegion->Vid())
                    continue;
            }

            selected = candidate;
            const unsigned value = static_cast<unsigned>(std::rand());
            if ((value & 0x80000003u) == 0u)
                break;
        }
        return selected;
    }

    void CIV_ROBOT::changeLinkedChildAnimationWhenIdle(int animation) noexcept
    {
        SPRITE* const child = childChain();
        VID* const ownerVid = Vid();
        if (!child || child->Vid() != ownerVid->linkedVid())
            return;
        if (child->currentAnimation() == animation)
            return;
        if (child->currentFrame() < child->currentFrameEnd())
            return;
        child->ChangeAnimation(animation);
    }

    int CIV_ROBOT::rotateLinkedChildTowardDirection(std::uint8_t direction) noexcept
    {
        int result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(this)));
        SPRITE* const child = childChain();
        if (!child)
            return result;

        VID* const ownerVid = Vid();
        VID* const childVid = child->Vid();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(childVid)));
        if (childVid != ownerVid->linkedVid())
            return result;

        result = child->currentFrame();
        if (result < child->currentFrameEnd())
            return result;

        const std::uint32_t elapsed =
            core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        std::uint32_t budget = static_cast<std::uint32_t>(
            ownerVid->hostFrameSpeedStorage(currentAnimation()));
        if (elapsed > budget)
            budget = elapsed;
        return child->RotateTact(ANGLE(static_cast<unsigned char>(direction)), budget).Int();
    }

    int releaseSpriteListReference(SPRITE* sprite) noexcept
    {
        if (!sprite)
            return 0;
        // negative-count diagnostics inside ReleaseListReference.
        return sprite->ReleaseListReference();
    }

    void CIV_ROBOT::DeletePointerToSprite(SPRITE* sprite)
    {
        clearRetainedTargetReference(sprite);
    }

    void CIV_ROBOT::clearRetainedTargetReference(SPRITE* sprite) noexcept
    {
        if (m_retainedTargetSprite == sprite && sprite)
        {
            SPRITE* const owned = m_retainedTargetSprite;
            (void)owned->ReleaseListReference();
            m_retainedTargetSprite = nullptr;
        }
        CREATURE::clearCreatureRegionReference(sprite);
    }

    CIV_ROBOT* CIV_ROBOT::scalarDeletingDestructorCivRobot(unsigned char flags) noexcept
    {
        CIV_ROBOT* const self = this;
        if (m_retainedTargetSprite)
        {
            const VID* const vid = m_retainedTargetSprite->Vid();
            LOG::ResourceError("SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, vid ? vid->nvid() : -1);
        }
        destroyCommandSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }
}
