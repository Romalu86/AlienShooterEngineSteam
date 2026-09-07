#pragma once
#include "types.h"
#include "as_string.h"
#include "base_stream.h"
#include "resource_filter.h"

#include <cstdio>

namespace as1
{
    class Filter;

    class RESOURCE : public BaseStream
    {
    public:
        struct ResTypes
        {
            using Type = std::uint32_t;
            static constexpr Type DATA       = AS_FOURCC('D', 'A', 'T', 'A');
            static constexpr Type ANY        = AS_FOURCC('A', 'N', 'Y', ' ');
            static constexpr Type CONSTANT   = AS_FOURCC('C', 'N', 'S', 'T');
            static constexpr Type DEMO       = AS_FOURCC('D', 'E', 'M', 'O');
            static constexpr Type MAP        = AS_FOURCC('M', 'A', 'P', ' ');
            static constexpr Type GRAPH      = AS_FOURCC('G', 'R', 'P', 'H');
            static constexpr Type HEAD       = AS_FOURCC('H', 'E', 'A', 'D');
            static constexpr Type GRID       = AS_FOURCC('G', 'R', 'I', 'D');
            static constexpr Type SPRITE     = AS_FOURCC('S', 'P', 'R', ' ');
            static constexpr Type SPRITEDATA = AS_FOURCC('S', 'P', 'R', 'D');
            static constexpr Type PLAY       = AS_FOURCC('P', 'L', 'A', 'Y');
            static constexpr Type GROUP      = AS_FOURCC('G', 'R', 'O', 'U');
            static constexpr Type WEAPON     = AS_FOURCC('W', 'E', 'A', 'P');
            static constexpr Type OBJECT     = AS_FOURCC('O', 'B', 'J', ' ');
            static constexpr Type VID        = AS_FOURCC('V', 'I', 'D', ' ');
            static constexpr Type MENU       = AS_FOURCC('M', 'E', 'N', 'U');
            static constexpr Type SPRI       = AS_FOURCC('S', 'P', 'R', 'I');
            static constexpr Type SURFACE    = AS_FOURCC('S', 'U', 'R', 'F');
            static constexpr Type SHADOW     = AS_FOURCC('S', 'H', 'A', 'D');
            static constexpr Type PALETTE    = AS_FOURCC('P', 'A', 'L', ' ');
            static constexpr Type RES        = AS_FOURCC('R', 'E', 'S', ' ');
            static constexpr Type RIFF       = AS_FOURCC('R', 'I', 'F', 'F');
            static constexpr Type SFX        = AS_FOURCC('S', 'F', 'X', ' ');
            static constexpr Type WAVE       = AS_FOURCC('W', 'A', 'V', 'E');
            static constexpr Type WAVE_FMT   = AS_FOURCC('f', 'm', 't', ' ');
            static constexpr Type WAVE_DATA  = AS_FOURCC('d', 'a', 't', 'a');
            static constexpr Type CADR       = AS_FOURCC('C', 'A', 'D', 'R');
            static constexpr Type UNKNOWN    = 0;
        };

        RESOURCE();
        ~RESOURCE();

        const STRING& name() const { return m_name; }
        ResTypes::Type type() const { return m_type; }

        int SubSize() const
        {
            return static_cast<int>(m_signature == ResTypes::RIFF ? m_sectionSize : m_currentSubresourceSize);
        }

        bool openFile(const STRING& name, ResTypes::Type res_type = ResTypes::DATA);
        bool openFileForWrite(const STRING& name, ResTypes::Type res_type = ResTypes::DATA);
        int openResourceNativeFile(std::FILE* file, ResTypes::Type res_type);
        void clear();

        int GoBegin(ResTypes::Type typ = ResTypes::ANY);
        int GoNext(ResTypes::Type typ = ResTypes::ANY);
        int GoBeginSub(ResTypes::Type typ = ResTypes::ANY);
        int GoNextSub(ResTypes::Type typ = ResTypes::ANY);
        int GetNoSubRes(ResTypes::Type typ);
        int GetBytesToEndSub() const;
        int ReadPayload(void* data, unsigned size, Filter* filter = nullptr);
        int WritePayload(const void* data, unsigned size, Filter* filter = nullptr);
        int SubLoad(void** data, Filter* packer = nullptr);
        int CopySectionTypeFrom(RESOURCE& source, ResTypes::Type typ, Filter* filter = nullptr);

        int BeginSection(ResTypes::Type typ, std::uint32_t options = 0, std::uint32_t packedDiff = 0);
        int EndSection();

        std::uint32_t CurrentSubCount() const { return m_currentSubresourceCount; }
        std::uint32_t CurrentOptions() const { return m_options; }
        std::uint32_t CurrentPackedDiff() const { return m_packedDiff; }
        size_t CurrentResourceSize() const { return m_sectionSize; }
        size_t CurrentResourcePosition() const { return m_sectionPosition; }

        int read(void* buf, unsigned size) override;
        int write(const void* buf, unsigned size) override;

        size_t seek(size_t pos);
        size_t shift(int delta);
#if defined(_MSC_VER) && defined(_M_IX86)
        __forceinline void retailShiftCurrentUnchecked(int delta) noexcept
        {
            m_state = st_seek;
            std::fseek(m_file, delta, SEEK_CUR);
        }
#endif
        size_t position() const;
        size_t length() const { return m_sectionSize; }
        void close();
        bool isOpen() const { return m_file != nullptr; }
        bool isWritable() const { return m_file != nullptr && (m_flags & 1u) != 0; }
        std::FILE* nativeFile() const { return m_file; }

    private:
        friend RESOURCE& initializeResourceState(RESOURCE& resource);
        friend RESOURCE& closeResourceOwner(RESOURCE& resource);
        friend int openResourceFileForRead(RESOURCE& resource, const STRING& name, ResTypes::Type res_type);
        friend int openResourceFileForWrite(RESOURCE& resource, const STRING& name, ResTypes::Type res_type);
        friend int readResourceBytes(RESOURCE& resource, void* data, std::size_t size);
        friend int writeResourceBytes(RESOURCE& resource, const void* data, std::size_t size);
        friend int readResourcePayload(RESOURCE& resource, void* data, std::size_t size, Filter* filter);
        friend int writeResourcePayload(RESOURCE& resource, const void* data, std::size_t size, Filter* filter);
        friend int goResourceBegin(RESOURCE& resource, ResTypes::Type typ);
        friend int goResourceNext(RESOURCE& resource, ResTypes::Type typ);
        friend int goResourceNextSubresource(RESOURCE& resource, ResTypes::Type typ);
        friend int beginResourceSection(RESOURCE& resource, ResTypes::Type typ, std::uint32_t options);
        friend int endResourceSection(RESOURCE& resource);
        friend int loadResourceSubresource(RESOURCE& resource, void** data, Filter* packer);
        friend int loadResourceSectionArray(RESOURCE& resource, ResTypes::Type typ, void** data, unsigned elementSize);
        friend int countResourceSubresources(RESOURCE& resource, ResTypes::Type typ);
        friend int copyResourceSection(RESOURCE& destination, RESOURCE& source, ResTypes::Type typ);
        friend void destroyResourceOwner(RESOURCE& resource);

        static constexpr std::uint32_t OPT_OPT_EXIST = 0x80000000u;
        enum State : std::uint32_t { st_write = 0, st_read = 1, st_seek = 2 };

        std::uint32_t m_flags;                    // +0x04
        std::uint32_t m_state;                    // +0x08
        STRING m_name;                            // +0x0C
        ResTypes::Type m_signature;               // +0x10
        std::uint32_t m_sectionSize;              // +0x14
        std::uint32_t m_sectionPosition;          // +0x18
        std::uint32_t m_rootBegin;                // +0x1C
        std::uint32_t m_rootEnd;                  // +0x20
        std::uint32_t m_options;                  // +0x24
        std::uint32_t m_currentSubresourceCount;  // +0x28
        std::uint32_t m_currentSubresourcePosition;// +0x2C
        std::uint32_t m_currentSubresourceSize;   // +0x30
        std::uint32_t m_packedDiff;               // +0x34
        std::FILE* m_file;                        // +0x38
        ResTypes::Type m_type;                    // +0x3C
    };

    RESOURCE& initializeResourceState(RESOURCE& resource);
    RESOURCE& closeResourceOwner(RESOURCE& resource);
    int openResourceFileForRead(RESOURCE& resource, const STRING& name, RESOURCE::ResTypes::Type res_type);
    int openResourceFileForWrite(RESOURCE& resource, const STRING& name, RESOURCE::ResTypes::Type res_type);
    int readResourceBytes(RESOURCE& resource, void* data, std::size_t size);
    int writeResourceBytes(RESOURCE& resource, const void* data, std::size_t size);
    int readResourcePayload(RESOURCE& resource, void* data, std::size_t size, Filter* filter);
    int writeResourcePayload(RESOURCE& resource, const void* data, std::size_t size, Filter* filter);
    int goResourceBegin(RESOURCE& resource, RESOURCE::ResTypes::Type typ);
    int goResourceNext(RESOURCE& resource, RESOURCE::ResTypes::Type typ);
    int goResourceNextSubresource(RESOURCE& resource, RESOURCE::ResTypes::Type typ);
    int beginResourceSection(RESOURCE& resource, RESOURCE::ResTypes::Type typ, std::uint32_t options);
    int endResourceSection(RESOURCE& resource);
    int loadResourceSubresource(RESOURCE& resource, void** data, Filter* packer);
    int loadResourceSectionArray(RESOURCE& resource, RESOURCE::ResTypes::Type typ, void** data, unsigned elementSize);
    int countResourceSubresources(RESOURCE& resource, RESOURCE::ResTypes::Type typ);
    int copyResourceSection(RESOURCE& destination, RESOURCE& source, RESOURCE::ResTypes::Type typ);
    void destroyResourceOwner(RESOURCE& resource);

#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                    
#endif
}
