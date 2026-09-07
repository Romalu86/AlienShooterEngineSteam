#include "vid/vid_hardware_z.h"

#include "core/application.h"
#include "graph.h"
#include "graphics/base_texture.h"
#include "graphics/rect.h"
#include "map.h"
#include "sprite.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>

namespace as1
{
    VID_HARDWARE_Z::VID_HARDWARE_Z(const VID_HARDWARE_Z& other)
        : VID_SOFTWARE(other)
    {

    }

    VID_HARDWARE_Z* VID_HARDWARE_Z::CreateMirror()
    {

        return new (std::nothrow) VID_HARDWARE_Z(*this);
    }

    int composeHardwareZOpaqueSpan(const WORD* sourceZ, const WORD* sourceColor, const WORD* destinationZ, WORD* destinationColor, int count) noexcept
    {

        std::uint32_t loopResult = static_cast<std::uint32_t>(count) - 1u;
        if (static_cast<std::int32_t>(loopResult) < 0)
            return static_cast<std::int32_t>(loopResult);

        std::uint32_t remaining = loopResult + 1u;
        do
        {
            const std::int32_t sourceDepth = static_cast<std::int16_t>(*sourceZ);
            const std::int32_t destinationDepth = static_cast<std::int16_t>(*destinationZ);
            const std::int32_t combined = static_cast<std::int16_t>(static_cast<WORD>(g_packedSoftwareDepth)) + sourceDepth;
            if (combined < destinationDepth)
            {
                *destinationColor = 0u;
            }
            else if (combined > destinationDepth + 127)
            {
                *destinationColor = *sourceColor;
            }
            else
            {
                const std::int32_t delta = combined - destinationDepth;
                const std::uint32_t productBits = static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(*sourceColor);
                const std::int32_t scaled = static_cast<std::int32_t>(productBits) >> 7;
                const WORD alpha = scaled <= 0xFFFF
                    ? static_cast<WORD>(static_cast<std::uint32_t>(scaled) & 0xF000u)
                    : static_cast<WORD>(0xF000u);
                *destinationColor = static_cast<WORD>(alpha + (*sourceColor & 0x0FFFu));
            }
            ++sourceZ;
            ++sourceColor;
            ++destinationZ;
            ++destinationColor;
            --remaining;
            loopResult = remaining;
        } while (remaining != 0u);
        return static_cast<std::int32_t>(loopResult);
    }

    int composeHardwareZAlphaSpan(const WORD* sourceZ, const WORD* sourceColor, const WORD* destinationZ, WORD* destinationColor, int count) noexcept
    {

        std::uint32_t loopResult = static_cast<std::uint32_t>(count) - 1u;
        if (static_cast<std::int32_t>(loopResult) < 0)
            return static_cast<std::int32_t>(loopResult);

        std::uint32_t remaining = loopResult + 1u;
        do
        {
            const std::int32_t sourceDepth = static_cast<std::int16_t>(*sourceZ);
            const std::int32_t destinationDepth = static_cast<std::int16_t>(*destinationZ);
            const std::int32_t combined = static_cast<std::int16_t>(static_cast<WORD>(g_packedSoftwareDepth)) + sourceDepth;
            if (combined < destinationDepth)
                *destinationColor = 0u;
            else if (combined > destinationDepth + 127)
                *destinationColor = static_cast<WORD>(*sourceColor | 0xF000u);
            else
            {
                const std::int32_t alphaSteps = (combined - destinationDepth) / 8;
                *destinationColor = static_cast<WORD>(*sourceColor |
                    static_cast<WORD>(static_cast<std::uint32_t>(alphaSteps) << 12u));
            }
            ++sourceZ;
            ++sourceColor;
            ++destinationZ;
            ++destinationColor;
            --remaining;
            loopResult = remaining;
        } while (remaining != 0u);
        return static_cast<std::int32_t>(loopResult);
    }

    namespace
    {
        DWORD a4r4g4b4ToBgra(WORD value)
        {
            const DWORD a = ((value >> 12u) & 0x0Fu) * 17u;
            const DWORD r = ((value >> 8u) & 0x0Fu) * 17u;
            const DWORD g = ((value >> 4u) & 0x0Fu) * 17u;
            const DWORD b = (value & 0x0Fu) * 17u;
            return (a << 24u) | (r << 16u) | (g << 8u) | b;
        }

        int signedWord(WORD value) noexcept
        {
            return static_cast<int>(static_cast<std::int16_t>(value));
        }

        WORD composeOpaqueHardwareZPixel(WORD sourceZ, WORD sourceColor, WORD destinationZ, WORD baseDepthWord)
        {

            const int destination = signedWord(destinationZ);
            const int z = signedWord(baseDepthWord) + signedWord(sourceZ);
            if (z < destination)
                return 0;
            if (z <= destination + 127)
            {
                const int scaled = (static_cast<int>(sourceColor) * (z - destination)) >> 7;
                const WORD alpha = scaled <= 0xFFFF ? static_cast<WORD>(scaled & 0xF000) : static_cast<WORD>(0xF000);
                return static_cast<WORD>(alpha | (sourceColor & 0x0FFF));
            }
            return sourceColor;
        }

        WORD composeAlphaHardwareZPixel(WORD sourceZ, WORD sourceColor, WORD destinationZ, WORD baseDepthWord)
        {

            const int destination = signedWord(destinationZ);
            const int z = signedWord(baseDepthWord) + signedWord(sourceZ);
            if (z < destination)
                return 0;
            if (z <= destination + 127)
                return static_cast<WORD>(sourceColor | (static_cast<WORD>((z - destination) / 8) << 12u));
            return static_cast<WORD>(sourceColor | 0xF000u);
        }
    }

    void selectHardwareZRenderLayer(VID_HARDWARE_Z* owner) noexcept
    {
        const DWORD property = owner->properties();
        if ((property & 0x40000000u) != 0u)
        {
            owner->setRenderLayer(4);
            return;
        }
        if (owner->spriteTypeId() == 0x40u)
        {
            owner->setRenderLayer(10);
            return;
        }

        const WORD typeFlags = owner->formatFlags();
        if ((typeFlags & (VID_TYPE_ZBUFFER | VID_TYPE_ALPHA)) ==
            (VID_TYPE_ZBUFFER | VID_TYPE_ALPHA))
        {
            owner->setRenderLayer(12);
            return;
        }
        if ((typeFlags & VID_TYPE_ALPHA) != 0u)
        {
            owner->setRenderLayer((property & 0x00010000u) != 0u ? 9 : 12);
            return;
        }
        owner->setRenderLayer(8);
    }

    void VID_HARDWARE_Z::SetLayer()
    {
        selectHardwareZRenderLayer(this);
    }

    void VID_HARDWARE_Z::SetGamma(const Gamma& gamma, unsigned n_gamma)
    {

        VID::SetGamma(gamma, n_gamma);
    }

    void VID_HARDWARE_Z::SetGammaRaw(const GammaRawPair& rawGamma, unsigned n_gamma)
    {

        VID::SetGammaRaw(rawGamma, n_gamma);
    }

    void VID_HARDWARE_Z::Draw(const SPRITE* sprite)
    {

        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        GRAPH* const graph = GRAPH::CurrentGraph();
        const core::ApplicationDrawDispatcherState& appDraw =
            core::GlobalApplicationDrawDispatcherState();

        const int sizeX = signedWord(vidWidth());
        const int sizeY = signedWord(vidHeight());
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();
        const int drawLeft = static_cast<int>(sprite->X() - cameraX - static_cast<float>(sizeX / 2));
        int drawTop = static_cast<int>(sprite->Y() - sprite->Z() - cameraY - static_cast<float>(sizeY / 2));

        const GraphViewportState& viewport = graph->viewportState();
        const int clipLeft = g_softwareClipLeft;
        const int clipRight = g_softwareClipRight;
        const int clipTop = g_softwareClipTop;
        const int clipBottom = g_softwareClipBottom;

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = static_cast<int>(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
        {
            baseDepth += 0x3FFF;
        }
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = static_cast<int>(
                SPRITE::rawDirectionSin(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                moveUpZ() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frameIndex = sprite->currentFrame();
        BYTE* rowStream = frameStorage() + frameOffsets()[frameIndex];
        const int contourCount = signedWord(*reinterpret_cast<const WORD*>(rowStream));
        rowStream += 2 + 6 * contourCount;
        const int frameTop = signedWord(*reinterpret_cast<const WORD*>(rowStream));
        const int frameHeightSpan = signedWord(*reinterpret_cast<const WORD*>(rowStream + 2));
        rowStream += 4;

        int activeTop = drawTop + frameTop;
        int activeBottom = activeTop + frameHeightSpan;
        if (activeTop >= clipBottom || activeBottom < clipTop)
            return;
        if (activeBottom > clipBottom)
            activeBottom = clipBottom;

        if (activeTop < clipTop)
        {
            int rowsToSkip = clipTop - activeTop;
            activeTop = clipTop;
            while (rowsToSkip-- > 0)
            {
                if (*reinterpret_cast<const WORD*>(rowStream) != 0u)
                {
                    do
                    {
                        const unsigned run = rowStream[1];
                        rowStream += 2u + static_cast<std::size_t>(run) * 4u;
                    }
                    while (*reinterpret_cast<const WORD*>(rowStream) != 0u);
                }
                rowStream += 2u;
            }
        }

        const int visibleHeightSpan = activeBottom - activeTop;
        RECTI destinationRect{ drawLeft, activeTop, drawLeft + sizeX, activeBottom };
        RECTI sourceRect{ 0, 0, sizeX, visibleHeightSpan };

        BASE_TEXTURE* const alphaScratch = graph->alphaBuffer();
        const std::uint16_t* const screenDepth = graph->softwareDepthBuffer();

#ifdef _WIN32
        int scratchPitchBytes = 0;
        WORD* scratchRow = alphaScratch->lock16(&scratchPitchBytes, &sourceRect);

        const int scratchPitchWords = scratchPitchBytes / 2;
        const int screenDepthPitch = graph->softwareDepthPitch();
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);
        const WORD typeFlags = formatFlags();
        const bool preserveSourceAlpha =
            (typeFlags & VID_TYPE_ALPHA) != 0u && (typeFlags & VID_TYPE_TEXTURE) != 0u;
        const bool fullyInsideHorizontally =
            drawLeft >= clipLeft && drawLeft + sizeX <= clipRight;

        const std::uint16_t* depthRow = screenDepth +
            static_cast<std::ptrdiff_t>(activeTop) * screenDepthPitch;

        for (int rowIndex = 0; rowIndex <= visibleHeightSpan; ++rowIndex)
        {
            std::fill_n(scratchRow, scratchPitchWords, static_cast<WORD>(0));

            int absoluteX = drawLeft;
            while (*reinterpret_cast<const WORD*>(rowStream) != 0u)
            {
                const int skip = rowStream[0];
                const int run = rowStream[1];
                rowStream += 2u;
                absoluteX += skip;

                const WORD* const sourceZ = reinterpret_cast<const WORD*>(rowStream);
                const WORD* const sourceColor = sourceZ + run;
                const int runEnd = absoluteX + run;

                int firstPixel = absoluteX;
                int lastPixel = runEnd;
                if (!fullyInsideHorizontally)
                {
                    firstPixel = std::max(firstPixel, clipLeft);
                    lastPixel = std::min(lastPixel, clipRight);
                }

                if (lastPixel > firstPixel)
                {
                    const int sourceOffset = firstPixel - absoluteX;
                    const int scratchOffset = firstPixel - drawLeft;
                    const int count = lastPixel - firstPixel;
                    g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord);
                    if (preserveSourceAlpha)
                    {
                        (void)composeHardwareZOpaqueSpan(sourceZ + sourceOffset,
                                         sourceColor + sourceOffset,
                                         depthRow + firstPixel,
                                         scratchRow + scratchOffset,
                                         count);
                    }
                    else
                    {
                        (void)composeHardwareZAlphaSpan(sourceZ + sourceOffset,
                                         sourceColor + sourceOffset,
                                         depthRow + firstPixel,
                                         scratchRow + scratchOffset,
                                         count);
                    }
                }

                rowStream += static_cast<std::size_t>(run) * 4u;
                absoluteX = runEnd;
            }

            rowStream += 2u;
            scratchRow += scratchPitchWords;
            depthRow += screenDepthPitch;
        }

        graph->setAlphaBlendFactors(5u, preserveSourceAlpha ? 6u : 2u);
        alphaScratch->unlock();

        graph->setRenderStateCached(0x17u, 8u);

        GammaRawPair selectedGamma{};
        sprite->buildRetailGammaPair(selectedGamma);
        GammaRawPair drawGamma{};
        drawGamma.setSaturatingAddRetail(gammaRaw, selectedGamma);
        if ((property & P_GAMMA) == 0u)
            drawGamma.setSaturatingAddRetail(drawGamma, *GRAPH::CurrentRawGammaPair());

        const DWORD colors[2] = { drawGamma.first, drawGamma.second };
        alphaScratch->DrawFixedDepthRectangle(destinationRect, sourceRect, colors);
#else

        (void)rowStream;
        (void)visibleHeightSpan;
        (void)destinationRect;
        (void)sourceRect;
        (void)screenDepth;
        return;
#endif
    }

    bool VID_HARDWARE_Z::transparencyCheck() const
    {
        return true;
    }

    bool VID_HARDWARE_Z::isLoaded() const
    {
        return frameStorage() != nullptr && frameOffsets() != nullptr &&
               static_cast<WORD>(totalFrames()) != 0u;
    }
}
