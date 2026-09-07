#pragma once

#include <cstddef>
#include <cstdint>

namespace as1
{
    class BaseStream;

    struct ANGLE
    {
        unsigned char value;

        ANGLE() noexcept {}
        ANGLE(unsigned char v) noexcept : value(v) {}
        ANGLE(const ANGLE& other) noexcept : value(other.value) {}
        ANGLE& operator=(const ANGLE& other) noexcept
        {
            value = other.value;
            return *this;
        }

        int Int() const noexcept { return static_cast<int>(value); }
        void Read(BaseStream* stream);

        static ANGLE FromXY(int x, int y, int* projectedLength = nullptr);
    };

                                                                                               

    ANGLE AngleFromXY(int x, int y, int* projectedLength = nullptr);
    ANGLE RetailDirectionFromFloatXY(float x, float y) noexcept;

    int IntegerSquareRoot(int value);
}
