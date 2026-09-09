#include "unit.h"
#include "map.h"
#ifdef _WIN32
#include "win/application_win.h"
#endif
#include "core/application.h"
#include "graphics/angle.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace
{
    bool retailUcomissOrderedEqualUnit(float lhs, float rhs) noexcept
    {
        return lhs == rhs;
    }
}

namespace as1
{
    UNIT::UNIT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : TERRAIN(owner, vid, xyz, direction, parent)
    {
        m_legacyCommandState2 = -1;
        m_legacyCommandState0 = 0;
        m_legacyCommandState1 = 0;
        m_turnTimer = 0;

        const VID* valueOwner = vid;
        if (childChain())
        {
            VID* const childVid = childChain()->Vid();
            VID* const linkVid = vid->linkedVid();
            if (childVid == linkVid &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
                valueOwner = linkVid;
        }
        m_behaviorFlags = valueOwner->weaponDefaultBehavior();

        const VID* counterOwner = vid;
        if (VID* const linkVid = vid->linkedVid())
        {
            if (linkVid->hasWeaponChildDescriptor() != 0u &&
                linkVid->weaponCount() != 0u)
                counterOwner = linkVid;
        }
        m_ammoFixedPoint = counterOwner->weaponRecordAmmoCapacity() << 6;
    }

    UNIT::~UNIT()
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
    }

    int UNIT::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;
        return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
    }

    #if defined(_MSC_VER)
    __declspec(safebuffers)
    #endif
    void UNIT::MoveTact()
    {
        VID* const vid = Vid();
        if (vid->spriteClassId() != B_UNIT && vid->spriteClassId() != B_AVIA)
        {
            performBaseMovementTact();
            return;
        }

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);
        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();

        // Retail RotateTact quantizes the angular step to an integer.  At very
        // high frame rates a 1 ms delta can therefore produce step == 0 on
        // every frame, while the UNIT obstacle timer continues to count down.
        // Preserve the retail path for every non-zero quantized step and only
        // accumulate the sub-step angular distance that retail would discard.
        auto rotateUnitHighFpsSafe = [this, vid, deltaMs](ANGLE targetDirection) noexcept
        {
            SpriteHostState& state = hostState();
            const int current = directionIndex() & 0xFF;
            const int target = targetDirection.Int() & 0xFF;
            if (current == target)
            {
                state.unitTurnSubstepCarry = 0.0f;
                state.unitTurnSubstepSign = 0;
                return;
            }

            const float rotationSpeed = vid->rotationSpeedValue();
            if (rotationSpeed == 999999.0f || rotationSpeed <= 0.0f)
            {
                state.unitTurnSubstepCarry = 0.0f;
                state.unitTurnSubstepSign = 0;
                RotateTact(targetDirection, deltaMs);
                return;
            }

            const float rawStep = static_cast<float>(static_cast<std::int32_t>(deltaMs)) * rotationSpeed;
            const int retailStep = static_cast<int>(rawStep + 0.5f);
            if (retailStep != 0)
            {
                state.unitTurnSubstepCarry = 0.0f;
                state.unitTurnSubstepSign = 0;
                RotateTact(targetDirection, deltaMs);
                return;
            }

            int absoluteDelta = current - target;
            if (absoluteDelta < 0)
                absoluteDelta = -absoluteDelta;
            const int wrapDelta = 0x100 - absoluteDelta;

            int turnSign = 0;
            if (current > target)
                turnSign = absoluteDelta < wrapDelta ? -1 : 1;
            else
                turnSign = absoluteDelta > wrapDelta ? -1 : 1;

            if (state.unitTurnSubstepSign != turnSign)
            {
                state.unitTurnSubstepCarry = 0.0f;
                state.unitTurnSubstepSign = turnSign;
            }

            state.unitTurnSubstepCarry += rawStep;
            if (state.unitTurnSubstepCarry < 1.0f)
                return;

            state.unitTurnSubstepCarry -= 1.0f;

            // Feed RotateTact the smallest synthetic delta that quantizes to
            // exactly one angular unit.  The carry above preserves the average
            // angular speed instead of forcing one unit on every high-FPS frame.
            std::uint32_t oneStepDelta = static_cast<std::uint32_t>(0.5f / rotationSpeed);
            if (static_cast<float>(oneStepDelta) * rotationSpeed < 0.5f)
                ++oneStepDelta;
            if (oneStepDelta == 0u)
                oneStepDelta = 1u;

            RotateTact(targetDirection, oneStepDelta);
        };

        int remainingTurnTicks = turnTimer();
        if (remainingTurnTicks != 0)
        {
            const int fps = static_cast<int>(core::DisplayedFramesPerSecond());
            const std::uint32_t timerBits = static_cast<std::uint32_t>(remainingTurnTicks);
            const std::uint32_t signMask = remainingTurnTicks < 0 ? 0xFFFFFFFFu : 0u;
            const std::uint32_t absoluteBits = (timerBits ^ signMask) - signMask;
            std::int32_t absoluteSigned = 0;
            std::memcpy(&absoluteSigned, &absoluteBits, sizeof(absoluteSigned));
            if (absoluteSigned > fps)
            {
                const int halfFps = fps / 2;
                remainingTurnTicks = remainingTurnTicks > 0 ? halfFps : -halfFps;
                setTurnTimer(remainingTurnTicks);
            }

            const int turnOffset = remainingTurnTicks > 0 ? 0x40 : -0x40;
            rotateUnitHighFpsSafe(ANGLE(static_cast<unsigned char>(directionIndex() + turnOffset)));

            remainingTurnTicks = turnTimer();
            if (remainingTurnTicks < 0)
                ++remainingTurnTicks;
            else
                --remainingTurnTicks;
            setTurnTimer(remainingTurnTicks);
        }
        else if (SPRITE* const target = goalSprite())
        {
            const float speed = Speed();
            if (speed != 0.0f)
            {
                const int invertDirection = speed < 0.0f ? 0x80 : 0;
                const int targetDirection = RetailDirectionFromFloatXY(
                    target->X() - X(), target->Y() - Y()).Int() + invertDirection;
                rotateUnitHighFpsSafe(GlideDirection(targetDirection));

                const DWORD flags = runtimeFlags();
                if ((flags & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask)
                    Stop();
            }
        }

        if (retailUcomissOrderedEqualUnit(X(), candidate.x) &&
            retailUcomissOrderedEqualUnit(Y(), candidate.y) &&
            retailUcomissOrderedEqualUnit(Z(), candidate.z))
            return;

        if (CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            steerAwayFromMapBoundary(candidate.x, candidate.y);
            ChangeCoor(candidate.x, candidate.y, candidate.z);
            return;
        }

        if (turnTimer() == 0)
        {
            const int halfFps = static_cast<int>(core::DisplayedFramesPerSecond()) / 2;
            setTurnTimer((std::rand() & 1) != 0 ? halfFps : -halfFps);
        }
    }

    void UNIT::DrawDebugOverlay()
    {
        drawBaseDebugOverlayThunk();
    }

    void UNIT::drawBaseDebugOverlayThunk()
    {
        SPRITE::drawBaseDebugOverlay();
    }

}
