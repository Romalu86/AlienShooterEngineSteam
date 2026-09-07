#include "graph.h"

#include "core/application.h"
#include "core/configuration.h"
#include "core/resource.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "sprite_collector_hash.h"
#include "vid/vid.h"
#include "vid/vid_software.h"
#include "vid/vid_software16.h"
#include "vid/vid_surface.h"
#include "vid/vid_texcoor.h"
#include "graphics/base_texture.h"
#include "graphics/gamma.h"
#include "images/picture.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <memory>
#include <new>
#include <utility>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include "win/application_win.h"
#include "d3d8.h"
#if defined(_M_IX86)
                                                    
                                                                    
#endif
#include "win/dialog_item.h"
#endif


namespace as1
{
    int graphRetailFtolLow32(float value) noexcept;

#if !defined(_MSC_VER) || !defined(_M_IX86)
    struct GraphHostState
    {
        GraphDeviceInitState deviceInitState{};
        GraphDisplayModeCatalog displayModeCatalog{};
        std::string deviceStatus;
        bool deviceReady = false;
        bool depthStencilEnabled = false;
        unsigned int startupFlags = 0u;
        int startupColorDepth = 16;
        bool startupFullscreen = true;
        bool startupVSync = true;
        STRING startupFontFace{"Arial"};
        int startupFontSizeX = 0;
        int startupFontSizeY = 8;
        DWORD lastChunkGamma = 0u;
        bool lastGammaRefreshChanged = false;
        std::size_t lastGammaRefreshScannedSlots = 0u;
        std::size_t lastGammaRefreshRequests = 0u;
        std::size_t lastGammaRefreshLoadedSlots = 0u;
        std::size_t lastGammaRefreshSoftwareBlockedSlots = 0u;
        bool useSoftZBuffer = false;
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        bool useMapCamera = false;
    };

    namespace
    {
        std::unordered_map<const GRAPH*, GraphHostState>& graphHostStateTable()
        {
            static std::unordered_map<const GRAPH*, GraphHostState> table;
            return table;
        }
    }

    GraphHostState& GRAPH::hostState() noexcept
    {
        return graphHostStateTable()[this];
    }

    const GraphHostState& GRAPH::hostState() const noexcept
    {
        auto& table = graphHostStateTable();
        auto it = table.find(this);
        if (it == table.end())
            it = table.emplace(this, GraphHostState{}).first;
        return it->second;
    }

    void GRAPH::releaseHostState() noexcept
    {
        graphHostStateTable().erase(this);
    }

#define m_deviceInitState (hostState().deviceInitState)
#define m_displayModeCatalog (hostState().displayModeCatalog)
#define m_deviceStatus (hostState().deviceStatus)
#define m_deviceReady (hostState().deviceReady)
#define m_depthStencilEnabled (hostState().depthStencilEnabled)
#define m_startupFlags (hostState().startupFlags)
#define m_startupColorDepth (hostState().startupColorDepth)
#define m_startupFullscreen (hostState().startupFullscreen)
#define m_startupVSync (hostState().startupVSync)
#define m_startupFontFace (hostState().startupFontFace)
#define m_startupFontSizeX (hostState().startupFontSizeX)
#define m_startupFontSizeY (hostState().startupFontSizeY)
#define m_lastChunkGamma (hostState().lastChunkGamma)
#define m_lastGammaRefreshChanged (hostState().lastGammaRefreshChanged)
#define m_lastGammaRefreshScannedSlots (hostState().lastGammaRefreshScannedSlots)
#define m_lastGammaRefreshRequests (hostState().lastGammaRefreshRequests)
#define m_lastGammaRefreshLoadedSlots (hostState().lastGammaRefreshLoadedSlots)
#define m_lastGammaRefreshSoftwareBlockedSlots (hostState().lastGammaRefreshSoftwareBlockedSlots)
#define m_useSoftZBuffer (hostState().useSoftZBuffer)
#endif
#ifdef _WIN32
#define m_d3d9PresentParameters (*reinterpret_cast<D3DPRESENT_PARAMETERS*>(m_presentParameters))
#endif
    DWORD g_color16RedMask = 0xF800u;
    DWORD g_color16GreenMask = 0x07E0u;
    DWORD g_color16RedShift = 8u;
    DWORD g_color16GreenShift = 3u;
    DWORD g_packedSoftwareDepth = 0u;
    WORD g_softwareDepthWordPrimary = 0u;
    WORD g_softwareDepthWordSecondary = 0u;
    const DWORD* g_softwarePaletteLookup = nullptr;
    int g_softwareClipLeft = 0;
    int g_softwareClipRight = 0;
    int g_softwareClipTop = 0;
    int g_softwareClipBottom = 0;

    namespace
    {
        GRAPH* g_currentGraph = nullptr;

        std::uint32_t g_renderPulseStartTime = 0u;
        float g_renderPulseBaseScale = -1.0f;

        float g_frameCameraShiftX = 0.0f;
        float g_frameCameraShiftY = 0.0f;

        std::uint32_t g_snowLightRampStartTime = 0u;
        std::uint32_t g_snowLightIntensity = 0u;

        std::uint32_t g_lightScratchTextureToggle = 0u;

        std::uint32_t g_moviePlaybackFrame = 0u;
        std::uint32_t g_moviePlaybackLastTime = 0u;

        char g_d3dFormatNameBuffer[256] = "Unknown";

        GammaRawPair graphLegacyGammaIndexToRaw(std::uint32_t packed)
        {

            std::uint32_t first = 0u;
            std::uint32_t second = 0u;

            if ((packed & 0x00000080u) != 0u)
                second = ((~packed) & 0x0000007Fu) << 1u;
            else
                first = (packed & 0x0000007Fu) << 1u;

            if ((packed & 0x00008000u) != 0u)
                second |= ((~packed) & 0x00007F80u) << 1u;
            else
                first |= (packed & 0x00007F80u) << 1u;

            if ((packed & 0x00800000u) != 0u)
                second |= ((~packed) & 0x007F8000u) << 1u;
            else
                first |= (packed & 0x007F8000u) << 1u;

            if ((packed & 0x80000000u) != 0u)
                second |= ((~packed) & 0xFF800000u) << 1u;
            else
                first |= (packed & 0xFF800000u) << 1u;

            return GammaRawPair{first, second};
        }

        int graphSignedGammaChannel(const GammaRawPair& raw, unsigned int shift) noexcept
        {
            const int negative = static_cast<int>((raw.first >> shift) & 0xFFu);
            if (negative != 0)
                return -negative;
            return static_cast<int>((raw.second >> shift) & 0xFFu);
        }

        GammaRawPair graphInterpolateGammaRawPair(const GammaRawPair& from,
                                                   const GammaRawPair& to,
                                                   float t) noexcept
        {
            const auto lane = [&from, &to, t](unsigned int shift) noexcept -> int
            {
                const int first = graphSignedGammaChannel(from, shift);
                const int second = graphSignedGammaChannel(to, shift);
                const float interpolated =
                    static_cast<float>(second - first) * t + static_cast<float>(first);
                return static_cast<int>(interpolated);
            };

            const int blue = lane(0u);
            const int green = lane(8u);
            const int red = lane(16u);
            const int alpha = lane(24u);
            GammaRawPair out{};
            out.setSignedDeltasRetail(alpha, red, green, blue);
            return out;
        }

        DWORD graphFloatBits(float value)
        {
            DWORD bits = 0u;
                                                                                          
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }


        struct GraphWeatherVertex44
        {
            float x;
            float y;
            float z;
            float rhw;
            DWORD color;
        };
                                                                                                               

        struct GraphWeatherLineParticle
        {
            GraphWeatherVertex44 vertex[2];
        };
                                                                                                            

        struct GraphWeatherCrossParticle
        {
            GraphWeatherVertex44 vertex[6];
        };
                                                                                                                

        std::array<GraphWeatherLineParticle, 250> g_lineParticles{};
        std::array<GraphWeatherCrossParticle, 1000> g_crossParticles{};
        int g_lineParticleCount = 0;
        int g_crossParticleCount = 0;
        DWORD g_lineParticleWindDirection = 999u;
        float g_lineParticleWindSpeed = 999.0f;
        DWORD g_crossParticleWindDirection = 999u;
        float g_crossParticleWindSpeed = 999.0f;
        float g_lineParticleWindX = 0.0f;
        float g_crossParticleWindX = 0.0f;
        float g_crossParticlePreviousNegatedCameraX = 0.0f;
        float g_crossParticlePreviousNegatedCameraY = 0.0f;

        constexpr float kGraphRand32767 = 0.000030518509f;
        constexpr float kGraphRand268435456 = 0.000000003725404f;
        constexpr float kGraphOneOver8192 = 0.00012207031f;
        constexpr float kGraphOneOver819_2 = 0.0012207031f;
        constexpr float kGraphOneOver200 = 0.0049999999f;

        float graphWeatherWindX(DWORD direction, float speed)
        {

            return SPRITE::rawDirectionSinUnchecked(direction) * speed * 1000.0f;
        }

        void graphTransformCrossParticle(GraphWeatherCrossParticle& particle)
        {
            const float middleX = (particle.vertex[1].x + particle.vertex[0].x) * 0.5f;
            const float middleY = (particle.vertex[1].y + particle.vertex[0].y) * 0.5f;
            for (GraphWeatherVertex44& vertex : particle.vertex)
            {
                const float oldX = vertex.x;
                const float oldY = vertex.y;
                vertex.x = middleX + oldY - middleY;
                vertex.y = oldX + middleY - middleX;
            }
        }

        int clampGraphByte(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return value;
        }

        std::uint16_t packGraphIntensityWord(int high, int green, int low, bool r5g6b5)
        {
            high = clampGraphByte(high);
            green = clampGraphByte(green);
            low = clampGraphByte(low);
            const int highShift = r5g6b5 ? 8 : 7;
            const int greenShift = r5g6b5 ? 3 : 2;
            const int greenMask = r5g6b5 ? 0x07E0 : 0x03E0;
            return static_cast<std::uint16_t>(
                ((high & 0xF8) << highShift) |
                ((green << greenShift) & greenMask) |
                ((low >> 3) & 0x1F));
        }

        void buildGraphIntensityPalette(std::array<std::uint16_t, 256>& table, bool r5g6b5)
        {

            for (int v = 8, base = 0; base < 256; v += 8, base += 8)
            {
                table[base + 0] = packGraphIntensityWord(v - 8, v - 8, v - 8, r5g6b5);
                table[base + 1] = packGraphIntensityWord(v - 7, v - 7, v, r5g6b5);
                table[base + 2] = packGraphIntensityWord(v, v - 6, v - 6, r5g6b5);
                table[base + 3] = packGraphIntensityWord(v, v - 5, v, r5g6b5);

                table[base + 4] = packGraphIntensityWord(v - 4, v, v - 4, r5g6b5);
                table[base + 5] = packGraphIntensityWord(v - 3, v, v, r5g6b5);
                table[base + 6] = packGraphIntensityWord(v, v, v - 2, r5g6b5);
                table[base + 7] = packGraphIntensityWord(v, v, v, r5g6b5);
            }
        }

        bool graphSnowEdgeIntensity(std::uint16_t center, std::uint16_t neighbor, std::uint32_t& intensity)
        {
            const int delta = std::abs(static_cast<int>(center) - static_cast<int>(neighbor));
            if (delta > 6)
                return false;
            intensity = (g_snowLightIntensity * static_cast<std::uint32_t>(6 - delta)) >> 3u;
            return true;
        }

        int signedHalfTowardZero(int value)
        {
            const int sign = value < 0 ? -1 : 0;
            return (value - sign) >> 1;
        }

        int signedHalfFromFloatTowardZero(float value)
        {
            return signedHalfTowardZero(graphRetailFtolLow32(value));
        }

        DWORD floatRaw(float value)
        {
            DWORD raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            return raw;
        }

        float rawFloat(DWORD raw)
        {
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            return value;
        }

        DWORD graphGrayRgb(DWORD value)
        {
            // Exact owner expression: v | ((v | (v << 8)) << 8).  Do not
            // clamp to one byte: effect 10 intentionally produces 0x100 at
            // elapsed==0 before the packed shifts.
            return value | ((value | (value << 8u)) << 8u);
        }

        DWORD graphScaleRgb(DWORD color, DWORD scale256)
        {

            const DWORD redProductRaw = static_cast<DWORD>(
                static_cast<std::uint64_t>(color) * static_cast<std::uint64_t>(scale256));
            std::int32_t redProductSigned = 0;
            std::memcpy(&redProductSigned, &redProductRaw, sizeof(redProductSigned));
            const DWORD red = static_cast<DWORD>(redProductSigned >> 8) & 0xFF0000u;
            const DWORD green = (((color & 0xFF00u) * scale256) >> 8u) & 0xFF00u;
            const DWORD blue = ((color & 0xFFu) * scale256) >> 8u;
            return red + green + blue;
        }

        void logMovieError(int errorCode, const char* detailText, int detailValue)
        {
            LOG::ResourceError("%s", errorCode, detailText, detailValue, "MOVIE");
        }

#ifdef _WIN32
        std::string hresultText(const char* op, HRESULT hr)
        {
            std::ostringstream ss;
            ss << op << " failed: HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
            return ss.str();
        }

        IDirect3D8* graphD3D(void* p) { return static_cast<IDirect3D8*>(p); }
        IDirect3DDevice8* graphDevice(void* p) { return static_cast<IDirect3DDevice8*>(p); }

        void appendTextureArgumentName(char* destination, DWORD value)
        {
            const unsigned char low = static_cast<unsigned char>(value);
            if ((low & 0x20u) != 0u)
                std::strcat(destination, "alp-");
            if ((low & 0x10u) != 0u)
                std::strcat(destination, "inv-");

            switch (low & 0x0Fu)
            {
            case 2u: std::strcat(destination, "tex"); break;
            case 0u: std::strcat(destination, "dif"); break;
            case 4u: std::strcat(destination, "spec"); break;
            case 1u: std::strcat(destination, "cur"); break;
            case 3u: std::strcat(destination, "tfac"); break;
            default: break;
            }
        }

        const char* textureOperationName(DWORD value) noexcept
        {
            switch (value)
            {
            case 1u: return "dis";
            case 2u: return "sel1";
            case 3u: return "sel2";
            case 4u: return "mod";
            case 13u: return "tex_alpha";
            default: return "unknown";
            }
        }

        const char* buildTextureStageDebugText()
        {
            static char text[0x40C];
            IDirect3DDevice8* const device = graphDevice(GRAPH::CurrentDevice());
            DWORD value;

            std::strcpy(text, "Op=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(1u), &value);
            std::strcat(text, textureOperationName(value));

            std::strcat(text, " Arg1=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(2u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " Arg2=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(3u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " AOp=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(4u), &value);
            std::strcat(text, textureOperationName(value));

            std::strcat(text, " Arg1=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(5u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " Arg2=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(6u), &value);
            appendTextureArgumentName(text, value);
            return text;
        }

        void publishBaseTextureCaps(void* deviceRaw)
        {
            IDirect3DDevice8* const device = graphDevice(deviceRaw);
            D3DCAPS8 caps{};
            const HRESULT capsResult = device->GetDeviceCaps(&caps);
            if (capsResult < 0)
                LOG::ResourceError("TEXTURE", 9, "Caps", 0);

            BaseTextureCaps textureCaps;
            textureCaps.maxWidth = static_cast<int>(caps.MaxTextureWidth);
            textureCaps.maxHeight = static_cast<int>(caps.MaxTextureHeight);
            textureCaps.requireSquare = (caps.TextureCaps & 0x20u) != 0u;
            textureCaps.requirePowerOfTwo = (caps.TextureCaps & 0x2u) != 0u;
            textureCaps.dynamicTextures = false;
            textureCaps.paletteTextures = false;
            textureCaps.compressedFormatMask =
                BASE_TEXTURE::RuntimeGlobals().compressedFormatMask | 0x15u; // DXT1|DXT3|DXT5

            BASE_TEXTURE::ConfigureCaps(textureCaps);
        }

        void invokeBaseTextureRetailDeleteSlot00(BASE_TEXTURE* texture) noexcept
        {
            if (!texture)
                return;
#if defined(_MSC_VER) && defined(_M_IX86)
            void** const vtable = *reinterpret_cast<void***>(texture);
            using ScalarDeletingOwner = void* (__thiscall*)(void*, unsigned char);
            reinterpret_cast<ScalarDeletingOwner>(vtable[0])(texture, 1u);
#else
            delete texture;
#endif
        }

        void deleteBaseTextureThroughRetailSlot00(BASE_TEXTURE*& texture) noexcept
        {
            if (!texture)
                return;
            invokeBaseTextureRetailDeleteSlot00(texture);
            texture = nullptr;
        }
        IDirect3DSurface8* graphSurface(void* p) { return static_cast<IDirect3DSurface8*>(p); }
        IDirect3DTexture8* graphTexture(void* p) { return static_cast<IDirect3DTexture8*>(p); }

        int pauseMovieMediaControl(void* rawComOwner)
        {

#ifdef _WIN32
            void** const slots = static_cast<void**>(rawComOwner);
            IMediaControl* const mediaControl = static_cast<IMediaControl*>(slots[1]);
            if (!mediaControl)
                return 0;
            return static_cast<int>(mediaControl->Pause());
#else
            (void)rawComOwner;
            return 0;
#endif
        }

        int nextPowerOfTwo(int v)
        {
            int out = 1;
            while (out < v && out < 4096)
                out <<= 1;
            return out;
        }

        struct TextureVertex
        {
            float x, y, z, rhw;
            DWORD color;
            float u, v;
        };

        constexpr DWORD kTextureVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;


        bool isAllowedDisplayMode(const as1::core::StartupSettingsBlock& startupSettings,
                         DWORD width, DWORD height, DWORD colorBits)
        {

            const std::uint32_t* colorTable = startupSettings.allowedColorBits;
            int index = 0;
            DWORD value = colorTable[0];
            while (value != 0u)
            {
                if (colorBits == value)
                    break;
                ++index;
                value = colorTable[index];
            }

            if (startupSettings.allowedColorBits[index] == 0u || startupSettings.allowedWidths[0] == 0u)
                return false;

            index = 0;
            value = startupSettings.allowedWidths[0];
            while (value != 0u)
            {
                if (width == value && height == startupSettings.allowedHeights[index])
                    return true;
                ++index;
                value = startupSettings.allowedWidths[index];
            }
            return false;
        }


#ifdef _WIN32
        DWORD chooseDepthStencilFormat(IDirect3D8* d3d, UINT adapter, D3DFORMAT format)
        {
            const D3DFORMAT depthFormats[] = {
                D3DFMT_D16,
                D3DFMT_D32,
                D3DFMT_D24X8,
                D3DFMT_D24S8
            };
            for (D3DFORMAT depth : depthFormats)
            {
                if (d3d->CheckDepthStencilMatch(adapter, D3DDEVTYPE_HAL, format, format, depth) == D3D_OK)
                    return static_cast<DWORD>(depth);
            }
            return 0;
        }
#endif

        float effectiveRenderZForStaticDraw(DWORD property, float spriteZ, float groundZ)
        {
            (void)property;
            (void)groundZ;
            return spriteZ;
        }
#endif
    }


    int graphRetailFtolLow32(float value) noexcept
    {
        const double d = static_cast<double>(value);
        if (!std::isfinite(d) ||
            d >= 9223372036854775808.0 || d < -9223372036854775808.0)
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
        return static_cast<int>(static_cast<std::uint32_t>(converted));
    }

    bool graphRetailFcompC3Equal(float lhs, float rhs) noexcept
    {

        return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
    }

    bool graphRetailFcompC0(float lhs, float rhs) noexcept
    {

        return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
    }

    struct GraphTextGlyph
    {
        float left;
        float top;
        float right;
        float bottom;
    };

#ifdef _WIN32
    namespace
    {
        constexpr DWORD kTextVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
        constexpr float kTextVertexZ = 0.899999976f;
        constexpr float kTextVertexRhw = 1.0f;
        constexpr UINT kTextVertexBufferBytes = 0x20D0u;
        constexpr UINT kTextVertexStride = 0x1Cu;
        constexpr UINT kTextVertexBufferUsage = 0x208u;
        constexpr DWORD kTextVertexLockFlags = 0x2000u;

        struct GraphTextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = kTextVertexZ;
            float rhw = kTextVertexRhw;
            DWORD color = 0xFFFFFFFFu;
            float u = 0.0f;
            float v = 0.0f;
        };
    }
#endif



    class GraphTextFont
    {
    public:
        GraphTextFont(const STRING& face, int sizeX, int sizeY, DWORD flags)
        {
            (void)initializeFontDescriptor(face.c_str(), sizeX, sizeY, flags);
        }

        GraphTextFont* initializeFontDescriptor(const char* face, int sizeX, int sizeY, DWORD flags)
        {

            std::strcpy(m_face, face);
            m_sizeY = sizeY;
            m_sizeX = sizeX;
            m_flags = flags;
#ifdef _WIN32
            m_device = nullptr;
            m_texture = nullptr;
            m_vertexBuffer = nullptr;
#else
            m_device = 0u;
            m_texture = 0u;
            m_vertexBuffer = 0u;
#endif
#ifdef _WIN32
            m_savedStateBlock = nullptr;
            m_textStateBlock = nullptr;
#endif
            return this;
        }

        ~GraphTextFont()
        {
#ifdef _WIN32
            if (m_savedStateBlock)
                m_savedStateBlock->Release();
            if (m_textStateBlock)
                m_textStateBlock->Release();
            m_savedStateBlock = nullptr;
            m_textStateBlock = nullptr;
#endif
        }

        const char* face() const { return m_face; }
        int sizeX() const { return m_sizeX; }
        int sizeY() const { return m_sizeY; }
        DWORD flags() const { return m_flags; }
        bool isReady() const
        {
#ifdef _WIN32
            return m_vertexBuffer != nullptr;
#else
            return false;
#endif
        }

        int releaseResetSensitiveResources()
        {
#ifdef _WIN32
            if (m_vertexBuffer)
            {
                m_vertexBuffer->Release();
                m_vertexBuffer = nullptr;
            }
            if (m_device)
            {
                if (m_savedStateBlock)
                    m_savedStateBlock->Release();
                if (m_textStateBlock)
                    m_textStateBlock->Release();
            }
            m_savedStateBlock = nullptr;
            m_textStateBlock = nullptr;
#endif
            return 0;
        }

        int releaseTextureAndDevice()
        {
#ifdef _WIN32
            if (m_texture)
            {
                m_texture->Release();
                m_texture = nullptr;
            }
            m_device = nullptr;
#endif
            return 0;
        }

        int releaseFontResources()
        {
            releaseResetSensitiveResources();
            return releaseTextureAndDevice();
        }

        void release() { (void)releaseFontResources(); }

#ifdef _WIN32
        int createFontAtlasTexture(IDirect3DDevice8* device, std::string* status)
        {
            m_device = device;

            m_textureWidth = atlasSizeForHeight(m_sizeY);
            m_textureHeight = m_textureWidth;
            m_scale = 1.0f;

            D3DCAPS8 caps;
            device->GetDeviceCaps(&caps);
            if (static_cast<DWORD>(m_textureWidth) > caps.MaxTextureWidth)
            {
                m_scale = static_cast<float>(caps.MaxTextureWidth) / static_cast<float>(m_textureWidth);
                m_textureWidth = static_cast<int>(caps.MaxTextureWidth);
                m_textureHeight = static_cast<int>(caps.MaxTextureWidth);
            }

            HRESULT hr = device->CreateTexture(
                static_cast<UINT>(m_textureWidth),
                static_cast<UINT>(m_textureHeight),
                1,
                0,
                D3DFMT_A4R4G4B4,
                D3DPOOL_MANAGED,
                &m_texture,
                nullptr);
            if (FAILED(hr) || !m_texture)
            {
                if (status)
                    *status = hresultText("font texture", hr);
                return static_cast<int>(hr);
            }

            if (!buildAtlas(status))
                return static_cast<int>(E_FAIL);

            return 0;
        }

        int createFontDeviceResources()
        {
            if (!m_device)
                return static_cast<int>(E_FAIL);

            HRESULT hr = m_device->CreateVertexBuffer(
                kTextVertexBufferBytes,
                kTextVertexBufferUsage,
                0,
                D3DPOOL_DEFAULT,
                &m_vertexBuffer,
                nullptr);
            if (FAILED(hr) || !m_vertexBuffer)
                return static_cast<int>(hr);

            if (!createStateBlocks(nullptr))
                return static_cast<int>(E_FAIL);

            return 0;
        }

        bool draw(float x, float y, DWORD color, const char* text, DWORD flags, std::string* status)
        {
            if (!m_device)
                return false;
            if (m_savedStateBlock)
                m_savedStateBlock->Capture();
            if (m_textStateBlock)
                m_textStateBlock->Apply();

            m_device->SetFVF(kTextVertexFVF);
            m_device->SetPixelShader(nullptr);
            m_device->SetStreamSource(0, m_vertexBuffer, 0u, kTextVertexStride);
            m_device->SetTexture(0, m_texture);
            if ((flags & 4u) != 0)
            {
                m_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                m_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            }

            void* raw = nullptr;
            HRESULT hr = m_vertexBuffer->Lock(0, 0, &raw, kTextVertexLockFlags);
            if (FAILED(hr) || !raw)
            {
                if (status)
                    *status = hresultText("font vertex lock", hr);
                return false;
            }

            GraphTextVertex* vertices = reinterpret_cast<GraphTextVertex*>(raw);
            UINT primitiveCount = 0;
            UINT vertexCount = 0;
            bool ok = true;
            float cursorX = x;
            float cursorY = y;
            const float lineStep = (m_glyphs[0].bottom - m_glyphs[0].top) * static_cast<float>(m_textureHeight);

            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
            {
                const unsigned char ch = *p;
                if (ch == '\n')
                {
                    cursorX = x;
                    cursorY += lineStep;
                }
                const GraphTextGlyph& g = m_glyphs[ch];
                const float glyphW = (g.right - g.left) * static_cast<float>(m_textureWidth) / m_scale;
                const float glyphH = (g.bottom - g.top) * static_cast<float>(m_textureHeight) / m_scale;
                if (ch == ' ')
                {
                    cursorX += glyphW;
                    continue;
                }

                const float x0 = cursorX - 0.5f;
                const float y0 = cursorY - 0.5f;
                const float x1 = cursorX + glyphW - 0.5f;
                const float y1 = cursorY + glyphH - 0.5f;
                const float u0 = g.left;
                const float v0 = g.top;
                const float u1 = g.right;
                const float v1 = g.bottom;
                GraphTextVertex quad[6] = {
                    {x0, y0, kTextVertexZ, kTextVertexRhw, color, u0, v0},
                    {x1, y0, kTextVertexZ, kTextVertexRhw, color, u1, v0},
                    {x1, y1, kTextVertexZ, kTextVertexRhw, color, u1, v1},
                    {x0, y0, kTextVertexZ, kTextVertexRhw, color, u0, v0},
                    {x1, y1, kTextVertexZ, kTextVertexRhw, color, u1, v1},
                    {x0, y1, kTextVertexZ, kTextVertexRhw, color, u0, v1},
                };
                std::copy(quad, quad + 6, vertices + vertexCount);
                vertexCount += 6u;
                primitiveCount += 2u;
                if (primitiveCount > 98u)
                {
                    hr = flush(primitiveCount, status);
                    if (FAILED(hr))
                    {
                        ok = false;
                        break;
                    }
                    raw = nullptr;
                    hr = m_vertexBuffer->Lock(0, 0, &raw, kTextVertexLockFlags);
                    if (FAILED(hr) || !raw)
                    {
                        if (status)
                            *status = hresultText("font vertex lock", hr);
                        ok = false;
                        break;
                    }
                    vertices = reinterpret_cast<GraphTextVertex*>(raw);
                    primitiveCount = 0;
                    vertexCount = 0;
                }
                cursorX += glyphW;
            }

            HRESULT unlockHr = m_vertexBuffer->Unlock();
            if (FAILED(unlockHr) && ok)
            {
                if (status)
                    *status = hresultText("font vertex unlock", unlockHr);
                ok = false;
            }
            if (ok && primitiveCount > 0)
            {
                hr = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitiveCount);
                if (FAILED(hr))
                {
                    if (status)
                        *status = hresultText("font draw", hr);
                    ok = false;
                }
            }

            if (m_savedStateBlock)
                m_savedStateBlock->Apply();
            return ok;
        }
#endif

        int drawFontText(float x, float y, DWORD color, const char* text, DWORD flags)
        {
#ifdef _WIN32
            return draw(x, y, color, text, flags, nullptr)
                ? 0
                : static_cast<int>(E_FAIL);
#else
            (void)x;
            (void)y;
            (void)color;
            (void)text;
            (void)flags;
            return 0;
#endif
        }

    private:
        static int atlasSizeForHeight(int sizeY)
        {
            if (sizeY > 40)
                return 1024;
            if (sizeY > 20)
                return 512;
            return 256;
        }

#ifdef _WIN32
        bool buildAtlas(std::string* status)
        {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = m_textureWidth;
            bmi.bmiHeader.biHeight = -m_textureHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

            SelectObject(dc, bitmap);
            SetMapMode(dc, MM_TEXT);
            const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
            const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
            const int fontHeight = -MulDiv(m_sizeY, static_cast<int>(static_cast<float>(dpiY) * m_scale), 72);
            const int fontWidth = -MulDiv(m_sizeX, static_cast<int>(static_cast<float>(dpiX) * m_scale), 72);
            const int weight = (m_flags & 1u) ? FW_BOLD : FW_NORMAL;
            const BOOL italic = (m_flags & 2u) ? TRUE : FALSE;
            const DWORD pitchAndFamily = 2u - ((m_flags & 8u) != 0u ? 1u : 0u);
            HFONT font = CreateFontA(fontHeight, fontWidth, 0, 0, weight, italic, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     ANTIALIASED_QUALITY, pitchAndFamily, m_face);
            if (!font)
                return false;
            SelectObject(dc, font);
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextAlign(dc, TA_TOP | TA_LEFT);

            int penX = 0;
            int penY = 0;
            for (int ch = 0; ch < 256; ++ch)
            {
                char c = static_cast<char>(ch);
                SIZE extent{};
                GetTextExtentPoint32A(dc, &c, 1, &extent);
                if (penX + extent.cx + 1 > m_textureWidth)
                {
                    penX = 0;
                    penY += extent.cy + 1;
                }
                ExtTextOutA(dc, penX, penY, ETO_OPAQUE, nullptr, &c, 1, nullptr);
                GraphTextGlyph& glyph = m_glyphs[static_cast<unsigned char>(ch)];
                glyph.left = static_cast<float>(penX) / static_cast<float>(m_textureWidth);
                glyph.top = static_cast<float>(penY) / static_cast<float>(m_textureHeight);
                glyph.right = static_cast<float>(penX + extent.cx) / static_cast<float>(m_textureWidth);
                glyph.bottom = static_cast<float>(penY + extent.cy) / static_cast<float>(m_textureHeight);
                penX += extent.cx + 1;
            }

            D3DLOCKED_RECT locked;
            m_texture->LockRect(0, &locked, nullptr, 0);
            const DWORD* src = static_cast<const DWORD*>(bits);
            for (int y = 0; y < m_textureHeight; ++y)
            {
                std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
                for (int x = 0; x < m_textureWidth; ++x)
                {
                    const DWORD pixel = src[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_textureWidth) + static_cast<std::size_t>(x)];
                    const BYTE alpha = static_cast<BYTE>((pixel >> 4) & 0x0Fu);
                    dst[x] = alpha ? static_cast<std::uint16_t>((alpha << 12) | 0x0FFFu) : 0;
                }
            }
            m_texture->UnlockRect(0);

            DeleteObject(bitmap);
            DeleteDC(dc);
            DeleteObject(font);
            (void)status;
            return true;
        }

        bool createStateBlocks(std::string* status)
        {
            HRESULT hr = m_device->BeginStateBlock();
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state begin", hr);
                return false;
            }
            applyTextRenderState();
            hr = m_device->EndStateBlock(&m_savedStateBlock);
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state end", hr);
                return false;
            }

            hr = m_device->BeginStateBlock();
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state begin", hr);
                return false;
            }
            applyTextRenderState();
            hr = m_device->EndStateBlock(&m_textStateBlock);
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state end", hr);
                return false;
            }
            return true;
        }

        void applyTextRenderState()
        {
            m_device->SetTexture(0, m_texture);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(7), (m_flags & 4u) != 0u ? 1u : 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(27), 1u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(19), 5u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(20), 6u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(15), 1u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(24), 8u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(25), 7u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(8), 3u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(22), 3u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(52), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(136), 1u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(40), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(152), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(151), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(167), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(28), 0u);

            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 4u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(2), 2u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(3), 0u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 4u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(5), 2u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(6), 0u);
            m_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            m_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            m_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            m_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(24), 0u);
            m_device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 1u);
            m_device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 1u);
        }

        HRESULT flush(UINT primitiveCount, std::string* status)
        {
            HRESULT unlockHr = m_vertexBuffer->Unlock();
            if (FAILED(unlockHr))
            {
                if (status)
                    *status = hresultText("font vertex unlock", unlockHr);
                return unlockHr;
            }
            HRESULT drawHr = S_OK;
            if (primitiveCount > 0)
                drawHr = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitiveCount);
            if (status && FAILED(drawHr))
                *status = hresultText("font draw", drawHr);
            return drawHr;
        }
#endif

        char m_face[80];                         // +0x000
        int m_sizeY;                             // +0x050
        int m_sizeX;                             // +0x054
        DWORD m_flags;                           // +0x058
#ifdef _WIN32
        IDirect3DDevice8* m_device;              // +0x05C
        IDirect3DTexture8* m_texture;            // +0x060
        IDirect3DVertexBuffer8* m_vertexBuffer;  // +0x064
#else
        DWORD m_device;
        DWORD m_texture;
        DWORD m_vertexBuffer;
#endif
        int m_textureWidth;                      // +0x068
        int m_textureHeight;                     // +0x06C
        float m_scale;                           // +0x070
        GraphTextGlyph m_glyphs[256];            // +0x074..+0x1073
#ifdef _WIN32
        IDirect3DStateBlock9* m_savedStateBlock;   // +0x1074
        IDirect3DStateBlock9* m_textStateBlock;    // +0x1078
#else
        DWORD m_savedStateBlock;
        DWORD m_textStateBlock;
#endif
    };

#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                           
                                                                                      
#endif

    namespace
    {
        GraphTextFont* allocateGraphTextFontRetail(const STRING& face, int sizeX, int sizeY)
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            void* const storage = ::operator new(0x107Cu);
#else
            void* const storage = ::operator new(sizeof(GraphTextFont));
#endif

            if (!storage)
                return nullptr;
            return new (storage) GraphTextFont(face, sizeX, sizeY, 8u);
        }

        void destroyGraphTextFontRetail(GraphTextFont*& font)
        {
            if (!font)
                return;
            (void)font->releaseFontResources();
            font->~GraphTextFont();
            ::operator delete(font);
            font = nullptr;
        }

        int retailDisplayFormatBits(DWORD format) noexcept;
    }

    GraphTextFont* createRetailVidFontOwner(const STRING& face, int sizeX, int sizeY)
    {
        GraphTextFont* const owner = allocateGraphTextFontRetail(face, sizeX, sizeY);
        if (!owner)
            return nullptr;
#ifdef _WIN32
        IDirect3DDevice8* const device = graphDevice(GRAPH::CurrentDevice());
        if (device)
        {
            (void)owner->createFontAtlasTexture(device, nullptr);
            (void)owner->createFontDeviceResources();
        }
#endif
        return owner;
    }

    void destroyRetailVidFontOwner(GraphTextFont*& owner) noexcept
    {
        destroyGraphTextFontRetail(owner);
    }

    int invalidateRetailVidFontOwner(GraphTextFont* owner) noexcept
    {
        return owner ? owner->releaseResetSensitiveResources() : 0;
    }

    int restoreRetailVidFontOwner(GraphTextFont* owner) noexcept
    {
#ifdef _WIN32
        return owner ? owner->createFontDeviceResources() : 0;
#else
        (void)owner;
        return 0;
#endif
    }

    bool drawRetailVidFontOwner(GraphTextFont* owner, float x, float y, DWORD color,
                                const char* text, DWORD flags) noexcept
    {
#ifdef _WIN32
        return owner ? owner->draw(x, y, color, text ? text : "", flags, nullptr) : false;
#else
        (void)owner; (void)x; (void)y; (void)color; (void)text; (void)flags;
        return false;
#endif
    }


    GRAPH* GRAPH::initializeRetailGraphState(const as1::core::StartupSettingsBlock& startupSettings)
    {


        for (GraphAdapterRecord& record : m_adapterRecords)
        {
            record.description[0] = '\0';
            record.displayModeCount = 0u;
            DWORD oldCaps = 0u;
            std::memcpy(&oldCaps, &record.capabilityFlags, sizeof(oldCaps));
            record.capabilityFlags = oldCaps & 0xFFFFFFF0u;
        }

        m_effectGammaPair.first = 0u;
        m_effectGammaPair.second = 0u;
        m_gammaPair.first = 0u;
        m_gammaPair.second = 0u;
        m_movieComObjects[0] = nullptr;
        m_movieComObjects[1] = nullptr;
        m_movieComObjects[2] = nullptr;
        m_movieComObjects[3] = nullptr;
        m_direct3D = nullptr;
        m_device = nullptr;
        m_textFont = nullptr;
        m_lightBuffer = nullptr;
        m_hiBuffer = nullptr;
        m_alphaBuffer = nullptr;
        m_tempBuffer = nullptr;
        m_backBuffer = nullptr;
        m_softwareDepthBuffer = nullptr;
        m_lockedBackBufferPixels = nullptr;
        m_renderFlags = 0u;
        m_deviceLifecycleState = 0u;
        m_selectedDisplayFormat = 0u;
        m_adapterDisplayFormat = 0u;
        m_depthStencilFormat = 0u;
        m_viewportLeft = 0.0f;
        m_viewportRight = 0.0f;
        m_viewportTop = 0.0f;
        m_viewportBottom = 0.0f;
        m_pendingGraphOpA = -1;
        m_pendingGraphOpB = -1;
        m_pendingGraphOpC = -1;
        m_windDirection = 0xDCu;
        m_windSpeed = 20.0f;

        DWORD flags = m_graphFlags;
        flags = (flags & ~0x80u) | ((startupSettings.flags & 0x1u) ? 0x80u : 0u);
        flags = (flags & ~0x21u) | ((startupSettings.flags & 0x2u) ? 0x20u : 0u);
        flags &= ~0x400u;
        m_graphFlags = flags;

        setEffect(0, 0, 0, 0);

#ifdef _WIN32
        m_direct3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!m_direct3D)
        {
            logAndShowError(g_fileLogger, "Can't create Direct3D9");
            return this;
        }

        IDirect3D8* const d3d = graphD3D(m_direct3D);
        m_adapterCount = 0u;

        for (UINT adapter = 0; adapter < d3d->GetAdapterCount(); ++adapter)
        {
            buildAdapterRecord(m_adapterRecords[adapter], d3d, static_cast<int>(adapter), startupSettings);
            ++m_adapterCount;
        }
#endif

        m_selectedAdapterIndex = startupSettings.device;
        m_sizeX = static_cast<float>(startupSettings.screenWidth);
        m_sizeY = static_cast<float>(startupSettings.screenHeight);
        m_graphFlags = (m_graphFlags & 0xFFFFFFFDu) | (startupSettings.colorDepth == 32 ? 0x2u : 0u);
        m_graphFlags = (m_graphFlags & ~0x10u) | (startupSettings.fullscreen != 0 ? 0x10u : 0u);
        if (m_selectedAdapterIndex >= static_cast<int>(m_adapterCount))
            m_selectedAdapterIndex = 0;
        return this;
    }

    void GRAPH::SetStartupFont(const STRING& face, int sizeX, int sizeY)
    {
        (void)rebuildTextFont(face, sizeX, sizeY);
    }

    int GRAPH::rebuildTextFont(const STRING& face, int sizeX, int sizeY)
    {

        GraphTextFont* const oldFont = m_textFont;
        if (oldFont)
        {
            (void)oldFont->releaseFontResources();
            oldFont->~GraphTextFont();
            ::operator delete(oldFont);
        }

        GraphTextFont* const replacement = allocateGraphTextFontRetail(face, sizeX, sizeY);
        m_textFont = replacement;
#ifdef _WIN32
        (void)m_textFont->createFontAtlasTexture(graphDevice(m_device), nullptr);
        return m_textFont->createFontDeviceResources();
#else
        return 0;
#endif
    }

    const char* GRAPH::D3DFormatToString(DWORD format)
    {

        const char* source = nullptr;
        switch (format)
        {
        case 20: source = "R8G8B8"; break;
        case 21: source = "A8R8G8B8"; break;
        case 22: source = "X8R8G8B8"; break;
        case 23: source = "R5G6B5"; break;
        case 24: source = "X1R5G5B5"; break;
        case 25: source = "A1R5G5B5"; break;
        case 26: source = "A4R4G4B4"; break;
        case 28: source = "A8"; break;
        case 40: source = "A8P8"; break;
        case 41: source = "P8"; break;
        case 50: source = "L8"; break;
        case 51: source = "A8L8"; break;
        case 52: source = "A4L4"; break;
        case 60: source = "V8U8"; break;
        case 70: source = "D16_LOCKABLE"; break;
        case 71: source = "D32"; break;
        case 73: source = "D15S1"; break;
        case 75: source = "D24S8"; break;
        case 77: source = "D24X8"; break;
        case 80: source = "D16"; break;
        case 100: source = "VERTEXDATA"; break;
        case 101: source = "INDEX16"; break;
        case 102: source = "INDEX32"; break;
        case 0x31545844u: source = "DXT1"; break;
        case 0x32545844u: source = "DXT2"; break;
        case 0x33545844u: source = "DXT3"; break;
        case 0x34545844u: source = "DXT4"; break;
        case 0x35545844u: source = "DXT5"; break;
        default: break;
        }
        if (source)
            std::strcpy(g_d3dFormatNameBuffer, source);
        return g_d3dFormatNameBuffer;
    }

    namespace
    {
        int retailDisplayFormatBits(DWORD format) noexcept
        {

            switch (format)
            {
            case 0x15u: // D3DFMT_A8R8G8B8
            case 0x16u: // D3DFMT_X8R8G8B8
                return 32;
            case 0x17u: // D3DFMT_R5G6B5
            case 0x18u: // D3DFMT_X1R5G5B5
            case 0x19u: // D3DFMT_A1R5G5B5
            case 0x1Au: // D3DFMT_A4R4G4B4
                return 16;
            case 0x14u: // D3DFMT_R8G8B8
                return 24;
            case 0x29u: // D3DFMT_P8
                return 8;
            default:
                return 0;
            }
        }
    }

    int GraphAdapterRecord::findDisplayModeIndex(int width, int height, int bitsPerPixel) const noexcept
    {

        for (DWORD index = 0; index < displayModeCount; ++index)
        {
            if (static_cast<int>(displayModeWidths[index]) == width &&
                static_cast<int>(displayModeHeights[index]) == height &&
                retailDisplayFormatBits(displayModeFormats[index]) == bitsPerPixel)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    STRING& GraphAdapterRecord::formatDisplayModeLabel(STRING& output, int modeIndex) const
    {
        bool multipleFormats = false;
        if (displayModeCount > 1u)
        {
            const DWORD firstFormat = displayModeFormats[0];
            for (DWORD index = displayModeCount - 1u; index >= 1u; --index)
            {
                if (displayModeFormats[index] != firstFormat)
                {
                    multipleFormats = true;
                    break;
                }
                if (index == 1u)
                    break;
            }
        }

        const DWORD index = static_cast<DWORD>(modeIndex);
        if (!multipleFormats)
            constructFormattedString(output, "%i x %i", static_cast<int>(displayModeWidths[index]), static_cast<int>(displayModeHeights[index]));
        else
            constructFormattedString(output, "%i x %i x %ibpp",
                       static_cast<int>(displayModeWidths[index]),
                       static_cast<int>(displayModeHeights[index]),
                       retailDisplayFormatBits(displayModeFormats[index]));
        return output;
    }

    const GraphAdapterRecord& GRAPH::selectedAdapterRecord() const noexcept
    {
        return m_adapterRecords[static_cast<std::size_t>(m_selectedAdapterIndex)];
    }

    GraphAdapterRecord& GRAPH::selectedAdapterRecord() noexcept
    {
        return m_adapterRecords[static_cast<std::size_t>(m_selectedAdapterIndex)];
    }

#ifdef _WIN32
    int GRAPH::syncDisplayModeDialog(const win::DialogItemRef& deviceRef,
                          const win::DialogItemRef& modeRef,
                          const win::DialogItemRef* fullscreenRef)
    {
        if (deviceRef.sendControlMessage(CB_GETCOUNT, 0, 0) != 0)
        {
            const LRESULT selection = deviceRef.currentSelection();
            m_selectedAdapterIndex = static_cast<int>(deviceRef.sendControlMessage(CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
        }
        else
        {
            if (fullscreenRef)
                fullscreenRef->sendControlMessage(BM_SETCHECK, (m_graphFlags >> 4u) & 1u, 0);

            deviceRef.sendControlMessage(CB_RESETCONTENT, 0, 0);
            for (DWORD adapter = 0; adapter < m_adapterCount; ++adapter)
            {
                const GraphAdapterRecord& record = m_adapterRecords[adapter];
                const LRESULT item = deviceRef.sendControlMessage(CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(record.description));
                if (item != CB_ERR)
                    deviceRef.sendControlMessage(CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(adapter));
                if (static_cast<int>(adapter) == m_selectedAdapterIndex)
                    deviceRef.sendControlMessage(CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            }
        }

        GraphAdapterRecord& record = selectedAdapterRecord();

        if (modeRef.sendControlMessage(CB_GETCOUNT, 0, 0) != 0)
        {
            const LRESULT selection = modeRef.currentSelection();
            const DWORD packed = static_cast<DWORD>(modeRef.sendControlMessage(CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
            m_sizeX = static_cast<float>(packed & 0x7FFFu);
            m_sizeY = static_cast<float>(static_cast<std::int32_t>(packed) >> 16u);
            m_graphFlags = (m_graphFlags & ~0x2u) | ((packed >> 14u) & 0x2u);
        }

        const int currentWidth = static_cast<int>(m_sizeX);
        const int currentHeight = static_cast<int>(m_sizeY);
        int currentMode = record.findDisplayModeIndex(currentWidth, currentHeight, (m_graphFlags & 0x2u) != 0u ? 32 : 16);
        if (currentMode < 0)
        {
            m_sizeX = static_cast<float>(record.displayModeWidths[0]);
            m_sizeY = static_cast<float>(record.displayModeHeights[0]);
            m_graphFlags = (m_graphFlags & ~0x2u) |
                           (retailDisplayFormatBits(record.displayModeFormats[0]) == 32 ? 0x2u : 0u);
        }

        if (fullscreenRef)
        {
            bool fullscreenAvailable = false;
            if ((record.capabilityFlags & 0x1u) != 0u &&
                ((m_graphFlags >> 1u) & 1u) == (retailDisplayFormatBits(record.desktopDisplayFormat) == 32))
            {
                fullscreenAvailable = static_cast<float>(GetSystemMetrics(SM_CXSCREEN)) >= m_sizeX &&
                                      static_cast<float>(GetSystemMetrics(SM_CYSCREEN)) >= m_sizeY;
            }

            if (fullscreenAvailable)
            {
                EnableWindow(GetDlgItem(fullscreenRef->dialog, fullscreenRef->controlId), TRUE);
            }
            else
            {
                fullscreenRef->sendControlMessage(BM_SETCHECK, BST_CHECKED, 0);
                EnableWindow(GetDlgItem(fullscreenRef->dialog, fullscreenRef->controlId), FALSE);
            }

            setFullscreenRequested(fullscreenRef->isChecked());
        }

        modeRef.sendControlMessage(CB_RESETCONTENT, 0, 0);
        for (DWORD index = 0; index < record.displayModeCount; ++index)
        {
            STRING text;
            record.formatDisplayModeLabel(text, static_cast<int>(index));
            const int bits = retailDisplayFormatBits(record.displayModeFormats[index]);
            const DWORD packed = (bits == 32 ? 0x8000u : 0u) |
                                 (record.displayModeWidths[index] & 0x7FFFu) |
                                 ((record.displayModeHeights[index] & 0xFFFFu) << 16u);
            const LRESULT item = modeRef.sendControlMessage(CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            if (item != CB_ERR)
                modeRef.sendControlMessage(CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(packed));
            if (static_cast<float>(record.displayModeWidths[index]) == m_sizeX &&
                static_cast<float>(record.displayModeHeights[index]) == m_sizeY &&
                ((m_graphFlags >> 1u) & 1u) == (bits == 32))
            {
                modeRef.sendControlMessage(CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            }
        }

        return m_selectedAdapterIndex;
    }
#endif

    void GRAPH::buildAdapterRecord(GraphAdapterRecord& record, void* direct3D, int adapter,
                                   const as1::core::StartupSettingsBlock& startupSettings)
    {
#ifdef _WIN32
        IDirect3D8* d3d = graphD3D(direct3D);

        D3DADAPTER_IDENTIFIER8 identifier;
        d3d->GetAdapterIdentifier(static_cast<UINT>(adapter), 0u, &identifier);
        D3DDISPLAYMODE desktop;
        d3d->GetAdapterDisplayMode(static_cast<UINT>(adapter), &desktop);
        std::strncpy(record.description, identifier.Description, 0x28u);
        record.desktopDisplayFormat = static_cast<DWORD>(desktop.Format);

        record.displayModeCount = 0u;
        const D3DFORMAT enumFormat = D3DFMT_X8R8G8B8;
        const UINT count = d3d->GetAdapterModeCount(static_cast<UINT>(adapter), enumFormat);
        for (UINT index = count; index > 0u; --index)
        {
            D3DDISPLAYMODE mode;
            d3d->EnumAdapterModes(static_cast<UINT>(adapter), enumFormat, index - 1u, &mode);

            const DWORD format = static_cast<DWORD>(mode.Format);
            DWORD colorBits = 0u;
            if (format == 21u || format == 22u)
                colorBits = 32u;
            else if (format == 23u || format == 24u)
                colorBits = 16u;
            else
                continue;

            if (!isAllowedDisplayMode(startupSettings, mode.Width, mode.Height, colorBits))
                continue;
            if (record.findDisplayModeIndex(static_cast<int>(mode.Width), static_cast<int>(mode.Height), static_cast<int>(colorBits)) >= 0)
                continue;

            const DWORD slot = record.displayModeCount;
            record.displayModeWidths[slot] = mode.Width;
            record.displayModeHeights[slot] = mode.Height;
            record.displayModeFormats[slot] = format;
            record.depthStencilFormats[slot] = chooseDepthStencilFormat(d3d, static_cast<UINT>(adapter), mode.Format);
            record.displayModeCount = slot + 1u;
            const char* const depthName = D3DFormatToString(record.depthStencilFormats[slot]);
            const char* const colorName = D3DFormatToString(format);
            LOG::Write("   Enum display modes %ix%i %s %s",
                       static_cast<int>(mode.Width),
                       static_cast<int>(mode.Height),
                       colorName, depthName);
        }

        D3DCAPS8 caps{};
        (void)d3d->GetDeviceCaps(static_cast<UINT>(adapter), D3DDEVTYPE_HAL, &caps);

        record.videoMemoryBudgetBytes = 0x007A1200u;
        record.capabilityFlags = (record.capabilityFlags & 0xFFFFFFFDu) | 0x1u;
#else
        (void)record;
        (void)direct3D;
        (void)adapter;
#endif
    }

    GRAPH::GRAPH()
    {

    }

    GRAPH::~GRAPH()
    {
        deinit();
#if !defined(_MSC_VER) || !defined(_M_IX86)
        releaseHostState();
#endif
    }

    bool GRAPH::isDeviceReady() const { return m_device != nullptr; }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    const std::string& GRAPH::deviceStatus() const { return hostState().deviceStatus; }
    const GraphDeviceInitState& GRAPH::deviceInitState() const { return hostState().deviceInitState; }
    const GraphDisplayModeCatalog& GRAPH::displayModeCatalog() const { return hostState().displayModeCatalog; }
#endif
    GraphViewportState GRAPH::viewportState() const
    {
        GraphViewportState state{};
        state.left = m_viewportLeft;
        state.right = m_viewportRight;
        state.top = m_viewportTop;
        state.bottom = m_viewportBottom;
        state.viewportX = static_cast<DWORD>(graphRetailFtolLow32(m_viewportLeft));
        state.viewportY = static_cast<DWORD>(graphRetailFtolLow32(m_viewportTop));
        state.viewportWidth = static_cast<DWORD>(graphRetailFtolLow32(m_viewportRight - m_viewportLeft));
        state.viewportHeight = static_cast<DWORD>(graphRetailFtolLow32(m_viewportBottom - m_viewportTop));
        state.minZ = 0.0f;
        state.maxZ = 1.0f;
        return state;
    }
    void GRAPH::SetCamera(float x, float y)
    {
        auto& drawState = core::GlobalApplicationDrawDispatcherState();
        drawState.setCameraShiftX(x);
        drawState.setCameraShiftY(y);
    }
    float GRAPH::cameraX() const { return core::GlobalApplicationDrawDispatcherState().cameraShiftX(); }
    float GRAPH::cameraY() const { return core::GlobalApplicationDrawDispatcherState().cameraShiftY(); }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    bool GRAPH::lastGammaRefreshChanged() const { return hostState().lastGammaRefreshChanged; }
    size_t GRAPH::lastGammaRefreshScannedSlots() const { return hostState().lastGammaRefreshScannedSlots; }
    size_t GRAPH::lastGammaRefreshRequests() const { return hostState().lastGammaRefreshRequests; }
    size_t GRAPH::lastGammaRefreshLoadedSlots() const { return hostState().lastGammaRefreshLoadedSlots; }
    size_t GRAPH::lastGammaRefreshSoftwareBlockedSlots() const { return hostState().lastGammaRefreshSoftwareBlockedSlots; }
    DWORD GRAPH::lastChunkGamma() const { return hostState().lastChunkGamma; }
    bool GRAPH::isUseSoftZBuffer() const { return hostState().useSoftZBuffer; }
    const STRING& GRAPH::startupFontFace() const { return hostState().startupFontFace; }
    int GRAPH::startupFontSizeX() const { return hostState().startupFontSizeX; }
    int GRAPH::startupFontSizeY() const { return hostState().startupFontSizeY; }
#endif

    void* GRAPH::CurrentDeviceHandle()
    {
        return CurrentDevice();
    }

    void* GRAPH::CurrentDevice()
    {

        return g_currentGraph ? g_currentGraph->deviceHandle() : nullptr;
    }

    GRAPH* GRAPH::CurrentGraph()
    {
        return g_currentGraph;
    }

    void GRAPH::BindCurrentGraph(GRAPH* graph) noexcept
    {

        g_currentGraph = graph;
    }

    int GRAPH::setAlphaBlendFactors(DWORD srcBlend, DWORD dstBlend)
    {

#ifdef _WIN32
        IDirect3DDevice8* const device = graphDevice(m_device);
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1Bu), 1u);
        m_graphFlags |= 0x00000200u;
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x13u), srcBlend);
        return static_cast<int>(device->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(0x14u), dstBlend));
#else
        (void)srcBlend;
        (void)dstBlend;
        return 0;
#endif
    }

    void GRAPH::reloadPaletteLightBuffer()
    {

        if (!m_lightBuffer || m_lightBuffer->format() != 41u)
            return;

#ifdef _WIN32
        invokeBaseTextureRetailDeleteSlot00(m_lightBuffer);
#else
        delete m_lightBuffer;
#endif
        m_lightBuffer = new BASE_TEXTURE(256, 256, 41u, 0u);
        if (!m_lightBuffer->isLoaded())
            LOG::ResourceError("%s", 3, "Light at RelodPalette()", 0, "GRAPH");

        if (m_lightBuffer->format() == 41u)
        {
            std::array<DWORD, 256> palette{};
            for (DWORD value = 0; value < 256u; ++value)
                palette[value] = 0xFF000000u | value | (value << 8u) | (value << 16u);
            m_lightBuffer->createPaletteSlot(palette.data());
        }
        LOG::Write("ReloadPalettes");
    }

    void GRAPH::drawBackBufferPixel2x2(float x, float y, DWORD color)
    {

        lockBackBuffer();

        if (!(x >= static_cast<double>(m_viewportLeft)) ||
            !(y >= static_cast<double>(m_viewportTop)) ||
            !(x < static_cast<double>(m_viewportRight) - 1.0) ||
            !(y < static_cast<double>(m_viewportBottom) - 1.0))
            return;

        const int ix = graphRetailFtolLow32(x);
        const int iy = graphRetailFtolLow32(y);
        if ((m_graphFlags & 2u) != 0u)
        {
            DWORD* const pixels = static_cast<DWORD*>(m_lockedBackBufferPixels);
            pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1] = color;
            pixels[ix + iy * m_backBufferPitchPixels + 1] = color;
            pixels[ix + (iy + 1) * m_backBufferPitchPixels] = color;
            pixels[ix + iy * m_backBufferPitchPixels] = color;
            return;
        }

        WORD* const pixels = static_cast<WORD*>(m_lockedBackBufferPixels);
        const WORD packed = static_cast<WORD>(
            ((color >> 3u) & 0x1Fu) |
            (g_color16RedMask & (color >> (16u - g_color16RedShift))) |
            (g_color16GreenMask & (color >> (8u - g_color16GreenShift))));
        pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1] = packed;
        pixels[ix + iy * m_backBufferPitchPixels + 1] = packed;
        pixels[ix + (iy + 1) * m_backBufferPitchPixels] = packed;
        pixels[ix + iy * m_backBufferPitchPixels] = packed;
    }

    DWORD* GRAPH::sampleBackBufferPixel(DWORD* colorOut, float x, float y)
    {

        lockBackBuffer();
        if (!(x >= static_cast<double>(m_viewportLeft) &&
              x < static_cast<double>(m_viewportRight) &&
              y >= static_cast<double>(m_viewportTop) &&
              y < static_cast<double>(m_viewportBottom)))
        {
            *colorOut = 0xFF000000u;
            return colorOut;
        }

        const int ix = graphRetailFtolLow32(x);
        const int iy = graphRetailFtolLow32(y);
        const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(ix) +
                                     static_cast<std::ptrdiff_t>(iy) * static_cast<std::ptrdiff_t>(m_backBufferPitchPixels);
        if ((m_graphFlags & 2u) != 0)
        {
            *colorOut = static_cast<const DWORD*>(m_lockedBackBufferPixels)[index];
        }
        else
        {
            const WORD raw = static_cast<const WORD*>(m_lockedBackBufferPixels)[index];
            *colorOut = 8u * (raw & 0x1Fu) |
                        ((static_cast<DWORD>(raw) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                        ((static_cast<DWORD>(raw) << (16u - g_color16RedShift)) & 0x00FF0000u);
        }
        return colorOut;
    }

    int GRAPH::captureBackBufferRegionTga(const STRING& outputPath, int x, int y, int width, int height)
    {

        images::PICTURE_RESOURCE pictureResource(width, height, 1);
        images::PICTURE* picture = pictureResource.picture();
        for (int py = 0; py < height; ++py)
        {
            for (int px = 0; px < width; ++px)
            {
                DWORD color = 0;
                sampleBackBufferPixel(&color, static_cast<float>(px + x), static_cast<float>(py + y));
                pictureResource.writePictureResourcePixel(px, py, color);
            }
        }
        return picture->saveTGA(outputPath, 0, 0, -1, -1);
    }

    DWORD GRAPH::advanceMovieFrameClock(VID* movieVid)
    {

#ifdef _WIN32
        const DWORD sampledTime = ::timeGetTime();
        core::SetRealTimeMilliseconds(sampledTime);
#else
        const DWORD sampledTime = core::RealTimeMilliseconds();
#endif
        DWORD result = sampledTime;
        if (movieVid)
        {
            result -= g_moviePlaybackLastTime;
            if (result > movieVid->defaultFrameSpeed())
            {
                beginSceneWithDeviceRecovery();
                clearFrameBuffers(0xFF000000u); // packOpaqueRgbClamped(0,0,0)
                const float drawY = static_cast<float>(m_sizeY) * 0.5f + 1.0f;
                const float drawX = static_cast<float>(m_sizeX) * 0.5f;
                drawVidFrame(movieVid, static_cast<int>(g_moviePlaybackFrame), drawX, drawY, 1.0f);
                DrawEffect(1);
                if (!m_loadingPresentationText.isEmpty())
                {
                    (void)drawTextColored(
                        200.0f,
                        m_viewportTop + 5.0f,
                        m_loadingPresentationText.c_str(),
                        0xFF00FF00u);
                }
                endSceneAndPresentRetail(1);
                result = g_moviePlaybackFrame + 1u;
                g_moviePlaybackLastTime = sampledTime;
                g_moviePlaybackFrame = result;
                const int frameCount = static_cast<int>(static_cast<std::int16_t>(movieVid->totalFrames()));
                if (static_cast<int>(result) >= frameCount)
                    g_moviePlaybackFrame = 0u;
            }
        }
        return result;
    }

    void GRAPH::drawVidFrame(VID* vid, int cadr, float x, float y, float z)
    {

        if (!vid || vid == MAP::NullVid())
            return;

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(vid->totalFrames()));
        if (cadr < 0 || cadr >= frameCount)
        {
            logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "ncadr in DrawVid", cadr);
            return;
        }

        const bool wasLocked = m_lockedBackBufferPixels != nullptr;
        const int vidLayer = vid->renderLayer();
        if (vidLayer == 2 || vidLayer == 3 || vidLayer == 4)
        {
            if (!m_lockedBackBufferPixels)
            {
#ifdef _WIN32

                D3DLOCKED_RECT locked;
                IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
                const HRESULT lockResult = surface->LockRect(&locked, nullptr, 0);
                if (FAILED(lockResult))
                    logGraphResourceError(0, "backBuffer", 0);
                m_lockedBackBufferPixels = locked.pBits;
                const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
                m_backBufferPitchPixels = locked.Pitch / divisor;
#else
                lockBackBuffer();
#endif
            }
        }
        else if (m_lockedBackBufferPixels)
        {
#ifdef _WIN32
            graphSurface(m_backBuffer)->UnlockRect();
#endif
            m_lockedBackBufferPixels = nullptr;
        }

        if (vidLayer > 5)
        {
            setRenderStateCached(14u, 0u);
            setRenderStateCached(27u, 1u);
            setRenderStateCached(23u, 7u);
        }
        else
        {
            setRenderStateCached(27u, 0u);
            setRenderStateCached(14u, 1u);
        }

        MAP* const map = MAP::Current();
        const core::ApplicationDrawDispatcherState& appDraw =
            core::GlobalApplicationDrawDispatcherState();
        SPRITE sprite(
            map,
            vid,
            VECTOR{x + appDraw.cameraShiftX(), y + appDraw.cameraShiftY(), z},
            ANGLE{});
        sprite.setCurrentFrameDirect(cadr);
        vid->Draw(&sprite);

        if (!wasLocked)
        {
            if (m_lockedBackBufferPixels)
            {
#ifdef _WIN32
                graphSurface(m_backBuffer)->UnlockRect();
#endif
                m_lockedBackBufferPixels = nullptr;
            }
        }
        else if (!m_lockedBackBufferPixels)
        {
#ifdef _WIN32

            D3DLOCKED_RECT locked;
            IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
            const HRESULT lockResult = surface->LockRect(&locked, nullptr, 0);
            if (FAILED(lockResult))
                logFileLoggerResourceError(g_fileLogger, "GRAPH", 0, "backBuffer", 0);
            m_lockedBackBufferPixels = locked.pBits;
            const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
            m_backBufferPitchPixels = locked.Pitch / divisor;
#else
            lockBackBuffer();
#endif
        }
    }

    int GRAPH::logGraphResourceError(int value1, const char* message, int value2)
    {

        return static_cast<int>(logFileLoggerResourceError(g_fileLogger, "GRAPH", value1, message, value2));
    }

    int GRAPH::saveGraphParameters(RESOURCE* stream)
    {

        int result = stream->write(&m_renderFlags, 4);
        result = stream->write(&m_gammaPair, 8);
        result = stream->write(&m_windDirection, 4);
        result = stream->write(&m_windSpeed, 4);
        return result;
    }

    void GRAPH::invalidateDeviceObjectsRetail() noexcept
    {
#ifdef _WIN32
        unlockBackBufferIfLocked();

        if (m_textFont)
            m_textFont->releaseResetSensitiveResources();
        if (MAP* const map = MAP::Current())
            map->invalidateFontVidDeviceObjects();

        if (m_softwareDepthBuffer)
        {
            ::operator delete(m_softwareDepthBuffer);
            m_softwareDepthBuffer = nullptr;
            m_softwareDepthPitch = 0;
        }
        if (m_backBuffer)
        {
            const ULONG releaseResult = graphSurface(m_backBuffer)->Release();
            m_backBuffer = nullptr;
            LOG::Rewrite("backBuffer release %i", static_cast<int>(releaseResult));
        }
        if (m_tempBuffer)
        {
            graphSurface(m_tempBuffer)->Release();
            m_tempBuffer = nullptr;
        }

        if (m_device)
        {
            IDirect3DDevice8* const device = graphDevice(m_device);
            (void)device->SetStreamSource(0u, nullptr, 0u, 12u);
            (void)device->SetIndices(nullptr);
            (void)device->SetTexture(0u, nullptr);
            (void)device->SetTexture(1u, nullptr);
            (void)device->SetTexture(2u, nullptr);
        }
#endif
    }

    int GRAPH::restoreDeviceRenderStatesRetail() noexcept
    {
#ifdef _WIN32
        if (!m_device)
            return 1;
        IDirect3DDevice8* const device = graphDevice(m_device);

        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(2), 2u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(3), 0u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 4u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(5), 2u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(6), 0u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 4u);

        (void)device->SetSamplerState(0u, static_cast<D3DSAMPLERSTATETYPE>(1), 3u);
        (void)device->SetSamplerState(0u, static_cast<D3DSAMPLERSTATETYPE>(2), 3u);
        (void)device->SetSamplerState(0u, static_cast<D3DSAMPLERSTATETYPE>(6), 1u);
        (void)device->SetSamplerState(0u, static_cast<D3DSAMPLERSTATETYPE>(5), 1u);
        (void)device->SetSamplerState(0u, static_cast<D3DSAMPLERSTATETYPE>(7), 0u);

        (void)device->SetTextureStageState(1u, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 1u);
        (void)device->SetTextureStageState(1u, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 1u);
        (void)device->SetSamplerState(1u, static_cast<D3DSAMPLERSTATETYPE>(1), 3u);
        (void)device->SetSamplerState(1u, static_cast<D3DSAMPLERSTATETYPE>(2), 3u);
        (void)device->SetSamplerState(1u, static_cast<D3DSAMPLERSTATETYPE>(6), 1u);
        (void)device->SetSamplerState(1u, static_cast<D3DSAMPLERSTATETYPE>(5), 1u);
        (void)device->SetSamplerState(1u, static_cast<D3DSAMPLERSTATETYPE>(7), 0u);

        const struct { DWORD state; DWORD value; } states[] = {
            {29u,0u}, {27u,0u}, {28u,0u}, {26u,0u}, {142u,0u}, {137u,0u},
            {15u,0u}, {23u,8u}, {14u,0u}, {7u,0u}, {23u,7u}, {174u,0u},
            {52u,0u}, {22u,3u}, {58u,0xFFFFFFFFu}, {59u,0xFFFFFFFFu}, {168u,15u}
        };
        for (const auto& state : states)
            (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state.state), state.value);
        return static_cast<int>(device->SetTexture(0u, nullptr));
#else
        return 0;
#endif
    }

    int GRAPH::restoreDeviceObjectsRetail() noexcept
    {
#ifdef _WIN32
        if (!m_device)
            return 1;
        IDirect3DDevice8* const device = graphDevice(m_device);

        IDirect3DSurface8* backBuffer = nullptr;
        const HRESULT backBufferResult = device->GetBackBuffer(0u, 0u, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (backBufferResult != D3D_OK)
        {
            LOG::ResourceError("%s", 9, "backBuffer", static_cast<int>(backBufferResult), "GRAPH");
            return 2;
        }
        m_backBuffer = backBuffer;

        if (m_textFont)
            (void)m_textFont->createFontDeviceResources();

        const D3DPRESENT_PARAMETERS& pp = m_d3d9PresentParameters;
        IDirect3DSurface8* tempBuffer = nullptr;
        const HRESULT tempResult = device->CreateOffscreenPlainSurface(
            pp.BackBufferWidth, pp.BackBufferWidth, pp.BackBufferFormat,
            D3DPOOL_SYSTEMMEM, &tempBuffer, nullptr);
        if (tempResult != D3D_OK)
        {
            LOG::ResourceError("%s", 3, "tempBuffer", static_cast<int>(tempResult), "GRAPH");
            return 3;
        }
        m_tempBuffer = tempBuffer;

        const std::size_t depthWords =
            static_cast<std::size_t>(pp.BackBufferWidth) * static_cast<std::size_t>(pp.BackBufferHeight);
        if (depthWords != 0u)
        {
            m_softwareDepthBuffer = static_cast<std::uint16_t*>(
                ::operator new(depthWords * sizeof(std::uint16_t), std::nothrow));
            if (!m_softwareDepthBuffer)
                return 4;
            m_softwareDepthPitch = static_cast<int>(pp.BackBufferWidth);
            std::fill_n(m_softwareDepthBuffer, depthWords, static_cast<std::uint16_t>(0x03FFu));
        }

        (void)setViewportRetail(m_viewportLeft, m_viewportTop, m_viewportRight, m_viewportBottom);
        (void)restoreDeviceRenderStatesRetail();

        if (MAP* const map = MAP::Current())
            map->restoreFontVidDeviceObjects();
        return 0;
#else
        return 0;
#endif
    }

    int GRAPH::beginSceneWithDeviceRecovery()
    {

#ifdef _WIN32
        if ((m_graphFlags & 0x00000400u) != 0u)
            return 0;

        IDirect3DDevice8* const device = graphDevice(m_device);
        const HRESULT cooperative = device->TestCooperativeLevel();
        if (cooperative == D3DERR_DEVICELOST)
        {
            reloadPaletteLightBuffer();
            LOG::ResourceError("%s", 10, "device lost", 0, "GRAPH");
            return 1;
        }

        if (cooperative == D3DERR_DEVICENOTRESET)
        {
            LOG::ResourceError("%s", 10, "device notreset", 0, "GRAPH");
            invalidateDeviceObjectsRetail();

            D3DPRESENT_PARAMETERS& pp = m_d3d9PresentParameters;
            const HRESULT resetResult = device->Reset(&pp);
            LOG::ResourceError("%s", 4, "device notreset", static_cast<int>(resetResult), "GRAPH");
            if (FAILED(resetResult))
                return 2;

            if (restoreDeviceObjectsRetail() != 0)
                return 2;
        }
        else if (cooperative != D3D_OK)
        {
            return 3;
        }

        const HRESULT beginResult = device->BeginScene();
        if (beginResult != D3D_OK)
        {
            LOG::ResourceError("%s", 10, "3dBeginScene for PreTact", static_cast<int>(beginResult), "GRAPH");
            return 4;
        }
        m_graphFlags |= 0x00000400u;
        return 0;
#else
        return 3;
#endif
    }

    int GRAPH::endSceneAndPresentRetail(int presentFlag)
    {

#ifdef _WIN32
        if ((m_graphFlags & 0x00000400u) == 0u)
            return 0;

        unlockBackBufferIfLocked();
        IDirect3DDevice8* const device = graphDevice(m_device);
        const HRESULT endResult = device->EndScene();
        if (endResult != D3D_OK)
            LOG::ResourceError("%s", 10, "3dEndScene for PostTact", static_cast<int>(endResult), "GRAPH");

        int result = 0;
        if (presentFlag && m_effectStartTimes[6] == 0u && m_effectStartTimes[7] == 0u)
        {
            RECT sourceRect{};
            sourceRect.left = static_cast<LONG>(m_viewportLeft);
            sourceRect.top = static_cast<LONG>(m_viewportTop);
            sourceRect.right = static_cast<LONG>(m_viewportRight);
            sourceRect.bottom = static_cast<LONG>(m_viewportBottom);
            RECT destinationRect = sourceRect;

            if (!fullscreenRequested())
            {
                RECT clientRect{};
                RECT windowRect{};
                ::GetClientRect(static_cast<HWND>(m_windowHandle), &clientRect);
                ::ClientToScreen(static_cast<HWND>(m_windowHandle), reinterpret_cast<POINT*>(&clientRect));
                ::GetWindowRect(static_cast<HWND>(m_windowHandle), &windowRect);
                const LONG offsetX = windowRect.left - clientRect.left;
                const LONG offsetY = windowRect.top - clientRect.top;
                destinationRect.left += offsetX;
                destinationRect.right += offsetX;
                destinationRect.top += offsetY;
                destinationRect.bottom += offsetY;
            }

            const HRESULT presentResult = device->Present(&sourceRect, &destinationRect, nullptr, nullptr);
            result = static_cast<int>(presentResult);
            if (presentResult != D3D_OK)
            {
                result = static_cast<int>(logFileLoggerResourceError(
                    g_fileLogger, "%s", 4, "Present", static_cast<int>(presentResult), "GRAPH"));
            }
        }
        else if (m_effectStartTimes[6] != 0u)
        {
            result = static_cast<int>(m_effectStartTimes[6]);
        }
        else if (m_effectStartTimes[7] != 0u)
        {
            result = static_cast<int>(m_effectStartTimes[7]);
        }

        m_graphFlags &= ~0x00000400u;
        applyPendingSteamDisplayChange();
        return result;
#else
        return presentFlag;
#endif
    }


    void GRAPH::flipToGdiRetail()
    {
#ifdef _WIN32
        if ((m_graphFlags & 0x00000010u) == 0u)
            return;

        (void)endSceneAndPresentRetail(1);

        HWND hwnd = static_cast<HWND>(m_windowHandle);
        if (win::applicationWinInstance())
            hwnd = win::applicationWinInstance()->nativeWindow();

        HDC const dc = ::GetDC(hwnd);
        if (!dc)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 9, "DC in FlipToGDI", 0);

        if (::SetPixel(dc, 0, 0, 0x00FFFFFFu) == CLR_INVALID)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "pixel in FlipToGDI", 0);

        ::Sleep(150u);
        for (int i = 0; ::GetPixel(dc, 0, 0) == 0x00FFFFFFu; ++i)
        {
            if (i >= 8)
                break;
            (void)beginSceneWithDeviceRecovery();
            (void)endSceneAndPresentRetail(1);
            ::Sleep(150u);
        }
        (void)::ReleaseDC(hwnd, dc);
#else
        return;
#endif
    }

    int GRAPH::clearFrameBuffers(DWORD color)
    {

        unlockBackBufferIfLocked();
#ifdef _WIN32
        const DWORD flags = (m_graphFlags & 0x00000400u) == 0
            ? D3DCLEAR_TARGET
            : (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
        graphDevice(m_device)->Clear(0u, nullptr, flags, color, 0.0f, 0u);
#else
        (void)color;
#endif
        if (m_softwareDepthBuffer)
        {

            const std::uint32_t heightRaw = static_cast<std::uint32_t>(graphRetailFtolLow32(m_sizeY));
            std::uint32_t wordCount = heightRaw * static_cast<std::uint32_t>(m_softwareDepthPitch);
            std::uint16_t* out = m_softwareDepthBuffer;
            if ((wordCount & 1u) != 0u)
            {
                *out++ = static_cast<std::uint16_t>(0x03FFu);
                --wordCount;
            }
            const std::uint32_t dwordCount = wordCount >> 1u;
            const std::uint32_t fill = 0x03FF03FFu;
            for (std::uint32_t i = 0; i < dwordCount; ++i)
            {
                std::memcpy(out, &fill, sizeof(fill));
                out += 2;
            }
        }
        return static_cast<int>(0x03FF03FFu);
    }

    void GRAPH::updateRenderPulse()
    {

        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((m_renderFlags & 0x80u) != 0u)
        {
            if (graphRetailFcompC3Equal(g_renderPulseBaseScale, -1.0f))
                g_renderPulseBaseScale = m_windSpeed;

            const std::uint32_t elapsed = now - g_renderPulseStartTime;
            std::uint32_t triangularTime = 0u;
            if (elapsed <= 0x800u)
            {
                triangularTime = elapsed;
            }
            else if (elapsed <= 0x1000u)
            {
                triangularTime = 0x1000u - elapsed;
            }
            else
            {
                m_renderFlags &= 0xFFFFFF7Fu;
                m_windSpeed = g_renderPulseBaseScale;
                g_renderPulseBaseScale = -1.0f;
                return;
            }

            m_windSpeed =
                static_cast<float>(triangularTime) * g_renderPulseBaseScale * 0.001953125f +
                g_renderPulseBaseScale;
            return;
        }

        g_renderPulseStartTime = now;
        if (graphRetailFcompC3Equal(g_renderPulseBaseScale, -1.0f))
        {
            g_renderPulseBaseScale = -1.0f;
            return;
        }

        m_windSpeed = g_renderPulseBaseScale;
        g_renderPulseBaseScale = -1.0f;
    }

    void GRAPH::drawFogBufferOverlay(float left, float top, float right, float bottom,
                              int a6, int a7, DWORD colorMask, const WORD* ramp,
                              int baseDepth, int blendFlag)
    {

        if ((m_graphFlags & 0x04u) != 0u || ramp == nullptr ||
            graphRetailFcompC0(right, m_viewportLeft) ||
            !graphRetailFcompC0(left, m_viewportRight) ||
            graphRetailFcompC0(bottom, m_viewportTop) ||
            !graphRetailFcompC0(top, m_viewportBottom))
            return;

        int clippedLeft = graphRetailFtolLow32(left);
        int clippedTop = graphRetailFtolLow32(top);
        int clippedRight = graphRetailFtolLow32(right);
        int clippedBottom = graphRetailFtolLow32(bottom);
        if (graphRetailFcompC0(left, m_viewportLeft))
            clippedLeft = graphRetailFtolLow32(m_viewportLeft);
        if (graphRetailFcompC0(top, m_viewportTop))
            clippedTop = graphRetailFtolLow32(m_viewportTop);
        if (!graphRetailFcompC0(right, m_viewportRight))
            clippedRight = graphRetailFtolLow32(m_viewportRight);
        if (!graphRetailFcompC0(bottom, m_viewportBottom))
            clippedBottom = graphRetailFtolLow32(m_viewportBottom);

        const int width = clippedRight - clippedLeft;
        const int height = clippedBottom - clippedTop;
        if (width < 4 || height < 4)
            return;

        RECTI sourceRect{0, 0, width / 4, height / 4};
        RECTI destinationRect{clippedLeft, clippedTop, clippedRight, clippedBottom};
        const std::uint32_t lowerDepthRaw =
            static_cast<std::uint32_t>(baseDepth) +
            ((static_cast<std::uint32_t>(a6) - static_cast<std::uint32_t>(a7)) << 3u);
        const int lowerDepth = static_cast<std::int32_t>(lowerDepthRaw);

        int texturePitchBytes = 0;
        std::uint16_t* const locked = m_lightBuffer->lock16(&texturePitchBytes, &sourceRect);
        if (!locked)
        {
            LOG::ResourceError("%s", 0, "fog buffer", 0, "GRAPH");
            return;
        }

        const std::uint16_t* const depth = m_softwareDepthBuffer;
        int depthIndex = clippedLeft + m_softwareDepthPitch * clippedTop;
        if (m_lightBuffer->format() == 41u)
        {
            std::uint8_t* output = reinterpret_cast<std::uint8_t*>(locked);
            std::uint8_t previous = 0u;
            const unsigned rowCount = static_cast<unsigned>(height + 3) >> 2u;
            for (unsigned row = 0; row < rowCount; ++row)
            {
                const unsigned columnCount = static_cast<unsigned>(width + 3) >> 2u;
                for (unsigned column = 0; column < columnCount; ++column)
                {
                    const std::uint16_t first = depth[depthIndex];
                    const std::uint16_t second = depth[depthIndex + 3];
                    const int depthValue = static_cast<int>(first < second ? first : second) - 1024;
                    if (depthValue > baseDepth)
                    {
                        const int upperFadeDepth = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(baseDepth) + 10u);
                        if (depthValue <= upperFadeDepth)
                        {
                            previous = 0u;
                            *output = 0u;
                        }
                        else
                        {
                            *output = previous;
                        }
                    }
                    else if (depthValue > lowerDepth)
                    {
                        previous = reinterpret_cast<const std::uint8_t*>(ramp)[2 * (baseDepth - depthValue)];
                        *output = previous;
                    }
                    else
                    {
                        previous = 0xFFu;
                        *output = 0xFFu;
                    }
                    ++output;
                    depthIndex += 4;
                }
                const int quarterWidth = (width + 3) / 4;
                depthIndex += 4 * (m_softwareDepthPitch - quarterWidth);
                output += texturePitchBytes - quarterWidth;
            }
        }
        else
        {
            std::uint16_t* output = locked;
            std::uint16_t previous = 0u;
            const unsigned rowCount = static_cast<unsigned>(height + 3) >> 2u;
            for (unsigned row = 0; row < rowCount; ++row)
            {
                const unsigned columnCount = static_cast<unsigned>(width + 3) >> 2u;
                for (unsigned column = 0; column < columnCount; ++column)
                {
                    const std::uint16_t first = depth[depthIndex];
                    const std::uint16_t second = depth[depthIndex + 3];
                    const int depthValue = static_cast<int>(first < second ? first : second) - 1024;
                    if (depthValue > baseDepth)
                    {
                        const int upperFadeDepth = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(baseDepth) + 10u);
                        if (depthValue <= upperFadeDepth)
                        {
                            previous = 0u;
                            *output = 0u;
                        }
                        else
                        {
                            *output = previous;
                        }
                    }
                    else if (depthValue > lowerDepth)
                    {
                        previous = ramp[baseDepth - depthValue];
                        *output = previous;
                    }
                    else
                    {
                        previous = ramp[baseDepth - lowerDepth];
                        *output = previous;
                    }
                    ++output;
                    depthIndex += 4;
                }
                const int quarterWidth = (width + 3) / 4;
                depthIndex += 4 * (m_softwareDepthPitch - quarterWidth);
                output += texturePitchBytes / 2 - quarterWidth;
            }
        }

        m_lightBuffer->unlock();
        setAlphaBlendFactors(2u - (blendFlag != 0 ? 1u : 0u), 4u);
        const int zDepth = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(baseDepth) + 0x3FEu);
        const float z = static_cast<float>(zDepth) * 0.000015258789f;
        GraphEffectGammaRawPair colors{};
        buildEffectGammaPair(&colors, colorMask, GammaRawCreateOpaque(0, 0, 0));
#ifdef _WIN32
        const DWORD zRaw = floatRaw(z);
        m_lightBuffer->DrawDepthRectangle(
            zRaw, zRaw, destinationRect, sourceRect, reinterpret_cast<const DWORD*>(&colors));
#else
        const DWORD zRaw = floatRaw(z);
        m_lightBuffer->DrawDepthRectangle(
            zRaw, zRaw, destinationRect, sourceRect, colors.inverseMask, colors.color);
#endif
    }

    void GRAPH::drawSnowLightBuffer()
    {

        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const int shiftX = graphRetailFtolLow32(appDraw.cameraShiftX());
        const int shiftY = graphRetailFtolLow32(appDraw.cameraShiftY());
        const int startX = (-(shiftX & 3)) & 3;
        const int startY = (-(shiftY & 3)) & 3;

        if ((m_graphFlags & 0x04u) != 0u ||
            (core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((m_renderFlags & 0x40u) == 0u)
        {
            g_snowLightRampStartTime = now;
            g_snowLightIntensity = 0u;
            return;
        }

        if (g_snowLightIntensity < 0x100u)
            g_snowLightIntensity = (now - g_snowLightRampStartTime) >> 7u;

        const int screenWidth = graphRetailFtolLow32(m_sizeX);
        const int screenHeight = graphRetailFtolLow32(m_sizeY);
        RECTI textureRect{0, 0, screenWidth / 4, screenHeight / 4};
        RECTI screenRect{0, 0, screenWidth, screenHeight};
        int texturePitchBytes = 0;
        std::uint16_t* const locked = m_lightBuffer->lock16(&texturePitchBytes, &textureRect);
        if (!locked)
        {
            LOG::ResourceError("%s", 0, "snow buffer", 0, "GRAPH");
            return;
        }

        const std::uint16_t* const depth = m_softwareDepthBuffer;
        const int depthPitch = m_softwareDepthPitch;
        const bool paletteTexture = m_lightBuffer->format() == 0x29u;
        if (paletteTexture)
        {
            std::uint8_t* const output = reinterpret_cast<std::uint8_t*>(locked);
            for (int y = startY; graphRetailFcompC0(static_cast<float>(y), m_sizeY); y += 4)
            {
                int depthIndex = startX + y * depthPitch;
                for (int x = startX; graphRetailFcompC0(static_cast<float>(x), m_sizeX); x += 4, depthIndex += 4)
                {
                    std::uint32_t intensity = 0u;
                    bool accepted = false;
                    if (y > 3)
                        accepted = graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex - 4 * depthPitch], intensity);
                    if (!accepted)
                    {
                        intensity = 0u;
                        graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex + 4 * depthPitch], intensity);
                    }
                    output[(y / 4) * texturePitchBytes + (x / 4)] = static_cast<std::uint8_t>(intensity);
                }
            }
        }
        else
        {
            std::uint16_t* const output = locked;
            const int texturePitchWords = texturePitchBytes / 2;
            for (int y = startY; graphRetailFcompC0(static_cast<float>(y), m_sizeY); y += 4)
            {
                int depthIndex = startX + y * depthPitch;
                for (int x = startX; graphRetailFcompC0(static_cast<float>(x), m_sizeX); x += 4, depthIndex += 4)
                {
                    std::uint32_t intensity = 0u;
                    bool accepted = false;
                    if (y > 3)
                        accepted = graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex - 4 * depthPitch], intensity);
                    if (!accepted)
                    {
                        intensity = 0u;
                        graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex + 4 * depthPitch], intensity);
                    }
                    output[(y / 4) * texturePitchWords + (x / 4)] =
                        m_intensityPalette16[static_cast<std::size_t>(intensity)];
                }
            }
        }

        m_lightBuffer->unlock();
        setRenderStateCached(0x1Du, 0u);
        setAlphaBlendFactors(2u, 4u);
        const DWORD colors[2] = {0u, 0u};
#ifdef _WIN32
        m_lightBuffer->DrawFixedDepthRectangle(screenRect, textureRect, colors);
#else
        m_lightBuffer->DrawFixedDepthRectangle(screenRect, textureRect, colors[0], colors[1]);
#endif
    }


    int GRAPH::drawLineParticles()
    {

        int result = static_cast<int>(m_renderFlags);
        if ((m_renderFlags & 0x00000C00u) == 0u)
        {
            g_lineParticleCount = 0;
            return result;
        }
        if ((m_graphFlags & 0x04u) != 0u)
            return result;
        void* const applicationOwner = core::ApplicationPhysicalOwner();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(applicationOwner)));
        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return result;

        const DWORD direction = m_windDirection;
        if (g_lineParticleWindDirection != direction ||
            !graphRetailFcompC3Equal(g_lineParticleWindSpeed, m_windSpeed))
        {
            const float oldWindX = g_lineParticleWindX;
            g_lineParticleWindX = graphWeatherWindX(m_windDirection, m_windSpeed);
            if (g_lineParticleCount > 0)
            {
                const float windDifference = g_lineParticleWindX - oldWindX;
                for (int index = 0; index < g_lineParticleCount; ++index)
                {
                    GraphWeatherLineParticle& particle = g_lineParticles[static_cast<std::size_t>(index)];
                    particle.vertex[1].x +=
                        (particle.vertex[1].y - particle.vertex[0].y) * windDifference * kGraphOneOver200;
                }
            }
            g_lineParticleWindDirection = direction;
            g_lineParticleWindSpeed = m_windSpeed;
        }

        const DWORD mode = m_renderFlags & 0x00000C00u;
        switch (mode)
        {
        case 0x00000C00u:
            g_lineParticleCount = 250;
            break;
        case 0x00000800u:
            if (g_lineParticleCount >= 250)
                updateRenderFlags(0x00000C00u);
            else
                ++g_lineParticleCount;
            break;
        case 0x00000400u:
            if (g_lineParticleCount <= 0)
                m_renderFlags &= 0xFFFFF3FFu;
            else
                --g_lineParticleCount;
            break;
        default:
            break;
        }

        if (g_lineParticleCount > 0)
        {
            const std::uint32_t deltaMilliseconds =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            int respawned = 0;
            for (int index = 0; index < g_lineParticleCount; ++index)
            {
                GraphWeatherLineParticle& particle = g_lineParticles[static_cast<std::size_t>(index)];
                GraphWeatherVertex44& first = particle.vertex[0];
                GraphWeatherVertex44& second = particle.vertex[1];
                const bool active =
                    first.color != 0u &&
                    first.x >= m_viewportLeft && first.x < m_viewportRight &&
                    first.y >= m_viewportTop && first.y < m_viewportBottom &&
                    first.z >= 0.015625f;

                if (active)
                {
                    const float travel =
                        (second.z - first.z) * static_cast<float>(deltaMilliseconds) * 200.0f;
                    float windTravel = g_lineParticleWindX * travel * kGraphOneOver200;
                    if (graphRetailFcompC0(windTravel + first.x, 0.0f))
                        windTravel += static_cast<float>(m_sizeX);
                    if (windTravel + first.x > static_cast<float>(m_sizeX))
                        windTravel -= static_cast<float>(m_sizeX);
                    const float depthTravel = travel * kGraphOneOver8192;
                    first.x += windTravel;
                    first.y += travel;
                    first.z -= depthTravel;
                    second.x += windTravel;
                    second.y += travel;
                    second.z -= depthTravel;
                }
                else
                {
                    ++respawned;
                    const float length = static_cast<float>((std::rand() % 51) + 15);
                    const float windTail = g_lineParticleWindX * length * -kGraphOneOver200;
                    const float x = static_cast<float>(std::rand()) *
                        (m_viewportRight - 1.0f) * kGraphRand32767;
                    const float yLimit = respawned >= 50 ? m_viewportBottom : 40.0f;
                    const float y = static_cast<float>(std::rand()) * yLimit * kGraphRand32767;
                    const float z = (length + 10.0f) * kGraphOneOver819_2 + 0.015625f;
                    first = GraphWeatherVertex44{x, y, z, 1.0f, 0x70E0E0FFu};
                    second = GraphWeatherVertex44{
                        x + windTail,
                        y - length,
                        z + length * kGraphOneOver8192,
                        1.0f,
                        0x308080FFu};
                }
            }
        }

#ifdef _WIN32
        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetTexture(0u, nullptr);
#endif
        setAlphaBlendFactors(5u, 6u);
        result = DrawPrimitiveList(
            2u,
            0x44u,
            g_lineParticles.data(),
            static_cast<DWORD>(sizeof(GraphWeatherVertex44)),
            2 * g_lineParticleCount);
        return result;
    }

    int GRAPH::drawCrossParticles()
    {

        int result = static_cast<int>(m_renderFlags);
        if ((m_renderFlags & 0x0000C000u) == 0u)
        {
            g_crossParticleCount = 0;
            return result;
        }
        if ((m_graphFlags & 0x04u) != 0u)
            return result;
        void* const applicationOwner = core::ApplicationPhysicalOwner();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(applicationOwner)));
        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return result;

        const DWORD direction = m_windDirection;
        if (g_crossParticleWindDirection != direction ||
            !graphRetailFcompC3Equal(g_crossParticleWindSpeed, m_windSpeed))
        {
            g_crossParticleWindX = graphWeatherWindX(m_windDirection, m_windSpeed);
            g_crossParticleWindDirection = direction;
            g_crossParticleWindSpeed = m_windSpeed;
        }

        const DWORD mode = m_renderFlags & 0x0000C000u;
        switch (mode)
        {
        case 0x0000C000u:
            g_crossParticleCount = 1000;
            break;
        case 0x00008000u:
            if (g_crossParticleCount >= 1000)
            {
                g_crossParticleCount = 1000;
                updateRenderFlags(0x0000C000u);
            }
            else
            {
                ++g_crossParticleCount;
            }
            break;
        case 0x00004000u:
            if (g_crossParticleCount <= 0)
                m_renderFlags &= 0xFFFF3FFFu;
            else
                --g_crossParticleCount;
            break;
        default:
            break;
        }

        int count = g_crossParticleCount;
        if (count > 0)
        {
            const std::uint32_t deltaMilliseconds =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
            const float cameraX = appDraw.cameraShiftX();
            const float cameraY = appDraw.cameraShiftY();
            int respawned = 0;
            for (int index = 0; index < count; ++index)
            {
                GraphWeatherCrossParticle& particle = g_crossParticles[static_cast<std::size_t>(index)];
                GraphWeatherVertex44& first = particle.vertex[0];
                const bool active = first.color != 0u && first.z >= 0.015625f;
                if (active)
                {
                    const float travel =
                        (particle.vertex[1].z - first.z - kGraphOneOver8192) *
                        static_cast<float>(deltaMilliseconds) * 150.0f;
                    const float xTravel =
                        (g_crossParticleWindX * travel + 50.0f - static_cast<float>(std::rand() % 101)) * 0.02f -
                        (g_crossParticlePreviousNegatedCameraX - -cameraX);
                    const float yTravel =
                        travel - (g_crossParticlePreviousNegatedCameraY - -cameraY);

                    float adjustedXTravel = xTravel;
                    float adjustedYTravel = yTravel;
                    const float movedX = adjustedXTravel + first.x;
                    if (!graphRetailFcompC0(m_viewportLeft - 30.0f, movedX) &&
                        !graphRetailFcompC3Equal(m_viewportLeft - 30.0f, movedX))
                        adjustedXTravel += m_viewportRight - m_viewportLeft;
                    const float movedXAfterLeft = adjustedXTravel + first.x;
                    if (graphRetailFcompC0(m_viewportRight + 30.0f, movedXAfterLeft))
                        adjustedXTravel -= m_viewportRight - m_viewportLeft;

                    const float movedY = adjustedYTravel + first.y;
                    if (!graphRetailFcompC0(m_viewportTop - 30.0f, movedY) &&
                        !graphRetailFcompC3Equal(m_viewportTop - 30.0f, movedY))
                        adjustedYTravel += m_viewportBottom - m_viewportTop;
                    const float movedYAfterTop = adjustedYTravel + first.y;
                    if (graphRetailFcompC0(m_viewportBottom + 30.0f, movedYAfterTop))
                        adjustedYTravel -= m_viewportBottom - m_viewportTop;

                    const float depthTravel = travel * kGraphOneOver8192;
                    for (GraphWeatherVertex44& vertex : particle.vertex)
                    {
                        vertex.x += adjustedXTravel;
                        vertex.y += adjustedYTravel;
                        vertex.z -= depthTravel;
                    }
                }
                else
                {
                    ++respawned;
                    const float radius = static_cast<float>((std::rand() % 3) + 2);
                    const float centerX = static_cast<float>(std::rand()) *
                        (m_viewportRight - 1.0f) * kGraphRand32767;
                    const float yLimit = respawned >= 50 ? m_viewportBottom : 40.0f;
                    const float centerY = static_cast<float>(std::rand()) * yLimit * kGraphRand32767;
                    const float z = static_cast<float>(std::rand()) *
                        (m_viewportBottom + 50.0f) * kGraphRand268435456 + 0.015625f;
                    const float halfRadius = radius * 0.5f;
                    const float zTail = z + radius * kGraphOneOver8192;

                    particle.vertex[0] = GraphWeatherVertex44{centerX - radius, centerY, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[1] = GraphWeatherVertex44{centerX + radius, centerY, zTail, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[2] = GraphWeatherVertex44{centerX - halfRadius, centerY - radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[3] = GraphWeatherVertex44{centerX + halfRadius, centerY + radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[4] = GraphWeatherVertex44{centerX - halfRadius, centerY + radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[5] = GraphWeatherVertex44{centerX + halfRadius, centerY - radius, z, 1.0f, 0xFFFFFFFFu};
                }
            }

            for (int index = 0; index < count; ++index)
            {
                if ((std::rand() % 5) == 0)
                    graphTransformCrossParticle(g_crossParticles[static_cast<std::size_t>(index)]);
            }
        }

        const core::ApplicationDrawDispatcherState& appDrawForShiftCache =
            core::GlobalApplicationDrawDispatcherState();
        g_crossParticlePreviousNegatedCameraX = -appDrawForShiftCache.cameraShiftX();
        g_crossParticlePreviousNegatedCameraY = -appDrawForShiftCache.cameraShiftY();

#ifdef _WIN32
        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetTexture(0u, nullptr);
#endif
        setAlphaBlendFactors(5u, 6u);
        result = DrawPrimitiveList(
            2u,
            0x44u,
            g_crossParticles.data(),
            static_cast<DWORD>(sizeof(GraphWeatherVertex44)),
            2 * g_crossParticleCount);
        return result;
    }

    void GRAPH::runFrameService(int worldTickFlag)
    {

        core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        MAP& map = *MAP::Current();

        if ((m_renderFlags & 4u) != 0u &&
            (core::ApplicationFlags() & application_flags::BucketTimingActive) == 0u)
        {
            g_frameCameraShiftX = appDraw.cameraShiftX();
            g_frameCameraShiftY = appDraw.cameraShiftY();
            const int jitterY = std::rand() % 9;
            const int jitterX = std::rand() % 9;
            map.SetShiftCoor(
                g_frameCameraShiftX + static_cast<float>(m_sizeX) * 0.5f + 4.0f - static_cast<float>(jitterX),
                g_frameCameraShiftY + static_cast<float>(m_sizeY) * 0.5f + 4.0f - static_cast<float>(jitterY),
                0);
        }

        auto setStage0MagFilter = [this](DWORD value) -> DWORD
        {
#ifdef _WIN32
            return static_cast<DWORD>(graphDevice(m_device)->SetSamplerState(
                0u, D3DSAMP_MAGFILTER, value));
#else
            (void)this;
            (void)value;
            return 0u;
#endif
        };

        if (worldTickFlag)
        {

            const float cameraX = appDraw.cameraShiftX();
            const float cameraY = appDraw.cameraShiftY();
            const float applicationMapSizeX = core::ApplicationMapWidth();
            const float applicationMapSizeY = core::ApplicationMapHeight();
            const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            const bool invalidCoverage =
                vidTable.count() <= 1024 ||
                vidTable.slot(1024) == nullptr ||
                cameraX < 0.0f ||
                applicationMapSizeX < m_viewportRight + cameraX - m_viewportLeft ||
                cameraY < 0.0f ||
                applicationMapSizeY < m_viewportBottom + cameraY - m_viewportTop;

            if (invalidCoverage)
                clearFrameBuffers(GammaRawCreateOpaque(0, 0, 0));

            setRenderStateCached(0x1Bu, 0u);
            setStage0MagFilter(1u);

            unlockBackBufferIfLocked();
            core::Application::drawSpritePass(appDraw, 0);
#ifdef _WIN32
            graphDevice(m_device)->SetTexture(0u, nullptr);
#endif
            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 1);
            core::Application::drawSpritePass(appDraw, 2);
            core::Application::drawSpritePass(appDraw, 3);

            unlockBackBufferIfLocked();
            core::Application::drawSpritePass(appDraw, 4);

            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 5);
            core::Application::drawSpritePass(appDraw, 6);
            core::Application::drawSpritePass(appDraw, 7);

            unlockBackBufferIfLocked();
            core::Application::drawSpritePass(appDraw, 8);

            setRenderStateCached(0x1Bu, 1u);
            setStage0MagFilter(1u);
            core::Application::drawSpritePass(appDraw, 9);
            core::Application::drawSpritePass(appDraw, 10);

            setStage0MagFilter(2u);
            core::Application::drawSpritePass(appDraw, 11);
            drawSnowLightBuffer();
            drawCrossParticles();

            setStage0MagFilter(1u);
            core::Application::drawSpritePass(appDraw, 12);
            drawLineParticles();

            setStage0MagFilter(1u);
            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 13);
            unlockBackBufferIfLocked();
            core::Application::drawSpritePass(appDraw, 14);

            if (Mouse && Mouse->hardwareCursorEnabled() == 0)
            {
                SPRITE* node = mouseSprite();
                VID* const mouseVid = node ? node->Vid() : nullptr;
                if (mouseVid && (mouseVid->properties() & 0x00008000u) != 0u)
                {
                    while (node)
                    {
                        if (!node->isDrawSuppressed())
                            node->Draw();
                        node = node->childChain();
                    }
                }
            }
        }

        updateRenderPulse();
        if ((m_renderFlags & 4u) != 0u &&
            (core::ApplicationFlags() & application_flags::BucketTimingActive) == 0u)
        {
            map.SetShiftCoor(
                g_frameCameraShiftX + static_cast<float>(m_sizeX) * 0.5f,
                g_frameCameraShiftY + static_cast<float>(m_sizeY) * 0.5f,
                0);
        }
        unlockBackBufferIfLocked();

        releaseMoviePlaybackIfComplete();

        setRenderStateCached(0x1Du, 0u);
        DrawEffect(worldTickFlag);
        unlockBackBufferIfLocked();
    }

    int GRAPH::setRenderStateCached(DWORD renderState, DWORD value)
    {

        if (renderState == 0x17u || renderState == 0x0Eu || renderState == 7u)
            return 0;

#ifdef _WIN32
        return static_cast<int>(graphDevice(m_device)->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(renderState),
            value));
#else
        (void)value;
        return 0;
#endif
    }

    int GRAPH::playMovieCentered(const STRING& moviePath)
    {

        const int centerY = signedHalfFromFloatTowardZero(m_sizeY);
        const int centerX = signedHalfFromFloatTowardZero(m_sizeX);
        return openMoviePlayback(moviePath, centerX, centerY);
    }

    int GRAPH::openMoviePlayback(const STRING& moviePath, int centerX, int centerY)
    {

        (void)centerX;
        (void)centerY;
        releaseMoviePlayback();
#ifdef _WIN32
        IGraphBuilder* graphBuilder = nullptr;
        HRESULT hr = ::CoCreateInstance(
            CLSID_FilterGraph,
            nullptr,
            1u,
            IID_IGraphBuilder,
            reinterpret_cast<void**>(&graphBuilder));
        m_movieComObjects[0] = graphBuilder;
        if (hr < 0)
        {
            return static_cast<int>(logFileLoggerResourceError(
                g_fileLogger, "MOVIE", 3, "GraphBuilder", static_cast<int>(hr)));
        }

        hr = graphBuilder->QueryInterface(
            IID_IMediaControl,
            reinterpret_cast<void**>(&m_movieComObjects[1]));
        if (hr < 0)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 3, "MediaControl", static_cast<int>(hr));
            return releaseMoviePlayback();
        }

        // IMediaEvent QI result/HRESULT are intentionally ignored.
        (void)graphBuilder->QueryInterface(
            IID_IMediaEvent,
            reinterpret_cast<void**>(&m_movieComObjects[2]));

        wchar_t widePath[1024];
        (void)convertStringToWideChars(moviePath, widePath, 1024);
        hr = graphBuilder->RenderFile(widePath, nullptr);
        if (hr < 0)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 4, "RenderFile", static_cast<int>(hr));
            return releaseMoviePlayback();
        }

        (void)graphBuilder->QueryInterface(
            IID_IVideoWindow,
            reinterpret_cast<void**>(&m_movieComObjects[3]));
        IVideoWindow* const videoWindow = static_cast<IVideoWindow*>(m_movieComObjects[3]);

        const HWND hwnd = win::applicationWinInstance()->nativeWindow();
        videoWindow->put_Owner(reinterpret_cast<OAHWND>(hwnd));
        videoWindow->put_WindowStyle(0x44000000L);

        const int top = graphRetailFtolLow32(m_viewportTop);
        const int left = graphRetailFtolLow32(m_viewportLeft);
        const int bottom = graphRetailFtolLow32(m_viewportBottom);
        const int right = graphRetailFtolLow32(m_viewportRight);
        videoWindow->SetWindowPosition(left, top, right - left + 1, bottom - top + 1);

        IMediaControl* const mediaControl = static_cast<IMediaControl*>(m_movieComObjects[1]);
        (void)mediaControl->Run();
        ::SetCapture(hwnd);
        const int cursorY = graphRetailFtolLow32(m_sizeY);
        const int cursorX = graphRetailFtolLow32(m_sizeX);
        return ::SetCursorPos(cursorX, cursorY);
#else
        (void)moviePath;
        return 0;
#endif
    }


    int GRAPH::enterModalRenderState()
    {

        m_graphFlags |= 0x1u;
#ifdef _WIN32
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }
        flipToGdiRetail();
        ::DrawMenuBar(static_cast<HWND>(m_windowHandle));
        ::RedrawWindow(static_cast<HWND>(m_windowHandle), nullptr, nullptr, 0x400u);
        return pauseMovieMediaControl(static_cast<void*>(m_movieComObjects));
#else
        m_lockedBackBufferPixels = nullptr;
        return 0;
#endif
    }

    int GRAPH::leaveModalRenderState()
    {

        m_graphFlags &= ~0x1u;
#ifdef _WIN32
        IMediaControl* const mediaControl = static_cast<IMediaControl*>(m_movieComObjects[1]);
        return mediaControl ? static_cast<int>(mediaControl->Run()) : 0;
#else
        return 0;
#endif
    }

    int GRAPH::lockBackBuffer()
    {

        if (m_lockedBackBufferPixels)
        {
            return static_cast<int>(
                static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(m_lockedBackBufferPixels)));
        }
#ifdef _WIN32
        IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
        D3DLOCKED_RECT locked;
        const HRESULT hr = surface->LockRect(&locked, nullptr, 0);
        if (FAILED(hr))
            LOG::ResourceError("%s", 0, "backBuffer", 0, "GRAPH");
        m_lockedBackBufferPixels = locked.pBits;
        const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
        m_backBufferPitchPixels = locked.Pitch / divisor;
#endif
        return m_backBufferPitchPixels;
    }

    namespace
    {
        struct GraphOverlayVertexRaw
        {
            DWORD x;
            DWORD y;
            DWORD z;
            DWORD rhw;
            DWORD diffuse;
            DWORD specular;
        };

                                                                                                          

        GraphOverlayVertexRaw makeGraphOverlayVertex(int xRaw, int yRaw, int colorRaw)
        {
            GraphOverlayVertexRaw vertex{};
            vertex.x = static_cast<DWORD>(xRaw);
            vertex.y = static_cast<DWORD>(yRaw);
            vertex.z = 0x3F7FFFFEu;
            vertex.rhw = 0x3F800000u;
            vertex.diffuse = static_cast<DWORD>(colorRaw);
            vertex.specular = 0xFFFFFFFFu;
            return vertex;
        }
    }

    int GRAPH::drawAlphaOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw)
    {

        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(leftRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, bottomRaw, colorRaw),
            makeGraphOverlayVertex(leftRaw, bottomRaw, colorRaw),
        };

        unlockBackBufferIfLocked();
#ifdef _WIN32
        graphDevice(m_device)->SetTexture(0, nullptr);
#endif
        setAlphaBlendFactors(1u, 4u);
        setRenderStateCached(0x0Eu, 0u);
        DrawPrimitiveList(6u, 0xC4u, vertices, 24u, 4);
        return setRenderStateCached(0x0Eu, 1u);
    }

    void GRAPH::drawMapLoadingPresentation(const char* text)
    {
        m_loadingPresentationText.Assign(text ? text : "");

        (void)beginSceneWithDeviceRecovery();

        const float top = m_viewportTop;
        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(200.0f)),
                static_cast<int>(floatRaw(top + 5.0f)),
                static_cast<int>(0xFF000000u)),
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(800.0f)),
                static_cast<int>(floatRaw(top + 5.0f)),
                static_cast<int>(0xFF000000u)),
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(800.0f)),
                static_cast<int>(floatRaw(top + 30.0f)),
                static_cast<int>(0xFF000000u)),
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(200.0f)),
                static_cast<int>(floatRaw(top + 30.0f)),
                static_cast<int>(0xFF000000u)),
        };

        unlockBackBufferIfLocked();
#ifdef _WIN32
        graphDevice(m_device)->SetTexture(0, nullptr);
#endif
        (void)setRenderStateCached(0x1Bu, 0u);
        (void)DrawPrimitiveList(6u, 0xC4u, vertices, 24u, 4);

        (void)drawTextColored(
            200.0f,
            top + 5.0f,
            m_loadingPresentationText.c_str(),
            0xFF00FF00u);

        (void)endSceneAndPresentRetail(1);
    }

    int GRAPH::drawAdditiveOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw)
    {

        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(leftRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, bottomRaw, colorRaw),
            makeGraphOverlayVertex(leftRaw, bottomRaw, colorRaw),
        };

        unlockBackBufferIfLocked();
#ifdef _WIN32
        graphDevice(m_device)->SetTexture(0, nullptr);
#endif
        setRenderStateCached(0x1Du, 0u);
        setAlphaBlendFactors(9u, 2u);
        setRenderStateCached(0x0Eu, 0u);
        DrawPrimitiveList(6u, 0xC4u, vertices, 24u, 4);
        return setRenderStateCached(0x0Eu, 1u);
    }

    int GRAPH::unlockBackBufferIfLocked()
    {

        if (!m_lockedBackBufferPixels)
            return 0;
#ifdef _WIN32
        const HRESULT result = graphSurface(m_backBuffer)->UnlockRect();
#else
        const int result = 0;
#endif
        m_lockedBackBufferPixels = nullptr;
        return static_cast<int>(result);
    }

    int GRAPH::isMoviePlaybackComplete()
    {

        if (!m_movieComObjects[1])
            return 1;
#ifdef _WIN32
        IMediaEvent* const mediaEvent = static_cast<IMediaEvent*>(m_movieComObjects[2]);

        long eventCode = static_cast<long>(
            reinterpret_cast<std::uintptr_t>(&m_movieComObjects[0]) & 0xFFFFFFFFu);
        mediaEvent->WaitForCompletion(0, &eventCode);
        return eventCode == 1 ? 1 : 0;
#else
        return 0;
#endif
    }

    void GRAPH::releaseMoviePlaybackIfComplete()
    {

        if (!m_movieComObjects[0])
            return;
        if (isMoviePlaybackComplete())
            releaseMoviePlayback();
    }

    int GRAPH::releaseMoviePlayback()
    {

        int result = 0;
#ifdef _WIN32
        ::ReleaseCapture();

        void* const slot0C = m_movieComObjects[3];
        if (slot0C)
            static_cast<IUnknown*>(slot0C)->Release();

        void* const slot04 = m_movieComObjects[1];
        m_movieComObjects[3] = nullptr;
        if (slot04)
            static_cast<IUnknown*>(slot04)->Release();

        void* const slot08 = m_movieComObjects[2];
        m_movieComObjects[1] = nullptr;
        if (slot08)
            static_cast<IUnknown*>(slot08)->Release();

        void* const slot00 = m_movieComObjects[0];
        m_movieComObjects[2] = nullptr;
        if (slot00)
            result = static_cast<int>(static_cast<IUnknown*>(slot00)->Release());
        m_movieComObjects[0] = nullptr;
#else
        m_movieComObjects[3] = nullptr;
        m_movieComObjects[1] = nullptr;
        m_movieComObjects[2] = nullptr;
        m_movieComObjects[0] = nullptr;
#endif
        return result;
    }

    int GRAPH::updateRenderFlags(DWORD mask)
    {

        DWORD result = mask;
        if ((mask & 0x80000000u) != 0)
        {
            result = ~mask;
            m_renderFlags &= ~mask;
        }
        else
        {
            if (mask == 1u || mask == 2u)
                m_renderFlags &= 0xFFFFFFFCu;
            if ((result & 0x00000C00u) != 0)
                m_renderFlags &= 0xFFFFF3FFu;
            if ((result & 0x0000C000u) != 0)
                m_renderFlags &= 0xFFFF3FFFu;
            m_renderFlags |= mask;
        }
        return static_cast<int>(result);
    }

    void GRAPH::resetMapRenderRuntimeState()
    {

        releaseMoviePlayback();
        m_windSpeed = 0.025f;
        m_windDirection = 200u;
        updateRenderFlags(0xFFFFFFFFu);
    }

    void GRAPH::deinit()
    {

        releaseMoviePlayback();

#ifdef _WIN32
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        if (m_device)
            graphDevice(m_device)->SetStreamSource(0, nullptr, 0u, 0u);

        destroyGraphTextFontRetail(m_textFont);

        if (m_softwareDepthBuffer)
        {
            ::operator delete(m_softwareDepthBuffer);
            m_softwareDepthBuffer = nullptr;
        }

        if (m_tempBuffer)
        {
            graphSurface(m_tempBuffer)->Release();
            m_tempBuffer = nullptr;
        }

        deleteBaseTextureThroughRetailSlot00(m_alphaBuffer);
        deleteBaseTextureThroughRetailSlot00(m_lightBuffer);
        deleteBaseTextureThroughRetailSlot00(m_hiBuffer);

        if (m_device)
        {
            const ULONG result = graphDevice(m_device)->Release();
            m_device = nullptr;
            LOG::Write("d3dDevice release %i", static_cast<int>(result));
        }
        if (m_backBuffer)
        {
            const ULONG result = graphSurface(m_backBuffer)->Release();
            m_backBuffer = nullptr;
            LOG::Rewrite("backBuffer release %i", static_cast<int>(result));
        }
        if (m_direct3D)
        {
            const ULONG result = graphD3D(m_direct3D)->Release();
            m_direct3D = nullptr;
            LOG::Write("d3d release %i", static_cast<int>(result));
        }
#else
        destroyGraphTextFontRetail(m_textFont);
        if (m_softwareDepthBuffer)
        {
            ::operator delete(m_softwareDepthBuffer);
            m_softwareDepthBuffer = nullptr;
        }
        m_softwareDepthPitch = 0;
        m_tempBuffer = nullptr;
        m_alphaBuffer = nullptr;
        m_lightBuffer = nullptr;
        m_hiBuffer = nullptr;
        m_device = nullptr;
        m_backBuffer = nullptr;
        m_direct3D = nullptr;
#endif

        releaseMoviePlayback();
    }


    int GRAPH::drawFormattedText(float x, float y, const char* format, ...)
    {

        char text[1024];
        va_list args;
        va_start(args, format);
        std::vsprintf(text, format, args);
        va_end(args);

        DWORD color = 0u;
        packOpaqueRgbClamped(&color, 0xFF, 0xFF, 0xFF);
        return drawTextColored(x, y, text, color);
    }

    void GRAPH::DrawText(float x, float y, const char* format, ...)
    {

        char text[1024];
        va_list args;
        va_start(args, format);
        std::vsprintf(text, format, args);
        va_end(args);

        DWORD color = 0u;
        packOpaqueRgbClamped(&color, 0xFF, 0xFF, 0xFF);
        (void)drawTextColored(x, y, text, color);
    }


    int GRAPH::drawTextColored(float x, float y, const char* text, DWORD color)
    {

        if (!m_textFont)
            return 0;
        unlockBackBufferIfLocked();
#ifdef _WIN32
        return m_textFont->drawFontText(x, y, color, text, 0u);
#else
        (void)x;
        (void)y;
        (void)text;
        (void)color;
        return 0;
#endif
    }


    int GRAPH::drawStringColored(float x, float y, const STRING& text, DWORD color)
    {

        return drawTextColored(x, y, text.c_str(), color);
    }

    void GRAPH::drawBackBufferPixel(float x, float y, DWORD color)
    {

        if (!(x >= m_viewportLeft && x < m_viewportRight &&
              y >= m_viewportTop && y < m_viewportBottom))
            return;

        (void)lockBackBuffer();
        const int ix = graphRetailFtolLow32(x);
        const int iy = graphRetailFtolLow32(y);
        const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(ix) +
                                     static_cast<std::ptrdiff_t>(iy) * static_cast<std::ptrdiff_t>(m_backBufferPitchPixels);

        if ((m_graphFlags & 2u) != 0u)
        {
            static_cast<DWORD*>(m_lockedBackBufferPixels)[index] = color;
            return;
        }

        const DWORD greenShift = 8u - g_color16GreenShift;
        const DWORD redShift = 16u - g_color16RedShift;
        const WORD packed = static_cast<WORD>(
            ((color >> greenShift) & g_color16GreenMask) |
            ((color >> redShift) & g_color16RedMask) |
            ((color >> 3u) & 0x1Fu));
        static_cast<WORD*>(m_lockedBackBufferPixels)[index] = packed;
    }

    bool GRAPH::DrawPixelToSoftwareBackBuffer(float x, float y, DWORD color)
    {

        if (!(x >= m_viewportLeft && x < m_viewportRight &&
              y >= m_viewportTop && y < m_viewportBottom))
            return false;
        drawBackBufferPixel(x, y, color);
        return true;
    }

    void GRAPH::drawLineRaster(float x0, float y0, float x1, float y1, DWORD color)
    {

        constexpr double kCoordinateLimit = 10000.0;
        if (std::fabs(static_cast<double>(x0)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "x in Line", 0, "GRAPH");
            x0 = 0.0f;
        }
        if (std::fabs(static_cast<double>(x1)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "x1 in Line", 0, "GRAPH");
            x1 = 0.0f;
        }
        if (std::fabs(static_cast<double>(y0)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "y in Line", 0, "GRAPH");
            y0 = 0.0f;
        }
        if (std::fabs(static_cast<double>(y1)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "y1 in Line", 0, "GRAPH");
            y1 = 0.0f;
        }

        int major = graphRetailFtolLow32(x0);
        int minor = graphRetailFtolLow32(y0);
        int majorStep = (x1 > x0) ? 1 : -1;
        int minorStep = (y1 > y0) ? 1 : -1;

        int dx = std::abs(graphRetailFtolLow32(x1 - x0));
        int dy = std::abs(graphRetailFtolLow32(y1 - y0));
        bool axesSwapped = false;
        if (dy > dx)
        {
            std::swap(major, minor);
            std::swap(dx, dy);
            std::swap(majorStep, minorStep);
            axesSwapped = true;
        }

        int error = dy * 2 - dx;
        const int errorAdvance = dy * 2;
        int remaining = dx;
        while (remaining != 0)
        {
            if (axesSwapped)
                drawBackBufferPixel(static_cast<float>(minor), static_cast<float>(major), color);
            else
                drawBackBufferPixel(static_cast<float>(major), static_cast<float>(minor), color);

            if (error >= 0)
            {
                const int subtract = dx * 2;
                do
                {
                    minor += minorStep;
                    error -= subtract;
                }
                while (error >= 0);
            }

            major += majorStep;
            error += errorAdvance;
            --remaining;
        }

        drawBackBufferPixel(x1, y1, color);
    }

    void GRAPH::DrawLine(float x1, float y1, float x2, float y2, DWORD color)
    {
        drawLineRaster(x1, y1, x2, y2, color);
    }

    void GRAPH::drawRectOutlineRaster(float left, float top, float right, float bottom, DWORD color)
    {

        drawLineRaster(left, top, right, top, color);
        drawLineRaster(left, bottom, right, bottom, color);
        drawLineRaster(left, top, left, bottom, color);
        drawLineRaster(right, top, right, bottom, color);
    }

    void GRAPH::DrawRect(float left, float top, float right, float bottom, DWORD color)
    {
        drawRectOutlineRaster(left, top, right, bottom, color);
    }

    bool GRAPH::BeginScene()
    {
        return beginSceneWithDeviceRecovery() == 0;
    }

    bool GRAPH::EndSceneAndPresent(bool doPresent)
    {
        return endSceneAndPresentRetail(doPresent ? 1 : 0) == 0;
    }

    double GRAPH::getViewportLeft() const noexcept
    {

        return static_cast<double>(m_viewportLeft);
    }

    double GRAPH::getViewportRight() const noexcept
    {

        return static_cast<double>(m_viewportRight);
    }

    double GRAPH::getViewportTop() const noexcept
    {

        return static_cast<double>(m_viewportTop);
    }

    double GRAPH::getViewportBottom() const noexcept
    {

        return static_cast<double>(m_viewportBottom);
    }

    int GRAPH::setViewportRetail(float left, float top, float right, float bottom)
    {

        m_viewportLeft = left;
        m_viewportRight = right;
        m_viewportTop = top;
        m_viewportBottom = bottom;
        g_softwareClipLeft = graphRetailFtolLow32(left);
        g_softwareClipRight = graphRetailFtolLow32(right);
        g_softwareClipTop = graphRetailFtolLow32(top);
        g_softwareClipBottom = graphRetailFtolLow32(bottom);

        int result = g_softwareClipBottom;
#ifdef _WIN32
        IDirect3DDevice8* const device = graphDevice(m_device);
        if (!device)
            return result;

        D3DVIEWPORT8 viewport{};
        viewport.X = static_cast<DWORD>(graphRetailFtolLow32(left));
        viewport.Y = static_cast<DWORD>(graphRetailFtolLow32(top));
        viewport.Width = static_cast<DWORD>(graphRetailFtolLow32(right - left));
        viewport.Height = static_cast<DWORD>(graphRetailFtolLow32(bottom - top));
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;

        result = static_cast<int>(device->SetViewport(&viewport));
        if (result != D3D_OK)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "viewport", result);

        D3DMATRIX matrix{};
        matrix._11 = 2.0f / static_cast<float>(static_cast<std::int32_t>(viewport.Width));
        matrix._22 = -2.0f / static_cast<float>(static_cast<std::int32_t>(viewport.Height));
        matrix._33 = (1.0f / (viewport.MaxZ - viewport.MinZ)) * 0.0049999999f;
        matrix._44 = 1.0f;

        result = static_cast<int>(device->SetTransform(D3DTS_PROJECTION, &matrix));
        if (result < 0)
            result = static_cast<int>(logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "Transform projection", result));
#else
#endif
        return result;
    }

    void GRAPH::SetViewport(float left, float top, float right, float bottom)
    {
        (void)setViewportRetail(left, top, right, bottom);
    }

    void GRAPH::Clear(DWORD color)
    {
        if (m_softwareDepthBuffer)
        {
            const std::size_t wordCount = static_cast<std::size_t>(m_softwareDepthPitch) *
                                          static_cast<std::size_t>(m_sizeY);
            std::fill_n(m_softwareDepthBuffer, wordCount, static_cast<std::uint16_t>(0x03FFu));
        }
#ifdef _WIN32
        if (!m_device)
            return;
        const DWORD clearFlags = D3DCLEAR_TARGET | (m_depthStencilFormat != 0u ? D3DCLEAR_ZBUFFER : 0u);
        (void)graphDevice(m_device)->Clear(0, nullptr, clearFlags, color, 1.0f, 0);
#else
        (void)color;
#endif
    }

    int GRAPH::drawTextureRectClipped(const RECTI& destination, const RECTI& source, BASE_TEXTURE& texture)
    {


        RECTI dst = destination;
        RECTI src = source;
        if (static_cast<float>(dst.right) < m_viewportLeft ||
            static_cast<float>(dst.left) >= m_viewportRight ||
            static_cast<float>(dst.bottom) < m_viewportTop ||
            static_cast<float>(dst.top) >= m_viewportBottom)
        {
            return 0;
        }

        if (static_cast<float>(dst.left) < m_viewportLeft)
        {
            const int clippedLeft = graphRetailFtolLow32(m_viewportLeft);
            src.left += clippedLeft - dst.left;
            dst.left = clippedLeft;
        }
        if (static_cast<float>(dst.top) < m_viewportTop)
        {
            const int clippedTop = graphRetailFtolLow32(m_viewportTop);
            src.top += clippedTop - dst.top;
            dst.top = clippedTop;
        }
        if (static_cast<float>(dst.right) > m_viewportRight)
        {
            const int clippedRight = graphRetailFtolLow32(m_viewportRight);
            src.right += clippedRight - dst.right;
            dst.right = clippedRight;
        }
        if (static_cast<float>(dst.bottom) > m_viewportBottom)
        {
            const int clippedBottom = graphRetailFtolLow32(m_viewportBottom);
            src.bottom += clippedBottom - dst.bottom;
            dst.bottom = clippedBottom;
        }

        int sourcePitchBytes = 0;
        const std::uint16_t* sourcePixels = texture.lock16(&sourcePitchBytes, &src);

        const int width = dst.right - dst.left;
        const int height = dst.bottom - dst.top;
        const int pairCount = width / 2;
        const int sourcePitchWords = sourcePitchBytes / 2;
        const std::ptrdiff_t destinationOffset =
            static_cast<std::ptrdiff_t>(dst.left) +
            static_cast<std::ptrdiff_t>(dst.top) * static_cast<std::ptrdiff_t>(m_softwareDepthPitch);
        std::uint16_t* destinationRow = m_softwareDepthBuffer + destinationOffset;
        const std::uint16_t* sourceRow = sourcePixels;

        for (int row = 0; row < height; ++row)
        {
            if (pairCount > 0)
                std::memcpy(destinationRow, sourceRow, static_cast<std::size_t>(pairCount) * 4u);
            if ((width & 1) != 0)
                destinationRow[pairCount * 2] = sourceRow[pairCount * 2];
            sourceRow += sourcePitchWords;
            destinationRow += m_softwareDepthPitch;
        }
        return 0;
    }

    int GRAPH::CopyTextureRectToSoftwareBackBuffer(BASE_TEXTURE& texture, const RECTI& destination, const RECTI& source)
    {
        return drawTextureRectClipped(destination, source, texture);
    }

    int GRAPH::drawPrimitiveUp(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount)
    {


        int primitiveCount = vertexCount;
        switch (primitiveType)
        {
        case 5:
        case 6:
            primitiveCount = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(vertexCount) - 2u);
            break;
        case 2:
            primitiveCount = vertexCount / 2;
            break;
        case 4:
            primitiveCount = vertexCount / 3;
            break;
        default:
            break;
        }

#ifdef _WIN32
        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetFVF(vertexShader);
        const HRESULT result = device->DrawPrimitiveUP(
            static_cast<D3DPRIMITIVETYPE>(primitiveType),
            static_cast<UINT>(primitiveCount),
            vertexData,
            static_cast<UINT>(vertexStride));
        if (result != D3D_OK)
            return static_cast<int>(logFileLoggerResourceError(
                g_fileLogger, "%s", 10, "DrawPrimitiveUP", static_cast<int>(result), "GRAPH"));
        return static_cast<int>(result);
#else
        (void)vertexData;
        return 0;
#endif
    }

    int GRAPH::DrawPrimitiveList(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount)
    {
        return drawPrimitiveUp(primitiveType, vertexShader, vertexData, vertexStride, vertexCount);
    }

    int GRAPH::SetLayer(int layer, DWORD textureToken, DWORD srcToken, DWORD flags, DWORD fallbackFlags)
    {
        if (layer < 0 || layer >= 16)
            return layer;
        DWORD resolved = fallbackFlags;
        if (resolved == 0)
        {
            switch (layer)
            {
            case 1:
            case 3:
                resolved = 2304;
                break;
            case 2:
                resolved = 512;
                break;
            case 5:
            case 10:
                resolved = 1024;
                break;
            case 9:
                resolved = 1280;
                break;
            default:
                break;
            }
        }
#ifdef _WIN32
        if (m_device)
        {
            IDirect3DDevice8* device = graphDevice(m_device);
            if (layer == 0)
            {
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            }
            else if (layer == 5)
            {
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            }
            else
            {
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            }
        }
#endif
        (void)textureToken;
        (void)srcToken;
        (void)flags;
        return static_cast<int>(resolved);
    }


    int GRAPH::initializeWindowDevice(void* hWnd)
    {

#ifdef _WIN32

        core::SetRealTimeMilliseconds(::timeGetTime());
        const STRING& registry = core::StartupRegistryPath();
        registry.WriteRegistryInt(STRING("ScreenX"), static_cast<int>(m_sizeX));
        registry.WriteRegistryInt(STRING("ScreenY"), static_cast<int>(m_sizeY));

        registry.WriteRegistryInt(STRING("BPP"), (m_graphFlags & 0x2u) != 0u ? 32 : 16);
        registry.WriteRegistryInt(STRING("Device"), m_selectedAdapterIndex);
        registry.WriteRegistryInt(STRING("FullScreen"), fullscreenRequested() ? 1 : 0);

        const int lowDetail = registry.ReadRegistryInt(STRING("LowDetail"), 0);
        m_graphFlags = (m_graphFlags & ~0x00000040u) | ((lowDetail & 1) != 0 ? 0x00000040u : 0u);

        const GraphAdapterRecord& startupCatalog = selectedAdapterRecord();
        m_graphFlags &= ~0x00000004u;

        const bool tripleBuffer =
            registry.ReadRegistryInt(STRING("TripleBuffer"), 1) != 0 &&
            fullscreenRequested() &&
            (m_graphFlags & 0x00000080u) == 0u;
        m_graphFlags = (m_graphFlags & ~0x00000008u) | (tripleBuffer ? 0x00000008u : 0u);

        if ((startupCatalog.capabilityFlags & 0x1u) == 0u)
            m_graphFlags |= 0x00000010u;

        if (!fullscreenRequested())
        {
            const int desktopWidth = ::GetSystemMetrics(SM_CXSCREEN);
            const int desktopHeight = ::GetSystemMetrics(SM_CYSCREEN);
            if (static_cast<float>(desktopWidth) < m_sizeX)
                m_sizeX = static_cast<float>(desktopWidth);
            if (static_cast<float>(desktopHeight) < m_sizeY)
                m_sizeY = static_cast<float>(desktopHeight);
            if (((m_graphFlags >> 1u) & 1u) !=
                static_cast<DWORD>(retailDisplayFormatBits(startupCatalog.desktopDisplayFormat) == 32))
            {
                m_graphFlags = (m_graphFlags & ~0x2u) |
                    (retailDisplayFormatBits(startupCatalog.desktopDisplayFormat) == 32 ? 0x2u : 0u);
            }
        }

        const std::int32_t rawAdapterLimit = static_cast<std::int32_t>(startupCatalog.videoMemoryBudgetBytes);
        const std::int32_t halfAdapterLimit =
            (rawAdapterLimit - (rawAdapterLimit < 0 ? -1 : 0)) >> 1;
        const double tripleBufferBytes =
            static_cast<double>(m_sizeY) * static_cast<double>(m_sizeX) * 2.0 * 3.0;
        if (tripleBufferBytes >= static_cast<double>(halfAdapterLimit))
            m_graphFlags &= ~0x00000008u;
        m_graphFlags &= ~0x00000008u;

        RECT windowRect{};
        RECT clientRect{};
        m_windowHandle = hWnd;
        HWND const window = static_cast<HWND>(m_windowHandle);
        GetWindowRect(window, &windowRect);
        GetClientRect(window, &clientRect);

        POINT clientTopLeft{clientRect.left, clientRect.top};
        POINT clientBottomRight{clientRect.right, clientRect.bottom};
        ClientToScreen(window, &clientTopLeft);
        ClientToScreen(window, &clientBottomRight);
        m_deviceLifecycleState = 0x17u;
#endif

        if (init(hWnd) != 0)
            return 1;
#ifdef _WIN32

        if (fullscreenRequested() && (m_graphFlags & 0x00000080u) == 0u)
        {
            (void)setViewportRetail(
                0.0f,
                0.0f,
                static_cast<float>(m_sizeX),
                static_cast<float>(m_sizeY));
        }
        else
        {
            (void)setViewportRetail(
                static_cast<float>(clientTopLeft.x - windowRect.left),
                static_cast<float>(clientTopLeft.y - windowRect.top),
                static_cast<float>(clientBottomRight.x - windowRect.left),
                static_cast<float>(clientBottomRight.y - windowRect.top));
        }

        LOG::Write(
            "SetViewPort (%.0f,%.0f) - (%.0f,%.0f)",
            m_viewportLeft,
            m_viewportTop,
            m_viewportRight,
            m_viewportBottom);

        LOG::Write("%s", buildTextureStageDebugText());
        LOG::Write("%s", buildTextureStageDebugText());
        publishBaseTextureCaps(m_device);

        m_lightBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x29u, 0u);
        if (!m_lightBuffer->isLoaded())
        {
            LOG::ResourceError("%s", 3, "light buffer", 0, "GRAPH");
            return 1;
        }
        if (m_lightBuffer->format() == 0x29u)
        {
            std::array<DWORD, 256> palette{};
            for (DWORD value = 0; value < 256u; ++value)
                palette[value] = 0xFF000000u | value | (value << 8u) | (value << 16u);
            m_lightBuffer->createPaletteSlot(palette.data());
        }

        m_hiBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x17u, 0u);
        if (!m_hiBuffer->isLoaded())
        {
            LOG::ResourceError("%s", 3, "hiBuffer", 0, "GRAPH");
            return 1;
        }

        m_alphaBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x1Au, 0u);
        if (!m_alphaBuffer->isLoaded())
        {
            LOG::ResourceError("%s", 3, "alphaBuffer", 0, "GRAPH");
            return 1;
        }

        clearFrameBuffers(0xFF000000u);
        rebuildTextFont(STRING("Courier"), 7, 8);

        if (m_hiBuffer->format() == 0x17u)
        {
            g_color16RedMask = 0xF800u;
            g_color16GreenMask = 0x07E0u;
            g_color16RedShift = 8u;
            g_color16GreenShift = 3u;
        }
        else
        {
            g_color16RedMask = 0x7C00u;
            g_color16GreenMask = 0x03E0u;
            g_color16RedShift = 7u;
            g_color16GreenShift = 2u;
        }
        buildGraphIntensityPalette(m_intensityPalette16, m_hiBuffer->format() == 0x17u);

        STRING caps;
        if ((m_graphFlags & 0x00000004u) != 0u)
            appendCStringToString(caps, "SOFTWARE ");
        else
            appendCStringToString(caps, "HARDWARE ");
        if ((m_graphFlags & 0x00000040u) != 0u)
            appendCStringToString(caps, "LOWDETAIL ");
        if (!fullscreenRequested())
            appendCStringToString(caps, "WINDOWED ");
        if ((m_graphFlags & 0x00000008u) != 0u)
            appendCStringToString(caps, "TRIPLEBUFFER ");
        else
            appendCStringToString(caps, "DOUBLEBUFFER ");
        if ((m_graphFlags & 0x00000002u) != 0u)
            appendCStringToString(caps, "COLOR32 ");
        if ((m_graphFlags & 0x00000020u) != 0u)
            appendCStringToString(caps, "VSYNC ");
        STRING pixelShader;
        constructFormattedString(pixelShader, "PIXELSHADER=%i", static_cast<int>(m_maxPixelShaderValueRaw));
        appendStringOwner(caps, pixelShader);
        LOG::Write("caps=%s", caps.c_str());

        if (!m_lockedBackBufferPixels)
        {
            D3DLOCKED_RECT lockedRect;
            const HRESULT lockResult =
                graphSurface(m_backBuffer)->LockRect(&lockedRect, nullptr, 0u);
            if (lockResult < 0)
                LOG::ResourceError("%s", 0, "backBuffer", 0, "GRAPH");

            m_lockedBackBufferPixels = lockedRect.pBits;
            const int bytesPerPixel = (m_graphFlags & 0x2u) != 0u ? 4 : 2;
            m_backBufferPitchPixels = lockedRect.Pitch / bytesPerPixel;
        }
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        const int bytesPerPixel = (m_graphFlags & 0x2u) != 0u ? 4 : 2;
        LOG::Write(
            "Pitch=%i zPitch=%i",
            m_backBufferPitchPixels * bytesPerPixel,
            2 * m_softwareDepthPitch);
        reloadPaletteLightBuffer();
#endif
        return 0;
    }


    int GRAPH::init(void* hWnd)
    {
#ifdef _WIN32

        const int requestedWidth = graphRetailFtolLow32(m_sizeX);
        const int requestedHeight = graphRetailFtolLow32(m_sizeY);
        const DWORD requestedColorBits = (m_graphFlags & 0x2u) != 0u ? 32u : 16u;
        const GraphAdapterRecord& adapterRecord = selectedAdapterRecord();
        const int modeIndex = adapterRecord.findDisplayModeIndex(
            requestedWidth,
            requestedHeight,
            static_cast<int>(requestedColorBits));

        int effectiveModeIndex = modeIndex;
        int effectiveWidth = requestedWidth;
        int effectiveHeight = requestedHeight;
        if (effectiveModeIndex < 0)
        {
            if (adapterRecord.displayModeCount == 0u)
            {
                return 1;
            }

            for (DWORD index = 0; index < adapterRecord.displayModeCount; ++index)
            {
                if (static_cast<int>(adapterRecord.displayModeWidths[index]) == requestedWidth &&
                    static_cast<int>(adapterRecord.displayModeHeights[index]) == requestedHeight)
                {
                    effectiveModeIndex = static_cast<int>(index);
                    break;
                }
            }
            if (effectiveModeIndex < 0)
                effectiveModeIndex = 0;

            effectiveWidth = static_cast<int>(adapterRecord.displayModeWidths[effectiveModeIndex]);
            effectiveHeight = static_cast<int>(adapterRecord.displayModeHeights[effectiveModeIndex]);
            m_sizeX = static_cast<float>(effectiveWidth);
            m_sizeY = static_cast<float>(effectiveHeight);

            const int fallbackBits = retailDisplayFormatBits(adapterRecord.displayModeFormats[effectiveModeIndex]);
            if (fallbackBits == 32)
                m_graphFlags |= 0x2u;
            else if (fallbackBits == 16)
                m_graphFlags &= ~0x2u;

        }

        std::memset(&m_d3d9PresentParameters, 0, sizeof(m_d3d9PresentParameters));
        D3DPRESENT_PARAMETERS& pp = m_d3d9PresentParameters;
        pp.BackBufferWidth = static_cast<UINT>(effectiveWidth);
        pp.BackBufferHeight = static_cast<UINT>(effectiveHeight);
        pp.BackBufferFormat = static_cast<D3DFORMAT>(
            fullscreenRequested()
                ? adapterRecord.displayModeFormats[effectiveModeIndex]
                : adapterRecord.desktopDisplayFormat);
        pp.BackBufferCount = 1u;
        pp.MultiSampleType = D3DMULTISAMPLE_NONE;
        pp.MultiSampleQuality = 0u;
        pp.SwapEffect = D3DSWAPEFFECT_COPY;
        pp.hDeviceWindow = static_cast<HWND>(m_windowHandle);
        pp.Windowed = !fullscreenRequested() ? TRUE : FALSE;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = static_cast<D3DFORMAT>(adapterRecord.depthStencilFormats[effectiveModeIndex]);
        pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        pp.FullScreen_RefreshRateInHz = 0u;
        pp.PresentationInterval = ((m_graphFlags & 0x00000020u) != 0u ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE);

        if (modeIndex < 0)
        {
            const int actualBackBufferBits = retailDisplayFormatBits(static_cast<DWORD>(pp.BackBufferFormat));
            if (actualBackBufferBits == 32)
                m_graphFlags |= 0x2u;
            else if (actualBackBufferBits == 16)
                m_graphFlags &= ~0x2u;
        }

        m_selectedDisplayFormat = static_cast<DWORD>(pp.BackBufferFormat);
        m_adapterDisplayFormat = adapterRecord.desktopDisplayFormat;
        m_depthStencilFormat = static_cast<DWORD>(pp.AutoDepthStencilFormat);

        IDirect3D8* const d3d = graphD3D(m_direct3D);
        IDirect3DDevice8* device = nullptr;

        DWORD vertexProcessing = (m_graphFlags & 0x00000004u) != 0u
            ? D3DCREATE_SOFTWARE_VERTEXPROCESSING
            : D3DCREATE_HARDWARE_VERTEXPROCESSING;
        HRESULT hr = d3d->CreateDevice(static_cast<UINT>(m_selectedAdapterIndex),
                                       D3DDEVTYPE_HAL,
                                       static_cast<HWND>(hWnd),
                                       vertexProcessing,
                                       &pp,
                                       &device);

        if (hr != D3D_OK && (m_graphFlags & 0x00000004u) == 0u)
        {
            m_graphFlags |= 0x00000004u;
            vertexProcessing = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
            hr = d3d->CreateDevice(static_cast<UINT>(m_selectedAdapterIndex),
                                   D3DDEVTYPE_HAL,
                                   static_cast<HWND>(hWnd),
                                   vertexProcessing,
                                   &pp,
                                   &device);
        }
        if (hr != D3D_OK)
        {
            LOG::ResourceError("%s", 3, "3dDevice", static_cast<int>(hr), "GRAPH");
            return 1;
        }

        m_device = device;

        {

            const char* const depthName = D3DFormatToString(adapterRecord.depthStencilFormats[effectiveModeIndex]);
            const char* const desktopName = D3DFormatToString(adapterRecord.desktopDisplayFormat);
            const char* const backBufferName = D3DFormatToString(static_cast<DWORD>(pp.BackBufferFormat));
            char selected[192] = {};
            std::snprintf(selected, sizeof(selected),
                          "Selected display mode %.0fx%.0f %s desktop %s zbuffer %s",
                          static_cast<double>(m_sizeX),
                          static_cast<double>(m_sizeY),
                          backBufferName, desktopName, depthName);
            LOG::Write("%s", selected);
        }

        D3DCAPS8 caps;
        hr = device->GetDeviceCaps(&caps);
        if (hr != D3D_OK)
        {
            LOG::ResourceError("%s", 9, "Caps", static_cast<int>(hr), "GRAPH");
            return 1;
        }

        std::memcpy(&m_maxPixelShaderValueRaw,
                    &caps.PixelShader1xMaxValue,
                    sizeof(DWORD));


        m_viewportLeft = 0.0f;
        m_viewportTop = 0.0f;
        m_viewportRight = m_sizeX;
        m_viewportBottom = m_sizeY;
        if (restoreDeviceObjectsRetail() != 0)
            return 1;

        return 0;
#else
        (void)hWnd;
        return 1;
#endif
    }

    bool GRAPH::presentClear()
    {
#ifdef _WIN32
        if (!m_device)
            return 1;
        if (beginSceneWithDeviceRecovery() != 0)
            return 1;
        Clear(D3DCOLOR_XRGB(0, 0, 0));
        return endSceneAndPresentRetail(1) == 0;
#else
        return 1;
#endif
    }

    bool GRAPH::fullscreenRequested() const noexcept
    {
        return (m_graphFlags & 0x10u) != 0u;
    }

    void GRAPH::setFullscreenRequested(bool enabled) noexcept
    {
        m_graphFlags = enabled ? (m_graphFlags | 0x10u) : (m_graphFlags & ~0x10u);
    }

    void GRAPH::queueSteamDisplayChange(int sizeX, int sizeY, int fullscreen) noexcept
    {
        // Native 0x96 stores PopInt() results in reverse address order:
        // +E40 fullscreen, +E3C height, +E38 width.
        m_pendingGraphOpA = sizeX;
        m_pendingGraphOpB = sizeY;
        m_pendingGraphOpC = fullscreen;
    }

    void GRAPH::applyPendingSteamDisplayChange()
    {
        const int width = m_pendingGraphOpA;
        const int height = m_pendingGraphOpB;
        const int fullscreen = m_pendingGraphOpC;
        const bool fullscreenOnlyChange = (width == -1 || height == -1) && fullscreen != -1;
        m_pendingGraphOpA = -1;
        m_pendingGraphOpB = -1;
        m_pendingGraphOpC = -1;

        if (width == -1 || height == -1)
        {
            if (fullscreen == -1)
                return;
            setFullscreenRequested(fullscreen != 0);
        }
        else
        {
            setScreenSize(width, height);
        }

#ifdef _WIN32
        if (!m_device || !m_windowHandle)
            return;

        if (width != -1 && height != -1)
            (void)setViewportRetail(m_viewportLeft, m_viewportTop, m_sizeX, m_sizeY);

        invalidateDeviceObjectsRetail();

        D3DPRESENT_PARAMETERS& pp = m_d3d9PresentParameters;
        pp.BackBufferWidth = static_cast<UINT>(graphRetailFtolLow32(m_sizeX));
        pp.BackBufferHeight = static_cast<UINT>(graphRetailFtolLow32(m_sizeY));
        pp.Windowed = fullscreenRequested() ? FALSE : TRUE;
        pp.hDeviceWindow = static_cast<HWND>(m_windowHandle);
        pp.BackBufferFormat = static_cast<D3DFORMAT>(m_selectedDisplayFormat);
        pp.AutoDepthStencilFormat = static_cast<D3DFORMAT>(m_depthStencilFormat);

        IDirect3DDevice8* const device = graphDevice(m_device);
        const HRESULT reset = device->Reset(&pp);
        if (FAILED(reset))
        {
            LOG::ResourceError("%s", 4, "Reset", static_cast<int>(reset), "GRAPH");
            return;
        }
        if (restoreDeviceObjectsRetail() != 0)
            return;

        if (fullscreenOnlyChange && fullscreenRequested())
            (void)setViewportRetail(0.0f, 0.0f, m_sizeX, m_sizeY);

        if (fullscreenOnlyChange && !fullscreenRequested())
        {
            const STRING& registry = core::StartupRegistryPath();
            const int y = registry.ReadRegistryInt(STRING("WindowPositionY"), 50);
            const int x = registry.ReadRegistryInt(STRING("WindowPositionX"), 50);
            (void)::SetWindowPos(
                static_cast<HWND>(m_windowHandle), nullptr, x, y, 0, 0, 0x201u);
        }
#endif
    }

    void GRAPH::SetWind(DWORD direction, float speed)
    {

        m_windDirection = direction & 0xFFu;
        m_windSpeed = speed;
    }

    const GammaRawPair* GRAPH::CurrentRawGammaPair()
    {
        return g_currentGraph ? &g_currentGraph->m_gammaPair : nullptr;
    }

    int GRAPH::setEffect(int effect, int argument1, int argument2, int duration)
    {

        int result = effect;
        if (effect < 0 || effect >= 16)
            return result;

        if (effect == 3 || effect == 9 || effect == 10)
        {
            m_effectStartTimes[10] = 0;
            m_effectStartTimes[9] = 0;
            m_effectStartTimes[3] = 0;
        }

        std::uint32_t rawDuration = static_cast<std::uint32_t>(duration);
        if (rawDuration == 0u)
        {
            rawDuration = 0x400u;
            switch (effect)
            {
            case 2: rawDuration = 0x200u; break;
            case 1:
            case 3: rawDuration = 0x900u; break;
            case 9: rawDuration = 0x500u; break;
            default: break;
            }
        }

        const std::size_t index = static_cast<std::size_t>(effect);
        m_effectStartTimes[index] = core::RealTimeMilliseconds();
        m_effectArgument1[index] = static_cast<std::uint32_t>(argument1);
        m_effectDurations[index] = rawDuration;
        m_effectArgument2[index] = static_cast<std::uint32_t>(argument2);

        if (effect == 0)
        {
            std::memset(m_effectStartTimes, 0, sizeof(m_effectStartTimes));
            return 0;
        }

        if (effect == 5)
        {
            if (m_tempBuffer)
            {
                m_effectStartTimes[10] = 0;
                m_effectStartTimes[9] = 0;
                m_effectStartTimes[3] = 0;
#ifdef _WIN32
                const HRESULT copyResult = graphDevice(m_device)->GetRenderTargetData(
                    graphSurface(m_backBuffer),
                    static_cast<IDirect3DSurface8*>(m_tempBuffer));
                result = static_cast<int>(copyResult);
                if (copyResult != D3D_OK)
                {
                    result = static_cast<int>(logFileLoggerResourceError(
                        g_fileLogger, "GRAPH", 1, "for EFF_ALPHAAPPEAR",
                        static_cast<int>(copyResult)));
                }
#else
                result = 0;
#endif
            }
            return result;
        }

        if (effect == 2)
        {
            const float cameraY = core::GlobalApplicationDrawDispatcherState().cameraShiftY();
            std::uint32_t cameraYBits = 0u;
            std::memcpy(&cameraYBits, &cameraY, sizeof(cameraYBits));
            m_effectSnapshotXBits = cameraYBits;
            m_effectSnapshotYBits = cameraYBits;
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(core::ApplicationPhysicalOwner()));
        }

        if (effect == 11)
            m_effectGammaPair = m_gammaPair;

        return result;
    }


    GraphEffectGammaRawPair* buildEffectGammaPair(GraphEffectGammaRawPair* destination, DWORD mask, DWORD color)
    {

        destination->inverseMask = ~mask;
        destination->color = color;
        return destination;
    }

    void GRAPH::DrawEffect(int drawEffects)
    {

        std::uint32_t now = core::RealTimeMilliseconds();

        const std::uint32_t effect5Start = m_effectStartTimes[5];
        if (effect5Start != 0u)
        {
            const std::uint32_t elapsed = now - effect5Start;
            const std::uint32_t duration = m_effectDurations[5];
            if (elapsed >= duration)
            {
                m_effectStartTimes[5] = 0u;
            }
            else
            {
                setAlphaBlendFactors(6u, 5u);
                std::uint32_t tileNow = now;
                const float left = m_viewportLeft;
                const float top = m_viewportTop;
                const float right = m_viewportRight;
                const float bottom = m_viewportBottom;
                for (float tileY = top; tileY < bottom; tileY += 256.0f)
                {
                    for (float tileX = left; tileX < right; tileX += 256.0f)
                    {
                        const float tileWidth = (tileX + 256.0f < right) ? 256.0f : (right - tileX);
                        const float tileHeight = (tileY + 256.0f < bottom) ? 256.0f : (bottom - tileY);
                        const RECTI destination{
                            graphRetailFtolLow32(tileX - left),
                            graphRetailFtolLow32(tileY - top),
                            graphRetailFtolLow32(tileX + tileWidth - left),
                            graphRetailFtolLow32(tileY + tileHeight - top)};
                        const RECTI copyOrigin{0, 0, 0, 0};
                        const RECTI source{0, 0,
                            graphRetailFtolLow32(tileWidth),
                            graphRetailFtolLow32(tileHeight)};
                        if (drawEffects != 0)
                        {
                            int copyResult = -1;
#ifdef _WIN32
                            copyResult = m_hiBuffer->PrepareSurfaceCopy(
                                static_cast<IDirect3DSurface8*>(m_tempBuffer), destination, &copyOrigin);
#else
                            copyResult = static_cast<int>(m_hiBuffer->PrepareSurfaceCopy(
                                m_tempBuffer, destination, &copyOrigin).surfaceCopyResult);
#endif
                            if (copyResult == 0)
                            {
                                const DWORD tileElapsed = tileNow - effect5Start;
                                const DWORD alpha = (tileElapsed << 8u) / duration;
                                const DWORD alphaWhite = GammaRawCreateARGB(
                                    static_cast<int>(alpha), 255, 255, 255);
                                GraphEffectGammaRawPair colors{};
                                buildEffectGammaPair(&colors, alphaWhite, GammaRawCreateOpaque(0, 0, 0));
#ifdef _WIN32
                                m_hiBuffer->DrawFixedDepthRectangle(destination, source, &colors.inverseMask);
#else
                                m_hiBuffer->DrawFixedDepthRectangle(
                                    destination, source, colors.inverseMask, colors.color);
#endif
                            }

                            tileNow = core::RealTimeMilliseconds();
                        }
                    }
                }
            }
            now = core::RealTimeMilliseconds();
        }

        // Effect 2: interpolate the MAP camera center from snapshots +0xCCC/
        // +0xCD0 toward integer targets +0xCDC/+0xD1C.
        const std::uint32_t effect2Start = m_effectStartTimes[2];
        if (effect2Start != 0u)
        {
            const std::uint32_t elapsed = now - effect2Start;
            const std::uint32_t duration = m_effectDurations[2];
            MAP* const map = MAP::Current();
            const float startX = rawFloat(m_effectSnapshotXBits);
            const float startY = rawFloat(m_effectSnapshotYBits);
            const float targetX = static_cast<float>(static_cast<std::int32_t>(m_effectArgument1[2]));
            const float targetY = static_cast<float>(static_cast<std::int32_t>(m_effectArgument2[2]));
            if (elapsed > duration)
            {
                map->SetShiftCoor(targetX, targetY, 0);
                m_effectStartTimes[2] = 0u;
            }
            else
            {

                const float t =
                    static_cast<float>(static_cast<std::int32_t>(elapsed)) /
                    static_cast<float>(static_cast<std::int32_t>(duration));
                map->SetShiftCoor((targetX - startX) * t + startX,
                                  (targetY - startY) * t + startY,
                                  0);
            }
            now = core::RealTimeMilliseconds();
        }

        const DWORD screenRightRaw = floatRaw(static_cast<float>(m_sizeX));
        const DWORD screenBottomRaw = floatRaw(static_cast<float>(m_sizeY));

        // Effect 1: RGB flash rendered three times through the additive overlay.
        const std::uint32_t effect1Start = m_effectStartTimes[1];
        if (effect1Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[1];
            const std::uint32_t elapsed = now - effect1Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[1] = 0u;
            }
            else if (drawEffects != 0)
            {
                const std::uint32_t ninth = duration / 9u;
                DWORD numerator = 0u;
                DWORD denominator = duration;
                if (elapsed >= ninth)
                {
                    numerator = (duration - elapsed) << 8u;
                    denominator = duration - ninth;
                }
                else
                {
                    numerator = (elapsed * 9u) << 8u;
                }
                const DWORD scale = numerator / denominator;
                const DWORD color = graphScaleRgb(m_effectArgument1[1], scale);
                drawAdditiveOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
                drawAdditiveOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
                drawAdditiveOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }
            now = core::RealTimeMilliseconds();
        }

        // Effect 3: white pulse with 4/9 rise, 1/9 plateau and 4/9 fall.
        const std::uint32_t effect3Start = m_effectStartTimes[3];
        if (effect3Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[3];
            const std::uint32_t elapsed = now - effect3Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[3] = 0u;
            }
            else if (drawEffects != 0)
            {
                const std::uint32_t riseEnd = (4u * duration) / 9u;
                const std::uint32_t plateauEnd = (5u * duration) / 9u;
                DWORD level = 255u;
                if (elapsed < riseEnd)
                    level = (2304u * elapsed) / (4u * duration);
                else if (elapsed > plateauEnd)
                    level = (2304u * (duration - elapsed)) / (4u * duration);
                const DWORD color = graphGrayRgb(level);
                drawAlphaOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }
            now = core::RealTimeMilliseconds();
        }

        // Effect 9: white fade-in reaching full intensity for the final fifth.
        const std::uint32_t effect9Start = m_effectStartTimes[9];
        if (effect9Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[9];
            const std::uint32_t elapsed = now - effect9Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[9] = 0u;
            }
            else if (drawEffects != 0)
            {
                const DWORD level = elapsed >= (4u * duration) / 5u
                    ? 255u
                    : (1280u * elapsed) / (4u * duration);
                const DWORD color = graphGrayRgb(level);
                drawAlphaOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }

            now = core::RealTimeMilliseconds();
        }

        // Effect 10: white fade-out.
        const std::uint32_t effect10Start = m_effectStartTimes[10];
        if (effect10Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[10];
            const std::uint32_t elapsed = now - effect10Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[10] = 0u;
            }
            else if (drawEffects != 0)
            {
                const DWORD level = ((duration - elapsed) << 8u) / duration;
                const DWORD color = graphGrayRgb(level);
                drawAlphaOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }
        }

        now = core::RealTimeMilliseconds();
        const std::uint32_t effect11Start = m_effectStartTimes[11];
        if (effect11Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[11];
            const std::uint32_t elapsed = now - effect11Start;
            const GammaRawPair targetGamma =
                graphLegacyGammaIndexToRaw(m_effectArgument1[11]);

            if (elapsed >= duration)
            {
                setGamma(targetGamma);
                m_effectStartTimes[11] = 0u;
                return;
            }

            if (drawEffects != 0)
            {
                const float t =
                    static_cast<float>(elapsed) / static_cast<float>(duration);
                setGamma(graphInterpolateGammaRawPair(m_effectGammaPair, targetGamma, t));
            }
        }
    }

    int GRAPH::getEffectState(int effect) const
    {

        if (effect < 1 || effect > 15)
            return -1;

        const std::size_t index = static_cast<std::size_t>(effect);
        const std::uint32_t start = m_effectStartTimes[index];
        if (start == 0u)
            return -1;

        const std::uint32_t now = core::RealTimeMilliseconds();
        const std::uint32_t elapsedTimes100 = (now - start) * 100u;
        return static_cast<int>(elapsedTimes100 / m_effectDurations[index]);
    }

    void GRAPH::setGamma(const GammaRawPair& rawGamma)
    {

        if (m_gammaPair.first == rawGamma.first &&
            m_gammaPair.second == rawGamma.second)
            return;

        m_gammaPair = rawGamma;

        auto& appVidTable = core::GlobalApplicationVidTable();
        const int rawVidCount = appVidTable.count();
        for (int slot = 0; slot < static_cast<int>(core::ApplicationVidTable::kCapacity); ++slot)
        {

            VID* const vid = (slot < rawVidCount) ? appVidTable.slot(slot) : nullptr;
            if (!vid)
                continue;

            vid->SetGammaRaw(rawGamma, 4);
        }
    }

    void GRAPH::setGamma(DWORD diffuse, DWORD specular)
    {
        setGamma(GammaRawPair{diffuse, specular});
    }

    void GRAPH::drawLightSourceRaw(float x, float y, float z, float sizeXValue, float sizeYValue, DWORD color)
    {


        g_lightScratchTextureToggle ^= 1u;

        if ((color & 0x00FFFFFFu) == 0u)
            return;

        const int sizeX = graphRetailFtolLow32(sizeXValue);
        const int sizeY = graphRetailFtolLow32(sizeYValue);
        int halfExtentX = static_cast<std::int32_t>(static_cast<std::uint32_t>(sizeX / 2) * 3u) & ~3;
        int halfExtentY = static_cast<std::int32_t>(static_cast<std::uint32_t>(sizeY / 2) * 3u) & ~3;
        if (halfExtentX > 512)
            halfExtentX = 512;
        if (halfExtentY > 512)
            halfExtentY = 512;

        const int depthScaleDivisor = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(sizeX) * static_cast<std::uint32_t>(sizeY)) / 500;
        const int sourceZ = graphRetailFtolLow32(z);
        const int doubledSourceZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceZ) * 2u);
        const int lightCenterZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(doubledSourceZ / 3) + 8u);
        const float centerX = x;
        const float centerY = y - (static_cast<float>(lightCenterZ) - z);

        const float right = centerX + static_cast<float>(halfExtentX);
        if (graphRetailFcompC0(right, m_viewportLeft))
            return;
        const float left = centerX - static_cast<float>(halfExtentX);
        if (!graphRetailFcompC0(left, m_viewportRight))
            return;
        const float bottom = centerY + static_cast<float>(halfExtentY);
        if (graphRetailFcompC0(bottom, m_viewportTop))
            return;
        const float top = centerY - static_cast<float>(halfExtentY);
        if (!graphRetailFcompC0(top, m_viewportBottom))
            return;

        RECTI destination{
            graphRetailFtolLow32(left),
            graphRetailFtolLow32(top),
            graphRetailFtolLow32(right),
            graphRetailFtolLow32(bottom)
        };
        RECTI source{0, 0, halfExtentX / 2, halfExtentY / 2};

        BASE_TEXTURE* const texture = g_lightScratchTextureToggle != 0u ? m_hiBuffer : m_lightBuffer;

        int texturePitchBytes = 0;
        std::uint16_t* const locked = texture->lock16(&texturePitchBytes, &source);
        if (!locked)
        {
            LOG::ResourceError("%s", 10, "light buffer", 0, "GRAPH");
            return;
        }

        const bool paletteTexture = texture->format() == 0x29u;
        const int texturePitch = paletteTexture ? texturePitchBytes : texturePitchBytes / 2;
        std::uint8_t* const output8 = reinterpret_cast<std::uint8_t*>(locked);
        std::uint16_t* const output16 = locked;
        const std::uint16_t* const worldDepth = softwareDepthBuffer();
        const int worldDepthPitch = softwareDepthPitch();

        int outputY = 0;
        for (int yOffset = -halfExtentY; yOffset < halfExtentY; yOffset += 4, outputY += 4)
        {
            const float sampleY = centerY + static_cast<float>(yOffset);
            const float sampleY3 = sampleY + 3.0f;
            const int outputRow = outputY / 4;

            for (int xOffset = -halfExtentX; xOffset < halfExtentX; xOffset += 4)
            {
                const float sampleX = centerX + static_cast<float>(xOffset);
                const float sampleX3 = sampleX + 3.0f;
                int sampledDepth = 0x7FFF;

                if (!graphRetailFcompC0(sampleX, m_viewportLeft) &&
                    graphRetailFcompC0(sampleX, m_viewportRight) &&
                    !graphRetailFcompC0(sampleY, m_viewportTop) &&
                    graphRetailFcompC0(sampleY, m_viewportBottom))
                {
                    const int x = static_cast<std::int32_t>(static_cast<std::uint32_t>(xOffset) + static_cast<std::uint32_t>(graphRetailFtolLow32(centerX)));
                    const int y = static_cast<std::int32_t>(static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(graphRetailFtolLow32(centerY)));
                    sampledDepth = static_cast<int>(worldDepth[y * worldDepthPitch + x] >> 3u) - 128;
                }

                int sampledDepth3 = 0x7FFF;
                if (!graphRetailFcompC0(sampleX3, m_viewportLeft) &&
                    graphRetailFcompC0(sampleX3, m_viewportRight) &&
                    !graphRetailFcompC0(sampleY3, m_viewportTop) &&
                    graphRetailFcompC0(sampleY3, m_viewportBottom))
                {
                    const int x = static_cast<std::int32_t>(static_cast<std::uint32_t>(xOffset) + static_cast<std::uint32_t>(graphRetailFtolLow32(centerX)) + 3u);
                    const int y = static_cast<std::int32_t>(static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(graphRetailFtolLow32(centerY)) + 3u);
                    sampledDepth3 = static_cast<int>(worldDepth[y * worldDepthPitch + x] >> 3u) - 128;
                }

                if (sampledDepth3 < sampledDepth)
                    sampledDepth = sampledDepth3;
                if (sampledDepth == 0x7FFF)
                    continue;

                const int vertical = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(sampledDepth) - static_cast<std::uint32_t>(lightCenterZ));
                const int depthDelta = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(sampledDepth) - static_cast<std::uint32_t>(sourceZ));
                const int xSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(xOffset) * static_cast<std::uint32_t>(xOffset));
                const int verticalSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(vertical) * static_cast<std::uint32_t>(vertical));
                const int verticalNine = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(verticalSquare) * 9u);
                const int depthSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(depthDelta) * static_cast<std::uint32_t>(depthDelta));
                const int metric = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(xSquare) +
                    static_cast<std::uint32_t>(verticalNine / 4) +
                    static_cast<std::uint32_t>(depthSquare / 4));
                int intensity = depthScaleDivisor != 0
                    ? 256 - metric / depthScaleDivisor
                    : 256 - metric;
                if (intensity < 0)
                    intensity = 0;
                else if (intensity > 255)
                    intensity = 255;

                const int outputX = (xOffset + halfExtentX) / 4;
                if (paletteTexture)
                    output8[outputRow * texturePitch + outputX] = static_cast<std::uint8_t>(intensity);
                else
                    output16[outputRow * texturePitch + outputX] = m_intensityPalette16[static_cast<std::size_t>(intensity)];
            }
        }

        texture->unlock();
        setRenderStateCached(29u, 0u);
        setAlphaBlendFactors(9u, 2u);

        ++source.left;
        --source.right;
        ++source.top;
        --source.bottom;

        const float deviceDepth =
            (z + sizeXValue + 50.0f) * 0.0001220703125f + 0.015625f;
        const DWORD deviceDepthRaw = graphFloatBits(deviceDepth);
        const DWORD colors[2] = {~color, 0xFF000000u};
#ifdef _WIN32
        texture->DrawDepthRectangle(deviceDepthRaw, deviceDepthRaw, destination, source, colors);
#else
        texture->DrawDepthRectangle(deviceDepthRaw, deviceDepthRaw, destination, source, colors[0], colors[1]);
#endif
    }


    void GRAPH::DrawLightSource(float x, float y, float z, float sizeXValue, float sizeYValue, DWORD color)
    {
        drawLightSourceRaw(x, y, z, sizeXValue, sizeYValue, color);
    }


    void GRAPH::LoadParameters(RESOURCE* map)
    {

        if (map->GoBegin(RESOURCE::ResTypes::GRAPH) == 0)
        {
            GammaRawPair raw{};
            map->read(&m_renderFlags, 4);
            map->read(&raw, 8);
            setGamma(raw);
            map->read(&m_windDirection, 4);
            map->read(&m_windSpeed, 4);

            return;
        }

        if (map->GoBegin(RESOURCE::ResTypes::HEAD) != 0 || map->SubSize() < 31)
            return;

        BYTE legacyHead[20]{};
        map->read(legacyHead, 20);

        std::uint32_t packedGamma = 0u;
        std::uint8_t direction = 0u;
        std::int16_t magnitude = 0;
        map->read(&m_renderFlags, 4);
        map->read(&packedGamma, 4);
        const GammaRawPair raw = graphLegacyGammaIndexToRaw(packedGamma);
        setGamma(raw);
        map->read(&direction, 1);
        map->read(&magnitude, 2);
        m_windDirection = direction;
        m_windSpeed = static_cast<float>(magnitude) * 0.001f;

    }
}
