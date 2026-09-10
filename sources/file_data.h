#pragma once

#include "core/as_string.h"

namespace as1
{
    void InitializeFileDataProfilePaths(const STRING& startupDirectory);
    void FileDataSave(const STRING& path, const STRING& value);
    STRING FileDataLoad(const STRING& path, const STRING& defaultValue);
    int FileDataFileExists(const STRING& filename);
    const char* FileDataSaveFolder() noexcept;
}
