#pragma once
#include "base_stream.h"
#include <cstdio>
#include <string>

namespace as1
{
    class FileStream final : public BaseStream
    {
    public:
        FileStream() = default;
        FileStream(const std::string& path, const char* mode) { open(path, mode); }
        ~FileStream() { close(); }

        bool open(const std::string& path, const char* mode);
        void close();
        bool isOpen() const { return m_file != nullptr; }
        bool isWritable() const;
        size_t seek(size_t pos);
        size_t shift(int delta);
        size_t position() const;
        size_t length() const;

        int read(void* buf, unsigned size) override;
        int write(const void* buf, unsigned size) override;

        std::FILE* nativeFile() const { return m_file; }

    private:
        friend std::size_t readFileRemainder(FileStream& stream, void* data, std::size_t size);
        friend std::size_t writeFileRemainder(FileStream& stream, const void* data, std::size_t size);

        std::FILE* m_file = nullptr;
    };

    using FSTREAM = FileStream;

    std::size_t readFileRemainder(FileStream& stream, void* data, std::size_t size);
    std::size_t writeFileRemainder(FileStream& stream, const void* data, std::size_t size);

#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                     
#endif
}
