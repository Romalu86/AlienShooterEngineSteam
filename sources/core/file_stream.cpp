#include "file_stream.h"
#include "core/file_stream_p.h"

#include <algorithm>
#include <cstring>

namespace as1
{
    bool FileStream::open(const std::string& path, const char* modeText)
    {
        close();
        m_file = core::file_stream_p::OpenFile(path.c_str(), modeText ? modeText : "rb");
        return m_file != nullptr;
    }

    void FileStream::close()
    {
        if (m_file)
        {
            std::fclose(m_file);
            m_file = nullptr;
        }
    }

    bool FileStream::isWritable() const
    {
        if (!m_file)
            return false;
        return true;
    }

    size_t FileStream::seek(size_t pos)
    {
        if (!m_file)
            return 0;
        core::file_stream_p::SeekFile(m_file, pos);
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    size_t FileStream::shift(int delta)
    {
        if (!m_file)
            return 0;
        std::fseek(m_file, delta, SEEK_CUR);
        return position();
    }

    size_t FileStream::position() const
    {
        if (!m_file)
            return 0;
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    size_t FileStream::length() const
    {
        return m_file ? core::file_stream_p::FileLength(m_file) : 0;
    }

    std::size_t readFileRemainder(FileStream& stream, void* data, std::size_t size)
    {

        return size - std::fread(data, 1, size, stream.m_file);
    }

    std::size_t writeFileRemainder(FileStream& stream, const void* data, std::size_t size)
    {

        return size - std::fwrite(data, 1, size, stream.m_file);
    }

    int FileStream::read(void* buf, unsigned size)
    {
        return static_cast<int>(readFileRemainder(*this, buf, size));
    }

    int FileStream::write(const void* buf, unsigned size)
    {
        return static_cast<int>(writeFileRemainder(*this, buf, size));
    }
}
