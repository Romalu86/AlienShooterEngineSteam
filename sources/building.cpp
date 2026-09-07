#include "building.h"
#include "constant.h"
#include "engine.h"
#include "map.h"
#include "sprite_collector_hash.h"
#include "vid/vid.h"

#include <cmath>
#include <cstdlib>

namespace as1
{
    namespace
    {
    }

    BUILDING::BUILDING(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        m_pendingBuildVidHandle = 0;
        m_serviceActivityFlag = 0;
        m_lastServiceTargetHandle = 0;
    }

    int BUILDING::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;

        switch (opcode)
        {
        case 0x49:
            appendCommandRecord(buildCommandRecord(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3));
            (void)dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);
            return 0;

        case 0x46:
            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0x40u || m_pendingBuildVidHandle == 0)
                return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
            appendCommandRecord(buildCommandRecord(0x23u,
                                  static_cast<VID*>(reinterpret_cast<void*>(m_pendingBuildVidHandle))->nvid(),
                                  0,
                                  0));
            SetCommand(0, nullptr);
            return 0;

        case 0x23:
        {
            int buildNvid = argument1;
            if (buildNvid == 0)
                buildNvid = dispatchBaseActionOpcode(0x3B, 4, 0, 0);
            if (buildNvid > 0 && (runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return 0;
            MAP* const map = mapOwner();
            if (!map->HasVidSlot(buildNvid))
                return 0;
            VID* const buildVid = map->Vid(buildNvid);
            m_pendingBuildVidHandle = reinterpret_cast<std::uintptr_t>(buildVid);
            m_buildPositionX = argument2;
            m_buildPositionY = argument3;
            setActionTimer(static_cast<std::uint32_t>(buildVid->weaponBuildTime()));
            SetCommand(0x10, nullptr);
            if (currentAnimation() < 15)
                ChangeAnimation(1);
            return 0;
        }

        case 0x82:
        {
            if (currentAnimation() >= 0x0F)
                return 0;
            if ((runtimeFlags() & SPRITE::CommandBitsMask) == 0x40u)
                ChangeAnimation(1);
            else if (currentAnimation() != 10)
                ChangeAnimation(0);

            VID* const buildVid = static_cast<VID*>(reinterpret_cast<void*>(m_pendingBuildVidHandle));
            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0x40u ||
                actionTimer() != 0u || !buildVid)
                return 0;

            float buildX = static_cast<float>(m_buildPositionX);
            float buildY = static_cast<float>(m_buildPositionY);
            if (m_buildPositionX == 0 && m_buildPositionY == 0)
            {
                buildX = X();
                buildY = Y();
            }
            if (m_buildPositionX < 0)
            {
                const int radius = -m_buildPositionX;
                buildX = X() + static_cast<float>(radius - 2 * (std::rand() % (radius + 1)));
            }
            if (m_buildPositionY < 0)
            {
                const int radius = -m_buildPositionY;
                buildY = Y() + static_cast<float>(radius - 2 * (std::rand() % (radius + 1)));
            }

            if (GlobalHashQueryCellCollisionByVid(*mapOwner(), buildVid, buildX, buildY, Z()) != nullptr)
                return 0;

            SetCommand(0, nullptr);
            SPRITE* const created = mapOwner()->CreateSpriteViaFactory(
                buildVid, VECTOR{buildX, buildY, Z()}, Direction(), this, false);
            if (created)
            {
                (void)dispatchBaseActionOpcode(0x4B,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(created) & 0xFFFFFFFFu), 0, 0);
            }
            m_pendingBuildVidHandle = 0;
            ChangeAnimation(0);
            return 0;
        }

        case 0x50:
            if (!hasCommandOpcode(0x49u))
                appendCommandRecord(buildCommandRecord(0x49u, 0, 0, 0));
            (void)dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
            return 0;

        default:
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
        }
    }

    void BUILDING::MoveTact()
    {
        updateBuildingAnimationTact();
    }

    void BUILDING::updateBuildingAnimationTact() noexcept
    {
        if (Vid()->nvid() == 0x68)
            advanceAnimationFrameTimeCapped(static_cast<int>(GlobalBaseConstants()->raw[0x3Cu / sizeof(DWORD)]));
    }

    void BUILDING::serviceNearbyEngine() noexcept
    {
        SPRITE* selected = nullptr;
        double selectedDistance = 100000.0;
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        for (SPRITE* candidate = hash->firstSpriteInBox(X() - 50.0f, Y() - 50.0f, X() + 50.0f, Y() + 50.0f);
             candidate;
             candidate = hash->nextSpriteInBox())
        {
            if (std::fabs(candidate->X() - X()) > 50.0f || std::fabs(candidate->Y() - Y()) > 50.0f)
                continue;
            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() != 21u)
                continue;

            SPRITE* const child = candidate->childChain();
            if (child)
            {
                VID* const childVid = child->Vid();
                if (childVid == candidateVid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u)
                    child->setActionTimer(6000u);
            }

            const double dx = static_cast<double>(X() - candidate->X());
            const double dy = static_cast<double>(Y() - candidate->Y());
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (!selected || distance < selectedDistance)
            {
                selected = candidate;
                selectedDistance = distance;
            }
        }

        m_serviceActivityFlag = 0;
        if (!selected)
        {
            m_lastServiceTargetHandle = 0;
            ChangeAnimation(0);
            return;
        }

        if (!sameArmy(*selected))
        {
            if (armyIndex() != 2)
            {
                SPRITE* chain = selected->engineChainHead();
                while (chain)
                {
                    VID* const chainVid = chain->Vid();
                    if (chainVid->weaponFloatAt(16) - chainVid->weaponFloatAt(12) > 1.0f &&
                        !selected->sameArmy(*chain))
                        break;
                    chain = chain->engineChainNext();
                }
                if (chain)
                {
                    VID* const selectedVid = selected->Vid();
                    const int oldBucket = selected->armyIndex();
                    selectedVid->setRecolorUnitCountForArmy(oldBucket,
                        selectedVid->recolorUnitCountForArmy(oldBucket) + 1);
                    selected->changeArmyBucket(armyIndex());

                    SPRITE* const owner3C = selected->goalSprite();
                    if (owner3C &&
                        selected->sameArmy(*owner3C) &&
                        owner3C->Vid()->spriteClassId() == 21u)
                        selected->inheritAdjacentEngineCommand();

                    SPRITE* const child = selected->childChain();
                    if (child)
                    {
                        SPRITE* const childOwner = child->goalSprite();
                        if (childOwner &&
                            selected->sameArmy(*childOwner) &&
                            childOwner->Vid()->spriteClassId() == 21u)
                            child->setGoalSprite(nullptr);
                    }
                    m_serviceActivityFlag = 1;
                    selected->clearCommandsTargetingThisSprite();
                }
            }
        }

        if (!sameArmy(*selected))
        {
            m_lastServiceTargetHandle = 0;
            ChangeAnimation(0);
            return;
        }

        if (static_cast<ENGINE*>(selected)->needsMaintenanceService())
            m_serviceActivityFlag = 1;
        if (selectedDistance < 20.0)
        {
            if (reinterpret_cast<std::uintptr_t>(selected) == m_lastServiceTargetHandle)
                m_serviceActivityFlag = 0;
            m_lastServiceTargetHandle = reinterpret_cast<std::uintptr_t>(selected);
        }

        if (m_serviceActivityFlag)
        {
            if (selectedDistance < 20.0)
            {
                (void)selected->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
                ChangeAnimation(1);
                return;
            }
            ChangeAnimation(0);
            return;
        }
        if (selectedDistance >= 20.0)
            ChangeAnimation(0);
    }
}
