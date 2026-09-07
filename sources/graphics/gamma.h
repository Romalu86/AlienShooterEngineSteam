#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
using BYTE  = std::uint8_t;
using DWORD = std::uint32_t;
#endif

namespace as1
{
    struct GammaRawPair
    {
        DWORD first = 0;
        DWORD second = 0;

        GammaRawPair* setSignedDeltasRetail(int alphaDelta, int redDelta, int greenDelta, int blueDelta) noexcept;
        GammaRawPair* setSaturatingAddRetail(GammaRawPair lhs, GammaRawPair rhs) noexcept;
    };
                                                                                           

    struct Gamma
    {

        int alpha = 0;
        int red = 0;
        int green = 0;
        int blue = 0;

        Gamma() = default;
        Gamma(int a, int r, int g, int b) : alpha(a), red(r), green(g), blue(b) {}

        bool isDefault() const { return alpha == 0 && red == 0 && green == 0 && blue == 0; }

        static Gamma FromRGB(int r, int g, int b);
        static Gamma FromARGB(int a, int r, int g, int b);

        static Gamma FromSignedDeltas(int a, int r, int g, int b);

        void setAlpha(int value);
        DWORD toDword() const;

        Gamma saturatedAdd(const Gamma& rhs) const;

        static BYTE clampByte(int value);
    };

    GammaRawPair* copyGammaRawPair(GammaRawPair* destination, const GammaRawPair* source) noexcept;
    GammaRawPair GammaRawCopy(const GammaRawPair& src);

    DWORD* GammaRawCopyPackedColor(DWORD* destination, const DWORD* source);


    DWORD* packOpaqueRgbClamped(DWORD* destination, int red, int green, int blue) noexcept;
    DWORD GammaRawCreateOpaque(int red, int green, int blue);

    DWORD* packArgbClamped(DWORD* destination, int alpha, int red, int green, int blue) noexcept;
    DWORD GammaRawCreateARGB(int alpha, int red, int green, int blue);
    DWORD* blendGammaRawPairWithColor(DWORD* destination, const GammaRawPair* maskAndColor, const DWORD* baseColor) noexcept;
    DWORD GammaRawBlend(const GammaRawPair& maskAndColor, DWORD baseColor);
}
