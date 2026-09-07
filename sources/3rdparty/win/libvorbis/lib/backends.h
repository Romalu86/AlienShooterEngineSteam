#pragma once

#include "3rdparty/win/libvorbis/lib/codec_internal.h"








namespace as1 { namespace thirdparty { namespace xiph2003
{
    struct FloorBackendRoute
    {
        int type;
        void* (*unpack)(void* infoRecord, BitCursor& cursor);
        void (*release)(void* record);
    };

    struct ResidueBackendRoute
    {
        int type;
        void* (*unpack)(void* infoRecord, BitCursor& cursor);
        void (*release)(void* record);
    };

    struct MappingBackendRoute
    {
        int type;
        void* (*unpack)(void* infoRecord, BitCursor& cursor);
        void (*release)(void* record);
    };
} } }
