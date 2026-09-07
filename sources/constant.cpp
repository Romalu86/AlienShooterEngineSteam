#include "constant.h"
#include "core/resource.h"
#include "core/log.h"

namespace as1
{
    BASE_CONSTANTS* g_baseConstants = nullptr;

    BASE_CONSTANTS* BASE_CONSTANTS::Load(RESOURCE* res)
    {
        if (res->GoBegin(RESOURCE::ResTypes::CONSTANT))
        {
            LOG::Write("!!!ERROR!!! CNST Load Constant section not found");
            return this;
        }

        (void)res->read(&raw[0], 4u);   // +0x00
        (void)res->read(&raw[1], 4u);   // +0x04
        (void)res->read(&raw[2], 4u);   // +0x08
        (void)res->read(&raw[3], 4u);   // +0x0C
        (void)res->read(&raw[4], 4u);   // +0x10
        (void)res->read(&raw[5], 4u);   // +0x14
        (void)res->read(&raw[6], 4u);   // +0x18
        (void)res->read(&raw[7], 4u);   // +0x1C
        (void)res->read(&raw[8], 4u);   // +0x20
        (void)res->read(&raw[9], 4u);   // +0x24

        DWORD discardedCnstValue;
        (void)res->read(&discardedCnstValue, 4u);

        (void)res->read(&raw[11], 4u);  // +0x2C
        (void)res->read(&raw[12], 4u);  // +0x30
        (void)res->read(&raw[13], 4u);  // +0x34
        (void)res->read(&raw[14], 4u);  // +0x38
        (void)res->read(&raw[15], 4u);  // +0x3C
        (void)res->read(&raw[16], 4u);  // +0x40
        (void)res->read(&raw[17], 4u);  // +0x44
        (void)res->read(&raw[18], 4u);  // +0x48
        (void)res->read(&raw[19], 4u);  // +0x4C
        (void)res->read(&raw[20], 4u);  // +0x50
        (void)res->read(&raw[21], 4u);  // +0x54
        (void)res->read(&raw[22], 4u);  // +0x58
        (void)res->read(&raw[23], 4u);  // +0x5C
        (void)res->read(&raw[24], 4u);  // +0x60
        (void)res->read(&raw[25], 4u);  // +0x64

        float* const values = reinterpret_cast<float*>(raw.data());
        values[0] /= 1000.0f;
        values[1] /= 1000.0f;
        values[2] /= 1000000.0f;
        values[3] /= 1000000.0f;
        values[7] /= 1000.0f;
        values[6] /= 1000.0f;
        values[24] /= 1000.0f;
        return this;
    }
}
