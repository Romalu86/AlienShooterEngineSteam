#include "vector.h"
#include "../core/base_stream.h"
#include <stdexcept>

namespace as1
{
    void VECTOR::read(BaseStream* stream)
    {
        if (!stream)
            throw std::runtime_error("VECTOR::read: null stream");
        stream->read(&x, sizeof(x));
        stream->read(&y, sizeof(y));
        stream->read(&z, sizeof(z));
    }
}
