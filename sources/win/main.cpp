#include "game/startup.h"
#include "steam_store.h"
#include "core/configuration.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/types.h"
#include "core/application.h"
#include "base_sprite_list.h"
#include "sprite.h"
#include "graph.h"
#include "map.h"
#include "constant.h"
#ifndef _WIN32
#include <filesystem>
#endif
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <new>
#include <cstdint>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include "win/resources/resource.h"
#include "win/main_sw.h"
#include "win/application_win.h"
#include "input.h"
#include "input/control_actions.h"
#include "mouse.h"
#include "sound/engine.h"
#endif

namespace
{

#ifndef _WIN32
    std::string joinedCommandLine(int argc, char** argv)
    {
        std::string out;
        for (int i = 1; i < argc; ++i)
        {
            if (!argv[i])
                continue;
            if (!out.empty())
                out += ' ';
            out += argv[i];
        }
        return out;
    }

    int runCommandLineStartup(int argc, char** argv, bool allowNoArgs)
    {
        if (!allowNoArgs && argc <= 1)
            return 0;
        static char portableProgramPath[] = "AlienShooter.exe";
        as1::BindRetailProgramPathOwner(portableProgramPath);
        as1::InitializeGlobalFileLoggerOwner(true);
        const as1::STRING commandLine(joinedCommandLine(argc, argv));
        as1::core::StartupConfiguration config = as1::core::Configuration::LoadStartupConfiguration(as1::STRING("AlienShooter"),
                                                                                                     commandLine,
                                                                                                     as1::STRING("."));
        as1::core::initializePostComProfileOwners(config);
        as1::core::readStartupResourceProfileBlock(config);
        as1::core::readStartupStartMapProfileBlock(config, commandLine);
        as1::StartupOptions options = as1::core::Configuration::BuildStartupOptions(config);
        int result = 0;
        try
        {
            as1::GRAPH graph;
            as1::MAP map(&graph);
            std::filesystem::path root(options.resourceRoot.c_str() ? options.resourceRoot.c_str() : ".");
            if (root.empty())
                root = ".";
            root = root.lexically_normal();
            map.setResourceRoot(as1::STRING(root.string()));
            map.setObjectsResource(options.objectsResource);

            if (options.loadGameResources)
            {
                const std::filesystem::path objectsRes = map.resolveGameFile(options.objectsResource);
                as1::RESOURCE objectsResource;
                if (!std::filesystem::exists(objectsRes) ||
                    !objectsResource.openFile(as1::STRING(objectsRes.string()), as1::RESOURCE::ResTypes::DATA))
                {
                    result = 2;
                }
                else
                {
                    map.LoadSfx(&objectsResource);
                    map.LoadConstants(&objectsResource);
                    if (!map.loadGameResourcesFromResource(&objectsResource, false, false))
                        result = 2;
                }
            }

            if (result == 0 && options.loadMap)
            {
                const std::filesystem::path mapFile = map.resolveGameFile(options.mapName);
                if (!std::filesystem::exists(mapFile))
                    result = 2;
                else
                    map.load(options.mapName);
            }
        }
        catch (...)
        {
            result = 2;
        }
        as1::core::ReleaseStartupStringsIniPathOwner();
        as1::core::ReleaseStartupRegistryPathOwner();
        as1::ReleaseGlobalFileLoggerOwner();
        return result;
    }
#endif

#ifdef _WIN32

#if defined(_MSC_VER) && defined(_M_IX86)
    int retailTopLevelExceptionFilter(const EXCEPTION_POINTERS* exceptionPointers)
    {
        const EXCEPTION_RECORD* const record =
            exceptionPointers ? exceptionPointers->ExceptionRecord : nullptr;
        if (!record)
            return EXCEPTION_EXECUTE_HANDLER;

        const auto logException = [&](const char* format, std::uintptr_t extra = 0u, bool hasExtra = false)
        {
            if (!as1::g_fileLogger || !as1::g_fileLogger->fileHandle())
                return;
            const unsigned int address = static_cast<unsigned int>(
                reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
            if (hasExtra)
                as1::writeLogLine(as1::g_fileLogger, format, address, static_cast<unsigned int>(extra));
            else
                as1::writeLogLine(as1::g_fileLogger, format, address);
        };

        switch (record->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            if (record->NumberParameters >= 2)
            {
                const bool writeAccess = record->ExceptionInformation[0] != 0;
                logException(writeAccess
                    ? "!!!ERROR EXCEPTION 0x%X!!!: Access violation write to 0x%X"
                    : "!!!ERROR EXCEPTION 0x%X!!!: Access violation read from 0x%X",
                    static_cast<std::uintptr_t>(record->ExceptionInformation[1]), true);
            }
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_ARRAY_BOUNDS_EXCEEDED");
            break;
        case EXCEPTION_BREAKPOINT:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_BREAKPOINT");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_DATATYPE_MISALIGNMENT");
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_DENORMAL_OPERAND");
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            logException("!!!ERROR EXCEPTION 0x%X!!!:FLT divide by zero");
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_INEXACT_RESULT");
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_INVALID_OPERATION");
            break;
        case EXCEPTION_FLT_OVERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_OVERFLOW");
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_STACK_CHECK");
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_UNDERFLOW");
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_ILLEGAL_INSTRUCTION");
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_IN_PAGE_ERROR");
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            logException("!!!ERROR EXCEPTION 0x%X!!!:INT divide by zero");
            break;
        case EXCEPTION_INT_OVERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_INT_OVERFLOW");
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_INVALID_DISPOSITION");
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_NONCONTINUABLE_EXCEPTION");
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_PRIV_INSTRUCTION");
            break;
        case EXCEPTION_SINGLE_STEP:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_SINGLE_STEP");
            break;
        case EXCEPTION_STACK_OVERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_STACK_OVERFLOW");
            break;
        default:
            break;
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

    int runWin32Application(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCmd)
    {
        as1::BindRetailProgramPathOwnerFromCrt();

        as1::win::ApplicationWinInit shellInit{};
        shellInit.hInstance = instance;
        shellInit.previousInstance = previousInstance;
        shellInit.commandLine = commandLine ? commandLine : "";
        shellInit.showCmd = showCmd;
        as1::win::ApplicationWin* applicationShell = as1::win::CreateApplicationWin(shellInit);
        if (!applicationShell)
            return 0;

        char* commandLineStorage = as1::STRING::SharedEmptyText();
        if (commandLine && commandLine[0] != '\0')
        {
            const std::size_t length = std::strlen(commandLine);
            commandLineStorage = static_cast<char*>(::operator new(length + 1u));
            std::memcpy(commandLineStorage, commandLine, length);
            commandLineStorage[length] = '\0';
        }
        const char* commandLineOwner = commandLineStorage;

        as1::win::ApplicationWin* const returnedOwner =
            applicationShell->initializeDerivedApplicationStartup(instance,
                                         previousInstance,
                                         &commandLineOwner,
                                         showCmd,
                                         &as1::core::StartupSettings());

        as1::core::BindApplicationPhysicalOwner(returnedOwner);

        if (commandLineStorage != as1::STRING::SharedEmptyText())
            ::operator delete(commandLineStorage);

        if (applicationShell->initialized())
        {
            if (applicationShell->pumpFrame() == 0)
            {
                do
                {
                } while (applicationShell->pumpFrame() == 0);
            }
        }

        as1::win::DestroyApplicationWin(applicationShell);
        as1::win::ReleaseApplicationWinHostMapCarrier();
        return 0;
    }
#endif
}

#ifndef _WIN32
int main(int argc, char** argv)
{
    return runCommandLineStartup(argc, argv, false);
}
#else
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
#if defined(_MSC_VER) && defined(_M_IX86)
    __try
    {
        return runWin32Application(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    }
    __except (retailTopLevelExceptionFilter(
        (const EXCEPTION_POINTERS*)GetExceptionInformation()))
    {
        return 0;
    }
#else
    return runWin32Application(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
#endif
}
#endif
