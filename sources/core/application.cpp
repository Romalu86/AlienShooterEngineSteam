#include "core/application.h"
#include "core/as_string.h"
#include "core/file_logger.h"
#include "core/log.h"

#include "graph.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "sprite_collector_hash.h"
#include "builded_terrain.h"
#include "vid/vid_hardware.h"
#include "unit.h"
#include "cannon.h"
#include "building.h"
#include "rail.h"
#include "depo.h"
#include "avia.h"
#include "vid/vid.h"
#include "script/lgc_script.h"
#include "constant.h"
#include "sound/engine.h"

#include <cmath>
#include <cstring>
#include <new>
#include <limits>

namespace as1 { namespace core
{
    std::uint32_t g_currentTimeMilliseconds = 0;
    std::uint32_t g_previousWorldTimeMilliseconds = 0;
    void* g_applicationPhysicalOwner = nullptr;
    std::uint32_t g_displayedFramesPerSecond = 0;

    namespace
    {
        float g_applicationTickScale = 1.0f;
        std::uint32_t g_applicationWorldFrameCounter = 0;
        std::uint32_t g_applicationWorldStartTime = 0;
        float g_applicationMapWidth = 640.0f;
        float g_applicationMapHeight = 480.0f;
        STRING g_applicationSavePath("saves");
        STRING g_applicationCurrentMapName;
        STRING g_applicationPreviousMapName;
        std::uint32_t g_applicationScrollType = 1;
        std::uint32_t g_demoStartTimestampMilliseconds = 0;
        float g_applicationScrollVelocityX = 0.0f;
        float g_applicationScrollVelocityY = 0.0f;
        std::uint32_t g_lastFpsSampleTimeMilliseconds = 0;
        std::uint32_t g_accumulatedFpsFrameCount = 0;
        std::uint32_t g_bucketTimingSnapshotMilliseconds = 0;
        std::uint32_t g_demoRealTimeBaseMilliseconds = 0;
        std::uint32_t g_demoRecordedTimeBaseMilliseconds = 0;
        std::array<int, kScriptCallbackSlotCount> g_scriptCallbackSlots{};
        std::uint32_t g_childRotationCorrectionPending = 0;
        std::uint32_t g_bulkSpriteDeleteActive = 0;
        std::uint32_t g_realTimeMilliseconds = 0;
        std::uint32_t g_previousRealTimeMilliseconds = 0;
        std::uint32_t g_applicationFlags = 0;
        std::uint32_t g_activePlayerIndexFallback = 0;
#ifndef _WIN32
        short* g_applicationTerrainGrid = nullptr;
        short* g_applicationTempTerrainGrid = nullptr;
        int g_applicationTerrainGridWidth = 0;
        int g_applicationTerrainGridHeight = 0;
        int g_applicationWeaponCount = 0;
        WEAPON* g_applicationWeaponTable = nullptr;
        int g_applicationVidCount = 0;
        std::array<VID*, ApplicationVidTable::kCapacity> g_applicationVidSlots{};
#endif
#ifndef _WIN32
        SCRIPT g_applicationScriptOwner;
#endif

        int subtractFloatToIntTruncated(float lhs, float rhs) noexcept
        {

            const long double value =
                static_cast<long double>(lhs) - static_cast<long double>(rhs);
            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(converted)));
        }

        int subtractRoundedFloatToInt(float lhs, float rhs) noexcept
        {
            const float rounded = static_cast<float>(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            return subtractFloatToIntTruncated(rounded, 0.0f);
        }

        bool debugPassReady(const ApplicationDebugPassContext& context)
        {
            (void)context;

            const BASE_CONSTANTS* const constants = GlobalBaseConstants();
            return constants && constants->raw[10] != 0;
        }

        class RetailPrivateClass7 final : public UNIT
        {
        public:
            RetailPrivateClass7(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
                : UNIT(owner, vid, xyz, ANGLE(direction.Int() & 0xFF), parent)
            {

                if (findLastCommandWord(0x0105) < 0)
                    appendCommandWordValue(0x0105);
                m_reserved9CB8.fill(0u);
            }

            int Action(int opcode, std::intptr_t argument1Raw, int argument2, int argument3) override
            {
                return dispatchPrivateClass7ActionOpcode(
                    opcode,
                    static_cast<int>(argument1Raw),
                    argument2,
                    argument3);
            }

            RetailPrivateClass7* privateClass7ScalarDeletingDestructor(unsigned char flags) noexcept
            {
                RetailPrivateClass7* const self = this;
                destroyPrivateClass7State();
                if ((flags & 1u) != 0u)
                    ::operator delete(static_cast<void*>(self));
                return self;
            }

            void destroyPrivateClass7State() noexcept
            {

                destroyCommandSpriteState();
            }

            void MoveTact() override
            {

                VECTOR candidate{X(), Y(), Z()};
                computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

                const float applicationSizeX = ApplicationMapWidth();
                const float applicationSizeY = ApplicationMapHeight();

                const bool changedXY = candidate.x != X() || candidate.y != Y();
                if (changedXY &&
                    candidate.x >= 0.0f && candidate.x < applicationSizeX &&
                    candidate.y >= 0.0f && candidate.y < applicationSizeY &&
                    CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
                {
                    ChangeCoor(candidate.x, candidate.y, candidate.z);
                }

                SPRITE* const target = goalSprite();
                if (target && (runtimeFlags() & SPRITE::CommandBitsMask) == 4u)
                {
                    const int reverse = Speed() >= 0.0f ? 0 : 128;

                    const int dx = subtractRoundedFloatToInt(target->X(), X());
                    const int dy = subtractFloatToIntTruncated(target->Y(), Y());

                    const int desired = AngleFromXY(dx, dy, nullptr).Int() + reverse;
                    const std::uint32_t delta = CurrentTimeMilliseconds() - PreviousWorldTimeMilliseconds();
                    const int turn = GlideDirection(ANGLE(static_cast<unsigned char>(desired))).Int();
                    RotateTact(turn, delta);

                    VID* const ownVid = Vid();
                    VID* const targetVid = target->Vid();
                    const bool stoppedByFlags =
                        (runtimeFlags() & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask;
                    const bool overlap =
                        ownVid->halfSizeX() + targetVid->halfSizeX() > std::fabs(X() - target->X()) &&
                        ownVid->halfSizeY() + targetVid->halfSizeY() > std::fabs(Y() - target->Y());
                    if (stoppedByFlags || overlap)
                        Stop();
                }
            }

        private:

            std::uint32_t m_reserved94;
            std::uint32_t m_reserved98;
            std::array<std::uint32_t, 8> m_reserved9CB8;
        };
#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                                                            
#endif
    }


    namespace
    {
#ifdef _WIN32
        template <class T>
        T& applicationPhysicalSlot(void* owner, std::size_t offset) noexcept
        {
                                                                                                              
            return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(owner) + offset);
        }

        template <class T>
        const T& applicationPhysicalSlot(const void* owner, std::size_t offset) noexcept
        {
                                                                                                              
            return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(owner) + offset);
        }
#endif
    }

    void BindApplicationPhysicalOwner(void* owner) noexcept
    {

        g_applicationPhysicalOwner = owner;
    }

    void InitializeApplicationPhysicalMapStorage(void* owner) noexcept
    {
#ifdef _WIN32
        if (!owner)
            return;

        applicationPhysicalSlot<short*>(owner, retail_application_layout::TerrainGrid) = nullptr;
        applicationPhysicalSlot<short*>(owner, retail_application_layout::TempTerrainGrid) = nullptr;
        applicationPhysicalSlot<int>(owner, retail_application_layout::TerrainGridWidth) = 0;
        applicationPhysicalSlot<int>(owner, retail_application_layout::TerrainGridHeight) = 0;
        applicationPhysicalSlot<int>(owner, retail_application_layout::WeaponCount) = 0;
        applicationPhysicalSlot<WEAPON*>(owner, retail_application_layout::WeaponTable) = nullptr;
        applicationPhysicalSlot<int>(owner, retail_application_layout::VidCount) = 0;
#else
        (void)owner;
        g_applicationTerrainGrid = nullptr;
        g_applicationTempTerrainGrid = nullptr;
        g_applicationTerrainGridWidth = 0;
        g_applicationTerrainGridHeight = 0;
        g_applicationWeaponCount = 0;
        g_applicationWeaponTable = nullptr;
        g_applicationVidCount = 0;
        g_applicationVidSlots.fill(nullptr);
#endif
    }

    void InitializeApplicationPhysicalDrawStorage(void* owner) noexcept
    {
#ifdef _WIN32
        if (!owner)
            return;

        for (int pass = 0; pass < ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            void* const slot = static_cast<std::uint8_t*>(owner) + retail_application_layout::DrawLayerOwners +
                               static_cast<std::size_t>(pass) * retail_application_layout::DrawLayerStride;
            new (slot) ApplicationDrawPassBucket();
        }
#else
        (void)owner;
#endif
    }

    void DestroyApplicationPhysicalDrawStorage(void* owner) noexcept
    {
#ifdef _WIN32
        if (!owner)
            return;
        for (int pass = ApplicationDrawDispatcherState::PassCount - 1; pass >= 0; --pass)
        {
            auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
                static_cast<std::uint8_t*>(owner) + retail_application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * retail_application_layout::DrawLayerStride);
            bucket->~ApplicationDrawPassBucket();
        }
#else
        (void)owner;
#endif
    }

    short* ApplicationTerrainGrid() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<short*>(owner, retail_application_layout::TerrainGrid) : nullptr;
#else
        return g_applicationTerrainGrid;
#endif
    }

    void SetApplicationTerrainGrid(short* value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<short*>(owner, retail_application_layout::TerrainGrid) = value;
#else
        g_applicationTerrainGrid = value;
#endif
    }

    short* ApplicationTempTerrainGrid() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<short*>(owner, retail_application_layout::TempTerrainGrid) : nullptr;
#else
        return g_applicationTempTerrainGrid;
#endif
    }

    void SetApplicationTempTerrainGrid(short* value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<short*>(owner, retail_application_layout::TempTerrainGrid) = value;
#else
        g_applicationTempTerrainGrid = value;
#endif
    }

    int ApplicationTerrainGridWidth() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<int>(owner, retail_application_layout::TerrainGridWidth) : 0;
#else
        return g_applicationTerrainGridWidth;
#endif
    }

    void SetApplicationTerrainGridWidth(int value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<int>(owner, retail_application_layout::TerrainGridWidth) = value;
#else
        g_applicationTerrainGridWidth = value;
#endif
    }

    int ApplicationTerrainGridHeight() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<int>(owner, retail_application_layout::TerrainGridHeight) : 0;
#else
        return g_applicationTerrainGridHeight;
#endif
    }

    void SetApplicationTerrainGridHeight(int value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<int>(owner, retail_application_layout::TerrainGridHeight) = value;
#else
        g_applicationTerrainGridHeight = value;
#endif
    }

    int ApplicationWeaponCount() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<int>(owner, retail_application_layout::WeaponCount) : 0;
#else
        return g_applicationWeaponCount;
#endif
    }

    void SetApplicationWeaponCount(int value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<int>(owner, retail_application_layout::WeaponCount) = value;
#else
        g_applicationWeaponCount = value;
#endif
    }

    WEAPON* ApplicationWeaponTable() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<WEAPON*>(owner, retail_application_layout::WeaponTable) : nullptr;
#else
        return g_applicationWeaponTable;
#endif
    }

    void SetApplicationWeaponTable(WEAPON* value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<WEAPON*>(owner, retail_application_layout::WeaponTable) = value;
#else
        g_applicationWeaponTable = value;
#endif
    }

    ApplicationVidTable& GlobalApplicationVidTable() noexcept
    {
        static ApplicationVidTable table;
        return table;
    }

    void ApplicationVidTable::clear() noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
        {
            applicationPhysicalSlot<int>(owner, kCountOffset) = 0;
            std::memset(static_cast<std::uint8_t*>(owner) + kFirstSlotOffset, 0,
                        kEndSlotOffset - kFirstSlotOffset);
        }
#else
        g_applicationVidCount = 0;
        g_applicationVidSlots.fill(nullptr);
#endif
    }

    int ApplicationVidTable::count() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<int>(owner, kCountOffset) : 0;
#else
        return g_applicationVidCount;
#endif
    }

    void ApplicationVidTable::setStoredCount(int value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<int>(owner, kCountOffset) = value;
#else
        g_applicationVidCount = value;
#endif
    }

    bool ApplicationVidTable::setSlot(int nvid, VID* vid) noexcept
    {
        if (nvid < 0 || static_cast<std::size_t>(nvid) >= kCapacity)
            return false;

        setSlotCell(nvid, vid);
        int storedCount = count();
        if (vid && nvid >= storedCount)
            setStoredCount(nvid + 1);
        else if (!vid && nvid == storedCount - 1)
        {
            while (storedCount > 0 && !slot(storedCount - 1))
                --storedCount;
            setStoredCount(storedCount);
        }
        return true;
    }

    VID* ApplicationVidTable::slot(int index) const noexcept
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kCapacity)
            return nullptr;
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<VID*>(owner, kFirstSlotOffset + static_cast<std::size_t>(index) * 4u) : nullptr;
#else
        return g_applicationVidSlots[static_cast<std::size_t>(index)];
#endif
    }

    void ApplicationVidTable::setSlotCell(int index, VID* vid) noexcept
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kCapacity)
            return;
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<VID*>(owner, kFirstSlotOffset + static_cast<std::size_t>(index) * 4u) = vid;
#else
        g_applicationVidSlots[static_cast<std::size_t>(index)] = vid;
#endif
    }

    VID* const* ApplicationVidTable::slotData() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? reinterpret_cast<VID* const*>(static_cast<std::uint8_t*>(owner) + kFirstSlotOffset) : nullptr;
#else
        return g_applicationVidSlots.data();
#endif
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    std::size_t ApplicationVidTable::loadedSlotCount() const noexcept
    {
        std::size_t loadedCount = 0;
        const int slotCount = count();
        for (int i = 0; i < slotCount && static_cast<std::size_t>(i) < kCapacity; ++i)
            if (slot(i))
                ++loadedCount;
        return loadedCount;
    }

    std::size_t ApplicationVidTable::loadedSlotCountWithinGammaScan() const noexcept
    {
        std::size_t loaded = 0;
        const int slotCount = count();
        for (std::size_t index = 0; index < kCapacity; ++index)
            if (static_cast<int>(index) < slotCount && slot(static_cast<int>(index)))
                ++loaded;
        return loaded;
    }

    std::vector<VID*> ApplicationVidTable::loadedSlotsSnapshot() const
    {
        std::vector<VID*> out;
        out.reserve(loadedSlotCount());
        const int slotCount = count();
        for (int i = 0; i < slotCount && static_cast<std::size_t>(i) < kCapacity; ++i)
            if (VID* const vid = slot(i))
                out.push_back(vid);
        return out;
    }


#endif

    SPRITE* ApplicationDrawPassBucket::spriteAt(int index) const noexcept
    {
        return (index >= 0 && index < list.activeCount())
            ? list.at(static_cast<std::size_t>(index))
            : nullptr;
    }

    int ApplicationDrawPassBucket::findAndNull(SPRITE* sprite) noexcept
    {
        return list.findAndNull(sprite);
    }

    void ApplicationDrawPassBucket::append(SPRITE* sprite)
    {
        list.append(sprite);
    }

    std::uint32_t ApplicationDrawDispatcherState::flags() const noexcept
    {
#ifdef _WIN32
        return ApplicationFlags();
#else
        return fallbackFlags;
#endif
    }

    void ApplicationDrawDispatcherState::setFlags(std::uint32_t value) noexcept
    {
#ifdef _WIN32
        SetApplicationFlags(value);
#else
        fallbackFlags = value;
#endif
    }

    float ApplicationDrawDispatcherState::scrollMinXLimit() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, ScrollMinXOffset) : 0.0f;
#else
        return scrollMinX;
#endif
    }

    float ApplicationDrawDispatcherState::scrollMaxXLimit() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, ScrollMaxXOffset) : 0.0f;
#else
        return scrollMaxX;
#endif
    }

    float ApplicationDrawDispatcherState::scrollMinYLimit() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, ScrollMinYOffset) : 0.0f;
#else
        return scrollMinY;
#endif
    }

    float ApplicationDrawDispatcherState::scrollMaxYLimit() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, ScrollMaxYOffset) : 0.0f;
#else
        return scrollMaxY;
#endif
    }

    void ApplicationDrawDispatcherState::setScrollMinXLimit(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<float>(owner, ScrollMinXOffset) = value;
#else
        scrollMinX = value;
#endif
    }

    void ApplicationDrawDispatcherState::setScrollMaxXLimit(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<float>(owner, ScrollMaxXOffset) = value;
#else
        scrollMaxX = value;
#endif
    }

    void ApplicationDrawDispatcherState::setScrollMinYLimit(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<float>(owner, ScrollMinYOffset) = value;
#else
        scrollMinY = value;
#endif
    }

    void ApplicationDrawDispatcherState::setScrollMaxYLimit(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<float>(owner, ScrollMaxYOffset) = value;
#else
        scrollMaxY = value;
#endif
    }

    float ApplicationDrawDispatcherState::cameraShiftX() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, retail_application_layout::CameraShiftX) : 0.0f;
#else
        return cameraX;
#endif
    }

    float ApplicationDrawDispatcherState::cameraShiftY() const noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, retail_application_layout::CameraShiftY) : 0.0f;
#else
        return cameraY;
#endif
    }

    double ApplicationDrawDispatcherState::cameraRelativeX(float value) const noexcept
    {
        return static_cast<double>(value) - static_cast<double>(cameraShiftX());
    }

    double ApplicationDrawDispatcherState::cameraRelativeY(float value) const noexcept
    {
        return static_cast<double>(value) - static_cast<double>(cameraShiftY());
    }

    void ApplicationDrawDispatcherState::setCameraShiftX(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<float>(owner, retail_application_layout::CameraShiftX) = value;
#else
        cameraX = value;
#endif
    }

    void ApplicationDrawDispatcherState::setCameraShiftY(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            applicationPhysicalSlot<float>(owner, retail_application_layout::CameraShiftY) = value;
#else
        cameraY = value;
#endif
    }

    ApplicationDrawPassBucket& ApplicationDrawDispatcherState::drawPassBucket(int pass) noexcept
    {
        static ApplicationDrawPassBucket dummy;
        if (pass < 0 || pass >= PassCount)
            return dummy;
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        if (!owner)
            return dummy;
        return *reinterpret_cast<ApplicationDrawPassBucket*>(
            static_cast<std::uint8_t*>(owner) + retail_application_layout::DrawLayerOwners +
            static_cast<std::size_t>(pass) * retail_application_layout::DrawLayerStride);
#else
        return passBuckets[static_cast<std::size_t>(pass)];
#endif
    }

    const ApplicationDrawPassBucket& ApplicationDrawDispatcherState::drawPassBucket(int pass) const noexcept
    {
        static const ApplicationDrawPassBucket dummy;
        if (pass < 0 || pass >= PassCount)
            return dummy;
#ifdef _WIN32
        const void* const owner = ApplicationPhysicalOwner();
        if (!owner)
            return dummy;
        return *reinterpret_cast<const ApplicationDrawPassBucket*>(
            static_cast<const std::uint8_t*>(owner) + retail_application_layout::DrawLayerOwners +
            static_cast<std::size_t>(pass) * retail_application_layout::DrawLayerStride);
#else
        return passBuckets[static_cast<std::size_t>(pass)];
#endif
    }

    void ApplicationDrawDispatcherState::clear() noexcept
    {
        setFlags(0);
        setCameraShiftX(0.0f);
        setCameraShiftY(0.0f);
        for (int pass = 0; pass < PassCount; ++pass)
            drawPassBucket(pass).clear();
    }

    ApplicationDrawDispatcherState& GlobalApplicationDrawDispatcherState() noexcept
    {
        static ApplicationDrawDispatcherState state;
        return state;
    }

    SPRITE* ApplicationFrameRuntimeState::currentFrameSprite() const noexcept
    {
#ifdef _WIN32
        return applicationMenu().selectedSprite();
#else
        return currentFrameSpriteFallback;
#endif
    }

    void ApplicationFrameRuntimeState::setCurrentFrameSprite(SPRITE* sprite) noexcept
    {
#ifdef _WIN32
        applicationMenu().setSelectedSprite(sprite);
#else
        currentFrameSpriteFallback = sprite;
#endif
    }

    bool ApplicationFrameRuntimeState::clearCurrentFrameSpriteIfMatches(SPRITE* sprite) noexcept
    {
#ifdef _WIN32
        return applicationMenu().clearSelectedSpriteIfMatches(sprite);
#else
        if (currentFrameSpriteFallback != sprite) return false;
        currentFrameSpriteFallback = nullptr;
        return true;
#endif
    }

    ApplicationFrameRuntimeState& GlobalApplicationFrameRuntimeState() noexcept
    {
        static ApplicationFrameRuntimeState state;
        return state;
    }

    float ApplicationTickScale() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? applicationPhysicalSlot<float>(owner, retail_application_layout::TickScale) : 1.0f;
#else
        return g_applicationTickScale;
#endif
    }
    void SetApplicationTickScale(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner()) applicationPhysicalSlot<float>(owner, retail_application_layout::TickScale) = value;
#else
        g_applicationTickScale = value;
#endif
    }
    std::uint32_t ApplicationWorldFrameCounter() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::WorldFrameCounter) : 0u;
#else
        return g_applicationWorldFrameCounter;
#endif
    }
    void SetApplicationWorldFrameCounter(std::uint32_t value) noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner()) applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::WorldFrameCounter)=value;
#else
        g_applicationWorldFrameCounter=value;
#endif
    }
    std::uint32_t ApplicationWorldStartTime() noexcept
    {
#ifdef _WIN32
        void* const owner=ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::WorldStartTime):0u;
#else
        return g_applicationWorldStartTime;
#endif
    }
    void SetApplicationWorldStartTime(std::uint32_t value) noexcept
    {
#ifdef _WIN32
        if (void* const owner=ApplicationPhysicalOwner()) applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::WorldStartTime)=value;
#else
        g_applicationWorldStartTime=value;
#endif
    }
    float ApplicationMapWidth() noexcept
    {
#ifdef _WIN32
        void* const owner=ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<float>(owner, retail_application_layout::MapExtentX):640.0f;
#else
        return g_applicationMapWidth;
#endif
    }
    void SetApplicationMapWidth(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner=ApplicationPhysicalOwner()) applicationPhysicalSlot<float>(owner, retail_application_layout::MapExtentX)=value;
#else
        g_applicationMapWidth=value;
#endif
    }
    float ApplicationMapHeight() noexcept
    {
#ifdef _WIN32
        void* const owner=ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<float>(owner, retail_application_layout::MapExtentY):480.0f;
#else
        return g_applicationMapHeight;
#endif
    }
    void SetApplicationMapHeight(float value) noexcept
    {
#ifdef _WIN32
        if (void* const owner=ApplicationPhysicalOwner()) applicationPhysicalSlot<float>(owner, retail_application_layout::MapExtentY)=value;
#else
        g_applicationMapHeight=value;
#endif
    }

    const STRING& ApplicationSavePath() noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            return applicationPhysicalSlot<STRING>(owner, retail_application_layout::SavePath);
#endif
        return g_applicationSavePath;
    }

    const STRING& ApplicationCurrentMapName() noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            return applicationPhysicalSlot<STRING>(owner, retail_application_layout::CurrentMapName);
#endif
        return g_applicationCurrentMapName;
    }

    const STRING& ApplicationPreviousMapName() noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            return applicationPhysicalSlot<STRING>(owner, retail_application_layout::PreviousMapName);
#endif
        return g_applicationPreviousMapName;
    }

    void SetApplicationCurrentMapName(const STRING& value)
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
        {
            STRING& current = applicationPhysicalSlot<STRING>(owner, retail_application_layout::CurrentMapName);
            STRING& previous = applicationPhysicalSlot<STRING>(owner, retail_application_layout::PreviousMapName);
            previous = current;
            current = value;
            return;
        }
#endif
        g_applicationPreviousMapName = g_applicationCurrentMapName;
        g_applicationCurrentMapName = value;
    }
    std::uint32_t ApplicationScrollType() noexcept
    {
#ifdef _WIN32
        void* const owner=ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::ScrollType):1u;
#else
        return g_applicationScrollType;
#endif
    }
    void SetApplicationScrollType(std::uint32_t value) noexcept
    {
#ifdef _WIN32
        if (void* const owner=ApplicationPhysicalOwner()) applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::ScrollType)=value;
#else
        g_applicationScrollType=value;
#endif
    }
    std::uint32_t DemoStartTimestampMilliseconds() noexcept { return g_demoStartTimestampMilliseconds; }
    void SetDemoStartTimestampMilliseconds(std::uint32_t value) noexcept { g_demoStartTimestampMilliseconds = value; }

    float& ApplicationScrollVelocityX() noexcept
    {
        return g_applicationScrollVelocityX;
    }

    float& ApplicationScrollVelocityY() noexcept
    {
        return g_applicationScrollVelocityY;
    }

    std::uint32_t& LastFpsSampleTimeMilliseconds() noexcept
    {
        return g_lastFpsSampleTimeMilliseconds;
    }

    std::uint32_t& AccumulatedFpsFrameCount() noexcept
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            return applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::FpsCounter);
#endif
        return g_accumulatedFpsFrameCount;
    }

    std::uint32_t BucketTimingSnapshotMilliseconds() noexcept
    {
        return g_bucketTimingSnapshotMilliseconds;
    }

    void SetBucketTimingSnapshotMilliseconds(std::uint32_t value) noexcept
    {
        g_bucketTimingSnapshotMilliseconds = value;
    }

    std::uint32_t DemoRealTimeBaseMilliseconds() noexcept { return g_demoRealTimeBaseMilliseconds; }
    void SetDemoRealTimeBaseMilliseconds(std::uint32_t value) noexcept { g_demoRealTimeBaseMilliseconds = value; }
    std::uint32_t DemoRecordedTimeBaseMilliseconds() noexcept { return g_demoRecordedTimeBaseMilliseconds; }
    void SetDemoRecordedTimeBaseMilliseconds(std::uint32_t value) noexcept { g_demoRecordedTimeBaseMilliseconds = value; }

    int scriptCallbackSlot(std::size_t index) noexcept
    {
        return index < g_scriptCallbackSlots.size()
            ? g_scriptCallbackSlots[index]
            : 0;
    }

    void setScriptCallbackSlot(std::size_t index, int value) noexcept
    {
        if (index < g_scriptCallbackSlots.size())
            g_scriptCallbackSlots[index] = value;
    }

    void resetScriptCallbackSlots() noexcept
    {

        for (std::size_t i = 0; i < g_scriptCallbackSlots.size(); ++i)
            g_scriptCallbackSlots[i] = 1000000 + static_cast<int>(i);
    }

    std::uint32_t ChildRotationCorrectionPending() noexcept
    {
        return g_childRotationCorrectionPending;
    }

    void SetChildRotationCorrectionPending(std::uint32_t value) noexcept
    {
        g_childRotationCorrectionPending = value;
    }

    std::uint32_t BulkSpriteDeleteActive() noexcept
    {
        return g_bulkSpriteDeleteActive;
    }

    void SetBulkSpriteDeleteActive(std::uint32_t value) noexcept
    {
        g_bulkSpriteDeleteActive = value;
    }

    std::uint32_t RealTimeMilliseconds() noexcept
    {
        return g_realTimeMilliseconds;
    }

    void SetRealTimeMilliseconds(std::uint32_t value) noexcept
    {
        g_realTimeMilliseconds = value;
    }

    std::uint32_t PreviousRealTimeMilliseconds() noexcept
    {
        return g_previousRealTimeMilliseconds;
    }

    void SetPreviousRealTimeMilliseconds(std::uint32_t value) noexcept
    {
        g_previousRealTimeMilliseconds = value;
    }

    std::uint32_t ApplicationFlags() noexcept
    {
#ifdef _WIN32
        void* const owner=ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::Flags):0u;
#else
        return g_applicationFlags;
#endif
    }

    void SetApplicationFlags(std::uint32_t value) noexcept
    {
#ifdef _WIN32
        if (void* const owner=ApplicationPhysicalOwner()) applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::Flags)=value;
#else
        g_applicationFlags=value;
#endif
    }

    std::uint32_t ActivePlayerIndex() noexcept
    {
#ifdef _WIN32
        void* const owner=ApplicationPhysicalOwner(); return owner ? applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::ActivePlayerIndex):0u;
#else
        return g_activePlayerIndexFallback;
#endif
    }

    void SetActivePlayerIndex(std::uint32_t value) noexcept
    {
#ifdef _WIN32
        if (void* const owner=ApplicationPhysicalOwner()) applicationPhysicalSlot<std::uint32_t>(owner, retail_application_layout::ActivePlayerIndex)=value;
#else
        g_activePlayerIndexFallback=value;
#endif
    }


    namespace
    {
        int drawPassFtolLow32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawPassFmulAddFtolLow32(float value, float scale, float addend) noexcept
        {
            const long double d = static_cast<long double>(value) * static_cast<long double>(scale) + static_cast<long double>(addend);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawPassFsubFtolLow32(float lhs, float rhs) noexcept
        {
            const long double d = static_cast<long double>(lhs) - static_cast<long double>(rhs);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawSpriteAndCaptureReturnValue(SPRITE* sprite) noexcept
        {
            sprite->Draw();
            return 0;
        }

        bool spriteVisibleForDrawPass(const SPRITE* sprite) noexcept
        {
            return sprite && !sprite->isDrawSuppressed();
        }
    }

    int Application::beginBucketTimingSnapshot(ApplicationDrawDispatcherState& state)
    {

        const std::uint32_t flag = ApplicationDrawDispatcherState::BucketTimingFlag;
        std::uint32_t applicationFlags = ApplicationFlags();
        if ((applicationFlags & flag) == 0)
            SetBucketTimingSnapshotMilliseconds(CurrentTimeMilliseconds());
        applicationFlags |= flag;
        SetApplicationFlags(applicationFlags);
        return static_cast<int>(flag);
    }

    int Application::endBucketTimingSnapshot(ApplicationDrawDispatcherState& state)
    {

        const std::uint32_t flag = ApplicationDrawDispatcherState::BucketTimingFlag;
        std::uint32_t applicationFlags = ApplicationFlags();
        if ((applicationFlags & flag) != 0)
        {
            for (int pass = 0; pass < ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
                int cursor = bucket.count() - 1;
                while (cursor >= 0)
                {
                    SPRITE* sprite = bucket.spriteAt(cursor);
                    if (sprite)
                        sprite->setApplicationBucketTime(BucketTimingSnapshotMilliseconds());
                    --cursor;
                }
            }
            SetCurrentTimeMilliseconds(BucketTimingSnapshotMilliseconds());
            SetPreviousWorldTimeMilliseconds(BucketTimingSnapshotMilliseconds() - 10u);
        }
        applicationFlags = ApplicationFlags() & ~flag;
        SetApplicationFlags(applicationFlags);
        return static_cast<int>(applicationFlags);
    }

    SPRITE* Application::previousSpriteInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor)
    {

        int index = --(*cursor);
        if (index < 0)
            return nullptr;
        const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
        while (!bucket.spriteAt(index))
        {
            index = --(*cursor);
            if (index < 0)
                return nullptr;
        }
        return bucket.spriteAt(index);
    }

    int Application::removeSpriteFromDrawBucket(ApplicationDrawDispatcherState& state, SPRITE* sprite)
    {

        const int layer = sprite->Vid()->renderLayer();
        return state.drawPassBucket(layer).findAndNull(sprite);
    }

    char* Application::appendSpriteToDrawBucketAndReleaseListReference(ApplicationDrawDispatcherState& state, SPRITE* sprite)
    {

        const int layer = sprite->Vid()->renderLayer();
        state.drawPassBucket(layer).append(sprite);

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs > 0)
            return reinterpret_cast<char*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(refs)));

        if (refs == 0)
        {
            SPRITE* const deletedOwner = sprite;
            DeleteSpriteThroughVirtualDeletingDestructor(sprite);
            return reinterpret_cast<char*>(deletedOwner);
        }

        const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
        const std::intptr_t logged = logFileLoggerResourceError(
            g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, nvid);
        return reinterpret_cast<char*>(static_cast<std::uintptr_t>(logged));
    }

    SPRITE* Application::previousSpriteOfTypeInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor, int spriteTypeMask)
    {

        const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
        int index = --(*cursor);
        while (index >= 0)
        {
            SPRITE* sprite = bucket.spriteAt(index);
            while (!sprite)
            {
                index = --(*cursor);
                if (index < 0)
                    return nullptr;
                sprite = bucket.spriteAt(index);
            }
            if ((sprite->Vid()->spriteTypeId() & static_cast<DWORD>(spriteTypeMask)) != 0u)
                return sprite;
            index = --(*cursor);
        }
        return nullptr;
    }

    SPRITE* Application::findSpriteAtPointByBounds(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y, SPRITE* previous)
    {

        SPRITE* const candidate = findNearestSpriteByFilter(map, state, filter, x, y, 300.0f, previous);
        if (!candidate)
            return nullptr;
        VID* const vid = candidate->Vid();
        const float halfX = vid->halfSizeX();
        if (candidate->X() - halfX > x || x > candidate->X() + halfX)
            return nullptr;
        const float halfY = vid->halfSizeY();
        if (candidate->Y() - halfY > y || y > candidate->Y() + halfY)
            return nullptr;
        return candidate;
    }

    SPRITE* Application::findSpriteAtPointByFilter(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y)
    {

        const int originalFilter = filter;
        int bucketMask = filter & 0x000F0000;
        if (bucketMask == 0)
            bucketMask = 0x000F0000;

        ApplicationVidTable& vidTable = GlobalApplicationVidTable();
        VID* requestedVid = MAP::NullVid();
        int typeMask = 0;
        if ((filter & 0x00000800) != 0)
        {
            const int nvid = filter & 0x7FF;
            if (nvid < vidTable.count())
            {
                if (VID* const slot = vidTable.slot(nvid))
                    requestedVid = slot;
            }
            if ((requestedVid->properties() & 0x40u) != 0u)
                filter |= 0x00008000;
            typeMask = static_cast<int>(requestedVid->spriteTypeId());
        }
        else
        {
            typeMask = (filter >> 20) & 0x67F;
            if (typeMask == 0)
                typeMask = 1663;
        }

        SPRITE* selected = nullptr;
        auto lessOrUnordered = [](long double lhs, long double rhs) noexcept -> bool
        {
            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        };
        auto preferCandidate = [&](SPRITE* candidate) noexcept
        {
            if (!selected ||
                lessOrUnordered(candidate->Vid()->sizeX(), selected->Vid()->sizeX()) ||
                lessOrUnordered(candidate->Vid()->sizeY(), selected->Vid()->sizeY()))
            {
                selected = candidate;
            }
        };

        auto passesFilter = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if ((typeMask & static_cast<int>(candidateVid->spriteTypeId())) == 0)
                return false;
            const int candidateBucketBit = 0x10000 << candidate->armyIndex();
            if ((candidateBucketBit & bucketMask) == 0)
                return false;
            if ((filter & static_cast<int>(0x80000000u)) != 0 && (candidate->runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return false;
            if ((filter & 0x1000) != 0 && candidateVid->spriteClassId() != static_cast<DWORD>(filter & 0x7FF))
                return false;
            if ((filter & 0x0800) != 0 && candidateVid->nvid() != (filter & 0x7FF))
                return false;
            return true;
        };

        auto standardHit = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            const long double centerX = candidate->X();
            const long double halfX = candidateVid->halfSizeX();
            const long double queryX = x;
            if (centerX - halfX > queryX || queryX > centerX + halfX)
                return false;

            const long double baseY = static_cast<long double>(candidate->Y()) - candidate->Z();
            const long double queryY = y;
            const long double lower = baseY - candidateVid->sizeZ() - candidateVid->halfSizeY();
            const long double upper = baseY + candidateVid->halfSizeY();
            // lower FCOMP uses test AH,1 (less OR unordered); upper FCOMP uses
            // test AH,41h and accepts only ordered greater-than.
            return lessOrUnordered(lower, queryY) && upper > queryY;
        };

        auto regionAwareHit = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() == 23u)
            {
                const REGION* const region = static_cast<const REGION*>(candidate);
                const long double centerX = candidate->X();
                const long double halfX = static_cast<long double>(region->regionWidth()) * 0.5L;
                const long double queryX = x;
                if (centerX - halfX > queryX || queryX > centerX + halfX)
                    return false;

                const long double baseY = static_cast<long double>(candidate->Y()) - candidate->Z();
                const long double halfY = static_cast<long double>(region->regionHeight()) * 0.5L;
                const long double queryY = y;
                const long double lower = baseY - candidateVid->sizeZ() - halfY;
                const long double upper = baseY + halfY;
                // Region path compares queryY against upper with another
                // test AH,1, so unordered is accepted on both Y boundaries.
                return lessOrUnordered(lower, queryY) && lessOrUnordered(queryY, upper);
            }
            return standardHit(candidate);
        };

        if ((filter & 0x8000) != 0)
        {
            const float maxY = map.GetGroundZ(VECTOR2{x, y}) + y + 300.0f;
            SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
            for (SPRITE* candidate = hash->firstSpriteInBox(x - 300.0f, y - 300.0f, x + 300.0f, maxY);
                 candidate;
                 candidate = hash->nextSpriteInBox())
            {
                if (candidate->childBacklink() != nullptr)
                    continue;
                if (!passesFilter(candidate) || !standardHit(candidate))
                    continue;
                preferCandidate(candidate);
            }
            return selected;
        }

        if ((typeMask & 0x0C) == 0 || (typeMask & 0x673) != 0)
        {
            if ((filter & 0x0800) != 0 && requestedVid->spriteClassId() == 10u)
            {
                SPRITE_LIST& frameList = applicationFrameSpriteList();
                int index = static_cast<int>(frameList.count()) - 1;
                while (index >= 0)
                {
                    SPRITE* const candidate = frameList.at(static_cast<std::size_t>(index));
                    if (!candidate)
                        return selected;
                    if (candidate->childBacklink() == nullptr && passesFilter(candidate) && standardHit(candidate))
                        preferCandidate(candidate);
                    --index;
                }
                return selected;
            }

            int firstPass = 0;
            int endPass = 13;
            if ((filter & 0x0800) != 0)
            {
                firstPass = requestedVid->renderLayer();
                endPass = firstPass + 1;
            }

            if ((typeMask & 0x40) == 0)
            {
                for (int pass = firstPass; pass < endPass; ++pass)
                {
                    int cursor = state.drawPassBucket(pass).count();
                    for (SPRITE* candidate = previousSpriteInDrawPass(state, pass, &cursor);
                         candidate;
                         candidate = previousSpriteInDrawPass(state, pass, &cursor))
                    {
                        if (candidate->childBacklink() != nullptr)
                            continue;
                        if (!passesFilter(candidate) || !standardHit(candidate))
                            continue;
                        preferCandidate(candidate);
                    }
                }
                return selected;
            }

            for (int pass = firstPass; pass < endPass; ++pass)
            {
                int cursor = state.drawPassBucket(pass).count();
                for (SPRITE* candidate = previousSpriteInDrawPass(state, pass, &cursor);
                     candidate;
                     candidate = previousSpriteInDrawPass(state, pass, &cursor))
                {
                    if (candidate->childBacklink() != nullptr)
                        continue;
                    if (!passesFilter(candidate) || !regionAwareHit(candidate))
                        continue;
                    preferCandidate(candidate);
                }
            }
            return selected;
        }

        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* candidate = overflow.beginReverseIteration(cursor);
             candidate;
             candidate = overflow.continueReverseIteration(cursor))
        {
            if (!passesFilter(candidate) || !standardHit(candidate))
                continue;
            preferCandidate(candidate);
        }
        (void)originalFilter;
        return selected;
    }

    SPRITE* Application::findNearestSpriteByFilter(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y, float radius, SPRITE* previous)
    {

        const int originalFilter = filter;
        int bucketMask = filter & 0x000F0000;
        if (bucketMask == 0)
            bucketMask = 0x000F0000;

        ApplicationVidTable& vidTable = GlobalApplicationVidTable();
        VID* requestedVid = MAP::NullVid();
        int typeMask = 0;
        if ((filter & 0x00000800) != 0)
        {
            const int nvid = filter & 0x7FF;
            if (nvid < vidTable.count())
            {
                if (VID* const slot = vidTable.slot(nvid))
                    requestedVid = slot;
            }
            if ((requestedVid->properties() & 0x40u) != 0u)
                filter |= 0x00008000;
            typeMask = static_cast<int>(requestedVid->spriteTypeId());
        }
        else
        {
            typeMask = (filter >> 20) & 0x67F;
            if (typeMask == 0)
                typeMask = 1663;
        }

        SPRITE* selected = nullptr;
        const long double previousDistance = previous
            ? as1::approximatePlanarDistance(x - previous->X(), y - previous->Y())
            : -1.0L;

        float bestDistance = radius;

        auto passesFilter = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if ((typeMask & static_cast<int>(candidateVid->spriteTypeId())) == 0)
                return false;
            const int candidateBucketBit = 0x10000 << candidate->armyIndex();
            if ((candidateBucketBit & bucketMask) == 0)
                return false;
            if ((filter & static_cast<int>(0x80000000u)) != 0 && (candidate->runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return false;
            if ((filter & 0x1000) != 0 && candidateVid->spriteClassId() != static_cast<DWORD>(filter & 0x7FF))
                return false;
            if ((filter & 0x0800) != 0 && candidateVid->nvid() != (filter & 0x7FF))
                return false;
            return true;
        };

        auto acceptMetric = [&](SPRITE* candidate, long double metric) noexcept
        {
            if (!(metric > previousDistance))
                return;
            if (metric < static_cast<long double>(bestDistance) ||
                std::isnan(bestDistance))
            {
                bestDistance = static_cast<float>(metric);
                selected = candidate;
            }
        };

        auto considerDistance = [&](SPRITE* candidate) noexcept
        {
            if (candidate->childBacklink() != nullptr || !passesFilter(candidate))
                return;

            const float dx = x - candidate->X();
            const float dy = y - candidate->Y();
            acceptMetric(candidate, as1::approximatePlanarDistance(dx, dy));
        };

        auto considerWeighted = [&](SPRITE* candidate) noexcept
        {
            if (candidate->childBacklink() != nullptr || !passesFilter(candidate))
                return;
            // Non-hash routes keep the float operands live in x87 and use
            // min(abs(dx),abs(dy))*0.5 + max(...).  FCOMP/test AH,41h takes
            // the first branch for <= and unordered.
            const long double dx = std::fabs(
                static_cast<long double>(x) - static_cast<long double>(candidate->X()));
            const long double dy = std::fabs(
                static_cast<long double>(y) - static_cast<long double>(candidate->Y()));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            acceptMetric(candidate, metric);
        };

        if ((filter & 0x8000) != 0)
        {
            SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
            for (SPRITE* candidate = hash->firstSpriteInBox(x - radius, y - radius, x + radius, y + radius);
                 candidate;
                 candidate = hash->nextSpriteInBox())
            {
                considerDistance(candidate);
            }
            return selected;
        }

        if ((typeMask & 0x0C) == 0 || (typeMask & 0x673) != 0)
        {
            if ((filter & 0x0800) != 0 && requestedVid->spriteClassId() == 10u)
            {
                SPRITE_LIST& frameList = applicationFrameSpriteList();
                int index = static_cast<int>(frameList.count()) - 1;
                if (index < 0)
                    return selected;
                SPRITE* candidate = frameList.at(static_cast<std::size_t>(index));
                if (!candidate)
                    return selected;
                for (;;)
                {
                    considerWeighted(candidate);
                    --index;
                    if (index < 0)
                        break;
                    candidate = frameList.at(static_cast<std::size_t>(index));
                    if (!candidate)
                        return selected;
                }
                return selected;
            }

            int firstPass = 0;
            int endPass = 13;
            if ((filter & 0x0800) != 0)
            {
                firstPass = requestedVid->renderLayer();
                endPass = firstPass + 1;
            }

            for (int pass = firstPass; pass < endPass; ++pass)
            {
                int cursor = state.drawPassBucket(pass).count();
                for (SPRITE* candidate = previousSpriteInDrawPass(state, pass, &cursor);
                     candidate;
                     candidate = previousSpriteInDrawPass(state, pass, &cursor))
                {
                    considerWeighted(candidate);
                }
            }
            return selected;
        }

        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* candidate = overflow.beginReverseIteration(cursor);
             candidate;
             candidate = overflow.continueReverseIteration(cursor))
        {
            considerWeighted(candidate);
        }
        (void)originalFilter;
        return selected;
    }

    int Application::drawSpritePass(ApplicationDrawDispatcherState& state, int pass)
    {

        const int currentPass = pass;
        int cursor = 0;
        if (pass != 0 && pass != 10)
        {
            GRAPH* const graph = GRAPH::CurrentGraph();
            constexpr float half = 0.5f;
            const int viewCenterX = drawPassFmulAddFtolLow32(
                graph->screenWidth(), half, state.cameraShiftX());
            const int viewCenterY = drawPassFmulAddFtolLow32(
                graph->screenHeight(), half, state.cameraShiftY());

            cursor = state.drawPassBucket(pass).count();
            SPRITE* sprite = previousSpriteInDrawPass(state, pass, &cursor);
            while (sprite)
            {
                if (spriteVisibleForDrawPass(sprite))
                {
                    const std::uint32_t xMaskValue =
                        static_cast<std::uint32_t>(drawPassFtolLow32(sprite->X())) -
                        static_cast<std::uint32_t>(viewCenterX) + 0x400u;

                    bool draw = false;
                    if ((xMaskValue & 0xFFFFF800u) != 0u)
                    {
                        const std::int32_t topYDelta = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(drawPassFtolLow32(sprite->Y())) -
                            static_cast<std::uint32_t>(viewCenterY));
                        if (topYDelta >= 0x200)
                            draw = true;
                    }
                    else
                    {
                        const std::uint32_t baseYMaskValue =
                            static_cast<std::uint32_t>(drawPassFsubFtolLow32(sprite->Y(), sprite->Z())) -
                            static_cast<std::uint32_t>(viewCenterY) + 0x200u;
                        if ((baseYMaskValue & 0xFFFFFC00u) == 0u)
                        {
                            draw = true;
                        }
                        else
                        {
                            const std::int32_t topYDelta = static_cast<std::int32_t>(
                                static_cast<std::uint32_t>(drawPassFtolLow32(sprite->Y())) -
                                static_cast<std::uint32_t>(viewCenterY));
                            if (topYDelta >= 0x200)
                                draw = true;
                        }
                    }

                    if (draw)
                        sprite->Draw();
                }

                --cursor;
                const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
                while (cursor >= 0 && !bucket.spriteAt(cursor))
                    --cursor;
                sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
            }
        }
        else
        {
            const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
            cursor = bucket.count() - 1;
            while (cursor >= 0 && !bucket.spriteAt(cursor))
                --cursor;
            SPRITE* sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
            while (sprite)
            {
                if (spriteVisibleForDrawPass(sprite))
                    sprite->Draw();
                --cursor;
                while (cursor >= 0 && !bucket.spriteAt(cursor))
                    --cursor;
                sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
            }
        }

        MOUSE* const mouse = mouseInstanceRef();
        int result = mouse->hardwareCursorEnabled();
        VID* const rootMouseVid = mouse->Vid();
        if (result == 0 && (rootMouseVid->properties() & 0x00008000u) == 0u && mouse)
        {
            for (SPRITE* node = mouse; node; node = node->childChain())
            {
                VID* const vid = node->Vid();
                if (vid->renderLayer() != currentPass)
                    continue;

                result = static_cast<int>(node->runtimeFlags());
                if (!node->isDrawSuppressed())
                    result = drawSpriteAndCaptureReturnValue(node);
            }
        }
        return result;
    }

    SCRIPT* ApplicationScriptRuntime() noexcept
    {
#ifdef _WIN32
        void* const owner = ApplicationPhysicalOwner();
        return owner ? reinterpret_cast<SCRIPT*>(static_cast<std::uint8_t*>(owner) + retail_application_layout::ScriptRuntime) : nullptr;
#else
        return &g_applicationScriptOwner;
#endif
    }

    void Application::DrawDebugPass(ApplicationDebugPassState& state, const ApplicationDebugPassContext& context)
    {
        if (!context.graph)
            return;

        if (state.flags & ApplicationDebugShowFps)
            drawFpsCounter(state, context);

        if (!debugPassReady(context))
            return;

        if (state.flags & ApplicationDebugShowSoundCount)
            drawSoundCount(context);

        if (state.flags & ApplicationDebugDrawTerrainGrid)
            drawTerrainGrid(context);

        if (state.flags & ApplicationDebugDrawSpriteBuckets)
            drawSpriteBuckets(context);

        if (state.flags & ApplicationDebugDrawCurrentSprite)
            drawCurrentSprite(context);

        if (state.flags & ApplicationDebugDrawScrollBox)
            drawScrollBox(context);

        if (state.flags & ApplicationDebugDrawAuxiliaryList)
            drawAuxiliaryList(context);
    }


    int Application::callScriptFunction(std::uint32_t applicationFlags, SCRIPT* scriptOwner, int functionIndex, int firstArgument, int secondArgument, int thirdArgument)
    {
        if ((applicationFlags & application_flags::ScriptCallbacksDisabled) != 0)
            return 0;
        return scriptOwner->callFunction(functionIndex, firstArgument, secondArgument, thirdArgument);
    }

    int Application::callScriptFunctionRetail(int functionIndex, int firstArgument, int secondArgument, int thirdArgument)
    {
        auto* const raw = reinterpret_cast<std::uint8_t*>(this);
        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
            raw + retail_application_layout::Flags);
        if ((flags & application_flags::ScriptCallbacksDisabled) != 0u)
            return 0;

        auto* const scriptOwner = reinterpret_cast<SCRIPT*>(
            raw + retail_application_layout::ScriptRuntime);
        return scriptOwner->callFunction(functionIndex, firstArgument, secondArgument, thirdArgument);
    }

    int Application::callScriptFunction(int functionIndex, int firstArgument, int secondArgument, int thirdArgument)
    {
#ifdef _WIN32
        auto* const owner = reinterpret_cast<Application*>(ApplicationPhysicalOwner());
        return owner->callScriptFunctionRetail(functionIndex, firstArgument, secondArgument, thirdArgument);
#else
        return callScriptFunction(ApplicationFlags(),
                                  ApplicationScriptRuntime(),
                                  functionIndex,
                                  firstArgument,
                                  secondArgument,
                                  thirdArgument);
#endif
    }


#if defined(_WIN32) && UINTPTR_MAX == 0xFFFFFFFFu
    SPRITE* Application::CreateSpriteRetail(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
    {
        MAP* const owner = MAP::Current();
        if (!owner || !vid)
            return nullptr;

        VID* selectedVid = vid;
        if ((selectedVid->runtimeAuxFlags() & 0x10u) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, xyz.x, xyz.y, xyz.z);

        SPRITE* sprite = nullptr;
        switch (selectedVid->spriteClassId())
        {
        case B_UNIT:
            sprite = new UNIT(owner, selectedVid, xyz, direction, parent);
            break;
        case B_AVIA:
            sprite = new AVIA(owner, selectedVid, xyz, direction, parent);
            break;
        case B_CANNON:
            sprite = new CANNON(owner, selectedVid, xyz, direction, parent);
            break;
        case B_PRIMITIVE:
            sprite = new PRIMITIVE(owner, selectedVid, xyz, direction, parent);
            break;
        case 7u:
            sprite = new RetailPrivateClass7(owner, selectedVid, xyz, direction, parent);
            break;
        case B_BUILDEDTERRAIN:
        {
            BUILDED_TERRAIN* const builtedTerrain =
                new BUILDED_TERRAIN(owner, selectedVid, xyz, direction, parent);
            if (!builtedTerrain)
                break;

            VID* ground = owner->VidOrNull(1024);
            if (ground == MAP::NullVid())
            {
                owner->CreateEmptyHardwareGround();
                ground = owner->VidOrNull(1024);
            }
            if (ground != MAP::NullVid() && ground->directionCount() == 1)
            {
                static_cast<VID_HARDWARE*>(ground)->AddVidToVid(builtedTerrain);
                if ((selectedVid->properties() & 0x00000040u) == 0u)
                    builtedTerrain->ChangeAnimation(15);
            }
            sprite = builtedTerrain;
            break;
        }
        case B_SPRITE:
            sprite = new SPRITE(owner, selectedVid, xyz, direction, parent);
            break;
        case B_FRAME:
            sprite = new FRAME(owner, selectedVid, xyz, direction, parent);
            break;
        case B_LINKER:
            sprite = new LINKER(owner, selectedVid, xyz, direction, parent);
            break;
        case B_TEXT:
            sprite = new STEXT(owner, selectedVid, xyz, direction, parent);
            break;
        case B_REGION:
            sprite = new REGION(owner, selectedVid, xyz, direction, parent);
            break;
        default:
            return nullptr;
        }

        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) + retail_application_layout::Flags);
        if (sprite &&
            (flags & (0x21u | application_flags::ScriptCallbacksDisabled)) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite)));
                callScriptFunctionRetail(functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }
#else
    std::unique_ptr<SPRITE> Application::CreateSprite(const ApplicationCreateSpriteRequest& request)
    {
        if (!request.owner || !request.vid)
            return nullptr;

        VID* selectedVid = request.vid;
        if ((selectedVid->runtimeAuxFlags() & 0x10u) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, request.xyz.x, request.xyz.y, request.xyz.z);

        std::unique_ptr<SPRITE> sprite;
        switch (selectedVid->spriteClassId())
        {
        case B_UNIT:
            sprite = std::make_unique<UNIT>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_AVIA:
            sprite = std::make_unique<AVIA>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_CANNON:
            sprite = std::make_unique<CANNON>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_PRIMITIVE:
            sprite = std::make_unique<PRIMITIVE>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case 7u:
            sprite = std::make_unique<RetailPrivateClass7>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_BUILDEDTERRAIN:
        {
            auto builtedTerrain = std::make_unique<BUILDED_TERRAIN>(
                request.owner, selectedVid, request.xyz, request.direction, request.parent);
            VID* ground = request.owner->VidOrNull(1024);
            if (ground == MAP::NullVid())
            {
                request.owner->CreateEmptyHardwareGround();
                ground = request.owner->VidOrNull(1024);
            }
            if (ground != MAP::NullVid() && ground->directionCount() == 1)
            {
                static_cast<VID_HARDWARE*>(ground)->AddVidToVid(builtedTerrain.get());
                if ((selectedVid->properties() & 0x00000040u) == 0u)
                    builtedTerrain->ChangeAnimation(15);
            }
            sprite = std::move(builtedTerrain);
            break;
        }
        case B_SPRITE:
            sprite = std::make_unique<SPRITE>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_FRAME:
            sprite = std::make_unique<FRAME>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_LINKER:
            sprite = std::make_unique<LINKER>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_TEXT:
            sprite = std::make_unique<STEXT>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_REGION:
            sprite = std::make_unique<REGION>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        default:
            return nullptr;
        }

        if (sprite &&
            (ApplicationFlags() & (0x21u | application_flags::ScriptCallbacksDisabled)) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite.get())));
                callScriptFunction(functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }
#endif

    void Application::drawFpsCounter(ApplicationDebugPassState& state, const ApplicationDebugPassContext& context)
    {
        (void)state;
        GRAPH& graph = *context.graph;

        graph.DrawText(static_cast<float>(g_softwareClipLeft), g_softwareClipTop + 1.0f,
                       "%i", static_cast<int>(DisplayedFramesPerSecond()));
    }

    void Application::drawSoundCount(const ApplicationDebugPassContext& context)
    {
        GRAPH& graph = *context.graph;
        const sound::Engine* const engine = sound::GlobalSoundEngine();
        const int playing = engine ? engine->playingSoundCount() : 0;
        graph.DrawText(g_softwareClipRight - 20.0f, g_softwareClipTop + 1.0f, "%2i", playing);
    }

    void Application::drawTerrainGrid(const ApplicationDebugPassContext& context)
    {
        context.map->DrawDebugTerrainGrid(*context.graph);
    }

    void Application::drawSpriteBuckets(const ApplicationDebugPassContext& context)
    {

        const ApplicationDrawDispatcherState& dispatcher = GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < 12; ++pass)
        {
            const ApplicationDrawPassBucket& bucket = dispatcher.drawPassBucket(pass);
            for (int index = bucket.count() - 1; index >= 0; --index)
            {
                SPRITE* const sprite = bucket.spriteAt(index);
                if (sprite)
                    (void)sprite->dispatchDebugOverlay();
            }
        }
    }

    void Application::drawCurrentSprite(const ApplicationDebugPassContext& context)
    {

        if (context.selectedSprite)
            context.selectedSprite->DrawDebugOverlay();
    }

    void Application::drawScrollBox(const ApplicationDebugPassContext& context)
    {
        context.map->DrawDebugScrollBox(*context.graph);
    }

    void Application::drawAuxiliaryList(const ApplicationDebugPassContext& context)
    {
        context.map->DrawDebugAuxiliaryList(*context.graph);
    }
} }
