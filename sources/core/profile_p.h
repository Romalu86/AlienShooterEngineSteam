#pragma once


#include "core/as_string.h"

namespace as1 { namespace core { namespace profile_p
{
    int ReadProfileInt(const STRING& fileName, const STRING& section, const STRING& key, int defaultValue);

    unsigned int readProfileIntValue(const STRING& fileName, const STRING& section, const STRING& key, int defaultValue);
    STRING ReadProfileString(const STRING& fileName, const STRING& section, const STRING& key, const STRING& defaultValue);

    STRING& readProfileStringInto(STRING& out, const STRING& fileName, const STRING& section, const STRING& key, const STRING& defaultValue);
} } }
