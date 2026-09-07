#include "sound/engine.h"

namespace as1 { namespace sound
{
    namespace
    {
        Engine* g_globalSoundEngine = nullptr;

        win::sound::SoundEngineStartup toPlatformStartup(const EngineStartup& startup)
        {
            win::sound::SoundEngineStartup platform{};
            platform.window = startup.window;
            platform.resource = startup.resource;
            platform.highQuality = startup.highQuality;
            return platform;
        }
    }

    Engine::~Engine() = default;

    Engine* Engine::Construct(const EngineStartup& startup)
    {
        return initializeSoundState(startup);
    }

    Engine* Engine::initializeSoundState(const EngineStartup& startup)
    {
        m_platform.initializeSoundState(toPlatformStartup(startup));
        return this;
    }

    void Engine::Destroy()
    {
        m_platform.Destroy();
    }

    bool Engine::Bootstrap(const EngineStartup& startup)
    {
        return m_platform.Bootstrap(toPlatformStartup(startup));
    }

    bool Engine::highQualityEnabled() const noexcept
    {
        return m_platform.highQualityEnabled();
    }

    int Engine::initializationState() const noexcept
    {
        return m_platform.initializationState();
    }

    int Engine::masterVolumePercent() const noexcept
    {
        return m_platform.masterVolumePercent();
    }

    int Engine::musicVolumePercent() const noexcept
    {
        return m_platform.musicVolumePercent();
    }

    int Engine::loadedSoundCount() const noexcept
    {
        return m_platform.loadedSoundCount();
    }

    int Engine::playingSoundCount() const noexcept
    {
        return m_platform.playingSoundCount();
    }

    int Engine::loadedSoundBufferCount(int soundNumber) const noexcept
    {
        return m_platform.loadedSoundBufferCount(soundNumber);
    }

    bool Engine::passesSfxRepeatGate(int soundNumber) const noexcept
    {
        return m_platform.passesSfxRepeatGate(soundNumber);
    }

    int Engine::prepareSoundPlaybackSlot(int soundNumber)
    {
        return m_platform.prepareSoundPlaybackSlot(soundNumber);
    }

    int Engine::stopSoundNumber(int soundNumber)
    {
        return m_platform.stopSoundNumber(soundNumber);
    }

    int Engine::enqueueSoundRequest(int soundNumber, int pan, int volume)
    {
        return m_platform.enqueueSoundRequest(soundNumber, pan, volume);
    }

    int Engine::enqueueSoundRequestFromCoordinates(int soundNumber, float x, float y)
    {
        return m_platform.enqueueSoundRequestFromCoordinates(soundNumber, x, y);
    }

    int Engine::updateSoundRequests()
    {
        return m_platform.updateSoundRequests();
    }

    int Engine::closeMusicPath()
    {
        return m_platform.closeMusicPath();
    }

    int Engine::playMusicFile(const char* path, int loop)
    {
        return m_platform.playMusicFile(path, loop);
    }

    int Engine::isMusicPlaying() const
    {
        return m_platform.isMusicPlaying();
    }

    int Engine::setMasterVolumePercent(int volumePercent)
    {
        return m_platform.setMasterVolumePercent(volumePercent);
    }

    int Engine::setMusicVolumePercent(int volumePercent)
    {
        return m_platform.setMusicVolumePercent(volumePercent);
    }

    int Engine::pauseAllPlayback()
    {

        return m_platform.pauseAllPlayback();
    }

    int Engine::resumeAllPlayback()
    {

        return m_platform.resumeAllPlayback();
    }

    Engine* GlobalSoundEngine() noexcept
    {
        return g_globalSoundEngine;
    }

    void BindGlobalSoundEngine(Engine* engine) noexcept
    {
        g_globalSoundEngine = engine;
    }

} }
