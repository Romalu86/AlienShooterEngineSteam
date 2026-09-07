#pragma once

#include "base_sprite_list.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;
    class RESOURCE;
    class SPRITE;

    class Group : public SPRITE_POINTER_LIST
    {
    public:
        Group(Group* insertAfter = nullptr, SPRITE* firstSprite = nullptr) noexcept;
        ~Group();

        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

        Group* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        void unlinkAndReleaseStorage() noexcept;
        int drawGroupOrdinals() const;

        float centerX() const noexcept { return m_centerX; }
        float centerY() const noexcept { return m_centerY; }
        Group* nextGroup() const noexcept { return m_next; }
        void setNextGroup(Group* next) noexcept { m_next = next; }

        static constexpr std::uint32_t RETAIL_VTABLE_TOKEN = 0x0047372Cu; // compatibility token; never stored live
        static std::uint32_t CurrentImageGroupVtable() noexcept;

    private:
        friend class GROUPS;

        float m_centerX;          // +0x10
        float m_centerY;          // +0x14
        std::uint32_t m_raw18;
        std::uint32_t m_raw1C;
        Group* m_next;            // +0x20 (x86)
    };

    class GROUPS : public Group
    {
    public:
        GROUPS() noexcept;
        ~GROUPS();

        GROUPS(const GROUPS&) = delete;
        GROUPS& operator=(const GROUPS&) = delete;

        int loadFromResource(RESOURCE* map);
        int saveToResource(RESOURCE* map) const;
        void Load(RESOURCE* map);
        int Save(RESOURCE* map) const;
        void removeSpriteReferences(SPRITE* sprite);

        Group* first() noexcept;
        const Group* first() const noexcept;
        std::size_t size() const noexcept;
        bool empty() const noexcept;
        std::size_t refCount() const noexcept;
    };

#if defined(_WIN32) && !defined(_WIN64)
                                                                                         
                                                                                                    
#endif
}
