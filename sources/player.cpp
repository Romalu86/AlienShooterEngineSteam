#include <cstring>

#include "player.h"

#include "core/resource.h"
#include "map.h"
#include "menu.h"
#include "graph.h"
#include "input.h"
#include "sprite.h"
#include "vid/vid.h"
#include "graphics/angle.h"
#include "input/control_actions.h"
#include "script/action_constants.h"
#include "core/application.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/as_string.h"
#include "core/configuration.h"
#include "core/profile_p.h"
#include <new>
#include <cstdio>
#include "sound/engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace as1
{
    namespace
    {
        class PlayerBaseVtableOwner
        {
        public:
            virtual PLAYER* deletingDestructor(unsigned char flags) noexcept { return reinterpret_cast<PLAYER*>(this)->scalarDeletingDestructorBasePlayer(flags); }
            virtual std::intptr_t deletePointer(SPRITE* sprite) noexcept { return reinterpret_cast<PLAYER*>(this)->removePlayerSpriteReference(sprite); }
            virtual int save(RESOURCE* resource) noexcept { return reinterpret_cast<PLAYER*>(this)->saveControlledSpriteReference(resource); }
            virtual int load(RESOURCE* resource) noexcept { return reinterpret_cast<PLAYER*>(this)->loadControlledSpriteReference(resource); }
            virtual void reset() noexcept { reinterpret_cast<PLAYER*>(this)->resetPlayerSpriteReferences(); }
            virtual void setFlagman(SPRITE* sprite) noexcept { reinterpret_cast<PLAYER*>(this)->setFlagmanSprite(sprite); }
            virtual void input(as1::input::InputMessageState*) noexcept {}
            virtual int reserved7() noexcept { return 0; }
            virtual int reserved8() noexcept { return 0; }
            virtual int coordinate(STRING*, float, float) noexcept { return 0; }
            virtual void reserved10(SPRITE* sprite) noexcept { reinterpret_cast<PLAYER*>(this)->noOpSpriteCallback(sprite); }
            virtual STRING* name(STRING* out) noexcept { return reinterpret_cast<PLAYER*>(this)->getAuxiliaryUnitName(out); }
        };

        class PlayerActiveVtableOwner final : public PlayerBaseVtableOwner
        {
        public:
            PLAYER* deletingDestructor(unsigned char flags) noexcept override { return reinterpret_cast<PLAYER*>(this)->scalarDeletingDestructorActivePlayer(flags); }
            std::intptr_t deletePointer(SPRITE* sprite) noexcept override { return reinterpret_cast<PLAYER*>(this)->removeActivePlayerSpriteReference(sprite); }
            void input(as1::input::InputMessageState* state) noexcept override { reinterpret_cast<PLAYER*>(this)->processInput(state); }
            int coordinate(STRING* name, float x, float y) noexcept override { return reinterpret_cast<PLAYER*>(this)->submitPathCoordinate(name, x, y); }
        };

        class PlayerPathVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return scalarDeletingDestructorPathOwner(this, flags);
            }
            virtual void deletePointer(SPRITE* sprite) noexcept
            {
                clearPathSpriteReferences(this, sprite);
            }
        };

        class PlayerPendingVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return scalarDeletingDestructorPathPendingList(this, flags);
            }
        };

        template <class T>
        DWORD currentImagePlayerVtable() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static T owner;
            return static_cast<DWORD>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
            return 0u;
#endif
        }

        SPRITE* decodeRetailSpritePointer(DWORD value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(value));
#else
            (void)value;
            return nullptr;
#endif
        }

        DWORD encodeRetailSpritePointer(SPRITE* sprite) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(sprite));
#else
            (void)sprite;
            return 0u;
#endif
        }

    }

    DWORD PLAYER::CurrentImageBaseVtable() noexcept { return currentImagePlayerVtable<PlayerBaseVtableOwner>(); }
    DWORD PLAYER::CurrentImageActiveVtable() noexcept { return currentImagePlayerVtable<PlayerActiveVtableOwner>(); }
    DWORD PLAYER::CurrentImagePathVtable() noexcept { return currentImagePlayerVtable<PlayerPathVtableOwner>(); }
    DWORD PLAYER::CurrentImagePendingVtable() noexcept { return currentImagePlayerVtable<PlayerPendingVtableOwner>(); }

    namespace
    {
        std::uint32_t playerFloatBits(float value) noexcept
        {
            std::uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        bool playerRetailFcompC3(float lhs, float rhs) noexcept
        {
            return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        int playerTruncateFloatToInt32(float value) noexcept
        {
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                value >= 2147483648.0f)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<int>(std::trunc(value));
        }

        bool playerOrderedFloatEqual(float lhs, float rhs) noexcept
        {
            // The fallthrough is ordered equality only; unordered is treated as
            // not-equal.
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs == rhs;
        }

        int playerConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
        }

        int playerProjectedRowToInt32(float spriteY, float spriteZ,
                                         float cameraY, float viewportTop) noexcept
        {
            // The game logic keeps the projected-row numerator in binary32:
            // single-precision subtraction(Y,Z) -> single-precision subtraction(cameraY) -> single-precision subtraction(viewportTop) -> truncating float-to-int conversion,
            // followed by the integer signed integer division performed by the caller.  The previous
            // Keep the calculation in single precision to preserve the intended rounding.
            float value = spriteY - spriteZ;
            value -= cameraY;
            value -= viewportTop;
            return playerTruncateFloatToInt32(value);
        }

        int playerImulLow32(int lhs, int rhs) noexcept
        {
            const std::uint64_t product =
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(lhs)) *
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(rhs));
            return static_cast<int>(static_cast<std::uint32_t>(product));
        }

        void destroyPendingStringArray(DWORD rawTable) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            if (rawTable == 0u)
                return;

            unsigned char* const table = reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(rawTable));
            int* const allocation = reinterpret_cast<int*>(table) - 1;
            const int count = *allocation;
            for (int i = count - 1; i >= 0; --i)
            {
                char* const text = *reinterpret_cast<char**>(table + static_cast<std::size_t>(i) * 16u);
                if (text != STRING::SharedEmptyText())
                    ::operator delete(text);
            }
            ::operator delete(allocation);
#else
            (void)rawTable;
#endif
        }
    }

    int releasePathSprites(void* pathOwner) noexcept
    {
        auto* const path = static_cast<PLAYER::PathOwnerLayout*>(pathOwner);
        const int routeCount = path->routeCount;
        if (routeCount <= 0)
            return routeCount;

        for (int i = 0; i < routeCount; ++i)
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            if (SPRITE* const secondary = decodeRetailSpritePointer(path->secondarySprites[i]))
                DeleteSpriteThroughVirtualDeletingDestructor(secondary);
#endif
            path->secondarySprites[i] = 0u;
#if UINTPTR_MAX == 0xFFFFFFFFu
            if (SPRITE* const primary = decodeRetailSpritePointer(path->primarySprites[i]))
                DeleteSpriteThroughVirtualDeletingDestructor(primary);
#endif
            path->primarySprites[i] = 0u;
        }
        return routeCount;
    }

    void clearPathSpriteReferences(void* pathOwner, SPRITE* sprite) noexcept
    {
        auto* const path = static_cast<PLAYER::PathOwnerLayout*>(pathOwner);
        const int routeCount = path->routeCount;
        if (routeCount <= 0)
            return;
        const DWORD target = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu);
        for (int i = 0; i < routeCount; ++i)
        {
            if (path->secondarySprites[i] == target)
                path->secondarySprites[i] = 0u;
            if (path->primarySprites[i] == target)
                path->primarySprites[i] = 0u;
        }
    }

    void* scalarDeletingDestructorPathPendingList(void* pendingOwner, unsigned char deleteSelfFlag) noexcept
    {
        auto* const pending = static_cast<PLAYER::PendingPathOwnerLayout*>(pendingOwner);
        pending->vtable = PLAYER::CurrentImagePendingVtable();
        destroyPendingStringArray(pending->entries);
        pending->entries = 0u;
        pending->count = 0;
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(pendingOwner);
        return pendingOwner;
    }

    void* scalarDeletingDestructorPathOwner(void* pathOwner, unsigned char deleteSelfFlag) noexcept
    {
        auto* const path = static_cast<PLAYER::PathOwnerLayout*>(pathOwner);
        path->vtable = PLAYER::CurrentImagePathVtable();
        (void)releasePathSprites(path);

        path->pending.vtable = PLAYER::CurrentImagePendingVtable();
        destroyPendingStringArray(path->pending.entries);
        path->pending.entries = 0u;
        path->pending.count = 0;

        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(pathOwner);
        return pathOwner;
    }

    PLAYER::~PLAYER() noexcept
    {
        destroyPathFindState();
        destroyBaseSpriteOwnerState();
    }

    PLAYER* PLAYER::initializeBasePlayerState(int controlMode, int playerSlot) noexcept
    {
        m_state.base.playerSlot = playerSlot;
        m_state.base.controlledSprite = 0u;
        m_state.base.listItems = 0u;
        m_state.base.listCount = 0;
        m_state.base.listCapacity = 0;
        m_state.base.listVtable = SPRITE_POINTER_LIST::CurrentImageCoreListVtable();
        m_state.base.auxiliarySprite = 0u;
        m_state.base.vtable = CurrentImageBaseVtable();

        // The two release branches in the compiler output immediately observe
        // the just-written zero state slots, so no hidden side effect is
        // omitted here.
        m_state.base.controlMode = controlMode;
        m_state.base.money = 1000u;
        return this;
    }

    PLAYER::PathOwnerLayout* PLAYER::PathOwnerLayout::initializePathOwner(
        int terrainVid, int secondaryTerrainVid, DWORD baseXBits, DWORD baseYBits,
        int routeCountValue, DWORD updateIntervalValue) noexcept
    {
        routeCount = routeCountValue;
        updateInterval = updateIntervalValue;
        std::memcpy(&baseX, &baseXBits, sizeof(baseX));
        std::memcpy(&baseY, &baseYBits, sizeof(baseY));
        this->terrainVid = terrainVid;
        this->secondaryTerrainVid = secondaryTerrainVid;
        pending.entries = 0u;
        pending.vtable = PLAYER::CurrentImagePendingVtable();
        pending.count = 0;
        pending.capacity = 0;
        vtable = PLAYER::CurrentImagePathVtable();
        rowDelta = -1;
        for (int i = 0; i < routeCount; ++i)
        {
            secondarySprites[i] = 0u;
            primarySprites[i] = 0u;
        }
        return this;
    }

    PLAYER* PLAYER::initializeActivePlayerState(int controlMode, int playerSlot) noexcept
    {
        initializeBasePlayerState(controlMode, playerSlot);
        m_state.path.initializePathOwner(4, -1, 0x43C70000u, 0x43C20000u, 5, 0x1388u);
        m_state.base.vtable = CurrentImageActiveVtable();

        GRAPH* const graph = GRAPH::CurrentGraph();
        const GraphViewportState viewport = graph->viewportState();
        m_state.path.baseX = viewport.right - 242.0f;
        m_state.path.baseY = viewport.bottom - 92.0f;

        m_state.path.rowDelta = 1;
        return this;
    }

    int PLAYER::submitPathCoordinate(STRING* className, float targetX, float targetY) noexcept
    {
        return submitPathCoordinateToOwner(className, targetX, targetY);
    }

    int PLAYER::submitPathCoordinateToOwner(STRING* className, float targetX, float targetY) noexcept
    {
        int terrainCellStep = 1;
        if (!validatePathCoordinateClass(className, &terrainCellStep))
            return 0;

        PathOwnerLayout& path = m_state.path;
        const int routeCount = path.routeCount;
        path.lastUpdate = as1::core::CurrentTimeMilliseconds();
        const int terminalIndex = routeCount - 1;
        if (path.secondarySprites[terminalIndex] != 0u)
            advancePathRouteWindow();

#if UINTPTR_MAX == 0xFFFFFFFFu
        MAP* const map = MAP::Current();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const float viewportTop = graph->viewportState().top;

        auto resolveVid = [](int index) noexcept -> VID* {
            VID* vid = nullptr;
            if (index >= 0 && index < as1::core::GlobalApplicationVidTable().count())
                vid = as1::core::GlobalApplicationVidTable().slot(index);
            return vid ? vid : MAP::NullVid();
        };

        if (path.secondarySprites[terminalIndex] == 0u)
        {
            VID* const vid = resolveVid(path.terrainVid);
            const float y = static_cast<float>(terrainCellStep * terminalIndex)
                          + path.baseY
                          + viewportTop + 2000.0f;
            SPRITE* const created = map->CreateSpriteViaFactory(
                vid,
                VECTOR{path.baseX, y, 2000.0f},
                ANGLE{0},
                nullptr,
                false);
            path.secondarySprites[terminalIndex] = encodeRetailSpritePointer(created);
        }

        if (SPRITE* const routeSprite = decodeRetailSpritePointer(path.secondarySprites[terminalIndex]))
        {
            routeSprite->dispatchVirtualAction(ActionCode::ACT_SET_TEXT,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(className)),
                0,
                0);
        }
#endif

        path.targetX[terminalIndex] = targetX;
        path.targetY[terminalIndex] = targetY;

#if UINTPTR_MAX == 0xFFFFFFFFu
        if (!playerRetailFcompC3(targetX, -1.0f) || !playerRetailFcompC3(targetY, -1.0f))
        {
            VID* primaryVid = nullptr;
            const int primaryVidIndex = path.secondaryTerrainVid;
            if (primaryVidIndex >= 0 && primaryVidIndex < as1::core::GlobalApplicationVidTable().count())
                primaryVid = as1::core::GlobalApplicationVidTable().slot(primaryVidIndex);
            if (!primaryVid)
                primaryVid = MAP::NullVid();

            GRAPH* const graph = GRAPH::CurrentGraph();
            const float viewportTop = graph->viewportState().top;
            const float y = static_cast<float>(terrainCellStep * terminalIndex)
                          + static_cast<float>(terrainCellStep / 2)
                          + path.baseY
                          + viewportTop + 2000.0f;
            const float x = path.baseX - 10.0f;
            SPRITE* const created = MAP::Current()->CreateSpriteViaFactory(
                primaryVid, VECTOR{x, y, 2000.0f}, ANGLE{0}, nullptr, false);
            path.primarySprites[terminalIndex] = encodeRetailSpritePointer(created);
        }
#endif

        return enqueuePathCoordinateSound();
    }

    int PLAYER::pathFindTerrainCellStep() const noexcept
    {
        VID* terrainVid = nullptr;
        const int terrainVidIndex = m_state.path.terrainVid;
        if (terrainVidIndex >= 0 && terrainVidIndex < as1::core::GlobalApplicationVidTable().count())
            terrainVid = as1::core::GlobalApplicationVidTable().slot(terrainVidIndex);
        if (!terrainVid)
            terrainVid = MAP::NullVid();

        return static_cast<int>(static_cast<short>(terrainVid->vidHeight())) + 1;
    }

    bool PLAYER::validatePathCoordinateClass(const STRING* className, int* terrainCellStepOut) noexcept
    {
        const int terrainCellStep = pathFindTerrainCellStep();
        if (terrainCellStepOut)
            *terrainCellStepOut = terrainCellStep;
        return std::strcmp(className->c_str(), "") != 0;
    }

    int PLAYER::enqueuePathCoordinateSound() noexcept
    {
        return sound::GlobalSoundEngine()->enqueueSoundRequest(0x6A, 0, 0);
    }

    void PLAYER::updatePathOwnerFrame() noexcept
    {
        PathOwnerLayout& path = m_state.path;
        const std::uint32_t now = as1::core::CurrentTimeMilliseconds();

#if UINTPTR_MAX == 0xFFFFFFFFu
        struct PendingPathCoordinateEntry
        {
            DWORD text;
            float x;
            float y;
            DWORD time;
        };
                                                                                                                    

        int index = 0;
        while (index < path.pending.count)
        {
            auto* const table = reinterpret_cast<PendingPathCoordinateEntry*>(
                static_cast<std::uintptr_t>(path.pending.entries));
            PendingPathCoordinateEntry& entry = table[index];

            if (now - as1::core::PreviousWorldTimeMilliseconds() < entry.time)
            {
                entry.time = as1::core::PreviousWorldTimeMilliseconds() + entry.time - now;
                ++index;
                continue;
            }

            submitPathCoordinateToOwner(reinterpret_cast<STRING*>(&entry), entry.x, entry.y);

            int count = path.pending.count;
            if (index >= 0 && index < count)
            {
                --count;
                path.pending.count = count;
                for (int move = index; move < count; ++move)
                {
                    PendingPathCoordinateEntry& dst = table[move];
                    PendingPathCoordinateEntry& src = table[move + 1];
                    assignStringFromString(*reinterpret_cast<STRING*>(&dst), *reinterpret_cast<const STRING*>(&src));
                    dst.x = src.x;
                    dst.y = src.y;
                    dst.time = src.time;
                }
            }

            if (path.pending.count == 0)
                clearPendingPathCoordinates();
        }
#endif

        const std::uint32_t previous = path.lastUpdate;
        const std::uint32_t interval = path.updateInterval;
        if (now - previous > interval)
        {
            advancePathRouteWindow();
            path.lastUpdate = now;
        }

        MENU& frameList = applicationMenu();
        if ((frameList.controlFlags() & 1u) != 0u &&
            frameList.SelectedNvid() == path.secondaryTerrainVid)
        {
            SPRITE* const selected = frameList.selectedSprite();
            GRAPH* const graph = GRAPH::CurrentGraph();
            const int terrainCellStep = pathFindTerrainCellStep();
            const int projectedRowNumerator = playerProjectedRowToInt32(
                selected->Y(),
                selected->Z(),
                as1::core::GlobalApplicationDrawDispatcherState().cameraShiftY(),
                graph->viewportState().top);
            const int row = projectedRowNumerator / terrainCellStep;
            const float cameraX = path.targetX[row];
            const float cameraY = path.targetY[row];
            if (!playerRetailFcompC3(cameraX, -999999.0f) || playerFloatBits(cameraY) != 0xC97423F0u)
                MAP::Current()->SetShiftCoor(cameraX, cameraY, 2);
        }
    }

    void PLAYER::advancePathRouteWindow() noexcept
    {
        PathOwnerLayout& path = m_state.path;
        const int routeCount = path.routeCount;
        if (routeCount <= 0)
            return;

        auto releaseLeadingRouteSprite = [](DWORD& slot) noexcept {
            if (SPRITE* const sprite = decodeRetailSpritePointer(slot))
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
            slot = 0u;
        };

        releaseLeadingRouteSprite(path.secondarySprites[0]);
        releaseLeadingRouteSprite(path.primarySprites[0]);

        if (routeCount <= 1)
            return;

        const int terrainCellStep = pathFindTerrainCellStep();
        const int pathRowDelta = playerImulLow32(path.rowDelta, terrainCellStep);

        for (int i = 1; i < routeCount; ++i)
        {
            SPRITE* const sprite = decodeRetailSpritePointer(path.primarySprites[i]);
            if (sprite)
            {
                sprite->ChangeCoor(sprite->X(), sprite->Y() + static_cast<float>(pathRowDelta), sprite->Z());
                path.primarySprites[i - 1] = path.primarySprites[i];
                path.targetX[i - 1] = path.targetX[i];
                path.targetY[i - 1] = path.targetY[i];
                path.primarySprites[i] = 0u;
            }
        }

        for (int i = 1; i < routeCount; ++i)
        {
            SPRITE* const sprite = decodeRetailSpritePointer(path.secondarySprites[i]);
            if (sprite)
            {
                sprite->ChangeCoor(sprite->X(), sprite->Y() + static_cast<float>(pathRowDelta), sprite->Z());
                path.secondarySprites[i - 1] = path.secondarySprites[i];
                path.secondarySprites[i] = 0u;
            }
        }
    }

    void PLAYER::clearPendingPathCoordinates() noexcept
    {
        PathOwnerLayout& path = m_state.path;
        path.pending.capacity = 0;
        path.pending.count = 0;

#if UINTPTR_MAX == 0xFFFFFFFFu
        if (path.pending.entries != 0u)
        {
            unsigned char* const table = reinterpret_cast<unsigned char*>(
                static_cast<std::uintptr_t>(path.pending.entries));
            int* const allocation = reinterpret_cast<int*>(table) - 1;
            const int count = *allocation;
            for (int i = count - 1; i >= 0; --i)
            {
                void* const text = *reinterpret_cast<void**>(table + static_cast<std::size_t>(i) * 16u);
                if (text != STRING::SharedEmptyText())
                    ::operator delete(text);
            }
            ::operator delete(allocation);
        }
#endif
        path.pending.entries = 0u;
    }

    void PLAYER::destroyPendingPathCoordinates() noexcept
    {
        PathOwnerLayout& path = m_state.path;
        path.pending.vtable = CurrentImagePendingVtable();
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (path.pending.entries != 0u)
        {
            unsigned char* const table = reinterpret_cast<unsigned char*>(
                static_cast<std::uintptr_t>(path.pending.entries));
            int* const allocation = reinterpret_cast<int*>(table) - 1;
            const int count = *allocation;
            for (int i = count - 1; i >= 0; --i)
            {
                void* const text = *reinterpret_cast<void**>(table + static_cast<std::size_t>(i) * 16u);
                if (text != STRING::SharedEmptyText())
                    ::operator delete(text);
            }
            ::operator delete(allocation);
        }
#endif
        path.pending.entries = 0u;
        path.pending.count = 0;
    }

    PLAYER* PLAYER::scalarDeletingDestructorBasePlayer(unsigned char deleteSelfFlag) noexcept
    {
        PLAYER* const self = this;
        destroyBaseSpriteOwnerState();
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    PLAYER* PLAYER::scalarDeletingDestructorActivePlayer(unsigned char deleteSelfFlag) noexcept
    {
        destroyPathFindState();
        destroyBaseSpriteOwnerState();
        PLAYER* const result = this;
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(result);
        return result;
    }

    void PLAYER::destroyPathFindState() noexcept
    {
        m_state.path.vtable = CurrentImagePathVtable();
        (void)releasePathSprites(&m_state.path);
        (void)scalarDeletingDestructorPathPendingList(&m_state.path.pending.vtable, 0u);
    }

    void PLAYER::destroyBaseSpriteOwnerState() noexcept
    {
        m_state.base.vtable = CurrentImageBaseVtable();
        resetPlayerSpriteReferences();

        if (SPRITE* const auxiliary = auxiliarySprite())
        {
            VID* const vid = auxiliary->Vid();
            LOG::ResourceError("SPRITE %i", 10, "ptr_spriteWith", 0, vid ? vid->nVid : -1);
        }

        m_state.base.listVtable = SPRITE_LIST::CurrentImageRelationListVtable();
#if UINTPTR_MAX == 0xFFFFFFFFu
        const DWORD listStorageAddress = m_state.base.listItems;
        if (listStorageAddress != 0u)
            ::operator delete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(listStorageAddress)));
#endif
        m_state.base.listItems = 0u;
        m_state.base.listCount = 0;

        if (SPRITE* const controlled = controlledSprite())
        {
            VID* const vid = controlled->Vid();
            LOG::ResourceError("SPRITE %i", 10, "ptr_spriteWith", 0, vid ? vid->nVid : -1);
        }
    }

    void PLAYER::resetPlayerSpriteReferences() noexcept
    {
        auto releaseSpriteReference = [](SPRITE* current) noexcept {
            if (!current)
                return;

            const int nextRef = current->listReferenceCount() - 1;
            current->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = current->Vid();
                const int nvid = vid ? vid->nVid : -1;
                LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, nvid);
                return;
            }

            if (nextRef == 0)
                DeleteSpriteThroughVirtualDeletingDestructor(current);
        };

        SPRITE* const controlled = controlledSprite();
        if (controlled)
            releaseSpriteReference(controlled);
        setControlledSprite(nullptr);

        SPRITE* const auxiliary = auxiliarySprite();
        if (auxiliary)
            releaseSpriteReference(auxiliary);
        setAuxiliarySprite(nullptr);

        m_state.base.money = 1000u;
    }

    void PLAYER::setFlagmanSprite(SPRITE* sprite) noexcept
    {
        if (sprite)
            sprite->setListReferenceCount(sprite->listReferenceCount() + 1);

        SPRITE* const previous = controlledSprite();
        if (previous)
        {
            const int nextRef = previous->listReferenceCount() - 1;
            previous->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = previous->Vid();
                const int nvid = vid ? vid->nVid : -1;
                LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, nvid);
            }
            else if (nextRef == 0)
            {
                DeleteSpriteThroughVirtualDeletingDestructor(previous);
            }
        }

        setControlledSprite(sprite);
    }

    int PLAYER::saveControlledSpriteReference(RESOURCE* mapResource) noexcept
    {
        return mapResource->write(reinterpret_cast<const BYTE*>(&m_state.base.controlledSprite), sizeof(m_state.base.controlledSprite));
    }

    int PLAYER::loadControlledSpriteReference(RESOURCE* mapResource) noexcept
    {
        SPRITE* const resolved = MAP::Current()->readSpriteRelationHandle(mapResource);

        if (resolved)
            resolved->setListReferenceCount(resolved->listReferenceCount() + 1);

        SPRITE* const previous = controlledSprite();
        if (previous)
        {
            const int nextRef = previous->listReferenceCount() - 1;
            previous->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = previous->Vid();
                const int nvid = vid ? vid->nVid : -1;
                LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, nvid);
            }
            else if (nextRef == 0)
            {
                DeleteSpriteThroughVirtualDeletingDestructor(previous);
            }
        }

        setControlledSprite(resolved);
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(resolved) & 0xFFFFFFFFu);
    }

    STRING* PLAYER::getAuxiliaryUnitName(STRING* out) noexcept
    {
        if (SPRITE* const auxiliary = auxiliarySprite())
        {
            VID* const vid = auxiliary->Vid();
            char keyBuffer[0x80]{};
#ifdef _WIN32
            _itoa(vid->nvid(), keyBuffer, 10);
#else
            std::snprintf(keyBuffer, sizeof(keyBuffer), "%d", vid->nvid());
#endif
            STRING section("Units");
            STRING key(keyBuffer);
            STRING defaultValue;
            as1::core::profile_p::readProfileStringInto(
                *out, as1::core::StartupStringsIniPath(), section, key, defaultValue);
        }
        else
        {
            *out = STRING();
        }
        return out;
    }

    std::intptr_t PLAYER::removeActivePlayerSpriteReference(SPRITE* sprite) noexcept
    {
        as1::clearPathSpriteReferences(&m_state.path, sprite);
        return removePlayerSpriteReference(sprite);
    }

    std::intptr_t PLAYER::clearSpriteReferenceViaVtable(SPRITE* sprite) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        using RetailDeletePointerFn = std::intptr_t (__thiscall*)(PLAYER*, SPRITE*);
        void** const vtable = *reinterpret_cast<void***>(this);
        return reinterpret_cast<RetailDeletePointerFn>(vtable[1])(this, sprite);
#else
        if (m_state.base.vtable == CurrentImageActiveVtable())
            return removeActivePlayerSpriteReference(sprite);
        return removePlayerSpriteReference(sprite);
#endif
    }

    void PLAYER::noOpSpriteCallback(SPRITE* sprite) noexcept
    {
        (void)sprite;
    }

    void PLAYER::PathOwnerLayout::clearPathSpriteReferences(SPRITE* sprite) noexcept
    {
        as1::clearPathSpriteReferences(this, sprite);
    }

    void PLAYER::clearPlayerPathSpriteReferences(SPRITE* sprite) noexcept
    {

        m_state.path.clearPathSpriteReferences(sprite);
    }

    int PLAYER::removeEmbeddedSpriteReference(SPRITE* sprite) noexcept
    {
        if (!sprite)
            return 1;

        int count = m_state.base.listCount;
        if (count <= 0)
            return 1;

#if UINTPTR_MAX == 0xFFFFFFFFu
        const std::uint32_t listStorageAddress = m_state.base.listItems;
        SPRITE** const items = reinterpret_cast<SPRITE**>(static_cast<std::uintptr_t>(listStorageAddress));
        if (!items)
            return 1;

        for (int index = count - 1; index >= 0; --index)
        {
            if (items[index] != sprite)
                continue;

            --count;
            m_state.base.listCount = count;
            items[index] = items[count];

            (void)sprite->ReleaseListReference();
            return 0;
        }
#endif
        return 1;
    }

    std::intptr_t PLAYER::removePlayerSpriteReference(SPRITE* sprite) noexcept
    {
        (void)removeEmbeddedSpriteReference(sprite);

        auto releaseNoReturn = [](SPRITE* current) noexcept
        {
            if (!current)
                return;
            const int nextRef = current->listReferenceCount() - 1;
            current->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = current->Vid();
                LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                DeleteSpriteThroughVirtualDeletingDestructor(current);
            }
        };

        SPRITE* const controlled = controlledSprite();
        if (controlled == sprite)
        {
            releaseNoReturn(controlled);
            setControlledSprite(nullptr);
        }

        SPRITE* const auxiliary = auxiliarySprite();
        std::intptr_t result = reinterpret_cast<std::intptr_t>(auxiliary);
        if (auxiliary == sprite)
        {
            if (auxiliary)
            {
                const int nextRef = auxiliary->listReferenceCount() - 1;
                auxiliary->setListReferenceCount(nextRef);
                result = static_cast<std::intptr_t>(nextRef);
                if (nextRef < 0)
                {
                    VID* const vid = auxiliary->Vid();
                    const int nvid = vid ? vid->nVid : -1;
                    result = logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4,
                                        "noRef at Release", nextRef, nvid);
                }
                else if (nextRef == 0)
                {
                    DeleteSpriteThroughVirtualDeletingDestructor(auxiliary);
                    result = reinterpret_cast<std::intptr_t>(auxiliary);
                }
            }
            setAuxiliarySprite(nullptr);
        }
        return result;
    }

    void PLAYER::processInputGlobalListPrepass() noexcept
    {
        MAP* const map = MAP::Current();
        GROUPS& groups = map->groupOwner();
        for (Group* group = groups.first(); group;)
        {
            SPRITE* goal = nullptr;
            int command = 0;
            int hasPlainSprite = 0;

            if (group->activeCount() > 0)
            {
                for (int i = 0; i < group->activeCount(); ++i)
                {
                    SPRITE* const sprite = group->at(static_cast<std::size_t>(i));
                    const std::uint32_t rawFlags = sprite->runtimeFlags();
                    const std::uint32_t masked = rawFlags & SPRITE::CommandBitsMask;
                    if (masked == 0u)
                        hasPlainSprite = 1;
                    if (masked == 0x0Cu || masked == 0x10u)
                    {
                        goal = sprite->goalSprite();
                        command = static_cast<int>((rawFlags >> 2) & 0x1Fu);
                    }
                }

                if (goal && command && hasPlainSprite)
                {
                    for (int i = 0; i < group->activeCount(); ++i)
                    {
                        SPRITE* const sprite = group->at(static_cast<std::size_t>(i));
                        if ((sprite->runtimeFlags() & SPRITE::CommandBitsMask) == 0u)
                            sprite->SetCommand(command, goal);
                    }
                }
            }

            Group* const next = group->nextGroup();
            group = next != &groups ? next : nullptr;
        }
    }

    void PLAYER::processInputAttackWeaponPreselect(SPRITE* controlled) noexcept
    {
        VID* const controlledVid = controlled->Vid();
        if (controlledVid->spriteClassId() != 7u)
            return;

        if (controlled->ammoCount() >= controlledVid->fightNoChildValue())
            return;

        SPRITE* const child = controlled->childChain();
        int selected = child->Vid()->nvid() - 0x0B;
        if (controlled->switchLinkedWeaponSlot(selected) != 0)
            return;

        do
        {
            --selected;
        }
        while (controlled->switchLinkedWeaponSlot(selected) == 0);
    }

    void PLAYER::processInputDigitWeaponSelect(SPRITE* controlled, std::uint32_t lastCode) noexcept
    {
        if (lastCode < 0x30u || lastCode > 0x39u)
            return;

        VID* const controlledVid = controlled->Vid();
        if (controlledVid->spriteClassId() != 7u)
            return;

        controlled->switchLinkedWeaponSlot(static_cast<int>(lastCode - 0x30u));
    }

    void PLAYER::processInputWheelWeaponSelect(SPRITE* controlled, as1::input::InputMessageState* inputState) noexcept
    {
        VID* const controlledVid = controlled->Vid();
        int selected = controlledVid->linkedVid()->nvid() - 0x0A;

        // does not mutate the input state here. With zero wheel delta it
        // also accepts the configured Prev/Next keyboard bindings via rawKeyCode.
        int delta = inputState->wheelDelta;
        if (delta == 0)
        {
            const as1::input::InputControlKeys& keys = as1::input::inputControlKeys();
            if (inputState->rawKeyCode == keys.nextWeapon)
                delta = 1;
            else if (inputState->rawKeyCode == keys.previousWeapon)
            {
                if (selected > 0)
                {
                    do
                    {
                        --selected;
                    }
                    while (controlled->switchLinkedWeaponSlot(selected) == 0 && selected > 0);
                }
                return;
            }
            else
                return;
        }

        if (delta > 0)
        {
            do
            {
                if (selected < 10)
                {
                    do
                    {
                        ++selected;
                    }
                    while (controlled->switchLinkedWeaponSlot(selected) == 0 && selected < 10);
                }
                --delta;
            }
            while (delta > 0);
            return;
        }

        if (selected > 0)
        {
            do
            {
                --selected;
            }
            while (controlled->switchLinkedWeaponSlot(selected) == 0 && selected > 0);
        }
    }

    void PLAYER::processInputDirectionCommandDispatch(SPRITE* controlled, const as1::input::InputMessageState* inputState, int& outDirection, std::uint32_t& outDeltaMs) noexcept
    {
        outDirection = 0;
        outDeltaMs = as1::core::CurrentTimeMilliseconds() - as1::core::PreviousWorldTimeMilliseconds();

        const float dx = inputState->worldX - controlled->X();
        const float dy = inputState->worldY + controlled->Z() - controlled->Y();
        outDirection = RetailDirectionFromFloatXY(dx, dy).Int();

        const std::uint32_t flags = inputState->flags;
        int commandBase = as1::input::relativeControlEnabled() ? outDirection : 0;
        const std::uint32_t deltaMs = outDeltaMs;

        const bool flag80 = (flags & 0x00000080u) != 0u;
        const bool flag100 = (flags & 0x00000100u) != 0u;
        const bool flag200 = (flags & 0x00000200u) != 0u;
        const bool flag400 = (flags & 0x00000400u) != 0u;

        if (flag400 && flag100)
            controlled->RotateTact(commandBase + 0x28, deltaMs);
        else if (flag400 && flag80)
            controlled->RotateTact(commandBase + 0xD8, deltaMs);
        else if (flag200 && flag100)
            controlled->RotateTact(commandBase + 0x58, deltaMs);
        else if (flag200 && flag80)
            controlled->RotateTact(commandBase + 0xA8, deltaMs);
        else if (flag80)
            controlled->RotateTact(controlled->GlideDirection(commandBase + 0xC0), deltaMs);
        else if (flag100)
            controlled->RotateTact(controlled->GlideDirection(commandBase + 0x40), deltaMs);
        else if (flag200)
            controlled->RotateTact(controlled->GlideDirection(commandBase + 0x80), deltaMs);
        else if (flag400)
            controlled->RotateTact(controlled->GlideDirection(commandBase), deltaMs);
        else if ((controlled->runtimeFlags() & SPRITE::CommandBitsMask) != 4u)
            controlled->Stop();
    }

    void PLAYER::processInputPostMovementHelper(SPRITE* controlled, std::uint32_t flags) noexcept
    {
        if ((flags & 0x00000700u) != 0u || (flags & 0x00000080u) != 0u)
            controlled->StartMove();
    }

    void PLAYER::dispatchInputViaRetailVtable(as1::input::InputMessageState* inputState) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        using RetailInputFn = void (__thiscall*)(PLAYER*, as1::input::InputMessageState*);
        void** const vtable = *reinterpret_cast<void***>(this);
        reinterpret_cast<RetailInputFn>(vtable[6])(this, inputState);
#else
        processInput(inputState);
#endif
    }

    void PLAYER::dispatchReservedScriptToggleViaRetailVtable(int value) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        using RetailReservedFn = void (__thiscall*)(PLAYER*);
        void** const vtable = *reinterpret_cast<void***>(this);
        // Action 122 selects one PLAYER callback for non-zero and another for zero.
        reinterpret_cast<RetailReservedFn>(vtable[value != 0 ? 7 : 8])(this);
#else
        (void)value;
#endif
    }

    void PLAYER::processInput(as1::input::InputMessageState* inputState) noexcept
    {
        processInputGlobalListPrepass();

        updatePathOwnerFrame();

        SPRITE* controlled = controlledSprite();
        if (!controlled)
            return;
        VID* controlledVid = controlled->Vid();
        if (controlledVid->spriteClassId() != 7u)
            return;

        const std::uint32_t flags = inputState->flags;

        if ((flags & 0x00008000u) != 0u)
        {
            // the projected Y coordinate before ACT_MOVE and converts both
            // coordinates with truncating float-to-int conversion.
            const float groundZ = MAP::Current()->sampleTerrainHeight(inputState->worldX, inputState->worldY);
            controlled->dispatchVirtualAction(
                static_cast<std::uint32_t>(ActionCode::ACT_MOVE),
                playerTruncateFloatToInt32(inputState->worldX),
                playerTruncateFloatToInt32(inputState->worldY + groundZ),
                0);
        }

        if (((flags & 0x00000700u) != 0u || (flags & 0x00000080u) != 0u) &&
            ((controlled->runtimeFlags() & SPRITE::CommandBitsMask) == 4u))
        {
            controlled->SetCommand(0, nullptr);
        }

        if ((flags & 0x00004000u) != 0u)
        {
            processInputAttackWeaponPreselect(controlled);
            controlled->dispatchVirtualAction(
                static_cast<std::uint32_t>(ActionCode::ACT_COOR_ATTACK),
                playerTruncateFloatToInt32(inputState->worldX),
                playerTruncateFloatToInt32(inputState->worldY),
                0);
        }

        processInputDigitWeaponSelect(controlled, inputState->lastCode);

        processInputWheelWeaponSelect(controlled, inputState);

        int angle = 0;
        std::uint32_t deltaMs = 0;
        processInputDirectionCommandDispatch(controlled, inputState, angle, deltaMs);

        processInputPostMovementHelper(controlled, flags);

        processInputDispatchChildTail(controlled, angle, deltaMs);
        releaseAuxiliarySpriteAfterInput();
    }

    void PLAYER::processInputDispatchChildTail(SPRITE* controlled, int direction, std::uint32_t deltaMs) noexcept
    {
        if (!controlled)
            return;

        SPRITE* child = controlled->childChain();
        if (!child)
            return;

        VID* controlledVid = controlled->Vid();
        VID* childVid = child->Vid();
        if (childVid != controlledVid->linkedVid())
            return;

        // animations. Child rotation is skipped for this stationary case.
        if (controlled->currentAnimation() >= 15)
            return;

        const bool speedIsOrderedZero = playerOrderedFloatEqual(controlled->Speed(), 0.0f);
        const bool movementOrSpecialFlag = !speedIsOrderedZero || (controlled->runtimeFlags() & 0x80u) != 0u;

        const unsigned char childByte = static_cast<unsigned char>(child->directionIndex() & 0xFF);
        const unsigned char ownerByte = static_cast<unsigned char>(controlled->directionIndex() & 0xFF);
        const unsigned char deltaA = static_cast<unsigned char>(ownerByte - childByte);
        const unsigned char deltaB = static_cast<unsigned char>(childByte - ownerByte);
        const unsigned char minDelta = std::min(deltaA, deltaB);

        // far enough away from the owner direction, but only when animation 6
        // exists in VID::noAnimCadr[].  Leaving that state restores animation 0
        // and flips the owner direction by 0x80 through ChangeDirection.
        const bool shouldUseTurnAnimation =
            movementOrSpecialFlag && controlledVid->noAnimCadr[6] != 0 && minDelta > 0x40u;
        if (shouldUseTurnAnimation)
        {
            if (controlled->currentAnimation() != 6)
                controlled->ChangeAnimation(6);
        }
        else if (controlled->currentAnimation() == 6)
        {
            controlled->ChangeAnimation(0);
            controlled->ChangeDirection(ANGLE(static_cast<unsigned char>(ownerByte - 0x80u)));
        }

        if (speedIsOrderedZero && controlled->currentAnimation() == 2)
            controlled->ChangeAnimation(0);

        if (speedIsOrderedZero && controlledVid->nVid == 9)
        {
            if (as1::core::ChildRotationCorrectionPending() != 0u)
            {
                if (controlled->RotateTact(ANGLE(childByte), deltaMs).Int() == 0)
                    as1::core::SetChildRotationCorrectionPending(0u);
            }
            else
            {
                as1::core::SetChildRotationCorrectionPending(minDelta > 0x37u ? 1u : 0u);
            }
        }

        // in command state 0x10.
        if ((child->runtimeFlags() & SPRITE::CommandBitsMask) != 0x10u)
            child->RotateTact(direction, deltaMs);
    }

    void PLAYER::releaseAuxiliarySpriteAfterInput() noexcept
    {
        SPRITE* auxiliary = auxiliarySprite();
        if (!auxiliary)
            return;

        VID* auxiliaryVid = auxiliary->Vid();
        if ((auxiliaryVid->spriteTypeId() & 0x2u) == 0u)
            return;

        const int nextRef = auxiliary->listReferenceCount() - 1;
        auxiliary->setListReferenceCount(nextRef);
        if (nextRef < 0)
        {
            const int nvid = auxiliaryVid ? auxiliaryVid->nVid : -1;
            LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, nvid);
            setAuxiliarySprite(nullptr);
            return;
        }

        if (nextRef == 0)
            DeleteSpriteThroughVirtualDeletingDestructor(auxiliary);

        setAuxiliarySprite(nullptr);
    }

    int PLAYER::controlMode() const noexcept
    {
        return m_state.base.controlMode;
    }

    int PLAYER::playerSlot() const noexcept
    {
        return m_state.base.playerSlot;
    }

    SPRITE* PLAYER::controlledSprite() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return decodeRetailSpritePointer(m_state.base.controlledSprite);
#else
        return nullptr;
#endif
    }

    void PLAYER::setControlledSprite(SPRITE* value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        m_state.base.controlledSprite = encodeRetailSpritePointer(value);
#else
        (void)value;
        m_state.base.controlledSprite = 0u;
#endif
    }

    SPRITE* PLAYER::auxiliarySprite() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return decodeRetailSpritePointer(m_state.base.auxiliarySprite);
#else
        return nullptr;
#endif
    }

    void PLAYER::setAuxiliarySprite(SPRITE* value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        m_state.base.auxiliarySprite = encodeRetailSpritePointer(value);
#else
        (void)value;
        m_state.base.auxiliarySprite = 0u;
#endif
    }

}
