#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        static void vorbisBlockRipcord(unsigned char* block)
        {
            void* reap = readBlockPointer(block, 0x54);
            while (reap)
            {
                void* next = readBlockPointer(static_cast<unsigned char*>(reap), 0x04);
                std::free(readBlockPointer(static_cast<unsigned char*>(reap), 0x00));
                writeBlockPointer(static_cast<unsigned char*>(reap), 0x00, nullptr);
                writeBlockPointer(static_cast<unsigned char*>(reap), 0x04, nullptr);
                std::free(reap);
                reap = next;
            }

            const int totalUse = readBlockField<int>(block, 0x50);
            if (totalUse)
            {
                void* localStore = std::realloc(
                    readBlockPointer(block, 0x44),
                    static_cast<std::size_t>(totalUse + readBlockField<int>(block, 0x4C)));
                writeBlockPointer(block, 0x44, localStore);
                writeBlockField<int>(block, 0x4C, totalUse + readBlockField<int>(block, 0x4C));
                writeBlockField<int>(block, 0x50, 0);
            }
            writeBlockField<int>(block, 0x48, 0);
            writeBlockPointer(block, 0x54, nullptr);
        }

        void* allocateBlockScratch(unsigned char* block, int bytes)
        {
            const int aligned = (bytes + 7) & ~7;
            int localTop = readBlockField<int>(block, 0x48);
            if (aligned + localTop > readBlockField<int>(block, 0x4C))
            {
                if (void* localStore = readBlockPointer(block, 0x44))
                {
                    auto* reap = static_cast<unsigned char*>(std::malloc(8));
                    writeBlockPointer(reap, 0x00, localStore);
                    writeBlockPointer(reap, 0x04, readBlockPointer(block, 0x54));
                    writeBlockPointer(block, 0x54, reap);
                    writeBlockField<int>(block, 0x50, readBlockField<int>(block, 0x50) + readBlockField<int>(block, 0x48));
                }
                writeBlockField<int>(block, 0x4C, aligned);
                writeBlockPointer(block, 0x44, std::malloc(static_cast<std::size_t>(aligned)));
                writeBlockField<int>(block, 0x48, 0);
                localTop = 0;
            }

            auto* result = static_cast<unsigned char*>(readBlockPointer(block, 0x44)) + localTop;
            writeBlockField<int>(block, 0x48, localTop + aligned);
            return result;
        }

        int vorbis_synthesis(VorbisStateBlob& state, const PacketToken& packet)
        {
            unsigned char* block = blockDecodeState(state);
            auto* synthesisState = static_cast<unsigned char*>(readBlockPointer(block, kBlockQueueOwnerOffset));
            auto* info = static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset));
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));

            vorbisBlockRipcord(block);

            // Retail vorbis_block embeds oggpack_buffer directly at +0x04.  On the target
            // Win32/x86 ABI BitCursor is the exact 0x14-byte physical layout.
            auto& cursor = *reinterpret_cast<BitCursor*>(block + 0x04);
            initializeBitCursor(cursor, readBlockPointer(packet.bytes, 0x00), readBlockField<int>(packet.bytes, 0x04));
            if (readBits(cursor, 1))
                return -135;

            const int modeIndex = readBits(cursor, modeIndexBitCount(setup));
            if (modeIndex == -1)
                return -136;
            writeBlockField<int>(block, kBlockModeIndexOffset, modeIndex);

            auto* mode = static_cast<unsigned char*>(readBlockPointer(setup, kSetupModePointerTableOffset + 4 * static_cast<std::size_t>(modeIndex)));
            const int longMode = readBlockField<int>(mode, 0x00);
            writeBlockField<int>(block, kBlockLongModeOffset, longMode);
            if (!longMode)
            {
                writeBlockField<int>(block, kBlockPreviousWindowFlagOffset, 0);
                writeBlockField<int>(block, kBlockNextWindowFlagOffset, 0);
            }
            else
            {
                writeBlockField<int>(block, kBlockPreviousWindowFlagOffset, readBits(cursor, 1));
                const int nextWindow = readBits(cursor, 1);
                writeBlockField<int>(block, kBlockNextWindowFlagOffset, nextWindow);
                if (nextWindow == -1)
                    return -136;
            }

            writeBlockField<int>(block, kBlockGranuleLowOffset, readBlockField<int>(packet.bytes, 0x10));
            writeBlockField<int>(block, kBlockGranuleHighOffset, readBlockField<int>(packet.bytes, 0x14));
            const std::int64_t packetNumber = readBlockSigned64(packet.bytes, 0x18) - 3;
            writeBlockSigned64(block, kBlockPacketNumberLowOffset, packetNumber);
            writeBlockField<int>(block, kBlockEndFlagOffset, readBlockField<int>(packet.bytes, 0x0C));
            writeBlockField<int>(block, kBlockDecodedSampleCountOffset, readBlockField<int>(setup, 4 * static_cast<std::size_t>(longMode)));

            const int channels = readBlockField<int>(info, kInfoChannelsOffset);
            auto* pcm = static_cast<unsigned char*>(allocateBlockScratch(block, 4 * channels));
            writeBlockPointer(block, kBlockDecodedChannelTableOffset, pcm);
            for (int channel = 0; channel < channels; ++channel)
            {
                writeBlockPointer(
                    pcm,
                    4 * static_cast<std::size_t>(channel),
                    allocateBlockScratch(block, 4 * readBlockField<int>(block, kBlockDecodedSampleCountOffset)));
            }

            const int mappingIndex = readBlockField<int>(mode, 0x0C);
            const auto* mapping = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupMappingPointerTableOffset + 4 * static_cast<std::size_t>(mappingIndex)));
            return inverseMapping0(block, mapping);
        }

} } }
