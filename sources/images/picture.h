#pragma once

#include "core/as_string.h"
#include "core/types.h"
#include <array>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace as1 { class RESOURCE; }

namespace as1 { namespace images
{
    class PICTURE;
    DWORD readPicturePixelRaw(const PICTURE* owner, int x, int y) noexcept;
    DWORD* readPicturePixelColor(const PICTURE* owner, DWORD* destination, int x, int y) noexcept;

    class PICTURE
    {
    public:
        PICTURE();
        PICTURE(int width, int height, int bytesPerPixel);
        virtual ~PICTURE();

        void resetDefaults();
        void allocateBuffer(int width, int height, int bytesPerPixel);
        std::intptr_t allocatePicturePixels(int width, int height, int bytesPerPixel) noexcept;

        virtual int loadNextFrame();
        virtual int reloadFirstFrame();
        virtual int openFilename(const STRING& path);
        virtual int release();

        int saveTGA(const STRING& path, int x, int y, int width, int height) const;

        DWORD getPixelRaw(int x, int y) const;
        void setPixelRaw(int x, int y, DWORD value);
        void setPixelRgb565Aware(int x, int y, DWORD value);
        void writePicturePixelRaw(int x, int y, DWORD value) noexcept;
        void writePicturePixelColor(int x, int y, DWORD value) noexcept;
        DWORD getPixelForTGA(int x, int y) const;

        int width() const { return m_width; }
        int height() const { return m_height; }
        int bytesPerPixel() const { return m_bytesPerPixel; }
        int totalFrames() const { return m_frameCount; }
        void setFrameCount(int value) noexcept { m_frameCount = value; }
        int currentFrame() const { return m_currentFrame; }
        int type() const { return m_type; }
        const BYTE* buffer() const { return m_buffer; }
        BYTE* buffer() { return m_buffer; }
        const STRING& name() const { return m_name; }
        STRING& name() { return m_name; }
        std::array<DWORD, 256>& palette() { return m_palette; }
        const std::array<DWORD, 256>& palette() const { return m_palette; }

    protected:
        void reportPictureError(int errorCode, const char* detailText, int detailValue) const;
        std::size_t bufferSize() const;
        bool isInside(int x, int y) const;
        FILE* reopenCurrentName();
        bool seekDataOffset();
        void lowerCaseStoredName();

        int m_frameCount = 0;
        int m_currentFrame = 0;
        int m_type = 0x47;
        int m_width = 0;
        int m_height = 0;
        int m_bytesPerPixel = 0;
        STRING m_name;
        std::FILE* m_file = nullptr;
        std::array<DWORD, 256> m_palette;
        BYTE* m_buffer = nullptr;
        int m_dataOffset;
    };

    class PICTURE_TGA final : public PICTURE
    {
    public:
        int loadNextFrame() override;
        int reloadFirstFrame() override;
        int openFilename(const STRING& path) override;

    private:
        BYTE m_tgaImageType;
        int readAndValidateHeader(void* rawHeader);
    };

    class PICTURE_Z final : public PICTURE
    {
    public:
        int loadNextFrame() override;
        int openFilename(const STRING& path) override;
    };

    class PICTURE_FLC final : public PICTURE
    {
    public:
        int loadNextFrame() override;
        int openFilename(const STRING& path) override;

    private:
        WORD m_flicMagic;
    };

    class PICTURE_BMP final : public PICTURE
    {
    public:
        int loadNextFrame() override;
        int reloadFirstFrame() override;
        int openFilename(const STRING& path) override;

    private:
        int readAndValidateHeader(void* rawHeader);
    };

    class PICTURE_RESOURCE
    {
    public:
        PICTURE_RESOURCE();
        PICTURE_RESOURCE(int width, int height, int pictureType);
        virtual ~PICTURE_RESOURCE();
        virtual int loadNextFrame();
        virtual int reloadFirstFrame();
        virtual int openFilename(const STRING& path);
        void writePictureResourcePixel(int x, int y, DWORD value) noexcept;
        PICTURE* picture() { return m_picture; }
        const PICTURE* picture() const { return m_picture; }
        int pictureType() const { return m_pictureType; }

    private:
        PICTURE* m_picture;
        int m_pictureType;
    };


    class PICTURE_COMPOSITE_RESOURCE
    {
    public:
        PICTURE_COMPOSITE_RESOURCE();
        virtual ~PICTURE_COMPOSITE_RESOURCE();

        virtual int loadNextFrames();
        virtual int reloadFirstFrames();
        virtual int openFilenames(STRING colorPath, STRING alphaPath, STRING zPath);
        virtual int releasePictureData();
        int buildPackedWordStream();

        DWORD composeArgbPixel(int x, int y) const;
        int alphaAt(int x, int y) const;
        WORD composeAuxWord(int x, int y) const;
        struct CompositeBounds
        {
            int minX = 0;
            int minY = 0;
            int maxXExclusive = 0;
            int maxYExclusive = 0;
        };

        struct CompositeTileRecord
        {
            int reserved0 = 0;
            int state = 0;
            int baseOffset = 0;
            int rowBase = 0;
            int width = 0;
            int height = 0;
            int x = 0;
            int y = 0;
            int next = 0;
        };

        DWORD convertPixelByFlags(int x, int y);
        int nearestPaletteIndex(DWORD argb) const;
        void replacePaletteLookup(DWORD argb, BYTE index);
        int isSolidAt(int x, int y);
        WORD auxWordForFirstSolidInRect(int x, int y, int width, int height);
        void scanSolidBounds(int* minX, int* minY, int* maxXExclusive, int* maxYExclusive);
        int copyCompositeSurfaceRows(BYTE* destination, const BYTE* source, int width, int height) const;
        int buildShadowDotControlWords(WORD* outWords);
        int buildAdaptivePaletteLookupFromFrames();
        STRING& duplicateBasePictureName(STRING& out) const;
        int anySolidInRect(int x, int y, int width, int height);
        int writeRawArgbFrameDataSections(as1::RESOURCE& resource);
        void writeRawBlockControlFrameDataSections(as1::RESOURCE& resource);
        void writeRunLengthFrameDataSections(as1::RESOURCE& resource);
        int buildCompositeTilePlan(std::vector<CompositeTileRecord>& records, std::vector<DWORD>& frameKeys);
        void writeCompositeResourceSections(as1::RESOURCE& resource);
        int writeVidResource(const STRING& outputPath, DWORD optionFlags);
        static int compareCompositeTileRecordPointers(const void* lhs, const void* rhs);

        PICTURE_RESOURCE& colorResource() { return m_color; }
        PICTURE_RESOURCE& alphaResource() { return m_alpha; }
        PICTURE_RESOURCE& zResource() { return m_z; }
        const PICTURE_RESOURCE& colorResource() const { return m_color; }
        const PICTURE_RESOURCE& alphaResource() const { return m_alpha; }
        const PICTURE_RESOURCE& zResource() const { return m_z; }

        DWORD flags() const { return m_flags; }
        void setFlags(DWORD flags) { m_flags = flags; }
        std::array<DWORD, 256>& palette() { return m_palette; }
        const std::array<DWORD, 256>& palette() const { return m_palette; }

    private:
        bool insideBase(int x, int y) const;
        int assignCompositeTileSurfacePages(std::vector<CompositeTileRecord>& records) const;
        int rasterizeCompositePages(const std::vector<CompositeTileRecord>& records, int surfaceCount, BYTE* colorPages, BYTE* auxPages);
        void normalizeAndResolveCompositeRecords(std::vector<CompositeTileRecord>& records) const;

        PICTURE_RESOURCE m_color;
        PICTURE_RESOURCE m_alpha;
        PICTURE_RESOURCE m_z;
        std::array<DWORD, 256> m_palette;
        BYTE* m_paletteLookup = nullptr;
        DWORD m_flags = 0;
    };


    class PICTURE_SCROLL_COMPOSITE_RESOURCE final : public PICTURE_COMPOSITE_RESOURCE
    {
    public:
        ~PICTURE_SCROLL_COMPOSITE_RESOURCE();

        int openFilenames(STRING colorPath, STRING alphaPath, STRING zPath) override;
        int loadNextFrames() override;
        int reloadFirstFrames() override;

        PICTURE_COMPOSITE_RESOURCE& nestedComposite() { return m_nested; }
        const PICTURE_COMPOSITE_RESOURCE& nestedComposite() const { return m_nested; }

    private:
        PICTURE_COMPOSITE_RESOURCE m_nested;
    };
#if defined(_WIN32) && !defined(_WIN64)

                                                                                
                                                                                        
                                                                                    
                                                                                        
                                                                                        
                                                                                                 
                                                                                                                      
                                                                                                                                    
#endif

} }
