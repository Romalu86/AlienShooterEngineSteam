#pragma once
#include "core/types.h"
#include "graphics/rect.h"
#include "graphics/rectangle_vertexes.h"
#include "graphics/surface_transfer.h"
#include <cstddef>
#include <cstdint>
#ifndef _WIN32
#include <vector>
#endif
#ifdef _WIN32
#include "d3d8.h"
#endif

namespace as1
{
    struct BaseTextureCaps
    {
        int maxWidth = 8192;
        int maxHeight = 8192;
        bool requirePowerOfTwo = true;
        bool requireSquare = false;
        bool dynamicTextures = false;
        bool paletteTextures = false;
        DWORD compressedFormatMask = 0;
    };


    struct BaseTextureRuntimeGlobals
    {

        int maxWidth = 8192;
        int maxHeight = 8192;
        DWORD squareOnlyMask = 0;
        DWORD dynamicTextureFlag = 0;
        DWORD powerOfTwoFlag = 2;
        DWORD paletteSupport = 0;
        DWORD compressedFormatMask = 0;
        DWORD nativeTextureBytes = 0;
        DWORD paletteSlotCounter = 0;
    };


    struct BaseTextureCreateAttempt
    {
        DWORD format = 0;
        DWORD usageArg = 0;
        DWORD poolArg = 0;
        bool dynamicPath = false;
    };

    struct BaseTextureCreateState
    {

        bool recorded = false;
        int requestedWidth = 0;
        int requestedHeight = 0;
        DWORD requestedFormat = 0;
        DWORD requestedFlags = 0;

        DWORD flagsAfterDynamicCapClear = 0;
        bool dynamicFlagClearedByCaps = false;
        bool squareAppliedFromCaps = false;
        bool power2AppliedFromCaps = false;
        int normalizedWidth = 0;
        int normalizedHeight = 0;
        bool rejectedByWidth = false;
        bool rejectedByHeight = false;

        bool softwareL8Path = false;
        DWORD softwareAllocationBytes = 0;

        bool nativeCreatePath = false;
        bool nativeCreateSucceeded = false;
        DWORD nativeCreateResult = 0;
        bool usesRenderTargetUsage = false;
        bool usesDynamicTexture = false;
        DWORD nativeUsage = 0;
        DWORD nativePool = 0;
        BaseTextureCreateAttempt attempts[6]{};
        int attemptCount = 0;
        DWORD finalFormat = 0;
        bool failureReported = false;
        DWORD byteCounterIncrement = 0;
    };

    struct BaseTextureReleaseState
    {

        bool recorded = false;
        void* nativeTexture = nullptr;
        DWORD format = 0;
        int width = 0;
        int height = 0;
        DWORD flags = 0;
        bool nativeReleaseRequired = false;
        DWORD byteCounterBefore = 0;
        DWORD byteCounterSubtract = 0;
        DWORD byteCounterAfter = 0;
        bool deletingDestructorFlag = false;
    };

    struct BaseTextureLockState
    {

        bool recorded = false;
        DWORD flags = 0;
        bool softwarePath = false;
        bool nativePath = false;
        bool nativeLockRequired = false;
        bool nativeLockSucceeded = false;
        DWORD nativeLockResult = 0;
        int pitchBytesOut = 0;
        RECTI rect{};
        bool rectProvided = false;
        int softwareOffsetPixels = 0;
        bool resultNonNull = false;
    };

    struct BaseTexturePaletteState
    {

        bool recorded = false;
        void* nativeTexture = nullptr;
        DWORD format = 0;
        DWORD paletteCounterBefore = 0;
        DWORD paletteCounterAfter = 0;
        int paletteSlotStored = -1;
        bool rejectedNotInitialized = false;
        bool rejectedNonP8 = false;
        bool setPaletteEntriesRequired = false;
        bool setPaletteEntriesSucceeded = false;
        DWORD setPaletteEntriesResult = 0;
    };

    struct BaseTextureSurfaceCopyState
    {

        bool recorded = false;
        void* nativeTexture = nullptr;
        void* sourceSurface = nullptr;
        void* destinationSurface = nullptr;
        bool getSurfaceLevelRequired = false;
        bool getSurfaceLevelSucceeded = false;
        DWORD getSurfaceLevelResult = 0;
        bool surfaceCopySubmitted = false;
        bool surfaceCopySucceeded = false;
        DWORD surfaceCopyResult = 0;
        bool destinationSurfaceReleased = false;
        SurfaceTransferState transfer{};
        RECTI sourceRect{};
        RECTI destinationOrigin{};
        RECTI shiftedDestinationRect{};
        bool sourceRectProvided = false;
    };

    struct BaseTextureFixedDepthRectangleDrawState
    {

        bool recorded = false;
        void* nativeTexture = nullptr;
        DWORD format = 0;
        int width = 0;
        int height = 0;
        int paletteSlot = -1;
        RECTI destination{};
        RECTI source{};
        DWORD color0 = 0;
        DWORD color1 = 0;
        DWORD diffuseAfterNot = 0;
        DWORD specular = 0;
        bool requiresSetTexture = false;
        bool drawSubmitted = false;
        DWORD drawResult = 0;
        bool requiresSetCurrentTexturePalette = false;
        bool alphaGateFromSecondColorDword = false;
        bool sourceAndDestinationSizeMatch = false;
        DWORD alphaBlendState = 0;
        DWORD minFilterState = 0;
        DWORD magFilterState = 0;
        DWORD vertexShaderFvf = 0x1C4u;
        DWORD primitiveType = 6u;
        DWORD primitiveCount = 2u;
        DWORD vertexStride = 0x20u;
        DWORD fixedZRaw = 0x3F7FFFFEu;
        RECTANGLE_VERTEXES_D3D vertices{};
    };

    struct BaseTextureDepthRectangleDrawState
    {

        bool recorded = false;
        void* nativeTexture = nullptr;
        DWORD format = 0;
        int width = 0;
        int height = 0;
        int paletteSlot = -1;

        DWORD zTop = 0;
        DWORD zBottom = 0;
        RECTI destination{};
        RECTI source{};
        DWORD color0 = 0;
        DWORD color1 = 0;
        DWORD diffuseAfterNot = 0;
        DWORD specular = 0;

        bool requiresSetTexture = false;
        bool drawSubmitted = false;
        DWORD drawResult = 0;
        bool requiresSetCurrentTexturePalette = false;
        bool alphaGateFromSecondColorDword = false;
        bool sourceAndDestinationSizeMatch = false;

        DWORD alphaBlendState = 0;
        DWORD minFilterState = 0;
        DWORD magFilterState = 0;
        DWORD vertexShaderFvf = 0x1C4u;
        DWORD primitiveType = 6u;
        DWORD primitiveCount = 2u;
        DWORD vertexStride = 0x20u;
        RECTANGLE_VERTEXES_D3D vertices{};
    };

    class BASE_TEXTURE
    {
    public:
        BASE_TEXTURE(int width, int height, DWORD format, DWORD flags);
        virtual ~BASE_TEXTURE();

        BASE_TEXTURE(const BASE_TEXTURE&) = delete;
        BASE_TEXTURE& operator=(const BASE_TEXTURE&) = delete;

#ifndef _WIN32
        bool create(int width, int height, DWORD format, DWORD flags);
        void release();
#endif

        static void ConfigureCaps(const BaseTextureCaps& caps);
        static const BaseTextureCaps& Caps();
        static const BaseTextureRuntimeGlobals& RuntimeGlobals();
        static DWORD FallbackFormatAfterCreateFailure(DWORD format, DWORD flags = 0u);
        static DWORD NativeTextureBytesAfterCreate(int width, int height, DWORD format, DWORD currentBytes);
        static DWORD NativeTextureBytesAfterRelease(int width, int height, DWORD format, DWORD currentBytes);
        static BaseTextureCreateState MakeCreateState(int width, int height, DWORD format, DWORD flags);

        bool isSoftwareBacked() const { return (m_flags & 0x2u) != 0; }
#ifdef _WIN32
        bool isLoaded() const { return isSoftwareBacked() ? m_softwarePixels != nullptr : m_nativeHandle != nullptr; }
#else
        bool isLoaded() const { return isSoftwareBacked() ? !m_pixels16.empty() : m_nativeHandle != nullptr; }
#endif
        int width() const { return m_width; }
        int height() const { return m_height; }
        int pitchWords() const { return m_width; }
        DWORD format() const { return m_format; }
        DWORD flags() const { return m_flags; }

        std::uint16_t* lock16(int* pitchBytes, const RECTI* rect = nullptr);
        const std::uint16_t* lock16(int* pitchBytes, const RECTI* rect = nullptr) const;
        void unlock();

        void* nativeHandle() const { return m_nativeHandle; }
        int paletteSlot() const { return m_paletteSlot; }
        std::intptr_t uploadPaletteEntries(const void* paletteEntries);
        void createPaletteSlot(const void* paletteEntries);

        static constexpr std::size_t RetailObjectSize = 0x20u;
#ifdef _WIN32

        int PrepareSurfaceCopy(IDirect3DSurface8* sourceSurface, const RECTI& sourceRect, const RECTI* destinationOrigin);
        void DrawFixedDepthRectangle(const RECTI& destination, const RECTI& source, const DWORD* colors);
        void DrawDepthRectangle(DWORD zTop, DWORD zBottom, const RECTI& destination, const RECTI& source, const DWORD* colors);
#else

        const BaseTextureCreateState& createState() const { return m_createState; }
        const BaseTextureReleaseState& releaseState() const { return m_releaseState; }
        const BaseTextureLockState& lockState() const { return m_lockState; }
        const BaseTexturePaletteState& paletteState() const { return m_paletteState; }
        const BaseTextureSurfaceCopyState& surfaceCopyState() const { return m_surfaceCopyState; }
        const BaseTextureFixedDepthRectangleDrawState& fixedDepthRectangleDrawState() const { return m_fixedDepthRectangleDrawState; }
        const BaseTextureDepthRectangleDrawState& depthRectangleDrawState() const { return m_depthRectangleDrawState; }
        const BaseTextureSurfaceCopyState& PrepareSurfaceCopy(void* sourceSurface, const RECTI& sourceRect, const RECTI* destinationOrigin);
        const BaseTextureSurfaceCopyState& PrepareSurfaceCopy(const RECTI& destination, const RECTI& source, const RECTI* sourceBase);
        const BaseTextureFixedDepthRectangleDrawState& DrawFixedDepthRectangle(const RECTI& destination, const RECTI& source, DWORD color0, DWORD color1);
        const BaseTextureDepthRectangleDrawState& DrawDepthRectangle(DWORD zTop, DWORD zBottom, const RECTI& destination, const RECTI& source, DWORD color0, DWORD color1);
#endif

    private:
#ifdef _WIN32
        void* m_softwarePixels;
        IDirect3DTexture8* m_nativeHandle;
        DWORD m_format;
        int m_width;
        int m_height;
        int m_paletteSlot;
        DWORD m_flags;
#else
        void* m_softwarePixels = nullptr;
        void* m_nativeHandle = nullptr;
        DWORD m_format = 0;
        int m_width = 0;
        int m_height = 0;
        int m_paletteSlot = -1;
        DWORD m_flags = 0;
        BaseTextureCreateState m_createState{};
        BaseTextureReleaseState m_releaseState{};
        BaseTextureLockState m_lockState{};
        BaseTexturePaletteState m_paletteState{};
        BaseTextureSurfaceCopyState m_surfaceCopyState{};
        BaseTextureFixedDepthRectangleDrawState m_fixedDepthRectangleDrawState{};
        BaseTextureDepthRectangleDrawState m_depthRectangleDrawState{};
        std::vector<std::uint16_t> m_pixels16;
#endif
    };
#if defined(_M_IX86)
                                                                                                                         
#endif
}
