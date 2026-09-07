#include "graphics/software_draw.h"
#include <cstring>
#include <cstddef>

namespace as1
{
    bool softwareDrawCopy16(SOFTWARE_SURFACE16& dst, const RECTI& dstRect, const SOFTWARE_SURFACE16& src, const RECTI& srcRect, const RECTI& clipRect)
    {
        if (!dst.pixels || !src.pixels || dst.pitchWords <= 0 || src.pitchWords <= 0)
            return false;
        if (dstRect.empty() || srcRect.empty())
            return false;

        RECTI clippedDst;
        if (!intersectRect(clippedDst, dstRect, clipRect))
            return false;

        const int copyW = clippedDst.width();
        const int copyH = clippedDst.height();
        if (copyW <= 0 || copyH <= 0)
            return false;

        const int srcX = srcRect.left + (clippedDst.left - dstRect.left);
        const int srcY = srcRect.top + (clippedDst.top - dstRect.top);
        if (srcX < 0 || srcY < 0 || srcX + copyW > src.width || srcY + copyH > src.height)
            return false;
        if (clippedDst.left < 0 || clippedDst.top < 0 || clippedDst.right > dst.width || clippedDst.bottom > dst.height)
            return false;

        const std::uint16_t* srcLine = src.pixels + srcX + srcY * src.pitchWords;
        std::uint16_t* dstLine = dst.pixels + clippedDst.left + clippedDst.top * dst.pitchWords;
        const std::size_t bytes = static_cast<std::size_t>(copyW) * sizeof(std::uint16_t);
        for (int y = 0; y < copyH; ++y)
        {
            std::memcpy(dstLine, srcLine, bytes);
            srcLine += src.pitchWords;
            dstLine += dst.pitchWords;
        }
        return true;
    }
}
