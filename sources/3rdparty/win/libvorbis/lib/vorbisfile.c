#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        int g_callbackReadErrorFlag = 0;

        void* readBlockPointer(const unsigned char* base, std::size_t offset)
        {
            // Retail AS1 is a Win32/x86 binary. Every pointer-bearing Xiph field
            // visible in the executable is one DWORD wide; using sizeof(void*)
            // here corrupts the next retail field when this source is parsed or
            // smoke-built by a 64-bit host compiler.
            const std::uint32_t raw = readBlockField<std::uint32_t>(base, offset);
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw));
        }

        void writeBlockPointer(unsigned char* base, std::size_t offset, void* value)
        {
            const std::uint32_t raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
            writeBlockField<std::uint32_t>(base, offset, raw);
        }

        void* readPointer(const VorbisStateBlob& state, std::size_t offset)
        {
            const std::uint32_t raw = readField<std::uint32_t>(state, offset);
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw));
        }

        void writePointer(VorbisStateBlob& state, std::size_t offset, void* value)
        {
            const std::uint32_t raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
            writeField<std::uint32_t>(state, offset, raw);
        }

        std::int64_t readSigned64(const VorbisStateBlob& state, std::size_t lowOffset)
        {
            const std::uint32_t low = readField<std::uint32_t>(state, lowOffset);
            const std::int32_t high = readField<std::int32_t>(state, lowOffset + 4);
            return (static_cast<std::int64_t>(high) << 32) | low;
        }

        void writeSigned64(VorbisStateBlob& state, std::size_t lowOffset, std::int64_t value)
        {
            writeField<std::uint32_t>(state, lowOffset, static_cast<std::uint32_t>(value));
            writeField<std::int32_t>(state, lowOffset + 4, static_cast<std::int32_t>(value >> 32));
        }

        OffsetPair splitOffset(std::int64_t value)
        {
            return {static_cast<std::uint32_t>(value), static_cast<std::int32_t>(value >> 32)};
        }

        std::int64_t combineOffset(std::uint32_t low, std::int32_t high)
        {
            return (static_cast<std::int64_t>(high) << 32) | low;
        }

        bool nonNegative(std::int64_t value)
        {
            return value >= 0;
        }

        void releasePointerField(VorbisStateBlob& state, std::size_t offset)
        {
            void* pointer = readPointer(state, offset);
            if (pointer)
            {
                std::free(pointer);
                writePointer(state, offset, nullptr);
            }
        }

        void releaseLinkArrays(VorbisStateBlob& state)
        {
            const int linkCount = readField<int>(state, kLinkCountOffset);
            void* infoList = readPointer(state, kInfoListOffset);
            void* commentList = readPointer(state, kCommentListOffset);

            if (infoList && linkCount > 0)
            {
                auto* records = static_cast<unsigned char*>(infoList);
                for (int index = 0; index < linkCount; ++index)
                    releaseInfoRecord(records + 0x20 * static_cast<std::size_t>(index));
            }

            if (commentList && linkCount > 0)
            {
                auto* records = static_cast<unsigned char*>(commentList);
                for (int index = 0; index < linkCount; ++index)
                    releaseCommentRecord(records + 0x10 * static_cast<std::size_t>(index));
            }

            releasePointerField(state, kInfoListOffset);
            releasePointerField(state, kCommentListOffset);
        }

        // 0x00457320: retail stdio seek callback. The third (high) word is part of
        // the four-dword callback ABI but retail ignores it and forwards low+origin
        // to fseek after rejecting a null FILE owner.
        int seekFileCallback(void* owner, int low, int high, int origin)
        {
            (void)high;
            if (!owner)
                return -1;
            return std::fseek(static_cast<std::FILE*>(owner), low, origin);
        }

        std::size_t readFileCallback(void* buffer, std::size_t size, std::size_t count, void* owner)
        {
            if (!owner)
                return 0;
            auto* file = static_cast<std::FILE*>(owner);
            const std::size_t readCount = std::fread(buffer, size, count, file);
            if (readCount == 0 && std::ferror(file))
                g_callbackReadErrorFlag = 1;
            return readCount;
        }

        int closeFileCallback(void* owner)
        {
            if (!owner)
                return 0;
            return std::fclose(static_cast<std::FILE*>(owner));
        }

        int tellFileCallback(void* owner)
        {
            if (!owner)
                return -1;
            return std::ftell(static_cast<std::FILE*>(owner));
        }

        void releasePacketStreamResources(VorbisStateBlob& state)
        {
            auto* stream = state.bytes + kPacketStreamOffset;
            if (void* pointer = readBlockPointer(stream, kPacketBodyDataOffset))
                std::free(pointer);
            if (void* pointer = readBlockPointer(stream, kPacketLacingValuesOffset))
                std::free(pointer);
            if (void* pointer = readBlockPointer(stream, kPacketGranuleValuesOffset))
                std::free(pointer);
            std::memset(stream, 0, kPacketStreamSize);
        }

        void resetPacketStreamCounters(VorbisStateBlob& state)
        {
            auto* stream = state.bytes + kPacketStreamOffset;
            writeBlockField<int>(stream, kPacketNumberLowOffset, 0);
            writeBlockField<int>(stream, kPacketNumberLowOffset + 4, 0);
            writeBlockField<int>(stream, kPacketBodyFillOffset, 0);
            writeBlockField<int>(stream, kPacketBodyReturnedOffset, 0);
            writeBlockField<int>(stream, kPacketLacingFillOffset, 0);
            writeBlockField<int>(stream, kPacketLacingPacketOffset, 0);
            writeBlockField<int>(stream, kPacketLacingReturnedOffset, 0);
            writeBlockField<int>(stream, kPacketEndOfStreamOffset, 0);
            writeBlockField<int>(stream, kPacketBeginOfStreamOffset, 0);
            writeBlockField<int>(stream, kPacketSerialNumberOffset, 0);
            writeBlockField<int>(stream, kPacketPageNumberOffset, -1);
            writeBlockField<int>(stream, kPacketGranuleLowOffset, 0);
            writeBlockField<int>(stream, kPacketGranuleLowOffset + 4, 0);
        }

        void initializePacketStreamResources(VorbisStateBlob& state, int serialNumber)
        {
            releasePacketStreamResources(state);
            auto* stream = state.bytes + kPacketStreamOffset;
            writeBlockField<int>(stream, kPacketBodyStorageOffset, 0x4000);
            writeBlockPointer(stream, kPacketBodyDataOffset, std::malloc(0x4000));
            writeBlockField<int>(stream, kPacketLacingStorageOffset, 1024);
            writeBlockPointer(stream, kPacketLacingValuesOffset, std::malloc(0x1000));
            writeBlockPointer(stream, kPacketGranuleValuesOffset, std::malloc(8 * 1024));
            writeBlockField<int>(stream, kPacketSerialNumberOffset, serialNumber);
            writeField<int>(state, kCurrentSerialOffset, serialNumber);
            writeField<int>(state, kPageSerialMirrorOffset, serialNumber);
            writeBlockField<int>(stream, kPacketPageNumberOffset, -1);
        }

        void initPacketStream(VorbisStateBlob& state, int serialNumber)
        {
            resetPacketStreamCounters(state);
            writeField<int>(state, kPacketStreamOffset + kPacketSerialNumberOffset, serialNumber);
            writeField<int>(state, kCurrentSerialOffset, serialNumber);
            writeField<int>(state, kPageSerialMirrorOffset, serialNumber);
        }

        void ensurePacketBodyStorage(unsigned char* stream, int bytes)
        {
            unsigned char* data = static_cast<unsigned char*>(readBlockPointer(stream, kPacketBodyDataOffset));
            int storage = readBlockField<int>(stream, kPacketBodyStorageOffset);
            const int fill = readBlockField<int>(stream, kPacketBodyFillOffset);
            if (storage <= fill + bytes)
            {
                const int newStorage = storage + bytes + 1024;
                void* resized = std::realloc(data, static_cast<std::size_t>(newStorage));
                if (!resized)
                    return;
                writeBlockPointer(stream, kPacketBodyDataOffset, resized);
                writeBlockField<int>(stream, kPacketBodyStorageOffset, newStorage);
            }
        }

        void ensurePacketLacingStorage(unsigned char* stream, int count)
        {
            int storage = readBlockField<int>(stream, kPacketLacingStorageOffset);
            const int fill = readBlockField<int>(stream, kPacketLacingFillOffset);
            if (storage <= fill + count)
            {
                const int newStorage = storage + count + 32;
                void* lacing = std::realloc(readBlockPointer(stream, kPacketLacingValuesOffset), 4 * static_cast<std::size_t>(newStorage));
                writeBlockPointer(stream, kPacketLacingValuesOffset, lacing);
                writeBlockField<int>(stream, kPacketLacingStorageOffset, newStorage);
                void* granule = std::realloc(readBlockPointer(stream, kPacketGranuleValuesOffset), 8 * static_cast<std::size_t>(newStorage));
                writeBlockPointer(stream, kPacketGranuleValuesOffset, granule);
            }
        }

        void feedPageToPacketStream(VorbisStateBlob& state, const PageToken& page)
        {
            auto* stream = state.bytes + kPacketStreamOffset;
            unsigned char* pageHeader = pageHeaderPointer(page);
            unsigned char* pageBody = pageBodyPointer(page);
            int bodyBytes = pageBodySize(page);
            const int segmentCount = pageHeader ? pageHeader[26] : 0;
            int skippedSegments = 0;
            const int oldBodyReturned = readBlockField<int>(stream, kPacketBodyReturnedOffset);
            const int oldLacingReturned = readBlockField<int>(stream, kPacketLacingReturnedOffset);

            if (!pageHeader || !pageBody)
                return;

            if (oldBodyReturned)
            {
                int fill = readBlockField<int>(stream, kPacketBodyFillOffset) - oldBodyReturned;
                unsigned char* bodyData = static_cast<unsigned char*>(readBlockPointer(stream, kPacketBodyDataOffset));
                writeBlockField<int>(stream, kPacketBodyFillOffset, fill);
                if (fill && bodyData)
                    std::memmove(bodyData, bodyData + oldBodyReturned, static_cast<std::size_t>(fill));
                writeBlockField<int>(stream, kPacketBodyReturnedOffset, 0);
            }

            if (oldLacingReturned)
            {
                int lacingFill = readBlockField<int>(stream, kPacketLacingFillOffset);
                if (lacingFill != oldLacingReturned)
                {
                    std::memmove(
                        static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset)),
                        static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset)) + 4 * oldLacingReturned,
                        4 * static_cast<std::size_t>(lacingFill - oldLacingReturned));
                    std::memmove(
                        static_cast<unsigned char*>(readBlockPointer(stream, kPacketGranuleValuesOffset)),
                        static_cast<unsigned char*>(readBlockPointer(stream, kPacketGranuleValuesOffset)) + 8 * oldLacingReturned,
                        8 * static_cast<std::size_t>(lacingFill - oldLacingReturned));
                }
                writeBlockField<int>(stream, kPacketLacingFillOffset, lacingFill - oldLacingReturned);
                writeBlockField<int>(stream, kPacketLacingPacketOffset, readBlockField<int>(stream, kPacketLacingPacketOffset) - oldLacingReturned);
                writeBlockField<int>(stream, kPacketLacingReturnedOffset, 0);
            }

            if (pageSerialNumber(page) != readBlockField<int>(stream, kPacketSerialNumberOffset))
                return;
            if (pageHeaderVersion(page) > 0)
                return;

            ensurePacketLacingStorage(stream, segmentCount + 1);

            if (pageSequenceNumber(page) != readBlockField<int>(stream, kPacketPageNumberOffset))
            {
                const int lacingPacket = readBlockField<int>(stream, kPacketLacingPacketOffset);
                const int lacingFill = readBlockField<int>(stream, kPacketLacingFillOffset);
                if (lacingPacket < lacingFill)
                {
                    auto* lacing = static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset));
                    for (int index = lacingPacket; index < lacingFill; ++index)
                        writeBlockField<int>(lacing, 4 * index, readBlockField<int>(lacing, 4 * index) & 0xFFFFFF00);
                }
                if (readBlockField<int>(stream, kPacketPageNumberOffset) != -1)
                {
                    auto* lacing = static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset));
                    const int lacingFillBefore = readBlockField<int>(stream, kPacketLacingFillOffset);
                    writeBlockField<int>(lacing, 4 * lacingFillBefore, 1024);
                    writeBlockField<int>(stream, kPacketLacingFillOffset, lacingFillBefore + 1);
                    writeBlockField<int>(stream, kPacketLacingPacketOffset, readBlockField<int>(stream, kPacketLacingPacketOffset) + 1);
                }
                if (pageIsContinued(page))
                {
                    while (skippedSegments < segmentCount)
                    {
                        const int value = pageHeader[27 + skippedSegments];
                        bodyBytes -= value;
                        pageBody += value;
                        ++skippedSegments;
                        if (value < 255)
                            break;
                    }
                }
            }

            if (bodyBytes)
            {
                ensurePacketBodyStorage(stream, bodyBytes);
                unsigned char* bodyData = static_cast<unsigned char*>(readBlockPointer(stream, kPacketBodyDataOffset));
                int bodyFill = readBlockField<int>(stream, kPacketBodyFillOffset);
                if (bodyData)
                {
                    std::memcpy(bodyData + bodyFill, pageBody, static_cast<std::size_t>(bodyBytes));
                    writeBlockField<int>(stream, kPacketBodyFillOffset, bodyFill + bodyBytes);
                }
            }

            int lastComplete = -1;
            for (int index = skippedSegments; index < segmentCount; ++index)
            {
                const int value = pageHeader[27 + index];
                auto* lacing = static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset));
                auto* granule = static_cast<unsigned char*>(readBlockPointer(stream, kPacketGranuleValuesOffset));
                const int lacingFill = readBlockField<int>(stream, kPacketLacingFillOffset);
                if (lacing)
                    writeBlockField<int>(lacing, 4 * lacingFill, value);
                if (granule)
                {
                    writeBlockField<std::uint32_t>(granule, 8 * lacingFill, 0xFFFFFFFFu);
                    writeBlockField<std::int32_t>(granule, 8 * lacingFill + 4, -1);
                }
                if (pageBeginsStream(page))
                {
                    auto* lacing2 = static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset));
                    if (lacing2)
                        writeBlockField<int>(lacing2, 4 * lacingFill, readBlockField<int>(lacing2, 4 * lacingFill) | 0x100);
                }
                writeBlockField<int>(stream, kPacketLacingFillOffset, lacingFill + 1);
                if (value < 255)
                {
                    lastComplete = lacingFill;
                    writeBlockField<int>(stream, kPacketLacingPacketOffset, lacingFill + 1);
                }
            }

            if (lastComplete != -1)
            {
                auto* granule = static_cast<unsigned char*>(readBlockPointer(stream, kPacketGranuleValuesOffset));
                if (granule)
                {
                    const std::int64_t gp = pageGranulePosition(page);
                    writeBlockField<std::uint32_t>(granule, 8 * lastComplete, static_cast<std::uint32_t>(gp));
                    writeBlockField<std::int32_t>(granule, 8 * lastComplete + 4, static_cast<std::int32_t>(gp >> 32));
                }
            }

            if (pageEndsStream(page))
            {
                writeBlockField<int>(stream, kPacketEndOfStreamOffset, 1);
                const int lacingFill = readBlockField<int>(stream, kPacketLacingFillOffset);
                if (lacingFill > 0)
                {
                    auto* lacing = static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset));
                    if (lacing)
                    {
                        const int last = readBlockField<int>(lacing, 4 * (lacingFill - 1));
                        writeBlockField<int>(lacing, 4 * (lacingFill - 1), last | 0x200);
                    }
                }
            }
            writeBlockField<int>(stream, kPacketPageNumberOffset, pageSequenceNumber(page) + 1);
        }

        void writePacketToken(PacketToken& packet, void* data, int bytes, int begins, int ends, std::int64_t granule, std::int64_t packetNumber)
        {
            writeBlockPointer(packet.bytes, 0x00, data);
            writeBlockField<int>(packet.bytes, 0x04, bytes);
            writeBlockField<int>(packet.bytes, 0x08, begins);
            writeBlockField<int>(packet.bytes, 0x0C, ends);
            writeBlockField<std::uint32_t>(packet.bytes, 0x10, static_cast<std::uint32_t>(granule));
            writeBlockField<std::int32_t>(packet.bytes, 0x14, static_cast<std::int32_t>(granule >> 32));
            writeBlockField<std::uint32_t>(packet.bytes, 0x18, static_cast<std::uint32_t>(packetNumber));
            writeBlockField<std::int32_t>(packet.bytes, 0x1C, static_cast<std::int32_t>(packetNumber >> 32));
        }

        int takePacketFromStreamInternal(VorbisStateBlob& state, PacketToken* packet, bool advance)
        {
            auto* stream = state.bytes + kPacketStreamOffset;
            int lacingReturned = readBlockField<int>(stream, kPacketLacingReturnedOffset);
            if (readBlockField<int>(stream, kPacketLacingPacketOffset) <= lacingReturned)
                return 0;

            auto* lacing = static_cast<unsigned char*>(readBlockPointer(stream, kPacketLacingValuesOffset));
            auto* granule = static_cast<unsigned char*>(readBlockPointer(stream, kPacketGranuleValuesOffset));
            if (!lacing || !granule)
                return 0;

            int value = readBlockField<int>(lacing, 4 * lacingReturned);
            if ((value >> 8) & 4)
            {
                const std::uint32_t oldLow = readBlockField<std::uint32_t>(stream, kPacketNumberLowOffset);
                writeBlockField<std::uint32_t>(stream, kPacketNumberLowOffset, oldLow + 1);
                if (oldLow + 1 == 0)
                    writeBlockField<std::uint32_t>(stream, kPacketNumberLowOffset + 4, readBlockField<std::uint32_t>(stream, kPacketNumberLowOffset + 4) + 1);
                writeBlockField<int>(stream, kPacketLacingReturnedOffset, lacingReturned + 1);
                return -1;
            }

            if (!packet && !advance)
                return 1;

            int bytes = value & 0xFF;
            int endFlag = value & 0x200;
            int scanIndex = lacingReturned;
            if ((value & 0xFF) == 255)
            {
                do
                {
                    ++scanIndex;
                    const int nextValue = readBlockField<int>(lacing, 4 * scanIndex);
                    if ((nextValue >> 8) & 2)
                        endFlag = 0x200;
                    bytes += nextValue & 0xFF;
                    value = nextValue;
                }
                while ((value & 0xFF) == 255);
            }

            if (packet)
            {
                const std::int64_t packetNumber = combineOffset(
                    readBlockField<std::uint32_t>(stream, kPacketNumberLowOffset),
                    readBlockField<std::int32_t>(stream, kPacketNumberLowOffset + 4));
                const std::int64_t granulePosition = combineOffset(
                    readBlockField<std::uint32_t>(granule, 8 * scanIndex),
                    readBlockField<std::int32_t>(granule, 8 * scanIndex + 4));
                writePacketToken(
                    *packet,
                    static_cast<unsigned char*>(readBlockPointer(stream, kPacketBodyDataOffset)) + readBlockField<int>(stream, kPacketBodyReturnedOffset),
                    bytes,
                    readBlockField<int>(lacing, 4 * lacingReturned) & 0x100,
                    endFlag,
                    granulePosition,
                    packetNumber);
            }

            if (advance)
            {
                const std::uint32_t oldLow = readBlockField<std::uint32_t>(stream, kPacketNumberLowOffset);
                writeBlockField<int>(stream, kPacketBodyReturnedOffset, readBlockField<int>(stream, kPacketBodyReturnedOffset) + bytes);
                writeBlockField<std::uint32_t>(stream, kPacketNumberLowOffset, oldLow + 1);
                if (oldLow + 1 == 0)
                    writeBlockField<std::uint32_t>(stream, kPacketNumberLowOffset + 4, readBlockField<std::uint32_t>(stream, kPacketNumberLowOffset + 4) + 1);
                writeBlockField<int>(stream, kPacketLacingReturnedOffset, scanIndex + 1);
            }
            return 1;
        }

        int takePacketFromStream(VorbisStateBlob& state, PacketToken& packet)
        {
            return takePacketFromStreamInternal(state, &packet, true);
        }

        int refillSyncBuffer(VorbisStateBlob& state)
        {
            g_callbackReadErrorFlag = 0;
            void* inputOwner = readPointer(state, kInputOwnerOffset);
            if (!inputOwner)
                return 0;

            auto callback = reinterpret_cast<ReadCallback>(static_cast<std::uintptr_t>(readField<std::uint32_t>(state, kCallbackReadDataOffset)));
            unsigned char* target = reserveSyncBuffer(state, kReadBlockSize);
            const int readBytes = static_cast<int>(callback(target, 1, kReadBlockSize, inputOwner));
            if (readBytes > 0)
                markSyncBytesWritten(state, readBytes);
            if (readBytes == 0 && g_callbackReadErrorFlag)
                return -1;
            return readBytes;
        }

        std::int64_t readPageUntil(VorbisStateBlob& state, PageToken& page, std::int64_t limit)
        {
            if (limit > 0)
                limit += readSigned64(state, kCurrentOffsetLowOffset);

            while (true)
            {
                if (limit > 0 && readSigned64(state, kCurrentOffsetLowOffset) >= limit)
                    return -1;

                const int pageOffset = takePageFromSyncBuffer(state, page);
                if (pageOffset < 0)
                {
                    const std::int64_t currentOffset = readSigned64(state, kCurrentOffsetLowOffset);
                    writeSigned64(state, kCurrentOffsetLowOffset, currentOffset - pageOffset);
                    continue;
                }
                if (pageOffset > 0)
                {
                    const std::int64_t currentOffset = readSigned64(state, kCurrentOffsetLowOffset);
                    writeSigned64(state, kCurrentOffsetLowOffset, currentOffset + pageOffset);
                    return currentOffset;
                }

                if (limit == 0)
                    return -1;

                const int refillResult = refillSyncBuffer(state);
                if (refillResult == 0)
                    return -2;
                if (refillResult < 0)
                    return kOpenCoreClosedError;
            }
        }

        void seekInputAndResetSync(VorbisStateBlob& state, std::int64_t position)
        {
            void* inputOwner = readPointer(state, kInputOwnerOffset);
            if (inputOwner)
            {
                auto callback = reinterpret_cast<SeekCallback>(static_cast<std::uintptr_t>(readField<std::uint32_t>(state, kCallbackSeekOffset)));
                const OffsetPair split = splitOffset(position);
                callback(inputOwner, static_cast<int>(split.low), split.high, 0);
                writeSigned64(state, kCurrentOffsetLowOffset, position);
                resetSyncReadPosition(state);
            }
        }

        std::int64_t findPageBeforePosition(VorbisStateBlob& state, PageToken& page)
        {
            // page before the original target is found. Once scanning reaches
            // the target, retail immediately seeks back to the saved page and
            // re-reads it with an 8500-byte limit, then returns that saved raw
            // offset. Do not restart the outer probe loop after this point.
            std::int64_t foundOffset = -1;
            std::int64_t probe = readSigned64(state, kCurrentOffsetLowOffset);
            const std::int64_t target = probe;

            while (true)
            {
                probe -= kReadBlockSize;
                if (probe < 0)
                    probe = 0;
                seekInputAndResetSync(state, probe);

                if (readSigned64(state, kCurrentOffsetLowOffset) <= target)
                {
                    while (true)
                    {
                        const std::int64_t remaining = target - readSigned64(state, kCurrentOffsetLowOffset);
                        const std::int64_t pageOffset = readPageUntil(state, page, remaining);
                        if (pageOffset == kOpenCoreClosedError)
                            return kOpenCoreClosedError;
                        if (pageOffset >= 0)
                        {
                            foundOffset = pageOffset;
                            if (readSigned64(state, kCurrentOffsetLowOffset) < target)
                                continue;
                        }
                        break;
                    }
                }

                if (foundOffset != -1)
                {
                    seekInputAndResetSync(state, foundOffset);
                    const std::int64_t pageOffset = readPageUntil(state, page, kReadBlockSize);
                    return pageOffset >= 0 ? foundOffset : kPageSearchError;
                }
            }
        }

        int buildSeekTableRecursive(
            VorbisStateBlob& state,
            std::int64_t beginOffset,
            std::int64_t searchedOffset,
            std::int64_t endOffset,
            int serialNumber,
            int depth)
        {
            const std::int64_t originalEndOffset = endOffset;
            std::int64_t bisectOffset = endOffset;
            std::int64_t nextPageOffset = endOffset;
            PageToken page{};

            if (searchedOffset < endOffset)
            {
                while (true)
                {
                    const std::int64_t probe = (bisectOffset - searchedOffset >= kReadBlockSize)
                        ? (searchedOffset + bisectOffset) / 2
                        : searchedOffset;
                    seekInputAndResetSync(state, probe);

                    const std::int64_t pageOffset = readPageUntil(state, page, -1);
                    if (pageOffset == kOpenCoreClosedError)
                        return kOpenCoreClosedError;

                    if (pageOffset >= 0 && pageSerialNumber(page) == serialNumber)
                    {
                        searchedOffset = readSigned64(state, kCurrentOffsetLowOffset);
                    }
                    else
                    {
                        bisectOffset = probe;
                        if (pageOffset >= 0)
                            nextPageOffset = pageOffset;
                    }

                    if (searchedOffset >= bisectOffset)
                        break;
                }
            }

            seekInputAndResetSync(state, nextPageOffset);
            const std::int64_t result = readPageUntil(state, page, -1);
            if (result == kOpenCoreClosedError)
                return kOpenCoreClosedError;

            if (searchedOffset >= originalEndOffset || result < 0)
            {
                const int linkCount = depth + 1;
                writeField<int>(state, kLinkCountOffset, linkCount);
                writePointer(state, kLinkOffsetListOffset, std::malloc(8 * static_cast<std::size_t>(linkCount + 1)));
                writePointer(state, kSerialListOffset, std::malloc(4 * static_cast<std::size_t>(linkCount)));

                auto* offsets = static_cast<unsigned char*>(readPointer(state, kLinkOffsetListOffset));
                std::memcpy(offsets + 8 * static_cast<std::size_t>(linkCount), &searchedOffset, sizeof(searchedOffset));
            }
            else
            {
                const int nextSerial = pageSerialNumber(page);
                if (buildSeekTableRecursive(
                        state,
                        nextPageOffset,
                        readSigned64(state, kCurrentOffsetLowOffset),
                        originalEndOffset,
                        nextSerial,
                        depth + 1) == kOpenCoreClosedError)
                    return kOpenCoreClosedError;
            }

            auto* offsets = static_cast<unsigned char*>(readPointer(state, kLinkOffsetListOffset));
            auto* serials = static_cast<unsigned char*>(readPointer(state, kSerialListOffset));
            std::memcpy(offsets + 8 * static_cast<std::size_t>(depth), &beginOffset, sizeof(beginOffset));
            std::memcpy(serials + 4 * static_cast<std::size_t>(depth), &serialNumber, sizeof(serialNumber));
            return 0;
        }

        int parseInitialHeaders(VorbisStateBlob& state, void* infoRecord, void* commentRecord, int* serialOut, PageToken* suppliedPage)
        {
            PageToken firstPage{};
            PageToken* page = suppliedPage;
            if (!page)
            {
                const std::int64_t result = readPageUntil(state, firstPage, kReadBlockSize);
                if (result == kOpenCoreClosedError)
                    return kOpenCoreClosedError;
                if (result < 0)
                    return kBadHeaderPageError;
                page = &firstPage;
            }

            const int serialNumber = pageSerialNumber(*page);
            initPacketStream(state, serialNumber);
            if (serialOut)
                *serialOut = readField<int>(state, kPageSerialMirrorOffset);

            writeField<int>(state, kReadyStateOffset, 3);
            clearInfoRecord(infoRecord);
            clearCommentRecord(commentRecord);

            int parsedCount = 0;
            while (true)
            {
                feedPageToPacketStream(state, *page);
                if (parsedCount >= 3)
                    return 0;

                while (true)
                {
                    PacketToken packet{};
                    const int packetResult = takePacketFromStream(state, packet);
                    if (packetResult == 0)
                        break;
                    if (packetResult == -1)
                    {
                        writeField<int>(state, kReadyStateOffset, 2);
                        return kBadHeaderPacketError;
                    }

                    const int parseResult = parseSetupPacket(infoRecord, commentRecord, packet);
                    if (parseResult)
                    {
                        writeField<int>(state, kReadyStateOffset, 2);
                        return parseResult;
                    }
                    ++parsedCount;
                    if (parsedCount >= 3)
                        return 0;
                }

                if (parsedCount >= 3)
                    return 0;

                const std::int64_t readResult = readPageUntil(state, *page, kReadBlockSize);
                if (readResult < 0)
                {
                    writeField<int>(state, kReadyStateOffset, 2);
                    return kBadHeaderPacketError;
                }
            }
        }

        int buildSeekableLinkTables(VorbisStateBlob& state)
        {
            if (readField<int>(state, kReadyStateOffset) < 2)
                writeField<int>(state, kReadyStateOffset, 2);

            if (!readField<int>(state, kSeekableFlagOffset))
                return 0;

            const std::int64_t savedOffset = readSigned64(state, kCurrentOffsetLowOffset);
            void* inputOwner = readPointer(state, kInputOwnerOffset);
            auto seekCallback = reinterpret_cast<SeekCallback>(static_cast<std::uintptr_t>(readField<std::uint32_t>(state, kCallbackSeekOffset)));
            auto tellCallback = reinterpret_cast<TellCallback>(static_cast<std::uintptr_t>(readField<std::uint32_t>(state, kCallbackTellOffset)));

            seekCallback(inputOwner, 0, 0, 2);
            const int endLow = tellCallback(inputOwner);
            writeSigned64(state, kEndOffsetLowOffset, endLow);
            writeSigned64(state, kCurrentOffsetLowOffset, endLow);

            PageToken page{};
            const std::int64_t lastPageOffset = findPageBeforePosition(state, page);
            if (lastPageOffset < 0)
                return static_cast<int>(lastPageOffset);

            const int firstSerial = readField<int>(state, kCurrentSerialOffset);
            const int lastSerial = pageSerialNumber(page);
            const std::int64_t afterLastPage = readSigned64(state, kCurrentOffsetLowOffset);
            if (lastSerial == firstSerial)
            {
                if (buildSeekTableRecursive(state, 0, lastPageOffset, afterLastPage, firstSerial, 0))
                    return kOpenCoreClosedError;
            }
            else if (buildSeekTableRecursive(state, 0, 0, afterLastPage, firstSerial, 0) < 0)
            {
                return kOpenCoreClosedError;
            }

            rebuildPerLinkPcmLengths(state, savedOffset);
            return SeekRawOffset(state, 0);
        }

        int initializeVorbisFileState(
            void* inputOwner,
            VorbisStateBlob& state,
            const void* initialData,
            unsigned int initialSize,
            ReadCallback readCallback,
            SeekCallback seekCallback,
            CloseCallback closeCallback,
            TellCallback tellCallback)
        {
            int initialTell = -1;
            if (inputOwner && tellCallback)
                initialTell = tellCallback(inputOwner);

            std::memset(state.bytes, 0, kStateSize);
            writePointer(state, kInputOwnerOffset, inputOwner);
            writeField<std::uint32_t>(state, kCallbackReadDataOffset, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(readCallback)));
            writeField<std::uint32_t>(state, kCallbackSeekOffset, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(seekCallback)));
            writeField<std::uint32_t>(state, kCallbackCloseOffset, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(closeCallback)));
            writeField<std::uint32_t>(state, kCallbackTellOffset, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tellCallback)));

            resetSyncBuffer(state);
            if (initialData && initialSize)
            {
                unsigned char* buffer = reserveSyncBuffer(state, static_cast<int>(initialSize));
                std::memcpy(buffer, initialData, initialSize);
                markSyncBytesWritten(state, static_cast<int>(initialSize));
            }

            if (initialTell != -1)
                writeField<int>(state, kSeekableFlagOffset, 1);

            writeField<int>(state, kLinkCountOffset, 1);
            writePointer(state, kInfoListOffset, std::calloc(1, 0x20));
            writePointer(state, kCommentListOffset, std::calloc(1, 0x10));
            initializePacketStreamResources(state, -1);

            int serialNumber = 0;
            const int parseResult = parseInitialHeaders(
                state,
                readPointer(state, kInfoListOffset),
                readPointer(state, kCommentListOffset),
                &serialNumber,
                nullptr);
            if (parseResult >= 0)
            {
                if (readField<int>(state, kReadyStateOffset) < 1)
                    writeField<int>(state, kReadyStateOffset, 1);
                return parseResult;
            }

            writePointer(state, kInputOwnerOffset, nullptr);
            CleanupVorbisState(state);
            return parseResult;
        }

        int openAndSetupVorbisState(
            void* inputOwner,
            VorbisStateBlob& state,
            const void* initialData,
            unsigned int initialSize,
            ReadCallback readCallback,
            SeekCallback seekCallback,
            CloseCallback closeCallback,
            TellCallback tellCallback)
        {
            const int initResult = initializeVorbisFileState(
                inputOwner,
                state,
                initialData,
                initialSize,
                readCallback,
                seekCallback,
                closeCallback,
                tellCallback);
            if (initResult)
                return initResult;
            return buildSeekableLinkTables(state);
        }

        int rebuildPerLinkPcmLengths(VorbisStateBlob& state, std::int64_t restoreOffset)
        {
            const int linkCount = readField<int>(state, kLinkCountOffset);
            writePointer(state, kInfoListOffset, std::realloc(readPointer(state, kInfoListOffset), 32 * static_cast<std::size_t>(linkCount)));
            writePointer(state, kCommentListOffset, std::realloc(readPointer(state, kCommentListOffset), 16 * static_cast<std::size_t>(linkCount)));
            writePointer(state, kLinkDataOffsetListOffset, std::malloc(8 * static_cast<std::size_t>(linkCount)));
            writePointer(state, kSampleLengthListOffset, std::malloc(16 * static_cast<std::size_t>(linkCount)));

            auto* infoList = static_cast<unsigned char*>(readPointer(state, kInfoListOffset));
            auto* commentList = static_cast<unsigned char*>(readPointer(state, kCommentListOffset));
            auto* linkOffsets = static_cast<unsigned char*>(readPointer(state, kLinkOffsetListOffset));
            auto* dataOffsets = static_cast<unsigned char*>(readPointer(state, kLinkDataOffsetListOffset));
            auto* serials = static_cast<unsigned char*>(readPointer(state, kSerialListOffset));
            auto* sampleLengths = static_cast<unsigned char*>(readPointer(state, kSampleLengthListOffset));

            for (int index = 0; index < linkCount; ++index)
            {
                std::int64_t dataOffset = restoreOffset;
                if (index != 0)
                {
                    std::int64_t linkOffset = 0;
                    std::memcpy(&linkOffset, linkOffsets + 8 * static_cast<std::size_t>(index), sizeof(linkOffset));
                    seekInputAndResetSync(state, linkOffset);
                    if (parseInitialHeaders(
                            state,
                            infoList + 32 * static_cast<std::size_t>(index),
                            commentList + 16 * static_cast<std::size_t>(index),
                            nullptr,
                            nullptr) < 0)
                    {
                        dataOffset = -1;
                    }
                    else
                    {
                        dataOffset = readSigned64(state, kCurrentOffsetLowOffset);
                    }
                }
                else
                {
                    seekInputAndResetSync(state, restoreOffset);
                }
                std::memcpy(dataOffsets + 8 * static_cast<std::size_t>(index), &dataOffset, sizeof(dataOffset));

                if (dataOffset != -1)
                {
                    std::int64_t initialGranule = 0;
                    int previousBlockSize = -1;
                    int serialNumber = 0;
                    std::memcpy(&serialNumber, serials + 4 * static_cast<std::size_t>(index), sizeof(serialNumber));
                    initPacketStream(state, serialNumber);

                    bool foundInitialGranule = false;
                    while (!foundInitialGranule)
                    {
                        PageToken page{};
                        const std::int64_t pageOffset = readPageUntil(state, page, -1);
                        if (pageOffset < 0 || pageSerialNumber(page) != serialNumber)
                            break;

                        feedPageToPacketStream(state, page);
                        while (true)
                        {
                            PacketToken packet{};
                            const int packetResult = takePacketFromStream(state, packet);
                            if (packetResult == 0)
                                break;
                            if (packetResult > 0)
                            {
                                const int blockSize = packetBlockSize(infoList + 32 * static_cast<std::size_t>(index), packet);
                                if (previousBlockSize != -1)
                                    initialGranule += (blockSize + previousBlockSize) >> 2;
                                previousBlockSize = blockSize;
                            }
                        }

                        const std::int64_t pageGranule = pageGranulePosition(page);
                        if (pageGranule != -1)
                        {
                            initialGranule = pageGranule - initialGranule;
                            foundInitialGranule = true;
                        }
                    }
                    if (initialGranule < 0)
                        initialGranule = 0;
                    std::memcpy(sampleLengths + 16 * static_cast<std::size_t>(index), &initialGranule, sizeof(initialGranule));
                }

                std::int64_t nextLinkOffset = 0;
                std::memcpy(&nextLinkOffset, linkOffsets + 8 * static_cast<std::size_t>(index + 1), sizeof(nextLinkOffset));
                seekInputAndResetSync(state, nextLinkOffset);

                std::int64_t finalGranule = -1;
                while (true)
                {
                    PageToken page{};
                    const std::int64_t pageOffset = findPageBeforePosition(state, page);
                    if (pageOffset < 0)
                    {
                        releaseInfoRecord(infoList + 32 * static_cast<std::size_t>(index));
                        releaseCommentRecord(commentList + 16 * static_cast<std::size_t>(index));
                        break;
                    }

                    finalGranule = pageGranulePosition(page);
                    if (finalGranule != -1)
                        break;
                    writeSigned64(state, kCurrentOffsetLowOffset, pageOffset);
                }

                if (finalGranule != -1)
                {
                    std::int64_t initialGranule = 0;
                    std::memcpy(&initialGranule, sampleLengths + 16 * static_cast<std::size_t>(index), sizeof(initialGranule));
                    const std::int64_t pcmLength = finalGranule - initialGranule;
                    std::memcpy(sampleLengths + 16 * static_cast<std::size_t>(index) + 8, &pcmLength, sizeof(pcmLength));
                }
            }
            return linkCount;
        }

        void clearDecodeMachine(VorbisStateBlob& state)
        {
            releasePcmQueueState(state);
            writeField<int>(state, 0x68, 0);
            writeField<int>(state, 0x6C, 0);
            writeField<int>(state, 0x70, 0);
            writeField<int>(state, 0x74, 0);
            writeField<int>(state, kReadyStateOffset, 2);
        }

        const unsigned char* infoRecordForLink(VorbisStateBlob& state, int linkIndex)
        {
            void* infoList = readPointer(state, kInfoListOffset);
            const int seekable = readField<int>(state, kSeekableFlagOffset);
            if (!seekable)
                return static_cast<const unsigned char*>(infoList);

            const int linkCount = readField<int>(state, kLinkCountOffset);
            if (linkIndex < 0)
            {
                const int ready = readField<int>(state, kReadyStateOffset);
                if (ready >= 3)
                    return static_cast<const unsigned char*>(infoList) + 32 * readField<int>(state, kCurrentLinkOffset);
                return static_cast<const unsigned char*>(infoList);
            }

            if (linkIndex < linkCount)
                return static_cast<const unsigned char*>(infoList) + 32 * linkIndex;
            return nullptr;
        }

        int nativePcmEndianFlag()
        {
            return 0;
        }

        int clampSigned8(float value)
        {
            // control word; C++ integer casts truncate toward zero and are not 1:1.
            int sample = static_cast<int>(std::lrint(static_cast<double>(value) * 128.0));
            if (sample > 127)
                sample = 127;
            else if (sample < -128)
                sample = -128;
            return sample;
        }

        int clampSigned16(float value)
        {
            // Use the same rounded integer-conversion behavior as the 16-bit conversion paths.
            int sample = static_cast<int>(std::lrint(static_cast<double>(value) * 32768.0));
            if (sample > 0x7FFF)
                sample = 0x7FFF;
            else if (sample < -32768)
                sample = -32768;
            return sample;
        }


    int cleanupVorbisState(VorbisStateBlob& state, bool closeInputOwner)
    {
        releasePcmQueueState(state);
        releaseLinkArrays(state);
        releasePointerField(state, kLinkDataOffsetListOffset);
        releasePointerField(state, kSampleLengthListOffset);
        releasePointerField(state, kSerialListOffset);
        releasePointerField(state, kLinkOffsetListOffset);
        releasePacketStreamResources(state);
        releasePointerField(state, kSyncBufferOffset);

        void* inputOwner = readPointer(state, kInputOwnerOffset);
        auto closeCallback = reinterpret_cast<CloseCallback>(static_cast<std::uintptr_t>(readField<std::uint32_t>(state, kCallbackCloseOffset)));
        if (closeInputOwner && inputOwner && closeCallback)
            closeCallback(inputOwner);

        std::memset(state.bytes, 0, kStateSize);
        return 0;
    }

    int CleanupVorbisState(VorbisStateBlob& state)
    {
        return cleanupVorbisState(state, true);
    }

    int CleanupVorbisStateWithoutClosingInput(VorbisStateBlob& state)
    {
        return cleanupVorbisState(state, false);
    }

    int OpenVorbisFileFromFileHandle(std::FILE* file, VorbisStateBlob& state, const void* initialData, unsigned int initialSize)
    {
        return openAndSetupVorbisState(
            file,
            state,
            initialData,
            initialSize,
            readFileCallback,
            seekFileCallback,
            closeFileCallback,
            tellFileCallback);
    }

    int TotalPcmSamples(VorbisStateBlob& state, int linkIndex)
    {
        const int ready = readField<int>(state, kReadyStateOffset);
        const int seekable = readField<int>(state, kSeekableFlagOffset);
        const int linkCount = readField<int>(state, kLinkCountOffset);
        if (ready < 2 || !seekable || linkIndex >= linkCount)
            return kNotReadyError;

        void* sampleLengths = readPointer(state, kSampleLengthListOffset);

        if (linkIndex >= 0)
        {
            std::int64_t value = 0;
            std::memcpy(&value, static_cast<unsigned char*>(sampleLengths) + 16 * linkIndex + 8, sizeof(value));
            return static_cast<int>(value);
        }

        int total = 0;
        for (int index = 0; index < linkCount; ++index)
        {
            const int value = TotalPcmSamples(state, index);
            if (value > 0)
                total += value;
        }
        return total;
    }

    int SeekRawOffset(VorbisStateBlob& state, std::int64_t rawOffset)
    {
        if (readField<int>(state, kReadyStateOffset) < 2)
            return kNotReadyError;
        if (!readField<int>(state, kSeekableFlagOffset))
            return kSeekTargetError;
        if (rawOffset < 0 || rawOffset > readSigned64(state, kEndOffsetLowOffset))
            return kNotReadyError;

        writeSigned64(state, kPreviousPacketOffsetLowOffset, -1);
        clearDecodeMachine(state);
        seekInputAndResetSync(state, rawOffset);

        int previousBlockSize = 0;
        std::int64_t accumulatedBlocks = 0;
        VorbisStateBlob workState{};
        initializePacketStreamResources(workState, -1);
        int currentBlockSize = 0;
        int pageBegins = 0;
        std::int64_t granulePosition = -1;

        while (true)
        {
            PacketToken packet{};
            while (readField<int>(state, kReadyStateOffset) != 3 || takePacketFromStream(workState, packet) <= 0)
            {
                if (previousBlockSize != 0)
                {
                    writeSigned64(state, kPreviousPacketOffsetLowOffset, -1);
                    releasePacketStreamResources(workState);
                    return 0;
                }

                PageToken page{};
                const std::int64_t pageOffset = readPageUntil(state, page, -1);
                if (pageOffset < 0)
                {
                    writeSigned64(state, kPreviousPacketOffsetLowOffset, TotalPcmSamples(state, -1));
                    releasePacketStreamResources(workState);
                    return 0;
                }

                if (readField<int>(state, kReadyStateOffset) == 3 &&
                    readField<int>(state, kCurrentSerialOffset) != pageSerialNumber(page))
                {
                    clearDecodeMachine(state);
                    releasePacketStreamResources(workState);
                }

                if (readField<int>(state, kReadyStateOffset) < 3)
                {
                    const int serialNumber = pageSerialNumber(page);
                    writeField<int>(state, kCurrentSerialOffset, serialNumber);

                    const int linkCount = readField<int>(state, kLinkCountOffset);
                    auto* serials = static_cast<unsigned char*>(readPointer(state, kSerialListOffset));
                    int linkIndex = 0;
                    while (linkIndex < linkCount)
                    {
                        int candidateSerial = 0;
                        std::memcpy(&candidateSerial, serials + 4 * static_cast<std::size_t>(linkIndex), sizeof(candidateSerial));
                        if (candidateSerial == serialNumber)
                            break;
                        ++linkIndex;
                    }
                    if (linkIndex == linkCount)
                    {
                        writeSigned64(state, kPreviousPacketOffsetLowOffset, -1);
                        releasePacketStreamResources(workState);
                        clearDecodeMachine(state);
                        return -137;
                    }

                    writeField<int>(state, kCurrentLinkOffset, linkIndex);
                    initPacketStream(state, serialNumber);
                    initPacketStream(workState, serialNumber);
                    writeField<int>(state, kReadyStateOffset, 3);
                }

                feedPageToPacketStream(state, page);
                feedPageToPacketStream(workState, page);
                pageBegins = pageEndsStream(page);
            }

            auto* infoList = static_cast<unsigned char*>(readPointer(state, kInfoListOffset));
            const int linkIndex = readField<int>(state, kCurrentLinkOffset);
            unsigned char* info = infoList + 32 * static_cast<std::size_t>(linkIndex);
            if (readBlockPointer(info, kInfoSetupPointerOffset))
                currentBlockSize = packetBlockSize(info, packet);

            if (pageBegins)
            {
                takePacketFromStreamInternal(state, nullptr, true);
            }
            else if (previousBlockSize != 0)
            {
                accumulatedBlocks += (previousBlockSize + currentBlockSize) >> 2;
            }

            granulePosition = combineOffset(
                readBlockField<std::uint32_t>(packet.bytes, 0x10),
                readBlockField<std::int32_t>(packet.bytes, 0x14));
            if (granulePosition != -1)
                break;
            previousBlockSize = currentBlockSize;
        }

        const int currentLink = readField<int>(state, kCurrentLinkOffset);
        auto* sampleLengths = static_cast<unsigned char*>(readPointer(state, kSampleLengthListOffset));
        std::int64_t linkInitialGranule = 0;
        std::memcpy(&linkInitialGranule, sampleLengths + 16 * static_cast<std::size_t>(currentLink), sizeof(linkInitialGranule));
        std::int64_t pcmOffset = granulePosition - linkInitialGranule;
        if (pcmOffset < 0)
            pcmOffset = 0;
        for (int index = 0; index < currentLink; ++index)
        {
            std::int64_t linkLength = 0;
            std::memcpy(&linkLength, sampleLengths + 16 * static_cast<std::size_t>(index) + 8, sizeof(linkLength));
            pcmOffset += linkLength;
        }
        writeSigned64(state, kPreviousPacketOffsetLowOffset, pcmOffset - accumulatedBlocks);
        releasePacketStreamResources(workState);
        return 0;
    }

    int fetchAndProcessPacket(VorbisStateBlob& state, int readPages)
    {
        PageToken page{};
        while (true)
        {
            if (readField<int>(state, kReadyStateOffset) == 4)
            {
                const int packetResult = decodeNextPacketIntoPcmQueue(state);
                if (packetResult != 0)
                    return packetResult;
            }

            if (readField<int>(state, kReadyStateOffset) >= 2)
            {
                if (!readPages)
                    return 0;
                const std::int64_t pageOffset = readPageUntil(state, page, -1);
                if (pageOffset < 0)
                    return -2;

                if (readField<int>(state, kReadyStateOffset) == 4
                    && readField<int>(state, kCurrentSerialOffset) != pageSerialNumber(page))
                {
                    clearDecodeMachine(state);
                    if (!readField<int>(state, kSeekableFlagOffset))
                    {
                        releaseInfoRecord(readPointer(state, kInfoListOffset));
                        releaseCommentRecord(readPointer(state, kCommentListOffset));
                    }
                }
            }

            if (readField<int>(state, kReadyStateOffset) != 4)
            {
                if (readField<int>(state, kReadyStateOffset) < 3)
                {
                    if (readField<int>(state, kSeekableFlagOffset))
                    {
                        const int serial = pageSerialNumber(page);
                        writeField<int>(state, kCurrentSerialOffset, serial);
                        const int linkCount = readField<int>(state, kLinkCountOffset);
                        auto* serials = static_cast<unsigned char*>(readPointer(state, kSerialListOffset));
                        int linkIndex = 0;
                        for (; linkIndex < linkCount; ++linkIndex)
                        {
                            if (readBlockField<int>(serials, 4 * static_cast<std::size_t>(linkIndex)) == serial)
                                break;
                        }
                        if (linkIndex == linkCount)
                            return -137;
                        writeField<int>(state, kCurrentLinkOffset, linkIndex);
                        initPacketStream(state, serial);
                        writeField<int>(state, kReadyStateOffset, 3);
                    }
                    else
                    {
                        const int headerResult = parseInitialHeaders(
                            state,
                            readPointer(state, kInfoListOffset),
                            readPointer(state, kCommentListOffset),
                            reinterpret_cast<int*>(state.bytes + kCurrentSerialOffset),
                            &page);
                        if (headerResult)
                            return headerResult;
                        writeField<int>(state, kCurrentLinkOffset, readField<int>(state, kCurrentLinkOffset) + 1);
                    }
                }
                // and ignores the returned block-init value.
                (void)ensureVorbisDecodeReady(state);
            }
            feedPageToPacketStream(state, page);
        }
    }

    bool ReadPcmInfo(VorbisStateBlob& state, int linkIndex, VorbisPcmInfo& info)
    {
        info = {};
        const unsigned char* record = infoRecordForLink(state, linkIndex);
        if (!record)
            return false;

        int channels = 0;
        int sampleRate = 0;
        std::memcpy(&channels, record + 4, sizeof(channels));
        std::memcpy(&sampleRate, record + 8, sizeof(sampleRate));
        if (channels <= 0 || sampleRate <= 0)
            return false;

        info.channels = channels;
        info.sampleRate = sampleRate;
        return true;
    }


} } }


extern "C" {

static as1::thirdparty::xiph2003::VorbisStateBlob& as1_vorbis_state(OggVorbis_File* vf)
{
    return *reinterpret_cast<as1::thirdparty::xiph2003::VorbisStateBlob*>(vf->state);
}

int ov_clear(OggVorbis_File* vf)
{
    if (!vf)
        return -1;
    return as1::thirdparty::xiph2003::CleanupVorbisState(as1_vorbis_state(vf));
}

int ov_clear_noclose(OggVorbis_File* vf)
{
    if (!vf)
        return -1;
    return as1::thirdparty::xiph2003::CleanupVorbisStateWithoutClosingInput(as1_vorbis_state(vf));
}

int ov_open(FILE* f, OggVorbis_File* vf, const char* initial, long ibytes)
{
    if (!vf)
        return -1;
    return as1::thirdparty::xiph2003::OpenVorbisFileFromFileHandle(
        f,
        as1_vorbis_state(vf),
        initial,
        ibytes < 0 ? 0u : static_cast<unsigned int>(ibytes));
}

vorbis_info* ov_info(OggVorbis_File* vf, int link)
{
    static vorbis_info info;
    if (!vf)
        return nullptr;

    as1::thirdparty::xiph2003::VorbisPcmInfo pcm{};
    if (!as1::thirdparty::xiph2003::ReadPcmInfo(as1_vorbis_state(vf), link, pcm))
        return nullptr;

    std::memset(&info, 0, sizeof(info));
    info.channels = pcm.channels;
    info.rate = pcm.sampleRate;
    return &info;
}

ogg_int64_t ov_pcm_total(OggVorbis_File* vf, int link)
{
    if (!vf)
        return -1;
    return static_cast<ogg_int64_t>(as1::thirdparty::xiph2003::TotalPcmSamples(as1_vorbis_state(vf), link));
}

int ov_raw_seek(OggVorbis_File* vf, ogg_int64_t pos)
{
    if (!vf)
        return -1;
    return as1::thirdparty::xiph2003::SeekRawOffset(as1_vorbis_state(vf), static_cast<std::int64_t>(pos));
}

long ov_read(OggVorbis_File* vf, char* buffer, int length, int bigendianp, int word, int sgned, int* bitstream)
{
    using namespace as1::thirdparty::xiph2003;

    VorbisStateBlob& state = as1_vorbis_state(vf);
    const int hostEndian = nativePcmEndianFlag();
    if (readField<int>(state, kReadyStateOffset) < 2)
        return kNotReadyError;

    void* pcm = nullptr;
    int samples = 0;
    for (;;)
    {
        if (readField<int>(state, kReadyStateOffset) >= 3)
        {
            samples = queuedPcmSamples(pcmQueueState(state), &pcm);
            if (samples)
                break;
        }

        const int result = fetchAndProcessPacket(state, 1);
        if (result == -2)
            return 0;
        if (result <= 0)
            return result;
    }

    const unsigned char* info = infoRecordForLink(state, -1);
    const int channels = readBlockField<int>(info, kInfoChannelsOffset);
    const int frameBytes = word * channels;
    if (samples > length / frameBytes)
        samples = length / frameBytes;

    if (samples <= 0)
        return kNotReadyError;

    auto* output = reinterpret_cast<unsigned char*>(buffer);
    if (word == 1)
    {
        int out = 0;
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* channelPcm = static_cast<float*>(readBlockPointer(static_cast<unsigned char*>(pcm), 4 * static_cast<std::size_t>(channel)));
                int value = clampSigned8(channelPcm[sample]);
                output[out++] = static_cast<unsigned char>(value + (sgned ? 0 : -128));
            }
        }
    }
    else
    {
        const int unsignedOffset = sgned ? 0 : 0x8000;
        if (hostEndian == bigendianp)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* channelPcm = static_cast<float*>(readBlockPointer(static_cast<unsigned char*>(pcm), 4 * static_cast<std::size_t>(channel)));
                unsigned char* destination = output + 2 * channel;
                for (int sample = 0; sample < samples; ++sample)
                {
                    int value = clampSigned16(channelPcm[sample]);
                    const std::uint16_t converted = static_cast<std::uint16_t>(value + unsignedOffset);
                    std::memcpy(destination, &converted, sizeof(converted));
                    destination += 2 * channels;
                }
            }
        }
        else if (bigendianp)
        {
            int out = 0;
            for (int sample = 0; sample < samples; ++sample)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    auto* channelPcm = static_cast<float*>(readBlockPointer(static_cast<unsigned char*>(pcm), 4 * static_cast<std::size_t>(channel)));
                    int value = clampSigned16(channelPcm[sample]);
                    const std::uint16_t converted = static_cast<std::uint16_t>(value + unsignedOffset);
                    output[out++] = static_cast<unsigned char>((converted >> 8) & 0xFF);
                    output[out++] = static_cast<unsigned char>(converted & 0xFF);
                }
            }
        }
        else
        {
            int out = 0;
            for (int sample = 0; sample < samples; ++sample)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    auto* channelPcm = static_cast<float*>(readBlockPointer(static_cast<unsigned char*>(pcm), 4 * static_cast<std::size_t>(channel)));
                    int value = clampSigned16(channelPcm[sample]);
                    const std::uint16_t converted = static_cast<std::uint16_t>(value + unsignedOffset);
                    output[out++] = static_cast<unsigned char>(converted & 0xFF);
                    output[out++] = static_cast<unsigned char>((converted >> 8) & 0xFF);
                }
            }
        }
    }

    consumeQueuedPcmSamples(pcmQueueState(state), samples);
    writeSigned64(state, kPreviousPacketOffsetLowOffset, readSigned64(state, kPreviousPacketOffsetLowOffset) + samples);
    if (bitstream)
        *bitstream = readField<int>(state, kCurrentLinkOffset);
    return samples * frameBytes;
}


} // extern "C"
