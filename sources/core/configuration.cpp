#include "core/configuration.h"

#include "core/file_storage.h"
#include "core/file_logger.h"
#include "core/profile.h"
#include "core/profile_p.h"
#include "game/startup.h"
#include "file_data.h"

#include <cstring>
#include <new>
#include <sstream>
#include <string>

namespace as1 { namespace core
{
    namespace
    {

        StartupSettingsBlock g_startupSettings = {
            {},
            {640u, 800u, 1024u, 1280u},
            {480u, 600u, 768u, 1024u},
            {32u},
            0u, 0, 1024, 768, 32, 1
        };
        STRING g_startupRegistryPathEmpty;
        STRING g_startupStringsIniPathEmpty;
        STRING* g_startupStringsIniPathOwner = nullptr;
        STRING* g_startupRegistryPathOwner = nullptr;

        STRING deriveProgramClassName()
        {
            STRING executablePath;
            if (as1::g_executablePath && *as1::g_executablePath)
                constructStringFromBytes(executablePath, as1::g_executablePath, std::strlen(as1::g_executablePath));

            STRING fileName;
            constructRightOfLastMarker(executablePath, fileName, "\\");

            STRING className;
            constructLeftOfLastMarker(fileName, className, ".");
            return className;
        }

        const STRING& directoryOrDot(const STRING& currentDirectory)
        {
            static const STRING dot(".");
            return currentDirectory.isEmpty() ? dot : currentDirectory;
        }

        STRING& buildDefaultConfigPath(STRING& destination, const STRING& currentDirectory)
        {

            STRING className = deriveProgramClassName();

            STRING directoryPrefix;
            constructConcatenatedString(directoryPrefix, directoryOrDot(currentDirectory).c_str(), "\\");

            STRING configBasePath;
            constructConcatenatedString(configBasePath, directoryPrefix.c_str(), className.c_str());

            STRING configPath;
            constructConcatenatedString(configPath, configBasePath.c_str(), ".cfg");

            resetAndAssignString(destination, configPath);
            return destination;
        }

        STRING& applyCommandLineConfigOverride(STRING& destination,
                                                          const STRING& commandLine,
                                                          const STRING& currentDirectory)
        {

            if (!std::strstr(commandLine.c_str(), ".cfg"))
                return destination;

            STRING directoryPrefix;
            constructConcatenatedString(directoryPrefix, directoryOrDot(currentDirectory).c_str(), "\\");

            STRING overridePath;
            constructConcatenatedString(overridePath, directoryPrefix.c_str(), commandLine.c_str());

            resetAndAssignString(destination, overridePath);
            return destination;
        }


        STRING& readStartupApplicationTitle(STRING& destination,
                                                       const STRING& configPath,
                                                       const STRING& className)
        {

            STRING titleKey;
            constructStringFromBytes(titleKey, "Title", std::strlen("Title"));

            STRING commonSection;
            constructStringFromBytes(commonSection, "common", std::strlen("common"));

            STRING configuredTitle;
            profile_p::readProfileStringInto(configuredTitle, configPath, commonSection, titleKey, className);

            assignStringFromString(destination, configuredTitle);
            return destination;
        }


        void readStartupProfileFlagBits(StartupConfiguration& config, const STRING& configPath)
        {

            STRING vsyncKey;
            constructStringFromBytes(vsyncKey, "VSync", std::strlen("VSync"));

            STRING graphSection;
            constructStringFromBytes(graphSection, "graph", std::strlen("graph"));

            StartupSettingsBlock& settings = g_startupSettings;
            config.startupFlags = settings.flags;
            int value = profile_p::readProfileIntValue(configPath, graphSection, vsyncKey, 1);
            settings.flags |= value ? StartupVSyncFlag : 0u;
            config.startupFlags = settings.flags;
            config.video.vsync = (config.startupFlags & StartupVSyncFlag) != 0;

            STRING startDialogFullscreenKey;
            constructStringFromBytes(startDialogFullscreenKey, "StartDialogIsFull", std::strlen("StartDialogIsFull"));

            STRING gameSection;
            constructStringFromBytes(gameSection, "game", std::strlen("game"));

            value = profile_p::readProfileIntValue(configPath, gameSection, startDialogFullscreenKey, 0);
            settings.flags |= value ? StartupDialogFullscreenFlag : 0u;
            config.startupFlags = settings.flags;
            config.startDialogFullscreen = (config.startupFlags & StartupDialogFullscreenFlag) != 0;
        }


        void readStartupRegistryVideoSettings(StartupConfiguration& config)
        {

            STRING deviceKey;
            constructStringFromBytes(deviceKey, "Device", std::strlen("Device"));
            g_startupSettings.device = readRegistryInt(*g_startupRegistryPathOwner, deviceKey, g_startupSettings.device);
            config.video.device = g_startupSettings.device;

            STRING screenWidthKey;
            constructStringFromBytes(screenWidthKey, "ScreenX", std::strlen("ScreenX"));
            g_startupSettings.screenWidth = readRegistryInt(*g_startupRegistryPathOwner, screenWidthKey, g_startupSettings.screenWidth);
            config.video.screenX = g_startupSettings.screenWidth;

            STRING screenHeightKey;
            constructStringFromBytes(screenHeightKey, "ScreenY", std::strlen("ScreenY"));
            g_startupSettings.screenHeight = readRegistryInt(*g_startupRegistryPathOwner, screenHeightKey, g_startupSettings.screenHeight);
            config.video.screenY = g_startupSettings.screenHeight;

            STRING colorDepthKey;

            constructStringFromBytes(colorDepthKey, "BPP", std::strlen("BPP"));
            g_startupSettings.colorDepth = readRegistryInt(*g_startupRegistryPathOwner, colorDepthKey, g_startupSettings.colorDepth);
            config.video.colorDepth = g_startupSettings.colorDepth;

            STRING fullscreenKey;
            constructStringFromBytes(fullscreenKey, "FullScreen", std::strlen("FullScreen"));
            g_startupSettings.fullscreen = readRegistryInt(*g_startupRegistryPathOwner, fullscreenKey, g_startupSettings.fullscreen);
            config.video.fullscreen = g_startupSettings.fullscreen != 0;
        }


        void readStartupStartDialogFlag(StartupConfiguration& config, const STRING& configPath)
        {

            STRING startDialogKey;
            constructStringFromBytes(startDialogKey, "StartDialog", std::strlen("StartDialog"));

            STRING gameSection;
            constructStringFromBytes(gameSection, "game", std::strlen("game"));

            const int value = profile_p::readProfileIntValue(configPath, gameSection, startDialogKey, 1);
            config.showStartDialog = value != 0;
        }


        void readWindowPositionRegistrySettings(StartupConfiguration& config, bool graphFullscreen)
        {

            if (graphFullscreen)
            {
                config.video.windowPositionX = 0;
                config.video.windowPositionY = 0;
                return;
            }

            STRING windowPositionXKey;
            constructStringFromBytes(windowPositionXKey, "WindowPositionX", std::strlen("WindowPositionX"));
            config.video.windowPositionX = readRegistryInt(*g_startupRegistryPathOwner, windowPositionXKey, 0);

            STRING windowPositionYKey;
            constructStringFromBytes(windowPositionYKey, "WindowPositionY", std::strlen("WindowPositionY"));
            config.video.windowPositionY = readRegistryInt(*g_startupRegistryPathOwner, windowPositionYKey, 0);
        }


        void readGraphFontProfileSettings(StartupConfiguration& config)
        {

            STRING fontSizeYKey;
            constructStringFromBytes(fontSizeYKey, "FontSizeY", std::strlen("FontSizeY"));

            STRING graphSectionForSizeY;
            constructStringFromBytes(graphSectionForSizeY, "graph", std::strlen("graph"));

            STRING fontSizeXKey;
            constructStringFromBytes(fontSizeXKey, "FontSizeX", std::strlen("FontSizeX"));

            STRING graphSectionForSizeX;
            constructStringFromBytes(graphSectionForSizeX, "graph", std::strlen("graph"));

            STRING defaultFontFace;
            constructStringFromBytes(defaultFontFace, "Arial", std::strlen("Arial"));

            STRING fontFaceKey;
            constructStringFromBytes(fontFaceKey, "Font", std::strlen("Font"));

            STRING graphSectionForFace;
            constructStringFromBytes(graphSectionForFace, "graph", std::strlen("graph"));

            config.font.sizeY = profile_p::readProfileIntValue(config.configPath, graphSectionForSizeY, fontSizeYKey, 8);
            config.font.sizeX = profile_p::readProfileIntValue(config.configPath, graphSectionForSizeX, fontSizeXKey, 0);

            STRING configuredFontFace;
            profile_p::readProfileStringInto(configuredFontFace, config.configPath, graphSectionForFace, fontFaceKey, defaultFontFace);
            assignStringFromString(config.font.face, configuredFontFace);
        }

        void readResourceProfileSettings(StartupConfiguration& config)
        {

            STRING defaultResourceName;
            constructStringFromBytes(defaultResourceName, "objects.res", std::strlen("objects.res"));

            STRING resourceKey;
            constructStringFromBytes(resourceKey, "Resource", std::strlen("Resource"));

            STRING gameSection;
            constructStringFromBytes(gameSection, "game", std::strlen("game"));

            STRING configuredResourceName;
            profile_p::readProfileStringInto(configuredResourceName, config.configPath, gameSection, resourceKey, defaultResourceName);
            assignStringFromString(config.objectsResource, configuredResourceName);
        }

        void readSoundQualityRegistrySetting(StartupConfiguration& config)
        {
            const STRING value = as1::FileDataLoad(
                STRING("options:\\sound\\SoundHighQuality"), STRING("0"));
            config.sound.highQuality = std::atoi(value.c_str()) != 0;
        }

        STRING oneByteDefaultString(unsigned char value)
        {
            char bytes[2] = {static_cast<char>(value), '\0'};
            STRING out;
            constructStringFromBytes(out, bytes, 1);
            return out;
        }

        STRING readStartupControlString(const StartupConfiguration& config,
                                        const char* keyName,
                                        const STRING& defaultValue)
        {
            STRING defaultText;
            assignStringFromString(defaultText, defaultValue);

            STRING controlKey;
            constructStringFromBytes(controlKey, keyName, std::strlen(keyName));

            STRING controlSection;
            constructStringFromBytes(controlSection, "control", std::strlen("control"));

            STRING configuredText;
            profile_p::readProfileStringInto(configuredText, config.configPath, controlSection, controlKey, defaultText);
            return configuredText;
        }

        void readControlProfileSettings(StartupConfiguration& config)
        {

            config.control.left = readStartupControlString(config, "Left", oneByteDefaultString(0x25));
            config.control.up = readStartupControlString(config, "Up", oneByteDefaultString(0x26));
            config.control.right = readStartupControlString(config, "Right", oneByteDefaultString(0x27));

            STRING downDefault;
            const unsigned char downDefaultBytes[2] = {0x04u, 0x38u};
            downDefault.AssignBytes(downDefaultBytes, 2);
            config.control.down = readStartupControlString(config, "Down", downDefault);

            STRING relativeKey;
            constructStringFromBytes(relativeKey, "Relative", std::strlen("Relative"));

            STRING controlSection;
            constructStringFromBytes(controlSection, "control", std::strlen("control"));

            config.control.relative = static_cast<std::uint32_t>(profile_p::readProfileIntValue(config.configPath, controlSection, relativeKey, 0));
            config.control.firstAction = readStartupControlString(config, "First", STRING("LBUTTON"));
            config.control.secondAction = readStartupControlString(config, "Second", STRING("RBUTTON"));
            // weapon bindings and decodes them with the same key-name helper.
            config.control.previousWeapon = readStartupControlString(config, "Prev", STRING("["));
            config.control.nextWeapon = readStartupControlString(config, "Next", STRING("]"));
        }


        void readDebugModeProfileSetting(StartupConfiguration& config)
        {

            STRING gameSection;
            constructStringFromBytes(gameSection, "game", std::strlen("game"));

            STRING debugModeKey;
            constructStringFromBytes(debugModeKey, "DebugMode", std::strlen("DebugMode"));
            config.debugMode = profile_p::readProfileIntValue(config.configPath, gameSection, debugModeKey, 0);

            // DrawFPS controls the application FPS-overlay flag and defaults to disabled.
            STRING drawFpsKey;
            constructStringFromBytes(drawFpsKey, "DrawFPS", std::strlen("DrawFPS"));
            config.drawFps = profile_p::readProfileIntValue(config.configPath, gameSection, drawFpsKey, 0);

            // DrawPresentation controls presentation rendering and defaults to enabled.
            STRING drawPresentationKey;
            constructStringFromBytes(drawPresentationKey, "DrawPresentation", std::strlen("DrawPresentation"));
            config.drawPresentation = profile_p::readProfileIntValue(config.configPath, gameSection, drawPresentationKey, 1);
        }

        void readStartMapProfileSetting(StartupConfiguration& config, const STRING& commandLine)
        {

            const bool isClassSentinel = std::strcmp(commandLine.c_str(), "") == 0;
            if (!isClassSentinel && !std::strstr(commandLine.c_str(), ".cfg"))
            {
                assignStringFromString(config.startMap, commandLine);
                return;
            }

            STRING defaultMapName;
            constructStringFromBytes(defaultMapName, "maps\\logo.map", std::strlen("maps\\logo.map"));

            STRING startMapKey;
            constructStringFromBytes(startMapKey, "StartMap", std::strlen("StartMap"));

            STRING gameSection;
            constructStringFromBytes(gameSection, "game", std::strlen("game"));

            STRING configuredMapName;
            profile_p::readProfileStringInto(configuredMapName, config.configPath, gameSection, startMapKey, defaultMapName);
            assignStringFromString(config.startMap, configuredMapName);
        }

        STRING readString(const Profile& profile, const char* section, const char* key, const STRING& value)
        {
            return profile.readString(section, key, value);
        }

        int readInt(const Profile& profile, const char* section, const char* key, int value)
        {
            return profile.readInt(section, key, value);
        }

        bool readBool(const Profile& profile, const char* section, const char* key, bool value)
        {
            return profile.readBool(section, key, value);
        }
    }

    StartupSettingsBlock& StartupSettings() noexcept
    {
        return g_startupSettings;
    }

    STRING Configuration::ResolveConfigPath(const STRING& executableName, const STRING& commandLine, const STRING& currentDirectory)
    {
        (void)executableName;
        STRING resolved;

        buildDefaultConfigPath(resolved, currentDirectory);
        applyCommandLineConfigOverride(resolved, commandLine, currentDirectory);
        return resolved;
    }

    StartupConfiguration Configuration::LoadStartupConfiguration(const STRING& executableName,
                                                                 const STRING& commandLine,
                                                                 const STRING& currentDirectory)
    {

        StartupConfiguration config;
        StartupSettingsBlock& settings = g_startupSettings;
        config.startupFlags = settings.flags;
        config.video.device = settings.device;
        config.video.screenX = settings.screenWidth;
        config.video.screenY = settings.screenHeight;
        config.video.colorDepth = settings.colorDepth;
        config.video.fullscreen = settings.fullscreen != 0;
        (void)executableName;
        config.applicationName = deriveProgramClassName();
        config.applicationTitle = config.applicationName;
        config.registryPath = STRING(std::string("SOFTWARE\\Gromada\\") + config.applicationName.str());
        config.configPath = ResolveConfigPath(config.applicationName, commandLine, currentDirectory);
        config.resourceRoot = currentDirectory.isEmpty() ? STRING(".") : currentDirectory;
        return config;
    }

    void initializePostComProfileOwners(StartupConfiguration& config)
    {

        InitializeStartupStringsIniPathOwner(config.resourceRoot);
        config.stringsPath = StartupStringsIniPath();

        readStartupApplicationTitle(config.applicationTitle, config.configPath, config.applicationName);
        initializeStartupRegistryPathOwnerFromProfile(config.configPath, config.applicationName);
        config.registryPath = StartupRegistryPath();

        const STRING commonSection("common");
        const STRING steamKey("Steam");
        config.steamAppId = profile_p::readProfileIntValue(
            config.configPath, commonSection, steamKey, -1);

        readStartupProfileFlagBits(config, config.configPath);

        const STRING graphSection("graph");
        g_startupSettings.screenWidth = static_cast<int>(profile_p::readProfileIntValue(
            config.configPath, graphSection, STRING("DefaultScreenX"), 1024));
        g_startupSettings.screenHeight = static_cast<int>(profile_p::readProfileIntValue(
            config.configPath, graphSection, STRING("DefaultScreenY"), 768));
        g_startupSettings.fullscreen = static_cast<int>(profile_p::readProfileIntValue(
            config.configPath, graphSection, STRING("DefaultFullScreenMode"), 1));
        g_startupSettings.colorDepth = static_cast<int>(profile_p::readProfileIntValue(
            config.configPath, graphSection, STRING("DefaultColorBPP"), 32));
        config.video.screenX = g_startupSettings.screenWidth;
        config.video.screenY = g_startupSettings.screenHeight;
        config.video.colorDepth = g_startupSettings.colorDepth;
        config.video.fullscreen = g_startupSettings.fullscreen != 0;

        readStartupRegistryVideoSettings(config);
    }

    void readStartupStartDialogProfileBlock(StartupConfiguration& config)
    {
        readStartupStartDialogFlag(config, config.configPath);
    }

    void readStartupWindowPositionRegistryBlock(StartupConfiguration& config, bool graphFullscreen)
    {
        readWindowPositionRegistrySettings(config, graphFullscreen);
    }


    void readStartupGraphFontProfileBlock(StartupConfiguration& config)
    {
        readGraphFontProfileSettings(config);
    }

    void readStartupResourceProfileBlock(StartupConfiguration& config)
    {
        readResourceProfileSettings(config);
    }

    void readStartupSoundHighQualityRegistryBlock(StartupConfiguration& config)
    {
        readSoundQualityRegistrySetting(config);
    }

    void readStartupDebugModeProfileBlock(StartupConfiguration& config)
    {
        readDebugModeProfileSetting(config);
    }

    void readStartupControlProfileBlock(StartupConfiguration& config)
    {
        readControlProfileSettings(config);
    }

    void readStartupStartMapProfileBlock(StartupConfiguration& config, const STRING& commandLine)
    {
        readStartMapProfileSetting(config, commandLine);
    }

    const STRING& StartupRegistryPath()
    {
#ifdef _WIN32

        return *g_startupRegistryPathOwner;
#else
        return g_startupRegistryPathOwner ? *g_startupRegistryPathOwner : g_startupRegistryPathEmpty;
#endif
    }

    void SetStartupRegistryPath(const STRING& path)
    {
        if (!g_startupRegistryPathOwner)
            g_startupRegistryPathOwner = new (std::nothrow) STRING();
        if (g_startupRegistryPathOwner)
            assignStringFromString(*g_startupRegistryPathOwner, path);
        else
            assignStringFromString(g_startupRegistryPathEmpty, path);
    }

    void initializeStartupRegistryPathOwnerFromProfile(const STRING& configPath, const STRING& className)
    {

        STRING* owner = new (std::nothrow) STRING();
        if (!owner)
        {
            g_startupRegistryPathOwner = nullptr;
            return;
        }

        STRING registryPathKey;
        constructStringFromBytes(registryPathKey, "RegPath", std::strlen("RegPath"));

        STRING commonSection;
        constructStringFromBytes(commonSection, "common", std::strlen("common"));

        STRING defaultRegistryPath;
        constructConcatenatedString(defaultRegistryPath, "SOFTWARE\\Gromada\\", className.c_str());

        STRING configuredRegistryPath;
        profile_p::readProfileStringInto(configuredRegistryPath, configPath, commonSection, registryPathKey, defaultRegistryPath);

        copyConstructString(*owner, configuredRegistryPath);
        g_startupRegistryPathOwner = owner;
    }

    void ReleaseStartupRegistryPathOwner()
    {
        delete g_startupRegistryPathOwner;
        g_startupRegistryPathOwner = nullptr;
    }

    void DestroyStartupRegistryPathOwnerForApplicationDestructor()
    {

        delete g_startupRegistryPathOwner;
    }

    const STRING& StartupStringsIniPath()
    {
        return g_startupStringsIniPathOwner ? *g_startupStringsIniPathOwner : g_startupStringsIniPathEmpty;
    }

    void SetStartupStringsIniPath(const STRING& path)
    {
        if (!g_startupStringsIniPathOwner)
            g_startupStringsIniPathOwner = new (std::nothrow) STRING();
        if (g_startupStringsIniPathOwner)
            *g_startupStringsIniPathOwner = path;
    }

    void initializeStartupStringsIniPathOwnerFromDirectory(const STRING& currentDirectory)
    {

        g_startupStringsIniPathOwner = new (std::nothrow) STRING();
        if (!g_startupStringsIniPathOwner)
            return;

        STRING directoryPath;
        constructStringFromCString(directoryPath, directoryOrDot(currentDirectory).c_str());

        STRING stringsIniPath;
        constructConcatenatedString(stringsIniPath, directoryPath.c_str(), "\\Strings.ini");

        resetAndAssignString(*g_startupStringsIniPathOwner, stringsIniPath);
    }

    void InitializeStartupStringsIniPathOwner(const STRING& currentDirectory)
    {
        initializeStartupStringsIniPathOwnerFromDirectory(currentDirectory);
    }

    void ReleaseStartupStringsIniPathOwner()
    {
        delete g_startupStringsIniPathOwner;
        g_startupStringsIniPathOwner = nullptr;
    }

    void DestroyStartupStringsIniPathOwnerForApplicationDestructor()
    {

        delete g_startupStringsIniPathOwner;
    }

    StartupOptions Configuration::BuildStartupOptions(const StartupConfiguration& config)
    {
        StartupOptions options;
        options.resourceRoot = config.resourceRoot;
        options.objectsResource = config.objectsResource;
        options.mapName = config.startMap;
        options.loadGameResources = true;
        options.loadMap = true;
        return options;
    }
} }
