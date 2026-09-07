#include "input.h"
#include "mouse.h"
#include <algorithm>
#include <cstring>

namespace as1 { namespace input
{
    namespace
    {
        constexpr std::uint32_t WM_MOVE_MESSAGE = 0x0003;
        constexpr std::uint32_t WM_ACTIVATEAPP_MESSAGE = 0x001C;
        constexpr std::uint32_t WM_NCHITTEST_MESSAGE = 0x0084;
        constexpr std::uint32_t WM_KEYDOWN_MESSAGE = 0x0100;
        constexpr std::uint32_t WM_KEYUP_MESSAGE = 0x0101;
        constexpr std::uint32_t WM_CHAR_MESSAGE = 0x0102;
        constexpr std::uint32_t WM_LBUTTONDOWN_MESSAGE = 0x0201;
        constexpr std::uint32_t WM_LBUTTONUP_MESSAGE = 0x0202;
        constexpr std::uint32_t WM_RBUTTONDOWN_MESSAGE = 0x0204;
        constexpr std::uint32_t WM_RBUTTONUP_MESSAGE = 0x0205;
        constexpr std::uint32_t WM_MBUTTONDOWN_MESSAGE = 0x0207;
        constexpr std::uint32_t WM_MOUSEWHEEL_MESSAGE = 0x020A;

        constexpr std::uint32_t VK_SHIFT_KEY = 0x10;
        constexpr std::uint32_t VK_CONTROL_KEY = 0x11;

        constexpr std::uint32_t FLAG_LEFT_KEY = 0x00000080;
        constexpr std::uint32_t FLAG_LEFT_BUTTON_EVENT = 0x00000001;
        constexpr std::uint32_t FLAG_MIDDLE_BUTTON_EVENT = 0x00000002;
        constexpr std::uint32_t FLAG_RIGHT_BUTTON_EVENT = 0x00000004;
        constexpr std::uint32_t FLAG_LEFT_BUTTON_RELEASE = 0x00000008;
        constexpr std::uint32_t FLAG_RIGHT_BUTTON_RELEASE = 0x00000010;
        constexpr std::uint32_t FLAG_LEFT_BUTTON_DOWN = 0x00000020;
        constexpr std::uint32_t FLAG_RIGHT_BUTTON_DOWN = 0x00000040;

        constexpr std::uint32_t FLAG_RIGHT_KEY = 0x00000100;
        constexpr std::uint32_t FLAG_DOWN_KEY = 0x00000200;
        constexpr std::uint32_t FLAG_UP_KEY = 0x00000400;
        constexpr std::uint32_t FLAG_SHIFT_KEY = 0x00000800;
        constexpr std::uint32_t FLAG_CONTROL_KEY = 0x00001000;
        constexpr std::uint32_t FLAG_FIRST_ACTION = 0x00004000;
        constexpr std::uint32_t FLAG_SECOND_ACTION = 0x00008000;

        constexpr std::uint32_t RESET_KEEP_MASK = 0xFFFFFFE0u;
        constexpr std::uint32_t INITIALIZE_KEEP_MASK = 0xFFFF2000u;
        constexpr std::uint32_t DEACTIVATE_KEEP_MASK = 0xFFFFC7FFu;

        bool keyEquals(std::uint32_t key, std::uint32_t a, std::uint32_t b) noexcept
        {
            return key == a || key == b;
        }

        short highWordSigned(std::uint32_t value) noexcept
        {
            return static_cast<short>((value >> 16) & 0xFFFFu);
        }

        int highWordUnsigned(std::uint32_t value) noexcept
        {
            return static_cast<int>((value >> 16) & 0xFFFFu);
        }

        int lowWordUnsigned(std::uint32_t value) noexcept
        {
            return static_cast<int>(value & 0xFFFFu);
        }

        bool defaultRect(std::uintptr_t, InputWindowRect&, void*) noexcept
        {
            return false;
        }

        bool queryRect(std::uintptr_t hwnd, InputWindowRect& rect, const InputWindowMessageContext* context)
        {
            if (context && context->getWindowRect)
                return context->getWindowRect(hwnd, rect, context->user);
            return defaultRect(hwnd, rect, nullptr);
        }

        bool updateMouseCoordinates(InputMessageState& state,
                                    std::uintptr_t hwnd,
                                    std::uint32_t lParam,
                                    const InputWindowMessageContext* context)
        {
            InputWindowRect rect;
            queryRect(hwnd, rect, context);
            inputWindowGlobals().windowPositionX = rect.left;
            inputWindowGlobals().windowPositionY = rect.top;

            const int screenX = lowWordUnsigned(lParam);
            const int screenY = highWordUnsigned(lParam);
            const float clientX = static_cast<float>(screenX - rect.left);
            const float clientY = static_cast<float>(screenY - rect.top);
            state.clientX = clientX;
            state.clientY = clientY;

            float clampedX = clientX;
            float clampedY = clientY;
            if (context && context->hasViewport)
            {
                if (clampedX < context->viewportMinX)
                    clampedX = context->viewportMinX;
                if (clampedX >= context->viewportMaxX)
                    clampedX = context->viewportMaxX - 1.0f;
                if (clampedY < context->viewportMinY)
                    clampedY = context->viewportMinY;
                if (clampedY >= context->viewportMaxY)
                    clampedY = context->viewportMaxY - 1.0f;
            }

            state.worldX = clampedX + context->worldOffsetX;
            state.worldY = clampedY + context->worldOffsetY;

            Mouse->ChangeCoor(state.worldX, state.worldY, Mouse->Z());

            return clientX >= context->viewportMinX && clientX < context->viewportMaxX &&
                   clientY >= context->viewportMinY && clientY < context->viewportMaxY;
        }

        void applyKeyDown(InputMessageState& state, std::uint32_t key)
        {
            state.lastCode = key << 8;
            state.rawKeyCode = key;
            const InputControlKeys& keys = inputControlKeys();
            if (keyEquals(key, keys.left0, keys.left1))
                state.flags |= FLAG_LEFT_KEY;
            else if (keyEquals(key, keys.right0, keys.right1))
                state.flags |= FLAG_RIGHT_KEY;
            else if (keyEquals(key, keys.up0, keys.up1))
                state.flags |= FLAG_UP_KEY;
            else if (keyEquals(key, keys.down0, keys.down1))
                state.flags |= FLAG_DOWN_KEY;
            else if (keyEquals(key, keys.first0, keys.first1))
                state.flags |= FLAG_FIRST_ACTION;
            else if (keyEquals(key, keys.second0, keys.second1))
                state.flags |= FLAG_SECOND_ACTION;
            else if (key == VK_SHIFT_KEY)
                state.flags |= FLAG_SHIFT_KEY;
            else if (key == VK_CONTROL_KEY)
                state.flags |= FLAG_CONTROL_KEY;
        }

        void applyKeyUp(InputMessageState& state, std::uint32_t key)
        {
            const InputControlKeys& keys = inputControlKeys();
            if (keyEquals(key, keys.left0, keys.left1))
                state.flags &= ~FLAG_LEFT_KEY;
            else if (keyEquals(key, keys.right0, keys.right1))
                state.flags &= ~FLAG_RIGHT_KEY;
            else if (keyEquals(key, keys.up0, keys.up1))
                state.flags &= ~FLAG_UP_KEY;
            else if (keyEquals(key, keys.down0, keys.down1))
                state.flags &= ~FLAG_DOWN_KEY;
            else if (keyEquals(key, keys.first0, keys.first1))
                state.flags &= ~FLAG_FIRST_ACTION;
            else if (keyEquals(key, keys.second0, keys.second1))
                state.flags &= ~FLAG_SECOND_ACTION;
            else if (key == VK_SHIFT_KEY)
                state.flags &= ~FLAG_SHIFT_KEY;
            else if (key == VK_CONTROL_KEY)
                state.flags &= ~FLAG_CONTROL_KEY;
        }

        void writeRawBytes(const InputMessageState& state, unsigned char (&raw)[0x20])
        {
            std::memcpy(raw + 0x00, &state.flags, 4);
            std::memcpy(raw + 0x04, &state.wheelDelta, 4);
            std::memcpy(raw + 0x08, &state.worldX, 4);
            std::memcpy(raw + 0x0C, &state.worldY, 4);
            std::memcpy(raw + 0x10, &state.clientX, 4);
            std::memcpy(raw + 0x14, &state.clientY, 4);
            std::memcpy(raw + 0x18, &state.lastCode, 4);
            std::memcpy(raw + 0x1C, &state.rawKeyCode, 4);
        }

        void readRawBytes(InputMessageState& state, const unsigned char (&raw)[0x20])
        {
            std::memcpy(&state.flags, raw + 0x00, 4);
            std::memcpy(&state.wheelDelta, raw + 0x04, 4);
            std::memcpy(&state.worldX, raw + 0x08, 4);
            std::memcpy(&state.worldY, raw + 0x0C, 4);
            std::memcpy(&state.clientX, raw + 0x10, 4);
            std::memcpy(&state.clientY, raw + 0x14, 4);
            std::memcpy(&state.lastCode, raw + 0x18, 4);
            std::memcpy(&state.rawKeyCode, raw + 0x1C, 4);
        }
    }

    InputControlKeys& inputControlKeys()
    {

        static InputControlKeys keys;
        return keys;
    }

    std::uint32_t& relativeControlEnabled() noexcept
    {

        static std::uint32_t relative = 0;
        return relative;
    }

    InputWindowGlobals& inputWindowGlobals()
    {

        static InputWindowGlobals globals;
        return globals;
    }

    InputMessageState* InputMessageState::initializePreservingPersistentFlags() noexcept
    {

        flags &= INITIALIZE_KEEP_MASK;
        wheelDelta = 0;
        clientX = 0.0f;
        worldX = 0.0f;
        clientY = 0.0f;
        worldY = 0.0f;
        lastCode = 0;
        rawKeyCode = 0;
        return this;
    }

    void InputMessageState::resetFrameState()
    {

        flags &= RESET_KEEP_MASK;
        lastCode = 0;
        rawKeyCode = 0;
        wheelDelta = 0;
        const InputControlKeys& keys = inputControlKeys();
        if (keys.firstMouseReleaseClears == 0)
            flags &= ~FLAG_FIRST_ACTION;
        if (keys.secondMouseReleaseClears == 0)
            flags &= ~FLAG_SECOND_ACTION;
    }

    void InputMessageState::clearTransientButtons()
    {

        flags &= 0xFFFFFFE0u;
        lastCode = 0;
        rawKeyCode = 0;
        wheelDelta = 0;
        const InputControlKeys& keys = inputControlKeys();
        if (keys.firstMouseReleaseClears == 0)
            flags &= ~FLAG_FIRST_ACTION;
        if (keys.secondMouseReleaseClears == 0)
            flags &= ~FLAG_SECOND_ACTION;
    }

    std::uint32_t InputMessageState::clearLeftButtonState()
    {

        std::uint32_t eax = flags;
        eax &= ~FLAG_LEFT_BUTTON_EVENT;
        flags = eax;
        const InputControlKeys& keys = inputControlKeys();
        if (keys.first0 == 1)
        {
            eax &= ~FLAG_FIRST_ACTION;
            flags = eax;
        }
        if (keys.second0 == 1)
        {
            eax = flags;
            eax &= ~FLAG_SECOND_ACTION;
            flags = eax;
        }
        return eax;
    }

    std::uint32_t InputMessageState::clearRightButtonState()
    {

        std::uint32_t eax = flags;
        eax &= ~FLAG_RIGHT_BUTTON_EVENT;
        flags = eax;
        const InputControlKeys& keys = inputControlKeys();
        if (keys.first0 == 2)
        {
            eax &= ~FLAG_FIRST_ACTION;
            flags = eax;
        }
        if (keys.second0 == 2)
        {
            eax = flags;
            eax &= ~FLAG_SECOND_ACTION;
            flags = eax;
        }
        return eax;
    }

    void InputMessageState::clearFirstButtonTransient()
    {
        (void)clearLeftButtonState();
    }

    void InputMessageState::clearSecondButtonTransient()
    {
        (void)clearRightButtonState();
    }

    int InputMessageState::writeRawState(BaseStream* stream) const
    {

        unsigned char raw[0x20];
        writeRawBytes(*this, raw);
        return stream->write(raw, sizeof(raw));
    }

    int InputMessageState::readRawState(BaseStream* stream)
    {

        unsigned char raw[0x20] = {};
        const int result = stream->read(raw, sizeof(raw));
        readRawBytes(*this, raw);
        return result;
    }

    int InputMessageState::handleWindowMessage(std::uintptr_t hwnd,
                                      std::uint32_t message,
                                      std::uint32_t wParam,
                                      std::uint32_t lParam,
                                      const InputWindowMessageContext* context)
    {
        InputMessageState& state = *this;

        switch (message)
        {
        case WM_NCHITTEST_MESSAGE:
            return updateMouseCoordinates(state, hwnd, lParam, context) ? 1 : 0;

        case WM_ACTIVATEAPP_MESSAGE:
            if (wParam == 0)
                state.flags &= DEACTIVATE_KEEP_MASK;
            return 0;

        case WM_MOVE_MESSAGE:
        {
            InputWindowRect rect;
            queryRect(hwnd, rect, context);
            inputWindowGlobals().windowPositionX = rect.left;
            inputWindowGlobals().windowPositionY = rect.top;
            return 0;
        }

        case WM_KEYDOWN_MESSAGE:
            applyKeyDown(state, wParam);
            return 0;

        case WM_KEYUP_MESSAGE:
            applyKeyUp(state, wParam);
            return 0;

        case WM_CHAR_MESSAGE:
            state.lastCode = wParam & 0xFFu;
            return 0;

        case WM_LBUTTONDOWN_MESSAGE:
            state.flags |= FLAG_LEFT_BUTTON_EVENT | FLAG_LEFT_BUTTON_DOWN;
            if (inputControlKeys().first0 == 1)
                state.flags |= FLAG_FIRST_ACTION;
            if (inputControlKeys().second0 == 1)
                state.flags |= FLAG_SECOND_ACTION;
            return 0;

        case WM_LBUTTONUP_MESSAGE:
            state.flags &= ~FLAG_LEFT_BUTTON_DOWN;
            state.flags |= FLAG_LEFT_BUTTON_RELEASE;
            if (inputControlKeys().first0 == 1 && inputControlKeys().firstMouseReleaseClears)
                state.flags &= ~FLAG_FIRST_ACTION;
            if (inputControlKeys().second0 == 1 && inputControlKeys().secondMouseReleaseClears)
                state.flags &= ~FLAG_SECOND_ACTION;
            return 0;

        case WM_RBUTTONDOWN_MESSAGE:
            state.flags |= FLAG_RIGHT_BUTTON_EVENT | FLAG_RIGHT_BUTTON_DOWN;
            if (inputControlKeys().first0 == 2)
                state.flags |= FLAG_FIRST_ACTION;
            if (inputControlKeys().second0 == 2)
                state.flags |= FLAG_SECOND_ACTION;
            return 0;

        case WM_RBUTTONUP_MESSAGE:
            state.flags &= ~FLAG_RIGHT_BUTTON_DOWN;
            state.flags |= FLAG_RIGHT_BUTTON_RELEASE;
            if (inputControlKeys().first0 == 2 && inputControlKeys().firstMouseReleaseClears)
                state.flags &= ~FLAG_FIRST_ACTION;
            if (inputControlKeys().second0 == 2 && inputControlKeys().secondMouseReleaseClears)
                state.flags &= ~FLAG_SECOND_ACTION;
            return 0;

        case WM_MBUTTONDOWN_MESSAGE:
            state.flags |= FLAG_MIDDLE_BUTTON_EVENT;
            return 0;

        case WM_MOUSEWHEEL_MESSAGE:

            state.wheelDelta = static_cast<int>(highWordSigned(wParam)) / 120;
            return 0;

        default:
            return 0;
        }
    }
    int HandleInputWindowMessage(InputMessageState& state,
                                    std::uintptr_t hwnd,
                                    std::uint32_t message,
                                    std::uint32_t wParam,
                                    std::uint32_t lParam,
                                    const InputWindowMessageContext* context)
    {
        return state.handleWindowMessage(hwnd, message, wParam, lParam, context);
    }

} }
