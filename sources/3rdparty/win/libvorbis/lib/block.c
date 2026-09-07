#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        unsigned char* pcmQueueState(VorbisStateBlob& state)
        {
            return state.bytes + kPcmQueueStateOffset;
        }

        unsigned char* blockDecodeState(VorbisStateBlob& state)
        {
            return state.bytes + kBlockDecodeStateOffset;
        }

        int queueChannelCount(const unsigned char* queue)
        {
            if (!queue)
                return 0;
            const auto* info = static_cast<const unsigned char*>(readBlockPointer(queue, kDecodeStateOwnerOffset));
            return info ? readBlockField<int>(info, kInfoChannelsOffset) : 0;
        }

        float* blockChannelSamples(unsigned char* scratch, int channel)
        {
            if (!scratch || channel < 0)
                return nullptr;
            auto* table = static_cast<unsigned char*>(readBlockPointer(scratch, kBlockDecodedChannelTableOffset));
            if (!table)
                return nullptr;
            return static_cast<float*>(readBlockPointer(table, 4 * static_cast<std::size_t>(channel)));
        }

        int queuedPcmSamples(unsigned char* queue, void** channelCursorList)
        {
            const int cursor = readBlockField<int>(queue, kDecodeStateQueuedCursorOffset);
            const int limit = readBlockField<int>(queue, kDecodeStateQueuedLimitOffset);
            if (cursor <= -1 || cursor >= limit)
                return 0;

            if (channelCursorList)
            {
                const int channels = readBlockField<int>(static_cast<unsigned char*>(readBlockPointer(queue, kDecodeStateOwnerOffset)), 0x04);
                void* cursorList = readBlockPointer(queue, kDecodeStateChannelCursorListOffset);
                void* channelBuffers = readBlockPointer(queue, kDecodeStateChannelBuffersOffset);
                if (cursorList && channelBuffers)
                {
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        void* buffer = readBlockPointer(static_cast<unsigned char*>(channelBuffers), 4 * static_cast<std::size_t>(channel));
                        writeBlockPointer(static_cast<unsigned char*>(cursorList), 4 * static_cast<std::size_t>(channel), static_cast<unsigned char*>(buffer) + 4 * cursor);
                    }
                    *channelCursorList = cursorList;
                }
            }
            return limit - cursor;
        }

        int consumeQueuedPcmSamples(unsigned char* queue, int count)
        {
            if (count && count + readBlockField<int>(queue, kDecodeStateQueuedCursorOffset) > readBlockField<int>(queue, kDecodeStateQueuedLimitOffset))
                return kNotReadyError;
            writeBlockField<int>(queue, kDecodeStateQueuedCursorOffset, readBlockField<int>(queue, kDecodeStateQueuedCursorOffset) + count);
            return 0;
        }


        std::int64_t readBlockSigned64(const unsigned char* base, std::size_t lowOffset)
        {
            const std::uint32_t low = readBlockField<std::uint32_t>(base, lowOffset);
            const std::int32_t high = readBlockField<std::int32_t>(base, lowOffset + 4);
            return (static_cast<std::int64_t>(high) << 32) | low;
        }

        void writeBlockSigned64(unsigned char* base, std::size_t lowOffset, std::int64_t value)
        {
            writeBlockField<std::uint32_t>(base, lowOffset, static_cast<std::uint32_t>(value));
            writeBlockField<std::int32_t>(base, lowOffset + 4, static_cast<std::int32_t>(value >> 32));
        }

        int vorbis_synthesis_blockin(unsigned char* synthesisState, unsigned char* blockState)
        {
            if (!blockState)
                return kNotReadyError;

            const int pcmCurrent = readBlockField<int>(synthesisState, kDecodeStateQueuedLimitOffset);
            const int pcmReturned = readBlockField<int>(synthesisState, kDecodeStateQueuedCursorOffset);
            if (pcmCurrent > pcmReturned && pcmReturned != -1)
                return kNotReadyError;

            auto* info = static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset));
            auto* setup = info ? static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset)) : nullptr;
            if (!info || !setup)
                return kNotReadyError;

            const int previousWindow = readBlockField<int>(synthesisState, kDecodeStateCurrentWindowFlagOffset);
            const int currentWindow = readBlockField<int>(blockState, kBlockLongModeOffset);
            writeBlockField<int>(synthesisState, kDecodeStatePreviousWindowFlagOffset, previousWindow);
            writeBlockField<int>(synthesisState, kDecodeStateCurrentWindowFlagOffset, currentWindow);
            writeBlockField<int>(synthesisState, kDecodeStateNextWindowFlagOffset, -1);

            const std::int64_t previousSequence = readBlockSigned64(synthesisState, kDecodeStateSequenceLowOffset);
            const std::int64_t blockSequence = readBlockSigned64(blockState, kBlockPacketNumberLowOffset);
            if (previousSequence + 1 != blockSequence)
                writeBlockSigned64(synthesisState, kDecodeStateGranuleLowOffset, -1);
            writeBlockSigned64(synthesisState, kDecodeStateSequenceLowOffset, blockSequence);

            auto* blockPcm = static_cast<unsigned char*>(readBlockPointer(blockState, kBlockDecodedChannelTableOffset));
            if (blockPcm)
            {
                const int halfCurrentBlock = readBlockField<int>(setup, 4 * static_cast<std::size_t>(currentWindow)) / 2;
                const int halfSmallBlock = readBlockField<int>(setup, kSetupBlock0Offset) / 2;
                const int halfLargeBlock = readBlockField<int>(setup, kSetupBlock1Offset) / 2;

                writeBlockSigned64(
                    synthesisState,
                    kDecodeStateGlueBitsLowOffset,
                    readBlockSigned64(synthesisState, kDecodeStateGlueBitsLowOffset) +
                        static_cast<std::int64_t>(readBlockField<int>(blockState, 0x58)));
                writeBlockSigned64(
                    synthesisState,
                    kDecodeStateTimeBitsLowOffset,
                    readBlockSigned64(synthesisState, kDecodeStateTimeBitsLowOffset) +
                        static_cast<std::int64_t>(readBlockField<int>(blockState, 0x5C)));
                writeBlockSigned64(
                    synthesisState,
                    kDecodeStateFloorBitsLowOffset,
                    readBlockSigned64(synthesisState, kDecodeStateFloorBitsLowOffset) +
                        static_cast<std::int64_t>(readBlockField<int>(blockState, 0x60)));
                writeBlockSigned64(
                    synthesisState,
                    kDecodeStateResidueBitsLowOffset,
                    readBlockSigned64(synthesisState, kDecodeStateResidueBitsLowOffset) +
                        static_cast<std::int64_t>(readBlockField<int>(blockState, 0x64)));

                const int centerWindow = readBlockField<int>(synthesisState, kDecodeStateCenterWindowOffset);
                const int currentCenter = centerWindow ? halfLargeBlock : 0;
                const int previousCenter = centerWindow ? 0 : halfLargeBlock;

                auto* channelBuffers = static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateChannelBuffersOffset));
                const int channels = readBlockField<int>(info, kInfoChannelsOffset);
                for (int channel = 0; channel < channels; ++channel)
                {
                    auto* target = channelBuffers
                        ? static_cast<float*>(readBlockPointer(channelBuffers, 4 * static_cast<std::size_t>(channel)))
                        : nullptr;
                    auto* source = static_cast<float*>(readBlockPointer(blockPcm, 4 * static_cast<std::size_t>(channel)));
                    if (!target || !source)
                        continue;

                    if (previousWindow)
                    {
                        if (currentWindow)
                        {
                            float* out = target + previousCenter;
                            for (int sample = 0; sample < halfLargeBlock; ++sample)
                                out[sample] += source[sample];
                        }
                        else
                        {
                            float* out = target + previousCenter + halfLargeBlock / 2 - halfSmallBlock / 2;
                            for (int sample = 0; sample < halfSmallBlock; ++sample)
                                out[sample] += source[sample];
                        }
                    }
                    else
                    {
                        float* out = target + previousCenter;
                        if (currentWindow)
                        {
                            const int sourceOffset = halfLargeBlock / 2 - halfSmallBlock / 2;
                            for (int sample = 0; sample < halfSmallBlock; ++sample)
                                out[sample] += source[sourceOffset + sample];

                            const int copyEnd = halfLargeBlock / 2 + halfSmallBlock / 2;
                            for (int sample = halfSmallBlock; sample < copyEnd; ++sample)
                                out[sample] = source[sourceOffset + sample];
                        }
                        else
                        {
                            for (int sample = 0; sample < halfSmallBlock; ++sample)
                                out[sample] += source[sample];
                        }
                    }

                    std::memcpy(
                        target + currentCenter,
                        source + halfCurrentBlock,
                        sizeof(float) * static_cast<std::size_t>(halfCurrentBlock));
                }

                writeBlockField<int>(
                    synthesisState,
                    kDecodeStateCenterWindowOffset,
                    centerWindow ? 0 : halfLargeBlock);

                if (pcmReturned == -1)
                {
                    writeBlockField<int>(synthesisState, kDecodeStateQueuedCursorOffset, currentCenter);
                    writeBlockField<int>(synthesisState, kDecodeStateQueuedLimitOffset, currentCenter);
                }
                else
                {
                    writeBlockField<int>(synthesisState, kDecodeStateQueuedCursorOffset, previousCenter);
                    const int previousQuarter = readBlockField<int>(setup, 4 * static_cast<std::size_t>(previousWindow)) / 4;
                    const int currentQuarter = readBlockField<int>(setup, 4 * static_cast<std::size_t>(currentWindow)) / 4;
                    writeBlockField<int>(
                        synthesisState,
                        kDecodeStateQueuedLimitOffset,
                        previousCenter + previousQuarter + currentQuarter);
                }
            }

            std::int64_t granulePosition = readBlockSigned64(synthesisState, kDecodeStateGranuleLowOffset);
            const std::int64_t blockGranule = readBlockSigned64(blockState, kBlockGranuleLowOffset);
            if (granulePosition == -1)
            {
                if (blockGranule != -1)
                    writeBlockSigned64(synthesisState, kDecodeStateGranuleLowOffset, blockGranule);
            }
            else
            {
                const int previousWindow = readBlockField<int>(synthesisState, kDecodeStatePreviousWindowFlagOffset);
                const int currentWindow = readBlockField<int>(synthesisState, kDecodeStateCurrentWindowFlagOffset);
                granulePosition +=
                    readBlockField<int>(setup, 4 * static_cast<std::size_t>(currentWindow)) / 4 +
                    readBlockField<int>(setup, 4 * static_cast<std::size_t>(previousWindow)) / 4;
                writeBlockSigned64(synthesisState, kDecodeStateGranuleLowOffset, granulePosition);

                if (blockGranule != -1 && granulePosition != blockGranule)
                {
                    if (granulePosition > blockGranule)
                    {
                        const int delta = static_cast<int>(granulePosition - blockGranule);
                        if (readBlockField<int>(blockState, kBlockEndFlagOffset))
                        {
                            writeBlockField<int>(
                                synthesisState,
                                kDecodeStateQueuedLimitOffset,
                                readBlockField<int>(synthesisState, kDecodeStateQueuedLimitOffset) - delta);
                        }
                        else if (readBlockSigned64(blockState, kBlockPacketNumberLowOffset) == 1)
                        {
                            int returned = readBlockField<int>(synthesisState, kDecodeStateQueuedCursorOffset) + delta;
                            const int current = readBlockField<int>(synthesisState, kDecodeStateQueuedLimitOffset);
                            if (returned > current)
                                returned = current;
                            writeBlockField<int>(synthesisState, kDecodeStateQueuedCursorOffset, returned);
                        }
                    }
                    writeBlockSigned64(synthesisState, kDecodeStateGranuleLowOffset, blockGranule);
                }
            }

            if (readBlockField<int>(blockState, kBlockEndFlagOffset))
                writeBlockField<int>(synthesisState, kDecodeStateEndFlagOffset, 1);
            return 0;
        }

        int decodeNextPacketIntoPcmQueue(VorbisStateBlob& state)
        {
            while (true)
            {
                PacketToken packet{};
                const int packetResult = takePacketFromStream(state, packet);
                if (packetResult == -1)
                    return -3;
                if (packetResult <= 0)
                    return packetResult;

                const int synthesisResult = vorbis_synthesis(state, packet);
                if (synthesisResult != 0)
                    continue;

                unsigned char* queue = pcmQueueState(state);
                if (queuedPcmSamples(queue, nullptr))
                    return kPageSearchError;
                const int blockInResult = vorbis_synthesis_blockin(pcmQueueState(state), blockDecodeState(state));
                if (blockInResult)
                    return blockInResult;

                const std::int64_t packetGranule = readBlockSigned64(packet.bytes, 0x10);
                if (packetGranule != -1 && !readBlockField<int>(packet.bytes, 0x0C))
                {
                    int linkIndex = readField<int>(state, kSeekableFlagOffset) ? readField<int>(state, kCurrentLinkOffset) : 0;
                    std::int64_t pcmPosition = packetGranule;
                    auto* lengths = static_cast<unsigned char*>(readPointer(state, kSampleLengthListOffset));
                    if (readField<int>(state, kSeekableFlagOffset) && linkIndex > 0 && lengths)
                    {
                        std::int64_t initialGranule = 0;
                        std::memcpy(&initialGranule, lengths + 16 * static_cast<std::size_t>(linkIndex), sizeof(initialGranule));
                        pcmPosition -= initialGranule;
                    }
                    if (pcmPosition < 0)
                        pcmPosition = 0;
                    pcmPosition -= queuedPcmSamples(queue, nullptr);
                    if (lengths)
                    {
                        for (int index = 0; index < linkIndex; ++index)
                        {
                            std::int64_t linkLength = 0;
                            std::memcpy(&linkLength, lengths + 16 * static_cast<std::size_t>(index) + 8, sizeof(linkLength));
                            pcmPosition += linkLength;
                        }
                    }
                    writeSigned64(state, kPreviousPacketOffsetLowOffset, pcmPosition);
                }
                return 1;
            }
        }

        void releaseBlockDecodeScratch(unsigned char* scratch)
        {
            if (!scratch)
                return;

            void* reap = readBlockPointer(scratch, 0x54);
            while (reap)
            {
                void* next = readBlockPointer(static_cast<unsigned char*>(reap), 0x04);
                std::free(readBlockPointer(static_cast<unsigned char*>(reap), 0x00));
                writeBlockPointer(static_cast<unsigned char*>(reap), 0x00, nullptr);
                writeBlockPointer(static_cast<unsigned char*>(reap), 0x04, nullptr);
                std::free(reap);
                reap = next;
            }
            if (void* localStore = readBlockPointer(scratch, 0x44))
                std::free(localStore);
            if (void* internal = readBlockPointer(scratch, 0x68))
                std::free(internal);
            std::memset(scratch, 0, 0x70);
        }

        void releasePcmQueueState(VorbisStateBlob& state)
        {
            unsigned char* queue = pcmQueueState(state);
            const unsigned char* info = static_cast<const unsigned char*>(readBlockPointer(queue, kDecodeStateOwnerOffset));
            const int channels = info ? readBlockField<int>(info, kInfoChannelsOffset) : 0;
            void* channelBuffers = readBlockPointer(queue, kDecodeStateChannelBuffersOffset);
            if (channelBuffers)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    void* buffer = readBlockPointer(static_cast<unsigned char*>(channelBuffers), 4 * static_cast<std::size_t>(channel));
                    if (buffer)
                        std::free(buffer);
                }
                std::free(channelBuffers);
            }
            if (void* cursorList = readBlockPointer(queue, kDecodeStateChannelCursorListOffset))
                std::free(cursorList);

            if (void* privateOwner = readBlockPointer(queue, kDecodeStatePrivateOwnerOffset))
            {
                auto* privateState = static_cast<unsigned char*>(privateOwner);
                const unsigned char* setup = info ? static_cast<const unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset)) : nullptr;
                if (void* window0 = readBlockPointer(privateState, kPrivateWindow0Offset))
                    std::free(window0);
                if (void* window1 = readBlockPointer(privateState, kPrivateWindow1Offset))
                    std::free(window1);
                for (std::size_t transformOffset : {kPrivateTransform0Offset, kPrivateTransform1Offset})
                {
                    if (auto* transformTable = static_cast<unsigned char*>(readBlockPointer(privateState, transformOffset)))
                    {
                        if (void* transform = readBlockPointer(transformTable, 0x00))
                        {
                            clearMdctLookup(transform);
                            std::free(transform);
                        }
                        std::free(transformTable);
                    }
                }
                releaseSmallFftLookup(privateState + kPrivateFft0Offset);
                releaseSmallFftLookup(privateState + kPrivateFft1Offset);
                const int psyCount = setup ? readBlockField<int>(setup, kSetupPsyCountOffset) : 0;
                if (auto* psyLooks = static_cast<unsigned char*>(readBlockPointer(privateState, kPrivatePsyLookArrayOffset)))
                {
                    for (int index = 0; index < psyCount; ++index)
                        releasePsyLookRecord(psyLooks + 0x30 * static_cast<std::size_t>(index));
                    std::free(psyLooks);
                }
                const int residueCount = setup ? readBlockField<int>(setup, kSetupResidueCountOffset) : 0;
                if (auto* residueLooks = static_cast<unsigned char*>(readBlockPointer(privateState, kPrivateResidueLookArrayOffset)))
                {
                    for (int index = 0; index < residueCount; ++index)
                        releaseResidueLookRecord(readBlockPointer(residueLooks, 4 * static_cast<std::size_t>(index)));
                    std::free(residueLooks);
                }
                if (auto* floorLooks = static_cast<unsigned char*>(readBlockPointer(privateState, kPrivateFloorLookArrayOffset)))
                {
                    const int floorCount = setup ? readBlockField<int>(setup, kSetupFloorCountOffset) : 0;
                    for (int index = 0; index < floorCount; ++index)
                    {
                        if (setup)
                        {
                            const int floorType = readBlockField<int>(setup, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(index));
                            void* look = readBlockPointer(floorLooks, 4 * static_cast<std::size_t>(index));
                            if (floorType == 0)
                                releaseFloor0LookRecord(look);
                            else if (floorType == 1)
                                releaseFloor1LookRecord(look);
                        }
                    }
                    std::free(floorLooks);
                }
                std::memset(privateState, 0, kVorbisPrivateStateSize);
                std::free(privateState);
            }
            std::memset(queue, 0, 0x70);
            releaseBlockDecodeScratch(blockDecodeState(state));
        }

        int initializeVorbisBlockState(unsigned char* synthesisState, unsigned char* blockState)
        {
            std::memset(blockState, 0, 0x70);
            writeBlockPointer(blockState, 0x40, synthesisState);
            writeBlockField<int>(blockState, 0x4C, 0);
            writeBlockField<int>(blockState, 0x44, 0);

            if (readBlockField<int>(synthesisState, 0x00))
            {
                void* internal = std::calloc(1, 0x48);
                writeBlockPointer(blockState, 0x68, internal);

                auto* bitWriter = blockState + 0x04;
                writeBlockField<int>(bitWriter, 0x00, 0);
                writeBlockField<int>(bitWriter, 0x04, 0);
                writeBlockPointer(bitWriter, 0x08, nullptr);
                writeBlockPointer(bitWriter, 0x0C, nullptr);
                writeBlockField<int>(bitWriter, 0x10, 0);
                void* buffer = std::malloc(0x100);
                writeBlockPointer(bitWriter, 0x08, buffer);
                writeBlockPointer(bitWriter, 0x0C, buffer);
                *static_cast<unsigned char*>(buffer) = 0;
                writeBlockField<int>(bitWriter, 0x10, 256);

                writeBlockField<std::uint32_t>(static_cast<unsigned char*>(internal), 0x04, 0xC61C3C00u);
            }
            return 0;
        }

        int initializeVorbisDspState(VorbisStateBlob& state, const unsigned char* info, int encoderFlag)
        {
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            const int channels = readBlockField<int>(info, kInfoChannelsOffset);
            const int block0 = readBlockField<int>(setup, kSetupBlock0Offset);
            const int block1 = readBlockField<int>(setup, kSetupBlock1Offset);

            unsigned char* synthesisState = pcmQueueState(state);
            std::memset(synthesisState, 0, 0x70);
            auto* privateState = static_cast<unsigned char*>(std::calloc(1, kVorbisPrivateStateSize));
            writeBlockPointer(synthesisState, kDecodeStateOwnerOffset, const_cast<unsigned char*>(info));
            writeBlockPointer(synthesisState, kDecodeStatePrivateOwnerOffset, privateState);

            writeBlockField<int>(privateState, kPrivateModeBitsOffset, countBitsForValueMinusOne(readBlockField<int>(setup, kSetupModeCountOffset)));
            auto* transformTable0 = static_cast<unsigned char*>(std::calloc(1, 4));
            auto* transformTable1 = static_cast<unsigned char*>(std::calloc(1, 4));
            writeBlockPointer(privateState, kPrivateTransform0Offset, transformTable0);
            writeBlockPointer(privateState, kPrivateTransform1Offset, transformTable1);
            auto* transform0 = static_cast<unsigned char*>(std::calloc(1, 0x14));
            auto* transform1 = static_cast<unsigned char*>(std::calloc(1, 0x14));
            writeBlockPointer(transformTable0, 0x00, transform0);
            writeBlockPointer(transformTable1, 0x00, transform1);
            initializeMdctLookup(transform0, block0);
            initializeMdctLookup(transform1, block1);
            writeBlockPointer(privateState, kPrivateWindow0Offset, buildVorbisWindow(0, block0 / 2));
            writeBlockPointer(privateState, kPrivateWindow1Offset, buildVorbisWindow(0, block1 / 2));

            if (encoderFlag)
            {
                initializeSmallFftLookup(privateState + kPrivateFft0Offset, block0);
                initializeSmallFftLookup(privateState + kPrivateFft1Offset, block1);
                if (!readBlockPointer(setup, kSetupFullbookArrayOffset))
                {
                    const int bookCount = readBlockField<int>(setup, kSetupBookCountOffset);
                    auto* fullbooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(bookCount), kRuntimeBookSize));
                    writeBlockPointer(setup, kSetupFullbookArrayOffset, fullbooks);
                    for (int index = 0; index < bookCount; ++index)
                    {
                        auto* staticBook = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupBookPointerTableOffset + 4 * static_cast<std::size_t>(index)));
                        initializeRuntimeCodebookRecord(fullbooks + kRuntimeBookSize * static_cast<std::size_t>(index), staticBook);
                    }
                }

                const int psyCount = readBlockField<int>(setup, kSetupPsyCountOffset);
                auto* psyLooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(psyCount), 0x30));
                writeBlockPointer(privateState, kPrivatePsyLookArrayOffset, psyLooks);
                for (int index = 0; index < psyCount; ++index)
                {
                    auto* psyInfo = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupPsyPointerTableOffset + 4 * static_cast<std::size_t>(index)));
                    const int blockFlag = readBlockField<int>(psyInfo, 0x00);
                    const int sampleCount = readBlockField<int>(setup, kSetupBlock0Offset + 4 * static_cast<std::size_t>(blockFlag)) / 2;
                    initializePsyLookRecord(
                        psyLooks + 0x30 * static_cast<std::size_t>(index),
                        psyInfo,
                        reinterpret_cast<const int*>(setup + kSetupPsyGlobalOffset),
                        sampleCount,
                        readBlockField<int>(info, kInfoRateOffset));
                }
                writeBlockField<int>(synthesisState, 0x00, 1);
            }
            else if (!readBlockPointer(setup, kSetupFullbookArrayOffset))
            {
                initializeDecodeCodebooksForSetup(setup);
            }

            writeBlockField<int>(synthesisState, 0x10, block1);
            auto* channelBuffers = static_cast<unsigned char*>(std::malloc(4 * static_cast<std::size_t>(channels)));
            auto* cursorList = static_cast<unsigned char*>(std::malloc(4 * static_cast<std::size_t>(channels)));
            writeBlockPointer(synthesisState, kDecodeStateChannelBuffersOffset, channelBuffers);
            writeBlockPointer(synthesisState, kDecodeStateChannelCursorListOffset, cursorList);
            for (int channel = 0; channel < channels; ++channel)
                writeBlockPointer(channelBuffers, 4 * static_cast<std::size_t>(channel), std::calloc(static_cast<std::size_t>(block1), 4));

            writeBlockField<int>(synthesisState, kDecodeStatePreviousWindowFlagOffset, 0);
            writeBlockField<int>(synthesisState, kDecodeStateCurrentWindowFlagOffset, 0);
            writeBlockField<int>(synthesisState, kDecodeStateCenterWindowOffset, block1 / 2);
            writeBlockField<int>(synthesisState, kDecodeStateQueuedLimitOffset, block1 / 2);

            const int floorCount = readBlockField<int>(setup, kSetupFloorCountOffset);
            auto* floorLooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(floorCount), 4));
            writeBlockPointer(privateState, kPrivateFloorLookArrayOffset, floorLooks);
            for (int index = 0; index < floorCount; ++index)
            {
                const int floorType = readBlockField<int>(setup, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(index));
                const auto* floorInfo = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupFloorPointerTableOffset + 4 * static_cast<std::size_t>(index)));
                void* look = floorType == 0 ? buildFloor0LookRecord(setup, index) : buildFloor1LookRecord(info, floorInfo);
                writeBlockPointer(floorLooks, 4 * static_cast<std::size_t>(index), look);
            }

            const int residueCount = readBlockField<int>(setup, kSetupResidueCountOffset);
            auto* residueLooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(residueCount), 4));
            writeBlockPointer(privateState, kPrivateResidueLookArrayOffset, residueLooks);
            for (int index = 0; index < residueCount; ++index)
                writeBlockPointer(residueLooks, 4 * static_cast<std::size_t>(index), buildResidueLookRecord(setup, index));
            return 0;
        }

        int initializeVorbisSynthesisState(VorbisStateBlob& state, const unsigned char* info)
        {
            // Retail initializeVorbisSynthesisState calls initializeVorbisDspState but deliberately discards its
            // return value, initializes the five decode cursors to -1 and
            // returns zero unconditionally.
            (void)initializeVorbisDspState(state, info, 0);
            unsigned char* synthesisState = pcmQueueState(state);
            writeBlockSigned64(synthesisState, kDecodeStateGranuleLowOffset, -1);
            writeBlockSigned64(synthesisState, kDecodeStateSequenceLowOffset, -1);
            writeBlockField<int>(synthesisState, kDecodeStateQueuedCursorOffset, -1);
            return 0;
        }

        int ensureVorbisDecodeReady(VorbisStateBlob& state)
        {
            int result = 0;
            if (readField<int>(state, kReadyStateOffset) == 3)
            {
                const unsigned char* info = infoRecordForLink(state, -1);
                (void)initializeVorbisSynthesisState(state, info);
                // Retail always initializes the block owner after synthesis
                // state initialization; it does not gate this call on the
                // discarded initializeVorbisDspState result.
                result = initializeVorbisBlockState(pcmQueueState(state), blockDecodeState(state));
                writeField<int>(state, kReadyStateOffset, 4);
            }
            return result;
        }

} } }
