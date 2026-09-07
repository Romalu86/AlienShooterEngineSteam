#include "graphics/surface_transfer.h"

#include <cstdint>

namespace as1
{
#ifdef _WIN32
    extern "C"
    {
        __declspec(dllimport) HRESULT WINAPI D3DXLoadSurfaceFromMemory(
            IDirect3DSurface9* destinationSurface,
            const PALETTEENTRY* destinationPalette,
            const RECT* destinationRect,
            const void* sourceMemory,
            D3DFORMAT sourceFormat,
            UINT sourcePitch,
            const PALETTEENTRY* sourcePalette,
            const RECT* sourceRect,
            DWORD filterFlags,
            D3DCOLOR colorKey);

        __declspec(dllimport) HRESULT WINAPI D3DXLoadSurfaceFromSurface(
            IDirect3DSurface9* destinationSurface,
            const PALETTEENTRY* destinationPalette,
            const RECT* destinationRect,
            IDirect3DSurface9* sourceSurface,
            const PALETTEENTRY* sourcePalette,
            const RECT* sourceRect,
            DWORD filterFlags,
            D3DCOLOR colorKey);
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    // The generated import library names the real DLL exports without stdcall
    // decoration.  The x86 compiler references decorated __imp__ symbols;
    // alias them to the undecorated import pointers while preserving WINAPI
    // calling convention at the call site.
#pragma comment(linker, "/alternatename:__imp__D3DXLoadSurfaceFromMemory@40=__imp__D3DXLoadSurfaceFromMemory")
#pragma comment(linker, "/alternatename:__imp__D3DXLoadSurfaceFromSurface@32=__imp__D3DXLoadSurfaceFromSurface")
#endif

                                                                                

    HRESULT __stdcall loadSurfaceFromMemoryRetail(
        IDirect3DSurface8* destinationSurface,
        const PALETTEENTRY* destinationPalette,
        const RECTI* destinationRect,
        const void* sourceMemory,
        D3DFORMAT sourceFormat,
        UINT sourcePitch,
        const PALETTEENTRY* sourcePalette,
        const RECTI* sourceRect,
        DWORD filterFlags,
        D3DCOLOR colorKey)
    {
        return D3DXLoadSurfaceFromMemory(
            destinationSurface,
            destinationPalette,
            reinterpret_cast<const RECT*>(destinationRect),
            sourceMemory,
            sourceFormat,
            sourcePitch,
            sourcePalette,
            reinterpret_cast<const RECT*>(sourceRect),
            filterFlags,
            colorKey);
    }

    HRESULT __stdcall loadSurfaceFromSurfaceRetail(
        IDirect3DSurface8* destinationSurface,
        const PALETTEENTRY* destinationPalette,
        const RECTI* destinationRect,
        IDirect3DSurface8* sourceSurface,
        const PALETTEENTRY* sourcePalette,
        const RECTI* sourceRect,
        DWORD filterFlags,
        D3DCOLOR colorKey)
    {
        return D3DXLoadSurfaceFromSurface(
            destinationSurface,
            destinationPalette,
            reinterpret_cast<const RECT*>(destinationRect),
            sourceSurface,
            sourcePalette,
            reinterpret_cast<const RECT*>(sourceRect),
            filterFlags,
            colorKey);
    }

    SurfaceTransferState CopySurfaceRegion(
        IDirect3DDevice8*,
        IDirect3DSurface8* destinationSurface,
        IDirect3DSurface8* sourceSurface,
        const RECTI& sourceRect,
        const RECTI& destinationRect,
        DWORD filterFlags)
    {
        SurfaceTransferState state{};
        state.recorded = true;
        state.sourceRect = sourceRect;
        state.destinationRect = destinationRect;
        state.filterFlags = filterFlags;
        state.result = static_cast<DWORD>(loadSurfaceFromSurfaceRetail(
            destinationSurface,
            nullptr,
            &destinationRect,
            sourceSurface,
            nullptr,
            &sourceRect,
            filterFlags,
            0u));
        return state;
    }
#else
    namespace
    {
        constexpr DWORD kInvalidCall = 0x8876086Cu;
    }

    SurfaceTransferState CopySurfaceRegion(
        void*,
        void* destinationSurface,
        void* sourceSurface,
        const RECTI& sourceRect,
        const RECTI& destinationRect,
        DWORD filterFlags)
    {
        SurfaceTransferState state{};
        state.recorded = true;
        state.sourceRect = sourceRect;
        state.destinationRect = destinationRect;
        state.filterFlags = filterFlags;
        state.rejectedInvalidArgument = destinationSurface == nullptr || sourceSurface == nullptr;
        state.result = kInvalidCall;
        return state;
    }
#endif
}
