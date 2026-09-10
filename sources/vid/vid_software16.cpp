#include "vid/vid_software16.h"
#include "sprite.h"
#include "graph.h"
#include "map.h"
#include "graphics/base_texture.h"
#include "core/application.h"
#include "core/log.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>
#include <array>
#include <cstring>
#include <new>

namespace as1
{
    namespace
    {
        int truncateFloatToInt32Software16(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return _mm_cvtt_ss2si(_mm_set_ss(value));
#else
            if (std::isnan(value) || value >= 2147483648.0f || value < -2147483648.0f)
                return std::numeric_limits<int>::min();
            return static_cast<int>(value);
#endif
        }

        int retailWrapSubSoftware16(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int softwareDrawXToInt3216(float x, float cameraX, int halfWidth) noexcept
        {
            int value = retailWrapSubSoftware16(truncateFloatToInt32Software16(x), truncateFloatToInt32Software16(cameraX));
            return retailWrapSubSoftware16(value, halfWidth);
        }

        int softwareDrawYToInt3216(float y, float z, float cameraY, int halfHeight) noexcept
        {
            float projectedY = y - z;
            int value = retailWrapSubSoftware16(truncateFloatToInt32Software16(projectedY), truncateFloatToInt32Software16(cameraY));
            return retailWrapSubSoftware16(value, halfHeight);
        }
    }
    DWORD* expandSoftware16ColorToBgra(DWORD* destination, const WORD* source) noexcept
    {

        const DWORD raw = static_cast<DWORD>(*source);
        const DWORD red = (raw << ((16u - g_color16RedShift) & 31u)) & 0x00FF0000u;
        const DWORD green = (raw << ((8u - g_color16GreenShift) & 31u)) & 0x0000FF00u;
        const DWORD blue = (raw & 0x001Fu) << 3u;
        *destination = red | green | blue;
        return destination;
    }

    int drawSoftware16AlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* destinationDepth, WORD* destinationColor, int count) noexcept
    {

        if (count <= 0)
            return count;

        const std::uintptr_t depthAddress = reinterpret_cast<std::uintptr_t>(destinationDepth);
        const std::uintptr_t colorAddress = reinterpret_cast<std::uintptr_t>(destinationColor);
        const std::int32_t result = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(depthAddress - colorAddress));

        for (int i = 0; i < count; ++i)
        {
            if (static_cast<WORD>(g_packedSoftwareDepth) >= destinationDepth[i])
            {
                const DWORD source = g_softwarePaletteLookup[paletteIndexes[i]];
                const int alpha = static_cast<int>(source >> 24u);
                DWORD unpacked = 0u;
                (void)expandSoftware16ColorToBgra(&unpacked, &destinationColor[i]);
                DWORD resultOwner = unpacked;
                (void)blendBgraPixelByAlpha(&unpacked, &resultOwner, static_cast<int>(source), alpha);
                destinationColor[i] = static_cast<WORD>(
                    ((unpacked >> 3u) & 0x001Fu) |
                    (g_color16RedMask & (unpacked >> (16u - g_color16RedShift))) |
                    (g_color16GreenMask & (unpacked >> (8u - g_color16GreenShift))));
            }
        }
        return result;
    }

    VID_SOFTWARE16::VID_SOFTWARE16(const VID_SOFTWARE16& other)
        : VID_SOFTWARE(other)
    {

    }

    VID_SOFTWARE16* VID_SOFTWARE16::CreateMirror()
    {

        return new (std::nothrow) VID_SOFTWARE16(*this);
    }

    int VID_SOFTWARE16::SoftwareGammaPaletteBlockBytes() const
    {
        return ((formatFlags() & VID_TYPE_ALPHA) != 0u ? 4 : 2) << 8;
    }

    DWORD VID_SOFTWARE16::UnpackRgb565ToBgra(WORD value) const
    {

        DWORD out = 0;
        (void)expandSoftware16ColorToBgra(&out, &value);
        return out;
    }

    WORD VID_SOFTWARE16::PackRgb565FromBgra(DWORD value) const
    {

        return static_cast<WORD>(
            ((value >> 3u) & 0x001Fu) |
            (g_color16RedMask & (value >> (16u - g_color16RedShift))) |
            (g_color16GreenMask & (value >> (8u - g_color16GreenShift))));
    }

    void VID_SOFTWARE16::EnsureSoftwareGammaBaseBuffer()
    {
    }

    void VID_SOFTWARE16::CopySoftwareGammaBlock(size_t dstBlock, size_t srcBlock)
    {
        const size_t blockBytes = static_cast<size_t>(SoftwareGammaPaletteBlockBytes());
        BYTE* const backing = frameStorage();
        const size_t bytes = frameStorageBytes();
        if (!backing || blockBytes == 0u)
            return;
        const size_t dst = dstBlock * blockBytes;
        const size_t src = srcBlock * blockBytes;
        if (dst + blockBytes > bytes || src + blockBytes > bytes)
            return;
        std::memmove(backing + dst, backing + src, blockBytes);
    }

    void VID_SOFTWARE16::ExpandSoftwareGammaBufferIfNeeded()
    {
        if (!frameStorage() || (formatFlags() & VID_TYPE_3D) != 0u)
        {
            EnsureSoftwareGammaBaseBuffer();
            return;
        }
        const size_t blockBytes = static_cast<size_t>(SoftwareGammaPaletteBlockBytes());
        const size_t oldSize = static_cast<size_t>(frameStorageBytes());
        if (blockBytes == 0u || oldSize < blockBytes)
            return;

        BYTE* const oldBacking = frameStorage();
        const size_t newSize = oldSize + 3u * blockBytes;
        BYTE* const newBacking = static_cast<BYTE*>(::operator new(newSize, std::nothrow));
        if (!newBacking)
        {
            ReportResourceError(2, "SetGamma", static_cast<int>(newSize));
            return;
        }
        std::memcpy(newBacking + 4u * blockBytes, oldBacking + blockBytes, oldSize - blockBytes);
        std::memcpy(newBacking, newBacking + 4u * blockBytes, blockBytes);
        std::memcpy(newBacking + blockBytes, newBacking + 4u * blockBytes, blockBytes);
        std::memcpy(newBacking + 2u * blockBytes, newBacking, 2u * blockBytes);

        setFrameStorage(newBacking);
        setFrameStorageBytes(static_cast<DWORD>(newSize));
        g_vidAllocatedBytes += static_cast<int>(3u * blockBytes);
        ::operator delete(oldBacking);
        if (DWORD* offsets = frameOffsets())
        {
            const unsigned frames = static_cast<unsigned>(totalFrames());
            for (unsigned i = 0; i < frames; ++i)
                offsets[i] += static_cast<DWORD>(3u * blockBytes);
        }
        type = static_cast<WORD>(type | 0x0400u);
        for (VID* mirror = nextMirrorVid(); mirror != this; mirror = mirror->nextMirrorVid())
        {
            auto* const softwareMirror = static_cast<VID_SOFTWARE*>(mirror);
            softwareMirror->type = static_cast<WORD>(softwareMirror->type | VID_TYPE_3D);
            softwareMirror->setFrameStorage(newBacking);
        }
        EnsureSoftwareGammaBaseBuffer();
    }

    bool VID_SOFTWARE16::SoftwareGammaComposesGraphRaw() const
    {
        return spriteClass != 8u && (property & P_GAMMA) == 0u;
    }

    GammaRawPair VID_SOFTWARE16::ComposeSoftwareGammaApplyRaw(const GammaRawPair& rawGamma) const
    {
        if (!SoftwareGammaComposesGraphRaw())
            return rawGamma;
        GammaRawPair out{};
        out.setSaturatingAddRetail(*GRAPH::CurrentRawGammaPair(), rawGamma);
        return out;
    }

    void VID_SOFTWARE16::ApplyGammaToSoftwareGammaBlock(size_t blockIndex, const GammaRawPair& rawGamma)
    {
        const size_t blockBytes = static_cast<size_t>(SoftwareGammaPaletteBlockBytes());
        BYTE* const backing = frameStorage();
        const size_t offset = blockIndex * blockBytes;
        if (!backing || blockBytes == 0u || offset + blockBytes > frameStorageBytes() ||
            (rawGamma.first == 0u && rawGamma.second == 0u))
            return;
        ApplyGammaToPaletteRaw(backing + offset, rawGamma);
    }

    void VID_SOFTWARE16::ApplyGammaToPaletteRaw(void* palette, const GammaRawPair& rawGamma)
    {

        if (!palette || (rawGamma.first == 0u && rawGamma.second == 0u))
            return;
        if ((formatFlags() & VID_TYPE_ALPHA) != 0u)
        {
            DWORD* entries = static_cast<DWORD*>(palette);
            for (size_t i = 0; i < 256u; ++i)
                entries[i] = GammaRawBlend(rawGamma, entries[i]);
            return;
        }
        WORD* entries = static_cast<WORD*>(palette);
        for (size_t i = 0; i < 256u; ++i)
            entries[i] = PackRgb565FromBgra(GammaRawBlend(rawGamma, UnpackRgb565ToBgra(entries[i])));
    }

    void VID_SOFTWARE16::SetGammaRawToSoftwareBuffer(const GammaRawPair& rawGamma, unsigned n_gamma, bool storeSlot)
    {
        if (!frameStorage() || !hasPalette())
            return;
        if (n_gamma == 4u)
        {
            if ((formatFlags() & VID_TYPE_3D) != 0u)
            {
                for (unsigned i = 0; i < 4u; ++i)
                    SetGammaRawToSoftwareBuffer(altGammaRaw[i], i, false);
                EnsureSoftwareGammaBaseBuffer();
                return;
            }
            CopySoftwareGammaBlock(0u, 1u);
            if (SoftwareGammaComposesGraphRaw())
                ApplyGammaToSoftwareGammaBlock(0u, rawGamma);
            EnsureSoftwareGammaBaseBuffer();
            return;
        }
        if (n_gamma >= 4u)
        {
            ReportResourceError(4, "n_gamma in VID_SOFTWARE::SetGamma", static_cast<int>(n_gamma));
            return;
        }
        if (storeSlot)
        {
            altGammaRaw[n_gamma] = rawGamma;
        }
        ExpandSoftwareGammaBufferIfNeeded();
        if ((formatFlags() & VID_TYPE_3D) == 0u || !frameStorage())
            return;
        CopySoftwareGammaBlock(n_gamma, 4u);
        ApplyGammaToSoftwareGammaBlock(n_gamma, ComposeSoftwareGammaApplyRaw(rawGamma));
        EnsureSoftwareGammaBaseBuffer();
    }

    void VID_SOFTWARE16::SetGamma(const Gamma& value, unsigned n_gamma)
    {
        VID::SetGamma(value, n_gamma);
    }

    void VID_SOFTWARE16::Draw(const SPRITE* sprite)
    {

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        GRAPH* const graph = GRAPH::CurrentGraph();
        const GraphViewportState& viewport = graph->viewportState();
        const int clipLeft = g_softwareClipLeft;
        const int clipTop = g_softwareClipTop;
        const int clipRight = g_softwareClipRight;
        const int clipBottom = g_softwareClipBottom;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());
        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();
        const int drawLeft = softwareDrawXToInt3216(sprite->X(), cameraX, sizeX / 2);
        int drawTop = softwareDrawYToInt3216(sprite->Y(), sprite->Z(), cameraY, sizeY / 2);

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = static_cast<int>(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
            baseDepth += 0x3FFF;
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = static_cast<int>(
                SPRITE::rawDirectionSin(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                moveUpZ() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frame = sprite->currentFrame();
        BYTE* const frameBase = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(frameBase));
        BYTE* row = frameBase + 2 + 6 * contourCount;
        const int frameTop = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row));
        const int rowCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row + 2));
        row += 4;
        drawTop += frameTop;
        const int drawBottom = drawTop + rowCount;
        if (drawTop >= clipBottom || drawBottom < clipTop)
            return;

        WORD* const depthBase = graph->softwareDepthBuffer();
        const int depthPitch = graph->softwareDepthPitch();
        if (!graph->backBufferPixels())
            graph->lockBackBuffer();
        WORD* const colorBase = static_cast<WORD*>(graph->backBufferPixels());
        const int colorPitch = graph->backBufferPitchPixels();
        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_TEXTURE) == 0u)
            return;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool directPayload = !palettePayload;

        const BYTE* drawPaletteBlock = nullptr;
        std::array<DWORD, 256> drawPaletteScratch{};
        {

            const size_t blockBytes = static_cast<size_t>(SoftwareGammaPaletteBlockBytes());
            GammaRawPair spriteOverride{};
            const bool hasSpriteOverride = sprite->spriteGammaOverride(spriteOverride);
            size_t blockIndex = 0u;
            if (!hasSpriteOverride)
            {
                blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                    ? static_cast<size_t>((sprite->runtimeFlags() >> 12u) & 3u)
                    : 0u;
                drawPaletteBlock = frameStorage() + blockIndex * blockBytes;
            }
            else
            {

                blockIndex = (typeFlags & VID_TYPE_3D) != 0u ? 4u : 0u;
                std::memcpy(drawPaletteScratch.data(), frameStorage() + blockIndex * blockBytes, blockBytes);
                GammaRawPair drawPaletteApply{};
                sprite->buildRetailGammaPair(drawPaletteApply);
                if ((property & P_GAMMA) == 0u)
                    drawPaletteApply.setSaturatingAddRetail(drawPaletteApply, *GRAPH::CurrentRawGammaPair());
                ApplyGammaToPaletteRaw(drawPaletteScratch.data(), drawPaletteApply);
                drawPaletteBlock = reinterpret_cast<const BYTE*>(drawPaletteScratch.data());
            }
            g_softwarePaletteLookup = reinterpret_cast<const DWORD*>(drawPaletteBlock);
        }

        const int constantDepthInt = std::min(baseDepth + 0x400, 0x7FFF);
        const WORD constantDepth = static_cast<WORD>(constantDepthInt);
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);
        WORD rowDepth = constantDepth;
        const int alphaDepthStep = alphaPalettePayload && this->sizeZ() > this->sizeY() ? -8 : 0;

        if (alphaPalettePayload)
        {
            g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(constantDepth);
        }
        else if (zPalettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord) | (static_cast<DWORD>(baseDepthWord) << 16u);
            g_softwareDepthWordPrimary = baseDepthWord;
            g_softwareDepthWordSecondary = baseDepthWord;
        }

        for (int sourceRow = 0; sourceRow < rowCount; ++sourceRow)
        {
            const int dy = drawTop + sourceRow;
            if (dy >= clipBottom)
                break;
            const bool visibleRow = dy >= clipTop;
            int sourceX = 0;
            for (;;)
            {
                const int skip = row[0];
                const int run = row[1];
                row += 2;
                if (skip == 0 && run == 0)
                    break;
                sourceX += skip;

                const WORD* zWords = nullptr;
                if (zPalettePayload)
                {
                    zWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                if (visibleRow)
                {
                    WORD* const colorRow = colorBase + static_cast<size_t>(dy) * static_cast<size_t>(colorPitch);
                    WORD* const depthRow = depthBase + static_cast<size_t>(dy) * static_cast<size_t>(depthPitch);
                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;

                        const WORD oldDepth = depthRow[dx];
                        WORD z = rowDepth;
                        if (zPalettePayload)
                            z = static_cast<WORD>(baseDepthWord + zWords[i]);

                        if (alphaPalettePayload)
                        {
                            if (z < oldDepth)
                                continue;
                        }
                        else if (zPalettePayload)
                        {
                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                        }
                        else if (palettePayload || directPayload)
                        {

                            if (constantDepthInt < static_cast<int>(oldDepth))
                                continue;
                        }

                        if (alphaPalettePayload)
                        {
                            const DWORD source = reinterpret_cast<const DWORD*>(drawPaletteBlock)[paletteIndexes[i]];
                            const DWORD dest = UnpackRgb565ToBgra(colorRow[dx]);
                            const DWORD alpha = source >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destWeight = 256u - sourceWeight;
                            const DWORD b = (sourceWeight * (source & 0xFFu) + destWeight * (dest & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((source >> 8u) & 0xFFu) + destWeight * ((dest >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((source >> 16u) & 0xFFu) + destWeight * ((dest >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = PackRgb565FromBgra(0xFF000000u | (r << 16u) | (g << 8u) | b);
                            continue;
                        }

                        WORD sourceWord = 0u;
                        if (palettePayload)
                        {
                            sourceWord = reinterpret_cast<const WORD*>(drawPaletteBlock)[paletteIndexes[i]];
                        }
                        else
                        {
                            // Direct software16 owner copies the physical WORD
                            // unchanged after the depth test.
                            sourceWord = colorWords[i];
                        }

                        depthRow[dx] = z;
                        colorRow[dx] = sourceWord;
                    }
                }
                sourceX += run;
            }

            if (visibleRow && alphaDepthStep != 0)
            {
                rowDepth = static_cast<WORD>(rowDepth + alphaDepthStep);
                g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }
    }

    void VID_SOFTWARE16::DrawToVid(
        SPRITE* sprite,
        void* texSizeRaw,
        BASE_TEXTURE* texture,
        BASE_TEXTURE* zTexture)
    {
        VID_HARDWARE::TEX_SIZE* const texSize = static_cast<VID_HARDWARE::TEX_SIZE*>(texSizeRaw);

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        const VID_HARDWARE::TEX_SIZE& tile = *texSize;
        const int clipLeft = tile.sourceX;
        const int clipTop = tile.sourceY;
        const int clipRight = tile.sourceX + tile.width;
        const int clipBottom = tile.sourceY + tile.height;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());

        float drawLeftFloat = sprite->X();
        drawLeftFloat -= static_cast<float>(sizeX / 2);
        drawLeftFloat -= static_cast<float>(tile.destinationX);
        drawLeftFloat -= static_cast<float>(tile.sourceX);
        const int drawLeft = truncateFloatToInt32Software16(drawLeftFloat);

        float drawTopFloat = sprite->Y() - sprite->Z();
        drawTopFloat -= static_cast<float>(sizeY / 2);
        drawTopFloat -= static_cast<float>(tile.destinationY);
        drawTopFloat -= static_cast<float>(tile.sourceY);
        int drawTop = truncateFloatToInt32Software16(drawTopFloat);

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = static_cast<int>(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
            baseDepth += 0x3FFF;
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = static_cast<int>(
                SPRITE::rawDirectionSin(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                moveUpZ() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frame = sprite->currentFrame();
        BYTE* const frameBase = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(frameBase));
        BYTE* row = frameBase + 2 + 6 * contourCount;
        const int frameTop = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row));
        const int rowCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row + 2));
        row += 4;
        drawTop += frameTop;
        const int drawBottom = drawTop + rowCount;
        if (drawTop >= clipBottom || drawBottom < clipTop)
            return;

        int zPitchBytes = 0;
        WORD* const zBits = zTexture->lock16(&zPitchBytes, nullptr);
        int colorPitchBytes = 0;
        WORD* const colorBits = texture->lock16(&colorPitchBytes, nullptr);
        const WORD typeFlags = formatFlags();


        const bool texturePayload = (typeFlags & VID_TYPE_TEXTURE) != 0u;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && texturePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool constantPalettePayload = texturePayload && palettePayload && !alphaPalettePayload && !zPalettePayload;
        const bool directPayload = texturePayload && !palettePayload;

        if (!texturePayload)
        {
            texture->unlock();
            zTexture->unlock();
            return;
        }

        const BYTE* paletteBlock = nullptr;
        if (palettePayload)
        {
            const std::size_t blockBytes = static_cast<std::size_t>(PaletteSize());
            const std::size_t blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                ? static_cast<std::size_t>((sprite->runtimeFlags() >> 12u) & 3u)
                : 0u;
            paletteBlock = frameStorage() + blockIndex * blockBytes;
            g_softwarePaletteLookup = reinterpret_cast<const DWORD*>(paletteBlock);
        }

        const int constantDepthInt = std::min(baseDepth + 0x400, 0x7FFF);
        const WORD constantDepth = static_cast<WORD>(constantDepthInt);
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);
        WORD rowDepth = constantDepth;
        const int alphaDepthStep = alphaPalettePayload && this->sizeZ() > this->sizeY() ? -8 : 0;


        if (alphaPalettePayload)
        {
            g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(constantDepth);
        }
        else if (zPalettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord) | (static_cast<DWORD>(baseDepthWord) << 16u);
            g_softwareDepthWordPrimary = baseDepthWord;
            g_softwareDepthWordSecondary = baseDepthWord;
        }

        for (int sourceRow = 0; sourceRow < rowCount; ++sourceRow)
        {
            const int dy = drawTop + sourceRow;
            if (dy >= clipBottom)
                break;
            const bool visibleRow = dy >= clipTop;
            int sourceX = 0;

            for (;;)
            {
                const int skip = row[0];
                const int run = row[1];
                row += 2;
                if (skip == 0 && run == 0)
                    break;
                sourceX += skip;

                const WORD* zWords = nullptr;
                if (zPalettePayload)
                {
                    zWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<std::size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<std::size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<std::size_t>(run) * 2u;
                }

                if (visibleRow)
                {

                    WORD* const colorRow = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(colorBits) +
                        static_cast<std::ptrdiff_t>(dy) * static_cast<std::ptrdiff_t>(colorPitchBytes));
                    WORD* const depthRow = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(zBits) +
                        static_cast<std::ptrdiff_t>(dy) * static_cast<std::ptrdiff_t>(zPitchBytes));

                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;
                        const WORD oldDepth = depthRow[dx];

                        if (alphaPalettePayload)
                        {
                            // drawSoftware16AlphaPaletteSpanWithDepth: unsigned compare, skip only when the
                            // new constant depth is strictly below old depth.
                            if (rowDepth < oldDepth)
                                continue;

                            const DWORD sourceColor = reinterpret_cast<const DWORD*>(paletteBlock)[paletteIndexes[i]];
                            const DWORD destination = UnpackRgb565ToBgra(colorRow[dx]);
                            const DWORD alpha = sourceColor >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destinationWeight = 256u - sourceWeight;
                            const DWORD b = (sourceWeight * (sourceColor & 0xFFu) + destinationWeight * (destination & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((sourceColor >> 8u) & 0xFFu) + destinationWeight * ((destination >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((sourceColor >> 16u) & 0xFFu) + destinationWeight * ((destination >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = PackRgb565FromBgra(0xFF000000u | (r << 16u) | (g << 8u) | b);
                            // Alpha owner never writes the depth surface.
                            continue;
                        }

                        if (zPalettePayload)
                        {
                            const WORD z = static_cast<WORD>(baseDepthWord + zWords[i]);
                            // These paths use a strict signed 16-bit greater-than depth test.
                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                            depthRow[dx] = z;
                            colorRow[dx] = reinterpret_cast<const WORD*>(paletteBlock)[paletteIndexes[i]];
                            continue;
                        }

                        // These paths compare the zero-extended previous depth as a 32-bit value; equality is accepted.
                        if (constantDepthInt < static_cast<int>(oldDepth))
                            continue;

                        depthRow[dx] = constantDepth;
                        if (constantPalettePayload)
                            colorRow[dx] = reinterpret_cast<const WORD*>(paletteBlock)[paletteIndexes[i]];
                        else if (directPayload)
                            colorRow[dx] = colorWords[i];
                    }
                }

                sourceX += run;
            }

            // Only the alpha+palette family applies the -8 row
            // slope when VID +0x24 is greater than +0x20.
            if (visibleRow && alphaDepthStep != 0)
            {
                rowDepth = static_cast<WORD>(rowDepth + alphaDepthStep);
                g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }


        // Release color texture first, then the depth texture.
        texture->unlock();
        zTexture->unlock();
    }

    int VID_SOFTWARE16::PaletteSize() const
    {

        return ((formatFlags() & VID_TYPE_ALPHA) != 0 ? 4 : 2) << 8;
    }
}
