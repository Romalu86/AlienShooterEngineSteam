#pragma once

#include <cstddef>
#include <cstdint>

#include "core/as_string.h"

namespace as1
{
    class SPRITE;
    namespace input { struct InputMessageState; }

    class SPRITE_POINTER_LIST
    {
    public:
        SPRITE_POINTER_LIST() noexcept;
        ~SPRITE_POINTER_LIST();

        SPRITE_POINTER_LIST(const SPRITE_POINTER_LIST&) = delete;
        SPRITE_POINTER_LIST& operator=(const SPRITE_POINTER_LIST&) = delete;

        void append(SPRITE* sprite);
        void InsertSorted(SPRITE* sprite);
        bool removeSorted(SPRITE* sprite);
        int removeSortedError(SPRITE* sprite);
        int removeAndReleaseReference(SPRITE* sprite);
        int findAndNull(SPRITE* sprite) noexcept;
        void compactSparse() noexcept;
        int releaseByIndexRetail(int index);
        int releaseRepeatedReferencesRetail();
        void clearSpriteReferences();
        void clear();
        void deleteAllSprites();
        SPRITE* NextNonNull(int* cursor) const;
        SPRITE* beginReverseIteration(int* cursor) const noexcept;
        SPRITE* continueReverseIteration(int* cursor) const noexcept;
        void add(SPRITE* sprite);
        void clearNoRelease() noexcept;
        void releaseRepeatedReferences();
        void releaseAllReferences();
        int releaseOneByIndex(int index);
        std::size_t count() const noexcept;
        bool empty() const noexcept;
        SPRITE* at(std::size_t index) const noexcept;
        int activeCount() const noexcept;
        int storageCapacity() const noexcept;
        SPRITE* const* data() const noexcept;
        bool contains(SPRITE* sprite) const noexcept;

        void initializeHashBucketRecordState() noexcept;
        SPRITE_POINTER_LIST* initializePointerListRecord() noexcept;
        // pointerListDeletingDestructor scalar/array deleting-destructor owner for the 0x10-byte
        // core::List record. Bit 2 selects cookie-array destruction.
        void* pointerListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept;
        void destroyCoreListStorage() noexcept;

        static constexpr std::uint32_t RETAIL_VTABLE_TOKEN = 0x004D96F8u;
        static std::uint32_t CurrentImageCoreListVtable() noexcept;

    protected:
        void setRetailVtableToken(std::uint32_t token) noexcept { m_vtableToken = token; }
        int releaseOne(std::size_t index, bool eraseEntry);
        int collapseDuplicateReference(std::size_t index);
        int releaseDuplicateAtIndex(std::size_t index);
        int releaseSpriteReference(SPRITE* sprite, bool callDeleteWhenZero);
        bool ensureCapacityForOneMore();

        std::uint32_t m_vtableToken = 0;
        int m_count = 0;                                   // +0x04
        int m_capacity = 0;                                // +0x08
        SPRITE** m_items = nullptr;                        // +0x0C (x86)
    };

    class SPRITE_LIST : public SPRITE_POINTER_LIST
    {
    public:
        SPRITE_LIST() noexcept;
        ~SPRITE_LIST();

        SPRITE_LIST(const SPRITE_LIST&) = delete;
        SPRITE_LIST& operator=(const SPRITE_LIST&) = delete;

        void initializeListState() noexcept;
        void initializeBaseSpriteListRecord() noexcept;
        SPRITE_LIST* destroyListStorage(bool deleteSelfFlag) noexcept;
        SPRITE_LIST* destroyBaseSpriteListRecord(bool deleteSelfFlag) noexcept;
        void* baseSpriteListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept;

        static constexpr std::uint32_t RETAIL_VTABLE_TOKEN = 0x004D9700u;
        static std::uint32_t CurrentImageBaseSpriteListVtable() noexcept;
        static std::uint32_t CurrentImageRelationListVtable() noexcept;

    };

#if INTPTR_MAX == INT32_MAX
                                                                                                           
                                                                                           
#endif

    extern SPRITE_POINTER_LIST g_spriteWorkList;

    // Sprite-list owner route used by separate application-owned list routes.
    SPRITE_LIST& applicationGlobalSpriteList();

    class MENU;
    MENU& applicationMenu();
    SPRITE_LIST& applicationFrameSpriteList();

    class SPRITE_OWNER_SLOT
    {
    public:
        using DestroyProc = void (*)(void* object, bool deleteObject);

        SPRITE_OWNER_SLOT() = default;
        explicit SPRITE_OWNER_SLOT(DestroyProc destroyProc) noexcept;
        ~SPRITE_OWNER_SLOT();

        SPRITE_OWNER_SLOT(const SPRITE_OWNER_SLOT&) = delete;
        SPRITE_OWNER_SLOT& operator=(const SPRITE_OWNER_SLOT&) = delete;

        void setDestroyProc(DestroyProc destroyProc) noexcept;
        void bind(void* object) noexcept;
        void* clearIfMatches(void* object) noexcept;
        void* get() const noexcept;
        bool empty() const noexcept;

        // Call the owned object's deleting destructor with arg 1, then clear the stored pointer.
        int release();

    private:
        void* m_object = nullptr;
        DestroyProc m_destroyProc = nullptr;
    };
}
