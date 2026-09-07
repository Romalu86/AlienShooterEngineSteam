#include "core/profile_p.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#endif

namespace as1 { namespace core { namespace profile_p
{
#ifndef _WIN32
    namespace
    {
        std::string trim(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.erase(value.begin());
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            return value;
        }

        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        STRING readProfileStringFallback(const STRING& fileName, const STRING& section, const STRING& key, const STRING& defaultValue)
        {
            std::FILE* file = std::fopen(fileName.c_str(), "rb");
            if (!file)
                return defaultValue;
            const std::string wantedSection = lower(section.str());
            const std::string wantedKey = lower(key.str());
            std::string currentSection;
            char line[4096];
            while (std::fgets(line, sizeof(line), file))
            {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                text = trim(text);
                if (text.empty() || text[0] == ';' || text[0] == '#')
                    continue;
                if (text.front() == '[' && text.back() == ']')
                {
                    currentSection = lower(trim(text.substr(1, text.size() - 2)));
                    continue;
                }
                const std::size_t eq = text.find('=');
                if (eq == std::string::npos)
                    continue;
                if (currentSection == wantedSection && lower(trim(text.substr(0, eq))) == wantedKey)
                {
                    std::fclose(file);
                    return STRING(trim(text.substr(eq + 1)));
                }
            }
            std::fclose(file);
            return defaultValue;
        }
    }
#endif

    unsigned int readProfileIntValue(const STRING& fileName, const STRING& section, const STRING& key, int defaultValue)
    {

#ifdef _WIN32
        return static_cast<unsigned int>(::GetPrivateProfileIntA(section.c_str(), key.c_str(), defaultValue, fileName.c_str()));
#else
        const STRING value = ReadProfileString(fileName, section, key, STRING(""));
        if (value.isEmpty())
            return static_cast<unsigned int>(defaultValue);
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 0);
        return (end && *end == 0) ? static_cast<unsigned int>(parsed) : static_cast<unsigned int>(defaultValue);
#endif
    }

    int ReadProfileInt(const STRING& fileName, const STRING& section, const STRING& key, int defaultValue)
    {
        return readProfileIntValue(fileName, section, key, defaultValue);
    }

    STRING ReadProfileString(const STRING& fileName, const STRING& section, const STRING& key, const STRING& defaultValue)
    {
        STRING out;
        readProfileStringInto(out, fileName, section, key, defaultValue);
        return out;
    }

    STRING& readProfileStringInto(STRING& out, const STRING& fileName, const STRING& section, const STRING& key, const STRING& defaultValue)
    {

        char returnedString[0x8000];
#ifdef _WIN32
        ::GetPrivateProfileStringA(section.c_str(),
                                   key.c_str(),
                                   defaultValue.c_str(),
                                   returnedString,
                                   0x7FFFu,
                                   fileName.c_str());
#else
        const STRING fallback = readProfileStringFallback(fileName, section, key, defaultValue);
        const char* ownedText = fallback.c_str();
        const std::size_t textLength = std::min<std::size_t>(std::strlen(ownedText), sizeof(returnedString) - 1);
        std::memcpy(returnedString, ownedText, textLength);
        returnedString[textLength] = '\0';
#endif
        out.AssignAllocatedCopyWithoutRelease(returnedString);
        return out;
    }
} } }
