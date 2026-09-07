#pragma once
#include "core/types.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "graphics/angle.h"
#include "graphics/vector.h"
#include <vector>

namespace as1
{
    struct MENU_HEAD
    {
        int version = 0;
        int sizeX = 0;
        int sizeY = 0;
        int shiftX = 0;
        int shiftY = 0;

        bool Read(BaseStream& stream);
    };

    struct MENU_PAYLOAD_RECORD
    {
        bool present = false;
        int byteSize = 0;
        int commandId = 0;
        bool hasTextReference = false;
        STRING textReference;
        bool hasResourceLink = false;
        STRING resourceLink;
        std::vector<int> prefixValues;
        std::vector<BYTE> tailBytes;
    };

    struct MENU_ITEM
    {
        int sourceOrder = 0;
        int subresourceSize = 0;
        int oldAddress = 0;
        int nvid = 0;
        VECTOR xyz;
        ANGLE direction;
        int army = 0;
        bool terminator = false;
        std::vector<BYTE> commandPayload;
        MENU_PAYLOAD_RECORD payloadRecord;

        bool Read(BaseStream& stream, int menuVersion);
        void DecodeCommandPayload();
        void ApplyMenuCentering(const MENU_HEAD& head, float viewportX, float viewportY);
    };
}
