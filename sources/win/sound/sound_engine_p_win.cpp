#include "win/sound/sound_engine_p_win.h"
#include "vorbis/vorbisfile.h"
#include "core/application.h"
#include "core/as_string.h"
#include "core/log.h"
#include "core/file_logger.h"

#ifdef _WIN32
#include <mmsystem.h>
#include <dsound.h>
#include <dshow.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <array>
#include <new>
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace as1 { namespace win { namespace sound
{
    namespace
    {
        constexpr const char* kSoundLogSection = "SOUND";
        constexpr const char* kMusicLogContext = "MUSIC '%s'";
        constexpr const char* kDirectSoundText = "DirectSound";
        constexpr const char* kPriorityLevelText = "PriorityLevel";
        constexpr const char* kCooperativeLevelText = "CooperativeLevel";
        constexpr const char* kPrimarySoundBufferText = "Primary sound buffer";
        constexpr const char* kUnablePlayPrimaryText = "unable play Primary";
        constexpr const char* kFormatPrimaryBufferText = "format primary buffer";
        constexpr const char* kSoundBufferText = "SoundBuffer";
        constexpr const char* kSoundBufferForOggText = "SoundBuffer for ogg";
        constexpr const char* kGraphBuilderText = "GraphBuilder";
        constexpr const char* kMediaControlText = "MediaControl";
        constexpr const char* kMediaSeekingText = "MediaSeeking";
        constexpr const char* kRenderFileText = "RenderFile";
        constexpr const char* kBasicAudioText = "BasicAudio";
        constexpr const char* kVolumeText = "Volume";
        constexpr std::uint32_t kOggStreamBufferSize = 0x40000u;
        constexpr std::uint32_t kOggStreamChunkSize = 0x1000u;
        constexpr std::uint32_t kOggRingBufferMask = kOggStreamBufferSize - 1u;
        constexpr std::uint32_t kOggSoundBufferFlags = 65666u;
        constexpr std::uint16_t kOggPcmFormatTag = 1u;
        constexpr std::uint16_t kOggPcmBitsPerSample = 16u;
        constexpr std::uint16_t kOggPcmCbSize = 18u;
        constexpr const char* kLostSoundBufferErrorText = "!!!ERROR!!! SFXBUFFER::buffer is lost";
        constexpr const char* kDuplicateBufferErrorText = "!!!ERROR!!!SFX:'%s' %X Couldn't duplicate buffer";
        constexpr const char* kWaveFormatText = "fmt ";
        constexpr const char* kWaveDataText = "data";
        constexpr std::uint32_t kDirectSoundStaticBufferFlags = 194u;
        constexpr const char* kOggExtensions[] = {".ogg", ".OGG", ".Ogg"};
#ifdef _WIN32
        constexpr char kStopMusicCommand[] = "stop FWMUSIC";
        constexpr char kCloseMusicCommand[] = "close FWMUSIC";
        constexpr char kOpenCdMusicCommand[] = "open cdaudio alias FWMUSIC shareable";
        constexpr char kSetCdMusicTimeFormatCommand[] = "set FWMUSIC time format tmsf";
        constexpr char kPlayCdMusicFromToNotifyFormat[] = "play FWMUSIC from %i to %i notify";
        constexpr char kPlayMusicNotifyCommand[] = "play FWMUSIC notify";
        constexpr char kPlayMusicToNotifyFormat[] = "play FWMUSIC to %i notify";
#endif


        int soundTruncateFloatToInt32(float value) noexcept
        {
            // The game logic converts the positional float directly with
            // truncating float-to-int conversion.  In particular, NaN/out-of-range produces the 32-bit
            // integer-indefinite value (0x80000000), not the low dword of an
            // intermediate 64-bit conversion.
#if defined(_MSC_VER) && defined(_M_IX86)
            return _mm_cvtt_ss2si(_mm_set_ss(value));
#else
            if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                return std::numeric_limits<int>::min();
            return static_cast<int>(value);
#endif
        }

        int retailNegDoubleAbs32(int value) noexcept
        {
            std::uint32_t raw = static_cast<std::uint32_t>(value);
            if (value < 0)
                raw = 0u - raw;
            raw = 0u - raw;
            raw <<= 1u;
            return static_cast<int>(raw);
        }

        int retailMul4Wrap32(int value) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(value) << 2u);
        }

        char* duplicateSoundName(const char* text)
        {
            if (*text == '\0')
                return as1::STRING::SharedEmptyText();
            const std::size_t length = std::strlen(text);
            auto* copy = static_cast<char*>(::operator new(length + 1));
            std::memcpy(copy, text, length + 1);
            return copy;
        }

        void releaseSoundName(char*& text)
        {
            if (text != as1::STRING::SharedEmptyText())
                ::operator delete(text);
            text = as1::STRING::SharedEmptyText();
        }

        void assignSoundName(char*& destination, const char* source)
        {

            if (destination == source)
                return;

            const std::size_t sourceSize = std::strlen(source) + 1;
            if (std::strlen(destination) != sourceSize - 1)
            {
                if (destination != as1::STRING::SharedEmptyText())
                    ::operator delete(destination);
                destination = sourceSize == 1
                    ? as1::STRING::SharedEmptyText()
                    : static_cast<char*>(::operator new(sourceSize));
            }
            std::strncpy(destination, source, sourceSize);
        }

        bool isOggFileName(const char* name)
        {
            for (const char* extension : kOggExtensions)
            {
                if (std::strstr(name, extension))
                    return true;
            }
            return false;
        }

        std::intptr_t logMusicError(int errorCode, const char* detailText, int detailValue, const char* path)
        {
            return as1::logFileLoggerResourceError(g_fileLogger, kMusicLogContext, errorCode, detailText, detailValue, path);
        }

        void logSharedSoundError(int errorCode, const char* detailText, int detailValue)
        {
            as1::LOG::ResourceError(kSoundLogSection, errorCode, detailText, detailValue);
        }

#ifdef _WIN32
        std::uint32_t soundLoadClockMilliseconds()
        {
            return static_cast<std::uint32_t>(::timeGetTime());
        }
#else
        std::uint32_t soundLoadClockMilliseconds()
        {
            return 0;
        }
#endif

        std::uint32_t g_musicFadeClockMilliseconds = 0;

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_SOUND_STREAM_ENTRY __fastcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_SOUND_STREAM_ENTRY __attribute__((fastcall))
#else
#define AS1_SOUND_STREAM_ENTRY
#endif

        struct MusicStreamOwnerVTable
        {
            void* (AS1_SOUND_STREAM_ENTRY *destroy)(void*, void*, unsigned char);
            int (AS1_SOUND_STREAM_ENTRY *isPlaying)(void*, void*);
            int (AS1_SOUND_STREAM_ENTRY *update)(void*, void*);
            int (AS1_SOUND_STREAM_ENTRY *pause)(void*, void*);
            int (AS1_SOUND_STREAM_ENTRY *resume)(void*, void*);
            int (AS1_SOUND_STREAM_ENTRY *stop)(void*, void*);
            int (AS1_SOUND_STREAM_ENTRY *start)(void*, void*);
            int (AS1_SOUND_STREAM_ENTRY *setVolume)(void*, void*, int);
        };

        struct EmptyMusicStreamOwner
        {
            const MusicStreamOwnerVTable* vtable;
            char* path;
        };

        using OggDecodeState = OggVorbis_File;

        struct OggPcmInfo
        {
            int channels;
            int sampleRate;
        };

        struct OggMusicStreamOwner
        {
            const MusicStreamOwnerVTable* vtable;
            char* path;
            void* buffer;
            int active;
            int endState;
            std::uint32_t reserved20;
            OggDecodeState decoderState;
            std::FILE* file;
            std::uint32_t writePosition;
        };

        struct DirectShowMusicStreamOwner
        {
            const MusicStreamOwnerVTable* vtable;
            char* path;
            void* graphBuilder;
            void* mediaControl;
            void* mediaSeeking;
        };

        struct OggBufferOpenResult
        {
            void* buffer = nullptr;
            std::FILE* file = nullptr;
            OggPcmInfo info{};
        };

        int openOggDecodeStateFromFile(std::FILE* file, OggDecodeState& state)
        {
            return ov_open(file, &state, nullptr, 0);
        }

        void closeOggDecodeState(OggDecodeState& state)
        {
            ov_clear(&state);
        }

        void clearOggDecodeStatePreservingFile(OggDecodeState& state)
        {
            ov_clear_noclose(&state);
        }

        int seekOggDecodeState(OggDecodeState& state, std::int64_t position)
        {
            return ov_raw_seek(&state, static_cast<ogg_int64_t>(position));
        }

        void readOggPcmInfo(OggDecodeState& state, OggPcmInfo& info)
        {

            vorbis_info* const vorbisInfo = ov_info(&state, -1);
            info.channels = vorbisInfo->channels;
            info.sampleRate = static_cast<int>(vorbisInfo->rate);
        }

        int oggPcmTotalSamples(OggDecodeState& state)
        {
            return static_cast<int>(ov_pcm_total(&state, -1));
        }

        int readOggPcmChunk(OggDecodeState& state, unsigned char* output, std::uint32_t outputSize, int& decodeFlag)
        {
            return static_cast<int>(ov_read(
                &state,
                reinterpret_cast<char*>(output),
                static_cast<int>(outputSize),
                0,
                2,
                1,
                &decodeFlag));
        }

#ifdef _WIN32
        void* createDirectSoundOggPcmBuffer(void* directSound, const OggPcmInfo& info, std::uint32_t bufferBytes, int& resultCode)
        {
            resultCode = 0;
            WAVEFORMATEX format;
            std::memset(&format, 0, sizeof(format));
            format.wFormatTag = kOggPcmFormatTag;
            format.nChannels = static_cast<unsigned short>(info.channels);
            format.nSamplesPerSec = static_cast<DWORD>(info.sampleRate);
            format.nAvgBytesPerSec = static_cast<DWORD>(2 * info.sampleRate * info.channels);
            format.nBlockAlign = static_cast<unsigned short>(2 * info.channels);
            format.wBitsPerSample = kOggPcmBitsPerSample;
            format.cbSize = kOggPcmCbSize;

            DSBUFFERDESC description;
            std::memset(&description, 0, sizeof(description));
            description.dwSize = sizeof(description);
            description.dwFlags = kOggSoundBufferFlags;
            description.dwBufferBytes = static_cast<DWORD>(bufferBytes);
            description.lpwfxFormat = &format;

            IDirectSoundBuffer* buffer = nullptr;
            const HRESULT createResult = static_cast<IDirectSound8*>(directSound)->CreateSoundBuffer(&description, &buffer, nullptr);
            resultCode = static_cast<int>(createResult);
            return SUCCEEDED(createResult) ? buffer : nullptr;
        }

        bool writeDecodedOggChunkToDirectSoundBuffer(
            IDirectSoundBuffer* buffer,
            std::uint32_t writeOffset,
            const unsigned char* decodedBlock,
            std::uint32_t decodedBytes)
        {
            void* first = nullptr;
            void* second = nullptr;
            DWORD firstSize = 0;
            DWORD secondSize = 0;
            if (FAILED(buffer->Lock(
                    static_cast<DWORD>(writeOffset),
                    static_cast<DWORD>(decodedBytes),
                    &first,
                    &firstSize,
                    &second,
                    &secondSize,
                    0)))
            {
                return false;
            }

            if (decodedBytes > firstSize)
                secondSize = decodedBytes - firstSize;
            else
            {
                firstSize = decodedBytes;
                secondSize = 0;
            }
            std::memcpy(first, decodedBlock, firstSize);
            if (secondSize != 0)
                std::memcpy(second, decodedBlock + firstSize, secondSize);
            buffer->Unlock(first, firstSize, second, secondSize);
            return true;
        }
#endif

        OggBufferOpenResult createOggBufferFromFile(void* directSound, const char* path, OggDecodeState& state, int requestedBufferBytes)
        {
            OggBufferOpenResult result{};
            if (path[0] == '\0')
            {
                logSharedSoundError(7, path, 0);
                return result;
            }

            result.file = std::fopen(path, "rb");
            if (!result.file)
            {
                logSharedSoundError(7, path, 0);
                return result;
            }

            if (openOggDecodeStateFromFile(result.file, state) < 0)
            {
                logSharedSoundError(4, path, 0);
                std::fclose(result.file);
                result.file = nullptr;
                return result;
            }

            readOggPcmInfo(state, result.info);

            std::uint32_t bufferBytes = static_cast<std::uint32_t>(requestedBufferBytes);
            if (requestedBufferBytes == 0)
                bufferBytes = static_cast<std::uint32_t>(2 * result.info.channels * oggPcmTotalSamples(state));

#ifdef _WIN32
            int createResult = 0;
            result.buffer = createDirectSoundOggPcmBuffer(directSound, result.info, bufferBytes, createResult);
            if (!result.buffer)
                logSharedSoundError(3, kSoundBufferForOggText, createResult);
#else
            (void)directSound;

            closeOggDecodeState(state);
            result.file = nullptr;
#endif
            return result;
        }

        int AS1_SOUND_STREAM_ENTRY emptyMusicStreamNoOp(void*, void*)
        {
            return 0;
        }

        int AS1_SOUND_STREAM_ENTRY emptyMusicStreamSetVolume(void*, void*, int)
        {
            return 0;
        }

        void* AS1_SOUND_STREAM_ENTRY emptyMusicStreamDestroy(void* object, void*, unsigned char flags)
        {
            auto* owner = static_cast<EmptyMusicStreamOwner*>(object);
            EmptyMusicStreamOwner* const self = owner;
            releaseSoundName(owner->path);
            if (flags & 1)
                ::operator delete(static_cast<void*>(self));
            return self;
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamStop(void* object, void*);
        int AS1_SOUND_STREAM_ENTRY oggMusicStreamUpdate(void* object, void*);

        void* AS1_SOUND_STREAM_ENTRY oggMusicStreamDestroy(void* object, void*, unsigned char flags)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            OggMusicStreamOwner* const self = owner;
            oggMusicStreamStop(owner, nullptr);
#ifdef _WIN32
            if (owner->buffer)
                static_cast<IDirectSoundBuffer*>(owner->buffer)->Release();
#endif
            owner->buffer = nullptr;
            if (owner->file)
            {
                std::FILE* const file = owner->file;
                clearOggDecodeStatePreservingFile(owner->decoderState);
                std::fclose(file);
                owner->file = nullptr;
            }
            releaseSoundName(owner->path);
            if (flags & 1)
                ::operator delete(static_cast<void*>(self));
            return self;
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamStart(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer)
                return 0;

#ifdef _WIN32
            auto* buffer = static_cast<IDirectSoundBuffer*>(owner->buffer);
            if (owner->file)
            {
                buffer->Stop();
                std::rewind(owner->file);
                owner->writePosition = 16;
                owner->endState = 0;
                buffer->SetCurrentPosition(0);
                seekOggDecodeState(owner->decoderState, 0);
                oggMusicStreamUpdate(owner, nullptr);
                const int result = static_cast<int>(buffer->Play(0, 0, DSBPLAY_LOOPING));
                owner->active = 1;
                return result;
            }
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(owner->buffer));
#else
            return 0;
#endif
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamStop(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            owner->active = 0;
#ifdef _WIN32
            if (owner->buffer)
                return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->Stop());
#endif
            return 0;
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamPause(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (owner->active == 0 || !owner->buffer)
                return 0;
#ifdef _WIN32
            return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->Stop());
#else
            return 0;
#endif
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamResume(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (owner->active == 0 || !owner->buffer)
                return 0;
#ifdef _WIN32
            return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->Play(0, 0, DSBPLAY_LOOPING));
#else
            return 0;
#endif
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamIsPlaying(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer || owner->active == 0)
                return 0;
#ifdef _WIN32
            DWORD status = 0;
            static_cast<IDirectSoundBuffer*>(owner->buffer)->GetStatus(&status);
            if ((status & DSBSTATUS_PLAYING) == 0)
                owner->active = 0;
#endif
            return owner->active;
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamSetVolume(void* object, void*, int volume)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer)
                return 0;
#ifdef _WIN32
            return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->SetVolume(static_cast<LONG>(volume)));
#else
            return 0;
#endif
        }

        int decodeOggStreamChunk(OggMusicStreamOwner& owner, unsigned char* output, std::uint32_t outputSize, int& decodeFlag)
        {
            return readOggPcmChunk(owner.decoderState, output, outputSize, decodeFlag);
        }

        int AS1_SOUND_STREAM_ENTRY oggMusicStreamUpdate(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer)
                return 0;

#ifdef _WIN32
            DWORD playPosition = 0;
            DWORD writeCursor = 0;
            auto* buffer = static_cast<IDirectSoundBuffer*>(owner->buffer);
            buffer->GetCurrentPosition(&playPosition, &writeCursor);

            std::array<unsigned char, kOggStreamChunkSize> decodedBlock{};
            while (owner->endState == 0)
            {
                const std::uint32_t pendingDistance = playPosition >= owner->writePosition
                    ? playPosition - owner->writePosition
                    : playPosition - owner->writePosition + kOggStreamBufferSize;
                if (pendingDistance < kOggStreamChunkSize)
                    break;

                int decodeFlag = 0;
                const int decodedBytes = decodeOggStreamChunk(
                    *owner,
                    decodedBlock.data(),
                    static_cast<std::uint32_t>(decodedBlock.size()),
                    decodeFlag);

                if (decodedBytes > 0)
                {
                    writeDecodedOggChunkToDirectSoundBuffer(
                        buffer,
                        owner->writePosition,
                        decodedBlock.data(),
                        static_cast<std::uint32_t>(decodedBytes));
                    owner->writePosition = (owner->writePosition + static_cast<std::uint32_t>(decodedBytes)) & kOggRingBufferMask;
                }
                else if (decodedBytes < 0)
                {
                    logMusicError(10, "decode", 0, owner->path);
                }
                else
                {
                    owner->endState = (owner->writePosition < playPosition) ? 2 : 1;
                }
            }

            if (owner->endState == 2 && playPosition < owner->writePosition)
                owner->endState = 1;
            if (owner->endState == 1 && playPosition >= owner->writePosition)
                return oggMusicStreamStop(owner, nullptr);
#endif
            return owner->endState;
        }

#ifdef _WIN32
        void releaseDirectShowInterface(void*& interfacePointer)
        {
            if (interfacePointer)
            {
                static_cast<IUnknown*>(interfacePointer)->Release();
                interfacePointer = nullptr;
            }
        }
#endif

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamStop(void* object, void*);

        void* AS1_SOUND_STREAM_ENTRY directShowMusicStreamDestroy(void* object, void*, unsigned char flags)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            DirectShowMusicStreamOwner* const self = owner;
            directShowMusicStreamStop(owner, nullptr);
#ifdef _WIN32
            releaseDirectShowInterface(owner->mediaSeeking);
            releaseDirectShowInterface(owner->mediaControl);
            releaseDirectShowInterface(owner->graphBuilder);
#endif
            releaseSoundName(owner->path);
            if (flags & 1)
                ::operator delete(static_cast<void*>(self));
            return self;
        }

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamStart(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);

#ifdef _WIN32
            if (owner->mediaSeeking)
            {
                LONGLONG start = 0;
                const HRESULT seekResult = static_cast<IMediaSeeking*>(owner->mediaSeeking)->SetPositions(
                    &start,
                    AM_SEEKING_AbsolutePositioning,
                    nullptr,
                    0);
                if (FAILED(seekResult) && owner->mediaControl)
                    static_cast<IMediaControl*>(owner->mediaControl)->Stop();
            }
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Run());
#endif
            return 0;
        }

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamStop(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
#ifdef _WIN32
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Stop());
#else
            (void)owner;
#endif
            return 0;
        }

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamPause(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
#ifdef _WIN32
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Pause());
#else
            (void)owner;
#endif
            return 0;
        }

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamResume(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
#ifdef _WIN32
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Run());
#else
            (void)owner;
#endif
            return 0;
        }

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamIsPlaying(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            int result = 0;
#ifdef _WIN32
            if (owner->mediaControl)
            {
                OAFilterState state = State_Stopped;
                static_cast<IMediaControl*>(owner->mediaControl)->GetState(3000, &state);
                if (state == State_Running)
                {
                    LONGLONG current = 0;
                    LONGLONG stop = 0;
                    static_cast<IMediaSeeking*>(owner->mediaSeeking)->GetPositions(&current, &stop);
                    if (current != stop)
                        result = 1;
                }
            }
#else
            (void)owner;
#endif
            return result;
        }

        int AS1_SOUND_STREAM_ENTRY directShowMusicStreamSetVolume(void* object, void*, int volume)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
#ifdef _WIN32
            if (!owner->graphBuilder)
                return 0;

            IBasicAudio* basicAudio = nullptr;
            const HRESULT queryResult = static_cast<IGraphBuilder*>(owner->graphBuilder)->QueryInterface(
                IID_IBasicAudio,
                reinterpret_cast<void**>(&basicAudio));
            if (FAILED(queryResult))
            {
                return static_cast<int>(logMusicError(9, kBasicAudioText, static_cast<int>(queryResult), owner->path));
            }

            long oldVolume = 0;
            const HRESULT getResult = basicAudio->get_Volume(&oldVolume);
            if (getResult != E_NOTIMPL)
            {
                if (FAILED(getResult))
                {
                    logMusicError(9, kVolumeText, static_cast<int>(getResult), owner->path);
                }
                else
                {
                    const HRESULT setResult = basicAudio->put_Volume(static_cast<long>(volume));
                    if (FAILED(setResult))
                        logMusicError(8, kVolumeText, static_cast<int>(setResult), owner->path);
                }
            }
            return static_cast<int>(basicAudio->Release());
#else
            (void)owner;
            (void)volume;
            return 0;
#endif
        }

        const MusicStreamOwnerVTable kEmptyMusicStreamOwnerVTable = {
            emptyMusicStreamDestroy,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamSetVolume,
        };

        const MusicStreamOwnerVTable kOggMusicStreamOwnerVTable = {
            oggMusicStreamDestroy,
            oggMusicStreamIsPlaying,
            oggMusicStreamUpdate,
            oggMusicStreamPause,
            oggMusicStreamResume,
            oggMusicStreamStop,
            oggMusicStreamStart,
            oggMusicStreamSetVolume,
        };

        const MusicStreamOwnerVTable kDirectShowMusicStreamOwnerVTable = {
            directShowMusicStreamDestroy,
            directShowMusicStreamIsPlaying,
            emptyMusicStreamNoOp,
            directShowMusicStreamPause,
            directShowMusicStreamResume,
            directShowMusicStreamStop,
            directShowMusicStreamStart,
            directShowMusicStreamSetVolume,
        };

        OggMusicStreamOwner* createOggMusicStreamOwner(const char* path, void* directSound)
        {
            auto* owner = new OggMusicStreamOwner;
            owner->path = duplicateSoundName(path);
            owner->vtable = &kOggMusicStreamOwnerVTable;
            owner->active = 0;

#ifdef _WIN32
            OggBufferOpenResult opened = createOggBufferFromFile(
                directSound,
                owner->path,
                owner->decoderState,
                static_cast<int>(kOggStreamBufferSize));
            owner->buffer = opened.buffer;
            owner->file = opened.file;
#else
            (void)path;
            (void)directSound;
#endif
            if (!owner->buffer)
                logMusicError(3, kSoundBufferText, 0, owner->path);
            return owner;
        }

        DirectShowMusicStreamOwner* createDirectShowMusicStreamOwner(const char* path)
        {
            auto* owner = new DirectShowMusicStreamOwner;
            owner->vtable = &kDirectShowMusicStreamOwnerVTable;
            owner->path = duplicateSoundName(path);
            owner->graphBuilder = nullptr;
            owner->mediaControl = nullptr;
            owner->mediaSeeking = nullptr;

#ifdef _WIN32
            IGraphBuilder* graphBuilder = nullptr;
            HRESULT result = ::CoCreateInstance(
                CLSID_FilterGraph,
                nullptr,
                3u,
                IID_IGraphBuilder,
                reinterpret_cast<void**>(&graphBuilder));
            const HRESULT graphCreateResult = result;
            owner->graphBuilder = graphBuilder;
            if (FAILED(result))
            {
                logMusicError(3, kGraphBuilderText, static_cast<int>(result), owner->path);
                return owner;
            }

            IMediaControl* mediaControl = nullptr;
            result = graphBuilder->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&mediaControl));
            owner->mediaControl = mediaControl;
            if (FAILED(result))
            {
                logMusicError(3, kMediaControlText, static_cast<int>(graphCreateResult), owner->path);
                return owner;
            }

            IMediaSeeking* mediaSeeking = nullptr;
            result = graphBuilder->QueryInterface(IID_IMediaSeeking, reinterpret_cast<void**>(&mediaSeeking));
            owner->mediaSeeking = mediaSeeking;
            if (FAILED(result))
            {
                logMusicError(3, kMediaSeekingText, 0, owner->path);
                return owner;
            }

            std::FILE* file = nullptr;
            if (path[0] != '\0')
                file = std::fopen(path, "rb");
            if (file)
                std::fclose(file);

            if (!file)
            {
                logMusicError(7, path, 0, owner->path);
                return owner;
            }

            WCHAR widePath[1024];
            widePath[0] = L'\0';
            ::MultiByteToWideChar(0, 0, path, -1, widePath, 1024);
            result = graphBuilder->RenderFile(widePath, nullptr);
            if (FAILED(result))
                logMusicError(4, kRenderFileText, static_cast<int>(result), owner->path);
#else
            (void)path;
#endif
            return owner;
        }

        void* streamVtableSlot(void* owner, std::size_t slot)
        {
            return (*static_cast<void***>(owner))[slot];
        }

        void callStreamDestroy(void* owner)
        {
            using Call = void* (AS1_SOUND_STREAM_ENTRY *)(void*, void*, unsigned char);
            (void)reinterpret_cast<Call>(streamVtableSlot(owner, 0))(owner, nullptr, 1);
        }

        int callStreamInt(void* owner, std::size_t slot)
        {
            using Call = int (AS1_SOUND_STREAM_ENTRY *)(void*, void*);
            return reinterpret_cast<Call>(streamVtableSlot(owner, slot))(owner, nullptr);
        }

        void callStreamVoid(void* owner, std::size_t slot)
        {
            using Call = int (AS1_SOUND_STREAM_ENTRY *)(void*, void*);
            (void)reinterpret_cast<Call>(streamVtableSlot(owner, slot))(owner, nullptr);
        }

        int callStreamVolume(void* owner, int volume)
        {
            using Call = int (AS1_SOUND_STREAM_ENTRY *)(void*, void*, int);
            return reinterpret_cast<Call>(streamVtableSlot(owner, 7))(owner, nullptr, volume);
        }

#undef AS1_SOUND_STREAM_ENTRY
    }

    SoundEngineWin::PlayingSlot::PlayingSlot() noexcept
        : buffer(nullptr),
          active(0),
          soundNumber(-1),
          volume(0),
          pan(0),
          startTime(0)
    {
    }

    SoundEngineWin::PlayingSlot::~PlayingSlot()
    {
        if (!buffer)
            return;
#ifdef _WIN32
        const ULONG releaseResult = static_cast<IDirectSoundBuffer*>(buffer)->Release();
        if (releaseResult != 0 && as1::g_fileLogger)
        {
            as1::g_fileLogger->ResourceError(
                "SFXBUFFER[%i]",
                10,
                "SoundBuffer release !=0",
                static_cast<int>(releaseResult),
                soundNumber);
        }
#endif
        buffer = nullptr;
    }

    void SoundEngineWin::resetSoundRequestSlots()
    {

        for (SoundRequestSlot& slot : m_soundRequestSlots)
            slot.soundNumber = -1;
    }

    void SoundEngineWin::validateSoundObjectLayout()
    {
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                                
                                                                                                                      
                                                                                                         
                                                                                                     
                                                                                                            

                                                                                                             
                                                                                          
                                                                                                                  
                                                                                                            
                                                                                                                     

                                                                                                                      
                                                                                                   
                                                                                                        
                                                                                                              
                                                                                                   
                                                                                             
                                                                                                          

                                                                                            
                                                                                                              
                                                                                                         
                                                                                                                 
                                                                                                      
                                                                                                                
                                                                                                             
                                                                                                                     
                                                                                                                
                                                                                                                
                                                                                                                
                                                                                                                
                                                                                                                             
                                                                                                              
                                                                                                                      
                                                                                                                 
                                                                                                                         
                                                                                                                    
                                                                                                                       
                                                                                                                           
                                                                                                                            
                                                                                                               
                                                                                                                                   
                                                                                                             
                                                                                                                
                                                                                                                    

                                                                                                                      
                                                                                                     
                                                                                                      
                                                                                                           
                                                                                                                   
                                                                                                   
                                                                                                                    

                                                                                                                                  
                                                                                                                          
                                                                                                                           
                                                                                                                           
#endif
    }

    SoundEngineWin::~SoundEngineWin()
    {
        Destroy();
    }

    SoundEngineWin* SoundEngineWin::Construct(const SoundEngineStartup& startup)
    {
        return initializeSoundState(startup);
    }

    SoundEngineWin* SoundEngineWin::initializeSoundState(const SoundEngineStartup& startup)
    {
        validateSoundObjectLayout();

        for (SoundRequestSlot& slot : m_soundRequestSlots)
            slot.soundNumber = -1;
        for (PlayingSlot& slot : m_playingSlots)
        {
            slot.buffer = nullptr;
            slot.active = 0;
            slot.soundNumber = -1;
            slot.volume = 0;
            slot.pan = 0;
            slot.startTime = 0;
        }

        m_pendingMusicLoopToken = 0;
        m_loadedSoundCount = 0;
        m_soundTable = nullptr;
        m_directSound = nullptr;
        m_streamOwner = nullptr;
        m_musicFadeVolume = 0;
        m_musicTrackIndex = -1;
        m_cdTrack0 = -1;
        m_cdTrack1 = -1;
        m_cdTrack2 = -1;
        m_cdTrack3 = -1;
        m_initializationState = PendingDirectSound;
        m_waveOutDeviceId = -1;
        m_auxDeviceId = -1;
        m_musicVolumePercent = -1;
        m_masterVolumePercent = 100;
        m_window = startup.window;
        m_musicPath.Assign(as1::STRING::SharedEmptyText());

        unsigned char* const flagsByte = reinterpret_cast<unsigned char*>(&m_flags);
        *flagsByte = static_cast<unsigned char>((*flagsByte & 0xFEu) | (startup.highQuality != 0 ? 1u : 0u));

        initializeDirectSoundDevice();
        loadSoundEffectsFromResource(startup.resource);
        return this;
    }

    void SoundEngineWin::Destroy()
    {
        if (m_streamOwner)
            releaseStreamOwner(m_streamOwner);
        m_streamOwner = nullptr;

        stopSoundSystem();
        releaseSoundTable();

        m_musicPath.ReleaseOwnedStorage();
        for (PlayingSlot& slot : m_playingSlots)
        {
            if (!slot.buffer)
                continue;
#ifdef _WIN32
            const ULONG releaseResult = static_cast<IDirectSoundBuffer*>(slot.buffer)->Release();
            if (releaseResult != 0 && as1::g_fileLogger)
            {
                as1::g_fileLogger->ResourceError(
                    "SFXBUFFER[%i]",
                    10,
                    "SoundBuffer release !=0",
                    static_cast<int>(releaseResult),
                    slot.soundNumber);
            }
#else
            releasePlayingBuffer(slot.buffer);
#endif
            slot.buffer = nullptr;
        }
    }

    bool SoundEngineWin::Bootstrap(const SoundEngineStartup& startup)
    {
        Construct(startup);
        return startup.resource != nullptr;
    }

    bool SoundEngineWin::highQualityEnabled() const noexcept
    {
        return (m_flags & kHighQualityFlag) != 0;
    }


    int SoundEngineWin::loadedSoundBufferCount(int soundNumber) const noexcept
    {
        if (soundNumber < 0 || soundNumber > m_loadedSoundCount || !m_soundTable)
            return 0;
        return m_soundTable[static_cast<std::size_t>(soundNumber)].loadedBufferCount;
    }

    bool SoundEngineWin::passesSfxRepeatGate(int soundNumber) const noexcept
    {

        if (!m_soundTable || soundNumber < 0 || soundNumber > m_loadedSoundCount)
            return false;
        const SoundEffectEntry& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
        return entry.buffers[0] != nullptr && entry.channelCount == 0;
    }

    int SoundEngineWin::playingSoundCount() const noexcept
    {
        if (m_initializationState != Initialized)
            return 0;

        int count = 0;
        for (const PlayingSlot& slot : m_playingSlots)
        {
            if (slot.active != 0)
                ++count;
        }
        return count;
    }

    int SoundEngineWin::prepareSoundPlaybackSlot(int soundNumber)
    {
        if (m_initializationState != Initialized)
            return -1;

        int requestedSoundNumber = soundNumber;
        if (soundNumber < 0 || soundNumber > m_loadedSoundCount)
        {
            logSoundError(4, "nsfx", soundNumber);
            return -1;
        }

        SoundEffectEntry& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
        if (!entry.buffers[0])
        {
            logSoundError(4, "nsfx", soundNumber);
            return -1;
        }

        const std::uint32_t currentTime = as1::core::CurrentTimeMilliseconds();
        for (std::size_t index = 0; index < kPlayingSlotCount; ++index)
        {
            PlayingSlot& slot = m_playingSlots[index];
            if (slot.soundNumber == requestedSoundNumber)
            {
                if (currentTime - slot.startTime <= 0x32u)
                    return -1;

                if (entry.loadedBufferCount == 1 && slot.active == 0)
                    return static_cast<int>(index);
                requestedSoundNumber = soundNumber;
            }
        }

        std::size_t selectedSlot = 0;
        while (selectedSlot < kPlayingSlotCount && m_playingSlots[selectedSlot].soundNumber >= 0)
            ++selectedSlot;

        if (selectedSlot >= kPlayingSlotCount)
        {
            selectedSlot = 0;
            while (selectedSlot < kPlayingSlotCount && m_playingSlots[selectedSlot].active != 0)
                ++selectedSlot;

            if (selectedSlot >= kPlayingSlotCount)
                return -1;

            resetPlayingSlot(m_playingSlots[selectedSlot]);
        }

        void* buffer = acquirePlayableSoundBuffer(entry);
        assignPlayingSlotBuffer(m_playingSlots[selectedSlot], requestedSoundNumber, buffer);
        return static_cast<int>(selectedSlot);
    }

    int SoundEngineWin::stopSound(int soundNumber)
    {

        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;
        if (soundNumber == -1)
            return stopAllPlayingBuffers();
        for (PlayingSlot& slot : m_playingSlots)
        {
            if (slot.soundNumber == soundNumber)
                result = resetPlayingSlot(slot);
        }
        return result;
    }

    int SoundEngineWin::stopSoundNumber(int soundNumber)
    {
        return stopSound(soundNumber);
    }

    int SoundEngineWin::playSoundAtPosition(int soundNumber, float x, float y)
    {

        const int integerX = soundTruncateFloatToInt32(x);
        const int xAttenuation = retailNegDoubleAbs32(integerX);
        const int integerY = soundTruncateFloatToInt32(y);
        int volume = retailNegDoubleAbs32(integerY);
        if (xAttenuation < volume)
            volume = xAttenuation;
        return enqueueSoundRequest(soundNumber, retailMul4Wrap32(integerX), volume);
    }

    int SoundEngineWin::enqueueSoundRequestFromCoordinates(int soundNumber, float x, float y)
    {
        return playSoundAtPosition(soundNumber, x, y);
    }

    int SoundEngineWin::submitSoundRequest(int soundNumber, int pan, int volume)
    {

        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        if (soundNumber < 0 || soundNumber > m_loadedSoundCount ||
            !m_soundTable[static_cast<std::size_t>(soundNumber)].buffers[0])
        {
            return static_cast<int>(static_cast<std::uint32_t>(
                logFileLoggerResourceError(g_fileLogger, "SOUND", 4, "nsfx", soundNumber)));
        }

        const std::uint32_t masterBits =
            (static_cast<std::uint32_t>(m_masterVolumePercent) - 100u) << 5u;
        const int masterVolume = static_cast<int>(masterBits);
        int queuedVolume = static_cast<int>(masterBits + static_cast<std::uint32_t>(volume));
        if (queuedVolume < -3000)
            return masterVolume;

        int queuedPan = pan;
        if (queuedPan > 10000)
            queuedPan = 10000;
        else if (queuedPan < -10000)
            queuedPan = -10000;

        if (soundEntryPriorityByte(soundNumber) == 100)
            queuedVolume = masterVolume;

        for (std::size_t index = 0; index < kSoundRequestSlotCount; ++index)
        {
            SoundRequestSlot& slot = m_soundRequestSlots[index];
            if (slot.soundNumber < 0)
            {
                slot.soundNumber = soundNumber;
                slot.volume = queuedVolume;
                slot.pan = queuedPan;
                return static_cast<int>(index);
            }
        }

        int selectedSlot = 0;
        int sameSoundAlreadyQueued = 0;
        for (int index = 1; index < static_cast<int>(kSoundRequestSlotCount); ++index)
        {
            const SoundRequestSlot& slot = m_soundRequestSlots[static_cast<std::size_t>(index)];
            if (slot.soundNumber == soundNumber)
                sameSoundAlreadyQueued = 1;

            const int currentPriority = soundEntryPriorityByte(slot.soundNumber);
            const int selectedPriority = soundEntryPriorityByte(
                m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)].soundNumber);
            if (currentPriority < selectedPriority)
                selectedSlot = index;
        }

        if (selectedSlot == 0 && sameSoundAlreadyQueued != 0)
            return sameSoundAlreadyQueued;

        SoundRequestSlot& slot = m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)];
        slot.soundNumber = soundNumber;
        slot.volume = queuedVolume;
        slot.pan = queuedPan;
        return static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&slot)));
    }

    int SoundEngineWin::enqueueSoundRequest(int soundNumber, int pan, int volume)
    {
        return submitSoundRequest(soundNumber, pan, volume);
    }

    int SoundEngineWin::updateSoundRequests()
    {

        return updateSoundRequestQueue();
    }

    int SoundEngineWin::updateSoundRequestQueue()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        if (m_streamOwner)
        {
            callStreamVoid(m_streamOwner, 2);
            if (m_musicFadeVolume >= 0)
            {
                if ((std::rand() & 0x0F) == 0 && callStreamInt(m_streamOwner, 1) == 0)
                {
                    if (!m_musicPath.isEmpty())
                    {
                        char* path = duplicateSoundName(m_musicPath.c_str());
                        openMusicStreamFromOwnedPath(path, m_pendingMusicLoopToken);
                    }
                    else
                    {
                        closeMusicPath();
                    }
                }
            }
            else
            {
                const std::uint32_t realTime = as1::core::RealTimeMilliseconds();
                if (g_musicFadeClockMilliseconds == 0 || realTime - g_musicFadeClockMilliseconds > 0x3E8u)
                    g_musicFadeClockMilliseconds = realTime;

                const int baseVolume = m_musicVolumePercent < 0 ? 0 : 32 * (m_musicVolumePercent - 100);
                callStreamVolume(m_streamOwner, baseVolume + m_musicFadeVolume);
                m_musicFadeVolume -= static_cast<int>((realTime - g_musicFadeClockMilliseconds) / 2u);
                if (m_musicFadeVolume < -3000)
                {
                    m_musicFadeVolume = 0;
                    g_musicFadeClockMilliseconds = 0;
                    if (!m_musicPath.isEmpty())
                    {
                        char* path = duplicateSoundName(m_musicPath.c_str());
                        openMusicStreamFromOwnedPath(path, m_pendingMusicLoopToken);
                    }
                    else
                    {
                        closeMusicPath();
                    }
                }
            }
        }

        int queuedRequestCount = 0;
        for (SoundRequestSlot& request : m_soundRequestSlots)
        {
            const int soundNumber = request.soundNumber;
            int matchingPlayingCount = 0;
            if (soundNumber >= 0)
            {
                for (PlayingSlot& playing : m_playingSlots)
                {
                    if (playing.active != 0 && playing.soundNumber == soundNumber)
                    {
                        if (soundNumber >= 0 && soundNumber <= m_loadedSoundCount && m_soundTable)
                        {
                            const SoundEffectEntry& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
                            if (entry.buffers[0] && soundEntryPriorityByte(soundNumber) == 0)
                            {
                                const int volumeDelta = playing.volume - request.volume;
                                if (std::abs(volumeDelta) < 100)
                                {
                                    const int panDelta = playing.pan - request.pan;
                                    if (std::abs(panDelta) < 100)
                                    {
                                        request.soundNumber = -1;
                                        startPlayingSlotBuffer(playing, request.pan, request.volume);
                                        break;
                                    }
                                }
                            }
                        }

                        ++matchingPlayingCount;
                        const int priority = soundEntryPriorityByte(soundNumber);
                        const int allowedMatchingCount = (priority == 100) ? 999999 : (priority != 0 ? priority / 10 : 1);
                        if (matchingPlayingCount > allowedMatchingCount)
                        {
                            request.soundNumber = -1;
                            break;
                        }
                    }
                }

                if (request.soundNumber >= 0)
                    ++queuedRequestCount;
            }
        }

        int freePlayingSlotCount = 0;
        for (PlayingSlot& slot : m_playingSlots)
        {
            if (!updatePlayingSlotStatus(slot))
                ++freePlayingSlotCount;
        }

        if (queuedRequestCount > freePlayingSlotCount)
        {
            do
            {
                int foundSlot = 0;
                int selectedSlot = 0;
                if (m_soundRequestSlots[0].soundNumber < 0)
                    m_soundRequestSlots[0].volume = 0;

                for (std::size_t index = 0; index < kSoundRequestSlotCount; ++index)
                {
                    const SoundRequestSlot& slot = m_soundRequestSlots[index];
                    if (slot.soundNumber >= 0)
                    {
                        if (soundEntryPriorityByte(slot.soundNumber) != 100
                            && slot.volume <= m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)].volume)
                        {
                            foundSlot = 1;
                            selectedSlot = static_cast<int>(index);
                        }
                    }
                }

                if (!foundSlot)
                    break;

                m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)].soundNumber = -1;
                --queuedRequestCount;
            }
            while (queuedRequestCount > freePlayingSlotCount);
        }

        if (queuedRequestCount > freePlayingSlotCount)
        {
            for (PlayingSlot& slot : m_playingSlots)
            {
                if (queuedRequestCount <= freePlayingSlotCount)
                    break;
                if (slot.active != 0 && soundEntryPriorityByte(slot.soundNumber) != 100)
                {
                    slot.active = 0;
                    if (slot.buffer)
                        stopPlayingBufferOnly(slot);
                    ++freePlayingSlotCount;
                }
            }
        }

        for (SoundRequestSlot& request : m_soundRequestSlots)
        {
            result = request.soundNumber;
            if (request.soundNumber >= 0)
            {
                result = prepareSoundPlaybackSlot(request.soundNumber);
                if (result >= 0)
                {
                    PlayingSlot& slot = m_playingSlots[static_cast<std::size_t>(result)];
                    result = startPlayingSlotBuffer(slot, request.pan, request.volume);
                }
                request.soundNumber = -1;
            }
        }
        return result;
    }

    void SoundEngineWin::initializeDirectSoundDevice()
    {
        if (m_initializationState == Initialized)
            return;
        if (m_directSound)
            return;

#ifdef _WIN32
        IDirectSound8* directSound = nullptr;
        HRESULT result = ::DirectSoundCreate8(nullptr, &directSound, nullptr);
        if (FAILED(result))
        {
            m_initializationState = DirectSoundCreateFailed;
            logSoundError(3, kDirectSoundText, static_cast<int>(result));
            return;
        }

        m_directSound = directSound;

        HRESULT cooperativeResult = DS_OK;
        if (highQualityEnabled())
        {
            cooperativeResult = directSound->SetCooperativeLevel(m_window, DSSCL_PRIORITY);
            if (FAILED(cooperativeResult))
            {
                m_flags &= ~kHighQualityFlag;
                logSoundError(8, kPriorityLevelText, static_cast<int>(cooperativeResult));
            }
        }

        if (!highQualityEnabled())
            cooperativeResult = directSound->SetCooperativeLevel(m_window, DSSCL_NORMAL);

        if (FAILED(cooperativeResult))
        {
            directSound->Release();
            m_directSound = nullptr;
            m_initializationState = CooperativeLevelFailed;
            logSoundError(8, kCooperativeLevelText, static_cast<int>(cooperativeResult));
            return;
        }

        DSBUFFERDESC primaryDescription;
        std::memset(&primaryDescription, 0, sizeof(primaryDescription));
        primaryDescription.dwSize = sizeof(primaryDescription);
        primaryDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

        IDirectSoundBuffer* primaryBuffer = nullptr;
        result = directSound->CreateSoundBuffer(&primaryDescription, &primaryBuffer, nullptr);
        if (FAILED(result))
        {
            logSoundError(3, kPrimarySoundBufferText, static_cast<int>(result));
        }
        else
        {
            result = primaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
            if (FAILED(result))
                logSoundError(4, kUnablePlayPrimaryText, 0);

            if (highQualityEnabled())
            {
                WAVEFORMATEX format;
                std::memset(&format, 0, sizeof(format));
                format.wFormatTag = WAVE_FORMAT_PCM;
                format.nChannels = 2;
                format.nSamplesPerSec = 44100;
                format.nAvgBytesPerSec = 176400;
                format.nBlockAlign = 4;
                format.wBitsPerSample = 16;
                format.cbSize = 0;

                result = primaryBuffer->SetFormat(&format);
                if (FAILED(result))
                    logSoundError(8, kFormatPrimaryBufferText, static_cast<int>(result));
            }

            primaryBuffer->Release();
        }

        m_initializationState = Initialized;
        rebuildLoadedSoundBuffers();
#else
        m_initializationState = DirectSoundCreateFailed;
#endif
    }

    void SoundEngineWin::shutdownDirectSoundDevice()
    {
        if (m_initializationState != Initialized)
            return;

        resetLoadedSoundBuffers();
        for (PlayingSlot& slot : m_playingSlots)
            resetPlayingSlot(slot);

#ifdef _WIN32
        const ULONG releaseResult = static_cast<IDirectSound8*>(m_directSound)->Release();
        if (releaseResult != 0 && as1::g_fileLogger)
            as1::g_fileLogger->ResourceError("SOUND", 10, "DirectSound release !=0", static_cast<int>(releaseResult));
        m_directSound = nullptr;
#else
        m_directSound = nullptr;
#endif

        resetSoundRequestSlots();
        m_initializationState = PendingDirectSound;
    }

    void SoundEngineWin::stopDirectSoundAndRestoreWaveVolume()
    {
        if (m_initializationState == Initialized)
        {
            shutdownDirectSoundDevice();
#ifdef _WIN32
            if (m_waveOutDeviceId >= 0)
                ::waveOutSetVolume(reinterpret_cast<HWAVEOUT>(static_cast<std::intptr_t>(m_waveOutDeviceId)), m_waveOutVolume);
#endif
        }
    }

    int SoundEngineWin::closeMusicStreamAndRestoreAuxVolume()
    {
        int result = 0;
        if (m_streamOwner)
        {
            releaseStreamOwner(m_streamOwner);
            m_streamOwner = nullptr;
        }

#ifdef _WIN32
        ::mciSendStringA(kStopMusicCommand, nullptr, 0, nullptr);
        ::mciSendStringA(kCloseMusicCommand, nullptr, 0, nullptr);
        if (m_auxDeviceId >= 0)
            result = static_cast<int>(::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxVolume));
#endif

        m_musicFadeVolume = 0;
        m_musicTrackIndex = -1;
        return result;
    }

    int SoundEngineWin::pauseMusicStreamAndRestoreAuxVolume()
    {
        if (m_streamOwner)
            callStreamVoid(m_streamOwner, 3);

#ifdef _WIN32
        if (m_auxDeviceId >= 0)
            ::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxVolume);
        return ::mciSendStringA(kStopMusicCommand, nullptr, 0, m_window) != 0 ? 1 : 0;
#else
        return 0;
#endif
    }

    int SoundEngineWin::resumeMusicStreamAndRestoreAuxVolume()
    {
        if (m_streamOwner)
            callStreamVoid(m_streamOwner, 4);

#ifdef _WIN32
        if (m_auxDeviceId >= 0)
            ::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxMusicVolume);

        char command[256];
        const char* commandText = kPlayMusicNotifyCommand;
        if (m_cdTrack0 >= 0)
        {
            std::snprintf(command, sizeof(command), kPlayMusicToNotifyFormat, m_musicTrackIndex + 1);
            commandText = command;
        }
        return ::mciSendStringA(commandText, nullptr, 0, m_window) != 0 ? 1 : 0;
#else
        return 0;
#endif
    }

    int SoundEngineWin::playCdTrackSequence(int track0, int track1, int track2, int track3)
    {
        if (track0 < 0)
            return 1;

        if (m_musicTrackIndex >= 0
            && m_cdTrack0 == track0
            && m_cdTrack1 == track1
            && m_cdTrack2 == track2
            && m_cdTrack3 == track3)
        {
            return 0;
        }

        closeMusicPlayback();

        m_cdTrack0 = track0;
        m_cdTrack1 = track1 >= 0 ? track1 : track0;
        m_cdTrack2 = track2 >= 0 ? track2 : track0;
        m_cdTrack3 = track3 >= 0 ? track3 : track0;

#ifdef _WIN32
        char command[256];
        std::snprintf(command, sizeof(command), "%s", kOpenCdMusicCommand);
        if (::mciSendStringA(command, nullptr, 0, nullptr) != 0)
            return 1;

        ::mciSendStringA(kSetCdMusicTimeFormatCommand, nullptr, 0, nullptr);

        m_musicTrackIndex = track0;
        std::snprintf(
            command,
            sizeof(command),
            kPlayCdMusicFromToNotifyFormat,
            track0,
            track0 + 1);
        ::mciSendStringA(command, nullptr, 0, m_window);
#else
        m_musicTrackIndex = track0;
#endif
        return 0;
    }

    int SoundEngineWin::advanceCdTrackSequence()
    {
        // path.  The four physical cells form a fixed sequence rather than a
        // vector/list owner.
        if (m_cdTrack0 < 0)
            return 0;

        int nextTrack = m_musicTrackIndex;
        if (m_musicTrackIndex == m_cdTrack0)
            nextTrack = m_cdTrack1;
        else if (m_musicTrackIndex == m_cdTrack1)
            nextTrack = m_cdTrack2;
        else if (m_musicTrackIndex == m_cdTrack2)
            nextTrack = m_cdTrack3;

        m_musicTrackIndex = nextTrack;
#ifdef _WIN32
        char command[256];
        std::snprintf(
            command,
            sizeof(command),
            kPlayCdMusicFromToNotifyFormat,
            nextTrack,
            nextTrack + 1);
        return ::mciSendStringA(command, nullptr, 0, m_window) != 0 ? 1 : 0;
#else
        return 0;
#endif
    }

    int SoundEngineWin::closeMusicPlayback()
    {
        if (m_streamOwner)
            callStreamVoid(m_streamOwner, 5);
        m_pendingMusicLoopToken = 0;
        m_cdTrack0 = -1;
#ifdef _WIN32
        ::mciSendStringA(kStopMusicCommand, nullptr, 0, nullptr);
        return ::mciSendStringA(kCloseMusicCommand, nullptr, 0, nullptr) != 0 ? 1 : 0;
#else
        return 0;
#endif
    }

    int SoundEngineWin::closeMusicPath()
    {
        return closeMusicPlayback();
    }

    int SoundEngineWin::isMusicPlaying() const
    {

        if (m_streamOwner && callStreamInt(m_streamOwner, 1) != 0)
            return 1;
        return m_pendingMusicLoopToken != 0 ? 1 : 0;
    }

    int SoundEngineWin::stopActivePlayingBuffers()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        for (PlayingSlot& slot : m_playingSlots)
        {
            result = slot.active;
            if (slot.active != 0 && slot.buffer)
                result = stopPlayingBufferOnly(slot);
        }
        return result;
    }

    int SoundEngineWin::restoreActivePlayingBuffers()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        for (PlayingSlot& slot : m_playingSlots)
            result = restorePlayingSlotBuffer(slot);
        return result;
    }

    int SoundEngineWin::pauseAllPlayback()
    {

        return pauseMusicAndStopActiveBuffers();
    }

    int SoundEngineWin::resumeAllPlayback()
    {

        return resumeMusicAndRestoreActiveBuffers();
    }

    int SoundEngineWin::pauseMusicAndStopActiveBuffers()
    {
        pauseMusicStreamAndRestoreAuxVolume();
        return stopActivePlayingBuffers();
    }

    int SoundEngineWin::resumeMusicAndRestoreActiveBuffers()
    {
        resumeMusicStreamAndRestoreAuxVolume();
        return restoreActivePlayingBuffers();
    }

    int SoundEngineWin::applyMasterVolumePercent(int volumePercent)
    {

        int result = volumePercent;
        if (volumePercent < 0)
            result = 0;
        else if (volumePercent > 100)
            result = 100;
        m_masterVolumePercent = result;
        return result;
    }

    int SoundEngineWin::setMasterVolumePercent(int volumePercent)
    {
        return applyMasterVolumePercent(volumePercent);
    }

    int SoundEngineWin::applyMusicVolumePercent(int volumePercent)
    {

        int value = volumePercent;
        if (value < 0)
            value = 0;
        else if (value > 100)
            value = 100;

        if (value == 0)
        {
            if (m_streamOwner)
                closeMusicStreamAndRestoreAuxVolume();
        }
        else if (!m_streamOwner)
        {
#ifdef _WIN32

            const int deviceVolume = 0xFFFF * value / 100;
            m_auxMusicVolume = static_cast<unsigned int>(deviceVolume | (deviceVolume << 16));
            if (m_auxDeviceId >= 0)
                ::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxMusicVolume);
#endif
        }

        m_musicVolumePercent = value;
        int result = 0;
        if (!m_streamOwner && value != 0)
        {
            char* path = duplicateSoundName(m_musicPath.c_str());
            result = openMusicStreamFromOwnedPath(path, m_pendingMusicLoopToken);
        }

        if (m_streamOwner)
            return callStreamVolume(m_streamOwner, 32 * (value - 100));
        return result;
    }

    int SoundEngineWin::setMusicVolumePercent(int volumePercent)
    {
        return applyMusicVolumePercent(volumePercent);
    }


    int SoundEngineWin::playMusicFile(const char* path, int loop)
    {

        const std::uint32_t loopToken = static_cast<std::uint32_t>(loop);
        return selectMusicPath(path, loopToken);
    }

    int SoundEngineWin::selectMusicPath(const char* path, std::uint32_t loopToken)
    {
        if (m_streamOwner)
        {
            m_musicFadeVolume = -1;
            m_musicPath.Assign(path);
            m_pendingMusicLoopToken = loopToken;
            return 0;
        }

        char* ownedPath = duplicateSoundName(path);
        return openMusicStreamFromOwnedPath(ownedPath, loopToken);
    }

    int SoundEngineWin::openMusicStreamFromOwnedPath(char* path, std::uint32_t loopToken)
    {
        char* ownedPath = path;
        if (std::strcmp(ownedPath, as1::STRING::SharedEmptyText()) == 0)
        {
            releaseSoundName(ownedPath);
            return 1;
        }

        m_pendingMusicLoopToken = loopToken;
        m_musicFadeVolume = 0;
        if (loopToken != 0)
            m_musicPath.Assign(ownedPath);
        else
            m_musicPath.Assign(as1::STRING::SharedEmptyText());

        const char* activePath = nullptr;
        if (m_streamOwner)
            activePath = *(static_cast<char**>(m_streamOwner) + 1);

        if (!m_streamOwner || std::strcmp(ownedPath, activePath) != 0)
        {
            if (m_streamOwner)
            {
                releaseStreamOwner(m_streamOwner);
                m_streamOwner = nullptr;
            }

            if (m_musicVolumePercent == 0)
            {
                releaseSoundName(ownedPath);
                return 0;
            }

            m_streamOwner = createMusicStreamOwner(ownedPath);
        }

        if (m_streamOwner)
        {
            if (m_musicVolumePercent > 0)
                callStreamVolume(m_streamOwner, 32 * (m_musicVolumePercent - 100));
            callStreamVoid(m_streamOwner, 6);
        }

        releaseSoundName(ownedPath);
        return 0;
    }

    void SoundEngineWin::stopMusicAndRestoreMixer()
    {

        closeMusicStreamAndRestoreAuxVolume();
    }

    void SoundEngineWin::stopSoundSystem()
    {
        stopMusicAndRestoreMixer();
        stopDirectSoundAndRestoreWaveVolume();
    }

    void SoundEngineWin::loadSoundEffectsFromResource(RESOURCE* resource)
    {
        as1::STRING temporaryNames[8];
        const std::uint32_t startClock = soundLoadClockMilliseconds();

        if (m_initializationState != Initialized)
            return;

        if (!resource->isOpen())
        {
            logSoundError(7, "res", 0);
            return;
        }

        releaseSoundTable();

        m_loadedSoundCount = resource->GetNoSubRes(as1::RESOURCE::ResTypes::SFX);
        if (m_loadedSoundCount == 0)
        {
            logSoundError(11, "SFX ", 0);
            return;
        }

        m_soundTable = new (std::nothrow) SoundEffectEntry[static_cast<std::size_t>(m_loadedSoundCount + 1)];
        if (!m_soundTable)
        {
            logSoundError(2, "LoadSfx", m_loadedSoundCount);
            return;
        }
        for (int i = 0; i <= m_loadedSoundCount; ++i)
            resetSoundEffectEntry(m_soundTable[i]);

        if (resource->GoBegin(as1::RESOURCE::ResTypes::SFX) != 0)
            return;

        int entryIndex = 0;
        do
        {
            unsigned char channelCount = 0;
            resource->read(&channelCount, 1);

            for (as1::STRING& name : temporaryNames)
                name.ReadLine(resource);

            initializeSoundEffectEntryFromResourceNames(m_soundTable[entryIndex], temporaryNames, channelCount);
            loadSoundEffectEntryBuffers(m_soundTable[entryIndex]);
            ++entryIndex;
        }
        while (resource->GoNextSub(as1::RESOURCE::ResTypes::SFX) == 0);

        as1::LOG::Write(
            "LoadSFX::No   =%-15i   sizeof(SFX)   =%-5i    load time     =%ims Quality=%i",
            m_loadedSoundCount,
            72,
            static_cast<unsigned>(soundLoadClockMilliseconds() - startClock),
            highQualityEnabled() ? 1 : 0);
    }

    void SoundEngineWin::resetSoundEffectEntry(SoundEffectEntry& entry)
    {

        for (char*& name : entry.names)
            name = as1::STRING::SharedEmptyText();
        for (void*& buffer : entry.buffers)
            buffer = nullptr;
    }

    void SoundEngineWin::releaseSoundEffectEntry(SoundEffectEntry& entry)
    {

        releaseSoundEffectBuffers(entry);
        for (char*& name : entry.names)
            releaseSoundName(name);
    }

    void SoundEngineWin::releaseSoundEffectBuffers(SoundEffectEntry& entry)
    {

        for (void*& buffer : entry.buffers)
        {
            if (buffer)
            {
                releasePlayingBuffer(buffer);
                buffer = nullptr;
            }
        }
    }

    void* SoundEngineWin::acquirePlayableSoundBuffer(SoundEffectEntry& entry)
    {

        const int selectedIndex = std::rand() % entry.loadedBufferCount;
        void* selectedBuffer = entry.buffers[static_cast<std::size_t>(selectedIndex)];

#ifdef _WIN32
        IDirectSoundBuffer* sourceBuffer = static_cast<IDirectSoundBuffer*>(selectedBuffer);
        const unsigned long referenceCountAfterAdd = sourceBuffer->AddRef();
        if (referenceCountAfterAdd <= 2)
            return sourceBuffer;

        sourceBuffer->Release();

        IDirectSoundBuffer* duplicatedBuffer = nullptr;
        HRESULT result = static_cast<IDirectSound8*>(m_directSound)->DuplicateSoundBuffer(sourceBuffer, &duplicatedBuffer);
        if (FAILED(result))
        {
            as1::LOG::Write(
                kDuplicateBufferErrorText,
                reinterpret_cast<const char*>(&entry),
                static_cast<unsigned>(result));
            return nullptr;
        }
        return duplicatedBuffer;
#else
        return selectedBuffer;
#endif
    }

    void SoundEngineWin::assignPlayingSlotBuffer(PlayingSlot& slot, int soundNumber, void* buffer)
    {
        slot.soundNumber = soundNumber;
        if (slot.buffer)
        {
#ifdef _WIN32
            const ULONG releaseResult = static_cast<IDirectSoundBuffer*>(slot.buffer)->Release();
            if (releaseResult != 0 && as1::g_fileLogger)
            {
                as1::g_fileLogger->ResourceError(
                    "SFXBUFFER[%i]",
                    10,
                    "SoundBuffer(Load()) release !=0",
                    static_cast<int>(releaseResult),
                    soundNumber);
            }
#else
            releasePlayingBuffer(slot.buffer);
#endif
        }
        slot.active = 0;
        slot.buffer = buffer;
        slot.startTime = 0;
    }

    int SoundEngineWin::resetPlayingSlot(PlayingSlot& slot)
    {

        int result = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(slot.buffer)));
#ifdef _WIN32
        if (slot.buffer)
            result = static_cast<int>(static_cast<IDirectSoundBuffer*>(slot.buffer)->Release());
#else
        if (slot.buffer)
            result = 0;
#endif
        slot.buffer = nullptr;
        slot.active = 0;
        slot.soundNumber = -1;
        slot.startTime = 0;
        return result;
    }

    int SoundEngineWin::startPlayingSlotBuffer(PlayingSlot& slot, int pan, int volume)
    {
        int result = slot.soundNumber;
        if (slot.soundNumber < 0)
            return result;
        if (!slot.buffer)
            return 0;

        slot.volume = volume;
        slot.pan = pan;

#ifdef _WIN32
        IDirectSoundBuffer* soundBuffer = static_cast<IDirectSoundBuffer*>(slot.buffer);
        soundBuffer->SetPan(static_cast<LONG>(pan));
        soundBuffer->SetVolume(static_cast<LONG>(volume));

        if (slot.active == 0)
        {
            const DWORD playFlags = soundEntryUsesLoopFlag(slot.soundNumber) ? DSBPLAY_LOOPING : 0u;
            const HRESULT playResult = soundBuffer->Play(0, 0, playFlags);
            if (playResult == DSERR_BUFFERLOST)
            {
                if (as1::g_fileLogger)
                {
                    as1::g_fileLogger->ResourceError(
                        "SFXBUFFER[%i]",
                        10,
                        "buffer is lost",
                        0,
                        slot.soundNumber);
                }
                rebuildSoundBuffersAfterLoss();
            }
        }
#endif

        slot.active = 1;
        const std::uint32_t currentTime = as1::core::CurrentTimeMilliseconds();
        result = static_cast<int>(currentTime);
        slot.startTime = currentTime;
        return result;
    }

    int SoundEngineWin::restorePlayingSlotBuffer(PlayingSlot& slot)
    {
        int result = slot.soundNumber;
        if (slot.soundNumber < 0 || !slot.buffer || slot.active == 0)
            return result;

#ifdef _WIN32
        IDirectSoundBuffer* soundBuffer = static_cast<IDirectSoundBuffer*>(slot.buffer);
        const DWORD playFlags = soundEntryUsesLoopFlag(slot.soundNumber) ? DSBPLAY_LOOPING : 0u;
        result = static_cast<int>(soundBuffer->Play(0, 0, playFlags));
        if (result == DSERR_BUFFERLOST)
        {
            if (as1::g_fileLogger)
                as1::g_fileLogger->WriteLine("!!!ERROR!!! SFXBUFFER::buffer is lost");
            rebuildSoundBuffersAfterLoss();
        }
#endif

        return result;
    }

    int SoundEngineWin::updatePlayingSlotStatus(PlayingSlot& slot)
    {
        if (slot.soundNumber < 0 || !slot.buffer || slot.active == 0)
            return 0;

#ifdef _WIN32
        DWORD status = 0;
        IDirectSoundBuffer* soundBuffer = static_cast<IDirectSoundBuffer*>(slot.buffer);
        soundBuffer->GetStatus(&status);
        if ((status & DSBSTATUS_PLAYING) == 0)
            slot.active = 0;

        if (soundEntryUsesLoopFlag(slot.soundNumber)
            && as1::core::CurrentTimeMilliseconds() - slot.startTime > 0x64u)
        {
            slot.active = 0;
            if (slot.buffer)
                soundBuffer->Stop();
        }
#endif

        return slot.active;
    }

    bool SoundEngineWin::soundEntryUsesLoopFlag(int soundNumber) const noexcept
    {
        if (soundNumber < 0 || soundNumber > m_loadedSoundCount || !m_soundTable)
            return false;

        const SoundEffectEntry& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
        if (!entry.buffers[0])
            return false;

        return entry.channelCount == 0;
    }


    int SoundEngineWin::soundEntryPriorityByte(int soundNumber) const noexcept
    {
        if (soundNumber < 0 || soundNumber > m_loadedSoundCount || !m_soundTable)
            return 0;
        const SoundEffectEntry& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
        if (!entry.buffers[0])
            return 0;
        return static_cast<int>(entry.channelCount);
    }

    void SoundEngineWin::rebuildSoundBuffersAfterLoss()
    {
        as1::LOG::Write(kLostSoundBufferErrorText);
        if (m_loadedSoundCount <= 0)
            return;

        for (int i = 0; i < m_loadedSoundCount; ++i)
        {
            releaseSoundEffectBuffers(m_soundTable[i]);
            loadSoundEffectEntryBuffers(m_soundTable[i]);
        }
    }

    int SoundEngineWin::stopAllPlayingBuffers()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        result = 0;
        for (PlayingSlot& slot : m_playingSlots)
        {
            slot.active = 0;
            if (slot.buffer)
                result = stopPlayingBufferOnly(slot);
        }
        return result;
    }

    int SoundEngineWin::stopPlayingBufferOnly(PlayingSlot& slot)
    {
#ifdef _WIN32
        if (!slot.buffer)
            return 0;
        return static_cast<int>(static_cast<IDirectSoundBuffer*>(slot.buffer)->Stop());
#else
        (void)slot;
        return 0;
#endif
    }

    void SoundEngineWin::initializeSoundEffectEntryFromResourceNames(SoundEffectEntry& entry, const as1::STRING* names, unsigned char channelCount)
    {

        releaseSoundEffectBuffers(entry);
        for (std::size_t i = 0; i < 8; ++i)
            assignSoundName(entry.names[i], names[i].c_str());
        entry.channelCount = channelCount;
    }

    void SoundEngineWin::loadSoundEffectEntryBuffers(SoundEffectEntry& entry)
    {

        as1::RESOURCE waveResource;

        for (std::size_t index = 0; index < 8; ++index)
        {
            const char* name = entry.names[index];
            if (name[0] == '\0')
                return;

            if (isOggFileName(name))
            {
                as1::LOG::ResourceError("SFX", 10, "not supported ogg", 1);
                entry.loadedBufferCount = static_cast<int>(index + 1);
                continue;
            }

            void* buffer = createWaveSoundBufferFromResourceFile(name, waveResource);
            entry.buffers[index] = buffer;
            if (!buffer)
                return;

            copyWaveResourcePayloadToBuffer(waveResource, buffer);
            as1::closeResourceOwner(waveResource);
            entry.loadedBufferCount = static_cast<int>(index + 1);
        }
    }

    void* SoundEngineWin::createWaveSoundBufferFromResourceFile(const char* name, as1::RESOURCE& waveResource)
    {
#ifdef _WIN32
        if (as1::openResourceFileForRead(waveResource, as1::STRING(name), as1::RESOURCE::ResTypes::WAVE) != 0)
            return nullptr;

        if (waveResource.GoBegin(as1::RESOURCE::ResTypes::WAVE_FMT) != 0)
        {
            as1::LOG::Write("!!!ERROR!!!SFX:'%s' 'fmt ' not found", name);
            return nullptr;
        }

        if (waveResource.SubSize() < 14)
        {
            as1::LOG::Write("!!!ERROR!!!SFX:'%s' incorrect size %i", name, waveResource.SubSize());
            return nullptr;
        }

        WAVEFORMATEX format;
        waveResource.read(&format, static_cast<unsigned>(sizeof(format)));

        if (waveResource.GoNext(as1::RESOURCE::ResTypes::WAVE_DATA) != 0 &&
            waveResource.GoBegin(as1::RESOURCE::ResTypes::WAVE_DATA) != 0)
        {
            as1::LOG::Write("!!!ERROR!!!SFX:'%s' 'data' not found", name);
            return nullptr;
        }

        DSBUFFERDESC description;
        std::memset(&description, 0, sizeof(description));
        description.dwSize = sizeof(description);
        description.dwFlags = kDirectSoundStaticBufferFlags;
        description.dwBufferBytes = static_cast<DWORD>(waveResource.SubSize());
        description.lpwfxFormat = &format;

        IDirectSoundBuffer* buffer = nullptr;
        const HRESULT result = static_cast<IDirectSound8*>(m_directSound)->CreateSoundBuffer(&description, &buffer, nullptr);
        if (FAILED(result))
        {
            logSoundError(3, kSoundBufferText, static_cast<int>(result));
            return nullptr;
        }
        return buffer;
#else
        (void)name;
        (void)waveResource;
        return nullptr;
#endif
    }

    bool SoundEngineWin::copyWaveResourcePayloadToBuffer(RESOURCE& resource, void* buffer)
    {
#ifdef _WIN32
        void* first = nullptr;
        void* second = nullptr;
        DWORD firstSize = 0;
        DWORD secondSize = 0;
        IDirectSoundBuffer* soundBuffer = static_cast<IDirectSoundBuffer*>(buffer);
        const DWORD totalSize = static_cast<DWORD>(resource.SubSize());

        const HRESULT result = soundBuffer->Lock(0, totalSize, &first, &firstSize, &second, &secondSize, 0);
        if (SUCCEEDED(result))
        {
            resource.ReadPayload(first, firstSize, nullptr);
            if (secondSize != 0)
                resource.ReadPayload(second, secondSize, nullptr);
            soundBuffer->Unlock(first, firstSize, second, secondSize);
        }
        return true;
#else
        (void)resource;
        (void)buffer;
        return false;
#endif
    }

    void SoundEngineWin::releaseSoundTable()
    {
        if (m_soundTable)
        {
            for (int i = 0; i <= m_loadedSoundCount; ++i)
                releaseSoundEffectEntry(m_soundTable[i]);
            delete[] m_soundTable;
        }
        m_soundTable = nullptr;
        m_loadedSoundCount = 0;
    }

    void SoundEngineWin::releasePlayingBuffer(void* buffer)
    {
#ifdef _WIN32
        if (buffer)
            static_cast<IDirectSoundBuffer*>(buffer)->Release();
#else
        (void)buffer;
#endif
    }

    void SoundEngineWin::releaseStreamOwner(void* owner)
    {
        if (!owner)
            return;
        callStreamDestroy(owner);
    }

    void* SoundEngineWin::createMusicStreamOwner(const char* path)
    {
#ifdef _WIN32
        if (isOggFileName(path))
            return createOggMusicStreamOwner(path, m_directSound);
        return createDirectShowMusicStreamOwner(path);
#else
        (void)path;
        return nullptr;
#endif
    }

    void SoundEngineWin::rebuildLoadedSoundBuffers()
    {
        for (int i = 0; i < m_loadedSoundCount; ++i)
        {
            releaseSoundEffectBuffers(m_soundTable[i]);
            loadSoundEffectEntryBuffers(m_soundTable[i]);
        }
    }

    void SoundEngineWin::resetLoadedSoundBuffers()
    {
        for (int i = 0; i < m_loadedSoundCount; ++i)
            releaseSoundEffectBuffers(m_soundTable[i]);
    }

    void SoundEngineWin::logSoundError(int errorCode, const char* detailText, int detailValue)
    {
        as1::LOG::ResourceError(kSoundLogSection, errorCode, detailText, detailValue);
    }
} } }
