#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>

using IDirect3D8 = IDirect3D9;
using IDirect3DDevice8 = IDirect3DDevice9;
using IDirect3DResource8 = IDirect3DResource9;
using IDirect3DBaseTexture8 = IDirect3DBaseTexture9;
using IDirect3DTexture8 = IDirect3DTexture9;
using IDirect3DCubeTexture8 = IDirect3DCubeTexture9;
using IDirect3DVolumeTexture8 = IDirect3DVolumeTexture9;
using IDirect3DVertexBuffer8 = IDirect3DVertexBuffer9;
using IDirect3DIndexBuffer8 = IDirect3DIndexBuffer9;
using IDirect3DSurface8 = IDirect3DSurface9;
using IDirect3DVolume8 = IDirect3DVolume9;
using IDirect3DSwapChain8 = IDirect3DSwapChain9;
using D3DADAPTER_IDENTIFIER8 = D3DADAPTER_IDENTIFIER9;
using D3DCAPS8 = D3DCAPS9;
using D3DVIEWPORT8 = D3DVIEWPORT9;
using D3DMATERIAL8 = D3DMATERIAL9;
using D3DLIGHT8 = D3DLIGHT9;
using D3DCLIPSTATUS8 = D3DCLIPSTATUS9;

inline IDirect3D8* WINAPI Direct3DCreate8(UINT)
{
    return Direct3DCreate9(D3D_SDK_VERSION);
}
