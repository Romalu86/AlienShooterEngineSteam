#include "3rdparty/win/libvorbis/lib/codec_internal.h"
#if defined(_MSC_VER)
#include <malloc.h>
#define AS1_XIPH_BOOK_ALLOCA _alloca
#else
#include <alloca.h>
#define AS1_XIPH_BOOK_ALLOCA alloca
#endif

namespace as1 { namespace thirdparty { namespace xiph2003
{
        int lookup1ValueCount(const unsigned char* codebook)
        {
            if (!codebook)
                return 0;
            const int dimensions = readBlockField<int>(codebook, kCodebookDimensionsOffset);
            const int entries = readBlockField<int>(codebook, kCodebookEntriesOffset);
            if (dimensions <= 0 || entries <= 0)
                return 0;

            int candidate = static_cast<int>(std::floor(std::pow(static_cast<double>(entries), 1.0 / static_cast<double>(dimensions))));
            while (true)
            {
                int lower = 1;
                int upper = 1;
                for (int index = dimensions; index > 0; --index)
                {
                    lower *= candidate;
                    upper *= candidate + 1;
                }

                if (lower > entries)
                {
                    --candidate;
                    continue;
                }
                if (upper > entries)
                    break;
                if (lower > entries)
                {
                    --candidate;
                    continue;
                }
                ++candidate;
            }
            return candidate;
        }

        float unpackCodebookFloat32(int packed)
        {
            float value = static_cast<float>(packed & 0x1FFFFF);
            if (packed & 0x80000000)
                value = -value;
            return static_cast<float>(std::ldexp(static_cast<double>(value), ((packed >> 21) & 0x3FF) - 788));
        }

        std::uint32_t reverseBits32(std::uint32_t value)
        {
            value = (value << 16) | (value >> 16);
            value = ((value >> 8) & 0x00FF00FFu) | ((value & 0x00FF00FFu) << 8);
            value = ((value >> 4) & 0x0F0F0F0Fu) | ((value & 0x0F0F0F0Fu) << 4);
            value = ((value >> 2) & 0x33333333u) | ((value & 0x33333333u) << 2);
            value = ((value >> 1) & 0x55555555u) | ((value & 0x55555555u) << 1);
            return value;
        }

        int activeCodebookEntryCount(const unsigned char* codebook)
        {
            if (!codebook)
                return 0;
            const int entries = readBlockField<int>(codebook, kCodebookEntriesOffset);
            const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kCodebookLengthListOffset));
            if (!lengths || entries <= 0)
                return 0;
            int count = 0;
            for (int index = 0; index < entries; ++index)
                if (readBlockField<int>(lengths, 4 * static_cast<std::size_t>(index)) > 0)
                    ++count;
            return count;
        }

        void* buildCanonicalCodewordList(const unsigned char* codebook, int sparseCount)
        {
            const int entries = readBlockField<int>(codebook, kCodebookEntriesOffset);
            const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kCodebookLengthListOffset));
            auto* words = static_cast<std::uint32_t*>(std::malloc(4u * static_cast<std::size_t>(sparseCount ? sparseCount : entries)));
            std::uint32_t marker[33]{};
            int count = 0;

            for (int entryIndex = 0; entryIndex < entries; ++entryIndex)
            {
                const int length = readBlockField<int>(lengths, 4 * static_cast<std::size_t>(entryIndex));
                if (length > 0)
                {
                    std::uint32_t entry = marker[length];
                    if (length < 32 && (entry >> length))
                    {
                        std::free(words);
                        return nullptr;
                    }
                    words[count++] = entry;

                    int depth = length;
                    while (depth > 0)
                    {
                        if (marker[depth] & 1u)
                        {
                            if (depth == 1)
                                ++marker[1];
                            else
                                marker[depth] = marker[depth - 1] << 1;
                            break;
                        }
                        ++marker[depth];
                        --depth;
                    }

                    for (depth = length + 1; depth < 33; ++depth)
                    {
                        if ((marker[depth] >> 1) != entry)
                            break;
                        entry = marker[depth];
                        marker[depth] = marker[depth - 1] << 1;
                    }
                }
                else if (!sparseCount)
                {
                    ++count;
                }
            }

            count = 0;
            for (int entryIndex = 0; entryIndex < entries; ++entryIndex)
            {
                const int length = readBlockField<int>(lengths, 4 * static_cast<std::size_t>(entryIndex));
                std::uint32_t reversed = 0;
                for (int bit = 0; bit < length; ++bit)
                    reversed = (reversed << 1) | ((words[count] >> bit) & 1u);
                if (!sparseCount || length)
                    words[count++] = reversed;
            }
            return words;
        }

        void* bookUnquantizeBoundary(const unsigned char* codebook, int vectorCount, const int* sortIndex)
        {
            const int lookupType = readBlockField<int>(codebook, kCodebookLookupTypeOffset);
            if (lookupType != 1 && lookupType != 2)
                return nullptr;

            const int dimensions = readBlockField<int>(codebook, kCodebookDimensionsOffset);
            const int entries = readBlockField<int>(codebook, kCodebookEntriesOffset);
            const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kCodebookLengthListOffset));
            const auto* quantValues = static_cast<const unsigned char*>(readBlockPointer(codebook, kCodebookQuantValueListOffset));
            const float minimumValue = unpackCodebookFloat32(readBlockField<int>(codebook, kCodebookMinimumValueOffset));
            const float deltaValue = unpackCodebookFloat32(readBlockField<int>(codebook, kCodebookDeltaValueOffset));
            const int sequenceFlag = readBlockField<int>(codebook, kCodebookSequenceFlagOffset);
            auto* vectors = static_cast<float*>(std::calloc(static_cast<std::size_t>(vectorCount) * static_cast<std::size_t>(dimensions), 4));

            int outputOrdinal = 0;
            const int* sortCursor = sortIndex;
            if (lookupType == 1)
            {
                const int quantValueCount = lookup1ValueCount(codebook);
                for (int entryIndex = 0; entryIndex < entries; ++entryIndex)
                {
                    if (!sortIndex || readBlockField<int>(lengths, 4 * static_cast<std::size_t>(entryIndex)))
                    {
                        int divisor = 1;
                        float last = 0.0f;
                        const int destination = sortIndex ? *sortCursor : outputOrdinal;
                        for (int dimension = 0; dimension < dimensions; ++dimension)
                        {
                            const int quantIndex = (entryIndex / divisor) % quantValueCount;
                            const int quantized = readBlockField<int>(quantValues, 4 * static_cast<std::size_t>(quantIndex));
                            const float value = static_cast<float>(std::fabs(static_cast<double>(quantized))) * deltaValue + last + minimumValue;
                            if (sequenceFlag)
                                last = value;
                            vectors[static_cast<std::size_t>(dimension) + static_cast<std::size_t>(destination) * static_cast<std::size_t>(dimensions)] = value;
                            divisor *= quantValueCount;
                        }
                        ++outputOrdinal;
                        if (sortIndex)
                            ++sortCursor;
                    }
                }
                return vectors;
            }

            for (int entryIndex = 0; entryIndex < entries; ++entryIndex)
            {
                if (!sortIndex || readBlockField<int>(lengths, 4 * static_cast<std::size_t>(entryIndex)))
                {
                    float last = 0.0f;
                    const int destination = sortIndex ? *sortCursor : outputOrdinal;
                    for (int dimension = 0; dimension < dimensions; ++dimension)
                    {
                        const int quantized = readBlockField<int>(quantValues, 4 * static_cast<std::size_t>(dimension + entryIndex * dimensions));
                        const float value = static_cast<float>(std::fabs(static_cast<double>(quantized))) * deltaValue + last + minimumValue;
                        if (sequenceFlag)
                            last = value;
                        vectors[static_cast<std::size_t>(dimension) + static_cast<std::size_t>(destination) * static_cast<std::size_t>(dimensions)] = value;
                    }
                    ++outputOrdinal;
                    if (sortIndex)
                        ++sortCursor;
                }
            }
            return vectors;
        }

        int initializeRuntimeCodebookRecord(unsigned char* runtimeBook, const unsigned char* staticBook)
        {
            std::memset(runtimeBook, 0, 0x2C);
            writeBlockPointer(runtimeBook, 0x0C, const_cast<unsigned char*>(staticBook));
            const int entries = readBlockField<int>(staticBook, kCodebookEntriesOffset);
            writeBlockField<int>(runtimeBook, 0x04, entries);
            writeBlockField<int>(runtimeBook, 0x08, entries);
            writeBlockField<int>(runtimeBook, 0x00, readBlockField<int>(staticBook, kCodebookDimensionsOffset));
            writeBlockPointer(runtimeBook, 0x14, buildCanonicalCodewordList(staticBook, 0));
            writeBlockPointer(runtimeBook, 0x10, bookUnquantizeBoundary(staticBook, entries, nullptr));
            return 0;
        }

        void* buildCodebookValueVectorList(const unsigned char* codebook, int vectorCount, const int* indexMap)
        {
            if (!codebook || vectorCount < 0)
                return nullptr;
            const int dimensions = readBlockField<int>(codebook, kCodebookDimensionsOffset);
            const int entries = readBlockField<int>(codebook, kCodebookEntriesOffset);
            const int lookupType = readBlockField<int>(codebook, kCodebookLookupTypeOffset);
            if (dimensions <= 0)
                return nullptr;

            auto* vectors = static_cast<float*>(std::calloc(static_cast<std::size_t>(vectorCount > 0 ? vectorCount : 1) * static_cast<std::size_t>(dimensions), sizeof(float)));
            if (!vectors)
                return nullptr;
            if (lookupType != 1 && lookupType != 2)
                return vectors;

            const auto* quantValues = static_cast<const unsigned char*>(readBlockPointer(codebook, kCodebookQuantValueListOffset));
            if (!quantValues)
                return vectors;

            const float minimumValue = unpackCodebookFloat32(readBlockField<int>(codebook, kCodebookMinimumValueOffset));
            const float deltaValue = unpackCodebookFloat32(readBlockField<int>(codebook, kCodebookDeltaValueOffset));
            const int sequenceFlag = readBlockField<int>(codebook, kCodebookSequenceFlagOffset);
            const int lookup1Values = lookupType == 1 ? lookup1ValueCount(codebook) : 0;

            for (int vectorIndex = 0; vectorIndex < vectorCount; ++vectorIndex)
            {
                const int entryIndex = indexMap ? indexMap[vectorIndex] : vectorIndex;
                if (entryIndex < 0 || entryIndex >= entries)
                    continue;

                float last = 0.0f;
                int divisor = 1;
                for (int dimension = 0; dimension < dimensions; ++dimension)
                {
                    int quantIndex = 0;
                    if (lookupType == 1)
                    {
                        if (lookup1Values <= 0)
                            break;
                        quantIndex = (entryIndex / divisor) % lookup1Values;
                        divisor *= lookup1Values;
                    }
                    else
                    {
                        quantIndex = entryIndex * dimensions + dimension;
                    }

                    const int raw = readBlockField<int>(quantValues, 4 * static_cast<std::size_t>(quantIndex));
                    const float value = static_cast<float>(std::abs(raw)) * deltaValue + minimumValue + last;
                    vectors[static_cast<std::size_t>(vectorIndex) * static_cast<std::size_t>(dimensions) + static_cast<std::size_t>(dimension)] = value;
                    if (sequenceFlag)
                        last = value;
                }
            }
            return vectors;
        }

        int clearRuntimeCodebook(unsigned char* runtimeBook)
        {
            if (void* valueList = readBlockPointer(runtimeBook, kRuntimeBookValueListOffset))
                std::free(valueList);
            if (void* codeList = readBlockPointer(runtimeBook, kRuntimeBookCodeListOffset))
                std::free(codeList);
            if (void* decodeIndex = readBlockPointer(runtimeBook, kRuntimeBookDecodeIndexOffset))
                std::free(decodeIndex);
            if (void* decodeLengths = readBlockPointer(runtimeBook, kRuntimeBookDecodeLengthsOffset))
                std::free(decodeLengths);
            if (void* firstTable = readBlockPointer(runtimeBook, kRuntimeBookFirstTableOffset))
                std::free(firstTable);
            std::memset(runtimeBook, 0, kRuntimeBookSize);
            return 0;
        }

        int vorbis_book_init_decode(unsigned char* runtimeBook, const unsigned char* staticBook)
        {
            std::memset(runtimeBook, 0, kRuntimeBookSize);

            const int entries = readBlockField<int>(staticBook, kCodebookEntriesOffset);
            const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(staticBook, kCodebookLengthListOffset));
            int usedEntries = 0;
            for (int index = 0; index < entries; ++index)
            {
                if (readBlockField<int>(lengths, 4 * static_cast<std::size_t>(index)) > 0)
                    ++usedEntries;
            }

            writeBlockField<int>(runtimeBook, kRuntimeBookEntriesOffset, entries);
            writeBlockField<int>(runtimeBook, kRuntimeBookUsedEntriesOffset, usedEntries);
            writeBlockField<int>(runtimeBook, kRuntimeBookDimensionsOffset, readBlockField<int>(staticBook, kCodebookDimensionsOffset));

            auto* words = static_cast<std::uint32_t*>(buildCanonicalCodewordList(staticBook, usedEntries));
            if (!words)
            {
                clearRuntimeCodebook(runtimeBook);
                return -1;
            }

            auto* sortedWords = static_cast<std::uint32_t**>(
                std::malloc(sizeof(std::uint32_t*) * static_cast<std::size_t>(usedEntries)));
            for (int index = 0; index < usedEntries; ++index)
            {
                words[index] = reverseBits32(words[index]);
                sortedWords[index] = words + index;
            }
            std::qsort(sortedWords, static_cast<std::size_t>(usedEntries), sizeof(std::uint32_t*),
                [](const void* left, const void* right) -> int
                {
                    const auto* lhs = *static_cast<std::uint32_t* const*>(left);
                    const auto* rhs = *static_cast<std::uint32_t* const*>(right);
                    return (*rhs < *lhs) ? 1 : -1;
                });

            auto* sortIndex = static_cast<int*>(std::malloc(4u * static_cast<std::size_t>(usedEntries)));
            auto* codeList = static_cast<std::uint32_t*>(std::malloc(4u * static_cast<std::size_t>(usedEntries)));
            writeBlockPointer(runtimeBook, kRuntimeBookCodeListOffset, codeList);

            for (int sorted = 0; sorted < usedEntries; ++sorted)
            {
                const int original = static_cast<int>(sortedWords[sorted] - words);
                sortIndex[original] = sorted;
                codeList[sorted] = *sortedWords[sorted];
            }
            std::free(sortedWords);
            std::free(words);

            writeBlockPointer(runtimeBook, kRuntimeBookValueListOffset, bookUnquantizeBoundary(staticBook, usedEntries, sortIndex));

            auto* decodeIndex = static_cast<int*>(std::malloc(4u * static_cast<std::size_t>(usedEntries)));
            writeBlockPointer(runtimeBook, kRuntimeBookDecodeIndexOffset, decodeIndex);
            int activeOrdinal = 0;
            for (int entry = 0; entry < entries; ++entry)
            {
                if (readBlockField<int>(lengths, 4 * static_cast<std::size_t>(entry)) > 0)
                {
                    decodeIndex[sortIndex[activeOrdinal]] = entry;
                    ++activeOrdinal;
                }
            }

            auto* decodeLengths = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(usedEntries)));
            writeBlockPointer(runtimeBook, kRuntimeBookDecodeLengthsOffset, decodeLengths);
            activeOrdinal = 0;
            for (int entry = 0; entry < entries; ++entry)
            {
                const int length = readBlockField<int>(lengths, 4 * static_cast<std::size_t>(entry));
                if (length > 0)
                {
                    decodeLengths[sortIndex[activeOrdinal]] = static_cast<unsigned char>(length);
                    ++activeOrdinal;
                }
            }
            std::free(sortIndex);

            int firstTableBits = countBitsForUnsignedValue(static_cast<unsigned int>(usedEntries)) - 4;
            if (firstTableBits < 5)
                firstTableBits = 5;
            if (firstTableBits > 8)
                firstTableBits = 8;
            writeBlockField<int>(runtimeBook, kRuntimeBookFirstTableBitsOffset, firstTableBits);

            const int firstTableSize = 1 << firstTableBits;
            auto* firstTable = static_cast<std::uint32_t*>(std::calloc(static_cast<std::size_t>(firstTableSize), 4));
            writeBlockPointer(runtimeBook, kRuntimeBookFirstTableOffset, firstTable);

            int maxLength = 0;
            for (int entry = 0; entry < usedEntries; ++entry)
            {
                const int length = decodeLengths[entry];
                if (maxLength < length)
                    maxLength = length;
                if (length <= firstTableBits)
                {
                    const std::uint32_t prefix = reverseBits32(codeList[entry]);
                    const int fillCount = 1 << (firstTableBits - length);
                    for (int fill = 0; fill < fillCount; ++fill)
                        firstTable[prefix | (static_cast<std::uint32_t>(fill) << length)] = static_cast<std::uint32_t>(entry + 1);
                }
            }
            writeBlockField<int>(runtimeBook, kRuntimeBookMaxLengthOffset, maxLength);

            int lowerCursor = 0;
            int lowerSaved = 0;
            int upperCursor = 0;
            const std::uint32_t prefixMask = 0xFFFFFFFEu << (31 - firstTableBits);
            for (int slot = 0; slot < firstTableSize; ++slot)
            {
                const std::uint32_t prefix = static_cast<std::uint32_t>(slot) << (32 - firstTableBits);
                const std::uint32_t tableIndex = reverseBits32(prefix);
                if (firstTable[tableIndex] != 0)
                    continue;

                int scan = lowerCursor + 1;
                if (scan < usedEntries)
                {
                    while (scan < usedEntries)
                    {
                        if (codeList[scan] > prefix)
                            break;
                        ++lowerCursor;
                        ++scan;
                    }
                    lowerSaved = lowerCursor;
                }

                while (upperCursor < usedEntries)
                {
                    if (prefix < (prefixMask & codeList[upperCursor]))
                        break;
                    ++upperCursor;
                }

                int lower = lowerSaved;
                int upperDistance = usedEntries - upperCursor;
                if (lower > 0x7FFF)
                    lower = 0x7FFF;
                if (upperDistance > 0x7FFF)
                    upperDistance = 0x7FFF;
                firstTable[tableIndex] = 0x80000000u
                    | (static_cast<std::uint32_t>(lower & 0x7FFF) << 15)
                    | static_cast<std::uint32_t>(upperDistance & 0x7FFF);
                lowerCursor = lowerSaved;
            }
            return 0;
        }


        int initializeDecodeCodebooksForSetup(unsigned char* setup)
        {
            if (readBlockPointer(setup, kSetupFullbookArrayOffset))
                return 0;

            const int bookCount = readBlockField<int>(setup, kSetupBookCountOffset);
            auto* fullbooks = static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(bookCount), kRuntimeBookSize));
            writeBlockPointer(setup, kSetupFullbookArrayOffset, fullbooks);
            for (int index = 0; index < bookCount; ++index)
            {
                auto* staticBook = static_cast<unsigned char*>(readBlockPointer(setup, kSetupBookPointerTableOffset + 4 * static_cast<std::size_t>(index)));
                vorbis_book_init_decode(fullbooks + kRuntimeBookSize * static_cast<std::size_t>(index), staticBook);
                if (readBlockField<int>(staticBook, kCodebookInitializedOffset))
                {
                    releaseCodebookRecord(staticBook);
                    std::free(staticBook);
                }
                writeBlockPointer(setup, kSetupBookPointerTableOffset + 4 * static_cast<std::size_t>(index), nullptr);
            }
            return 0;
        }

        const unsigned char* codebookRecordFromSetup(const unsigned char* setup, int bookIndex)
        {
            if (!setup || bookIndex < 0 || bookIndex >= readBlockField<int>(setup, kSetupBookCountOffset))
                return nullptr;
            const auto* fullbooks = static_cast<const unsigned char*>(readBlockPointer(setup, kSetupFullbookArrayOffset));
            if (!fullbooks)
                return nullptr;
            return fullbooks + kRuntimeBookSize * static_cast<std::size_t>(bookIndex);
        }

        static int decodeCodebookPackedIndex(const unsigned char* codebook, BitCursor& cursor)
        {
            const int maxLength = readBlockField<int>(codebook, kRuntimeBookMaxLengthOffset);
            int availableBits = maxLength;
            const int firstTableBits = readBlockField<int>(codebook, kRuntimeBookFirstTableBitsOffset);
            int look = lookBits(cursor, firstTableBits);
            int low = 0;
            int high = readBlockField<int>(codebook, kRuntimeBookUsedEntriesOffset);

            if (look >= 0)
            {
                const auto* firstTable = static_cast<const std::uint32_t*>(readBlockPointer(codebook, kRuntimeBookFirstTableOffset));
                const std::uint32_t packed = firstTable[look];
                if (!(packed & 0x80000000u))
                {
                    const int decoded = static_cast<int>(packed) - 1;
                    const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kRuntimeBookDecodeLengthsOffset));
                    advanceBits(cursor, lengths[decoded]);
                    return decoded;
                }
                low = static_cast<int>((packed >> 15) & 0x7FFFu);
                high -= static_cast<int>(packed & 0x7FFFu);
            }

            look = lookBits(cursor, availableBits);
            if (look < 0)
            {
                while (availableBits > 1)
                {
                    --availableBits;
                    look = lookBits(cursor, availableBits);
                    if (look >= 0)
                        break;
                }
                if (look < 0)
                    return -1;
            }

            const std::uint32_t reversed = reverseBits32(static_cast<std::uint32_t>(look));
            const auto* codeList = static_cast<const std::uint32_t*>(readBlockPointer(codebook, kRuntimeBookCodeListOffset));
            while (high - low > 1)
            {
                const int half = (high - low) >> 1;
                if (reversed < codeList[low + half])
                    high -= half;
                else
                    low += half;
            }

            const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kRuntimeBookDecodeLengthsOffset));
            if (lengths[low] > availableBits)
            {
                advanceBits(cursor, availableBits);
                return -1;
            }
            advanceBits(cursor, lengths[low]);
            return low;
        }

        int decodeCodebookEntryIndex(const unsigned char* codebook, BitCursor& cursor)
        {
            int availableBits = readBlockField<int>(codebook, kRuntimeBookMaxLengthOffset);
            const int firstTableBits = readBlockField<int>(codebook, kRuntimeBookFirstTableBitsOffset);
            int low = 0;
            int high = readBlockField<int>(codebook, kRuntimeBookUsedEntriesOffset);

            int look = lookBits(cursor, firstTableBits);
            if (look >= 0)
            {
                const auto* firstTable = static_cast<const std::uint32_t*>(readBlockPointer(codebook, kRuntimeBookFirstTableOffset));
                const std::uint32_t packed = firstTable[look];
                if (!(packed & 0x80000000u))
                {
                    const int packedEntry = static_cast<int>(packed) - 1;
                    const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kRuntimeBookDecodeLengthsOffset));
                    advanceBits(cursor, lengths[packedEntry]);
                    const auto* decodeIndex = static_cast<const int*>(readBlockPointer(codebook, kRuntimeBookDecodeIndexOffset));
                    return decodeIndex[packedEntry];
                }
                low = static_cast<int>((packed >> 15) & 0x7FFFu);
                high -= static_cast<int>(packed & 0x7FFFu);
            }

            look = lookBits(cursor, availableBits);
            if (look < 0)
            {
                while (availableBits > 1)
                {
                    --availableBits;
                    look = lookBits(cursor, availableBits);
                    if (look >= 0)
                        break;
                }
                if (look < 0)
                    return -1;
            }

            const std::uint32_t reversed = reverseBits32(static_cast<std::uint32_t>(look));
            const auto* codeList = static_cast<const std::uint32_t*>(readBlockPointer(codebook, kRuntimeBookCodeListOffset));
            int span = high - low;
            while (span > 1)
            {
                const int half = span >> 1;
                if (reversed < codeList[low + half])
                    high -= half;
                else
                    low += half;
                span = high - low;
            }

            const auto* lengths = static_cast<const unsigned char*>(readBlockPointer(codebook, kRuntimeBookDecodeLengthsOffset));
            if (lengths[low] > availableBits)
            {
                advanceBits(cursor, availableBits);
                return -1;
            }
            advanceBits(cursor, lengths[low]);
            const auto* decodeIndex = static_cast<const int*>(readBlockPointer(codebook, kRuntimeBookDecodeIndexOffset));
            return low >= 0 ? decodeIndex[low] : low;
        }

        int vorbis_book_decode(const unsigned char* codebook, BitCursor& cursor)
        {
            return decodeCodebookEntryIndex(codebook, cursor);
        }

        int decodeCodebookVectorAdd(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleStride)
        {
            if (!codebook || !target || sampleStride <= 0)
                return -1;
            const int packed = decodeCodebookPackedIndex(codebook, cursor);
            if (packed < 0)
                return -1;
            const int dimensions = readBlockField<int>(codebook, kRuntimeBookDimensionsOffset);
            const auto* values = static_cast<const float*>(readBlockPointer(codebook, kRuntimeBookValueListOffset));
            if (!values)
                return -1;
            for (int dimension = 0; dimension < dimensions; ++dimension)
                target[dimension * sampleStride] += values[static_cast<std::size_t>(packed) * static_cast<std::size_t>(dimensions) + static_cast<std::size_t>(dimension)];
            return 0;
        }

        int decodeCodebookVectorsStridedAdd(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount)
        {
            if (!codebook || !target || sampleCount <= 0)
                return 0;
            const int dimensions = readBlockField<int>(codebook, kRuntimeBookDimensionsOffset);
            const auto* values = static_cast<const float*>(readBlockPointer(codebook, kRuntimeBookValueListOffset));
            if (dimensions <= 0 || !values)
                return -1;
            const int step = sampleCount / dimensions;
            if (step <= 0)
                return 0;

            // Retail decodeCodebookVectorsStridedAdd keeps both packed-entry ids and resolved vector pointers on stack.
            auto* entries = static_cast<int*>(AS1_XIPH_BOOK_ALLOCA(sizeof(int) * static_cast<std::size_t>(step)));
            auto** vectors = static_cast<const float**>(AS1_XIPH_BOOK_ALLOCA(sizeof(float*) * static_cast<std::size_t>(step)));
            for (int i = 0; i < step; ++i)
            {
                entries[i] = decodeCodebookPackedIndex(codebook, cursor);
                if (entries[i] == -1)
                    return -1;
                vectors[i] = values + static_cast<std::size_t>(entries[i]) * static_cast<std::size_t>(dimensions);
            }
            for (int dimension = 0; dimension < dimensions; ++dimension)
            {
                float* out = target + dimension * step;
                for (int i = 0; i < step; ++i)
                    out[i] += vectors[i][dimension];
            }
            return 0;
        }

        int vorbis_book_decodevs_add(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount)
        {
            return decodeCodebookVectorsStridedAdd(codebook, cursor, target, sampleCount);
        }

        int decodeCodebookVectorsAdd(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount)
        {
            if (!codebook || !target || sampleCount <= 0)
                return 0;
            const int dimensions = readBlockField<int>(codebook, kRuntimeBookDimensionsOffset);
            const auto* values = static_cast<const float*>(readBlockPointer(codebook, kRuntimeBookValueListOffset));
            if (dimensions <= 0 || !values)
                return -1;
            int written = 0;
            while (written < sampleCount)
            {
                const int decoded = decodeCodebookPackedIndex(codebook, cursor);
                if (decoded < 0)
                    return -1;
                const float* vector = values + static_cast<std::size_t>(decoded) * static_cast<std::size_t>(dimensions);
                for (int dimension = 0; dimension < dimensions && written < sampleCount; ++dimension)
                    target[written++] += vector[dimension];
            }
            return 0;
        }

        int vorbis_book_decodev_add(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount)
        {
            return decodeCodebookVectorsAdd(codebook, cursor, target, sampleCount);
        }

        int decodeCodebookVectorsSet(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount)
        {
            if (!codebook || !target || sampleCount <= 0)
                return 0;
            const int dimensions = readBlockField<int>(codebook, kRuntimeBookDimensionsOffset);
            const auto* values = static_cast<const float*>(readBlockPointer(codebook, kRuntimeBookValueListOffset));
            if (dimensions <= 0 || !values)
                return -1;
            int written = 0;
            while (written < sampleCount)
            {
                const int decoded = decodeCodebookPackedIndex(codebook, cursor);
                if (decoded < 0)
                    return -1;
                const float* vector = values + static_cast<std::size_t>(decoded) * static_cast<std::size_t>(dimensions);
                for (int dimension = 0; dimension < dimensions && written < sampleCount; ++dimension)
                    target[written++] = vector[dimension];
            }
            return 0;
        }

        int vorbis_book_decodev_set(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount)
        {
            return decodeCodebookVectorsSet(codebook, cursor, target, sampleCount);
        }

        int decodeCodebookVectorsAcrossChannelsAdd(const unsigned char* codebook, BitCursor& cursor, float** channels, int offset, int channelCount, int sampleCount)
        {
            if (!codebook || !channels || channelCount <= 0 || sampleCount <= 0)
                return 0;
            const int dimensions = readBlockField<int>(codebook, kRuntimeBookDimensionsOffset);
            const auto* values = static_cast<const float*>(readBlockPointer(codebook, kRuntimeBookValueListOffset));
            if (dimensions <= 0 || !values)
                return -1;

            int channel = 0;
            int sample = offset / channelCount;
            const int end = (offset + sampleCount) / channelCount;
            while (sample < end)
            {
                const int decoded = decodeCodebookPackedIndex(codebook, cursor);
                if (decoded < 0)
                    return -1;
                const float* vector = values + static_cast<std::size_t>(decoded) * static_cast<std::size_t>(dimensions);
                for (int dimension = 0; dimension < dimensions; ++dimension)
                {
                    if (channels[channel])
                        channels[channel][sample] += vector[dimension];
                    if (++channel == channelCount)
                    {
                        channel = 0;
                        ++sample;
                        if (sample >= end)
                            break;
                    }
                }
            }
            return 0;
        }

        int vorbis_book_decodevv_add(const unsigned char* codebook, BitCursor& cursor, float** channels, int offset, int channelCount, int sampleCount)
        {
            return decodeCodebookVectorsAcrossChannelsAdd(codebook, cursor, channels, offset, channelCount, sampleCount);
        }

        int releaseCodebookRecord(void* record)
        {
            if (!record)
                return 0;
            auto* codebook = static_cast<unsigned char*>(record);
            const int initialized = readBlockField<int>(codebook, kCodebookInitializedOffset);
            if (initialized)
            {
                if (void* quantValues = readBlockPointer(codebook, kCodebookQuantValueListOffset))
                    std::free(quantValues);
                if (void* lengthList = readBlockPointer(codebook, kCodebookLengthListOffset))
                    std::free(lengthList);
                if (void* helperA = readBlockPointer(codebook, kCodebookAuxiliaryAOffset))
                {
                    auto* helper = static_cast<unsigned char*>(helperA);
                    if (void* p0 = readBlockPointer(helper, 0x00))
                        std::free(p0);
                    if (void* p1 = readBlockPointer(helper, 0x04))
                        std::free(p1);
                    if (void* p2 = readBlockPointer(helper, 0x08))
                        std::free(p2);
                    if (void* p3 = readBlockPointer(helper, 0x0C))
                        std::free(p3);
                    std::memset(helperA, 0, 0x18);
                    std::free(helperA);
                }
                if (void* helperB = readBlockPointer(codebook, kCodebookAuxiliaryBOffset))
                {
                    auto* helper = static_cast<unsigned char*>(helperB);
                    if (void* p0 = readBlockPointer(helper, 0x00))
                        std::free(p0);
                    if (void* p1 = readBlockPointer(helper, 0x04))
                        std::free(p1);
                    writeBlockPointer(helper, 0x00, nullptr);
                    writeBlockPointer(helper, 0x04, nullptr);
                    writeBlockField<int>(helper, 0x08, 0);
                    writeBlockField<int>(helper, 0x0C, 0);
                    std::free(helperB);
                }
                std::memset(record, 0, 0x34);
            }
            return 0;
        }

        int parseCodebookRecordBoundary(BitCursor& cursor, void* codebookRecord)
        {
            if (!codebookRecord)
                return -1;

            auto* codebook = static_cast<unsigned char*>(codebookRecord);
            std::memset(codebook, 0, 0x34);
            writeBlockField<int>(codebook, kCodebookInitializedOffset, 1);

            auto fail = [&]() -> int
            {
                releaseCodebookRecord(codebookRecord);
                return -1;
            };

            if (readBits(cursor, 24) != 0x564342)
                return fail();

            writeBlockField<int>(codebook, kCodebookDimensionsOffset, readBits(cursor, 16));
            const int entries = readBits(cursor, 24);
            writeBlockField<int>(codebook, kCodebookEntriesOffset, entries);
            if (entries == -1)
                return fail();

            const int ordered = readBits(cursor, 1);
            void* lengthList = std::malloc(4u * static_cast<std::size_t>(entries));
            writeBlockPointer(codebook, kCodebookLengthListOffset, lengthList);
            if (!lengthList && entries > 0)
                return fail();

            if (ordered)
            {
                if (ordered != 1)
                    return -1;
                int currentLength = readBits(cursor, 5) + 1;
                for (int index = 0; index < entries; ++currentLength)
                {
                    const int bitCount = countBitsForUnsignedValue(static_cast<unsigned int>(entries - index));
                    const int runCount = readBits(cursor, bitCount);
                    if (runCount == -1)
                        return fail();
                    for (int run = 0; run < runCount; ++run)
                    {
                        if (index >= entries)
                            break;
                        writeBlockField<int>(static_cast<unsigned char*>(lengthList), 4 * static_cast<std::size_t>(index), currentLength);
                        ++index;
                    }
                }
            }
            else
            {
                const int sparse = readBits(cursor, 1);
                if (sparse)
                {
                    for (int index = 0; index < entries; ++index)
                    {
                        if (readBits(cursor, 1))
                        {
                            const int length = readBits(cursor, 5);
                            if (length == -1)
                                return fail();
                            writeBlockField<int>(static_cast<unsigned char*>(lengthList), 4 * static_cast<std::size_t>(index), length + 1);
                        }
                        else
                        {
                            writeBlockField<int>(static_cast<unsigned char*>(lengthList), 4 * static_cast<std::size_t>(index), 0);
                        }
                    }
                }
                else
                {
                    for (int index = 0; index < entries; ++index)
                    {
                        const int length = readBits(cursor, 5);
                        if (length == -1)
                            return fail();
                        writeBlockField<int>(static_cast<unsigned char*>(lengthList), 4 * static_cast<std::size_t>(index), length + 1);
                    }
                }
            }

            const int lookupType = readBits(cursor, 4);
            writeBlockField<int>(codebook, kCodebookLookupTypeOffset, lookupType);
            if (lookupType)
            {
                if (lookupType <= 0 || lookupType > 2)
                    return fail();

                writeBlockField<int>(codebook, kCodebookMinimumValueOffset, readBits(cursor, 32));
                writeBlockField<int>(codebook, kCodebookDeltaValueOffset, readBits(cursor, 32));
                writeBlockField<int>(codebook, kCodebookValueBitsOffset, readBits(cursor, 4) + 1);
                writeBlockField<int>(codebook, kCodebookSequenceFlagOffset, readBits(cursor, 1));

                int quantValueCount = 0;
                if (lookupType == 1)
                    quantValueCount = lookup1ValueCount(codebook);
                else if (lookupType == 2)
                    quantValueCount = readBlockField<int>(codebook, kCodebookEntriesOffset) * readBlockField<int>(codebook, kCodebookDimensionsOffset);

                void* quantValueList = std::malloc(4u * static_cast<std::size_t>(quantValueCount));
                writeBlockPointer(codebook, kCodebookQuantValueListOffset, quantValueList);
                if (!quantValueList && quantValueCount > 0)
                    return fail();

                for (int index = 0; index < quantValueCount; ++index)
                    writeBlockField<int>(static_cast<unsigned char*>(quantValueList), 4 * static_cast<std::size_t>(index), readBits(cursor, readBlockField<int>(codebook, kCodebookValueBitsOffset)));

                if (quantValueCount != 0
                    && readBlockField<int>(static_cast<unsigned char*>(quantValueList), 4 * static_cast<std::size_t>(quantValueCount) - 4) == -1)
                {
                    return fail();
                }
            }

            return 0;
        }

} } }
