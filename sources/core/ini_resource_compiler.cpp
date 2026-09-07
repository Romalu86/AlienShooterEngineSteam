#include "ini_resource_compiler.h"
#include "file_logger.h"
#include "log.h"

#include <cctype>
#include <cstdarg>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace as1
{
    namespace
    {
        enum RetailIniType : std::uint16_t
        {
            IniChar = 0,
            IniByte = 1,
            IniWord = 2,
            IniDword = 4,
            IniFile = 5,
            IniString = 6,
            IniFloat = 7,
        };

        struct IniFieldRecord
        {
            STRING name;
            std::uint16_t type = 0;
            std::uint16_t count = 1;
        };

        struct IniConstantRecord
        {
            STRING name;
            STRING text;
            int value = 0;
        };

        template <typename T>
        class RetailRawList
        {
        public:
            RetailRawList() = default;
            RetailRawList(const RetailRawList&) = delete;
            RetailRawList& operator=(const RetailRawList&) = delete;

            ~RetailRawList()
            {
                clearStorage();
            }

            void clear()
            {
                for (int i = 0; i < m_count; ++i)
                    m_data[i].~T();
                m_count = 0;
            }

            int count() const noexcept { return m_count; }
            T& operator[](int index) noexcept { return m_data[index]; }
            const T& operator[](int index) const noexcept { return m_data[index]; }

            T& append()
            {
                if (m_count >= m_capacity)
                    reserveExact(m_capacity * 2 + 4);
                T* const slot = &m_data[m_count++];
                new (slot) T();
                return *slot;
            }

            int findReverse(const char* name) const noexcept
            {
                for (int i = m_count - 1; i >= 0; --i)
                {
                    if (std::strcmp(m_data[i].name.c_str(), name ? name : "") == 0)
                        return i;
                }
                return -1;
            }

        private:
            void reserveExact(int capacity)
            {
                if (capacity <= m_capacity)
                    return;

                void* const raw = ::operator new(sizeof(T) * static_cast<std::size_t>(capacity), std::nothrow);
                if (!raw)
                {
                    if (g_fileLogger)
                        g_fileLogger->Fatal("!!!ERROR!!!::LIST: Not enough memory %i", capacity);
                    std::abort();
                }

                T* const replacement = static_cast<T*>(raw);
                for (int i = 0; i < m_count; ++i)
                {
                    new (&replacement[i]) T();
                    copyRecord(replacement[i], m_data[i]);
                }
                for (int i = 0; i < m_count; ++i)
                    m_data[i].~T();
                ::operator delete(m_data);
                m_data = replacement;
                m_capacity = capacity;
            }

            static void copyRecord(IniFieldRecord& dst, const IniFieldRecord& src)
            {
                dst.name.Assign(src.name);
                dst.type = src.type;
                dst.count = src.count;
            }

            static void copyRecord(IniConstantRecord& dst, const IniConstantRecord& src)
            {
                dst.name.Assign(src.name);
                dst.text.Assign(src.text);
                dst.value = src.value;
            }

            void clearStorage()
            {
                clear();
                ::operator delete(m_data);
                m_data = nullptr;
                m_capacity = 0;
            }

            int m_count = 0;
            int m_capacity = 0;
            T* m_data = nullptr;
        };

        char* trimLeft(char* text) noexcept
        {
            if (!text)
                return text;
            while (*text && std::strchr(" \n\r\t", *text))
                ++text;
            return text;
        }

        void trimRight(char* text) noexcept
        {
            if (!text)
                return;
            std::size_t length = std::strlen(text);
            while (length && std::strchr(" \n\r\t", text[length - 1]))
                text[--length] = '\0';
        }

        void trim(char*& text) noexcept
        {
            text = trimLeft(text);
            trimRight(text);
        }

        void stripComment(char* text) noexcept
        {
            if (char* const semi = std::strchr(text, ';'))
                *semi = '\0';
        }

        bool startsWithToken(const char* text, const char* token) noexcept
        {
            const std::size_t tokenLength = std::strlen(token);
            return std::strncmp(text, token, tokenLength) == 0;
        }

        int parseRetailInteger(const char* text) noexcept
        {
            if (!text)
                return 0;
            if (text[0] != '\0' && text[1] == 'x')
            {
                int value = 0;
                (void)std::sscanf(text, "%i", &value);
                return value;
            }
            return std::atoi(text);
        }

        bool tokenStartsNumber(const char* text) noexcept
        {
            if (!text || !*text)
                return false;
            const unsigned char first = static_cast<unsigned char>(*text);
            if (std::isdigit(first))
                return true;
            return *text == '-' && std::isdigit(static_cast<unsigned char>(text[1]));
        }

        std::uint32_t fourccFromText(const char* text) noexcept
        {
            std::uint32_t result = 0;
            if (!text)
                return result;
            unsigned char* out = reinterpret_cast<unsigned char*>(&result);
            for (int i = 0; i < 4 && text[i]; ++i)
                out[i] = static_cast<unsigned char>(text[i]);
            return result;
        }

        const IniConstantRecord* findConstant(const RetailRawList<IniConstantRecord>& constants, const char* name) noexcept
        {
            const int index = constants.findReverse(name);
            return index >= 0 ? &constants[index] : nullptr;
        }

        void reportIni(const char* format, ...)
        {
            if (!g_fileLogger || !format)
                return;
            va_list args;
            va_start(args, format);
            g_fileLogger->WriteLineV(format, args);
            va_end(args);
        }

        char* takeToken(char*& text, const char* separators) noexcept
        {
            text = trimLeft(text);
            if (!*text)
                return text;
            char* begin = text;
            char* cursor = begin;
            while (*cursor && !std::strchr(separators, *cursor))
                ++cursor;
            if (*cursor)
            {
                *cursor++ = '\0';
                text = cursor;
            }
            else
            {
                text = cursor;
            }
            return begin;
        }

        int resolveIntegerToken(const char* token,
                                const RetailRawList<IniConstantRecord>& constants,
                                int elementIndex,
                                const IniFieldRecord& field)
        {
            if (tokenStartsNumber(token))
                return parseRetailInteger(token);
            if (const IniConstantRecord* const constant = findConstant(constants, token))
                return constant->value;
            if (token && *token)
                reportIni("Undefine constant '%s'", token);
            (void)elementIndex;
            (void)field;
            return 0;
        }

        int writeIntegerArray(RESOURCE& output,
                              char* valueText,
                              const IniFieldRecord& field,
                              const RetailRawList<IniConstantRecord>& constants,
                              int lineNumber)
        {
            char* cursor = valueText;
            for (unsigned int element = 0; element < field.count; ++element)
            {
                cursor = trimLeft(cursor);
                int value = 0;
                bool consumedAny = false;

                while (*cursor)
                {
                    char* token = cursor;
                    while (*cursor && *cursor != '+' && *cursor != '|' && *cursor != ' ' && *cursor != '\t')
                        ++cursor;
                    const char delimiter = *cursor;
                    if (*cursor)
                        *cursor++ = '\0';

                    if (*token)
                    {
                        value += resolveIntegerToken(token, constants, static_cast<int>(element), field);
                        consumedAny = true;
                    }

                    while (*cursor == ' ' || *cursor == '\t')
                        ++cursor;
                    if (delimiter != '+' && delimiter != '|')
                        break;
                    while (*cursor == '+' || *cursor == '|' || *cursor == ' ' || *cursor == '\t')
                        ++cursor;
                }

                if (!consumedAny && std::strcmp(field.name.c_str(), "Acceleration") != 0)
                    value = 0;

                const unsigned int width = field.type == IniByte ? 1u : (field.type == IniWord ? 2u : 4u);
                output.write(&value, width);

                cursor = trimLeft(cursor);
                if (element < 15u && !*cursor && element + 1u < field.count &&
                    std::strcmp(field.name.c_str(), "Acceleration") != 0)
                {
                    reportIni("INI:Not enough element %i in '%s[%i]' in %i line",
                              static_cast<int>(element), field.name.c_str(), static_cast<int>(field.count), lineNumber);
                }
            }
            return 0;
        }

        int writeFloatArray(RESOURCE& output,
                            char* valueText,
                            const IniFieldRecord& field,
                            const RetailRawList<IniConstantRecord>& constants,
                            int lineNumber)
        {
            char* cursor = valueText;
            for (unsigned int element = 0; element < field.count; ++element)
            {
                char* token = takeToken(cursor, " \t");
                float value = 0.0f;
                if (token && *token)
                {
                    if (!tokenStartsNumber(token))
                    {
                        if (const IniConstantRecord* const constant = findConstant(constants, token))
                            token = const_cast<char*>(constant->text.c_str());
                        else
                            reportIni("Undefine constant '%s'", token);
                    }
                    value = std::strtof(token, nullptr);
                }
                output.write(&value, 4);

                cursor = trimLeft(cursor);
                if (element < 15u && !*cursor && element + 1u < field.count &&
                    std::strcmp(field.name.c_str(), "Acceleration") != 0)
                {
                    reportIni("INI:Not enough element %i in '%s[%i] in %i line'",
                              static_cast<int>(element), field.name.c_str(), static_cast<int>(field.count), lineNumber);
                }
            }
            return 0;
        }

        int writeStringArray(RESOURCE& output,
                             char* valueText,
                             const IniFieldRecord& field,
                             const RetailRawList<IniConstantRecord>& constants)
        {
            char* cursor = valueText;
            for (unsigned int element = 0; element < field.count; ++element)
            {
                char* token = nullptr;
                if (field.count <= 1)
                {
                    token = cursor;
                    cursor += std::strlen(cursor);
                }
                else
                {
                    token = takeToken(cursor, " ");
                }
                if (!token)
                    token = const_cast<char*>("");

                const IniConstantRecord* const constant = findConstant(constants, token);
                const char* const outputText = constant ? constant->text.c_str() : token;
                output.write(outputText, static_cast<unsigned int>(std::strlen(outputText) + 1u));
            }
            return 0;
        }

        int writeFilePayload(RESOURCE& output, const char* path)
        {
            if (!path || !*path)
                return -1;
            std::FILE* const input = std::fopen(path, "rb");
            if (!input)
                return -1;
            std::fseek(input, 0, SEEK_END);
            const long size = std::ftell(input);
            std::fseek(input, 0, SEEK_SET);
            if (size <= 0)
            {
                std::fclose(input);
                return 0;
            }
            void* const memory = ::operator new(static_cast<std::size_t>(size), std::nothrow);
            if (!memory)
            {
                std::fclose(input);
                return -2;
            }
            (void)std::fread(memory, static_cast<std::size_t>(size), 1u, input);
            std::fclose(input);
            const int result = output.write(memory, static_cast<unsigned int>(size));
            ::operator delete(memory);
            return result;
        }

        bool parseDeclaration(char* line, RetailRawList<IniFieldRecord>& fields)
        {
            struct TypeWord { const char* word; std::uint16_t type; };
            static const TypeWord kTypes[] = {
                {"CHAR", IniChar}, {"STRING", IniString}, {"FLOAT", IniFloat},
                {"BYTE", IniByte}, {"DWORD", IniDword}, {"WORD", IniWord}, {"FILE", IniFile},
            };

            char* bracket = std::strchr(line, '[');
            unsigned int count = 1;
            if (bracket)
            {
                count = static_cast<unsigned int>(parseRetailInteger(bracket + 1));
                if (count == 0)
                    count = 1;
                *bracket = '\0';
                trimRight(line);
            }

            for (const TypeWord& entry : kTypes)
            {
                const std::size_t length = std::strlen(entry.word);
                if (std::strncmp(line, entry.word, length) != 0)
                    continue;
                if (line[length] != ' ' && line[length] != '\t')
                    continue;

                char* name = line + length;
                name = trimLeft(name);
                trimRight(name);
                IniFieldRecord& field = fields.append();
                field.name.Assign(name);
                field.type = entry.type;
                field.count = static_cast<std::uint16_t>(count);
                return true;
            }
            return false;
        }

        bool parseConstant(char* line, RetailRawList<IniConstantRecord>& constants)
        {
            if (!startsWithToken(line, "const ") && !startsWithToken(line, "const\t"))
                return false;
            char* cursor = line + 5;
            cursor = trimLeft(cursor);
            char* name = takeToken(cursor, " \n\r\t");
            cursor = trimLeft(cursor);
            IniConstantRecord& constant = constants.append();
            constant.name.Assign(name);
            constant.text.Assign(cursor);
            constant.value = parseRetailInteger(cursor);
            return true;
        }
    }

    int CompileIniToResource(RESOURCE& output, const STRING& inputPath)
    {
        if (!output.isOpen())
        {
            reportIni("Not open resouce file");
            return 2;
        }

        std::FILE* const input = std::fopen(inputPath.c_str(), "rt");
        if (!input)
        {
            reportIni("Can't open file '%s'", inputPath.c_str());
            return 1;
        }

        RetailRawList<IniFieldRecord> fields;
        RetailRawList<IniConstantRecord> constants;
        std::uint32_t currentType = 0;
        int previousField = -1;
        int lineNumber = 0;
        char lineStorage[0x1000];

        while (std::fgets(lineStorage, sizeof(lineStorage), input))
        {
            ++lineNumber;
            stripComment(lineStorage);
            char* line = lineStorage;
            trim(line);
            if (!*line)
                continue;

            if (*line == '[')
            {
                const char* const equals = std::strchr(line, '=');
                currentType = equals ? fourccFromText(trimLeft(const_cast<char*>(equals + 1))) : 0u;
                previousField = -1;
                fields.clear();
                constants.clear();
                continue;
            }

            if (!currentType)
                continue;

            if (parseDeclaration(line, fields))
                continue;
            if (parseConstant(line, constants))
                continue;

            char* cursor = line;
            char* name = takeToken(cursor, " =\t");
            if (!name || !*name)
                continue;
            const int fieldIndex = fields.findReverse(name);
            if (fieldIndex < 0)
            {
                reportIni("INI:Unknown word '%s'", name);
                continue;
            }
            if (fieldIndex != previousField + 1)
            {
                const int expectedIndex = previousField + 1;
                const char* const expectedName =
                    (expectedIndex >= 0 && expectedIndex < fields.count())
                        ? fields[expectedIndex].name.c_str()
                        : name;
                reportIni("INI:Enough word '%s' in %i line", expectedName, lineNumber);
                previousField = fieldIndex;
                continue;
            }

            previousField = fieldIndex;
            cursor = trimLeft(cursor);
            while (*cursor == '=' || *cursor == ' ' || *cursor == '\t')
                ++cursor;
            trimRight(cursor);

            if (fieldIndex == 0)
                output.BeginSection(currentType);

            const IniFieldRecord& field = fields[fieldIndex];
            switch (field.type)
            {
            case IniChar:
                if (!*cursor)
                {
                    const unsigned char zero = 0;
                    for (unsigned int i = 0; i < field.count; ++i)
                        output.write(&zero, 1);
                }
                else
                {
                    writeStringArray(output, cursor, field, constants);
                }
                break;
            case IniString:
                writeStringArray(output, cursor, field, constants);
                break;
            case IniFloat:
                writeFloatArray(output, cursor, field, constants, lineNumber);
                break;
            case IniByte:
            case IniWord:
            case IniDword:
                writeIntegerArray(output, cursor, field, constants, lineNumber);
                break;
            case IniFile:
            {
                const int fileResult = writeFilePayload(output, cursor);
                if (fileResult == -1)
                    reportIni("INI:Can't open file '%s'", cursor);
                else if (fileResult == -2)
                    reportIni("INI:Not enough memory for '%s'", cursor);
                break;
            }
            default:
                break;
            }

            if (fieldIndex == fields.count() - 1)
            {
                output.EndSection();
                previousField = -1;
            }
        }

        std::fclose(input);
        return 0;
    }

    int RunIni2ResCommand(int argc, const char* const* argv)
    {
        if (argc < 3 || !argv)
        {
            std::puts("ini2res file.ini resource.res");
            return 0;
        }

        RESOURCE output;
        if (!output.openFileForWrite(STRING(argv[2]), RESOURCE::ResTypes::DATA))
            return 1;
        const int result = CompileIniToResource(output, STRING(argv[1]));
        output.close();
        return result;
    }
}
