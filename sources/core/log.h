#pragma once
#include "types.h"
#include <cstdarg>
#include <cstdint>

namespace as1
{
    class FileLogger;

    // Formatted write-line plus message-box helper. Callers pass g_fileLogger.
    std::intptr_t logAndShowError(FileLogger* logger, const char* format, ...);

    // Formatted rewrite helper. Callers pass g_fileLogger.
    std::intptr_t rewriteLogLine(FileLogger* logger, const char* format, ...);

    // Formatted write-line helper. Callers pass g_fileLogger.
    std::intptr_t writeLogLine(FileLogger* logger, const char* format, ...);

    // Fatal formatted logger helper. Callers pass g_fileLogger.
    [[noreturn]] void fatalLogError(FileLogger* logger, const char* format, ...);

    // Resource-error formatter with the suffix-selection route.
    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);

    namespace LOG
    {
        bool OpenErrorLog(bool rewriteLog);
        void CloseErrorLog();
        void Write(const char* format, ...);
        void WriteV(const char* format, va_list args);
        void Rewrite(const char* format, ...);
        void RewriteV(const char* format, va_list args);
        void ShowMessage(const char* format, ...);
        [[noreturn]] void Fatal(const char* format, ...);
        void ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
    }
}
