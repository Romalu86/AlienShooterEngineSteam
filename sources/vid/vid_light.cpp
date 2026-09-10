#include "vid/vid_light.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>

#include <new>

#ifdef _WIN32
#include "d3d8.h"
#endif

namespace as1
{
    namespace
    {
        int vidConvertFloatToInt32Light(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return _mm_cvtt_ss2si(_mm_set_ss(value));
#else
            if (std::isnan(value) || value >= 2147483648.0f || value < -2147483648.0f)
                return std::numeric_limits<int>::min();
            return static_cast<int>(value);
#endif
        }
    }
    float spriteCameraRelativeX(const SPRITE* sprite) noexcept
    {
        return sprite->X() - core::GlobalApplicationDrawDispatcherState().cameraShiftX();
    }

    float spriteCameraRelativeProjectedY(const SPRITE* sprite) noexcept
    {
        return (sprite->Y() - sprite->Z()) -
               core::GlobalApplicationDrawDispatcherState().cameraShiftY();
    }

    float spriteWorldZ(const SPRITE* sprite) noexcept
    {
        return sprite->Z();
    }

    VID_LIGHT::VID_LIGHT(const VID_LIGHT& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_LIGHT&>(other).nextMirror = this;
        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));
        m_lightData = other.m_lightData;
        m_lightDataBytes = other.m_lightDataBytes;
    }

    VID_LIGHT::~VID_LIGHT()
    {

        if (isMirrorChainOwner())
        {
            if (m_lightData)
                ::operator delete(m_lightData);
            m_lightData = nullptr;
            g_vidAllocatedBytes -= static_cast<int>(m_lightDataBytes);
            m_lightDataBytes = 0;
        }
    }

    VID_LIGHT* vidLightScalarDeletingDestructor(VID_LIGHT* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_LIGHT();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    VID_LIGHT* VID_LIGHT::CreateMirror()
    {

        return new (std::nothrow) VID_LIGHT(*this);
    }

    void VID_LIGHT::SetLayer()
    {
        layer = 11;
    }

    std::intptr_t loadVidLightData(VID_LIGHT* owner, RESOURCE* resource) noexcept
    {

        if (resource->GoNext(RESOURCE::ResTypes::DATA) != 0)
            (void)owner->logVidResourceError(5, "DATA", 0);

        owner->setVidWidth(static_cast<short>(vidConvertFloatToInt32Light(owner->sizeX())));
        owner->setVidHeight(static_cast<short>(vidConvertFloatToInt32Light(owner->sizeY())));

        void* rawData = owner->lightData();
        const DWORD byteCount = static_cast<DWORD>(resource->SubLoad(&rawData, nullptr));
        owner->setLightData(static_cast<BYTE*>(rawData));
        owner->setLightDataBytes(byteCount);
        if (byteCount == 0u)
            (void)owner->logVidResourceError(5, "cadr", 0);

        g_vidAllocatedBytes = static_cast<int>(
            static_cast<std::uint32_t>(g_vidAllocatedBytes) + static_cast<std::uint32_t>(byteCount));
        return static_cast<std::intptr_t>(g_vidAllocatedBytes);
    }

    void VID_LIGHT::Load(RESOURCE* resource)
    {
        (void)loadVidLightData(this, resource);
    }

    void VID_LIGHT::Draw(const SPRITE* sprite)
    {

        DWORD color = reinterpret_cast<const DWORD*>(m_lightData)
            [static_cast<std::size_t>(sprite->currentFrame())];

        if ((runtimeAuxFlags() & 0x40u) != 0u ||
            color == 0u || color == 0xFF000000u)
        {
            return;
        }

#ifdef _WIN32
        IDirect3DDevice8* const device = static_cast<IDirect3DDevice8*>(GRAPH::CurrentDevice());
        if ((properties() & P_DBLLIGHT) != 0u)
            device->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
#endif

        GammaRawPair selectedGamma{};
        sprite->buildRetailGammaPair(selectedGamma);

        GammaRawPair drawGamma{};
        drawGamma.setSaturatingAddRetail(gammaRaw, selectedGamma);
        if ((properties() & P_GAMMA) == 0u)
        {
            if (const GammaRawPair* graphGamma = GRAPH::CurrentRawGammaPair())
                drawGamma.setSaturatingAddRetail(drawGamma, *graphGamma);
        }
        color = GammaRawBlend(drawGamma, color);

        const VECTOR lightPosition{
            spriteCameraRelativeX(sprite),
            spriteCameraRelativeProjectedY(sprite),
            sprite->Z()
        };

        GRAPH::CurrentGraph()->DrawLightSource(
            lightPosition.x,
            lightPosition.y,
            lightPosition.z,
            sizeX(),
            sizeY(),
            color);

#ifdef _WIN32
        if ((properties() & P_DBLLIGHT) != 0u)
            device->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE);
#endif
    }

    bool VID_LIGHT::transparencyCheck() const
    {
        return true;
    }

    bool VID_LIGHT::isLoaded() const
    {
        // loadVidLightData stores the physical light-data allocation on the VID owner.
        return m_lightData != nullptr;
    }
}
