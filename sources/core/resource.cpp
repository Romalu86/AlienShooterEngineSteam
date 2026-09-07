#include "resource.h"
#include "log.h"
#include "file_logger.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_MSC_VER)
#include <io.h>
#endif

namespace as1
{
    namespace
    {
        void fourccText(RESOURCE::ResTypes::Type type, char (&out)[5])
        {
            out[0] = static_cast<char>(type & 0xFFu);
            out[1] = static_cast<char>((type >> 8) & 0xFFu);
            out[2] = static_cast<char>((type >> 16) & 0xFFu);
            out[3] = static_cast<char>((type >> 24) & 0xFFu);
            out[4] = '\0';
        }

        void logResourceError(const STRING& name, RESOURCE::ResTypes::Type type, int code, const char* detail, int value)
        {
            char typ[5];
            fourccText(type, typ);
            LOG::ResourceError("RES '%s' '%.4s'", code, detail, value, name.c_str(), typ);
        }

        long fileLength(std::FILE* file)
        {
            if (!file)
                return -1;
#if defined(_MSC_VER)
            return static_cast<long>(::_filelength(::_fileno(file)));
#else
            const long old = std::ftell(file);
            if (old < 0 || std::fseek(file, 0, SEEK_END) != 0)
                return -1;
            const long length = std::ftell(file);
            std::fseek(file, old, SEEK_SET);
            return length;
#endif
        }
    }

    RESOURCE::RESOURCE()
    {
        m_file = nullptr;
        m_sectionPosition = 0;
        m_sectionSize = 0;
        m_rootBegin = 0;
        m_rootEnd = 0;
        m_state = st_seek;
        m_flags &= ~1u;
    }

    RESOURCE::~RESOURCE()
    {
        destroyResourceOwner(*this);
    }

    RESOURCE& initializeResourceState(RESOURCE& resource)
    {

        resource.m_file = nullptr;
        resource.m_sectionPosition = 0;
        resource.m_sectionSize = 0;
        resource.m_rootBegin = 0;
        resource.m_rootEnd = 0;
        resource.m_state = RESOURCE::st_seek;
        resource.m_flags &= ~1u;
        return resource;
    }

    RESOURCE& closeResourceOwner(RESOURCE& resource)
    {

        if (resource.m_file)
        {
            if ((resource.m_flags & 1u) != 0)
            {
                resource.m_state = RESOURCE::st_seek;
                std::fseek(resource.m_file, static_cast<long>(resource.m_rootBegin + 4u), SEEK_SET);
                resource.m_rootEnd += static_cast<std::uint32_t>(-8 - static_cast<std::int32_t>(resource.m_rootBegin));
                resource.write(&resource.m_rootEnd, 4);
            }
            std::fclose(resource.m_file);
        }
        resource.m_file = nullptr;
        resource.m_rootEnd = 0;
        assignStringFromCString(resource.m_name, "Not opened");
        return resource;
    }

    int RESOURCE::openResourceNativeFile(std::FILE* file, RESOURCE::ResTypes::Type res_type)
    {
        RESOURCE& resource = *this;

        if (resource.m_file)
            closeResourceOwner(resource);

        resource.m_file = file;
        if (!file)
        {
            logResourceError(resource.m_name, resource.m_type, 7, "file is NULL", 0);
            return 1;
        }

        resource.m_rootBegin = static_cast<std::uint32_t>(std::ftell(file));
        if (resource.read(&resource.m_signature, 4) != 0)
        {
            logResourceError(resource.m_name, resource.m_type, 5, "empty file", 0);
            closeResourceOwner(resource);
            return 4;
        }

        if (resource.m_signature != RESOURCE::ResTypes::RES && resource.m_signature != RESOURCE::ResTypes::RIFF)
        {
            logResourceError(resource.m_name, resource.m_signature, 4, "resource signature", 0);
            closeResourceOwner(resource);
            return 2;
        }

        resource.read(&resource.m_rootEnd, 4);
        resource.m_rootEnd += resource.m_rootBegin + 8u;
        const long realLength = fileLength(resource.m_file);
        if (realLength < static_cast<long>(resource.m_rootEnd))
        {
            logResourceError(resource.m_name,
                             resource.m_type,
                             10,
                             "Invalid filelength",
                             static_cast<int>(realLength - static_cast<long>(resource.m_rootEnd)));
        }

        RESOURCE::ResTypes::Type rootType = 0;
        resource.read(&rootType, 4);
        if (rootType != res_type && res_type != RESOURCE::ResTypes::ANY)
        {
            resource.m_type = rootType;
            logResourceError(resource.m_name, rootType, 4, "resource type", 0);
            closeResourceOwner(resource);
            return 3;
        }

        goResourceBegin(resource, RESOURCE::ResTypes::ANY);
        return 0;
    }

    int openResourceFileForRead(RESOURCE& resource, const STRING& name, RESOURCE::ResTypes::Type res_type)
    {

        if (resource.m_file)
            closeResourceOwner(resource);

        std::FILE* file = nullptr;
        if (name.c_str()[0] != '\0')
            file = std::fopen(name.c_str(), "rb");
        if (!file)
        {
            logResourceError(resource.m_name, resource.m_type, 7, name.c_str(), 0);
            return 1;
        }

        assignStringFromString(resource.m_name, name);
        return resource.openResourceNativeFile(file, res_type);
    }

    int openResourceFileForWrite(RESOURCE& resource, const STRING& name, RESOURCE::ResTypes::Type res_type)
    {

        if (resource.m_file)
            closeResourceOwner(resource);

        resource.m_file = name.c_str()[0] != '\0' ? std::fopen(name.c_str(), "w+b") : nullptr;
        if (!resource.m_file)
        {
            logResourceError(resource.m_name, resource.m_type, 3, name.c_str(), 0);
            return 1;
        }

        resource.m_rootBegin = 0;
        resource.m_rootEnd = 12;
        resource.m_type = 0;
        assignStringFromString(resource.m_name, name);
        resource.m_signature = RESOURCE::ResTypes::RES;
        resource.write(&resource.m_signature, 4);
        std::uint32_t rootSize = 4;
        resource.write(&rootSize, 4);
        resource.write(&res_type, 4);
        goResourceBegin(resource, RESOURCE::ResTypes::ANY);
        return 0;
    }

    int readResourceBytes(RESOURCE& resource, void* data, std::size_t size)
    {

        std::size_t missing = size;
        if (resource.m_file && size)
        {
            if (resource.m_state == RESOURCE::st_write)
            {
                resource.m_state = RESOURCE::st_seek;
                std::fseek(resource.m_file, 0, SEEK_CUR);
            }
            resource.m_state = RESOURCE::st_read;
            missing = size - std::fread(data, 1, size, resource.m_file);
        }
        return static_cast<int>(missing);
    }

    int writeResourceBytes(RESOURCE& resource, const void* data, std::size_t size)
    {

        std::size_t missing = size;
        if (resource.m_file && size)
        {
            if (resource.m_state == RESOURCE::st_read)
            {
                resource.m_state = RESOURCE::st_seek;
                std::fseek(resource.m_file, 0, SEEK_CUR);
            }
            resource.m_state = RESOURCE::st_write;
            missing = size - std::fwrite(data, 1, size, resource.m_file);
        }
        return static_cast<int>(missing);
    }

    int readResourcePayload(RESOURCE& resource, void* data, std::size_t size, Filter* filter)
    {

        if (!resource.m_file || size == 0)
            return static_cast<int>(size);
        if (filter)
        {
            const std::size_t done = filter->ReadDecoded(data, size, resource);
            return static_cast<int>(size - done);
        }
        return resource.read(data, static_cast<unsigned>(size));
    }

    int writeResourcePayload(RESOURCE& resource, const void* data, std::size_t size, Filter* filter)
    {

        if (!resource.m_file || size == 0)
            return static_cast<int>(size);
        if (filter)
        {
            const std::size_t encoded = filter->WriteEncoded(data, size, resource);

            resource.m_packedDiff += static_cast<std::uint32_t>(size - encoded);
            return 0;
        }
        return resource.write(data, static_cast<unsigned>(size));
    }

    int goResourceBegin(RESOURCE& resource, RESOURCE::ResTypes::Type typ)
    {

        if (!resource.m_file)
            return 1;
        resource.m_sectionPosition = resource.m_rootBegin + 4u;
        resource.m_sectionSize = 0;
        return goResourceNext(resource, typ);
    }

    int goResourceNext(RESOURCE& resource, RESOURCE::ResTypes::Type typ)
    {

        if (!resource.m_file)
            return 1;

        for (;;)
        {
            const std::uint32_t aligned = (resource.m_sectionSize + 1u) & ~1u;
            const std::uint32_t next = resource.m_sectionPosition + aligned + 8u;
            resource.m_state = RESOURCE::st_seek;
            resource.m_sectionPosition = next;
            std::fseek(resource.m_file, static_cast<long>(next), SEEK_SET);

            if (resource.m_sectionPosition >= resource.m_rootEnd)
            {
                resource.m_sectionPosition -= aligned + 8u;
                resource.m_state = RESOURCE::st_seek;
                if (resource.m_signature == RESOURCE::ResTypes::RES)
                    std::fseek(resource.m_file, static_cast<long>(resource.m_sectionPosition + 24u), SEEK_SET);
                else
                    std::fseek(resource.m_file, static_cast<long>(resource.m_sectionPosition + 8u), SEEK_SET);
                return 2;
            }

            resource.read(&resource.m_type, 4);
            resource.read(&resource.m_sectionSize, 4);
            if (resource.m_signature == RESOURCE::ResTypes::RES)
            {
                resource.read(&resource.m_options, 4);
                if ((resource.m_options & RESOURCE::OPT_OPT_EXIST) != 0)
                {
                    resource.read(&resource.m_packedDiff, 4);
                    resource.read(&resource.m_currentSubresourceCount, 4);
                }
                else
                {
                    resource.m_currentSubresourceCount = resource.m_options;
                    resource.m_options = 0;
                }
                resource.m_currentSubresourcePosition = static_cast<std::uint32_t>(std::ftell(resource.m_file));
                resource.read(&resource.m_currentSubresourceSize, 4);
            }

            if (typ == resource.m_type || typ == RESOURCE::ResTypes::ANY)
                return 0;
        }
    }

    int goResourceNextSubresource(RESOURCE& resource, RESOURCE::ResTypes::Type typ)
    {

        if (!resource.m_file)
            return -1;

        resource.m_currentSubresourcePosition += resource.m_currentSubresourceSize + 4u;
        if (resource.m_currentSubresourcePosition < resource.m_sectionPosition + resource.m_sectionSize + 8u)
        {
            resource.m_state = RESOURCE::st_seek;
            std::fseek(resource.m_file, static_cast<long>(resource.m_currentSubresourcePosition), SEEK_SET);
            resource.read(&resource.m_currentSubresourceSize, 4);
            return 0;
        }
        return goResourceNext(resource, typ);
    }

    int beginResourceSection(RESOURCE& resource, RESOURCE::ResTypes::Type typ, std::uint32_t options)
    {

        if (!resource.m_file)
            return -1;

        if (options)
            resource.m_options |= 0x00000100u;

        if (goResourceNext(resource, RESOURCE::ResTypes::ANY) == 0)
        {
            while (goResourceNext(resource, RESOURCE::ResTypes::ANY) == 0)
            {
            }
        }

        if (resource.m_type != typ)
        {
            resource.m_type = typ;
            resource.m_rootEnd += 20u;
            resource.m_sectionPosition += ((resource.m_sectionSize + 1u) & ~1u) + 8u;
            resource.m_currentSubresourceCount = 0;
            resource.m_options = 0;
            resource.m_sectionSize = 12;
        }

        resource.m_currentSubresourceSize = 0;
        resource.m_packedDiff = 0;
        resource.m_currentSubresourcePosition = resource.m_sectionPosition + resource.m_sectionSize + 8u;
        resource.m_state = RESOURCE::st_seek;
        std::fseek(resource.m_file, static_cast<long>(resource.m_sectionPosition + resource.m_sectionSize + 12u), SEEK_SET);
        return 0;
    }

    int endResourceSection(RESOURCE& resource)
    {

        resource.m_options |= RESOURCE::OPT_OPT_EXIST;
        ++resource.m_currentSubresourceCount;
        const std::uint32_t payloadBegin = resource.m_currentSubresourcePosition;
        const long filePos = std::ftell(resource.m_file);
        resource.m_flags |= 1u;
        resource.m_currentSubresourceSize = static_cast<std::uint32_t>(filePos - static_cast<long>(payloadBegin) - 4L);
        resource.m_state = RESOURCE::st_seek;
        resource.m_sectionSize += resource.m_currentSubresourceSize + 4u;
        resource.m_rootEnd += resource.m_currentSubresourceSize + 4u;

        std::fseek(resource.m_file, static_cast<long>(payloadBegin), SEEK_SET);
        resource.m_currentSubresourcePosition += resource.m_currentSubresourceSize + 4u;
        resource.write(&resource.m_currentSubresourceSize, 4);

        resource.m_state = RESOURCE::st_seek;
        std::fseek(resource.m_file, static_cast<long>(resource.m_sectionPosition), SEEK_SET);
        resource.write(&resource.m_type, 4);
        resource.write(&resource.m_sectionSize, 4);
        resource.write(&resource.m_options, 4);
        resource.write(&resource.m_packedDiff, 4);
        const int result = resource.write(&resource.m_currentSubresourceCount, 4);
        resource.m_options &= ~0x00000100u;
        return result;
    }

    int loadResourceSubresource(RESOURCE& resource, void** data, Filter* packer)
    {

        (void)packer;
        if (!resource.m_file)
        {
            logResourceError(resource.m_name, resource.m_type, 5, "file not opened", 0);
            return 0;
        }
        if (resource.m_currentSubresourceSize == 0)
        {
            logResourceError(resource.m_name, resource.m_type, 11, "SubLoad", 0);
            return 0;
        }

        void* loaded = ::operator new(resource.m_currentSubresourceSize, std::nothrow);
        *data = loaded;
        if (!loaded)
        {
            logResourceError(resource.m_name, resource.m_type, 2, "Subload data", static_cast<int>(resource.m_currentSubresourceSize));
            return 0;
        }
        if (resource.read(loaded, resource.m_currentSubresourceSize) != 0)
            logResourceError(resource.m_name, resource.m_type, 5, "Subload", 0);
        return static_cast<int>(resource.m_currentSubresourceSize);
    }

    int countResourceSubresources(RESOURCE& resource, RESOURCE::ResTypes::Type typ)
    {

        if (!resource.m_file)
            return 0;
        int count = 0;
        if (goResourceBegin(resource, typ) == 0)
        {
            do
                count += static_cast<int>(resource.m_currentSubresourceCount);
            while (goResourceNext(resource, typ) == 0);
        }
        return count;
    }

    int loadResourceSectionArray(RESOURCE& resource, RESOURCE::ResTypes::Type typ, void** data, unsigned elementSize)
    {

        if (!resource.m_file)
        {
            logResourceError(resource.m_name, resource.m_type, 5, "file not opened", 0);
            std::exit(1);
        }

        const int count = countResourceSubresources(resource, typ);
        if (count == 0)
        {
            logResourceError(resource.m_name, typ, 11, "Load", static_cast<int>(typ));
            std::exit(1);
        }
        goResourceBegin(resource, typ);

        if (*data)
        {
            logResourceError(resource.m_name, resource.m_type, 5, "Already loaded", 0);
        }
        else
        {
            *data = ::operator new(static_cast<std::size_t>(elementSize) * static_cast<std::size_t>(count), std::nothrow);
        }

        if (!*data)
        {
            char typText[5];
            fourccText(typ, typText);
            fatalLogError(g_fileLogger, "ResLoad::type=%.4s no_sub=%i Not enough Memory", typText, count);
        }

        BYTE* out = static_cast<BYTE*>(*data);
        for (int i = 0; i < count; ++i)
        {
            resource.read(out + static_cast<std::size_t>(i) * elementSize, elementSize);
            goResourceNextSubresource(resource, typ);
        }
        return count;
    }

    int copyResourceSection(RESOURCE& destination, RESOURCE& source, RESOURCE::ResTypes::Type typ)
    {

        if (!destination.m_file || !source.m_file)
            return 1;
        if (goResourceBegin(source, typ) != 0)
            return 0;

        beginResourceSection(destination, typ, 0);
        destination.m_currentSubresourceCount = 0;
        std::uint32_t copySize = source.m_sectionSize;
        destination.m_currentSubresourceCount = source.m_currentSubresourceCount;
        void* buffer = ::operator new(copySize, std::nothrow);
        if (!buffer)
            return 1;

        for (;;)
        {
            source.read(buffer, copySize);
            destination.write(buffer, copySize);
            ::operator delete(buffer);
            if (goResourceNext(source, typ) != 0)
                break;
            copySize = source.m_sectionSize;
            destination.m_currentSubresourceCount += source.m_currentSubresourceCount;
            buffer = ::operator new(copySize, std::nothrow);
            if (!buffer)
                return 1;
        }
        --destination.m_currentSubresourceCount;
        endResourceSection(destination);
        return 0;
    }

    void destroyResourceOwner(RESOURCE& resource)
    {

        closeResourceOwner(resource);
        resource.m_name.ReleaseOwnedStorage();
    }

    bool RESOURCE::openFile(const STRING& name, ResTypes::Type res_type)
    {
        return openResourceFileForRead(*this, name, res_type) == 0;
    }

    bool RESOURCE::openFileForWrite(const STRING& name, ResTypes::Type res_type)
    {
        return openResourceFileForWrite(*this, name, res_type) == 0;
    }

    void RESOURCE::clear()
    {
        closeResourceOwner(*this);
    }

    int RESOURCE::GoBegin(ResTypes::Type typ)
    {
        return goResourceBegin(*this, typ);
    }

    int RESOURCE::GoNext(ResTypes::Type typ)
    {
        return goResourceNext(*this, typ);
    }

    int RESOURCE::GoBeginSub(ResTypes::Type typ)
    {
        return goResourceBegin(*this, typ);
    }

    int RESOURCE::GoNextSub(ResTypes::Type typ)
    {
        return goResourceNextSubresource(*this, typ);
    }

    int RESOURCE::GetNoSubRes(ResTypes::Type typ)
    {
        return countResourceSubresources(*this, typ);
    }

    int RESOURCE::GetBytesToEndSub() const
    {
        if (!m_file)
            return 0;
        const long pos = std::ftell(m_file);
        const std::uint32_t end = m_currentSubresourcePosition + 4u + m_currentSubresourceSize;
        return pos >= 0 && static_cast<std::uint32_t>(pos) < end ? static_cast<int>(end - static_cast<std::uint32_t>(pos)) : 0;
    }

    int RESOURCE::ReadPayload(void* data, unsigned size, Filter* filter)
    {
        return readResourcePayload(*this, data, size, filter);
    }

    int RESOURCE::WritePayload(const void* data, unsigned size, Filter* filter)
    {
        return writeResourcePayload(*this, data, size, filter);
    }

    int RESOURCE::SubLoad(void** data, Filter* packer)
    {
        return loadResourceSubresource(*this, data, packer);
    }

    int RESOURCE::CopySectionTypeFrom(RESOURCE& source, ResTypes::Type typ, Filter* filter)
    {
        (void)filter;
        return copyResourceSection(*this, source, typ);
    }

    int RESOURCE::BeginSection(ResTypes::Type typ, std::uint32_t options, std::uint32_t packedDiff)
    {
        const int result = beginResourceSection(*this, typ, options);
        if (result == 0)
            m_packedDiff = packedDiff;
        return result;
    }

    int RESOURCE::EndSection()
    {
        return endResourceSection(*this);
    }

    size_t RESOURCE::seek(size_t pos)
    {
        if (!m_file)
            return 0;
        m_state = st_seek;
        std::fseek(m_file, static_cast<long>(pos), SEEK_SET);
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    size_t RESOURCE::shift(int delta)
    {
        if (!m_file)
            return 0;
        m_state = st_seek;
        std::fseek(m_file, delta, SEEK_CUR);
        return position();
    }

    size_t RESOURCE::position() const
    {
        if (!m_file)
            return 0;
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    void RESOURCE::close()
    {
        closeResourceOwner(*this);
    }

    int RESOURCE::read(void* buf, unsigned size)
    {
        unsigned missing = size;
        if (m_file && size)
        {
            if (m_state == st_write)
            {
                m_state = st_seek;
                std::fseek(m_file, 0, SEEK_CUR);
            }
            m_state = st_read;
            missing -= static_cast<unsigned>(std::fread(buf, 1, size, m_file));
        }
        return static_cast<int>(missing);
    }

    int RESOURCE::write(const void* buf, unsigned size)
    {
        unsigned missing = size;
        if (m_file && size)
        {
            if (m_state == st_read)
            {
                m_state = st_seek;
                std::fseek(m_file, 0, SEEK_CUR);
            }
            m_state = st_write;
            missing -= static_cast<unsigned>(std::fwrite(buf, 1, size, m_file));
        }
        return static_cast<int>(missing);
    }
}
