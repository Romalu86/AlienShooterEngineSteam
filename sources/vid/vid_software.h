#pragma once
#include "vid.h"
#include <array>

namespace as1
{
DWORD* blendBgraPixelByAlpha(DWORD* destination, DWORD* resultOwner, int sourceColor, int sourceAlpha) noexcept;
int drawOpaquePaletteSpanWithDepth(BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept;
int drawAlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept;

class VID_SOFTWARE : public VID
{
public:
    VID_SOFTWARE() = default;
    VID_SOFTWARE(const VID_SOFTWARE& other);
    ~VID_SOFTWARE() override;
    VID_SOFTWARE* CreateMirror() override;
    void Load(RESOURCE* resource) override;
    void loadCompactSoftwareVidData(RESOURCE* resource);
    void SetLayer() override;
    void SetGamma(const Gamma& gamma, unsigned n_gamma = 0);
    void Draw(const SPRITE* sprite) override;
    int updateGroundZFromCompactFrame(const SPRITE* sprite) noexcept;
    int DrawShadow(const SPRITE* sprite) const override;
    int HaveShadow() const override;
    bool transparencyCheck() const;
    virtual int PaletteSize() const;
    bool isLoaded() const;
    bool unloadable() const;

    DWORD* frameOffsets() const noexcept { return m_frameOffsets; }
    DWORD frameStorageBytes() const noexcept { return m_frameStorageBytes; }
    BYTE* frameStorage() const noexcept { return m_frameStorage; }
    void setFrameOffsets(DWORD* value) noexcept { m_frameOffsets = value; }
    void setFrameStorageBytes(DWORD value) noexcept { m_frameStorageBytes = value; }
    void setFrameStorage(BYTE* value) noexcept { m_frameStorage = value; }

    void SetGammaRaw(const GammaRawPair& rawGamma, unsigned n_gamma) override;

    virtual void ApplyGammaToPaletteRaw(void* palette, const GammaRawPair& rawGamma);

    int SoftwareGammaPaletteBlockBytes() const;

    bool SoftwareGammaComposesGraphRaw() const;

private:
    void SetGammaRawToSoftwareBuffer(const GammaRawPair& rawGamma, unsigned n_gamma, bool storeSlot);
    void EnsureSoftwareGammaBaseBuffer();
    void ExpandSoftwareGammaBufferIfNeeded();
    void CopySoftwareGammaBlock(size_t dstBlock, size_t srcBlock);
    void ApplyGammaToSoftwareGammaBlock(size_t blockIndex, const GammaRawPair& rawGamma);
    GammaRawPair ComposeSoftwareGammaApplyRaw(const GammaRawPair& rawGamma) const;


    DWORD* m_frameOffsets = nullptr;
    DWORD m_frameStorageBytes = 0;
    BYTE* m_frameStorage = nullptr;
};
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                 
#endif
}
