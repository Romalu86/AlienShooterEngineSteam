#include "vid/vid_surface.h"
#include "core/resource.h"
#include "core/application.h"
#include "graphics/gamma.h"
#include "core/log.h"
#include "graph.h"
#include "sprite.h"
#ifdef _WIN32
#include "d3d8.h"
#endif
#include <array>
#include <vector>
#include <new>
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace as1
{
    namespace
    {
        WORD paletteDwordToR5G6B5(DWORD value)
        {

            return static_cast<WORD>(((value >> 8u) & 0xF800u) |
                                     ((value >> 5u) & 0x07E0u) |
                                     ((value >> 3u) & 0x001Fu));
        }

        unsigned surfaceFormatBitsPerPixel(DWORD format)
        {
            switch (format)
            {
            case 20u: return 24u;
            case 21u:
            case 22u: return 32u;
            case 23u:
            case 24u:
            case 25u:
            case 26u: return 16u;
            case 41u: return 8u;
            case 0x31545844u: return 4u;  // DXT1
            case 0x33545844u:             // DXT3
            case 0x35545844u: return 8u;  // DXT5
            default: return 0u;
            }
        }

        bool isRetailDxtPitchFormat(DWORD format) noexcept
        {
            return format == 0x31545844u ||
                   format == 0x33545844u ||
                   format == 0x35545844u;
        }

        std::uint32_t retailFramePointerTableBytes(std::int32_t frameCount) noexcept
        {
            // The game logic: signed extension frameCount; unsigned 32-bit multiplication by 4;
            // overflow detection saturates any 32-bit overflow to 0xFFFFFFFF.
            const std::uint64_t product =
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(frameCount)) * 4u;
            return product > 0xFFFFFFFFull
                ? 0xFFFFFFFFu
                : static_cast<std::uint32_t>(product);
        }

        float interpolateSurfaceEffectCurve(const VID_SURFACE* owner,
                                            float position,
                                            int baseOffset) noexcept
        {
            int segment = 0;
            if (!std::isfinite(position) ||
                position < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                position >= 2147483648.0f)
            {
                segment = std::numeric_limits<std::int32_t>::min();
            }
            else
            {
                segment = static_cast<int>(std::trunc(position));
            }
            if (segment >= 7)
                return owner->weaponFloatAt(baseOffset + 7 * 4);

            const float first = owner->weaponFloatAt(baseOffset + segment * 4);
            const float second = owner->weaponFloatAt(baseOffset + (segment + 1) * 4);
            return (second - first) * (position - static_cast<float>(segment)) + first;
        }

        int truncateSurfaceFloatToInt32(float value) noexcept
        {
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                value >= 2147483648.0f)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<int>(std::trunc(value));
        }

        float multiplySurfaceFloat(float lhs, float rhs) noexcept
        {
            return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(lhs), _mm_set_ss(rhs)));
        }


        template <class T>
        void deleteThroughVirtualDestructor(T* owner) noexcept
        {
            if (!owner)
                return;
            delete owner;
        }

        template <class T>
        void releaseSurfacePointerTable(T**& table, int frameCount) noexcept
        {
            if (!table)
                return;

            if (frameCount > 0)
            {
                for (int earlier = 0; earlier < frameCount; ++earlier)
                {
                    for (int later = earlier + 1; later < frameCount; ++later)
                    {
                        if (table[earlier] == table[later])
                            table[later] = nullptr;
                    }
                }

                for (int frame = 0; frame < frameCount; ++frame)
                {
                    deleteThroughVirtualDestructor(table[frame]);
                }
            }

            ::operator delete(table);
            table = nullptr;
        }


    }

    VID_SURFACE::VID_SURFACE() = default;

    VID_SURFACE::VID_SURFACE(const VID_SURFACE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_SURFACE&>(other).nextMirror = this;

        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));

        m_surfaceTextureOwners = other.m_surfaceTextureOwners;
        m_surfaceTexcoordOwners = other.m_surfaceTexcoordOwners;
        m_surfaceSourceFormat = other.m_surfaceSourceFormat;
    }

    VID_SURFACE* VID_SURFACE::CreateMirror()
    {

        return new (std::nothrow) VID_SURFACE(*this);
    }

    VID_SURFACE::~VID_SURFACE()
    {
        ReleaseSurfaceOwnerSlots();
    }

    VID_SURFACE* vidSurfaceScalarDeletingDestructor(VID_SURFACE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_SURFACE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    void VID_SURFACE::ReleaseSurfaceOwnerSlots() const noexcept
    {

        if (!isMirrorChainOwner())
            return;

        const int frameCount = static_cast<int>(static_cast<short>(totalFrames()));
        releaseSurfacePointerTable(m_surfaceTextureOwners, frameCount);
        releaseSurfacePointerTable(m_surfaceTexcoordOwners, frameCount);
    }


    void VID_SURFACE::SetLayer()
    {
        layer = 8;
    }


    void VID_SURFACE::Load(RESOURCE* globalRes)
    {
        // The game logic dereferences the RESOURCE argument directly.
        globalRes->read(&m_surfaceSourceFormat, sizeof(m_surfaceSourceFormat));

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(totalFrames()));
        const std::uint32_t tableBytes32 = retailFramePointerTableBytes(frameCount);
        const std::size_t tableBytes = static_cast<std::size_t>(tableBytes32);
        m_surfaceTextureOwners = static_cast<BASE_TEXTURE**>(::operator new(tableBytes));
        m_surfaceTexcoordOwners = static_cast<VID_TEXCOOR**>(::operator new(tableBytes));

        std::array<DWORD, 256> paletteDwords{};
        bool havePalette = false;
        if ((formatFlags() & VID_TYPE_PALETTE) != 0)
        {
            if (globalRes->GoNext(RESOURCE::ResTypes::PALETTE) != 0)
                ReportResourceError(5, "PAL ", 0);
            else
            {
                globalRes->read(paletteDwords.data(), static_cast<unsigned>(sizeof(paletteDwords)));
                havePalette = true;
            }
        }

        if (globalRes->GoNext(RESOURCE::ResTypes::DATA) != 0)
            ReportResourceError(5, "DATA", 0);

        if (frameCount <= 0)
            return;

#ifdef _WIN32
        std::array<PALETTEENTRY, 256> sourcePalette{};
        if (havePalette)
        {
            const BYTE* raw = reinterpret_cast<const BYTE*>(paletteDwords.data());
            for (std::size_t i = 0; i < sourcePalette.size(); ++i)
            {
                sourcePalette[i].peRed   = raw[i * 4u + 2u];
                sourcePalette[i].peGreen = raw[i * 4u + 1u];
                sourcePalette[i].peBlue  = raw[i * 4u + 0u];
                sourcePalette[i].peFlags = raw[i * 4u + 3u];
            }
        }
#endif

        const unsigned sourceBits = surfaceFormatBitsPerPixel(m_surfaceSourceFormat);
        for (int frame = 0; frame < frameCount; ++frame)
        {
            WORD textureWidth = 0;
            WORD textureHeight = 0;
            globalRes->read(&textureWidth, sizeof(textureWidth));
            globalRes->read(&textureHeight, sizeof(textureHeight));

            const DWORD textureFlags =
                static_cast<DWORD>(((formatFlags() & 2u) | 4u) << 2u);
            BASE_TEXTURE* texture = new (std::nothrow) BASE_TEXTURE(
                static_cast<int>(textureWidth),
                static_cast<int>(textureHeight),
                m_surfaceSourceFormat,
                textureFlags);
            m_surfaceTextureOwners[frame] = texture;

            const std::size_t payloadBytes =
                (static_cast<std::size_t>(textureWidth) *
                 static_cast<std::size_t>(textureHeight) *
                 static_cast<std::size_t>(sourceBits)) / 8u;
            std::vector<BYTE> payload(payloadBytes);
            if (!payload.empty())
                globalRes->read(payload.data(), static_cast<unsigned>(payload.size()));

#ifdef _WIN32
            if (texture && texture->format() == 41u && havePalette)
                texture->createPaletteSlot(sourcePalette.data());

            if (texture && texture->nativeHandle() && sourceBits != 0u && !payload.empty())
            {
                IDirect3DSurface8* destinationSurface = nullptr;
                IDirect3DTexture8* const nativeTexture =
                    static_cast<IDirect3DTexture8*>(texture->nativeHandle());
                if (nativeTexture->GetSurfaceLevel(0u, &destinationSurface) == D3D_OK && destinationSurface)
                {
                    RECTI sourceRect{0, 0, static_cast<int>(textureWidth), static_cast<int>(textureHeight)};
                    unsigned sourcePitch = static_cast<unsigned>(textureWidth) * sourceBits;
                    sourcePitch >>= isRetailDxtPitchFormat(m_surfaceSourceFormat) ? 1u : 3u;

                    const PALETTEENTRY* palette =
                        (m_surfaceSourceFormat == 41u && havePalette) ? sourcePalette.data() : nullptr;
                    const D3DCOLOR colorKey = (texture->flags() & 8u) != 0u ? 0u : 0xFF000000u;
                    (void)loadSurfaceFromMemoryRetail(
                        destinationSurface,
                        nullptr,
                        nullptr,
                        payload.data(),
                        static_cast<D3DFORMAT>(m_surfaceSourceFormat),
                        sourcePitch,
                        palette,
                        &sourceRect,
                        1u,
                        colorKey);
                    destinationSurface->Release();
                }
            }
#else
            (void)havePalette;
#endif

            DWORD vertexCount = 0;
            DWORD indexCount = 0;
            globalRes->read(&vertexCount, sizeof(vertexCount));
            globalRes->read(&indexCount, sizeof(indexCount));

            VID_TEXCOOR* texcoor = new (std::nothrow) VID_TEXCOOR(
                static_cast<int>(vertexCount),
                static_cast<int>(indexCount));
            m_surfaceTexcoordOwners[frame] = texcoor;

            // The game logic immediately calls through the newly-created
            // VID_TEXCOOR and its lock result. The normal path
            // added OOM/lock-null stream-preservation fallbacks that are not in retail.
            VID_TEXCOOR_VERTEX* dstVertex = texcoor->lockVertexBuffer();
            for (int vertex = 0; vertex < static_cast<int>(vertexCount); ++vertex)
            {
                std::int16_t screenX = 0;
                std::int16_t screenY = 0;
                std::int16_t depthCode = 0;
                std::int16_t texU = 0;
                std::int16_t texV = 0;
                globalRes->read(&screenX, sizeof(screenX));
                globalRes->read(&screenY, sizeof(screenY));
                globalRes->read(&depthCode, sizeof(depthCode));
                globalRes->read(&texU, sizeof(texU));
                globalRes->read(&texV, sizeof(texV));

                dstVertex[vertex].x = static_cast<float>(screenX);
                // this code path performs both single-precision multiplication operations explicitly.
                float projectedY = multiplySurfaceFloat(static_cast<float>(screenY), 5793.0f);
                projectedY = multiplySurfaceFloat(projectedY, 1.0f / 4096.0f);
                dstVertex[vertex].y = projectedY;
                dstVertex[vertex].z =
                    static_cast<float>(static_cast<int>(depthCode) - 1024) * 0.125f;
                // Retail performs single-precision division even for a zero texture dimension; it does
                // not replace the result with a synthetic zero.
                dstVertex[vertex].u =
                    (static_cast<float>(texU) + 0.5f) / static_cast<float>(texture->width());
                dstVertex[vertex].v =
                    (static_cast<float>(texV) + 0.5f) / static_cast<float>(texture->height());
            }
            texcoor->unlockVertexBuffer();

            if (indexCount != 0u)
            {
                WORD* dstIndex = texcoor->lockIndexBuffer();
                globalRes->read(dstIndex, static_cast<unsigned>(indexCount * sizeof(WORD)));
                texcoor->unlockIndexBuffer();
            }

            globalRes->GoNextSub(RESOURCE::ResTypes::DATA);
        }
    }

    void VID_SURFACE::Draw(const SPRITE* sprite)
    {
        const DWORD auxFlags = runtimeAuxFlags();
        if ((auxFlags & 0x40u) != 0u)
            return;

        GRAPH* const graph = GRAPH::CurrentGraph();
        const WORD flags = formatFlags();
        const DWORD propertyFlags = properties();

        if ((propertyFlags & P_ALWAYSTOP) == 0u &&
            (flags & VID_TYPE_ZBUFFER) == 0u)
        {
            const auto* const appOwner = static_cast<const std::uint8_t*>(
                core::ApplicationPhysicalOwner());
            const float cameraShiftX = *reinterpret_cast<const float*>(
                appOwner + core::retail_application_layout::CameraShiftX);
            const float cameraShiftY = *reinterpret_cast<const float*>(
                appOwner + core::retail_application_layout::CameraShiftY);
            const float projectedX = sprite->X() - cameraShiftX;
            const float projectedY = (sprite->Y() - sprite->Z()) - cameraShiftY;

            if (!(projectedX >= graph->viewportLeft() && projectedX < graph->viewportRight() &&
                  projectedY >= graph->viewportTop() && projectedY < graph->viewportBottom()))
            {
                return;
            }

            const int screenX = truncateSurfaceFloatToInt32(projectedX);
            const int screenY = truncateSurfaceFloatToInt32(projectedY);
            const int zInt = truncateSurfaceFloatToInt32(sprite->Z());
            const WORD* const depth = graph->softwareDepthBuffer();
            const int pitch = graph->softwareDepthPitch();
            const int depthLimit = static_cast<int>(
                static_cast<std::uint32_t>(zInt) * 8u + 0x400u);
            if (depth && static_cast<int>(depth[screenX + screenY * pitch]) > depthLimit)
                return;
        }

        float scaleX = scaleXYZ.x;
        float scaleY = scaleXYZ.y;
        float scaleZ = scaleXYZ.z;
        float worldX = sprite->X();
        float worldY = sprite->Y();
        float worldZ = sprite->Z();

        if ((auxFlags & 0x02u) != 0u)
        {
            const float position = sprite->actionAuxEffectCurvePosition();
            scaleX *= interpolateSurfaceEffectCurve(this, position, 0x0E4);
            scaleY *= interpolateSurfaceEffectCurve(this, position, 0x104);
            scaleZ *= interpolateSurfaceEffectCurve(this, position, 0x124);
        }
        if ((auxFlags & 0x04u) != 0u)
        {
            const float position = sprite->actionAuxEffectCurvePosition();
            worldX += interpolateSurfaceEffectCurve(this, position, 0x144);
            worldY += interpolateSurfaceEffectCurve(this, position, 0x164);
            worldZ += interpolateSurfaceEffectCurve(this, position, 0x184);
        }

#ifdef _WIN32
        D3DMATRIX world{};
        world.m[0][0] = scaleX;
        world.m[1][1] = scaleY;
        world.m[2][2] = scaleZ;
        world.m[3][0] = worldX;
        world.m[3][1] = worldY;
        world.m[3][2] = worldZ;
        world.m[3][3] = 1.0f;

        IDirect3DDevice8* const transformDevice =
            static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        const HRESULT transformResult =
            transformDevice->SetTransform(D3DTS_WORLD, &world);
        if (FAILED(transformResult))
        {
            LOG::ResourceError(
                "VID [%i-%s]", 8, "Transform world",
                static_cast<int>(transformResult), nvid(), name.c_str());
        }
#endif

        GammaRawPair drawGamma{};
        sprite->buildRetailGammaPair(drawGamma);
        if ((propertyFlags & P_GAMMA) == 0u)
        {
            drawGamma.setSaturatingAddRetail(drawGamma, graph->rawGammaPair());
        }

        const bool useTextureFactor = drawGamma.first != 0u || drawGamma.second != 0u;
#ifdef _WIN32
        IDirect3DDevice8* const gammaDevice =
            static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        if (useTextureFactor)
        {
            gammaDevice->SetTextureStageState(0u, D3DTSS_COLORARG2, D3DTA_TFACTOR);
            gammaDevice->SetTextureStageState(0u, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
            graph->setRenderStateCached(D3DRS_TEXTUREFACTOR, ~drawGamma.first);
        }
#endif

        if ((flags & VID_TYPE_ALPHA) != 0u)
            graph->setAlphaBlendFactors(5u, 6u);
        else
            graph->setRenderStateCached(0x1Bu, 0u);

        const int frame = sprite->currentFrame();
        BASE_TEXTURE* const texture = m_surfaceTextureOwners[frame];
#ifdef _WIN32
        static_cast<IDirect3DDevice8*>(graph->deviceHandle())->SetTexture(
            0u,
            static_cast<IDirect3DTexture8*>(texture->nativeHandle()));
#else
        (void)texture;
#endif
        m_surfaceTexcoordOwners[frame]->drawTexcoorMesh(*sprite);

#ifdef _WIN32
        if (useTextureFactor)
        {
            IDirect3DDevice8* const resetDevice =
                static_cast<IDirect3DDevice8*>(graph->deviceHandle());
            resetDevice->SetTextureStageState(0u, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            resetDevice->SetTextureStageState(0u, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        }
#endif
    }

    bool VID_SURFACE::transparencyCheck() const
    {
        return true;
    }

    bool VID_SURFACE::isLoaded() const
    {

        return m_surfaceTextureOwners != nullptr && m_surfaceTexcoordOwners != nullptr;
    }
}
