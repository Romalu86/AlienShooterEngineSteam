#include "3rdparty/win/libvorbis/lib/codec_internal.h"
#include "3rdparty/win/libvorbis/lib/psy_retail_tables.h"

#if defined(_MSC_VER)
#include <malloc.h>
#define AS1_XIPH_ALLOCA _alloca
#else
#include <alloca.h>
#define AS1_XIPH_ALLOCA alloca
#endif

namespace as1 { namespace thirdparty { namespace xiph2003
{
    namespace
    {
        constexpr int kPsyBands = 17;
        constexpr int kPsyLevels = 8;
        constexpr int kPsyNoiseCurves = 3;
        constexpr int kEhmerOffset = 16;
        constexpr int kEhmerMax = 56;
        constexpr int kMaxAth = 88;
        constexpr float kPsyLevel0 = 30.0f;

        float toOctave(float value)
        {
            return std::log(value) * 1.442695f - 5.965784f;
        }

        float fromOctave(float value)
        {
            return std::exp((value + 5.965784f) * 0.693147f);
        }

        float toBark(float value)
        {
            return 13.1f * std::atan(0.00074f * value)
                + 2.24f * std::atan(value * value * 1.85e-8f)
                + 1.0e-4f * value;
        }

        void minCurve(float* curve, const float* other)
        {
            for (int index = 0; index < kEhmerMax; ++index)
            {
                if (other[index] < curve[index])
                    curve[index] = other[index];
            }
        }

        void maxCurve(float* curve, const float* other)
        {
            for (int index = 0; index < kEhmerMax; ++index)
            {
                if (other[index] > curve[index])
                    curve[index] = other[index];
            }
        }

        void attenuateCurve(float* curve, float attenuation)
        {
            for (int index = 0; index < kEhmerMax; ++index)
                curve[index] += attenuation;
        }

        float*** setupToneCurves(const float* curveAttenuationDb, float binHz, int n, float centerBoost, float centerDecayRate)
        {
            float ath[kEhmerMax];
            float work[kPsyBands][kPsyLevels][kEhmerMax];
            float athCurves[kPsyLevels][kEhmerMax];
            auto* bruteBuffer = static_cast<float*>(AS1_XIPH_ALLOCA(static_cast<std::size_t>(n) * sizeof(float)));
            auto*** result = static_cast<float***>(std::malloc(static_cast<std::size_t>(kPsyBands) * sizeof(float**)));

            std::memset(work, 0, sizeof(work));
            for (int band = 0; band < kPsyBands; ++band)
            {
                const int athOffset = band * 4;
                for (int sample = 0; sample < kEhmerMax; ++sample)
                {
                    float minimum = 999.0f;
                    for (int spread = 0; spread < 4; ++spread)
                    {
                        const int source = sample + spread + athOffset;
                        const float candidate = retailPsyFloat(kRetailPsyAthBits[source < kMaxAth ? source : kMaxAth - 1]);
                        if (minimum > candidate)
                            minimum = candidate;
                    }
                    ath[sample] = minimum;
                }

                for (int level = 0; level < 6; ++level)
                {
                    for (int sample = 0; sample < kEhmerMax; ++sample)
                        work[band][level + 2][sample] = retailPsyFloat(kRetailPsyToneMaskBits[band][level][sample]);
                }
                std::memcpy(work[band][0], work[band][2], sizeof(work[band][0]));
                std::memcpy(work[band][1], work[band][2], sizeof(work[band][1]));

                for (int level = 0; level < kPsyLevels; ++level)
                {
                    for (int sample = 0; sample < kEhmerMax; ++sample)
                    {
                        float adjustment = centerBoost + static_cast<float>(std::abs(kEhmerOffset - sample)) * centerDecayRate;
                        if (adjustment < 0.0f && centerBoost > 0.0f)
                            adjustment = 0.0f;
                        if (adjustment > 0.0f && centerBoost < 0.0f)
                            adjustment = 0.0f;
                        work[band][level][sample] += adjustment;
                    }
                }

                for (int level = 0; level < kPsyLevels; ++level)
                {
                    attenuateCurve(
                        work[band][level],
                        curveAttenuationDb[band] + 100.0f - static_cast<float>(level < 2 ? 2 : level) * 10.0f - kPsyLevel0);
                    std::memcpy(athCurves[level], ath, sizeof(ath));
                    attenuateCurve(athCurves[level], 100.0f - static_cast<float>(level) * 10.0f - kPsyLevel0);
                    maxCurve(athCurves[level], work[band][level]);
                }

                for (int level = 1; level < kPsyLevels; ++level)
                {
                    minCurve(athCurves[level], athCurves[level - 1]);
                    minCurve(work[band][level], athCurves[level]);
                }
            }

            for (int band = 0; band < kPsyBands; ++band)
            {
                const int bin = static_cast<int>(std::floor(fromOctave(static_cast<float>(band) * 0.5f) / binHz));
                int lowCurve = static_cast<int>(std::ceil(toOctave(static_cast<float>(bin) * binHz + 1.0f) * 2.0f));
                int highCurve = static_cast<int>(std::floor(toOctave(static_cast<float>(bin + 1) * binHz) * 2.0f));
                if (lowCurve > band)
                    lowCurve = band;
                if (lowCurve < 0)
                    lowCurve = 0;
                if (highCurve >= kPsyBands)
                    highCurve = kPsyBands - 1;

                result[band] = static_cast<float**>(std::malloc(static_cast<std::size_t>(kPsyLevels) * sizeof(float*)));
                for (int level = 0; level < kPsyLevels; ++level)
                {
                    result[band][level] = static_cast<float*>(std::malloc(static_cast<std::size_t>(kEhmerMax + 2) * sizeof(float)));
                    for (int sample = 0; sample < n; ++sample)
                        bruteBuffer[sample] = 999.0f;

                    for (int curve = lowCurve; curve <= highCurve; ++curve)
                    {
                        int cursor = 0;
                        for (int sample = 0; sample < kEhmerMax; ++sample)
                        {
                            int lowBin = static_cast<int>(fromOctave(static_cast<float>(sample) * 0.125f + static_cast<float>(curve) * 0.5f - 2.0625f) / binHz);
                            int highBin = static_cast<int>(fromOctave(static_cast<float>(sample) * 0.125f + static_cast<float>(curve) * 0.5f - 1.9375f) / binHz) + 1;
                            if (lowBin < 0)
                                lowBin = 0;
                            if (lowBin > n)
                                lowBin = n;
                            if (lowBin < cursor)
                                cursor = lowBin;
                            if (highBin < 0)
                                highBin = 0;
                            if (highBin > n)
                                highBin = n;
                            for (; cursor < highBin && cursor < n; ++cursor)
                            {
                                if (bruteBuffer[cursor] > work[curve][level][sample])
                                    bruteBuffer[cursor] = work[curve][level][sample];
                            }
                        }
                        for (; cursor < n; ++cursor)
                        {
                            if (bruteBuffer[cursor] > work[curve][level][kEhmerMax - 1])
                                bruteBuffer[cursor] = work[curve][level][kEhmerMax - 1];
                        }
                    }

                    if (band + 1 < kPsyBands)
                    {
                        int cursor = 0;
                        const int curve = band + 1;
                        for (int sample = 0; sample < kEhmerMax; ++sample)
                        {
                            int lowBin = static_cast<int>(fromOctave(static_cast<float>(sample) * 0.125f + static_cast<float>(band) * 0.5f - 2.0625f) / binHz);
                            int highBin = static_cast<int>(fromOctave(static_cast<float>(sample) * 0.125f + static_cast<float>(band) * 0.5f - 1.9375f) / binHz) + 1;
                            if (lowBin < 0)
                                lowBin = 0;
                            if (lowBin > n)
                                lowBin = n;
                            if (lowBin < cursor)
                                cursor = lowBin;
                            if (highBin < 0)
                                highBin = 0;
                            if (highBin > n)
                                highBin = n;
                            for (; cursor < highBin && cursor < n; ++cursor)
                            {
                                if (bruteBuffer[cursor] > work[curve][level][sample])
                                    bruteBuffer[cursor] = work[curve][level][sample];
                            }
                        }
                        for (; cursor < n; ++cursor)
                        {
                            if (bruteBuffer[cursor] > work[curve][level][kEhmerMax - 1])
                                bruteBuffer[cursor] = work[curve][level][kEhmerMax - 1];
                        }
                    }

                    for (int sample = 0; sample < kEhmerMax; ++sample)
                    {
                        const int sourceBin = static_cast<int>(fromOctave(static_cast<float>(sample) * 0.125f + static_cast<float>(band) * 0.5f - 2.0f) / binHz);
                        result[band][level][sample + 2] = sourceBin < 0 || sourceBin >= n ? -999.0f : bruteBuffer[sourceBin];
                    }

                    int lowerFence = 0;
                    for (; lowerFence < kEhmerOffset; ++lowerFence)
                    {
                        if (result[band][level][lowerFence + 2] > -200.0f)
                            break;
                    }
                    result[band][level][0] = static_cast<float>(lowerFence);

                    int upperFence = kEhmerMax - 1;
                    for (; upperFence > kEhmerOffset + 1; --upperFence)
                    {
                        if (result[band][level][upperFence + 2] > -200.0f)
                            break;
                    }
                    result[band][level][1] = static_cast<float>(upperFence);
                }
            }

            return result;
        }
    }

    void* initializePsyLookRecord(unsigned char* look, const unsigned char* infoPsy, const int* globalPsy, int n, int rate)
    {
        std::memset(look, 0, 0x30);
        int low = -99;
        int high = 1;
        const int eighthOctaveLines = globalPsy[0];
        writeBlockField<int>(look, 0x24, eighthOctaveLines);
        const int shiftOctave = static_cast<int>(std::floor(std::log(static_cast<double>(eighthOctaveLines) * 8.0) / std::log(2.0) + 0.5)) - 1;
        writeBlockField<int>(look, 0x20, shiftOctave);

        const int octaveScale = 1 << (shiftOctave + 1);
        const int firstOctave = static_cast<int>(toOctave(0.25f * static_cast<float>(rate) * 0.5f / static_cast<float>(n)) * static_cast<float>(octaveScale)) - eighthOctaveLines;
        writeBlockField<int>(look, 0x1C, firstOctave);
        const int maxOctave = static_cast<int>(toOctave((static_cast<float>(n) + 0.25f) * static_cast<float>(rate) * 0.5f / static_cast<float>(n)) * static_cast<float>(octaveScale) + 0.5f);
        writeBlockField<int>(look, 0x28, maxOctave - firstOctave + 1);

        auto* ath = static_cast<float*>(std::malloc(static_cast<std::size_t>(n) * sizeof(float)));
        auto* octave = static_cast<int*>(std::malloc(static_cast<std::size_t>(n) * sizeof(int)));
        auto* bark = static_cast<int*>(std::malloc(static_cast<std::size_t>(n) * sizeof(int)));
        writeBlockPointer(look, 0x10, ath);
        writeBlockPointer(look, 0x14, octave);
        writeBlockPointer(look, 0x18, bark);
        writeBlockPointer(look, 0x04, const_cast<unsigned char*>(infoPsy));
        writeBlockField<int>(look, 0x00, n);
        writeBlockField<int>(look, 0x2C, rate);

        int position = 0;
        for (int athIndex = 0; athIndex < kMaxAth - 1; ++athIndex)
        {
            const int endPosition = static_cast<int>(std::floor(fromOctave((static_cast<float>(athIndex) + 1.0f) * 0.125f - 2.0f) * 2.0f * static_cast<float>(n) / static_cast<float>(rate) + 0.5f));
            float base = retailPsyFloat(kRetailPsyAthBits[athIndex]);
            if (position < endPosition)
            {
                const float delta = (retailPsyFloat(kRetailPsyAthBits[athIndex + 1]) - base) / static_cast<float>(endPosition - position);
                for (; position < endPosition && position < n; ++position)
                {
                    ath[position] = base + 100.0f;
                    base += delta;
                }
            }
        }

        const float noiseWindowLow = readBlockField<float>(infoPsy, 0x70);
        const float noiseWindowHigh = readBlockField<float>(infoPsy, 0x74);
        const int noiseWindowLowMinimum = readBlockField<int>(infoPsy, 0x78);
        const int noiseWindowHighMinimum = readBlockField<int>(infoPsy, 0x7C);
        for (int index = 0; index < n; ++index)
        {
            const float currentBark = toBark(static_cast<float>(rate / (2 * n) * index));
            for (; low + noiseWindowLowMinimum < index
                && toBark(static_cast<float>(rate / (2 * n) * low)) < currentBark - noiseWindowLow; ++low)
            {
            }
            for (; high <= n
                && (high < index + noiseWindowHighMinimum
                    || toBark(static_cast<float>(rate / (2 * n) * high)) < currentBark + noiseWindowHigh); ++high)
            {
            }
            bark[index] = ((low - 1) << 16) + (high - 1);
        }

#if defined(_MSC_VER) && defined(_M_IX86)
#pragma loop(no_vector)
#endif
        for (int index = 0; index < n; ++index)
        {
            octave[index] = static_cast<int>(toOctave((static_cast<float>(index) + 0.25f) * 0.5f * static_cast<float>(rate) / static_cast<float>(n)) * static_cast<float>(octaveScale) + 0.5f);
        }

        const auto* toneAttenuation = reinterpret_cast<const float*>(infoPsy + 0x24);
        const float centerBoost = readBlockField<float>(infoPsy, 0x18);
        const float centerDecay = readBlockField<float>(infoPsy, 0x1C);
        writeBlockPointer(look, 0x08, setupToneCurves(toneAttenuation, static_cast<float>(rate) * 0.5f / static_cast<float>(n), n, centerBoost, centerDecay));

        auto** noiseOffset = static_cast<float**>(std::malloc(kPsyNoiseCurves * sizeof(float*)));
        writeBlockPointer(look, 0x0C, noiseOffset);
        for (int curve = 0; curve < kPsyNoiseCurves; ++curve)
            noiseOffset[curve] = static_cast<float*>(std::malloc(static_cast<std::size_t>(n) * sizeof(float)));

        for (int index = 0; index < n; ++index)
        {
            float halfOctave = toOctave((static_cast<float>(index) + 0.5f) * static_cast<float>(rate) / (2.0f * static_cast<float>(n))) * 2.0f;
            if (halfOctave < 0.0f)
                halfOctave = 0.0f;
            if (halfOctave >= static_cast<float>(kPsyBands - 1))
                halfOctave = static_cast<float>(kPsyBands - 1);
            const int integerHalfOctave = static_cast<int>(halfOctave);
            const float fraction = halfOctave - static_cast<float>(integerHalfOctave);
            for (int curve = 0; curve < kPsyNoiseCurves; ++curve)
            {
                // Retail initializePsyLookRecord uses the 17-float noiseoff tables starting at
                // vorbis_info_psy +0x84.  A legacy offset variant uses +0x88 and
                // reads [ptr-4]/[ptr], which is exactly noiseoff[k][band]/[band+1].
                const auto* source = reinterpret_cast<const float*>(infoPsy + 0x84 + static_cast<std::size_t>(curve) * 17u * sizeof(float));
                noiseOffset[curve][index] = source[integerHalfOctave] * (1.0f - fraction) + source[integerHalfOctave + 1] * fraction;
            }
        }
        return look;
    }

    void releasePsyLookRecord(void* record)
    {
        if (!record)
            return;
        auto* look = static_cast<unsigned char*>(record);
        if (void* ath = readBlockPointer(look, 0x10))
            std::free(ath);
        if (void* octave = readBlockPointer(look, 0x14))
            std::free(octave);
        if (void* bark = readBlockPointer(look, 0x18))
            std::free(bark);
        if (auto*** toneCurves = static_cast<float***>(readBlockPointer(look, 0x08)))
        {
            for (int band = 0; band < kPsyBands; ++band)
            {
                for (int level = 0; level < kPsyLevels; ++level)
                    std::free(toneCurves[band][level]);
                std::free(toneCurves[band]);
            }
            std::free(toneCurves);
        }
        if (auto** noiseOffset = static_cast<float**>(readBlockPointer(look, 0x0C)))
        {
            for (int curve = 0; curve < kPsyNoiseCurves; ++curve)
                std::free(noiseOffset[curve]);
            std::free(noiseOffset);
        }
        std::memset(look, 0, 0x30);
    }
} } }
