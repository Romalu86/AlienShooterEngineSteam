#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#if !defined(_MSC_VER) || !defined(_M_IX86)
#include <vector>
#endif

#include "core/types.h"
#include "base_sprite_list.h"

namespace as1
{
    namespace application_flags
    {
        constexpr std::uint32_t EnemyCanAttackNeutralTrains = 1u << 1;
        constexpr std::uint32_t BucketTimingActive = 1u << 4;
        constexpr std::uint32_t MapLoading = 1u << 5;
        constexpr std::uint32_t PendingCommandOrLoad = 1u << 6;
        constexpr std::uint32_t ScriptControlBit7 = 1u << 7;
        constexpr std::uint32_t DemoWriteToResource = 1u << 8;
        constexpr std::uint32_t DemoUseResource = 1u << 9;
        constexpr std::uint32_t ScriptCallbacksDisabled = 1u << 19;
    }
    class GRAPH;
    class MAP;
    class SPRITE;
    class VID;
    class SCRIPT;
    class STRING;
    struct WEAPON;
}

namespace as1 { namespace core
{

    namespace retail_application_layout
    {
        constexpr std::size_t ObjectSize = 0x22C8u;
        constexpr std::size_t Fps = 0x04u;
        constexpr std::size_t FpsCounter = 0x08u;
        constexpr std::size_t Flags = 0x0Cu;
        constexpr std::size_t TickScale = 0x14u;
        constexpr std::size_t SavePath = 0x18u;
        constexpr std::size_t ApplicationTitle = 0x1Cu;
        constexpr std::size_t CurrentMapName = 0x20u;
        constexpr std::size_t PendingCommand = 0x24u;
        constexpr std::size_t PreviousMapName = 0x28u;
        constexpr std::size_t ResourceName = 0x2Cu;
        constexpr std::size_t RegistryPath = ResourceName;
        constexpr std::size_t WorldFrameCounter = 0x30u;
        constexpr std::size_t WorldStartTime = 0x34u;
        constexpr std::size_t MapExtentX = 0x38u;
        constexpr std::size_t MapExtentY = 0x3Cu;
        constexpr std::size_t ScrollType = 0x40u;
        constexpr std::size_t ScrollMinX = 0x44u;
        constexpr std::size_t ScrollMaxX = 0x48u;
        constexpr std::size_t ScrollMinY = 0x4Cu;
        constexpr std::size_t ScrollMaxY = 0x50u;
        constexpr std::size_t CameraShiftX = 0x54u;
        constexpr std::size_t CameraShiftY = 0x58u;
        constexpr std::size_t DrawLayerOwners = 0x5Cu;
        constexpr std::size_t DrawLayerCount = 17u;
        constexpr std::size_t DrawLayerStride = 0x10u;
        constexpr std::size_t ScriptRuntime = 0x16Cu;
        constexpr std::size_t DemoResource = 0x1C4u;
        constexpr std::size_t RelationTable = 0x204u;
        constexpr std::size_t TerrainGrid = 0x224u;
        constexpr std::size_t TempTerrainGrid = 0x228u;
        constexpr std::size_t TerrainGridWidth = 0x22Cu;
        constexpr std::size_t TerrainGridHeight = 0x230u;
        constexpr std::size_t InstanceHandle = 0x234u;
        constexpr std::size_t MainWindow = 0x238u;
        constexpr std::size_t Accelerator = 0x23Cu;
        constexpr std::size_t ActivePlayerIndex = 0x240u;
        constexpr std::size_t PlayerSlots = 0x244u;
        constexpr std::size_t PlayerSlotStride = sizeof(std::uint32_t);
        constexpr std::size_t InputState = 0x254u;
        constexpr std::size_t Menu = 0x274u;
        constexpr std::size_t BaseSpriteList = Menu; // compatibility alias for generic list access
        constexpr std::size_t Groups = 0x28Cu;
        constexpr std::size_t WeaponCount = 0x2B0u;
        constexpr std::size_t WeaponTable = 0x2B4u;
        constexpr std::size_t VidCount = 0x2B8u;
        constexpr std::size_t VidTable = 0x2BCu;
        constexpr std::size_t VidTableBytes = 0x2000u;
        constexpr std::size_t ShellOwnedSpriteVtable = 0x22BCu;
        constexpr std::size_t ShellOwnedSpritePointer = 0x22C0u;
        constexpr std::size_t ShellFlags = 0x22C4u;

                                                                                          
                                                                             
                                                            
                                                               
                                                            
                                                                 
                                                           
                                                                           
                                                 
                                                                    
                                             
                                                             
                                                    
                                                               
                                                                         
                                                                                 
                                                                       
                                                           
    }

    extern void* g_applicationPhysicalOwner;
    void BindApplicationPhysicalOwner(void* owner) noexcept;
#if defined(_MSC_VER)
    __forceinline void* ApplicationPhysicalOwner() noexcept { return g_applicationPhysicalOwner; }
#else
    inline void* ApplicationPhysicalOwner() noexcept { return g_applicationPhysicalOwner; }
#endif
    void InitializeApplicationPhysicalMapStorage(void* owner) noexcept;
    void InitializeApplicationPhysicalDrawStorage(void* owner) noexcept;
    void DestroyApplicationPhysicalDrawStorage(void* owner) noexcept;

    short* ApplicationTerrainGrid() noexcept;
    void SetApplicationTerrainGrid(short* value) noexcept;
    short* ApplicationTempTerrainGrid() noexcept;
    void SetApplicationTempTerrainGrid(short* value) noexcept;
    int ApplicationTerrainGridWidth() noexcept;
    void SetApplicationTerrainGridWidth(int value) noexcept;
    int ApplicationTerrainGridHeight() noexcept;
    void SetApplicationTerrainGridHeight(int value) noexcept;
    int ApplicationWeaponCount() noexcept;
    void SetApplicationWeaponCount(int value) noexcept;
    WEAPON* ApplicationWeaponTable() noexcept;
    void SetApplicationWeaponTable(WEAPON* value) noexcept;

    class ApplicationVidTable
    {
    public:
        static constexpr std::size_t kRetailCapacity = 0x800u;
        static constexpr std::size_t kCapacity = 0x2000u;
        static constexpr std::uint32_t kCountOffset = retail_application_layout::VidCount;
        static constexpr std::uint32_t kFirstSlotOffset = retail_application_layout::VidTable;
        static constexpr std::uint32_t kEndSlotOffset = retail_application_layout::ShellOwnedSpriteVtable;

        void clear() noexcept;
        bool setSlot(int nvid, VID* vid) noexcept;
        void setWeaponSentinel(WEAPON* weapon) noexcept { SetApplicationWeaponTable(weapon); }
        WEAPON* weaponSentinel() const noexcept { return ApplicationWeaponTable(); }
        int count() const noexcept;
        VID* slot(int index) const noexcept;
        void setStoredCount(int value) noexcept;
        void setSlotCell(int index, VID* vid) noexcept;
        VID* const* slotData() const noexcept;
        std::size_t capacity() const noexcept { return kCapacity; }
#if !defined(_MSC_VER) || !defined(_M_IX86)
        std::size_t loadedSlotCount() const noexcept;
        std::size_t loadedSlotCountWithinGammaScan() const noexcept;
        std::vector<VID*> loadedSlotsSnapshot() const;
#endif

    };

    ApplicationVidTable& GlobalApplicationVidTable() noexcept;

    constexpr int EncodeVidQueryFilter(int nvid) noexcept
    {
        return nvid < static_cast<int>(ApplicationVidTable::kRetailCapacity)
            ? nvid + 0x0800
            : 0x4000 | (nvid & 0x1FFF);
    }


    struct ApplicationDrawPassBucket
    {
        static constexpr std::uint32_t CountOffset = 0x50u;
        static constexpr std::uint32_t ListOffset = 0x58u;
        static constexpr std::uint32_t Stride = 0x10u;

        SPRITE_POINTER_LIST list;

        int count() const noexcept { return list.activeCount(); }
        SPRITE* const* data() const noexcept { return list.data(); }
        SPRITE* spriteAt(int index) const noexcept;
        int findAndNull(SPRITE* sprite) noexcept;
        void append(SPRITE* sprite);
        void compactSparse() noexcept { list.compactSparse(); }
        void clear() noexcept { list.clearSpriteReferences(); }
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                                                   
#endif

    struct ApplicationDrawDispatcherState
    {
        static constexpr int PassCount = 17;
        static constexpr std::uint32_t ScrollMinXOffset = 0x44u;
        static constexpr std::uint32_t ScrollMaxXOffset = 0x48u;
        static constexpr std::uint32_t ScrollMinYOffset = 0x4Cu;
        static constexpr std::uint32_t ScrollMaxYOffset = 0x50u;
        static constexpr std::uint32_t CameraXOffset = 0x54u;
        static constexpr std::uint32_t CameraYOffset = 0x58u;
        static constexpr std::uint32_t BucketCountBaseOffset = 0x60u;
        static constexpr std::uint32_t BucketListBaseOffset = 0x68u;
        static constexpr std::uint32_t BucketStride = 0x10u;

#ifndef _WIN32
        std::uint32_t fallbackFlags = 0;
        float scrollMinX = 0.0f;
        float scrollMaxX = 0.0f;
        float scrollMinY = 0.0f;
        float scrollMaxY = 0.0f;
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        std::array<ApplicationDrawPassBucket, PassCount> passBuckets{};
#endif

        static constexpr std::uint32_t FlagsOffset = 0x0Cu;
        static constexpr std::uint32_t BucketTimingFlag = application_flags::BucketTimingActive;

        std::uint32_t flags() const noexcept;
        void setFlags(std::uint32_t value) noexcept;
        bool bucketTimingEnabled() const noexcept { return (flags() & BucketTimingFlag) != 0; }
        float scrollMinXLimit() const noexcept;
        float scrollMaxXLimit() const noexcept;
        float scrollMinYLimit() const noexcept;
        float scrollMaxYLimit() const noexcept;
        void setScrollMinXLimit(float value) noexcept;
        void setScrollMaxXLimit(float value) noexcept;
        void setScrollMinYLimit(float value) noexcept;
        void setScrollMaxYLimit(float value) noexcept;
        float cameraShiftX() const noexcept;
        float cameraShiftY() const noexcept;
        double cameraRelativeX(float value) const noexcept;
        double cameraRelativeY(float value) const noexcept;
        void setCameraShiftX(float value) noexcept;
        void setCameraShiftY(float value) noexcept;
        ApplicationDrawPassBucket& drawPassBucket(int pass) noexcept;
        const ApplicationDrawPassBucket& drawPassBucket(int pass) const noexcept;
        void clear() noexcept;
    };

    ApplicationDrawDispatcherState& GlobalApplicationDrawDispatcherState() noexcept;


    struct ApplicationFrameRuntimeState
    {
#ifndef _WIN32
        SPRITE* currentFrameSpriteFallback = nullptr;
#endif
        SPRITE* currentFrameSprite() const noexcept;
        void setCurrentFrameSprite(SPRITE* sprite) noexcept;
        bool clearCurrentFrameSpriteIfMatches(SPRITE* sprite) noexcept;
    };

    ApplicationFrameRuntimeState& GlobalApplicationFrameRuntimeState() noexcept;

    extern std::uint32_t g_currentTimeMilliseconds;
    extern std::uint32_t g_previousWorldTimeMilliseconds;
#if defined(_MSC_VER)
    __forceinline std::uint32_t CurrentTimeMilliseconds() noexcept { return g_currentTimeMilliseconds; }
    __forceinline void SetCurrentTimeMilliseconds(std::uint32_t value) noexcept { g_currentTimeMilliseconds = value; }
#else
    inline std::uint32_t CurrentTimeMilliseconds() noexcept { return g_currentTimeMilliseconds; }
    inline void SetCurrentTimeMilliseconds(std::uint32_t value) noexcept { g_currentTimeMilliseconds = value; }
#endif

    float ApplicationTickScale() noexcept;
    void SetApplicationTickScale(float value) noexcept;
    std::uint32_t ApplicationWorldFrameCounter() noexcept;
    void SetApplicationWorldFrameCounter(std::uint32_t value) noexcept;
    std::uint32_t ApplicationWorldStartTime() noexcept;
    void SetApplicationWorldStartTime(std::uint32_t value) noexcept;

    float ApplicationMapWidth() noexcept;
    void SetApplicationMapWidth(float value) noexcept;
    float ApplicationMapHeight() noexcept;
    void SetApplicationMapHeight(float value) noexcept;
    const STRING& ApplicationSavePath() noexcept;
    const STRING& ApplicationCurrentMapName() noexcept;
    const STRING& ApplicationPreviousMapName() noexcept;
    void SetApplicationCurrentMapName(const STRING& value);
    std::uint32_t ApplicationScrollType() noexcept;
    void SetApplicationScrollType(std::uint32_t value) noexcept;
    std::uint32_t DemoStartTimestampMilliseconds() noexcept;
    void SetDemoStartTimestampMilliseconds(std::uint32_t value) noexcept;

#if defined(_MSC_VER)
    __forceinline std::uint32_t PreviousWorldTimeMilliseconds() noexcept { return g_previousWorldTimeMilliseconds; }
    __forceinline void SetPreviousWorldTimeMilliseconds(std::uint32_t value) noexcept { g_previousWorldTimeMilliseconds = value; }
#else
    inline std::uint32_t PreviousWorldTimeMilliseconds() noexcept { return g_previousWorldTimeMilliseconds; }
    inline void SetPreviousWorldTimeMilliseconds(std::uint32_t value) noexcept { g_previousWorldTimeMilliseconds = value; }
#endif

    float& ApplicationScrollVelocityX() noexcept;
    float& ApplicationScrollVelocityY() noexcept;
    std::uint32_t& LastFpsSampleTimeMilliseconds() noexcept;
    extern std::uint32_t g_displayedFramesPerSecond;
#if defined(_MSC_VER)
    __forceinline std::uint32_t& DisplayedFramesPerSecond() noexcept
#else
    inline std::uint32_t& DisplayedFramesPerSecond() noexcept
#endif
    {
#ifdef _WIN32
        if (void* const owner = ApplicationPhysicalOwner())
            return *reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(owner) + retail_application_layout::Fps);
#endif
        return g_displayedFramesPerSecond;
    }
    std::uint32_t& AccumulatedFpsFrameCount() noexcept;
    std::uint32_t BucketTimingSnapshotMilliseconds() noexcept;
    void SetBucketTimingSnapshotMilliseconds(std::uint32_t value) noexcept;

    std::uint32_t DemoRealTimeBaseMilliseconds() noexcept;
    void SetDemoRealTimeBaseMilliseconds(std::uint32_t value) noexcept;
    std::uint32_t DemoRecordedTimeBaseMilliseconds() noexcept;
    void SetDemoRecordedTimeBaseMilliseconds(std::uint32_t value) noexcept;

    static constexpr std::size_t kScriptCallbackSlotCount = 64u;
    int scriptCallbackSlot(std::size_t index) noexcept;
    void setScriptCallbackSlot(std::size_t index, int value) noexcept;
    void resetScriptCallbackSlots() noexcept;

    std::uint32_t ChildRotationCorrectionPending() noexcept;
    void SetChildRotationCorrectionPending(std::uint32_t value) noexcept;
    std::uint32_t BulkSpriteDeleteActive() noexcept;
    void SetBulkSpriteDeleteActive(std::uint32_t value) noexcept;

    std::uint32_t RealTimeMilliseconds() noexcept;
    void SetRealTimeMilliseconds(std::uint32_t value) noexcept;
    std::uint32_t PreviousRealTimeMilliseconds() noexcept;
    void SetPreviousRealTimeMilliseconds(std::uint32_t value) noexcept;

    std::uint32_t ApplicationFlags() noexcept;
    void SetApplicationFlags(std::uint32_t value) noexcept;

    std::uint32_t ActivePlayerIndex() noexcept;
    void SetActivePlayerIndex(std::uint32_t value) noexcept;

    SCRIPT* ApplicationScriptRuntime() noexcept;

    enum ApplicationDebugFlag : std::uint32_t
    {
        ApplicationDebugShowFps               = 0x00020000u,
        ApplicationDebugShowSoundCount        = 0x00010000u,
        ApplicationDebugDrawTerrainGrid       = 0x00000800u,
        ApplicationDebugDrawSpriteBuckets     = 0x00008000u,
        ApplicationDebugDrawCurrentSprite     = 0x00001000u,
        ApplicationDebugDrawAuxiliaryList     = 0x00002000u,
        ApplicationDebugDrawScrollBox         = 0x00004000u,
    };

    struct ApplicationDebugPassState
    {

        std::uint32_t flags = 0;
    };

    struct ApplicationDebugPassContext
    {
        GRAPH* graph = nullptr;
        MAP* map = nullptr;
        SPRITE* selectedSprite = nullptr;
    };

    struct ApplicationCreateSpriteRequest
    {
        MAP* owner = nullptr;
        VID* vid = nullptr;
        VECTOR xyz{};
        ANGLE direction{};
        SPRITE* parent = nullptr;
        bool remoteControlled = false;
    };

    class Application
    {
    public:

        static void DrawDebugPass(ApplicationDebugPassState& state, const ApplicationDebugPassContext& context);

        static std::unique_ptr<SPRITE> CreateSprite(const ApplicationCreateSpriteRequest& request);
#if defined(_WIN32) && UINTPTR_MAX == 0xFFFFFFFFu
        SPRITE* CreateSpriteRetail(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent);
#endif

        static int callScriptFunction(std::uint32_t applicationFlags, SCRIPT* scriptOwner, int functionIndex, int firstArgument, int secondArgument, int thirdArgument = 0);

        // Application image whose flags live at +0x0C and SCRIPT at +0x16C.
        int callScriptFunctionRetail(int functionIndex, int firstArgument, int secondArgument, int thirdArgument = 0);

        // Source-facing singleton wrapper. Optimized Win32 callers collapse
        // this into a call of callScriptFunctionRetail with ECX=Application.
        static int callScriptFunction(int functionIndex, int firstArgument, int secondArgument, int thirdArgument = 0);

        static int drawSpritePass(ApplicationDrawDispatcherState& state, int pass);
        static int beginBucketTimingSnapshot(ApplicationDrawDispatcherState& state);
        static int endBucketTimingSnapshot(ApplicationDrawDispatcherState& state);
        static SPRITE* previousSpriteInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor);
        static int removeSpriteFromDrawBucket(ApplicationDrawDispatcherState& state, SPRITE* sprite);
        static char* appendSpriteToDrawBucketAndReleaseListReference(ApplicationDrawDispatcherState& state, SPRITE* sprite);
        static SPRITE* previousSpriteOfTypeInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor, int spriteTypeMask);
        static SPRITE* findSpriteAtPointByBounds(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y,
                                                   SPRITE* previous = nullptr);
        static SPRITE* findSpriteAtPointByFilter(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y);
        static SPRITE* findNearestSpriteByFilter(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y, float radius,
                                                   SPRITE* previous = nullptr);

    private:
        static void drawFpsCounter(ApplicationDebugPassState& state, const ApplicationDebugPassContext& context);
        static void drawSoundCount(const ApplicationDebugPassContext& context);
        static void drawTerrainGrid(const ApplicationDebugPassContext& context);
        static void drawSpriteBuckets(const ApplicationDebugPassContext& context);
        static void drawCurrentSprite(const ApplicationDebugPassContext& context);
        static void drawScrollBox(const ApplicationDebugPassContext& context);
        static void drawAuxiliaryList(const ApplicationDebugPassContext& context);
    };
} }
