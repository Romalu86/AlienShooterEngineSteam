#pragma once

#include "core/base_stream.h"
#include "core/types.h"
#include <cstdint>
#include <cstddef>

namespace as1 { namespace input
{
    struct InputWindowMessageContext;

    struct InputMessageState
    {
        std::uint32_t flags = 0;
        int wheelDelta = 0;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float clientX = 0.0f;
        float clientY = 0.0f;
        std::uint32_t lastCode = 0;
        std::uint32_t rawKeyCode = 0;

        InputMessageState* initializePreservingPersistentFlags() noexcept;
        void resetFrameState();
        void clearTransientButtons();
        std::uint32_t clearLeftButtonState();
        std::uint32_t clearRightButtonState();
        void clearFirstButtonTransient();
        void clearSecondButtonTransient();

        int handleWindowMessage(std::uintptr_t hwnd, std::uint32_t message,
                       std::uint32_t wParam, std::uint32_t lParam,
                       const InputWindowMessageContext* context);

        int writeRawState(BaseStream* stream) const;
        int readRawState(BaseStream* stream);
    };
                                                                                                          
                                                                                                            
                                                                                                              
                                                                                                              
                                                                                                                
                                                                                                                
                                                                                                                  
                                                                                                                      

    struct InputControlKeys
    {

        std::uint32_t left0 = 0x25;
        std::uint32_t left1 = 0x25;
        std::uint32_t right0 = 0x27;
        std::uint32_t right1 = 0x27;
        std::uint32_t up0 = 0x26;
        std::uint32_t up1 = 0x26;
        std::uint32_t down0 = 0x28;
        std::uint32_t down1 = 0x28;
        std::uint32_t first0 = 1;
        std::uint32_t first1 = 1;
        std::uint32_t second0 = 2;
        std::uint32_t second1 = 2;
        // These input-state values are independent
        // keyboard bindings for previous/next weapon (defaults '[' and ']').
        std::uint32_t previousWeapon = 0x5B;
        std::uint32_t nextWeapon = 0x5D;
        std::uint32_t firstMouseReleaseClears = 1;
        std::uint32_t secondMouseReleaseClears = 1;
    };
                                                                                                        
                                                                                                        
                                                                                                          
                                                                                                          
                                                                                                    
                                                                                                    
                                                                                                        
                                                                                                        
                                                                                                          
                                                                                                          
                                                                                                            
                                                                                                            
                                                                                                                          
                                                                                                                  
                                                                                                                                            
                                                                                                                                              
                                                                                      

    struct InputWindowRect
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    struct InputWindowMessageContext
    {
        using GetWindowRectFn = bool (*)(std::uintptr_t hwnd, InputWindowRect& rect, void* user);
        void* user = nullptr;
        GetWindowRectFn getWindowRect = nullptr;
        float viewportMinX = 0.0f;
        float viewportMaxX = 0.0f;
        float viewportMinY = 0.0f;
        float viewportMaxY = 0.0f;
        float worldOffsetX = 0.0f;
        float worldOffsetY = 0.0f;
        bool hasViewport = false;
    };

    struct InputWindowGlobals
    {
        int windowPositionX = 0;
        int windowPositionY = 0;
    };

    InputControlKeys& inputControlKeys();
    std::uint32_t& relativeControlEnabled() noexcept;
    InputWindowGlobals& inputWindowGlobals();

    int HandleInputWindowMessage(InputMessageState& state,
                                    std::uintptr_t hwnd,
                                    std::uint32_t message,
                                    std::uint32_t wParam,
                                    std::uint32_t lParam,
                                    const InputWindowMessageContext* context = nullptr);
} }
