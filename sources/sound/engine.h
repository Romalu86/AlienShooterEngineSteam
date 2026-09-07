#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/resource.h"
#include "win/sound/sound_engine_p_win.h"
#include <cstddef>
#include <type_traits>

namespace as1 { namespace sound
{
    struct EngineStartup
    {
#ifdef _WIN32
        HWND window = nullptr;
#else
        void* window = nullptr;
#endif
        RESOURCE* resource = nullptr;
        int highQuality = 0;
    };
    class Engine
    {
    public:
        Engine() = default;
        ~Engine();

        Engine* Construct(const EngineStartup& startup);
        Engine* initializeSoundState(const EngineStartup& startup);
        void Destroy();
        bool Bootstrap(const EngineStartup& startup);

        bool highQualityEnabled() const noexcept;
        int initializationState() const noexcept;
        int masterVolumePercent() const noexcept;
        int musicVolumePercent() const noexcept;
        int loadedSoundCount() const noexcept;
        int loadedSoundBufferCount(int soundNumber) const noexcept;
        bool passesSfxRepeatGate(int soundNumber) const noexcept;
        int playingSoundCount() const noexcept;
        int prepareSoundPlaybackSlot(int soundNumber);
        int stopSoundNumber(int soundNumber);
        int enqueueSoundRequest(int soundNumber, int pan, int volume);
        int enqueueSoundRequestFromCoordinates(int soundNumber, float x, float y);
        int updateSoundRequests();
        int closeMusicPath();
        int playMusicFile(const char* path, int loop);
        int isMusicPlaying() const;
        int setMasterVolumePercent(int volumePercent);
        int setMusicVolumePercent(int volumePercent);
        int pauseAllPlayback();
        int resumeAllPlayback();

    private:
        win::sound::SoundEngineWin m_platform;
    };

#if defined(_MSC_VER) && defined(_M_IX86)

                                                                                                                    
                                                                                                 
#endif

    Engine* GlobalSoundEngine() noexcept;
    void BindGlobalSoundEngine(Engine* engine) noexcept;
} }
