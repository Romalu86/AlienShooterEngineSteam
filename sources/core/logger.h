#pragma once
#include "types.h"
#include <cstdarg>
#include <cstddef>

namespace as1
{
    class LOGGER
    {
    public:
        static void Format(char* destination, std::size_t capacity, const char* format, va_list args);
        static const char* ResourceErrorSuffix(int errorCode);
        static void BuildResourceError(char* destination,
                                       std::size_t capacity,
                                       const char* contextFormat,
                                       int errorCode,
                                       const char* detailText,
                                       int detailValue,
                                       va_list contextArgs);
    };
}
