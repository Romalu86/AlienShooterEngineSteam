#include "engine.h"
#include "base_sprite_list.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/weak_controller.h"
#include "player.h"
#include "win/application_win.h"
#include "constant.h"
#include "map.h"
#include "sprite_collector_hash.h"
#include "vid/vid.h"
#include "graph.h"
#include "core/as_string.h"
#include "core/weak_controller.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>

namespace as1
{
    namespace
    {
        constexpr std::size_t kEngineCallbackPeerFound = 4u;
        constexpr std::size_t kEngineCallbackPair = 5u;
        constexpr std::size_t kEngineCallbackNoPair = 6u;
        constexpr std::size_t kEngineCallbackWeapon = 7u;
        constexpr std::size_t kEngineCallbackDestroy = 24u;
        constexpr std::size_t kEngineCallbackMoveRange = 1u;
        constexpr std::size_t kEngineCallbackMoveFrame = 3u;
        constexpr std::size_t kEngineCallbackDamageEnemy = 10u;

        int pointerToRetailInt(const void* ptr) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ptr)));
        }

        template <class T>
        T* retailIntToPointer(int value) noexcept
        {
            return reinterpret_cast<T*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        int engineFtolLow32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(INT64_MIN) ||
                d > static_cast<long double>(INT64_MAX))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
        }

        int engineSub32Wrap(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int engineAdd32Wrap(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
        }

        float engineFildToF32(int value) noexcept
        {
            return static_cast<float>(value);
        }

        bool engineFcompEqualOrUnorderedZero(float value) noexcept
        {
            // FCOMP/FNSTSW + TEST AH,40h takes the branch for equal and unordered.
            return value == 0.0f || std::isnan(value);
        }

        bool engineLessEqualOrUnordered(float lhs, float rhs) noexcept
        {
            // TEST AH,41h observes C0|C3: less/equal and unordered all take the branch.
            return lhs <= rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        float engineRawConstantFloat(DWORD bits) noexcept
        {
            float value = 0.0f;
                                                                                                  
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        bool engineSpeedExceedsStopThreshold(float speed, float threshold) noexcept
        {
            return std::isnan(speed) || std::isnan(threshold) || threshold < std::fabs(speed);
        }

        VID* engineActionApplicationVid(int index) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            VID* vid = table.count() > index ? table.slot(index) : nullptr;
            return vid ? vid : MAP::NullVid();
        }
    }

    ENGINE::ENGINE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        setDerivedStateValue(0, derivedStateValue(0) & ~1);
        setChainPrevious(nullptr);
        setChainNext(nullptr);
        setEngineCommandReferenceOwner(nullptr);
        setEngineCommandArgument0(0);
        setEngineCommandArgument1(0);
        engineCommandArgument2Ref() = 0;
        engineAccelerationDelayRef() = 0;
        engineTargetSpeedRef() = 0.0f;
        pushLineActiveRef() = 0;
        primaryPathNodeRef() = nullptr;
        primaryPathAuxiliaryRef() = 0;
        secondaryPathNodeRef() = nullptr;
        secondaryPathAuxiliaryRef() = 0;
        setSecondaryStateFlag(1);
        setProductionBatchCompletionPending(0);
        previousPathXRef() = 0.0f;
        previousPathYRef() = 0.0f;
        previousPathZRef() = 0.0f;
        routeActionReadyRef() = 0;
        routeActionStartTimeRef() = 0;
        setProductionSequenceId(-1);
        pathBufferSizeRef() = 0;

        if ((core::ApplicationFlags() & application_flags::MapLoading) == 0u)
        {
            initializeEnginePathEndpoints();
            if (SPRITE* const child = childChain())
            {
                VID* const linkVid = vid ? vid->linkedVid() : nullptr;
                if (linkVid && child->Vid() == linkVid)
                    child->ChangeDirection(directionIndex());
            }
        }
    }

    ENGINE::~ENGINE()
    {
        releaseEngineRuntimeState();

    }

    void ENGINE::releaseEngineCommandReference() noexcept
    {
        SPRITE* const ref = engineCommandReferenceOwner();
        if (!ref)
            return;

        if (ref == this)
        {
            dispatchEnginePrivateCommand(0, 0, 0, 0);
            return;
        }

        (void)ref->ReleaseListReference();
        setEngineCommandReferenceOwner(nullptr);
    }

    void ENGINE::releaseEngineRuntimeState() noexcept
    {
        const bool bulkDelete = core::BulkSpriteDeleteActive() != 0u;

        if (!bulkDelete)
            (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackDestroy),
                                                static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this))),
                                                0);

        releaseEngineCommandReference();

        if (MAP* const owner = mapOwner())
        {
            if (this == owner->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex())))
                g_spriteWorkList.releaseRepeatedReferencesRetail();
        }

        if (!bulkDelete)
            clearPathNodeOwnership();

        // [ENGINE+0x98] PrevEngine and [ENGINE+0x9C] NextEngine are the same
        // action-list owner slots consumed by engineChainHead/engineChainTail.
        SPRITE* const first = chainPrevious();
        SPRITE* const second = chainNext();
        if (first)
        {
            first->setEngineChainNext(nullptr);
            first->updateEngineChainSpeedTarget();
        }
        if (second)
        {
            second->setEngineChainPrevious(nullptr);
            second->updateEngineChainSpeedTarget();
        }
        if (!bulkDelete)
        {
            const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
            SPRITE* const currentFirst = chainPrevious();
            SPRITE* const currentSecond = chainNext();
            if (currentFirst && currentSecond)
            {
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackPair),
                                                    static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(currentFirst))),
                                                    static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(currentSecond))));
            }

            VID* const vid = Vid();
            if (vid->weaponFloatAt(16) - vid->weaponFloatAt(12) > 1.0f)
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackWeapon), selfArg, 0);

            if (!chainPrevious() && !chainNext())
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackNoPair), selfArg, 0);

            if (productionBatchCompletionPending() != 0)
            {
                SPRITE_COLLECTOR_HASH_MAP* const collector = GlobalSpriteHashMap();
                const int count = collector->overflowCount();
                if (count != 0)
                {
                    int cursor = count - 1;
                    collector->setReverseCursor(cursor);
                    SPRITE* candidate = collector->overflowSpriteAt(cursor);
                    while (candidate)
                    {
                        VID* const candidateVid = candidate->Vid();
                        if (candidateVid->spriteClassId() == B_ENGINE &&
                            (candidate->sameArmy(*this)) &&
                            static_cast<ENGINE*>(candidate)->productionSequenceId() == productionSequenceId())
                        {
                            (void)core::Application::callScriptFunction(
                                core::scriptCallbackSlot(kEngineCallbackPeerFound),
                                static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(candidate))),
                                0);
                            break;
                        }

                        const int currentCount = collector->overflowCount();
                        cursor = collector->reverseCursor();
                        if (cursor > currentCount)
                            cursor = currentCount;
                        --cursor;
                        collector->setReverseCursor(cursor);
                        if (cursor < 0)
                            break;
                        candidate = collector->overflowSpriteAt(cursor);
                    }
                }
            }
        }
    }

    int ENGINE::applyRepairPulseTo(SPRITE* target) noexcept
    {
        if (!target)
            return 0;

        const BASE_CONSTANTS* const constants = GlobalBaseConstants();
        const int step = static_cast<int>(constants->raw[4]);
        const int negativeStep = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(step));

        const int actionResult = target->dispatchVirtualAction(ActionCode::ACT_DAMAGE, negativeStep, 0, 0);
        const bool zeroResult = (actionResult == 0);
        if (actionResult != 0)
        {
            VID* const targetVid = target->Vid();
            VID* const linkVid = targetVid->linkedVid();
            if (linkVid)
            {
                SPRITE* const child = target->childChain();
                if (!child || child->Vid() != linkVid)
                {
                    int progress = target->sharedPrimaryState();
                    if (progress < 0)
                        progress = 0;
                    progress = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(progress) + static_cast<std::uint32_t>(step));
                    target->setSharedPrimaryState(progress);

                    const int bucket = target->armyIndex();
                    if (progress >= linkVid->animationFrameDuration(bucket))
                    {
                        target->repairLinkedChildState(1);
                        setSharedPrimaryState(-1);
                    }
                }
            }
        }
        return zeroResult ? 1 : 0;
    }

    void ENGINE::updateRepairTarget() noexcept
    {
        SPRITE* best = nullptr;
        int bestMetric = 0;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNext())
        {
            if (!sameArmy(*node))
                continue;
            const int metric = node->animationRemainingPercent();
            if (metric > bestMetric)
            {
                bestMetric = metric;
                best = node;
            }
        }

        if (best)
        {
            (void)applyRepairPulseTo(best);
            if (currentAnimation() != static_cast<int>(AnimationCode::ANI_OPEN))
                ChangeAnimation(static_cast<int>(AnimationCode::ANI_OPEN));
        }
        else if (currentAnimation() == static_cast<int>(AnimationCode::ANI_OPEN))
        {
            ChangeAnimation(static_cast<int>(AnimationCode::ANI_STAND));
        }
    }

    int ENGINE::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;
        return dispatchEngineActionOpcode(opcode, argument1, argument2, argument3);
    }

    int ENGINE::dispatchEngineActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        const unsigned int action = static_cast<unsigned int>(opcode) & 0xFFu;
        switch (action)
        {
        case static_cast<unsigned int>(ActionCode::ACT_REPAIR):
        {
            VID* const vid = Vid();
            VID* const link = vid->linkedVid();
            const int capacity = (link && link->hasWeaponChildDescriptor() && link->weaponCount())
                ? link->weaponRecordAmmoCapacity()
                : vid->weaponRecordAmmoCapacity();
            if (vid->nvid() != 82 || ammoCount() >= capacity)
                return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);

            const int bucket = armyIndex();
            const DWORD selfCount = vid->spriteCountForArmy(bucket);
            VID* const slot70 = engineActionApplicationVid(70);
            const DWORD slot70Count = slot70->spriteCountForArmy(bucket);
            int sameArmySum = 0;
            SPRITE_COLLECTOR_HASH_MAP* const collector = GlobalSpriteHashMap();
            const int count = collector->overflowCount();
            SPRITE* const* const raw = collector->mutableOverflowList().data();
            for (int index = count - 1; index >= 0; --index)
            {
                SPRITE* const candidate = raw[index];
                if (!candidate)
                    break;
                if (candidate->Vid()->nvid() == 82 &&
                    sameArmy(*candidate))
                {
                    sameArmySum += candidate->ammoCount();
                }
                if (index > collector->overflowCount())
                    index = collector->overflowCount();
            }

            VID* const currentLink = vid->linkedVid();
            const int currentCapacity = (currentLink && currentLink->hasWeaponChildDescriptor() && currentLink->weaponCount())
                ? currentLink->weaponRecordAmmoCapacity()
                : vid->weaponRecordAmmoCapacity();
            if (static_cast<int>(slot70Count) + sameArmySum >= static_cast<int>(selfCount) * currentCapacity)
                return dispatchExtendedSpriteActionOpcode(opcode, argument1, argument2, argument3);
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
        }
        case static_cast<unsigned int>(ActionCode::ACT_UNDO_REMOVE):
            (void)dispatchActionOpcode(opcode, argument1, argument2, argument3);
            clearPathNodeOwnership();
            if (SPRITE* const prev = engineChainPrevious())
                prev->setEngineChainNext(nullptr);
            if (SPRITE* const next = engineChainNext())
                next->setEngineChainPrevious(nullptr);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_UNDO_INSERT):
            (void)dispatchActionOpcode(opcode, argument1, argument2, argument3);
            claimPathNodeOwnership();
            if (SPRITE* const prev = engineChainPrevious())
                prev->setEngineChainNext(this);
            if (SPRITE* const next = engineChainNext())
                next->setEngineChainPrevious(this);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_SET_ARMY):
        {
#ifdef _WIN32
            PLAYER* const player = win::applicationWinInstance()->startupPlayerSlotByIndex(
                armyIndex());
            (void)player->clearSpriteReferenceViaVtable(this);
#endif
            changeArmyBucket(static_cast<signed char>(argument1));
#ifdef _WIN32
            if (!engineFcompEqualOrUnorderedZero(Vid()->weaponFloatAt(16)))
            {
                PLAYER* const player = win::applicationWinInstance()->startupPlayerSlotByIndex(
                    armyIndex());
                player->noOpSpriteCallback(this);
            }
#endif
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_GET_GOAL):
            return pointerToRetailInt(engineChainHead()->goalSprite());

        case 0x4Au:
        {
            dispatchEnginePrivateCommand((static_cast<unsigned int>(opcode) >> 8) & 0xFFu, argument1, argument2, argument3);
            SPRITE* const ref = retailIntToPointer<SPRITE>(argument1);
            if (!ref)
                return 0;
            (void)ref->ReleaseListReference();
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_BACKUP_COMMAND):
        {
            const std::uint32_t commandOpcode = (static_cast<std::uint32_t>(commandIndex()) << 8) + 74u;
            const SpriteCommandRecord command = SPRITE::buildCommandRecord(
                commandOpcode,
                pointerToRetailInt(goalSprite()),
                engineCommandArgument0Ref(),
                engineCommandArgument1Ref());
            m_commandStack.appendCommandRecord(command);
            if (SPRITE* const target = goalSprite())
                target->AddListReference();
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_NEXT_COMMAND):
        {
            const int animation = currentAnimation();
            if (animation >= 15)
                return 0;
            if (engineAccelerationDelayRef() > 0)
            {
                if (animation != 3)
                    ChangeAnimation(3);
            }
            else if (engineAccelerationDelayRef() < 0)
            {
                if (animation != 1)
                    ChangeAnimation(1);
            }
            else if (m_speed == 0.0f)
            {
                if (animation != 0)
                    ChangeAnimation(static_cast<int>(AnimationCode::ANI_STAND));
            }
            else if (animation != 2)
            {
                ChangeAnimation(2);
            }

            VID* const vid = Vid();
            bool decisionOwner = vid->hasWeaponChildDescriptor() && vid->weaponCount();
            if (!decisionOwner)
            {
                SPRITE* const child = childChain();
                decisionOwner = child && child->Vid() == vid->linkedVid() &&
                    child->Vid()->hasWeaponChildDescriptor() && child->Vid()->weaponCount();
            }
            if (decisionOwner)
                (void)updateCombatDecision();

            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 96u || vid->nvid() != 85 ||
                routeActionReadyRef() == 0 || m_speed != 0.0f)
            {
                return 0;
            }

            const std::uint32_t now = core::CurrentTimeMilliseconds();
            if (routeActionStartTimeRef() != 0)
            {
                if (now - static_cast<std::uint32_t>(routeActionStartTimeRef()) > GlobalBaseConstants()->raw[17])
                {
                    if (ammoCount() > 0)
                    {
                        core::WeakController* const actionNode = engineCommandArgument0Node();
                        (void)mapOwner()->CreateSpriteViaFactory(
                            engineActionApplicationVid(595),
                            VECTOR(static_cast<float>(actionNode->x()),
                                   static_cast<float>(actionNode->y()),
                                   static_cast<float>(actionNode->id())),
                            ANGLE(0), this, false);
                        SPRITE* const created = mapOwner()->CreateSpriteViaFactory(
                            engineActionApplicationVid(86), xyz(), ANGLE(directionIndex()), this, false);
                        if (created)
                        {
                            (void)created->dispatchVirtualAction(ActionCode::ACT_SET_BEHAVE, 0, 0, 0);
                            (void)dispatchVirtualAction(ActionCode::ACT_ADD_AMMO, -1, 0, 0);
                        }
                    }
                    dispatchEnginePrivateCommand(0, 0, 0, 0);
                }
            }
            else
            {
                routeActionStartTimeRef() = static_cast<int>(now);
            }

            (void)mapOwner()->CreateSpriteViaFactory(
                engineActionApplicationVid(588), xyz(), ANGLE(0), this, false);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_DAMAGE):
            if (argument1 > 0)
            {
                SPRITE* const head = engineChainHead();
                SPRITE* const target = retailIntToPointer<SPRITE>(argument2);
                if ((head->runtimeFlags() & 2u) == 0u && target &&
                    !sameArmy(*target))
                {
                    (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackDamageEnemy),
                                                        pointerToRetailInt(this), pointerToRetailInt(target));
                    head->setRuntimeFlags(head->runtimeFlags() | 2u);
                }
            }
            if (SPRITE* const child = childChain())
            {
                VID* const childVid = child->Vid();
                if (childVid == Vid()->linkedVid() &&
                    childVid->animationFrameDuration(child->armyIndex()) != 0)
                {
                    return child->dispatchVirtualAction(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
                }
            }
            return dispatchActionOpcode(opcode, argument1, argument2, argument3);

        case static_cast<unsigned int>(ActionCode::ACT_SAVE):
        {
            RESOURCE* const resource = retailIntToPointer<RESOURCE>(argument1);
            (void)dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
            core::PathPosition c8{primaryPathNodeRef(), primaryPathProgressRef(), primaryPathAuxiliaryRef(), primaryPathEdgeIndexRef()};
            core::PathPosition d8{secondaryPathNodeRef(), secondaryPathProgressRef(), secondaryPathAuxiliaryRef(), secondaryPathEdgeIndexRef()};
            (void)core::serializePathPosition(&c8, resource);
            (void)core::serializePathPosition(&d8, resource);
            const std::uint32_t prev = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(engineChainPrevious()));
            const std::uint32_t next = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(engineChainNext()));
            resource->write(&prev, 4u);
            resource->write(&next, 4u);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_RESTORE):
        case 0xC8u:
        {
            RESOURCE* const resource = retailIntToPointer<RESOURCE>(argument1);
            (void)dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
            if (argument2 < 6)
            {
                initializeEnginePathEndpoints();
            }
            else
            {
                core::PathPosition c8{};
                core::PathPosition d8{};
                (void)c8.deserializePathPosition(resource);
                (void)d8.deserializePathPosition(resource);
                primaryPathNodeRef() = c8.node;
                primaryPathProgressRef() = c8.progress;
                primaryPathAuxiliaryRef() = c8.auxiliary;
                primaryPathEdgeIndexRef() = c8.edgeIndex;
                secondaryPathNodeRef() = d8.node;
                secondaryPathProgressRef() = d8.progress;
                secondaryPathAuxiliaryRef() = d8.auxiliary;
                secondaryPathEdgeIndexRef() = d8.edgeIndex;

                setEngineChainPrevious(mapOwner()->readSpriteRelationHandle(resource));
                setEngineChainNext(mapOwner()->readSpriteRelationHandle(resource));
                claimPathNodeOwnership();

                std::uint8_t facing = 0;
                if (primaryPathNodeRef())
                    facing = primaryPathNodeRef()->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].facing;
                const std::uint8_t direction = static_cast<std::uint8_t>(directionIndex());
                const std::uint8_t d1 = static_cast<std::uint8_t>(direction - facing);
                const std::uint8_t d2 = static_cast<std::uint8_t>(facing - direction);
                if ((d1 < d2 ? d1 : d2) > 127u)
                    setDerivedStateValue(0, derivedStateValue(0) | 1);
            }
#ifdef _WIN32
            if (!engineFcompEqualOrUnorderedZero(Vid()->weaponFloatAt(16)))
            {
                PLAYER* const player = win::applicationWinInstance()->startupPlayerSlotByIndex(
                    armyIndex());
                player->noOpSpriteCallback(this);
            }
#endif
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_COOR_ATTACK):
        {
            const int nvid = Vid()->nvid();
            if (nvid == 85)
            {
                core::WeakController* const node = core::findNearestLinkedNode2D(&core::globalWeakControllerMap(), argument1, argument2);
                dispatchEnginePrivateCommand(24, 0, pointerToRetailInt(node), 0);
                return 0;
            }
            if (nvid != 97 || engineChainNext() || engineChainPrevious())
            {
                const float x = engineFildToF32(argument1);
                const float y = engineFildToF32(argument2);
                const float groundPlus19 = mapOwner()->GetGroundZ(VECTOR2{x, y}) + 19.0f;
                const int groundInt = engineFtolLow32(groundPlus19);
                const int helperY = engineAdd32Wrap(engineAdd32Wrap(groundInt, argument2), -19);
                SPRITE* const created = new (std::nothrow) SPRITE(
                    mapOwner(), MAP::NullVid(),
                    VECTOR(x, engineFildToF32(helperY), groundPlus19),
                    ANGLE(0), nullptr);
                dispatchEnginePrivateCommand(29, pointerToRetailInt(created), 0, 0);
                return 0;
            }
            core::WeakController* const node = core::findNearestLinkedNode2D(&core::globalWeakControllerMap(), argument1, argument2);
            dispatchEnginePrivateCommand(27, 0, pointerToRetailInt(node), 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_MOVE):
            setEnginePathGoalFromCoordinates(engineFildToF32(argument1), engineFildToF32(argument2), 0.0f, 0, 0);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_ATTACK):
        {
            if (argument1 == 0)
                return 0;
            const int actionOpcode = Vid()->nvid() == 97 && !engineChainNext() && !engineChainPrevious() ? 27 : 28;
            dispatchEnginePrivateCommand(actionOpcode, argument1, 0, 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_LINK_ENGINE):
        case static_cast<unsigned int>(ActionCode::ACT_FORCELINK_ENGINE):
        {
            SPRITE* const target = retailIntToPointer<SPRITE>(argument1);
            if (!target || target->Vid()->spriteClassId() != 21u || isInEngineChain(target))
                return 0;
            dispatchEnginePrivateCommand(26, argument1, 0, 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_CLASH_ENGINE):
        {
            SPRITE* const target = retailIntToPointer<SPRITE>(argument1);
            if (!target || target->Vid()->spriteClassId() != 21u || isInEngineChain(target))
                return 0;
            VID* const vid = Vid();
            SPRITE* const child = childChain();
            if (vid->nvid() == 35 && child && child->Vid() == vid->linkedVid())
                (void)child->SetCommand(8, target);
            else
                dispatchEnginePrivateCommand(27, argument1, 0, 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_TRAIN_BEHAVE):
        {
            for (SPRITE* node = engineChainHead(); node; node = node->engineChainNext())
            {
                if (argument1 == 1)
                {
                    node->setBehaviorFlags(0);
                    continue;
                }
                node->setBehaviorFlags(1);
                VID* const nodeVid = node->Vid();
                VID* const link = nodeVid->linkedVid();
                const int capacity = (link && link->hasWeaponChildDescriptor() && link->weaponCount())
                    ? link->weaponRecordAmmoCapacity()
                    : nodeVid->weaponRecordAmmoCapacity();
                if (capacity <= 5 && (argument1 == 2 || argument1 == 4))
                    node->setBehaviorFlags(0);
                if (argument1 == 4 || argument1 == 5)
                    node->setBehaviorFlags(node->behaviorFlags() | 2);
                if (argument1 == 3 || argument1 == 5)
                    node->setBehaviorFlags(node->behaviorFlags() | 0x10);
            }
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_STOP):
            if (argument1 != 0)
                stopEngineChain();
            dispatchEnginePrivateCommand(0, 0, 0, 0);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_IS_TRAIN):
            return Vid()->weaponFloatAt(16) - Vid()->weaponFloatAt(12) > 1.0f ? 1 : 0;
        case static_cast<unsigned int>(ActionCode::ACT_FIRST_ENGINE):
            return pointerToRetailInt(engineChainHead());
        case static_cast<unsigned int>(ActionCode::ACT_LAST_ENGINE):
            return pointerToRetailInt(engineChainTail());
        case static_cast<unsigned int>(ActionCode::ACT_NEXT_ENGINE):
            return pointerToRetailInt(engineChainNext());
        case static_cast<unsigned int>(ActionCode::ACT_IS_FIRST):
            return engineChainPrevious() == nullptr ? 1 : 0;
        case static_cast<unsigned int>(ActionCode::ACT_IN_TRAIN):
            return isInEngineChain(retailIntToPointer<SPRITE>(argument1));
        default:
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
        }
    }

    SPRITE* ENGINE::findEngineChainSpecialWeaponNode() noexcept
    {
        SPRITE* node = engineChainHead();
        while (node)
        {
            VID* const vid = node->Vid();
            if (vid->weaponFloatAt(16) - vid->weaponFloatAt(12) > 1.0f)
                return node;
            node = node->engineChainNext();
        }
        return nullptr;
    }

    int ENGINE::engineChainContainsArmy(int bucket) noexcept
    {
        for (SPRITE* node = this; node; node = node->engineChainNext())
            if (node->armyIndex() == bucket)
                return 1;
        for (SPRITE* node = engineChainPrevious(); node; node = node->engineChainPrevious())
            if (node->armyIndex() == bucket)
                return 1;
        return 0;
    }

    int ENGINE::updateCombatDecision() noexcept
    {
        int result = 0;
        if (hasLinkedVidChild())
        {
            SPRITE* const child = childChain();
            VID* const childVid = child->Vid();
            if (childVid->hasWeaponChildDescriptor() && childVid->weaponCount())
            {
                SPRITE* const target = child->goalSprite();
                if (target && sameArmy(*target))
                {
                    const std::uint32_t childAction = child->runtimeFlags() & SPRITE::CommandBitsMask;
                    if (childAction == 12u || childAction == 16u)
                        result = child->SetCommand(0, nullptr);
                }
            }
        }

        VID* const vid = Vid();
        bool runDecision = vid->nvid() != 35;
        if (!runDecision && hasLinkedVidChild())
        {
            SPRITE* const child = childChain();
            VID* const childVid = child->Vid();
            if (childVid->hasWeaponChildDescriptor() && childVid->weaponCount())
            {
                result = child->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                runDecision = result >= childVid->weaponRecordAmmoCapacity();
            }
        }

        if (runDecision && (runtimeFlags() & 1u) == 0u)
        {
            const std::uint32_t actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
            if (actionBits == 112u || actionBits == 116u)
            {
                SPRITE* const refOwner = engineCommandReferenceOwner();
                if (!refOwner || refOwner == this)
                {
                    SPRITE* const target = goalSprite();
                    SPRITE* const child = childChain();
                    if (target != child->goalSprite() &&
                        (target->Vid() == MAP::NullVid() || canWeaponAffectTarget(target)))
                    {
                        const float dx = std::fabs(target->X() - X());
                        const float dy = std::fabs(target->Y() - Y());
                        const float distance = engineLessEqualOrUnordered(dx, dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
                        VID* const childVid = child->Vid();
                        const bool childLinkReady = childVid == vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() && childVid->weaponCount() &&
                            engineFcompEqualOrUnorderedZero(vid->weaponBattleRange());
                        VID* const thresholdVid = childLinkReady ? childVid : vid;
                        if (distance < thresholdVid->weaponBattleRange())
                            result = child->SetCommandWithoutLink(4, target);
                    }
                }
            }
            else
            {
                SPRITE* const child = childChain();
                if (child)
                {
                    VID* const childVid = child->Vid();
                    if (childVid == vid->linkedVid() && childVid->hasWeaponChildDescriptor() && childVid->weaponCount())
                    {
                        SPRITE* const target = child->goalSprite();
                        if (target)
                        {
                            const float dx = std::fabs(target->X() - X());
                            const float dy = std::fabs(target->Y() - Y());
                            const float distance = engineLessEqualOrUnordered(dx, dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
                            if (distance > childVid->weaponDetectRange())
                                result = child->SetCommandWithoutLink(0, nullptr);
                        }
                    }
                }
            }

            int delta = static_cast<int>(vid->defaultFrameSpeed());
            const std::uint32_t elapsed = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            if (elapsed > static_cast<std::uint32_t>(static_cast<unsigned short>(delta)))
                delta = static_cast<int>(elapsed);
            result = computeAttackDecisionCode(static_cast<std::uint32_t>(delta));
            setAttackDecisionCode(result);

            if (result == 1 || result == 2 || result == 5 || result == 6)
            {
                SPRITE* const child = childChain();
                if ((behaviorFlags() & 1) != 0)
                {
                    if (child->actionTimer() != 0 || (static_cast<std::uint32_t>(std::rand()) & 0x80000003u) == 0u)
                    {
                        if (SPRITE* const target = SeekEnemy())
                            result = child->SetCommand(5, target);
                    }
                    else if (SPRITE* const target = bestTargetSprite())
                    {
                        result = child->SetCommand(5, target);
                    }
                }
                else if (SPRITE* const target = bestTargetSprite())
                {
                    result = child->SetCommand(5, target);
                }
            }
        }
        return result;
    }

    int ENGINE::needsMaintenanceService() noexcept
    {
        VID* const vid = Vid();
        const int bucket = armyIndex();
        if (animationFrameTime() >= vid->animationFrameDuration(bucket))
        {
            VID* linkVid = vid->linkedVid();
            if (linkVid)
            {
                SPRITE* const child = childChain();
                if (!child || child->Vid() != linkVid)
                {
                    if (vid->nvid() != 35)
                        return 1;
                    core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                    VID* slot40 = table.count() > 40 ? table.slot(40) : nullptr;
                    if (!slot40)
                        slot40 = MAP::NullVid();
                    const DWORD count40 = slot40->spriteCountForArmy(bucket);
                    VID* slot35 = table.count() > 35 ? table.slot(35) : nullptr;
                    if (!slot35)
                        slot35 = MAP::NullVid();
                    if (count40 < slot35->spriteCountForArmy(bucket))
                        return 1;
                    linkVid = vid->linkedVid();
                }
            }

            SPRITE* const child = childChain();
            if (!child || child->Vid() != linkVid || linkVid->spriteClassId() == 9u ||
                child->animationFrameTime() >= child->Vid()->animationFrameDuration(child->armyIndex()))
            {
                int capacity = vid->weaponRecordAmmoCapacity();
                if (linkVid && linkVid->hasWeaponChildDescriptor() && linkVid->weaponCount())
                    capacity = linkVid->weaponRecordAmmoCapacity();
                if (ammoCount() >= capacity)
                    return 0;
            }
        }
        return 1;
    }

    void ENGINE::stopEngineChain() noexcept
    {
        SPRITE* head = this;
        if (engineChainPrevious())
            head = engineChainHead();
        if (engineSpeedExceedsStopThreshold(head->m_speed, engineRawConstantFloat(GlobalBaseConstants()->raw[24])))
        {
            for (SPRITE* node = head; node; node = node->engineChainNext())
                (void)node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 2, 0, 0);
        }
        head->engineTargetSpeedRef() = 0.0f;
        head->m_speed = 0.0f;
    }

    void ENGINE::setEnginePathGoalFromCoordinates(float x, float y, float z, int, int project2D) noexcept
    {
        core::WeakController* node = nullptr;
        if (project2D)
        {
            const int iy = engineFtolLow32(y);
            const int iz = engineFtolLow32(z);
            const int ix = engineFtolLow32(x);
            node = core::findNearestLinkedNode2D(&core::globalWeakControllerMap(), ix, engineSub32Wrap(iy, iz));
        }
        else
        {
            const int iz = engineFtolLow32(z);
            const int iy = engineFtolLow32(y);
            const int ix = engineFtolLow32(x);
            node = core::findNearestLinkedNode3D(&core::globalWeakControllerMap(), ix, iy, iz);
        }
        dispatchEnginePrivateCommand(23, 0, static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node))), 0);
    }

    void ENGINE::MoveTact()
    {
        updateEngineChainRoute();

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & 0xFFFFC000u) > core::PreviousWorldTimeMilliseconds())
            setRuntimeFlags(runtimeFlags() & ~0x2u);

        if ((now & 0xFFFFFC00u) > core::PreviousWorldTimeMilliseconds() && !engineChainPrevious())
        {
            EngineChainMetrics range;
            range.categoryFlags = 0;
            range.collectEngineChainMetrics(this);
            const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
            if (range.averageDistanceRatio <= 10)
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackMoveRange), selfArg, 0);
            if (range.spriteFrameTimeSum < range.vidFrameTimeSum / 2)
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(kEngineCallbackMoveFrame), selfArg, 0);
        }

        if ((now & 0xFFFFFC00u) > core::PreviousWorldTimeMilliseconds())
        {
            VID* const vid = Vid();
            const int nvid = vid->nvid();
            switch (nvid)
            {
            case 85:
                updateRepairTarget();
                break;
            case 45:
                updateMaintenanceTarget();
                break;
            case 35:
                setSecondaryStateFlag(0);
                break;
            default:
                break;
            }
        }
    }

    void ENGINE::updateMaintenanceTarget() noexcept
    {
        SPRITE* best = nullptr;
        int bestMetric = 0;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNext())
        {
            if (!sameArmy(*node))
                continue;
            const int metric = node->ammoMissingPercent();
            VID* const nodeVid = node->Vid();
            if (metric > bestMetric && nodeVid->nvid() != 85)
            {
                bestMetric = metric;
                best = node;
            }
        }

        if (best)
        {
            const BASE_CONSTANTS* const constants = GlobalBaseConstants();
            const int divisor = static_cast<int>(constants->raw[5]);
            best->refillAmmoByCapacityFraction(divisor);
            if (currentAnimation() != static_cast<int>(AnimationCode::ANI_OPEN))
                ChangeAnimation(static_cast<int>(AnimationCode::ANI_OPEN));
        }
        else if (currentAnimation() == static_cast<int>(AnimationCode::ANI_OPEN))
        {
            ChangeAnimation(static_cast<int>(AnimationCode::ANI_STAND));
        }
    }

    void ENGINE::DeletePointerToSprite(SPRITE* sprite)
    {
        clearEngineSpriteReference(sprite);
    }

    void ENGINE::clearEngineSpriteReference(SPRITE* sprite) noexcept
    {
        if (engineCommandReferenceOwner() == sprite)
            dispatchEnginePrivateCommand(0, 0, 0, 0);

        VID* const targetVid = sprite->Vid();
        if (targetVid->spriteClassId() == B_ENGINE &&
            goalSprite() == sprite && (runtimeFlags() & SPRITE::CommandBitsMask) == 0x68u)
        {
            ENGINE* const targetEngine = static_cast<ENGINE*>(sprite);
            SPRITE* const peer = targetEngine->chainNext() ? targetEngine->chainNext() : targetEngine->chainPrevious();
            if (peer)
                dispatchEnginePrivateCommand(26, static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(peer))), 0, 0);
        }

        if (goalSprite() == sprite)
            dispatchEnginePrivateCommand(0, 0, 0, 0);

        SPRITE::DeletePointerToSprite(sprite);
    }

    ENGINE* ENGINE::scalarDeletingDestructorEngine(unsigned char flags) noexcept
    {
        ENGINE* const self = this;
        destroyEngineStateNoFree();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void ENGINE::DrawDebugOverlay()
    {
        drawEngineDebugState();
    }

    void ENGINE::DrawRelationDebugOverlay()
    {
        (void)drawEnginePathDebug();
    }

    void ENGINE::drawEngineDebugState() noexcept
    {
        GRAPH* const graph = GRAPH::CurrentGraph();
        float y = static_cast<float>(graph->getViewportTop());
        const float x = static_cast<float>(graph->getViewportLeft()) + 30.0f;

        auto nvidOf = [](SPRITE* sprite) noexcept -> int {
            return sprite ? sprite->Vid()->nvid() : 0;
        };
        auto drawStatus = [&](SPRITE* sprite, bool includeSpeed) {
            const int ammo = sprite->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
            if (includeSpeed)
            {
                graph->DrawText(
                    x, y,
                    "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i speed=%-3i timer=%i",
                    sprite->listReferenceCount(),
                    sprite->commandIndex(),
                    sprite->currentAnimation(), ammo, sprite->animationFrameTime(),
                    sprite->attackDecisionCode(), nvidOf(sprite->goalSprite()),
                    nvidOf(sprite->bestTargetSprite()),
                    static_cast<int>(sprite->Speed() * 1000.0f),
                    static_cast<int>(sprite->actionTimer()));
            }
            else
            {
                graph->DrawText(
                    x, y,
                    "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i timer=%i",
                    sprite->listReferenceCount(),
                    sprite->commandIndex(),
                    sprite->currentAnimation(), ammo, sprite->animationFrameTime(),
                    sprite->attackDecisionCode(), nvidOf(sprite->goalSprite()),
                    nvidOf(sprite->bestTargetSprite()),
                    static_cast<int>(sprite->actionTimer()));
            }
        };

        drawStatus(this, true);
        y += 12.0f;
        graph->DrawText(
            x,
            y,
            "IS_PBJMF(%i%i%i%i%i) Accel=%i MaxSpeed=%i NoStep=%i NoStepNotF=%i",
            pushLineActiveRef(),
            secondaryStateFlag(),
            static_cast<int>(runtimeFlags() & 1u),
            routeActionReadyRef(),
            static_cast<int>((runtimeFlags() >> 7u) & 1u),
            engineAccelerationDelayRef(),
            static_cast<int>(engineTargetSpeedRef() * 1000.0f),
            PathSearchResultScore(),
            PathSearchSecondaryBestCost());

        if (SPRITE* const child = childChain())
        {
            VID* const linkVid = Vid()->linkedVid();
            if (child->Vid() == linkVid)
            {
                y += 12.0f;
                drawStatus(child, false);
            }
        }

        const std::uint32_t commandCount = commandRecordCount();
        if (commandCount != 0u)
        {
            y += 12.0f;
            STRING commandText;
            STRING part;
            constructFormattedString(part, "%i - ", static_cast<int>(commandCount));
            appendStringOwner(commandText, part);
            for (std::uint32_t i = 0; i < commandCount; ++i)
            {
                constructFormattedString(part,
                           "%i(%i,%i,%i) ",
                           static_cast<int>(commandRecordWord(i, 0)),
                           static_cast<int>(commandRecordWord(i, 1)),
                           static_cast<int>(commandRecordWord(i, 2)),
                           static_cast<int>(commandRecordWord(i, 3)));
                appendStringOwner(commandText, part);
            }
            graph->drawStringColored(x, y, commandText, 0xFFFFFFFFu);
        }

        DrawRelationDebugOverlay();

        if (SPRITE* const ref = engineCommandReferenceOwner())
        {
            const auto& drawState = core::GlobalApplicationDrawDispatcherState();
            const float refX = ref->X() - drawState.cameraShiftX();
            const float refY = ref->Y() - ref->Z() - drawState.cameraShiftY();
            graph->drawBackBufferPixel2x2(refX, refY, 0xFFFF0000u);
        }
    }

    int ENGINE::drawEnginePathDebug() noexcept
    {
        GRAPH* const graph = GRAPH::CurrentGraph();
        const auto& drawState = core::GlobalApplicationDrawDispatcherState();
        auto screenX = [&](const SPRITE* sprite) noexcept -> float {
            return sprite->X() - drawState.cameraShiftX();
        };
        auto screenY = [&](const SPRITE* sprite) noexcept -> float {
            return sprite->Y() - sprite->Z() - drawState.cameraShiftY();
        };
        auto drawWeak = [&](core::WeakController* node, float offset, DWORD color) {
            if (!node)
                return;
            graph->DrawLine(screenX(this) + offset,
                            screenY(this) + offset,
                            static_cast<float>(core::weakControllerScreenX(node)) + offset,
                            static_cast<float>(core::weakControllerScreenY(node)) + offset,
                            color);
        };

        drawWeak(engineCommandArgument1Node(), 2.0f, 0xFFFFFFFFu);
        drawWeak(engineCommandArgument2Node(), 2.0f, 0xFFFFFFFFu);

        if (SPRITE* const target = goalSprite())
            graph->DrawLine(screenX(this), screenY(this), screenX(target), screenY(target), 0xFF00FF00u);

        if (SPRITE* const child = childChain())
        {
            if (SPRITE* const childTarget = child->goalSprite())
                graph->DrawLine(screenX(child), screenY(child), screenX(childTarget), screenY(childTarget), 0xFFFF0000u);
        }

        drawWeak(engineCommandArgument0Node(), 0.0f, 0xFF0000FFu);
        return graph->unlockBackBufferIfLocked();
    }

    void ENGINE::destroyEngineStateNoFree() noexcept
    {
        // No-delete scalar-destructor body: run the ENGINE-specific prefix and
        // then the existing UNIT body exactly once. This mirrors
        // destroyEngineStateNoFree -> destroyCommandSpriteState without freeing storage.
        releaseEngineRuntimeState();
        destroyCommandSpriteState();
    }
}
