#pragma once
#include "vid_software.h"

namespace as1
{
class VID_SOFTWARE_PNG : public VID_SOFTWARE
{
public:
    void Draw(const SPRITE* sprite) override;
};
}
