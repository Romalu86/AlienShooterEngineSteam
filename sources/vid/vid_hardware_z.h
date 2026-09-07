#pragma once
#include "vid_software.h"

namespace as1
{
int composeHardwareZOpaqueSpan(const WORD* sourceZ, const WORD* sourceColor, const WORD* destinationZ, WORD* destinationColor, int count) noexcept;
int composeHardwareZAlphaSpan(const WORD* sourceZ, const WORD* sourceColor, const WORD* destinationZ, WORD* destinationColor, int count) noexcept;
    void selectHardwareZRenderLayer(class VID_HARDWARE_Z* owner) noexcept;
class VID_HARDWARE_Z : public VID_SOFTWARE
{
public:
    VID_HARDWARE_Z() = default;
    VID_HARDWARE_Z(const VID_HARDWARE_Z& other);
    VID_HARDWARE_Z* CreateMirror() override;
    void SetLayer() override;
    void SetGamma(const Gamma& gamma, unsigned n_gamma = 0);
    void SetGammaRaw(const GammaRawPair& rawGamma, unsigned n_gamma) override;
    void Draw(const SPRITE* sprite) override;
    bool transparencyCheck() const;
    bool isLoaded() const;
};
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                     
#endif
}
