#include "core/file_stream_p.h"

#if defined(_MSC_VER)
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace as1 { namespace core { namespace file_stream_p
{

    std::FILE* OpenFile(const char* path, const char* modeText)
    {
        if (!path || !*path)
            return nullptr;
        return std::fopen(path, modeText ? modeText : "rb");
    }

    std::size_t FileLength(std::FILE* file)
    {
        if (!file)
            return 0;
#if defined(_MSC_VER)
        const int fd = _fileno(file);
        if (fd < 0)
            return 0;
        const long len = _filelength(fd);
        return len > 0 ? static_cast<std::size_t>(len) : 0;
#else
        const int fd = fileno(file);
        if (fd < 0)
            return 0;
        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size <= 0)
            return 0;
        return static_cast<std::size_t>(st.st_size);
#endif
    }


    bool SeekFile(std::FILE* file, std::size_t position)
    {
        if (!file)
            return false;
        return std::fseek(file, static_cast<long>(position), SEEK_SET) == 0;
    }
} } }
