#pragma once
#include "types.h"
#include "as_string.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace as1
{

    class FileLogger
    {
        friend void VerifyFileLoggerLayout();

    public:
        FileLogger();
        explicit FileLogger(bool rewriteLog);
        ~FileLogger();

        static size_t ReadRaw(FILE* file, void* buffer, size_t size);
        static size_t WriteRaw(FILE* file, const void* buffer, size_t size);
        bool IsOpen() const { return fileHandle() != nullptr; }
        const char* Path() const { return pathStorage(); }

#if defined(_WIN32)
        void SetMessageWindow(HWND window);
#else
        void SetMessageWindow(void* window);
#endif

        void WriteLine(const char* format, ...);
        void WriteLineV(const char* format, va_list args);
        void RewriteLine(const char* format, ...);
        void RewriteLineV(const char* format, va_list args);
        void ShowMessage(const char* format, ...);
        [[noreturn]] void Fatal(const char* format, ...);
        void ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
        void ResourceErrorV(const char* contextFormat, int errorCode, const char* detailText, int detailValue, va_list contextArgs);

        std::uint32_t messageWindowToken() const { return m_messageWindowToken; }
        std::uint32_t& mutableMessageWindowToken() { return m_messageWindowToken; }
        FILE* fileHandle() const { return m_file; }
        FILE*& mutableFileHandle() { return m_file; }
        void clearFileHandle() { m_file = nullptr; }
        const char* pathStorage() const { return m_path; }
        char* mutablePathStorage() { return m_path; }
        void closeFileHandle();
        void constructFileLoggerState(bool rewriteLog);

        friend int destroyFileLogger(FileLogger* logger);
        friend FileLogger* deleteFileLogger(FileLogger* logger, unsigned char flags);

    private:
        static constexpr size_t PathCapacity = 0x400;
        static constexpr size_t MessageCapacity = 1024;

        static FILE* OpenFile(const char* path, const char* mode);
        static void FormatText(char* destination, size_t capacity, const char* format, va_list args);

        void WriteRawLine(const char* text);

        std::uint32_t m_vtableToken;
        std::uint32_t m_messageWindowToken;
        FILE* m_file;
        char m_path[PathCapacity];
    };

    void VerifyFileLoggerLayout();
    extern FileLogger* g_fileLogger;
    extern char* g_executablePath;
    void BindRetailProgramPathOwner(char* path) noexcept;
    void BindRetailProgramPathOwnerFromCrt() noexcept;
    FileLogger* constructFileLogger(FileLogger* logger, bool rewriteLog);
    int destroyFileLogger(FileLogger* logger);
    FileLogger* deleteFileLogger(FileLogger* logger, unsigned char flags);
    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
    FileLogger* InitializeGlobalFileLoggerOwner(bool rewriteLog);
    void ReleaseGlobalFileLoggerOwner();
    void DestroyGlobalFileLoggerOwnerForApplicationDestructor();
    FileLogger& GlobalFileLogger();
}
