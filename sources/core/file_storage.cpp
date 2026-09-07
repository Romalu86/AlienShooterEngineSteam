#include "core/file_storage.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace as1 { namespace core
{
    namespace
    {
        bool isSlash(char ch) noexcept { return ch == '\\' || ch == '/'; }

        std::string textOf(const STRING& value)
        {
            return value.c_str() ? value.str() : std::string();
        }

        std::string lowerCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }
    }

    bool FileStorage::IsAbsolutePath(const STRING& path)
    {
        const std::string text = textOf(path);
        if (text.size() >= 3 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':' && isSlash(text[2]))
            return true;
        return !text.empty() && isSlash(text[0]);
    }

    bool FileStorage::HasExtension(const STRING& path, const char* extension)
    {
        if (!extension || !*extension)
            return false;
        const std::string text = lowerCopy(textOf(path));
        std::string ext = lowerCopy(extension);
        if (!ext.empty() && ext[0] != '.')
            ext.insert(ext.begin(), '.');
        if (text.size() < ext.size())
            return false;
        return text.compare(text.size() - ext.size(), ext.size(), ext) == 0;
    }

    STRING FileStorage::JoinPath(const STRING& left, const STRING& right)
    {
        if (right.isEmpty())
            return left;
        if (IsAbsolutePath(right) || left.isEmpty())
            return right;
        std::string l = textOf(left);
        const std::string r = textOf(right);
        if (!l.empty() && !isSlash(l.back()))
            l.push_back('\\');
        l += r;
        return STRING(l);
    }

    STRING FileStorage::FileStem(const STRING& path)
    {
        std::string text = textOf(path);
        std::size_t slash = text.find_last_of("\\/");
        std::size_t begin = (slash == std::string::npos) ? 0 : slash + 1;
        std::size_t dot = text.find_last_of('.');
        if (dot == std::string::npos || dot < begin)
            dot = text.size();
        const std::string stem = text.substr(begin, dot - begin);
        return stem.empty() ? path : STRING(stem);
    }

    STRING FileStorage::DirectoryName(const STRING& path)
    {
        const std::string text = textOf(path);
        const std::size_t slash = text.find_last_of("\\/");
        if (slash == std::string::npos)
            return STRING(".");
        if (slash == 0)
            return STRING(text.substr(0, 1));
        return STRING(text.substr(0, slash));
    }

    bool FileStorage::ExistsForRead(const STRING& path)
    {
        if (path.isEmpty())
            return false;
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (!file)
            return false;
        std::fclose(file);
        return true;
    }
} }
