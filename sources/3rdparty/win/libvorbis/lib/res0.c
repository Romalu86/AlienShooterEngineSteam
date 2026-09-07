#include "3rdparty/win/libvorbis/lib/codec_internal.h"
#if defined(_MSC_VER)
#include <malloc.h>
#define AS1_XIPH_RES_ALLOCA _alloca
#else
#include <alloca.h>
#define AS1_XIPH_RES_ALLOCA alloca
#endif

namespace as1 { namespace thirdparty { namespace xiph2003
{
        void releaseResidueRecord(void* record)
        {
            if (!record)
                return;
            std::memset(record, 0, 0x714);
            std::free(record);
        }


        void releaseResidueLookRecord(void* record)
        {
            if (record)
            {
                auto* look = static_cast<unsigned char*>(record);
                const int partitionCount = readBlockField<int>(look, 0x04);
                auto* partitionBooks = static_cast<unsigned char*>(readBlockPointer(look, 0x14));
                for (int index = 0; index < partitionCount; ++index)
                {
                    if (void* entry = readBlockPointer(partitionBooks, 4 * static_cast<std::size_t>(index)))
                        std::free(entry);
                }
                std::free(partitionBooks);

                const int decodeMapCount = readBlockField<int>(look, 0x18);
                auto* decodeMap = static_cast<unsigned char*>(readBlockPointer(look, 0x1C));
                for (int index = 0; index < decodeMapCount; ++index)
                    std::free(readBlockPointer(decodeMap, 4 * static_cast<std::size_t>(index)));
                std::free(decodeMap);

                std::memset(record, 0, 0x2C);
                std::free(record);
            }
        }

        void* parseResidueSetupRecord(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return nullptr;
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            if (!setup)
                return nullptr;

            auto* residue = static_cast<unsigned char*>(std::calloc(1, 0x714));
            if (!residue)
                return nullptr;

            writeBlockField<int>(residue, 0x00, readBits(cursor, 24));
            writeBlockField<int>(residue, 0x04, readBits(cursor, 24));
            writeBlockField<int>(residue, 0x08, readBits(cursor, 24) + 1);
            writeBlockField<int>(residue, 0x0C, readBits(cursor, 6) + 1);
            writeBlockField<int>(residue, 0x10, readBits(cursor, 8));

            int totalCascadeBooks = 0;
            for (int classIndex = 0; classIndex < readBlockField<int>(residue, 0x0C); ++classIndex)
            {
                int cascade = readBits(cursor, 3);
                if (readBits(cursor, 1))
                    cascade |= 8 * readBits(cursor, 5);
                writeBlockField<int>(residue, 0x14 + 4 * static_cast<std::size_t>(classIndex), cascade);
                totalCascadeBooks += populationCountUnsigned(static_cast<unsigned int>(cascade));
            }

            for (int index = 0; index < totalCascadeBooks; ++index)
                writeBlockField<int>(residue, 0x114 + 4 * static_cast<std::size_t>(index), readBits(cursor, 8));

            const int bookCount = readBlockField<int>(setup, kSetupBookCountOffset);
            if (readBlockField<int>(residue, 0x10) >= bookCount)
            {
                releaseResidueRecord(residue);
                return nullptr;
            }
            for (int index = 0; index < totalCascadeBooks; ++index)
            {
                if (readBlockField<int>(residue, 0x114 + 4 * static_cast<std::size_t>(index)) >= bookCount)
                {
                    releaseResidueRecord(residue);
                    return nullptr;
                }
            }
            return residue;
        }

        void* buildResidueLookRecord(const unsigned char* setup, int residueIndex)
        {
            if (!setup || residueIndex < 0 || residueIndex >= readBlockField<int>(setup, kSetupResidueCountOffset))
                return nullptr;
            const auto* info = static_cast<const unsigned char*>(readBlockPointer(
                setup,
                kSetupResiduePointerTableOffset + 4 * static_cast<std::size_t>(residueIndex)));
            if (!info)
                return nullptr;

            auto* look = static_cast<unsigned char*>(std::calloc(1, 0x2C));
            if (!look)
                return nullptr;

            const int partitions = readBlockField<int>(info, 0x0C);
            const int groupbookIndex = readBlockField<int>(info, 0x10);
            const unsigned char* fullbooks = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupFullbookArrayOffset));
            if (partitions <= 0 || !fullbooks || groupbookIndex < 0 || groupbookIndex >= readBlockField<int>(setup, kSetupBookCountOffset))
            {
                std::free(look);
                return nullptr;
            }

            const unsigned char* phrasebook = fullbooks + static_cast<std::size_t>(groupbookIndex) * kRuntimeBookSize;
            const int phraseDimensions = readBlockField<int>(phrasebook, kRuntimeBookDimensionsOffset);
            if (phraseDimensions <= 0)
            {
                std::free(look);
                return nullptr;
            }

            writeBlockPointer(look, 0x00, const_cast<unsigned char*>(info));
            writeBlockField<int>(look, 0x04, partitions);
            writeBlockPointer(look, 0x0C, const_cast<unsigned char*>(fullbooks));
            writeBlockPointer(look, 0x10, const_cast<unsigned char*>(phrasebook));

            auto* partitionBooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(partitions), 4));
            writeBlockPointer(look, 0x14, partitionBooks);
            if (!partitionBooks)
            {
                releaseResidueLookRecord(look);
                return nullptr;
            }

            int maximumStages = 0;
            int bookCursor = 0;
            for (int partition = 0; partition < partitions; ++partition)
            {
                const unsigned int cascade = static_cast<unsigned int>(readBlockField<int>(info, 0x14 + 4 * static_cast<std::size_t>(partition)));
                const int stages = countBitsForUnsignedValue(cascade);
                if (stages > maximumStages)
                    maximumStages = stages;
                if (!stages)
                    continue;

                auto* stageBooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(stages), 4));
                writeBlockPointer(partitionBooks, 4 * static_cast<std::size_t>(partition), stageBooks);
                if (!stageBooks)
                {
                    releaseResidueLookRecord(look);
                    return nullptr;
                }
                for (int stage = 0; stage < stages; ++stage)
                {
                    if (!(cascade & (1u << stage)))
                        continue;
                    const int bookIndex = readBlockField<int>(info, 0x114 + 4 * static_cast<std::size_t>(bookCursor++));
                    if (bookIndex < 0 || bookIndex >= readBlockField<int>(setup, kSetupBookCountOffset))
                    {
                        releaseResidueLookRecord(look);
                        return nullptr;
                    }
                    writeBlockPointer(
                        stageBooks,
                        4 * static_cast<std::size_t>(stage),
                        const_cast<unsigned char*>(fullbooks + static_cast<std::size_t>(bookIndex) * kRuntimeBookSize));
                }
            }
            writeBlockField<int>(look, 0x08, maximumStages);

            const int partvals = static_cast<int>(std::floor(std::pow(static_cast<double>(partitions), static_cast<double>(phraseDimensions)) + 0.5));
            if (partvals <= 0)
            {
                releaseResidueLookRecord(look);
                return nullptr;
            }
            writeBlockField<int>(look, 0x18, partvals);
            auto* decodeMap = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(partvals), 4));
            writeBlockPointer(look, 0x1C, decodeMap);
            if (!decodeMap)
            {
                releaseResidueLookRecord(look);
                return nullptr;
            }

            for (int value = 0; value < partvals; ++value)
            {
                auto* entry = static_cast<int*>(std::malloc(static_cast<std::size_t>(phraseDimensions) * sizeof(int)));
                writeBlockPointer(decodeMap, 4 * static_cast<std::size_t>(value), entry);
                if (!entry)
                {
                    releaseResidueLookRecord(look);
                    return nullptr;
                }
                int remaining = value;
                int divisor = partvals / partitions;
                for (int dimension = 0; dimension < phraseDimensions; ++dimension)
                {
                    entry[dimension] = divisor ? remaining / divisor : 0;
                    if (divisor)
                        remaining -= entry[dimension] * divisor;
                    divisor = divisor ? divisor / partitions : 0;
                }
            }
            return look;
        }

        using Residue01DecodeCallback = int (*)(const unsigned char*, BitCursor&, float*, int);

        // Retail core owner inverseResidue01Core. The per-channel classification pointer rows are
        // block-owned through allocateBlockScratch; only the row-pointer vector itself lives on stack.
        int inverseResidue01Core(unsigned char* block, const unsigned char* look, float** channels,
                      int channelCount, Residue01DecodeCallback decodePartition)
        {
            if (!block || !look || !channels || channelCount <= 0 || !decodePartition)
                return 0;
            const auto* info = static_cast<const unsigned char*>(readBlockPointer(look, 0x00));
            const auto* phrasebook = static_cast<const unsigned char*>(readBlockPointer(look, 0x10));
            auto* partitionBooks = static_cast<unsigned char*>(readBlockPointer(look, 0x14));
            auto* decodeMap = static_cast<unsigned char*>(readBlockPointer(look, 0x1C));
            if (!info || !phrasebook || !partitionBooks || !decodeMap)
                return 0;

            const int phraseDimensions = readBlockField<int>(phrasebook, kRuntimeBookDimensionsOffset);
            const int grouping = readBlockField<int>(info, 0x08);
            const int partitionCount = grouping > 0
                ? (readBlockField<int>(info, 0x04) - readBlockField<int>(info, 0x00)) / grouping : 0;
            if (phraseDimensions <= 0 || grouping <= 0 || partitionCount <= 0)
                return 0;
            const int words = (partitionCount + phraseDimensions - 1) / phraseDimensions;

            auto** classifications = static_cast<unsigned char**>(
                AS1_XIPH_RES_ALLOCA(sizeof(unsigned char*) * static_cast<std::size_t>(channelCount)));
            for (int channel = 0; channel < channelCount; ++channel)
            {
                classifications[channel] = static_cast<unsigned char*>(allocateBlockScratch(block, 4 * words));
                if (!classifications[channel])
                    return 0;
            }

            auto& cursor = *reinterpret_cast<BitCursor*>(block + 0x04);
            const int stages = readBlockField<int>(look, 0x08);
            const int begin = readBlockField<int>(info, 0x00);
            const int partvals = readBlockField<int>(look, 0x18);
            for (int stage = 0; stage < stages; ++stage)
            {
                int partition = 0;
                int word = 0;
                while (partition < partitionCount)
                {
                    if (stage == 0)
                    {
                        for (int channel = 0; channel < channelCount; ++channel)
                        {
                            const int decoded = decodeCodebookEntryIndex(phrasebook, cursor);
                            if (decoded == -1 || decoded < 0 || decoded >= partvals)
                                return 0;
                            void* map = readBlockPointer(decodeMap, 4 * static_cast<std::size_t>(decoded));
                            writeBlockPointer(classifications[channel], 4 * static_cast<std::size_t>(word), map);
                            if (!map)
                                return 0;
                        }
                    }

                    for (int dimension = 0; dimension < phraseDimensions && partition < partitionCount;
                         ++dimension, ++partition)
                    {
                        const int offset = begin + partition * grouping;
                        for (int channel = 0; channel < channelCount; ++channel)
                        {
                            const auto* map = static_cast<const int*>(readBlockPointer(
                                classifications[channel], 4 * static_cast<std::size_t>(word)));
                            if (!map)
                                return 0;
                            const int classification = map[dimension];
                            const unsigned int cascade = static_cast<unsigned int>(
                                readBlockField<int>(info, 0x14 + 4 * static_cast<std::size_t>(classification)));
                            if (!(cascade & (1u << stage)))
                                continue;
                            auto* stageBooks = static_cast<unsigned char*>(readBlockPointer(
                                partitionBooks, 4 * static_cast<std::size_t>(classification)));
                            const auto* book = stageBooks ? static_cast<const unsigned char*>(readBlockPointer(
                                stageBooks, 4 * static_cast<std::size_t>(stage))) : nullptr;
                            if (book && decodePartition(book, cursor, channels[channel] + offset, grouping) == -1)
                                return 0;
                        }
                    }
                    ++word;
                }
            }
            return 0;
        }

        static int compactResidueChannelsAndDecode(unsigned char* block, const unsigned char* look,
                                                    float** channels, const int* nonzero, int channelCount,
                                                    Residue01DecodeCallback decodePartition)
        {
            if (channelCount <= 0)
                return 0;
            int active = 0;
            for (int channel = 0; channel < channelCount; ++channel)
            {
                if (nonzero[channel])
                    channels[active++] = channels[channel];
            }
            return active ? inverseResidue01Core(block, look, channels, active, decodePartition) : 0;
        }

        int inverseResidue0(unsigned char* block, const unsigned char* look, float** channels,
                      const int* nonzero, int channelCount)
        {
            return compactResidueChannelsAndDecode(block, look, channels, nonzero, channelCount,
                                                   decodeCodebookVectorsStridedAdd);
        }

        int inverseResidue1(unsigned char* block, const unsigned char* look, float** channels,
                      const int* nonzero, int channelCount)
        {
            return compactResidueChannelsAndDecode(block, look, channels, nonzero, channelCount,
                                                   decodeCodebookVectorsAdd);
        }

        int inverseResidue2(unsigned char* block, const unsigned char* look, float** channels,
                      const int* nonzero, int channelCount)
        {
            if (!block || !look || !channels || !nonzero || channelCount <= 0)
                return 0;
            int firstNonzero = 0;
            while (firstNonzero < channelCount && !nonzero[firstNonzero])
                ++firstNonzero;
            if (firstNonzero == channelCount)
                return 0;

            const auto* info = static_cast<const unsigned char*>(readBlockPointer(look, 0x00));
            const auto* phrasebook = static_cast<const unsigned char*>(readBlockPointer(look, 0x10));
            auto* partitionBooks = static_cast<unsigned char*>(readBlockPointer(look, 0x14));
            auto* decodeMap = static_cast<unsigned char*>(readBlockPointer(look, 0x1C));
            if (!info || !phrasebook || !partitionBooks || !decodeMap)
                return 0;

            const int grouping = readBlockField<int>(info, 0x08);
            const int phraseDimensions = readBlockField<int>(phrasebook, kRuntimeBookDimensionsOffset);
            const int partitionCount = grouping > 0
                ? (readBlockField<int>(info, 0x04) - readBlockField<int>(info, 0x00)) / grouping : 0;
            if (grouping <= 0 || phraseDimensions <= 0 || partitionCount <= 0)
                return 0;
            const int words = (partitionCount + phraseDimensions - 1) / phraseDimensions;
            auto* classifications = static_cast<unsigned char*>(allocateBlockScratch(block, 4 * words));
            if (!classifications)
                return 0;

            auto& cursor = *reinterpret_cast<BitCursor*>(block + 0x04);
            const int stages = readBlockField<int>(look, 0x08);
            const int begin = readBlockField<int>(info, 0x00);
            const int partvals = readBlockField<int>(look, 0x18);
            for (int stage = 0; stage < stages; ++stage)
            {
                int partition = 0;
                int word = 0;
                while (partition < partitionCount)
                {
                    if (stage == 0)
                    {
                        const int decoded = decodeCodebookEntryIndex(phrasebook, cursor);
                        if (decoded == -1 || decoded < 0 || decoded >= partvals)
                            return 0;
                        void* map = readBlockPointer(decodeMap, 4 * static_cast<std::size_t>(decoded));
                        writeBlockPointer(classifications, 4 * static_cast<std::size_t>(word), map);
                        if (!map)
                            return 0;
                    }
                    const auto* map = static_cast<const int*>(readBlockPointer(
                        classifications, 4 * static_cast<std::size_t>(word)));
                    if (!map)
                        return 0;
                    for (int dimension = 0; dimension < phraseDimensions && partition < partitionCount;
                         ++dimension, ++partition)
                    {
                        const int classification = map[dimension];
                        const unsigned int cascade = static_cast<unsigned int>(
                            readBlockField<int>(info, 0x14 + 4 * static_cast<std::size_t>(classification)));
                        if (!(cascade & (1u << stage)))
                            continue;
                        auto* stageBooks = static_cast<unsigned char*>(readBlockPointer(
                            partitionBooks, 4 * static_cast<std::size_t>(classification)));
                        const auto* book = stageBooks ? static_cast<const unsigned char*>(readBlockPointer(
                            stageBooks, 4 * static_cast<std::size_t>(stage))) : nullptr;
                        if (book && decodeCodebookVectorsAcrossChannelsAdd(book, cursor, channels,
                                begin + partition * grouping, channelCount, grouping) == -1)
                            return 0;
                    }
                    ++word;
                }
            }
            return 0;
        }

} } }
