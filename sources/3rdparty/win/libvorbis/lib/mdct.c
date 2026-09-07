#include "3rdparty/win/libvorbis/lib/codec_internal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace as1 { namespace thirdparty { namespace xiph2003
{
    namespace
    {
        constexpr float kPi = 3.1415927f;
        constexpr float kPi1_8 = 0.92387953251128675613f;
        constexpr float kPi2_8 = 0.70710678118654752441f;
        constexpr float kPi3_8 = 0.38268343236508977175f;

        struct MdctLookup
        {
            int n{};
            int log2n{};
            float* trig{};
            int* bitrev{};
            float scale{};
        };

        void mdctInit(MdctLookup& lookup, int n)
        {
            lookup.n = n;
            lookup.log2n = static_cast<int>(std::floor(std::log(static_cast<float>(n)) / std::log(2.0f) + 0.5f));
            lookup.bitrev = static_cast<int*>(std::malloc(sizeof(int) * static_cast<std::size_t>(n / 4)));
            lookup.trig = static_cast<float*>(std::malloc(sizeof(float) * static_cast<std::size_t>(n + n / 4)));
            lookup.scale = 4.0f / static_cast<float>(n);

            const int n2 = n >> 1;
            for (int i = 0; i < n / 4; ++i)
            {
                lookup.trig[i * 2] = std::cos((kPi / static_cast<float>(n)) * static_cast<float>(4 * i));
                lookup.trig[i * 2 + 1] = -std::sin((kPi / static_cast<float>(n)) * static_cast<float>(4 * i));
                lookup.trig[n2 + i * 2] = std::cos((kPi / static_cast<float>(2 * n)) * static_cast<float>(2 * i + 1));
                lookup.trig[n2 + i * 2 + 1] = std::sin((kPi / static_cast<float>(2 * n)) * static_cast<float>(2 * i + 1));
            }
            for (int i = 0; i < n / 8; ++i)
            {
                lookup.trig[n + i * 2] = std::cos((kPi / static_cast<float>(n)) * static_cast<float>(4 * i + 2)) * 0.5f;
                lookup.trig[n + i * 2 + 1] = -std::sin((kPi / static_cast<float>(n)) * static_cast<float>(4 * i + 2)) * 0.5f;
            }

            const int mask = (1 << (lookup.log2n - 1)) - 1;
            const int msb = 1 << (lookup.log2n - 2);
            for (int i = 0; i < n / 8; ++i)
            {
                int acc = 0;
                for (int j = 0; (msb >> j) != 0; ++j)
                    if ((msb >> j) & i)
                        acc |= 1 << j;
                lookup.bitrev[i * 2] = ((~acc) & mask) - 1;
                lookup.bitrev[i * 2 + 1] = acc;
            }
        }

        void mdctClear(MdctLookup& lookup)
        {
            std::free(lookup.trig);
            std::free(lookup.bitrev);
            std::memset(&lookup, 0, sizeof(lookup));
        }

        inline void mdctButterfly8(float* x)
        {
            float r0 = x[6] + x[2];
            float r1 = x[6] - x[2];
            float r2 = x[4] + x[0];
            float r3 = x[4] - x[0];
            x[6] = r0 + r2;
            x[4] = r0 - r2;
            r0 = x[5] - x[1];
            r2 = x[7] - x[3];
            x[0] = r1 + r0;
            x[2] = r1 - r0;
            r0 = x[5] + x[1];
            r1 = x[7] + x[3];
            x[3] = r2 + r3;
            x[1] = r2 - r3;
            x[7] = r1 + r0;
            x[5] = r1 - r0;
        }

        inline void mdctButterfly16(float* x)
        {
            float r0 = x[1] - x[9];
            float r1 = x[0] - x[8];
            x[8] += x[0]; x[9] += x[1];
            x[0] = (r0 + r1) * kPi2_8;
            x[1] = (r0 - r1) * kPi2_8;
            r0 = x[3] - x[11]; r1 = x[10] - x[2];
            x[10] += x[2]; x[11] += x[3]; x[2] = r0; x[3] = r1;
            r0 = x[12] - x[4]; r1 = x[13] - x[5];
            x[12] += x[4]; x[13] += x[5];
            x[4] = (r0 - r1) * kPi2_8; x[5] = (r0 + r1) * kPi2_8;
            r0 = x[14] - x[6]; r1 = x[15] - x[7];
            x[14] += x[6]; x[15] += x[7]; x[6] = r0; x[7] = r1;
            mdctButterfly8(x); mdctButterfly8(x + 8);
        }

        inline void mdctButterfly32(float* x)
        {
            float r0 = x[30] - x[14], r1 = x[31] - x[15];
            x[30] += x[14]; x[31] += x[15]; x[14] = r0; x[15] = r1;
            r0 = x[28] - x[12]; r1 = x[29] - x[13];
            x[28] += x[12]; x[29] += x[13];
            x[12] = r0 * kPi1_8 - r1 * kPi3_8; x[13] = r0 * kPi3_8 + r1 * kPi1_8;
            r0 = x[26] - x[10]; r1 = x[27] - x[11];
            x[26] += x[10]; x[27] += x[11];
            x[10] = (r0 - r1) * kPi2_8; x[11] = (r0 + r1) * kPi2_8;
            r0 = x[24] - x[8]; r1 = x[25] - x[9];
            x[24] += x[8]; x[25] += x[9];
            x[8] = r0 * kPi3_8 - r1 * kPi1_8; x[9] = r1 * kPi3_8 + r0 * kPi1_8;
            r0 = x[22] - x[6]; r1 = x[7] - x[23];
            x[22] += x[6]; x[23] += x[7]; x[6] = r1; x[7] = r0;
            r0 = x[4] - x[20]; r1 = x[5] - x[21];
            x[20] += x[4]; x[21] += x[5];
            x[4] = r1 * kPi1_8 + r0 * kPi3_8; x[5] = r1 * kPi3_8 - r0 * kPi1_8;
            r0 = x[2] - x[18]; r1 = x[3] - x[19];
            x[18] += x[2]; x[19] += x[3];
            x[2] = (r1 + r0) * kPi2_8; x[3] = (r1 - r0) * kPi2_8;
            r0 = x[0] - x[16]; r1 = x[1] - x[17];
            x[16] += x[0]; x[17] += x[1];
            x[0] = r1 * kPi3_8 + r0 * kPi1_8; x[1] = r1 * kPi1_8 - r0 * kPi3_8;
            mdctButterfly16(x); mdctButterfly16(x + 16);
        }

        inline void mdctButterflyFirst(float* T, float* x, int points)
        {
            float* x1 = x + points - 8;
            float* x2 = x + (points >> 1) - 8;
            do
            {
                float r0 = x1[6] - x2[6], r1 = x1[7] - x2[7];
                x1[6] += x2[6]; x1[7] += x2[7];
                x2[6] = r1 * T[1] + r0 * T[0]; x2[7] = r1 * T[0] - r0 * T[1];
                r0 = x1[4] - x2[4]; r1 = x1[5] - x2[5];
                x1[4] += x2[4]; x1[5] += x2[5];
                x2[4] = r1 * T[5] + r0 * T[4]; x2[5] = r1 * T[4] - r0 * T[5];
                r0 = x1[2] - x2[2]; r1 = x1[3] - x2[3];
                x1[2] += x2[2]; x1[3] += x2[3];
                x2[2] = r1 * T[9] + r0 * T[8]; x2[3] = r1 * T[8] - r0 * T[9];
                r0 = x1[0] - x2[0]; r1 = x1[1] - x2[1];
                x1[0] += x2[0]; x1[1] += x2[1];
                x2[0] = r1 * T[13] + r0 * T[12]; x2[1] = r1 * T[12] - r0 * T[13];
                x1 -= 8; x2 -= 8; T += 16;
            } while (x2 >= x);
        }

        inline void mdctButterflyGeneric(float* T, float* x, int points, int trigint)
        {
            float* x1 = x + points - 8;
            float* x2 = x + (points >> 1) - 8;
            do
            {
                float r0 = x1[6] - x2[6], r1 = x1[7] - x2[7];
                x1[6] += x2[6]; x1[7] += x2[7];
                x2[6] = r1*T[1] + r0*T[0]; x2[7] = r1*T[0] - r0*T[1]; T += trigint;
                r0 = x1[4] - x2[4]; r1 = x1[5] - x2[5]; x1[4] += x2[4]; x1[5] += x2[5];
                x2[4] = r1*T[1] + r0*T[0]; x2[5] = r1*T[0] - r0*T[1]; T += trigint;
                r0 = x1[2] - x2[2]; r1 = x1[3] - x2[3]; x1[2] += x2[2]; x1[3] += x2[3];
                x2[2] = r1*T[1] + r0*T[0]; x2[3] = r1*T[0] - r0*T[1]; T += trigint;
                r0 = x1[0] - x2[0]; r1 = x1[1] - x2[1]; x1[0] += x2[0]; x1[1] += x2[1];
                x2[0] = r1*T[1] + r0*T[0]; x2[1] = r1*T[0] - r0*T[1]; T += trigint;
                x1 -= 8; x2 -= 8;
            } while (x2 >= x);
        }

        void mdctButterflies(MdctLookup& init, float* x, int points)
        {
            float* T = init.trig;
            int stages = init.log2n - 5;
            if (--stages > 0)
                mdctButterflyFirst(T, x, points);
            for (int i = 1; --stages > 0; ++i)
                for (int j = 0; j < (1 << i); ++j)
                    mdctButterflyGeneric(T, x + (points >> i) * j, points >> i, 4 << i);
            for (int j = 0; j < points; j += 32)
                mdctButterfly32(x + j);
        }

        void mdctBitreverse(MdctLookup& init, float* x)
        {
            int* bit = init.bitrev;
            float* w0 = x;
            float* w1 = x = w0 + (init.n >> 1);
            float* T = init.trig + init.n;
            do
            {
                float* x0 = x + bit[0];
                float* x1 = x + bit[1];
                float r0 = x0[1] - x1[1];
                float r1 = x0[0] + x1[0];
                float r2 = r1*T[0] + r0*T[1];
                float r3 = r1*T[1] - r0*T[0];
                w1 -= 4;
                r0 = (x0[1] + x1[1]) * 0.5f;
                r1 = (x0[0] - x1[0]) * 0.5f;
                w0[0] = r0 + r2; w1[2] = r0 - r2; w0[1] = r1 + r3; w1[3] = r3 - r1;
                x0 = x + bit[2]; x1 = x + bit[3];
                r0 = x0[1] - x1[1]; r1 = x0[0] + x1[0];
                r2 = r1*T[2] + r0*T[3]; r3 = r1*T[3] - r0*T[2];
                r0 = (x0[1] + x1[1]) * 0.5f; r1 = (x0[0] - x1[0]) * 0.5f;
                w0[2] = r0 + r2; w1[0] = r0 - r2; w0[3] = r1 + r3; w1[1] = r3 - r1;
                T += 4; bit += 4; w0 += 4;
            } while (w0 < w1);
        }

        void mdctBackward(MdctLookup& init, float* in, float* out)
        {
            const int n = init.n;
            const int n2 = n >> 1;
            const int n4 = n >> 2;
            float* iX = in + n2 - 7;
            float* oX = out + n2 + n4;
            float* T = init.trig + n4;
            do
            {
                oX -= 4;
                oX[0] = -iX[2]*T[3] - iX[0]*T[2];
                oX[1] =  iX[0]*T[3] - iX[2]*T[2];
                oX[2] = -iX[6]*T[1] - iX[4]*T[0];
                oX[3] =  iX[4]*T[1] - iX[6]*T[0];
                iX -= 8; T += 4;
            } while (iX >= in);

            iX = in + n2 - 8; oX = out + n2 + n4; T = init.trig + n4;
            do
            {
                T -= 4;
                oX[0] = iX[4]*T[3] + iX[6]*T[2];
                oX[1] = iX[4]*T[2] - iX[6]*T[3];
                oX[2] = iX[0]*T[1] + iX[2]*T[0];
                oX[3] = iX[0]*T[0] - iX[2]*T[1];
                iX -= 8; oX += 4;
            } while (iX >= in);

            mdctButterflies(init, out + n2, n2);
            mdctBitreverse(init, out);

            float* oX1 = out + n2 + n4;
            float* oX2 = oX1;
            iX = out; T = init.trig + n2;
            do
            {
                oX1 -= 4;
                oX1[3] = iX[0]*T[1] - iX[1]*T[0]; oX2[0] = -(iX[0]*T[0] + iX[1]*T[1]);
                oX1[2] = iX[2]*T[3] - iX[3]*T[2]; oX2[1] = -(iX[2]*T[2] + iX[3]*T[3]);
                oX1[1] = iX[4]*T[5] - iX[5]*T[4]; oX2[2] = -(iX[4]*T[4] + iX[5]*T[5]);
                oX1[0] = iX[6]*T[7] - iX[7]*T[6]; oX2[3] = -(iX[6]*T[6] + iX[7]*T[7]);
                oX2 += 4; iX += 8; T += 8;
            } while (iX < oX1);

            iX = out + n2 + n4; oX1 = out + n4; oX2 = oX1;
            do
            {
                oX1 -= 4; iX -= 4;
                oX2[0] = -(oX1[3] = iX[3]); oX2[1] = -(oX1[2] = iX[2]);
                oX2[2] = -(oX1[1] = iX[1]); oX2[3] = -(oX1[0] = iX[0]);
                oX2 += 4;
            } while (oX2 < iX);
            iX = out + n2 + n4; oX1 = out + n2 + n4; oX2 = out + n2;
            do
            {
                oX1 -= 4;
                oX1[0] = iX[3]; oX1[1] = iX[2]; oX1[2] = iX[1]; oX1[3] = iX[0];
                iX += 4;
            } while (oX1 > oX2);
        }

        void applyRetailWindow(float* pcm, float* const windows[2], const int blocksizes[2], int lW, int W, int nW)
        {
            const int leftWindow = W ? lW : 0;
            const int n = blocksizes[W];
            const int rightWindow = W ? nW : 0;
            const int leftN = blocksizes[leftWindow];
            const int rightN = blocksizes[rightWindow];
            const int leftBegin = n / 4 - leftN / 4;
            const int leftEnd = leftBegin + leftN / 2;
            const int rightBegin = n / 2 + n / 4 - rightN / 4;
            const int rightEnd = rightBegin + rightN / 2;

            if (leftBegin > 0)
                std::memset(pcm, 0, sizeof(float) * static_cast<std::size_t>(leftBegin));
            for (int i = leftBegin, j = 0; i < leftEnd; ++i, ++j)
                pcm[i] *= windows[leftWindow][j];
            for (int i = rightBegin, j = rightN / 2 - 1; i < rightEnd; ++i, --j)
                pcm[i] *= windows[rightWindow][j];
            if (rightEnd < n)
                std::memset(pcm + rightEnd, 0, sizeof(float) * static_cast<std::size_t>(n - rightEnd));
        }
    }

    int imdctLog2FloorFromSize(int value)
    {
        int result = 0;
        if (value > 1)
        {
            unsigned int work = static_cast<unsigned int>(value - 1);
            do { ++result; work >>= 1; } while (work);
        }
        return result;
    }

    void releaseSmallWindowRecord(void* record)
    {
        if (record) { std::memset(record, 0, 0x24); std::free(record); }
    }

    void releaseLargeTransformRecord(void* record)
    {
        if (record) { std::memset(record, 0, 0x210); std::free(record); }
    }

    void* buildVorbisWindow(int type, int sampleCount)
    {
        auto* result = static_cast<float*>(std::calloc(static_cast<std::size_t>(sampleCount), 4));
        if (type)
        {
            std::free(result);
            return nullptr;
        }
#if defined(_MSC_VER) && defined(_M_IX86)
#pragma loop(no_vector)
#endif
        for (int index = 0; index < sampleCount; ++index)
        {
            const double sample = static_cast<double>(index);
            const double inner = std::sin((sample + 0.5) / static_cast<double>(sampleCount) * 3.1415927 * 0.5);
            result[index] = static_cast<float>(std::sin(inner * inner * 1.5707964));
        }
        return result;
    }

    int initializeMdctLookup(void* owner, int sampleCount)
    {
        if (!owner || sampleCount <= 0)
            return 0;
        mdctInit(*static_cast<MdctLookup*>(owner), sampleCount);
        return 0;
    }

    int clearMdctLookup(void* owner)
    {
        if (owner)
            mdctClear(*static_cast<MdctLookup*>(owner));
        return 0;
    }

    int runMdctBackward(void* owner, float* input, float* output)
    {
        if (!owner || !input || !output)
            return 0;
        mdctBackward(*static_cast<MdctLookup*>(owner), input, output);
        return 0;
    }

    int applyVorbisWindow(float* pcm, const unsigned char* privateState, const unsigned char* setup, int previousW, int W, int nextW)
    {
        if (!pcm || !privateState || !setup)
            return 0;
        float* windows[2] = {
            static_cast<float*>(readBlockPointer(privateState, kPrivateWindow0Offset)),
            static_cast<float*>(readBlockPointer(privateState, kPrivateWindow1Offset))
        };
        const int blocksizes[2] = {
            readBlockField<int>(setup, kSetupBlock0Offset),
            readBlockField<int>(setup, kSetupBlock1Offset)
        };
        applyRetailWindow(pcm, windows, blocksizes, previousW, W, nextW);
        return 0;
    }

} } }
