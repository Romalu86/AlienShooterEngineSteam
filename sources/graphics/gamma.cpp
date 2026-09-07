#include "gamma.h"

#if defined(_MSC_VER) && defined(_M_IX86)
#include <mmintrin.h>
#endif

namespace as1
{
    namespace
    {
        BYTE clampByteImpl(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return static_cast<BYTE>(value);
        }

        DWORD packARGB(BYTE a, BYTE r, BYTE g, BYTE b)
        {
            return (static_cast<DWORD>(a) << 24u) |
                   (static_cast<DWORD>(r) << 16u) |
                   (static_cast<DWORD>(g) << 8u)  |
                   static_cast<DWORD>(b);
        }

        BYTE satAddByte(BYTE a, BYTE b)
        {
            const unsigned int v = static_cast<unsigned int>(a) + static_cast<unsigned int>(b);
            return static_cast<BYTE>(v > 255u ? 255u : v);
        }
    }

    BYTE Gamma::clampByte(int value)
    {
        return clampByteImpl(value);
    }

    Gamma Gamma::FromRGB(int r, int g, int b)
    {

        return Gamma(255, clampByteImpl(r), clampByteImpl(g), clampByteImpl(b));
    }

    Gamma Gamma::FromARGB(int a, int r, int g, int b)
    {
        return Gamma(clampByteImpl(a), clampByteImpl(r), clampByteImpl(g), clampByteImpl(b));
    }

    Gamma Gamma::FromSignedDeltas(int a, int r, int g, int b)
    {
        auto clampSigned = [](int value) -> int
        {
            if (value < -255)
                return -255;
            if (value > 255)
                return 255;
            return value;
        };
        return Gamma(clampSigned(a), clampSigned(r), clampSigned(g), clampSigned(b));
    }

    void Gamma::setAlpha(int value)
    {
        alpha = clampByteImpl(value);
    }

    DWORD Gamma::toDword() const
    {
        return packARGB(clampByteImpl(alpha), clampByteImpl(red), clampByteImpl(green), clampByteImpl(blue));
    }

    Gamma Gamma::saturatedAdd(const Gamma& rhs) const
    {

        return Gamma(satAddByte(clampByteImpl(alpha), clampByteImpl(rhs.alpha)),
                     satAddByte(clampByteImpl(red),   clampByteImpl(rhs.red)),
                     satAddByte(clampByteImpl(green), clampByteImpl(rhs.green)),
                     satAddByte(clampByteImpl(blue),  clampByteImpl(rhs.blue)));
    }

    GammaRawPair* GammaRawPair::setSaturatingAddRetail(GammaRawPair lhs, GammaRawPair rhs) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        const __m64 left = *reinterpret_cast<const __m64*>(&lhs);
        const __m64 right = *reinterpret_cast<const __m64*>(&rhs);
        const __m64 sum = _m_paddusb(left, right);
        *reinterpret_cast<__m64*>(this) = sum;
        _mm_empty();
#else
        first = 0u;
        second = 0u;
        for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
        {
            const unsigned int shift = static_cast<unsigned int>(byteIndex * 8);
            const BYTE a0 = static_cast<BYTE>((lhs.first >> shift) & 0xFFu);
            const BYTE b0 = static_cast<BYTE>((rhs.first >> shift) & 0xFFu);
            const BYTE a1 = static_cast<BYTE>((lhs.second >> shift) & 0xFFu);
            const BYTE b1 = static_cast<BYTE>((rhs.second >> shift) & 0xFFu);
            first  |= static_cast<DWORD>(satAddByte(a0, b0)) << shift;
            second |= static_cast<DWORD>(satAddByte(a1, b1)) << shift;
        }
#endif
        return this;
    }

    GammaRawPair* copyGammaRawPair(GammaRawPair* destination, const GammaRawPair* source) noexcept
    {
        destination->first = source->first;
        destination->second = source->second;
        return destination;
    }

    GammaRawPair GammaRawCopy(const GammaRawPair& src)
    {
        GammaRawPair out{};
        copyGammaRawPair(&out, &src);
        return out;
    }

    DWORD* GammaRawCopyPackedColor(DWORD* destination, const DWORD* source)
    {

        *destination = *source;
        return destination;
    }

    GammaRawPair* GammaRawPair::setSignedDeltasRetail(int alphaDelta, int redDelta, int greenDelta, int blueDelta) noexcept
    {
        first = 0u;
        second = 0u;

        auto clampSigned = [](int value) -> int
        {
            if (value < -255)
                return -255;
            if (value > 255)
                return 255;
            return value;
        };

        const int a = clampSigned(alphaDelta);
        if (a >= 0)
            second |= (static_cast<DWORD>(a) & 0xFFu) << 24u;
        else
            first |= (static_cast<DWORD>(-a) & 0xFFu) << 24u;

        const int r = clampSigned(redDelta);
        if (r >= 0)
            second |= (static_cast<DWORD>(r) & 0xFFu) << 16u;
        else
            first |= (static_cast<DWORD>(-r) & 0xFFu) << 16u;

        const int g = clampSigned(greenDelta);
        if (g >= 0)
            second |= (static_cast<DWORD>(g) & 0xFFu) << 8u;
        else
            first |= (static_cast<DWORD>(-g) & 0xFFu) << 8u;

        const int b = clampSigned(blueDelta);
        if (b >= 0)
            second |= static_cast<DWORD>(b) & 0xFFu;
        else
            first |= static_cast<DWORD>(-b) & 0xFFu;

        return this;
    }

    DWORD* packOpaqueRgbClamped(DWORD* destination, int red, int green, int blue) noexcept
    {

        *destination = packARGB(255, clampByteImpl(red), clampByteImpl(green), clampByteImpl(blue));
        return destination;
    }

    DWORD GammaRawCreateOpaque(int red, int green, int blue)
    {
        DWORD out = 0;
        packOpaqueRgbClamped(&out, red, green, blue);
        return out;
    }

    DWORD* packArgbClamped(DWORD* destination, int alpha, int red, int green, int blue) noexcept
    {

        *destination = packARGB(clampByteImpl(alpha),
                                clampByteImpl(red),
                                clampByteImpl(green),
                                clampByteImpl(blue));
        return destination;
    }

    DWORD GammaRawCreateARGB(int alpha, int red, int green, int blue)
    {
        DWORD out = 0;
        packArgbClamped(&out, alpha, red, green, blue);
        return out;
    }

    DWORD* blendGammaRawPairWithColor(DWORD* destination, const GammaRawPair* maskAndColor, const DWORD* baseColor) noexcept
    {

        if (maskAndColor->first == 0u && maskAndColor->second == 0u)
        {
            *destination = *baseColor;
            return destination;
        }

        const DWORD mask = ~maskAndColor->first;
        const DWORD src = maskAndColor->second;
        const unsigned int baseB = *baseColor & 0xFFu;
        const unsigned int baseG = (*baseColor >> 8u) & 0xFFu;
        const unsigned int baseR = (*baseColor >> 16u) & 0xFFu;
        const unsigned int maskB = (mask & 0xFFu) + 1u;
        const unsigned int maskG = ((mask >> 8u) & 0xFFu) + 1u;
        const unsigned int maskR = ((mask >> 16u) & 0xFFu) + 1u;
        const int outB = static_cast<int>((src & 0xFFu) + ((baseB * maskB) >> 8u));
        const int outG = static_cast<int>(((src >> 8u) & 0xFFu) + ((baseG * maskG) >> 8u));
        const int outR = static_cast<int>(((src >> 16u) & 0xFFu) + ((baseR * maskR) >> 8u));
        *destination = packARGB(255, clampByteImpl(outR), clampByteImpl(outG), clampByteImpl(outB));
        return destination;
    }

    DWORD GammaRawBlend(const GammaRawPair& maskAndColor, DWORD baseColor)
    {
        DWORD out = 0;
        blendGammaRawPairWithColor(&out, &maskAndColor, &baseColor);
        return out;
    }
}
