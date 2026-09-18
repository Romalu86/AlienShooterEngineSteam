#include "angle.h"
#include "../core/base_stream.h"
#include <cstdint>
#include <cstddef>
#include <cmath>
#if defined(_MSC_VER) && defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace as1
{
    namespace
    {
        int kCordicAngleSteps[7] = {64, 37, 19, 10, 5, 2, 1};

        int sar32(std::int32_t value, int shift)
        {
            return static_cast<int>(value >> shift);
        }

        std::int32_t add32Wrap(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
        }

        std::int32_t sub32Wrap(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        std::int32_t neg32Wrap(std::int32_t value) noexcept
        {
            return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(value));
        }

        unsigned char retailCvttss2siLowByte(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return static_cast<unsigned char>(_mm_cvtt_ss2si(_mm_set_ss(value)));
#else
            // Steam sub_445600 finishes with CVTTSS2SI and stores CL.  For
            // unordered or out-of-range input the x86 instruction returns
            // integer-indefinite (0x80000000), whose low byte is zero.
            if (!(value >= -2147483648.0f && value < 2147483648.0f))
                return 0u;
            return static_cast<unsigned char>(static_cast<int>(value));
#endif
        }
    }

    void ANGLE::Read(BaseStream* stream)
    {
        stream->read(&value, 1);
    }

    ANGLE ANGLE::FromXY(int x, int y, int* projectedLength)
    {
        return AngleFromXY(x, y, projectedLength);
    }

    ANGLE AngleFromXY(int x, int y, int* projectedLength)
    {
        std::int32_t vecX = static_cast<std::int32_t>(x);
        std::int32_t vecY = 0;
        std::int32_t angle = 0;

        if (vecX < 0)
        {
            vecY = neg32Wrap(static_cast<std::int32_t>(y));
            vecX = neg32Wrap(vecX);
            angle = 0x180;
        }
        else
        {
            vecY = static_cast<std::int32_t>(y);
            angle = 0x80;
        }

        for (int i = 0; i < 7; ++i)
        {
            const std::int32_t shiftX = sar32(vecX, i);
            const std::int32_t shiftY = sar32(vecY, i);
            if (vecY < 0)
            {
                vecY = add32Wrap(vecY, shiftX);
                vecX = sub32Wrap(vecX, shiftY);
                angle = sub32Wrap(angle,
                    kCordicAngleSteps[i]);
            }
            else
            {
                vecY = sub32Wrap(vecY, shiftX);
                vecX = add32Wrap(vecX, shiftY);
                angle = add32Wrap(angle,
                    kCordicAngleSteps[i]);
            }
        }

        if (projectedLength)
        {
            const std::uint32_t product = static_cast<std::uint32_t>(vecX) * 0x0009B74Eu;
            *projectedLength = sar32(static_cast<std::int32_t>(product), 20);
        }

        return ANGLE(static_cast<unsigned char>(sar32(angle, 1)));
    }

    ANGLE RetailDirectionFromFloatXY(float x, float y) noexcept
    {
        // Steam 1.22 sub_445600.  Keep the original unordered branches:
        // UCOMISS treats NaN as the non-zero path; COMISS/JBE maps NaN x to
        // the 0xC0 zero-y branch; and for y < 0 an unordered x follows the
        // +256 path.
        if (y == 0.0f)
        {
            if (x == 0.0f)
                return ANGLE(0u);
            if (x > 0.0f)
                return ANGLE(0x40u);
            return ANGLE(0xC0u);
        }

        const float ratio = x / y;
        float angle = static_cast<float>(std::atan(static_cast<double>(-ratio)));
        angle = angle * 40.743668f;

        // COMISS 0,y / JBE: y >= 0 OR unordered -> +128.
        if (!(y < 0.0f))
        {
            angle = angle + 128.0f;
        }
        // COMISS x,0 / JNB: only ordered x >= 0 skips +256.
        else if (!(x >= 0.0f))
        {
            angle = angle + 256.0f;
        }

        return ANGLE(retailCvttss2siLowByte(angle));
    }

    int IntegerSquareRoot(int value)
    {
        std::int32_t source = static_cast<std::int32_t>(value);
        std::int32_t result = 0;
        std::int32_t bit = 0x40000000;

        do
        {
            const std::int32_t trial = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(bit) + static_cast<std::uint32_t>(result));
            if (source < trial)
            {
                result = sar32(result, 1);
            }
            else
            {
                source = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(source) - static_cast<std::uint32_t>(trial));
                result = bit | sar32(result, 1);
            }
            bit = sar32(bit, 2);
        }
        while (bit != 0);

        return result;
    }
}
