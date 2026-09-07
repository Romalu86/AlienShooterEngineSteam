#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        void resetSyncBuffer(VorbisStateBlob& state)
        {
            std::memset(state.bytes + kSyncBufferOffset, 0, kSyncStateSize);
        }

        void clearSyncBufferedPage(VorbisStateBlob& state)
        {
            auto* sync = state.bytes + kSyncBufferOffset;
            writeBlockField<int>(sync, kSyncFillOffset, 0);
            writeBlockField<int>(sync, kSyncReturnedOffset, 0);
            writeBlockField<int>(sync, kSyncUnsyncedOffset, 0);
            writeBlockField<int>(sync, kSyncHeaderBytesOffset, 0);
            writeBlockField<int>(sync, kSyncBodyBytesOffset, 0);
        }

        unsigned char* reserveSyncBuffer(VorbisStateBlob& state, int bytes)
        {
            if (bytes <= 0)
                return nullptr;

            auto* sync = state.bytes + kSyncBufferOffset;
            unsigned char* data = static_cast<unsigned char*>(readBlockPointer(sync, kSyncDataOffset));
            int storage = readBlockField<int>(sync, kSyncStorageOffset);
            int fill = readBlockField<int>(sync, kSyncFillOffset);
            const int returned = readBlockField<int>(sync, kSyncReturnedOffset);

            if (returned)
            {
                fill -= returned;
                writeBlockField<int>(sync, kSyncFillOffset, fill);
                if (fill >= 0 && fill != 0 && data)
                    std::memmove(data, data + returned, static_cast<std::size_t>(fill));
                writeBlockField<int>(sync, kSyncReturnedOffset, 0);
            }

            if (bytes > storage - fill)
            {
                const int newStorage = fill + bytes + 4096;
                void* resized = data ? std::realloc(data, static_cast<std::size_t>(newStorage)) : std::malloc(static_cast<std::size_t>(newStorage));
                if (!resized)
                    return nullptr;
                data = static_cast<unsigned char*>(resized);
                writeBlockPointer(sync, kSyncDataOffset, data);
                writeBlockField<int>(sync, kSyncStorageOffset, newStorage);
            }
            return data + fill;
        }

        void markSyncBytesWritten(VorbisStateBlob& state, int bytes)
        {
            if (bytes <= 0)
                return;
            auto* sync = state.bytes + kSyncBufferOffset;
            const int fill = readBlockField<int>(sync, kSyncFillOffset);
            const int storage = readBlockField<int>(sync, kSyncStorageOffset);
            if (fill + bytes <= storage)
                writeBlockField<int>(sync, kSyncFillOffset, fill + bytes);
        }

        void resetSyncReadPosition(VorbisStateBlob& state)
        {
            clearSyncBufferedPage(state);
        }

        std::uint32_t oggCrcTableValue(unsigned int index)
        {
            std::uint32_t value = index << 24;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 0x80000000u) ? ((value << 1) ^ 0x04C11DB7u) : (value << 1);
            return value;
        }

        std::uint32_t updateOggChecksum(std::uint32_t checksum, const unsigned char* data, int size)
        {
            for (int index = 0; index < size; ++index)
                checksum = (checksum << 8) ^ oggCrcTableValue(((checksum >> 24) & 0xFFu) ^ data[index]);
            return checksum;
        }

        void writePageToken(PageToken& page, unsigned char* header, int headerBytes, unsigned char* body, int bodyBytes)
        {
            writeBlockPointer(page.bytes, 0x00, header);
            writeBlockField<int>(page.bytes, 0x04, headerBytes);
            writeBlockPointer(page.bytes, 0x08, body);
            writeBlockField<int>(page.bytes, 0x0C, bodyBytes);
        }

        unsigned char* pageHeaderPointer(const PageToken& page)
        {
            return static_cast<unsigned char*>(readBlockPointer(page.bytes, 0x00));
        }

        unsigned char* pageBodyPointer(const PageToken& page)
        {
            return static_cast<unsigned char*>(readBlockPointer(page.bytes, 0x08));
        }

        int pageHeaderSize(const PageToken& page)
        {
            return readBlockField<int>(page.bytes, 0x04);
        }

        int pageBodySize(const PageToken& page)
        {
            return readBlockField<int>(page.bytes, 0x0C);
        }

        int takePageFromSyncBuffer(VorbisStateBlob& state, PageToken& page)
        {
            auto* sync = state.bytes + kSyncBufferOffset;
            unsigned char* data = static_cast<unsigned char*>(readBlockPointer(sync, kSyncDataOffset));
            const int returned = readBlockField<int>(sync, kSyncReturnedOffset);
            if (!data)
                return 0;

            unsigned char* pageStart = data + returned;
            int available = readBlockField<int>(sync, kSyncFillOffset) - returned;
            int headerBytes = readBlockField<int>(sync, kSyncHeaderBytesOffset);
            int bodyBytes = readBlockField<int>(sync, kSyncBodyBytesOffset);
            auto syncFailure = [&]() -> int
            {
                writeBlockField<int>(sync, kSyncHeaderBytesOffset, 0);
                writeBlockField<int>(sync, kSyncBodyBytesOffset, 0);
                unsigned char* next = nullptr;
                if (available > 1)
                    next = static_cast<unsigned char*>(std::memchr(pageStart + 1, 'O', static_cast<std::size_t>(available - 1)));
                if (!next)
                    next = data + readBlockField<int>(sync, kSyncFillOffset);
                writeBlockField<int>(sync, kSyncReturnedOffset, static_cast<int>(next - data));
                return static_cast<int>(pageStart - next);
            };

            if (!headerBytes)
            {
                if (available < 27)
                    return 0;
                if (std::memcmp(pageStart, "OggS", 4) != 0)
                    return syncFailure();

                const int segmentCount = pageStart[26];
                if (available < segmentCount + 27)
                    return 0;

                bodyBytes = 0;
                for (int index = 0; index < segmentCount; ++index)
                    bodyBytes += pageStart[27 + index];
                headerBytes = segmentCount + 27;
                writeBlockField<int>(sync, kSyncHeaderBytesOffset, headerBytes);
                writeBlockField<int>(sync, kSyncBodyBytesOffset, bodyBytes);
            }

            if (bodyBytes + headerBytes > available)
                return 0;

            const std::uint32_t savedChecksum = readBlockField<std::uint32_t>(pageStart, 22);
            writeBlockField<std::uint32_t>(pageStart, 22, 0);
            std::uint32_t checksum = updateOggChecksum(0, pageStart, headerBytes);
            checksum = updateOggChecksum(checksum, pageStart + headerBytes, bodyBytes);
            writeBlockField<std::uint32_t>(pageStart, 22, checksum);
            if (savedChecksum != checksum)
            {
                writeBlockField<std::uint32_t>(pageStart, 22, savedChecksum);
                return syncFailure();
            }

            writePageToken(page, pageStart, headerBytes, pageStart + headerBytes, bodyBytes);
            writeBlockField<int>(sync, kSyncUnsyncedOffset, 0);
            writeBlockField<int>(sync, kSyncReturnedOffset, returned + headerBytes + bodyBytes);
            writeBlockField<int>(sync, kSyncHeaderBytesOffset, 0);
            writeBlockField<int>(sync, kSyncBodyBytesOffset, 0);
            return headerBytes + bodyBytes;

        }

        int pageHeaderVersion(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            return header ? header[4] : 0;
        }

        int pageIsContinued(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            return header ? (header[5] & 1) : 0;
        }

        int pageBeginsStream(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            return header ? (header[5] & 2) : 0;
        }

        int pageEndsStream(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            return header ? (header[5] & 4) : 0;
        }

        int pageSequenceNumber(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            if (!header)
                return 0;
            return static_cast<int>(header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24));
        }

        int pageSerialNumber(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            if (!header)
                return 0;
            return static_cast<int>(header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24));
        }

        std::int64_t pageGranulePosition(const PageToken& page)
        {
            const unsigned char* header = pageHeaderPointer(page);
            if (!header)
                return -1;
            std::uint64_t value = 0;
            for (int index = 7; index >= 0; --index)
                value = (value << 8) | header[6 + index];
            return static_cast<std::int64_t>(value);
        }

} } }
