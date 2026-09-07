#pragma once

#include "core/as_string.h"

#include <cstddef>
#include <cstdint>

namespace as1
{
    struct StartupOptions;
}

namespace as1 { namespace core
{
    constexpr std::uint32_t StartupVSyncFlag = 1u << 1;
    constexpr std::uint32_t StartupDialogFullscreenFlag = 1u << 2;

    struct VideoConfiguration
    {
        int device = 0;
        int screenX = 640;
        int screenY = 480;
        int colorDepth = 32;
        bool fullscreen = true;
        bool vsync = true;
        int windowPositionX = 0;
        int windowPositionY = 0;
    };

    struct FontConfiguration
    {
        STRING face{"Arial"};
        int sizeX = 0;
        int sizeY = 8;
    };

    struct SoundConfiguration
    {
        bool highQuality = false;
    };

    struct ControlConfiguration
    {
        STRING left{"\x25"};
        STRING right{"\x27"};
        STRING up{"\x26"};
        STRING down{"\x04\x38"};
        STRING firstAction{"LBUTTON"};
        STRING secondAction{"RBUTTON"};
        STRING previousWeapon{"["};
        STRING nextWeapon{"]"};
        std::uint32_t relative = 0;
    };

    struct StartupSettingsBlock
    {
        char title[0x100];                         // +0x000 String
        std::uint32_t allowedWidths[32];
        std::uint32_t allowedHeights[32];
        std::uint32_t allowedColorBits[8];
        std::uint32_t flags;
        std::int32_t device;               // +0x224
        std::int32_t screenWidth;              // +0x228 = 1024
        std::int32_t screenHeight;              // +0x22C = 768
        std::int32_t colorDepth;                  // +0x230 = 32
        std::int32_t fullscreen;           // +0x234 = 1
    };

                                                                                                                             
                                                                                                                               
                                                                                                                              
                                                                                                         
                                                                                                          
                                                                                                               
                                                                                                                
                                                                                                              
                                                                                                              
                                                                                                      

    StartupSettingsBlock& StartupSettings() noexcept;

    struct StartupConfiguration
    {
        STRING configPath;
        STRING stringsPath;
        STRING applicationName{"AlienShooter"};
        STRING applicationTitle{"AlienShooter"};
        STRING registryPath{"SOFTWARE\\Gromada\\AlienShooter"};
        STRING resourceRoot{"."};
        STRING objectsResource{"objects.res"};
        STRING startMap{"maps\\logo.map"};
        bool showStartDialog = true;
        bool startDialogFullscreen = false;
        int debugMode = 0;
        int drawFps = 0;
        int drawPresentation = 1;
        int steamAppId = -1;
        unsigned int startupFlags = 0;
        VideoConfiguration video;
        FontConfiguration font;
        SoundConfiguration sound;
        ControlConfiguration control;
    };

    class Configuration
    {
    public:
        static StartupConfiguration LoadStartupConfiguration(const STRING& executableName,
                                                             const STRING& commandLine,
                                                             const STRING& currentDirectory);
        static StartupOptions BuildStartupOptions(const StartupConfiguration& config);

    private:
        static STRING ResolveConfigPath(const STRING& executableName,
                                        const STRING& commandLine,
                                        const STRING& currentDirectory);
    };

    const STRING& StartupRegistryPath();
    void SetStartupRegistryPath(const STRING& path);
    void initializeStartupRegistryPathOwnerFromProfile(const STRING& configPath, const STRING& lpClassName);
    void ReleaseStartupRegistryPathOwner();
    void DestroyStartupRegistryPathOwnerForApplicationDestructor();

    const STRING& StartupStringsIniPath();
    void SetStartupStringsIniPath(const STRING& path);
    void InitializeStartupStringsIniPathOwner(const STRING& currentDirectory);
    void initializeStartupStringsIniPathOwnerFromDirectory(const STRING& currentDirectory);
    void ReleaseStartupStringsIniPathOwner();
    void DestroyStartupStringsIniPathOwnerForApplicationDestructor();


    void initializePostComProfileOwners(StartupConfiguration& config);
    void readStartupStartDialogProfileBlock(StartupConfiguration& config);
    void readStartupWindowPositionRegistryBlock(StartupConfiguration& config, bool graphFullscreen);

    void readStartupGraphFontProfileBlock(StartupConfiguration& config);

    void readStartupResourceProfileBlock(StartupConfiguration& config);

    void readStartupSoundHighQualityRegistryBlock(StartupConfiguration& config);

    void readStartupDebugModeProfileBlock(StartupConfiguration& config);

    void readStartupControlProfileBlock(StartupConfiguration& config);

    void readStartupStartMapProfileBlock(StartupConfiguration& config, const STRING& commandLine);
} }
