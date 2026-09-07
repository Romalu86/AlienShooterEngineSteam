#pragma once

#include "core/types.h"
#include "graphics/rect.h"

#ifdef _WIN32
#include "d3d8.h"
#endif

namespace as1
{
    struct SurfaceTransferState
    {

        bool recorded = false;
        bool rejectedInvalidArgument = false;
        bool rejectedSourceRect = false;
        bool rejectedDestinationRect = false;
        bool sourceDescRead = false;
        bool destinationDescRead = false;
        DWORD sourceFormat = 0;
        DWORD destinationFormat = 0;
        DWORD sourceWidth = 0;
        DWORD sourceHeight = 0;
        DWORD destinationWidth = 0;
        DWORD destinationHeight = 0;
        RECTI sourceRect{};
        RECTI destinationRect{};
        DWORD filterFlags = 0xFFFFFFFFu;
        DWORD normalizedFilterFlags = 0;
        DWORD colorKey = 0;
        bool sourceWholeSurfaceLock = false;
        bool destinationWholeSurfaceLock = false;
        bool sourceLockAttempted = false;
        bool sourceLockSucceeded = false;
        DWORD sourceLockResult = 0;
        bool destinationLockAttempted = false;
        bool destinationLockSucceeded = false;
        DWORD destinationLockResult = 0;
        bool destinationDescriptorCreated = false;
        bool sourceDescriptorCreated = false;
        DWORD destinationDescriptorCategory = 0;
        DWORD sourceDescriptorCategory = 0;
        DWORD destinationDescriptorObjectSize = 0;
        DWORD sourceDescriptorObjectSize = 0;
        DWORD destinationDescriptorVtable = 0;
        DWORD sourceDescriptorVtable = 0;
        DWORD destinationDescriptorConstructor = 0;
        DWORD sourceDescriptorConstructor = 0;
        bool descriptorCategoryMismatch = false;
        DWORD converterAttemptMask = 0;
        int converterSelectedIndex = -1;
        DWORD converterSelectedAddress = 0;
        int descriptorReleaseCount = 0;
        DWORD firstReleasedDescriptor = 0;
        DWORD secondReleasedDescriptor = 0;
        bool directCopyRoute = false;
        bool directCompressedCopyRoute = false;
        bool sameSizeConverterRoute = false;
        bool filterRoute = false;
        DWORD filterType = 0;
        bool formatConversionRequired = false;
        bool unsupportedSourceFormat = false;
        bool unsupportedDestinationFormat = false;
        bool sourceUnlocked = false;
        bool destinationUnlocked = false;
        int descriptorReadRowCalls = 0;
        int descriptorWriteRowCalls = 0;
        int zeroFillRows = 0;
        int pointSourceRowReloads = 0;
        bool boxOptimizedAttempted = false;
        bool boxOptimizedRoute = false;
        bool boxGenericRoute = false;
        DWORD boxOptimizedHelperAddress = 0;
        int copiedRows = 0;
        int copiedPixels = 0;
        DWORD result = 0;
    };

#ifdef _WIN32

    HRESULT __stdcall loadSurfaceFromMemoryRetail(IDirect3DSurface8* destinationSurface,
                                 const PALETTEENTRY* destinationPalette,
                                 const RECTI* destinationRect,
                                 const void* sourceMemory,
                                 D3DFORMAT sourceFormat,
                                 UINT sourcePitch,
                                 const PALETTEENTRY* sourcePalette,
                                 const RECTI* sourceRect,
                                 DWORD filterFlags,
                                 D3DCOLOR colorKey);

    HRESULT __stdcall loadSurfaceFromSurfaceRetail(IDirect3DSurface8* destinationSurface,
                                 const PALETTEENTRY* destinationPalette,
                                 const RECTI* destinationRect,
                                 IDirect3DSurface8* sourceSurface,
                                 const PALETTEENTRY* sourcePalette,
                                 const RECTI* sourceRect,
                                 DWORD filterFlags,
                                 D3DCOLOR colorKey);

    SurfaceTransferState CopySurfaceRegion(IDirect3DDevice8* unusedDevice,
                                           IDirect3DSurface8* destinationSurface,
                                           IDirect3DSurface8* sourceSurface,
                                           const RECTI& sourceRect,
                                           const RECTI& destinationRect,
                                           DWORD filterFlags = 0xFFFFFFFFu);
#else
    SurfaceTransferState CopySurfaceRegion(void* unusedDevice,
                                           void* destinationSurface,
                                           void* sourceSurface,
                                           const RECTI& sourceRect,
                                           const RECTI& destinationRect,
                                           DWORD filterFlags = 0xFFFFFFFFu);
#endif
}
