#include "msvc_array_helpers.h"
#include "as_string.h"
#include <cstdint>

namespace as1
{
    void* msvcStringRecordDeletingDestructor(void* rawThis, unsigned char flags) noexcept
    {
        auto* self = static_cast<std::uint8_t*>(rawThis);
#if defined(_WIN32) && defined(_M_IX86)
        if ((flags & 0x02u) != 0)
        {
            auto* header = reinterpret_cast<std::uint32_t*>(self) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
            {
                auto* record = self + static_cast<std::size_t>(i - 1u) * 12u;
                char* const text = *reinterpret_cast<char**>(record + 8u);
                if (text != STRING::SharedEmptyText())
                    ::operator delete(static_cast<void*>(text));
            }
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(header));
            return header;
        }
#endif
        char* const text = *reinterpret_cast<char**>(self + 8u);
        if (text != STRING::SharedEmptyText())
            ::operator delete(static_cast<void*>(text));
        if ((flags & 0x01u) != 0)
            ::operator delete(rawThis);
        return rawThis;
    }
}
