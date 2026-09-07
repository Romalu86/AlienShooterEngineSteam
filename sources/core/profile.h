#pragma once


#include "core/as_string.h"

namespace as1 { namespace core
{
    class Profile
    {
    public:
        Profile() = default;
        explicit Profile(const STRING& fileName) : m_fileName(fileName) {}

        void setFileName(const STRING& fileName) { m_fileName = fileName; }
        const STRING& fileName() const { return m_fileName; }

        int readInt(const char* section, const char* key, int defaultValue) const;
        STRING readString(const char* section, const char* key, const STRING& defaultValue) const;
        bool readBool(const char* section, const char* key, bool defaultValue) const;

    private:
        STRING m_fileName;
    };
} }
