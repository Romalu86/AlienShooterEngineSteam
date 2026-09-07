#include "vid/vid_hardware.h"
#include "vid/vid_software16.h"

#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "graph.h"
#include "graphics/base_texture.h"
#include "map.h"
#include "sprite.h"
#include "script/logic_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <limits>
#include <new>

namespace as1
{
    namespace
    {


        DWORD floatBits(float value)
        {
            DWORD bits = 0;
                                                                                      
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        DWORD paletteBgra(const VID& vid, BYTE index)
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            // The Stage11 x86 sidecar palette has no production population path.
            // Preserve its effective grayscale fallback without linking the
            // reconstruction-only vector/unordered_map decode subsystem.
            (void)vid;
            const DWORD v = static_cast<DWORD>(index);
            return 0xFF000000u | (v << 16u) | (v << 8u) | v;
#else
            const auto& palette = vid.palette();
            if (palette.empty())
            {
                const DWORD v = static_cast<DWORD>(index);
                return 0xFF000000u | (v << 16u) | (v << 8u) | v;
            }
            const VID::PaletteEntry& e = palette[static_cast<std::size_t>(index) % palette.size()];
            const BYTE a = (vid.formatFlags() & VID_TYPE_ALPHA) != 0 ? e.a : 0xFFu;
            return (static_cast<DWORD>(a) << 24u) |
                   (static_cast<DWORD>(e.r) << 16u) |
                   (static_cast<DWORD>(e.g) << 8u) |
                   static_cast<DWORD>(e.b);
#endif
        }

        constexpr DWORD kD3dRenderStateZFunc = 23u;
        constexpr DWORD kD3dRenderStateAlphaBlendEnable = 27u;
        constexpr DWORD kD3dCmpGreaterEqual = 7u;
        constexpr DWORD kD3dCmpAlways = 8u;
        constexpr DWORD kD3dBlendOne = 2u;
        constexpr DWORD kD3dBlendSrcAlpha = 5u;
        constexpr DWORD kD3dBlendInvSrcAlpha = 6u;
        constexpr DWORD kD3dBlendDestColor = 9u;

        float interpolateHardwareEffectCurve(const VID_HARDWARE* owner,
                                             float position,
                                             int baseOffset) noexcept
        {
            const int segment = static_cast<int>(position);
            if (segment >= 7)
                return owner->weaponFloatAt(baseOffset + 7 * 4);

            const float first = owner->weaponFloatAt(baseOffset + segment * 4);
            const float second = owner->weaponFloatAt(baseOffset + (segment + 1) * 4);
            return (second - first) * (position - static_cast<float>(segment)) + first;
        }

        std::int32_t retailWrapAdd32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        std::int32_t retailWrapSub32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        std::int32_t retailWrapMul32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
        }

        std::int32_t retailAbs32(std::int32_t value) noexcept
        {
            const std::uint32_t sign = static_cast<std::uint32_t>(value) >> 31u;
            const std::uint32_t mask = 0u - sign;
            return static_cast<std::int32_t>((static_cast<std::uint32_t>(value) ^ mask) - mask);
        }

        int retailFtolLow32Hardware(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int retailFtolDifferenceHardware(float lhs, float rhs) noexcept
        {
            return retailFtolLow32Hardware(lhs - rhs);
        }

        int retailFtolDifference3Hardware(float lhs, float rhs1, float rhs2) noexcept
        {
            const long double value = static_cast<long double>(lhs) -
                                      static_cast<long double>(rhs1) -
                                      static_cast<long double>(rhs2);
            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        void deleteBaseTextureThroughVirtualDestructor(BASE_TEXTURE* texture) noexcept
        {
            if (!texture)
                return;
            delete texture;
        }


    }

    VID_HARDWARE::VID_HARDWARE() = default;

    VID_HARDWARE::VID_HARDWARE(const VID_HARDWARE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_HARDWARE&>(other).nextMirror = this;

        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));

        m_textureLayout = other.m_textureLayout;
        m_texturePageCount = other.m_texturePageCount;
        m_texturePages = other.m_texturePages;
    }

    VID_HARDWARE* VID_HARDWARE::CreateMirror()
    {

        return new (std::nothrow) VID_HARDWARE(*this);
    }

    VID_HARDWARE::VID_HARDWARE(int nvid, int width, int height)
    {

        nVid = nvid;
        sizeXYZ = VECTOR(256.0f, 256.0f, 1.0f);
        setVidWidth(static_cast<short>(width));
        setVidHeight(static_cast<short>(height));
        noDir = 1;
        layer = 0;
        name = STRING("Self Created Hardware Prerendered Ground ");
        type = 549; // 0x225: TEXTURE | ZBUFFER | HARDWARE | LINKXYZ.
        createEmptyTextures(width, height);
    }

    VID_HARDWARE::~VID_HARDWARE()
    {

        releaseHardwareResources();
    }

    VID_HARDWARE* vidHardwareScalarDeletingDestructor(VID_HARDWARE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_HARDWARE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    void VID_HARDWARE::releaseHardwareResources() noexcept
    {
        if (!isMirrorChainOwner())
            return;

        if (m_textureLayout)
            ::operator delete(m_textureLayout);
        m_textureLayout = nullptr;

        if (m_texturePages)
        {

            --m_texturePageCount;
            if (static_cast<std::int16_t>(m_texturePageCount) >= 0)
            {
                do
                {
                    BASE_TEXTURE* const texture =
                        m_texturePages[static_cast<std::int16_t>(m_texturePageCount)];
                    deleteBaseTextureThroughVirtualDestructor(texture);
                    --m_texturePageCount;
                }
                while (static_cast<std::int16_t>(m_texturePageCount) >= 0);
            }
            ::operator delete(m_texturePages);
        }
        m_texturePages = nullptr;
        m_texturePageCount = 0;
    }

    void VID_HARDWARE::createEmptyTextures(int width, int height)
    {

        const std::int32_t tilesXCapacity = width / 256 + 1;
        const std::int32_t tilesYCapacity = height / 256 + 1;
        const std::int32_t rawRecordCapacity =
            retailWrapAdd32(retailWrapMul32(tilesXCapacity, tilesYCapacity), 1);
        const std::uint32_t rawRecordBytes =
            static_cast<std::uint32_t>(retailWrapMul32(rawRecordCapacity, 36));

        m_textureLayout = static_cast<TEX_SIZE*>(
            ::operator new(static_cast<std::size_t>(rawRecordBytes), std::nothrow));
        if (!m_textureLayout)
        {
            ReportResourceError(2, "texcoor", rawRecordCapacity);
            std::exit(1);
        }

        m_texturePageCount = 0;
        std::int32_t recordIndex = 0;
        std::int32_t destinationY = 0;
        std::int32_t remainingHeight = height;
        while (destinationY < height)
        {
            std::int32_t destinationX = 0;
            std::int32_t remainingWidth = width;
            while (destinationX < width)
            {
                TEX_SIZE* const tile = reinterpret_cast<TEX_SIZE*>(
                    reinterpret_cast<BYTE*>(m_textureLayout) +
                    static_cast<std::uint32_t>(retailWrapMul32(recordIndex, 36)));

                if (recordIndex != 0)
                    (tile - 1)->next = recordIndex;

                tile->textureIndex = static_cast<std::int16_t>(m_texturePageCount);
                tile->sourceX = 0;
                tile->sourceY = 0;
                tile->destinationX = destinationX;
                tile->destinationY = destinationY;
                tile->width = remainingWidth > 256 ? 256 : remainingWidth;
                tile->height = remainingHeight > 256 ? 256 : remainingHeight;
                tile->next = 0;

                destinationX = retailWrapAdd32(destinationX, 256);
                remainingWidth = retailWrapAdd32(remainingWidth, -256);
                m_texturePageCount = static_cast<WORD>(m_texturePageCount + 2u);
                recordIndex = retailWrapAdd32(recordIndex, 1);
            }

            destinationY = retailWrapAdd32(destinationY, 256);
            remainingHeight = retailWrapAdd32(remainingHeight, -256);
        }


        const std::int32_t signedTextureCount = static_cast<std::int16_t>(m_texturePageCount);
        const std::uint32_t textureTableBytes =
            static_cast<std::uint32_t>(retailWrapMul32(signedTextureCount, 4));
        m_texturePages = static_cast<BASE_TEXTURE**>(
            ::operator new(static_cast<std::size_t>(textureTableBytes), std::nothrow));
        if (!m_texturePages)
        {
            ReportResourceError(2, "textures", signedTextureCount);
            return;
        }

        std::int32_t textureIndex = 0;
        while (textureIndex < signedTextureCount)
        {
            const std::int32_t recordForPair = textureIndex / 2;
            TEX_SIZE* const tile = reinterpret_cast<TEX_SIZE*>(
                reinterpret_cast<BYTE*>(m_textureLayout) +
                static_cast<std::uint32_t>(retailWrapMul32(recordForPair, 36)));

            BASE_TEXTURE* colorTexture = new (std::nothrow) BASE_TEXTURE(
                tile->width, tile->height, 23u, 0u);
            m_texturePages[textureIndex] = colorTexture;

            int colorPitchBytes = 0;
            WORD* colorBits = colorTexture->lock16(&colorPitchBytes, nullptr);
            const std::uint32_t colorBytes = static_cast<std::uint32_t>(
                retailWrapMul32(colorTexture->height(), colorPitchBytes));
            std::memset(colorBits, 0, static_cast<std::size_t>(colorBytes));
            colorTexture->unlock();

            BASE_TEXTURE* zTexture = new (std::nothrow) BASE_TEXTURE(
                tile->width, tile->height, 80u, 2u);
            m_texturePages[textureIndex + 1] = zTexture;

            int zPitchBytes = 0;
            WORD* zBits = zTexture->lock16(&zPitchBytes, nullptr);
            const std::int32_t zPitchWords = zPitchBytes / 2;
            const std::uint32_t zWordCount = static_cast<std::uint32_t>(
                retailWrapMul32(zTexture->height(), zPitchWords));
            WORD* zWrite = zBits;
            const std::uint32_t dwordCount = zWordCount >> 1u;
            if ((zWordCount & 1u) != 0u)
                *zWrite++ = static_cast<WORD>(0x0400u);

            if (static_cast<std::uint16_t>(dwordCount) != 0u)
            {
                DWORD* dwordWrite = reinterpret_cast<DWORD*>(zWrite);
                for (std::uint32_t i = 0; i < dwordCount; ++i)
                    dwordWrite[i] = 0x04000400u;
            }
            zTexture->unlock();

            textureIndex = retailWrapAdd32(textureIndex, 2);
        }
    }

    void VID_HARDWARE::Load(RESOURCE* resource)
    {

        if (resource->GoNext(RESOURCE::ResTypes::SURFACE) != 0)
            ReportResourceError(5, "SURF", 0);

        resource->read(&m_texturePageCount, 2u);
        if (m_texturePageCount == 0)
            return;

        script::LogicAdaptiveCodec* colorCodec = nullptr;
        script::LogicAdaptiveCodec* zCodec = nullptr;
        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_COMPRESS) != 0u)
        {
            colorCodec = new (std::nothrow) script::LogicAdaptiveCodec();
            if (colorCodec)
                colorCodec->setMode((typeFlags & VID_TYPE_SHADOW) != 0u ? 1 : 2);
            zCodec = new (std::nothrow) script::LogicAdaptiveCodec();
            if (zCodec)
                zCodec->setMode(2);
        }

        const int signedTextureCount = static_cast<std::int16_t>(m_texturePageCount);
        const std::uint32_t textureTableBytes = static_cast<std::uint32_t>(
            retailWrapMul32(signedTextureCount, 4));
        m_texturePages = static_cast<BASE_TEXTURE**>(
            ::operator new(static_cast<std::size_t>(textureTableBytes), std::nothrow));
        if (!m_texturePages)
        {
            ReportResourceError(2, "textures", signedTextureCount);
            return;
        }
        if (signedTextureCount > 0)
            std::fill_n(m_texturePages, signedTextureCount, nullptr);

        void* const unpackOwner = ::operator new(0x20008u, std::nothrow);
        if (!unpackOwner)
        {
            ReportResourceError(2, "(unpack)", 0);
            return;
        }
        BYTE* const unpack = static_cast<BYTE*>(unpackOwner);

        int textureIndex = 0;
        while (textureIndex < signedTextureCount)
        {
            VID* loadingVid = MAP::NullVid();
            core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            if (vidTable.count() > 0)
            {
                VID* const first = vidTable.slot(0);
                if (first)
                    loadingVid = first;
            }
            GRAPH::CurrentGraph()->advanceMovieFrameClock(loadingVid);

            WORD widthWord = 0;
            WORD heightWord = 0;
            resource->read(&widthWord, 2u);
            resource->read(&heightWord, 2u);
            const int width = static_cast<int>(static_cast<std::int16_t>(widthWord));
            const int height = static_cast<int>(static_cast<std::int16_t>(heightWord));

            DWORD requestedFormat = 23u;
            if ((typeFlags & VID_TYPE_SHADOW) != 0u)
                requestedFormat = ((typeFlags & (VID_TYPE_TEXTURE | VID_TYPE_ALPHA)) == (VID_TYPE_TEXTURE | VID_TYPE_ALPHA)) ? 0x33545844u : 0x31545844u;
            else if ((typeFlags & VID_TYPE_PALETTE) != 0u)
                requestedFormat = 41u;
            else if ((typeFlags & (VID_TYPE_TEXTURE | VID_TYPE_ALPHA)) == (VID_TYPE_TEXTURE | VID_TYPE_ALPHA))
                requestedFormat = 26u;

            BASE_TEXTURE* colorTexture = new (std::nothrow) BASE_TEXTURE(width, height, requestedFormat, 0u);
            m_texturePages[textureIndex] = colorTexture;
            if (!colorTexture->nativeHandle() && !colorTexture->isLoaded())
            {
                ReportResourceError(3, "texture", 0);
                return;
            }

            std::array<BYTE, 768> paletteBytes;
            if ((typeFlags & VID_TYPE_PALETTE) != 0u)
            {
                ReportResourceError(10, "palette %i", colorTexture->format() == 41u ? 1 : 0);
                resource->read(paletteBytes.data(), static_cast<unsigned>(paletteBytes.size()));
                if (colorTexture->format() == 41u)
                {
                    std::array<DWORD, 256> paletteDwords{};
                    for (std::size_t i = 0; i < paletteDwords.size(); ++i)
                    {
                        const DWORD c0 = paletteBytes[i * 3u + 0u];
                        const DWORD c1 = paletteBytes[i * 3u + 1u];
                        const DWORD c2 = paletteBytes[i * 3u + 2u];
                        paletteDwords[i] = 0xFF000000u | (c0 << 16u) | (c1 << 8u) | c2;
                    }
                    colorTexture->createPaletteSlot(paletteDwords.data());
                }
            }

            DWORD decodedSize;
            resource->read(&decodedSize, 4u);
            const int missing = resource->ReadPayload(unpack, decodedSize, colorCodec);

            if (missing != 0)
                ReportResourceError(5, "Can't decode", textureIndex);

            int pitchBytes = 0;
            const std::uint32_t rawExpectedColorBytes = static_cast<std::uint32_t>(
                retailWrapMul32(retailWrapMul32(width, height), 2));
            if ((typeFlags & VID_TYPE_PALETTE) == 0u &&
                static_cast<std::int32_t>(decodedSize) < static_cast<std::int32_t>(rawExpectedColorBytes))
            {
                ReportResourceError(10, "Load DXT", 0);
                WORD* const locked = colorTexture->lock16(&pitchBytes, nullptr);
                if (!locked)
                {
                    ReportResourceError(0, "DXT texture surface", 0);
                }
                else
                {
                    std::memcpy(locked, unpack, static_cast<std::size_t>(decodedSize));
                    colorTexture->unlock();
                }
            }
            else
            {
                WORD* const locked = colorTexture->lock16(&pitchBytes, nullptr);
                if (!locked)
                {

                    ReportResourceError(0, "texture surface", 0);
                    return;
                }
                else
                {
                    BYTE* destinationRow = reinterpret_cast<BYTE*>(locked);
                    for (int row = 0; row < height; ++row)
                    {
                        const DWORD actualFormat = colorTexture->format();
                        if ((typeFlags & VID_TYPE_PALETTE) != 0u)
                        {
                            if (actualFormat == 41u)
                            {
                                std::memcpy(destinationRow,
                                            unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                                            static_cast<std::size_t>(width));
                            }
                            else
                            {
                                WORD* destination = reinterpret_cast<WORD*>(destinationRow);
                                const BYTE* indices = unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
                                for (int x = 0; x < width; ++x)
                                {
                                    const std::size_t paletteOffset = static_cast<std::size_t>(indices[x]) * 3u;
                                    const DWORD c0 = paletteBytes[paletteOffset + 0u];
                                    const DWORD c1 = paletteBytes[paletteOffset + 1u];
                                    const DWORD c2 = paletteBytes[paletteOffset + 2u];
                                    destination[x] = static_cast<WORD>(
                                        ((c2 >> 3u) & 0x001Fu) |
                                        ((c0 & 0xF8u) << g_color16RedShift) |
                                        (g_color16GreenMask & (c1 << g_color16GreenShift)));
                                }
                            }
                        }
                        else if (actualFormat == 23u || actualFormat == 26u)
                        {
                            std::memcpy(destinationRow,
                                        unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 2u,
                                        static_cast<std::size_t>(width) * 2u);
                        }
                        else
                        {
                            WORD* destination = reinterpret_cast<WORD*>(destinationRow);
                            const WORD* source = reinterpret_cast<const WORD*>(unpack) +
                                static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
                            for (int x = 0; x < width; ++x)
                            {
                                const DWORD value = source[x];
                                destination[x] = static_cast<WORD>((value & 0x001Fu) | ((value >> 1u) & 0x7FE0u));
                            }
                        }
                        destinationRow += pitchBytes;
                    }
                    colorTexture->unlock();
                }
            }

            int lastTextureIndex = textureIndex;
            if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
            {
                const int zIndex = textureIndex + 1;
                BASE_TEXTURE* const zTexture = new (std::nothrow) BASE_TEXTURE(width, height, 80u, 2u);
                m_texturePages[zIndex] = zTexture;

                DWORD zDecodedSize;
                resource->read(&zDecodedSize, 4u);
                if (zTexture->isLoaded())
                {
                    int zPitchBytes = 0;
                    WORD* const zBits = zTexture->lock16(&zPitchBytes, nullptr);
                    if (zBits)
                    {
                        const DWORD expectedZBytes = static_cast<DWORD>(retailWrapMul32(zPitchBytes, height));
                        if (zDecodedSize == expectedZBytes)
                        {
                            const int zMissing = resource->ReadPayload(zBits, zDecodedSize, zCodec);
                            if (zMissing != 0)
                                ReportResourceError(5, "Can't decode z", static_cast<std::int32_t>(
                                    static_cast<std::uint32_t>(zDecodedSize) - static_cast<std::uint32_t>(zMissing)));
                        }
                        else
                        {
                            ReportResourceError(5, "ZBuffer: invalid size", static_cast<int>(zDecodedSize));
                        }
                        zTexture->unlock();
                    }
                    else
                    {
                        ReportResourceError(0, "texture z surface", 0);
                        resource->shift(static_cast<int>(zDecodedSize));
                    }
                }
                else
                {
                    ReportResourceError(3, "texture z surface", 0);
                    resource->shift(static_cast<int>(zDecodedSize));
                }
                lastTextureIndex = zIndex;
            }

            textureIndex = retailWrapAdd32(lastTextureIndex, 1);
        }

        if (resource->GoNext(RESOURCE::ResTypes::DATA) != 0)
            ReportResourceError(5, "DATA", 0);

        if ((typeFlags & VID_TYPE_NEWVERSION) != 0u)
        {
            void* loaded = nullptr;
            resource->SubLoad(&loaded, nullptr);
            m_textureLayout = static_cast<TEX_SIZE*>(loaded);
            if (!m_textureLayout)
                ReportResourceError(5, "tex_coor", 0);
        }
        else
        {
            const int recordCount = resource->SubSize() / 20;
            const std::uint32_t texSizeBytes = static_cast<std::uint32_t>(
                retailWrapMul32(recordCount, static_cast<int>(sizeof(TEX_SIZE))));
            m_textureLayout = static_cast<TEX_SIZE*>(
                ::operator new(static_cast<std::size_t>(texSizeBytes), std::nothrow));
            for (int record = 0; record < recordCount; ++record)
            {
                TEX_SIZE& out = m_textureLayout[record];
                resource->read(&out.marker, 4u);
                WORD value = 0;
                resource->read(&value, 2u); out.textureIndex = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.sourceX = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.sourceY = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.width = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.height = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.destinationX = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.destinationY = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.next = static_cast<int>(static_cast<std::int16_t>(value));
            }
        }

        resource->GoNext(RESOURCE::ResTypes::SHADOW);
        delete colorCodec;
        delete zCodec;
        ::operator delete(unpackOwner);
    }

    void VID_HARDWARE::selectHardwareRenderLayer() noexcept
    {
        if ((property & 0x40000000u) != 0u)
        {
            layer = 4;
            return;
        }
        if (spriteType == 0x40u)
        {
            layer = 10;
            return;
        }
        if (m_texturePageCount == 0)
        {
            layer = 15;
            return;
        }

        const WORD typeFlags = formatFlags();
        if ((typeFlags & (VID_TYPE_ZBUFFER | VID_TYPE_ALPHA)) ==
            (VID_TYPE_ZBUFFER | VID_TYPE_ALPHA))
        {
            layer = 9;
            return;
        }
        if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
        {
            layer = 0;
            return;
        }
        if (nvid() == 1)
        {
            layer = 14;
            return;
        }
        if ((typeFlags & VID_TYPE_ALPHA) != 0u)
        {
            layer = (property & 0x00010000u) != 0u ? 9 : 12;
            return;
        }
        layer = 8;
    }

    void VID_HARDWARE::SetLayer()
    {
        selectHardwareRenderLayer();
    }


    WORD compositeSpriteIntoHardwareVid(VID_HARDWARE* owner, SPRITE* sprite) noexcept
    {

        const WORD typeFlags = owner->formatFlags();
        if ((typeFlags & VID_TYPE_TEXTURE) == 0u ||
            (typeFlags & VID_TYPE_ZBUFFER) == 0u ||
            owner->directionCount() != 1)
        {
            return typeFlags;
        }

        VID* const sourceVid = sprite->Vid();
        if ((sourceVid->properties() & P_INVISIBLEFORENEMY) != 0u)
        {
            return static_cast<WORD>(reinterpret_cast<std::uintptr_t>(sourceVid));
        }

        const int savedLeft = g_softwareClipLeft;
        const int savedRight = g_softwareClipRight;
        const int savedTop = g_softwareClipTop;
        const int savedBottom = g_softwareClipBottom;

        const int spriteX = retailFtolLow32Hardware(sprite->X());
        const int spriteYProjected = retailFtolDifferenceHardware(sprite->Y(), sprite->Z());
        const int sourceHalfWidth = static_cast<std::int16_t>(sourceVid->vidWidth()) / 2;
        const int sourceHalfHeight = static_cast<std::int16_t>(sourceVid->vidHeight()) / 2;

        VID_HARDWARE::TEX_SIZE* tile = owner->textureLayout();
        while (tile)
        {
            const int tileHalfWidth = tile->width / 2;
            const int tileHalfHeight = tile->height / 2;

            int deltaX = retailWrapSub32(tile->destinationX, spriteX);
            deltaX = retailWrapAdd32(deltaX, tile->sourceX);
            deltaX = retailWrapAdd32(deltaX, tileHalfWidth);
            const int absX = retailAbs32(deltaX);
            const int thresholdX = retailWrapAdd32(tileHalfWidth, sourceHalfWidth);

            if (absX < thresholdX)
            {
                int deltaY = retailWrapSub32(tile->destinationY, spriteYProjected);
                deltaY = retailWrapAdd32(deltaY, tile->sourceY);
                deltaY = retailWrapAdd32(deltaY, tileHalfHeight);
                const int absY = retailAbs32(deltaY);
                const int thresholdY = retailWrapAdd32(tileHalfHeight, sourceHalfHeight);

                if (absY < thresholdY)
                {
                    g_softwareClipLeft = tile->sourceX;
                    g_softwareClipRight = retailWrapAdd32(tile->sourceX, tile->width);
                    g_softwareClipTop = tile->sourceY;
                    g_softwareClipBottom = retailWrapAdd32(tile->sourceY, tile->height);

                    const int textureIndex = tile->textureIndex;
                    BASE_TEXTURE** const table = owner->texturePages();
                    sourceVid->DrawToVid(sprite, tile, table[textureIndex], table[textureIndex + 1]);
                }
            }

            const int nextIndex = tile->next;
            if (nextIndex == 0)
                break;
            tile = reinterpret_cast<VID_HARDWARE::TEX_SIZE*>(
                reinterpret_cast<BYTE*>(owner->textureLayout()) +
                static_cast<std::uint32_t>(retailWrapMul32(nextIndex, 36)));
        }

        g_softwareClipLeft = savedLeft;
        g_softwareClipRight = savedRight;
        g_softwareClipTop = savedTop;
        g_softwareClipBottom = savedBottom;
        return static_cast<WORD>(savedBottom);
    }

    void VID_HARDWARE::AddVidToVid(SPRITE* sprite)
    {
        (void)compositeSpriteIntoHardwareVid(this, sprite);
    }

    void VID_HARDWARE::Draw(const SPRITE* sprite)
    {

        if (m_texturePageCount == 0)
            return;

        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const int frameIndex = sprite->currentFrame();
        const TEX_SIZE* first = &m_textureLayout[static_cast<std::size_t>(frameIndex)];
        if (first->height == 0)
            return;

        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        const WORD typeFlags = formatFlags();
        const float currentX = sprite->X();
        const float currentY = sprite->Y();
        const float currentZ = sprite->Z();
        const float currentProjectedY = currentY - currentZ;

        float drawScaleX = scaleXYZ.x;
        float drawScaleY = scaleXYZ.y;
        int effectOffsetX = 0;
        int effectOffsetY = 0;
        int effectOffsetZ = 0;
        const DWORD auxFlags = runtimeAuxFlags();
        if ((auxFlags & 0x02u) != 0u)
        {
            const float position = sprite->actionAuxEffectCurvePosition();
            drawScaleX *= interpolateHardwareEffectCurve(this, position, 0x0E4);
            drawScaleY *= interpolateHardwareEffectCurve(this, position, 0x104);
        }
        if ((auxFlags & 0x04u) != 0u)
        {
            const float position = sprite->actionAuxEffectCurvePosition();
            effectOffsetX = retailFtolLow32Hardware(interpolateHardwareEffectCurve(this, position, 0x144));
            effectOffsetY = retailFtolLow32Hardware(interpolateHardwareEffectCurve(this, position, 0x164));
            effectOffsetZ = retailFtolLow32Hardware(interpolateHardwareEffectCurve(this, position, 0x184));
        }

        int sampleCount = 1;
        if ((property & P_BLUR) != 0u)
        {

            const int sampleWidth = first->width;
            if (sampleWidth != 0)
            {
                const float historyX = sprite->blurHistoryX();
                const float historyProjectedY = sprite->blurHistoryY() - sprite->blurHistoryZ();
                const auto abs64 = [](std::int64_t value) noexcept -> std::int64_t { return value < 0 ? -value : value; };
                const std::int64_t dx = abs64(static_cast<std::int64_t>(historyX - currentX)) / sampleWidth;
                const std::int64_t dy = abs64(static_cast<std::int64_t>(historyProjectedY - currentProjectedY)) / first->height;
                sampleCount = static_cast<int>(2 * std::max(dx, dy) + 1);
            }
        }

        for (int sample = 0; sample < sampleCount; ++sample)
        {
            int screenX = retailFtolDifferenceHardware(currentX, cameraX) + effectOffsetX;
            int screenY = retailFtolDifference3Hardware(currentY, currentZ, cameraY) + effectOffsetY;
            int spriteZInt = retailFtolLow32Hardware(currentZ) + effectOffsetZ;

            if ((property & P_BLUR) != 0u)
            {
                const double ratio = static_cast<double>(sample) / static_cast<double>(sampleCount);
                const float historyX = sprite->blurHistoryX();
                const float historyY = sprite->blurHistoryY();
                const float historyZ = sprite->blurHistoryZ();
                const float historyProjectedY = historyY - historyZ;
                screenX = static_cast<int>(currentX - cameraX + (historyX - currentX) * ratio);
                screenY = static_cast<int>(currentProjectedY - cameraY +
                                           (historyProjectedY - currentProjectedY) * ratio);
                spriteZInt = static_cast<int>(currentZ + (historyZ - currentZ) * ratio);
            }

            if ((property & P_ALWAYSTOP) == 0u && (typeFlags & VID_TYPE_ZBUFFER) == 0u)
            {
                const GraphViewportState& viewport = graph->viewportState();
                if (static_cast<float>(screenX) < viewport.left ||
                    static_cast<float>(screenX) >= viewport.right ||
                    static_cast<float>(screenY) < viewport.top ||
                    static_cast<float>(screenY) >= viewport.bottom)
                {
                    continue;
                }

                const WORD* depth = graph->softwareDepthBuffer();
                const int pitch = graph->softwareDepthPitch();
                if (depth[screenX + screenY * pitch] > spriteZInt * 8 + 0x400)
                    continue;
            }

            float baseDepth = static_cast<float>(spriteZInt) * 0.0001220703125f + 0.015625f;
            if ((property & P_WAVE) != 0u && moveUpZ() != 0.0f)
            {
                baseDepth += SPRITE::rawDirectionSin(
                    static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                    moveUpZ() * 0.0001220703125f * 0.5f;
            }

            int recordIndex = frameIndex;
            for (;;)
            {
                const TEX_SIZE& record = m_textureLayout[static_cast<std::size_t>(recordIndex)];
                if (record.height == 0)
                    break;

                const int left = screenX + static_cast<int>(
                    static_cast<float>(record.destinationX - static_cast<int>(vidWidth()) / 2) * drawScaleX);
                const int top = screenY + static_cast<int>(
                    static_cast<float>(record.destinationY - static_cast<int>(vidHeight()) / 2) * drawScaleY);

                int drawWidth = record.width;
                int drawHeight = record.height;
                const GraphViewportState& viewport = graph->viewportState();
                if (left + drawWidth >= g_softwareClipLeft && left < g_softwareClipRight &&
                    top + drawHeight >= g_softwareClipTop && top < g_softwareClipBottom)
                {
                    if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        if (left + drawWidth > g_softwareClipRight)
                            drawWidth = g_softwareClipRight - left;
                        if (top + drawHeight > g_softwareClipBottom)
                            drawHeight = g_softwareClipBottom - top;
                    }

                    RECTI destination{
                        left,
                        top,
                        left + static_cast<int>(static_cast<float>(drawWidth) * drawScaleX),
                        top + static_cast<int>(static_cast<float>(drawHeight) * drawScaleY)
                    };
                    RECTI source{
                        record.sourceX,
                        record.sourceY,
                        record.sourceX + drawWidth,
                        record.sourceY + drawHeight
                    };


                    if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        BASE_TEXTURE* zTexture = m_texturePages[static_cast<std::size_t>(record.textureIndex + 1)];
                        const int copyResult = graph->drawTextureRectClipped(destination, source, *zTexture);
                        if (copyResult != 0)
                            ReportResourceError(1, "zbuffer", copyResult);
                    }

                    float zTop = baseDepth;
                    float zBottom = baseDepth;
                    DWORD zFunc = kD3dCmpGreaterEqual;
                    if ((property & P_ALWAYSTOP) != 0u || (typeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        zTop = 0.9999998807907104f;
                        zBottom = 0.9999998807907104f;
                        zFunc = kD3dCmpAlways;
                    }
                    else if (this->sizeZ() > this->sizeY())
                    {
                        const float depthDelta = static_cast<float>(drawHeight) * 0.0001220703125f;
                        zTop = baseDepth + depthDelta;
                        zBottom = baseDepth - depthDelta;
                    }
                    graph->setRenderStateCached(kD3dRenderStateZFunc, zFunc);

                    if ((typeFlags & VID_TYPE_ALPHA) != 0u)
                    {
                        if ((typeFlags & VID_TYPE_TEXTURE) != 0u)
                            graph->setAlphaBlendFactors(kD3dBlendSrcAlpha, kD3dBlendInvSrcAlpha);
                        else
                            graph->setAlphaBlendFactors(kD3dBlendDestColor, kD3dBlendOne);
                    }
                    else
                    {
                        graph->setRenderStateCached(kD3dRenderStateAlphaBlendEnable, 0u);
                    }

                    GammaRawPair selectedGamma{};
                    sprite->buildRetailGammaPair(selectedGamma);
                    GammaRawPair drawGamma{};
                    drawGamma.setSaturatingAddRetail(gammaRaw, selectedGamma);
                    if ((property & P_GAMMA) == 0u)
                    {
                        if (const GammaRawPair* graphGamma = GRAPH::CurrentRawGammaPair())
                            drawGamma.setSaturatingAddRetail(drawGamma, *graphGamma);
                    }
                    const DWORD colors[2] = { drawGamma.first, drawGamma.second };

                    BASE_TEXTURE* texture = m_texturePages[static_cast<std::size_t>(record.textureIndex)];

#ifdef _WIN32
                    texture->DrawDepthRectangle(
                        floatBits(zTop),
                        floatBits(zBottom),
                        destination,
                        source,
                        colors);
#else
                    texture->DrawDepthRectangle(
                        floatBits(zTop),
                        floatBits(zBottom),
                        destination,
                        source,
                        colors[0],
                        colors[1]);
#endif
                }

                if (record.next == 0)
                    break;
                recordIndex = record.next;
            }
        }

    }

    bool VID_HARDWARE::transparencyCheck() const
    {
        return VID::transparencyCheck();
    }

    bool VID_HARDWARE::isLoaded() const
    {

        return m_texturePageCount != 0 && m_textureLayout != nullptr &&
               m_texturePages != nullptr;
    }
}
