#include "as_string.h"
#include "base_stream.h"
#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <cwchar>
#include <stdexcept>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace as1
{
    namespace
    {
        char gSharedEmptyStringStorage = '\0';
        unsigned char gRegistryData[0x200]{};

#if defined(_WIN32)
        bool queryRegistryValue(HKEY root, const char* subKey, const char* valueName,
                              DWORD& type, DWORD& cbData) noexcept
        {
            HKEY opened = nullptr;
            if (::RegOpenKeyExA(root, subKey, 0, KEY_QUERY_VALUE, &opened) != ERROR_SUCCESS)
                return false;

            type = 0;
            cbData = 0x1FFu;
            const LONG result = ::RegQueryValueExA(opened, valueName, nullptr, &type, gRegistryData, &cbData);
            ::RegCloseKey(opened);
            return result == ERROR_SUCCESS;
        }

        bool writeRegistryValue(HKEY root, const char* subKey, const char* valueName,
                              DWORD type, const BYTE* data, DWORD cbData) noexcept
        {
            HKEY opened = nullptr;
            DWORD disposition = 0;
            if (::RegCreateKeyExA(root, subKey, 0, const_cast<LPSTR>(""), 0,
                                  0xF003Fu, nullptr, &opened, &disposition) != ERROR_SUCCESS)
                return false;
            const LONG result = ::RegSetValueExA(opened, valueName, 0, type, data, cbData);
            ::RegCloseKey(opened);
            return result == ERROR_SUCCESS;
        }

        void deleteRegistryValue(HKEY root, const char* subKey, const char* valueName) noexcept
        {
            HKEY opened = nullptr;
            if (::RegOpenKeyExA(root, subKey, 0, 0xF003Fu, &opened) != ERROR_SUCCESS)
                return;
            ::RegDeleteValueA(opened, valueName);
            ::RegCloseKey(opened);
        }
#endif

        const char* safeCString(const char* text)
        {
            return text ? text : "";
        }

#if defined(_MSC_VER)
        __forceinline
#endif
        std::size_t retailStringAllocationSize(std::size_t textLength) noexcept
        {
            return (textLength & ~static_cast<std::size_t>(0x0Fu)) + 0x10u;
        }

#if defined(_MSC_VER)
        __forceinline
#endif
        bool sameRetailStringBucket(std::size_t oldLength, std::size_t newLength) noexcept
        {
            return ((oldLength ^ newLength) & ~static_cast<std::size_t>(0x0Fu)) == 0u;
        }

        char* allocateStringBuffer(const char* text)
        {
            text = safeCString(text);
            const std::size_t len = std::strlen(text);
            if (len == 0)
                return STRING::SharedEmptyText();

            auto* out = static_cast<char*>(::operator new(retailStringAllocationSize(len)));
            std::memcpy(out, text, len + 1);
            return out;
        }

        char* allocateStringRange(const char* text, std::size_t len)
        {
            if (!text || len == 0)
                return STRING::SharedEmptyText();

            auto* out = static_cast<char*>(::operator new(retailStringAllocationSize(len)));
            std::memcpy(out, text, len);
            out[len] = '\0';
            return out;
        }

        STRING stringFromRange(const char* text, std::size_t len)
        {
            if (!text || len == 0)
                return STRING();
            return STRING(std::string(text, len));
        }

        char* allocateConcatBuffer(const char* left, const char* right)
        {
            left = safeCString(left);
            right = safeCString(right);

            const std::size_t lenLeft = std::strlen(left);
            const std::size_t lenRight = std::strlen(right);
            auto* out = static_cast<char*>(::operator new(retailStringAllocationSize(lenLeft + lenRight)));
            std::memcpy(out, left, lenLeft);
            std::memcpy(out + lenLeft, right, lenRight + 1);
            return out;
        }

        char* appendIntoNewBuffer(const char* current, const char* suffix)
        {
            current = safeCString(current);
            suffix = safeCString(suffix);

            const std::size_t lenCurrent = std::strlen(current);
            const std::size_t lenSuffix = std::strlen(suffix);
            auto* out = static_cast<char*>(::operator new(retailStringAllocationSize(lenCurrent + lenSuffix)));
            std::memcpy(out, current, lenCurrent);
            std::memcpy(out + lenCurrent, suffix, lenSuffix + 1);
            return out;
        }

        void releaseStringBuffer(char* text)
        {
            if (text && text != STRING::SharedEmptyText())
                ::operator delete(text);
        }


        char* allocateFormattedBuffer(const char* format, va_list args)
        {
            if (!format || *format == '\0')
                return STRING::SharedEmptyText();

            char buffer[0x1000];
            std::memset(buffer, 0, sizeof(buffer));

            va_list copy;
            va_copy(copy, args);
            const int written = std::vsnprintf(buffer, sizeof(buffer), format, copy);
            va_end(copy);

            if (written <= 0 || buffer[0] == '\0')
                return STRING::SharedEmptyText();

            const std::size_t len = std::strlen(buffer);
            auto* out = static_cast<char*>(::operator new(retailStringAllocationSize(len)));
            std::memcpy(out, buffer, len + 1);
            return out;
        }

        std::size_t fileLengthFromStart(FILE* file)
        {
            if (!file)
                return std::numeric_limits<std::size_t>::max();

            if (std::fseek(file, 0, SEEK_END) != 0)
                return std::numeric_limits<std::size_t>::max();

            const long len = std::ftell(file);
            if (len < 0)
                return std::numeric_limits<std::size_t>::max();

            if (std::fseek(file, 0, SEEK_SET) != 0)
                return std::numeric_limits<std::size_t>::max();

            return static_cast<std::size_t>(len);
        }


        FILE* openBinaryFileForRead(const char* path)
        {
#if defined(_MSC_VER)
            FILE* file = nullptr;
            return fopen_s(&file, path, "rb") == 0 ? file : nullptr;
#else
            return std::fopen(path, "rb");
#endif
        }

        bool getLocalTime(std::time_t rawTime, std::tm& out)
        {
#if defined(_MSC_VER)
            return localtime_s(&out, &rawTime) == 0;
#else
            const std::tm* local = std::localtime(&rawTime);
            if (!local)
                return false;
            out = *local;
            return true;
#endif
        }

        void copyCStringBounded(char* destination, std::size_t capacity, const char* source)
        {
            if (!destination || capacity == 0)
                return;
            source = safeCString(source);
            const std::size_t len = std::min<std::size_t>(std::strlen(source), capacity - 1);
            std::memcpy(destination, source, len);
            destination[len] = '\0';
        }

        char* createTemporaryNameOwner(const char* directory, const char* prefix)
        {
#if defined(_MSC_VER)
            return ::_tempnam(directory, prefix);
#else
            return ::tempnam(directory, prefix);
#endif
        }
    }

    char* STRING::SharedEmptyText()
    {
        return &gSharedEmptyStringStorage;
    }


    char* constructStringFromBytes(STRING& destination, const void* source, std::size_t length)
    {

        auto* owner = static_cast<char*>(::operator new(retailStringAllocationSize(length)));
        std::memcpy(owner, source, length);
        owner[length] = '\0';
        destination.m_text = owner;
        return destination.m_text;
    }


    STRING& constructStringFromCString(STRING& destination, const char* source)
    {

        if (!source || *source == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t length = std::strlen(source);
        auto* owner = static_cast<char*>(::operator new(retailStringAllocationSize(length)));
        std::memcpy(owner, source, length);
        owner[length] = '\0';
        destination.m_text = owner;
        return destination;
    }

    STRING::STRING() : m_text(SharedEmptyText())
    {
    }

    STRING::STRING(const char* s) : m_text(allocateStringBuffer(s))
    {
    }

    STRING::STRING(std::string s) : STRING(s.c_str())
    {
    }

    STRING::STRING(const char* left, const char* right) : m_text(SharedEmptyText())
    {
        constructConcatenatedString(*this, left, right);
    }

    STRING::STRING(const STRING& other) : m_text(SharedEmptyText())
    {
        copyConstructString(*this, other);
    }

    STRING::STRING(STRING&& other) noexcept : m_text(other.m_text)
    {
        other.m_text = SharedEmptyText();
    }

    STRING::~STRING()
    {

        destroyStringStorage(*this);
    }

    void destroyStringStorage(STRING& value)
    {

        if (value.m_text != STRING::SharedEmptyText())
            ::operator delete(value.m_text);
    }

    void STRING::ReleaseOwnedStorage()
    {

        releaseStringBuffer(m_text);
        m_text = SharedEmptyText();
    }

    char* STRING::DetachOwnedStorage() noexcept
    {
        char* const owner = m_text;
        m_text = SharedEmptyText();
        return owner;
    }

    void STRING::AdoptOwnedStorage(char* owner) noexcept
    {
        m_text = owner;
    }


    void STRING::ResetSharedEmptyWithoutRelease() noexcept
    {
        m_text = SharedEmptyText();
    }

    void STRING::AssignAllocatedCopyWithoutRelease(const char* text)
    {

        text = safeCString(text);

        if (*text == '\0')
        {
            m_text = SharedEmptyText();
            return;
        }

        const std::size_t len = std::strlen(text);
        auto* out = static_cast<char*>(::operator new(retailStringAllocationSize(len)));
        std::memcpy(out, text, len);
        out[len] = '\0';
        m_text = out;
    }

    STRING& STRING::operator=(const STRING& other)
    {
        return Assign(other);
    }

    STRING& STRING::operator=(STRING&& other) noexcept
    {
        if (this != &other)
        {
            releaseStringBuffer(m_text);
            m_text = other.m_text;
            other.m_text = SharedEmptyText();
        }
        return *this;
    }

    STRING& STRING::operator=(const char* text)
    {
        return Assign(text);
    }

    STRING& STRING::operator=(const std::string& text)
    {
        return Assign(text.c_str());
    }

    const char* STRING::c_str() const
    {

        return m_text;
    }

    const char* STRING::CharPtr() const
    {
        return c_str();
    }

    bool STRING::isEmpty() const
    {
        return c_str()[0] == '\0';
    }

    int STRING::Length() const
    {
        return static_cast<int>(std::strlen(c_str()));
    }

    int STRING::Int() const
    {
        return std::atoi(c_str());
    }

    STRING STRING::ToLower() const
    {
        std::string out = str();
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return STRING(out);
    }

    STRING STRING::ToUpper() const
    {
        std::string out = str();
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return STRING(out);
    }

    STRING STRING::ToBase64(int add) const
    {
        static constexpr char table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        const std::string source = str();
        std::string result;
        result.reserve(((source.size() + 2u) / 3u) * 4u);

        for (std::size_t pos = 0; pos < source.size(); pos += 57u)
        {
            const std::size_t chunk = std::min<std::size_t>(57u, source.size() - pos);
            for (std::size_t t = 0; t < chunk; t += 3u)
            {
                unsigned char bytes[3] = {0, 0, 0};
                const std::size_t available = std::min<std::size_t>(3u, chunk - t);
                for (std::size_t i = 0; i < available; ++i)
                {
                    const unsigned char raw = static_cast<unsigned char>(source[pos + t + i]);
                    bytes[i] = static_cast<unsigned char>(static_cast<unsigned int>(raw) + static_cast<unsigned int>(add));
                }

                const unsigned int triple =
                    (static_cast<unsigned int>(bytes[0]) << 16) |
                    (static_cast<unsigned int>(bytes[1]) << 8) |
                    static_cast<unsigned int>(bytes[2]);
                result.push_back(table[(triple >> 18) & 0x3Fu]);
                result.push_back(table[(triple >> 12) & 0x3Fu]);
                result.push_back(table[(triple >> 6) & 0x3Fu]);
                result.push_back(table[triple & 0x3Fu]);
            }
        }

        const std::size_t pad = (3u - source.size() % 3u) % 3u;
        for (std::size_t i = 0; i < pad && i < result.size(); ++i)
            result[result.size() - 1u - i] = '=';
        return STRING(std::move(result));
    }

    void STRING::AssignBytes(const void* data, std::size_t size)
    {
        if (!data || size == 0)
        {
            Assign("");
            return;
        }

        const auto* bytes = static_cast<const char*>(data);
        const auto* end = static_cast<const char*>(std::memchr(bytes, '\0', size));
        const std::size_t realSize = end ? static_cast<std::size_t>(end - bytes) : size;
        std::string tmp(bytes, bytes + realSize);
        Assign(tmp.c_str());
    }

    void STRING::Read(BaseStream* stream)
    {
        if (!stream)
            throw std::runtime_error("STRING::Read called with null stream");

        std::string tmp;
        char ch = 0;
        while (stream->read_new(&ch, 1) == 1 && ch != '\0')
            tmp.push_back(ch);
        Assign(tmp.c_str());
    }

    void STRING::Write(BaseStream* stream) const
    {
        if (!stream)
            throw std::runtime_error("STRING::Write called with null stream");

        const char* text = c_str();
        stream->write_new(text, std::strlen(text));
        const char zero = 0;
        stream->write_new(&zero, 1);
    }

    STRING& STRING::ReadLine(FILE* file)
    {
        return readStringLineFromFile(*this, file);
    }

    STRING& STRING::ReadLine(BaseStream* stream)
    {
        return readStringLineFromStream(*this, stream);
    }


    int STRING::LoadFile(const STRING& path)
    {
        return loadStringFromFile(*this, &path);
    }

    int STRING::LoadFile(const char* path)
    {

        const STRING pathOwner(path ? path : "");
        return loadStringFromFile(*this, &pathOwner);
    }



    STRING& readStringLineFromFile(STRING& destination, FILE* file)
    {

        assignStringFromCString(destination, "");

        char chunk[256];
        int count = 0;
        int ch = 0;

        do
        {
            if (count == 255)
            {
                chunk[255] = '\0';
                appendCStringToString(destination, chunk);
                count = 0;
            }

            ch = std::fgetc(file);
            if (ch < 0 || ch == '\n')
                ch = 0;
            else if (ch == '\r')
                continue;

            chunk[count++] = static_cast<char>(ch);
        } while (ch > 0);

        return appendCStringToString(destination, chunk);
    }

    STRING& readStringLineFromStream(STRING& destination, BaseStream* stream)
    {

        assignStringFromCString(destination, "");

        char chunk[256];
        int count = 0;
        unsigned value = 0;
        int ch = 0;

        do
        {
            if (count == 255)
            {
                chunk[255] = '\0';
                appendCStringToString(destination, chunk);
                count = 0;
            }

            value = 0;
            if (stream->read(&value, 1) != 0)
            {
                ch = 0;
                value = 0;
            }
            else
            {
                ch = static_cast<int>(value & 0xFFu);
                if (ch == '\n')
                {
                    ch = 0;
                    value = 0;
                }
                else if (ch == '\r')
                {
                    continue;
                }
            }

            chunk[count++] = static_cast<char>(ch);
        } while (ch > 0);

        return appendCStringToString(destination, chunk);
    }

    int loadStringFromFile(STRING& destination, const STRING* pathOwner)
    {

        const char* const path = pathOwner ? pathOwner->c_str() : STRING::SharedEmptyText();
        FILE* file = (*path != '\0') ? openBinaryFileForRead(path) : nullptr;
        if (!file)
        {
            assignStringFromCString(destination, "");
            return 0;
        }

        const int fileLength = static_cast<int>(fileLengthFromStart(file));
        if (destination.m_text != STRING::SharedEmptyText())
            ::operator delete(destination.m_text);

        destination.m_text = static_cast<char*>(::operator new(static_cast<unsigned int>(fileLength + 1), std::nothrow));
        if (!destination.m_text)
        {
            destination.m_text = STRING::SharedEmptyText();
            std::fclose(file);
            return 0;
        }

        std::fread(destination.m_text, 1u, static_cast<std::size_t>(fileLength), file);
        destination.m_text[fileLength] = '\0';
        std::fclose(file);
        return fileLength;
    }

    STRING STRING::Format(const char* format, ...)
    {

        va_list args;
        va_start(args, format);
        STRING out = FormatV(format, args);
        va_end(args);
        return out;
    }

    STRING STRING::FormatV(const char* format, va_list args)
    {
        STRING out;
        releaseStringBuffer(out.m_text);
        out.m_text = allocateFormattedBuffer(format, args);
        return out;
    }

    STRING& STRING::AssignFormat(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        AssignFormatV(format, args);
        va_end(args);
        return *this;
    }

    STRING& STRING::AssignFormatV(const char* format, va_list args)
    {
        char* formatted = allocateFormattedBuffer(format, args);
        releaseStringBuffer(m_text);
        m_text = formatted;
        return *this;
    }

    STRING& constructCurrentTimeString(STRING& destination)
    {

        std::time_t rawTime = 0;
        std::time(&rawTime);
        std::tm* localTime = std::localtime(&rawTime);
        char timeText[0x100];
        std::strftime(timeText, sizeof(timeText), "%H:%M:%S", localTime);

        if (timeText[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(timeText);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, timeText, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    STRING& constructCurrentDateString(STRING& destination)
    {

        std::time_t rawTime = 0;
        std::time(&rawTime);
        std::tm* localTime = std::localtime(&rawTime);
        char dateText[0x100];
        std::strftime(dateText, sizeof(dateText), "%Y-%m-%d", localTime);

        if (dateText[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(dateText);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, dateText, textLen);
        owner[textLen] = '\0';
        return destination;
    }

#if defined(_WIN32)
    STRING& constructFileTimestampString(STRING& destination, const STRING& fileName)
    {
        FILETIME creationTime{};
        FILETIME lastAccessTime{};
        FILETIME lastWriteTime{};
        HANDLE const file = ::CreateFileA(fileName.c_str(), GENERIC_READ, 0, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ::GetFileTime(file, &creationTime, &lastAccessTime, &lastWriteTime);
        ::CloseHandle(file);

        destination.Assign("Cr-");

        FILETIME localTime{};
        SYSTEMTIME systemTime{};
        char text[0x50]{};

        auto appendRetailStamp = [&](const char* prefix)
        {
            ::FileTimeToLocalFileTime(&lastWriteTime, &localTime);
            ::FileTimeToSystemTime(&localTime, &systemTime);
            ::GetDateFormatA(0x400, 0, &systemTime, "yyyy-MM-dd", text, 0x50);
            destination.Append(text);
            destination.Append(" ");
            ::GetTimeFormatA(0x400, 0, &systemTime, "hh:mm:ss", text, 0x50);
            destination.Append(text);
            if (prefix)
                destination.Append(prefix);
        };

        appendRetailStamp(" La-");
        appendRetailStamp(" Lw-");
        appendRetailStamp(nullptr);
        return destination;
    }
#else
    STRING& constructFileTimestampString(STRING& destination, const STRING&)
    {
        destination.Assign("");
        return destination;
    }
#endif

    STRING& constructTemporaryNameString(STRING& destination, const char* directory, const char* prefix)
    {

        char temporaryName[0x1000];
        std::memset(temporaryName, 0, sizeof(temporaryName));

        char* tempOwner = createTemporaryNameOwner(directory, prefix);
        std::strcpy(temporaryName, tempOwner);
        std::free(tempOwner);

        if (temporaryName[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(temporaryName);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, temporaryName, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    STRING STRING::CurrentTimeString()
    {
        STRING out;
        constructCurrentTimeString(out);
        return out;
    }

    STRING STRING::CurrentDateString()
    {
        STRING out;
        constructCurrentDateString(out);
        return out;
    }


    STRING STRING::TemporaryName(const char* directory, const char* prefix)
    {
        STRING out;
        constructTemporaryNameString(out, directory, prefix);
        return out;
    }

    STRING& trimStringRight(STRING& destination, const char* chars)
    {

        char* begin = destination.m_text;
        char* cursor = begin + std::strlen(begin);
        if (cursor == begin)
            return destination;

        --cursor;
        while (cursor >= begin)
        {
            const int ch = static_cast<unsigned char>(*cursor);
            if (std::strchr(chars, ch) == nullptr)
                break;
            *cursor = '\0';
            if (cursor == begin)
                break;
            --cursor;
        }
        return destination;
    }

    int replaceStringFirst(STRING& destination, const char* search, const char* replacement)
    {

        char* current = destination.m_text;
        char* found = std::strstr(current, search);
        if (!found)
            return 0;

        const std::size_t searchLen = std::strlen(search);
        const std::size_t replacementLen = std::strlen(replacement);

        if (searchLen < replacementLen)
        {
            char* oldOwner = destination.m_text;
            const std::size_t prefixLen = static_cast<std::size_t>(found - oldOwner);
            const std::size_t oldLen = std::strlen(oldOwner);
            const std::size_t newLen = oldLen - searchLen + replacementLen;
            char* newOwner = static_cast<char*>(::operator new(retailStringAllocationSize(newLen)));
            destination.m_text = newOwner;

            if (prefixLen != 0)
                std::strncpy(newOwner, oldOwner, prefixLen);
            if (replacementLen != 0)
                std::strncpy(newOwner + prefixLen, replacement, replacementLen);
            std::strncpy(newOwner + prefixLen + replacementLen, found + searchLen, oldLen - prefixLen - searchLen + 1);

            releaseStringBuffer(oldOwner);
            return 1;
        }

        if (replacementLen != 0)
            std::strncpy(found, replacement, replacementLen);

        const std::size_t tailLen = std::strlen(found + searchLen);
        std::memcpy(found + replacementLen, found + searchLen, tailLen + 1);
        return 1;
    }

    STRING& incrementTrailingNumberString(const STRING& source, STRING& destination, int delta)
    {

        const char* sourceText = source.m_text;
        char* tempOwner = nullptr;

        if (*sourceText != '\0')
        {
            const std::size_t sourceLen = std::strlen(sourceText);
            tempOwner = static_cast<char*>(::operator new(retailStringAllocationSize(sourceLen)));
            std::memcpy(tempOwner, sourceText, sourceLen);
            tempOwner[sourceLen] = '\0';
        }
        else
        {
            tempOwner = STRING::SharedEmptyText();
        }

        if (delta != 0)
        {
            int lastDigit = static_cast<int>(std::strlen(sourceText)) - 2;
            while (lastDigit >= 0 && !std::isdigit(static_cast<int>(static_cast<signed char>(sourceText[lastDigit]))))
                --lastDigit;

            if (lastDigit < 0)
            {
                destination.m_text = STRING::SharedEmptyText();
                releaseStringBuffer(tempOwner);
                return destination;
            }

            int firstDigit = lastDigit - 1;
            while (firstDigit >= 0 && std::isdigit(static_cast<int>(static_cast<signed char>(sourceText[firstDigit]))))
                --firstDigit;
            if (firstDigit < 0 || sourceText[firstDigit] != '-')
                ++firstDigit;

            const int width = lastDigit - firstDigit + 1;
            const int oldValue = std::atoi(sourceText + firstDigit);
            char formattedNumber[0x80];
            std::memset(formattedNumber, 0, sizeof(formattedNumber));
            std::sprintf(formattedNumber, "%0*i", width, oldValue + delta);
            std::strncpy(tempOwner + firstDigit, formattedNumber, static_cast<std::size_t>(width));
        }

        if (*tempOwner != '\0')
        {
            const std::size_t textLen = std::strlen(tempOwner);
            char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
            destination.m_text = owner;
            std::memcpy(owner, tempOwner, textLen);
            owner[textLen] = '\0';
        }
        else
        {
            destination.m_text = STRING::SharedEmptyText();
        }

        releaseStringBuffer(tempOwner);
        return destination;
    }

    STRING& STRING::TrimRight(const char* chars)
    {
        return trimStringRight(*this, chars);
    }

    int STRING::ReplaceFirst(const char* search, const char* replacement)
    {
        return replaceStringFirst(*this, search, replacement);
    }

    STRING STRING::LeftOfFirst(const char* marker) const
    {
        STRING out;
        constructLeftOfFirstMarker(*this, out, marker);
        return out;
    }

    STRING STRING::RightOfFirst(const char* marker) const
    {
        STRING out;
        constructRightOfFirstMarker(*this, out, marker);
        return out;
    }

    STRING STRING::LeftOfLast(const char* marker) const
    {
        STRING out;
        constructLeftOfLastMarker(*this, out, marker);
        return out;
    }

    STRING STRING::RightOfLast(const char* marker) const
    {
        STRING out;
        constructRightOfLastMarker(*this, out, marker);
        return out;
    }

    STRING STRING::IncrementTrailingNumber(int delta) const
    {
        STRING out;
        incrementTrailingNumberString(*this, out, delta);
        return out;
    }

    STRING& splitRegistryRootPath(const STRING& source, STRING& destination, DWORD* rootKey)
    {

        struct RegistryPrefix
        {
            const char* prefix;
            DWORD root;
        };

        static const RegistryPrefix prefixes[] = {
            {"HKEY_USERS\\", 0x80000003u},
            {"HKEY_CURRENT_USER\\", 0x80000001u},
            {"HKEY_CLASSES_ROOT\\", 0x80000000u},
            {"HKEY_CURRENT_CONFIG\\", 0x80000005u},
            {"HKEY_LOCAL_MACHINE\\", 0x80000002u},
        };

        const char* sourceText = source.m_text;
        for (const RegistryPrefix& item : prefixes)
        {
            const std::size_t prefixLen = std::strlen(item.prefix);
            if (std::strncmp(sourceText, item.prefix, prefixLen) == 0)
            {
                *rootKey = item.root;
                return constructRightOfFirstMarker(source, destination, "\\");
            }
        }

        *rootKey = 0x80000002u;

        if (*sourceText == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(sourceText);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, sourceText, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    STRING STRING::ExtractRegistrySubkey(DWORD* rootKey) const
    {
        STRING out;
        splitRegistryRootPath(*this, out, rootKey);
        return out;
    }

    STRING STRING::ReadRegistryString(const STRING& valueName, const STRING& defaultValue) const
    {

#if defined(_WIN32)
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);

        auto readSupportedValue = [&](HKEY queryRoot, STRING& output) -> bool
        {
            DWORD type = 0;
            DWORD cbData = 0x1FFu;
            if (!queryRegistryValue(queryRoot, subKey.c_str(), valueName.c_str(), type, cbData))
                return false;

            if (type == REG_DWORD || type == REG_BINARY)
            {
                DWORD raw = 0;
                std::memcpy(&raw, gRegistryData, sizeof(raw));
#if defined(_MSC_VER)
                ::_itoa(static_cast<int>(raw), reinterpret_cast<char*>(gRegistryData), 10);
#else
                std::sprintf(reinterpret_cast<char*>(gRegistryData), "%d", static_cast<int>(raw));
#endif
                output = gRegistryData[0] ? STRING(reinterpret_cast<const char*>(gRegistryData)) : STRING();
                return true;
            }
            if (type == REG_SZ)
            {
                output = gRegistryData[0] ? STRING(reinterpret_cast<const char*>(gRegistryData)) : STRING();
                return true;
            }
            return false;
        };

        STRING result;
        HKEY retailRoot = reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root));
        if (readSupportedValue(retailRoot, result))
            return result;
        return defaultValue.isEmpty() ? STRING() : STRING(defaultValue);
#else
        (void)valueName;
        return defaultValue.isEmpty() ? STRING() : STRING(defaultValue);
#endif
    }

    int STRING::ReadRegistryInt(const STRING& valueName, int defaultValue) const
    {

#if defined(_WIN32)
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);

        auto readSupportedValue = [&](HKEY queryRoot, int& output) -> bool
        {
            DWORD type = 0;
            DWORD cbData = 0x1FFu;
            if (!queryRegistryValue(queryRoot, subKey.c_str(), valueName.c_str(), type, cbData))
                return false;
            if (type == REG_SZ)
            {
                output = std::atoi(reinterpret_cast<const char*>(gRegistryData));
                return true;
            }
            if (type == REG_DWORD || type == REG_BINARY)
            {
                DWORD raw = 0;
                std::memcpy(&raw, gRegistryData, sizeof(raw));
                output = static_cast<int>(raw);
                return true;
            }
            return false;
        };

        int result = defaultValue;
        HKEY retailRoot = reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root));
        if (readSupportedValue(retailRoot, result))
            return result;
        return defaultValue;
#else
        (void)valueName;
        return defaultValue;
#endif
    }


    int readRegistryInt(const STRING& registryPath, const STRING& valueName, int defaultValue)
    {

        return registryPath.ReadRegistryInt(valueName, defaultValue);
    }

    void STRING::WriteRegistryString(const STRING& valueName, const STRING& value) const
    {

#if defined(_WIN32)
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);
        const char* raw = value.c_str();
        const DWORD cbData = static_cast<DWORD>(std::strlen(raw) + 1);
        HKEY retailRoot = reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root));

        (void)writeRegistryValue(retailRoot, subKey.c_str(), valueName.c_str(), REG_SZ,
                               reinterpret_cast<const BYTE*>(raw), cbData);
#else
        (void)valueName;
        (void)value;
#endif
    }

    void STRING::WriteRegistryInt(const STRING& valueName, int value) const
    {

#if defined(_WIN32)
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);
        DWORD data = static_cast<DWORD>(value);
        HKEY retailRoot = reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root));

        (void)writeRegistryValue(retailRoot, subKey.c_str(), valueName.c_str(), REG_DWORD,
                               reinterpret_cast<const BYTE*>(&data), 4u);
#else
        (void)valueName;
        (void)value;
#endif
    }

    void STRING::DeleteRegistryValue(const STRING& valueName) const
    {

#if defined(_WIN32)
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);
        HKEY retailRoot = reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root));
        deleteRegistryValue(retailRoot, subKey.c_str(), valueName.c_str());
#else
        (void)valueName;
#endif
    }

    int convertStringToWideChars(const STRING& source, wchar_t* out, int count)
    {

#if defined(_WIN32)
        return ::MultiByteToWideChar(0, 0, source.m_text, -1, out, count);
#else
        if (!out || count <= 0)
            return 0;
        const std::size_t converted = std::mbstowcs(out, source.m_text, static_cast<std::size_t>(count));
        if (converted == static_cast<std::size_t>(-1))
            return 0;
        if (converted < static_cast<std::size_t>(count))
            return static_cast<int>(converted + 1);
        out[count - 1] = L'\0';
        return count;
#endif
    }

    int resetAndAssignString(STRING& destination, const STRING& source)
    {

        assignStringFromCString(destination, "");
        assignStringFromString(destination, source);
        return 0;
    }

    STRING& copyConstructString(STRING& destination, const STRING& source)
    {

        const char* sourceText = source.m_text;
        if (*sourceText == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(sourceText);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, sourceText, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    int STRING::ToWideChar(wchar_t* out, int count) const
    {
        return convertStringToWideChars(*this, out, count);
    }

#if defined(_WIN32)
    int STRING::ReadClipboardText(HWND ownerWindow)
    {
        if (!::OpenClipboard(ownerWindow))
            return 1;

        HANDLE const clipboardData = ::GetClipboardData(CF_TEXT);
        if (!clipboardData)
        {
            ::CloseClipboard();
            return 1;
        }

        void* const locked = ::GlobalLock(clipboardData);
        if (!locked)
        {
            ::GlobalUnlock(clipboardData);
            ::CloseClipboard();
            return 1;
        }

        if (m_text != SharedEmptyText())
            ::operator delete(m_text);

        const SIZE_T payloadSize = ::GlobalSize(clipboardData);
        const SIZE_T allocationSize = (payloadSize & ~static_cast<SIZE_T>(0x0Fu)) + 0x10u;
        auto* const replacement = static_cast<char*>(::operator new(allocationSize));
        m_text = replacement;
        std::memcpy(replacement, locked, payloadSize);
        replacement[payloadSize] = '\0';

        ::GlobalUnlock(clipboardData);
        ::CloseClipboard();
        return 0;
    }

    int STRING::WriteClipboardText(HWND ownerWindow) const
    {
        const std::size_t length = std::strlen(m_text ? m_text : SharedEmptyText());
        if (!::OpenClipboard(ownerWindow))
            return 1;

        ::EmptyClipboard();
        int result = 0;
        if (length != 0)
        {
            const SIZE_T bytes = static_cast<SIZE_T>(length + 1u);
            HGLOBAL const memory = ::GlobalAlloc(0x2042u, bytes);
            if (!memory)
            {
                result = 1;
            }
            else
            {
                void* const destination = ::GlobalLock(memory);
                if (!destination)
                {
                    result = 1;
                }
                else
                {
                    std::memcpy(destination, m_text, bytes);
                    ::GlobalUnlock(memory);
                    if (!::SetClipboardData(CF_TEXT, memory))
                        result = 1;
                }
            }
        }

        ::CloseClipboard();
        return result;
    }
#endif

    int STRING::ResetAndAssign(const STRING& other)
    {
        return resetAndAssignString(*this, other);
    }

    int STRING::ReadProfileInt(const STRING& section, const STRING& key, int defaultValue) const
    {
#if defined(_WIN32)
        return static_cast<int>(::GetPrivateProfileIntA(section.c_str(), key.c_str(), defaultValue, c_str()));
#else
        (void)section;
        (void)key;
        return defaultValue;
#endif
    }

    STRING STRING::ReadProfileString(const STRING& section, const STRING& key, const STRING& defaultValue) const
    {
        char buffer[0x8000];
#if defined(_WIN32)
        ::GetPrivateProfileStringA(section.c_str(), key.c_str(), defaultValue.c_str(), buffer, 0x7FFFu, c_str());
#else
        (void)section;
        (void)key;
        copyCStringBounded(buffer, sizeof(buffer), defaultValue.c_str());
#endif
        return buffer[0] ? STRING(buffer) : STRING();
    }

    STRING STRING::ReadProfileSection(const STRING& section, const STRING& defaultValue) const
    {
#if defined(_WIN32)
        char buffer[0x8000] = {};
        const DWORD copied = ::GetPrivateProfileSectionA(section.c_str(), buffer, 0x7FFFu, c_str());
        if (copied == 0u)
            return defaultValue;

        for (char* cursor = buffer; cursor[0] != '\0' || cursor[1] != '\0'; ++cursor)
        {
            if (*cursor == '\0')
                *cursor = '\n';
        }
        return buffer[0] ? STRING(buffer) : STRING();
#else
        (void)section;
        return defaultValue;
#endif
    }


    std::string STRING::str() const
    {
        return std::string(c_str());
    }

    STRING STRING::Concat(const char* left, const char* right)
    {
        return STRING(left, right);
    }

    STRING& constructConcatenatedString(STRING& destination, const char* left, const char* right)
    {

        const std::size_t leftLen = std::strlen(left);
        const std::size_t rightLen = std::strlen(right);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(leftLen + rightLen)));
        destination.m_text = owner;
        if (leftLen != 0)
            std::memcpy(owner, left, leftLen);
        std::memcpy(owner + leftLen, right, rightLen + 1);
        return destination;
    }

    STRING& STRING::Assign(const STRING& other)
    {
        if (this == &other)
            return *this;

        const char* const sourceText = other.m_text;
        const std::size_t sourceLen = std::strlen(sourceText);
        char* const oldText = m_text;

        if (oldText == SharedEmptyText())
        {
            if (sourceLen == 0u)
                return *this;
            m_text = static_cast<char*>(::operator new(retailStringAllocationSize(sourceLen)));
        }
        else if (sourceLen == 0u)
        {
            ::operator delete(oldText);
            m_text = SharedEmptyText();
            return *this;
        }
        else
        {
            const std::size_t oldLen = std::strlen(oldText);
            if (!sameRetailStringBucket(oldLen, sourceLen))
            {
                ::operator delete(oldText);
                m_text = static_cast<char*>(::operator new(retailStringAllocationSize(sourceLen)));
            }
        }

        std::memcpy(m_text, sourceText, sourceLen + 1u);
        return *this;
    }

    STRING& assignStringFromString(STRING& destination, const STRING& source)
    {
        return destination.Assign(source);
    }

    STRING& assignStringFromCString(STRING& destination, const char* source)
    {
        return destination.Assign(source);
    }

    STRING& appendStringOwner(STRING& destination, const STRING& source)
    {
        return destination.Append(source);
    }

    STRING& appendCStringToString(STRING& destination, const char* source)
    {
        return destination.Append(source);
    }

    STRING& constructFormattedString(STRING& destination, const char* format, ...)
    {

        char formattedText[0x1000];
        formattedText[0] = '\0';
        std::memset(formattedText + 1, 0, sizeof(formattedText) - 1);

        va_list args;
        va_start(args, format);
#if defined(_MSC_VER)
        ::vsprintf(formattedText, format, args);
#else
        std::vsprintf(formattedText, format, args);
#endif
        va_end(args);

        if (formattedText[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(formattedText);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, formattedText, textLen);
        owner[textLen] = '\0';
        return destination;
    }


    STRING& constructLeftOfFirstMarker(const STRING& source, STRING& destination, const char* marker)
    {

        const char* text = source.m_text;
        const char* found = std::strstr(text, marker);
        std::size_t textLen = 0;

        if (found)
        {
            textLen = static_cast<std::size_t>(found - text);
        }
        else if (*text != '\0')
        {
            textLen = std::strlen(text);
        }
        else
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        if (textLen == 0)
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, text, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    STRING& constructRightOfFirstMarker(const STRING& source, STRING& destination, const char* marker)
    {

        const char* text = source.m_text;
        const char* found = std::strstr(text, marker);
        if (!found)
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const char* right = found + std::strlen(marker);
        if (*right == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(right);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, right, textLen);
        owner[textLen] = '\0';
        return destination;
    }


    STRING& constructLeftOfLastMarker(const STRING& source, STRING& destination, const char* marker)
    {

        const char* text = source.m_text;
        const char* last = nullptr;

        const char* cursor = std::strstr(text, marker);
        while (cursor)
        {
            last = cursor;
            cursor = std::strstr(cursor + 1, marker);
        }

        std::size_t textLen = 0;
        if (last)
        {
            textLen = static_cast<std::size_t>(last - text);
        }
        else if (*text != '\0')
        {
            textLen = std::strlen(text);
        }
        else
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        if (textLen == 0)
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, text, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    STRING& constructRightOfLastMarker(const STRING& source, STRING& destination, const char* marker)
    {

        const char* text = source.m_text;
        const char* last = nullptr;

        const char* cursor = std::strstr(text, marker);
        while (cursor)
        {
            last = cursor;
            cursor = std::strstr(cursor + 1, marker);
        }

        if (!last)
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const char* right = last + std::strlen(marker);
        if (*right == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(right);
        char* owner = static_cast<char*>(::operator new(retailStringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, right, textLen);
        owner[textLen] = '\0';
        return destination;
    }

    STRING& STRING::Assign(const char* text)
    {
        text = safeCString(text);
        const std::size_t newLen = std::strlen(text);
        char* const oldText = m_text;

        if (oldText == SharedEmptyText())
        {
            if (newLen == 0u)
                return *this;
            m_text = static_cast<char*>(::operator new(retailStringAllocationSize(newLen)));
        }
        else if (newLen == 0u)
        {
            ::operator delete(oldText);
            m_text = SharedEmptyText();
            return *this;
        }
        else
        {
            const std::size_t oldLen = std::strlen(oldText);
            if (!sameRetailStringBucket(oldLen, newLen))
            {
                ::operator delete(oldText);
                m_text = static_cast<char*>(::operator new(retailStringAllocationSize(newLen)));
            }
        }

        std::memcpy(m_text, text, newLen + 1u);
        return *this;
    }

    STRING& STRING::AssignConcat(const char* left, const char* right)
    {

        releaseStringBuffer(m_text);
        m_text = SharedEmptyText();
        return constructConcatenatedString(*this, left, right);
    }

    STRING& STRING::Append(const STRING& other)
    {
        const char* sourceText = other.m_text;
        if (*sourceText == '\0')
            return *this;

        char* const oldText = m_text;
        const std::size_t oldLen = std::strlen(oldText);
        const std::size_t sourceLen = std::strlen(sourceText);
        const std::size_t combinedLen = oldLen + sourceLen;

        if (oldText == SharedEmptyText() || !sameRetailStringBucket(oldLen, combinedLen))
        {
            char* const replacement = static_cast<char*>(::operator new(retailStringAllocationSize(combinedLen)));
            if (oldLen != 0u)
                std::memcpy(replacement, oldText, oldLen);
            m_text = replacement;
            if (oldText != SharedEmptyText())
                ::operator delete(oldText);
        }

        sourceText = other.m_text;
        std::memcpy(m_text + oldLen, sourceText, sourceLen + 1u);
        return *this;
    }

    STRING& STRING::Append(const char* text)
    {
        text = safeCString(text);
        if (*text == '\0')
            return *this;

        char* const oldText = m_text;
        const std::size_t oldLen = std::strlen(oldText);
        const std::size_t sourceLen = std::strlen(text);
        const std::size_t combinedLen = oldLen + sourceLen;

        if (oldText == SharedEmptyText() || !sameRetailStringBucket(oldLen, combinedLen))
        {
            char* const replacement = static_cast<char*>(::operator new(retailStringAllocationSize(combinedLen)));
            if (oldLen != 0u)
                std::memcpy(replacement, oldText, oldLen);
            m_text = replacement;
            if (oldText != SharedEmptyText())
                ::operator delete(oldText);
        }

        std::memcpy(m_text + oldLen, text, sourceLen + 1u);
        return *this;
    }

    STRING& STRING::operator+=(const STRING& other)
    {
        return Append(other);
    }

    STRING& STRING::operator+=(const char* text)
    {
        return Append(text);
    }
}
