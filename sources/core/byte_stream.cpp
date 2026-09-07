#include "byte_stream.h"
#include <algorithm>
#include <cstring>

namespace as1
{
    ByteStream::ByteStream(size_t reserveSize)
    {
        m_data.reserve(reserveSize);
    }

    ByteStream::ByteStream(std::vector<BYTE> data)
        : m_data(std::move(data))
    {
    }

    int ByteStream::read(void* buf, unsigned size)
    {
        if (!m_open || !buf || size == 0)
            return static_cast<int>(size);
        const size_t available = (m_pos < m_data.size()) ? (m_data.size() - m_pos) : 0;
        const size_t n = std::min<size_t>(size, available);
        if (n)
        {
            std::memcpy(buf, m_data.data() + m_pos, n);
            m_pos += n;
        }
        return static_cast<int>(static_cast<size_t>(size) - n);
    }

    int ByteStream::write(const void* buf, unsigned size)
    {
        if (!m_open || !buf || size == 0)
            return static_cast<int>(size);
        if (m_pos + size > m_data.size())
            m_data.resize(m_pos + size);
        std::memcpy(m_data.data() + m_pos, buf, size);
        m_pos += size;
        return 0;
    }

    size_t ByteStream::seek(size_t pos)
    {
        m_pos = std::min(pos, m_data.size());
        return m_pos;
    }

    size_t ByteStream::shift(int delta)
    {
        if (delta < 0)
        {
            const size_t back = static_cast<size_t>(-static_cast<long long>(delta));
            m_pos = (back > m_pos) ? 0 : (m_pos - back);
        }
        else
        {
            m_pos = std::min(m_data.size(), m_pos + static_cast<size_t>(delta));
        }
        return m_pos;
    }
}
