#include "logger.h"
#include "logger_p.h"
#include "as_string.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace as1
{
    namespace
    {
        const char* safeText(const char* text)
        {
            return text ? text : "";
        }

        void appendBounded(char* destination, std::size_t capacity, const char* text)
        {
            if (!destination || capacity == 0)
                return;
            const std::size_t base = std::strlen(destination);
            if (base >= capacity - 1)
                return;
            text = safeText(text);
            const std::size_t add = std::min<std::size_t>(std::strlen(text), capacity - base - 1);
            std::memcpy(destination + base, text, add);
            destination[base + add] = '\0';
        }
    }

    void LOGGER::Format(char* destination, std::size_t capacity, const char* format, va_list args)
    {
        if (!destination || capacity == 0)
            return;
        destination[0] = '\0';
        if (!format)
            return;
        (void)capacity;
        if (std::vsprintf(destination, format, args) < 0)
            destination[0] = '\0';
    }

    const char* LOGGER::ResourceErrorSuffix(int errorCode)
    {
        // Resource-error suffix table used by the 2003 logger route.
        switch (errorCode)
        {
        case 0: return "0x%X Couldn't lock %s";
        case 1: return "0x%X Couldn't copy %s";
        case 2: return "%i There was not enough memory for %s";
        case 3: return "0x%X Couldn't create the %s";
        case 4: return "0x%X Invalid %s";
        case 5: return "0x%X Load %s";
        case 6: return "0x%X Save %s";
        case 7: return "0x%X Couldn't open '%s'";
        case 8: return "0x%X Couldn't set the %s";
        case 9: return "0x%X Couldn't get the %s";
        case 10: return "%i %s";
        case 11: return "0x%X Section can't found (%s)";
        case 12: return "0x%X Unable initialize %s";
        case 13: return "%i Missing %s";
        case 14: return "%i Unknownn %s";
        default: return nullptr;
        }
    }

    void LOGGER::BuildResourceError(char* destination,
                                    std::size_t capacity,
                                    const char* contextFormat,
                                    int errorCode,
                                    const char* detailText,
                                    int detailValue,
                                    va_list contextArgs)
    {
        if (!destination || capacity == 0)
            return;

        destination[0] = '\0';
        std::snprintf(destination, capacity, "!!!ERROR %s!!!", STRING::CurrentTimeString().c_str());
        destination[capacity - 1] = '\0';

        char context[0x400] = {};
        Format(context, sizeof(context), contextFormat, contextArgs);
        appendBounded(destination, capacity, context);
        appendBounded(destination, capacity, ": ");

        char suffix[0x400] = {};
        const char* suffixFormat = ResourceErrorSuffix(errorCode);
        if (suffixFormat)
            std::snprintf(suffix, sizeof(suffix), suffixFormat, detailValue, safeText(detailText));
        else
            std::snprintf(suffix, sizeof(suffix), "%i %s", detailValue, safeText(detailText));
        suffix[sizeof(suffix) - 1] = '\0';
        appendBounded(destination, capacity, suffix);
    }
}
