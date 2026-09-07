#pragma once

#include <cstdint>

#include "base_sprite_list.h"
#include "core/as_string.h"

namespace as1
{
    namespace input { struct InputMessageState; }

    class MENU final : public SPRITE_LIST
    {
    public:
        MENU() noexcept;
        ~MENU();

        MENU(const MENU&) = delete;
        MENU& operator=(const MENU&) = delete;

        int Control(input::InputMessageState* input);

        int DeleteFromFile(const STRING& path);
        int Load(const STRING& path, int options = 0);

        int SelectedDirectionFrame() const noexcept;
        int SelectedNvid() const noexcept;

        unsigned controlFlags() const noexcept { return m_controlFlags; }
        SPRITE* selectedSprite() const noexcept { return m_selectedSprite; }
        void setSelectedSprite(SPRITE* sprite) noexcept { m_selectedSprite = sprite; }
        bool clearSelectedSpriteIfMatches(SPRITE* sprite) noexcept
        {
            if (m_selectedSprite != sprite)
                return false;
            m_selectedSprite = nullptr;
            return true;
        }

        void initializeMenuRecord() noexcept;
        MENU* destroyMenuRecord(bool deleteSelfFlag) noexcept;
        void* menuDeletingDestructor(unsigned char deletingDestructorFlags) noexcept;

        static constexpr std::uint32_t RETAIL_VTABLE_TOKEN = 0x004DB080u;
        static std::uint32_t CurrentImageMenuVtable() noexcept;

    private:
        unsigned m_controlFlags;        // +0x10, only low two bits are MENU latches
        SPRITE* m_selectedSprite; // +0x14
    };

#if INTPTR_MAX == INT32_MAX
                                                                                      
                                                                        
#endif
}
