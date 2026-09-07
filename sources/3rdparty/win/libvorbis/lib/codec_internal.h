#pragma once

#include "3rdparty/win/libogg/include/ogg/ogg.h"
#include "3rdparty/win/libvorbis/include/vorbis/codec.h"
#include "3rdparty/win/libvorbis/include/vorbis/vorbisfile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>


namespace as1 { namespace thirdparty { namespace xiph2003
{
    struct VorbisPcmInfo
    {
        int channels = 0;
        int sampleRate = 0;
    };

    struct VorbisStateBlob
    {
        unsigned char bytes[0x2D0]{};
    };

                                                                                                          
#if defined(_WIN32) && defined(_M_IX86)
                                                                                             
                                                                                               
                                                                                                                  
#endif

    int CleanupVorbisState(VorbisStateBlob& state);
    int CleanupVorbisStateWithoutClosingInput(VorbisStateBlob& state);
    int OpenVorbisFileFromFileHandle(std::FILE* file, VorbisStateBlob& state, const void* initialData, unsigned int initialSize);
    int TotalPcmSamples(VorbisStateBlob& state, int linkIndex);
    int SeekRawOffset(VorbisStateBlob& state, std::int64_t rawOffset);
    bool ReadPcmInfo(VorbisStateBlob& state, int linkIndex, VorbisPcmInfo& info);
} } }

namespace as1 { namespace thirdparty { namespace xiph2003
{
        constexpr std::size_t kStateSize = 0x2D0;
        constexpr std::size_t kInputOwnerOffset = 0x00;
        constexpr std::size_t kSeekableFlagOffset = 0x04;
        constexpr std::size_t kCurrentOffsetLowOffset = 0x08;
        constexpr std::size_t kCurrentOffsetHighOffset = 0x0C;
        constexpr std::size_t kEndOffsetLowOffset = 0x10;
        constexpr std::size_t kEndOffsetHighOffset = 0x14;
        constexpr std::size_t kSyncBufferOffset = 0x18;
        constexpr std::size_t kSyncStateSize = 0x1C;
        constexpr std::size_t kLinkCountOffset = 0x34;
        constexpr std::size_t kLinkOffsetListOffset = 0x38;
        constexpr std::size_t kLinkDataOffsetListOffset = 0x3C;
        constexpr std::size_t kSerialListOffset = 0x40;
        constexpr std::size_t kSampleLengthListOffset = 0x44;
        constexpr std::size_t kInfoListOffset = 0x48;
        constexpr std::size_t kCommentListOffset = 0x4C;
        constexpr std::size_t kPreviousPacketOffsetLowOffset = 0x50;
        constexpr std::size_t kPreviousPacketOffsetHighOffset = 0x54;
        constexpr std::size_t kReadyStateOffset = 0x58;
        constexpr std::size_t kCurrentSerialOffset = 0x5C;
        constexpr std::size_t kCurrentLinkOffset = 0x60;
        constexpr std::size_t kPacketStreamOffset = 0x78;
        constexpr std::size_t kPacketStreamSize = 0x168;
        constexpr std::size_t kPageSerialMirrorOffset = 0x1C8;
        constexpr std::size_t kCallbackReadDataOffset = 0x2C0;
        constexpr std::size_t kCallbackSeekOffset = 0x2C4;
        constexpr std::size_t kCallbackCloseOffset = 0x2C8;
        constexpr std::size_t kCallbackTellOffset = 0x2CC;

        constexpr int kNotReadyError = -131;
        constexpr int kOpenCoreClosedError = -128;
        constexpr int kPageSearchError = -129;
        constexpr int kBadHeaderPageError = -132;
        constexpr int kBadHeaderPacketError = -133;
        constexpr int kSeekTargetError = -138;
        constexpr int kParserClosed = -128;
        constexpr int kReadBlockSize = 8500;

        constexpr std::size_t kInfoVersionOffset = 0x00;
        constexpr std::size_t kInfoChannelsOffset = 0x04;
        constexpr std::size_t kInfoRateOffset = 0x08;
        constexpr std::size_t kInfoBitrateUpperOffset = 0x0C;
        constexpr std::size_t kInfoBitrateNominalOffset = 0x10;
        constexpr std::size_t kInfoBitrateLowerOffset = 0x14;
        constexpr std::size_t kInfoSetupPointerOffset = 0x1C;

        constexpr std::size_t kCommentVendorOffset = 0x0C;
        constexpr std::size_t kCommentCountOffset = 0x08;
        constexpr std::size_t kCommentTextListOffset = 0x00;
        constexpr std::size_t kCommentLengthListOffset = 0x04;

        constexpr std::size_t kSetupBlock0Offset = 0x00;
        constexpr std::size_t kSetupBlock1Offset = 0x04;
        constexpr std::size_t kSetupModeCountOffset = 0x08;
        constexpr std::size_t kSetupMappingCountOffset = 0x0C;
        constexpr std::size_t kSetupFloorCountOffset = 0x10;
        constexpr std::size_t kSetupResidueCountOffset = 0x14;
        constexpr std::size_t kSetupBookCountOffset = 0x18;
        constexpr std::size_t kSetupPsyCountOffset = 0x1C;
        constexpr std::size_t kSetupModePointerTableOffset = 0x20;
        constexpr std::size_t kSetupMappingTypeTableOffset = 0x120;
        constexpr std::size_t kSetupMappingPointerTableOffset = 0x220;
        constexpr std::size_t kSetupFloorTypeTableOffset = 0x320;
        constexpr std::size_t kSetupFloorPointerTableOffset = 0x420;
        constexpr std::size_t kSetupResidueTypeTableOffset = 0x520;
        constexpr std::size_t kSetupResiduePointerTableOffset = 0x620;
        constexpr std::size_t kSetupBookPointerTableOffset = 0x720;
        constexpr std::size_t kSetupFullbookArrayOffset = 0xB20;
        constexpr std::size_t kSetupPsyPointerTableOffset = 0xB24;
        constexpr std::size_t kSetupPsyGlobalOffset = 0xB34;

        constexpr std::size_t kCodebookDimensionsOffset = 0x00;
        constexpr std::size_t kCodebookEntriesOffset = 0x04;
        constexpr std::size_t kCodebookLengthListOffset = 0x08;
        constexpr std::size_t kCodebookLookupTypeOffset = 0x0C;
        constexpr std::size_t kCodebookMinimumValueOffset = 0x10;
        constexpr std::size_t kCodebookDeltaValueOffset = 0x14;
        constexpr std::size_t kCodebookValueBitsOffset = 0x18;
        constexpr std::size_t kCodebookSequenceFlagOffset = 0x1C;
        constexpr std::size_t kCodebookQuantValueListOffset = 0x20;
        constexpr std::size_t kCodebookAuxiliaryAOffset = 0x24;
        constexpr std::size_t kCodebookAuxiliaryBOffset = 0x28;
        constexpr std::size_t kCodebookInitializedOffset = 0x30;

        constexpr std::size_t kRuntimeBookDimensionsOffset = 0x00;
        constexpr std::size_t kRuntimeBookEntriesOffset = 0x04;
        constexpr std::size_t kRuntimeBookUsedEntriesOffset = 0x08;
        constexpr std::size_t kRuntimeBookStaticOwnerOffset = 0x0C;
        constexpr std::size_t kRuntimeBookValueListOffset = 0x10;
        constexpr std::size_t kRuntimeBookCodeListOffset = 0x14;
        constexpr std::size_t kRuntimeBookDecodeIndexOffset = 0x18;
        constexpr std::size_t kRuntimeBookDecodeLengthsOffset = 0x1C;
        constexpr std::size_t kRuntimeBookFirstTableOffset = 0x20;
        constexpr std::size_t kRuntimeBookFirstTableBitsOffset = 0x24;
        constexpr std::size_t kRuntimeBookMaxLengthOffset = 0x28;
        constexpr std::size_t kRuntimeBookSize = 0x2C;

        struct BitCursor
        {
            int consumedBytes = 0;
            int bitOffset = 0;
            const unsigned char* bufferStart = nullptr;
            const unsigned char* cursor = nullptr;
            int bufferSize = 0;
        };

#if defined(_WIN32) && defined(_M_IX86)
                                                                                                              
#endif

        struct PageToken
        {
            unsigned char bytes[16]{};
        };

        struct PacketToken
        {
            unsigned char bytes[32]{};
        };

        struct OffsetPair
        {
            std::uint32_t low = 0;
            std::int32_t high = 0;
        };



        using ReadCallback = std::size_t (*)(void*, std::size_t, std::size_t, void*);
        using SeekCallback = int (*)(void*, int, int, int);
        using CloseCallback = int (*)(void*);
        using TellCallback = int (*)(void*);

        extern int g_callbackReadErrorFlag;


        template <typename T>
        T readField(const VorbisStateBlob& state, std::size_t offset)
        {
            T value{};
            if (offset + sizeof(T) <= kStateSize)
                std::memcpy(&value, state.bytes + offset, sizeof(T));
            return value;
        }

        template <typename T>
        void writeField(VorbisStateBlob& state, std::size_t offset, T value)
        {
            if (offset + sizeof(T) <= kStateSize)
                std::memcpy(state.bytes + offset, &value, sizeof(T));
        }

        template <typename T>
        T readBlockField(const unsigned char* base, std::size_t offset)
        {
            T value{};
            std::memcpy(&value, base + offset, sizeof(T));
            return value;
        }

        template <typename T>
        void writeBlockField(unsigned char* base, std::size_t offset, T value)
        {
            std::memcpy(base + offset, &value, sizeof(T));
        }


        void releaseInfoRecord(void* record);
        void releaseCommentRecord(void* record);
        int releaseCodebookRecord(void* record);



        constexpr std::size_t kSyncDataOffset = 0x00;
        constexpr std::size_t kSyncStorageOffset = 0x04;
        constexpr std::size_t kSyncFillOffset = 0x08;
        constexpr std::size_t kSyncReturnedOffset = 0x0C;
        constexpr std::size_t kSyncUnsyncedOffset = 0x10;
        constexpr std::size_t kSyncHeaderBytesOffset = 0x14;
        constexpr std::size_t kSyncBodyBytesOffset = 0x18;

        constexpr std::size_t kPacketBodyDataOffset = 0x00;
        constexpr std::size_t kPacketBodyStorageOffset = 0x04;
        constexpr std::size_t kPacketBodyFillOffset = 0x08;
        constexpr std::size_t kPacketBodyReturnedOffset = 0x0C;
        constexpr std::size_t kPacketLacingValuesOffset = 0x10;
        constexpr std::size_t kPacketGranuleValuesOffset = 0x14;
        constexpr std::size_t kPacketLacingStorageOffset = 0x18;
        constexpr std::size_t kPacketLacingFillOffset = 0x1C;
        constexpr std::size_t kPacketLacingPacketOffset = 0x20;
        constexpr std::size_t kPacketLacingReturnedOffset = 0x24;
        constexpr std::size_t kPacketEndOfStreamOffset = 0x144;
        constexpr std::size_t kPacketBeginOfStreamOffset = 0x148;
        constexpr std::size_t kPacketSerialNumberOffset = 0x150;
        constexpr std::size_t kPacketPageNumberOffset = 0x154;
        constexpr std::size_t kPacketNumberLowOffset = 0x158;
        constexpr std::size_t kPacketGranuleLowOffset = 0x160;



        int pageSerialNumber(const PageToken& page);
        std::int64_t pageGranulePosition(const PageToken& page);



        int rebuildPerLinkPcmLengths(VorbisStateBlob& state, std::int64_t restoreOffset);




        constexpr std::size_t kPcmQueueStateOffset = 0x1E0;
        constexpr std::size_t kBlockDecodeStateOffset = 0x250;
        constexpr std::size_t kDecodeStateOwnerOffset = 0x04;
        constexpr std::size_t kDecodeStateChannelBuffersOffset = 0x08;
        constexpr std::size_t kDecodeStateChannelCursorListOffset = 0x0C;
        constexpr std::size_t kDecodeStateQueuedLimitOffset = 0x14;
        constexpr std::size_t kDecodeStateQueuedCursorOffset = 0x18;
        constexpr std::size_t kDecodeStateEndFlagOffset = 0x20;
        constexpr std::size_t kDecodeStatePreviousWindowFlagOffset = 0x24;
        constexpr std::size_t kDecodeStateCurrentWindowFlagOffset = 0x28;
        constexpr std::size_t kDecodeStateNextWindowFlagOffset = 0x2C;
        constexpr std::size_t kDecodeStateCenterWindowOffset = 0x30;
        constexpr std::size_t kDecodeStateGranuleLowOffset = 0x38;
        constexpr std::size_t kDecodeStateSequenceLowOffset = 0x40;
        constexpr std::size_t kDecodeStateGlueBitsLowOffset = 0x48;
        constexpr std::size_t kDecodeStateTimeBitsLowOffset = 0x50;
        constexpr std::size_t kDecodeStateFloorBitsLowOffset = 0x58;
        constexpr std::size_t kDecodeStateResidueBitsLowOffset = 0x60;
        constexpr std::size_t kDecodeStatePrivateOwnerOffset = 0x68;
        constexpr std::size_t kVorbisPrivateStateSize = 0xB8;
        constexpr std::size_t kPrivateWindow0Offset = 0x04;
        constexpr std::size_t kPrivateWindow1Offset = 0x08;
        constexpr std::size_t kPrivateTransform0Offset = 0x0C;
        constexpr std::size_t kPrivateTransform1Offset = 0x10;
        constexpr std::size_t kPrivateFft0Offset = 0x14;
        constexpr std::size_t kPrivateFft1Offset = 0x20;
        constexpr std::size_t kPrivateModeBitsOffset = 0x2C;
        constexpr std::size_t kPrivateFloorLookArrayOffset = 0x30;
        constexpr std::size_t kPrivateResidueLookArrayOffset = 0x34;
        constexpr std::size_t kPrivatePsyLookArrayOffset = 0x38;

        constexpr std::size_t kBlockDecodedChannelTableOffset = 0x00;
        constexpr std::size_t kBlockPreviousWindowFlagOffset = 0x18;
        constexpr std::size_t kBlockLongModeOffset = 0x1C;
        constexpr std::size_t kBlockNextWindowFlagOffset = 0x20;
        constexpr std::size_t kBlockDecodedSampleCountOffset = 0x24;
        constexpr std::size_t kBlockModeIndexOffset = 0x28;
        constexpr std::size_t kBlockEndFlagOffset = 0x2C;
        constexpr std::size_t kBlockGranuleLowOffset = 0x30;
        constexpr std::size_t kBlockGranuleHighOffset = 0x34;
        constexpr std::size_t kBlockPacketNumberLowOffset = 0x38;
        constexpr std::size_t kBlockPacketNumberHighOffset = 0x3C;
        constexpr std::size_t kBlockQueueOwnerOffset = 0x40;
        constexpr std::size_t kBlockWindowTempOffset = 0x44;



        void releaseBlockDecodeScratch(unsigned char* scratch);



        const unsigned char* setupFromInfo(const unsigned char* info);




        void* readBlockPointer(const unsigned char* base, std::size_t offset);
        void writeBlockPointer(unsigned char* base, std::size_t offset, void* value);
        void* readPointer(const VorbisStateBlob& state, std::size_t offset);
        void writePointer(VorbisStateBlob& state, std::size_t offset, void* value);
        std::int64_t readSigned64(const VorbisStateBlob& state, std::size_t lowOffset);
        void writeSigned64(VorbisStateBlob& state, std::size_t lowOffset, std::int64_t value);
        OffsetPair splitOffset(std::int64_t value);
        std::int64_t combineOffset(std::uint32_t low, std::int32_t high);
        bool nonNegative(std::int64_t value);
        void releasePointerField(VorbisStateBlob& state, std::size_t offset);
        void releaseLinkArrays(VorbisStateBlob& state);
        int seekFileCallback(void* owner, int low, int high, int origin);
        std::size_t readFileCallback(void* buffer, std::size_t size, std::size_t count, void* owner);
        int closeFileCallback(void* owner);
        int tellFileCallback(void* owner);
        void initializeBitCursor(BitCursor& cursor, const void* data, int size);
        void initializeBitWriter(BitCursor& cursor);
        void alignBitWriter(BitCursor& cursor);
        void clearBitWriter(BitCursor& cursor);
        void writeBits(BitCursor& cursor, unsigned int value, int bits);
        int bitMaskForWidth(int bits);
        int readBits(BitCursor& cursor, int bits);
        int lookBits(const BitCursor& cursor, int bits);
        void advanceBits(BitCursor& cursor, int bits);
        int bitByteOffset(const BitCursor& cursor);
        int readBytesFromPacket(BitCursor& cursor, void* target, int count);
        int countBitsForUnsignedValue(unsigned int value);
        int countBitsForValueMinusOne(int value);
        void* initializePsyLookRecord(unsigned char* look, const unsigned char* infoPsy, const int* globalPsy, int n, int rate);
        void releasePsyLookRecord(void* record);
        void initializeSmallFftLookup(unsigned char* owner, int n);
        void initializeSmallFftCache(int n, float* cache, int* split);
        void initializeSmallFftFactors(int n, float* wa, int* ifac);
        void releaseSmallFftLookup(void* record);
        int populationCountUnsigned(unsigned int value);
        void releaseFloor0Record(void* record);
        void releaseFloor1Record(void* record);
        void releaseResidueRecord(void* record);
        void releaseResidueLookRecord(void* record);
        void releaseMapping0Record(void* record);
        void* parseFloorSetupRecordByType(int type, void* infoRecord, BitCursor& cursor);
        void* parseResidueSetupRecordByType(int type, void* infoRecord, BitCursor& cursor);
        void* parseMappingSetupRecordByType(int type, void* infoRecord, BitCursor& cursor);
        void releaseFloorRecordByType(int type, void* record);
        void releaseResidueRecordByType(int type, void* record);
        void releaseMappingRecordByType(int type, void* record);
        int lookup1ValueCount(const unsigned char* codebook);
        float unpackCodebookFloat32(int packed);
        std::uint32_t reverseBits32(std::uint32_t value);
        int activeCodebookEntryCount(const unsigned char* codebook);
        void* buildCanonicalCodewordList(const unsigned char* codebook, int sparseCount);
        void* buildCodebookValueVectorList(const unsigned char* codebook, int vectorCount, const int* indexMap);
        void* bookUnquantizeBoundary(const unsigned char* codebook, int vectorCount, const int* sortIndex);
        int initializeRuntimeCodebookRecord(unsigned char* runtimeBook, const unsigned char* staticBook);
        int vorbis_book_init_decode(unsigned char* runtimeBook, const unsigned char* staticBook);
        int initializeDecodeCodebooksForSetup(unsigned char* setup);
        int clearRuntimeCodebook(unsigned char* runtimeBook);
        const unsigned char* codebookRecordFromSetup(const unsigned char* setup, int bookIndex);
        int decodeCodebookEntryIndex(const unsigned char* codebook, BitCursor& cursor);
        int decodeCodebookVectorsStridedAdd(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount);
        int decodeCodebookVectorsAdd(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount);
        int decodeCodebookVectorsSet(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount);
        int decodeCodebookVectorsAcrossChannelsAdd(const unsigned char* codebook, BitCursor& cursor, float** channels, int offset, int channelCount, int sampleCount);
        int vorbis_book_decode(const unsigned char* codebook, BitCursor& cursor);
        int decodeCodebookVectorAdd(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleStride);
        int vorbis_book_decodevs_add(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount);
        int vorbis_book_decodev_add(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount);
        int vorbis_book_decodev_set(const unsigned char* codebook, BitCursor& cursor, float* target, int sampleCount);
        int vorbis_book_decodevv_add(const unsigned char* codebook, BitCursor& cursor, float** channels, int offset, int channelCount, int sampleCount);
        void resetSyncBuffer(VorbisStateBlob& state);
        void clearSyncBufferedPage(VorbisStateBlob& state);
        unsigned char* reserveSyncBuffer(VorbisStateBlob& state, int bytes);
        void markSyncBytesWritten(VorbisStateBlob& state, int bytes);
        void resetSyncReadPosition(VorbisStateBlob& state);
        std::uint32_t oggCrcTableValue(unsigned int index);
        std::uint32_t updateOggChecksum(std::uint32_t checksum, const unsigned char* data, int size);
        void writePageToken(PageToken& page, unsigned char* header, int headerBytes, unsigned char* body, int bodyBytes);
        unsigned char* pageHeaderPointer(const PageToken& page);
        unsigned char* pageBodyPointer(const PageToken& page);
        int pageHeaderSize(const PageToken& page);
        int pageBodySize(const PageToken& page);
        int takePageFromSyncBuffer(VorbisStateBlob& state, PageToken& page);
        int pageHeaderVersion(const PageToken& page);
        int pageIsContinued(const PageToken& page);
        int pageBeginsStream(const PageToken& page);
        int pageEndsStream(const PageToken& page);
        void releasePacketStreamResources(VorbisStateBlob& state);
        void resetPacketStreamCounters(VorbisStateBlob& state);
        void initializePacketStreamResources(VorbisStateBlob& state, int serialNumber);
        void initPacketStream(VorbisStateBlob& state, int serialNumber);
        void releaseCommentRecord(void* record);
        void releaseSetupRecord(void* setup);
        void releaseInfoRecord(void* record);
        void clearInfoRecord(void* record);
        void clearCommentRecord(void* record);
        void ensurePacketBodyStorage(unsigned char* stream, int bytes);
        void ensurePacketLacingStorage(unsigned char* stream, int count);
        int pageSequenceNumber(const PageToken& page);
        void feedPageToPacketStream(VorbisStateBlob& state, const PageToken& page);
        void writePacketToken(PacketToken& packet, void* data, int bytes, int begins, int ends, std::int64_t granule, std::int64_t packetNumber);
        int takePacketFromStreamInternal(VorbisStateBlob& state, PacketToken* packet, bool advance);
        int takePacketFromStream(VorbisStateBlob& state, PacketToken& packet);
        int pageSerialNumber(const PageToken& page);
        std::int64_t pageGranulePosition(const PageToken& page);
        int parseIdentificationPacket(void* infoRecord, BitCursor& cursor);
        int parseCommentPacket(void* commentRecord, BitCursor& cursor);
        int releaseCodebookRecord(void* record);
        int parseCodebookRecordBoundary(BitCursor& cursor, void* codebookRecord);
        void* parseFloor0SetupRecord(void* infoRecord, BitCursor& cursor);
        void* parseFloor1SetupRecord(void* infoRecord, BitCursor& cursor);
        void* parseResidueSetupRecord(void* infoRecord, BitCursor& cursor);
        void* parseMapping0SetupRecord(void* infoRecord, BitCursor& cursor);
        int parseModeSetupRecords(void* infoRecord, BitCursor& cursor);
        int parseSetupPacketBody(void* infoRecord, BitCursor& cursor);
        int parseSetupPacket(void* infoRecord, void* commentRecord, const PacketToken& packet);
        int packetBlockSize(void* infoRecord, const PacketToken& packet);
        int refillSyncBuffer(VorbisStateBlob& state);
        std::int64_t readPageUntil(VorbisStateBlob& state, PageToken& page, std::int64_t limit);
        void seekInputAndResetSync(VorbisStateBlob& state, std::int64_t position);
        std::int64_t findPageBeforePosition(VorbisStateBlob& state, PageToken& page);
        int buildSeekTableRecursive(
            VorbisStateBlob& state,
            std::int64_t beginOffset,
            std::int64_t searchedOffset,
            std::int64_t endOffset,
            int serialNumber,
            int depth);
        int parseInitialHeaders(VorbisStateBlob& state, void* infoRecord, void* commentRecord, int* serialOut, PageToken* suppliedPage);
        int buildSeekableLinkTables(VorbisStateBlob& state);
        int initializeVorbisFileState(
            void* inputOwner,
            VorbisStateBlob& state,
            const void* initialData,
            unsigned int initialSize,
            ReadCallback readCallback,
            SeekCallback seekCallback,
            CloseCallback closeCallback,
            TellCallback tellCallback);
        int openAndSetupVorbisState(
            void* inputOwner,
            VorbisStateBlob& state,
            const void* initialData,
            unsigned int initialSize,
            ReadCallback readCallback,
            SeekCallback seekCallback,
            CloseCallback closeCallback,
            TellCallback tellCallback);
        int rebuildPerLinkPcmLengths(VorbisStateBlob& state, std::int64_t restoreOffset);
        const unsigned char* infoRecordForLink(VorbisStateBlob& state, int linkIndex);
        int nativePcmEndianFlag();
        int fetchAndProcessPacket(VorbisStateBlob& state, int readPages);
        unsigned char* pcmQueueState(VorbisStateBlob& state);
        unsigned char* blockDecodeState(VorbisStateBlob& state);
        int queueChannelCount(const unsigned char* queue);
        float* blockChannelSamples(unsigned char* scratch, int channel);
        void applyInverseCouplingBoundary(unsigned char* scratch, const unsigned char* mapping, int channels);
        int queuedPcmSamples(unsigned char* queue, void** channelCursorList);
        int consumeQueuedPcmSamples(unsigned char* queue, int count);
        int clampSigned8(float value);
        int clampSigned16(float value);
        int modeIndexBitCount(const unsigned char* setup);
        const unsigned char* setupFromInfo(const unsigned char* info);
        const unsigned char* modeRecordFromPacket(const unsigned char* setup, const PacketToken& packet, int* modeIndex);
        const unsigned char* mappingRecordFromMode(const unsigned char* setup, const unsigned char* mode);
        int floor1LowNeighbour(const unsigned char* floorRecord, int pointIndex);
        int floor1HighNeighbour(const unsigned char* floorRecord, int pointIndex);
        int floor1RenderPoint(int x0, int x1, int y0, int y1, int x);
        float floorLookupGainFromIndex(int value);
        int ensureFloor0LinearMap(unsigned char* block, unsigned char* floorInfo, unsigned char* floorLook);
        int renderFloor0LspCurve(float* curve, const int* map, int n, int ln, float* lsp, int m, float amp, float ampoffset);
        double vorbisCosLookup(float value);
        double vorbisInverseSqrtLookup(float value);
        double vorbisInverseSqrtExponentLookup(int value);
        double vorbisFromDbLookup(float value);
        void renderFloorLineIntoSamples(int x0, int x1, int y0, int y1, float* samples, int sampleCount);
        void* buildFloor0LookRecord(const unsigned char* setup, int floorIndex);
        void* buildFloor1LookRecord(const unsigned char* info, const unsigned char* floorRecord);
        void releaseFloor1LookRecord(void* look);
        void* allocateBlockScratch(unsigned char* block, int bytes);
        void* buildVorbisWindow(int type, int sampleCount);
        int initializeMdctLookup(void* owner, int sampleCount);
        int clearMdctLookup(void* owner);
        int runMdctBackward(void* owner, float* input, float* output);
        int applyVorbisWindow(float* pcm, const unsigned char* privateState, const unsigned char* setup, int previousW, int W, int nextW);
        void* decodeFloor0Memo(unsigned char* block, unsigned char* floorLook);
        int applyFloor0Memo(unsigned char* block, unsigned char* floorLook, void* memo, float* samples);
        void* decodeFloor1Memo(unsigned char* block, unsigned char* floorLook);
        int applyFloor1Memo(unsigned char* block, unsigned char* floorLook, void* memo, float* samples);
        int inverseMapping0(unsigned char* block, const unsigned char* mapping);
        void releaseFloor0LookRecord(void* record);
        int parseAudioPacketMode(const unsigned char* info, const PacketToken& packet, int* modeIndex, int* previousWindowFlag, int* nextWindowFlag);
        void* buildResidueLookRecord(const unsigned char* setup, int residueIndex);
        using Residue01DecodeCallback = int (*)(const unsigned char*, BitCursor&, float*, int);
        int inverseResidue01Core(unsigned char* block, const unsigned char* look, float** channels, int channelCount, Residue01DecodeCallback decodePartition);
        int inverseResidue0(unsigned char* block, const unsigned char* look, float** channels, const int* nonzero, int channelCount);
        int inverseResidue1(unsigned char* block, const unsigned char* look, float** channels, const int* nonzero, int channelCount);
        int inverseResidue2(unsigned char* block, const unsigned char* look, float** channels, const int* nonzero, int channelCount);
        int imdctLog2FloorFromSize(int value);
        void releaseSmallWindowRecord(void* record);
        void releaseLargeTransformRecord(void* record);
        std::int64_t readBlockSigned64(const unsigned char* base, std::size_t lowOffset);
        void writeBlockSigned64(unsigned char* base, std::size_t lowOffset, std::int64_t value);
        int vorbis_synthesis_blockin(unsigned char* synthesisState, unsigned char* blockState);
        int vorbis_synthesis(VorbisStateBlob& state, const PacketToken& packet);
        int decodeNextPacketIntoPcmQueue(VorbisStateBlob& state);
        void releaseBlockDecodeScratch(unsigned char* scratch);
        void releasePcmQueueState(VorbisStateBlob& state);
        int initializeVorbisDspState(VorbisStateBlob& state, const unsigned char* info, int encoderFlag);
        int initializeVorbisSynthesisState(VorbisStateBlob& state, const unsigned char* info);
        int initializeVorbisBlockState(unsigned char* synthesisState, unsigned char* blockState);
        int ensureVorbisDecodeReady(VorbisStateBlob& state);
} } }
