#include "base_sprite_list.h"

#include <cmath>
#include <algorithm>
#include <new>
#include <cstring>
#include "sprite.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "menu.h"
#include "input.h"
#include "vid/vid.h"

namespace as1
{
    namespace
    {

        class CoreListVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return reinterpret_cast<SPRITE_POINTER_LIST*>(this)->pointerListDeletingDestructor(flags);
            }
        };

        class BaseSpriteListVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return reinterpret_cast<SPRITE_LIST*>(this)->baseSpriteListDeletingDestructor(flags);
            }
        };

        class RelationListVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                auto* const raw = reinterpret_cast<std::uint32_t*>(this);
                void* const items = reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw[3]));
                if (items)
                    ::operator delete(items);
                raw[3] = 0;
                raw[1] = 0;
                if ((flags & 1u) != 0u)
                    ::operator delete(static_cast<void*>(this));
                return this;
            }
        };

        template <class T>
        std::uint32_t currentImageVtableOf() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static T owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
            return 0u;
#endif
        }
    }

    std::uint32_t SPRITE_POINTER_LIST::CurrentImageCoreListVtable() noexcept
    {
        return currentImageVtableOf<CoreListVtableOwner>();
    }

    std::uint32_t SPRITE_LIST::CurrentImageBaseSpriteListVtable() noexcept
    {
        return currentImageVtableOf<BaseSpriteListVtableOwner>();
    }

    std::uint32_t SPRITE_LIST::CurrentImageRelationListVtable() noexcept
    {
        return currentImageVtableOf<RelationListVtableOwner>();
    }

    SPRITE_POINTER_LIST::SPRITE_POINTER_LIST() noexcept
    {
        initializePointerListRecord();
    }

    SPRITE_POINTER_LIST::~SPRITE_POINTER_LIST()
    {
        destroyCoreListStorage();
    }

    SPRITE_LIST::SPRITE_LIST() noexcept
    {
        initializeBaseSpriteListRecord();
    }

    SPRITE_LIST::~SPRITE_LIST()
    {
        destroyBaseSpriteListRecord(false);
    }

    SPRITE_POINTER_LIST g_spriteWorkList;

    bool SPRITE_POINTER_LIST::ensureCapacityForOneMore()
    {
        if (m_count < m_capacity)
            return true;

        const int nextCapacity = m_capacity * 2 + 4;
        if (nextCapacity <= m_capacity)
            return true;

        SPRITE** next = static_cast<SPRITE**>(::operator new(sizeof(SPRITE*) * static_cast<std::size_t>(nextCapacity), std::nothrow));
        if (!next)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", nextCapacity);

        if (m_items)
        {
            for (int i = 0; i < m_capacity; ++i)
                next[i] = m_items[i];
            ::operator delete(m_items);
        }
        m_items = next;
        m_capacity = nextCapacity;
        return true;
    }

    void SPRITE_POINTER_LIST::append(SPRITE* sprite)
    {

        if (!sprite)
            return;
        sprite->AddListReference();
        if (!ensureCapacityForOneMore())
            return;
        m_items[m_count++] = sprite;
    }

    void SPRITE_POINTER_LIST::InsertSorted(SPRITE* sprite)
    {
        if (!sprite)
            return;
        if (!ensureCapacityForOneMore())
            return;
        int pos = 0;
        for (; pos < m_count; ++pos)
        {
            SPRITE* cur = m_items[pos];
            const int lo = cur ? cur->oldAddress() : 0;
            const int ro = sprite->oldAddress();
            if (lo > ro || (lo == ro && cur > sprite))
                break;
        }
        for (int i = m_count; i > pos; --i)
            m_items[i] = m_items[i - 1];
        sprite->AddListReference();
        m_items[pos] = sprite;
        ++m_count;
    }

    bool SPRITE_POINTER_LIST::removeSorted(SPRITE* sprite)
    {
        return removeSortedError(sprite) == 0;
    }

    int SPRITE_POINTER_LIST::removeSortedError(SPRITE* sprite)
    {
        return removeAndReleaseReference(sprite);
    }

    int SPRITE_POINTER_LIST::removeAndReleaseReference(SPRITE* sprite)
    {
        if (!sprite || m_count == 0 || !m_items)
            return 1;
        for (int i = m_count - 1; i >= 0; --i)
        {
            if (m_items[i] == sprite)
                return releaseOne(static_cast<std::size_t>(i), true);
        }
        return 1;
    }

    int SPRITE_POINTER_LIST::findAndNull(SPRITE* sprite) noexcept
    {
        // Application::removeSpriteFromDrawBucket scans the selected 0x10-byte list from
        // count-1 toward zero and only nulls the matching slot.  It does not
        // alter count/capacity and does not change the sprite reference counter.
        int index = m_count - 1;
        if (index < 0)
            return index;
        while (m_items[index] != sprite)
        {
            --index;
            if (index < 0)
                return index;
        }
        m_items[index] = nullptr;
        return index;
    }

    void SPRITE_POINTER_LIST::compactSparse() noexcept
    {

        const int oldCount = m_count;
        if (oldCount <= 0)
            return;

        int firstNull = 0;
        while (firstNull < oldCount && m_items[firstNull] != nullptr)
            ++firstNull;
        if (firstNull >= oldCount)
            return;

        int holes = 1;
        for (int src = firstNull + 1; src < oldCount; ++src)
        {
            if (m_items[src] != nullptr)
                m_items[src - holes] = m_items[src];
            else
                ++holes;
        }

        const int newCount = oldCount - holes;
        if (newCount > 0)
        {
            if (newCount < oldCount)
                m_count = newCount;
            return;
        }

        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }

    void SPRITE_POINTER_LIST::clear()
    {
        clearSpriteReferences();
    }

    void SPRITE_POINTER_LIST::deleteAllSprites()
    {
        int result = m_count;
        int base = 0;
        if (result > 0)
        {
            do
            {
                for (int probe = result - 1; probe > base; --probe)
                {
                    SPRITE* const baseSprite = m_items[base];
                    if (!baseSprite || m_items[probe] != baseSprite)
                        continue;

                    const int refs = baseSprite->listReferenceCount() - 1;
                    baseSprite->setListReferenceCount(refs);
                    if (refs < 0)
                    {
                        VID* const vid = baseSprite->Vid();
                        (void)logFileLoggerResourceError(
                            g_fileLogger, "SPRITE[%i](%i,%i,%i)", 4,
                            "noRef at Release", refs, vid ? vid->nvid() : -1,
                            static_cast<int>(baseSprite->X()),
                            static_cast<int>(baseSprite->Y()),
                            static_cast<int>(baseSprite->Z()));
                    }
                    else if (refs == 0)
                    {
                        DeleteSpriteThroughVirtualDeletingDestructor(baseSprite);
                    }

                    if (probe >= 0 && probe < m_count)
                    {
                        --m_count;
                        m_items[probe] = m_items[m_count];
                    }
                }
                result = m_count;
                ++base;
            } while (base < result);
        }

        for (int index = m_count - 1; index >= 0; --index)
        {
            SPRITE* const sprite = m_items[index];
            if (!sprite)
                continue;

            --m_count;
            m_items[index] = m_items[m_count];

            const int refs = sprite->listReferenceCount() - 1;
            sprite->setListReferenceCount(refs);
            if (refs < 0)
            {
                VID* const vid = sprite->Vid();
                (void)logFileLoggerResourceError(
                    g_fileLogger, "SPRITE[%i](%i,%i,%i)", 4,
                    "noRef at Release", refs, vid ? vid->nvid() : -1,
                    static_cast<int>(sprite->X()),
                    static_cast<int>(sprite->Y()),
                    static_cast<int>(sprite->Z()));
            }
            else
            {
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
            }
        }

        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }

    SPRITE* SPRITE_POINTER_LIST::NextNonNull(int* cursor) const
    {
        if (!cursor)
            return nullptr;
        int i = *cursor;
        if (i < 0)
            i = 0;
        for (; i < m_count; ++i)
        {
            SPRITE* sprite = m_items ? m_items[i] : nullptr;
            *cursor = i + 1;
            if (sprite)
                return sprite;
        }
        *cursor = m_count;
        return nullptr;
    }

    SPRITE* SPRITE_POINTER_LIST::beginReverseIteration(int* cursor) const noexcept
    {

        if (m_count == 0)
            return nullptr;
        const int index = m_count - 1;
        *cursor = index;
        return m_items[index];
    }

    SPRITE* SPRITE_POINTER_LIST::continueReverseIteration(int* cursor) const noexcept
    {

        if (*cursor > m_count)
            *cursor = m_count;
        const int index = *cursor - 1;
        *cursor = index;
        return index >= 0 ? m_items[index] : nullptr;
    }

    void SPRITE_POINTER_LIST::add(SPRITE* sprite)
    {
        append(sprite);
    }

    void SPRITE_POINTER_LIST::clearNoRelease() noexcept
    {
        m_count = 0;
    }

    void SPRITE_POINTER_LIST::releaseRepeatedReferences()
    {
        (void)releaseRepeatedReferencesRetail();
    }

    int SPRITE_POINTER_LIST::releaseRepeatedReferencesRetail()
    {
        int result = m_count;
        if (result > 0)
        {
            int base = 0;
            do
            {
                for (int probe = result - 1; probe > base; --probe)
                {
                    if (m_items[base] && m_items[probe] == m_items[base])
                        collapseDuplicateReference(static_cast<std::size_t>(probe));
                }
                result = m_count;
                ++base;
            }
            while (base < result);
        }
        for (int i = m_count - 1; i >= 0; --i)
        {
            if (m_items[i])
                result = releaseByIndexRetail(i);
        }
        return result;
    }

    void SPRITE_LIST::initializeListState() noexcept
    {
        initializeBaseSpriteListRecord();
    }

    void SPRITE_LIST::initializeBaseSpriteListRecord() noexcept
    {

        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setRetailVtableToken(CurrentImageBaseSpriteListVtable());
    }

    SPRITE_POINTER_LIST* SPRITE_POINTER_LIST::initializePointerListRecord() noexcept
    {

        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setRetailVtableToken(CurrentImageCoreListVtable());
        return this;
    }

    void SPRITE_POINTER_LIST::initializeHashBucketRecordState() noexcept
    {
        (void)initializePointerListRecord();
    }

    void* SPRITE_POINTER_LIST::pointerListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        if ((deletingDestructorFlags & 2u) != 0u)
        {
            unsigned char* const first = reinterpret_cast<unsigned char*>(this);
            auto* const cookie = reinterpret_cast<std::uint32_t*>(first) - 1;
            const std::uint32_t count = *cookie;
            for (std::uint32_t i = count; i != 0u; --i)
            {
                auto* const record = reinterpret_cast<SPRITE_POINTER_LIST*>(
                    first + static_cast<std::size_t>(i - 1u) * 0x10u);
                record->destroyCoreListStorage();
            }
            void* const result = static_cast<void*>(cookie);
            if ((deletingDestructorFlags & 1u) != 0u)
                ::operator delete(result);
            return result;
        }

        SPRITE_POINTER_LIST* const self = this;
        destroyCoreListStorage();
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SPRITE_POINTER_LIST::destroyCoreListStorage() noexcept
    {

        setRetailVtableToken(CurrentImageCoreListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
    }

    SPRITE_LIST* SPRITE_LIST::destroyListStorage(bool deleteSelfFlag) noexcept
    {
        return destroyBaseSpriteListRecord(deleteSelfFlag);
    }

    SPRITE_LIST* SPRITE_LIST::destroyBaseSpriteListRecord(bool deleteSelfFlag) noexcept
    {

        setRetailVtableToken(SPRITE_POINTER_LIST::CurrentImageCoreListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
        if (deleteSelfFlag)
            ::operator delete(this);
        return this;
    }

    void* SPRITE_LIST::baseSpriteListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        if ((deletingDestructorFlags & 2u) != 0u)
        {
            unsigned char* const first = reinterpret_cast<unsigned char*>(this);
            auto* const cookie = reinterpret_cast<std::uint32_t*>(first) - 1;
            const std::uint32_t count = *cookie;
            for (std::uint32_t i = count; i != 0u; --i)
            {
                auto* const record = reinterpret_cast<SPRITE_LIST*>(
                    first + static_cast<std::size_t>(i - 1u) * 0x10u);
                record->destroyBaseSpriteListRecord(false);
            }
            void* const allocation = static_cast<void*>(cookie);
            if ((deletingDestructorFlags & 1u) != 0u)
                ::operator delete(allocation);
            return allocation;
        }

        SPRITE_LIST* const self = this;
        destroyBaseSpriteListRecord(false);
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


    void SPRITE_POINTER_LIST::clearSpriteReferences()
    {
        while (m_count > 0)
            (void)releaseByIndexRetail(m_count - 1);
    }

    int SPRITE_POINTER_LIST::releaseOneByIndex(int index)
    {
        return releaseByIndexRetail(index);
    }

    int SPRITE_POINTER_LIST::releaseByIndexRetail(int index)
    {
        if (index < 0 || index >= m_count)
            return 1;

        SPRITE* const sprite = m_items[index];
        --m_count;
        m_items[index] = m_items[m_count];

        if (sprite)
        {
            const int refs = sprite->ReleaseListReference();
            if (refs != 0)
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
        }
        return 0;
    }

    void SPRITE_POINTER_LIST::releaseAllReferences()
    {
        clearSpriteReferences();
    }

    std::size_t SPRITE_POINTER_LIST::count() const noexcept
    {
        return m_count > 0 ? static_cast<std::size_t>(m_count) : 0u;
    }

    bool SPRITE_POINTER_LIST::empty() const noexcept
    {
        return m_count == 0;
    }

    SPRITE* SPRITE_POINTER_LIST::at(std::size_t index) const noexcept
    {
        return m_items && index < static_cast<std::size_t>(m_count) ? m_items[index] : nullptr;
    }

    int SPRITE_POINTER_LIST::activeCount() const noexcept
    {
        return m_count;
    }

    int SPRITE_POINTER_LIST::storageCapacity() const noexcept
    {
        return m_capacity;
    }

    SPRITE* const* SPRITE_POINTER_LIST::data() const noexcept
    {
        return m_items;
    }

    bool SPRITE_POINTER_LIST::contains(SPRITE* sprite) const noexcept
    {
        if (!m_items)
            return false;
        for (int i = 0; i < m_count; ++i)
            if (m_items[i] == sprite)
                return true;
        return false;
    }

    int SPRITE_POINTER_LIST::releaseOne(std::size_t index, bool eraseEntry)
    {
        if (!m_items || index >= static_cast<std::size_t>(m_count))
            return 1;
        SPRITE* sprite = m_items[index];
        if (eraseEntry)
        {
            --m_count;
            m_items[index] = m_items[m_count];
        }
        else
            m_items[index] = nullptr;
        if (sprite)
            releaseSpriteReference(sprite, true);
        return 0;
    }

    int SPRITE_POINTER_LIST::collapseDuplicateReference(std::size_t index)
    {
        if (index >= static_cast<std::size_t>(m_count))
            return 1;
        SPRITE* sprite = m_items[index];
        releaseSpriteReference(sprite, true);
        --m_count;
        m_items[index] = m_items[m_count];
        return 0;
    }

    int SPRITE_POINTER_LIST::releaseDuplicateAtIndex(std::size_t index)
    {
        return collapseDuplicateReference(index);
    }

    int SPRITE_POINTER_LIST::releaseSpriteReference(SPRITE* sprite, bool callDeleteWhenZero)
    {
        (void)callDeleteWhenZero;
        return sprite ? sprite->ReleaseListReference() : 0;
    }

    SPRITE_LIST& applicationGlobalSpriteList()
    {
        static SPRITE_LIST list;
        return list;
    }

    MENU& applicationMenu()
    {
#ifdef _WIN32
        if (void* const owner = as1::core::ApplicationPhysicalOwner())
            return *reinterpret_cast<MENU*>(static_cast<std::uint8_t*>(owner) + as1::core::retail_application_layout::Menu);
#endif
        static MENU portableFallback;
        return portableFallback;
    }

    SPRITE_LIST& applicationFrameSpriteList()
    {
        return applicationMenu();
    }

    SPRITE_OWNER_SLOT::SPRITE_OWNER_SLOT(DestroyProc destroyProc) noexcept
        : m_destroyProc(destroyProc)
    {
    }

    SPRITE_OWNER_SLOT::~SPRITE_OWNER_SLOT()
    {
        release();
    }

    void SPRITE_OWNER_SLOT::setDestroyProc(DestroyProc destroyProc) noexcept
    {
        m_destroyProc = destroyProc;
    }

    void SPRITE_OWNER_SLOT::bind(void* object) noexcept
    {
        m_object = object;
    }

    void* SPRITE_OWNER_SLOT::clearIfMatches(void* object) noexcept
    {
        void* previous = m_object;
        if (previous == object)
            m_object = nullptr;
        return previous;
    }

    void* SPRITE_OWNER_SLOT::get() const noexcept
    {
        return m_object;
    }

    bool SPRITE_OWNER_SLOT::empty() const noexcept
    {
        return m_object == nullptr;
    }

    int SPRITE_OWNER_SLOT::release()
    {
        void* object = m_object;
        if (object && m_destroyProc)
            m_destroyProc(object, true);
        m_object = nullptr;
        return object ? 1 : 0;
    }
}
