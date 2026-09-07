#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "base_sprite_list.h"
#include "input.h"
#include "player.h"
#include "core/application.h"
#include "core/configuration.h"

namespace as1
{
    class GRAPH;
    class MAP;
    class RESOURCE;
    class VID;
}

namespace as1 { namespace win
{

    struct ApplicationWinInit
    {
        HINSTANCE hInstance = nullptr;
        HINSTANCE previousInstance = nullptr;
        const char* commandLine = nullptr;
        int showCmd = SW_SHOWDEFAULT;
        const char* shellString = "";
        std::uint32_t startupFlags = 0u;
    };

    class ApplicationWin
    {
    public:
        explicit ApplicationWin(const ApplicationWinInit& init);
        virtual ~ApplicationWin();

        virtual bool dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result);

        ApplicationWin(const ApplicationWin&) = delete;
        ApplicationWin& operator=(const ApplicationWin&) = delete;

        virtual void transferFrom(SPRITE* sprite);

        void clearSpriteReferencesAcrossRuntime(SPRITE* sprite);

        virtual bool pumpOnce();
        int pumpFrame();

        virtual void drawShellOverlays();

        virtual void deinitialize();

        virtual void runCommandLine(char* ownedCommandLine);
        void runCommandLineMap(char* ownedCommandLine);

        bool loadVidDepot(as1::RESOURCE* resource);
        SPRITE* loadSpriteFromMapResource(as1::RESOURCE* resource, int version);
        as1::VID* createVidFromResource(as1::RESOURCE* resource, int nvid);
        void releaseLinkVidRuntime();
        void releaseWorldRuntime();
        ApplicationWin* initializeDerivedApplicationStartup(HINSTANCE instance, HINSTANCE previousInstance, const char** commandLineOwner, int showCmd,
                                   as1::core::StartupSettingsBlock* startupSettings);
        ApplicationWin* initializeBaseApplicationStartup(HINSTANCE instance, HINSTANCE previousInstance, const char** commandLineOwner, int showCmd,
                       as1::core::StartupSettingsBlock* startupSettings);
        int rebuildTerrainGrid();
        void runPostStartupHandoff();
        void drawApplicationDebugOverlayPass();

        virtual void saveMap(const as1::STRING& outputName);

        virtual SPRITE* CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent);

        bool registerMainWindowClass(const as1::core::StartupConfiguration& config, HINSTANCE previousInstance);
        HWND createMainWindow(const as1::core::StartupConfiguration& config);
        HACCEL loadMainAccelerators();
        void showMainWindow(int nShowCmd);

        void destroyDerivedApplicationState();
        void destroyBaseApplicationState();
        void selectMapFilePath(as1::STRING& outPath, bool saveDialog, const char* filter);
        void commandLoadMapFiles();
        void commandSaveCurrent();
        void commandWriteScreenshot();
        void commandRequestExit(HWND hwnd);
        void bindNativeWindow(HWND hwnd, HACCEL accelerator) noexcept;
        void setFramePumpEnabled(bool enabled) noexcept;
        void setRenderControlsEnabled(bool enabled) noexcept;
        void storeInputState(const as1::input::InputMessageState& state) noexcept;
        const as1::input::InputMessageState& inputStateSnapshot() const noexcept;
        void allocateStartupPlayerControls();
        void copyStartupShellMapFields() noexcept;

        bool initialized() const noexcept;
        bool wantsStartupPump() const noexcept;
        void clearStartupShellNibble() noexcept;

        HINSTANCE instance() const noexcept;
        HWND nativeWindow() const noexcept;
        HACCEL accelerator() const noexcept;
        std::string commandLine() const;
        std::string shellString() const;

        std::uint32_t flags() const noexcept;
        void setFlags(std::uint32_t value) noexcept;
        void setPendingCommand(const as1::STRING& value);
        void setCurrentMapName(const as1::STRING& value);
        void setApplicationTitle(const as1::STRING& value);
        void setObjectsResourceName(const as1::STRING& value);
        std::uint32_t shellFlags() const noexcept;
        std::uint32_t frameCounter() const noexcept;
        void resetWorldFrameCounter() noexcept;
        void resetTickScale() noexcept;
        void setWorldStartTime(std::uint32_t value) noexcept;
        std::uint32_t worldStartTime() const noexcept;
        std::uint32_t tickAccumulator() const noexcept;
        int lastVirtualKey() const noexcept;

        float mapExtentX() const noexcept;
        float mapExtentY() const noexcept;
        PLAYER* playerSlotByIndex(int index) const noexcept;
        PLAYER* startupPlayerSlotByIndex(int index) const noexcept;

        SPRITE* controlledSpriteForPlayer(int index) const noexcept;
        std::uint32_t activeStartupPlayerIndex() const noexcept;
    private:
        void initializeBase(const ApplicationWinInit& init);
        void addFlags(std::uint32_t mask) noexcept;
        void removeFlags(std::uint32_t mask) noexcept;
        void toggleFlags(std::uint32_t mask) noexcept;
        void drawApplicationDebugPass();
        bool pumpNativeMessages();
        void updateFrameClock();
        void compactDeferredObjectListsIfNeeded();
        void reportNoVidReleaseResidue();
        void releaseBaseApplicationRuntime();
        void dispatchShellCommandKey();
        bool shouldWaitForMessageGate() const noexcept;
        bool shouldWaitForMessage() const noexcept;
        bool shouldWaitForMessageForDebugGate(bool debugMode) const noexcept;
        bool shouldAbortBeforeFrameCounter() const noexcept;
        int incrementFrameCounterAndProcessDemoFrame(bool sceneActive);
        int processDemoFrame();
        void frameDispatchPrologue(bool worldTick);
        SPRITE* activeAuxiliarySprite() const noexcept;
        void dispatchSpriteCommandMask(SPRITE* target, const std::uint32_t* commandMask) const noexcept;
        SPRITE* childCommandTargetByVidSlot5C(SPRITE* target) const noexcept;
        void dispatchChildCommandIfVidSlot5CMatches(SPRITE* target, const std::uint32_t* commandMask) const noexcept;
        void dispatchPrimaryControlMovementMask();
        void dispatchAuxiliaryControlMovementMask();
        void graphFrameDispatch(bool worldTick);
        void clearControlMovementMasksAfterGraph();
        void dispatchSpriteFrameBuckets(bool worldTick);
        void prepareInputAndDispatchControls(bool worldTick);
        int selectFrameSprite() noexcept;
        as1::STRING* buildMouseTipText(as1::STRING* out);
        void dispatchInputOwner2294() noexcept;
        void updateCameraFromInput();
        bool cameraOwnerReady() const noexcept;
        void dispatchStartupPlayerControls();
        void conditionalOverlayDrawAndPresent(bool worldTick);
        void soundTick();
        void dispatchInputControls(bool worldTick);
        void waitForMessageGate();
        static void splitPackedDirectionalMask(std::uint32_t packed, std::uint32_t& positive, std::uint32_t& negative) noexcept;

    };


    LRESULT CALLBACK applicationWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    ApplicationWin* applicationWinInstance() noexcept;
    ApplicationWin* CreateApplicationWin(const ApplicationWinInit& init);
    void DestroyApplicationWin(ApplicationWin* app);
    void ReleaseApplicationWinHostMapCarrier() noexcept;
} }
#endif
