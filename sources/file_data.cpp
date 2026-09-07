#include "file_data.h"
#include "core/application.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace as1
{
    namespace
    {
        struct ResolvedFileDataPath
        {
            const char* fileName;
            std::string entryPath;
        };

        ResolvedFileDataPath resolveFileDataPath(const STRING& input)
        {
            std::string path = input.c_str() ? input.c_str() : "";
            for (std::size_t at = path.find('/'); at != std::string::npos; at = path.find('/', at))
                path.erase(at, 1u);

            const char* fileName = "saves\\save.ini";
            if (path.compare(0u, 10u, "options:\\") == 0)
            {
                fileName = "saves\\options.ini";
                path.erase(0u, 10u);
            }
            else if (path.compare(0u, 7u, "save:\\") == 0)
            {
                path.erase(0u, 7u);
            }

            return {fileName, std::move(path)};
        }

        std::string loadWholeFile(const char* fileName)
        {
            std::FILE* file = std::fopen(fileName, "rb");
            if (!file)
                return {};

            if (std::fseek(file, 0, SEEK_END) != 0)
            {
                std::fclose(file);
                return {};
            }
            const long length = std::ftell(file);
            if (length <= 0 || std::fseek(file, 0, SEEK_SET) != 0)
            {
                std::fclose(file);
                return {};
            }

            std::string contents(static_cast<std::size_t>(length), '\0');
            char* const contentsData = contents.empty() ? nullptr : &contents[0];
            const std::size_t read = std::fread(contentsData, 1u, contents.size(), file);
            std::fclose(file);
            contents.resize(read);
            return contents;
        }

        void storeWholeFile(const char* fileName, const std::string& contents)
        {
            std::FILE* file = std::fopen(fileName, "wb");
            if (!file)
                return;
            if (!contents.empty())
                std::fwrite(contents.data(), 1u, contents.size(), file);
            std::fclose(file);
        }

        bool isLineStart(const std::string& text, std::size_t at) noexcept
        {
            return at == 0u || text[at - 1u] == '\r' || text[at - 1u] == '\n';
        }

        void splitEntryPath(const std::string& entryPath, std::string& section, std::string& key)
        {
            const std::size_t slash = entryPath.find('\\');
            if (slash == std::string::npos)
            {
                section = entryPath;
                key.clear();
                return;
            }
            section.assign(entryPath.data(), slash);
            key.assign(entryPath.data() + slash + 1u, entryPath.size() - slash - 1u);
        }

        std::size_t findLineToken(const std::string& text,
                                  const std::string& token,
                                  std::size_t begin,
                                  std::size_t end)
        {
            for (std::size_t at = text.find(token, begin);
                 at != std::string::npos && at < end;
                 at = text.find(token, at + token.size()))
            {
                if (isLineStart(text, at))
                    return at;
            }
            return std::string::npos;
        }

        std::size_t findNextSection(const std::string& text, std::size_t begin)
        {
            for (std::size_t at = text.find('[', begin);
                 at != std::string::npos;
                 at = text.find('[', at + 1u))
            {
                if (isLineStart(text, at))
                    return at;
            }
            return text.size();
        }

        std::string readRetailIniValue(const std::string& contents,
                                       const std::string& entryPath,
                                       const char* defaultValue)
        {
            std::string section;
            std::string key;
            splitEntryPath(entryPath, section, key);

            const std::string sectionHeader = "[" + section + "]";
            const std::string keyPrefix = key + "=";
            const std::size_t sectionAt = findLineToken(contents, sectionHeader, 0u, contents.size());
            if (sectionAt == std::string::npos)
                return defaultValue ? defaultValue : "";

            const std::size_t sectionBody = sectionAt + sectionHeader.size();
            const std::size_t sectionEnd = findNextSection(contents, sectionBody);
            const std::size_t keyAt = findLineToken(contents, keyPrefix, sectionBody, sectionEnd);
            if (keyAt == std::string::npos)
                return defaultValue ? defaultValue : "";

            std::size_t valueAt = keyAt + keyPrefix.size();
            while (valueAt < sectionEnd && (contents[valueAt] == '=' || contents[valueAt] == ' '))
                ++valueAt;

            std::size_t valueEnd = contents.find("\r\n", valueAt);
            if (valueEnd == std::string::npos || valueEnd > sectionEnd)
                valueEnd = sectionEnd;
            return contents.substr(valueAt, valueEnd - valueAt);
        }

        void writeRetailIniValue(std::string& contents,
                                 const std::string& entryPath,
                                 const char* valueText)
        {
            std::string section;
            std::string key;
            splitEntryPath(entryPath, section, key);

            const std::string sectionHeader = "[" + section + "]";
            const std::string keyPrefix = key + "=";
            const std::string value = valueText ? valueText : "";
            const std::size_t sectionAt = findLineToken(contents, sectionHeader, 0u, contents.size());

            if (sectionAt == std::string::npos)
            {
                contents += sectionHeader;
                contents += "\r\n";
                contents += keyPrefix;
                contents += value;
                contents += "\r\n";
                return;
            }

            const std::size_t sectionBody = sectionAt + sectionHeader.size();
            const std::size_t sectionEnd = findNextSection(contents, sectionBody);
            const std::size_t keyAt = findLineToken(contents, keyPrefix, sectionBody, sectionEnd);

            if (keyAt != std::string::npos)
            {
                std::size_t lineEnd = contents.find("\r\n", keyAt);
                const bool hasCrLf = lineEnd != std::string::npos;
                if (!hasCrLf)
                    lineEnd = contents.size();

                std::string replacement = keyPrefix + value;
                if (hasCrLf)
                    replacement += contents.substr(lineEnd);
                else
                    replacement += "\r\n";
                contents.replace(keyAt, contents.size() - keyAt, replacement);
                return;
            }

            std::string insertion;
            if (sectionEnd != 0u && contents[sectionEnd - 1u] != '\r' && contents[sectionEnd - 1u] != '\n')
                insertion += "\r\n";
            insertion += keyPrefix;
            insertion += value;
            insertion += "\r\n";
            contents.insert(sectionEnd, insertion);
        }
    }

    void FileDataSave(const STRING& path, const STRING& value)
    {
        const ResolvedFileDataPath resolved = resolveFileDataPath(path);
        std::string contents = loadWholeFile(resolved.fileName);
        writeRetailIniValue(contents, resolved.entryPath, value.c_str());
        storeWholeFile(resolved.fileName, contents);
    }

    STRING FileDataLoad(const STRING& path, const STRING& defaultValue)
    {
        const ResolvedFileDataPath resolved = resolveFileDataPath(path);
        const std::string contents = loadWholeFile(resolved.fileName);
        return STRING(readRetailIniValue(contents, resolved.entryPath, defaultValue.c_str()).c_str());
    }

    int FileDataFileExists(const STRING& filename)
    {
        std::FILE* file = std::fopen(filename.c_str(), "rb");
        if (!file)
            return 0;
        std::fclose(file);
        return 1;
    }

    const char* FileDataSaveFolder() noexcept
    {
        // Native 189 pushes the physical Application/MAP owner STRING at +0x18.
        return core::ApplicationSavePath().c_str();
    }
}
