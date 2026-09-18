#include "cannon.h"
#include "map.h"
#include "constant.h"
#include "core/application.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "graphics/angle.h"
#include "vid/vid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace as1
{
    namespace
    {
        // Steam 1.22 CANNON paths compare single-precision values directly with
        // COMISS/UCOMISS.  Keep the predicates in float as well: promoting the
        // operands to double changes both codegen and
        // NaN/unordered behaviour around the retail branches.
        __forceinline bool cannonLessOrUnordered(float lhs, float rhs) noexcept
        {
            return !(lhs >= rhs);
        }

        __forceinline bool cannonLessEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return !(lhs > rhs);
        }

        __forceinline bool cannonOrderedGreaterEqual(float lhs, float rhs) noexcept
        {
            return lhs >= rhs;
        }

        __forceinline bool cannonNotEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return !(lhs == rhs);
        }

        __forceinline bool cannonOrderedLess(float lhs, float rhs) noexcept
        {
            return lhs < rhs;
        }

        __forceinline bool cannonOrderedLessEqual(float lhs, float rhs) noexcept
        {
            return lhs <= rhs;
        }

        __forceinline bool cannonOrderedEqual(float lhs, float rhs) noexcept
        {
            return lhs == rhs;
        }

        __forceinline bool cannonRetailContinuousZCross(float targetZ, float previousZ, float currentZ) noexcept
        {
            if (cannonOrderedLess(previousZ, currentZ))
            {
                if (cannonLessOrUnordered(targetZ, previousZ))
                    return false;
                return cannonOrderedLessEqual(targetZ, currentZ);
            }

            if (cannonLessOrUnordered(targetZ, currentZ))
                return false;
            return cannonOrderedLessEqual(targetZ, previousZ);
        }
    }

    CANNON::CANNON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : SPRITE(owner, vid, xyz, direction, parent)
    {
        m_cannonMotionFlags |= 1;

        if ((vid->properties() & P_RANDZSPEED) != 0u &&
            cannonNotEqualOrUnordered(vid->maximumZSpeed(), 0.0f))
        {
            const bool negative = (std::rand() & 1) == 0;
            const float divisor = negative ? -32767.0f : 32767.0f;
            setZSpeedDirect(static_cast<float>(std::rand()) * vid->maximumZSpeed() / divisor);
        }
        else if (parent && parent->currentAnimation() >= 15 &&
                 cannonOrderedLess(vid->moveUpZ(), 0.0f))
        {
            setZSpeedDirect(vid->maximumZSpeed());
            ChangeSpeed(runtimeMaxSpeedValue() + parent->Speed());
        }
        else
        {
            setZSpeedDirect(vid->maximumZSpeed());
        }

        if ((vid->properties() & P_RANDSPEED) != 0u &&
            cannonNotEqualOrUnordered(vid->maxSpeedValue(), 0.0f))
        {
            const float randomizedMaxSpeed =
                static_cast<float>(std::rand()) * vid->maxSpeedValue() / 32767.0f;
            setActionAuxMaxSpeedDirect(randomizedMaxSpeed);
        }

        StartMove();
    }

    int CANNON::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        if (opcode != 0x82)
            return dispatchActionOpcode(static_cast<std::uint32_t>(opcode), argument1, argument2Carrier, argument3Carrier);

        const int animation = currentAnimation();
        if (animation == 8)
        {
            ChangeAnimation(15);
            return 0;
        }

        if (animation < 15)
        {
            // floating-point comparison(-100.0, Z): unordered follows the no-change path.
            if (cannonOrderedLess(Z(), -100.0f))
            {
                ChangeAnimation(16);
                return 0;
            }

            // floating-point comparison(Speed, 0) + comparison status handling AH,44h changes animation for
            // every state except ordered equality, including unordered.
            if (cannonNotEqualOrUnordered(Speed(), 0.0f))
            {
                ChangeAnimation(2);
                return 0;
            }

            if (animation >= 7 && animation != 10)
                ChangeAnimation(0);
        }
        return 0;
    }

    void CANNON::drawBaseDebugOverlayThunk()
    {
        SPRITE::drawBaseDebugOverlay();
    }

    void CANNON::MoveTact()
    {
        VID* const vid = Vid();
        MAP* const owner = mapOwner();
        if (!vid->movementTactEnabled())
            return;

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        if ((runtimeFlags() & 0x00000400u) != 0u && currentAnimation() < 15)
            ChangeAnimation(15);

        const float currentGround = owner->GetGroundZ(VECTOR2{X(), Y()});
        const float candidateGround = owner->GetGroundZ(VECTOR2{candidate.x, candidate.y});
        const float movePlane = candidateGround + vid->moveUpZ();
        const float previousZ = Z();

        auto triggerImpactAnimationSlot = [this, vid](int animationSlot)
        {
            // Steam 1.22 helper at 0x46C830: trigger ChildVid/SFX for the
            // impact slot without leaving the CANNON in that animation.
            if (vid->childVid[animationSlot] != nullptr)
            {
                const int savedAnimation = currentAnimation();
                setCurrentAnimationDirect(animationSlot);
                CreateChildForAnimation();
                setCurrentAnimationDirect(savedAnimation);
            }

            const int sfx = vid->sfxForAnimation(animationSlot);
            if (sfx != 0)
                playSfxAtWorldPosition(sfx);
        };

        if (cannonOrderedLessEqual(candidate.z, candidateGround) &&
            cannonOrderedGreaterEqual(Z(), currentGround))
        {
            // Steam 1.22 CANNON::MoveTact 0x41836D..0x418479: there is no
            // sprite-type or P_GRAVITY gate before the ground-crossing route.
            candidate.z = Z();

            if (currentAnimation() >= 15)
            {
                Stop();
                if (candidateGround > currentGround)
                    triggerImpactAnimationSlot(11);
            }
            else if ((vid->properties() & P_BOUNCE) == 0u)
            {
                Stop();
                candidate.z = currentGround;
                triggerImpactAnimationSlot(candidateGround > currentGround ? 11 : 12);
                ChangeAnimation(15);
            }
            else
            {
                if (cannonLessEqualOrUnordered(candidateGround, currentGround))
                {
                    const float zSpeed = ZSpeed();
                    if (cannonOrderedLess(zSpeed, -0.022f))
                    {
                        ChangeAnimation(12);
                        setZSpeedDirect(zSpeed * -0.5f);
                    }
                    else if (cannonLessEqualOrUnordered(zSpeed, 0.005f))
                    {
                        setZSpeedDirect(0.0f);
                        ChangeAnimation(15);
                    }
                    else
                    {
                        setZSpeedDirect(zSpeed * 0.5f);
                        ChangeSpeed(Speed() * 0.5f);
                    }
                }
                else
                {
                    ChangeAnimation(11);
                }

                ChangeDirection(ANGLE(static_cast<unsigned char>(directionIndex() - 0x80)));
            }

            candidate.x = X();
            candidate.y = Y();
        }
        else if (cannonNotEqualOrUnordered(Z(), candidate.z) &&
                 (vid->properties() & P_GRAVITY) == 0u &&
                 cannonNotEqualOrUnordered(movePlane, 0.0f))
        {
            // Steam 1.22 0x4184B2..0x4184E7.
            if (cannonOrderedLess(Z(), movePlane))
            {
                if (!cannonOrderedLess(candidate.z, movePlane))
                {
                    candidate.z = movePlane;
                    setZSpeedDirect(0.0f);
                }
            }
            else if (cannonOrderedLess(movePlane, Z()))
            {
                if (cannonLessOrUnordered(candidate.z, movePlane))
                {
                    candidate.z = movePlane;
                    setZSpeedDirect(0.0f);
                }
            }
            else if ((vid->properties() & P_SELFMOVING) == 0u)
            {
                // Equality/unordered: retail only zeroes Z speed here.
                setZSpeedDirect(0.0f);
            }
        }

        if (cannonNotEqualOrUnordered(X(), candidate.x) ||
            cannonNotEqualOrUnordered(Y(), candidate.y))
        {
            if (CanPlaceWithCrush(candidate.x, candidate.y, candidate.z) != nullptr)
            {
                if (currentAnimation() < 15)
                    ChangeAnimation(15);
            }
            else
            {
                ChangeCoor(candidate.x, candidate.y, candidate.z);
                if (currentAnimation() < 15 &&
                    (vid->properties() & P_GRAVITY) == 0u &&
                    (cannonOrderedLess(X(), -220.0f) ||
                     cannonOrderedLess(Y(), -200.0f) ||
                     cannonOrderedLess(owner->SizeX() + 220.0f, X()) ||
                     cannonOrderedLess(owner->SizeY() + 200.0f, Y())))
                    ChangeAnimation(15);
            }
        }
        if (cannonNotEqualOrUnordered(Z(), candidate.z))
            ChangeCoor(X(), Y(), candidate.z);

        SPRITE* const target = goalSprite();
        if (!target)
        {
            if (cannonOrderedLess(vid->moveUpZ(), 0.0f) &&
                cannonOrderedLess(ZSpeed(), 0.0f))
            {
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                RotateTact(directionIndex() + 32, deltaMs);
            }
            return;
        }

        const float dx = std::fabs(target->X() - X());
        const float dy = std::fabs(target->Y() - Y());
        const float distance = cannonLessEqualOrUnordered(dx, dy)
            ? static_cast<float>(dy + dx * 0.5f)
            : static_cast<float>(dx + dy * 0.5f);

        if ((vid->properties() & P_SELFMOVING) != 0u)
        {
            if ((m_cannonMotionFlags & 1) != 0)
            {
                const float ground = owner->GetGroundZ(VECTOR2{X(), Y()});
                if ((cannonLessEqualOrUnordered(ground + vid->moveUpZ(), Z()) &&
                     cannonLessEqualOrUnordered(target->Z(), Z())) ||
                    cannonLessEqualOrUnordered(ZSpeed(), 0.0f))
                    m_cannonMotionFlags &= ~1;
                else
                {
                    const BASE_CONSTANTS* const constants = GlobalBaseConstants();
                    float gravity = 0.0f;
                    std::memcpy(&gravity, &constants->raw[2], sizeof(gravity));
                    const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                    setZSpeedDirect(ZSpeed() - static_cast<float>(deltaMs) * gravity);
                }
            }
            else
            {
                const int reverse = cannonOrderedLess(Speed(), 0.0f) ? 0x80 : 0;
                const int desired = (RetailDirectionFromFloatXY(
                    target->X() - X(), target->Y() - Y()).Int() + reverse) & 0xFF;
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                const int turn = RotateTact(ANGLE(static_cast<unsigned char>(desired)), deltaMs).Int();
                if (distance > 10.0f && distance < 30.0f && turn > 70)
                {
                    m_cannonMotionFlags |= 1;
                    StartMove();
                }
                else if (cannonLessEqualOrUnordered(Z(), target->Z()) ||
                         cannonOrderedEqual(distance, 0.0f))
                {
                    setZSpeedDirect(0.0f);
                }
                else
                {
                    float zSpeed = ((target->Z() - Z()) / distance) / 10.0f;
                    if (cannonOrderedLess(zSpeed, -vid->maximumZSpeed()))
                        zSpeed = -vid->maximumZSpeed();
                    setZSpeedDirect(zSpeed);
                }
            }
        }
        else if (distance > 100.0f)
        {
            const int previousDir = directionIndex() & 0xFF;
            const int reverse = cannonOrderedLess(Speed(), 0.0f) ? 0x80 : 0;
            const int desired = (RetailDirectionFromFloatXY(
                target->X() - X(), target->Y() - Y()).Int() + reverse) & 0xFF;
            const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            RotateTact(desired, deltaMs);
            const int currentDir = directionIndex() & 0xFF;
            const int diffA = (previousDir - currentDir) & 0xFF;
            const int diffB = (currentDir - previousDir) & 0xFF;
            if (std::min(diffA, diffB) > 100)
            {
                Stop();
                ChangeAnimation(15);
            }
        }

        const VID* const targetVid = target->Vid();
        const std::uint32_t flags = runtimeFlags();
        bool xyOverlap =
            (flags & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask;
        if (!xyOverlap)
        {
            xyOverlap = vid->halfSizeX() + targetVid->halfSizeX() > std::fabs(X() - target->X()) &&
                        vid->halfSizeY() + targetVid->halfSizeY() > std::fabs(Y() - target->Y());
        }

        bool overlapping = false;
        if (xyOverlap)
        {
            overlapping = vid->sizeZ() + Z() >= target->Z() &&
                          target->Z() + targetVid->sizeZ() >= Z();
        }

        if (!overlapping &&
            cannonRetailContinuousZCross(target->Z(), previousZ, Z()))
        {
            overlapping = true;
        }

        if (!overlapping && (vid->properties() & P_SELFMOVING) != 0u)
            overlapping = cannonOrderedLess(std::fabs(target->Z() - Z()), 20.0f) &&
                          cannonOrderedLess(std::fabs(target->X() - X()), 10.0f) &&
                          cannonOrderedLess(std::fabs(target->Y() - Y()), 10.0f);
        if (overlapping)
        {
            Stop();
            ChangeAnimation(15);
        }
    }

    void CANNON::DeletePointerToSprite(SPRITE* sprite)
    {
        replaceDeletedGoalAndClearReference(sprite);
    }

    void CANNON::replaceDeletedGoalAndClearReference(SPRITE* target) noexcept
    {
        if (target && goalSprite() == target)
        {
            SPRITE* const replacement = new (std::nothrow) SPRITE(
                mapOwner(), MAP::NullVid(), target->xyz(), ANGLE(0), nullptr);
            setGoalSprite(replacement);
        }
        SPRITE::DeletePointerToSprite(target);
    }
}
