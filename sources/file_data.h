#pragma once

#include "core/as_string.h"

namespace as1
{
    void InitializeFileDataProfilePaths(const STRING& startupDirectory);
    void FSaveData(const STRING& path, const STRING& value);
    STRING FLoadData(const STRING& path, const STRING& defaultValue);
    int FileDataFileExist(const STRING& filename);
    const char* FSaveDataFolder() noexcept;
}
