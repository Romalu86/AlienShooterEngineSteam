#pragma once
#include "core/types.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/application.h"
#include "constant.h"
#include "vid/vid.h"
#include "sprite.h"
#include "script/lgc_script.h"
#include "groups.h"
#include "player.h"
#include "base_sprite_list.h"
#include <map>
#include <memory>
#ifndef _WIN32
#include <filesystem>
#endif
#include <unordered_map>
#include <set>
#include <vector>

namespace as1
{
#ifdef _WIN32
    using GamePath = STRING;
#else
    using GamePath = std::filesystem::path;
#endif

    class GRAPH;
    namespace win { class ApplicationWin; }

    long double approximatePlanarDistance(float dx, float dy) noexcept;

    struct WEAPON
    {
        static constexpr size_t AS1_RECORD_SIZE = 0x264;
        std::array<BYTE, AS1_RECORD_SIZE> raw{};

        static WEAPON fromAS1Record(const BYTE* data, size_t size);
    };
                                                                                                                 

    struct SFX
    {
        std::vector<BYTE> raw;
        BYTE controlByte = 0;
        std::vector<STRING> wavNames;
        std::vector<STRING> missingFiles;

        bool isNull() const;
        static SFX fromAS1Record(const BYTE* data, size_t size, const GamePath& gameRoot);
    };
    struct MapSpriteRestoreRecord
    {
        int oldAddress = 0;
        SpriteRestoreState state;
        SPRITE* sprite = nullptr;
        bool attachedToSprite = false;
        std::vector<SPRITE*> resolvedObjectRefs;
        size_t resolvedCommandArgRefs = 0;
    };
    class RelationTable
    {
    public:
        RelationTable() noexcept;
        ~RelationTable() noexcept;
        RelationTable(const RelationTable&) = delete;
        RelationTable& operator=(const RelationTable&) = delete;

        void append(int oldSpriteAddr, SPRITE* newSpritePtr);
        SPRITE* getPointer(int oldSpriteAddr) const noexcept;
        int getIndex(int oldSpriteAddr) const noexcept;
        void clear() noexcept;
        size_t size() const noexcept { return static_cast<size_t>(m_old.count); }

    private:
        struct RawList
        {
            DWORD vtable;
            int count;      // +0x04
            int capacity;   // +0x08
            DWORD items;
        };

        RawList m_old;      // +0x00 old-address handles
        RawList m_new;      // +0x10 resolved SPRITE* values

        static void initializeList(RawList& list) noexcept;
        static void destroyList(RawList& list) noexcept;
        static DWORD encodePointer(const void* pointer) noexcept;
        template <class T>
        static T* decodePointer(DWORD pointer) noexcept
        {
            return reinterpret_cast<T*>(static_cast<std::uintptr_t>(pointer));
        }
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                                               
#endif

    class MAP
    {
    public:
        explicit MAP(GRAPH* graph = nullptr);
        MAP(GRAPH* graph, STRING resourceRoot);
        ~MAP();
        static MAP* Current();

        void setResourceRoot(const STRING& root) { m_resourceRoot = root; }
        const STRING& resourceRoot() const { return m_resourceRoot; }
        void setObjectsResource(const STRING& name) { m_objectsResource = name; }
        const STRING& objectsResource() const { return m_objectsResource; }

        bool loadGameResources();
        bool ensureGameResourcesLoaded();
        bool loadGameResourcesFromResource(RESOURCE* res, bool loadSfxTable, bool loadConstantsBlock);
        bool hostLoadVidDepot(RESOURCE* res);
        bool createStartupSpriteHashMap();
        bool reloadGameResourceParameters();
        bool reloadGameResourceParametersFromResource(RESOURCE* res);
        void LoadWeapon(RESOURCE* res);
        void LoadSfx(RESOURCE* res);
        void LoadConstants(RESOURCE* res);
        const BASE_CONSTANTS& Constants() const { return m_constants; }
        bool loadVids(RESOURCE* res, bool initialLoad, bool extra);

        void finishResourcesLoad();
        void invalidateFontVidDeviceObjects() noexcept;
        void restoreFontVidDeviceObjects() noexcept;
        void resetGameResourceRuntime();
        size_t noVid() const;
        size_t noWeapon() const { const int count = weaponCount(); return count > 0 ? static_cast<size_t>(count) : 0u; }
        const WEAPON* weapons() const { return weaponTable(); }
        int weaponCount() const noexcept
        {
#ifdef _WIN32
            return core::ApplicationWeaponCount();
#else
            return m_weaponCount;
#endif
        }
        WEAPON* weaponTable() noexcept
        {
#ifdef _WIN32
            return core::ApplicationWeaponTable();
#else
            return m_weaponTable;
#endif
        }
        const WEAPON* weaponTable() const noexcept
        {
#ifdef _WIN32
            return core::ApplicationWeaponTable();
#else
            return m_weaponTable;
#endif
        }
        const std::vector<SFX>& sfx() const { return m_sfx; }
        size_t noSfx() const { return m_sfx.size(); }
        const SFX* Sfx(int nsfx) const;
        size_t noSprite() const { return m_sprites.size(); }
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
        size_t noSpriteRecord() const { return m_spriteRecordCount; }
        size_t noNullSpriteRecord() const { return m_nullSpriteRecordCount; }
        size_t noMissingVidSpriteRecord() const { return m_missingVidSpriteRecordCount; }
        size_t noInvalidSpriteRecord() const { return m_invalidSpriteRecordCount; }
        size_t noRestoreEntry() const { return m_restoreEntries.size(); }
        size_t noAttachedRestoreEntry() const { return m_attachedRestoreEntries; }
        size_t noRestoreCommand() const { return m_restoreCommandCount; }
        size_t noAppliedRestoreState() const { return m_appliedRestoreStates; }
        size_t noActionStackSprite() const { return m_actionStackSprites; }
        size_t noResolvedRestoreObjectRef() const { return m_resolvedRestoreObjectRefs; }
        size_t noUnresolvedRestoreObjectRef() const { return m_unresolvedRestoreObjectRefs; }
        const std::map<std::string, size_t>& restoreLayoutCounts() const { return m_restoreLayoutCounts; }
        size_t noPlayerSlot() const { return m_playerSlots.size(); }
#endif
        size_t noGroup() const { return groupOwner().size(); }
        int version() const { return m_version; }
        DWORD currentTime() const { return m_currentTime; }
        const VECTOR2& sizeXY() const { return m_sizeXY; }
        const VECTOR2& shiftXY() const { return m_shiftXY; }
        const VECTOR2& originalShiftXY() const { return m_originalShiftXY; }

        float SizeX() const { return m_sizeXY.x; }
        float SizeY() const { return m_sizeXY.y; }
        float ToScreenX(float x) const;
        float ToScreenY(float y, float z = 0.0f) const;
        VECTOR2 ToScreenScaled(const VECTOR& world) const;
        float ToScreenScaledShiftX(float x, float shiftX, float scale) const;
        float ToScreenScaledShiftY(float y, float z, float shiftY, float scale) const;
        float FromScreenScaledShiftX(float x, float shiftX, float scale) const;
        float FromScreenScaledShiftY(float y, float z, float shiftY, float scale) const;
        void SetScrollBox(float minX, float minY, float maxX, float maxY);

        void SetShiftCoor(float centerX, float centerY, int effect = 0);
        void SetShiftCoor(const VECTOR2& center, int effect = 0) { SetShiftCoor(center.x, center.y, effect); }
        int noGridX() const { return terrainGridWidth(); }
        int noGridY() const { return terrainGridHeight(); }
#ifdef _WIN32
        const short* gridZ() const { return terrainGrid(); }
#else
        const std::vector<short>& gridZ() const { return m_gridZ; }
#endif
        const std::vector<std::unique_ptr<SPRITE>>& sprites() const { return m_sprites; }
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
        const std::vector<SPRITE*>& playerSprites() const { return m_playerSprites; }
        const std::vector<MapSpriteRestoreRecord>& spriteRestoreRecords() const { return m_restoreEntries; }
#endif
        VID* hostCreateVid(RESOURCE* res, int nvid);
        VID* createVIDByType(WORD type, DWORD spriteClass) const;
        VID* createVIDByType(VID::VidType type, bool parentPreloaded, bool letterAtlasAutoCreation) const;
        VID* Vid(int nvid) const;
        bool ValidateVid(int nvid) const
        {
            return HasVidSlot(nvid);
        }
        GamePath resolveGameFile(const STRING& gamePath) const;

        void DrawSpriteNumberLabels(GRAPH& graph) const;
        void DrawOverlaySpriteList() const;

        void DrawDebugTerrainGrid(GRAPH& graph) const;
        void DrawDebugCurrentSprite(GRAPH& graph, const SPRITE* sprite) const;
        void DrawDebugScrollBox(GRAPH& graph) const;
        void DrawDebugAuxiliaryList(GRAPH& graph) const;
        SPRITE* DebugCurrentSprite() const;

        void load(const STRING& name);
        void saveMapHost(const STRING& outputName);
        void hostReleaseWorldRuntime();
        // Portable fallback only; Win32 canonical clearSpriteReferencesAcrossRuntime is ApplicationWin.
        void releaseSpriteReferencesHost(SPRITE* sprite);
        void hostReleaseLinkVidRuntime();
        bool startLoadMap(RESOURCE* map);
        void loadGridZ(RESOURCE* map);
        int hostReinitializeGridFromMapSize();
        bool loadSprites(RESOURCE* map);
        bool loadSpriteRestoreData(RESOURCE* map);
        int spriteRestoreActionOpcode() const;
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
        SpriteRestoreState decodeSpriteRestoreData(int oldAddress, const std::vector<BYTE>& payload) const;
#endif
        bool loadPlayers(RESOURCE* map);
        bool loadGroups(RESOURCE* map);
        void loadScript();
        void processScriptFunctions();
        void runRetailPostLoadScriptPasses();
        const SCRIPT& script() const;
        SCRIPT& scriptRuntime();
        int ExecFunc(int opcode);
        int scriptPopIntegerRetail();
        VID* scriptPopVidRetail(const char* errorContext);
        void scriptPushIntegerRetail(int value);
        void scriptPushSpriteReferenceRetail(int value);
        RESOURCE& demoResource() noexcept;
        const RESOURCE& demoResource() const noexcept;
        SPRITE* OldLoadSprite(BaseStream* res);
        SPRITE* LoadSprite(BaseStream* res, int version);
        void CreateEmptyHardwareGround();
        SPRITE* CreateSprite(VID* vid, const VECTOR& v, const ANGLE& direction, SPRITE* parent = nullptr, bool remoteControlled = false, bool createChildRoute = true);
        void SetFlagman(int playerIndex, SPRITE* sprite) noexcept;
        SPRITE* flagmanSpriteForPlayer(int playerIndex) const noexcept;
        bool ReleaseSpriteForScalarDeletingDestructor(SPRITE* sprite);
        bool ReleaseVidForScalarDeletingDestructor(VID* vid);
        SPRITE* CreateSpriteViaFactory(VID* vid, const VECTOR& v, const ANGLE& direction, SPRITE* parent = nullptr, bool remoteControlled = false);
        SPRITE* readSpriteRelationHandle(BaseStream* stream) const;
        SPRITE* ReadSpriteHandle(BaseStream* stream, int* oldAddress = nullptr) const;
        SPRITE* ResolveOldSpriteHandle(int oldAddress) const;
        SPRITE* ResolveRelationHandle(int oldAddress) const noexcept;
        GROUPS& groupOwner() noexcept;
        const GROUPS& groupOwner() const noexcept;
        SPRITE* SpriteByOldAddress(int oldAddress) const;
        void BindLoadedSpriteHandle(int oldAddress, SPRITE* sprite);

        bool HasVidSlot(int nvid) const;
        VID* VidOrNull(int nvid) const;
        void swapVidReferences(VID* first, VID* second);
        static VID* NullVid();

        float sampleTerrainHeight(float x, float y) const noexcept;
        float GetGroundZ(const VECTOR2& v) const;
        float GetGroundZ(const VID* vid, const VECTOR2& v, ANGLE dir) const;
        void ResetGroundZ();
        void setTerrainHeightAtWorldPosition(float x, float y, float z);
        void SetTempGroundZ(float x, float y, float z);
        void ClearTempGroundZ(float x, float y, float z);

    private:
        friend class win::ApplicationWin;
        void clearLoadedMapRuntime();
        bool loadMapResourceSections(RESOURCE& mapResource);
        void installScriptNativeContext();
        void reinitSpritesCollector();
        void reinitSpritesCollectorHash();
        void deleteSpritesCollectorHash();
        void bindWeaponForVid(VID* vid);
        short* terrainGrid() noexcept;
        const short* terrainGrid() const noexcept;
        int terrainGridWidth() const noexcept;
        int terrainGridHeight() const noexcept;
        void setTerrainGridDimensions(int x, int y) noexcept;
        void replaceTerrainGridStorage(short* grid) noexcept;
        void releaseTerrainGridStorage() noexcept;
        void setWeaponTableState(int count, WEAPON* table) noexcept;
        RelationTable& relationTable() noexcept;
        const RelationTable& relationTable() const noexcept;

        GRAPH* m_graph = nullptr;
        STRING m_fileName;
        STRING m_resourceRoot{"."};
        STRING m_objectsResource{"objects.res"};
        std::vector<std::unique_ptr<VID>> m_vids;
        std::vector<std::unique_ptr<SPRITE>> m_sprites;
#ifndef _WIN32
        int m_weaponCount = 0;
        WEAPON* m_weaponTable = nullptr;
#endif
        std::vector<SFX> m_sfx;
        BASE_CONSTANTS m_constants;
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
        bool readSpriteRestoreSubResource(RESOURCE* map, MapSpriteRestoreRecord& out);
#endif

        int m_version = 0;
        bool m_useLegacyCompactSpriteRecords = false;
        DWORD m_currentTime = 0;
        VECTOR2 m_sizeXY;
        VECTOR2 m_scrollMinXY;
        VECTOR2 m_scrollMaxXY;
        VECTOR2 m_shiftXY;
        VECTOR2 m_originalShiftXY;
        VECTOR2 m_shiftDeltaXY;
#ifndef _WIN32
        int m_noGridX = 0;
        int m_noGridY = 0;
        std::vector<short> m_gridZ;
#endif
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
        size_t m_spriteRecordCount = 0;
        size_t m_nullSpriteRecordCount = 0;
        size_t m_missingVidSpriteRecordCount = 0;
        size_t m_invalidSpriteRecordCount = 0;
        std::vector<MapSpriteRestoreRecord> m_restoreEntries;
        size_t m_attachedRestoreEntries = 0;
        size_t m_restoreCommandCount = 0;
        size_t m_appliedRestoreStates = 0;
        size_t m_actionStackSprites = 0;
        size_t m_resolvedRestoreObjectRefs = 0;
        size_t m_unresolvedRestoreObjectRefs = 0;
        std::map<std::string, size_t> m_restoreLayoutCounts;
        std::vector<int> m_playerSlots;
        std::vector<SPRITE*> m_playerSprites;
#endif
#ifndef _WIN32
        GROUPS m_groupOwner;
#endif
#ifndef _WIN32
        RESOURCE m_demoResource;
        RelationTable m_relationTable;
#endif
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
        std::map<int, SPRITE*> m_spriteByOldAddress;
#endif
    };
}
