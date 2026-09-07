#pragma once

#include <cstdint>
#include <array>

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_WEAK_STDCALL __stdcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_WEAK_STDCALL __attribute__((stdcall))
#else
#define AS1_WEAK_STDCALL
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_WEAK_FASTCALL __fastcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_WEAK_FASTCALL __attribute__((fastcall))
#else
#define AS1_WEAK_FASTCALL
#endif

namespace as1
{
    class SPRITE;
    class RESOURCE;

    namespace core
    {
        class WeakController;
        class WeakControllerMap;
        struct PathPosition;

        void* destroyWeakControllerPointerListStorage(void* self, unsigned char flags) noexcept;

        int publishPathCandidate(WeakController* self, unsigned int pathSize, int score, int minimumDistance, int* resultIndex) noexcept;
        int searchPathRecursive(WeakController* self, int incomingEdgeIndex, int incomingFacing) noexcept;
        int searchFacingLimitedPath(WeakController* self, int depth, WeakController* excludedA, WeakController* excludedB, SPRITE* routeOwner, unsigned char facing) noexcept;
        int validatePathLink(WeakController* self, int edgeIndex, SPRITE* routeOwner) noexcept;
        int advancePathPosition(PathPosition* self, WeakController* firstNode, SPRITE* secondSprite, SPRITE* routeOwner) noexcept;
        void initializePathSearch(WeakControllerMap* self, WeakController* firstNode, SPRITE* secondSprite, int actionBucket, SPRITE* routeOwner) noexcept;
        int scoreNextPathStep(PathPosition* self, WeakController* firstNode, SPRITE* secondSprite, int actionBucket, SPRITE* routeOwner) noexcept;
        int serializePathPosition(PathPosition* self, RESOURCE* resource) noexcept;

        class WeakController
        {
        public:
            struct Link
            {
                WeakController* target;
                std::uint32_t length;
                int reciprocalIndex;
                std::uint32_t crossingLinkToken;
                std::uint32_t facing;
            };

            WeakController() noexcept;

            int refCount() const noexcept { return m_refCount; }
            void setRefCount(int value) noexcept { m_refCount = value; }
            std::uint32_t pathEventFlag() const noexcept { return m_pathEventFlag; }
            void setPathEventFlag(std::uint32_t value) noexcept { m_pathEventFlag = value; }
            int selectedLinkIndex() const noexcept { return m_selectedLinkIndex; }
            std::uint32_t pushLineValue() const noexcept { return m_pushLineValue; }
            void setPushLineValue(std::uint32_t value) noexcept { m_pushLineValue = value; }
            std::uint32_t routeClassTag() const noexcept { return m_routeClassTag; }
            void setRouteClassTag(std::uint32_t value) noexcept { m_routeClassTag = value; }
            int linkCount() const noexcept { return m_linkCount; }
            unsigned char firstLinkFacing() const noexcept
            {
                return m_linkCount > 0
                    ? static_cast<unsigned char>(m_links[0].facing)
                    : 0;
            }
            SPRITE* ownerSprite() const noexcept { return m_ownerSprite; }
            int x() const noexcept { return m_x; }
            int y() const noexcept { return m_y; }
            int id() const noexcept { return m_id; }

            std::array<Link, 6>& links() noexcept { return m_links; }
            const std::array<Link, 6>& links() const noexcept { return m_links; }
            Link* linkAt(int index) noexcept;
            const Link* linkAt(int index) const noexcept;
            void setSelectedLinkIndex(int value) noexcept { m_selectedLinkIndex = value; }
            void setOwnerSprite(SPRITE* value) noexcept { m_ownerSprite = value; }
            void setCoordinatesAndId(int x, int y, int id) noexcept;
            const std::array<int, 6>& pathDepthByEdge() const noexcept { return m_pathDepthByEdge; }
            const std::array<int, 6>& pathCostByEdge() const noexcept { return m_pathCostByEdge; }
            const std::array<int, 6>& pathDurationByEdge() const noexcept { return m_pathDurationByEdge; }

        private:
            friend int publishPathCandidate(WeakController* self, unsigned int pathSize, int score, int minimumDistance, int* resultIndex) noexcept;
            friend int searchPathRecursive(WeakController* self, int incomingEdgeIndex, int incomingFacing) noexcept;
            friend int searchFacingLimitedPath(WeakController* self, int depth, WeakController* excludedA, WeakController* excludedB, SPRITE* routeOwner, unsigned char facing) noexcept;
            friend int validatePathLink(WeakController* self, int edgeIndex, SPRITE* routeOwner) noexcept;
            friend int advancePathPosition(PathPosition* self, WeakController* firstNode, SPRITE* secondSprite, SPRITE* routeOwner) noexcept;
            friend void initializePathSearch(WeakControllerMap* self, WeakController* firstNode, SPRITE* secondSprite, int actionBucket, SPRITE* routeOwner) noexcept;
            friend int scoreNextPathStep(PathPosition* self, WeakController* firstNode, SPRITE* secondSprite, int actionBucket, SPRITE* routeOwner) noexcept;
            friend int findLinkIndex(WeakController* self, WeakController* target) noexcept;
            friend void removeLinkTo(WeakController* self, WeakController* target) noexcept;
            friend void connectBidirectional(WeakController* self, WeakController* target) noexcept;
            friend int findNearestPathPosition(WeakController* self, int x, int y, int z, struct PathPosition* out) noexcept;
            friend int distanceToLink(WeakController* self, int x, int y, int z, int edgeIndex) noexcept;
            friend int findClosestFacingLink(WeakController* self, unsigned char facing) noexcept;
            friend int projectDistanceAlongLink(WeakController* self, int x, int y, int z, int edgeIndex) noexcept;
            friend int buildCrossingLinkGrid(WeakControllerMap* self) noexcept;
            friend void AS1_WEAK_STDCALL markCrossingLinks(WeakController* a1, WeakController* a2, WeakController* a3, WeakController* a4) noexcept;
            friend int searchPushLineRecursive(WeakController* self) noexcept;
            friend int setPushLine(WeakControllerMap* self, int x1, int y1, int x2, int y2, int value) noexcept;
            friend WeakController* setNearestLinkValue(WeakControllerMap* self, int x, int y, int value, int unused) noexcept;
            friend int drawWeakControllerMapDebug(WeakControllerMap* self) noexcept;
            friend void AS1_WEAK_FASTCALL releaseWeakController(WeakController* self) noexcept;
            friend WeakController* findNodeNearCoordinates(WeakControllerMap* self, int x, int y, int id) noexcept;
            friend WeakController* createOrRetainNode(WeakControllerMap* self, float x, float y, float id);
            friend WeakController* findNearestLinkedNode2D(WeakControllerMap* self, int x, int y) noexcept;
            friend WeakController* findNearestLinkedNode3D(WeakControllerMap* self, int x, int y, int z) noexcept;

            int m_refCount;
            std::uint32_t m_pathEventFlag;
            std::uint32_t m_reservedSlot08;
            int m_selectedLinkIndex;
            std::uint32_t m_pushLineValue;
            std::uint32_t m_routeClassTag;
            int m_linkCount;
            std::array<Link, 6> m_links;

            std::array<int, 6> m_pathDepthByEdge;
            std::array<int, 6> m_pathCostByEdge;
            std::array<int, 6> m_pathDurationByEdge;
            SPRITE* m_ownerSprite;
            int m_x;
            int m_y;
            int m_id;
        };


        struct PathPosition
        {
            int deserializePathPosition(RESOURCE* resource) noexcept;

            WeakController* node;
            int progress;
            int auxiliary;
            int edgeIndex;
        };

        class WeakControllerMap
        {
        public:
            WeakControllerMap() noexcept = default;
            ~WeakControllerMap() noexcept;

            int minX() const noexcept { return m_minX; }
            int minY() const noexcept { return m_minY; }
            int maxX() const noexcept { return m_maxX; }
            int maxY() const noexcept { return m_maxY; }
            int dotCount() const noexcept { return m_dotCount; }
            int dotCapacity() const noexcept { return m_dotCapacity; }
            WeakController* const* dots() const noexcept { return m_dots; }

        private:
            friend void initializePathSearch(WeakControllerMap* self, WeakController* firstNode, SPRITE* secondSprite, int actionBucket, SPRITE* routeOwner) noexcept;
            friend WeakController* findNodeNearCoordinates(WeakControllerMap* self, int x, int y, int id) noexcept;
            friend WeakController* createOrRetainNode(WeakControllerMap* self, float x, float y, float id);
            friend WeakController* findNearestLinkedNode2D(WeakControllerMap* self, int x, int y) noexcept;
            friend WeakController* findNearestLinkedNode3D(WeakControllerMap* self, int x, int y, int z) noexcept;
            friend int buildCrossingLinkGrid(WeakControllerMap* self) noexcept;
            friend void AS1_WEAK_FASTCALL releaseWeakController(WeakController* self) noexcept;

            int m_minX = 0;
            int m_minY = 0;
            int m_maxX = 0;
            int m_maxY = 0;
            std::uint32_t m_dotListOwnerVtableToken;
            int m_dotCount = 0;
            int m_dotCapacity = 0;
            WeakController** m_dots = nullptr;
        };


#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                                                               
                                                                                                   
                                                                                               
                                                                                                         
#endif

        int findLinkIndex(WeakController* self, WeakController* target) noexcept;
        void removeLinkTo(WeakController* self, WeakController* target) noexcept;
        void connectBidirectional(WeakController* self, WeakController* target) noexcept;
        int findNearestPathPosition(WeakController* self, int x, int y, int z, PathPosition* out) noexcept;
        int distanceToLink(WeakController* self, int x, int y, int z, int edgeIndex) noexcept;
        int findClosestFacingLink(WeakController* self, unsigned char facing) noexcept;
        int projectDistanceAlongLink(WeakController* self, int x, int y, int z, int edgeIndex) noexcept;
        int AS1_WEAK_STDCALL addNodeToSpatialGrid(int x, int y, int width, int height, int dotIndex) noexcept;
        int buildCrossingLinkGrid(WeakControllerMap* self) noexcept;
        void AS1_WEAK_STDCALL markCrossingLinks(WeakController* a1, WeakController* a2, WeakController* a3, WeakController* a4) noexcept;
        int searchPushLineRecursive(WeakController* self) noexcept;
        int setPushLine(WeakControllerMap* self, int x1, int y1, int x2, int y2, int value) noexcept;
        WeakController* setNearestLinkValue(WeakControllerMap* self, int x, int y, int value, int unused) noexcept;
        int drawWeakControllerMapDebug(WeakControllerMap* self) noexcept;
        double squareDistanceComponent(double value) noexcept;
        double weakControllerScreenX(const WeakController* self) noexcept;
        double weakControllerScreenY(const WeakController* self) noexcept;
        int pathResultScore() noexcept;
        int pathSecondaryBestCost() noexcept;
        WeakController* pathBestNode() noexcept;
        WeakControllerMap& globalWeakControllerMap() noexcept;
        WeakController* findNodeNearCoordinates(WeakControllerMap* self, int x, int y, int id) noexcept;
        WeakController* createOrRetainNode(WeakControllerMap* self, float x, float y, float id);
        WeakController* findNearestLinkedNode2D(WeakControllerMap* self, int x, int y) noexcept;
        WeakController* findNearestLinkedNode3D(WeakControllerMap* self, int x, int y, int z) noexcept;
        void AS1_WEAK_FASTCALL releaseWeakControllerThunk(WeakController* self) noexcept;
        void AS1_WEAK_FASTCALL releaseWeakController(WeakController* self) noexcept;
    }
}
