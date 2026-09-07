#include "depo.h"
#include "constant.h"
#include "core/application.h"
#include "core/log.h"
#include "engine.h"
#include "map.h"
#include "mouse.h"
#include "player.h"
#include "sprite_collector_hash.h"
#include "vid/vid.h"
#include "win/application_win.h"

#include <array>

namespace as1
{
    namespace
    {
        constexpr std::uint32_t kDepoCoarseTimeMask = 0xFFFFC000u;

        PLAYER* retailPlayerSlot(int bucket) noexcept
        {
#ifdef _WIN32
            return win::applicationWinInstance()->startupPlayerSlotByIndex(bucket);
#else
            (void)bucket;
            return nullptr;
#endif
        }
    }

    DEPO::DEPO(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        m_createdEngineSequence = 0;
        m_queuedNvids.fill(0);
        m_queuedBuildTimes.fill(0);
        m_queuedCompletionFlags.fill(0);
        m_activeQueueCursor = 0;
        m_queueCapacity = 0x0A;
        m_queueCount = 0;
    }

    DEPO::~DEPO() = default;

    DEPO* DEPO::depoScalarDeletingDestructor(unsigned char flags) noexcept
    {
        DEPO* const self = this;
        destroyDepoState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void DEPO::destroyDepoState() noexcept
    {
        destroyCommandSpriteState();
    }

    std::uint16_t DEPO::queuedNvidAt(int index) const noexcept
    {
        return (index >= 0 && index < 20) ? m_queuedNvids[static_cast<std::size_t>(index)] : 0;
    }

    std::uint32_t DEPO::queuedBuildTimeAt(int index) const noexcept
    {
        return (index >= 0 && index < 20) ? m_queuedBuildTimes[static_cast<std::size_t>(index)] : 0;
    }

    std::uint32_t DEPO::queuedCompletionFlagAt(int index) const noexcept
    {
        return (index >= 0 && index < 20) ? m_queuedCompletionFlags[static_cast<std::size_t>(index)] : 0;
    }

    int DEPO::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
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
            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0x40u)
                return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
            appendCommandRecord(buildCommandRecord(0x23u, static_cast<int>(m_queuedNvids[0]), 0, 0));
            SetCommand(0, nullptr);
            return 0;

        case 0x23:
            if (!mapOwner()->HasVidSlot(argument1))
                return 0;
            (void)enqueueDepoPurchase(argument1);
            startNextDepoBuild();
            return 0;

        case 0x82:
            if (currentAnimation() >= 0x0F)
                return 0;
            if (currentAnimation() == 0x0D)
                ChangeAnimation(0);
            if (m_activeQueueCursor == 0u || m_queuedCompletionFlags[m_activeQueueCursor - 1u] != 0u)
            {
                if (currentAnimation() != 0)
                    ChangeAnimation(0);
                if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                    SetCommand(0, nullptr);
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(14u), reinterpret_cast<intptr_t>(this), 0);
                return 0;
            }
            if (actionTimer() != 0u)
            {
                if (currentAnimation() != 1)
                    ChangeAnimation(1);
                return 0;
            }
            (void)spawnNextDepoUnit(argument1, argument2);
            return 0;

        case 0x55:
            if (argument1 > 0 && (runtimeFlags() & 2u) == 0u && argument2 != 0)
            {
                SPRITE* const target = reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument2)));
#if INTPTR_MAX == INT32_MAX
                if (((runtimeFlags() ^ target->runtimeFlags()) & SPRITE::ArmyBitsMask) != 0u)
                {
                    (void)core::Application::callScriptFunction(core::scriptCallbackSlot(13u), reinterpret_cast<intptr_t>(this), reinterpret_cast<intptr_t>(target));
                    setRuntimeFlags(runtimeFlags() | 2u);
                }
#endif
            }
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);

        case 0x61:
        {
            const int oldBucket = armyIndex();
            retailPlayerSlot(oldBucket)->removeActivePlayerSpriteReference(this);
            changeArmyBucket(static_cast<signed char>(argument1));
            const int newBucket = armyIndex();
            if (oldBucket != newBucket)
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(15u), reinterpret_cast<intptr_t>(this), 0);
            retailPlayerSlot(newBucket)->noOpSpriteCallback(this);
            return 0;
        }

        case 0x51:
        case 0xC8:
        {
            (void)dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
            const int bucket = armyIndex();
            retailPlayerSlot(bucket)->noOpSpriteCallback(this);
            static constexpr std::array<std::int16_t, 14> kOrder = {
                5, 10, 20, 25, 80, 85, 45, 30, 35, 82, 97, 90, 75, 62
            };
            int writeIndex = 0;
            for (std::int16_t wanted : kOrder)
            {
                int scan = writeIndex;
                while (scan < static_cast<int>(commandWordCount()))
                {
                    if (commandWordAt(scan) == wanted)
                    {
                        const int atWrite = commandWordAt(writeIndex);
                        std::int32_t* const words = const_cast<std::int32_t*>(commandWordData());
                        words[writeIndex] = static_cast<std::int32_t>(wanted);
                        words[scan] = static_cast<std::int32_t>(atWrite);
                        ++writeIndex;
                    }
                    ++scan;
                }
            }
            return 0;
        }

        case 0x50:
            if (!hasCommandOpcode(0x49u))
                appendCommandRecord(buildCommandRecord(0x49u, 0, 0, 0));
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);

        default:
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
        }
    }

    int DEPO::enqueueDepoPurchase(int nvid) noexcept
    {
        if (static_cast<std::int32_t>(m_queueCount) >=
            static_cast<std::int32_t>(m_queueCapacity))
            return static_cast<int>(m_queueCount);
        const int bucket = armyIndex();
        PLAYER* const player = retailPlayerSlot(bucket);
        VID* const vid = mapOwner()->VidOrNull(nvid);
        const int cost = vid->getWeaponValue24Scaled();
        if (static_cast<int>(player->money()) < cost)
            return static_cast<int>(m_queueCount);

        player->setMoney(player->money() - static_cast<DWORD>(cost));
        const std::size_t slot = m_queueCount;
        m_queuedNvids[slot] = static_cast<std::uint16_t>(nvid);
        m_queuedBuildTimes[slot] = GlobalBaseConstants()->raw[9] * static_cast<DWORD>(cost);
        m_queuedCompletionFlags[slot] = 0;
        ++m_queueCount;
        return static_cast<int>(m_queueCount);
    }

    void DEPO::startNextDepoBuild() noexcept
    {
        if (m_activeQueueCursor != 0u)
            return;
        m_activeQueueCursor = 1u;
        while (static_cast<std::int32_t>(m_activeQueueCursor) <=
                   static_cast<std::int32_t>(m_queueCapacity) &&
               m_queuedCompletionFlags[m_activeQueueCursor - 1u] != 0u)
            ++m_activeQueueCursor;

        if (static_cast<std::int32_t>(m_activeQueueCursor) >
            static_cast<std::int32_t>(m_queueCount))
        {
            m_activeQueueCursor = 0;
            SetCommand(0, nullptr);
            ChangeAnimation(0);
            return;
        }

        SetCommand(0x10, nullptr);
        const std::size_t slot = m_activeQueueCursor - 1u;
        const std::uint32_t saved = m_queuedBuildTimes[slot];
        setActionTimer(saved != 0u
            ? saved
            : GlobalBaseConstants()->raw[9] * static_cast<DWORD>(mapOwner()->VidOrNull(m_queuedNvids[slot])->getWeaponValue24Scaled()));
    }

    int DEPO::spawnNextDepoUnit(int, int) noexcept
    {
        const std::size_t slot = m_activeQueueCursor - 1u;
        VID* const createVid = mapOwner()->VidOrNull(m_queuedNvids[slot]);
        for (;;)
        {
            SPRITE* const collision = GlobalHashQueryCellCollisionByVid(*mapOwner(), createVid, X(), Y(), Z());
            if (!collision || collision == mouseSprite())
                break;
            DeleteSpriteThroughVirtualDeletingDestructor(collision);
        }

        SPRITE* const created = mapOwner()->CreateSpriteViaFactory(createVid, xyz(), Direction(), this, false);
        if (!created)
        {
            const VID* const selfVid = Vid();
            LOG::ResourceError("SPRITE %i", 10, "Depo can't create unit",
                               static_cast<int>(m_queuedNvids[slot]),
                               selfVid ? selfVid->nvid() : -1);
            return 0;
        }

        if (created->Vid()->spriteClassId() == 21u)
            static_cast<ENGINE*>(created)->setProductionSequenceId(static_cast<int>(m_createdEngineSequence));

        --m_queueCount;
        for (std::int32_t i = static_cast<std::int32_t>(slot);
             i < static_cast<std::int32_t>(m_queueCount); ++i)
        {
            const std::size_t at = static_cast<std::size_t>(i);
            m_queuedNvids[at] = m_queuedNvids[at + 1u];
            m_queuedBuildTimes[at] = m_queuedBuildTimes[at + 1u];
            m_queuedCompletionFlags[at] = m_queuedCompletionFlags[at + 1u];
        }
        m_activeQueueCursor = 0;
        created->setRuntimeFlags(created->runtimeFlags() | 1u);
        (void)core::Application::callScriptFunction(core::scriptCallbackSlot(23u), reinterpret_cast<intptr_t>(created), 0);
        created->playSfxAtWorldPosition(105);

        if (m_queueCount != 0u || (noCommandStackEntry() != 0u && lastCommandOpcode() != 0x49u))
        {
            startNextDepoBuild();
            return 0;
        }
        if (created->Vid()->spriteClassId() == 21u)
            static_cast<ENGINE*>(created)->setProductionBatchCompletionPending(1);
        ++m_createdEngineSequence;
        return 0;
    }

    void DEPO::swapQueueEntries(int firstIndex, int secondIndex) noexcept
    {
        if (firstIndex == secondIndex)
            return;

        bool restartActive = false;
        const int activeIndex = static_cast<int>(m_activeQueueCursor) - 1;
        if (firstIndex == activeIndex)
        {
            m_queuedBuildTimes[static_cast<std::size_t>(firstIndex)] = actionTimer();
            restartActive = true;
        }
        else if (secondIndex == activeIndex)
        {
            m_queuedBuildTimes[static_cast<std::size_t>(secondIndex)] = actionTimer();
            restartActive = true;
        }

        const std::uint16_t nvid = m_queuedNvids[static_cast<std::size_t>(secondIndex)];
        m_queuedNvids[static_cast<std::size_t>(secondIndex)] = m_queuedNvids[static_cast<std::size_t>(firstIndex)];
        m_queuedNvids[static_cast<std::size_t>(firstIndex)] = nvid;

        const std::uint32_t buildTime = m_queuedBuildTimes[static_cast<std::size_t>(firstIndex)];
        m_queuedBuildTimes[static_cast<std::size_t>(firstIndex)] = m_queuedBuildTimes[static_cast<std::size_t>(secondIndex)];
        m_queuedBuildTimes[static_cast<std::size_t>(secondIndex)] = buildTime;

        const std::uint32_t flag = m_queuedCompletionFlags[static_cast<std::size_t>(secondIndex)];
        m_queuedCompletionFlags[static_cast<std::size_t>(secondIndex)] = m_queuedCompletionFlags[static_cast<std::size_t>(firstIndex)];
        m_queuedCompletionFlags[static_cast<std::size_t>(firstIndex)] = flag;

        if (restartActive)
        {
            m_activeQueueCursor = 0;
            startNextDepoBuild();
        }
    }

    void DEPO::cancelQueueEntry(int index) noexcept
    {
        if (m_queueCount == 0u)
            return;

        if (m_activeQueueCursor != 0u)
            m_queuedBuildTimes[m_activeQueueCursor - 1u] = actionTimer();

        const std::size_t slot = static_cast<std::size_t>(index);
        VID* const vid = mapOwner()->VidOrNull(m_queuedNvids[slot]);
        PLAYER* const player = retailPlayerSlot(armyIndex());
        player->setMoney(player->money() + static_cast<DWORD>(vid->getWeaponValue24Scaled()));

        for (std::uint32_t i = static_cast<std::uint32_t>(index); i + 1u < m_queueCount; ++i)
        {
            m_queuedNvids[i] = m_queuedNvids[i + 1u];
            m_queuedBuildTimes[i] = m_queuedBuildTimes[i + 1u];
            m_queuedCompletionFlags[i] = m_queuedCompletionFlags[i + 1u];
        }

        --m_queueCount;
        if (m_queueCount == 0u)
        {
            m_queueCount = 0;
            m_activeQueueCursor = 0;
            SetCommand(0, nullptr);
            ChangeAnimation(0);
            return;
        }

        m_activeQueueCursor = 0;
        startNextDepoBuild();
    }

    int DEPO::queueProgress255(int index) const noexcept
    {
        if (static_cast<std::int32_t>(m_queueCount) <= index)
            return 0xFF;

        const std::size_t slot = static_cast<std::size_t>(index);
        VID* const vid = mapOwner()->VidOrNull(m_queuedNvids[slot]);
        if (!vid)
            return 0xFF;

        const std::uint32_t remaining =
            index == static_cast<int>(m_activeQueueCursor) - 1
                ? actionTimer()
                : m_queuedBuildTimes[slot];
        const std::uint32_t cost = static_cast<std::uint32_t>(vid->getWeaponValue24Scaled());
        return static_cast<int>((remaining * 0xFFu / cost) / GlobalBaseConstants()->raw[9]);
    }

    int DEPO::queueEntryFlag(int index) const noexcept
    {
        if (index >= static_cast<int>(m_queueCount))
            return 0;
        return m_queuedCompletionFlags[static_cast<std::size_t>(index)] != 0u ? 1 : 0;
    }

    void DEPO::moveQueueEntryUp(int index) noexcept
    {
        if (index < static_cast<int>(m_queueCount) && index >= 1)
            swapQueueEntries(index - 1, index);
    }

    int DEPO::toggleQueueEntryFlag(int index) noexcept
    {
        if (index >= static_cast<int>(m_queueCount))
            return 0;

        const std::size_t slot = static_cast<std::size_t>(index);
        if (m_queuedCompletionFlags[slot] == 0u)
        {
            m_queuedCompletionFlags[slot] = 1u;
            if (index == static_cast<int>(m_activeQueueCursor) - 1)
            {
                m_queuedBuildTimes[slot] = actionTimer();
                m_activeQueueCursor = 0;
                startNextDepoBuild();
            }
            return static_cast<int>(m_queuedCompletionFlags[slot]);
        }

        m_queuedCompletionFlags[slot] = 0u;
        const std::uint32_t active = m_activeQueueCursor;
        if (index < static_cast<int>(active) - 1 || active == 0u)
        {
            if (active != 0u)
                m_queuedBuildTimes[active - 1u] = actionTimer();
            m_activeQueueCursor = 0;
            startNextDepoBuild();
        }
        return static_cast<int>(m_queuedCompletionFlags[slot]);
    }

    void DEPO::replaceQueueEntryNvid(int index, int nvid) noexcept
    {
        if (index >= static_cast<int>(m_queueCount))
            return;

        const std::size_t slot = static_cast<std::size_t>(index);
        PLAYER* const player = retailPlayerSlot(armyIndex());
        VID* const oldVid = mapOwner()->VidOrNull(m_queuedNvids[slot]);
        player->setMoney(player->money() + static_cast<DWORD>(oldVid->getWeaponValue24Scaled()));

        VID* const newVid = mapOwner()->VidOrNull(nvid);
        player->setMoney(player->money() - static_cast<DWORD>(newVid->getWeaponValue24Scaled()));
        m_queuedNvids[slot] = static_cast<std::uint16_t>(nvid);
        m_queuedBuildTimes[slot] =
            static_cast<std::uint32_t>(newVid->getWeaponValue24Scaled()) * GlobalBaseConstants()->raw[9];

        if (m_activeQueueCursor != 1u)
        {
            m_activeQueueCursor = 0;
            startNextDepoBuild();
        }
    }

    void DEPO::moveQueueEntryDown(int index) noexcept
    {
        const int next = index + 1;
        if (next < static_cast<int>(m_queueCount))
            swapQueueEntries(index, next);
    }

    void DEPO::MoveTact()
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & kDepoCoarseTimeMask) > core::PreviousWorldTimeMilliseconds())
            setRuntimeFlags(runtimeFlags() & 0xFFFFFFFDu);
        advanceAnimationFrameTimeCapped(static_cast<int>(GlobalBaseConstants()->raw[0x38u / sizeof(DWORD)]));
    }
}
