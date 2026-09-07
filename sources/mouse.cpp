
#include "mouse.h"

#include "core/log.h"
#include "core/application.h"
#include "map.h"
#include "win/application_win.h"
#include "sprite_collector_hash.h"

#include <algorithm>
#include <new>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace as1
{
    namespace
    {
        constexpr const char* kCursorDirectory = "cursores\\";
        constexpr const char* kCursorExtension = ".ani";

        constexpr const char* kCursorNames[MOUSE::CursorCount] = {
            "arrow",
            "noammo",
            "move",
            "clash",
            "repair",
            "attack",
            "farattack",
            "select",
            "nomove",
            "cycle",
            "link",
            "unlink",
            "cantmove",
            "patrol",
            "delete",
            "capture",
            "mine",
            "unmine",
            "small-arrow",
            "small-noammo",
            "small-move",
            "small-taran",
            "small-repair",
            "small-attack",
            "small-farattack",
            "small-select",
            "small-nomove",
            "cycle",
            "small-link",
            "small-unlink",
            "small-cantmove",
            "small-patrol",
            "delete",
            "small-capture",
            "small-mine",
            "small-unmine",
        };


#ifdef _WIN32
        HCURSOR asCursor(void* handle) noexcept
        {
            return static_cast<HCURSOR>(handle);
        }

        void* fromCursor(HCURSOR handle) noexcept
        {
            return static_cast<void*>(handle);
        }
#else
        void* asCursor(void* handle) noexcept
        {
            return handle;
        }

        void* fromCursor(void* handle) noexcept
        {
            return handle;
        }
#endif
    }

    MOUSE* Mouse = nullptr;

    MOUSE::MOUSE()
        : MOUSE(MAP::NullVid(), 0.0f, 0.0f, 0.0f, 0, nullptr)
    {
    }

    MOUSE::MOUSE(VID* vid, float x, float y, float z, int direction, SPRITE* parent)
        : SPRITE(nullptr, vid, VECTOR{x, y, z}, ANGLE(direction), parent)
    {

        setCursorHandlesLoaded(0);
        m_cursorHandles.fill(nullptr);
        setHardwareCursorEnabled(1);

        if (Vid() != MAP::NullVid() || childChain() != nullptr)
            removeFromDrawBucketsRecursive();

        if (Vid() == MAP::NullVid())
            setListReferenceCount(listReferenceCount() + 1);
    }

    MOUSE::~MOUSE()
    {
        // Language destructor represents destroyMouseState prefix; SPRITE::~SPRITE
        // performs the final destroyBaseSpriteState exactly once.
        LOG::Write("Mouse  release");
    }

    MOUSE* MOUSE::scalarDeletingDestructor(unsigned char flags) noexcept
    {

        MOUSE* const self = this;
        destroyMouseState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void MOUSE::destroyMouseState()
    {

        LOG::Write("Mouse  release");
        destroyBaseSpriteState();
    }

    void* MOUSE::cursorHandle(std::size_t index) const noexcept
    {
        if (index >= m_cursorHandles.size())
            return nullptr;
        return m_cursorHandles[index];
    }

    void MOUSE::setCursorHandle(std::size_t index, void* handle) noexcept
    {
        if (index < m_cursorHandles.size())
            m_cursorHandles[index] = handle;
    }

    void MOUSE::HardwareOn()
    {

        if (m_cursorHandlesLoaded)
            return;

        m_cursorHandlesLoaded = 1;
#ifdef _WIN32
        ::SetCursor(nullptr);
#endif
        if (!m_hardwareCursorEnabled)
            return;

#ifdef _WIN32
        for (std::size_t i = 0; i < m_cursorHandles.size(); ++i)
        {
            if (m_cursorHandles[i])
            {
                ::DestroyCursor(asCursor(m_cursorHandles[i]));
                m_cursorHandles[i] = nullptr;
            }

            const char* name = kCursorNames[i];
            if (name && name[0])
            {
                std::string fileName;
                fileName.reserve(std::strlen(kCursorDirectory) + std::strlen(name) + std::strlen(kCursorExtension) + 1);
                fileName += kCursorDirectory;
                fileName += name;
                fileName += kCursorExtension;
                m_cursorHandles[i] = fromCursor(::LoadCursorFromFileA(fileName.c_str()));
            }

            if (!m_cursorHandles[i])
                m_cursorHandles[i] = fromCursor(::LoadCursorA(nullptr, IDC_ARROW));
        }

        const int cursorId = currentCursorId();
        if (cursorId >= 0 && static_cast<std::size_t>(cursorId) < m_cursorHandles.size())
            ::SetCursor(asCursor(m_cursorHandles[static_cast<std::size_t>(cursorId)]));
#else
        // Non-Windows syntax/test build: preserve slot and ownership semantics only.
        for (void*& slot : m_cursorHandles)
            slot = nullptr;
#endif
    }

    void MOUSE::HardwareOff()
    {

        if (!m_cursorHandlesLoaded)
            return;

        m_cursorHandlesLoaded = 0;
        if (!m_hardwareCursorEnabled)
            return;

#ifdef _WIN32
        ::SetCursor(nullptr);
        for (void*& slot : m_cursorHandles)
        {
            if (slot)
            {
                ::DestroyCursor(asCursor(slot));
                slot = nullptr;
            }
        }
#else
        for (void*& slot : m_cursorHandles)
            slot = nullptr;
#endif
    }

    int MOUSE::Action(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3)
    {
        if (opcode == 0x3D)
        {
            setCursorId(static_cast<int>(rawVar1));
            return 0;
        }

        if (opcode == 0x3F)
        {
#ifdef _WIN32
            RECT rect;
            ::GetWindowRect(win::applicationWinInstance()->nativeWindow(), &rect);
            ::SetCursorPos(rect.left + static_cast<int>(rawVar1), rect.top + rawVar2);
#else
            (void)rawVar1;
            (void)rawVar2;
#endif
            (void)rawVar3;
            return 0;
        }

        if (opcode != 0x3E)
            return dispatchActionOpcode(static_cast<std::uint32_t>(opcode),
                              static_cast<int>(rawVar1), rawVar2, rawVar3);

        const int vidIndex = static_cast<int>(rawVar1);
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (vidIndex < 0 || vidIndex >= table.count())
            return 0;

        VID* const replacement = table.slot(vidIndex);
        if (!replacement)
            return 0;

        VID* const current = Vid();
        if (current && current->nvid() == vidIndex)
            return 0;

        addToDrawBucketsRecursive();
        (void)dispatchActionOpcode(static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_VID), vidIndex, rawVar2, rawVar3);
        for (SPRITE* node = this; node; node = node->childChain())
            (void)RemoveSpriteFromGlobalHashForActionSwitch(node);
        removeFromDrawBucketsRecursive();
        return 0;
    }


    void MOUSE::enableHardwareCursorModeRetail()
    {
        if (m_hardwareCursorEnabled != 0)
            return;

        HardwareOff();
        setVidPointerDirect(MAP::NullVid());
        m_hardwareCursorEnabled = 1;
        HardwareOn();
    }

    void MOUSE::disableHardwareCursorModeRetail()
    {
        if (m_hardwareCursorEnabled == 0)
            return;

        HardwareOff();
        VID* softwareVid = MAP::NullVid();
        const core::ApplicationVidTable& vids = core::GlobalApplicationVidTable();
        if (vids.count() > 1 && vids.slot(1))
            softwareVid = vids.slot(1);

        const int savedAnimation = currentCursorId();
        setVidPointerDirect(softwareVid);
        SPRITE::ChangeAnimation(savedAnimation == 0 ? 1 : 0);
        SPRITE::ChangeAnimation(savedAnimation);
        m_hardwareCursorEnabled = 0;
        HardwareOn();
    }

    void MOUSE::setCursorId(int cursorId)
    {

        if (m_hardwareCursorEnabled)
        {
#ifdef _WIN32
            if (currentCursorId() != cursorId && m_cursorHandlesLoaded &&
                cursorId >= 0 && static_cast<std::size_t>(cursorId) < m_cursorHandles.size())
                ::SetCursor(asCursor(m_cursorHandles[static_cast<std::size_t>(cursorId)]));
#endif
            if (cursorId < AnimatedCursorCount)
            {
                SPRITE::ChangeAnimation(cursorId);
                return;
            }

            setCurrentCursorIdDirect(cursorId);
            return;
        }

        SPRITE::ChangeAnimation(cursorId);
    }

}
