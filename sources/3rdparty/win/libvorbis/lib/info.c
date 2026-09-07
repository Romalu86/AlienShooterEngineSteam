#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        void releaseCommentRecord(void* record)
        {
            if (!record)
                return;
            const int count = readBlockField<int>(static_cast<unsigned char*>(record), kCommentCountOffset);
            if (void* textList = readBlockPointer(static_cast<unsigned char*>(record), kCommentTextListOffset))
            {
                for (int index = 0; index < count; ++index)
                {
                    void* entry = readBlockPointer(static_cast<unsigned char*>(textList), 4 * static_cast<std::size_t>(index));
                    if (entry)
                        std::free(entry);
                }
                std::free(textList);
            }
            if (void* lengthList = readBlockPointer(static_cast<unsigned char*>(record), kCommentLengthListOffset))
                std::free(lengthList);
            if (void* vendor = readBlockPointer(static_cast<unsigned char*>(record), kCommentVendorOffset))
                std::free(vendor);
            std::memset(record, 0, 0x10);
        }

        void releaseSetupRecord(void* setup)
        {
            if (!setup)
                return;
            auto* bytes = static_cast<unsigned char*>(setup);
            const int modeCount = readBlockField<int>(bytes, kSetupModeCountOffset);
            for (int index = 0; index < modeCount; ++index)
            {
                if (void* pointer = readBlockPointer(bytes, kSetupModePointerTableOffset + 4 * static_cast<std::size_t>(index)))
                    std::free(pointer);
            }

            const int mappingCount = readBlockField<int>(bytes, kSetupMappingCountOffset);
            for (int index = 0; index < mappingCount; ++index)
            {
                const int type = readBlockField<int>(bytes, kSetupMappingTypeTableOffset + 4 * static_cast<std::size_t>(index));
                if (void* pointer = readBlockPointer(bytes, kSetupMappingPointerTableOffset + 4 * static_cast<std::size_t>(index)))
                    releaseMappingRecordByType(type, pointer);
            }

            const int floorCount = readBlockField<int>(bytes, kSetupFloorCountOffset);
            for (int index = 0; index < floorCount; ++index)
            {
                const int type = readBlockField<int>(bytes, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(index));
                if (void* pointer = readBlockPointer(bytes, kSetupFloorPointerTableOffset + 4 * static_cast<std::size_t>(index)))
                    releaseFloorRecordByType(type, pointer);
            }

            const int residueCount = readBlockField<int>(bytes, kSetupResidueCountOffset);
            for (int index = 0; index < residueCount; ++index)
            {
                const int type = readBlockField<int>(bytes, kSetupResidueTypeTableOffset + 4 * static_cast<std::size_t>(index));
                if (void* pointer = readBlockPointer(bytes, kSetupResiduePointerTableOffset + 4 * static_cast<std::size_t>(index)))
                    releaseResidueRecordByType(type, pointer);
            }

            const int bookCount = readBlockField<int>(bytes, kSetupBookCountOffset);
            auto* fullbooks = static_cast<unsigned char*>(readBlockPointer(bytes, kSetupFullbookArrayOffset));
            for (int index = 0; index < bookCount; ++index)
            {
                if (void* pointer = readBlockPointer(bytes, kSetupBookPointerTableOffset + 4 * static_cast<std::size_t>(index)))
                {
                    releaseCodebookRecord(pointer);
                    std::free(pointer);
                }
                if (fullbooks)
                    clearRuntimeCodebook(fullbooks + kRuntimeBookSize * static_cast<std::size_t>(index));
            }
            if (fullbooks)
                std::free(fullbooks);
            std::memset(setup, 0, 0xE78);
            std::free(setup);
        }

        void releaseInfoRecord(void* record)
        {
            if (!record)
                return;
            releaseSetupRecord(readBlockPointer(static_cast<unsigned char*>(record), kInfoSetupPointerOffset));
            std::memset(record, 0, 0x20);
        }

        void clearInfoRecord(void* record)
        {
            if (!record)
                return;
            releaseInfoRecord(record);
            std::memset(record, 0, 0x20);
            writeBlockPointer(static_cast<unsigned char*>(record), kInfoSetupPointerOffset, std::calloc(1, 0xE78));
        }

        void clearCommentRecord(void* record)
        {
            releaseCommentRecord(record);
        }

        int parseIdentificationPacket(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return kParserClosed;
            auto* info = static_cast<unsigned char*>(infoRecord);
            void* setup = readBlockPointer(info, kInfoSetupPointerOffset);
            if (!setup)
                return kParserClosed;

            const int version = readBits(cursor, 32);
            writeBlockField<int>(info, kInfoVersionOffset, version);
            if (version)
                return -134;

            writeBlockField<int>(info, kInfoChannelsOffset, readBits(cursor, 8));
            writeBlockField<int>(info, kInfoRateOffset, readBits(cursor, 32));
            writeBlockField<int>(info, kInfoBitrateUpperOffset, readBits(cursor, 32));
            writeBlockField<int>(info, kInfoBitrateNominalOffset, readBits(cursor, 32));
            writeBlockField<int>(info, kInfoBitrateLowerOffset, readBits(cursor, 32));

            auto* setupBytes = static_cast<unsigned char*>(setup);
            const int smallBlock = 1 << readBits(cursor, 4);
            const int largeBlock = 1 << readBits(cursor, 4);
            writeBlockField<int>(setupBytes, kSetupBlock0Offset, smallBlock);
            writeBlockField<int>(setupBytes, kSetupBlock1Offset, largeBlock);

            if (readBlockField<int>(info, kInfoRateOffset) >= 1
                && readBlockField<int>(info, kInfoChannelsOffset) >= 1
                && smallBlock >= 8
                && largeBlock >= smallBlock
                && readBits(cursor, 1) == 1)
            {
                return 0;
            }

            releaseInfoRecord(infoRecord);
            return kBadHeaderPacketError;
        }

        int parseCommentPacket(void* commentRecord, BitCursor& cursor)
        {
            if (!commentRecord)
                return kBadHeaderPacketError;
            auto* comment = static_cast<unsigned char*>(commentRecord);

            const int vendorLength = readBits(cursor, 32);
            if (vendorLength < 0)
            {
                releaseCommentRecord(commentRecord);
                return kBadHeaderPacketError;
            }
            void* vendor = std::calloc(static_cast<std::size_t>(vendorLength) + 1u, 1);
            writeBlockPointer(comment, kCommentVendorOffset, vendor);
            readBytesFromPacket(cursor, vendor, vendorLength);

            const int userCommentCount = readBits(cursor, 32);
            writeBlockField<int>(comment, kCommentCountOffset, userCommentCount);
            if (userCommentCount < 0)
            {
                releaseCommentRecord(commentRecord);
                return kBadHeaderPacketError;
            }

            void* textList = std::calloc(static_cast<std::size_t>(userCommentCount) + 1u, sizeof(std::uint32_t));
            void* lengthList = std::calloc(static_cast<std::size_t>(userCommentCount) + 1u, sizeof(int));
            writeBlockPointer(comment, kCommentTextListOffset, textList);
            writeBlockPointer(comment, kCommentLengthListOffset, lengthList);

            for (int index = 0; index < userCommentCount; ++index)
            {
                const int length = readBits(cursor, 32);
                if (length < 0)
                {
                    releaseCommentRecord(commentRecord);
                    return kBadHeaderPacketError;
                }
                writeBlockField<int>(static_cast<unsigned char*>(lengthList), 4 * static_cast<std::size_t>(index), length);
                void* text = std::calloc(static_cast<std::size_t>(length) + 1u, 1);
                writeBlockPointer(static_cast<unsigned char*>(textList), 4 * static_cast<std::size_t>(index), text);
                readBytesFromPacket(cursor, text, length);
            }

            if (readBits(cursor, 1) != 1)
            {
                releaseCommentRecord(commentRecord);
                return kBadHeaderPacketError;
            }
            return 0;
        }

        int parseModeSetupRecords(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return kParserClosed;
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            if (!setup)
                return kParserClosed;

            const int modeCount = readBits(cursor, 6) + 1;
            writeBlockField<int>(setup, kSetupModeCountOffset, modeCount);
            for (int index = 0; index < modeCount; ++index)
            {
                auto* mode = static_cast<unsigned char*>(std::calloc(1, 0x10));
                writeBlockPointer(setup, kSetupModePointerTableOffset + 4 * static_cast<std::size_t>(index), mode);
                if (!mode)
                    return kBadHeaderPacketError;
                writeBlockField<int>(mode, 0x00, readBits(cursor, 1));
                writeBlockField<int>(mode, 0x04, readBits(cursor, 16));
                writeBlockField<int>(mode, 0x08, readBits(cursor, 16));
                writeBlockField<int>(mode, 0x0C, readBits(cursor, 8));
                if (readBlockField<int>(mode, 0x04) >= 1
                    || readBlockField<int>(mode, 0x08) >= 1
                    || readBlockField<int>(mode, 0x0C) >= readBlockField<int>(setup, kSetupMappingCountOffset))
                {
                    return kBadHeaderPacketError;
                }
            }
            return readBits(cursor, 1) == 1 ? 0 : kBadHeaderPacketError;
        }

        int parseSetupPacketBody(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return kParserClosed;
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            if (!setup)
                return kParserClosed;

            auto fail = [&]() -> int
            {
                releaseInfoRecord(infoRecord);
                return kBadHeaderPacketError;
            };

            const int bookCount = readBits(cursor, 8) + 1;
            writeBlockField<int>(setup, kSetupBookCountOffset, bookCount);
            for (int index = 0; index < bookCount; ++index)
            {
                void* book = std::calloc(1, 0x34);
                writeBlockPointer(setup, kSetupBookPointerTableOffset + 4 * static_cast<std::size_t>(index), book);
                if (parseCodebookRecordBoundary(cursor, book))
                    return fail();
            }

            const int transformCount = readBits(cursor, 6) + 1;
            for (int index = 0; index < transformCount; ++index)
            {
                const int transformType = readBits(cursor, 16);
                if (transformType < 0 || transformType >= 1)
                    return fail();
            }

            const int floorCount = readBits(cursor, 6) + 1;
            writeBlockField<int>(setup, kSetupFloorCountOffset, floorCount);
            for (int index = 0; index < floorCount; ++index)
            {
                const int floorType = readBits(cursor, 16);
                writeBlockField<int>(setup, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(index), floorType);
                if (floorType < 0 || floorType >= 2)
                    return fail();
                void* floorRecord = parseFloorSetupRecordByType(floorType, infoRecord, cursor);
                writeBlockPointer(setup, kSetupFloorPointerTableOffset + 4 * static_cast<std::size_t>(index), floorRecord);
                if (!floorRecord)
                    return fail();
            }

            const int residueCount = readBits(cursor, 6) + 1;
            writeBlockField<int>(setup, kSetupResidueCountOffset, residueCount);
            for (int index = 0; index < residueCount; ++index)
            {
                const int residueType = readBits(cursor, 16);
                writeBlockField<int>(setup, kSetupResidueTypeTableOffset + 4 * static_cast<std::size_t>(index), residueType);
                if (residueType < 0 || residueType >= 3)
                    return fail();
                void* residueRecord = parseResidueSetupRecordByType(residueType, infoRecord, cursor);
                writeBlockPointer(setup, kSetupResiduePointerTableOffset + 4 * static_cast<std::size_t>(index), residueRecord);
                if (!residueRecord)
                    return fail();
            }

            const int mappingCount = readBits(cursor, 6) + 1;
            writeBlockField<int>(setup, kSetupMappingCountOffset, mappingCount);
            for (int index = 0; index < mappingCount; ++index)
            {
                const int mappingType = readBits(cursor, 16);
                writeBlockField<int>(setup, kSetupMappingTypeTableOffset + 4 * static_cast<std::size_t>(index), mappingType);
                if (mappingType < 0 || mappingType >= 1)
                    return fail();
                void* mappingRecord = parseMappingSetupRecordByType(mappingType, infoRecord, cursor);
                writeBlockPointer(setup, kSetupMappingPointerTableOffset + 4 * static_cast<std::size_t>(index), mappingRecord);
                if (!mappingRecord)
                    return fail();
            }

            const int modeResult = parseModeSetupRecords(infoRecord, cursor);
            if (modeResult)
                return fail();
            return 0;
        }

        int parseSetupPacket(void* infoRecord, void* commentRecord, const PacketToken& packet)
        {
            const void* packetData = readBlockPointer(packet.bytes, 0x00);
            const int packetBytes = readBlockField<int>(packet.bytes, 0x04);
            if (!packetData)
                return kBadHeaderPacketError;

            BitCursor cursor{};
            initializeBitCursor(cursor, packetData, packetBytes);
            const int packetType = readBits(cursor, 8);

            char marker[6]{};
            readBytesFromPacket(cursor, marker, 6);
            if (std::memcmp(marker, "vorbis", 6) != 0)
                return kBadHeaderPageError;

            if (packetType == 1)
            {
                const int beginsStream = readBlockField<int>(packet.bytes, 0x08);
                if (beginsStream && infoRecord && !readBlockField<int>(static_cast<unsigned char*>(infoRecord), kInfoRateOffset))
                    return parseIdentificationPacket(infoRecord, cursor);
            }
            else if (packetType == 3)
            {
                if (infoRecord && readBlockField<int>(static_cast<unsigned char*>(infoRecord), kInfoRateOffset))
                    return parseCommentPacket(commentRecord, cursor);
            }
            else if (packetType == 5)
            {
                if (infoRecord
                    && readBlockField<int>(static_cast<unsigned char*>(infoRecord), kInfoRateOffset)
                    && commentRecord
                    && readBlockPointer(static_cast<unsigned char*>(commentRecord), kCommentVendorOffset))
                {
                    return parseSetupPacketBody(infoRecord, cursor);
                }
            }
            return kBadHeaderPacketError;
        }

        int packetBlockSize(void* infoRecord, const PacketToken& packet)
        {
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));

            BitCursor cursor{};
            initializeBitCursor(cursor, readBlockPointer(packet.bytes, 0x00), readBlockField<int>(packet.bytes, 0x04));
            if (readBits(cursor, 1))
                return -135;

            int bits = 0;
            int modeCount = readBlockField<int>(setup, kSetupModeCountOffset);
            for (; modeCount > 1; ++bits)
                modeCount >>= 1;
            const int modeIndex = readBits(cursor, bits);
            if (modeIndex == -1)
                return -136;
            auto* modeRecord = static_cast<unsigned char*>(readBlockPointer(setup, kSetupModePointerTableOffset + 4 * static_cast<std::size_t>(modeIndex)));
            const int blockFlag = readBlockField<int>(modeRecord, 0);
            return readBlockField<int>(setup, 4 * static_cast<std::size_t>(blockFlag));
        }

} } }
