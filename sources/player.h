#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;
    class RESOURCE;
    class SPRITE;
    class STRING;
    namespace input { struct InputMessageState; }

    // Path-owner helpers used by PLAYER-owned path state.
    void* scalarDeletingDestructorPathOwner(void* pathOwner, unsigned char deleteSelfFlag) noexcept;
    int releasePathSprites(void* pathOwner) noexcept;
    void clearPathSpriteReferences(void* pathOwner, SPRITE* sprite) noexcept;
    void* scalarDeletingDestructorPathPendingList(void* pendingOwner, unsigned char deleteSelfFlag) noexcept;

    class PLAYER
    {
    public:
        static constexpr std::size_t RetailObjectSize = 0x32C;
        static DWORD CurrentImageBaseVtable() noexcept;
        static DWORD CurrentImageActiveVtable() noexcept;
        static DWORD CurrentImagePathVtable() noexcept;
        static DWORD CurrentImagePendingVtable() noexcept;

#ifdef _MSC_VER
#pragma warning(suppress : 26495)
#endif
        PLAYER() noexcept {}
        ~PLAYER() noexcept;

        PLAYER* initializeActivePlayerState(int controlMode, int playerSlot) noexcept;
        int submitPathCoordinate(STRING* className, float targetX, float targetY) noexcept;
        PLAYER* scalarDeletingDestructorActivePlayer(unsigned char deleteSelfFlag) noexcept;
        PLAYER* scalarDeletingDestructorBasePlayer(unsigned char deleteSelfFlag) noexcept;
        std::intptr_t removeActivePlayerSpriteReference(SPRITE* sprite) noexcept;
        std::intptr_t clearSpriteReferenceViaVtable(SPRITE* sprite) noexcept;
        void noOpSpriteCallback(SPRITE* sprite) noexcept;
        void destroyPathFindState() noexcept;
        void destroyBaseSpriteOwnerState() noexcept;
        PLAYER* initializeBasePlayerState(int controlMode, int playerSlot) noexcept;
        void resetPlayerSpriteReferences() noexcept;
        void setFlagmanSprite(SPRITE* sprite) noexcept;
        int saveControlledSpriteReference(RESOURCE* mapResource) noexcept;
        int loadControlledSpriteReference(RESOURCE* mapResource) noexcept;
        STRING* getAuxiliaryUnitName(STRING* out) noexcept;
        int submitPathCoordinateToOwner(STRING* className, float targetX, float targetY) noexcept;
        int pathFindTerrainCellStep() const noexcept;
        bool validatePathCoordinateClass(const STRING* className, int* terrainCellStepOut = nullptr) noexcept;
        int enqueuePathCoordinateSound() noexcept;
        void updatePathOwnerFrame() noexcept;
        void advancePathRouteWindow() noexcept;
        void clearPendingPathCoordinates() noexcept;
        void destroyPendingPathCoordinates() noexcept;
        void clearPlayerPathSpriteReferences(SPRITE* sprite) noexcept;
        void releaseSpriteReferenceBaseOwner(SPRITE* sprite) noexcept;
        std::intptr_t removePlayerSpriteReference(SPRITE* sprite) noexcept;
        int removeEmbeddedSpriteReference(SPRITE* sprite) noexcept;
        void processInput(as1::input::InputMessageState* inputState) noexcept;
        void dispatchInputViaRetailVtable(as1::input::InputMessageState* inputState) noexcept;
        void dispatchReservedScriptToggleViaRetailVtable(int value) noexcept;
        void processInputGlobalListPrepass() noexcept;
        void processInputAttackWeaponPreselect(SPRITE* controlled) noexcept;
        void processInputDigitWeaponSelect(SPRITE* controlled, std::uint32_t lastCode) noexcept;
        void processInputWheelWeaponSelect(SPRITE* controlled, as1::input::InputMessageState* inputState) noexcept;
        void processInputDirectionCommandDispatch(SPRITE* controlled, const as1::input::InputMessageState* inputState, int& outDirection, std::uint32_t& outDeltaMs) noexcept;
        void processInputPostMovementHelper(SPRITE* controlled, std::uint32_t flags) noexcept;
        int controlMode() const noexcept;
        int playerSlot() const noexcept;
        SPRITE* controlledSprite() const noexcept;
        void setControlledSprite(SPRITE* value) noexcept;
        SPRITE* auxiliarySprite() const noexcept;
        void setAuxiliarySprite(SPRITE* value) noexcept;
        DWORD money() const noexcept { return m_state.base.money; }
        DWORD getMoney() const noexcept { return m_state.base.money; }
        DWORD setMoney(DWORD value) noexcept { m_state.base.money = value; return value; }
        DWORD setPathSecondaryFlag(int value) noexcept
        {
            const DWORD result = (m_state.path.vtable & ~2u) | (value != 0 ? 2u : 0u);
            m_state.path.vtable = result;
            return result;
        }
        void processInputDispatchChildTail(SPRITE* controlled, int direction, std::uint32_t deltaMs) noexcept;
        void releaseAuxiliarySpriteAfterInput() noexcept;

    private:
        struct BaseOwnerLayout
        {
            DWORD vtable;
            DWORD money;               // starting money reset to 1000 by initializeBasePlayerState
            int controlMode;           // +0x08
            int playerSlot;           // +0x0C
            DWORD controlledSprite;    // +0x10 SPRITE*
            DWORD listVtable;
            int listCount;             // +0x18
            int listCapacity;          // +0x1C
            DWORD listItems;           // +0x20 SPRITE**
            DWORD auxiliarySprite;     // +0x24 SPRITE*
        };

        struct PendingPathOwnerLayout
        {
            DWORD vtable;
            int count;                  // path+0x2F8
            int capacity;               // path+0x2FC
            DWORD entries;              // path+0x300, 16-byte entry array after cookie
        };

        struct PathOwnerLayout
        {
            DWORD vtable;
            int routeCount;             // +0x004
            DWORD updateInterval;       // +0x008
            int terrainVid;             // +0x00C
            int secondaryTerrainVid;    // +0x010
            float baseX;                // +0x014
            float baseY;                // +0x018
            int rowDelta;               // +0x01C
            DWORD lastUpdate;           // +0x020
            DWORD primarySprites[45];   // +0x024..+0x0D7
            DWORD secondarySprites[45]; // +0x0D8..+0x18B
            float targetX[45];          // +0x18C..+0x23F
            float targetY[45];          // +0x240..+0x2F3
            PendingPathOwnerLayout pending; // +0x2F4..+0x303

            PathOwnerLayout* initializePathOwner(int terrainVid, int secondaryTerrainVid,
                                         DWORD baseXBits, DWORD baseYBits,
                                         int routeCount, DWORD updateInterval) noexcept;

            void clearPathSpriteReferences(SPRITE* sprite) noexcept;
        };

        struct PlayerLayout
        {
            BaseOwnerLayout base;        // +0x000..+0x027
            PathOwnerLayout path;        // +0x028..+0x32B
        };

                                                                                                     
                                                                                                   
                                                                                                               
                                                                                                                          
                                                                                                                     
                                                                                                                       
                                                                                                                       
                                                                                                                    
                                                                                                                    
                                                                                                                    
                                                                                                                                       
                                                                                                         
                                                                                                         
                                                                                                                         
                                                                                                                            
                                                                                                                                
                                                                                                                
                                                                                                                
                                                                                                                        
                                                                                                                                                                      
                                                                                                       
                                                                                                     
                                                                                                         

        friend int releasePathSprites(void* pathOwner) noexcept;
        friend void clearPathSpriteReferences(void* pathOwner, SPRITE* sprite) noexcept;
        friend void* scalarDeletingDestructorPathOwner(void* pathOwner, unsigned char deleteSelfFlag) noexcept;
        friend void* scalarDeletingDestructorPathPendingList(void* pendingOwner, unsigned char deleteSelfFlag) noexcept;

        PlayerLayout m_state;
    };

                                                                                                    
}
