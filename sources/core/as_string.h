#pragma once
#include "types.h"
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <iosfwd>
#include <string>
#include <cwchar>

namespace as1
{
    class BaseStream;

    class STRING
    {
    public:
        STRING();
        STRING(const char* s);
        STRING(std::string s);
        STRING(const char* left, const char* right);
        STRING(const STRING& other);
        STRING(STRING&& other) noexcept;
        ~STRING();

        STRING& operator=(const STRING& other);
        STRING& operator=(STRING&& other) noexcept;
        STRING& operator=(const char* text);
        STRING& operator=(const std::string& text);

        const char* c_str() const;
        const char* CharPtr() const;
        bool isEmpty() const;
        int Length() const;
        int Int() const;
        STRING ToLower() const;
        STRING ToUpper() const;
        STRING ToBase64(int add) const;
        void AssignBytes(const void* data, std::size_t size);

        void Read(BaseStream* stream);
        void Write(BaseStream* stream) const;

        STRING& ReadLine(FILE* file);

        STRING& ReadLine(BaseStream* stream);

        int LoadFile(const STRING& path);
        int LoadFile(const char* path);

        static STRING Format(const char* format, ...);
        static STRING FormatV(const char* format, va_list args);
        STRING& AssignFormat(const char* format, ...);
        STRING& AssignFormatV(const char* format, va_list args);

        std::string str() const;

        static STRING Concat(const char* left, const char* right);

        STRING& Assign(const STRING& other);
        STRING& Assign(const char* text);
        STRING& AssignConcat(const char* left, const char* right);

        STRING& Append(const STRING& other);
        STRING& Append(const char* text);
        STRING& operator+=(const STRING& other);
        STRING& operator+=(const char* text);

        static STRING CurrentTimeString();
        static STRING CurrentDateString();

        static STRING TemporaryName(const char* directory, const char* prefix);

        STRING& TrimRight(const char* chars);
        int ReplaceFirst(const char* search, const char* replacement);

        STRING LeftOfFirst(const char* marker) const;
        STRING RightOfFirst(const char* marker) const;
        STRING LeftOfLast(const char* marker) const;
        STRING RightOfLast(const char* marker) const;
        STRING IncrementTrailingNumber(int delta) const;

        STRING ExtractRegistrySubkey(DWORD* rootKey) const;
        STRING ReadRegistryString(const STRING& valueName, const STRING& defaultValue) const;
        int ReadRegistryInt(const STRING& valueName, int defaultValue) const;
        void WriteRegistryString(const STRING& valueName, const STRING& value) const;
        void WriteRegistryInt(const STRING& valueName, int value) const;
        void DeleteRegistryValue(const STRING& valueName) const;
        int ToWideChar(wchar_t* out, int count) const;
        int ReadProfileInt(const STRING& section, const STRING& key, int defaultValue) const;
        STRING ReadProfileString(const STRING& section, const STRING& key, const STRING& defaultValue) const;
        STRING ReadProfileSection(const STRING& section, const STRING& defaultValue) const;
        int ResetAndAssign(const STRING& other);

#if defined(_WIN32)
        int ReadClipboardText(HWND ownerWindow);
        int WriteClipboardText(HWND ownerWindow) const;
#endif

        static char* SharedEmptyText();

        friend char* constructStringFromBytes(STRING& destination, const void* source, std::size_t length);
        friend STRING& constructConcatenatedString(STRING& destination, const char* left, const char* right);
        friend STRING& assignStringFromString(STRING& destination, const STRING& source);
        friend STRING& assignStringFromCString(STRING& destination, const char* source);
        friend STRING& appendStringOwner(STRING& destination, const STRING& source);
        friend STRING& appendCStringToString(STRING& destination, const char* source);
        friend STRING& readStringLineFromFile(STRING& destination, FILE* file);
        friend STRING& readStringLineFromStream(STRING& destination, BaseStream* stream);
        friend int loadStringFromFile(STRING& destination, const STRING* pathOwner);
        friend STRING& constructFormattedString(STRING& destination, const char* format, ...);
        friend STRING& constructCurrentTimeString(STRING& destination);
        friend STRING& constructCurrentDateString(STRING& destination);
        friend STRING& constructTemporaryNameString(STRING& destination, const char* directory, const char* prefix);
        friend STRING& trimStringRight(STRING& destination, const char* chars);
        friend int replaceStringFirst(STRING& destination, const char* search, const char* replacement);
        friend STRING& incrementTrailingNumberString(const STRING& source, STRING& destination, int delta);
        friend STRING& splitRegistryRootPath(const STRING& source, STRING& destination, DWORD* rootKey);
        friend int convertStringToWideChars(const STRING& source, wchar_t* out, int count);
        friend int resetAndAssignString(STRING& destination, const STRING& source);
        friend STRING& copyConstructString(STRING& destination, const STRING& source);
        friend STRING& constructStringFromCString(STRING& destination, const char* source);
        friend void destroyStringStorage(STRING& value);
        friend STRING& constructLeftOfFirstMarker(const STRING& source, STRING& destination, const char* marker);
        friend STRING& constructRightOfFirstMarker(const STRING& source, STRING& destination, const char* marker);
        friend STRING& constructLeftOfLastMarker(const STRING& source, STRING& destination, const char* marker);
        friend STRING& constructRightOfLastMarker(const STRING& source, STRING& destination, const char* marker);

        void ReleaseOwnedStorage();

        // Ownership transfer for one-word STRING temporaries at ABI call boundaries.
        // These helpers transfer the character buffer without copying it.
        char* DetachOwnedStorage() noexcept;
        void AdoptOwnedStorage(char* owner) noexcept;

        void ResetSharedEmptyWithoutRelease() noexcept;

        void AssignAllocatedCopyWithoutRelease(const char* text);

    private:
        char* m_text;
    };

    char* constructStringFromBytes(STRING& destination, const void* source, std::size_t length);
    STRING& constructConcatenatedString(STRING& destination, const char* left, const char* right);
    STRING& assignStringFromString(STRING& destination, const STRING& source);
    STRING& assignStringFromCString(STRING& destination, const char* source);
    STRING& appendStringOwner(STRING& destination, const STRING& source);
    STRING& appendCStringToString(STRING& destination, const char* source);
    STRING& readStringLineFromFile(STRING& destination, FILE* file);
    STRING& readStringLineFromStream(STRING& destination, BaseStream* stream);
    int loadStringFromFile(STRING& destination, const STRING* pathOwner);
    STRING& constructFormattedString(STRING& destination, const char* format, ...);
    STRING& constructCurrentTimeString(STRING& destination);
    STRING& constructCurrentDateString(STRING& destination);
    STRING& constructFileTimestampString(STRING& destination, const STRING& fileName);
    STRING& constructTemporaryNameString(STRING& destination, const char* directory, const char* prefix);
    STRING& trimStringRight(STRING& destination, const char* chars);
    int replaceStringFirst(STRING& destination, const char* search, const char* replacement);
    STRING& incrementTrailingNumberString(const STRING& source, STRING& destination, int delta);
    STRING& splitRegistryRootPath(const STRING& source, STRING& destination, DWORD* rootKey);
    STRING& constructLeftOfFirstMarker(const STRING& source, STRING& destination, const char* marker);
    STRING& constructRightOfFirstMarker(const STRING& source, STRING& destination, const char* marker);
    STRING& constructLeftOfLastMarker(const STRING& source, STRING& destination, const char* marker);
    STRING& constructRightOfLastMarker(const STRING& source, STRING& destination, const char* marker);
    STRING& copyConstructString(STRING& destination, const STRING& source);
    STRING& constructStringFromCString(STRING& destination, const char* source);
    void destroyStringStorage(STRING& value);
    int readRegistryInt(const STRING& registryPath, const STRING& valueName, int defaultValue);
}
