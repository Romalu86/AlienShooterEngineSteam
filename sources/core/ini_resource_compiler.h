#pragma once

#include "resource.h"
#include "as_string.h"

namespace as1
{
    int CompileIniToResource(RESOURCE& output, const STRING& inputPath);
    int RunIni2ResCommand(int argc, const char* const* argv);
}
