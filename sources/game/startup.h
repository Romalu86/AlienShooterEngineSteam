#pragma once
#include "core/as_string.h"

namespace as1
{
    struct StartupOptions
    {
        STRING resourceRoot{"."};
        STRING objectsResource{"objects.res"};
        STRING mapName{"maps\\logo.map"};
        bool loadGameResources = true;
        bool loadMap = true;
    };

}
