#pragma once
#include "base_stream.h"

namespace as1
{

    class Filter
    {
    public:
        virtual ~Filter() = default;

        // Returns decoded bytes written to destination. Default filter is a pass-through.
        virtual size_t ReadDecoded(void* destination, size_t size, BaseStream& stream)
        {
            return stream.read_new(destination, size);
        }

        // Returns encoded bytes written to stream. Default filter is a pass-through.
        virtual size_t WriteEncoded(const void* source, size_t size, BaseStream& stream)
        {
            return stream.write_new(source, size);
        }
    };
}
