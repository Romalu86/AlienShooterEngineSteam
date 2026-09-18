#include "vid/vid_font.h"

#include "graph.h"
#include "map.h"
#include "sprite.h"
#include "core/log.h"

#include <cmath>
#include <new>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>

namespace as1
{
    namespace
    {
        int fontCvttss2si(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return _mm_cvtt_ss2si(_mm_set_ss(value));
#else
            if (!(value >= -2147483648.0f && value < 2147483648.0f))
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<int>(value);
#endif
        }

        int fontFtolInt64Low32(float value) noexcept
        {
            // sub_479730 does not reuse the CVTTSS2SI result for the font
            // constructor.  It calls the retail float->int64 helper (sub_47D1A0)
            // and pushes EAX, i.e. the low 32 bits of the truncated int64.
            const double widened = static_cast<double>(value);
            if (!std::isfinite(widened) ||
                widened >= 9223372036854775808.0 ||
                widened < -9223372036854775808.0)
            {
                // Retail helper returns INT64_MIN; its low DWORD is zero.
                return 0;
            }
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(widened));
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(converted));
        }
    }

    VID_FONT::VID_FONT() = default;

    VID_FONT::VID_FONT(const VID_FONT& other)
        : VID()
    {
        nextMirror = other.nextMirrorVid();
        const_cast<VID_FONT&>(other).nextMirror = this;
        layer = other.layer;
        type = other.type;
        frameSpeedDefault = other.frameSpeedDefault;
        noCadr = other.noCadr;
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));
        m_fontOwner = other.m_fontOwner;
    }

    VID_FONT::~VID_FONT()
    {
        if (isMirrorChainOwner())
            destroyRetailVidFontOwner(m_fontOwner);
        else
            m_fontOwner = nullptr;
    }

    VID_FONT* VID_FONT::CreateMirror()
    {
        return new (std::nothrow) VID_FONT(*this);
    }

    void VID_FONT::Load(RESOURCE*)
    {
        const int storedSizeX = fontCvttss2si(sizeXYZ.x);
        const int storedSizeY = fontCvttss2si(sizeXYZ.y);

        frameSpeedDefault = 71u;
        noCadr = 256;
        setVidWidth(static_cast<short>(storedSizeX));
        setVidHeight(static_cast<short>(storedSizeY));
        type = static_cast<WORD>(VID_TYPE_FONT);

        const int fontSizeX = fontFtolInt64Low32(sizeXYZ.x);
        const int fontSizeY = fontFtolInt64Low32(sizeXYZ.y);
        m_fontOwner = createRetailVidFontOwner(vidName, fontSizeX, fontSizeY);
    }

    void VID_FONT::Draw(const SPRITE* sprite)
    {
        if (!m_fontOwner)
            return;

        GammaRawPair selected{};
        sprite->buildRetailGammaPair(selected);

        MAP* const map = MAP::Current();
        const float x = map->ToScreenX(sprite->X());
        const float y = map->ToScreenY(sprite->Y(), sprite->Z());
        (void)drawRetailVidFontOwner(m_fontOwner, x, y, ~selected.first, "", 0u);
    }


    void VID_FONT::SetLayer()
    {
        layer = 14;
    }

    int VID_FONT::InvalidateDeviceObjects() noexcept
    {
        LOG::Write("InvalidateDeviceObjects(%i)", nVid);
        return invalidateRetailVidFontOwner(m_fontOwner);
    }

    int VID_FONT::RestoreDeviceObjects() noexcept
    {
        return restoreRetailVidFontOwner(m_fontOwner);
    }
}
