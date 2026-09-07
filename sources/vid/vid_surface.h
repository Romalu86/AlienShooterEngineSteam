#pragma once
#include "vid.h"
#include "vid/vid_texcoor.h"
#include "graphics/base_texture.h"
#include <array>
#include <cstddef>

namespace as1
{
class VID_SURFACE : public VID
{
public:
    VID_SURFACE();
    VID_SURFACE(const VID_SURFACE& other);
    ~VID_SURFACE() override;
    VID_SURFACE* CreateMirror() override;
    void SetLayer() override;

    void Draw(const SPRITE* sprite) override;
    bool transparencyCheck() const;
    bool isLoaded() const;

    void Load(RESOURCE* globalRes) override;
    BASE_TEXTURE* const* surfaceTextureOwners() const { return m_surfaceTextureOwners; }
    VID_TEXCOOR* const* surfaceTexcoordOwners() const { return m_surfaceTexcoordOwners; }

private:
    void ReleaseSurfaceOwnerSlots() const noexcept;

#if defined(_MSC_VER) && defined(_M_IX86)
    std::array<BYTE, 0x28> m_rawSurfaceTail41C_443;
    mutable VID_TEXCOOR** m_surfaceTexcoordOwners = nullptr; // +0x4AC
    mutable BASE_TEXTURE** m_surfaceTextureOwners = nullptr;   // +0x4B0
    DWORD m_surfaceSourceFormat = 0;                            // +0x4B4
#else
    mutable BASE_TEXTURE** m_surfaceTextureOwners = nullptr;
    mutable VID_TEXCOOR** m_surfaceTexcoordOwners = nullptr;
    DWORD m_surfaceSourceFormat = 0;
#endif
};
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                               
#endif
}
