#include "file_logger.h"
#include "logger.h"
#include "logger_p.h"
#include "log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(_WIN32) && defined(_MSC_VER)
#include <stdlib.h>
#endif

namespace as1
{
    namespace
    {
        const char* notNull(const char* text)
        {
            return text ? text : "";
        }

        std::intptr_t formatResourceErrorLog(FileLogger* logger,
                                      const char* contextFormat,
                                      int errorCode,
                                      const char* detailText,
                                      int detailValue,
                                      va_list contextArgs);

        struct FileLoggerVtableOwner
        {
            virtual FileLogger* deletingDestructor(unsigned char flags) noexcept
            {
                return deleteFileLogger(reinterpret_cast<FileLogger*>(this), flags);
            }
        };

        std::uint32_t currentImageFileLoggerVtable() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static FileLoggerVtableOwner owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else

            return 0u;
#endif
        }

    }

    FileLogger::FileLogger()
    {

    }

    FileLogger::FileLogger(bool rewriteLog) : FileLogger()
    {
        constructFileLoggerState(rewriteLog);
    }

    void FileLogger::constructFileLoggerState(bool rewriteLog)
    {

        m_vtableToken = currentImageFileLoggerVtable();
        m_messageWindowToken = 0;
        STRING dateText;
        constructCurrentDateString(dateText);

        STRING datedErrorPath("saves\\logs\\error", dateText.c_str());
        STRING datedErrorPathWithSpace(datedErrorPath.c_str(), " " );

        STRING timeText;
        constructCurrentTimeString(timeText);

        STRING datedTimedErrorPath(datedErrorPathWithSpace.c_str(), timeText.c_str());
        STRING logPath(datedTimedErrorPath.c_str(), ".log");

        // Release temporary path components immediately after the final log path is constructed.
        datedTimedErrorPath.ReleaseOwnedStorage();
        datedErrorPathWithSpace.ReleaseOwnedStorage();
        datedErrorPath.ReleaseOwnedStorage();
        dateText.ReleaseOwnedStorage();
        timeText.ReleaseOwnedStorage();

        replaceStringFirst(logPath, ":", "h");
        replaceStringFirst(logPath, ":", "m");

        const char* mode = rewriteLog ? "wt" : "at";
        if (!rewriteLog)
            assignStringFromCString(logPath, "saves\\logs\\error.log");

        FILE* file = nullptr;
        if (!logPath.isEmpty())
            file = OpenFile(logPath.c_str(), mode);
        mutableFileHandle() = file;

        if (!fileHandle())
        {
            STRING fallback;
            constructRightOfFirstMarker(logPath, fallback, "saves\\logs\\");
            assignStringFromString(logPath, fallback);
            fallback.ReleaseOwnedStorage();
            mode = rewriteLog ? "wt" : "at";
            if (!logPath.isEmpty())
                mutableFileHandle() = OpenFile(logPath.c_str(), mode);
        }

        std::strcpy(mutablePathStorage(), logPath.c_str());

        STRING stampTime;
        constructCurrentTimeString(stampTime);
        STRING stampDate;
        constructCurrentDateString(stampDate);
        STRING executablePath(g_executablePath);
        STRING executableTimes;
        constructFileTimestampString(executableTimes, executablePath);
        writeLogLine(this, "----< %s %s >----< %s (%s) >----",
                     stampDate.c_str(), stampTime.c_str(), g_executablePath, executableTimes.c_str());
    }

    FileLogger::~FileLogger()
    {
        destroyFileLogger(this);
    }

    FILE* FileLogger::OpenFile(const char* path, const char* mode)
    {
        return std::fopen(notNull(path), notNull(mode));
    }

    size_t FileLogger::ReadRaw(FILE* file, void* buffer, size_t size)
    {
        if (!file || !buffer || size == 0)
            return 0;
        return std::fread(buffer, 1, size, file);
    }

    size_t FileLogger::WriteRaw(FILE* file, const void* buffer, size_t size)
    {
        if (!file || !buffer || size == 0)
            return 0;
        return std::fwrite(buffer, 1, size, file);
    }

    void FileLogger::FormatText(char* destination, size_t capacity, const char* format, va_list args)
    {
        LOGGER::Format(destination, capacity, format, args);
    }

#if defined(_WIN32)
    void FileLogger::SetMessageWindow(HWND window)
    {

        mutableMessageWindowToken() = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window));
    }
#else
    void FileLogger::SetMessageWindow(void* window)
    {

        mutableMessageWindowToken() = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window));
    }
#endif

    void FileLogger::closeFileHandle()
    {
        FILE* file = fileHandle();
        if (file)
        {
            std::fclose(file);
            clearFileHandle();
        }
    }

    void FileLogger::WriteRawLine(const char* text)
    {
        FILE* file = fileHandle();
        if (!file)
            return;
        std::fputs(notNull(text), file);
        std::fputs("\n", file);
        std::fflush(file);
    }

    void FileLogger::WriteLine(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        WriteLineV(format, args);
        va_end(args);
    }

    void FileLogger::WriteLineV(const char* format, va_list args)
    {
        char text[MessageCapacity];
        FormatText(text, sizeof(text), format, args);
        writeLogLine(this, "%s", text);
    }

    void FileLogger::RewriteLine(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        RewriteLineV(format, args);
        va_end(args);
    }

    void FileLogger::RewriteLineV(const char* format, va_list args)
    {
        char text[MessageCapacity];
        FormatText(text, sizeof(text), format, args);
        rewriteLogLine(this, "%s", text);
    }

    void FileLogger::ShowMessage(const char* format, ...)
    {
        char text[MessageCapacity];
        va_list args;
        va_start(args, format);
        FormatText(text, sizeof(text), format, args);
        va_end(args);

        logAndShowError(this, "%s", text);
    }

    [[noreturn]] void FileLogger::Fatal(const char* format, ...)
    {
        char text[MessageCapacity];
        va_list args;
        va_start(args, format);
        FormatText(text, sizeof(text), format, args);
        va_end(args);
        fatalLogError(this, "%s", text);
    }

    void FileLogger::ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...)
    {
        va_list args;
        va_start(args, detailValue);
        ResourceErrorV(contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
    }

    void FileLogger::ResourceErrorV(const char* contextFormat, int errorCode, const char* detailText, int detailValue, va_list contextArgs)
    {
        va_list args;
        va_copy(args, contextArgs);
        formatResourceErrorLog(this, contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
    }

    FileLogger* g_fileLogger = nullptr;
    char* g_executablePath = nullptr;

    void BindRetailProgramPathOwner(char* path) noexcept
    {

        g_executablePath = path;
    }

    void BindRetailProgramPathOwnerFromCrt() noexcept
    {
#if defined(_WIN32) && defined(_MSC_VER)
        char* programPath = nullptr;
        if (::_get_pgmptr(&programPath) == 0)
            BindRetailProgramPathOwner(programPath);
        else
            BindRetailProgramPathOwner(nullptr);
#else
        BindRetailProgramPathOwner(nullptr);
#endif
    }

#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                           
#endif

    namespace
    {
        struct FileLoggerPhysicalLayout32
        {
            std::uint32_t vtableSlot0;
            std::uint32_t hwndSlot4;
            std::uint32_t fileSlot8;
            char pathSlot0C[0x400];
        };

                                                                                                                           
                                                                                                                             
                                                                                                                        
                                                                                                                                  
                                                                                                                 
    }

    void VerifyFileLoggerLayout()
    {
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                                   
                                                                                                                          
                                                                                                            
                                                                                                               
                                                                                                                     
#endif
    }

    namespace
    {
        constexpr std::size_t kOriginalFileLoggerAllocationSize = 0x40C;

        std::size_t fileLoggerAllocationSize()
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return kOriginalFileLoggerAllocationSize;
#else
            return sizeof(FileLogger);
#endif
        }
    }


    namespace
    {
        const char* resourceErrorSuffixFormat(int errorCode)
        {

            switch (errorCode)
            {
            case 0: return "0x%X Couldn't lock %s";
            case 1: return "0x%X Couldn't copy %s";
            case 2: return "%i There was not enough memory for %s";
            case 3: return "0x%X Couldn't create the %s";
            case 4: return "0x%X Invalid %s";
            case 5: return "0x%X Load %s";
            case 6: return "0x%X Save %s";
            case 7: return "0x%X Couldn't open '%s'";
            case 8: return "0x%X Couldn't set the %s";
            case 9: return "0x%X Couldn't get the %s";
            case 10: return "%i %s";
            case 11: return "0x%X Section can't found (%s)";
            case 12: return "0x%X Unable initialize %s";
            case 13: return "%i Missing %s";
            case 14: return "%i Unknownn %s";
            default: return nullptr;
            }
        }

        std::intptr_t formatResourceErrorLog(FileLogger* logger,
                                      const char* contextFormat,
                                      int errorCode,
                                      const char* detailText,
                                      int detailValue,
                                      va_list contextArgs)
        {

            STRING timeText04;
            constructCurrentTimeString(timeText04);

            char timeText00[0x400] = {};
            std::sprintf(timeText00, "!!!ERROR %s!!!", timeText04.c_str());

            timeText04.ReleaseOwnedStorage();

            char* const contextWrite = timeText00 + std::strlen(timeText00);
            std::vsprintf(contextWrite, contextFormat, contextArgs);

            std::strcat(timeText00, ": ");

            const char* suffix = resourceErrorSuffixFormat(errorCode);
            if (suffix)
                std::strcat(timeText00, suffix);

            // Return the log-write result directly.
            return writeLogLine(logger, timeText00, detailValue, detailText);
        }
    }

    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...)
    {
        va_list args;
        va_start(args, detailValue);
        const std::intptr_t result = formatResourceErrorLog(logger, contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
        return result;
    }

    FileLogger* constructFileLogger(FileLogger* logger, bool rewriteLog)
    {
        logger->constructFileLoggerState(rewriteLog);
        return logger;
    }

    int destroyFileLogger(FileLogger* logger)
    {

        logger->m_vtableToken = currentImageFileLoggerVtable();

        FILE* file = logger->fileHandle();
        if (file)
            std::fclose(file);
        logger->clearFileHandle();

        const char* path = logger->pathStorage();
        int result = std::strcmp(path, logger_detail::kPrimaryErrorLogPath);
        if (result != 0)
        {
            result = std::strcmp(path, logger_detail::kFallbackErrorLog);
            if (result != 0)
            {
                std::remove(logger_detail::kPrimaryErrorLogPath);
                result = std::rename(path, logger_detail::kPrimaryErrorLogPath);
                if (result != 0)
                {
                    std::remove(logger_detail::kFallbackErrorLog);
                    result = std::rename(path, logger_detail::kFallbackErrorLog);
                }
            }
        }
        return result;
    }

    FileLogger* deleteFileLogger(FileLogger* logger, unsigned char flags)
    {

        FileLogger* const result = logger;
        destroyFileLogger(result);
        if ((flags & 1u) != 0)
            ::operator delete(result);
        return result;
    }

    FileLogger* InitializeGlobalFileLoggerOwner(bool rewriteLog)
    {

        void* storage = ::operator new(fileLoggerAllocationSize(), std::nothrow);
        if (!storage)
        {
            g_fileLogger = nullptr;
            return nullptr;
        }
        auto* logger = new (storage) FileLogger();
        g_fileLogger = constructFileLogger(logger, rewriteLog);
        return g_fileLogger;
    }

    void ReleaseGlobalFileLoggerOwner()
    {
        FileLogger* logger = g_fileLogger;
        if (!logger)
            return;
        g_fileLogger = nullptr;
        deleteFileLogger(logger, 1);
    }

    void DestroyGlobalFileLoggerOwnerForApplicationDestructor()
    {

        if (g_fileLogger)
            deleteFileLogger(g_fileLogger, 1);
    }

    FileLogger& GlobalFileLogger()
    {
        if (!g_fileLogger)
            InitializeGlobalFileLoggerOwner(false);
        return *g_fileLogger;
    }
}
