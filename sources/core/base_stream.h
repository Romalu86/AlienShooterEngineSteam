#pragma once
#include "types.h"
#include <cstddef>

namespace as1
{
    class BaseStream
    {
    protected:
        // Retail STREAM vtable slot 0 is the deleting destructor; keep the
        // declaration before read/write so MSVC emits the original slot order.
        virtual ~BaseStream() = default;

    public:
        virtual int read(void* buf, unsigned size) = 0;
        virtual int write(const void* buf, unsigned size) = 0;

        size_t read_new(void* buf, size_t size)
        {
            const int missing = read(buf, static_cast<unsigned>(size));
            return missing >= 0 && size >= static_cast<size_t>(missing)
                ? size - static_cast<size_t>(missing)
                : 0;
        }

        size_t write_new(const void* buf, size_t size)
        {
            const int missing = write(buf, static_cast<unsigned>(size));
            return missing >= 0 && size >= static_cast<size_t>(missing)
                ? size - static_cast<size_t>(missing)
                : 0;
        }

    };

    template<class T>
    inline bool read(BaseStream& stream, T& value)
    {
        return stream.read(&value, static_cast<unsigned>(sizeof(T))) == 0;
    }

    template<class T>
    inline bool write(BaseStream& stream, const T& value)
    {
        return stream.write(&value, static_cast<unsigned>(sizeof(T))) == 0;
    }
}
