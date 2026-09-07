#pragma once


#include "core/as_string.h"

namespace as1 { namespace core
{
    class FileStorage
    {
    public:
        static bool IsAbsolutePath(const STRING& path);
        static bool HasExtension(const STRING& path, const char* extension);
        static STRING JoinPath(const STRING& left, const STRING& right);
        static STRING FileStem(const STRING& path);
        static STRING DirectoryName(const STRING& path);
        static bool ExistsForRead(const STRING& path);
    };
} }
