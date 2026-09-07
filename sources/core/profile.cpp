#include "core/profile.h"
#include "core/profile_p.h"

namespace as1 { namespace core
{
    int Profile::readInt(const char* section, const char* key, int defaultValue) const
    {
        return profile_p::ReadProfileInt(m_fileName, STRING(section ? section : ""), STRING(key ? key : ""), defaultValue);
    }

    STRING Profile::readString(const char* section, const char* key, const STRING& defaultValue) const
    {
        return profile_p::ReadProfileString(m_fileName, STRING(section ? section : ""), STRING(key ? key : ""), defaultValue);
    }

    bool Profile::readBool(const char* section, const char* key, bool defaultValue) const
    {
        return readInt(section, key, defaultValue ? 1 : 0) != 0;
    }
} }
