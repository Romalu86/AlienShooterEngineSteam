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
        bool cannonX87LessOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        bool cannonX87LessEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs <= rhs;
        }

        bool cannonX87EqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        bool cannonX87OrderedGreaterEqual(double lhs, double rhs) noexcept
        {
            // FCOMP/FNSTSW + TEST AH,1 followed by JNZ rejects C0.
            // Unordered has C0 set and therefore must be rejected as well.
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs >= rhs;
        }

        bool cannonX87OrderedNotEqual(double lhs, double rhs) noexcept
        {
            // TEST AH,40h treats equality and unordered identically.
            return !cannonX87EqualOrUnordered(lhs, rhs);
        }

        bool cannonUcomissNotEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs != rhs;
        }

        bool cannonOrderedLess(double lhs, double rhs) noexcept
        {
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs < rhs;
        }

        bool cannonRetailContinuousZCross(double targetZ, double previousZ, double currentZ) noexcept
        {
            if (cannonX87LessOrUnordered(previousZ, currentZ))
            {
                if (cannonX87LessOrUnordered(targetZ, previousZ))
                    return false;
                return cannonX87LessEqualOrUnordered(targetZ, currentZ);
            }

            if (cannonX87LessOrUnordered(targetZ, currentZ))
                return false;
            return cannonX87LessEqualOrUnordered(targetZ, previousZ);
        }
    }

    CANNON::CANNON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : SPRITE(owner, vid, xyz, direction, parent)
    {
        m_cannonMotionFlags |= 1;

        if ((vid->properties() & P_RANDZSPEED) != 0u &&
            cannonUcomissNotEqualOrUnordered(vid->maximumZSpeed(), 0.0f))
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
            cannonUcomissNotEqualOrUnordered(vid->maxSpeedValue(), 0.0f))
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
            // COMISS(-100.0, Z): unordered follows the no-change path.
            if (cannonOrderedLess(Z(), -100.0f))
            {
                ChangeAnimation(16);
                return 0;
            }

            // UCOMISS(Speed, 0) + LAHF/TEST AH,44h changes animation for
            // every state except ordered equality, including unordered.
            if (cannonUcomissNotEqualOrUnordered(Speed(), 0.0f))
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

        if ((vid->spriteTypeId() & 0x00000200u) != 0u &&
            (vid->properties() & P_GRAVITY) != 0u &&
            cannonX87LessEqualOrUnordered(candidate.z, candidateGround) &&
            cannonX87OrderedGreaterEqual(Z(), currentGround))
        {
            dispatchVirtualAction(ActionCode::ACT_PATH_GROUND,
                                     static_cast<int>(Z()),
                                     static_cast<int>(candidateGround),
                                     static_cast<int>(currentGround));
            candidate.x = X();
            candidate.y = Y();
        }
        else if (cannonX87OrderedNotEqual(Z(), candidate.z) &&
                 (vid->properties() & P_GRAVITY) == 0u &&
                 cannonX87OrderedNotEqual(movePlane, 0.0f))
        {
            bool clampToMovePlane = false;
            if (cannonX87LessOrUnordered(Z(), movePlane))
            {
                clampToMovePlane = !cannonX87LessOrUnordered(candidate.z, movePlane);
            }
            else if (cannonX87LessEqualOrUnordered(Z(), movePlane))
            {
                // The first branch rejected < and unordered, so this is the
                clampToMovePlane = (vid->properties() & P_SELFMOVING) == 0u;
            }
            else
            {
                clampToMovePlane = cannonX87LessOrUnordered(candidate.z, movePlane);
            }

            if (clampToMovePlane)
            {
                candidate.z = movePlane;
                setZSpeedDirect(0.0f);
            }
        }

        if (cannonX87OrderedNotEqual(X(), candidate.x) ||
            cannonX87OrderedNotEqual(Y(), candidate.y))
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
                    (cannonX87LessOrUnordered(X(), -220.0f) ||
                     cannonX87LessOrUnordered(Y(), -200.0f) ||
                     cannonX87LessOrUnordered(owner->SizeX() + 220.0f, X()) ||
                     cannonX87LessOrUnordered(owner->SizeY() + 200.0f, Y())))
                    ChangeAnimation(15);
            }
        }
        if (cannonX87OrderedNotEqual(Z(), candidate.z))
            ChangeCoor(X(), Y(), candidate.z);

        SPRITE* const target = goalSprite();
        if (!target)
        {
            if (cannonX87LessOrUnordered(vid->moveUpZ(), 0.0f) &&
                cannonX87LessOrUnordered(ZSpeed(), 0.0f))
            {
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                RotateTact(directionIndex() + 32, deltaMs);
            }
            return;
        }

        const float dx = std::fabs(target->X() - X());
        const float dy = std::fabs(target->Y() - Y());
        const float distance = cannonX87LessEqualOrUnordered(dx, dy)
            ? static_cast<float>(dy + dx * 0.5f)
            : static_cast<float>(dx + dy * 0.5f);

        if ((vid->properties() & P_SELFMOVING) != 0u)
        {
            if ((m_cannonMotionFlags & 1) != 0)
            {
                const float ground = owner->GetGroundZ(VECTOR2{X(), Y()});
                if ((cannonX87LessEqualOrUnordered(ground + vid->moveUpZ(), Z()) &&
                     cannonX87LessEqualOrUnordered(target->Z(), Z())) ||
                    cannonX87LessEqualOrUnordered(ZSpeed(), 0.0f))
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
                const int reverse = cannonX87LessOrUnordered(Speed(), 0.0f) ? 0x80 : 0;
                const int desired = (AngleFromXY(static_cast<int>(target->X() - X()),
                                                 static_cast<int>(target->Y() - Y()), nullptr).Int() + reverse) & 0xFF;
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                const int turn = RotateTact(ANGLE(static_cast<unsigned char>(desired)), deltaMs).Int();
                if (distance > 10.0f && distance < 30.0f && turn > 70)
                {
                    m_cannonMotionFlags |= 1;
                    StartMove();
                }
                else if (target->Z() >= Z() || cannonX87EqualOrUnordered(distance, 0.0f))
                {
                    setZSpeedDirect(0.0f);
                }
                else
                {
                    float zSpeed = (target->Z() - Z()) / distance * 0.1f;
                    if (cannonX87LessOrUnordered(zSpeed, -vid->maximumZSpeed()))
                        zSpeed = -vid->maximumZSpeed();
                    setZSpeedDirect(zSpeed);
                }
            }
        }
        else if (distance > 100.0f)
        {
            const int previousDir = directionIndex() & 0xFF;
            const int reverse = cannonX87LessOrUnordered(Speed(), 0.0f) ? 0x80 : 0;
            const int desired = (AngleFromXY(static_cast<int>(target->X() - X()),
                                             static_cast<int>(target->Y() - Y()), nullptr).Int() + reverse) & 0xFF;
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
        bool xyOverlap = (flags & 0x00006000u) == 0x00006000u;
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
            overlapping = cannonX87LessOrUnordered(std::fabs(target->Z() - Z()), 20.0f) &&
                          cannonX87LessOrUnordered(std::fabs(target->X() - X()), 10.0f) &&
                          cannonX87LessOrUnordered(std::fabs(target->Y() - Y()), 10.0f);
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
