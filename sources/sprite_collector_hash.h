#pragma once

#include "core/types.h"
#include "base_sprite_list.h"
#include <cstddef>

namespace as1
{
    class MAP;
    class SPRITE;
    class VID;

    struct SpriteHashCollisionQuery
    {
        SPRITE* sprite = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct SpriteHashLineTrace
    {
        SPRITE* sprite = nullptr;
        VECTOR start;
        VECTOR end;
        VECTOR hit;
        bool hitFound = false;
    };

    class SPRITE_COLLECTOR_HASH_MAP
    {
    public:
        SPRITE_COLLECTOR_HASH_MAP(float mapWidth, float mapHeight, VID* const* vids, int vidCount);
        ~SPRITE_COLLECTOR_HASH_MAP();

        SPRITE_COLLECTOR_HASH_MAP(const SPRITE_COLLECTOR_HASH_MAP&) = delete;
        SPRITE_COLLECTOR_HASH_MAP& operator=(const SPRITE_COLLECTOR_HASH_MAP&) = delete;

        void reset(float mapWidth, float mapHeight, VID* const* vids, int vidCount);
        void clear();

        void addSprite(SPRITE* sprite);
        int removeSprite(SPRITE* sprite);
        void moveSprite(SPRITE* sprite, float newX, float newY);

        SPRITE* firstSpriteInBox(float minX, float minY, float maxX, float maxY);
        SPRITE* nextBoxQuerySprite();
        SPRITE* nextSpriteInBox();

        SPRITE* findSpriteCollisionAt(const MAP& map, SPRITE* sprite, float x, float y, float z);
        SPRITE* findCollisionAtPosition(const MAP& map, const VID* probeVid, float x, float y, float z);
        SPRITE* findVidCollisionAt(const MAP& map, const VID* probeVid, float x, float y, float z);
        bool traceSpriteMovementCollision(const MAP& map,
                                             SPRITE* sprite,
                                             const VECTOR& start,
                                             VECTOR& end,
                                             VECTOR& hit);
        bool traceVidMovementCollision(const MAP& map,
                                                  const VID* probeVid,
                                                  float startX,
                                                  float startY,
                                                  float startZ,
                                                  float* targetX,
                                                  float* targetY,
                                                  float* targetZ);

        int bucketWidth() const noexcept { return m_bucketWidth; }
        int bucketHeight() const noexcept { return m_bucketHeight; }
        int bucketRowShift() const noexcept { return m_bucketRowShift; }
        float inverseCellWidth() const noexcept { return m_inverseCellWidth; }
        float inverseCellHeight() const noexcept { return m_inverseCellHeight; }
        const SPRITE_POINTER_LIST& overflowList() const noexcept { return m_overflowList; }

        int reverseCursor() const noexcept { return m_reverseCursor; }
        void setReverseCursor(int value) noexcept { m_reverseCursor = value; }
        int* reverseCursorAddress() noexcept { return &m_reverseCursor; }
        SPRITE_POINTER_LIST& mutableOverflowList() noexcept { return m_overflowList; }
        const SPRITE_POINTER_LIST& mutableOverflowList() const noexcept { return m_overflowList; }
        int overflowCount() const noexcept { return m_overflowList.activeCount(); }
        SPRITE* overflowSpriteAt(int index) const noexcept
        {
            if (index < 0 || index >= m_overflowList.activeCount())
                return nullptr;
            SPRITE* const* const raw = m_overflowList.data();
            return raw ? raw[static_cast<std::size_t>(index)] : nullptr;
        }
        std::size_t bucketCount() const noexcept;

    private:
        friend class SPRITE;

        SPRITE_COLLECTOR_HASH_MAP* initializeHashGrid(float mapWidth, float mapHeight, VID* const* vids, int vidCount);
        void insertSpriteIntoHash(SPRITE* sprite);
        bool traceMovementCollision(const MAP& map, const VID* probeVid, float startX, float startY, float startZ, float* targetX, float* targetY, float* targetZ);
        SPRITE* beginBoxQuery(float minX, float minY, float maxX, float maxY);

        static bool hashEligible(const SPRITE* sprite) noexcept;
        static bool overflowEligible(const SPRITE* sprite) noexcept;
        static bool iteratorCandidateAllowed(const SPRITE* sprite) noexcept;
        static float objectHalfX(const SPRITE* sprite) noexcept;
        static float objectHalfY(const SPRITE* sprite) noexcept;
        static float objectTopZ(const SPRITE* sprite) noexcept;
        static DWORD objectMoveMask(const SPRITE* sprite) noexcept;
        static float objectHalfX(const VID* vid) noexcept;
        static float objectHalfY(const VID* vid) noexcept;
        static float objectTopZ(const VID* vid) noexcept;
        static DWORD objectMoveMask(const VID* vid) noexcept;
        void allocateBucketsFromMaxObjectSize(float mapWidth, float mapHeight, float maxObjectSizeX, float maxObjectSizeY);
        static int bucketTableElementCount(int bucketWidth, int bucketHeight) noexcept;
        int bucketTableElementCount() const noexcept;
        int bucketReleaseCount() const noexcept;
        static int ftolClamp(float scaled, int limit) noexcept;
        static int constructorPowerShiftFor(float value) noexcept;

        int cellX(float x) const noexcept;
        int cellY(float y) const noexcept;
        void setIteratorCellWindow(int minX, int minY, int maxX, int maxY) noexcept;
        void configureBoxQueryWindow(float minX, float minY, float maxX, float maxY) noexcept;
        static int bucketRecordByteOffsetFromIndex(int index) noexcept;
        static int bucketIndexFromRecordByteOffset(int byteOffset) noexcept;
        int bucketIndexForCell(int x, int y) const noexcept;
        int bucketRecordByteOffsetForCell(int x, int y) const noexcept;
        int bucketRowBase(int y) const noexcept;
        static int bucketRecordByteOffsetFromRowBaseAndColumn(int rowBase, int x) noexcept;
        void destroyBucketTableStorageWithDeletingDestructorFlags(unsigned deletingDestructorFlags) noexcept;
        void destroyOverflowListBackingStorageRoute() noexcept;
        SPRITE_POINTER_LIST* bucketAtByteOffsetUnchecked(int byteOffset) noexcept;
        const SPRITE_POINTER_LIST* bucketAtByteOffsetUnchecked(int byteOffset) const noexcept;
        SPRITE_POINTER_LIST* bucketAtCellBoundary(int x, int y) noexcept;
        const SPRITE_POINTER_LIST* bucketAtCellBoundary(int x, int y) const noexcept;
        SPRITE_POINTER_LIST* bucketAt(int x, int y) noexcept;
        const SPRITE_POINTER_LIST* bucketAt(int x, int y) const noexcept;

        int m_queryMinX;
        int m_queryRow;
        int m_queryMaxX;
        int m_queryMaxY;
        int m_queryColumn;
        int m_querySpriteIndex;
        int m_reverseCursor;
        int m_bucketRowShift;
        int m_bucketWidth;
        int m_bucketHeight;
        float m_inverseCellWidth;
        float m_inverseCellHeight;
        SPRITE_POINTER_LIST* m_bucketTable;
        SPRITE_POINTER_LIST m_overflowList;
    };


#if INTPTR_MAX == INT32_MAX
                                                                                                                       
#endif

    SPRITE_COLLECTOR_HASH_MAP*& GlobalSpriteHashMapSlot();
    SPRITE_COLLECTOR_HASH_MAP* GlobalSpriteHashMap();
    void SetGlobalSpriteHashMap(SPRITE_COLLECTOR_HASH_MAP* value);

    void DeleteGlobalSpriteHashMap();
    void DestroyGlobalSpriteHashMapForApplicationDestructor();
    bool ReinitGlobalSpriteHashMapFromVidTable(float mapWidth, float mapHeight, VID* const* vids, int vidCount);

    SPRITE* GlobalHashFirstInBoxAroundDot(float minX, float minY, float maxX, float maxY);
    SPRITE* GlobalHashNextInBoxAroundDot();
    SPRITE* GlobalHashQueryCellCollision(const MAP& map, SPRITE* sprite, float x, float y, float z);
    SPRITE* GlobalHashQueryCellCollisionByVid(const MAP& map, const VID* probeVid, float x, float y, float z);

    bool RemoveSpriteFromGlobalHashForActionSwitch(SPRITE* sprite);
    bool AddSpriteToGlobalHashForActionSwitch(SPRITE* sprite);
    int RemoveSpriteChainFromGlobalHashForActionSwitch(SPRITE* first, SPRITE* (*nextSprite)(SPRITE*));
    bool GlobalHashLineTrace(const MAP& map, SPRITE* sprite, const VECTOR& start, VECTOR& end, VECTOR& hit);
    bool GlobalHashLineTraceByVid(const MAP& map, const VID* probeVid, VECTOR& start, VECTOR& end);

    bool SpriteHashDepoCanCreateUnitFilter(const SPRITE* candidate);
}
