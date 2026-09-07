#pragma once
#include "graphics/rect.h"
#include <cstdint>

namespace as1
{
    struct SOFTWARE_SURFACE16
    {
        std::uint16_t* pixels = nullptr;
        int width = 0;
        int height = 0;
        int pitchWords = 0;
    };

    bool softwareDrawCopy16(SOFTWARE_SURFACE16& dst, const RECTI& dstRect, const SOFTWARE_SURFACE16& src, const RECTI& srcRect, const RECTI& clipRect);
}
