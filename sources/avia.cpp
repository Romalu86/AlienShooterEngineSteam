#include "avia.h"
#include "map.h"
#include "core/application.h"
#include "graphics/angle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>

namespace as1
{
    AVIA::AVIA(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        StartMove();
        setZSpeedDirect(Vid()->maximumZSpeed());
    }

    namespace
    {

        std::uint32_t manFrameDeltaMilliseconds(const VID* vid, int animation) noexcept
        {
            const std::uint32_t delta = as1::core::CurrentTimeMilliseconds() - as1::core::PreviousWorldTimeMilliseconds();
            const std::uint32_t frameSpeed =
                static_cast<std::uint32_t>(vid->hostFrameSpeedStorage(animation));
            return std::max(delta, frameSpeed);
        }

        int manRandModulo(int divisor) noexcept
        {
            return std::rand() % divisor;
        }

    }

    int AVIA::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        switch (opcode)
        {
        case 0x82:
            updateFlightBehavior(static_cast<int>(argument1Carrier), argument2Carrier);
            return 0;

        case 0x21:
        {
            const float targetX = static_cast<float>(static_cast<int>(argument1Carrier));
            const float targetY = static_cast<float>(argument2Carrier);
            MAP* const map = mapOwner();
            const float targetZ =
                map->GetGroundZ(Vid(), VECTOR2{targetX, targetY}, ANGLE{}) + 80.0f;
            SPRITE* const helper = new (std::nothrow) SPRITE(
                map, MAP::NullVid(), VECTOR(targetX, targetY, targetZ), ANGLE(0), nullptr);
            Move(helper);
            return 0;
        }

        case 0x55:
            return dispatchExtendedSpriteActionOpcode(opcode,
                              static_cast<int>(argument1Carrier),
                              argument2Carrier,
                              argument3Carrier);

        default:
            return dispatchBaseActionOpcode(opcode,
                              static_cast<int>(argument1Carrier),
                              argument2Carrier,
                              argument3Carrier);
        }
    }

    int AVIA::updateFlightIdleBehavior() noexcept
    {
        const int animation = currentAnimation();
        if (animation == 4)
        {
            if (manRandModulo(3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() - 0x20);
        }
        else if (animation == 5)
        {
            if (manRandModulo(3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() + 0x20);
        }
        else if (animation == 2)
        {
            if (manRandModulo(11) == 0)
                ChangeAnimation(manRandModulo(2) == 0 ? 5 : 4);
        }
        else
        {
            ChangeAnimation(2);
        }

        const int randomValue = std::rand();
        int result = randomValue / 21;
        if ((randomValue % 21) == 0 && (behaviorFlags() & 1) != 0)
        {
            if (SPRITE* const target = SeekEnemy())
                result = SetCommand(4, target);
            else
                result = 0;
        }
        return result;
    }

    int AVIA::updateFlightCombatBehavior() noexcept
    {
        const std::uint32_t deltaMs = manFrameDeltaMilliseconds(Vid(), currentAnimation());
        int result = computeAttackDecisionCode(deltaMs);
        setAttackDecisionCode(result);

        if ((behaviorFlags() & 1) == 0)
            return result;

        if (result == 6)
        {
            const int randomValue = std::rand();
            result = randomValue / 21;
            if ((randomValue % 21) != 0)
                return result;
        }

        if (SPRITE* const target = SeekEnemy())
            return SetCommand(4, target);
        return 0;
    }

    int AVIA::faceFlightTargetAndUpdateCombat() noexcept
    {
        SPRITE* const target = goalSprite();
        const std::uint32_t deltaMs = manFrameDeltaMilliseconds(Vid(), currentAnimation());
        const int direction = RetailDirectionFromFloatXY(
            target->X() - X(), target->Y() - Y()).Int();
        const int turnResult = RotateTact(ANGLE(static_cast<unsigned char>(direction)), deltaMs).Int();

        if (turnResult == 0)
            ChangeAnimation(2);

        return updateFlightCombatBehavior();
    }

    void AVIA::updateAltitudeState() noexcept
    {
        VID* const vid = Vid();
        const float maxZSpeed = vid->maximumZSpeed();

        const float lowerGround = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()}, Direction());
        const float lower = (vid->moveUpZ() + lowerGround) - 10.0f;
        if (lower > Z())
        {
            setZSpeedDirect(maxZSpeed);
            return;
        }

        const float upperGround = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()}, Direction());
        const float upper = (vid->moveUpZ() + upperGround) + 10.0f;
        if (Z() > upper)
        {
            setZSpeedDirect(-maxZSpeed);
            return;
        }
        setZSpeedDirect(0.0f);
    }

    int AVIA::updateFlightAuxiliaryBehavior() noexcept
    {
        return 0;
    }

    int AVIA::probeForwardFlightObstacle() noexcept
    {
        const DWORD direction = directionIndex();
        const float x = X() + rawDirectionSinUnchecked(direction) * 128.0f;
        const float y = Y() - rawDirectionCosUnchecked(direction) * 128.0f;
        return probeMovementFootprint(x, y) ? 1 : 0;
    }

    void AVIA::chooseFlightAvoidanceTurn() noexcept
    {
        const int probeDirection = (directionIndex() + 0x10) & 0xFF;
        const float x = X() + rawDirectionSin(probeDirection) * 128.0f;
        const float y = Y() - rawDirectionCos(probeDirection) * 128.0f;
        if (probeMovementFootprint(x, y))
        {
            ChangeAnimation(5);
            ChangeDirection(directionIndex() + 0x10);
            return;
        }

        ChangeAnimation(4);
        ChangeDirection(directionIndex() - 0x10);
    }

    int AVIA::updateFlightBehavior(int behaviorArgument1, int behaviorArgument2) noexcept
    {
        (void)behaviorArgument1;
        (void)behaviorArgument2;
        int result = currentAnimation();
        if (result >= 0x0F || result == 0x0C)
            return result;

        if (result >= 7 && result != 0x0A)
            ChangeAnimation(0);

        updateAltitudeState();
        result = updateFlightAuxiliaryBehavior();

        if (childBacklink())
            return result;

        if ((runtimeFlags() & 0x00000080u) == 0u)
            setRuntimeFlags(runtimeFlags() | 0x00000080u);

        if (goalSprite() != nullptr)
            return updateFlightCombatBehavior();
        return updateFlightIdleBehavior();
    }

}
