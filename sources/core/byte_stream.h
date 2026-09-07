#pragma once
#include "base_stream.h"
#include <vector>

namespace as1
{
    class ByteStream final : public BaseStream
    {
    public:
        explicit ByteStream(size_t reserveSize = 0);
        explicit ByteStream(std::vector<BYTE> data);

        int read(void* buf, unsigned size) override;
        int write(const void* buf, unsigned size) override;
        size_t seek(size_t pos);
        size_t shift(int delta);
        size_t position() const { return m_pos; }
        size_t length() const { return m_data.size(); }
        void close() { m_open = false; }
        bool isOpen() const { return m_open; }
        bool isWritable() const { return true; }

        const std::vector<BYTE>& data() const { return m_data; }
        std::vector<BYTE>& data() { return m_data; }

    private:
        std::vector<BYTE> m_data;
        size_t m_pos = 0;
        bool m_open = true;
    };
}
