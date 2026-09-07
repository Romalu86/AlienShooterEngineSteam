#pragma once
#include <cstdint>

namespace as1
{
    class BaseStream;

    struct VECTOR2
    {
        float x = 0.0f;
        float y = 0.0f;

        VECTOR2() = default;
        VECTOR2(float ax, float ay) : x(ax), y(ay) {}
    };

    struct VECTOR
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        VECTOR() = default;
        VECTOR(float ax, float ay, float az) : x(ax), y(ay), z(az) {}
        VECTOR2 XY() const { return VECTOR2{x, y}; }
        void read(BaseStream* stream);
    };
}
