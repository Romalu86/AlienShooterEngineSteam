#include "3rdparty/win/libvorbis/lib/codec_internal.h"
#if defined(_MSC_VER)
#include <malloc.h>
#define AS1_XIPH_ALLOCA _alloca
#else
#include <alloca.h>
#define AS1_XIPH_ALLOCA alloca
#endif

namespace as1 { namespace thirdparty { namespace xiph2003
{
        void releaseMapping0Record(void* record)
        {
            if (!record)
                return;
            std::memset(record, 0, 0xC88);
            std::free(record);
        }


        void* parseMapping0SetupRecord(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return nullptr;
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            if (!setup)
                return nullptr;

            auto* mapping = static_cast<unsigned char*>(std::calloc(1, 0xC88));
            if (!mapping)
                return nullptr;

            const int channels = readBlockField<int>(info, kInfoChannelsOffset);
            if (readBits(cursor, 1))
                writeBlockField<int>(mapping, 0x00, readBits(cursor, 4) + 1);
            else
                writeBlockField<int>(mapping, 0x00, 1);

            if (readBits(cursor, 1))
            {
                const int couplingSteps = readBits(cursor, 8) + 1;
                writeBlockField<int>(mapping, 0x484, couplingSteps);
                for (int index = 0; index < couplingSteps; ++index)
                {
                    const int bits = countBitsForValueMinusOne(channels);
                    const int magnitude = readBits(cursor, bits);
                    const int angle = readBits(cursor, bits);
                    writeBlockField<int>(mapping, 0x488 + 4 * static_cast<std::size_t>(index), magnitude);
                    writeBlockField<int>(mapping, 0x888 + 4 * static_cast<std::size_t>(index), angle);
                    if (magnitude < 0 || angle < 0 || magnitude == angle || magnitude >= channels || angle >= channels)
                    {
                        releaseMapping0Record(mapping);
                        return nullptr;
                    }
                }
            }

            if (readBits(cursor, 2) > 0)
            {
                releaseMapping0Record(mapping);
                return nullptr;
            }

            if (readBlockField<int>(mapping, 0x00) > 1)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    const int mux = readBits(cursor, 4);
                    writeBlockField<int>(mapping, 0x04 + 4 * static_cast<std::size_t>(channel), mux);
                    if (mux >= readBlockField<int>(mapping, 0x00))
                    {
                        releaseMapping0Record(mapping);
                        return nullptr;
                    }
                }
            }

            for (int submap = 0; submap < readBlockField<int>(mapping, 0x00); ++submap)
            {
                readBits(cursor, 8);
                const int floorNumber = readBits(cursor, 8);
                writeBlockField<int>(mapping, 0x404 + 4 * static_cast<std::size_t>(submap), floorNumber);
                if (floorNumber >= readBlockField<int>(setup, kSetupFloorCountOffset))
                {
                    releaseMapping0Record(mapping);
                    return nullptr;
                }
                const int residueNumber = readBits(cursor, 8);
                writeBlockField<int>(mapping, 0x444 + 4 * static_cast<std::size_t>(submap), residueNumber);
                if (residueNumber >= readBlockField<int>(setup, kSetupResidueCountOffset))
                {
                    releaseMapping0Record(mapping);
                    return nullptr;
                }
            }
            return mapping;
        }

        void applyInverseCouplingBoundary(unsigned char* scratch, const unsigned char* mapping, int channels)
        {
            if (!scratch || !mapping || channels <= 0)
                return;
            const int samples = readBlockField<int>(scratch, kBlockDecodedSampleCountOffset) / 2;
            const int couplingSteps = readBlockField<int>(mapping, 0x484);
            for (int step = couplingSteps - 1; step >= 0; --step)
            {
                const int magnitude = readBlockField<int>(mapping, 0x488 + 4 * static_cast<std::size_t>(step));
                const int angle = readBlockField<int>(mapping, 0x888 + 4 * static_cast<std::size_t>(step));
                if (magnitude < 0 || angle < 0 || magnitude == angle || magnitude >= channels || angle >= channels)
                    continue;
                float* magSamples = blockChannelSamples(scratch, magnitude);
                float* angleSamples = blockChannelSamples(scratch, angle);
                if (!magSamples || !angleSamples)
                    continue;
                for (int i = 0; i < samples; ++i)
                {
                    const float mag = magSamples[i];
                    const float ang = angleSamples[i];
                    if (mag > 0.0f)
                    {
                        if (ang > 0.0f)
                        {
                            magSamples[i] = mag;
                            angleSamples[i] = mag - ang;
                        }
                        else
                        {
                            angleSamples[i] = mag;
                            magSamples[i] = mag + ang;
                        }
                    }
                    else
                    {
                        if (ang > 0.0f)
                        {
                            magSamples[i] = mag;
                            angleSamples[i] = mag + ang;
                        }
                        else
                        {
                            angleSamples[i] = mag;
                            magSamples[i] = mag - ang;
                        }
                    }
                }
            }
        }

        int modeIndexBitCount(const unsigned char* setup)
        {
            if (!setup)
                return 0;
            unsigned int value = static_cast<unsigned int>(readBlockField<int>(setup, kSetupModeCountOffset));
            if (!value)
                return 0;
            value -= 1;
            int bits = 0;
            while (value)
            {
                ++bits;
                value >>= 1;
            }
            return bits;
        }

        const unsigned char* setupFromInfo(const unsigned char* info)
        {
            if (!info)
                return nullptr;
            return static_cast<const unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
        }

        const unsigned char* modeRecordFromPacket(const unsigned char* setup, const PacketToken& packet, int* modeIndex)
        {
            if (modeIndex)
                *modeIndex = -1;
            if (!setup)
                return nullptr;

            BitCursor cursor{};
            initializeBitCursor(cursor, readBlockPointer(packet.bytes, 0x00), readBlockField<int>(packet.bytes, 0x04));
            if (readBits(cursor, 1))
                return nullptr;

            const int bits = modeIndexBitCount(setup);
            const int index = readBits(cursor, bits);
            if (index < 0 || index >= readBlockField<int>(setup, kSetupModeCountOffset))
                return nullptr;
            if (modeIndex)
                *modeIndex = index;
            return static_cast<const unsigned char*>(readBlockPointer(setup, kSetupModePointerTableOffset + 4 * static_cast<std::size_t>(index)));
        }

        const unsigned char* mappingRecordFromMode(const unsigned char* setup, const unsigned char* mode)
        {
            if (!setup || !mode)
                return nullptr;
            const int mappingIndex = readBlockField<int>(mode, 0x0C);
            if (mappingIndex < 0 || mappingIndex >= readBlockField<int>(setup, kSetupMappingCountOffset))
                return nullptr;
            const int mappingType = readBlockField<int>(setup, kSetupMappingTypeTableOffset + 4 * static_cast<std::size_t>(mappingIndex));
            if (mappingType != 0)
                return nullptr;
            return static_cast<const unsigned char*>(readBlockPointer(setup, kSetupMappingPointerTableOffset + 4 * static_cast<std::size_t>(mappingIndex)));
        }

        int parseAudioPacketMode(const unsigned char* info, const PacketToken& packet, int* modeIndex, int* previousWindowFlag, int* nextWindowFlag)
        {
            if (modeIndex)
                *modeIndex = -1;
            if (previousWindowFlag)
                *previousWindowFlag = 0;
            if (nextWindowFlag)
                *nextWindowFlag = 0;

            const unsigned char* setup = setupFromInfo(info);
            if (!setup)
                return kParserClosed;

            BitCursor cursor{};
            initializeBitCursor(cursor, readBlockPointer(packet.bytes, 0x00), readBlockField<int>(packet.bytes, 0x04));
            if (readBits(cursor, 1))
                return -135;

            const int index = readBits(cursor, modeIndexBitCount(setup));
            if (index == -1 || index >= readBlockField<int>(setup, kSetupModeCountOffset))
                return -136;

            const unsigned char* mode = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupModePointerTableOffset + 4 * static_cast<std::size_t>(index)));
            if (!mode)
                return kParserClosed;
            if (modeIndex)
                *modeIndex = index;
            if (readBlockField<int>(mode, 0x00))
            {
                if (previousWindowFlag)
                    *previousWindowFlag = readBits(cursor, 1);
                if (nextWindowFlag)
                    *nextWindowFlag = readBits(cursor, 1);
                if ((previousWindowFlag && *previousWindowFlag == -1) || (nextWindowFlag && *nextWindowFlag == -1))
                    return -136;
            }
            return 0;
        }

        int inverseMapping0(unsigned char* block, const unsigned char* mapping)
        {
            if (!block || !mapping)
                return kParserClosed;
            auto* synthesisState = static_cast<unsigned char*>(readBlockPointer(block, kBlockQueueOwnerOffset));
            auto* info = synthesisState ? static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset)) : nullptr;
            auto* privateState = synthesisState ? static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStatePrivateOwnerOffset)) : nullptr;
            auto* setup = info ? static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset)) : nullptr;
            if (!synthesisState || !info || !privateState || !setup)
                return kParserClosed;

            const int W = readBlockField<int>(block, kBlockLongModeOffset);
            const int n = readBlockField<int>(setup, 4 * static_cast<std::size_t>(W));
            writeBlockField<int>(block, kBlockDecodedSampleCountOffset, n);
            const int channels = readBlockField<int>(info, kInfoChannelsOffset);
            if (channels <= 0)
                return kParserClosed;

            auto** pcmbundle = static_cast<float**>(AS1_XIPH_ALLOCA(sizeof(float*) * static_cast<std::size_t>(channels)));
            auto* zerobundle = static_cast<int*>(AS1_XIPH_ALLOCA(sizeof(int) * static_cast<std::size_t>(channels)));
            auto* nonzero = static_cast<int*>(AS1_XIPH_ALLOCA(sizeof(int) * static_cast<std::size_t>(channels)));
            auto** floormemo = static_cast<void**>(AS1_XIPH_ALLOCA(sizeof(void*) * static_cast<std::size_t>(channels)));
            auto* floorLooks = static_cast<unsigned char*>(readBlockPointer(privateState, kPrivateFloorLookArrayOffset));
            auto* residueLooks = static_cast<unsigned char*>(readBlockPointer(privateState, kPrivateResidueLookArrayOffset));

            for (int channel = 0; channel < channels; ++channel)
            {
                const int submap = readBlockField<int>(mapping, 0x04 + 4 * static_cast<std::size_t>(channel));
                const int floorIndex = readBlockField<int>(mapping, 0x404 + 4 * static_cast<std::size_t>(submap));
                const int floorType = readBlockField<int>(setup, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(floorIndex));
                auto* floorLook = floorLooks
                    ? static_cast<unsigned char*>(readBlockPointer(floorLooks, 4 * static_cast<std::size_t>(floorIndex)))
                    : nullptr;
                void* memo = nullptr;
                if (floorType == 0)
                    memo = decodeFloor0Memo(block, floorLook);
                else if (floorType == 1)
                    memo = decodeFloor1Memo(block, floorLook);
                floormemo[channel] = memo;
                nonzero[channel] = memo != nullptr;
                if (float* pcm = blockChannelSamples(block, channel))
                    std::memset(pcm, 0, 2 * static_cast<std::size_t>(n));
            }

            const int couplingSteps = readBlockField<int>(mapping, 0x484);
            for (int step = 0; step < couplingSteps; ++step)
            {
                const int magnitude = readBlockField<int>(mapping, 0x488 + 4 * static_cast<std::size_t>(step));
                const int angle = readBlockField<int>(mapping, 0x888 + 4 * static_cast<std::size_t>(step));
                if (nonzero[magnitude] || nonzero[angle])
                {
                    nonzero[magnitude] = 1;
                    nonzero[angle] = 1;
                }
            }

            const int submaps = readBlockField<int>(mapping, 0x00);
            for (int submap = 0; submap < submaps; ++submap)
            {
                int bundleCount = 0;
                for (int channel = 0; channel < channels; ++channel)
                {
                    if (readBlockField<int>(mapping, 0x04 + 4 * static_cast<std::size_t>(channel)) == submap)
                    {
                        zerobundle[bundleCount] = nonzero[channel];
                        pcmbundle[bundleCount] = blockChannelSamples(block, channel);
                        ++bundleCount;
                    }
                }
                const int residueIndex = readBlockField<int>(mapping, 0x444 + 4 * static_cast<std::size_t>(submap));
                const int residueType = readBlockField<int>(setup, kSetupResidueTypeTableOffset + 4 * static_cast<std::size_t>(residueIndex));
                const auto* residueLook = residueLooks
                    ? static_cast<const unsigned char*>(readBlockPointer(residueLooks, 4 * static_cast<std::size_t>(residueIndex)))
                    : nullptr;
                if (residueType == 0)
                    inverseResidue0(block, residueLook, pcmbundle, zerobundle, bundleCount);
                else if (residueType == 1)
                    inverseResidue1(block, residueLook, pcmbundle, zerobundle, bundleCount);
                else if (residueType == 2)
                    inverseResidue2(block, residueLook, pcmbundle, zerobundle, bundleCount);
            }

            applyInverseCouplingBoundary(block, mapping, channels);

            for (int channel = 0; channel < channels; ++channel)
            {
                const int submap = readBlockField<int>(mapping, 0x04 + 4 * static_cast<std::size_t>(channel));
                const int floorIndex = readBlockField<int>(mapping, 0x404 + 4 * static_cast<std::size_t>(submap));
                const int floorType = readBlockField<int>(setup, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(floorIndex));
                auto* floorLook = floorLooks
                    ? static_cast<unsigned char*>(readBlockPointer(floorLooks, 4 * static_cast<std::size_t>(floorIndex)))
                    : nullptr;
                float* pcm = blockChannelSamples(block, channel);
                if (floorType == 0)
                    applyFloor0Memo(block, floorLook, floormemo[channel], pcm);
                else if (floorType == 1)
                    applyFloor1Memo(block, floorLook, floormemo[channel], pcm);
            }

            for (int channel = 0; channel < channels; ++channel)
            {
                float* pcm = blockChannelSamples(block, channel);
                auto* transformTable = static_cast<unsigned char*>(readBlockPointer(
                    privateState, W ? kPrivateTransform1Offset : kPrivateTransform0Offset));
                void* transform = transformTable ? readBlockPointer(transformTable, 0x00) : nullptr;
                runMdctBackward(transform, pcm, pcm);
            }

            for (int channel = 0; channel < channels; ++channel)
            {
                float* pcm = blockChannelSamples(block, channel);
                if (nonzero[channel])
                {
                    applyVorbisWindow(
                        pcm,
                        privateState,
                        setup,
                        readBlockField<int>(block, kBlockPreviousWindowFlagOffset),
                        W,
                        readBlockField<int>(block, kBlockNextWindowFlagOffset));
                }
                else if (pcm && n > 0)
                {
                    std::memset(pcm, 0, 4 * static_cast<std::size_t>(n));
                }
            }
            return 0;
        }

} } }
