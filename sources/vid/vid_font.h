#pragma once

#include "vid/vid.h"

namespace as1
{
    class GraphTextFont;

    class VID_FONT : public VID
    {
    public:
        VID_FONT();
        VID_FONT(const VID_FONT& other);
        ~VID_FONT() override;

        VID_FONT* CreateMirror() override;
        void Draw(const SPRITE* sprite) override;
        void Load(RESOURCE* resource) override;
        int HaveShadow() const override;
        void SetLayer() override;

        int InvalidateDeviceObjects() noexcept;
        int RestoreDeviceObjects() noexcept;

    private:
        GraphTextFont* m_fontOwner = nullptr; // +0x484
    };

#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                             
#endif
}
