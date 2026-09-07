#include "sprite_collector_hash.h"

#include "core/log.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "vid/vid.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace as1
{
    namespace
    {
        SPRITE_COLLECTOR_HASH_MAP* g_spriteHashMap = nullptr;

        int kDepoCanCreateUnitNvids[] = {
            0x000000AA, 0x00000323, 0x00000325, 0x00000326,
            0x00000327, 0x00000329, 0x0000032A, 0x0000032D,
            0x0000034E, 0x00000350, 0x0000035D, 0x0000035F,
            0x00000360, 0x0000036A, 0x0000036B, 0x0000037F,
        };

        constexpr int kBucketListRecordStride = 0x10;
        constexpr unsigned short kX87ConditionLess = 0x0100u;
        constexpr unsigned short kX87ConditionEqual = 0x4000u;
        constexpr unsigned short kX87ConditionUnordered = 0x4500u;
        constexpr unsigned short kX87LessOrEqualOrUnorderedMask =
            kX87ConditionLess | kX87ConditionEqual;
        constexpr std::uint32_t kMovementTraceSampleMask = 0x0Fu; // sample every 16 integer steps

        int x87FloatToInt(float value) noexcept
        {
            const double d = static_cast<double>(value);
            if (!std::isfinite(d) || d >= 9223372036854775808.0 || d < -9223372036854775808.0)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        unsigned short x87CompareStatus(float lhs, float rhs) noexcept
        {
            // x87 C3:C2:C0 encoding: greater=000, less=001, equal=100,
            // unordered=111.  Only those status bits are consumed here.
            if (std::isnan(lhs) || std::isnan(rhs))
                return kX87ConditionUnordered;
            if (lhs < rhs)
                return kX87ConditionLess;
            if (lhs == rhs)
                return kX87ConditionEqual;
            return 0;
        }

        bool x87LessThanOrUnordered(float lhs, float rhs) noexcept
        {
            // `test ah,1`: C0 set for less-than and unordered.
            return (x87CompareStatus(lhs, rhs) & kX87ConditionLess) != 0u;
        }

        bool x87LessOrEqualOrUnordered(float lhs, float rhs) noexcept
        {
            // `test ah,41h`: C0/C3 set for less/equal/unordered.
            return (x87CompareStatus(lhs, rhs) & kX87LessOrEqualOrUnorderedMask) != 0u;
        }

        int x87FloatToIntClamped(float scaled, int limit) noexcept
        {
            if (x87LessThanOrUnordered(scaled, 0.0f))
                return 0;
            if (x87LessOrEqualOrUnordered(static_cast<float>(limit), scaled))
                return limit - 1;
            return x87FloatToInt(scaled);
        }

        int x87MultiplyToIntClamped(float value, float scale, int limit) noexcept
        {
            return x87FloatToIntClamped(value * scale, limit);
        }

        float x87ReciprocalStored(float divisor) noexcept
        {
            return 1.0f / divisor;
        }

        int x87SubtractMultiplyToIntClamped(float value, float subtractValue, float scale, int limit) noexcept
        {
            return x87FloatToIntClamped((value - subtractValue) * scale, limit);
        }

        int x87AddMultiplyToIntClamped(float value, float addValue, float scale, int limit) noexcept
        {
            return x87FloatToIntClamped((value + addValue) * scale, limit);
        }

        int x87SubtractReciprocalMultiplyToIntClamped(float value, float divisor, int limit) noexcept
        {
            const float reciprocal = 1.0f / divisor;
            return x87FloatToIntClamped((value - reciprocal) * divisor, limit);
        }

        int x87AddReciprocalMultiplyToIntClamped(float value, float divisor, int limit) noexcept
        {
            const float reciprocal = 1.0f / divisor;
            return x87FloatToIntClamped((value + reciprocal) * divisor, limit);
        }

        float inversePowerOfTwoX87(int shift) noexcept
        {
            const std::int32_t base = static_cast<std::int32_t>(std::uint32_t{1} << (shift & 31));
            return 1.0f / static_cast<float>(base);
        }

        int computeHashHeightCellsX87(float mapHeight, float inverseCellHeight) noexcept
        {
            return x87FloatToInt((mapHeight - 1.0f) * inverseCellHeight + 3.0f);
        }

        int computeHashWidthShiftX87(float mapWidth, float inverseCellWidth) noexcept
        {
            const float widthScaled = (mapWidth - 1.0f) * inverseCellWidth + 1.0f;
            int shift = 0;
            while (x87LessThanOrUnordered(
                       static_cast<float>(static_cast<std::int32_t>(std::uint32_t{1} << (shift & 31))),
                       widthScaled))
                ++shift;
            return shift;
        }

        std::uint32_t abs32Wrap(std::int32_t value) noexcept
        {
            const std::uint32_t raw = static_cast<std::uint32_t>(value);
            const std::uint32_t sign = raw >> 31u;
            return (raw ^ (0u - sign)) + sign;
        }


        std::int32_t wrapAdd32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        std::int32_t wrapSub32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        std::int32_t signedDivide32(std::int32_t numerator, std::int32_t denominator) noexcept
        {
            if (denominator == 0)
                return 0;
            if (numerator == static_cast<std::int32_t>(0x80000000u) && denominator == -1)
                return static_cast<std::int32_t>(0x80000000u);
            return numerator / denominator;
        }

        float x87IntToFloat(std::int32_t value) noexcept
        {
            return static_cast<float>(value);
        }
    }

    SPRITE_COLLECTOR_HASH_MAP::SPRITE_COLLECTOR_HASH_MAP(float mapWidth,
                                                         float mapHeight,
                                                         VID* const* vids,
                                                         int vidCount)
    {
        (void)initializeHashGrid(mapWidth, mapHeight, vids, vidCount);
    }

    SPRITE_COLLECTOR_HASH_MAP::~SPRITE_COLLECTOR_HASH_MAP()
    {

        clear();
    }

    void SPRITE_COLLECTOR_HASH_MAP::reset(float mapWidth,
                                          float mapHeight,
                                          VID* const* vids,
                                          int vidCount)
    {
        (void)initializeHashGrid(mapWidth, mapHeight, vids, vidCount);
    }

    SPRITE_COLLECTOR_HASH_MAP* SPRITE_COLLECTOR_HASH_MAP::initializeHashGrid(float mapWidth,
                                                                    float mapHeight,
                                                                    VID* const* vids,
                                                                    int vidCount)
    {
        m_querySpriteIndex = 0;
        m_reverseCursor = 0;

        float maxObjectSizeX = 0.0f;
        float maxObjectSizeY = 0.0f;
        if (vidCount > 0)
        {
            for (int i = 0; i < vidCount; ++i)
            {
                VID* const vid = vids[i];
                if (vid && (vid->properties() & P_HASH) != 0)
                {
                    const float sizeX = vid->sizeX();
                    const float sizeY = vid->sizeY();
                    if (!x87LessOrEqualOrUnordered(sizeX, maxObjectSizeX))
                        maxObjectSizeX = sizeX;
                    if (!x87LessOrEqualOrUnordered(sizeY, maxObjectSizeY))
                        maxObjectSizeY = sizeY;
                }
            }
        }

        allocateBucketsFromMaxObjectSize(mapWidth, mapHeight, maxObjectSizeX, maxObjectSizeY);
        return this;
    }

    void SPRITE_COLLECTOR_HASH_MAP::allocateBucketsFromMaxObjectSize(float mapWidth,
                                                                     float mapHeight,
                                                                     float maxObjectSizeX,
                                                                     float maxObjectSizeY)
    {
        // initializeHashGrid: halve maximum hash VID dimensions, choose independent
        // power-of-two X/Y cell scales, then allocate width*height inline
        // 0x10-byte core::List records preceded by a DWORD element cookie.
        const float halfMaxX = maxObjectSizeX * 0.5f;
        const float halfMaxY = maxObjectSizeY * 0.5f;
        const int shiftX = constructorPowerShiftFor(halfMaxX);
        const int shiftY = constructorPowerShiftFor(halfMaxY);
        m_inverseCellWidth = inversePowerOfTwoX87(shiftX);
        m_inverseCellHeight = inversePowerOfTwoX87(shiftY);

        m_bucketHeight = computeHashHeightCellsX87(mapHeight, m_inverseCellHeight);
        m_bucketRowShift = computeHashWidthShiftX87(mapWidth, m_inverseCellWidth);
        m_bucketWidth = static_cast<int>(std::uint32_t{1} << (m_bucketRowShift & 31));

        const int total = bucketTableElementCount();
        m_bucketTable = nullptr;

        const std::uint32_t bytes32 = static_cast<std::uint32_t>(total) * 16u + 4u;
        unsigned char* raw = static_cast<unsigned char*>(
            ::operator new(static_cast<std::size_t>(bytes32), std::nothrow));
        if (!raw)
        {
            LOG::Write("!!!ERROR!!!HASH_MAP: Enough memory %i,%i", m_bucketWidth, m_bucketHeight);
            return;
        }

        *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(total);
        m_bucketTable = reinterpret_cast<SPRITE_POINTER_LIST*>(raw + sizeof(std::uint32_t));
        for (int i = 0; i < total; ++i)
        {
            void* const record = raw + sizeof(std::uint32_t) + static_cast<std::size_t>(i) * 0x10u;
            new (record) SPRITE_POINTER_LIST();
        }
    }

    std::size_t SPRITE_COLLECTOR_HASH_MAP::bucketCount() const noexcept
    {
        if (!m_bucketTable)
            return 0u;
        const unsigned char* first = reinterpret_cast<const unsigned char*>(m_bucketTable);
        const int count = static_cast<int>(*reinterpret_cast<const std::uint32_t*>(first - sizeof(std::uint32_t)));
        return count > 0 ? static_cast<std::size_t>(count) : 0u;
    }

    void SPRITE_COLLECTOR_HASH_MAP::clear()
    {

        m_overflowList.releaseAllReferences();

        const int bucketCount = bucketReleaseCount();
        for (int byteOffset = bucketRecordByteOffsetFromIndex(bucketCount - 1);
             byteOffset >= 0;
             byteOffset -= kBucketListRecordStride)
        {
            bucketAtByteOffsetUnchecked(byteOffset)->releaseAllReferences();
        }

        destroyBucketTableStorageWithDeletingDestructorFlags(3u);

        destroyOverflowListBackingStorageRoute();
    }

    void SPRITE_COLLECTOR_HASH_MAP::destroyBucketTableStorageWithDeletingDestructorFlags(unsigned deletingDestructorFlags) noexcept
    {

        if (!m_bucketTable)
            return;
        SPRITE_POINTER_LIST* const table = m_bucketTable;
        m_bucketTable = nullptr;
        table->pointerListDeletingDestructor(static_cast<unsigned char>(deletingDestructorFlags));
    }

    void SPRITE_COLLECTOR_HASH_MAP::destroyOverflowListBackingStorageRoute() noexcept
    {

        m_overflowList.destroyCoreListStorage();
    }

    void SPRITE_COLLECTOR_HASH_MAP::addSprite(SPRITE* sprite)
    {
        insertSpriteIntoHash(sprite);
    }

    void SPRITE_COLLECTOR_HASH_MAP::insertSpriteIntoHash(SPRITE* sprite)
    {
        if (hashEligible(sprite))
        {

            const int x = cellX(sprite->X());
            const int y = cellY(sprite->Y());
            const int byteOffset = bucketRecordByteOffsetForCell(x, y);
            bucketAtByteOffsetUnchecked(byteOffset)->append(sprite);
        }

        if (overflowEligible(sprite))
            m_overflowList.append(sprite);
    }

    int SPRITE_COLLECTOR_HASH_MAP::removeSprite(SPRITE* sprite)
    {

        int errorMask = 0;

        if (m_bucketTable && hashEligible(sprite))
        {
            const int x = cellX(sprite->X());
            const int y = cellY(sprite->Y());

            const int byteOffset = bucketRecordByteOffsetForCell(x, y);
            SPRITE_POINTER_LIST* const list = bucketAtByteOffsetUnchecked(byteOffset);

            if (m_queryColumn == x && m_queryRow == y && m_querySpriteIndex > 0 &&
                m_querySpriteIndex < list->activeCount() &&
                list->data()[m_querySpriteIndex - 1] == sprite)
            {
                --m_querySpriteIndex;
            }
            if (list->removeAndReleaseReference(sprite) != 0)
                errorMask |= 1;
        }

        if (overflowEligible(sprite))
        {
            if (m_overflowList.removeAndReleaseReference(sprite) != 0)
                errorMask |= 2;
        }

        if (errorMask != 0)
        {

            SPRITE* const ground = mouseSprite();
            SPRITE* const groundChild = ground->childChain();
            if (sprite != ground && sprite != groundChild)
            {
                const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
                LOG::ResourceError("SPRITE %i", 10, "hash can't delete", errorMask, nvid);
            }
        }
        return errorMask;
    }

    void SPRITE_COLLECTOR_HASH_MAP::moveSprite(SPRITE* sprite, float newX, float newY)
    {

        const int oldX = cellX(sprite->X());
        const int oldY = cellY(sprite->Y());
        const int nextX = cellX(newX);
        const int nextY = cellY(newY);
        if (oldX == nextX && oldY == nextY)
            return;

        const int oldByteOffset = bucketRecordByteOffsetForCell(oldX, oldY);
        SPRITE_POINTER_LIST* const oldBucket = bucketAtByteOffsetUnchecked(oldByteOffset);
        if (oldBucket->removeAndReleaseReference(sprite) != 0)
            return;

        const int newByteOffset = bucketRecordByteOffsetForCell(nextX, nextY);
        bucketAtByteOffsetUnchecked(newByteOffset)->append(sprite);
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::firstSpriteInBox(float minX, float minY, float maxX, float maxY)
    {
        return beginBoxQuery(minX, minY, maxX, maxY);
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::beginBoxQuery(float minX, float minY, float maxX, float maxY)
    {
        configureBoxQueryWindow(minX, minY, maxX, maxY);
        return nextBoxQuerySprite();
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::nextBoxQuerySprite()
    {

        while (m_queryRow <= m_queryMaxY)
        {
            const int rowBase = bucketRowBase(m_queryRow);
            while (m_queryColumn <= m_queryMaxX)
            {
                const int byteOffset = bucketRecordByteOffsetFromRowBaseAndColumn(rowBase, m_queryColumn);
                SPRITE_POINTER_LIST* const list = bucketAtByteOffsetUnchecked(byteOffset);
                if (m_querySpriteIndex < list->activeCount())
                {
                    const int cursor = m_querySpriteIndex++;
                    return list->data()[cursor];
                }

                ++m_queryColumn;
                m_querySpriteIndex = 0;
            }

            ++m_queryRow;
            m_queryColumn = m_queryMinX;
            m_querySpriteIndex = 0;
        }
        return nullptr;
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::nextSpriteInBox()
    {
        return nextBoxQuerySprite();
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::findSpriteCollisionAt(const MAP& map, SPRITE* sprite, float x, float y, float z)
    {
        return sprite ? findVidCollisionAt(map, sprite->Vid(), x, y, z) : nullptr;
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::findCollisionAtPosition(const MAP& map, const VID* probeVid, float x, float y, float z)
    {

        if (map.GetGroundZ(probeVid, VECTOR2{x, y}, ANGLE{}) > z)
            return mouseSprite();

        const DWORD probeMask = objectMoveMask(probeVid);
        if (probeMask == 0)
            return nullptr;

        const float hx = objectHalfX(probeVid);
        const float hy = objectHalfY(probeVid);
        for (SPRITE* candidate = firstSpriteInBox(x - hx, y - hy, x + hx, y + hy);
             candidate;
             candidate = nextSpriteInBox())
        {
            if (!iteratorCandidateAllowed(candidate))
                continue;
            const VID* const cvid = candidate->Vid();
            const float absDx = std::fabs(candidate->X() - x);
            const float sumHalfX = objectHalfX(cvid) + hx;
            if (x87LessOrEqualOrUnordered(sumHalfX, absDx))
                continue;
            const float absDy = std::fabs(candidate->Y() - y);
            const float sumHalfY = objectHalfY(cvid) + hy;
            if (x87LessOrEqualOrUnordered(sumHalfY, absDy))
                continue;
            const float candidateTop = candidate->Z() + objectTopZ(cvid);
            if (x87LessThanOrUnordered(candidateTop, z))
                continue;
            const float probeTop = z + objectTopZ(probeVid);
            if (x87LessThanOrUnordered(probeTop, candidate->Z()))
                continue;
            if ((objectMoveMask(cvid) & probeMask) == 0)
                continue;
            return candidate;
        }
        return nullptr;
    }

    SPRITE* SPRITE_COLLECTOR_HASH_MAP::findVidCollisionAt(const MAP& map, const VID* probeVid, float x, float y, float z)
    {
        return findCollisionAtPosition(map, probeVid, x, y, z);
    }

    bool SPRITE_COLLECTOR_HASH_MAP::traceSpriteMovementCollision(const MAP& map,
                                                                     SPRITE* sprite,
                                                                     const VECTOR& start,
                                                                     VECTOR& end,
                                                                     VECTOR& hit)
    {
        if (!sprite)
            return false;
        float targetX = end.x;
        float targetY = end.y;
        float targetZ = end.z;
        if (!traceVidMovementCollision(map, sprite->Vid(), start.x, start.y, start.z, &targetX, &targetY, &targetZ))
        {
            hit = VECTOR{};
            return false;
        }
        hit = VECTOR{targetX, targetY, targetZ};
        end = hit;
        return true;
    }

    bool SPRITE_COLLECTOR_HASH_MAP::traceVidMovementCollision(const MAP& map,
                                                                         const VID* probeVid,
                                                                         float startX,
                                                                         float startY,
                                                                         float startZ,
                                                                         float* targetX,
                                                                         float* targetY,
                                                                         float* targetZ)
    {
        return traceMovementCollision(map, probeVid, startX, startY, startZ, targetX, targetY, targetZ);
    }

    bool SPRITE_COLLECTOR_HASH_MAP::traceMovementCollision(const MAP& map,
                                               const VID* probeVid,
                                               float startX,
                                               float startY,
                                               float startZ,
                                               float* targetX,
                                               float* targetY,
                                               float* targetZ)
    {

        if (!probeVid || objectMoveMask(probeVid) == 0)
            return false;

        std::int32_t x = x87FloatToInt(startX);
        std::int32_t y = x87FloatToInt(startY);
        std::int32_t currentZ = x87FloatToInt(startZ);
        const std::int32_t targetXi = x87FloatToInt(*targetX);
        const std::int32_t targetYi = x87FloatToInt(*targetY);
        const std::int32_t targetZi = x87FloatToInt(*targetZ);

        std::uint32_t major = abs32Wrap(wrapSub32(targetXi, x));
        std::uint32_t minor = abs32Wrap(wrapSub32(targetYi, y));
        std::int32_t sx = x87LessOrEqualOrUnordered(*targetX, startX) ? -1 : 1;
        std::int32_t sy = x87LessOrEqualOrUnordered(*targetY, startY) ? -1 : 1;
        bool swapped = false;
        if (minor > major)
        {
            std::swap(x, y);
            std::swap(major, minor);
            std::swap(sx, sy);
            swapped = true;
        }

        std::int32_t zStepPer16 = 0;
        if (major != 0u)
        {
            const std::uint32_t rawDeltaZ = static_cast<std::uint32_t>(targetZi) -
                                            static_cast<std::uint32_t>(currentZ);
            const std::int32_t scaledDeltaZ = static_cast<std::int32_t>(rawDeltaZ << 4u);
            zStepPer16 = signedDivide32(scaledDeltaZ, static_cast<std::int32_t>(major));
        }

        const std::int32_t errStep = static_cast<std::int32_t>(minor << 1u);
        std::int32_t err = wrapSub32(errStep, static_cast<std::int32_t>(major));
        for (std::uint32_t i = 0; i < major; ++i)
        {
            if ((i & kMovementTraceSampleMask) == 0u && i != 0u)
            {
                currentZ = wrapAdd32(currentZ, zStepPer16);
                const float qx = x87IntToFloat(swapped ? y : x);
                const float qy = x87IntToFloat(swapped ? x : y);
                const float qz = x87IntToFloat(currentZ);
                if (findVidCollisionAt(map, probeVid, qx, qy, qz))
                {
                    if (swapped)
                    {
                        *targetX = x87IntToFloat(y);
                        *targetY = x87IntToFloat(x);
                    }
                    else
                    {
                        *targetX = x87IntToFloat(x);
                        *targetY = x87IntToFloat(y);
                    }
                    *targetZ = x87IntToFloat(currentZ);
                    return true;
                }
            }

            if (err >= 0)
            {
                const std::int32_t twiceMajor = static_cast<std::int32_t>(major << 1u);
                do
                {
                    y = wrapAdd32(y, sy);
                    err = wrapSub32(err, twiceMajor);
                }
                while (err >= 0);
            }
            x = wrapAdd32(x, sx);
            err = wrapAdd32(err, errStep);
        }
        return false;
    }

    bool SPRITE_COLLECTOR_HASH_MAP::iteratorCandidateAllowed(const SPRITE* sprite) noexcept
    {

        return sprite && sprite->currentAnimation() < 0x0F;
    }

    bool SPRITE_COLLECTOR_HASH_MAP::hashEligible(const SPRITE* sprite) noexcept
    {
        const VID* const vid = sprite->Vid();
        return (vid->properties() & P_HASH) != 0;
    }

    bool SPRITE_COLLECTOR_HASH_MAP::overflowEligible(const SPRITE* sprite) noexcept
    {
        const VID* const vid = sprite->Vid();

        return (vid->spriteTypeId() & 0x0Cu) != 0u;
    }

    float SPRITE_COLLECTOR_HASH_MAP::objectHalfX(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectHalfX(vid);
    }

    float SPRITE_COLLECTOR_HASH_MAP::objectHalfY(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectHalfY(vid);
    }

    float SPRITE_COLLECTOR_HASH_MAP::objectTopZ(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectTopZ(vid);
    }

    DWORD SPRITE_COLLECTOR_HASH_MAP::objectMoveMask(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectMoveMask(vid);
    }

    float SPRITE_COLLECTOR_HASH_MAP::objectHalfX(const VID* vid) noexcept
    {

        return vid ? vid->halfSizeX() : 0.0f;
    }

    float SPRITE_COLLECTOR_HASH_MAP::objectHalfY(const VID* vid) noexcept
    {

        return vid ? vid->halfSizeY() : 0.0f;
    }

    float SPRITE_COLLECTOR_HASH_MAP::objectTopZ(const VID* vid) noexcept
    {

        return vid ? vid->sizeZ() : 0.0f;
    }

    DWORD SPRITE_COLLECTOR_HASH_MAP::objectMoveMask(const VID* vid) noexcept
    {

        return vid ? vid->movementMask() : 0;
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketTableElementCount(int bucketWidth, int bucketHeight) noexcept
    {

        const std::int32_t h = static_cast<std::int32_t>(bucketHeight);
        const std::int32_t w = static_cast<std::int32_t>(bucketWidth);
        const std::uint32_t raw = static_cast<std::uint32_t>(h) * static_cast<std::uint32_t>(w);
        return static_cast<int>(raw);
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketTableElementCount() const noexcept
    {
        return bucketTableElementCount(m_bucketWidth, m_bucketHeight);
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketReleaseCount() const noexcept
    {

        return m_bucketTable ? bucketTableElementCount() : 0;
    }

    int SPRITE_COLLECTOR_HASH_MAP::ftolClamp(float scaled, int limit) noexcept
    {
        return x87FloatToIntClamped(scaled, limit);
    }

    int SPRITE_COLLECTOR_HASH_MAP::constructorPowerShiftFor(float value) noexcept
    {

        int shift = 0;
        while (x87LessThanOrUnordered(
                   static_cast<float>(static_cast<std::int32_t>(std::uint32_t{1} << (shift & 31))),
                   value))
        {
            ++shift;
        }
        return shift;
    }

    int SPRITE_COLLECTOR_HASH_MAP::cellX(float x) const noexcept
    {

        return x87MultiplyToIntClamped(x, m_inverseCellWidth, m_bucketWidth);
    }

    int SPRITE_COLLECTOR_HASH_MAP::cellY(float y) const noexcept
    {
        return x87MultiplyToIntClamped(y, m_inverseCellHeight, m_bucketHeight);
    }

    void SPRITE_COLLECTOR_HASH_MAP::setIteratorCellWindow(int minX, int minY, int maxX, int maxY) noexcept
    {

        m_queryMinX = minX;
        m_queryRow = minY;
        m_queryMaxX = maxX;
        m_queryMaxY = maxY;
        m_queryColumn = m_queryMinX;
        m_querySpriteIndex = 0;
    }

    void SPRITE_COLLECTOR_HASH_MAP::configureBoxQueryWindow(float minX,
                                                                         float minY,
                                                                         float maxX,
                                                                         float maxY) noexcept
    {

        const float cellW = x87ReciprocalStored(m_inverseCellWidth);

        const int rawMinX = x87SubtractMultiplyToIntClamped(minX, cellW, m_inverseCellWidth, m_bucketWidth);
        const int rawMinY = x87SubtractReciprocalMultiplyToIntClamped(minY, m_inverseCellHeight, m_bucketHeight);
        const int rawMaxX = x87AddMultiplyToIntClamped(maxX, cellW, m_inverseCellWidth, m_bucketWidth);
        const int rawMaxY = x87AddReciprocalMultiplyToIntClamped(maxY, m_inverseCellHeight, m_bucketHeight);

        setIteratorCellWindow(rawMinX, rawMinY, rawMaxX, rawMaxY);
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketRecordByteOffsetFromIndex(int index) noexcept
    {

        return static_cast<int>(static_cast<std::uint32_t>(index) << 4u);
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketIndexFromRecordByteOffset(int byteOffset) noexcept
    {

        return byteOffset >> 4;
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketIndexForCell(int x, int y) const noexcept
    {

        return (y << m_bucketRowShift) + x;
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketRecordByteOffsetForCell(int x, int y) const noexcept
    {

        return bucketRecordByteOffsetFromRowBaseAndColumn(bucketRowBase(y), x);
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketRowBase(int y) const noexcept
    {

        return static_cast<int>(static_cast<std::uint32_t>(y) << (m_bucketRowShift & 31));
    }

    int SPRITE_COLLECTOR_HASH_MAP::bucketRecordByteOffsetFromRowBaseAndColumn(int rowBase, int x) noexcept
    {

        const std::uint32_t raw = static_cast<std::uint32_t>(rowBase) + static_cast<std::uint32_t>(x);
        return static_cast<int>(raw << 4u);
    }

    SPRITE_POINTER_LIST* SPRITE_COLLECTOR_HASH_MAP::bucketAtByteOffsetUnchecked(int byteOffset) noexcept
    {
        return reinterpret_cast<SPRITE_POINTER_LIST*>(
            reinterpret_cast<unsigned char*>(m_bucketTable) + byteOffset);
    }

    const SPRITE_POINTER_LIST* SPRITE_COLLECTOR_HASH_MAP::bucketAtByteOffsetUnchecked(int byteOffset) const noexcept
    {
        return reinterpret_cast<const SPRITE_POINTER_LIST*>(
            reinterpret_cast<const unsigned char*>(m_bucketTable) + byteOffset);
    }

    SPRITE_POINTER_LIST* SPRITE_COLLECTOR_HASH_MAP::bucketAtCellBoundary(int x, int y) noexcept
    {
        return bucketAtByteOffsetUnchecked(bucketRecordByteOffsetForCell(x, y));
    }

    const SPRITE_POINTER_LIST* SPRITE_COLLECTOR_HASH_MAP::bucketAtCellBoundary(int x, int y) const noexcept
    {
        return bucketAtByteOffsetUnchecked(bucketRecordByteOffsetForCell(x, y));
    }

    SPRITE_POINTER_LIST* SPRITE_COLLECTOR_HASH_MAP::bucketAt(int x, int y) noexcept
    {
        return bucketAtCellBoundary(x, y);
    }

    const SPRITE_POINTER_LIST* SPRITE_COLLECTOR_HASH_MAP::bucketAt(int x, int y) const noexcept
    {
        return bucketAtCellBoundary(x, y);
    }

    SPRITE_COLLECTOR_HASH_MAP*& GlobalSpriteHashMapSlot()
    {
        return g_spriteHashMap;
    }

    SPRITE_COLLECTOR_HASH_MAP* GlobalSpriteHashMap()
    {
        return g_spriteHashMap;
    }

    void SetGlobalSpriteHashMap(SPRITE_COLLECTOR_HASH_MAP* value)
    {
        g_spriteHashMap = value;
    }

    void DeleteGlobalSpriteHashMap()
    {

        SPRITE_COLLECTOR_HASH_MAP* current = g_spriteHashMap;
        if (!current)
            return;
        g_spriteHashMap = nullptr;
        current->clear();

        ::operator delete(current);
    }

    void DestroyGlobalSpriteHashMapForApplicationDestructor()
    {

        SPRITE_COLLECTOR_HASH_MAP* const current = g_spriteHashMap;
        if (!current)
            return;
        current->clear();
        ::operator delete(current);
    }

    bool ReinitGlobalSpriteHashMapFromVidTable(float mapWidth,
                                               float mapHeight,
                                               VID* const* vids,
                                               int vidCount)
    {

        DeleteGlobalSpriteHashMap();
        void* const storage = ::operator new(0x44u, std::nothrow);
        SPRITE_COLLECTOR_HASH_MAP* const created = storage
            ? new (storage) SPRITE_COLLECTOR_HASH_MAP(mapWidth, mapHeight, vids, vidCount)
            : nullptr;
        g_spriteHashMap = created;
        return created != nullptr;
    }
    SPRITE* GlobalHashFirstInBoxAroundDot(float minX, float minY, float maxX, float maxY)
    {

        return g_spriteHashMap ? g_spriteHashMap->firstSpriteInBox(minX, minY, maxX, maxY) : nullptr;
    }

    SPRITE* GlobalHashNextInBoxAroundDot()
    {

        return g_spriteHashMap ? g_spriteHashMap->nextSpriteInBox() : nullptr;
    }

    SPRITE* GlobalHashQueryCellCollision(const MAP& map, SPRITE* sprite, float x, float y, float z)
    {

        return g_spriteHashMap ? g_spriteHashMap->findSpriteCollisionAt(map, sprite, x, y, z) : nullptr;
    }

    SPRITE* GlobalHashQueryCellCollisionByVid(const MAP& map, const VID* probeVid, float x, float y, float z)
    {

        return g_spriteHashMap ? g_spriteHashMap->findVidCollisionAt(map, probeVid, x, y, z) : nullptr;
    }

    bool RemoveSpriteFromGlobalHashForActionSwitch(SPRITE* sprite)
    {

        return (sprite && g_spriteHashMap) ? g_spriteHashMap->removeSprite(sprite) == 0 : false;
    }

    bool AddSpriteToGlobalHashForActionSwitch(SPRITE* sprite)
    {

        if (!sprite || !g_spriteHashMap)
            return false;
        g_spriteHashMap->addSprite(sprite);
        return true;
    }

    int RemoveSpriteChainFromGlobalHashForActionSwitch(SPRITE* first, SPRITE* (*nextSprite)(SPRITE*))
    {

        if (!nextSprite)
            return 0;

        int removed = 0;
        for (SPRITE* cur = first; cur; cur = nextSprite(cur))
        {
            if (RemoveSpriteFromGlobalHashForActionSwitch(cur))
                ++removed;
        }
        return removed;
    }

    bool GlobalHashLineTrace(const MAP& map, SPRITE* sprite, const VECTOR& start, VECTOR& end, VECTOR& hit)
    {

        return g_spriteHashMap ? g_spriteHashMap->traceSpriteMovementCollision(map, sprite, start, end, hit) : false;
    }

    bool GlobalHashLineTraceByVid(const MAP& map, const VID* probeVid, VECTOR& start, VECTOR& end)
    {
        if (!g_spriteHashMap)
            return false;
        float targetX = end.x;
        float targetY = end.y;
        float targetZ = end.z;
        if (!g_spriteHashMap->traceVidMovementCollision(map, probeVid, start.x, start.y, start.z, &targetX, &targetY, &targetZ))
            return false;
        end = VECTOR{targetX, targetY, targetZ};
        return true;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_SPRITE_HASH_STDCALL __stdcall
#else
#define AS1_SPRITE_HASH_STDCALL
#endif

    int AS1_SPRITE_HASH_STDCALL depoCanCreateUnitCandidate(const SPRITE* candidate)
    {

        if (!candidate)
            return 0;
        const int nvid = candidate->Vid()->nVid;
        for (int allowed : kDepoCanCreateUnitNvids)
        {
            if (allowed == nvid)
                return 1;
        }
        return 0;
    }

    bool SpriteHashDepoCanCreateUnitFilter(const SPRITE* candidate)
    {
        return depoCanCreateUnitCandidate(candidate) != 0;
    }

    bool SpriteHashCandidateHasProperty(const SPRITE* sprite, DWORD propertyMask)
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return vid && ((vid->property & propertyMask) != 0);
    }

    bool SpriteHashCandidateHasClass(const SPRITE* sprite, DWORD spriteClass)
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return vid && vid->spriteClass == spriteClass;
    }

    bool SpriteHashCandidateCanCrush(const SPRITE* sprite)
    {

        return SpriteHashCandidateHasProperty(sprite, P_CRUSH);
    }
}
