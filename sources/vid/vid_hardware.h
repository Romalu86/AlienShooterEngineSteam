#pragma once
#include "vid.h"

namespace as1
{
class BASE_TEXTURE;
class VID_HARDWARE;
WORD compositeSpriteIntoHardwareVid(VID_HARDWARE* owner, SPRITE* sprite) noexcept;
class RESOURCE;

class VID_HARDWARE : public VID
{
public:

    struct TEX_SIZE
    {
        int marker = 0;          // +0x00
        int textureIndex = 0;    // +0x04
        int sourceX = 0;         // +0x08
        int sourceY = 0;         // +0x0C
        int width = 0;           // +0x10
        int height = 0;          // +0x14
        int destinationX = 0;    // +0x18
        int destinationY = 0;    // +0x1C
        int next = 0;            // +0x20, 0 terminates the chain
    };
                                                                                       

    VID_HARDWARE();
    VID_HARDWARE(const VID_HARDWARE& other);
    VID_HARDWARE(int nvid, int width, int height);
    ~VID_HARDWARE() override;

    VID_HARDWARE* CreateMirror() override;
    void SetLayer() override;
    void selectHardwareRenderLayer() noexcept;
    void Draw(const SPRITE* sprite) override;
    bool transparencyCheck() const;
    bool isLoaded() const;

    void Load(RESOURCE* resource) override;

    void AddVidToVid(SPRITE* sprite) override;

    WORD texturePageCount() const noexcept { return m_texturePageCount; }
    TEX_SIZE* textureLayout() const noexcept { return m_textureLayout; }
    BASE_TEXTURE** texturePages() const noexcept { return m_texturePages; }

private:
    void createEmptyTextures(int width, int height);
    void releaseHardwareResources() noexcept;

    TEX_SIZE* m_textureLayout = nullptr;
    WORD m_texturePageCount = 0;
    BASE_TEXTURE** m_texturePages = nullptr;

};
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                 
#endif
}
