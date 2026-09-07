#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/resource.h"
#include "core/types.h"
#include "core/as_string.h"
#include <array>
#include <cstdint>
#include <cstddef>

namespace as1 { namespace win { namespace sound
{
    struct SoundEngineStartup
    {
#ifdef _WIN32
        HWND window = nullptr;
#else
        void* window = nullptr;
#endif
        RESOURCE* resource = nullptr;
        int highQuality = 0;
    };

    class SoundEngineWin
    {
    public:
#ifdef _MSC_VER
#pragma warning(suppress : 26495)
#endif
        SoundEngineWin() = default;
        ~SoundEngineWin();

        SoundEngineWin* Construct(const SoundEngineStartup& startup);
        SoundEngineWin* initializeSoundState(const SoundEngineStartup& startup);
        void Destroy();
        bool Bootstrap(const SoundEngineStartup& startup);

        bool highQualityEnabled() const noexcept;
        int initializationState() const noexcept { return m_initializationState; }
        int masterVolumePercent() const noexcept { return m_masterVolumePercent; }
        int musicVolumePercent() const noexcept { return m_musicVolumePercent; }
        int loadedSoundCount() const noexcept { return m_loadedSoundCount; }
        int loadedSoundBufferCount(int soundNumber) const noexcept;
        bool passesSfxRepeatGate(int soundNumber) const noexcept;
        int playingSoundCount() const noexcept;
        int prepareSoundPlaybackSlot(int soundNumber);
        int stopSound(int soundNumber);
        int stopSoundNumber(int soundNumber);
        int enqueueSoundRequest(int soundNumber, int pan, int volume);
        int playSoundAtPosition(int soundNumber, float x, float y);
        int submitSoundRequest(int soundNumber, int pan, int volume);
        int enqueueSoundRequestFromCoordinates(int soundNumber, float x, float y);
        int updateSoundRequests();
        int closeMusicPlayback();
        int closeMusicPath();
        int playMusicFile(const char* path, int loop);
        int playCdTrackSequence(int track0, int track1, int track2, int track3);
        int advanceCdTrackSequence();
        int isMusicPlaying() const;
        int setMasterVolumePercent(int volumePercent);
        int setMusicVolumePercent(int volumePercent);
        int pauseAllPlayback();
        int resumeAllPlayback();

    private:
        struct SoundRequestSlot
        {
            std::int32_t soundNumber;
            std::int32_t owner;
            std::int32_t pan;
            std::int32_t volume;
        };

        struct SoundEffectEntry
        {
            char* names[8];
            unsigned char channelCount;
            unsigned char reserved[3];
            void* buffers[8];
            int loadedBufferCount;
        };

        struct PlayingSlot
        {
            PlayingSlot() noexcept;
            virtual ~PlayingSlot();

            void* buffer;
            std::int32_t active;
            std::int32_t soundNumber;
            std::int32_t volume;
            std::int32_t pan;
            std::uint32_t startTime;
        };

        enum InitializationState
        {
            Initialized = 0,
            PendingDirectSound = 1,
            DirectSoundCreateFailed = 2,
            CooperativeLevelFailed = 3,
        };

        void resetSoundRequestSlots();
        static void validateSoundObjectLayout();
        void initializeDirectSoundDevice();
        void shutdownDirectSoundDevice();
        void stopDirectSoundAndRestoreWaveVolume();
        int closeMusicStreamAndRestoreAuxVolume();
        int pauseMusicStreamAndRestoreAuxVolume();
        int resumeMusicStreamAndRestoreAuxVolume();
        int stopActivePlayingBuffers();
        int restoreActivePlayingBuffers();
        int pauseMusicAndStopActiveBuffers();
        int resumeMusicAndRestoreActiveBuffers();
        int selectMusicPath(const char* path, std::uint32_t loopToken);
        int openMusicStreamFromOwnedPath(char* path, std::uint32_t loopToken);
        void stopMusicAndRestoreMixer();
        void stopSoundSystem();
        void loadSoundEffectsFromResource(RESOURCE* resource);
        void resetSoundEffectEntry(SoundEffectEntry& entry);
        void releaseSoundEffectEntry(SoundEffectEntry& entry);
        void releaseSoundEffectBuffers(SoundEffectEntry& entry);
        void* acquirePlayableSoundBuffer(SoundEffectEntry& entry);
        void assignPlayingSlotBuffer(PlayingSlot& slot, int soundNumber, void* buffer);
        int resetPlayingSlot(PlayingSlot& slot);
        int startPlayingSlotBuffer(PlayingSlot& slot, int pan, int volume);
        int restorePlayingSlotBuffer(PlayingSlot& slot);
        int updatePlayingSlotStatus(PlayingSlot& slot);
        int updateSoundRequestQueue();
        int applyMasterVolumePercent(int volumePercent);
        int applyMusicVolumePercent(int volumePercent);
        int stopAllPlayingBuffers();
        int stopPlayingBufferOnly(PlayingSlot& slot);
        bool soundEntryUsesLoopFlag(int soundNumber) const noexcept;
        int soundEntryPriorityByte(int soundNumber) const noexcept;
        void rebuildSoundBuffersAfterLoss();
        void initializeSoundEffectEntryFromResourceNames(SoundEffectEntry& entry, const as1::STRING* names, unsigned char channelCount);
        void loadSoundEffectEntryBuffers(SoundEffectEntry& entry);
        void* createWaveSoundBufferFromResourceFile(const char* name, RESOURCE& resource);
        bool copyWaveResourcePayloadToBuffer(RESOURCE& resource, void* buffer);
        void releaseSoundTable();
        void releasePlayingBuffer(void* buffer);
        void releaseStreamOwner(void* owner);
        void* createMusicStreamOwner(const char* path);
        void rebuildLoadedSoundBuffers();
        void resetLoadedSoundBuffers();
        void logSoundError(int errorCode, const char* detailText, int detailValue);

        static constexpr std::size_t kSoundRequestSlotCount = 32;
        static constexpr std::size_t kPlayingSlotCount = 16;
        static constexpr std::uint32_t kHighQualityFlag = 0x00000001u;

        std::uint32_t m_flags;
        int m_loadedSoundCount;
        SoundEffectEntry* m_soundTable;
        std::array<SoundRequestSlot, kSoundRequestSlotCount> m_soundRequestSlots;

#ifdef _WIN32
        HWND m_window;
#else
        void* m_window;
#endif
        void* m_directSound;
        std::array<PlayingSlot, kPlayingSlotCount> m_playingSlots;

        int m_musicTrackIndex;
        int m_cdTrack0;
        int m_cdTrack1;
        int m_cdTrack2;
        int m_cdTrack3;
        int m_initializationState;
        int m_auxDeviceId;
        int m_waveOutDeviceId;
        unsigned int m_auxVolume;
        unsigned int m_waveOutVolume;
        unsigned int m_auxMusicVolume;
        int m_lastPlaybackStatus;
        int m_masterVolumePercent;
        int m_musicVolumePercent;
        int m_musicFadeVolume;
        std::uint32_t m_pendingMusicLoopToken;
        void* m_streamOwner;
        as1::STRING m_musicPath;
    };
} } }
