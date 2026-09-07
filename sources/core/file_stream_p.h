#pragma once


#include <cstdio>
#include <cstddef>

namespace as1 { namespace core { namespace file_stream_p
{
    std::FILE* OpenFile(const char* path, const char* modeText);
    std::size_t FileLength(std::FILE* file);
    bool SeekFile(std::FILE* file, std::size_t position);
} } }
