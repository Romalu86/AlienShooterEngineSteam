#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
    void initializeSmallFftFactors(int n, float* wa, int* ifac)
    {
        static const int factors[4] = {4, 2, 3, 5};
        constexpr float twoPi = 6.28318530717958648f;
        int factor = 0;
        int factorIndex = -1;
        int remaining = n;
        int factorCount = 0;

        for (;;)
        {
            ++factorIndex;
            if (factorIndex < 4)
                factor = factors[factorIndex];
            else
                factor += 2;

            for (;;)
            {
                const int quotient = remaining / factor;
                const int remainder = remaining - factor * quotient;
                if (remainder != 0)
                    break;

                ++factorCount;
                ifac[factorCount + 1] = factor;
                remaining = quotient;
                if (factor == 2 && factorCount != 1)
                {
                    for (int index = 1; index < factorCount; ++index)
                    {
                        const int destination = factorCount - index + 1;
                        ifac[destination + 1] = ifac[destination];
                    }
                    ifac[2] = 2;
                }
                if (quotient == 1)
                    goto factors_complete;
                if (remaining % factor != 0)
                    break;
            }
        }

    factors_complete:
        ifac[0] = n;
        ifac[1] = factorCount;
        const float argumentHigh = twoPi / static_cast<float>(n);
        int trigOffset = 0;
        const int factorCountMinusOne = factorCount - 1;
        int lengthOne = 1;
        if (factorCountMinusOne == 0)
            return;

        for (int factorOrdinal = 0; factorOrdinal < factorCountMinusOne; ++factorOrdinal)
        {
            const int radix = ifac[factorOrdinal + 2];
            int phaseLength = 0;
            const int lengthTwo = lengthOne * radix;
            const int inner = n / lengthTwo;
            for (int radixOrdinal = 0; radixOrdinal < radix - 1; ++radixOrdinal)
            {
                phaseLength += lengthOne;
                int writeIndex = trigOffset;
                const float argumentLow = static_cast<float>(phaseLength) * argumentHigh;
                float ordinal = 0.0f;
                for (int innerIndex = 2; innerIndex < inner; innerIndex += 2)
                {
                    ordinal += 1.0f;
                    const float argument = ordinal * argumentLow;
                    wa[writeIndex++] = std::cos(argument);
                    wa[writeIndex++] = std::sin(argument);
                }
                trigOffset += inner;
            }
            lengthOne = lengthTwo;
        }
    }

    void initializeSmallFftCache(int n, float* cache, int* split)
    {
        if (n != 1)
            initializeSmallFftFactors(n, cache + n, split);
    }

    void initializeSmallFftLookup(unsigned char* owner, int n)
    {
        writeBlockField<int>(owner, 0x00, n);
        auto* cache = static_cast<float*>(std::calloc(static_cast<std::size_t>(3 * n), sizeof(float)));
        auto* split = static_cast<int*>(std::calloc(32u, sizeof(int)));
        writeBlockPointer(owner, 0x04, cache);
        writeBlockPointer(owner, 0x08, split);
        initializeSmallFftCache(n, cache, split);
    }

    void releaseSmallFftLookup(void* record)
    {
        if (!record)
            return;
        auto* owner = static_cast<unsigned char*>(record);
        if (void* cache = readBlockPointer(owner, 0x04))
            std::free(cache);
        if (void* split = readBlockPointer(owner, 0x08))
            std::free(split);
        std::memset(owner, 0, 0x0C);
    }
} } }
