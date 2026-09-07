#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        void releaseFloor1Record(void* record)
        {
            if (!record)
                return;
            std::memset(record, 0, 0x460);
            std::free(record);
        }

        void* parseFloor1SetupRecord(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return nullptr;
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            if (!setup)
                return nullptr;

            auto* floor = static_cast<unsigned char*>(std::calloc(1, 0x460));
            if (!floor)
                return nullptr;

            const int partitionCount = readBits(cursor, 5);
            writeBlockField<int>(floor, 0x00, partitionCount);
            int maximumClass = -1;
            for (int index = 0; index < partitionCount; ++index)
            {
                const int partitionClass = readBits(cursor, 4);
                writeBlockField<int>(floor, 0x04 + 4 * static_cast<std::size_t>(index), partitionClass);
                if (maximumClass < partitionClass)
                    maximumClass = partitionClass;
            }

            const int classCount = maximumClass + 1;
            for (int classIndex = 0; classIndex < classCount; ++classIndex)
            {
                writeBlockField<int>(floor, 0x80 + 4 * static_cast<std::size_t>(classIndex), readBits(cursor, 3) + 1);
                const int subclassBits = readBits(cursor, 2);
                writeBlockField<int>(floor, 0xC0 + 4 * static_cast<std::size_t>(classIndex), subclassBits);
                if (subclassBits < 0)
                {
                    releaseFloor1Record(floor);
                    return nullptr;
                }
                if (subclassBits)
                    writeBlockField<int>(floor, 0x100 + 4 * static_cast<std::size_t>(classIndex), readBits(cursor, 8));
                const int masterBook = readBlockField<int>(floor, 0x100 + 4 * static_cast<std::size_t>(classIndex));
                if (masterBook < 0 || masterBook >= readBlockField<int>(setup, kSetupBookCountOffset))
                {
                    releaseFloor1Record(floor);
                    return nullptr;
                }

                const int subclassBookCount = 1 << subclassBits;
                for (int subIndex = 0; subIndex < subclassBookCount; ++subIndex)
                {
                    const int book = readBits(cursor, 8) - 1;
                    writeBlockField<int>(floor, 0x140 + 32 * static_cast<std::size_t>(classIndex) + 4 * static_cast<std::size_t>(subIndex), book);
                    if (book < -1 || book >= readBlockField<int>(setup, kSetupBookCountOffset))
                    {
                        releaseFloor1Record(floor);
                        return nullptr;
                    }
                }
            }

            writeBlockField<int>(floor, 0x340, readBits(cursor, 2) + 1);
            const int rangeBits = readBits(cursor, 4);
            int writtenPosts = 0;
            int requiredPosts = 0;
            for (int partitionIndex = 0; partitionIndex < partitionCount; ++partitionIndex)
            {
                const int partitionClass = readBlockField<int>(floor, 0x04 + 4 * static_cast<std::size_t>(partitionIndex));
                requiredPosts += readBlockField<int>(floor, 0x80 + 4 * static_cast<std::size_t>(partitionClass));
                while (writtenPosts < requiredPosts)
                {
                    const int point = readBits(cursor, rangeBits);
                    writeBlockField<int>(floor, 0x34C + 4 * static_cast<std::size_t>(writtenPosts), point);
                    if (point < 0 || point >= (1 << rangeBits))
                    {
                        releaseFloor1Record(floor);
                        return nullptr;
                    }
                    ++writtenPosts;
                }
            }
            // Retail vorbis_info_floor1 physical postlist starts at +0x344.
            writeBlockField<int>(floor, 0x344, 0);
            writeBlockField<int>(floor, 0x348, 1 << rangeBits);
            return floor;
        }

        int floor1RenderPoint(int x0, int x1, int y0, int y1, int x)
        {
            const int dy = y1 - y0;
            const int adx = x1 - x0;
            if (adx == 0)
                return y0;
            const int ady = std::abs(dy);
            const int err = ady * (x - x0) / adx;
            return dy < 0 ? y0 - err : y0 + err;
        }

        void renderFloorLineIntoSamples(int x0, int x1, int y0, int y1, float* samples, int sampleCount)
        {
            if (!samples || sampleCount <= 0)
                return;
            if (x0 < 0)
                x0 = 0;
            if (x1 > sampleCount)
                x1 = sampleCount;
            if (x1 <= x0)
                return;

            const int dy = y1 - y0;
            const int adx = x1 - x0;
            const int ady = std::abs(dy);
            int err = 0;
            int y = y0;
            const int base = adx ? dy / adx : 0;
            const int step = dy < 0 ? base - 1 : base + 1;
            const int baseStep = base;
            const int errLimit = adx ? ady - std::abs(adx * base) : 0;

            for (int x = x0; x < x1; ++x)
            {
                samples[x] *= floorLookupGainFromIndex(y);
                err += errLimit;
                if (err >= adx)
                {
                    err -= adx;
                    y += step;
                }
                else
                {
                    y += baseStep;
                }
            }
        }


        namespace
        {
            int floor1PostCount(const unsigned char* floorInfo)
            {
                int posts = 2;
                const int partitions = readBlockField<int>(floorInfo, 0x00);
                for (int partition = 0; partition < partitions; ++partition)
                {
                    const int cls = readBlockField<int>(floorInfo, 0x04 + 4 * static_cast<std::size_t>(partition));
                    posts += readBlockField<int>(floorInfo, 0x80 + 4 * static_cast<std::size_t>(cls));
                }
                return posts;
            }

            int floor1QuantQ(int multiplier)
            {
                switch (multiplier)
                {
                case 1: return 256;
                case 2: return 128;
                case 3: return 86;
                case 4: return 64;
                default: return 0;
                }
            }
        }

        void releaseFloor1LookRecord(void* owner)
        {
            if (!owner)
                return;
            std::memset(owner, 0, 0x520);
            std::free(owner);
        }

        int compareFloor1PostPositions(const void* lhs, const void* rhs)
        {
            const auto* const* a = static_cast<const int* const*>(lhs);
            const auto* const* b = static_cast<const int* const*>(rhs);
            return **a - **b;
        }

        void* buildFloor1LookRecord(const unsigned char* info, const unsigned char* floorInfo)
        {
            (void)info;
            if (!floorInfo)
                return nullptr;
            auto* look = static_cast<unsigned char*>(std::calloc(1, 0x520));
            if (!look)
                return nullptr;

            const int posts = floor1PostCount(floorInfo);
            writeBlockPointer(look, 0x510, const_cast<unsigned char*>(floorInfo));
            writeBlockField<int>(look, 0x508, readBlockField<int>(floorInfo, 0x348));
            writeBlockField<int>(look, 0x504, posts);
            writeBlockField<int>(look, 0x50C, floor1QuantQ(readBlockField<int>(floorInfo, 0x340)));
            if (posts < 2 || posts > 65 || readBlockField<int>(look, 0x50C) == 0)
            {
                releaseFloor1LookRecord(look);
                return nullptr;
            }

            const int* order[65]{};
            const auto* postlist = reinterpret_cast<const int*>(floorInfo + 0x344);
            for (int i = 0; i < posts; ++i)
                order[i] = postlist + i;
            std::qsort(order, static_cast<std::size_t>(posts), sizeof(order[0]), compareFloor1PostPositions);
            for (int sorted = 0; sorted < posts; ++sorted)
            {
                const int original = static_cast<int>(order[sorted] - postlist);
                writeBlockField<int>(look, 0x104 + 4 * static_cast<std::size_t>(sorted), original);
                writeBlockField<int>(look, 0x208 + 4 * static_cast<std::size_t>(original), sorted);
                writeBlockField<int>(look, 0x000 + 4 * static_cast<std::size_t>(sorted),
                    readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(original)));
            }

            for (int post = 2; post < posts; ++post)
            {
                const int x = readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(post));
                int low = 0;
                int high = 1;
                int lowX = readBlockField<int>(floorInfo, 0x344);
                int highX = readBlockField<int>(floorInfo, 0x348);
                for (int prior = 0; prior < post; ++prior)
                {
                    const int candidate = readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(prior));
                    if (candidate < x && candidate > lowX)
                    {
                        low = prior;
                        lowX = candidate;
                    }
                    if (candidate > x && candidate < highX)
                    {
                        high = prior;
                        highX = candidate;
                    }
                }
                writeBlockField<int>(look, 0x408 + 4 * static_cast<std::size_t>(post - 2), low);
                writeBlockField<int>(look, 0x30C + 4 * static_cast<std::size_t>(post - 2), high);
            }
            return look;
        }

        void* decodeFloor1Memo(unsigned char* block, unsigned char* floorLook)
        {
            if (!block || !floorLook)
                return nullptr;
            auto* floorInfo = static_cast<unsigned char*>(readBlockPointer(floorLook, 0x510));
            auto* synthesisState = static_cast<unsigned char*>(readBlockPointer(block, kBlockQueueOwnerOffset));
            auto* info = synthesisState ? static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset)) : nullptr;
            auto* setup = info ? static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset)) : nullptr;
            if (!floorInfo || !setup)
                return nullptr;
            auto& cursor = *reinterpret_cast<BitCursor*>(block + 0x04);
            if (readBits(cursor, 1) != 1)
                return nullptr;

            const int posts = readBlockField<int>(floorLook, 0x504);
            auto* fit = static_cast<int*>(allocateBlockScratch(block, 4 * posts));
            if (!fit)
                return nullptr;
            const int quantQ = readBlockField<int>(floorLook, 0x50C);
            int rangeBits = 0;
            for (unsigned int remaining = quantQ > 0 ? static_cast<unsigned int>(quantQ - 1) : 0; remaining; remaining >>= 1)
                ++rangeBits;
            fit[0] = readBits(cursor, rangeBits);
            fit[1] = readBits(cursor, rangeBits);
            if (fit[0] < 0 || fit[1] < 0)
                return nullptr;

            int post = 2;
            const int partitions = readBlockField<int>(floorInfo, 0x00);
            for (int partition = 0; partition < partitions; ++partition)
            {
                const int cls = readBlockField<int>(floorInfo, 0x04 + 4 * static_cast<std::size_t>(partition));
                const int cdim = readBlockField<int>(floorInfo, 0x80 + 4 * static_cast<std::size_t>(cls));
                const int cbits = readBlockField<int>(floorInfo, 0xC0 + 4 * static_cast<std::size_t>(cls));
                int cval = 0;
                if (cbits)
                {
                    const int master = readBlockField<int>(floorInfo, 0x100 + 4 * static_cast<std::size_t>(cls));
                    const auto* book = codebookRecordFromSetup(setup, master);
                    cval = book ? decodeCodebookEntryIndex(book, cursor) : -1;
                    if (cval == -1)
                        return nullptr;
                }
                const int mask = (1 << cbits) - 1;
                for (int k = 0; k < cdim && post < posts; ++k, ++post)
                {
                    const int bookIndex = readBlockField<int>(floorInfo,
                        0x140 + 32 * static_cast<std::size_t>(cls) + 4 * static_cast<std::size_t>(cval & mask));
                    cval >>= cbits;
                    if (bookIndex < 0)
                        fit[post] = 0;
                    else
                    {
                        const auto* book = codebookRecordFromSetup(setup, bookIndex);
                        fit[post] = book ? decodeCodebookEntryIndex(book, cursor) : -1;
                        if (fit[post] == -1)
                            return nullptr;
                    }
                }
            }

            for (int i = 2; i < posts; ++i)
            {
                const int low = readBlockField<int>(floorLook, 0x408 + 4 * static_cast<std::size_t>(i - 2));
                const int high = readBlockField<int>(floorLook, 0x30C + 4 * static_cast<std::size_t>(i - 2));
                const int predicted = floor1RenderPoint(
                    readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(low)),
                    readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(high)),
                    fit[low], fit[high],
                    readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(i)));
                const int highroom = quantQ - predicted;
                const int lowroom = predicted;
                const int room = 2 * std::min(highroom, lowroom);
                int value = fit[i];
                if (value)
                {
                    if (value < room)
                        value = (value & 1) ? -((value + 1) >> 1) : (value >> 1);
                    else if (highroom <= lowroom)
                        value = highroom - value - 1;
                    else
                        value -= lowroom;
                    fit[i] = predicted + value;
                    fit[low] &= 0x7FFF;
                    fit[high] &= 0x7FFF;
                }
                else
                {
                    fit[i] = predicted | 0x8000;
                }
            }
            return fit;
        }

        int applyFloor1Memo(unsigned char* block, unsigned char* floorLook, void* memo, float* samples)
        {
            if (!block || !floorLook || !samples)
                return 0;
            auto* floorInfo = static_cast<unsigned char*>(readBlockPointer(floorLook, 0x510));
            auto* synthesisState = static_cast<unsigned char*>(readBlockPointer(block, kBlockQueueOwnerOffset));
            auto* info = synthesisState ? static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset)) : nullptr;
            auto* setup = info ? static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset)) : nullptr;
            if (!floorInfo || !setup)
                return 0;
            const int W = readBlockField<int>(block, kBlockLongModeOffset);
            const int n = readBlockField<int>(setup, 4 * static_cast<std::size_t>(W)) / 2;
            if (!memo)
            {
                if (n > 0)
                    std::memset(samples, 0, 4 * static_cast<std::size_t>(n));
                return 0;
            }

            auto* fit = static_cast<int*>(memo);
            const int posts = readBlockField<int>(floorLook, 0x504);
            const int multiplier = readBlockField<int>(floorInfo, 0x340);
            int hx = 0;
            const int firstPost = readBlockField<int>(floorLook, 0x104);
            int hy = fit[firstPost] * multiplier;
            for (int i = 1; i < posts; ++i)
            {
                const int post = readBlockField<int>(floorLook, 0x104 + 4 * static_cast<std::size_t>(i));
                const int value = fit[post];
                if ((value & 0x7FFF) == value)
                {
                    const int x = readBlockField<int>(floorInfo, 0x344 + 4 * static_cast<std::size_t>(post));
                    const int y = value * multiplier;
                    renderFloorLineIntoSamples(hx, std::min(x, n), hy, y, samples, n);
                    hx = x;
                    hy = y;
                }
            }
            if (hx < n)
            {
                const float gain = floorLookupGainFromIndex(hy);
                for (int i = hx; i < n; ++i)
                    samples[i] *= gain;
            }
            return 1;
        }



} } }
