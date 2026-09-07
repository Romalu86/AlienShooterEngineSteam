#include "log.h"
#include "file_logger.h"
#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace as1
{
    std::intptr_t logAndShowError(FileLogger* logger, const char* format, ...)
    {

        if (!format)
            return 0;

        char messageBuffer[0x400] = {};
        va_list args;
        va_start(args, format);
        std::vsprintf(messageBuffer, format, args);
        va_end(args);

        FILE* file = logger->fileHandle();
        if (file)
        {
            std::fputs(messageBuffer, file);
            std::fputs("\n", file);
            std::fflush(file);
        }

#if defined(_WIN32)
        HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(logger->messageWindowToken()));
        if (!hwnd)
        {
            hwnd = ::GetForegroundWindow();
            logger->mutableMessageWindowToken() = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hwnd));
        }
        return static_cast<std::intptr_t>(::MessageBoxA(hwnd, messageBuffer, "Error", MB_OK));
#else
        return reinterpret_cast<std::intptr_t>(format);
#endif
    }

    std::intptr_t rewriteLogLine(FileLogger* logger, const char* format, ...)
    {

        if (!format)
            return 0;

        FILE* file = logger->fileHandle();
        if (!file)
            return reinterpret_cast<std::intptr_t>(format);

        char messageBuffer[0x400] = {};
        va_list args;
        va_start(args, format);
        std::vsprintf(messageBuffer, format, args);
        va_end(args);

        const long saved = std::ftell(file);
        std::fputs(messageBuffer, file);
        std::fputs("\n", file);
        std::fflush(file);
        return static_cast<std::intptr_t>(std::fseek(file, saved, SEEK_SET));
    }

    std::intptr_t writeLogLine(FileLogger* logger, const char* format, ...)
    {

        if (!format)
            return 0;

        FILE* file = logger->fileHandle();
        if (!file)
            return reinterpret_cast<std::intptr_t>(format);

        char messageBuffer[0x400];
        va_list args;
        va_start(args, format);
        std::vsprintf(messageBuffer, format, args);
        va_end(args);

        std::fputs(messageBuffer, file);
        std::fputs("\n", file);
        return static_cast<std::intptr_t>(std::fflush(file));
    }

    [[noreturn]] void fatalLogError(FileLogger* logger, const char* format, ...)
    {

        char messageBuffer[0x400] = {};
        va_list args;
        va_start(args, format);
        std::vsprintf(messageBuffer, format, args);
        va_end(args);

        logAndShowError(logger, messageBuffer);
        std::exit(1);
    }
}

namespace as1 { namespace LOG
{
    bool OpenErrorLog(bool rewriteLog)
    {
        return InitializeGlobalFileLoggerOwner(rewriteLog) != nullptr;
    }

    void CloseErrorLog()
    {
        ReleaseGlobalFileLoggerOwner();
    }

    void Write(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        WriteV(format, args);
        va_end(args);
    }

    void WriteV(const char* format, va_list args)
    {
        GlobalFileLogger().WriteLineV(format, args);
    }

    void Rewrite(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        RewriteV(format, args);
        va_end(args);
    }

    void RewriteV(const char* format, va_list args)
    {
        GlobalFileLogger().RewriteLineV(format, args);
    }

    void ShowMessage(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        char text[1024] = {};
        LOGGER::Format(text, sizeof(text), format, args);
        va_end(args);
        GlobalFileLogger().ShowMessage("%s", text);
    }

    [[noreturn]] void Fatal(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        char text[1024] = {};
        LOGGER::Format(text, sizeof(text), format, args);
        va_end(args);
        GlobalFileLogger().Fatal("%s", text);
        std::abort();
    }

    void ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...)
    {
        va_list args;
        va_start(args, detailValue);
        GlobalFileLogger().ResourceErrorV(contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
    }
} }
