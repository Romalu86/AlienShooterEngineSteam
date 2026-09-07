#include "rail.h"
#include "map.h"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>

namespace as1
{

    namespace
    {
        struct RailHeightTables
        {
            std::uint8_t initFlags = 0u;
            float primary[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float secondary[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        };

        RailHeightTables& mutableRailHeightTables() noexcept
        {
            static RailHeightTables tables;
            return tables;
        }

        const float kRailPrimaryXComponents[12] = {
            0.0f, 1.0f, 1.0f, -1.0f,
            1.0f, -1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, -1.0f, -1.0f
        };

        const float kRailPrimaryYComponents[12] = {
            -1.0f, 0.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f, 1.0f,
            1.0f, -1.0f, -1.0f, 1.0f
        };

        const float kRailSecondaryXComponents[12] = {
            0.0f, -1.0f, -1.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            -1.0f, -1.0f, 1.0f, 1.0f
        };

        const float kRailSecondaryYComponents[12] = {
            1.0f, 0.0f, 1.0f, 1.0f,
            1.0f, 1.0f, -1.0f, -1.0f,
            0.0f, 0.0f, 0.0f, 0.0f
        };

        int truncateRailFloatToInt(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
            {
                return 0;
            }
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        float computeRailScaledCoordinate(float base, float sizeValue, float componentValue, float scaleValue) noexcept
        {
            const long double v =
                static_cast<long double>(sizeValue) * static_cast<long double>(componentValue) *
                static_cast<long double>(scaleValue) + static_cast<long double>(base);
            return static_cast<float>(v);
        }

        float computeRailPointZ(float baseZ, float heightTableValue, float moveUpZ) noexcept
        {
            const long double v =
                static_cast<long double>(baseZ) +
                static_cast<long double>(heightTableValue) +
                static_cast<long double>(moveUpZ);
            return static_cast<float>(v);
        }

        float computeRailPointZMoveThenHeight(float baseZ, float moveUpZ, float heightTableValue) noexcept
        {
            const long double v =
                static_cast<long double>(baseZ) +
                static_cast<long double>(moveUpZ) +
                static_cast<long double>(heightTableValue);
            return static_cast<float>(v);
        }

        int computeRailLinkZToInt(float baseZ, float heightTableValue, float moveUpZ) noexcept
        {
            const long double d =
                static_cast<long double>(baseZ) +
                static_cast<long double>(heightTableValue) +
                static_cast<long double>(moveUpZ);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
            {
                return 0;
            }
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int computeRailLinkZMoveThenHeightToInt(float baseZ, float moveUpZ, float heightTableValue) noexcept
        {
            const long double d =
                static_cast<long double>(baseZ) +
                static_cast<long double>(moveUpZ) +
                static_cast<long double>(heightTableValue);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
            {
                return 0;
            }
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int floatBitsAsInt(float value) noexcept
        {
            int raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            return raw;
        }

        constexpr float kRailOwnerPointScale = 0.25f;
        constexpr float kRailLinkPointScale = 0.75f;
        constexpr unsigned kRailDirectionTableModulo = 12u;
        constexpr unsigned kRailDirectionByteMask = 0xFFu;

        unsigned railDirectionSource(VID* vid, unsigned rawDirection) noexcept
        {
            return static_cast<unsigned>((vid->directionQuantizationOffset() + rawDirection) & kRailDirectionByteMask);
        }

        unsigned railDirectionIndex(VID* vid, unsigned directionSource) noexcept
        {
            const unsigned noDir = static_cast<unsigned>(vid->directionCount());
            return (directionSource * noDir) >> 8;
        }

        unsigned railHeightIndex(unsigned directionIndex) noexcept
        {
            return directionIndex / kRailDirectionTableModulo;
        }

        unsigned railComponentIndex(unsigned directionIndex) noexcept
        {
            return directionIndex % kRailDirectionTableModulo;
        }

        float railOwnerPointCoordinate(float base, float size, float component) noexcept
        {
            return computeRailScaledCoordinate(base, size, component, kRailOwnerPointScale);
        }

        float railLinkPointCoordinate(float base, float size, float component) noexcept
        {
            return computeRailScaledCoordinate(base, size, component, kRailLinkPointScale);
        }

        float railPointZ(float baseZ, float heightTableValue, float moveUpZ) noexcept
        {
            return computeRailPointZ(baseZ, heightTableValue, moveUpZ);
        }

        bool shouldFlagRailNodes(VID* vid) noexcept
        {
            return vid->deathChildNvid() < vid->nvid();
        }

        void flagRailNodes(core::WeakController* slot78, core::WeakController* slot7C) noexcept
        {
            slot78->setPathEventFlag(1u);
            slot7C->setPathEventFlag(1u);
        }

        constexpr unsigned kRailDamageClassGate = 0x16u;
        constexpr unsigned kRailDamageChangeVidOpcode = 0x3Eu;

        bool shouldRunRailDamageVidSwitch(int animation) noexcept
        {
            return animation >= 0x0F;
        }

        VID* railDamageClassGateVid(VID* vid) noexcept
        {
            return vid->deathChildVid();
        }

        bool railDamageClassGateMatches(VID* classGate) noexcept
        {
            return classGate && classGate->spriteClassId() == kRailDamageClassGate;
        }

        int railDamageChangeVid(VID* vid) noexcept
        {
            return vid->deathChildNvid();
        }

        bool shouldFlagRailNodesAfterDamage(int changeVid, VID* vid) noexcept
        {
            return changeVid > vid->nvid();
        }

        unsigned railDamageChangeVidOpcode() noexcept
        {
            return kRailDamageChangeVidOpcode;
        }

        int armyBucketFromRuntimeFlags(DWORD flags) noexcept
        {
            return static_cast<int>((flags >> SPRITE::ArmyBitsShift) & SPRITE::ArmyValueMask);
        }

        int frameTimeForArmyBucket(VID* vid, int bucket) noexcept
        {
            return vid->animationFrameDuration(bucket);
        }

        bool weakControllerHasOwner(core::WeakController* owner) noexcept
        {
            return owner != nullptr;
        }

        void releaseRailNodeOwner(core::WeakController* owner) noexcept
        {
            if (weakControllerHasOwner(owner))
                core::releaseWeakController(owner);
        }

        bool railNodeHandleMatches(core::WeakController* slot78, core::WeakController* slot7C, std::uintptr_t ownerHandle) noexcept
        {
            return reinterpret_cast<std::uintptr_t>(slot78) == ownerHandle ||
                   reinterpret_cast<std::uintptr_t>(slot7C) == ownerHandle;
        }

        int railNodeReleaseChangeVid(VID* vid) noexcept
        {
            return vid->deathChildNvid();
        }

        bool railNodeReleaseChangeVidGate(int changeVid, VID* vid) noexcept
        {
            return changeVid != 0 && changeVid < vid->nvid();
        }

        unsigned railNodeReleaseChangeVidOpcode() noexcept
        {
            return kRailDamageChangeVidOpcode;
        }

        int railNodeArmyBucketFromFlags(DWORD flags) noexcept
        {
            return armyBucketFromRuntimeFlags(flags);
        }

        int railNodeFrameTimeForArmyBucket(VID* vid, int bucket) noexcept
        {
            return frameTimeForArmyBucket(vid, bucket);
        }
    }
    RAIL::RAIL(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : TERRAIN(owner, vid, xyz, direction, parent),
          m_firstRailNode(nullptr),
          m_secondRailNode(nullptr)
    {
        dispatchRailActionOpcode(0x3C, direction.Int() & 0xFF, 0.0f, 0);
    }

    RAIL::~RAIL()
    {
        releaseRailNodeOwner(m_firstRailNode);
        releaseRailNodeOwner(m_secondRailNode);
    }

    RAIL* RAIL::railScalarDeletingDestructor(unsigned char flags) noexcept
    {
        RAIL* const self = this;
        destroyRailState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    int RAIL::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const std::uint32_t rawArg8 = static_cast<std::uint32_t>(argument2Carrier);
        const int argument3 = argument3Carrier;
        float argument2 = 0.0f;
                                                                                             
        std::memcpy(&argument2, &rawArg8, sizeof(argument2));
        return dispatchRailActionOpcode(opcode, argument1, argument2, argument3);
    }

    void RAIL::MoveTact()
    {
        performBaseMovementTact();
    }

    int RAIL::dispatchRailActionOpcode(int opcode, int actionArgument1, float actionArgument2, int actionArgument3)
    {
        VID* const entryVid = Vid();
        RailHeightTables& heightTables = mutableRailHeightTables();
        std::uint8_t flags = heightTables.initFlags;
        if ((flags & 1u) == 0u)
        {
            flags = static_cast<std::uint8_t>(flags | 1u);
            heightTables.initFlags = flags;
            heightTables.primary[1] = entryVid->sizeZ();
            heightTables.primary[2] = 0.0f;
            heightTables.primary[3] = 0.0f;
        }
        if ((flags & 2u) == 0u)
        {
            heightTables.initFlags = static_cast<std::uint8_t>(flags | 2u);
            heightTables.secondary[2] = entryVid->sizeZ();
            heightTables.secondary[3] = 0.0f;
        }

        const float moveUpZ = entryVid->moveUpZ();
        if (opcode == 0x3C)
            return rebuildRailNodes(actionArgument1, moveUpZ);
        if (opcode == 0x55)
            return handleRailDamageAction(actionArgument1, actionArgument2, actionArgument3);
        return dispatchExtendedSpriteActionOpcode(opcode, actionArgument1, floatBitsAsInt(actionArgument2), actionArgument3);
    }

    int RAIL::handleRailDamageAction(int actionArgument1, float actionArgument2, int actionArgument3)
    {
        dispatchActionOpcode(static_cast<std::uint32_t>(ActionCode::ACT_DAMAGE), actionArgument1, floatBitsAsInt(actionArgument2), actionArgument3);

        if (!shouldRunRailDamageVidSwitch(currentAnimation()))
            return 0;

        VID* const vid = Vid();
        VID* const classGate = railDamageClassGateVid(vid);
        if (!railDamageClassGateMatches(classGate))
            return 0;

        const int changeVid = railDamageChangeVid(vid);
        if (shouldFlagRailNodesAfterDamage(changeVid, vid))
        {
            m_firstRailNode->setPathEventFlag(1u);
            m_secondRailNode->setPathEventFlag(1u);
        }

        (void)Action(static_cast<int>(railDamageChangeVidOpcode()),
                     static_cast<std::intptr_t>(changeVid), 0, 0);

        const int bucket = armyBucketFromRuntimeFlags(runtimeFlags());
        const int frameTime = frameTimeForArmyBucket(vid, bucket);
        updateAnimationFrameTime(frameTime);
        return 0;
    }

    int RAIL::rebuildRailNodes(int direction, float moveUpZ)
    {
        ChangeDirection(direction);

        if (m_firstRailNode)
            core::releaseWeakController(m_firstRailNode);
        if (m_secondRailNode)
            core::releaseWeakController(m_secondRailNode);

        VID* const vid = Vid();
        RailHeightTables& heightTables = mutableRailHeightTables();
        const unsigned directionSource = railDirectionSource(vid, directionIndex());
        const unsigned directionIndex = railDirectionIndex(vid, directionSource);
        const unsigned heightIndex = railHeightIndex(directionIndex);
        const unsigned tableIndex = railComponentIndex(directionIndex);

        const float baseX = X();
        const float baseY = Y();
        const float baseZ = Z();
        const float sizeX = vid->sizeX();
        const float sizeY = vid->sizeY();

        const float firstZ = railPointZ(baseZ, heightTables.primary[heightIndex], moveUpZ);
        const float firstY = railOwnerPointCoordinate(baseY, sizeY, kRailPrimaryYComponents[tableIndex]);
        const float firstX = railOwnerPointCoordinate(baseX, sizeX, kRailPrimaryXComponents[tableIndex]);
        m_firstRailNode = core::createOrRetainNode(&core::globalWeakControllerMap(), firstX, firstY, firstZ);

        const float secondZ = computeRailPointZMoveThenHeight(
            baseZ, moveUpZ, heightTables.secondary[heightIndex]);
        const float secondY = railOwnerPointCoordinate(baseY, sizeY, kRailSecondaryYComponents[tableIndex]);
        const float secondX = railOwnerPointCoordinate(baseX, sizeX, kRailSecondaryXComponents[tableIndex]);
        m_secondRailNode = core::createOrRetainNode(&core::globalWeakControllerMap(), secondX, secondY, secondZ);

        const int firstLinkZ = computeRailLinkZToInt(
            baseZ, heightTables.primary[heightIndex], moveUpZ);
        const int firstLinkY = truncateRailFloatToInt(railLinkPointCoordinate(baseY, sizeY, kRailPrimaryYComponents[tableIndex]));
        const int firstLinkX = truncateRailFloatToInt(railLinkPointCoordinate(baseX, sizeX, kRailPrimaryXComponents[tableIndex]));
        core::WeakController* const firstLink = core::findNodeNearCoordinates(&core::globalWeakControllerMap(), firstLinkX, firstLinkY, firstLinkZ);
        core::connectBidirectional(m_firstRailNode, firstLink);

        const int secondLinkZ = computeRailLinkZMoveThenHeightToInt(
            baseZ, moveUpZ, heightTables.secondary[heightIndex]);
        const int secondLinkY = truncateRailFloatToInt(railLinkPointCoordinate(baseY, sizeY, kRailSecondaryYComponents[tableIndex]));
        const int secondLinkX = truncateRailFloatToInt(railLinkPointCoordinate(baseX, sizeX, kRailSecondaryXComponents[tableIndex]));
        core::WeakController* const secondLink = core::findNodeNearCoordinates(&core::globalWeakControllerMap(), secondLinkX, secondLinkY, secondLinkZ);
        core::connectBidirectional(m_secondRailNode, secondLink);
        core::connectBidirectional(m_secondRailNode, m_firstRailNode);

        if (shouldFlagRailNodes(vid))
            flagRailNodes(m_firstRailNode, m_secondRailNode);
        return 0;
    }

    void RAIL::setRailNodes(core::WeakController* slot78, core::WeakController* slot7C) noexcept
    {
        m_firstRailNode = slot78;
        m_secondRailNode = slot7C;
    }

    void RAIL::destroyRailState() noexcept
    {
        releaseRailNodeOwner(m_firstRailNode);
        releaseRailNodeOwner(m_secondRailNode);
        destroyBaseSpriteState();
    }

    void RAIL::handleRailNodeReleased(std::uintptr_t ownerHandle) noexcept
    {
        if (!railNodeHandleMatches(m_firstRailNode, m_secondRailNode, ownerHandle))
            return;

        VID* vid = Vid();
        const int changeVid = railNodeReleaseChangeVid(vid);
        if (!railNodeReleaseChangeVidGate(changeVid, vid))
            return;

        (void)Action(static_cast<int>(railNodeReleaseChangeVidOpcode()),
                     static_cast<std::intptr_t>(changeVid), 0, 0);

        VID* const currentVid = Vid();
        const int bucket = railNodeArmyBucketFromFlags(runtimeFlags());
        const int frameTime = railNodeFrameTimeForArmyBucket(currentVid, bucket);
        updateAnimationFrameTime(frameTime);
    }
}
