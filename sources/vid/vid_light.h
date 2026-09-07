#pragma once
#include "vid.h"

namespace as1
{
    float spriteCameraRelativeX(const class SPRITE* sprite) noexcept;
    float spriteCameraRelativeProjectedY(const class SPRITE* sprite) noexcept;
    float spriteWorldZ(const class SPRITE* sprite) noexcept;
    std::intptr_t loadVidLightData(class VID_LIGHT* owner, class RESOURCE* resource) noexcept;
class VID_LIGHT : public VID
{
public:
    VID_LIGHT() = default;
    VID_LIGHT(const VID_LIGHT& other);
    ~VID_LIGHT() override;
    VID_LIGHT* CreateMirror() override;
    void SetLayer() override;

    void Load(RESOURCE* resource) override;
    void Draw(const SPRITE* sprite) override;
    bool transparencyCheck() const;
    bool isLoaded() const;
    DWORD lightDataBytes() const noexcept { return m_lightDataBytes; }
    BYTE* lightData() const noexcept { return m_lightData; }
    void setLightDataBytes(DWORD value) noexcept { m_lightDataBytes = value; }
    void setLightData(BYTE* value) noexcept { m_lightData = value; }

private:
    DWORD m_lightDataBytes = 0;
    BYTE* m_lightData = nullptr;
};
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                           
#endif
}
