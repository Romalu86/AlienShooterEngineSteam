#include "picture.h"

#include "core/log.h"
#include "core/file_logger.h"
#include "core/crc32.h"
#include "script/logic_runtime.h"
#include "core/resource.h"
#include "graphics/angle.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <new>
#include <string>
#include <vector>
#include <memory>
#ifdef _WIN32
#include <ddraw.h>
#if defined(_M_IX86)
                                                                                                            
                                                                                                          
#endif
#endif

namespace as1 { namespace images
{
    namespace
    {
        inline int clampRetailByteInt(int value) noexcept
        {
            return value < 0 ? 0 : (value > 255 ? 255 : value);
        }
    }
                                                                                                                                                          
    namespace
    {
#ifdef _WIN32

        IDirectDraw* g_pictureDirectDraw = nullptr;
#endif
        struct TgaHeader
        {
            BYTE idLength;
            BYTE colorMapType;
            BYTE imageType;
            BYTE colorMapFirst[2];
            BYTE colorMapLength[2];
            BYTE colorMapDepth;
            BYTE xOrigin[2];
            BYTE yOrigin[2];
            WORD width;
            WORD height;
            BYTE pixelDepth;
            BYTE imageDescriptor;
        };

                                                                                       

        void writeLittleWord(BYTE (&dst)[2], WORD value)
        {
            dst[0] = static_cast<BYTE>(value & 0xFFu);
            dst[1] = static_cast<BYTE>((value >> 8u) & 0xFFu);
        }

        WORD readLe16(const BYTE* p)
        {
            return static_cast<WORD>(p[0] | (static_cast<WORD>(p[1]) << 8u));
        }

        DWORD readLe32(const BYTE* p)
        {
            return static_cast<DWORD>(p[0]) |
                   (static_cast<DWORD>(p[1]) << 8u) |
                   (static_cast<DWORD>(p[2]) << 16u) |
                   (static_cast<DWORD>(p[3]) << 24u);
        }

        void writeLe16ToBytes(BYTE* dst, WORD value)
        {
            dst[0] = static_cast<BYTE>(value & 0xFFu);
            dst[1] = static_cast<BYTE>((value >> 8u) & 0xFFu);
        }

        int nextPowerOfTwoAtLeast32(int value)
        {
            int out = 0x20;
            while (out < value)
                out <<= 1;
            return out;
        }

        bool rectanglesOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
        {

            const int aCenterX = (aw + 2 * ax) / 2;
            const int bCenterX = (bw + 2 * bx) / 2;
            if (std::abs(bCenterX - aCenterX) >= (bw + aw) / 2)
                return false;

            const int aCenterY = (ah + 2 * ay) / 2;
            const int bCenterY = (bh + 2 * by) / 2;
            return std::abs(bCenterY - aCenterY) < (bh + ah) / 2;
        }

        int signedModulo256(int value)
        {

            int v = value & static_cast<int>(0x800000FFu);
            if (v < 0)
            {
                --v;
                v |= static_cast<int>(0xFFFFFF00u);
                ++v;
            }
            return v;
        }

        int signedModulo16(int value)
        {

            int v = value & static_cast<int>(0x8000000Fu);
            if (v < 0)
            {
                --v;
                v |= static_cast<int>(0xFFFFFFF0u);
                ++v;
            }
            return v;
        }

        int signedDivide16(int value)
        {

            const int bias = value < 0 ? 0x0F : 0;
            return (value + bias) >> 4;
        }

        int signedDivide16MinusOne(int value)
        {

            return signedDivide16(value) - 1;
        }

        int addWrap32(int a, int b) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        int multiplyWrap32(int a, int b) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
        }

        void writeU16Resource(as1::RESOURCE& resource, WORD value)
        {
            resource.write_new(&value, sizeof(value));
        }

        void writeU32Resource(as1::RESOURCE& resource, DWORD value)
        {
            resource.write_new(&value, sizeof(value));
        }

        void appendByte(std::vector<BYTE>& dst, std::size_t& pos, BYTE value)
        {
            // write through raw cursors without bounds/failure branches.
            dst[pos++] = value;
        }

        void appendWord(std::vector<BYTE>& dst, std::size_t& pos, WORD value)
        {
            std::memcpy(dst.data() + pos, &value, sizeof(value));
            pos += 2u;
        }

        void appendDword(std::vector<BYTE>& dst, std::size_t& pos, DWORD value)
        {
            std::memcpy(dst.data() + pos, &value, sizeof(value));
            pos += 4u;
        }

        void overwriteDword(std::vector<BYTE>& dst, std::size_t pos, DWORD value)
        {
            std::memcpy(dst.data() + pos, &value, sizeof(value));
        }

        void appendByte(BYTE* dst, std::size_t& pos, BYTE value)
        {
            dst[pos++] = value;
        }

        void appendWord(BYTE* dst, std::size_t& pos, WORD value)
        {
            std::memcpy(dst + pos, &value, sizeof(value));
            pos += 2u;
        }

        void appendDword(BYTE* dst, std::size_t& pos, DWORD value)
        {
            std::memcpy(dst + pos, &value, sizeof(value));
            pos += 4u;
        }

        void overwriteDword(BYTE* dst, std::size_t pos, DWORD value)
        {
            std::memcpy(dst + pos, &value, sizeof(value));
        }

        WORD scratchWordAt(const std::vector<WORD>& scratch, int stride, int minX, int minY, int x, int y)
        {
            const int sx = x - minX;
            const int sy = y - minY;
            if (stride <= 0 || sx < 0 || sy < 0)
                return 0;
            const std::size_t index = static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(sx);
            if (index >= scratch.size())
                return 0;
            return scratch[index];
        }

        WORD scratchWordAt(const WORD* scratch, int stride, int minX, int minY, int x, int y)
        {
            return scratch[(y - minY) * stride + (x - minX)];
        }

        WORD rgb888To565(DWORD value)
        {

            return static_cast<WORD>(((value & 0xFFu) >> 3u) |
                                     ((value >> 6u) & 0x03E0u) |
                                     ((value >> 9u) & 0x7C00u));
        }

        DWORD rgb565To888Approx(WORD value)
        {

            DWORD expandedColor = (static_cast<DWORD>(value) & 0x1Fu) |
                        (8u * ((static_cast<DWORD>(value) & 0x03FCu) |
                               (8u * (static_cast<DWORD>(value) & 0x7F80u))));
            return expandedColor << 3u;
        }

        BYTE clampByteFromInt(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return static_cast<BYTE>(value);
        }

        bool containsCaseLiteral(const char* text, const char* lower, const char* upper)
        {
            text = text ? text : "";
            return std::strstr(text, lower) != nullptr || std::strstr(text, upper) != nullptr;
        }

        FILE* openBinaryRead(const char* filename)
        {
            if (!filename || !*filename)
                return nullptr;
            return std::fopen(filename, "rb");
        }

        bool fileExistsReadable(const char* filename)
        {
            FILE* f = openBinaryRead(filename);
            if (!f)
                return false;
            std::fclose(f);
            return true;
        }

        STRING incrementTrailingNumberBeforeExtension(const STRING& input, int delta)
        {
            return input.IncrementTrailingNumber(delta);
        }

        bool finalCharBeforeExtensionIsDigit(const STRING& input, const char* extension)
        {

            const STRING prefix = input.LeftOfFirst(extension);
            if (prefix.isEmpty())
                return false;
            const char* const text = prefix.c_str();
            return std::isdigit(static_cast<unsigned char>(text[prefix.Length() - 1])) != 0;
        }


        STRING numberedFrameName(const STRING& baseName, int frameIndex)
        {
            return baseName.IncrementTrailingNumber(frameIndex);
        }

        bool reopenPictureFrame(FILE*& file, const STRING& baseName, int frameIndex)
        {
            if (file)
            {
                std::fclose(file);
                file = nullptr;
            }
            STRING frameName = numberedFrameName(baseName, frameIndex);
            const char* text = frameName.c_str();
            file = (text && text[0]) ? std::fopen(text, "rb") : nullptr;
            return file != nullptr;
        }

        void copyPixelBytes(BYTE* dst, const BYTE* src, int bytesPerPixel)
        {
            switch (bytesPerPixel)
            {
            case 4:
                std::memcpy(dst, src, 4u);
                break;
            case 3:
                std::memcpy(dst, src, 3u);
                break;
            case 2:
                std::memcpy(dst, src, 2u);
                break;
            case 1:
                *dst = *src;
                break;
            default:
                break;
            }
        }

        bool readExact(FILE* file, void* dst, std::size_t bytes)
        {
            if (!file || !dst)
                return false;
            return bytes == 0 || std::fread(dst, bytes, 1u, file) == 1u;
        }

        BYTE clampFlicPaletteByte(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return static_cast<BYTE>(value);
        }

        DWORD makeFlicPaletteDword(BYTE r, BYTE g, BYTE b)
        {
            return 0xFF000000u |
                   (static_cast<DWORD>(r) << 16u) |
                   (static_cast<DWORD>(g) << 8u) |
                   static_cast<DWORD>(b);
        }

        WORD readChunkWord(const BYTE*& p)
        {
            WORD v = readLe16(p);
            p += 2;
            return v;
        }

        std::int16_t readChunkSignedWord(const BYTE*& p)
        {
            const WORD v = readChunkWord(p);
            return static_cast<std::int16_t>(v);
        }
        int countNumberedSiblings(const STRING& firstPath, const char* extension)
        {

            int count = 1;
            if (!finalCharBeforeExtensionIsDigit(firstPath, extension))
                return count;

            while (true)
            {
                STRING next = incrementTrailingNumberBeforeExtension(firstPath, count);
                if (next.isEmpty() || !fileExistsReadable(next.c_str()))
                    break;
                ++count;
            }
            return count;
        }

        void deletePictureThroughRetailSlot00(PICTURE* picture) noexcept
        {
            if (!picture)
                return;
            delete picture;
        }
    }

    PICTURE::PICTURE()
    {

        resetDefaults();
    }

    PICTURE::PICTURE(int width, int height, int bytesPerPixel)
    {

        resetDefaults();
        m_frameCount = 1;
        allocateBuffer(width, height, bytesPerPixel);
    }

    PICTURE::~PICTURE()
    {

        release();
    }

    PICTURE* deletePictureOwner(PICTURE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~PICTURE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    void PICTURE::resetDefaults()
    {
        m_frameCount = 0;
        m_currentFrame = 0;
        m_type = 0x47;
        m_width = 0;
        m_height = 0;
        m_bytesPerPixel = 0;
        m_name = "";
        m_file = nullptr;
        m_buffer = nullptr;

    }

    std::size_t PICTURE::bufferSize() const
    {
        if (m_width <= 0 || m_height <= 0 || m_bytesPerPixel <= 0)
            return 0;
        return static_cast<std::size_t>(m_width) *
               static_cast<std::size_t>(m_height) *
               static_cast<std::size_t>(m_bytesPerPixel);
    }

    bool PICTURE::isInside(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < m_width && y < m_height;
    }

    void PICTURE::reportPictureError(int errorCode, const char* detailText, int detailValue) const
    {
        LOG::ResourceError("PICTURE '%s'", errorCode, detailText, detailValue, m_name.c_str());
    }

    FILE* PICTURE::reopenCurrentName()
    {
        if (m_file)
            std::fclose(m_file);
        m_file = openBinaryRead(m_name.c_str());
        return m_file;
    }

    bool PICTURE::seekDataOffset()
    {
        return m_file && std::fseek(m_file, m_dataOffset, SEEK_SET) == 0;
    }

    void PICTURE::lowerCaseStoredName()
    {
        std::string lowered = m_name.str();
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        m_name = lowered.c_str();
    }

    std::intptr_t PICTURE::allocatePicturePixels(int width, int height, int bytesPerPixel) noexcept
    {

        m_width = width;
        m_height = height;
        m_bytesPerPixel = bytesPerPixel;

        if (m_buffer)
            ::operator delete(static_cast<void*>(m_buffer));

        std::uint32_t byteCount = static_cast<std::uint32_t>(m_height);
        byteCount *= static_cast<std::uint32_t>(m_bytesPerPixel);
        byteCount *= static_cast<std::uint32_t>(m_width);

        m_buffer = static_cast<BYTE*>(::operator new(static_cast<std::size_t>(byteCount), std::nothrow));
        if (m_buffer)
        {
            std::memset(m_buffer, 0, static_cast<std::size_t>(byteCount));
            return 0;
        }

        return logFileLoggerResourceError(g_fileLogger, "PICTURE '%s'", 2, "picture buffer",
                           static_cast<int>(byteCount), m_name.c_str());
    }

    void PICTURE::allocateBuffer(int width, int height, int bytesPerPixel)
    {
        (void)allocatePicturePixels(width, height, bytesPerPixel);
    }

    int PICTURE::loadNextFrame()
    {

        int result = m_frameCount;
        if (result)
        {
            const int next = m_currentFrame + 1;
            m_currentFrame = next;
            if (next >= result)
                result = reloadFirstFrame();
        }
        return result;
    }

    int PICTURE::reloadFirstFrame()
    {

        int result = m_buffer ? 1 : 0;
        if (result)
        {
            if (m_file)
                std::fseek(m_file, m_dataOffset, SEEK_SET);
            m_currentFrame = -1;
            result = loadNextFrame();
        }
        return result;
    }

    int PICTURE::openFilename(const STRING& path)
    {

        release();
        if (path.isEmpty())
        {
            reportPictureError(4, "filename", 0);
            return 1;
        }

        m_name = path;
        lowerCaseStoredName();
        m_file = openBinaryRead(path.c_str());
        if (m_file)
            return 0;

        reportPictureError(7, "", 0);
        return 1;
    }

    int PICTURE::release()
    {

        if (m_buffer)
            ::operator delete(static_cast<void*>(m_buffer));
        m_buffer = nullptr;

        int result = 0;
        if (m_file)
            result = std::fclose(m_file);
        m_file = nullptr;

        m_frameCount = 0;
        m_width = 0;
        m_height = 0;
        return result;
    }

    DWORD readPicturePixelRaw(const PICTURE* owner, int x, int y) noexcept
    {

        if (x < 0 || y < 0 || x >= owner->width() || y >= owner->height())
            return 0u;
        const std::uint32_t index = static_cast<std::uint32_t>(x) +
            static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(owner->width());
        const BYTE* const buffer = owner->buffer();
        switch (owner->bytesPerPixel())
        {
        case 4:
        {
            DWORD value = 0;
            std::memcpy(&value, buffer + index * 4u, sizeof(value));
            return value;
        }
        case 3:
        {
            DWORD value = 0;
            std::memcpy(&value, buffer + index * 3u, sizeof(value));
            return value & 0x00FFFFFFu;
        }
        case 2:
        {
            WORD value = 0;
            std::memcpy(&value, buffer + index * 2u, sizeof(value));
            return static_cast<DWORD>(value);
        }
        case 1:
            return static_cast<DWORD>(buffer[index]);
        default:
            return 0u;
        }
    }

    DWORD PICTURE::getPixelRaw(int x, int y) const
    {
        return readPicturePixelRaw(this, x, y);
    }

    namespace
    {
        inline void retailWrite24PreserveHighByte(BYTE* destination, DWORD value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            DWORD* const slot = reinterpret_cast<DWORD*>(destination);
            *slot &= 0xFF000000u;
            *slot |= value & 0x00FFFFFFu;
#else
            // Portable fallback: preserve the same final four bytes without
            // requiring aligned access.
            DWORD existing = 0;
            std::memcpy(&existing, destination, sizeof(existing));
            existing &= 0xFF000000u;
            std::memcpy(destination, &existing, sizeof(existing));
            existing |= value & 0x00FFFFFFu;
            std::memcpy(destination, &existing, sizeof(existing));
#endif
        }
    }

    void PICTURE::writePicturePixelRaw(int x, int y, DWORD value) noexcept
    {

        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
            return;

        const std::uint32_t index = static_cast<std::uint32_t>(x) +
            static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(m_width);
        switch (m_bytesPerPixel)
        {
        case 4:
        {
            const std::uint32_t offset = index * 4u;
            std::memcpy(m_buffer + offset, &value, sizeof(value));
            return;
        }
        case 3:
        {
            const std::uint32_t offset = index * 3u;
            retailWrite24PreserveHighByte(m_buffer + offset, value);
            return;
        }
        case 2:
        {
            const std::uint32_t offset = index * 2u;
            const WORD word = static_cast<WORD>(value);
            std::memcpy(m_buffer + offset, &word, sizeof(word));
            return;
        }
        case 1:
            m_buffer[index] = static_cast<BYTE>(value);
            return;
        default:
            return;
        }
    }

    void PICTURE::setPixelRaw(int x, int y, DWORD value)
    {
        writePicturePixelRaw(x, y, value);
    }

    void PICTURE::writePicturePixelColor(int x, int y, DWORD value) noexcept
    {

        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
            return;

        const std::uint32_t index = static_cast<std::uint32_t>(x) +
            static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(m_width);
        switch (m_bytesPerPixel)
        {
        case 4:
        {
            const std::uint32_t offset = index * 4u;
            std::memcpy(m_buffer + offset, &value, sizeof(value));
            return;
        }
        case 3:
        {
            const std::uint32_t offset = index * 3u;
            retailWrite24PreserveHighByte(m_buffer + offset, value);
            return;
        }
        case 2:
        {
            const std::uint32_t offset = index * 2u;
            const WORD word = rgb888To565(value);
            std::memcpy(m_buffer + offset, &word, sizeof(word));
            return;
        }
        case 1:
            m_buffer[index] = static_cast<BYTE>(value);
            return;
        default:
            return;
        }
    }

    void PICTURE::setPixelRgb565Aware(int x, int y, DWORD value)
    {
        writePicturePixelColor(x, y, value);
    }

    DWORD* readPicturePixelColor(const PICTURE* owner, DWORD* destination, int x, int y) noexcept
    {

        if (x < 0 || y < 0 || x >= owner->width() || y >= owner->height())
        {
            *destination = 0xFF000000u;
            return destination;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(x) +
            static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(owner->width());
        const BYTE* const buffer = owner->buffer();
        switch (owner->bytesPerPixel())
        {
        case 1:
            *destination = owner->palette()[buffer[index]];
            return destination;
        case 4:
            std::memcpy(destination, buffer + index * 4u, sizeof(*destination));
            return destination;
        case 3:
        {
            DWORD value = 0;
            std::memcpy(&value, buffer + index * 3u, sizeof(value));
            *destination = value & 0x00FFFFFFu;
            return destination;
        }
        case 2:
        {
            WORD value = 0;
            std::memcpy(&value, buffer + index * 2u, sizeof(value));
            DWORD expanded = static_cast<DWORD>(value);
            DWORD expandedColor = (expanded & 0x7F80u) << 3u;
            expandedColor |= (expanded & 0x03FCu);
            expandedColor <<= 3u;
            expandedColor |= (expanded & 0x001Fu);
            expandedColor <<= 3u;
            *destination = expandedColor;
            return destination;
        }
        default:
            *destination = 0xFF000000u;
            return destination;
        }
    }

    DWORD PICTURE::getPixelForTGA(int x, int y) const
    {
        DWORD out = 0;
        readPicturePixelColor(this, &out, x, y);
        return out;
    }

    int PICTURE::saveTGA(const STRING& path, int x, int y, int width, int height) const
    {

        if (!m_buffer)
        {
            return static_cast<int>(logFileLoggerResourceError(g_fileLogger, "PICTURE '%s'", 10,
                                                "SaveTGA-not picture", 0, m_name.c_str()));
        }

        if (height == -1)
            height = m_height;
        if (width == -1)
            width = m_width;

        const int xEnd = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(width));
        const int yEnd = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(height));
        if (xEnd > m_width || yEnd > m_height)
        {
            return static_cast<int>(logFileLoggerResourceError(g_fileLogger, "PICTURE '%s'", 4,
                                                "size in SaveTGA", 0, m_name.c_str()));
        }

        const char* const filename = path.c_str();
        std::FILE* out = nullptr;
        if (filename[0] != '\0')
            out = std::fopen(filename, "wb");
        if (!out)
        {
            return static_cast<int>(logFileLoggerResourceError(g_fileLogger, "PICTURE '%s'", 7,
                                                filename, 0, m_name.c_str()));
        }

        TgaHeader header{};
        header.imageType = 2;
        header.width = static_cast<WORD>(width);
        header.height = static_cast<WORD>(height);
        header.pixelDepth = (m_bytesPerPixel == 4) ? 32 : 24;
        header.imageDescriptor = 1;
        std::fwrite(&header, 0x12u, 1u, out);

        int rowCount = height;
        int row = static_cast<std::int32_t>(static_cast<std::uint32_t>(height) - 1u);
        if (row >= 0)
        {
            row = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(row) + static_cast<std::uint32_t>(y));
            do
            {
                if (width > 0)
                {
                    int col = x;
                    int remaining = width;
                    do
                    {
                        DWORD pixel = 0;
                        readPicturePixelColor(this, &pixel, col, row);
                        if (m_bytesPerPixel == 4)
                        {
                            std::fwrite(&pixel, 1u, 4u, out);
                        }
                        else
                        {
                            const DWORD rgb = pixel & 0x00FFFFFFu;
                            std::fwrite(&rgb, 1u, 3u, out);
                        }
                        col = static_cast<std::int32_t>(static_cast<std::uint32_t>(col) + 1u);
                        --remaining;
                    }
                    while (remaining != 0);
                }
                row = static_cast<std::int32_t>(static_cast<std::uint32_t>(row) - 1u);
                --rowCount;
            }
            while (rowCount != 0);
        }

        return std::fclose(out);
    }

    int PICTURE_TGA::readAndValidateHeader(void* rawHeader)
    {

        BYTE* h = static_cast<BYTE*>(rawHeader);
        if (!m_file)
        {
            reportPictureError(7, "tga cadr", m_currentFrame);
            return 1;
        }

        std::fread(h, 18u, 1u, m_file);
        m_tgaImageType = h[2];
        m_dataOffset = static_cast<int>(h[0]) + 18;

        if (!m_tgaImageType)
        {
            release();
            reportPictureError(10, "not image in tga", 0);
            return 1;
        }
        if (h[1] == 1)
        {
            release();
            reportPictureError(10, "not supported ColorMapType in tga", 0);
            return 1;
        }
        if ((m_tgaImageType & 3u) == 1u)
        {
            release();
            reportPictureError(10, "not supported ColorMap in tga", 0);
            return 1;
        }

        const int headerWidth = readLe16(h + 12);
        const int headerHeight = readLe16(h + 14);
        const int headerBpp = static_cast<int>(h[16]) >> 3;
        if (m_buffer && (m_width != headerWidth || m_height != headerHeight || m_bytesPerPixel != headerBpp))
        {
            reportPictureError(10, "TGA parameters different from first cadr", m_currentFrame);
            return 1;
        }
        return 0;
    }

    int PICTURE_TGA::openFilename(const STRING& path)
    {

        BYTE header[18];
        if (PICTURE::openFilename(path))
            return 1;
        if (readAndValidateHeader(header))
            return 1;

        m_frameCount = countNumberedSiblings(m_name, ".tga");
        allocateBuffer(readLe16(header + 12), readLe16(header + 14), static_cast<int>(header[16]) >> 3);
        reloadFirstFrame();
        return 0;
    }

    int PICTURE_TGA::reloadFirstFrame()
    {

        if (!m_buffer)
            return 0;
        reopenCurrentName();
        BYTE header[18];
        int result = readAndValidateHeader(header);
        if (!result)
        {
            std::fseek(m_file, m_dataOffset, 0);
            m_currentFrame = -1;
            result = loadNextFrame();
        }
        return result;
    }

    int PICTURE_TGA::loadNextFrame()
    {

        if (!m_buffer)
        {
            reportPictureError(10, "Picture has not opened", m_currentFrame);
            return 0;
        }

        const int next = m_currentFrame + 1;
        m_currentFrame = next;
        if (next >= m_frameCount)
            return reloadFirstFrame();

        reopenPictureFrame(m_file, m_name, m_currentFrame);

        BYTE header[18];
        int result = readAndValidateHeader(header);
        if (result)
            return result;

        std::fseek(m_file, m_dataOffset, SEEK_SET);

        const std::size_t rowBytes = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_bytesPerPixel);
        const std::size_t fullBytes = rowBytes * static_cast<std::size_t>(m_height);

        if (m_tgaImageType & 8u)
        {

            BYTE* encoded = static_cast<BYTE*>(::operator new(fullBytes, std::nothrow));
            if (encoded)
            {
                std::fread(encoded, fullBytes, 1u, m_file);
                std::size_t src = 0;
                for (int row = m_height - 1; row >= 0; --row)
                {
                    int x = 0;
                    while (x < m_width)
                    {
                        const BYTE marker = encoded[src++];
                        const bool rlePacket = (marker & 0x80u) != 0;
                        int count = static_cast<int>(marker & 0x7Fu) + 1;
                        do
                        {
                            BYTE* dst = m_buffer +
                                (static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) +
                                 static_cast<std::size_t>(x)) * static_cast<std::size_t>(m_bytesPerPixel);
                            copyPixelBytes(dst, encoded + src, m_bytesPerPixel);
                            if (!rlePacket)
                                src += static_cast<std::size_t>(m_bytesPerPixel);
                            ++x;
                        }
                        while (--count > 0);

                        if (rlePacket)
                            src += static_cast<std::size_t>(m_bytesPerPixel);
                    }
                }
                ::operator delete(encoded);
            }
            else
            {
                reportPictureError(2, "cadr2", m_currentFrame);
            }
            return 0;
        }

        for (int row = m_height - 1; row >= 0; --row)
        {
            BYTE* dst = m_buffer + static_cast<std::size_t>(row) * rowBytes;
            std::fread(dst, rowBytes, 1u, m_file);
        }
        return 0;
    }

    int PICTURE_Z::openFilename(const STRING& path)
    {

        if (PICTURE::openFilename(path))
            return 1;

        BYTE header[16];
        std::fread(header, 16u, 1u, m_file);
        if (readLe32(header) != 0x6675425Au)
        {
            release();
            reportPictureError(4, "Z format file", 0);
            return 1;
        }

        m_frameCount = static_cast<int>(readLe32(header + 12));
        m_dataOffset = 16;
        m_type = 0x47;
        allocateBuffer(static_cast<int>(readLe32(header + 4)), static_cast<int>(readLe32(header + 8)), 2);
        reloadFirstFrame();
        return 0;
    }

    int PICTURE_Z::loadNextFrame()
    {

        if (!m_buffer)
        {
            reportPictureError(10, "Picture has not opened", m_currentFrame);
            return 0;
        }
        const int next = m_currentFrame + 1;
        m_currentFrame = next;
        if (next >= m_frameCount)
            return reloadFirstFrame();

        for (int row = 0; row < m_height; ++row)
        {
            int x = 0;
            while (x < m_width)
            {
                WORD code = 0;
                std::fread(&code, 2u, 1u, m_file);
                const int count = static_cast<int>(code & 0x7FFFu);
                BYTE* dst = m_buffer + (static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)) * 2u;
                if (code & 0x8000u)
                {

                    for (int i = 0; i < count; ++i)
                    {
                        const WORD fill = 0x8000u;
                        std::memcpy(dst + static_cast<std::size_t>(i) * 2u, &fill, 2u);
                    }
                }
                else
                {
                    std::fread(dst, 2u, static_cast<std::size_t>(count), m_file);
                }
                x += count;
            }
        }
        return 0;
    }

    int PICTURE_FLC::openFilename(const STRING& path)
    {

        if (PICTURE::openFilename(path))
            return 1;

        BYTE header[0x80];
        std::fread(header, 0x80u, 1u, m_file);
        const WORD magic = readLe16(header + 4);
        const int frameCount = readLe16(header + 6);
        const int width = readLe16(header + 8);
        const int height = readLe16(header + 10);
        const DWORD speedRaw = readLe32(header + 16);
        m_flicMagic = magic;
        m_frameCount = frameCount;

        if (magic == 0xAF11u)
        {
            m_dataOffset = 128;

            m_type = static_cast<int>((static_cast<std::uint32_t>(speedRaw) * 1000u) / 0x46u);
        }
        else if (magic == 0xAF12u)
        {
            m_dataOffset = static_cast<int>(readLe32(header + 0x50));
            m_type = static_cast<int>(speedRaw);
        }
        else
        {
            release();
            reportPictureError(4, "flic type", 0);
            return 1;
        }

        allocateBuffer(width, height, 1);
        reloadFirstFrame();
        return 0;
    }

    int PICTURE_FLC::loadNextFrame()
    {

        if (!m_buffer)
        {
            reportPictureError(10, "Picture has not opened", m_currentFrame);
            return 0;
        }

        ++m_currentFrame;
        if (m_currentFrame >= m_frameCount)
            return reloadFirstFrame();

        DWORD frameSize = 0;
        std::fread(&frameSize, 4u, 1u, m_file);
        BYTE* frame = static_cast<BYTE*>(::operator new(static_cast<std::size_t>(frameSize), std::nothrow));
        if (!frame)
        {
            reportPictureError(2, "cadr", m_currentFrame);
            return 0;
        }
        std::fread(frame, static_cast<std::size_t>(frameSize - 4u), 1u, m_file);

        if (readLe16(frame) != 0xF1FAu)
        {
            ::operator delete(frame);
            reportPictureError(4, "chunk mark", m_currentFrame);
            return 0;
        }

        const WORD chunkCount = readLe16(frame + 2u);
        DWORD chunkOffset = 12u;
        for (WORD chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
        {
            BYTE* chunk = frame + chunkOffset;
            const DWORD nextChunkOffset = chunkOffset + readLe32(chunk);
            const BYTE chunkType = chunk[4];
            BYTE* p = chunk + 6u;

            switch (chunkType)
            {
            case 4:
            case 0x0B:
            {
                int packetCount = static_cast<int>(readLe16(p));
                p += 2;
                int paletteIndex = 0;
                while (packetCount-- > 0)
                {
                    paletteIndex += static_cast<int>(*p++);
                    int run = static_cast<int>(*p++);
                    if (run == 0)
                        run = 256;
                    while (run-- > 0)
                    {
                        int r = static_cast<int>(p[0]);
                        int g = static_cast<int>(p[1]);
                        int b = static_cast<int>(p[2]);
                        p += 3;
                        if (chunkType == 0x0B)
                        {
                            r *= 4;
                            g *= 4;
                            b *= 4;
                        }
                        m_palette[static_cast<std::size_t>(paletteIndex)] =
                            makeFlicPaletteDword(clampFlicPaletteByte(r), clampFlicPaletteByte(g), clampFlicPaletteByte(b));
                        ++paletteIndex;
                    }
                }
                break;
            }
            case 7:
            {
                int offset = 0;
                int lineCount = static_cast<int>(readLe16(p));
                p += 2;
                int lineIndex = 0;
                while (lineIndex < lineCount)
                {
                    std::int16_t op = 0;
                    for (;;)
                    {
                        op = static_cast<std::int16_t>(readLe16(p));
                        p += 2;
                        const WORD top = static_cast<WORD>(op) & 0xC000u;
                        if (top == 0xC000u)
                        {
                            offset -= static_cast<int>(op) * m_width;
                            lineCount -= static_cast<int>(op);
                            lineIndex -= static_cast<int>(op);
                            continue;
                        }
                        if (top != 0x8000u)
                            break;
                        m_buffer[offset + m_width - 1] = static_cast<BYTE>(op);
                    }

                    const int lineBase = offset;
                    if ((static_cast<WORD>(op) & 0xC000u) == 0u)
                    {
                        int packets = static_cast<int>(op);
                        while (packets-- > 0)
                        {
                            offset += static_cast<int>(*p++);
                            const std::int8_t count = static_cast<std::int8_t>(*p++);
                            if (count >= 0)
                            {
                                const int bytes = 2 * static_cast<int>(count);
                                std::memcpy(m_buffer + offset, p, static_cast<std::size_t>(bytes));
                                p += bytes;
                                offset += bytes;
                            }
                            else
                            {
                                int repeats = -static_cast<int>(count);
                                const WORD value = readLe16(p);
                                p += 2;
                                while (repeats-- > 0)
                                {
                                    std::memcpy(m_buffer + offset, &value, sizeof(value));
                                    offset += 2;
                                }
                            }
                        }
                    }
                    offset = lineBase + m_width;
                    ++lineIndex;
                }
                break;
            }
            case 0x0C:
            {
                int line = static_cast<int>(readLe16(p));
                const int endLine = line + static_cast<int>(readLe16(p + 2));
                p += 4;
                while (line < endLine)
                {
                    int x = 0;
                    int packets = static_cast<int>(*p++);
                    while (packets-- > 0)
                    {
                        x += static_cast<int>(*p++);
                        const std::int8_t count = static_cast<std::int8_t>(*p++);
                        if (count >= 0)
                        {
                            const int bytes = static_cast<int>(count);
                            std::memcpy(m_buffer + x + line * m_width, p, static_cast<std::size_t>(bytes));
                            p += bytes;
                            x += bytes;
                        }
                        else
                        {
                            const BYTE value = *p++;
                            int repeats = -static_cast<int>(count);
                            std::memset(m_buffer + x + line * m_width, value, static_cast<std::size_t>(repeats));
                            x += repeats;
                        }
                    }
                    ++line;
                }
                break;
            }
            case 0x0D:
                std::memset(m_buffer, 0, static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
                break;
            case 0x0F:
            {
                if (m_flicMagic == 0xAF11u)
                {
                    for (int row = 0; row < m_height; ++row)
                    {
                        int x = 0;
                        int packets = static_cast<int>(*p++);
                        while (packets-- > 0)
                        {
                            const std::int8_t count = static_cast<std::int8_t>(*p);
                            if (count >= 0)
                            {
                                const int endX = x + static_cast<int>(*p++);
                                while (x < endX)
                                    m_buffer[x++ + m_width * row] = *p;
                            }
                            else
                            {
                                const int endX = x - static_cast<int>(*p);
                                while (x < endX)
                                {
                                    m_buffer[x++ + m_width * row] = p[1];
                                    ++p;
                                }
                            }
                            ++p;
                        }
                    }
                }
                else
                {
                    int outputOffset = 0;
                    for (int row = 0; row < m_height; ++row)
                    {
                        ++p;
                        int produced = 0;
                        do
                        {
                            const std::int8_t count = static_cast<std::int8_t>(*p++);
                            if (count >= 0)
                            {
                                const BYTE value = *p++;
                                std::memset(m_buffer + outputOffset, value, static_cast<std::size_t>(count));
                                outputOffset += static_cast<int>(count);
                                produced += static_cast<int>(count);
                            }
                            else
                            {
                                const int bytes = -static_cast<int>(count);
                                std::memcpy(m_buffer + outputOffset, p, static_cast<std::size_t>(bytes));
                                p += bytes;
                                outputOffset += bytes;
                                produced += bytes;
                            }
                        }
                        while (produced < m_width);
                    }
                }
                break;
            }
            case 0x10:
                std::memcpy(m_buffer, p, static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
                break;
            default:
                break;
            }

            chunkOffset = nextChunkOffset;
        }

        ::operator delete(frame);
        return 0;
    }

    int PICTURE_BMP::readAndValidateHeader(void* rawHeader)
    {

        BYTE* h = static_cast<BYTE*>(rawHeader);
        if (!m_file)
        {
            reportPictureError(7, "bmp cadr", m_currentFrame);
            return 1;
        }

        std::fread(h, 0x36u, 1u, m_file);
        if (readLe32(h + 30) != 0)
        {
            release();
            reportPictureError(10, "not supported bmp compression type", 0);
            return 1;
        }
        if (readLe16(h + 28) < 8u)
        {
            release();
            reportPictureError(10, "not supported bmp 2 and 4 bit type", 0);
            return 1;
        }

        m_dataOffset = static_cast<int>(readLe32(h + 10));
        const int headerWidth = static_cast<int>(readLe32(h + 18));
        const int headerHeight = static_cast<int>(readLe32(h + 22));
        const int headerBpp = static_cast<int>(readLe16(h + 28)) >> 3;
        if (m_buffer && (m_width != headerWidth || m_height != headerHeight || m_bytesPerPixel != headerBpp))
        {
            reportPictureError(10, "BMP parameters different from first cadr", m_currentFrame);
            return 1;
        }
        return 0;
    }

    int PICTURE_BMP::openFilename(const STRING& path)
    {

        BYTE header[0x36];
        if (PICTURE::openFilename(path))
            return 1;
        if (readAndValidateHeader(header))
            return 1;

        m_frameCount = countNumberedSiblings(path, ".bmp");
        allocateBuffer(static_cast<int>(readLe32(header + 18)),
                       static_cast<int>(readLe32(header + 22)),
                       static_cast<int>(readLe16(header + 28)) >> 3);
        reloadFirstFrame();
        return 0;
    }

    int PICTURE_BMP::reloadFirstFrame()
    {

        if (!m_buffer)
            return 0;
        reopenCurrentName();
        BYTE header[0x36];
        int result = readAndValidateHeader(header);
        if (!result)
        {
            std::fseek(m_file, m_dataOffset, 0);
            m_currentFrame = -1;
            result = loadNextFrame();
        }
        return result;
    }

    int PICTURE_BMP::loadNextFrame()
    {

        if (!m_buffer)
        {
            reportPictureError(10, "Picture has not opened", m_currentFrame);
            return 0;
        }
        const int next = m_currentFrame + 1;
        m_currentFrame = next;
        if (next >= m_frameCount)
            return reloadFirstFrame();

        reopenPictureFrame(m_file, m_name, m_currentFrame);

        BYTE header[0x36];
        int result = readAndValidateHeader(header);
        if (result)
            return result;

        std::fseek(m_file, m_dataOffset, SEEK_SET);
        if (m_bytesPerPixel == 1)
            std::fread(m_palette.data(), 0x400u, 1u, m_file);

        const int rowBytes = m_width * m_bytesPerPixel;
        const int rowSkip = (-rowBytes) & 3;
        std::fseek(m_file, m_dataOffset, SEEK_SET);

        BYTE* rowPtr = m_buffer + static_cast<std::size_t>(m_height) * static_cast<std::size_t>(rowBytes);
        for (int row = 0; row < m_height; ++row)
        {
            rowPtr -= rowBytes;
            std::fread(rowPtr, static_cast<std::size_t>(rowBytes), 1u, m_file);
            if (rowSkip)
                std::fseek(m_file, rowSkip, SEEK_CUR);
        }
        return 0;
    }

    PICTURE_RESOURCE::PICTURE_RESOURCE()
    {

        m_picture = new (std::nothrow) PICTURE();
    }

    PICTURE_RESOURCE::PICTURE_RESOURCE(int width, int height, int pictureType)
    {

        m_pictureType = pictureType;
        if (pictureType <= 0)
            return;
        if (pictureType <= 2)
            m_picture = new (std::nothrow) PICTURE(width, height, 3);
        else if (pictureType == 5)
            m_picture = new (std::nothrow) PICTURE(width, height, 2);
    }

    PICTURE_RESOURCE::~PICTURE_RESOURCE()
    {

        deletePictureThroughRetailSlot00(m_picture);
    }

    PICTURE_RESOURCE* deletePictureResourceOwner(PICTURE_RESOURCE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~PICTURE_RESOURCE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    int PICTURE_RESOURCE::loadNextFrame()
    {

        return m_picture->loadNextFrame();
    }

    int PICTURE_RESOURCE::reloadFirstFrame()
    {

        return m_picture->reloadFirstFrame();
    }

    int PICTURE_RESOURCE::openFilename(const STRING& path)
    {

        deletePictureThroughRetailSlot00(m_picture);

        const char* text = path.c_str();
        if (containsCaseLiteral(text, ".tga", ".TGA"))
        {
            m_pictureType = 1;
            m_picture = new (std::nothrow) PICTURE_TGA();
        }
        else if (containsCaseLiteral(text, ".z", ".Z"))
        {
            m_pictureType = 5;
            m_picture = new (std::nothrow) PICTURE_Z();
        }
        else if (containsCaseLiteral(text, ".flc", ".FLC"))
        {
            m_pictureType = 3;
            m_picture = new (std::nothrow) PICTURE_FLC();
        }
        else if (containsCaseLiteral(text, ".bmp", ".BMP"))
        {
            m_pictureType = 2;
            m_picture = new (std::nothrow) PICTURE_BMP();
        }
        else
        {
            if (text && *text)
                LOG::Write("!!!ERROR!!!PICTURE '%s': Unknown format file", text);
            m_pictureType = 0;
            m_picture = new (std::nothrow) PICTURE();
        }

        if (!m_pictureType)
            return 1;
        return m_picture->openFilename(path);
    }

    void PICTURE_RESOURCE::writePictureResourcePixel(int x, int y, DWORD value) noexcept
    {

        m_picture->writePicturePixelColor(x, y, value);
    }

    PICTURE_COMPOSITE_RESOURCE::PICTURE_COMPOSITE_RESOURCE()
    {

    }

    PICTURE_COMPOSITE_RESOURCE::~PICTURE_COMPOSITE_RESOURCE()
    {

        if (m_paletteLookup)
            ::operator delete(m_paletteLookup);
    }

    PICTURE_COMPOSITE_RESOURCE* deletePictureCompositeResourceOwner(PICTURE_COMPOSITE_RESOURCE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~PICTURE_COMPOSITE_RESOURCE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    bool PICTURE_COMPOSITE_RESOURCE::insideBase(int x, int y) const
    {
        const PICTURE* base = m_color.picture();
        return x >= 0 && y >= 0 && x < base->width() && y < base->height();
    }

    int PICTURE_COMPOSITE_RESOURCE::openFilenames(STRING colorPath, STRING alphaPath, STRING zPath)
    {

        if (m_paletteLookup)
        {
            ::operator delete(m_paletteLookup);
            m_paletteLookup = nullptr;
        }

        STRING color = colorPath;
        STRING alpha = alphaPath;
        STRING z = zPath;

        if (!alpha.isEmpty())
            m_flags |= 0x02u;
        if (!z.isEmpty())
            m_flags |= 0x04u;
        if (!color.isEmpty())
        {
            m_flags |= 0x01u;
        }
        else
        {

            color = alpha;
            alpha = "";
        }

        const int colorOpenResult = m_color.openFilename(color);
        const int alphaOpenResult = m_alpha.openFilename(alpha);
        m_z.openFilename(z);

        const PICTURE* base = m_color.picture();
        if (base->width() == 2 && base->height() == 2)
            m_flags = 0x80u;

        const int anyPrimaryOpened = (colorOpenResult == 0 ? 1 : 0) | (alphaOpenResult == 0 ? 1 : 0);
        return anyPrimaryOpened == 0 ? 1 : 0;
    }

    int PICTURE_COMPOSITE_RESOURCE::releasePictureData()
    {

        m_color.picture()->release();
        m_alpha.picture()->release();
        return m_z.picture()->release();
    }

    int PICTURE_COMPOSITE_RESOURCE::buildPackedWordStream()
    {

        const PICTURE* base = m_color.picture();
        if (base->width() <= 0)
            return 0;

        std::uint32_t crc = 0;
        for (int x = 0; x < base->width(); ++x)
        {
            for (int y = 0; y < base->height(); ++y)
            {
                if (m_flags & 0x04u)
                {
                    const WORD aux = composeAuxWord(x, y);
                    as1::UpdateCrc32(crc, &aux, 2);
                }

                DWORD converted = 0;
                if (m_flags & 0x08u)
                {
                    m_flags &= ~0x08u;
                    converted = convertPixelByFlags(x, y);
                    m_flags |= 0x08u;
                }
                else
                {
                    converted = convertPixelByFlags(x, y);
                }
                const WORD packed = static_cast<WORD>(converted & 0xFFFFu);
                as1::UpdateCrc32(crc, &packed, 2);
            }
        }
        return static_cast<int>(crc);
    }

    int PICTURE_COMPOSITE_RESOURCE::reloadFirstFrames()
    {

        m_color.reloadFirstFrame();
        m_alpha.reloadFirstFrame();
        return m_z.reloadFirstFrame();
    }

    int PICTURE_COMPOSITE_RESOURCE::loadNextFrames()
    {

        m_color.loadNextFrame();
        m_alpha.loadNextFrame();
        return m_z.loadNextFrame();
    }

    DWORD PICTURE_COMPOSITE_RESOURCE::composeArgbPixel(int x, int y) const
    {

        if (!insideBase(x, y))
            return 0xFF000000u;

        const PICTURE* base = m_color.picture();
        const PICTURE* alpha = m_alpha.picture();
        const DWORD basePixel = base->getPixelForTGA(x, y);
        if (alpha->buffer())
        {
            const BYTE b = static_cast<BYTE>(basePixel & 0xFFu);
            const BYTE g = static_cast<BYTE>((basePixel >> 8u) & 0xFFu);
            const BYTE r = static_cast<BYTE>((basePixel >> 16u) & 0xFFu);
            const BYTE a = static_cast<BYTE>(alpha->getPixelForTGA(x, y) & 0xFFu);
            return (static_cast<DWORD>(a) << 24u) |
                   (static_cast<DWORD>(r) << 16u) |
                   (static_cast<DWORD>(g) << 8u) |
                   static_cast<DWORD>(b);
        }
        return basePixel;
    }

    int PICTURE_COMPOSITE_RESOURCE::alphaAt(int x, int y) const
    {

        if (!insideBase(x, y))
            return 0;
        const PICTURE* alpha = m_alpha.picture();
        if (alpha->buffer())
            return static_cast<int>(alpha->getPixelForTGA(x, y) & 0xFFu);
        const PICTURE* base = m_color.picture();
        if (base->bytesPerPixel() == 4)
            return static_cast<int>((base->getPixelRaw(x, y) >> 24u) & 0xFFu);
        return 255;
    }

    WORD PICTURE_COMPOSITE_RESOURCE::composeAuxWord(int x, int y) const
    {

        const PICTURE* base = m_color.picture();
        if (x < 0 || y < 0 || x >= base->width() || y >= base->height())
            return 0x0400u;

        short value = 0;
        const PICTURE* z = m_z.picture();
        if (z->buffer() && m_z.pictureType() == 5)
        {
            value = static_cast<short>(z->getPixelRaw(x, y) & 0xFFFFu);
            if (value == static_cast<short>(0x8000u))
                return 0;
        }
        return static_cast<WORD>(static_cast<int>(value) + 0x400);
    }

    int PICTURE_COMPOSITE_RESOURCE::nearestPaletteIndex(DWORD argb) const
    {

        if (m_paletteLookup)
        {
            const DWORD index = (argb >> 28u) +
                16u * (((argb & 0xF8u) | (((argb & 0xFC00u) | ((argb >> 3u) & 0x1F0000u)) >> 2u)) >> 3u);
            return static_cast<int>(m_paletteLookup[index & 0xFFFFFu]);
        }

        const int targetR = static_cast<int>((argb >> 19u) & 0x1Fu);
        const int targetG = static_cast<int>((static_cast<WORD>(argb) >> 10u) & 0x3Fu);
        const int targetB = static_cast<int>((static_cast<BYTE>(argb)) >> 3u);
        const int targetA = static_cast<int>(argb / 0x0F000000u);
        int bestIndex = 0;
        int bestDistance = 9999999;
        for (int i = 0; i < 256; ++i)
        {
            const DWORD p = m_palette[static_cast<std::size_t>(i)];
            const int dr = targetR - static_cast<int>((p >> 19u) & 0x1Fu);
            const int dg = targetG - static_cast<int>(((static_cast<WORD>(p) >> 8u) & 0xFFu) >> 2u);
            const int db = targetB - static_cast<int>((static_cast<BYTE>(p)) >> 3u);
            const int da = targetA - static_cast<int>(p / 0x0F000000u);
            const int distance = 222784 * dr * dr + 14400 * dg * dg + 1936 * db * db + 6750 * da * da;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        return bestIndex;
    }

    void PICTURE_COMPOSITE_RESOURCE::replacePaletteLookup(DWORD argb, BYTE index)
    {

        if (!m_paletteLookup)
            return;
        const DWORD tableIndex = (argb >> 28u) +
            16u * (((argb & 0xF8u) | (((argb & 0xFC00u) | ((argb >> 3u) & 0x1F0000u)) >> 2u)) >> 3u);
        BYTE* cell = m_paletteLookup + (tableIndex & 0xFFFFFu);
        const BYTE oldValue = *cell;
        if (oldValue != 0 && oldValue != index)
        {
            for (int i = 0; i < 0x100000; ++i)
            {
                if (m_paletteLookup[i] == oldValue)
                    m_paletteLookup[i] = index;
            }
        }
        else
        {
            *cell = index;
        }
    }

    int PICTURE_COMPOSITE_RESOURCE::isSolidAt(int x, int y)
    {

        if (!insideBase(x, y))
            return 0;

        const PICTURE* z = m_z.picture();
        if (z->buffer() && m_z.pictureType() == 5)
        {
            if (static_cast<WORD>(z->getPixelRaw(x, y) & 0xFFFFu) == 0x8000u)
                return 0;
            if ((m_flags & 0x20u) && (m_flags & 0x02u) && (m_flags & 0x04u))
            {
                if (m_flags & 0x01u)
                    return alphaAt(x, y) >> 4;
                return static_cast<int>(convertPixelByFlags(x, y) & 0x0FFFu);
            }
            return 1;
        }

        if ((m_flags & 0x02u) && (m_flags & 0x01u))
            return alphaAt(x, y) >> 4;
        return static_cast<int>(composeArgbPixel(x, y) & 0x00FFFFFFu);
    }


    WORD PICTURE_COMPOSITE_RESOURCE::auxWordForFirstSolidInRect(int x, int y, int width, int height)
    {

        const int xEnd = x + width;
        const int yEnd = y + height;
        const int lastX = xEnd - 1;
        const int lastY = yEnd - 1;

        if (isSolidAt(lastX, lastY))
            return composeAuxWord(lastX, lastY);

        for (int yy = y; yy < yEnd; ++yy)
        {
            for (int xx = x; xx < xEnd; ++xx)
            {
                if (isSolidAt(xx, yy))
                    return composeAuxWord(xx, yy);
            }
        }
        return 0;
    }

    void PICTURE_COMPOSITE_RESOURCE::scanSolidBounds(int* minX, int* minY, int* maxXExclusive, int* maxYExclusive)
    {

        const PICTURE* base = m_color.picture();
        const int w = base->width();
        const int h = base->height();

        if (minY && maxYExclusive)
        {
            *minY = -1;
            for (int yy = 0; yy < h && *minY == -1; ++yy)
            {
                for (int xx = 0; xx < w; ++xx)
                {
                    if (isSolidAt(xx, yy))
                    {
                        *minY = yy;
                        break;
                    }
                }
            }

            *maxYExclusive = -1;
            for (int yy = h - 1; yy >= 0 && *maxYExclusive == -1; --yy)
            {
                for (int xx = 0; xx < w; ++xx)
                {
                    if (isSolidAt(xx, yy))
                    {
                        *maxYExclusive = yy;
                        break;
                    }
                }
            }
            if (*maxYExclusive >= 0)
                ++*maxYExclusive;

            if (*maxYExclusive <= *minY || *minY < 0 || *maxYExclusive < 0)
            {
                *minY = 0;
                *maxYExclusive = 0;
            }
        }

        if (minX && maxXExclusive)
        {
            *minX = -1;
            for (int xx = 0; xx < w && *minX == -1; ++xx)
            {
                for (int yy = 0; yy < h; ++yy)
                {
                    if (isSolidAt(xx, yy))
                    {
                        *minX = xx;
                        break;
                    }
                }
            }

            *maxXExclusive = -1;
            for (int xx = w - 1; xx >= 0 && *maxXExclusive == -1; --xx)
            {
                for (int yy = 0; yy < h; ++yy)
                {
                    if (isSolidAt(xx, yy))
                    {
                        *maxXExclusive = xx;
                        break;
                    }
                }
            }
            if (*maxXExclusive >= 0)
                ++*maxXExclusive;

            if (*maxXExclusive <= *minX || *minX < 0 || *maxXExclusive < 0)
            {

                if (minY) *minY = 0;
                if (maxYExclusive) *maxYExclusive = 0;
            }
        }
    }

    int PICTURE_COMPOSITE_RESOURCE::copyCompositeSurfaceRows(BYTE* destination, const BYTE* source, int width, int height) const
    {

        if ((m_flags & 0x08u) != 0u || (m_flags & 0x0800u) == 0u)
        {
            unsigned int written = 0u;
            const BYTE* row = source;
            for (int yy = 0; yy < height; ++yy)
            {
                if ((m_flags & 0x08u) != 0u)
                {
                    for (int xx = 0; xx < width; ++xx)
                        destination[written++] = row[xx * 2];
                }
                else
                {
                    const unsigned int bytes = static_cast<unsigned int>(2 * width);
                    std::memcpy(destination + written, row, bytes);
                    written += bytes;
                }
                row += 0x200;
            }
            return static_cast<int>(written);
        }

#ifdef _WIN32
        DDSURFACEDESC desc{};
        desc.dwSize = sizeof(DDSURFACEDESC);
        desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
        if ((m_flags & 0x06u) == 0u)
            desc.dwFlags |= DDSD_CKSRCBLT;
        desc.dwWidth = static_cast<DWORD>(width);
        desc.dwHeight = static_cast<DWORD>(height);
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        desc.ddpfPixelFormat.dwRGBBitCount = 16u;
        if ((m_flags & 0x03u) == 0x03u)
        {
            desc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
            desc.ddpfPixelFormat.dwRBitMask = 0x0F00u;
            desc.ddpfPixelFormat.dwGBitMask = 0x00F0u;
            desc.ddpfPixelFormat.dwBBitMask = 0x000Fu;
            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xF000u;
        }
        else
        {
            desc.ddpfPixelFormat.dwRBitMask = 0xF800u;
            desc.ddpfPixelFormat.dwGBitMask = 0x07E0u;
            desc.ddpfPixelFormat.dwBBitMask = 0x001Fu;
        }

        IDirectDrawSurface* sourceSurface = nullptr;
        HRESULT result = g_pictureDirectDraw->CreateSurface(&desc, &sourceSurface, nullptr);
        if (result != DD_OK)
        {
            const PICTURE* base = m_color.picture();
            LOG::ResourceError("PICTURE '%s'", 3, "DD surface", 0, base->name().c_str());
            return 0;
        }

        desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
        desc.ddpfPixelFormat = {};
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
        desc.ddpfPixelFormat.dwFourCC = ((m_flags & 0x03u) == 0x03u)
            ? 0x33545844u
            : 0x31545844u;

        IDirectDrawSurface* dxtSurface = nullptr;
        result = g_pictureDirectDraw->CreateSurface(&desc, &dxtSurface, nullptr);
        if (result != DD_OK)
        {
            const PICTURE* base = m_color.picture();
            LOG::ResourceError("PICTURE '%s'", 3, "DXT surface", 0, base->name().c_str());
            return 0;
        }

        result = sourceSurface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
        if (result != DD_OK)
        {
            const PICTURE* base = m_color.picture();
            LOG::ResourceError("PICTURE '%s'", 0, "surface", 0, base->name().c_str());
            return 0;
        }

        BYTE* surfaceBits = static_cast<BYTE*>(desc.lpSurface);
        const unsigned int rowBytes = static_cast<unsigned int>(2 * width);
        const BYTE* sourceRow = source;
        for (int yy = 0; yy < height; ++yy)
        {
            std::memcpy(surfaceBits, sourceRow, rowBytes);
            sourceRow += 0x200;
            surfaceBits += rowBytes;
        }
        sourceSurface->Unlock(nullptr);

        result = dxtSurface->BltFast(0u, 0u, sourceSurface, nullptr, DDBLTFAST_WAIT);
        if (result != DD_OK)
        {
            const PICTURE* base = m_color.picture();
            LOG::ResourceError("PICTURE '%s'", 1, "surface", 0, base->name().c_str());
            return 0;
        }

        result = dxtSurface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
        if (result != DD_OK)
        {
            const PICTURE* base = m_color.picture();
            LOG::ResourceError("PICTURE '%s'", 0, "DXT surface", 0, base->name().c_str());
            return 0;
        }

        unsigned int written = 0u;
        if ((desc.dwFlags & DDSD_LINEARSIZE) != 0u)
        {
            written = desc.dwLinearSize;
            std::memcpy(destination, desc.lpSurface, written);
        }
        else
        {
            const PICTURE* base = m_color.picture();
            LOG::ResourceError("PICTURE '%s'", 10, "not DDSD_LINEARSIZE", 0, base->name().c_str());
        }

        dxtSurface->Unlock(nullptr);
        dxtSurface->Release();
        sourceSurface->Release();
        return static_cast<int>(written);
#else
        return 0;
#endif
    }


    int PICTURE_COMPOSITE_RESOURCE::buildShadowDotControlWords(WORD* outWords)
    {

        outWords[0] = 0;
        if ((m_flags & 0x20000u) == 0)
            return 2;

        const PICTURE* base = m_color.picture();
        const int width = base->width();
        const int height = base->height();
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        DWORD* contour = static_cast<DWORD*>(::operator new(pixelCount * sizeof(DWORD), std::nothrow));
        if (!contour)
        {
            LOG::ResourceError("PICTURE '%s'", 2, "shadow dot", 0, base->name().c_str());
            return 2;
        }
        BYTE* visited = static_cast<BYTE*>(::operator new(pixelCount, std::nothrow));
        if (!visited)
        {
            LOG::ResourceError("PICTURE '%s'", 2, "dot without shadow", 0, base->name().c_str());
            return 2;
        }
        std::memset(visited, 0, pixelCount);

        static const short dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        static const short dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

        for (;;)
        {
            int x = 0;
            int y = 0;
            bool found = false;
            while (x < width && !found)
            {
                y = 0;
                while (y <= x)
                {
                    if (isSolidAt(x, y) && !visited[static_cast<std::size_t>(y) * width + x])
                    {
                        found = true;
                        break;
                    }
                    ++y;
                }
                if (found)
                    break;

                if (x > 0)
                {
                    int scanX = 0;
                    while (scanX < x)
                    {
                        if (isSolidAt(scanX, x) && !visited[static_cast<std::size_t>(x) * width + scanX])
                        {
                            y = x;
                            x = scanX;
                            found = true;
                            break;
                        }
                        ++scanX;
                    }
                    if (found)
                        break;
                }
                ++x;
            }

            if (!found || x >= width)
                return 2;

            const int startX = x;
            const int startY = y;
            int count = 1;
            contour[0] = static_cast<DWORD>(static_cast<WORD>(x)) |
                         (static_cast<DWORD>(static_cast<WORD>(y)) << 16u);
            int backDirection = 0;
            int previousDirection = 0;

            for (;;)
            {
                int direction = (previousDirection + 1) & 7;
                bool advanced = false;
                while (direction != backDirection)
                {
                    const int nx = x + dx[direction];
                    const int ny = y + dy[direction];
                    if (isSolidAt(nx, ny) && !visited[static_cast<std::size_t>(ny) * width + nx])
                    {
                        x = nx;
                        y = ny;
                        contour[static_cast<std::size_t>(count)] =
                            static_cast<DWORD>(static_cast<WORD>(x)) |
                            (static_cast<DWORD>(static_cast<WORD>(y)) << 16u);
                        ++count;
                        backDirection = direction ^ 4;
                        advanced = true;
                        break;
                    }
                    direction = (direction + 1) & 7;
                }

                if (!advanced)
                {
                    --count;
                    if (count <= 0)
                        break;
                    const DWORD current = contour[static_cast<std::size_t>(count)];
                    const DWORD previous = contour[static_cast<std::size_t>(count - 1)];
                    const int cx = static_cast<short>(current & 0xFFFFu);
                    const int cy = static_cast<short>((current >> 16u) & 0xFFFFu);
                    const int px = static_cast<short>(previous & 0xFFFFu);
                    const int py = static_cast<short>((previous >> 16u) & 0xFFFFu);
                    int dir = 0;
                    while (dir < 8 && (cx != px + dx[dir] || cy != py + dy[dir]))
                        ++dir;
                    backDirection = dir ^ 4;
                    x = cx;
                    y = cy;
                }

                if (x == startX && y == startY)
                    break;
                previousDirection = backDirection;
            }

            int simplifiedCount = 1;
            int index = 0;
            const int tolerance = (m_flags & 0x80000u) ? 3 : 0;
            while (index < count)
            {
                int candidate = index + 3;
                if (candidate >= count)
                    candidate = count;
                else
                {
                    while (candidate < count)
                    {
                        const DWORD a = contour[static_cast<std::size_t>(index)];
                        const DWORD b = contour[static_cast<std::size_t>(candidate)];
                        const int ax = static_cast<short>(a & 0xFFFFu);
                        const int ay = static_cast<short>((a >> 16u) & 0xFFFFu);
                        const int bx = static_cast<short>(b & 0xFFFFu);
                        const int by = static_cast<short>((b >> 16u) & 0xFFFFu);

                        bool split = false;
                        if (ax != bx || ay != by)
                        {
                            const DWORD previous = contour[static_cast<std::size_t>(candidate - 1)];
                            const int prevX = static_cast<short>(previous & 0xFFFFu);
                            const int prevY = static_cast<short>((previous >> 16u) & 0xFFFFu);
                            const int shortcut = ax - prevY - prevX + ay;
                            const int absShortcut = shortcut < 0 ? -shortcut : shortcut;
                            if (absShortcut >= 1)
                            {
                                const int ddx = ax - bx;
                                const int ddy = ay - by;
                                const int lineLength = as1::IntegerSquareRoot(addWrap32(multiplyWrap32(ddx, ddx), multiplyWrap32(ddy, ddy)));
                                for (int middle = index + 1; middle < candidate; ++middle)
                                {
                                    const DWORD p = contour[static_cast<std::size_t>(middle)];
                                    const int px = static_cast<short>(p & 0xFFFFu);
                                    const int py = static_cast<short>((p >> 16u) & 0xFFFFu);
                                    const long long cross = static_cast<long long>(ay) * bx +
                                        static_cast<long long>(ax - bx) * py +
                                        static_cast<long long>(px) * (by - ay) -
                                        static_cast<long long>(by) * ax;
                                    const long long absCross = cross < 0 ? -cross : cross;
                                    if (static_cast<int>(absCross / lineLength) > tolerance)
                                    {
                                        split = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (split)
                            break;
                        ++candidate;
                    }
                }

                index = candidate - 1;
                if (candidate < count)
                    contour[static_cast<std::size_t>(simplifiedCount++)] = contour[static_cast<std::size_t>(index)];
            }

            if (simplifiedCount < 3)
            {
                visited[static_cast<std::size_t>(startY) * width + startX] = 1;
                continue;
            }

            while (simplifiedCount > 255)
            {
                simplifiedCount /= 2;
                for (int i = 0; i < simplifiedCount; ++i)
                    contour[static_cast<std::size_t>(i)] = contour[static_cast<std::size_t>(i * 2)];
            }

            int outputWordCount = 1;
            outWords[0] = static_cast<WORD>(simplifiedCount);
            for (int i = 0; i < simplifiedCount; ++i)
            {
                const DWORD p = contour[static_cast<std::size_t>(i)];
                const int px = static_cast<short>(p & 0xFFFFu);
                const int py = static_cast<short>((p >> 16u) & 0xFFFFu);
                const int zDelta = static_cast<short>(composeAuxWord(px, py)) / 8 - 128;
                outWords[outputWordCount++] = static_cast<WORD>(px);
                outWords[outputWordCount++] = static_cast<WORD>(py + zDelta);
                outWords[outputWordCount++] = static_cast<WORD>(zDelta);
            }
            ::operator delete(contour);
            ::operator delete(visited);
            return outputWordCount * static_cast<int>(sizeof(WORD));
        }
    }

    int PICTURE_COMPOSITE_RESOURCE::buildAdaptivePaletteLookupFromFrames()
    {

        if (m_paletteLookup)
            ::operator delete(m_paletteLookup);
        m_paletteLookup = static_cast<BYTE*>(::operator new(0x100000u));
        std::memset(m_paletteLookup, 0, 0x100000u);

        m_palette[0] = 0u;

        std::array<int, 256 * 256> distances{};
        std::array<int, 256> newDistances{};
        int paletteCount = 1;
        int returnValue = 0;
        const PICTURE* base = m_color.picture();
        const int frames = base->totalFrames();

        auto colorDistance = [](DWORD lhs, DWORD rhs) -> int
        {
            const int lhsG = static_cast<int>((static_cast<WORD>(lhs) >> 10u));
            const int rhsG = static_cast<int>((static_cast<WORD>(rhs) >> 10u));
            const int lhsB = static_cast<int>((lhs >> 3u) & 0x1Fu);
            const int rhsB = static_cast<int>((rhs >> 3u) & 0x1Fu);
            const int lhsR = static_cast<int>((lhs >> 19u) & 0x1Fu);
            const int rhsR = static_cast<int>((rhs >> 19u) & 0x1Fu);
            const int lhsA = static_cast<int>(lhs / 0x0F000000u);
            const int rhsA = static_cast<int>(rhs / 0x0F000000u);
            const int dg = lhsG - rhsG;
            const int db = lhsB - rhsB;
            const int da = lhsA - rhsA;
            const int dr = lhsR - rhsR;
            return 14400 * dg * dg +
                   1936 * db * db +
                   6750 * da * da +
                   222784 * dr * dr;
        };

        for (int frame = 0; frame < frames; ++frame)
        {
            const PICTURE* currentBase = m_color.picture();
            for (int y = 0; y < currentBase->height(); ++y)
            {
                for (int x = 0; x < currentBase->width(); ++x)
                {
                    if (!isSolidAt(x, y))
                        continue;

                    const DWORD color = composeArgbPixel(x, y);
                    const DWORD tableIndex = (color >> 28u) +
                        16u * (((color & 0xF8u) | (((color & 0xFC00u) | ((color >> 3u) & 0x1F0000u)) >> 2u)) >> 3u);
                    if (m_paletteLookup[tableIndex & 0xFFFFFu] != 0)
                        continue;

                    int nearestIndex = 0;
                    int nearestDistance = 0;
                    bool exact = false;
                    for (int i = 0; i < paletteCount; ++i)
                    {
                        const int distance = colorDistance(m_palette[static_cast<std::size_t>(i)], color);
                        newDistances[static_cast<std::size_t>(i)] = distance;
                        if (distance == 0)
                        {
                            replacePaletteLookup(color, static_cast<BYTE>(i));
                            exact = true;
                            break;
                        }
                        if (i == 0 || distance < nearestDistance)
                        {
                            nearestDistance = distance;
                            nearestIndex = i;
                        }
                    }
                    if (exact)
                        continue;

                    if (paletteCount < 256)
                    {
                        for (int i = 0; i < paletteCount; ++i)
                        {
                            const int distance = newDistances[static_cast<std::size_t>(i)];
                            distances[static_cast<std::size_t>(paletteCount) * 256u + static_cast<std::size_t>(i)] = distance;
                            distances[static_cast<std::size_t>(i) * 256u + static_cast<std::size_t>(paletteCount)] = distance;
                        }
                        const int newIndex = paletteCount++;
                        m_palette[static_cast<std::size_t>(newIndex)] = color;
                        replacePaletteLookup(color, static_cast<BYTE>(newIndex));
                        continue;
                    }

                    int pairHigh = 1;
                    int pairLow = 0;
                    int pairDistance = distances[256u];
                    for (int high = 1; high < 256; ++high)
                    {
                        for (int low = 0; low < high; ++low)
                        {
                            const int distance = distances[static_cast<std::size_t>(high) * 256u + static_cast<std::size_t>(low)];
                            if (distance < pairDistance)
                            {
                                pairDistance = distance;
                                pairHigh = high;
                                pairLow = low;
                            }
                        }
                    }

                    if (pairDistance < newDistances[static_cast<std::size_t>(nearestIndex)])
                    {
                        const DWORD replacedColor = m_palette[static_cast<std::size_t>(pairLow)];
                        replacePaletteLookup(replacedColor, static_cast<BYTE>(nearestIndex));

                        m_palette[static_cast<std::size_t>(pairLow)] = color;
                        replacePaletteLookup(color, static_cast<BYTE>(pairLow));

                        for (int i = 0; i < 256; ++i)
                        {
                            const int distance = newDistances[static_cast<std::size_t>(i)];
                            distances[static_cast<std::size_t>(pairLow) * 256u + static_cast<std::size_t>(i)] = distance;
                            distances[static_cast<std::size_t>(i) * 256u + static_cast<std::size_t>(pairLow)] = distance;
                        }
                        (void)pairHigh;
                    }
                    else
                    {
                        replacePaletteLookup(color, static_cast<BYTE>(nearestIndex));
                    }
                }
            }
            loadNextFrames();
        }
        reloadFirstFrames();

        std::array<DWORD, 256> counts{};
        std::array<DWORD, 256> sumR{};
        std::array<DWORD, 256> sumG{};
        std::array<DWORD, 256> sumB{};
        std::array<DWORD, 256> sumA{};
        for (DWORD i = 0; i < 0x100000u; ++i)
        {
            const BYTE idx = m_paletteLookup[i];
            if (!idx)
                continue;
            const DWORD coarse = i / 16u;
            ++counts[idx];
            sumR[idx] += (coarse >> 8u) & 0xF8u;
            sumG[idx] += (coarse >> 3u) & 0xFCu;
            sumB[idx] += 8u * (coarse & 0x1Fu);
            sumA[idx] += 16u * (i & 0x0Fu) + 15u;
        }

        for (int i = 0; i < 256; ++i)
        {
            const DWORD count = counts[static_cast<std::size_t>(i)];
            if (!count)
                continue;
            int b = static_cast<int>(sumB[static_cast<std::size_t>(i)] / count);
            int g = static_cast<int>(sumG[static_cast<std::size_t>(i)] / count);
            int r = static_cast<int>(sumR[static_cast<std::size_t>(i)] / count);
            int a = static_cast<int>(sumA[static_cast<std::size_t>(i)] / count);
            a = clampRetailByteInt(a);
            r = clampRetailByteInt(r);
            g = clampRetailByteInt(g);
            b = clampRetailByteInt(b);
            m_palette[static_cast<std::size_t>(i)] =
                static_cast<DWORD>(b) |
                (static_cast<DWORD>(g) << 8u) |
                (static_cast<DWORD>(r) << 16u) |
                (static_cast<DWORD>(a) << 24u);
            returnValue = static_cast<int>(m_palette[static_cast<std::size_t>(i)]);
        }

        if ((m_flags & 0x02u) && paletteCount > 0)
        {
            for (int i = 0; i < paletteCount; ++i)
            {
                DWORD p = m_palette[static_cast<std::size_t>(i)];
                int alpha = static_cast<int>(p >> 24u);
                if ((p & 0xFF000000u) <= 0xEF000000u)
                    alpha = clampRetailByteInt(16 * static_cast<int>(p >> 28u));
                else
                    alpha = 255;
                p = (p & 0x00FFFFFFu) | (static_cast<DWORD>(alpha) << 24u);
                m_palette[static_cast<std::size_t>(i)] = p;
                if (p & 0xFF000000u)
                {
                    const int a = static_cast<int>(p >> 24u);
                    const int b = clampRetailByteInt(255 * static_cast<int>(static_cast<BYTE>(p)) / a);
                    const int g = clampRetailByteInt(255 * static_cast<int>(static_cast<BYTE>(p >> 8u)) / a);
                    const int r = clampRetailByteInt(255 * static_cast<int>(static_cast<BYTE>(p >> 16u)) / a);
                    m_palette[static_cast<std::size_t>(i)] =
                        static_cast<DWORD>(b) |
                        (static_cast<DWORD>(g) << 8u) |
                        (static_cast<DWORD>(r) << 16u) |
                        (static_cast<DWORD>(a) << 24u);
                }
            }
            returnValue = 0;
        }
        return returnValue;
    }

    STRING& PICTURE_COMPOSITE_RESOURCE::duplicateBasePictureName(STRING& out) const
    {

        const PICTURE* base = m_color.picture();
        if (base->name().isEmpty())
        {

            out.ResetSharedEmptyWithoutRelease();
            return out;
        }
        out.AssignAllocatedCopyWithoutRelease(base->name().c_str());
        return out;
    }

    int PICTURE_COMPOSITE_RESOURCE::anySolidInRect(int x, int y, int width, int height)
    {

        const int endX = static_cast<int>(static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(width));
        const int endY = static_cast<int>(static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(height));
        for (int yy = y; yy < endY; yy = static_cast<int>(static_cast<std::uint32_t>(yy) + 1u))
            for (int xx = x; xx < endX; xx = static_cast<int>(static_cast<std::uint32_t>(xx) + 1u))
                if (isSolidAt(xx, yy))
                    return 1;
        return 0;
    }

    int PICTURE_COMPOSITE_RESOURCE::writeRawArgbFrameDataSections(as1::RESOURCE& resource)
    {

        resource.BeginSection(as1::RESOURCE::ResTypes::DATA, 0);
        const PICTURE* base = m_color.picture();
        const int frames = base->totalFrames();
        for (int frame = 0; frame < frames; ++frame)
        {
            const DWORD value = composeArgbPixel(0, 0);
            resource.write_new(&value, sizeof(value));
            loadNextFrames();
        }
        return resource.EndSection();
    }


    void PICTURE_COMPOSITE_RESOURCE::writeRawBlockControlFrameDataSections(as1::RESOURCE& resource)
    {

        const PICTURE* base = m_color.picture();

        BYTE* payload = static_cast<BYTE*>(::operator new(0x200000u, std::nothrow));
        if (!payload)
        {
            LOG::ResourceError("PICTURE '%s'", 2, "cdr_s", 0, base->name().c_str());
            std::exit(1);
        }
        const int frames = base->totalFrames();
        DWORD* frameKeys = static_cast<DWORD*>(::operator new(static_cast<std::size_t>(frames) * sizeof(DWORD), std::nothrow));
        if (!frameKeys)
        {
            LOG::ResourceError("PICTURE '%s'", 2, "cntrl_s", 0, base->name().c_str());
            std::exit(1);
        }

        std::unique_ptr<script::LogicAdaptiveCodec> filter;
        if (m_flags & 0x100u)
        {
            filter.reset(new (std::nothrow) script::LogicAdaptiveCodec());
            if (filter)
                filter->setMode(1);
        }

        if (m_flags & 0x02u)
            m_flags &= ~0x08u;

        if (m_flags & 0x08u)
        {
            resource.BeginSection(as1::RESOURCE::ResTypes::PALETTE, 0);
            const auto& srcPalette = base->palette();
            resource.write_new(srcPalette.data(), 0x400u);
            resource.EndSection();
        }

        int writtenFrames = 0;
        for (int frame = 0; frame < frames; ++frame)
        {
            resource.BeginSection(as1::RESOURCE::ResTypes::DATA, 0);

            const DWORD key = static_cast<DWORD>(buildPackedWordStream());
            frameKeys[frame] = key;

            int duplicateIndex = -1;
            for (int i = 0; i < frame; ++i)
            {
                if (frameKeys[i] == key)
                {
                    duplicateIndex = i;
                    break;
                }
            }

            std::size_t pos = 0;
            if (duplicateIndex >= 0)
            {
                appendDword(payload, pos, static_cast<DWORD>(duplicateIndex));
            }
            else
            {
                int minX = 0;
                int minY = 0;
                int maxX = 0;
                int maxY = 0;
                scanSolidBounds(&minX, &minY, &maxX, &maxY);

                const DWORD mode = (m_flags & 0x08u) ? 0x29u : ((m_flags & 0x02u) ? 0x15u : 0x19u);
                appendDword(payload, pos, mode);
                appendWord(payload, pos, static_cast<WORD>(maxX - minX));
                appendWord(payload, pos, static_cast<WORD>(maxY - minY));

                for (int y = minY; y < maxY; ++y)
                {
                    for (int x = minX; x < maxX; ++x)
                    {
                        const DWORD pixel = convertPixelByFlags(x, y);
                        if (m_flags & 0x08u)
                        {
                            appendByte(payload, pos, static_cast<BYTE>(pixel & 0xFFu));
                        }
                        else if (m_flags & 0x02u)
                        {
                            appendDword(payload, pos, pixel);
                        }
                        else
                        {
                            appendWord(payload, pos, static_cast<WORD>(pixel & 0xFFFFu));
                        }
                    }
                }

                const std::size_t controlHeader = pos;
                appendDword(payload, pos, 0);
                appendDword(payload, pos, 0);

                const int widthForScratch = maxX - minX + 9;
                const int heightForScratch = maxY - minY + 9;

                const std::uint32_t minXBits = static_cast<std::uint32_t>(minX);
                const std::uint32_t negMinXBits = 0u - minXBits;
                const std::uint32_t retailSkew = (negMinXBits << 31u) - minXBits;
                const std::uint32_t allocWidth = retailSkew + static_cast<std::uint32_t>(maxX) + 9u;
                const std::uint32_t allocHeight = static_cast<std::uint32_t>(maxY) - static_cast<std::uint32_t>(minY) + 9u;
                const std::uint32_t allocBytes = allocWidth * allocHeight * 2u;
                WORD* scratch = static_cast<WORD*>(::operator new(static_cast<std::size_t>(allocBytes)));
                DWORD pointCount = 0;

                for (int y = minY; y < maxY + 8; y += 8)
                {
                    for (int x = minX; x < maxX + 8; x += 8)
                    {
                        if (!anySolidInRect(x - 8, y - 8, 9, 9))
                            continue;

                        const WORD aux = auxWordForFirstSolidInRect(x - 8, y - 8, 9, 9);
                        appendWord(payload, pos, static_cast<WORD>(x));
                        appendWord(payload, pos, static_cast<WORD>(y));
                        appendWord(payload, pos, aux);
                        appendWord(payload, pos, static_cast<WORD>(x - minX));
                        appendWord(payload, pos, static_cast<WORD>(y - minY));

                        const int sx = x - minX;
                        const int sy = y - minY;
                        const std::size_t scratchIndex = static_cast<std::size_t>(sy) * static_cast<std::size_t>(widthForScratch) + static_cast<std::size_t>(sx);
                        scratch[scratchIndex] = static_cast<WORD>(pointCount);
                        ++pointCount;
                    }
                }
                overwriteDword(payload, controlHeader, pointCount);

                DWORD indexWordCount = 0;
                for (int y = minY; y < maxY + 8; y += 8)
                {
                    for (int x = minX; x < maxX + 8; x += 8)
                    {
                        if (!anySolidInRect(x, y, 8, 8))
                            continue;

                        const WORD topLeft = scratchWordAt(scratch, widthForScratch, minX, minY, x, y);
                        const WORD topRight = scratchWordAt(scratch, widthForScratch, minX, minY, x + 8, y);
                        const WORD bottomLeft = scratchWordAt(scratch, widthForScratch, minX, minY, x, y + 8);
                        const WORD bottomRight = scratchWordAt(scratch, widthForScratch, minX, minY, x + 8, y + 8);
                        appendWord(payload, pos, topLeft);
                        appendWord(payload, pos, topRight);
                        appendWord(payload, pos, bottomLeft);
                        appendWord(payload, pos, bottomRight);
                        appendWord(payload, pos, topRight);
                        appendWord(payload, pos, bottomLeft);
                        indexWordCount += 6;
                    }
                }
                overwriteDword(payload, controlHeader + 4u, indexWordCount);
                ::operator delete(scratch);
            }

            const DWORD byteCount = static_cast<DWORD>(pos);
            writeU32Resource(resource, byteCount);
            resource.WritePayload(payload, byteCount, filter.get());
            resource.EndSection();
            loadNextFrames();
            ++writtenFrames;
        }

        ::operator delete(frameKeys);
        ::operator delete(payload);
    }

    void PICTURE_COMPOSITE_RESOURCE::writeRunLengthFrameDataSections(as1::RESOURCE& resource)
    {

        const PICTURE* base = m_color.picture();

        std::unique_ptr<script::LogicAdaptiveCodec> filter;
        if (m_flags & 0x100u)
        {
            filter.reset(new (std::nothrow) script::LogicAdaptiveCodec());
            if (filter)
                filter->setMode(1);
        }

        if (m_flags & 0x08u)
        {
            resource.BeginSection(as1::RESOURCE::ResTypes::PALETTE, 0);
            if (m_flags & 0x10u)
            {
                if (m_flags & 0x02u)
                {
                    buildAdaptivePaletteLookupFromFrames();
                    resource.write_new(m_palette.data(), 0x400u);
                }
                else
                {
                    resource.write_new(base->palette().data(), 0x400u);
                }
            }
            else
            {
                BYTE pal[0x300];
                const auto& src = base->palette();
                for (int i = 0; i < 0x100; ++i)
                {
                    const DWORD p = src[static_cast<std::size_t>(i)];
                    pal[i * 3 + 0] = static_cast<BYTE>((p >> 16u) & 0xFFu);
                    pal[i * 3 + 1] = static_cast<BYTE>((p >> 8u) & 0xFFu);
                    pal[i * 3 + 2] = static_cast<BYTE>(p & 0xFFu);
                }
                resource.write_new(pal, sizeof(pal));
            }
            resource.EndSection();
        }

        BYTE* payload = static_cast<BYTE*>(::operator new(0x200000u, std::nothrow));
        if (!payload)
        {
            LOG::ResourceError("PICTURE '%s'", 2, "cdr_s", 0, base->name().c_str());
            std::exit(1);
        }
        DWORD* frameKeys = static_cast<DWORD*>(::operator new(static_cast<std::size_t>(base->totalFrames()) * sizeof(DWORD), std::nothrow));
        if (!frameKeys)
        {
            LOG::ResourceError("PICTURE '%s'", 2, "cntrl_s", 0, base->name().c_str());
            std::exit(1);
        }
        int writtenFrames = 0;
        for (int frame = 0; frame < base->totalFrames(); ++frame)
        {
            resource.BeginSection(as1::RESOURCE::ResTypes::DATA, 0);
            const DWORD key = static_cast<DWORD>(buildPackedWordStream());
            frameKeys[frame] = key;

            std::size_t pos = 0;
            int duplicate = -1;
            for (int i = 0; i < frame; ++i)
                if (frameKeys[i] == key)
                {
                    duplicate = i;
                    break;
                }
            if (duplicate >= 0)
            {
                const WORD dup = static_cast<WORD>(duplicate);
                std::memcpy(payload, &dup, sizeof(dup));
                pos = 2;
            }
            else
            {
                pos = static_cast<std::size_t>(buildShadowDotControlWords(reinterpret_cast<WORD*>(payload)));

                int minX = 0, minY = 0, maxX = 0, maxY = 0;
                scanSolidBounds(&minX, &minY, &maxX, &maxY);
                if (m_flags & 0x100000u)
                {
                    minY = 0;
                    maxY = 0;
                }
                const WORD row0 = static_cast<WORD>(minY);
                const WORD rows = static_cast<WORD>(maxY - minY);
                std::memcpy(payload + pos, &row0, 2); pos += 2;
                std::memcpy(payload + pos, &rows, 2); pos += 2;

                for (int y = minY; y < maxY; ++y)
                {
                    int x = 0;
                    while (x < base->width())
                    {
                        int skipStart = x;
                        while (x < base->width() && !isSolidAt(x, y))
                            ++x;
                        if (x >= base->width())
                            break;
                        int skip = x - skipStart;
                        while (skip > 0xFF)
                        {
                            payload[pos++] = 0xFF;
                            payload[pos++] = 0;
                            skip -= 0xFF;
                        }
                        payload[pos++] = static_cast<BYTE>(skip & 0xFF);

                        const int runStart = x;
                        int run = 0;
                        while (x < base->width() && run < 0xFF)
                        {
                            if (m_flags & 0x04u)
                            {
                                if (!isSolidAt(x, y) && !isSolidAt(x + 1, y) && !isSolidAt(x + 2, y))
                                    break;
                            }
                            else if (!isSolidAt(x, y))
                            {
                                break;
                            }
                            ++x;
                            ++run;
                        }
                        payload[pos++] = static_cast<BYTE>(run & 0xFF);

                        if (m_flags & 0x04u)
                        {
                            for (int xx = runStart; xx < runStart + run; ++xx)
                            {
                                const WORD aux = composeAuxWord(xx, y);
                                std::memcpy(payload + pos, &aux, 2);
                                pos += 2;
                            }
                        }
                        const int pixelBytes = (m_flags & 0x08u) ? 1 : 2;
                        for (int xx = runStart; xx < runStart + run; ++xx)
                        {
                            const DWORD px = convertPixelByFlags(xx, y);
                            if (m_flags & 0x08u)
                            {
                                payload[pos++] = static_cast<BYTE>(px & 0xFFu);
                            }
                            else
                            {
                                const WORD w = static_cast<WORD>(px & 0xFFFFu);
                                std::memcpy(payload + pos, &w, 2);
                                pos += 2;
                            }
                        }
                    }
                    payload[pos++] = 0;
                    payload[pos++] = 0;
                }
            }

            const DWORD byteCount = static_cast<DWORD>(pos);
            writeU32Resource(resource, byteCount);
            resource.WritePayload(payload, byteCount, filter.get());
            resource.EndSection();
            loadNextFrames();
            ++writtenFrames;
        }

        ::operator delete(frameKeys);
        ::operator delete(payload);
    }

    int PICTURE_COMPOSITE_RESOURCE::compareCompositeTileRecordPointers(const void* lhs, const void* rhs)
    {

        const auto* leftPtr = static_cast<CompositeTileRecord* const*>(lhs);
        const auto* rightPtr = static_cast<CompositeTileRecord* const*>(rhs);
        const CompositeTileRecord* left = *leftPtr;
        const CompositeTileRecord* right = *rightPtr;

        const int maxWidth = std::max(left->width, right->width);
        const int maxHeight = std::max(left->height, right->height);
        const auto subWrap32 = [](int lhsValue, int rhsValue) noexcept -> int
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhsValue) -
                                    static_cast<std::uint32_t>(rhsValue));
        };
        if (maxWidth > maxHeight)
            return subWrap32(right->width, left->width);
        return subWrap32(right->height, left->height);
    }

    int PICTURE_COMPOSITE_RESOURCE::assignCompositeTileSurfacePages(std::vector<CompositeTileRecord>& records) const
    {

        std::vector<CompositeTileRecord*> sorted;
        sorted.reserve(records.size());
        for (CompositeTileRecord& rec : records)
            sorted.push_back(&rec);
        if (!sorted.empty())
            std::qsort(sorted.data(), sorted.size(), sizeof(CompositeTileRecord*), &PICTURE_COMPOSITE_RESOURCE::compareCompositeTileRecordPointers);

        const PICTURE* base = m_color.picture();
        const int frames = base->totalFrames();
        const int widthPages = (base->width() + 0xFF) >> 8;
        const int heightPages = (base->height() + 0xFF) >> 8;
        const int placementLimitY = std::max(1, widthPages * heightPages * std::max(1, frames) + 1) << 8;

        int surfaceCount = 0;
        int currentY = 0;
        int placedCount = 0;
        CompositeTileRecord* previous = nullptr;

        for (CompositeTileRecord* rec : sorted)
        {
            if (rec->state < 0 || rec->height == 0)
            {
                ++placedCount;
                previous = rec;
                continue;
            }

            if (placedCount == 0 || rec->width < previous->width || rec->height < previous->height)
                currentY = 0;

            bool placed = false;
            for (int y = currentY; y <= placementLimitY - rec->height && !placed; )
            {
                const int row = y & 0xFF;
                const int nextPageY = (y - row) + 0x100;
                if (row != 0 && row + rec->height > 0x100)
                {
                    y = nextPageY;
                    currentY = y;
                    continue;
                }

                for (int x = 0; x <= 0x100 - rec->width; )
                {
                    bool overlap = false;
                    int nextX = x + 1;
                    for (int i = placedCount - 1; i >= 0; --i)
                    {
                        CompositeTileRecord* other = sorted[static_cast<std::size_t>(i)];
                        if (other->state < 0)
                            continue;
                        if (rectanglesOverlap(x, y, rec->width, rec->height,
                                              other->baseOffset, other->rowBase, other->width, other->height))
                        {
                            overlap = true;
                            nextX = other->baseOffset + other->width;
                            break;
                        }
                    }

                    if (!overlap)
                    {
                        int page = y >> 8;
                        rec->state = (m_flags & 0x04u) ? (page << 1) : page;
                        rec->baseOffset = x;
                        rec->rowBase = y;
                        if (page >= surfaceCount)
                            surfaceCount = page + 1;
                        currentY = y;
                        placed = true;
                        break;
                    }
                    x = nextX;
                }

                if (!placed)
                {
                    ++y;
                    currentY = y;
                }
            }

            if (!placed)
            {
                LOG::Write("!!!ERROR!!!PICTURE '%s': Can't replace rectangle", base->name().c_str());

                std::exit(1);
            }

            ++placedCount;
            previous = rec;
        }

        return surfaceCount;
    }

    int PICTURE_COMPOSITE_RESOURCE::rasterizeCompositePages(const std::vector<CompositeTileRecord>& records, int surfaceCount, BYTE* colorPages, BYTE* auxPages)
    {

        if (surfaceCount <= 0)
            return 0;
        const PICTURE* base = m_color.picture();
        const int frames = base->totalFrames();

        reloadFirstFrames();
        for (int frame = 0; frame < frames; ++frame)
        {
            int recordIndex = frame;
            while (recordIndex >= 0 && static_cast<std::size_t>(recordIndex) < records.size())
            {
                const CompositeTileRecord& rec = records[static_cast<std::size_t>(recordIndex)];
                if (rec.state >= 0 && rec.width > 0 && rec.height > 0)
                {
                    for (int x = rec.x; x < rec.x + rec.width; ++x)
                    {
                        for (int y = rec.y; y < rec.y + rec.height; ++y)
                        {
                            const int pageX = rec.baseOffset + (x - rec.x);
                            const int stackedY = rec.rowBase + (y - rec.y);
                            const std::size_t wordIndex = static_cast<std::size_t>(stackedY) * 0x100u + static_cast<std::size_t>(pageX);
                            const WORD converted = static_cast<WORD>(convertPixelByFlags(x, y) & 0xFFFFu);
                            writeLe16ToBytes(colorPages + wordIndex * 2u, converted);
                            if (m_flags & 0x04u)
                            {
                                const WORD aux = composeAuxWord(x, y);
                                writeLe16ToBytes(auxPages + wordIndex * 2u, aux);
                            }
                        }
                    }
                }

                if (rec.next == 0)
                    break;
                recordIndex = rec.next;
            }
            loadNextFrames();
        }
        return 0;
    }

    void PICTURE_COMPOSITE_RESOURCE::normalizeAndResolveCompositeRecords(std::vector<CompositeTileRecord>& records) const
    {

        for (CompositeTileRecord& rec : records)
            rec.rowBase = signedModulo256(rec.rowBase);

        for (CompositeTileRecord& rec : records)
        {
            if (rec.state < 0)
            {
                const int duplicateIndex = rec.width;
                rec = records[static_cast<std::size_t>(duplicateIndex)];
            }
        }
    }

    int PICTURE_COMPOSITE_RESOURCE::buildCompositeTilePlan(std::vector<CompositeTileRecord>& records, std::vector<DWORD>& frameKeys)
    {

        records.clear();
        frameKeys.clear();

        const PICTURE* base = m_color.picture();
        const int frames = base->totalFrames();
        if (frames <= 0)
            return 0;

        const int recordCapacity =
            frames * (base->width() / 0x100 + 1) * (base->height() / 0x100 + 1) + 1 + 900;
        records.reserve(static_cast<std::size_t>(recordCapacity));
        records.resize(static_cast<std::size_t>(frames));
        frameKeys.resize(static_cast<std::size_t>(frames));
        for (int frame = 0; frame < frames; ++frame)
        {
            const DWORD key = static_cast<DWORD>(buildPackedWordStream());
            frameKeys[static_cast<std::size_t>(frame)] = key;

            int duplicateIndex = -1;
            for (int i = 0; i < frame; ++i)
            {
                if (frameKeys[static_cast<std::size_t>(i)] == key)
                {
                    duplicateIndex = i;
                    break;
                }
            }

            CompositeTileRecord& root = records[static_cast<std::size_t>(frame)];
            if (duplicateIndex >= 0)
            {
                root.state = -1;
                root.width = duplicateIndex;
                loadNextFrames();
                continue;
            }

            int minX = 0;
            int minY = 0;
            int maxX = 0;
            int maxY = 0;
            scanSolidBounds(&minX, &minY, &maxX, &maxY);

            const int totalWidth = maxX - minX;
            const int totalHeight = maxY - minY;
            if (totalWidth <= 0 || totalHeight <= 0)
            {
                root = CompositeTileRecord{};
                loadNextFrames();
                continue;
            }

            int* previousNext = nullptr;
            bool first = true;
            for (int tileY = minY; tileY < maxY; tileY += 0x100)
            {
                for (int tileX = minX; tileX < maxX; tileX += 0x100)
                {
                    CompositeTileRecord* rec = nullptr;
                    if (first)
                    {
                        rec = &root;
                        first = false;
                    }
                    else
                    {
                        records.push_back(CompositeTileRecord{});
                        const int newIndex = static_cast<int>(records.size()) - 1;
                        if (previousNext)
                            *previousNext = newIndex;
                        rec = &records.back();
                    }

                    rec->state = 0;
                    rec->baseOffset = 0;
                    rec->rowBase = 0;
                    rec->width = std::min(0x100, maxX - tileX);
                    rec->height = std::min(0x100, maxY - tileY);
                    rec->x = tileX;
                    rec->y = tileY;
                    rec->next = 0;
                    previousNext = &rec->next;
                }
            }

            loadNextFrames();
        }

        assignCompositeTileSurfacePages(records);
        return static_cast<int>(records.size());
    }


    int PICTURE_COMPOSITE_RESOURCE::writeVidResource(const STRING& outputPath, DWORD optionFlags)
    {

        const BYTE opt = static_cast<BYTE>(optionFlags & 0xFFu);

        if (opt & 0x10u)
            m_flags |= 0x1000u;
        else if (opt & 0x01u)
            m_flags |= 0x20u;

        if (opt & 0x02u)
            m_flags |= 0x100u;
        if (opt & 0x04u)
            m_flags |= 0x20000u;
        if (opt & 0x08u)
            m_flags |= 0x800u;

        const PICTURE* base = m_color.picture();
        const PICTURE* zpic = m_z.picture();
        if (base->buffer() && base->bytesPerPixel() == 1)
            m_flags |= 0x08u;

        if (m_flags & 0x20u)
            m_flags &= ~0x08u;

        if (!base->buffer() && !zpic->buffer())
        {
            STRING name;
            duplicateBasePictureName(name);
            LOG::Write("!!!ERROR!!!PICTURE '%s': not picture3", name.c_str());
            return 1;
        }

        if ((m_flags & 0x04u) == 0 && (m_flags & 0x1000u) != 0)
        {
            STRING name;
            duplicateBasePictureName(name);
            LOG::Write("!!!ERROR!!!PICTURE '%s': Unsupported files combination", name.c_str());
            return 1;
        }

        STRING realOutput(outputPath);
        if (realOutput.isEmpty())
        {
            const STRING baseName = base->name();
            realOutput = STRING::Concat(baseName.LeftOfFirst(".").c_str(), ".vid");
        }

        as1::RESOURCE resource;
        if (!resource.openFileForWrite(realOutput, as1::RESOURCE::ResTypes::VID))
        {
            STRING name;
            duplicateBasePictureName(name);
            LOG::Write("!!!ERROR!!!PICTURE '%s': file (.vid)", name.c_str());
            return 1;
        }

        resource.BeginSection(as1::RESOURCE::ResTypes::HEAD, 0);
        m_flags |= 0x10u;
        const WORD headFlags = static_cast<WORD>(m_flags & 0xFFFFu);
        const WORD pictureType = static_cast<WORD>(base->type());
        const WORD frameCount = static_cast<WORD>(base->totalFrames());
        const WORD width = static_cast<WORD>(base->width());
        const WORD height = static_cast<WORD>(base->height());
        writeU16Resource(resource, headFlags);
        writeU16Resource(resource, pictureType);
        writeU16Resource(resource, frameCount);
        writeU16Resource(resource, width);
        writeU16Resource(resource, height);
        resource.EndSection();

        if (opt & 0x20u)
            m_flags |= 0x80000u;
        if (opt & 0x40u)
            m_flags |= 0x100000u;

        if (m_flags & 0x1000u)
            writeRawBlockControlFrameDataSections(resource);
        else if (m_flags & 0x80u)
            writeRawArgbFrameDataSections(resource);
        else if ((m_flags & 0x20u) && (m_flags & 0x02u) && (m_flags & 0x04u))
            writeRunLengthFrameDataSections(resource);
        else if (m_flags & 0x20u)
            writeCompositeResourceSections(resource);
        else
            writeRunLengthFrameDataSections(resource);

        resource.close();
        return 0;
    }

    PICTURE_SCROLL_COMPOSITE_RESOURCE::~PICTURE_SCROLL_COMPOSITE_RESOURCE()
    {

    }

    int PICTURE_SCROLL_COMPOSITE_RESOURCE::openFilenames(STRING colorPath, STRING alphaPath, STRING zPath)
    {

        releasePictureData();

        const int nestedOpen = m_nested.openFilenames(colorPath, alphaPath, zPath);
        if (nestedOpen)
            return nestedOpen;

        PICTURE* dst = colorResource().picture();
        const PICTURE* src = m_nested.colorResource().picture();

        dst->allocateBuffer(signedDivide16MinusOne(src->width()),
                            signedDivide16MinusOne(src->height()),
                            src->bytesPerPixel());

        dst->setFrameCount(0x100);
        setFlags(0x01u);
        if (src->bytesPerPixel() == 1)
        {
            dst->palette() = src->palette();
            setFlags(flags() | 0x08u);
        }

        reloadFirstFrames();
        dst->name() = colorPath;
        return 0;
    }

    int PICTURE_SCROLL_COMPOSITE_RESOURCE::loadNextFrames()
    {

        PICTURE_COMPOSITE_RESOURCE::loadNextFrames();

        PICTURE* dst = colorResource().picture();
        const PICTURE* src = m_nested.colorResource().picture();

        const int frame = dst->currentFrame();
        const int xOffset = multiplyWrap32(signedModulo16(frame), addWrap32(dst->width(), 1));
        const int yOffset = multiplyWrap32(signedDivide16(frame), addWrap32(dst->height(), 1));

        for (int y = 0; y < dst->height(); y = addWrap32(y, 1))
        {
            for (int x = 0; x < dst->width(); x = addWrap32(x, 1))
            {
                const DWORD value = src->getPixelRaw(addWrap32(x, xOffset), addWrap32(y, yOffset));
                dst->setPixelRaw(x, y, value);
            }
        }
        return static_cast<int>(reinterpret_cast<std::intptr_t>(dst));
    }

    int PICTURE_SCROLL_COMPOSITE_RESOURCE::reloadFirstFrames()
    {

        PICTURE_COMPOSITE_RESOURCE::reloadFirstFrames();

        PICTURE* dst = colorResource().picture();
        const PICTURE* src = m_nested.colorResource().picture();

        for (int y = 0; y < dst->height(); y = addWrap32(y, 1))
        {
            for (int x = 0; x < dst->width(); x = addWrap32(x, 1))
            {
                const DWORD value = src->getPixelRaw(x, y);
                dst->setPixelRaw(x, y, value);
            }
        }
        return static_cast<int>(reinterpret_cast<std::intptr_t>(dst));
    }


    void PICTURE_COMPOSITE_RESOURCE::writeCompositeResourceSections(as1::RESOURCE& resource)
    {

        const PICTURE* base = m_color.picture();
        const int frameCount = base->totalFrames();

        script::LogicAdaptiveCodec* colorFilter = nullptr;
        script::LogicAdaptiveCodec* auxFilter = nullptr;
        if (m_flags & 0x100u)
        {
            colorFilter = new (std::nothrow) script::LogicAdaptiveCodec();
            if (colorFilter)
                colorFilter->setMode((m_flags & 0x800u) ? 1 : 2);

            auxFilter = new (std::nothrow) script::LogicAdaptiveCodec();
            if (auxFilter)
                auxFilter->setMode(2);
        }

        auto fatalAllocation = [&](const char* label, int value)
        {
            if (value != 0)
                LOG::Write("!!!ERROR!!!PICTURE '%s': %s %d", base->name().c_str(), label, value);
            else
                LOG::Write("!!!ERROR!!!PICTURE '%s': %s", base->name().c_str(), label);
            std::exit(1);
        };

        const auto signedDiv256 = [](int value) noexcept -> int
        {
            return value / 0x100;
        };
        const int pagesYPlusOne = addWrap32(signedDiv256(base->height()), 1);
        const int pagesXPlusOne = addWrap32(signedDiv256(base->width()), 1);
        int planCapacity = multiplyWrap32(pagesYPlusOne, pagesXPlusOne);
        planCapacity = multiplyWrap32(planCapacity, frameCount);
        planCapacity = addWrap32(planCapacity, 1);

        const std::uint32_t controlBytes32 = static_cast<std::uint32_t>(frameCount) * 4u;
        DWORD* frameKeys = static_cast<DWORD*>(::operator new(static_cast<std::size_t>(controlBytes32), std::nothrow));
        if (!frameKeys)
            fatalAllocation("cntrl", 0);

        BYTE* initialShadow = static_cast<BYTE*>(::operator new(0x200000u, std::nothrow));
        if (!initialShadow)
            fatalAllocation("shadow", 0);

        const int recordCapacity = addWrap32(planCapacity, 0x384);
        const std::uint32_t recordBytes32 =
            static_cast<std::uint32_t>(recordCapacity) * static_cast<std::uint32_t>(sizeof(CompositeTileRecord));
        CompositeTileRecord* records = static_cast<CompositeTileRecord*>(
            ::operator new(static_cast<std::size_t>(recordBytes32), std::nothrow));
        if (!records)
            fatalAllocation("texcoor", recordCapacity);

        int recordCount = frameCount;
        for (int frame = 0; frame < frameCount; ++frame)
        {
            const DWORD key = static_cast<DWORD>(buildPackedWordStream());
            frameKeys[frame] = key;

            int duplicateIndex = 0;
            while (duplicateIndex < frame && frameKeys[duplicateIndex] != key)
                ++duplicateIndex;

            CompositeTileRecord& root = records[frame];
            if (duplicateIndex < frame)
            {
                root.state = -1;
                root.width = duplicateIndex;
                loadNextFrames();
                continue;
            }

            int minX = 0;
            int minY = 0;
            int maxX = 0;
            int maxY = 0;
            root.state = 0;
            scanSolidBounds(&minX, &minY, &maxX, &maxY);

            const int fullWidth = maxX - minX;
            const int fullHeight = maxY - minY;
            if (fullWidth <= 0 || fullHeight <= 0)
            {
                root.state = 0;
                root.width = 0;
                root.height = 0;
                root.x = minX;
                root.y = minY;
                root.next = 0;
                loadNextFrames();
                continue;
            }

            if (fullWidth <= 0x100 && fullHeight <= 0x100)
            {
                root.state = 0;
                root.next = 0;
                root.x = minX;
                root.width = fullWidth > 0x100 ? 0x100 : fullWidth;
                root.y = minY;
                root.height = fullHeight > 0x100 ? 0x100 : fullHeight;
            }
            else
            {

                int previousIndex = -1;
                bool first = true;
                for (int tileX = minX; tileX < maxX; tileX = addWrap32(tileX, 0x100))
                {
                    for (int tileY = minY; tileY < maxY; tileY = addWrap32(tileY, 0x100))
                    {
                        int recIndex = frame;
                        if (!first)
                        {
                            recIndex = recordCount;
                            ++recordCount;
                            if (previousIndex >= 0)
                                records[previousIndex].next = recIndex;
                        }
                        first = false;

                        CompositeTileRecord& rec = records[recIndex];
                        rec.state = 0;
                        rec.width = std::min(0x100, maxX - tileX);
                        rec.height = std::min(0x100, maxY - tileY);
                        rec.x = tileX;
                        rec.y = tileY;
                        rec.next = 0;
                        previousIndex = recIndex;
                    }
                }
                if (previousIndex >= 0)
                    records[previousIndex].next = 0;
            }

            loadNextFrames();
        }

        ::operator delete(frameKeys);
        frameKeys = nullptr;

        const std::uint32_t sortBytes32 = static_cast<std::uint32_t>(recordCount) * 4u;
        CompositeTileRecord** sorted = static_cast<CompositeTileRecord**>(
            ::operator new(static_cast<std::size_t>(sortBytes32), std::nothrow));
        if (!sorted)
            fatalAllocation("indices for sort", recordCount);
        for (int i = 0; i < recordCount; ++i)
            sorted[i] = &records[i];
        if (recordCount > 0)
            std::qsort(sorted, static_cast<std::size_t>(recordCount), sizeof(CompositeTileRecord*),
                       &PICTURE_COMPOSITE_RESOURCE::compareCompositeTileRecordPointers);

        int surfaceCount = 0;
        int currentY = 0;
        for (int sortedIndex = 0; sortedIndex < recordCount; ++sortedIndex)
        {
            CompositeTileRecord* rec = sorted[sortedIndex];
            if (rec->state < 0 || rec->height == 0)
                continue;

            if (sortedIndex == 0 || rec->width < sorted[sortedIndex - 1]->width || rec->height < sorted[sortedIndex - 1]->height)
                currentY = 0;

            const int placementLimitY = static_cast<int>(static_cast<std::uint32_t>(planCapacity) << 8u);
            bool placed = false;
            int y = currentY;
            while (y <= placementLimitY - rec->height)
            {
                const int row = y & 0xFF;
                const int pageEnd = (y - row) + 0x100;
                if (row != 0 && y > pageEnd - rec->height && y < pageEnd)
                {
                    y = pageEnd;
                    currentY = y;
                }

                int x = 0;
                const int maxX = 0x100 - rec->width;
                while (x <= maxX)
                {
                    bool overlap = false;
                    for (int i = sortedIndex - 1; i >= 0; --i)
                    {
                        CompositeTileRecord* other = sorted[i];
                        if (other->state < 0)
                            continue;
                        if (rectanglesOverlap(x, y, rec->width, rec->height,
                                              other->baseOffset, other->rowBase, other->width, other->height))
                        {
                            x = other->baseOffset + other->width;
                            overlap = true;
                            break;
                        }
                    }

                    if (!overlap)
                    {
                        const int page = y / 0x100;
                        rec->state = (m_flags & 0x04u) ? multiplyWrap32(page, 2) : page;
                        if (page >= surfaceCount)
                            surfaceCount = page + 1;
                        rec->baseOffset = x;
                        rec->rowBase = y;
                        currentY = y;
                        placed = true;
                        break;
                    }

                    if (x > maxX)
                        break;
                }

                if (placed)
                    break;
                y = addWrap32(y, 1);
                currentY = y;
            }

            if (!placed)
            {
                LOG::Write("!!!ERROR!!!PICTURE '%s': Can't replace rectangle", base->name().c_str());
                std::exit(1);
            }
        }

        ::operator delete(sorted);
        sorted = nullptr;

        reloadFirstFrames();

        BYTE* staging = static_cast<BYTE*>(::operator new(0x200000u, std::nothrow));
        if (!staging)
            fatalAllocation("2097152", 0);

        const std::uint32_t pageBytes32 = static_cast<std::uint32_t>(surfaceCount) << 17u;
        BYTE* colorPages = static_cast<BYTE*>(::operator new(static_cast<std::size_t>(pageBytes32), std::nothrow));
        if (!colorPages)
            fatalAllocation("Buf", surfaceCount);

        BYTE* auxPages = static_cast<BYTE*>(::operator new(static_cast<std::size_t>(pageBytes32), std::nothrow));
        if (!auxPages)
            fatalAllocation("ZBuf", surfaceCount);

        std::memset(colorPages, 0, static_cast<std::size_t>(pageBytes32));
        std::memset(auxPages, 0, static_cast<std::size_t>(pageBytes32));

        for (int frame = 0; frame < frameCount; ++frame)
        {
            CompositeTileRecord* rec = &records[frame];
            if (rec->state >= 0)
            {
                while (rec)
                {
                    for (int x = rec->x; x < rec->x + rec->width; ++x)
                    {
                        for (int y = rec->y; y < rec->y + rec->height; ++y)
                        {
                            const int index = x + rec->baseOffset + ((rec->rowBase + y - rec->y) << 8) - rec->x;
                            if (m_flags & 0x04u)
                                reinterpret_cast<WORD*>(auxPages)[index] = composeAuxWord(x, y);
                            reinterpret_cast<WORD*>(colorPages)[index] =
                                static_cast<WORD>(convertPixelByFlags(x, y) & 0xFFFFu);
                        }
                    }
                    if (rec->next == 0)
                        break;
                    rec = &records[rec->next];
                }
            }
            loadNextFrames();
        }

        for (int i = 0; i < recordCount; ++i)
            records[i].rowBase = signedModulo256(records[i].rowBase);
        for (int i = 0; i < recordCount; ++i)
        {
            if (records[i].state < 0)
                std::memcpy(&records[i], &records[records[i].width], sizeof(CompositeTileRecord));
        }

        resource.BeginSection(as1::RESOURCE::ResTypes::SURFACE, 0);
        const WORD writtenSurfaceCount = static_cast<WORD>((m_flags & 0x04u) ? multiplyWrap32(surfaceCount, 2) : surfaceCount);
        writeU16Resource(resource, writtenSurfaceCount);

        for (int surface = 0; surface < surfaceCount; ++surface)
        {
            int maxX = 0;
            int maxY = 0;
            const int stateKey = (m_flags & 0x04u) ? multiplyWrap32(surface, 2) : surface;
            for (int i = 0; i < recordCount; ++i)
            {
                const CompositeTileRecord& rec = records[i];
                if (rec.state != stateKey)
                    continue;
                maxX = std::max(maxX, rec.baseOffset + rec.width);
                maxY = std::max(maxY, rec.rowBase + rec.height);
            }

            int surfaceWidth = nextPowerOfTwoAtLeast32(maxX);
            int surfaceHeight = nextPowerOfTwoAtLeast32(maxY);
            if (surfaceWidth > 0x100)
                LOG::Write("!!!ERROR!!!PICTURE '%s': SurfSizeX %d", base->name().c_str(), surfaceWidth);
            if (surfaceHeight > 0x100)
                LOG::Write("!!!ERROR!!!PICTURE '%s': SurfSizeY %d", base->name().c_str(), surfaceHeight);

            writeU16Resource(resource, static_cast<WORD>(surfaceWidth));
            writeU16Resource(resource, static_cast<WORD>(surfaceHeight));

            const BYTE* colorSource = colorPages + (static_cast<std::size_t>(surface) << 17u);
            const int colorBytes = copyCompositeSurfaceRows(staging, colorSource, surfaceWidth, surfaceHeight);
            writeU32Resource(resource, static_cast<DWORD>(colorBytes));
            resource.WritePayload(staging, static_cast<unsigned>(colorBytes), colorFilter);

            if (m_flags & 0x04u)
            {
                unsigned auxBytes = 0;
                const unsigned rowBytes = static_cast<unsigned>(surfaceWidth * 2);
                for (int row = 0; row < surfaceHeight; ++row)
                {
                    const BYTE* src = auxPages + (static_cast<std::size_t>(surface) << 17u) + static_cast<std::size_t>(row) * 0x200u;
                    std::memcpy(staging + auxBytes, src, rowBytes);
                    auxBytes += rowBytes;
                }
                writeU32Resource(resource, static_cast<DWORD>(auxBytes));
                resource.WritePayload(staging, auxBytes, auxFilter);
            }
        }
        resource.EndSection();

        resource.BeginSection(as1::RESOURCE::ResTypes::DATA, 0);
        resource.write_new(records, static_cast<std::size_t>(recordCount) * sizeof(CompositeTileRecord));
        resource.EndSection();

        if (colorFilter)
            ::operator delete(static_cast<void*>(colorFilter));
        if (auxFilter)
            ::operator delete(static_cast<void*>(auxFilter));
        ::operator delete(colorPages);
        ::operator delete(auxPages);
        ::operator delete(records);
        ::operator delete(staging);
        ::operator delete(initialShadow);
    }

    DWORD PICTURE_COMPOSITE_RESOURCE::convertPixelByFlags(int x, int y)
    {

        if (!insideBase(x, y))
            return 0;

        const PICTURE* base = m_color.picture();
        if (m_flags & 0x1000u)
        {
            if (m_flags & 0x08u)
                return base->getPixelRaw(x, y);

            const DWORD c = composeArgbPixel(x, y);
            int a = static_cast<int>((c >> 24u) & 0xFFu);
            int r = static_cast<int>((c >> 16u) & 0xFFu);
            int g = static_cast<int>((c >> 8u) & 0xFFu);
            int b = static_cast<int>(c & 0xFFu);
            if (!isSolidAt(x, y))
                a = 0;
            if ((m_flags & 0x01u) && (m_flags & 0x02u))
            {
                r = std::min(255, (255 * r) / a);
                g = std::min(255, (255 * g) / a);
                b = std::min(255, (255 * b) / a);
                return (static_cast<DWORD>(a) << 24u) |
                       (static_cast<DWORD>(r) << 16u) |
                       (static_cast<DWORD>(g) << 8u) |
                       static_cast<DWORD>(b);
            }
            const DWORD alphaBit = static_cast<DWORD>(a & 0x80u);
            return (static_cast<DWORD>(b) >> 3u) |
                   (4u * ((static_cast<DWORD>(g) & 0xF8u) |
                          (32u * ((static_cast<DWORD>(r) & 0xF8u) | (2u * alphaBit)))));
        }

        if (m_flags & 0x08u)
        {
            if (m_flags & 0x02u)
            {
                const DWORD c = composeArgbPixel(x, y);
                return static_cast<DWORD>(nearestPaletteIndex(c));
            }
            return base->getPixelRaw(x, y);
        }

        const DWORD c = composeArgbPixel(x, y);
        const int a = static_cast<int>((c >> 24u) & 0xFFu);
        int r = static_cast<int>((c >> 16u) & 0xFFu);
        int g = static_cast<int>((c >> 8u) & 0xFFu);
        int b = static_cast<int>(c & 0xFFu);

        if ((m_flags & 0x01u) && (m_flags & 0x02u))
        {
            const int highAlpha = a & 0xF0;
            if (highAlpha == 0)
                return 0;
            r = (255 * r / a) >> 4;
            g = (255 * g / a) >> 4;
            b = (255 * b / a) >> 4;
            if (r > 0x0F) r = 0x0F;
            if (g > 0x0F) g = 0x0F;
            if (b > 0x0F) b = 0x0F;
            return static_cast<DWORD>(b | (16 * (g | (16 * (highAlpha | r)))));
        }

        if ((m_flags & 0x04u) && (m_flags & 0x02u))
            return static_cast<DWORD>((g & 0xF0) | ((r & 0xF0) << 4) | (b >> 4));

        return static_cast<DWORD>((b >> 3) | ((g & 0xFC) << 3) | ((r & 0xF8) << 8));
    }

} }
