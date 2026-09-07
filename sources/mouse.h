
#pragma once

#include "sprite.h"
#include <array>
#include <cstddef>

namespace as1
{
    class MOUSE : public SPRITE
    {
    public:
        static constexpr std::size_t CursorCount = 36u;
        static constexpr int AnimatedCursorCount = 17;

        MOUSE();
        MOUSE(VID* vid, float x, float y, float z, int direction, SPRITE* parent);
        virtual ~MOUSE();


        int Action(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3) override;
        MOUSE* scalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyMouseState();
        void HardwareOn();
        void HardwareOff();
        void enableHardwareCursorModeRetail();
        void disableHardwareCursorModeRetail();
        void setCursorId(int cursorId);
        void ChangeAnimation(int animationId) { SPRITE::ChangeAnimation(animationId); }

        int currentCursorId() const noexcept { return currentAnimation(); }
        void setCurrentCursorIdDirect(int value) noexcept { setCurrentAnimationDirect(value); }
        int hardwareCursorEnabled() const noexcept { return m_hardwareCursorEnabled; }
        void setHardwareCursorEnabled(int value) noexcept { m_hardwareCursorEnabled = value; }
        int cursorHandlesLoaded() const noexcept { return m_cursorHandlesLoaded; }
        void setCursorHandlesLoaded(int value) noexcept { m_cursorHandlesLoaded = value; }
        void* cursorHandle(std::size_t index) const noexcept;
        void setCursorHandle(std::size_t index, void* handle) noexcept;

    private:
        friend struct MouseRetailLayoutProbe;
        int m_hardwareCursorEnabled = 1;
        std::array<void*, CursorCount> m_cursorHandles{};
        int m_cursorHandlesLoaded = 0;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct MouseRetailLayoutProbe
    {
        static constexpr std::size_t hardware74 = offsetof(MOUSE, m_hardwareCursorEnabled);
        static constexpr std::size_t handles78 = offsetof(MOUSE, m_cursorHandles);
        static constexpr std::size_t loaded108 = offsetof(MOUSE, m_cursorHandlesLoaded);
    };
                                                                                                  
                                                                                                  
                                                                                                 
                                                                                                  
#endif

    extern MOUSE* Mouse;
    inline MOUSE*& mouseInstanceRef() noexcept { return Mouse; }
    inline SPRITE* mouseSprite() noexcept { return static_cast<SPRITE*>(Mouse); }

}
