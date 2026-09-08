#include "sprite.h"
#include "engine.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include "vid/vid_software.h"
#include "map.h"
#ifdef _WIN32
#include "win/application_win.h"
#endif
#include "graph.h"
#include "graphics/gamma.h"
#include "graphics/base_texture.h"
#include "sprite_collector_hash.h"
#include "rail.h"
#include "mouse.h"
#include "core/application.h"
#include "constant.h"
#include "base_sprite_list.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/configuration.h"
#include "core/profile_p.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/retail_stack_abi.h"
#include "core/weak_controller.h"
#include "sound/engine.h"
#include <array>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <new>
#include <memory>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#if defined(_MSC_VER)
#include <intrin.h>
#if defined(_M_IX86)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_SPRITE_STDCALL __stdcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_SPRITE_STDCALL __attribute__((stdcall))
#else
#define AS1_SPRITE_STDCALL
#endif

namespace as1
{
    SpriteCommandRecord* copyCommandRecord(SpriteCommandRecord* destination, const SpriteCommandRecord* source) noexcept
    {
        destination->opcode = source->opcode;
        destination->argument1 = source->argument1;
        destination->argument2 = source->argument2;
        destination->argument3 = source->argument3;
        return destination;
    }

    namespace
    {

        const char kEmptyString[] = "";
        const char kCommandRecordDelimiter[] = ";";
        const char kCommandWordPrefixMarker[] = { '\x01', '\0' };
        constexpr unsigned short kX87TruncateRoundingBits = static_cast<unsigned short>(3u << 10);
        constexpr unsigned short kX87RoundingBitsClearMask = static_cast<unsigned short>(~kX87TruncateRoundingBits);

        enum class InternalActionCode : std::uint32_t
        {
            RandomItemBySpriteType = 59u,
            CopyCommandPrefixToSprite = 75u,
            GetCommandStackCount = 76u,
            SetAnimationAndDirection = 201u,
            GetAnimation = 202u,
            ChangeCoordinateXY = 203u,
            ChangeCoordinateZ = 204u,
        };

        int g_pathSearchSecondaryBestCost = 0;
        int g_pathSearchResultScore = 0;

        constexpr char kTrainCollapseBeginLog[] =
        {
            static_cast<char>(0xF1), static_cast<char>(0xF5), static_cast<char>(0xEB),
            static_cast<char>(0xE0), static_cast<char>(0xEF), static_cast<char>(0xFB),
            static_cast<char>(0xE2), static_cast<char>(0xE0), static_cast<char>(0xED),
            static_cast<char>(0xE8), static_cast<char>(0xE5), ' ',
            static_cast<char>(0xE2), static_cast<char>(0xE0), static_cast<char>(0xE3),
            static_cast<char>(0xEE), static_cast<char>(0xED), static_cast<char>(0xEE),
            static_cast<char>(0xE2), ' ', 'z', 'm', '-', 'e', 'r', 'r', 'o', 'r', ' ',
            '-', ' ', 'k', 'a', 'w', 'a', 'b', 'a', 'n', 'g', 'a', ' ', '-', ' ',
            'b', 'e', 'g', 'i', 'n', '\0'
        };

        constexpr char kMissingTailDot2ResourceError[] =
        {
            static_cast<char>(240), static_cast<char>(229), static_cast<char>(235), static_cast<char>(252),
            static_cast<char>(241), static_cast<char>(251), ' ', static_cast<char>(237), static_cast<char>(229),
            static_cast<char>(239), static_cast<char>(240), static_cast<char>(224), static_cast<char>(226),
            static_cast<char>(232), static_cast<char>(235), static_cast<char>(252), static_cast<char>(237),
            static_cast<char>(251), static_cast<char>(229), ' ', static_cast<char>(237), static_cast<char>(229),
            static_cast<char>(242), ' ', 't', 'a', 'i', 'l', '.', 'D', 'o', 't', '2', '(', ')', 0
        };
        constexpr char kMissingLinkResourceError[] =
        {
            static_cast<char>(240), static_cast<char>(229), static_cast<char>(235), static_cast<char>(252),
            static_cast<char>(241), static_cast<char>(251), ' ', static_cast<char>(237), static_cast<char>(229),
            static_cast<char>(239), static_cast<char>(240), static_cast<char>(224), static_cast<char>(226),
            static_cast<char>(232), static_cast<char>(235), static_cast<char>(252), static_cast<char>(237),
            static_cast<char>(251), static_cast<char>(229), ' ', 'l', 'i', 'n', 'k', '>', 'n', 'o', 'l', 'i', 'n', 'k', 0
        };

        int g_collisionPushRecursionDepth = 0;

        std::uintptr_t g_currentImageFrameVtable = 0u;

        void captureCurrentImageFrameVtable(const FRAME* frame) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            g_currentImageFrameVtable = *reinterpret_cast<const std::uintptr_t*>(frame);
#else
            (void)frame;
#endif
        }

        void publishCurrentImageFrameVtable(FRAME* frame) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            if (g_currentImageFrameVtable != 0u)
                *reinterpret_cast<std::uintptr_t*>(frame) = g_currentImageFrameVtable;
#else
            (void)frame;
#endif
        }
        std::array<std::uint32_t, 1024> g_retailDirectionTrigWindow = {{
            0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
            0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
            0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        }};

        float spriteFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

                                                                     
                                                                               

        float directionSin(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionSinUnchecked(std::uint32_t index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[index]);
        }

        float directionCos(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[256u + static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionCosUnchecked(std::uint32_t index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[256u + index]);
        }

        float directionSinAux(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[512u + static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionCosAux(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[768u + static_cast<std::size_t>(index & 0xFF)]);
        }

        std::int32_t spriteImul32Low(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) *
                    static_cast<std::uint32_t>(b)));
        }

        std::int32_t spriteAdd32Wrap(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        std::int32_t spriteSub32Wrap(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        int spriteFtolLow32(long double value) noexcept;

        float spriteFildMulF32(std::int32_t value, float scale) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(value) * static_cast<long double>(scale));
        }

        float spriteFildToF32(std::int32_t value) noexcept
        {
            return static_cast<float>(value);
        }

        float spriteFildAddF32(std::int32_t value, float addend) noexcept
        {
            return static_cast<float>(
                static_cast<long double>(value) + static_cast<long double>(addend));
        }

        float spriteFildSubF32(std::int32_t value, float subtrahend) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(value) - static_cast<long double>(subtrahend));
        }

        float addThenSubtractRounded(float base, float addend, float subtractend) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(base) +
                static_cast<long double>(addend) -
                static_cast<long double>(subtractend));
        }

        float spriteWeightedQuarterF32(float primary, float secondary) noexcept
        {

            return static_cast<float>(
                (static_cast<long double>(primary) * 3.0L +
                 static_cast<long double>(secondary)) * 0.25L);
        }

        int spriteAddF32StoreAndFtolLow32(float value, float addend,
                                          float& storedValue) noexcept
        {

            const long double extended =
                static_cast<long double>(value) + static_cast<long double>(addend);
            storedValue = static_cast<float>(extended);
            return spriteFtolLow32(extended);
        }

        bool spriteFcompC3(float lhs, float rhs) noexcept
        {
            return lhs == rhs;
        }

        int spriteFtolLow32(long double value) noexcept
        {

            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(value)))));
        }

        int spriteFsubFtolLow32(float lhs, float rhs) noexcept
        {

            return spriteFtolLow32(static_cast<long double>(lhs) -
                                   static_cast<long double>(rhs));
        }

        int spriteFsubStoreF32FtolLow32(float lhs, float rhs) noexcept
        {
            const float rounded = static_cast<float>(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            return spriteFtolLow32(static_cast<long double>(rounded));
        }

        int spriteFmulFtolLow32(float value, float multiplier) noexcept
        {
            return spriteFtolLow32(static_cast<long double>(value) *
                                   static_cast<long double>(multiplier));
        }

        int spriteFdivMulFtolLow32(float numerator, float denominator, float multiplier) noexcept
        {
            return spriteFtolLow32(static_cast<long double>(numerator) /
                                   static_cast<long double>(denominator) *
                                   static_cast<long double>(multiplier));
        }

        float spriteFildMulStoreFloat(int value, float multiplier) noexcept
        {
            return static_cast<float>(static_cast<long double>(value) *
                                      static_cast<long double>(multiplier));
        }

        int pathScaledProgressQuotient(int progress, int delta, int duration) noexcept
        {

            std::uint32_t product =
                static_cast<std::uint32_t>(progress) * static_cast<std::uint32_t>(delta);
            product <<= 8;
            return static_cast<std::int32_t>(product) / duration;
        }

        float pathInterpolateCoordinate(int quotient, int base) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(quotient) +
                static_cast<long double>(base) * 256.0L);
        }

        float pathAverageCoordinate(float lhs, float rhs) noexcept
        {

            return static_cast<float>(
                (static_cast<long double>(lhs) + static_cast<long double>(rhs)) *
                0.001953125L);
        }

        int pathDirectionDeltaXToInt(float lhs, float rhs) noexcept
        {

            return spriteFtolLow32(
                (static_cast<long double>(lhs) - static_cast<long double>(rhs) + 128.0L) *
                0.00390625L);
        }

        int pathDirectionDeltaYToInt(float lhs, float rhs) noexcept
        {

            return spriteFtolLow32(
                (static_cast<long double>(lhs) - static_cast<long double>(rhs) + 128.0L) *
                3.0L * 0.001953125L);
        }

        int animationDelayFromSpeed(float speed) noexcept
        {

            const std::uint32_t scaled = static_cast<std::uint32_t>(
                spriteFmulFtolLow32(speed, 1000.0f));
            const std::uint32_t sign = 0u - (scaled >> 31);
            const std::uint32_t magnitude = (scaled ^ sign) - sign;
            const std::uint32_t plusTen = magnitude + 10u;
            const std::uint32_t sign2 = 0u - (plusTen >> 31);
            const std::uint32_t adjusted = plusTen - sign2;
            const std::uint32_t half = (adjusted >> 1) | (adjusted & 0x80000000u);
            return static_cast<std::int32_t>(0u - half);
        }

        bool spriteFildIntLessEqualOrUnordered(std::int32_t lhs, float rhs) noexcept
        {

            return std::isnan(rhs) ||
                   static_cast<long double>(lhs) <= static_cast<long double>(rhs);
        }

        void computeCollisionKinematics(float thisSpeedRaw, float targetSpeedRaw,
                                              float thisWeight, float targetWeight, int mode,
                                              float& sharedSpeedOut, float& relativeSpeedOut) noexcept
        {
            // resolveEngineChainCollision keeps the weighted-speed division live in x87 extended
            // precision for the lower clamp comparison, stores a binary32 copy,
            // and computes relative speed from the two fabs values that remain
            // on the x87 stack. Preserve both observable rounding boundaries.
            const long double thisSpeed = std::fabs(static_cast<long double>(thisSpeedRaw));
            const long double targetSpeed = std::fabs(static_cast<long double>(targetSpeedRaw));
            const long double numerator =
                static_cast<long double>(targetWeight) * targetSpeed +
                static_cast<long double>(thisWeight) * thisSpeed;
            const long double denominator =
                static_cast<long double>(targetWeight) + static_cast<long double>(thisWeight);
            const long double sharedExtended = numerator / denominator;
            sharedSpeedOut = static_cast<float>(sharedExtended);
            if (!std::isnan(sharedExtended) && sharedExtended > 0.001L &&
                (sharedSpeedOut < 0.01f || std::isnan(sharedSpeedOut)))
                sharedSpeedOut = 0.01f;
            const long double relative = (mode == 2 || mode == 3)
                ? std::fabs(thisSpeed - targetSpeed)
                : targetSpeed + thisSpeed;
            relativeSpeedOut = static_cast<float>(relative);
        }

        bool projectVerticalMotionDirection(int direction, float speed, float zSpeed,
                                            int& projectedDirection) noexcept
        {
            const float projectedX =
                (directionSin(direction) * speed) * 1000000.0f;
            const float projectedY =
                ((directionCos(direction) * speed) + zSpeed) * -1000000.0f;
            projectedDirection = RetailDirectionFromFloatXY(projectedX, projectedY).Int();
            return true;
        }

        double trainEndpointMetric(float x, float y, float nodeX, float nodeY) noexcept
        {
            // Every input is binary32 and the only scale is exactly 0.5, so
            // double retains all finite x87 precision needed by this metric.
            const double dx = std::fabs(static_cast<double>(x) - static_cast<double>(nodeX));
            const double dy = std::fabs(static_cast<double>(y) - static_cast<double>(nodeY));

            if (dx <= dy || std::isnan(dx) || std::isnan(dy))
                return dx * 0.5 + dy;
            return dx + dy * 0.5;
        }

        bool preferFirstTrainEndpoint(float x, float y,
                                    float prevX, float prevY,
                                    float nextX, float nextY) noexcept
        {
            const double firstMetric = trainEndpointMetric(x, y, prevX, prevY);
            const double lastMetric = trainEndpointMetric(x, y, nextX, nextY);

            return firstMetric < lastMetric || std::isnan(firstMetric) || std::isnan(lastMetric);
        }

        bool x87IsZeroOrUnordered(float value) noexcept
        {

            return value == 0.0f || std::isnan(value);
        }

        bool engineCommandReferenceBlockedRetail(const SPRITE* sprite) noexcept
        {
            const VID* const vid = sprite->Vid();
            return vid->nvid() == 45 || vid->weaponFloatAt(0x10) != 0.0f;
        }

        bool x87EqualOrUnordered(float value, float reference) noexcept
        {

            return value == reference || std::isnan(value) || std::isnan(reference);
        }

        bool x87LessOrUnordered(float lhs, float rhs) noexcept
        {

            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        bool x87LessEqualOrUnordered(float lhs, float rhs) noexcept
        {

            return lhs <= rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        bool x87OrderedLess(float lhs, float rhs) noexcept
        {
            return lhs < rhs;
        }

        bool x87OrderedGreater(float lhs, float rhs) noexcept
        {
            // Same ordered-comparison rule as above; NaN naturally returns false.
            return lhs > rhs;
        }

        bool x87AbsDiffGreaterOrdered(float lhs, float rhs, float limit) noexcept
        {

            const long double diff = std::fabs(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            const long double bound = static_cast<long double>(limit);
            return !std::isnan(diff) && !std::isnan(bound) && diff > bound;
        }

        bool metricWithinFromRoundedDeltas(float deltaX, float deltaY, float radius) noexcept
        {
            // Initial linked-child route in evaluateEngineTargetRangeState stores X/Y deltas to
            // binary32 stack locals, calls approximatePlanarDistance, then compares the live
            // x87 metric with (radius-10) using TEST AH,41h. Recreate that
            // exact numeric route without a premature metric spill.
            const long double ax = std::fabs(static_cast<long double>(deltaX));
            const long double ay = std::fabs(static_cast<long double>(deltaY));
            const long double metric =
                (ax <= ay || std::isnan(ax) || std::isnan(ay))
                    ? ax * 0.5L + ay
                    : ax + ay * 0.5L;
            const long double limit = static_cast<long double>(radius) - 10.0L;
            return metric <= limit || std::isnan(metric) || std::isnan(limit);
        }

        bool metricWithinPositions(float ownerX, float ownerY,
                                             float targetX, float targetY,
                                             float radius) noexcept
        {

            const long double ax = std::fabs(
                static_cast<long double>(ownerX) - static_cast<long double>(targetX));
            const long double ay = std::fabs(
                static_cast<long double>(ownerY) - static_cast<long double>(targetY));
            const long double metric =
                (ax <= ay || std::isnan(ax) || std::isnan(ay))
                    ? ax * 0.5L + ay
                    : ax + ay * 0.5L;
            const long double limit = static_cast<long double>(radius) - 10.0L;
            return metric <= limit || std::isnan(metric) || std::isnan(limit);
        }

        bool spriteBitsEqual(float value, std::uint32_t bits) noexcept
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            return raw == bits;
        }

        bool advanceAccelerationStep(std::int32_t deltaMs, float factor,
                                    float maxSpeed, float& speed) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 value = _mm_mul_ss(delta, _mm_set_ss(factor));
            value = _mm_add_ss(value, _mm_set_ss(speed));
            speed = _mm_cvtss_f32(value);
            return speed >= maxSpeed;
#else
            const float delta = static_cast<float>(deltaMs);
            speed = delta * factor + speed;
            return speed >= maxSpeed;
#endif
        }

        bool advanceDecelerationStep(std::int32_t deltaMs, float factor, float& speed) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 amount = _mm_mul_ss(delta, _mm_set_ss(factor));
            __m128 value = _mm_sub_ss(_mm_set_ss(speed), amount);
            speed = _mm_cvtss_f32(value);
            return speed < 0.0f;
#else
            const float delta = static_cast<float>(deltaMs);
            speed = speed - delta * factor;
            return speed < 0.0f;
#endif
        }

        void advancePlanarPosition(std::int32_t deltaMs, float speed,
                                float sinValue, float cosValue,
                                float& x, float& y) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 step = _mm_mul_ss(delta, _mm_set_ss(speed));
            const __m128 dx = _mm_mul_ss(step, _mm_set_ss(sinValue));
            const __m128 dy = _mm_mul_ss(step, _mm_set_ss(cosValue));
            x = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(x), dx));
            y = _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(y), dy));
#else
            const float step = static_cast<float>(deltaMs) * speed;
            x = x + step * sinValue;
            y = y - step * cosValue;
#endif
        }

        void applyGravityStep(std::int32_t deltaMs, float gravity, float& zSpeed) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 amount = _mm_mul_ss(delta, _mm_set_ss(gravity));
            zSpeed = _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(zSpeed), amount));
#else
            zSpeed = zSpeed - static_cast<float>(deltaMs) * gravity;
#endif
        }

        void advanceVerticalPosition(std::int32_t deltaMs, float zSpeed, float& z) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 step = _mm_mul_ss(delta, _mm_set_ss(zSpeed));
            z = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(z), step));
#else
            z = z + static_cast<float>(deltaMs) * zSpeed;
#endif
        }

        float applicationWorldFloatAt(std::size_t offset) noexcept
        {
            if (offset == core::retail_application_layout::MapExtentX)
                return core::ApplicationMapWidth();
            if (offset == core::retail_application_layout::MapExtentY)
                return core::ApplicationMapHeight();
            return 0.0f;
        }

        int computeRegionTileCount(float width, float height,
                                             float childSizeX, float childSizeY) noexcept
        {
            const long double value =
                static_cast<long double>(width) * static_cast<long double>(height) /
                static_cast<long double>(childSizeX) / static_cast<long double>(childSizeY);
            return spriteFtolLow32(value);
        }

        std::uint32_t computeChildAnimationCadence(int direction,
                                               float speed,
                                               float zSpeed,
                                               float childMaxZ,
                                               bool subtractGraphMotion,
                                               int graphDirection,
                                               float graphSpeed,
                                               float childSizeX,
                                               float childSizeY) noexcept
        {
            const std::uint32_t rawDirection = static_cast<std::uint32_t>(direction);
            const std::uint32_t rawGraphDirection = static_cast<std::uint32_t>(graphDirection);
            const float dirSin = spriteFloatFromBits(
                g_retailDirectionTrigWindow[512u + rawDirection]);
            const float dirCos = spriteFloatFromBits(
                g_retailDirectionTrigWindow[768u + rawDirection]);
            const float graphSin = spriteFloatFromBits(
                g_retailDirectionTrigWindow[512u + rawGraphDirection]);
            const float graphCos = spriteFloatFromBits(
                g_retailDirectionTrigWindow[768u + rawGraphDirection]);
            long double projectedX =
                static_cast<long double>(dirSin) * static_cast<long double>(speed);
            float projectedY = static_cast<float>(
                static_cast<long double>(zSpeed) - static_cast<long double>(childMaxZ) +
                static_cast<long double>(dirCos) * static_cast<long double>(speed));
            if (subtractGraphMotion)
            {
                projectedX -= static_cast<long double>(graphSpeed) * static_cast<long double>(graphSin);
                projectedY = static_cast<float>(
                    static_cast<long double>(projectedY) -
                    static_cast<long double>(graphSpeed) * static_cast<long double>(graphCos));
            }
            float xTime = 30000.0f;
            if (projectedX != 0.0L && !std::isnan(projectedX))
                xTime = static_cast<float>(static_cast<long double>(childSizeX) / std::fabs(projectedX));

            long double yTime = 30000.0L;
            if (projectedY != 0.0f && !std::isnan(projectedY))
                yTime = static_cast<long double>(childSizeY) /
                    std::fabs(static_cast<long double>(projectedY));
            const long double selected =
                (static_cast<long double>(xTime) < yTime ||
                 std::isnan(static_cast<long double>(xTime)) || std::isnan(yTime))
                    ? static_cast<long double>(xTime)
                    : yTime;
            return static_cast<std::uint32_t>(spriteFtolLow32(selected));
        }

        int computeDirectionToTarget(float targetX, float targetY,
                                     float sourceX, float sourceY) noexcept
        {
            const float dx = targetX - sourceX;
            const float dy = targetY - sourceY;
            const int x = static_cast<int>(dx);
            const int y = static_cast<int>(dy);
            int projectedLength = 0;
            return AngleFromXY(x, y, &projectedLength).Int();
        }

        float targetDistanceMetric(float targetX, float targetY,
                                 float sourceX, float sourceY) noexcept
        {
            const long double dx = std::fabs(
                static_cast<long double>(targetX) - static_cast<long double>(sourceX));
            const long double dy = std::fabs(
                static_cast<long double>(targetY) - static_cast<long double>(sourceY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            return static_cast<float>(metric);
        }

        std::int32_t spriteNeg32Wrap(std::int32_t value) noexcept
        {
            return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(value));
        }

        std::int32_t spriteAbs32Wrap(std::int32_t value) noexcept
        {
            const std::int32_t sign = value < 0 ? -1 : 0;
            return static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(value) ^ static_cast<std::uint32_t>(sign)) -
                static_cast<std::uint32_t>(sign));
        }

        bool x87SumGreaterThanAbsDiffOrdered(float boundA, float boundB,
                                             float lhs, float rhs) noexcept
        {
            const long double diff = std::fabs(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            const long double bound =
                static_cast<long double>(boundA) + static_cast<long double>(boundB);
            return !std::isnan(diff) && !std::isnan(bound) && bound > diff;
        }

        bool x87SumLessOrUnordered(float lhsA, float lhsB, float rhs) noexcept
        {

            const long double sum =
                static_cast<long double>(lhsA) + static_cast<long double>(lhsB);
            const long double right = static_cast<long double>(rhs);
            return sum < right || std::isnan(sum) || std::isnan(right);
        }

        bool shouldSuppressFlagmanCommand(std::int32_t x, std::int32_t y,
                                                std::int32_t range,
                                                float controlledX,
                                                float controlledY) noexcept
        {
            const long double dx = std::fabs(
                static_cast<long double>(x) - static_cast<long double>(controlledX));
            const long double dy = std::fabs(
                static_cast<long double>(y) - static_cast<long double>(controlledY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            const long double threshold = static_cast<long double>(range);
            return metric < threshold || std::isnan(metric) || std::isnan(threshold);
        }

        bool computeFalloffDamage(float sourceX, float sourceY,
                                       float candidateX, float candidateY,
                                       float deathRange, std::int32_t damageRaw,
                                       int& damageOut) noexcept
        {
            const long double dx = std::fabs(
                static_cast<long double>(candidateX) - static_cast<long double>(sourceX));
            const long double dy = std::fabs(
                static_cast<long double>(candidateY) - static_cast<long double>(sourceY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            const long double range = static_cast<long double>(deathRange);
            if (!(metric <= range || std::isnan(metric) || std::isnan(range)))
                return false;
            const long double scaledDamage =
                static_cast<long double>(damageRaw) -
                static_cast<long double>(damageRaw) * metric / range;
            damageOut = spriteFtolLow32(scaledDamage);
            return true;
        }

        int quantizeDirectionForVid(int direction, int directionBase, int directionCount) noexcept
        {
            if (directionCount == 0)
                return direction & 0xFF;
            const std::uint32_t angle = static_cast<std::uint32_t>(directionBase + direction) & 0xFFu;
            const std::uint32_t lowProduct = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(angle) * static_cast<std::uint32_t>(directionCount));
            const std::uint32_t numeratorBits = (lowProduct >> 8u) << 8u;
            const std::int32_t numerator = static_cast<std::int32_t>(numeratorBits);
            return numerator / directionCount;
        }

        void appendU32LE(std::vector<BYTE>& out, std::uint32_t v)
        {
            out.push_back(static_cast<BYTE>(v & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 16) & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 24) & 0xFF));
        }

        std::vector<BYTE> wordsToBytes(const std::vector<std::uint32_t>& words)
        {
            std::vector<BYTE> out;
            out.reserve(words.size() * 4);
            for (std::uint32_t v : words)
                appendU32LE(out, v);
            return out;
        }

    }

    void SPRITE::initializeRetailStartupTrigTables() noexcept
    {
        const float kScale4096 = 4096.0f;
        const float kScaleRadians = 0.00017262212f;
        for (std::size_t i = 0; i < 256u; ++i)
        {
            const float sourceSin = spriteFloatFromBits(g_retailDirectionTrigWindow[i]);
            const float sourceCosWindow = spriteFloatFromBits(g_retailDirectionTrigWindow[256u + i]);
            const float derivedSin = static_cast<float>(
                static_cast<long double>(sourceSin) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            const float derivedCosWindow = static_cast<float>(
                static_cast<long double>(sourceCosWindow) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            std::memcpy(&g_retailDirectionTrigWindow[512u + i], &derivedSin, sizeof(derivedSin));
            std::memcpy(&g_retailDirectionTrigWindow[768u + i], &derivedCosWindow, sizeof(derivedCosWindow));
        }
    }

    float SPRITE::rawDirectionSin(int index) noexcept
    {
        return directionSin(index);
    }

    float SPRITE::rawDirectionSinUnchecked(DWORD index) noexcept
    {
        return directionSinUnchecked(index);
    }

    float SPRITE::rawDirectionCos(int index) noexcept
    {
        return directionCos(index);
    }

    float SPRITE::rawDirectionCosUnchecked(DWORD index) noexcept
    {
        return directionCosUnchecked(index);
    }

    float SPRITE::rawDirectionSinAux(int index) noexcept
    {
        return directionSinAux(index);
    }

    float SPRITE::rawDirectionCosAux(int index) noexcept
    {
        return directionCosAux(index);
    }

    void* destroyCommandRecordListOwner(void* rawListOwner, unsigned char deleteSelfFlag) noexcept
    {
        auto* const raw = reinterpret_cast<BYTE*>(rawListOwner);
        const std::uint32_t vtable = currentCommandRecordListVtable();
        std::memcpy(raw + 0x00, &vtable, sizeof(vtable));

        std::uint32_t dataToken = 0u;
        std::memcpy(&dataToken, raw + 0x0C, sizeof(dataToken));
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (dataToken != 0u)
            ::operator delete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(dataToken)));
#endif
        const std::uint32_t zero = 0u;
        std::memcpy(raw + 0x0C, &zero, sizeof(zero));
        std::memcpy(raw + 0x04, &zero, sizeof(zero));

        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(rawListOwner);
        return rawListOwner;
    }

    namespace
    {
        struct CommandRecordListVtableOwner
        {
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return destroyCommandRecordListOwner(this, flags);
            }
        };
    }

    std::uint32_t currentCommandRecordListVtable() noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        static CommandRecordListVtableOwner owner;
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
        return 0u;
#endif
    }

    void* destroyCommandWordListOwner(void* rawListOwner, unsigned char deleteSelfFlag) noexcept
    {
        auto* const raw = reinterpret_cast<BYTE*>(rawListOwner);
        const std::uint32_t vtable = currentCommandWordListVtable();
        std::memcpy(raw + 0x00, &vtable, sizeof(vtable));

        std::uint32_t dataToken = 0;
        std::memcpy(&dataToken, raw + 0x0C, sizeof(dataToken));
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (dataToken != 0u)
            ::operator delete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(dataToken)));
#endif
        const std::uint32_t zero = 0u;
        std::memcpy(raw + 0x0C, &zero, sizeof(zero));
        std::memcpy(raw + 0x04, &zero, sizeof(zero));

        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(rawListOwner);
        return rawListOwner;
    }

    namespace
    {
        struct CommandWordListVtableOwner
        {
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return destroyCommandWordListOwner(this, flags);
            }
        };
    }

    std::uint32_t currentCommandWordListVtable() noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        static CommandWordListVtableOwner owner;
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
        return 0u;
#endif
    }

    namespace
    {
        using CommandWordListOwner = SpriteCommandStack::CommandWordList;

        std::unordered_map<const SpriteCommandStack*, CommandWordListOwner>& commandWordListSidecars()
        {
            static std::unordered_map<const SpriteCommandStack*, CommandWordListOwner> owners;
            return owners;
        }

        std::unordered_set<const SpriteCommandStack*>& physicalCommandWordListOwners()
        {
            static std::unordered_set<const SpriteCommandStack*> owners;
            return owners;
        }

        void eraseCommandWordListSidecar(const SpriteCommandStack* owner) noexcept
        {
            commandWordListSidecars().erase(owner);
        }
    }

    void SpriteCommandStack::markCommandWordsPhysicalOwner(bool enabled) noexcept
    {
        (void)enabled;
        physicalCommandWordListOwners().erase(this);
    }

    SpriteCommandStack::CommandWordList& SpriteCommandStack::commandWords() noexcept
    {
        return commandWordListSidecars()[this];
    }

    const SpriteCommandStack::CommandWordList& SpriteCommandStack::commandWords() const noexcept
    {
        return commandWordListSidecars()[this];
    }

    SpriteCommandStack::SpriteCommandStack()
    {
        m_commandRecords.vtableTag = currentCommandRecordListVtable();
    }

    SpriteCommandStack::SpriteCommandStack(const SpriteCommandStack& other)
    {
        copyCommandRecordsFrom(other);
        copyCommandWordsFrom(other);
    }

    SpriteCommandStack& SpriteCommandStack::operator=(const SpriteCommandStack& other)
    {
        if (this == &other)
            return *this;
        copyCommandRecordsFrom(other);
        copyCommandWordsFrom(other);
        return *this;
    }

    int PathSearchSecondaryBestCost() noexcept { return g_pathSearchSecondaryBestCost; }
    int PathSearchResultScore() noexcept { return g_pathSearchResultScore; }

    SpriteCommandStack::SpriteCommandStack(SpriteCommandStack&& other) noexcept
        : m_commandRecords(other.m_commandRecords)
    {
        commandWords() = other.commandWords();
        other.m_commandRecords.count = 0;
        other.m_commandRecords.capacity = 0;
        other.m_commandRecords.records = nullptr;
        other.commandWords().count = 0;
        other.commandWords().capacity = 0;
        other.commandWords().words = nullptr;
        eraseCommandWordListSidecar(&other);
    }

    SpriteCommandStack& SpriteCommandStack::operator=(SpriteCommandStack&& other) noexcept
    {
        if (this == &other)
            return *this;
        releaseCommandRecords();
        releaseCommandWords();
        m_commandRecords = other.m_commandRecords;
        commandWords() = other.commandWords();
        other.m_commandRecords.count = 0;
        other.m_commandRecords.capacity = 0;
        other.m_commandRecords.records = nullptr;
        other.commandWords().count = 0;
        other.commandWords().capacity = 0;
        other.commandWords().words = nullptr;
        eraseCommandWordListSidecar(&other);
        return *this;
    }

    SpriteCommandStack::~SpriteCommandStack()
    {
        releaseCommandRecords();
        releaseCommandWords();
        markCommandWordsPhysicalOwner(false);
        eraseCommandWordListSidecar(this);
    }

    void SpriteCommandStack::releaseCommandRecords()
    {
        if (m_commandRecords.records)
            ::operator delete(m_commandRecords.records);
        m_commandRecords.records = nullptr;
        m_commandRecords.vtableTag = currentCommandRecordListVtable();
        m_commandRecords.count = 0;
        m_commandRecords.capacity = 0;
    }

    void SpriteCommandStack::releaseCommandRecordsRetailTail()
    {
        m_commandRecords.vtableTag = currentCommandRecordListVtable();
        if (m_commandRecords.records)
            ::operator delete(m_commandRecords.records);
        m_commandRecords.records = nullptr;
        m_commandRecords.count = 0;
    }

    void SpriteCommandStack::clearTargetReferences(SPRITE* target)
    {
        if (!target || !m_commandRecords.records)
            return;

        const std::uint32_t targetBits = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(target) & 0xFFFFFFFFu);
        const std::uint32_t count = m_commandRecords.count;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            CommandRecordStorage& raw = m_commandRecords.records[i];
            if (raw.words[1] != targetBits)
                continue;

            const std::uint32_t opcode = raw.words[0];
            if (opcode != 32u && opcode != 34u && opcode != 74u &&
                opcode != 150u && opcode != 151u && opcode != 152u)
                continue;

            raw.words[0] = 255u;
            raw.words[1] = 0u;
        }
    }

    void SpriteCommandStack::copyCommandRecordsFrom(const SpriteCommandStack& other)
    {
        releaseCommandRecords();
        m_commandRecords.vtableTag = other.m_commandRecords.vtableTag;
        if (other.m_commandRecords.count == 0)
            return;
        ensureCommandRecordCapacity(other.m_commandRecords.count);
        std::memcpy(m_commandRecords.records, other.m_commandRecords.records, other.m_commandRecords.count * sizeof(CommandRecordStorage));
        m_commandRecords.count = other.m_commandRecords.count;
    }

    void SpriteCommandStack::ensureCommandRecordCapacity(std::uint32_t requiredCapacity)
    {
        if (static_cast<std::int32_t>(requiredCapacity) <=
            static_cast<std::int32_t>(m_commandRecords.capacity))
            return;

        CommandRecordStorage* const oldArray = m_commandRecords.records;
        const std::uint32_t oldCapacity = m_commandRecords.capacity;
        const std::uint32_t allocationBytes = requiredCapacity << 4;
        CommandRecordStorage* const newArray = static_cast<CommandRecordStorage*>(
            ::operator new(static_cast<std::size_t>(allocationBytes), std::nothrow));
        m_commandRecords.records = newArray;
        if (!m_commandRecords.records)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(requiredCapacity));
        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
        {
            // ensureCommandRecordCapacityRetail copies one 16-byte element per signed-positive old
            // capacity.  Normal capacities are tiny; the explicit loop also
            // keeps the owner semantics independent of host size_t width.
            for (std::uint32_t i = 0; i < oldCapacity; ++i)
                m_commandRecords.records[i] = oldArray[i];
        }
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.capacity = requiredCapacity;
    }

    void SpriteCommandStack::writeCommandRecord(std::size_t index, const SpriteCommandRecord& command)
    {
        CommandRecordStorage& raw = m_commandRecords.records[index];
        raw.words[0] = command.opcode;
        raw.words[1] = command.argument1;
        raw.words[2] = command.argument2;
        raw.words[3] = command.argument3;
    }

    void SpriteCommandStack::releaseCommandWords()
    {
        if (commandWords().words)
            ::operator delete(commandWords().words);
        commandWords().words = nullptr;
        commandWords().count = 0;
        commandWords().capacity = 0;
    }

    void SpriteCommandStack::initializeCommandWords()
    {
        CommandWordList& owner = commandWords();
        owner.count = 0;
        owner.capacity = 0;
        owner.words = nullptr;
    }

    void SpriteCommandStack::releaseCommandWordsRetailTail()
    {
        if (commandWords().words)
            ::operator delete(commandWords().words);
        commandWords().words = nullptr;
        commandWords().count = 0;
    }

    void SpriteCommandStack::copyCommandWordsFrom(const SpriteCommandStack& other)
    {
        releaseCommandWords();
        if (other.commandWords().count == 0)
            return;
        ensureCommandWordCapacity(other.commandWords().count);
        std::memcpy(commandWords().words, other.commandWords().words, other.commandWords().count * sizeof(std::int16_t));
        commandWords().count = other.commandWords().count;
    }

    void SpriteCommandStack::ensureCommandWordCapacity(std::uint32_t requiredCapacity)
    {
        if (static_cast<std::int32_t>(requiredCapacity) <=
            static_cast<std::int32_t>(commandWords().capacity))
            return;

        std::int16_t* const oldArray = commandWords().words;
        const std::uint32_t oldCapacity = commandWords().capacity;
        std::int16_t* const newArray = static_cast<std::int16_t*>(::operator new(requiredCapacity * sizeof(std::int16_t), std::nothrow));
        commandWords().words = newArray;
        if (!commandWords().words)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(requiredCapacity));
        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
            std::memcpy(commandWords().words, oldArray, oldCapacity * sizeof(std::int16_t));
        if (oldArray)
            ::operator delete(oldArray);
        commandWords().capacity = requiredCapacity;
    }

    void SpriteCommandStack::appendCommandWord(std::int16_t word)
    {
        if (commandWords().count >= commandWords().capacity)
        {
            const std::uint32_t oldCapacity = commandWords().capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (grownCapacity > oldCapacity)
                ensureCommandWordCapacity(grownCapacity);
        }
        commandWords().words[commandWords().count] = word;
        ++commandWords().count;
    }

    std::uint32_t SpriteCommandStack::commandWordCount() const noexcept
    {
        return commandWords().count;
    }

    const std::int16_t* SpriteCommandStack::commandWordData() const noexcept
    {
        return commandWords().words;
    }

    int SpriteCommandStack::findLastCommandWord(std::uint16_t word) const noexcept
    {
        std::uint32_t eax = commandWords().count;
        const std::int16_t* ecx = commandWords().words;
        if (eax == 0)
            return -1;

        ecx += eax;
        while (eax != 0)
        {
            --ecx;
            --eax;
            if (static_cast<std::uint16_t>(*ecx) == word)
                return static_cast<int>(eax);
        }
        return -1;
    }

    int SpriteCommandStack::findLastCommandWordPointer(const std::int16_t* word) const noexcept
    {
        return findLastCommandWord(static_cast<std::uint16_t>(*word));
    }

    int SpriteCommandStack::removeCommandWordAt(int index) noexcept
    {
        if (index < 0)
            return 1;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        std::uint32_t count = commandWords().count;
        if (index >= static_cast<std::int32_t>(count))
            return 1;
        --count;
        commandWords().count = count;
        commandWords().words[rawIndex] = commandWords().words[count];
        return 0;
    }

    void SpriteCommandStack::ensureCommandWordCapacityRetail(std::uint32_t requiredCapacity)
    {
        ensureCommandWordCapacity(requiredCapacity);
    }

    void SpriteCommandStack::appendCommandWordRetail(std::int16_t word)
    {
        auto& list = commandWords();
        if (static_cast<std::int32_t>(list.count) >= static_cast<std::int32_t>(list.capacity))
        {
            const std::uint32_t oldCapacity = list.capacity;
            ensureCommandWordCapacityRetail(oldCapacity * 2u + 4u);
        }
        list.words[list.count] = word;
        ++list.count;
    }

    void SpriteCommandStack::clear()
    {
        CommandRecordStorage* const oldArray = m_commandRecords.records;
        m_commandRecords.capacity = 0;
        m_commandRecords.count = 0;
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.records = nullptr;
    }

    void SpriteCommandStack::appendCommandRecord(const SpriteCommandRecord& command)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
                ensureCommandRecordCapacity(grownCapacity);
        }
        const std::uint32_t oldCount = m_commandRecords.count;
        writeCommandRecord(oldCount, command);
        ++m_commandRecords.count;
    }

    void SpriteCommandStack::prependCommandRecord(const SpriteCommandRecord& command)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
                ensureCommandRecordCapacity(grownCapacity);
        }
        const std::uint32_t oldCount = m_commandRecords.count;
        m_commandRecords.count = oldCount + 1u;
        if (oldCount != 0)
        {
            for (std::size_t i = oldCount; i > 0; --i)
                m_commandRecords.records[i] = m_commandRecords.records[i - 1u];
        }
        writeCommandRecord(0, command);
    }

    void SpriteCommandStack::insertCommandRecord(size_t index, const SpriteCommandRecord& command)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
                ensureCommandRecordCapacity(grownCapacity);
        }
        const std::uint32_t oldCount = m_commandRecords.count;
        m_commandRecords.count = oldCount + 1u;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        if (static_cast<std::int32_t>(oldCount) > static_cast<std::int32_t>(rawIndex))
        {
            for (std::uint32_t i = oldCount; i > rawIndex; --i)
                m_commandRecords.records[i] = m_commandRecords.records[i - 1u];
        }
        writeCommandRecord(rawIndex, command);

    }

    void SpriteCommandStack::ensureCommandRecordCapacityRetail(std::uint32_t requiredCapacity)
    {
        ensureCommandRecordCapacity(requiredCapacity);
    }

    void SpriteCommandStack::setCommandRecordCount(std::uint32_t count)
    {

        m_commandRecords.count = count;
    }

    void SpriteCommandStack::serializeCommandRecordsText(STRING& out) const
    {
        STRING serializedText;
        const int recordCount = static_cast<int>(m_commandRecords.count);
        for (int i = 0; i < recordCount; ++i)
        {
            const CommandRecordStorage& raw = m_commandRecords.records[static_cast<std::size_t>(i)];
            const unsigned char encoded = static_cast<unsigned char>((raw.words[0] + RetailSpriteLayout::CommandSerializationBias) & 0xFFu);
            STRING recordText;
            constructFormattedString(recordText, "%c%i,%i,%i;",
                static_cast<char>(encoded),
                static_cast<int>(raw.words[1]),
                static_cast<int>(raw.words[2]),
                static_cast<int>(raw.words[3]));
            appendStringOwner(serializedText, recordText);
            recordText.ReleaseOwnedStorage();
        }
        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    std::string SpriteCommandStack::serializeCommandRecordsText() const
    {
        STRING out;
        serializeCommandRecordsText(out);
        return out.str();
    }

    void SpriteCommandStack::parseCommandRecordsText(const STRING& text)
    {
        STRING remainingText(text);
        std::uint32_t encodedOpcodeWord = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
        int parsedArgument1;
        int parsedArgument2;
        int parsedArgument3;
#else
        int parsedArgument1 = 0;
        int parsedArgument2 = 0;
        int parsedArgument3 = 0;
#endif
        while (std::strcmp(remainingText.c_str(), kEmptyString) != 0)
        {
            char* const encodedOpcodeByte = reinterpret_cast<char*>(&encodedOpcodeWord);
            std::sscanf(remainingText.c_str(), "%c%i,%i,%i", encodedOpcodeByte, &parsedArgument1, &parsedArgument2, &parsedArgument3);

            const int argument1Value = static_cast<int>(core::retailReadStackDword(&parsedArgument1));
            const int argument2Value = static_cast<int>(core::retailReadStackDword(&parsedArgument2));
            const int argument3Value = static_cast<int>(core::retailReadStackDword(&parsedArgument3));
            SpriteCommandRecord rec = SPRITE::buildCommandRecord(
                (encodedOpcodeWord & 0xFFu) - 0x3Cu,
                argument1Value, argument2Value, argument3Value);
            appendCommandRecord(rec);

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandRecordDelimiter);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();
        }
        remainingText.ReleaseOwnedStorage();
    }

    void SpriteCommandStack::queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        std::uint32_t esi = m_commandRecords.count;
        if (esi != 0 && m_commandRecords.records)
        {
            while (esi != 0)
            {
                const std::uint32_t idx = esi - 1u;
                const CommandRecordStorage& raw = m_commandRecords.records[idx];
                --esi;
                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK) && raw.words[1] == 0u && raw.words[2] == 0u && raw.words[3] == 0u)
                {
                    SpriteCommandRecord rec = SPRITE::buildCommandRecord(opcode, argument1, argument2, argument3);
                    insertCommandRecord(esi + 1u, rec);
                    return;
                }
            }
        }

        SpriteCommandRecord rec = SPRITE::buildCommandRecord(opcode, argument1, argument2, argument3);
        prependCommandRecord(rec);
    }

    void SpriteCommandStack::saveCommandRecordsToStream(BaseStream* stream)
    {
        if (m_commandRecords.count == 1u
            && m_commandRecords.records[0].words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
        {
            clear();
            }

        const std::uint32_t count = m_commandRecords.count;
        stream->write(&count, 4u);
        const std::uint32_t bytes = count << 4;
        stream->write(m_commandRecords.records, bytes);
    }

    void SpriteCommandStack::restoreCommandRecordsFromStream(BaseStream* stream, const SPRITE* ownerSprite)
    {
        std::uint32_t count = 0;
        stream->read(&count, 4u);
        m_commandRecords.count = count;
        if (static_cast<std::int32_t>(count) > static_cast<std::int32_t>(m_commandRecords.capacity))
            ensureCommandRecordCapacity(count);

        const std::uint32_t bytes = count << 4;
        stream->read(m_commandRecords.records, bytes);

        const std::uint32_t rawLastIndex = m_commandRecords.count - 1u;
        std::int32_t index = static_cast<std::int32_t>(rawLastIndex);
        while (index >= 0)
        {
            CommandRecordStorage& raw = m_commandRecords.records[index];
            const std::uint32_t opcode = raw.words[0];
            if (opcode == static_cast<std::uint32_t>(ActionCode::ACT_ATTACK) || opcode == static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO) || opcode == 0x4Au
                || opcode == 0x4Bu || opcode == 0x96u || opcode == 0x97u || opcode == 0x98u)
            {
                raw.words[1] = ownerSprite->rawResolveOldSpriteHandleLow32(static_cast<int>(raw.words[1]));
            }
            else if (index != 0 && opcode == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK) && m_commandRecords.records[index - 1].words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
            {
                const std::uint32_t oldCount = m_commandRecords.count;
                if (index < static_cast<std::int32_t>(oldCount))
                {
                    const std::uint32_t newCount = oldCount - 1u;
                    m_commandRecords.count = newCount;
                    m_commandRecords.records[index] = m_commandRecords.records[newCount];
                }
            }
            --index;
        }
    }

    bool SpriteCommandStack::restoreOldMapCommandRecordsFromStream(BaseStream* stream, int mapVersion, const SPRITE* ownerSprite, int* armyBucket)
    {
        if (armyBucket)
            *armyBucket = 0;

        std::int16_t signedCount = 0;
        stream->read(&signedCount, 2u);
        const std::int32_t count = static_cast<std::int32_t>(signedCount);
        m_commandRecords.count = static_cast<std::uint32_t>(count);

        if (count > static_cast<std::int32_t>(m_commandRecords.capacity))
            ensureCommandRecordCapacity(static_cast<std::uint32_t>(count));

        const std::uint32_t legacyBytes = static_cast<std::uint32_t>(count) * 12u;
        stream->read(m_commandRecords.records, legacyBytes);

        if (mapVersion < 7)
            clear();

        {
            std::int32_t i = 0;
            const std::int32_t normalizedCount =
                static_cast<std::int32_t>(m_commandRecords.count);
            while (i < normalizedCount)
            {
                CommandRecordStorage& raw =
                    m_commandRecords.records[static_cast<std::uint32_t>(i)];
                raw.words[0] &= 0xFFu;
                raw.words[3] = 0u;
                if (raw.words[0] == 0x28u)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_MOVE);
                else if (raw.words[0] == 0x27u)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_ATTACK);
                else if (raw.words[0] == 0x2Fu)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK);
                else
                {
                    const int nvid = ownerSprite->Vid() ? ownerSprite->Vid()->nVid : -1;
                    LOG::ResourceError("SPRITE %i", 14, "actionStack.act restore", static_cast<int>(raw.words[0]), nvid);
                }

                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_ATTACK) || raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO) || raw.words[0] == 0x4Au
                    || raw.words[0] == 0x96u || raw.words[0] == 0x97u || raw.words[0] == 0x98u)
                {
                    raw.words[1] = ownerSprite->rawResolveOldSpriteHandleLow32(static_cast<int>(raw.words[1]));
                }
                ++i;
            }
        }

        bool hasArmyBucket = false;
        if (mapVersion >= 7)
        {
            std::uint32_t rawArmy = 0u;
            stream->read(&rawArmy, 1u);
            if (armyBucket)
                *armyBucket = static_cast<int>(rawArmy & 0xFFu);
            hasArmyBucket = true;
        }
        return hasArmyBucket;
    }

    void SpriteCommandStack::serializeCommandWordsText(STRING& out) const
    {
        static const char kCommandWordFormat[] = { '\x01', '%', 'i', '\0' };
        static const char kCommandSectionDelimiter[] = { '\x02', '\0' };

        STRING serializedText;
        const int wordCount = static_cast<int>(commandWords().count);
        for (int i = 0; i < wordCount; ++i)
        {
            const std::int16_t word = commandWords().words[static_cast<std::size_t>(i)];
            STRING wordText;
            constructFormattedString(wordText, kCommandWordFormat, static_cast<int>(word));
            appendStringOwner(serializedText, wordText);
            wordText.ReleaseOwnedStorage();
        }

        if (wordCount != 0)
            appendCStringToString(serializedText, kCommandSectionDelimiter);

        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    std::string SpriteCommandStack::serializeCommandWordsText() const
    {
        STRING out;
        serializeCommandWordsText(out);
        return out.str();
    }

    void SpriteCommandStack::parseCommandWordsText(STRING remainingText)
    {
        constructRightOfFirstMarker(remainingText, remainingText, kCommandWordPrefixMarker);

        const char* parserPointer = remainingText.c_str();
#if defined(_MSC_VER) && defined(_M_IX86)
        int parsedWord;
#else
        int parsedWord = 0;
#endif
        while (std::strcmp(parserPointer, kEmptyString) != 0)
        {
            std::sscanf(parserPointer, "%i", &parsedWord);
            const int parsedWordValue = static_cast<int>(core::retailReadStackDword(&parsedWord));

            appendCommandWord(static_cast<std::int16_t>(parsedWordValue));

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandWordPrefixMarker);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();

            parserPointer = remainingText.c_str();
        }

        remainingText.ReleaseOwnedStorage();
    }

    void SPRITE::serializeCommandWordsText(STRING& out) const
    {
        static const char kCommandWordFormat[] = { '\x01', '%', 'i', '\0' };
        static const char kCommandSectionDelimiter[] = { '\x02', '\0' };

        STRING serializedText;
        const std::uint32_t count = commandWordCount();
        const std::int32_t* const values = commandWordData();
        for (std::uint32_t i = 0; i < count; ++i)
        {
            STRING wordText;
            constructFormattedString(wordText, kCommandWordFormat, values[i]);
            appendStringOwner(serializedText, wordText);
            wordText.ReleaseOwnedStorage();
        }
        if (count != 0u)
            appendCStringToString(serializedText, kCommandSectionDelimiter);

        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    std::string SPRITE::serializeCommandWordsText() const
    {
        STRING out;
        serializeCommandWordsText(out);
        return out.str();
    }

    void SPRITE::parseCommandWordsText(STRING remainingText)
    {
        constructRightOfFirstMarker(remainingText, remainingText, kCommandWordPrefixMarker);
        const char* parserPointer = remainingText.c_str();
#if defined(_MSC_VER) && defined(_M_IX86)
        int parsedWord;
#else
        int parsedWord = 0;
#endif
        while (std::strcmp(parserPointer, kEmptyString) != 0)
        {
            std::sscanf(parserPointer, "%i", &parsedWord);
            appendCommandWordValue(static_cast<std::int32_t>(core::retailReadStackDword(&parsedWord)));

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandWordPrefixMarker);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();
            parserPointer = remainingText.c_str();
        }
        remainingText.ReleaseOwnedStorage();
    }

    namespace
    {
        std::unordered_map<const SPRITE*, SpriteHostState>& spriteHostStates()
        {
            static std::unordered_map<const SPRITE*, SpriteHostState> states;
            return states;
        }
    }

    SpriteHostState& SPRITE::hostState() noexcept
    {
        return spriteHostStates()[this];
    }

    MAP* SPRITE::mapOwner() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return MAP::Current();
#else
        return hostState().owner;
#endif
    }

    const SpriteHostState& SPRITE::hostState() const noexcept
    {
        return spriteHostStates()[this];
    }

    void SPRITE::releaseHostState() noexcept
    {
#if UINTPTR_MAX != 0xFFFFFFFFu
        spriteHostStates().erase(this);
#endif
    }

    SPRITE::SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir)
        : SPRITE(owner, vid, xyz, dir, nullptr)
    {
    }

    SPRITE::SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : m_vid(vid), m_xyz(xyz), m_direction(dir)
    {
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().owner = owner;
#else
        (void)owner;
#endif
        initializeBaseSprite(vid, xyz.x, xyz.y, xyz.z, dir.Int() & 0xFF, parent);
    }

    SPRITE* SPRITE::initializeBaseSprite(VID* vid, float x, float y, float z, int direction, SPRITE* parent) noexcept
    {
        m_vid = vid;
        m_childChain = nullptr;
        m_childBacklink = nullptr;
        m_parentSprite = parent;
        m_goalSprite = nullptr;
        m_bestTargetSprite = nullptr;
        m_actionTimer = 0;
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().actionAuxCommandMask = {0, 0};
#endif
        m_actionAuxState = nullptr;
        m_commandStack.releaseCommandRecordsRetailTail();

        int dirByte = direction & 0xFF;
        VECTOR next{x, y, z};

        if ((vid->properties() & P_NOISE) != 0)
        {
            next.x += static_cast<float>(8 - (std::rand() % 17));
            next.y += static_cast<float>(8 - (std::rand() % 17));
        }
        if ((vid->properties() & P_ZEROZ) != 0)
        {
            const float groundZ = mapOwner()->GetGroundZ(vid, VECTOR2{next.x, next.y}, ANGLE(dirByte));
            next.z = parent ? (z - parent->Z() + groundZ) : groundZ;
        }

        m_xyz = next;

        m_direction = ANGLE(0);

        int bucket = 0;
        if (parent)
            bucket = parent->armyIndex();
        else
        {
            const VID* bucketOwner = vid;
            if (vid->weaponCount() == 0)
            {
                if (VID* linkVid = vid->linkedVid())
                    bucketOwner = linkVid;
            }
            bucket = bucketOwner ? (bucketOwner->weaponDefaultArmy() & 3) : 0;
        }

        DWORD nextFlags = static_cast<DWORD>(bucket & 3) << ArmyBitsShift;
        if ((vid->properties() & P_INVISIBLEFORENEMY) != 0)
        {
            const std::uint32_t appBucket = core::ActivePlayerIndex();
            if (static_cast<std::uint32_t>(bucket & 3) != appBucket)
                nextFlags |= DrawSuppressedFlag;
        }
        m_runtimeFlags = nextFlags;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        int randomDelay = 0;
        if ((vid->properties() & P_ONEPHASE) == 0)
        {
            const int speedDefault = static_cast<int>(vid->defaultFrameSpeed());
            randomDelay = std::rand() % (speedDefault + 1);
        }
        m_applicationBucketTime = now - static_cast<std::uint32_t>(randomDelay);
        m_animationLastTick = now;
        m_listReferenceCount = 0;
        m_speed = 0.0f;
        m_zSpeed = 0.0f;
        m_currentFrame = 0;
        m_currentFrameBegin = 0;
        m_currentFrameEnd = 0;

        if (vid->actionAuxStateRequired() != 0)
        {
            m_actionAuxState = static_cast<ActionAuxState*>(
                ::operator new(sizeof(ActionAuxState), std::nothrow));
            if (m_actionAuxState)
                initializeActionAuxState(this);
        }

        initializeAnimationRouteFromVid();
        m_animationFrameTime = vid->animationFrameDuration(armyIndex());
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().runtimeInitializedFromVid = true;
#endif

        ChangeDirection(dirByte);

        if (m_currentAnimation == 0 && m_currentFrameEnd > m_currentFrame)
        {
            const int span = m_currentFrameEnd - m_currentFrameBegin;
            if (span > 0 && (!vid || (vid->properties() & P_ONEPHASE) == 0))
                m_currentFrame += std::rand() % (span + 1);
        }

        if (m_vid != MAP::NullVid())
        {
            ++m_listReferenceCount;
            addToDrawBucketsRecursive();
        }

        ensureLinkedVidChild();
        GlobalSpriteHashMap()->addSprite(this);

        m_vid->setLastSpriteCountChangeTimestamp(core::RealTimeMilliseconds());
        m_vid->incrementSpriteCountForArmy(armyIndex());

        if (m_currentAnimation != 14 && (core::ApplicationFlags() & application_flags::MapLoading) == 0)
        {
            const int child238Gate = m_vid->birthChildVid() ? 1 : 0;
            if (child238Gate)
            {
                const int savedAnimation = m_currentAnimation;
                m_currentAnimation = 14;
                spawnAnimationChild();
                m_currentAnimation = savedAnimation;
            }

            const int constructorSfx = m_vid->constructorSfxId();
            if (constructorSfx != 0)
            {
                playSfxAtWorldPosition(constructorSfx);
            }
        }

        if (m_vid->gridDotCount() > 0)
            m_vid->SetGridZ(this);

        return this;
    }

    int SPRITE::ammoFixedPoint() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::AmmoFixedPoint);
#else
        return hostState().ammoFixedPointValue;
#endif
    }

    int SPRITE::turnTimer() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::TurnTimer);
#else
        return hostState().turnTimer;
#endif
    }

    void SPRITE::setTurnTimer(int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::TurnTimer) = value;
#else
        hostState().turnTimer = value;
#endif
    }

    int SPRITE::behaviorFlags() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::BehaviorFlags);
#else
        return hostState().behaviorFlags;
#endif
    }

    void SPRITE::setBehaviorFlags(int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::BehaviorFlags) = value;
#else
        hostState().behaviorFlags = value;
#endif
    }

    void SPRITE::setCommandWordListVtable(std::uint32_t value) noexcept
    {
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().commandWordListVtable = value;
#else
        (void)value;
#endif
    }

    void SPRITE::setAmmoFixedPoint(int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::AmmoFixedPoint) = value;
#else
        hostState().ammoFixedPointValue = value;
#endif
    }

    SPRITE* SPRITE::initializeExtendedSpriteState(VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent) noexcept
    {
        initializeBaseSprite(vid, xyz.x, xyz.y, xyz.z, dir.Int() & 0xFF, parent);
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().extendedSpriteVtableSnapshot = 0u;
#endif
        setSharedPrimaryState(-1);
        setSharedSecondaryState(0);
        return this;
    }

    SPRITE* SPRITE::initializeCommandSpriteState(VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent) noexcept
    {
        initializeExtendedSpriteState(vid, xyz, dir, parent);


#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::ExtendedStateBase) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::LegacyCommandState1) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::LegacyCommandState2) = -1;
#else
        hostState().legacyCommandState0 = 0;
        hostState().legacyCommandState1 = 0;
        hostState().legacyCommandState2 = -1;
#endif
        setTurnTimer(0);

        VID* const baseVid = Vid();
        const VID* valueOwner = baseVid;
        if (m_childChain)
        {
            VID* const childVid = m_childChain->Vid();
            VID* const linkVid = baseVid->linkedVid();
            if (childVid == linkVid &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                valueOwner = linkVid;
            }
        }

        setBehaviorFlags(valueOwner->weaponDefaultBehavior());

        const VID* counterOwner = baseVid;
        if (VID* const linkVid = baseVid->linkedVid())
        {
            if (linkVid->hasWeaponChildDescriptor() != 0u &&
                linkVid->weaponCount() != 0u)
            {
                counterOwner = linkVid;
            }
        }
        setAmmoFixedPoint(counterOwner->weaponRecordAmmoCapacity() << 6);

        return this;
    }

    TERRAIN::TERRAIN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent),
          m_sharedPrimaryState(-1),
          m_sharedSecondaryState(0)
    {

        setSharedPrimaryState(m_sharedPrimaryState);
        setSharedSecondaryState(m_sharedSecondaryState);
    }

    int TERRAIN::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchExtendedSpriteActionOpcode(
            opcode, static_cast<int>(argument1Carrier), argument2Carrier, argument3Carrier);
    }

    LINKER::LINKER(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent)
    {
        const int directionByte = dir.Int() & 0xFF;
        if (parent)
        {
            parent->appendChildChain(this);
            setLinkerState(
                xyz.x - parent->X(),
                xyz.y - parent->Y(),
                xyz.z - parent->Z(),
                directionByte,
                parent);
            return;
        }

        setLinkerState(0.0f, 0.0f, 0.0f, directionByte, nullptr);
    }

    LINKER::~LINKER()
    {
        detachFromChildChain();
    }

    REGION::REGION(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent),
          m_fogRampPhase(0),
          m_lastFogRampPhase(0),
          m_fogRamp(nullptr),
          m_regionFlags(0),
          m_fogEnd(0),
          m_fogStart(0),
          m_fogColor(0xFF000000u),
          m_reservedRegionState8C(0),
          m_regionWidth(0.0f),
          m_regionHeight(0.0f),
          m_savedRegionVid(MAP::NullVid())
    {
        for (int i = 0; i < 6; ++i)
        {
            m_sourceVidMap[i] = nullptr;
            m_targetVidMap[i] = nullptr;
        }
    }

    REGION::~REGION()
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        if (m_fogRamp)
        {
            ::operator delete(m_fogRamp);
            m_fogRamp = nullptr;
        }
    }

    REGION* REGION::regionScalarDeletingDestructor(unsigned char flags) noexcept
    {

        REGION* const self = this;
        destroyRegionState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void REGION::destroyRegionState() noexcept
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        if (m_fogRamp)
        {
            ::operator delete(m_fogRamp);
            m_fogRamp = nullptr;
        }
        destroyBaseSpriteState();
    }

    VID* resolveRegionMappedVid(VID* sourceVid, float x, float y, float z) noexcept
    {
        const core::ApplicationDrawPassBucket& bucket =
            core::GlobalApplicationDrawDispatcherState().drawPassBucket(7);

        for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
        {
            SPRITE* const candidate = bucket.spriteAt(cursor);
            if (!candidate)
                continue;

            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() != B_REGION)
                continue;

            const double zGate = static_cast<double>(candidate->Z()) + 25.0;
            if (zGate <= static_cast<double>(z) || std::isnan(zGate) || std::isnan(z))
                continue;

            REGION* const region = static_cast<REGION*>(candidate);
            if ((region->regionFlags() & REGION::FullViewportFlag) == 0u)
            {
                const double cx = static_cast<double>(candidate->X());
                const double cy = static_cast<double>(candidate->Y());
                const double halfWidth = static_cast<double>(region->regionWidth()) * 0.5;
                const double halfHeight = static_cast<double>(region->regionHeight()) * 0.5;
                const double px = static_cast<double>(x);
                const double py = static_cast<double>(y);
                const bool insideX =
                    (cx - halfWidth <= px || std::isnan(cx - halfWidth) || std::isnan(px)) &&
                    (px <= cx + halfWidth || std::isnan(px) || std::isnan(cx + halfWidth));
                const bool insideY =
                    (cy - halfHeight <= py || std::isnan(cy - halfHeight) || std::isnan(py)) &&
                    (py <= cy + halfHeight || std::isnan(py) || std::isnan(cy + halfHeight));
                if (!insideX || !insideY)
                    continue;
            }

            for (int index = 0; index < 6; ++index)
            {
                if (region->sourceMappedVid(index) == sourceVid)
                    return region->targetMappedVid(index);
            }
        }
        return sourceVid;
    }

    void REGION::Draw()
    {
        drawRegionTilesAndFog();
    }

    double REGION::regionScreenLeft() const noexcept
    {
        return static_cast<double>(X()) - static_cast<double>(m_regionWidth) * 0.5 -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftX());
    }

    double REGION::regionScreenTop() const noexcept
    {
        return static_cast<double>(Y()) - static_cast<double>(Z()) -
               static_cast<double>(m_regionHeight) * 0.5 -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftY());
    }

    double REGION::regionScreenRight() const noexcept
    {
        return static_cast<double>(m_regionWidth) * 0.5 + static_cast<double>(X()) -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftX());
    }

    double REGION::regionScreenBottom() const noexcept
    {
        return static_cast<double>(Y()) - static_cast<double>(Z()) +
               static_cast<double>(m_regionHeight) * 0.5 -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftY());
    }

    void REGION::DrawDebugOverlay()
    {
        drawRegionDebugBounds();
    }

    void REGION::drawRegionDebugBounds()
    {
        GRAPH* const graph = GRAPH::CurrentGraph();
        const DWORD white = GammaRawCreateOpaque(255, 255, 255);
        graph->DrawRect(static_cast<float>(regionScreenLeft() - 1.0),
                        static_cast<float>(regionScreenTop() - 1.0),
                        static_cast<float>(regionScreenRight() + 1.0),
                        static_cast<float>(regionScreenBottom() + 1.0),
                        white);
    }

    void REGION::drawRegionTilesAndFog()
    {
        const int savedFrame = currentFrame();
        const float savedX = X();
        const float savedY = Y();
        VID* const regionVid = Vid();
        GRAPH* const graph = GRAPH::CurrentGraph();

        if (regionVid != MAP::NullVid())
        {
            if ((m_regionFlags & FullViewportFlag) == 0u)
            {
                graph->rawSetSoftwareClipBounds(
                    spriteFtolLow32(static_cast<long double>(regionScreenLeft())),
                    spriteFtolLow32(static_cast<long double>(regionScreenTop())),
                    spriteFtolLow32(static_cast<long double>(regionScreenRight())),
                    spriteFtolLow32(static_cast<long double>(regionScreenBottom())));
            }

            const float halfHeight = static_cast<float>(
                static_cast<long double>(m_regionHeight) * 0.5L);
            float tileY = static_cast<float>(static_cast<long double>(savedY) - halfHeight);
            const float tileYEnd = static_cast<float>(static_cast<long double>(savedY) + halfHeight);
            int tileIndex = 0;
            while (x87OrderedLess(tileY, tileYEnd))
            {
                const float halfWidth = static_cast<float>(
                    static_cast<long double>(m_regionWidth) * 0.5L);
                float tileX = static_cast<float>(static_cast<long double>(savedX) - halfWidth);
                const float tileXEnd = static_cast<float>(static_cast<long double>(savedX) + halfWidth);
                while (x87OrderedLess(tileX, tileXEnd))
                {
                    if ((regionVid->properties() & P_ONEPHASE) == 0u)
                    {
                        const int noCadr = static_cast<int>(regionVid->totalFrames());
                        setCurrentFrameDirect((savedFrame + 2 * tileIndex++) % noCadr);
                    }
                    setXPosition(tileX + static_cast<float>(static_cast<std::int16_t>(regionVid->vidWidth()) / 2));
                    setYPosition(tileY + static_cast<float>(static_cast<std::int16_t>(regionVid->vidHeight()) / 2));
                    regionVid->Draw(this);
                    tileX += static_cast<float>(static_cast<std::int16_t>(regionVid->vidWidth()));
                }
                tileY += static_cast<float>(static_cast<std::int16_t>(regionVid->vidHeight()));
            }

            if ((m_regionFlags & FullViewportFlag) == 0u)
            {
                const GraphViewportState& liveViewport = graph->viewportState();
                graph->rawSetSoftwareClipBounds(
                    spriteFtolLow32(static_cast<long double>(liveViewport.left)),
                    spriteFtolLow32(static_cast<long double>(liveViewport.top)),
                    spriteFtolLow32(static_cast<long double>(liveViewport.right)),
                    spriteFtolLow32(static_cast<long double>(liveViewport.bottom)));
            }
        }

        setXPosition(savedX);
        setYPosition(savedY);
        setCurrentFrameDirect(savedFrame);

        if (m_fogStart >= m_fogEnd)
        {
            m_fogRampPhase = 0;
            return;
        }

        if ((m_regionFlags & FogAnimatedFlag) != 0u)
        {
            const int phase = static_cast<int>(core::CurrentTimeMilliseconds() & 7u);
            if (static_cast<unsigned>(phase) < static_cast<unsigned>(m_lastFogRampPhase))
            {
                const int limit = 8 * m_fogEnd;
                if (m_fogRampPhase < limit)
                {
                    m_fogRampPhase += 2;
                    m_lastFogRampPhase = phase;
                }
                else if (m_fogRampPhase > limit)
                {
                    m_fogRampPhase = 0;
                    m_lastFogRampPhase = phase;
                }
            }
            m_lastFogRampPhase = phase;
        }
        else
        {
            m_fogRampPhase = 8 * m_fogEnd;
        }

        const DWORD color = m_fogColor;
        const WORD* const ramp = static_cast<const WORD*>(m_fogRamp);
        const int blend = static_cast<int>(m_regionFlags & FogBlendFlag);
        if ((m_regionFlags & FullViewportFlag) != 0u)
        {
            graph->drawFogBufferOverlay(static_cast<float>(graph->getViewportLeft()),
                              static_cast<float>(graph->getViewportTop()),
                              static_cast<float>(graph->getViewportRight()),
                              static_cast<float>(graph->getViewportBottom()),
                              m_fogStart, m_fogEnd, color, ramp,
                              m_fogRampPhase, blend);
        }
        else
        {
            graph->drawFogBufferOverlay(static_cast<float>(regionScreenLeft()),
                              static_cast<float>(regionScreenTop()),
                              static_cast<float>(regionScreenRight()),
                              static_cast<float>(regionScreenBottom()),
                              m_fogStart, m_fogEnd, color, ramp,
                              m_fogRampPhase, blend);
        }
    }

    int REGION::rebuildRegionFogRamp(int start, int end, int color)
    {
        m_fogEnd = end;
        m_fogStart = start;
        m_fogColor = static_cast<std::uint32_t>(color);
        if (m_fogRamp)
            ::operator delete(m_fogRamp);

        int eaxCarrier = start;
        if (start >= end)
            return eaxCarrier;

        const std::uint32_t difference =
            static_cast<std::uint32_t>(end) - static_cast<std::uint32_t>(start);
        const std::uint32_t allocationSize = difference * 16u + 2u;
        m_fogRamp = ::operator new(static_cast<std::size_t>(allocationSize), std::nothrow);
        if (!m_fogRamp)
        {
            fatalLogError(g_fileLogger, "Enough memory for DrawFog",
                       static_cast<int>(difference * 8u + 1u));
        }

        const std::int32_t count = static_cast<std::int32_t>(difference * 8u);
        if (count < 0)
            return eaxCarrier;

        WORD* const ramp = static_cast<WORD*>(m_fogRamp);
        std::int32_t index = count;
        std::int32_t numerator = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(count) * 255u);
        do
        {
            const std::int32_t signedDifference = static_cast<std::int32_t>(difference);
            std::int32_t quotient = numerator / signedDifference;

            if (quotient < 0)
                quotient += 7;
            const int intensity = quotient >> 3;
            eaxCarrier = intensity;
            if (GRAPH::CurrentGraph()->lightBuffer()->format() != 41u)
            {
                const WORD palette = GRAPH::CurrentGraph()->intensityPaletteEntry(
                    static_cast<std::size_t>(intensity));
                eaxCarrier = (eaxCarrier & ~0xFFFF) | static_cast<int>(palette);
            }
            ramp[index] = static_cast<WORD>(eaxCarrier);
            --index;
            numerator = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(numerator) - 255u);
        }
        while (index >= 0);
        return eaxCarrier;
    }

    int REGION::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        RESOURCE* const resource = reinterpret_cast<RESOURCE*>(static_cast<std::uintptr_t>(argument1Carrier));
        if (opcode == 80)
        {
            (void)SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
            resource->write(&m_regionFlags, 4u);
            resource->write(&m_fogEnd, 4u);
            resource->write(&m_fogStart, 4u);
            resource->write(&m_fogColor, 4u);
            resource->write(&m_regionWidth, 4u);
            resource->write(&m_regionHeight, 4u);
            resource->write(&m_persistedRegionState, 4u);
            int nvid = m_savedRegionVid ? m_savedRegionVid->nvid() : -1;
            resource->write(&nvid, 4u);
            for (int i = 0; i < 6; ++i)
            {
                nvid = m_sourceVidMap[i] ? m_sourceVidMap[i]->nvid() : -1;
                resource->write(&nvid, 4u);
                nvid = m_targetVidMap[i] ? m_targetVidMap[i]->nvid() : -1;
                resource->write(&nvid, 4u);
            }
            return 0;
        }
        if (opcode != SpriteActConst::ACT_RESTORE && opcode != SpriteActConst::ACT_RESTORE_OLD_MAP)
            return SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);

        const int routedVersion = argument2Carrier;
        (void)SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
        resource->read(&m_regionFlags, 4u);
        int slot80 = 0, slot84 = 0, color88 = 0;
        resource->read(&slot80, 4u);
        resource->read(&slot84, 4u);
        resource->read(&color88, 4u);
        rebuildRegionFogRamp(slot84, slot80, color88);
        if (routedVersion <= 9)
        {
            BYTE legacyByte = 0;
            resource->read(&legacyByte, 1u);
            m_regionWidth = static_cast<float>(color88);
            int legacyHeight = 0;
            resource->read(&legacyHeight, 4u);
            m_regionHeight = static_cast<float>(legacyHeight);
        }
        else
        {
            resource->read(&m_regionWidth, 4u);
            resource->read(&m_regionHeight, 4u);
        }
        resource->read(&m_persistedRegionState, 4u);
        core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        const auto resolveVid = [&vidTable](int nvid) -> VID*
        {
            if (nvid < 0 || nvid >= vidTable.count())
                return nullptr;
            return vidTable.slot(nvid);
        };
        int nvid = -1;
        resource->read(&nvid, 4u);
        m_savedRegionVid = resolveVid(nvid);
        if (!m_savedRegionVid)
            m_savedRegionVid = MAP::NullVid();
        for (int i = 0; i < 6; ++i)
        {
            resource->read(&nvid, 4u);
            m_sourceVidMap[i] = resolveVid(nvid);
            resource->read(&nvid, 4u);
            m_targetVidMap[i] = resolveVid(nvid);
            if (m_sourceVidMap[i])
            {
                m_sourceVidMap[i]->setRuntimeAuxFlags(
                    m_sourceVidMap[i]->runtimeAuxFlags() | 0x10u);
            }
        }
        return 0;
    }

    FRAME::FRAME(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner,
                 vid,
                 VECTOR{xyz.x + core::GlobalApplicationDrawDispatcherState().cameraShiftX(),
                        xyz.y + core::GlobalApplicationDrawDispatcherState().cameraShiftY(),
                        xyz.z},
                 dir,
                 parent)
    {
        captureCurrentImageFrameVtable(this);
        applicationFrameSpriteList().append(this);
    }

    FRAME::~FRAME()
    {
        // Language destructor path represents destroyFrameState's derived prefix;
        // SPRITE::~SPRITE supplies the final destroyBaseSpriteState exactly once.
        as1::core::GlobalApplicationFrameRuntimeState().clearCurrentFrameSpriteIfMatches(this);
        applicationFrameSpriteList().removeSortedError(this);
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
    }

    FRAME* FRAME::frameScalarDeletingDestructor(unsigned char flags) noexcept
    {
        FRAME* const self = this;
        destroyFrameState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void FRAME::destroyFrameState() noexcept
    {
        publishCurrentImageFrameVtable(this);
        as1::core::GlobalApplicationFrameRuntimeState().clearCurrentFrameSpriteIfMatches(this);
        applicationFrameSpriteList().removeSortedError(this);
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        destroyBaseSpriteState();
    }

    STEXT::STEXT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : FRAME(owner, vid, xyz, dir, parent)
    {
        initializeTextState(
            STRING::SharedEmptyText(),
            STRING::SharedEmptyText(),
            0,
            0);
    }

    STEXT::~STEXT()
    {
        releaseOwnedText(m_textClass);
        releaseOwnedText(m_text);
    }

    STEXT* STEXT::textScalarDeletingDestructor(unsigned char flags) noexcept
    {
        STEXT* const self = this;
        releaseOwnedText(m_textClass);
        releaseOwnedText(m_text);
        destroyFrameState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void STEXT::initializeTextState(char* text70, char* class74, int length78, int flags7C) noexcept
    {
        m_text = text70 ? text70 : STRING::SharedEmptyText();
        m_textClass = class74 ? class74 : STRING::SharedEmptyText();
        m_textLength = length78;
        m_textFlags = flags7C;
        m_textState84 = 1;
        m_textState88 = 0;
    }

    char* STEXT::cloneOwnedText(const char* text)
    {
        if (!text || *text == '\0')
            return STRING::SharedEmptyText();
        const std::size_t len = std::strlen(text);
        char* owner = static_cast<char*>(::operator new(len + 1));
        std::memcpy(owner, text, len + 1);
        return owner;
    }

    void STEXT::releaseOwnedText(char*& owner) noexcept
    {
        if (owner && owner != STRING::SharedEmptyText())
            ::operator delete(owner);
        owner = STRING::SharedEmptyText();
    }

    void STEXT::assignText(const char* text)
    {
        releaseOwnedText(m_text);
        m_text = cloneOwnedText(text);
        m_textLength = static_cast<int>(std::strlen(m_text));
    }

    void STEXT::assignTextClass(const char* text)
    {
        releaseOwnedText(m_textClass);
        m_textClass = cloneOwnedText(text);
    }

    int STEXT::calcTextProperty() noexcept
    {
        const char* const text = m_text ? m_text : kEmptyString;
        int length = 0;
        int lineStart = 0;
        m_textState84 = 1;
        m_textState88 = 0;

        while (text[length] != '\0')
        {
            if (text[length] == '\n')
            {
                const int columns = length - lineStart;
                if (columns > m_textState88)
                    m_textState88 = columns;
                ++m_textState84;
                lineStart = length + 1;
            }
            ++length;
        }

        m_textLength = length;
        if (m_textState88 == 0)
            m_textState88 = length;
        return length;
    }

    int FRAME::dispatchFrameActionOpcode(int opcode, std::intptr_t actionArgument1, std::intptr_t actionArgument2, std::intptr_t actionArgument3)
    {
        if (opcode >= 0 && opcode <= 5)
            return 0;

        if (opcode == 0x82)
        {
            const int animation = currentAnimation();
            if (animation >= 15)
                return 0;
            if (animation == 4)
            {
                ChangeAnimation(2);
                setRuntimeFlags(runtimeFlags() | 0x00000200u);
            }
            else if (animation == 5)
            {
                ChangeAnimation(3);
                setRuntimeFlags(runtimeFlags() | 0x00000200u);
            }
            if (currentAnimation() == 14)
                ChangeAnimation(0);
            return 0;
        }

        return dispatchActionOpcode(static_cast<std::uint32_t>(opcode),
                          static_cast<int>(actionArgument1),
                          static_cast<int>(actionArgument2),
                          static_cast<int>(actionArgument3));
    }

    int FRAME::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchFrameActionOpcode(opcode, argument1Carrier,
                          static_cast<std::intptr_t>(argument2Carrier),
                          static_cast<std::intptr_t>(argument3Carrier));
    }

    int STEXT::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchTextActionOpcode(opcode, argument1Carrier,
                          static_cast<std::intptr_t>(argument2Carrier),
                          static_cast<std::intptr_t>(argument3Carrier));
    }

    void STEXT::Draw()
    {
        VID* fontVid = Vid();
        if (fontVid && fontVid != MAP::NullVid() && fontVid->directionCount() > 0x7E)
        {
            const int savedFrame = currentFrame();
            const float savedX = X();
            const float savedY = Y();

            if ((m_textFlags & 0x70) == 0x60)
            {
                STRING expanded = expandTextScriptExpression(STRING(m_textClass));
                assignText(expanded.c_str());
                calcTextProperty();
            }

            if (std::strcmp(m_text, kEmptyString) == 0)
                return;

            const int flags = m_textFlags;
            const float glyphWidth = fontVid->sizeX();
            const int columns = m_textState88;
            const int rows = m_textState84;

            if ((flags & 1) != 0)
            {
                setXPosition(X() - static_cast<float>(columns - 1) * glyphWidth * 0.5f);
            }
            else if ((flags & 2) != 0)
            {
                setXPosition(X() - (static_cast<float>(columns - 1) * glyphWidth + fontVid->halfSizeX()));
            }
            else
            {
                setXPosition(X() + fontVid->halfSizeX());
            }

            if ((flags & 8) != 0)
            {
                setYPosition(Y() - static_cast<float>(rows - 1) * fontVid->sizeY() * 0.5f);
            }
            else if ((flags & 4) != 0)
            {
                setYPosition(Y() - (static_cast<float>(rows - 1) * fontVid->sizeY() + fontVid->halfSizeY()));
            }
            else
            {
                setYPosition(Y() + fontVid->halfSizeY());
            }

            float lineStartX = X() - fontVid->sizeX();
            VID* currentFont = fontVid;
            const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            auto resolveFont = [&](int nvid) -> VID*
            {
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        return resolved;
                }
                return MAP::NullVid();
            };

            int i = 0;
            while (i < m_textLength && m_text[i] != '\0')
            {
                const char* const text = m_text;
                const unsigned char ch = static_cast<unsigned char>(text[i]);

                if (ch == '\n')
                {
                    setXPosition(lineStartX);
                    setYPosition(Y() + currentFont->sizeY());
                }
                else if (ch == '\r')
                {
                    setXPosition(lineStartX);
                }
                else if (ch == '\t')
                {
                    setXPosition(X() + currentFont->sizeX() * 7.0f);
                }
                else if (ch == '<' && std::strncmp(text, "<Font=", std::strlen("<Font=")) == 0)
                {
                    STRING fontText;
                    constructRightOfFirstMarker(STRING(text), fontText, "<Font=");
                    const int fontNVid = script::ParseStackIntegerText(fontText.c_str());
                    currentFont = resolveFont(fontNVid);
                    setXPosition(X() - currentFont->sizeX());

                    STRING throughClose;
                    constructLeftOfFirstMarker(STRING(text), throughClose, ">");
                    i += throughClose.Length();
                }
                else if (ch == 0x1B && i + 1 < m_textLength)
                {
                    const int unsignedFontIndex = static_cast<unsigned char>(text[i + 1]);
                    if (unsignedFontIndex < vidTable.count() &&
                        vidTable.slot(unsignedFontIndex) != nullptr)
                    {
                        ++i;
                        const int signedFontNVid = static_cast<signed char>(text[i]);
                        currentFont = resolveFont(signedFontNVid);
                        setXPosition(X() - currentFont->sizeX());
                    }
                }
                else if (ch >= 0x20)
                {
                    setCurrentFrameDirect(static_cast<int>(ch));
                    currentFont->Draw(this);
                }

                ++i;
                setXPosition(X() + currentFont->sizeX());
            }

            setXPosition(savedX);
            setYPosition(savedY);
            setCurrentFrameDirect(savedFrame);
            return;
        }

        (void)GRAPH::CurrentGraph()->drawTextColored(X(), Y(), m_text, 0xFFFFFFFFu);
    }

    STRING STEXT::expandTextScriptExpression(const STRING& expression) const
    {
        return core::ApplicationScriptRuntime()->getVariableString(expression);
    }

    int STEXT::dispatchTextActionOpcode(int opcode, std::intptr_t actionArgument1, std::intptr_t actionArgument2, std::intptr_t actionArgument3)
    {
        switch (opcode)
        {
        case 0x50:
        {
            auto* stream = reinterpret_cast<BaseStream*>(actionArgument1);
            dispatchFrameActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
            stream->write_new(&m_textFlags, sizeof(m_textFlags));
            STRING(m_textClass).Write(stream);
            return 0;
        }

        case 0x51:
        {
            auto* stream = reinterpret_cast<BaseStream*>(actionArgument1);
            dispatchFrameActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
            stream->read_new(&m_textFlags, sizeof(m_textFlags));
            STRING tmpClass;
            readStringLineFromStream(tmpClass, stream);

            return dispatchTextActionOpcode(0x78, reinterpret_cast<std::intptr_t>(&tmpClass), 0, 0);
        }

        case 0x5E:
            return m_textFlags;

        case 0x5F:
            m_textFlags = static_cast<int>(actionArgument1);
            return 0;

        case 0x78:
        {
            const STRING* const requestedClassOwner =
                reinterpret_cast<const STRING*>(actionArgument1);
            const STRING requestedClassCopy(
                requestedClassOwner ? requestedClassOwner->c_str() : kEmptyString);
            const char* const requestedClass = requestedClassCopy.c_str();

            assignTextClass(requestedClass);

            const int mode = m_textFlags & 0x70;

            if (mode == 0x10)
            {
                STRING section("menu");
                STRING key(m_textClass);
                STRING defaultValue(m_textClass);
                STRING out;
                core::profile_p::readProfileStringInto(out, core::StartupStringsIniPath(), section, key, defaultValue);
                assignText(out.c_str());
                calcTextProperty();
                return 0;
            }

            if (mode == 0x20)
            {
                STRING loaded;
                loadStringFromFile(loaded, reinterpret_cast<const STRING*>(&m_textClass));
                assignText(loaded.c_str());
                calcTextProperty();
                return 0;
            }

            if (mode == 0)
            {
                assignText(m_textClass);
                calcTextProperty();
                return 0;
            }

            STRING expanded = expandTextScriptExpression(STRING(m_textClass));
            assignText(expanded.c_str());

            if (mode == 0x40)
            {
                STRING loaded;
                loadStringFromFile(loaded, reinterpret_cast<const STRING*>(&m_text));
                assignText(loaded.c_str());
                calcTextProperty();
                return 0;
            }

            if (mode == 0x50)
            {
                STRING section("menu");
                STRING key(m_text);
                STRING defaultValue(m_text);
                STRING out;
                core::profile_p::readProfileStringInto(out, core::StartupStringsIniPath(), section, key, defaultValue);
                assignText(out.c_str());
                calcTextProperty();
                return 0;
            }

            calcTextProperty();
            return 0;
        }

        case 0x7C:
            return static_cast<int>(reinterpret_cast<std::intptr_t>(&m_text));

        case 0x79:
            return static_cast<int>(reinterpret_cast<std::intptr_t>(&m_textClass));

        case 0x7A:
            m_textLength = static_cast<int>(actionArgument1);
            return 0;

        case 0x7B:
        {
            STRING tmp;
            loadStringFromFile(tmp, reinterpret_cast<const STRING*>(actionArgument1));
            assignText(tmp.c_str());
            return 0;
        }

        case 0x82:
        {
            const char* const text = m_text;
            if (std::strcmp(text, kEmptyString) == 0)
                return 0;
            if (actionTimer() != 0)
                return 0;

            const int textLength = static_cast<int>(static_cast<std::int16_t>(std::strlen(text)));
            int cursor = m_textLength;
            if (cursor < textLength)
            {
                m_textLength = cursor + 1;
                const int ch = static_cast<int>(static_cast<signed char>(text[cursor]));
                if (!std::isspace(ch))
                {
                    ChangeAnimation(1);
                    setRuntimeFlags(runtimeFlags() & ~0x00000200u);
                    return 0;
                }

                int sawNewLine = 0;
                while (m_textLength < textLength)
                {
                    cursor = m_textLength;
                    if (text[cursor] == '\n')
                        sawNewLine = 1;
                    m_textLength = cursor + 1;
                    const int nextCh = static_cast<int>(static_cast<signed char>(text[cursor]));
                    if (!std::isspace(nextCh))
                        break;
                }

                if (sawNewLine)
                {
                    if (m_textLength < textLength)
                        --m_textLength;
                    setActionTimer(0x96u);
                    ChangeAnimation(2);
                    return 0;
                }

                ChangeAnimation(1);
                setRuntimeFlags(runtimeFlags() & ~0x00000200u);
                return 0;
            }

            if (currentAnimation() != 0)
                ChangeAnimation(0);
            return 0;
        }

        default:
            return dispatchFrameActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
        }
    }

    void SPRITE::initializeAnimationRouteFromVid()
    {
        if (!m_vid)
        {
            m_currentAnimation = 0;
            return;
        }

        if (m_vid->noAnimCadr[0] == 0 && m_vid->noAnimCadr[15] != 0)
            m_currentAnimation = 15;
        else if (m_vid->noAnimCadr[14] != 0 && (core::ApplicationFlags() & application_flags::MapLoading) == 0u)
            m_currentAnimation = 14;
        else
            m_currentAnimation = 0;

        const int baseFrame = static_cast<int>(m_vid->animationBaseFrame[m_currentAnimation]);
        m_currentFrameBegin = baseFrame;
        m_currentFrame = baseFrame;
        m_currentFrameEnd = static_cast<int>(m_vid->animationFrameCount[m_currentAnimation]) - 1;
    }

    void SPRITE::attachChildSprite(SPRITE* child)
    {
        if (!child || child == this)
            return;

        if (insertChildChainHead(child) != 0)
            return;

    }

    int SPRITE::insertChildChainHead(SPRITE* child)
    {
        if (!child)
            return 1;
        if (child->childBacklink())
            return 1;

        SPRITE* oldHead = childChain();
        if (oldHead)
        {
            oldHead->setChildBacklink(nullptr);
            child->appendChildChain(oldHead);
        }

        setChildChain(child);
        child->setChildBacklink(this);
        return 0;
    }

    int SPRITE::appendChildChain(SPRITE* child)
    {
        if (!child)
            return 1;
        if (child->childBacklink())
            return 1;

        SPRITE* node = this;
        while (SPRITE* next = node->childChain())
            node = next;

        node->setChildChain(child);
        child->setChildBacklink(node);
        return 0;
    }

    void SPRITE::detachFromChildChain()
    {
        SPRITE* next = childChain();
        if (next)
            next->setChildBacklink(childBacklink());

        SPRITE* previous = childBacklink();
        if (previous)
        {
            previous->setChildChain(childChain());
            setChildBacklink(nullptr);
        }

        setChildChain(nullptr);
    }

    int SPRITE::deleteChildByVid(VID* childVid)
    {
        SPRITE* previous = this;
        SPRITE* child = childChain();

        while (child)
        {
            if (child->Vid() == childVid)
                break;
            previous = child;
            child = child->childChain();
        }

        if (!child)
            return 0;

        SPRITE* const next = child->childChain();
        previous->setChildChain(next);
        if (next)
            next->setChildBacklink(previous);

        child->setChildChain(nullptr);
        child->setChildBacklink(nullptr);

        DeleteSpriteThroughVirtualDeletingDestructor(child);
        return 1;
    }

    void SPRITE::syncActionAuxMaxSpeedFromVid() noexcept
    {
        if (!m_actionAuxState || !m_vid)
            return;
        const float maxSpeed = m_vid->maxSpeedValue();
        std::memcpy(&m_actionAuxState->maxSpeedBits, &maxSpeed, sizeof(maxSpeed));
    }

    void SPRITE::setActionAuxMaxSpeedDirect(float value) noexcept
    {
        if (!m_actionAuxState)
            return;
        std::memcpy(&m_actionAuxState->maxSpeedBits, &value, sizeof(value));
    }

    float SPRITE::runtimeMaxSpeedValue() const noexcept
    {
        if (m_actionAuxState)
            return spriteFloatFromBits(m_actionAuxState->maxSpeedBits);
        return m_vid->maxSpeedValue();
    }

    void SPRITE::initializeActionAuxState(SPRITE* ownerSprite) noexcept
    {
        SPRITE* const source = ownerSprite;

        m_actionAuxState->sourceX = source->m_xyz.x;
        m_actionAuxState->sourceY = source->m_xyz.y;
        m_actionAuxState->sourceZ = source->m_xyz.z;

        m_actionAuxState->lifetimeRemaining = static_cast<std::uint32_t>(source->m_vid->lifetimeValue());
        m_actionAuxState->childCadence = 1u;
        m_actionAuxState->effectTimestamp = core::CurrentTimeMilliseconds();
        m_actionAuxState->effectCurvePosition = 0u;
        const float maxSpeed = source->m_vid->maxSpeedValue();
        std::memcpy(&m_actionAuxState->maxSpeedBits, &maxSpeed, sizeof(maxSpeed));
        m_actionAuxState->commandMask0 = 0u;
        m_actionAuxState->commandMask1 = 0u;
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().actionAuxCommandMask = {0, 0};
#endif
        m_actionAuxState->items.vtableTag = currentCommandWordListVtable();
        m_actionAuxState->items.count = 0u;
        m_actionAuxState->items.capacity = 0u;
        m_actionAuxState->items.values = nullptr;
    }

    bool SPRITE::ensureActionAuxStateForLocalAction() noexcept
    {
        if (m_actionAuxState)
            return true;

        void* storage = ::operator new(sizeof(ActionAuxState), std::nothrow);
        if (!storage)
            return false;

        m_actionAuxState = static_cast<ActionAuxState*>(storage);
        initializeActionAuxState(this);
        return m_actionAuxState != nullptr;
    }

    void SPRITE::ensureActionAuxItemCapacity(std::uint32_t requiredCapacity) noexcept
    {
        if (!ensureActionAuxStateForLocalAction())
            return;

        ActionAuxState::ItemList& list = m_actionAuxState->items;
        if (static_cast<std::int32_t>(requiredCapacity) <= static_cast<std::int32_t>(list.capacity))
            return;

        std::int32_t* const oldArray = list.values;
        const std::uint32_t oldCapacity = list.capacity;
        std::int32_t* const newArray = static_cast<std::int32_t*>(
            ::operator new(static_cast<std::size_t>(requiredCapacity) * sizeof(std::int32_t), std::nothrow));
        list.values = newArray;
        if (!newArray)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(requiredCapacity));

        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
        {
            for (std::uint32_t i = 0; i < oldCapacity; ++i)
                newArray[i] = oldArray[i];
            ::operator delete(oldArray);
        }
        list.capacity = requiredCapacity;
    }

    void SPRITE::dispatchSpriteCommandMask(const std::uint32_t* commandMask) noexcept
    {
        if (!m_actionAuxState)
        {
            void* storage = ::operator new(sizeof(ActionAuxState), std::nothrow);
            if (storage)
            {
                m_actionAuxState = static_cast<ActionAuxState*>(storage);
                initializeActionAuxState(this);
            }
        }

        m_actionAuxState->commandMask0 = commandMask[0];
        m_actionAuxState->commandMask1 = commandMask[1];
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().actionAuxCommandMask[0] = commandMask[0];
        hostState().actionAuxCommandMask[1] = commandMask[1];
#endif
    }

    int SPRITE::dispatchVirtualAction(std::uint32_t opcode, int argument1, int argument2, int argument3) noexcept
    {
        return Action(static_cast<int>(opcode), static_cast<std::intptr_t>(argument1), argument2, argument3);
    }

    void SPRITE::setGoalSprite(SPRITE* goal) noexcept
    {
        SPRITE* const oldOwner = m_goalSprite;
        if (oldOwner == goal)
            return;

        if (oldOwner)
            (void)oldOwner->ReleaseListReference();

        m_goalSprite = goal;
        if (goal)
            goal->setListReferenceCount(goal->listReferenceCount() + 1);
    }

    void SPRITE::advanceAnimationFrameTimeCapped(int delta) noexcept
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & 0xFFFFFC00u) <= core::PreviousWorldTimeMilliseconds())
            return;

        const int currentFrame = animationFrameTime();
        if (currentFrame <= 0)
            return;

        VID* const vid = Vid();
        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);
        const std::uint32_t nextBits =
            static_cast<std::uint32_t>(currentFrame) + static_cast<std::uint32_t>(delta);
        int nextFrame = static_cast<std::int32_t>(nextBits);
        if (nextFrame > duration)
            nextFrame = duration;
        updateAnimationFrameTime(nextFrame);
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    int SPRITE::SetCommand(int argument1, SPRITE* goal) noexcept
    {
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u && argument1 != 0x12)
            m_actionTimer = 0;

        setGoalSprite(goal);

        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->Vid();
            if (childVid == m_vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                child->SetCommand(argument1, goal);
            }
        }

        const DWORD preserved = m_runtimeFlags & ~CommandBitsMask;
        if (argument1 < 0x10 && m_goalSprite == nullptr)
        {
            m_runtimeFlags = preserved;
            return 1;
        }

        const DWORD commandBits = (static_cast<DWORD>(argument1) & CommandValueMask) << CommandBitsShift;
        m_runtimeFlags = preserved | commandBits;
        return 0;
    }

    int SPRITE::SetCommandWithoutLink(int argument1, SPRITE* goal) noexcept
    {
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u && argument1 != 0x12)
            m_actionTimer = 0u;

        setGoalSprite(goal);

        const DWORD preserved = m_runtimeFlags & ~CommandBitsMask;
        if (argument1 < 0x10 && m_goalSprite == nullptr)
        {
            m_runtimeFlags = preserved;
            return 1;
        }

        m_runtimeFlags = preserved | ((static_cast<DWORD>(argument1) & CommandValueMask) << CommandBitsShift);
        return 0;
    }

    int SPRITE::Move(SPRITE* goal) noexcept
    {
        int result = SetCommandWithoutLink(1, goal);
        if (result == 0)
        {
            result = StartMove();
            if (result == 0)
                return SetCommand(0, nullptr);
        }

        SPRITE* const child = m_childChain;
        if (!child)
            return result;

        VID* const childVid = child->Vid();
        result = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(childVid)));
        if (childVid == m_vid->linkedVid())
        {
            if (childVid->hasWeaponChildDescriptor() != 0u)
            {
                if (childVid->weaponCount() != 0u)
                {
                    result = static_cast<int>(child->runtimeFlags() & SPRITE::CommandBitsMask);
                    if (result != 0 && result != 0x10)
                        result = child->SetCommand(0, nullptr);
                }
            }
        }
        return result;
    }

    int SPRITE::ammoCount() const noexcept
    {
        int value = ammoFixedPoint();
        const int signBits = value < 0 ? -1 : 0;
        value += (signBits & 0x3F);
        return value >> 6;
    }

    int SPRITE::addAmmoUnits(int value) noexcept
    {
        const int weaponValue = m_vid->activeWeaponAmmoCapacity();
        if (weaponValue == 999999)
        {
            setAmmoFixedPoint(63999936);
            return weaponValue;
        }

        const int delta = value * 64;
        setAmmoFixedPoint(ammoFixedPoint() + delta);
        const int returned = ammoFixedPoint();
        if (ammoFixedPoint() < 0)
            setAmmoFixedPoint(0);
        if (forceAmmoCapacityAfterAdd())
            setAmmoFixedPoint(m_vid->activeWeaponAmmoCapacity() << 6);
        return returned;
    }

    int SPRITE::refillAmmoByCapacityFraction(int divisor) noexcept
    {
        VID* const vid = Vid();
        VID* metricVid = vid;
        if (VID* const link = vid->linkedVid())
        {
            if (link->hasWeaponChildDescriptor() != 0u && link->weaponCount() != 0u)
                metricVid = link;
        }
        const int units = metricVid->weaponRecordAmmoCapacity();

        const int maxFixed = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(units) << 6u);
        if (maxFixed == 0 || maxFixed <= ammoFixedPoint())
            return 0;

        int next = maxFixed;
        if (divisor != 0)
        {
            next = spriteAdd32Wrap(ammoFixedPoint(), maxFixed / divisor);
            if (next > maxFixed)
                next = maxFixed;
        }
        setAmmoFixedPoint(next);
        return 1;
    }

    int SPRITE::ammoMissingPercent() const noexcept
    {
        VID* const vid = Vid();
        VID* metricVid = vid;
        if (VID* const link = vid->linkedVid())
        {
            if (link->hasWeaponChildDescriptor() != 0u && link->weaponCount() != 0u)
                metricVid = link;
        }
        const int capacity = metricVid->weaponRecordAmmoCapacity();
        if (capacity == 0)
            return 0;
        const int scaled = spriteImul32Low(ammoCount(), 100);
        return spriteSub32Wrap(100, scaled / capacity);
    }

    int SPRITE::animationRemainingPercent() const noexcept
    {
        VID* const vid = Vid();
        const int bucket = armyIndex();
        const int frame = animationFrameTime();
        const int ownDuration = vid->animationFrameDuration(bucket);
        VID* const link = vid->linkedVid();
        SPRITE* const child = childChain();
        const bool linkedChild = link && child && child->Vid() == link;

        if (frame < ownDuration)
        {
            int total = ownDuration;
            if (link && !linkedChild)
                total = spriteAdd32Wrap(total, link->animationFrameDuration(bucket));
            const int scaledFrame = spriteImul32Low(frame, 100);
            return spriteSub32Wrap(100, scaledFrame / total);
        }

        if (link && !linkedChild)
            return vid->nvid() != 35 ? 50 : 0;

        if (linkedChild && child->Vid()->spriteClassId() != 9u)
        {
            const int childFrame = child->animationFrameTime();
            const int childBucket = child->armyIndex();
            const int childDuration = child->Vid()->animationFrameDuration(childBucket);
            if (childFrame < childDuration)
            {
                const int total = spriteAdd32Wrap(ownDuration, childDuration);
                const int combinedFrame = spriteAdd32Wrap(childFrame, frame);
                const int scaledFrame = spriteImul32Low(combinedFrame, 100);
                return spriteSub32Wrap(100, scaledFrame / total);
            }
        }
        return 0;
    }

    int SPRITE::appendCommandWordValue(std::int32_t word) noexcept
    {
        if (!ensureActionAuxStateForLocalAction())
            return 0;

        ActionAuxState::ItemList& list = m_actionAuxState->items;
        if (static_cast<std::int32_t>(list.count) >= static_cast<std::int32_t>(list.capacity))
            ensureActionAuxItemCapacity(list.capacity * 2u + 4u);
        if (!m_actionAuxState || !m_actionAuxState->items.values)
            return 0;
        m_actionAuxState->items.values[m_actionAuxState->items.count++] = word;
        return 0;
    }

    void SPRITE::growCommandWordStorage() noexcept
    {
        if (!ensureActionAuxStateForLocalAction())
            return;
        ActionAuxState::ItemList& list = m_actionAuxState->items;
        if (static_cast<std::int32_t>(list.count) < static_cast<std::int32_t>(list.capacity))
            return;
        ensureActionAuxItemCapacity(list.capacity * 2u + 4u);
    }

    int SPRITE::commandWordAt(int index) const noexcept
    {
        if (index < 0 || !m_actionAuxState)
            return 0;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        if (!m_actionAuxState->items.values || rawIndex >= m_actionAuxState->items.count)
            return 0;
        return m_actionAuxState->items.values[rawIndex];
    }

    int SPRITE::findLastCommandWord(std::int32_t word) const noexcept
    {
        if (!m_actionAuxState || m_actionAuxState->items.count == 0u || !m_actionAuxState->items.values)
            return -1;

        std::uint32_t index = m_actionAuxState->items.count;
        const std::int32_t* cursor = m_actionAuxState->items.values + index;
        while (index != 0u)
        {
            --cursor;
            --index;
            if (*cursor == word)
                return static_cast<int>(index);
        }
        return -1;
    }

    int SPRITE::removeCommandWordValue(std::int32_t word) noexcept
    {
        if (!m_actionAuxState)
            return 0;
        const int index = findLastCommandWord(word);
        if (index < 0)
            return 0;

        ActionAuxState::ItemList& list = m_actionAuxState->items;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        --list.count;
        list.values[rawIndex] = list.values[list.count];
        return 1;
    }

    int SPRITE::hasCommandOpcode(std::uint32_t opcode) const noexcept
    {
        const auto& owner = m_commandStack.m_commandRecords;
        for (std::uint32_t i = 0; i < owner.count; ++i)
        {
            if (owner.records[i].words[0] == opcode)
                return 1;
        }
        return 0;
    }

    void SPRITE::appendCommandRecord(const SpriteCommandRecord& command)
    {

        m_commandStack.appendCommandRecord(command);
    }

    void SPRITE::prependCommandRecord(const SpriteCommandRecord& command)
    {
        m_commandStack.prependCommandRecord(command);
    }

    std::uint32_t SPRITE::lastCommandOpcode() const noexcept
    {
        const auto& owner = m_commandStack.m_commandRecords;
        if (owner.count == 0u)
            return 0u;
        return owner.records[owner.count - 1u].words[0];
    }

    int SPRITE::clearCommandWordList() noexcept
    {
        if (!m_actionAuxState)
            return 0;
        ActionAuxState::ItemList& list = m_actionAuxState->items;
        std::int32_t* const old = list.values;
        list.capacity = 0u;
        list.count = 0u;
        if (old)
            ::operator delete(old);
        list.values = nullptr;
        return 0;
    }

    int SPRITE::dispatchBaseActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        const int op = opcode;
        switch (op)
        {
        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {
            const int weaponValue = m_vid->activeWeaponAmmoCapacity();
            if (weaponValue == 999999)
            {
                setAmmoFixedPoint(63999936);
                return weaponValue;
            }

            const std::uint32_t sum = static_cast<std::uint32_t>(ammoFixedPoint()) +
                (static_cast<std::uint32_t>(argument1) << 6);
            setAmmoFixedPoint(static_cast<std::int32_t>(sum));
            const int result = ammoFixedPoint();
            if (ammoFixedPoint() < 0)
                setAmmoFixedPoint(0);
            if (forceAmmoCapacityAfterAdd())
                setAmmoFixedPoint(static_cast<std::int32_t>(static_cast<std::uint32_t>(m_vid->activeWeaponAmmoCapacity()) << 6));
            return result;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
            return ammoCount();

        case static_cast<int>(ActionCode::ACT_SET_BEHAVE):
            setBehaviorFlags(argument1);
            if (m_childChain)
                m_childChain->dispatchVirtualAction(static_cast<std::uint32_t>(op), argument1, argument2, argument3);
            return 0;

        case static_cast<int>(ActionCode::ACT_GET_BEHAVE):
            return behaviorFlags();

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
                return 0;

            if (spriteFcompC3(m_speed, 0.0f))
                ChangeAnimation(0);
            else if (m_currentAnimation != 2)
                ChangeAnimation(2);

            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x0Cu)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(3, m_goalSprite);
                    }
                }
            }

            if (commandBits == 0x10u)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(4, m_goalSprite);
                    }
                }
            }

            const auto hasActiveLinkedWeaponChild = [this]() noexcept -> bool
            {
                SPRITE* const child = m_childChain;
                if (!child)
                    return false;
                VID* const childVid = child->m_vid;
                return childVid == m_vid->linkedVid() &&
                       childVid->hasWeaponChildDescriptor() != 0u &&
                       childVid->weaponCount() != 0u;
            };

            int decision = 0;
            if (!hasActiveLinkedWeaponChild() && turnTimer() != 0)
            {
                decision = 2;
            }
            else
            {
                std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                std::uint32_t decisionDelta = static_cast<std::uint32_t>(
                    m_vid->frameSpeedForAnimation(m_currentAnimation));
                if (delta > decisionDelta)
                    decisionDelta = delta;

                decision = computeAttackDecisionCode(decisionDelta);
                setAttackDecisionCode(decision);
            }

            if (decision == 7)
            {
                SetCommand(0, nullptr);
            }
            else if (decision == 1)
            {
                const float runtimeMaxSpeed = m_actionAuxState
                    ? spriteFloatFromBits(m_actionAuxState->maxSpeedBits)
                    : m_vid->maxSpeedValue();
                if (spriteFcompC3(runtimeMaxSpeed, 0.0f))
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            child->m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                        else if (m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                    }
                    else if (m_goalSprite != nullptr)
                    {
                        SetCommand(0, nullptr);
                    }
                }
                else if (spriteFcompC3(m_speed, 0.0f))
                {
                    StartMove();
                }

                const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                if (postBits == 0x0Cu || postBits == 0x10u)
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            (behaviorFlags() & 1) != 0)
                        {
                            if (child->m_actionTimer != 0u || (std::rand() % 4) == 0)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }
            else if (decision == 0)
            {
                // State 0 stops a moving sprite except for an active linked
                // weapon child whose target differs from the parent's target.
                if (!spriteFcompC3(m_speed, 0.0f))
                {
                    bool stopMoving = true;
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            m_goalSprite != child->m_goalSprite)
                        {
                            stopMoving = false;
                        }
                    }

                    if (stopMoving)
                        Stop();
                }
            }
            else if (decision == 3)
            {
                SetCommand(0, nullptr);
            }
            else if (decision == 2 &&
                     (behaviorFlags() & 2) != 0 &&
                     spriteFcompC3(m_speed, 0.0f))
            {
                StartMove();
            }

            if (decision == 6)
            {
                const int behavior = behaviorFlags();
                if ((behavior & 1) != 0)
                {
                    if ((behavior & 2) != 0)
                    {
                        if (SPRITE* const target = SeekEnemy())
                            SetCommand(4, target);
                    }
                    else
                    {
                        SPRITE* const child = m_childChain;
                        if (child)
                        {
                            VID* const childVid = child->m_vid;
                            if (childVid == m_vid->linkedVid() &&
                                childVid->hasWeaponChildDescriptor() != 0u &&
                                childVid->weaponCount() != 0u)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }

            if (decision == 2 || decision == 5)
            {
                if ((behaviorFlags() & 1) != 0)
                {
                    const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                    if (postBits == 0u || postBits == 4u || postBits == 0x10u)
                    {
                        bool acquire = m_currentFrameEnd > m_currentFrame;
                        if (!acquire)
                        {
                            SPRITE* const child = m_childChain;
                            std::uint32_t timer = m_actionTimer;
                            if (child)
                            {
                                VID* const childVid = child->m_vid;
                                if (childVid == m_vid->linkedVid() &&
                                    childVid->hasWeaponChildDescriptor() != 0u &&
                                    childVid->weaponCount() != 0u)
                                {
                                    timer = child->m_actionTimer;
                                }
                            }
                            acquire = timer != 0u || (std::rand() % 11) == 0;
                        }

                        if (acquire)
                        {
                            if (SPRITE* const target = SeekEnemy())
                                SetCommand(4, target);
                        }
                    }
                }
            }

            if ((m_runtimeFlags & SPRITE::CommandBitsMask) != 0u ||
                m_goalSprite != nullptr ||
                m_actionTimer != 0u)
            {
                return 0;
            }

            Stop();
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {
            const int result = dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            const float runtimeMaxSpeed = m_actionAuxState
                ? spriteFloatFromBits(m_actionAuxState->maxSpeedBits)
                : m_vid->maxSpeedValue();
            if (argument1 >= 0 && (m_runtimeFlags & SPRITE::CommandBitsMask) == 0u &&
                !spriteFcompC3(runtimeMaxSpeed, 0.0f))
            {
                const int direction = std::rand() % 256;
                const float x = m_xyz.x + rawDirectionSin(direction) * 64.0f;
                const float y = m_xyz.y - rawDirectionCos(direction) * 64.0f;
                SPRITE* const helper = new (std::nothrow) SPRITE(mapOwner(), MAP::NullVid(), VECTOR(x, y, m_xyz.z), ANGLE(0), nullptr);
                Move(helper);
            }
            return result;
        }

        case static_cast<int>(ActionCode::ACT_REPAIR):
            setAmmoFixedPoint(static_cast<std::int32_t>(static_cast<std::uint32_t>(m_vid->activeWeaponAmmoCapacity()) << 6));
            return dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);

        case static_cast<int>(ActionCode::ACT_SAVE):
        {
            dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int rawBehavior8C = behaviorFlags();
            stream->write(&rawBehavior8C, 4u);

            return 0;
        }

        case static_cast<int>(ActionCode::ACT_RESTORE):
        {
            dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int rawBehavior8C = 0;
            stream->read(&rawBehavior8C, 4u);
            setBehaviorFlags(rawBehavior8C);

            const int mapVersion = argument2;
            if (mapVersion == 11)
            {
                std::uint32_t count = 0u;
                stream->read(&count, 4u);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::int32_t value = 0;
                    stream->read(&value, 4u);
                    appendCommandWordValue(value);
                }
            }
            else if (mapVersion < 11)
            {
                std::uint32_t count = 0u;
                stream->read(&count, 4u);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::int16_t value = 0;
                    stream->read(&value, 2u);
                    appendCommandWordValue(static_cast<std::int32_t>(value));
                }
            }
            return 0;
        }

        case SpriteActConst::ACT_RESTORE_OLD_MAP:
        {
            dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int mapVersion = argument2;
            if (mapVersion < 7)
            {
                std::uint8_t bucket = 0;
                stream->read(&bucket, 1u);
                changeArmyBucket(static_cast<int>(bucket));
            }

            std::uint8_t rawBehavior8C = 0;
            stream->read(&rawBehavior8C, 1u);
            setBehaviorFlags(static_cast<int>(rawBehavior8C));
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_SET_ARMY):
        {
            const int oldBucket = armyIndex();
            changeArmyBucket(static_cast<std::int8_t>(argument1));
            const int newBucket = armyIndex();
            if (oldBucket != newBucket && m_vid->nvid() == 104)
            {
                const int selfArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(15u), selfArg, 0);
            }
            return 0;
        }

        default:
            return dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
        }
    }

    int SPRITE::extendedStateValue(int index) const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        constexpr std::size_t Class7AmmoNvidBiasBase = 0x6Cu;
        return *reinterpret_cast<const int*>(
            reinterpret_cast<const unsigned char*>(this) + Class7AmmoNvidBiasBase +
            static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride);
#else
        return hostState().extendedStateValue[static_cast<std::size_t>(index)];
#endif
    }

    int SPRITE::setExtendedStateValue(int index, int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        constexpr std::size_t Class7AmmoNvidBiasBase = 0x6Cu;
        *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(this) + Class7AmmoNvidBiasBase +
            static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride) = value;
#else
        hostState().extendedStateValue[static_cast<std::size_t>(index)] = value;
#endif
        return value;
    }

    int SPRITE::derivedStateValue(int index) const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride);
#else

        return hostState().extendedStateValue[static_cast<std::size_t>(index + 10)];
#endif
    }

    int SPRITE::setDerivedStateValue(int index, int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride) = value;
#else
        hostState().extendedStateValue[static_cast<std::size_t>(index + 10)] = value;
#endif
        return value;
    }

    namespace
    {
        float addIntegerToFloatRounded(float base, int addend) noexcept
        {

            return static_cast<float>(static_cast<long double>(base) +
                                      static_cast<long double>(addend));
        }
    }

    int SPRITE::dispatchExtendedSpriteActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        if (opcode == static_cast<int>(ActionCode::ACT_REPAIR))
        {
            repairLinkedChildState(1);
            return 0;
        }

        if (opcode == SpriteActConst::ACT_RESTORE_OLD_MAP)
        {
            dispatchActionOpcode(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
            int localArgC = argument3;
            RESOURCE* const resource = reinterpret_cast<RESOURCE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            readResourceBytes(*resource, &localArgC,
                              static_cast<std::size_t>((argument2 > 7) + 1));
            return 0;
        }

        return dispatchActionOpcode(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
    }

    int SPRITE::repairLinkedChildState(int createMissingLinker) noexcept
    {
        VID* const vid = m_vid;

        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);
        updateAnimationFrameTime(duration);

        VID* const linkerVid = vid->linkedVid();
        SPRITE* const existingChild = childChain();
        const bool hasLinkerChild = existingChild && existingChild->Vid() == linkerVid;

        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (!hasLinkerChild && createMissingLinker != 0)
        {
            bool allowCreate = true;
            if (vid->nvid() == 35)
            {
                const int rawCount = table.count();
                VID* const gateA = (rawCount > 0x28) ? table.slot(0x28) : nullptr;
                VID* const gateB = (rawCount > 0x23) ? table.slot(0x23) : nullptr;
                VID* const leftVid = gateA ? gateA : MAP::NullVid();
                VID* const rightVid = gateB ? gateB : MAP::NullVid();
                const int leftCounter = leftVid->spriteCountForArmy(bucket);
                const int rightCounter = rightVid->spriteCountForArmy(bucket);
                allowCreate = leftCounter < rightCounter;
            }

            if (allowCreate)
            {
                ensureLinkedVidChild();
                const int rawCount = table.count();
                VID* const createVid = (rawCount > 0x24E) ? table.slot(0x24E) : nullptr;
                VID* const resolvedCreateVid = createVid ? createVid : MAP::NullVid();
                static_cast<void>(mapOwner()->CreateSpriteViaFactory(resolvedCreateVid, m_xyz, ANGLE(0), this, false));
                return 1;
            }
        }

        if (SPRITE* const child = childChain())
        {
            if (child->Vid() == linkerVid)
                (void)child->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
        }
        return 1;
    }

    int SPRITE::dispatchPrivateClass7ActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        switch (opcode)
        {
        case static_cast<int>(ActionCode::ACT_COOR_ATTACK):
        {
            SPRITE* const child = childChain();
            if (!child)
                return 0;

            VID* const ownVid = Vid();
            VID* const childVid = child->Vid();
            VID* const linkVid = ownVid->linkedVid();
            if (childVid != linkVid ||
                childVid->hasWeaponChildDescriptor() == 0u ||
                childVid->weaponCount() == 0u ||
                child->actionTimer() > 5000u ||
                (child->currentAnimation() == 8 && child->currentFrame() <= child->currentFrameEnd()))
            {
                return 0;
            }

            const float worldX = spriteFildToF32(argument1);
            const float worldY = spriteFildToF32(argument2);
            const auto& drawState = core::GlobalApplicationDrawDispatcherState();
            GRAPH* const graph = GRAPH::CurrentGraph();
            const float screenX = spriteFildSubF32(argument1, drawState.cameraShiftX());
            const float screenY = spriteFildSubF32(argument2, drawState.cameraShiftY());

            float height = 0.0f;
            int constructorYRaw = argument2;
            if (screenX >= static_cast<float>(graph->getViewportLeft()) &&
                screenX < static_cast<float>(graph->getViewportRight()) &&
                screenY >= static_cast<float>(graph->getViewportTop()) &&
                screenY < static_cast<float>(graph->getViewportBottom()))
            {
                const int pixelX = spriteFtolLow32(static_cast<long double>(screenX));
                const int pixelY = spriteFtolLow32(static_cast<long double>(screenY));
                const std::uint16_t* const depth = graph->softwareDepthBuffer();
                const int pitch = graph->softwareDepthPitch();
                const int pixelIndex = spriteAdd32Wrap(pixelX, spriteImul32Low(pitch, pixelY));
                height = static_cast<float>(depth[pixelIndex] >> 3) - 128.0f;
                if (height > 70.0f)
                    height = 50.0f;
            }
            else
            {
                const int weaponMode =
                    childVid->hasWeaponChildDescriptor() != 0u && childVid->weaponCount() != 0u
                        ? childVid->weaponTypeMask()
                        : ownVid->weaponTypeMask();
                if (weaponMode == 8)
                {
                    const float probeY = spriteFildAddF32(argument2, 80.0f);
                    height = mapOwner()->GetGroundZ(VECTOR2{worldX, probeY}) + 80.0f;
                }
                else
                {
                    constructorYRaw = spriteSub32Wrap(argument2, 19);
                    height = mapOwner()->GetGroundZ(VECTOR2{worldX, worldY}) + 19.0f;
                }
            }

            SPRITE* const marker = new (std::nothrow) SPRITE(
                mapOwner(),
                MAP::NullVid(),
                VECTOR{worldX, spriteFildAddF32(constructorYRaw, height), height},
                ANGLE(0),
                nullptr);
            child->SetCommand(4, marker);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_PATH_BLOCK):
        {
            const float candidateZ = spriteFildAddF32(argument3, Z());
            const float candidateX = spriteFildAddF32(argument1, X());
            if (CanPlaceWithCrush(candidateX, Y(), candidateZ) == nullptr)
            {
                ChangeCoor(candidateX, Y(), candidateZ);
                return 0;
            }

            const float candidateY = spriteFildAddF32(argument2, Y());
            if (CanPlaceWithCrush(X(), candidateY, candidateZ) != nullptr)
            {
                setSpeedDirect(0.0f);
                return 0;
            }
            ChangeCoor(X(), candidateY, candidateZ);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (currentAnimation() >= 15)
                return 0;

            SPRITE* const child = childChain();
            if (goalSprite() || (child && child->goalSprite()))
            {
                std::uint32_t delta = static_cast<std::uint32_t>(
                    Vid()->frameSpeedForAnimation(currentAnimation()));
                const std::uint32_t frameDelta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                if (frameDelta > delta)
                    delta = frameDelta;
                setAttackDecisionCode(computeAttackDecisionCode(delta));
            }

            ChangeAnimation(x87IsZeroOrUnordered(Speed()) ? 0 : 2);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_ADD_ITEM):
        {
            const std::int32_t word = argument1;
            if (argument1 == 301 || argument1 == 235)
            {
                appendCommandWordValue(word);
                return 0;
            }

            if (findLastCommandWord(word) >= 0)
                return 0;

            growCommandWordStorage();
            appendCommandWordValue(word);
            if (argument1 < 260 || argument1 > 269)
                return 0;

            if (dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0) != 0 &&
                argument1 - 260 <= Vid()->linkedVid()->nvid() - 10 &&
                argument1 != 260)
            {
                return 0;
            }
            switchLinkedWeaponSlot(argument1 - 260);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
        {
            const int index = argument1;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
                return derivedStateValue(index);
            return dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {
            const int index = argument2;
            if (index > 9)
                return 0;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
            {
                const int value = spriteAdd32Wrap(derivedStateValue(index), argument1);
                setDerivedStateValue(index, value);
                return value;
            }
            return dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO, argument1, 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {
            int damage = argument1;
            if (damage > 0)
            {
                if ((m_runtimeFlags & InvulnerableFlag) != 0u)
                    return 0;
                VID* const ownVid = Vid();
                if (ownVid->nvid() != 350)
                {
                    for (SPRITE* child = childChain(); child; child = child->childChain())
                    {
                        const int nvid = child->Vid()->nvid();
                        if (nvid == 203 || nvid == 181)
                            return 0;
                    }

                    static constexpr int kDamagePercent[3] = {50, 70, 90};
                    for (SPRITE* child = childChain(); child; child = child->childChain())
                    {
                        const int nvid = child->Vid()->nvid();
                        if (nvid >= 200 && nvid <= 202)
                        {
                            const int percent = kDamagePercent[nvid - 200];
                            const int scaledDamage = spriteImul32Low(damage, percent);
                            const int childDamage = spriteAdd32Wrap(scaledDamage, 50) / 100;
                            child->dispatchVirtualAction(ActionCode::ACT_DAMAGE, childDamage, argument2, argument3);
                            damage = spriteAdd32Wrap(damage, scaledDamage / -100);
                        }
                    }
                }

                if (damage >= animationFrameTime() &&
                    dispatchVirtualAction(ActionCode::ACT_HAVE_ITEM, 230, 0, 0) != 0)
                {
                    static_cast<void>(dispatchVirtualAction(ActionCode::ACT_DELETE_ITEM, 230, 0, 0));
                    const int bucket = armyIndex();
                    updateAnimationFrameTime(Vid()->animationFrameDuration(bucket));

                    core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                    VID* createVid = MAP::NullVid();
                    if (table.count() > 181)
                    {
                        if (VID* const raw = table.slot(181))
                            createVid = raw;
                    }
                    static_cast<void>(mapOwner()->CreateSpriteViaFactory(
                        createVid,
                        VECTOR{X(), Y(), Z() + 22.0f},
                        ANGLE(0),
                        this,
                        false));
                    return 0;
                }
            }
            return dispatchActionOpcode(static_cast<std::uint32_t>(ActionCode::ACT_DAMAGE), damage, argument2, argument3);
        }

        case static_cast<int>(ActionCode::ACT_CHANGE_VID):
        {
            VID* const previousVid = Vid();
            VID* const previousLink = previousVid->linkedVid();
            if (previousVid->nvid() < 20)
                setExtendedStateValue(previousLink->nvid(), dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0));

            static_cast<void>(dispatchBaseActionOpcode(ActionCode::ACT_CHANGE_VID, argument1, argument2, argument3));

            VID* const vid = Vid();
            VID* const link = vid->linkedVid();
            if (vid->nvid() > 20)
            {
                const int weaponValue = vid->activeWeaponAmmoCapacity();
                const int currentValue = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                static_cast<void>(dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO, spriteSub32Wrap(weaponValue, currentValue), 0, 0));
                return 0;
            }

            int childCommandVidSlot = -1;
            if (findLastCommandWord(204u) >= 0)
                childCommandVidSlot = 200;
            else if (findLastCommandWord(205u) >= 0)
                childCommandVidSlot = 201;
            else if (findLastCommandWord(206u) >= 0)
                childCommandVidSlot = 202;

            if (childCommandVidSlot >= 0)
            {
                core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
                VID* childCommandVid = MAP::NullVid();
                if (appVidTable.count() > childCommandVidSlot)
                {
                    if (VID* const vidSlot = appVidTable.slot(childCommandVidSlot))
                        childCommandVid = vidSlot;
                }

                SPRITE* const rawHead = childChain();
                const VECTOR childXYZ(X(), Y(), Z() + vid->linkOffset().z);
                SPRITE* const created = mapOwner()->CreateSpriteViaFactory(
                    childCommandVid,
                    childXYZ,
                    Direction(),
                    nullptr,
                    false);
                static_cast<void>(rawHead->insertChildChainHead(created));
            }

            const int currentValue = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
            static_cast<void>(dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO,
                spriteSub32Wrap(extendedStateValue(link->nvid()), currentValue), 0, 0));
            return 0;
        }

        default:
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
        }
    }

    SPRITE* SPRITE::probeMovementFootprint(float x, float y) noexcept
    {
        const float mapSizeX = applicationWorldFloatAt(core::retail_application_layout::MapExtentX);
        const float mapSizeY = applicationWorldFloatAt(core::retail_application_layout::MapExtentY);
        if (x87LessOrUnordered(x, 0.0f) ||
            !x87LessOrUnordered(x, mapSizeX) ||
            x87LessOrUnordered(y, 0.0f) ||
            !x87LessOrUnordered(y, mapSizeY))
        {
            return this;
        }

        MAP* const map = mapOwner();
        const float groundZ = map->GetGroundZ(
            Vid(), VECTOR2{X(), Y()}, Direction());
        if (!x87LessOrUnordered(groundZ, Z()))
            return this;

        return CanPlace(x, y, Z());
    }

    int SPRITE::switchLinkedWeaponSlot(int value) noexcept
    {
        VID* const vid = m_vid;
        VID* const activeLinkVid = vid->linkedVid();
        if (activeLinkVid->nvid() > 20)
            return 0;

        int selectedSlot = value;
        if (selectedSlot == 10)
            selectedSlot = 0;

        int remaining = commandWordCount();
        if (remaining == 0)
            return 0;

        const std::int32_t* cursor = commandWordData() + remaining;
        const std::int32_t selectedWord = selectedSlot + 260;
        bool found = false;
        do
        {
            --cursor;
            --remaining;
            if (*cursor == selectedWord)
            {
                found = true;
                break;
            }
        }
        while (remaining != 0);

        if (!found)
            return 0;

        SPRITE* const child = childChain();
        if (!child || child->Vid() != activeLinkVid)
            return 0;

        const int saved = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        setExtendedStateValue(activeLinkVid->nvid(), saved);
        child->dispatchVirtualAction(ActionCode::ACT_CHANGE_VID, selectedSlot + 10, 0, 0);

        const int nextNvid = selectedSlot + 10;
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        VID* nextLinkVid = MAP::NullVid();
        if (nextNvid >= 0 && nextNvid < appVidTable.count())
        {
            if (VID* const slot = appVidTable.slot(nextNvid))
                nextLinkVid = slot;
        }
        vid->setLinkedVid(nextLinkVid);

        const int current = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        (void)dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO, derivedStateValue(selectedSlot) - current, 0, 0);
        return 1;
    }

    void SPRITE::playSfxAtWorldPosition(int nsfx) noexcept
    {
        const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const int graphSizeX = graph->SizeX();
        const int graphSizeY = graph->SizeY();
        const float halfScreenX = static_cast<float>(graphSizeX) * 0.5f;
        const float halfScreenY = static_cast<float>(graphSizeY) * 0.5f;
        const float soundX = m_xyz.x - drawState.cameraShiftX() - halfScreenX;
        const float soundY = m_xyz.y - m_xyz.z - drawState.cameraShiftY() - halfScreenY;
        sound::GlobalSoundEngine()->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);

    }

    int SPRITE::IsInsideRetail(float x, float y) const noexcept
    {
        VID* const vid = m_vid;
        const float halfX = vid->halfSizeX();
        if (!(x >= m_xyz.x - halfX))
            return 0;
        if (!(m_xyz.x + halfX >= x))
            return 0;

        const float baseY = m_xyz.y - m_xyz.z;
        const float halfY = vid->halfSizeY();
        if (!(y > baseY - vid->sizeZ() - halfY))
            return 0;
        if (!(baseY + halfY > y))
            return 0;
        return 1;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    SPRITE* SPRITE::CanPlace(float x, float y, float z)
    {
        VID* const vid = m_vid;
        const DWORD movementMask = static_cast<DWORD>(vid->movementMask());
        if (movementMask == 0u)
            return nullptr;

#if defined(_WIN32)
        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
        MAP* const map = mapOwner();
#endif
        SPRITE* const terrainSentinel = mouseSprite();

        if ((vid->properties() & P_ZEROZ) != 0u)
        {
            const float moveUp = vid->moveUpZ();
            const float moveDown = vid->moveDownZ();
            float ground = map->GetGroundZ(vid, VECTOR2{x, y}, m_direction);
            if ((ground - z > moveUp) || (z - ground > moveDown))
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                ground = map->GetGroundZ(VECTOR2{left, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{left, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
            }
        }
        else
        {
            if (map->GetGroundZ(vid, VECTOR2{x, y}, m_direction) > z)
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                if (map->GetGroundZ(VECTOR2{left, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{left, bottom}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, bottom}) > z)
                    return terrainSentinel;
            }
        }

        const float probeHalfX = vid->halfSizeX();
        const float probeHalfY = vid->halfSizeY();
        SPRITE_COLLECTOR_HASH_MAP* const spatialHash = GlobalSpriteHashMap();
        for (SPRITE* candidate = spatialHash->firstSpriteInBox(
                 x - probeHalfX, y - probeHalfY, x + probeHalfX, y + probeHalfY);
             candidate;
             candidate = spatialHash->nextSpriteInBox())
        {
            if (candidate == this || candidate->currentAnimation() >= 0x0F)
                continue;

            VID* const candidateVid = candidate->m_vid;
            if (!(candidateVid->halfSizeX() + probeHalfX > std::fabs(candidate->m_xyz.x - x)))
                continue;
            if (!(candidateVid->halfSizeY() + probeHalfY > std::fabs(candidate->m_xyz.y - y)))
                continue;
            if (candidateVid->sizeZ() + candidate->m_xyz.z < z)
                continue;
            if (z + vid->sizeZ() < candidate->m_xyz.z)
                continue;
            if ((static_cast<DWORD>(candidateVid->movementMask()) & movementMask) == 0u)
                continue;
            return candidate;
        }
        return nullptr;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    SPRITE* SPRITE::CanPlaceWithCrush(float x, float y, float z)
    {
        VID* const vid = m_vid;
        const DWORD movementMask = static_cast<DWORD>(vid->movementMask());
        if (movementMask == 0u)
            return nullptr;

#if defined(_WIN32)
        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
        MAP* const map = mapOwner();
#endif
        SPRITE* const terrainSentinel = mouseSprite();

        if ((vid->properties() & P_ZEROZ) != 0u)
        {
            const float moveUp = vid->moveUpZ();
            const float moveDown = vid->moveDownZ();
            float ground = map->GetGroundZ(vid, VECTOR2{x, y}, m_direction);
            if ((ground - z > moveUp) || (z - ground > moveDown))
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                ground = map->GetGroundZ(VECTOR2{left, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{left, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
            }
        }
        else
        {
            if (map->GetGroundZ(vid, VECTOR2{x, y}, m_direction) > z)
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                if (map->GetGroundZ(VECTOR2{left, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{left, bottom}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, bottom}) > z)
                    return terrainSentinel;
            }
        }

        const float probeHalfX = vid->halfSizeX();
        const float probeHalfY = vid->halfSizeY();
        SPRITE_COLLECTOR_HASH_MAP* const spatialHash = GlobalSpriteHashMap();
        for (SPRITE* candidate = spatialHash->firstSpriteInBox(
                 x - probeHalfX, y - probeHalfY, x + probeHalfX, y + probeHalfY);
             candidate;
             candidate = spatialHash->nextSpriteInBox())
        {
            if (candidate == this || candidate->currentAnimation() >= 0x0F)
                continue;

            VID* const candidateVid = candidate->m_vid;
            if (!(candidateVid->halfSizeX() + probeHalfX > std::fabs(candidate->m_xyz.x - x)))
                continue;
            if (!(candidateVid->halfSizeY() + probeHalfY > std::fabs(candidate->m_xyz.y - y)))
                continue;
            if (candidateVid->sizeZ() + candidate->m_xyz.z < z)
                continue;
            if (z + vid->sizeZ() < candidate->m_xyz.z)
                continue;

            if ((static_cast<DWORD>(candidateVid->movementMask()) & movementMask) != 0u)
                return candidate;

            if ((candidateVid->properties() & P_CRUSH) != 0u)
                candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 5, 0, 0);
        }
        return nullptr;
    }

    SPRITE* SPRITE::CanPlaceWithCrushAndGlide(float* xOut, float* yOut, float* zOut)
    {
        if (m_vid->movementMask() == 0)
            return 0;

        SPRITE* const blocker = CanPlaceWithCrush(*xOut, *yOut, *zOut);
        if (!blocker)
        {
            if ((m_vid->properties() & P_ZEROZ) != 0u)
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
                MAP* const map = mapOwner();
#endif
                *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut}, m_direction);
            }
            return 0;
        }

        const int collisionScript = m_vid->collisionScriptFunction();
        if (collisionScript >= 0)
        {
            SPRITE* const scriptBlocker = blocker == mouseSprite() ? nullptr : blocker;
            const int selfArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            const int blockerArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(scriptBlocker) & 0xFFFFFFFFu);
            int collisionResult = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
            collisionResult = reinterpret_cast<core::Application*>(
                core::ApplicationPhysicalOwner())->callScriptFunctionRetail(
                    collisionScript, selfArg, blockerArg, 0);
#else
            collisionResult = core::Application::callScriptFunction(
                collisionScript, selfArg, blockerArg);
#endif
            if (collisionResult != 0)
            {
                *xOut = X();
                *yOut = Y();
                *zOut = Z();
                return nullptr;
            }
        }

        if (blocker != mouseSprite())
        {
            VID* const blockerVid = blocker->Vid();
            if ((static_cast<std::uint32_t>(blockerVid->weaponFlags()) & 0x00000040u) != 0u)
            {
                const float pushedXInitial = addThenSubtractRounded(
                    blocker->X(), *xOut, X());
                const float pushedYInitial = addThenSubtractRounded(
                    blocker->Y(), *yOut, Y());
                float pushedX = pushedXInitial;
                float pushedY = pushedYInitial;
                float pushedZ = blocker->Z();

                // INC/CMP/JGE and DEC are raw signed x86 DWORD operations.
                g_collisionPushRecursionDepth = spriteAdd32Wrap(g_collisionPushRecursionDepth, 1);
                if (g_collisionPushRecursionDepth < 5 &&
                    blocker->CanPlaceWithCrushAndGlide(&pushedX, &pushedY, &pushedZ) == nullptr)
                {
                    bool allowPush = true;
                    if ((static_cast<std::uint32_t>(m_vid->weaponFlags()) & 0x00000040u) != 0u)
                    {
                        const std::uint8_t ownDir = static_cast<std::uint8_t>(directionIndex());
                        const std::uint8_t shifted = static_cast<std::uint8_t>(ownDir - 0x80u);
                        const std::uint8_t blockerDir = static_cast<std::uint8_t>(blocker->directionIndex());
                        const std::uint8_t d1 = static_cast<std::uint8_t>(shifted - blockerDir);
                        const std::uint8_t d2 = static_cast<std::uint8_t>(blockerDir - shifted);
                        const std::uint8_t delta = d1 < d2 ? d1 : d2;
                        allowPush = delta >= 0x10u || ownDir < 0x80u;
                    }
                    if (allowPush)
                        blocker->ChangeCoor(pushedX, pushedY, pushedZ);
                }
                if (g_collisionPushRecursionDepth != 0)
                    g_collisionPushRecursionDepth = spriteSub32Wrap(g_collisionPushRecursionDepth, 1);
            }
        }

        const float savedX = X();
        const float savedY = Y();
        const float savedZ = Z();

        if (CanPlace(savedX, *yOut, *zOut) == nullptr)
        {
            *xOut = savedX;
            if ((m_vid->properties() & P_ZEROZ) != 0u)
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
                MAP* const map = mapOwner();
#endif
                *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut}, m_direction);
            }
            return 0;
        }

        if (CanPlace(*xOut, savedY, *zOut) == nullptr)
        {
            *yOut = savedY;
            if ((m_vid->properties() & P_ZEROZ) != 0u)
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
                MAP* const map = mapOwner();
#endif
                *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut}, m_direction);
            }
            return 0;
        }

        if (blocker != mouseSprite())
        {
            VID* const blockerVid = blocker->Vid();
            const bool overlapsOld =
                blocker->currentAnimation() < 15 &&
                blockerVid->halfSizeX() + m_vid->halfSizeX() > std::fabs(blocker->X() - savedX) &&
                blockerVid->halfSizeY() + m_vid->halfSizeY() > std::fabs(blocker->Y() - savedY) &&
                blockerVid->sizeZ() + blocker->Z() >= savedZ &&
                m_vid->sizeZ() + savedZ >= blocker->Z();
            if (overlapsOld)
            {
                if ((m_vid->properties() & P_ZEROZ) != 0u)
                {
#if defined(_MSC_VER) && defined(_M_IX86)
                    MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
                    MAP* const map = mapOwner();
#endif
                    *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut}, m_direction);
                }
                return nullptr;
            }
        }

        *xOut = savedX;
        *yOut = savedY;
        *zOut = savedZ;
        return blocker;
    }

    ANGLE SPRITE::GlideDirection(ANGLE value) noexcept
    {
        const int direction = value.Int();
        const float halfX = m_vid->sizeX() * 0.5f;
        const float halfY = m_vid->sizeY() * 0.5f;
        const float x = m_xyz.x;
        const float y = m_xyz.y;
        const float z = m_xyz.z;

        if (direction > 0x60 && direction < 0xA0)
        {
            if (CanPlace(x - halfX, y + halfY, z) != nullptr)
            {
                if (CanPlace(x + halfX, y, z) == nullptr)
                    return ANGLE(0x58u);
            }

            if (CanPlace(x + halfX, y + halfY, z) != nullptr)
            {
                if (CanPlace(x - halfX, y, z) == nullptr)
                    return ANGLE(0xA8u);
            }
            return value;
        }

        if (direction >= 0x20 && direction <= 0xE0)
        {
            if (direction > 0xB0 && direction < 0xD0)
            {
                if (CanPlace(x - halfX, y + halfY, z) != nullptr)
                {
                    if (CanPlace(x, y - halfY, z) == nullptr)
                        return ANGLE(0xD8u);
                }

                if (CanPlace(x - halfX, y - halfY, z) != nullptr)
                {
                    if (CanPlace(x, y + halfY, z) == nullptr)
                        return ANGLE(0xA8u);
                }
            }
            else if (direction > 0x30 && direction < 0x50)
            {
                if (CanPlace(x + halfX, y + halfY, z) != nullptr)
                {
                    if (CanPlace(x, y - halfY, z) == nullptr)
                        return ANGLE(0x28u);
                }

                if (CanPlace(x + halfX, y - halfY, z) != nullptr)
                {
                    if (CanPlace(x, y + halfY, z) == nullptr)
                        return ANGLE(0x58u);
                }
            }
            return value;
        }

        if (CanPlace(x - halfX, y - halfY, z) != nullptr)
        {
            if (CanPlace(x + halfX, y, z) == nullptr)
                return ANGLE(0x28u);
        }

        if (CanPlace(x + halfX, y - halfY, z) == nullptr)
            return value;

        if (CanPlace(x - halfX, y, z) == nullptr)
            return ANGLE(0xD8u);

        return value;
    }

    void SPRITE::ChangeDirection(ANGLE direction) noexcept
    {
        const int requestedDirection = direction.Int();
        int frameDirection = requestedDirection;

        if (m_zSpeed != 0.0f && (m_vid->property & P_VERTDIR) != 0)
        {
            int projectedDirection = frameDirection;
            (void)projectVerticalMotionDirection(
                frameDirection, m_speed, m_zSpeed, projectedDirection);
            frameDirection = projectedDirection;
        }

        const int beforeDirection = directionIndex();
        if (beforeDirection == frameDirection)
            return;

        if (SPRITE* const child = childChain())
        {
            VID* const childVid = child->Vid();
            const VECTOR link = m_vid->linkOffset();
            const bool preserveLinkedWorldOffset =
                (childVid->properties() & P_NOTCHANGELINKERCOOR) != 0u;
            const bool fixedLinkRoute =
                childVid == m_vid->linkedVid() &&
                !preserveLinkedWorldOffset &&
                (!spriteFcompC3(link.x, 0.0f) ||
                 !spriteFcompC3(link.y, 0.0f));

            const int steppedDirection = quantizeDirectionForVid(
                frameDirection,
                static_cast<int>(m_vid->directionQuantizationOffset()),
                m_vid->directionCount());

            if (fixedLinkRoute)
            {
                const float offsetX =
                    directionCos(steppedDirection) * link.x +
                    directionSin(steppedDirection) * link.y;
                const float offsetY =
                    directionSinAux(steppedDirection) * link.x -
                    directionCosAux(steppedDirection) * link.y;
                child->ChangeCoor(m_xyz.x + offsetX,
                                  m_xyz.y + offsetY,
                                  child->m_xyz.z);
            }
            else if (!preserveLinkedWorldOffset &&
                     childVid->spriteClassId() == B_LINKER)
            {
                const int deltaDirection =
                    (steppedDirection - child->linkerDirection()) & 0xFF;
                const float linkerX = child->linkerX();
                const float linkerY = child->linkerY();
                const float rotatedX =
                    directionCos(deltaDirection) * linkerX -
                    directionSin(deltaDirection) * linkerY;
                const float rotatedY =
                    directionCosAux(deltaDirection) * linkerY +
                    directionSinAux(deltaDirection) * linkerX;
                SPRITE* const base = child->linkerOwner()
                    ? child->linkerOwner()
                    : child->childBacklink();
                child->ChangeCoor(base->m_xyz.x + rotatedX,
                                  base->m_xyz.y + rotatedY,
                                  child->m_xyz.z);
            }

            if (spriteFcompC3(childVid->rotationSpeedValue(), 0.0f))
                child->ChangeDirection(requestedDirection);
        }

        if (m_vid->noDir != 1)
        {
            const int oldFrameDelta = m_currentFrame - m_currentFrameBegin;
            const int animationSlot = m_currentAnimation;
            int beginFrame = static_cast<int>(m_vid->animationBaseFrame[animationSlot]);
            const int noDir = static_cast<int>(m_vid->noDir);
            if (noDir != 0)
            {
                const std::uint32_t angle =
                    static_cast<std::uint32_t>(
                        static_cast<int>(m_vid->directionQuantizationOffset()) + frameDirection) & 0xFFu;
                const std::uint32_t dirProduct = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(angle) * static_cast<std::uint32_t>(noDir));
                const std::int32_t dirIndex = static_cast<std::int32_t>(dirProduct >> 8u);
                const std::int32_t frameOffset = spriteImul32Low(
                    dirIndex, static_cast<int>(m_vid->animationFrameCount[animationSlot]));
                beginFrame = spriteAdd32Wrap(beginFrame, frameOffset);
            }

            const int frameCount = static_cast<int>(m_vid->animationFrameCount[animationSlot]);
            m_currentFrameBegin = beginFrame;
            m_currentFrameEnd = beginFrame + frameCount - 1;
            m_currentFrame = beginFrame + oldFrameDelta;
        }

        m_direction = ANGLE(requestedDirection);
    }

    ANGLE SPRITE::RotateTact(ANGLE value, std::uint32_t deltaMs) noexcept
    {
        const unsigned char target = value.value;
        unsigned char current = m_direction.value;

        if (m_childChain != nullptr && m_vid->nvid() == 9 && m_vid->noAnimCadr[6] != 0)
        {
            const unsigned char clockwise = static_cast<unsigned char>(current - target);
            const unsigned char counterClockwise = static_cast<unsigned char>(target - current);
            const unsigned char shortest = clockwise < counterClockwise ? clockwise : counterClockwise;
            if (shortest > 0x40u)
            {
                ChangeDirection(ANGLE(static_cast<unsigned char>(current - 0x80u)));
                current = m_direction.value;
            }
        }

        if (target == current)
            return ANGLE(0u);

        const float rotationSpeed = m_vid->rotationSpeedValue();
        if (rotationSpeed == 999999.0f)
        {
            ChangeDirection(value);
            return ANGLE(1u);
        }

        const std::int32_t signedDelta = static_cast<std::int32_t>(deltaMs);
        const int step = static_cast<int>(
            static_cast<float>(signedDelta) * rotationSpeed + 0.5f);

        if (step == 0)
        {
            const unsigned char a = static_cast<unsigned char>(current - target);
            const unsigned char b = static_cast<unsigned char>(target - current);
            return ANGLE(a < b ? a : b);
        }

        int absDelta = static_cast<int>(current) - static_cast<int>(target);
        if (absDelta < 0)
            absDelta = -absDelta;
        const int wrapDelta = 0x100 - absDelta;

        bool subtractStep = false;
        bool addStep = false;
        if (current > target)
        {
            if (absDelta < wrapDelta)
                subtractStep = true;
            else
                addStep = true;
        }
        else if (current < target)
        {
            if (absDelta > wrapDelta)
                subtractStep = true;
            else
                addStep = true;
        }

        const int shortest = absDelta < wrapDelta ? absDelta : wrapDelta;
        if (step >= shortest)
        {
            ChangeDirection(value);
            return ANGLE(0u);
        }

        if (subtractStep)
            ChangeDirection(ANGLE(static_cast<unsigned char>(current - step)));
        if (addStep)
            ChangeDirection(ANGLE(static_cast<unsigned char>(m_direction.value + step)));

        current = m_direction.value;
        const unsigned char a = static_cast<unsigned char>(current - target);
        const unsigned char b = static_cast<unsigned char>(target - current);
        return ANGLE(a < b ? a : b);
    }

    void SPRITE::clearCommandStackAndReleaseTargets() noexcept
    {
        const std::int32_t count = static_cast<std::int32_t>(
            m_commandStack.m_commandRecords.count);
        SpriteCommandStack::CommandRecordStorage* const raw = m_commandStack.m_commandRecords.records;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::uint32_t index = static_cast<std::uint32_t>(i);
            if ((raw[index].words[0] & 0xFFu) != 0x4Au || raw[index].words[1] == 0u)
                continue;

            SPRITE* target = nullptr;
#ifdef _WIN32
            target = reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(raw[index].words[1]));
#endif
            if (!target)
                continue;

            (void)target->ReleaseListReference();
        }
        m_commandStack.clear();
    }

    void SPRITE::copyCommandPrefixTo(SPRITE* target) noexcept
    {
        if (!target || target == this)
            return;

        target->clearCommandStackAndReleaseTargets();
        const std::uint32_t count = m_commandStack.m_commandRecords.count;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const SpriteCommandStack::CommandRecordStorage& raw = m_commandStack.m_commandRecords.records[i];
            if (raw.words[0] == 73u)
                break;
            SpriteCommandRecord record{};
            record.opcode = raw.words[0];
            record.argument1 = raw.words[1];
            record.argument2 = raw.words[2];
            record.argument3 = raw.words[3];
            target->m_commandStack.appendCommandRecord(record);
        }
    }

    float SPRITE::nearDistanceToRetail(const SPRITE* other) const noexcept
    {
        const float dx = std::fabs(other->m_xyz.x - m_xyz.x);
        const float dy = std::fabs(other->m_xyz.y - m_xyz.y);
        if (dx > dy)
            return dx + dy * 0.5f;
        return dy + dx * 0.5f;
    }

    float SPRITE::weaponBattleRangeRetail() const noexcept
    {
        VID* const vid = m_vid;
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                const float parentRange = vid->weaponBattleRange();
                if (parentRange == 0.0f)
                    return childVid->weaponBattleRange();
            }
        }
        return vid->weaponBattleRange();
    }

    int SPRITE::weaponEnemyPriorityRetail() const noexcept
    {
        VID* vid = m_vid;
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                vid = childVid;
            }
        }
        return vid->weaponEnemyPriority();
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    int SPRITE::enemyPriority(float candidateMetric,
                            float selectedMetric,
                            SPRITE* candidate,
                            SPRITE* selected) noexcept
    {
        if (!selected)
            return 1;

        VID* const thisVid = Vid();
        const DWORD weaponFlags = static_cast<DWORD>(thisVid->weaponFlags());
        if ((weaponFlags & 0x00000100u) != 0u)
            return selectedMetric > candidateMetric ? 1 : 0;

        if (SPRITE* const currentTarget = bestTargetSprite())
        {
            if (selected == currentTarget)
                return 0;
            if (candidate == currentTarget)
                return 1;
        }

        const DWORD candidateBucket = candidate->armyBits();
        const DWORD selectedBucket = selected->armyBits();
        if (candidateBucket == (2u << ArmyBitsShift) && selectedBucket != (2u << ArmyBitsShift))
            return 0;
        if (selectedBucket != (2u << ArmyBitsShift) && candidateBucket == (2u << ArmyBitsShift))
            return 1;

        const DWORD selectedType = selected->Vid()->spriteTypeId();
        const DWORD candidateType = candidate->Vid()->spriteTypeId();
        if ((selectedType & 0x08u) != 0u && (candidateType & 0x08u) == 0u)
            return 0;
        if ((selectedType & 0x08u) == 0u && (candidateType & 0x08u) != 0u)
            return 1;

        const float nearRange = weaponBattleRangeRetail();
        if (selectedMetric > nearRange && candidateMetric <= nearRange)
            return 1;

        int candidateAction92 = 0;
        if ((candidateType & 0x04u) != 0u)
            candidateAction92 = candidate->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);

        if ((selectedType & 0x04u) != 0u &&
            selected->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0) != 0)
        {
            if (candidateAction92 == 0)
                return 0;
        }
        else if (candidateAction92 != 0)
        {
            return 1;
        }

        const int candidatePriority = candidate->weaponEnemyPriorityRetail();
        const int selectedPriority = selected->weaponEnemyPriorityRetail();
        if (candidatePriority > selectedPriority)
            return 1;
        if (candidatePriority >= selectedPriority && selectedMetric > candidateMetric)
            return 1;
        return 0;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    SPRITE* SPRITE::SeekEnemy()
    {
        SPRITE* scanOwner = this;
        while (SPRITE* const child = scanOwner->childChain())
        {
            VID* const ownerVid = scanOwner->Vid();
            VID* const childVid = child->Vid();
            if (childVid != ownerVid->linkedVid() ||
                childVid->hasWeaponChildDescriptor() == 0u ||
                childVid->weaponCount() == 0u)
            {
                break;
            }
            scanOwner = child;
        }

        VID* const ownerVid = scanOwner->Vid();
        const DWORD ownerTypeMask = static_cast<DWORD>(ownerVid->weaponTypeMask());
        const DWORD ownerWeaponFlags = static_cast<DWORD>(ownerVid->weaponFlags());
        const float maxRange = ownerVid->weaponDetectRange();
        const float nearRange = ownerVid->weaponBattleRange();
        const float minRange = ownerVid->weaponMinimumRange();
        if (maxRange == 0.0f || ownerTypeMask == 0u)
            return nullptr;

        float selectedMetric = maxRange + 1.0f;
        SPRITE* selected = nullptr;
        const DWORD ownerBucket = scanOwner->armyBits();

        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();

        SPRITE* candidate = nullptr;
        if (ownerBucket == (1u << ArmyBitsShift))
        {
            candidate = scanOwner->mapOwner()->flagmanSpriteForPlayer(0);
        }
        else
        {
            candidate = overflow.beginReverseIteration(cursor);
        }

        while (candidate)
        {
            VID* const candidateVid = candidate->Vid();
            const DWORD candidateType = candidateVid->spriteTypeId();
            const DWORD candidateBucket = candidate->armyBits();

            if ((candidateType & ownerTypeMask) != 0u &&
                candidateVid->maxHp != 0 &&
                (((scanOwner->runtimeFlags() ^ candidate->runtimeFlags()) & ArmyBitsMask) != 0u ||
                 (ownerWeaponFlags & 0x80u) != 0u) &&
                (candidateVid->properties() & P_INVISIBLEFORENEMY) == 0u &&
                !(candidateBucket == (2u << ArmyBitsShift) &&
                  (ownerWeaponFlags & 0x80u) == 0u &&
                  (((core::ApplicationFlags() & application_flags::EnemyCanAttackNeutralTrains) == 0u) ||
                   ownerBucket != (1u << ArmyBitsShift) ||
                   candidateVid->spriteClassId() != 21u)) &&
                !((candidateType & 0x08u) != 0u && candidate->childBacklink() != nullptr))
            {
                if ((ownerWeaponFlags & 0x02u) == 0u)
                {
                    // fall through to distance test
                }
                else
                {
                    const unsigned char wanted = RetailDirectionFromFloatXY(
                        candidate->X() - scanOwner->X(),
                        candidate->Y() - scanOwner->Y()).value;
                    const unsigned char current = static_cast<unsigned char>(scanOwner->directionIndex());
                    const unsigned char d1 = static_cast<unsigned char>(wanted - current);
                    const unsigned char d2 = static_cast<unsigned char>(current - wanted);
                    if ((d1 < d2 ? d1 : d2) >= 0x20u)
                        goto next_candidate;
                }

                {
                    const float metric = scanOwner->nearDistanceToRetail(candidate);
                    if (metric <= maxRange && metric >= minRange)
                    {
                        if ((ownerVid->spriteTypeId() & 0x08u) != 0u &&
                            scanOwner->bestTargetSprite() == nullptr)
                        {
                            if (metric < selectedMetric)
                            {
                                const DWORD flags = scanOwner->runtimeFlags();
                                if ((flags & ArmyBitsMask) != 0u ||
                                    candidateVid->spriteClassId() != 21u ||
                                    !static_cast<ENGINE*>(candidate)->engineChainContainsArmy(0))
                                {
                                    selectedMetric = metric;
                                    selected = candidate;
                                    if ((ownerWeaponFlags & 0x08u) != 0u && (std::rand() % 3) == 0)
                                        return candidate;
                                }
                            }
                        }
                        else if (candidateVid->nvid() != 104)
                        {
                            bool acceptCandidate = false;
                            if (selectedMetric <= nearRange)
                            {
                                if (metric <= nearRange)
                                    acceptCandidate = scanOwner->enemyPriority(metric, selectedMetric, candidate, selected) != 0;
                            }
                            else if (metric <= nearRange)
                            {
                                acceptCandidate = true;
                            }
                            else
                            {
                                acceptCandidate = scanOwner->enemyPriority(metric, selectedMetric, candidate, selected) != 0;
                            }

                            if (acceptCandidate)
                            {
                                const DWORD flags = scanOwner->runtimeFlags();
                                if ((flags & ArmyBitsMask) != 0u ||
                                    candidateVid->spriteClassId() != 21u ||
                                    !static_cast<ENGINE*>(candidate)->engineChainContainsArmy(0))
                                {
                                    selectedMetric = metric;
                                    selected = candidate;
                                    if ((ownerWeaponFlags & 0x08u) != 0u && (std::rand() % 3) == 0)
                                        return candidate;
                                }
                            }
                        }
                    }
                }
            }

        next_candidate:
            if (ownerBucket == (1u << ArmyBitsShift))
                return selected;
            candidate = overflow.continueReverseIteration(cursor);
        }

        return selected;
    }

    void SPRITE::buildRetailGammaPair(GammaRawPair& out) const noexcept
    {
        if (m_actionAuxState &&
            (m_actionAuxState->commandMask0 != 0u || m_actionAuxState->commandMask1 != 0u))
        {
            out.first = m_actionAuxState->commandMask0;
            out.second = m_actionAuxState->commandMask1;
        }
        else
        {
            out = m_vid->armyGammaOverride(static_cast<unsigned>(armyIndex()));
        }

        if ((m_vid->runtimeAuxFlags() & 0x01u) == 0u)
            return;

        const float position = m_actionAuxState->effectCurvePosition;
        const int segment = static_cast<int>(position); // CVTTSS2SI: trunc toward zero.

        const auto curveValue = [this, position, segment](int baseOffset) noexcept -> int
        {
            if (segment >= 7)
                return m_vid->weaponIntAt(baseOffset + 7 * 4);

            const int first = m_vid->weaponIntAt(baseOffset + segment * 4);
            const int second = m_vid->weaponIntAt(baseOffset + (segment + 1) * 4);
            const float interpolated =
                static_cast<float>(second - first) * (position - static_cast<float>(segment)) +
                static_cast<float>(first);
            return static_cast<int>(interpolated); // CVTTSS2SI.
        };

        const int blue = curveValue(0x0A4);
        const int green = curveValue(0x084);
        const int red = curveValue(0x064);
        const int alpha = curveValue(0x0C4);
        GammaRawPair effect{};
        effect.setSignedDeltasRetail(alpha, red, green, blue);
        out.setSaturatingAddRetail(effect, out);
    }

    void SPRITE::Tact()
    {
        const std::uint32_t previousFrameClock = core::PreviousWorldTimeMilliseconds();
        const std::uint32_t now = core::CurrentTimeMilliseconds();

        if ((m_vid->runtimeAuxFlags() & 0x0Fu) != 0u && m_actionAuxState)
        {
            ActionAuxState* const aux = m_actionAuxState;
            const std::uint32_t elapsed = now - aux->effectTimestamp;
            std::uint32_t duration = static_cast<std::uint32_t>(
                m_vid->weaponIntAt(static_cast<int>(VID::WeaponFieldOffset::EffectRefreshInterval)));

            if (duration == 999999u)
            {
                if (m_currentAnimation < 15 || m_currentFrame == m_currentFrameBegin)
                {
                    const std::uint32_t lifetime = aux->lifetimeRemaining;
                    if (lifetime != 999999u)
                        duration = elapsed + lifetime;
                    else
                        duration = 0u;
                }
                else
                {
                    duration = static_cast<std::uint32_t>(spriteImul32Low(
                        m_vid->frameSpeedForAnimation(m_currentAnimation),
                        spriteSub32Wrap(m_currentFrame, m_currentFrameBegin)));
                }
            }

            if (duration != 0u)
            {
                int segment = static_cast<int>(std::floor(aux->effectCurvePosition)) + 1;
                if (segment < 1)
                    segment = 1;
                if (segment > 7)
                    segment = 7;

                const float elapsedF = static_cast<float>(elapsed);
                const float durationF = static_cast<float>(duration);
                while (segment < 7)
                {
                    const float threshold = m_vid->weaponFloatAt(0x44 + segment * 4);
                    if (threshold * durationF > elapsedF)
                        break;
                    ++segment;
                }

                const int previousSegment = segment - 1;
                const float previousPoint = m_vid->weaponFloatAt(0x44 + previousSegment * 4);
                const float nextPoint = m_vid->weaponFloatAt(0x44 + segment * 4);
                aux->effectCurvePosition =
                    ((elapsedF - previousPoint * durationF) /
                     ((nextPoint - previousPoint) * durationF)) +
                    static_cast<float>(previousSegment);
            }
        }

        if (!m_childBacklink)
        {
            const std::uint32_t savedClock14 = m_applicationBucketTime;
            m_applicationBucketTime = previousFrameClock;
            MoveTact();
            m_applicationBucketTime = savedClock14;
        }

        if (m_vid->gridDotCount() > 0)
            m_vid->SetGridZ(this);

        VID* const vid = m_vid;
        const int animation = m_currentAnimation;
        VID* const childVid = vid->childVid[animation];

        if (childVid)
        {
            const bool class23ZeroOffset =
                vid->spriteClassId() == 23u &&
                vid->childX[animation] == 0.0f &&
                vid->childY[animation] == 0.0f;

            if (class23ZeroOffset)
            {
                const REGION* const region = static_cast<const REGION*>(this);
                const bool applicationSized = (region->regionFlags() & REGION::FullViewportFlag) != 0u;
                const float width = applicationSized
                    ? applicationWorldFloatAt(core::retail_application_layout::MapExtentX)
                    : region->regionWidth();
                const float height = applicationSized
                    ? applicationWorldFloatAt(core::retail_application_layout::MapExtentY)
                    : region->regionHeight();
                const std::uint32_t tileCount = static_cast<std::uint32_t>(
                    computeRegionTileCount(width, height,
                                               childVid->sizeX(),
                                               childVid->sizeY()));
                const std::uint32_t delta = now - previousFrameClock;
                if (tileCount != 0u && delta != 0u)
                {

                    const std::uint32_t divisor = 1000u / delta / tileCount;
                    const int modulo = static_cast<int>(divisor + 1u);
                    if ((std::rand() % modulo) == 0)
                        spawnAnimationChild();
                }
            }
            else if ((childVid->properties() & P_BIRTHASSMOKE) != 0u)
            {
                ActionAuxState* const cadenceOwner = m_actionAuxState;
                std::uint32_t cadence = cadenceOwner->childCadence;
                if (now - now % cadence > previousFrameClock)
                {
                    const bool subtractGraphMotion =
                        (childVid->properties() & P_WIND) != 0u;
                    int graphDirection = 0;
                    float graphSpeed = 0.0f;
                    if (subtractGraphMotion)
                    {
                        GRAPH* const graph = GRAPH::CurrentGraph();
                        graphDirection = static_cast<int>(graph->windDirection());
                        graphSpeed = graph->windSpeed();
                    }

                    cadence = computeChildAnimationCadence(
                        m_direction.Int(), m_speed, m_zSpeed,
                        childVid->maximumZSpeed(),
                        subtractGraphMotion, graphDirection, graphSpeed,
                        childVid->sizeX(), childVid->sizeY());

                    if (animation == 8 &&
                        (vid->weaponFlags() & 0x10) != 0)
                    {
                        cadence >>= 1;
                    }
                    if (cadence > 30000u)
                        cadence = 30000u;
                    if (cadence == 0u)
                        cadence = 1u;
                    cadenceOwner->childCadence = cadence;
                    spawnAnimationChild();
                }
            }
        }

        if (m_animationLastTick == now)
            return;

        const std::uint32_t frameInterval = static_cast<std::uint32_t>(
            vid->frameSpeedForAnimation(m_currentAnimation));
        if (now - now % frameInterval <= previousFrameClock)
            return;

        DWORD flags = m_runtimeFlags;
        if ((flags & CommandBitsMask) != 0u)
        {
            const int action = static_cast<int>((flags >> CommandBitsShift) & CommandValueMask);
            if (action < 16 && !m_goalSprite)
            {
                LOG::ResourceError("SPRITE %i", 10, "command need goal, but goal==NULL",
                                   action, vid ? vid->nVid : -1);
                SetCommand(0, nullptr);
            }
        }

        if (m_actionTimer != 0u)
        {
            if (now - m_applicationBucketTime < m_actionTimer)
            {
                m_actionTimer =
                    m_applicationBucketTime + m_actionTimer - now;
            }
            else
            {
                m_actionTimer = 0;
                if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u)
                    SetCommand(0, nullptr);
            }
        }

        for (;;)
        {
            const int callbackAnimation = m_currentAnimation;
            VID* const callbackVid = m_vid;
            const int functionIndex = callbackVid->scriptFunctionAt(callbackAnimation);
            if (functionIndex < 0 ||
                (m_currentFrame != m_currentFrameBegin &&
                 (callbackVid->properties() & P_TRACK) == 0u))
            {
                break;
            }

            const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            if (core::Application::callScriptFunction(functionIndex, spriteArg, 0) != 0)
                return;
            if (callbackAnimation == m_currentAnimation)
                break;
        }

        VID* const postCallbackVid = m_vid;
        const int postCallbackAnimation = m_currentAnimation;
        const int sfx = postCallbackVid->sfxForAnimation(postCallbackAnimation);
        if (sfx != 0)
        {
            const bool firstSfx = (m_runtimeFlags & 0x00000200u) == 0u;
            const bool repeatSfx = !firstSfx &&
                sound::GlobalSoundEngine()->passesSfxRepeatGate(sfx);
            if (firstSfx || repeatSfx)
            {
                m_runtimeFlags |= 0x00000200u;
                playSfxAtWorldPosition(sfx);
            }
        }

        VID* const postCallbackChildVid =
            postCallbackVid->childVid[postCallbackAnimation];

        // Frame-bound child creation route.  Dynamic (P_80) children were
        // handled by the cadence path above and do not use this trigger.
        if (postCallbackChildVid &&
            (postCallbackChildVid->properties() & P_BIRTHASSMOKE) == 0u)
        {
            bool createChild = false;
            if (postCallbackAnimation == 8 &&
                (postCallbackVid->weaponFlags() & 0x10) != 0)
            {
                const std::int32_t midpointNumerator = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(m_currentFrameEnd) +
                    static_cast<std::uint32_t>(m_currentFrameBegin) + 1u);
                const int midpoint = midpointNumerator >= 0
                    ? midpointNumerator / 2
                    : -static_cast<int>(
                        (0u - static_cast<std::uint32_t>(midpointNumerator)) / 2u);
                if (m_currentFrame == midpoint)
                    createChild = true;
                else
                {
                    const int alternateFrame =
                        (postCallbackVid->properties() & P_CREATECHILDEND) != 0u
                            ? m_currentFrameEnd
                            : m_currentFrameBegin;
                    createChild = m_currentFrame == alternateFrame;
                }
            }
            else
            {
                if ((postCallbackVid->properties() & P_TRACK) != 0u)
                {
                    createChild = true;
                }
                else
                {
                    const int triggerFrame =
                        (postCallbackVid->properties() & P_CREATECHILDEND) != 0u
                            ? m_currentFrameEnd
                            : m_currentFrameBegin;
                    createChild = m_currentFrame == triggerFrame;
                }
            }

            if (createChild)
                spawnAnimationChild();
        }

        m_currentFrame = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(m_currentFrame) + 1u);

        // Animations 15/16 are terminal owners.  The virtual action call is
        // preserved before the scalar deleting-destructor route.
        if (m_currentAnimation >= 15 &&
            (m_currentFrame > m_currentFrameEnd ||
             m_vid->declaredAnimationFrameCount(m_currentAnimation) == 0))
        {
            m_currentFrame = m_currentFrameEnd;
            dispatchVirtualAction(15u, 0, 0, 0);
            DeleteSpriteThroughVirtualDeletingDestructor(this);
            return;
        }

        const int actionMask = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        if (actionMask == 0x44 || actionMask == 0x48)
        {
            if (m_currentAnimation < 15 && m_currentAnimation != 10 &&
                m_currentFrame > m_currentFrameEnd)
            {
                if (m_speed == 0.0f)
                {
                    if (m_currentAnimation == 2 || m_currentAnimation >= 7)
                        ChangeAnimation(0);
                }
                else if (m_currentAnimation != 2)
                {
                    ChangeAnimation(2);
                }
            }
        }
        else if (m_currentFrame > m_currentFrameEnd ||
                 (m_vid->properties() & P_TRACK) != 0u)
        {
            const std::size_t commandCount = m_commandStack.size();
            if (commandCount == 0 || actionMask != 0)
            {
                dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);
            }
            else
            {
                const SpriteCommandStack::CommandRecordStorage command =
                    m_commandStack.m_commandRecords.records[commandCount - 1];
                const int opcode = static_cast<int>(command.words[0]);

                if (opcode != static_cast<int>(ActionCode::ACT_STOP_STACK))
                {
                    const bool resetTransientAnimation =
                        (m_currentAnimation < 15 && m_currentAnimation >= 7 && m_currentAnimation != 10) ||
                        (m_currentAnimation == 2 && m_speed == 0.0f);
                    if (resetTransientAnimation && opcode >= 17)
                        ChangeAnimation(0);

                    m_commandStack.setCommandRecordCount(
                        static_cast<std::uint32_t>(commandCount - 1));

                    const int argument1 = static_cast<int>(command.words[1]);
                    const int argument2 = static_cast<int>(command.words[2]);
                    const int argument3 = static_cast<int>(command.words[3]);
                    if (opcode < 17)
                    {
                        ChangeAnimation(opcode);
                    }
                    else
                    {
                        dispatchVirtualAction(static_cast<std::uint32_t>(opcode),
                                              argument1, argument2, argument3);
                    }
                }
            }
        }

        if (m_actionAuxState)
        {
            std::uint32_t& terminalTimer = m_actionAuxState->lifetimeRemaining;
            if (terminalTimer != 999999u && m_currentAnimation < 15)
            {
                if (now - m_applicationBucketTime >= terminalTimer)
                    ChangeAnimation(15);
                else
                    terminalTimer = terminalTimer + m_applicationBucketTime - now;
            }

            const std::uint32_t refreshInterval = static_cast<std::uint32_t>(
                m_vid->weaponIntAt(static_cast<int>(VID::WeaponFieldOffset::EffectRefreshInterval)));
            if (refreshInterval != 999999u &&
                now - m_actionAuxState->effectTimestamp > refreshInterval)
            {
                m_actionAuxState->effectTimestamp = now;
            }
        }

        m_applicationBucketTime = now;
        if (m_currentFrame > m_currentFrameEnd)
            m_currentFrame = m_currentFrameBegin;
    }

    void PRIMITIVE::Tact()
    {
        (void)advancePrimitiveFrame();
    }

    void PRIMITIVE::MoveTact()
    {
    }

    void PRIMITIVE::DeletePointerToSprite(SPRITE*)
    {
    }

    void PRIMITIVE::DrawDebugOverlay()
    {
    }

    int SPRITE::advancePrimitiveFrame() noexcept
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        int result = static_cast<int>(now);
        const std::uint32_t frameInterval =
            static_cast<std::uint32_t>(m_vid->defaultFrameSpeed());

        if (now - m_applicationBucketTime >= frameInterval)
        {
            m_applicationBucketTime = now;
            m_currentFrame = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(m_currentFrame) + 1u);
            result = m_currentFrame;
            if (m_currentFrame > m_currentFrameEnd)
            {
                m_currentFrame = m_currentFrameBegin;
                result = m_currentFrame;
            }
        }
        return result;
    }

    int SPRITE::computeAttackDecisionCode(std::uint32_t deltaMs) noexcept
    {
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == m_vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                int result = child->computeAttackDecisionCode(deltaMs);
                if (result == 5 && m_goalSprite != nullptr)
                    result = 6;
                return result;
            }
        }

        if (m_vid->hasWeaponChildDescriptor() == 0u || m_vid->weaponCount() == 0u)
            return 8;

        SPRITE* const target = m_goalSprite;
        if (!target)
        {
            if (SPRITE* const uplink = m_childBacklink)
            {
                if (uplink->Vid()->spriteClassId() != 7 && m_actionTimer == 0u)
                    RotateTact(uplink->directionIndex(), deltaMs);
            }
            return 5;
        }

        auto directionToTarget = [this, target]() noexcept -> int
        {
            return computeDirectionToTarget(
                target->m_xyz.x, target->m_xyz.y, m_xyz.x, m_xyz.y);
        };
        auto metricToTarget = [this, target]() noexcept -> float
        {
            return nearDistanceToRetail(target);
        };

        const std::uint32_t waitTimer = m_actionTimer;
        if (waitTimer > 5000u || (m_currentAnimation == 8 && m_currentFrame <= m_currentFrameEnd))
        {
            RotateTact(directionToTarget(), deltaMs);
            return 4;
        }

        const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
        if (commandBits != 0x14u && commandBits != 0x0Cu && commandBits != 0x10u)
        {
            if (SPRITE* const uplink = m_childBacklink)
            {
                if (uplink->Vid()->spriteClassId() != 7 && waitTimer == 0u)
                    RotateTact(uplink->directionIndex(), deltaMs);
            }
            return 6;
        }

        const float battleRange = m_vid->weaponBattleRange();
        const float minimumRange = m_vid->weaponMinimumRange();
        const float metric = metricToTarget();
        SPRITE* const uplink = m_childBacklink;
        const bool uplinkByBattleRange = x87LessEqualOrUnordered(battleRange, metric);
        const bool uplinkByMinimumRange = x87LessEqualOrUnordered(metric, minimumRange);
        if (uplink && commandBits != 0x14u &&
            (uplinkByBattleRange || uplinkByMinimumRange))
        {
            if (uplink->Vid()->spriteClassId() != 7)
                RotateTact(uplink->directionIndex(), deltaMs);
        }
        else
        {
            const int rotateResult = RotateTact(ANGLE(static_cast<unsigned char>(directionToTarget())), deltaMs).Int();
            if (rotateResult == 0 || (m_vid->weaponFlags() & 1) != 0)
            {
                if (x87LessEqualOrUnordered(metric, battleRange))
                {
                    const std::int32_t scale = static_cast<std::int32_t>(
                        m_vid->fightNoChildValue());
                    const int ammo = dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                    if (ammo < spriteAbs32Wrap(scale))
                        return 7;

                    dispatchVirtualAction(ActionCode::ACT_ADD_AMMO, spriteNeg32Wrap(scale), 0, 0);
                    ChangeAnimation(8);
                    m_actionTimer = static_cast<std::uint32_t>(
                        static_cast<std::uint32_t>(m_vid->weaponIntAt(0x20)) +
                        5000u);
                    return 0;
                }

                if (commandBits == 0x0Cu)
                    return 1;
                const float detectRange = m_vid->weaponDetectRange();
                return x87LessEqualOrUnordered(detectRange + detectRange, metric) ? 3 : 2;
            }
        }

        const float postMetric = metricToTarget();
        if (x87LessEqualOrUnordered(postMetric, battleRange))
            return 4;
        if (commandBits == 0x0Cu)
            return 1;
        const float detectRange = m_vid->weaponDetectRange();
        return x87LessEqualOrUnordered(detectRange + detectRange, postMetric) ? 3 : 2;
    }

    int SPRITE::healthRatio255() noexcept
    {
        if (SPRITE* const child = m_childChain)
        {
            VID* const thisVid = m_vid;
            VID* const childVid = child->m_vid;
            if (childVid == thisVid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                const int bucket = child->armyIndex();
                const int duration = childVid->animationFrameDuration(bucket);
                if (duration != 0)
                {
                    const std::uint32_t rawNumerator =
                        (static_cast<std::uint32_t>(child->m_animationFrameTime) << 8) -
                        static_cast<std::uint32_t>(child->m_animationFrameTime);
                    const std::int32_t numerator = static_cast<std::int32_t>(rawNumerator);
                    return numerator / duration;
                }
            }
        }

        VID* const vid = m_vid;
        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);
        if (duration == 0)
            return 0;

        const std::uint32_t rawNumerator =
            (static_cast<std::uint32_t>(m_animationFrameTime) << 8) -
            static_cast<std::uint32_t>(m_animationFrameTime);
        const std::int32_t numerator = static_cast<std::int32_t>(rawNumerator);
        return numerator / duration;
    }

    void SPRITE::Stop()
    {
        const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
        if (commandBits == 0u || commandBits == 4u)
        {
            if (commandBits == 0x48u)
                m_actionTimer = 0u;

            SPRITE* const goal = m_goalSprite;
            if (goal)
            {
                (void)goal->ReleaseListReference();
                m_goalSprite = nullptr;
            }

#if defined(_MSC_VER) && defined(_M_IX86)
            _ReadWriteBarrier();
#endif
            m_runtimeFlags &= ~SPRITE::CommandBitsMask;
        }

        VID* const vid = m_vid;
#if defined(_MSC_VER) && defined(_M_IX86)
        _ReadWriteBarrier();
#endif
        m_runtimeFlags &= ~(MovementStartedFlag | CrossedGoalAxesMask);
        m_zSpeed = 0.0f;

        if (vid->slowValue() == 999999.0f)
            m_speed = 0.0f;
    }

    int SPRITE::traceMovementCollisionTo(float* xOut, float* yOut, float* zOut) noexcept
    {
        return GlobalSpriteHashMap()->traceMovementCollision(
            *mapOwner(), m_vid,
            m_xyz.x, m_xyz.y, m_xyz.z,
            xOut, yOut, zOut) ? 1 : 0;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    int SPRITE::StartMove() noexcept
    {
        VID* const vid = m_vid;
        const float runtimeMaxSpeed = m_actionAuxState
            ? spriteFloatFromBits(m_actionAuxState->maxSpeedBits)
            : vid->maxSpeedValue();
        if (runtimeMaxSpeed == 0.0f)
            return 0;

        SPRITE* const target = m_goalSprite;
        int projectedLength = 0;

        if (target)
        {
            if (m_xyz.x == target->m_xyz.x && m_xyz.y == target->m_xyz.y)
                return 0;

            if (vid->directionCount() == 1 || (vid->properties() & P_RANDBIRTH) != 0u)
            {
                const int dy = static_cast<int>(target->m_xyz.y - m_xyz.y);
                const int dx = static_cast<int>(target->m_xyz.x - m_xyz.x);
                const int direction = AngleFromXY(dx, dy, &projectedLength).Int();
                ChangeDirection(direction);
            }

            if ((vid->spriteTypeId() & 0x00000200u) == 0u && vid->spriteClassId() != B_CANNON)
            {
                if (target->m_xyz.z > m_xyz.z)
                    m_zSpeed = vid->maximumZSpeed();
                else if (target->m_xyz.z < m_xyz.z)
                    m_zSpeed = -vid->maximumZSpeed();
                else
                    m_zSpeed = 0.0f;
            }
            else
            {
                if (projectedLength == 0)
                {
                    const int dy = static_cast<int>(target->m_xyz.y - m_xyz.y);
                    const int dx = static_cast<int>(target->m_xyz.x - m_xyz.x);
                    (void)AngleFromXY(dx, dy, &projectedLength);
                    if (projectedLength == 0)
                        return 0;
                }

                m_zSpeed = vid->calculateMoveUpZ(
                    target->m_xyz.z - m_xyz.z, static_cast<float>(projectedLength));
            }
        }

        m_runtimeFlags = (m_runtimeFlags & ~CrossedGoalAxesMask) | MovementStartedFlag;

        if (vid->accelerationValue() == 999999.0f)
            m_speed = runtimeMaxSpeed;

        return 1;
    }


    void SPRITE::releaseActionAuxState() noexcept
    {
        if (m_actionAuxState)
        {
            ActionAuxState::ItemList& list = m_actionAuxState->items;
            list.vtableTag = currentCommandWordListVtable();
            if (list.values)
                ::operator delete(list.values);
            list.values = nullptr;
            list.count = 0u;
            ::operator delete(m_actionAuxState);
            m_actionAuxState = nullptr;
        }
#if UINTPTR_MAX != 0xFFFFFFFFu
        hostState().actionAuxCommandMask = {0, 0};
#endif
    }

    void SPRITE::releaseBestTargetSprite() noexcept
    {
        SPRITE* target = m_bestTargetSprite;
        if (!target)
            return;

        const int nextRef = target->m_listReferenceCount - 1;
        target->m_listReferenceCount = nextRef;
        if (nextRef < 0)
        {
            const int targetNvid = target->m_vid ? target->m_vid->nVid : -1;
            LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, targetNvid);
        }
        else if (nextRef == 0)
        {
            DeleteSpriteThroughVirtualDeletingDestructor(target);
        }

        m_bestTargetSprite = nullptr;
    }

    void SPRITE::deleteChildChain() noexcept
    {
        while (SPRITE* child = m_childChain)
            DeleteSpriteThroughVirtualDeletingDestructor(child);
    }

    size_t SPRITE::childChainCount() const noexcept
    {
        size_t count = 0;
        const SPRITE* node = childChain();
        while (node)
        {
            ++count;
            node = node->childChain();
        }
        return count;
    }

    size_t SPRITE::childChainVidCount(const VID* vid) const noexcept
    {
        if (!vid)
            return 0;
        size_t count = 0;
        const SPRITE* node = childChain();
        while (node)
        {
            if (node->Vid() == vid)
                ++count;
            node = node->childChain();
        }
        return count;
    }

    bool SPRITE::childChainContainsVid(const VID* vid) const noexcept
    {
        return childChainVidCount(vid) != 0;
    }

    size_t SPRITE::childChainApplicationBucketCandidateCount() const noexcept
    {
        size_t count = 0;
        const SPRITE* node = childChain();
        while (node)
        {
            const VID* vid = node->Vid();
            const int layer = vid ? vid->renderLayer() : -1;
            if (layer >= 0 && layer < core::ApplicationDrawDispatcherState::PassCount)
                ++count;
            node = node->childChain();
        }
        return count;
    }

    void SPRITE::clearChildBacklink() noexcept
    {
        if (SPRITE* backlink = m_childBacklink)
            backlink->m_childChain = nullptr;
    }

    void SPRITE::releaseCommandRecordsRetailTail() noexcept
    {
        m_commandStack.releaseCommandRecordsRetailTail();
    }

    void SPRITE::DrawDebugOverlay()
    {
        drawBaseDebugOverlay();
    }

    void SPRITE::drawBaseDebugOverlay()
    {
        GRAPH* const graph = GRAPH::CurrentGraph();

        const float left = static_cast<float>(graph->getViewportLeft());
        float top = static_cast<float>(graph->getViewportTop());

        int bestNvid = 0;
        if (m_bestTargetSprite)
            bestNvid = m_bestTargetSprite->Vid()->nvid();

        int goalNvid = 0;
        if (m_goalSprite)
            goalNvid = m_goalSprite->Vid()->nvid();

        const int ammo = dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        graph->DrawText(left + 30.0f, top,
            "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i spd=%-3i,%-3i timer=%i moveFin=%1u%1u",
            listReferenceCount(),
            commandIndex(),
            m_currentAnimation,
            ammo,
            m_animationFrameTime,
            m_attackDecisionCode,
            goalNvid,
            bestNvid,
            spriteFmulFtolLow32(m_speed, 1000.0f),
            spriteFmulFtolLow32(m_zSpeed, 1000.0f),
            static_cast<int>(m_actionTimer),
            static_cast<unsigned>((m_runtimeFlags & CrossedGoalXFlag) != 0u),
            static_cast<unsigned>((m_runtimeFlags & CrossedGoalYFlag) != 0u));

        SPRITE* const child = m_childChain;
        if (child && child->Vid() == m_vid->linkedVid())
        {
            top += 12.0f;
            int childBestNvid = 0;
            if (child->m_bestTargetSprite)
                childBestNvid = child->m_bestTargetSprite->Vid()->nvid();
            int childGoalNvid = 0;
            if (child->m_goalSprite)
                childGoalNvid = child->m_goalSprite->Vid()->nvid();
            const int childAmmo = child->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
            graph->DrawText(left + 30.0f, top,
                "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i spd=%-3i,%-3i timer=%i moveFin=%1u%1u",
                child->listReferenceCount(),
                child->commandIndex(),
                child->m_currentAnimation,
                childAmmo,
                child->m_animationFrameTime,
                child->m_attackDecisionCode,
                childGoalNvid,
                childBestNvid,
                spriteFmulFtolLow32(child->m_speed, 1000.0f),
                spriteFmulFtolLow32(child->m_zSpeed, 1000.0f),
                static_cast<int>(child->m_actionTimer),
                static_cast<unsigned>((child->m_runtimeFlags & CrossedGoalXFlag) != 0u),
                static_cast<unsigned>((child->m_runtimeFlags & CrossedGoalYFlag) != 0u));
        }

        const std::uint32_t commandCount = m_commandStack.m_commandRecords.count;
        if (commandCount != 0u)
        {
            top += 12.0f;
            STRING commandText = STRING::Format("%i - ", static_cast<int>(commandCount));
            const auto* const records = m_commandStack.m_commandRecords.records;
            for (std::uint32_t index = 0; index < commandCount; ++index)
            {
                const std::uint32_t* const words = records[index].words;
                const STRING item = STRING::Format("%i(%i,%i,%i) ",
                    static_cast<int>(words[0]), static_cast<int>(words[1]),
                    static_cast<int>(words[2]), static_cast<int>(words[3]));
                commandText.Append(item);
            }
            graph->drawStringColored(left + 30.0f, top, commandText, 0xFFFFFFFFu);
        }

        DrawRelationDebugOverlay();
    }

    void SPRITE::DrawRelationDebugOverlay()
    {
        GRAPH* const graph = GRAPH::CurrentGraph();

        const auto& drawState = core::GlobalApplicationDrawDispatcherState();
        const auto screenX = [&drawState](const SPRITE* sprite) -> float
        {
            return sprite->X() - drawState.cameraShiftX();
        };
        const auto screenY = [&drawState](const SPRITE* sprite) -> float
        {
            return sprite->Y() - sprite->Z() - drawState.cameraShiftY();
        };

        if (m_goalSprite)
        {
            graph->DrawLine(screenX(this), screenY(this),
                            screenX(m_goalSprite), screenY(m_goalSprite),
                            0xFF00FF00u);
        }

        SPRITE* const child = m_childChain;
        if (child && child->m_goalSprite)
        {
            graph->DrawLine(screenX(child), screenY(child),
                            screenX(child->m_goalSprite), screenY(child->m_goalSprite),
                            0xFFFF0000u);
        }
    }

    int SPRITE::removeFromDrawBucketsRecursive()
    {
        if (SPRITE* child = childChain())
            child->removeFromDrawBucketsRecursive();
        return as1::core::Application::removeSpriteFromDrawBucket(as1::core::GlobalApplicationDrawDispatcherState(), this);
    }

    char* SPRITE::addToDrawBucketsRecursive()
    {
        if (SPRITE* child = childChain())
            child->addToDrawBucketsRecursive();
        return as1::core::Application::appendSpriteToDrawBucketAndReleaseListReference(as1::core::GlobalApplicationDrawDispatcherState(), this);
    }

    unsigned int SPRITE::serializeSpriteRecord(RESOURCE* resource) noexcept
    {
        const DWORD flags = m_runtimeFlags;
        if ((flags & 0x00000100u) != 0u)
            return flags;

        const std::uint32_t armyBits = (flags >> ArmyBitsShift) & ArmyValueMask;
        const std::uint32_t rawSprite = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
        resource->write(&rawSprite, 4u);
        resource->write(&m_vid->nVid, 4u);
        resource->write(&m_xyz.x, 4u);
        resource->write(&m_xyz.y, 4u);
        resource->write(&m_xyz.z, 4u);

        const int direction = m_direction.Int();
        resource->write(&direction, 4u);
        return static_cast<unsigned int>(resource->write(&armyBits, 4u));
    }

    namespace
    {
        int signedHalfTowardZero(int value) noexcept
        {
            const int sign = value < 0 ? -1 : 0;
            return (value - sign) >> 1;
        }

        int rescaleAnimationFrameTime(int frameTime, int oldDuration, int nextDuration) noexcept
        {
            const std::int32_t numerator = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(frameTime) << 8u);
            const std::int32_t quotient = numerator / oldDuration;
            const std::int32_t product = spriteImul32Low(quotient, nextDuration);
            const std::int32_t sign = product < 0 ? -1 : 0;
            const std::int32_t remainderBias = sign & 0xFF;
            const std::int32_t biased = spriteAdd32Wrap(product, remainderBias);
            return biased >> 8;
        }
    }

    void SPRITE::suppressDrawRecursive() noexcept
    {
        for (SPRITE* node = this; node; node = node->m_childChain)
            node->m_runtimeFlags |= DrawSuppressedFlag;
    }

    void SPRITE::restoreDrawRecursive() noexcept
    {
        for (SPRITE* node = this; node; node = node->m_childChain)
            node->m_runtimeFlags &= ~DrawSuppressedFlag;
    }

    void SPRITE::updateAnimationFrameTime(int frameTime) noexcept
    {
        VID* const vid = m_vid;
        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);

        if (frameTime <= 0 && vid->maxHp != 0)
        {
            if (m_currentAnimation >= 15)
                return;

            vid->incrementKilledUnitCountForArmy(bucket);
            const int remaining = m_animationFrameTime - frameTime;
            if (remaining > duration && vid->hasDeath2ChildVid() != 0)
                ChangeAnimation(16);
            else
                ChangeAnimation(15);

            return;
        }

        const int half = signedHalfTowardZero(duration);
        if (frameTime > half && m_animationFrameTime <= half)
        {
            VID* const deleteVid = vid->woundChildVid();
            (void)deleteChildByVid(deleteVid);
        }

        if (frameTime <= half && m_animationFrameTime > half)
            ChangeAnimation(13);

        m_animationFrameTime = frameTime;
    }

    int SPRITE::changeArmyBucket(int bucketIndex) noexcept
    {
        const int oldBucket = armyIndex();
        const int nextBucket = bucketIndex & static_cast<int>(ArmyValueMask);
        m_runtimeFlags = (m_runtimeFlags & ~ArmyBitsMask) | (static_cast<DWORD>(nextBucket) << ArmyBitsShift);

        if (SPRITE* const child = m_childChain)
            (void)child->changeArmyBucket(nextBucket);

        VID* const vid = m_vid;
        const int nextDuration = vid->animationFrameDuration(nextBucket);
        const int oldDuration = vid->animationFrameDuration(oldBucket);
        if (nextDuration != oldDuration)
            updateAnimationFrameTime(rescaleAnimationFrameTime(m_animationFrameTime, oldDuration, nextDuration));

        if ((vid->properties() & P_INVISIBLEFORENEMY) != 0)
        {
            if (nextBucket == static_cast<int>(core::ActivePlayerIndex()))
            {
                m_runtimeFlags &= ~DrawSuppressedFlag;
                if (SPRITE* const child = m_childChain)
                    child->restoreDrawRecursive();
            }
            else
            {
                m_runtimeFlags |= DrawSuppressedFlag;
                if (SPRITE* const child = m_childChain)
                    child->suppressDrawRecursive();
            }
        }

        if (vid->spriteCountForArmy(oldBucket) != 0u)
            vid->decrementSpriteCountForArmy(oldBucket);
        vid->setLastSpriteCountChangeTimestamp(core::RealTimeMilliseconds());
        vid->incrementSpriteCountForArmy(nextBucket);
        return nextBucket;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    void SPRITE::ensureLinkedVidChild() noexcept
    {
        VID* const vid = m_vid;
        VID* const linkVid = vid->linkedVid();
        if (!linkVid)
            return;

        if (SPRITE* child = childChain())
        {
            if (child->Vid() == linkVid)
                return;
        }

        if (linkVid->isNotCreateAsChild())
            return;

        const int direction = directionIndex();
        const int directionByte = direction & 0xFF;
        const VECTOR& link = vid->linkOffset();
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        if ((linkVid->properties() & P_NOTCHANGELINKERCOOR) != 0u)
        {
            offsetX = link.x;
            offsetY = -link.y;
        }
        else
        {
            offsetX = (directionCos(directionByte) * link.x) +
                      (directionSin(directionByte) * link.y);
            offsetY = (directionSinAux(directionByte) * link.x) -
                      (directionCosAux(directionByte) * link.y);
        }
        const VECTOR target(m_xyz.x + offsetX, m_xyz.y + offsetY, m_xyz.z + link.z);

#if defined(_WIN32) && UINTPTR_MAX == 0xFFFFFFFFu
        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
        MAP* const map = mapOwner();
#endif
        SPRITE* const created = map->CreateSpriteViaFactory(
            linkVid, target, ANGLE(direction), nullptr, false);
        insertChildChainHead(created);

        if (SPRITE* child = childChain())
        {
            child->changeArmyBucket(armyIndex());
            return;
        }

        LOG::ResourceError("SPRITE %i", 3, "link", 0, vid->nVid);
    }

    int SPRITE::spawnAnimationChild() noexcept
    {
        const int animationSlot = m_currentAnimation;

        VID* const childVid = m_vid->childVid[animationSlot];
        if (!childVid)
            return 0;
        if (childVid->isNotCreateAsChild() != 0)
            return 0;

        CreateChildForAnimation();
        return m_currentAnimation;
    }

    int SPRITE::hasLinkedVidChild() const noexcept
    {
        SPRITE* const child = childChain();
        return child && child->Vid() == Vid()->linkedVid() ? 1 : 0;
    }

    int SPRITE::canWeaponAffectTarget(SPRITE* owner) noexcept
    {
        if (!owner)
            return 0;

        VID* const vid = m_vid;
        const std::uint32_t ownerType = owner->m_vid->spriteTypeId();

        if (vid->hasWeaponChildDescriptor() != 0u &&
            vid->weaponCount() != 0u &&
            (static_cast<std::uint32_t>(vid->weaponTypeMask()) & ownerType) != 0u)
        {
            return 1;
        }

        SPRITE* const child = m_childChain;
        if (!child)
            return 0;

        VID* const childVid = child->m_vid;
        VID* const linkVid = vid->linkedVid();
        if (childVid != linkVid)
            return 0;
        if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
            return 0;

        return (static_cast<std::uint32_t>(childVid->weaponTypeMask()) & ownerType) != 0u ? 1 : 0;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    __declspec(safebuffers)
#endif
    int SPRITE::setAttackCommandForTarget(SPRITE* owner) noexcept
    {
        VID* const vid = m_vid;
        const int spriteClass = static_cast<int>(vid->spriteClassId());

        if (spriteClass == B_LINKER && vid->hasWeaponChildDescriptor() != 0u)
        {
            const int savedAnimation = m_currentAnimation;
            m_currentAnimation = 8;
            SetCommand(4, owner);

            CreateChildForAnimation();
            m_currentAnimation = savedAnimation;

            if ((m_runtimeFlags & CommandBitsMask) == 0x48u)
                m_actionTimer = 0u;

            if (m_goalSprite)
            {
                m_goalSprite->ReleaseListReference();
                m_goalSprite = nullptr;
            }

            if (SPRITE* const child = m_childChain)
            {
                VID* const childVid = child->m_vid;
                if (childVid == vid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u)
                {
                    child->SetCommand(0, nullptr);
                }
            }

            m_runtimeFlags &= ~CommandBitsMask;
            return 0;
        }

        if (owner &&
            owner->m_vid != MAP::NullVid() &&
            canWeaponAffectTarget(owner) == 0 &&
            spriteClass != B_CANNON &&
            dispatchVirtualAction(ActionCode::ACT_IS_TRAIN, 0, 0, 0) == 0)
        {
            return 0;
        }

        if ((m_runtimeFlags & CommandBitsMask) == 0x48u)
            m_actionTimer = 0u;

        if (m_goalSprite != owner)
        {
            if (m_goalSprite)
                m_goalSprite->ReleaseListReference();

            m_goalSprite = owner;
            if (owner)
                ++owner->m_listReferenceCount;
        }

        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                child->SetCommand(3, owner);
            }
        }

        if (!m_goalSprite)
        {
            m_runtimeFlags &= ~CommandBitsMask;
            return 0;
        }

        m_runtimeFlags = (m_runtimeFlags & ~CommandBitsMask) | 0x0Cu;
        return 0;
    }


    int SPRITE::sumLinkedChildContributions() noexcept
    {
        int result = 0;
        for (SPRITE* node = this; node; node = node->childChain())
        {
            VID* const nodeVid = node->Vid();
            VID* const metricVid = nodeVid->fightChildVid();
            if (metricVid && nodeVid->weaponCount() != 0u)
                result += metricVid->calculateLinkedContribution();
        }
        return result;
    }

    SPRITE* SPRITE::engineChainHead() noexcept
    {
        SPRITE* result = this;
        for (SPRITE* node = engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
            result = node;
        return result;
    }

    bool SPRITE::isInEngineChain(SPRITE* target) noexcept
    {
        if (!target)
            return false;

        for (SPRITE* node = this; node; node = node->engineChainNextRef())
        {
            if (node == target)
                return true;
        }
        for (SPRITE* node = engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
        {
            if (node == target)
                return true;
        }
        return false;
    }

    int SPRITE::minimumEngineWeaponRange() noexcept
    {
        if (!goalSprite())
            return 0;

        const std::uint32_t actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
        if (actionBits != 0x70u && actionBits != 0x74u)
            return 0;

        const auto candidateHeight = [](SPRITE* node) noexcept -> float
        {
            SPRITE* const child = node->childChain();
            if (!child)
                return 10000.0f;

            VID* const nodeVid = node->Vid();
            VID* const childVid = child->Vid();
            if (childVid != nodeVid->linkedVid())
                return 10000.0f;
            if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
                return 10000.0f;
            if (node->ammoCount() <= 0)
                return 10000.0f;

            VID* valueOwner = childVid;
            if (nodeVid->nvid() == 35)
                valueOwner = nodeVid;

            const float value = valueOwner->weaponBattleRange();

            return x87LessOrUnordered(value, 10000.0f) ? value : 10000.0f;
        };

        float result = 10000.0f;
        if (SPRITE* const refOwner = engineCommandReferenceOwnerRef())
        {
            result = candidateHeight(refOwner);
        }
        else
        {
            for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
            {
                const float value = candidateHeight(node);
                if (result > value)
                    result = value;
            }
        }

        if (x87EqualOrUnordered(result, 10000.0f))
            result = 0.0f;

        return spriteFtolLow32(static_cast<long double>(result));
    }

    int SPRITE::updateSecondaryPathPosition(core::PathPosition* pathPair) noexcept
    {
        using PathNode = core::WeakController;
        using PathEdge = core::WeakController::Link;

        const int primaryProgress = primaryPathProgressRef();
        VID* const vid = Vid();
        const float radiusFloat = vid->weaponRadius();
        const int radiusLimit = spriteFtolLow32(static_cast<long double>(radiusFloat));

        auto edgeAt = [](PathNode* node, int index) noexcept -> PathEdge&
        {
            return *node->linkAt(index);
        };

        if (!spriteFildIntLessEqualOrUnordered(primaryProgress, radiusFloat))
        {
            PathEdge& edge = edgeAt(primaryPathNodeRef(), primaryPathEdgeIndexRef());
            secondaryPathNodeRef() = edge.target;
            secondaryPathProgressRef() = spriteSub32Wrap(static_cast<int>(edge.length), primaryProgress);
            secondaryPathAuxiliaryRef() = 0;
            secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
            const int result = spriteSub32Wrap(
                spriteAdd32Wrap(static_cast<int>(edge.length), radiusLimit), primaryProgress);
            secondaryPathProgressRef() = result;
            return result;
        }

        if (secondaryPathNodeRef() == primaryPathNodeRef())
        {
            const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
            secondaryPathProgressRef() = result;
            return result;
        }

        PathNode* const argumentNode = pathPair->node;
        if (argumentNode == primaryPathNodeRef())
        {
            int bridgeIndex = core::findLinkIndex(primaryPathNodeRef(), secondaryPathNodeRef());
            if (bridgeIndex < 0)
            {
                LOG::ResourceError("ENGINE %i", 10, "rail 1", 0,
                                   vid ? vid->nvid() : -1);
                unsigned char facing = 0;
                if (primaryPathNodeRef())
                    facing = static_cast<unsigned char>(edgeAt(primaryPathNodeRef(),
                                                               primaryPathEdgeIndexRef()).facing);
                bridgeIndex = core::findClosestFacingLink(primaryPathNodeRef(),
                                                static_cast<unsigned char>(facing - 128));
                secondaryPathNodeRef() = edgeAt(primaryPathNodeRef(), bridgeIndex).target;
            }

            const int duration = static_cast<int>(edgeAt(primaryPathNodeRef(), bridgeIndex).length);
            int result = 0;
            const int durationPlusPrimary = spriteAdd32Wrap(duration, primaryProgress);
            if (spriteFildIntLessEqualOrUnordered(durationPlusPrimary, radiusFloat))
            {
                result = spriteSub32Wrap(spriteSub32Wrap(radiusLimit, duration), primaryProgress);
                secondaryPathProgressRef() = result;
            }
            else if (primaryPathEdgeIndexRef() == bridgeIndex)
            {
                result = spriteSub32Wrap(spriteAdd32Wrap(duration, radiusLimit), primaryProgress);
                secondaryPathProgressRef() = result;
            }
            else
            {
                secondaryPathNodeRef() = primaryPathNodeRef();
                secondaryPathEdgeIndexRef() = bridgeIndex;
                result = spriteSub32Wrap(radiusLimit, primaryProgress);
                secondaryPathProgressRef() = result;
            }
            return result;
        }

        if (argumentNode == secondaryPathNodeRef())
        {
            const int duration = argumentNode
                ? static_cast<int>(edgeAt(argumentNode, pathPair->edgeIndex).length)
                : 0;
            const int primaryPlusDuration = spriteAdd32Wrap(primaryProgress, duration);
            if (spriteFildIntLessEqualOrUnordered(primaryPlusDuration, radiusFloat))
            {
                const int result = spriteSub32Wrap(spriteSub32Wrap(radiusLimit, primaryProgress), duration);
                secondaryPathProgressRef() = result;
                return result;
            }

            PathEdge& edge = edgeAt(argumentNode, pathPair->edgeIndex);
            secondaryPathNodeRef() = edge.target;
            secondaryPathProgressRef() = spriteSub32Wrap(
                static_cast<int>(edge.length), pathPair->progress);
            secondaryPathAuxiliaryRef() = 0;
            secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
            const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
            secondaryPathProgressRef() = result;
            return result;
        }

        const int argumentDuration = argumentNode
            ? static_cast<int>(edgeAt(argumentNode, pathPair->edgeIndex).length)
            : 0;
        const int argumentPlusPrimary = spriteAdd32Wrap(argumentDuration, primaryProgress);
        if (spriteFildIntLessEqualOrUnordered(argumentPlusPrimary, radiusFloat))
        {
            writeLogLine(g_fileLogger, "zmdots6");
            int bridgeIndex = core::findLinkIndex(argumentNode, secondaryPathNodeRef());
            secondaryPathEdgeIndexRef() = bridgeIndex;
            secondaryPathNodeRef() = argumentNode;
            const int result = spriteSub32Wrap(
                spriteSub32Wrap(radiusLimit, argumentDuration), primaryProgress);
            secondaryPathProgressRef() = result;
            if (bridgeIndex < 0)
            {
                LOG::ResourceError("ENGINE %i", 10, "rail 2", 0,
                                   vid ? vid->nvid() : -1);
                unsigned char facing = 0;
                if (argumentNode)
                    facing = static_cast<unsigned char>(edgeAt(argumentNode,
                                                               pathPair->edgeIndex).facing);
                bridgeIndex = core::findClosestFacingLink(secondaryPathNodeRef(),
                                                static_cast<unsigned char>(facing - 128));
                secondaryPathEdgeIndexRef() = bridgeIndex;
                return bridgeIndex;
            }
            return result;
        }

        writeLogLine(g_fileLogger, "zmdots5");
        PathEdge& edge = edgeAt(argumentNode, pathPair->edgeIndex);
        secondaryPathNodeRef() = edge.target;
        secondaryPathProgressRef() = spriteSub32Wrap(
            static_cast<int>(edge.length), pathPair->progress);
        secondaryPathAuxiliaryRef() = 0;
        secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
        const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
        secondaryPathProgressRef() = result;
        return result;
    }

    SPRITE* SPRITE::findEnginePathRelationSprite() noexcept
    {
        auto edgeTarget = [](core::WeakController* node, int index) noexcept -> core::WeakController*
        {
            return node ? node->linkAt(index)->target : nullptr;
        };

        SPRITE* candidate = primaryPathNodeRef()->ownerSprite();
        if (candidate && classifyEngineChainRelation(candidate, 0))
            return primaryPathNodeRef()->ownerSprite();

        core::WeakController* target = edgeTarget(primaryPathNodeRef(),
                                                   primaryPathEdgeIndexRef());
        if (target->ownerSprite() && classifyEngineChainRelation(target->ownerSprite(), 0))
            return edgeTarget(primaryPathNodeRef(),
                              primaryPathEdgeIndexRef())->ownerSprite();

        candidate = secondaryPathNodeRef()->ownerSprite();
        if (candidate && classifyEngineChainRelation(candidate, 0))
            return secondaryPathNodeRef()->ownerSprite();

        target = edgeTarget(secondaryPathNodeRef(), secondaryPathEdgeIndexRef());
        if (target->ownerSprite() && classifyEngineChainRelation(target->ownerSprite(), 0))
            return edgeTarget(secondaryPathNodeRef(),
                              secondaryPathEdgeIndexRef())->ownerSprite();
        return nullptr;
    }

    int SPRITE::classifyEngineChainRelation(SPRITE* target, int strictProgressGate) noexcept
    {
        if (!target || isInEngineChain(target))
            return 0;

        auto edgeTarget = [](core::WeakController* node, int index) noexcept -> core::WeakController*
        {
            return node ? node->links()[static_cast<std::size_t>(index)].target : nullptr;
        };
        auto edgeDuration = [](core::WeakController* node, int index) noexcept -> int
        {
            return node ? static_cast<int>(node->links()[static_cast<std::size_t>(index)].length) : 0;
        };
        auto progressPasses = [strictProgressGate](int progress, int duration, int otherProgress) noexcept -> bool
        {
            const int threshold = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(duration) -
                static_cast<std::uint32_t>(otherProgress));
            return strictProgressGate ? progress > threshold : progress >= threshold;
        };

        core::WeakController* const primary = primaryPathNodeRef();
        if (primary)
        {
            core::WeakController* const targetPrimary = target->primaryPathNodeRef();
            if (targetPrimary &&
                edgeTarget(primary, primaryPathEdgeIndexRef()) == targetPrimary &&
                edgeTarget(targetPrimary, target->primaryPathEdgeIndexRef()) == primary &&
                progressPasses(primaryPathProgressRef(),
                               edgeDuration(primary, primaryPathEdgeIndexRef()),
                               target->primaryPathProgressRef()))
            {
                return 1;
            }

            core::WeakController* const targetSecondary = target->secondaryPathNodeRef();
            if (targetSecondary &&
                edgeTarget(primary, primaryPathEdgeIndexRef()) == targetSecondary &&
                edgeTarget(targetSecondary, target->secondaryPathEdgeIndexRef()) == primary &&
                progressPasses(primaryPathProgressRef(),
                               edgeDuration(primary, primaryPathEdgeIndexRef()),
                               target->secondaryPathProgressRef()))
            {
                return 2;
            }
        }

        core::WeakController* const secondary = secondaryPathNodeRef();
        if (secondary)
        {
            core::WeakController* const targetPrimary = target->primaryPathNodeRef();
            if (targetPrimary &&
                edgeTarget(secondary, secondaryPathEdgeIndexRef()) == targetPrimary &&
                edgeTarget(targetPrimary, target->primaryPathEdgeIndexRef()) == secondary &&
                progressPasses(secondaryPathProgressRef(),
                               edgeDuration(secondary, secondaryPathEdgeIndexRef()),
                               target->primaryPathProgressRef()))
            {
                return 3;
            }

            core::WeakController* const targetSecondary = target->secondaryPathNodeRef();
            if (targetSecondary &&
                edgeTarget(secondary, secondaryPathEdgeIndexRef()) == targetSecondary &&
                edgeTarget(targetSecondary, target->secondaryPathEdgeIndexRef()) == secondary &&
                progressPasses(secondaryPathProgressRef(),
                               edgeDuration(secondary, secondaryPathEdgeIndexRef()),
                               target->secondaryPathProgressRef()))
            {
                return 4;
            }
        }

        if (!strictProgressGate)
        {
            if (primary)
            {
                if (primary == target->primaryPathNodeRef())
                    return 100;
                if (primary == target->secondaryPathNodeRef())
                    return 200;
            }
            if (secondary)
            {
                if (secondary == target->primaryPathNodeRef())
                    return 300;
                if (secondary == target->secondaryPathNodeRef())
                    return 400;
            }
            return 0;
        }

        if (primary)
        {
            if (primary == target->primaryPathNodeRef())
                return primaryPathEdgeIndexRef() == target->primaryPathEdgeIndexRef() ? 2 : 1;
            if (primary == target->secondaryPathNodeRef())
                return 200;
        }
        if (secondary)
        {
            if (secondary == target->primaryPathNodeRef())
                return 300;
            if (secondary == target->secondaryPathNodeRef())
                return 400;
        }
        return 0;
    }

    SPRITE* SPRITE::engineChainTail() noexcept
    {
        SPRITE* result = this;
        for (SPRITE* node = engineChainNextRef(); node; node = node->engineChainNextRef())
            result = node;
        return result;
    }

    SPRITE* SPRITE::reverseEngineChain() noexcept
    {
        SPRITE* const head = engineChainHead();
        for (SPRITE* node = head; node; )
        {
            SPRITE* const oldNext = node->engineChainNextRef();
            node->engineChainNextRef() = node->engineChainPreviousRef();
            node->engineChainPreviousRef() = oldNext;

            std::swap(node->primaryPathNodeRef(), node->secondaryPathNodeRef());
            std::swap(node->primaryPathProgressRef(), node->secondaryPathProgressRef());
            std::swap(node->primaryPathAuxiliaryRef(), node->secondaryPathAuxiliaryRef());
            std::swap(node->primaryPathEdgeIndexRef(), node->secondaryPathEdgeIndexRef());
            node->setDerivedStateValue(0, node->derivedStateValue(0) ^ 1);

            if (!oldNext)
            {
                if (node == head)
                {
                    node->engineTargetSpeedRef() = -head->engineTargetSpeedRef();
                }
                else
                {
                    node->m_runtimeFlags = (node->m_runtimeFlags & ~MovementStartedFlag) | (head->m_runtimeFlags & MovementStartedFlag);
                    node->m_speed = head->m_speed;
                    node->engineTargetSpeedRef() = -head->engineTargetSpeedRef();
                    node->engineAccelerationDelayRef() = head->engineAccelerationDelayRef();
                    const int pathByteCount = head->pathBufferSizeRef();
                    std::memcpy(node->pathBufferData(),
                                head->pathBufferData(),
                                static_cast<std::size_t>(pathByteCount));
                    node->pathBufferSizeRef() = pathByteCount;
                    head->pathBufferSizeRef() = 0;
                }
            }
            node = oldNext;
        }

        return head;
    }

    int SPRITE::attachEngineChain(SPRITE* target) noexcept
    {
        if (!target || isInEngineChain(target))
            return 1;

        auto edgeAt = [](core::WeakController* node, int index) noexcept -> core::WeakController::Link&
        {
            return *node->linkAt(index);
        };
        auto distance3 = [this](core::WeakController* node) noexcept -> double
        {
            const double dx = static_cast<double>(node->x()) - static_cast<double>(m_xyz.x);
            const double dy = static_cast<double>(node->y()) - static_cast<double>(m_xyz.y);
            const double dz = static_cast<double>(node->id()) - static_cast<double>(m_xyz.z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };

        if (!target->engineChainPreviousRef() && !target->engineChainNextRef())
        {
            core::WeakController* const primaryEnd =
                edgeAt(target->primaryPathNodeRef(),
                       target->primaryPathEdgeIndexRef()).target;
            core::WeakController* const secondaryEnd =
                edgeAt(target->secondaryPathNodeRef(),
                       target->secondaryPathEdgeIndexRef()).target;
            if (distance3(secondaryEnd) > distance3(primaryEnd))
                target->reverseEngineChain();
        }
        else
        {
            SPRITE* const first = target->engineChainHead();
            const double firstDx = static_cast<double>(first->m_xyz.x) - static_cast<double>(m_xyz.x);
            const double firstDy = static_cast<double>(first->m_xyz.y) - static_cast<double>(m_xyz.y);
            SPRITE* const last = target->engineChainTail();
            const double lastDx = static_cast<double>(last->m_xyz.x) - static_cast<double>(m_xyz.x);
            const double lastDy = static_cast<double>(last->m_xyz.y) - static_cast<double>(m_xyz.y);
            if (std::sqrt(lastDx * lastDx + lastDy * lastDy) >
                std::sqrt(firstDx * firstDx + firstDy * firstDy))
            {
                target->reverseEngineChain();
            }
        }

        SPRITE* const tail = target->engineChainTail();
        tail->engineChainNextRef() = this;
        engineChainPreviousRef() = tail;

        core::WeakController::Link& source =
            edgeAt(tail->secondaryPathNodeRef(), tail->secondaryPathEdgeIndexRef());
        primaryPathNodeRef() = source.target;
        primaryPathProgressRef() = static_cast<int>(source.length) - tail->secondaryPathProgressRef();
        primaryPathAuxiliaryRef() = 0;
        primaryPathEdgeIndexRef() = source.reciprocalIndex;

        const unsigned char facing = static_cast<unsigned char>(
            edgeAt(tail->secondaryPathNodeRef(), tail->secondaryPathEdgeIndexRef()).facing);
        const int secondarySourceIndex = core::findClosestFacingLink(primaryPathNodeRef(), facing);
        secondaryPathNodeRef() = edgeAt(primaryPathNodeRef(), secondarySourceIndex).target;
        secondaryPathEdgeIndexRef() = core::findClosestFacingLink(secondaryPathNodeRef(), facing);

        unsigned char currentFacing = 0;
        if (primaryPathNodeRef())
            currentFacing = static_cast<unsigned char>(
                edgeAt(primaryPathNodeRef(), primaryPathEdgeIndexRef()).facing);
        const unsigned char deltaA = static_cast<unsigned char>(directionIndex() - currentFacing);
        const unsigned char deltaB = static_cast<unsigned char>(currentFacing - directionIndex());
        const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
        if (delta > 127)
            setDerivedStateValue(0, derivedStateValue(0) | 1);

        core::PathPosition primary{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};
        updateSecondaryPathPosition(&primary);
        updatePositionFromPathEndpoints();
        return 0;
    }

    void SPRITE::clearPathNodeOwnership() noexcept
    {
        if (primaryPathNodeRef() && primaryPathNodeRef()->ownerSprite() == this)
            primaryPathNodeRef()->setOwnerSprite(nullptr);

        if (secondaryPathNodeRef() && secondaryPathNodeRef()->ownerSprite() == this)
            secondaryPathNodeRef()->setOwnerSprite(nullptr);

        core::WeakController* primary = primaryPathNodeRef();
        if (primary)
        {
            core::WeakController* const target =
                primary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;
            if (target && target->ownerSprite() == this)
                target->setOwnerSprite(nullptr);
        }

        core::WeakController* secondary = secondaryPathNodeRef();
        if (secondary)
        {
            core::WeakController* const target =
                secondary->links()[static_cast<std::size_t>(secondaryPathEdgeIndexRef())].target;
            if (target && target->ownerSprite() == this)
                target->setOwnerSprite(nullptr);
        }

    }

    core::WeakController* SPRITE::claimPathNodeOwnership() noexcept
    {
        clearPathNodeOwnership();
        if (primaryPathNodeRef())
            primaryPathNodeRef()->setOwnerSprite(this);
        core::WeakController* result = secondaryPathNodeRef();
        if (result && result != primaryPathNodeRef())
            result->setOwnerSprite(this);
        return result;
    }

    SPRITE* SPRITE::validateEngineChainLinks() noexcept
    {
        if (SPRITE* const previous = engineChainPreviousRef())
        {
            SPRITE* const actualNext = previous->engineChainNextRef();
            if (actualNext != this)
            {
                const int nvid = Vid() ? Vid()->nvid() : -1;
                LOG::ResourceError("ENGINE %i", 4, "PrevEngine->NextEngine!=this",
                                   static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actualNext))),
                                   nvid);
                previous->engineChainNextRef() = this;
            }
        }

        SPRITE* result = engineChainNextRef();
        if (result)
        {
            SPRITE* const actualPrevious = result->engineChainPreviousRef();
            if (actualPrevious != this)
            {
                const int nvid = Vid() ? Vid()->nvid() : -1;
                LOG::ResourceError("ENGINE %i", 4, "NextEngine->PrevEngine!=this",
                                   static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actualPrevious))),
                                   nvid);
                result = engineChainNextRef();
                result->engineChainPreviousRef() = this;
            }
        }
        return result;
    }

    int SPRITE::isSpriteClass(int spriteClass) const noexcept
    {
        return m_vid->spriteClassId() == static_cast<DWORD>(spriteClass) ? 1 : 0;
    }

    SPRITE::EngineChainMetrics* SPRITE::EngineChainMetrics::collectEngineChainMetrics(SPRITE* root) noexcept
    {
        categoryFlags &= 0xFFFFFFFCu;
        maxBattleRange = 0.0f;
        minBattleRange = 999999.0f;
        weapon10Sum = 0.0f;
        weapon0CSum = 0.0f;
        activeWeapon0CSum = 0.0f;
        movementDelayMs = 10000;
        spriteCount = 0;
        spriteFrameTimeSum = 0;
        vidFrameTimeSum = 0;
        routeMetricSum = 0;
        weaponMetricSum = 0;
        distanceSampleCount = 0;
        fixedDistanceSum = 0;
        distanceWeightSum = 0;
        averageDistanceRatio = 0;

        for (SPRITE* node = root; node; node = node->engineChainNext())
            accumulateEngineChainSprite(node);

        for (SPRITE* node = root->engineChainPrevious(); node; node = node->engineChainPrevious())
            accumulateEngineChainSprite(node);

        if (distanceSampleCount != 0)
            averageDistanceRatio /= distanceSampleCount;
        else
            averageDistanceRatio = 100;

        if (minBattleRange == 999999.0f)
            minBattleRange = 0.0f;

        const double denominator = static_cast<double>(weapon10Sum) -
                                   static_cast<double>(activeWeapon0CSum);

        if (!std::isnan(denominator) && denominator != 0.0)
        {
            const double numerator = denominator -
                (static_cast<double>(weapon0CSum) - static_cast<double>(activeWeapon0CSum));
            const int projected = static_cast<int>((numerator * static_cast<double>(movementDelayMs)) / denominator);
            movementDelayMs = projected;
            if (projected < 5)
                movementDelayMs = 0;
        }

        if (movementDelayMs == 10000)
            movementDelayMs = 0;
        return this;
    }

    int SPRITE::EngineChainMetrics::accumulateEngineChainSprite(SPRITE* sprite) noexcept
    {
        VID* const vid = sprite->Vid();
        const float weapon10 = vid->weaponFloatAt(0x10);
        const float weapon0C = vid->weaponFloatAt(0x0C);

        if (!x87EqualOrUnordered(weapon10, 0.0f))
        {
            const double candidateDelay = static_cast<double>(vid->maxSpeedValue()) * 1000.0;
            const double currentDelay = static_cast<double>(movementDelayMs);
            if (candidateDelay < currentDelay ||
                std::isnan(candidateDelay) || std::isnan(currentDelay))
            {
                movementDelayMs = spriteFtolLow32(static_cast<long double>(candidateDelay));
            }
        }

        weapon10Sum += weapon10;
        weapon0CSum += weapon0C;
        if (weapon10 > 0.0f)
            activeWeapon0CSum += weapon0C;

        if (vid->nvid() == 45)
            categoryFlags |= 2u;
        else
            categoryFlags |= 1u;

        spriteFrameTimeSum += sprite->animationFrameTime();

        const int fixedDistance = sprite->ammoFixedPoint() / 64;
        const int vidWeaponMetric = vid->activeWeaponAmmoCapacity();

        if (fixedDistance > 0)
        {
            float weapon18 = vid->weaponBattleRange();
            if (SPRITE* const child = sprite->childChain())
            {
                VID* const link = vid->linkedVid();
                VID* const childVid = child->Vid();
                if (childVid == link && link->hasWeaponChildDescriptor() != 0u &&
                    link->weaponCount() != 0u &&
                    x87EqualOrUnordered(weapon18, 0.0f))
                {
                    weapon18 = link->weaponBattleRange();
                }
            }

            if (weapon18 > maxBattleRange)
                maxBattleRange = weapon18;
            if (weapon18 != 0.0f && weapon18 < minBattleRange)
                minBattleRange = weapon18;
        }

        if (vidWeaponMetric != 0 && vidWeaponMetric != 999999 && vid->nvid() != 85)
        {
            ++distanceSampleCount;
            distanceWeightSum += vidWeaponMetric;
            fixedDistanceSum += fixedDistance;
            averageDistanceRatio += (100 * fixedDistance) / vidWeaponMetric;
        }

        const int bucket = sprite->armyIndex();
        vidFrameTimeSum += vid->animationFrameDuration(bucket);
        weaponMetricSum += vid->productionCost();

        int routeMetric = 0;
        if (fixedDistance != 0)
        {
            if (vid->nvid() == 82)
            {
                const core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
                VID* metricVid = MAP::NullVid();
                if (appVidTable.count() > 70)
                {
                    if (VID* const slot70 = appVidTable.slot(70))
                        metricVid = slot70;
                }
                routeMetric = metricVid->calculateLinkedContribution();
            }
            else
            {
                routeMetric = sprite->sumLinkedChildContributions();
            }
        }
        routeMetricSum += routeMetric;

        if (VID* const link = vid->linkedVid())
        {
            vidFrameTimeSum += link->animationFrameDuration(bucket);

            SPRITE* const child = sprite->childChain();
            VID* const childVid = child ? child->Vid() : nullptr;
            if (childVid != link &&
                static_cast<int>(link->spriteCountForArmy(bucket)) >=
                    static_cast<int>(vid->spriteCountForArmy(bucket)))
            {
                spriteFrameTimeSum += link->animationFrameDuration(bucket);
            }

            if (childVid == link)
            {
                weapon0CSum += childVid->weaponFloatAt(0x0C);
                if (childVid->spriteClassId() != 9u)
                    spriteFrameTimeSum += child->animationFrameTime();
                if (weapon10 > 0.0f)
                    activeWeapon0CSum += childVid->weaponFloatAt(0x0C);
            }
        }

        ++spriteCount;
        return spriteCount;
    }

    int SPRITE::EngineChainMetrics::weaponRatioScaledByEight() const noexcept
    {
        const float denominator = weapon0CSum;
        if (denominator == 0.0f || std::isnan(denominator))
            return 0;
        const long double scaled =
            (static_cast<long double>(weapon10Sum) / static_cast<long double>(denominator)) * 8.0L;
        if (!std::isfinite(scaled) ||
            scaled >= 9223372036854775808.0L || scaled < -9223372036854775808.0L)
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
        return static_cast<int>(static_cast<std::uint32_t>(converted));
    }

    SPRITE* SPRITE::findCrossingConstraintOwner() noexcept
    {
        struct RawConstraintPath
        {
            core::WeakController* node;
            int pad04;
            int edgeIndex08;
        };

        auto constraint = [](std::uint32_t rawValue) noexcept -> RawConstraintPath*
        {
            return reinterpret_cast<RawConstraintPath*>(static_cast<std::uintptr_t>(rawValue));
        };
        auto acceptableConstraintOwner = [this](RawConstraintPath* path) noexcept -> SPRITE*
        {
            SPRITE* owner = path->node->ownerSprite();
            if (owner && !owner->isInEngineChain(this))
                return owner;

            core::WeakController* const target =
                path->node->links()[static_cast<std::size_t>(path->edgeIndex08)].target;
            owner = target->ownerSprite();
            if (owner && !owner->isInEngineChain(this))
                return owner;
            return nullptr;
        };

        core::WeakController* const primary = primaryPathNodeRef();
        if (primary)
        {
            RawConstraintPath* const direct = constraint(
                primary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].crossingLinkToken);
            if (direct)
            {
                if (SPRITE* const owner = acceptableConstraintOwner(direct))
                    return owner;
            }
        }

        core::WeakController* const primaryAgain = primaryPathNodeRef();
        if (!primaryAgain)
            return nullptr;

        const core::WeakController::Link& current =
            primaryAgain->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())];
        if (!current.target)
            return nullptr;

        RawConstraintPath* const reciprocal = constraint(
            current.target->links()[static_cast<std::size_t>(current.reciprocalIndex)].crossingLinkToken);
        if (!reciprocal)
            return nullptr;
        return acceptableConstraintOwner(reciprocal);
    }

    SPRITE* SPRITE::resolvePathOwnerRelation(int* relationOut) noexcept
    {
        core::WeakController* const primary = primaryPathNodeRef();
        SPRITE* owner = primary->ownerSprite();
        if (owner)
        {
            const int relation = classifyEngineChainRelation(owner, 1);
            *relationOut = relation;
            if (relation != 0)
                return primary->ownerSprite();
        }

        core::WeakController* next = nullptr;
        core::WeakController* const currentPrimary = primaryPathNodeRef();
        if (currentPrimary)
            next = currentPrimary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;

        owner = next->ownerSprite();
        if (owner)
        {
            core::WeakController* currentNext = nullptr;
            core::WeakController* const source = primaryPathNodeRef();
            if (source)
                currentNext = source->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;

            const int relation = classifyEngineChainRelation(currentNext->ownerSprite(), 1);
            *relationOut = relation;
            if (relation != 0)
            {
                core::WeakController* const returnSource = primaryPathNodeRef();
                core::WeakController* returnNode = nullptr;
                if (returnSource)
                    returnNode = returnSource->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;
                return returnNode->ownerSprite();
            }
        }

        *relationOut = 0;
        return nullptr;
    }

    int SPRITE::canLinkEngineChain(SPRITE* target) noexcept
    {
        if (!target)
            return 0;

        const int thisAction = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        const int targetAction = static_cast<int>(target->m_runtimeFlags & SPRITE::CommandBitsMask);

        if (target->isInEngineChain(goalSprite()) && thisAction == 0x68)
            return 1;

        if (isInEngineChain(target->goalSprite()) && targetAction == 0x68)
            return 1;

        if ((pushLineActiveRef() != 0 || target->pushLineActiveRef() != 0) &&
            (((target->m_runtimeFlags ^ m_runtimeFlags) & ArmyBitsMask) == 0))
            return 1;

        return 0;
    }

    float SPRITE::resolveEngineChainCollision(SPRITE* target, int mode) noexcept
    {
        EngineChainMetrics thisRange;
        thisRange.categoryFlags = 0;
        thisRange.collectEngineChainMetrics(this);
        EngineChainMetrics targetRange;
        targetRange.categoryFlags = 0;
        targetRange.collectEngineChainMetrics(target);

        float sharedSpeed = 0.0f;
        float relativeSpeed = 0.0f;
        computeCollisionKinematics(
            m_speed, target->m_speed,
            thisRange.weapon0CSum, targetRange.weapon0CSum,
            mode, sharedSpeed, relativeSpeed);

        if (mode == 1 || mode == 3)
            target->reverseEngineChain();

        for (SPRITE* node = target->engineChainHead(); node; node = node->engineChainNextRef())
            node->m_speed = (node->derivedStateValue(0) & 1) ? -sharedSpeed : sharedSpeed;

        float collisionLimit = 0.0f;
        const BASE_CONSTANTS* const constants = GlobalBaseConstants();
        std::memcpy(&collisionLimit, &constants->raw[24], sizeof(collisionLimit));

        const bool damageRoute =
            ((m_runtimeFlags & SPRITE::CommandBitsMask) == 108u && target->isInEngineChain(goalSprite())) ||
            ((target->m_runtimeFlags & SPRITE::CommandBitsMask) == 108u && isInEngineChain(target->goalSprite()));
        if (relativeSpeed > collisionLimit && damageRoute)
        {
            ChangeAnimation(12);
            playSfxAtWorldPosition(16);

            const int damage = spriteFmulFtolLow32(relativeSpeed, 1500.0f);
            int thisDamage = damage / 2;
            int targetDamage = damage / 2;

            VID* const targetVid = target->Vid();
            int targetActionValue = targetDamage;
            if (targetVid->nvid() == 97)
            {
                const int bucket = target->armyIndex();
                targetActionValue = targetVid->animationFrameDuration(bucket) + 10;
            }
            target->dispatchVirtualAction(ActionCode::ACT_DAMAGE, targetActionValue, 0, 0);

            for (SPRITE* node = target->engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
            {
                if (targetDamage < 2)
                    break;
                targetDamage /= 2;
                node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, targetDamage, 0, 0);
            }

            VID* const thisVid = Vid();
            int thisActionValue = thisDamage;
            if (thisVid->nvid() == 97)
            {
                const int bucket = armyIndex();
                thisActionValue = thisVid->animationFrameDuration(bucket) + 10;
            }
            dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisActionValue, 0, 0);

            SPRITE* node = engineChainPreviousRef();
            if (node)
            {
                while (node)
                {
                    if (thisDamage < 2)
                        return 0.0f;
                    thisDamage /= 2;
                    node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisDamage, 0, 0);
                    node = node->engineChainPreviousRef();
                }
                return 0.0f;
            }

            node = engineChainNextRef();
            if (node)
            {
                while (node)
                {
                    if (thisDamage < 2)
                        return 0.0f;
                    thisDamage /= 2;
                    node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisDamage, 0, 0);
                    node = node->engineChainNextRef();
                }
                return 0.0f;
            }
        }
        else
        {
            playSfxAtWorldPosition(19);
        }
        return 0.0f;
    }

    int SPRITE::resolveEngineChainPathInteraction(core::PathPosition* pathPair, float* distanceOut) noexcept
    {
        int relation = -1;
        int result = 0;
        SPRITE* resolved = resolvePathOwnerRelation(&relation);
        if (resolved)
        {
            primaryPathNodeRef() = pathPair->node;
            primaryPathProgressRef() = pathPair->progress;
            primaryPathAuxiliaryRef() = pathPair->auxiliary;
            primaryPathEdgeIndexRef() = pathPair->edgeIndex;

            if (canLinkEngineChain(resolved))
            {
                if (resolved->engineChainNextRef() || relation == 1 || relation == 4)
                    resolved->reverseEngineChain();

                if (((m_runtimeFlags ^ resolved->m_runtimeFlags) & ArmyBitsMask) == 0)
                {
                    if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x68u &&
                        resolved->isInEngineChain(goalSprite()))
                    {
                        if ((resolved->m_runtimeFlags & SPRITE::CommandBitsMask) != 0x68u ||
                            !isInEngineChain(resolved->goalSprite()))
                        {
                            resolved->dispatchVirtualAction(ActionCode::ACT_BACKUP_COMMAND, 0, 0, 0);
                        }
                    }
                    else
                    {
                        dispatchVirtualAction(ActionCode::ACT_BACKUP_COMMAND, 0, 0, 0);
                    }
                }

                if (resolved->engineChainNextRef())
                {
                    const int nvid = Vid() ? Vid()->nvid() : -1;
                    LOG::ResourceError("ENGINE %i", 10, "can't link", 0, nvid);
                }
                else
                {
                    resolved->engineChainNextRef() = this;
                    engineChainPreviousRef() = resolved;
                }

                dispatchEnginePrivateCommand(0, 0, 0, 0);
                ChangeAnimation(0x0B);

                if (((resolved->m_runtimeFlags ^ m_runtimeFlags) & ArmyBitsMask) != 0)
                {
                    for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
                    {
                        if ((node->m_runtimeFlags & ArmyBitsMask) != 0)
                            node->clearCommandsTargetingThisSprite();
                    }

                    SPRITE* callbackSprite = this;
                    if ((resolved->m_runtimeFlags & ArmyBitsMask) == (1u << ArmyBitsShift))
                        callbackSprite = resolved;
                    const int callbackArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(callbackSprite)));
                    (void)core::Application::callScriptFunction(core::scriptCallbackSlot(21u), callbackArg, 0);
                    return 0;
                }
            }
            else
            {
                result = 1;
                *distanceOut = resolveEngineChainCollision(resolved, relation);
                const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
                const int resolvedArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved)));
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(22u), selfArg, resolvedArg);
            }
            return result;
        }

        if (!findCrossingConstraintOwner())
            return result;

        if (*distanceOut > 0.2f)
            playSfxAtWorldPosition(0x94);

        primaryPathNodeRef() = pathPair->node;
        primaryPathProgressRef() = pathPair->progress;
        primaryPathAuxiliaryRef() = pathPair->auxiliary;
        primaryPathEdgeIndexRef() = pathPair->edgeIndex;
        *distanceOut = 0.0f;
        engineTargetSpeedRef() = -engineTargetSpeedRef();
        return 1;
    }

    void SPRITE::applyEngineChainPathMovement(core::PathPosition* pathPair, float speed, int delay) noexcept
    {
        SPRITE* const root = this;
        for (SPRITE* node = this; node; node = node->engineChainNextRef())
        {
            node->clearPathNodeOwnership();
            node->updateSecondaryPathPosition(pathPair);

            SPRITE* const next = node->engineChainNextRef();
            if (next)
            {
                pathPair->node = next->primaryPathNodeRef();
                pathPair->progress = next->primaryPathProgressRef();
                pathPair->auxiliary = next->primaryPathAuxiliaryRef();
                pathPair->edgeIndex = next->primaryPathEdgeIndexRef();

                core::WeakController* const secondaryNode = node->secondaryPathNodeRef();
                const core::WeakController::Link& secondaryEdge =
                    secondaryNode->links()[static_cast<std::size_t>(node->secondaryPathEdgeIndexRef())];
                next->primaryPathNodeRef() = secondaryEdge.target;
                next->primaryPathProgressRef() = static_cast<int>(secondaryEdge.length) - node->secondaryPathProgressRef();
                next->primaryPathAuxiliaryRef() = 0;
                next->primaryPathEdgeIndexRef() = secondaryEdge.reciprocalIndex;
            }

            node->updatePositionFromPathEndpoints();
            if ((!x87EqualOrUnordered(node->previousPathXRef(), 0.0f) ||
                 !x87EqualOrUnordered(node->previousPathYRef(), 0.0f)) &&
                x87AbsDiffGreaterOrdered(node->previousPathXRef(), node->m_xyz.x, 30.0f))
            {
                writeLogLine(g_fileLogger, kTrainCollapseBeginLog);
            }

            node->previousPathXRef() = node->m_xyz.x;
            node->previousPathYRef() = node->m_xyz.y;
            node->previousPathZRef() = node->m_xyz.z;
            node->claimPathNodeOwnership();

            node->m_speed = (node->derivedStateValue(0) & 1) ? -speed : speed;
            node->engineAccelerationDelayRef() = delay;
        }

        if (!x87EqualOrUnordered(speed, 0.0f))
        {
            VID* const rootVid = root->Vid();
            const float rootX = root->m_xyz.x;
            const float rootY = root->m_xyz.y;
            const float rootZ = root->m_xyz.z;
            const float minX = rootX - rootVid->halfSizeX();
            const float minY = rootY - rootVid->halfSizeY();
            const float maxX = rootX + rootVid->halfSizeX();
            const float maxY = rootY + rootVid->halfSizeY();

            SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
            for (SPRITE* candidate = hash->firstSpriteInBox(minX, minY, maxX, maxY);
                 candidate;
                 candidate = hash->nextSpriteInBox())
            {
                if (candidate == root)
                    continue;

                if (candidate->m_currentAnimation >= 0x0F)
                    continue;

                VID* const candidateVid = candidate->Vid();
                if (!x87SumGreaterThanAbsDiffOrdered(
                        candidateVid->halfSizeX(), rootVid->halfSizeX(),
                        candidate->m_xyz.x, rootX))
                    continue;
                if (!x87SumGreaterThanAbsDiffOrdered(
                        candidateVid->halfSizeY(), rootVid->halfSizeY(),
                        candidate->m_xyz.y, rootY))
                    continue;
                if (x87SumLessOrUnordered(candidateVid->sizeZ(), candidate->m_xyz.z, rootZ))
                    continue;
                if (x87SumLessOrUnordered(rootZ, rootVid->sizeZ(), candidate->m_xyz.z))
                    continue;

                if ((candidateVid->properties() & P_CRUSH) != 0)
                    candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 5, 0, 0);
            }
        }
    }

    void SPRITE::clearCommandsTargetingThisSprite() noexcept
    {
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();

        for (SPRITE* candidate = list.beginReverseIteration(cursor);
             candidate;
             candidate = list.continueReverseIteration(cursor))
        {
            SPRITE* actionSprite = candidate;
            if (candidate->goalSprite() == this)
            {
                const int action = static_cast<int>(candidate->m_runtimeFlags & SPRITE::CommandBitsMask);
                if (candidate->Vid()->spriteClassId() == 0x15u &&
                    (action == 0x70 || action == 0x74))
                {
                    candidate->dispatchEnginePrivateCommand(0, 0, 0, 0);
                    continue;
                }

                if (action != 0x14 && action != 0x0C && action != 0x10)
                    continue;
            }
            else
            {
                actionSprite = candidate->childChain();
                if (!actionSprite || actionSprite->goalSprite() != this)
                    continue;

                const int action = static_cast<int>(actionSprite->m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action != 0x14 && action != 0x0C && action != 0x10 &&
                    action != 0x70 && action != 0x74)
                    continue;
            }

            actionSprite->SetCommand(0, nullptr);
        }
    }

    int SPRITE::createRouteMarkerSprites(core::WeakController* pathNode) noexcept
    {
        SPRITE_POINTER_LIST& list = g_spriteWorkList;
        createPathSpritesFromBuffer(pathNode, &list, 603);

        int result = list.activeCount();
        for (int i = 0; i < list.activeCount(); ++i)
        {
            SPRITE* const value = list.data()[i];
            const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
            switch (action)
            {
            case 112:
            case 116:
                value->changeArmyBucket(1);
                break;
            case 108:
                value->changeArmyBucket(3);
                break;
            case 104:
                value->changeArmyBucket(2);
                break;
            default:
                break;
            }
            result = list.activeCount();
        }
        return result;
    }

    int SPRITE::createPathSpritesFromBuffer(core::WeakController* pathNode, SPRITE_POINTER_LIST* list, int nvid) noexcept
    {
        g_pathSearchResultScore = core::pathResultScore();
        g_pathSearchSecondaryBestCost = core::pathSecondaryBestCost();
        list->releaseRepeatedReferencesRetail();

        int result = pathBufferSizeRef();
        for (int i = 0; i < pathBufferSizeRef(); ++i)
        {
            const int edgeIndex = static_cast<int>(pathBufferData()[static_cast<std::size_t>(i)]);
            if (edgeIndex < pathNode->linkCount())
            {
                pathNode = pathNode->links()[static_cast<std::size_t>(edgeIndex)].target;

                VID* createVid = nullptr;
                core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                if (nvid < 0 || nvid >= table.count() ||
                    (createVid = table.slot(nvid)) == nullptr)
                {
                    createVid = MAP::NullVid();
                }

                SPRITE* const created = mapOwner()->CreateSprite(
                    createVid,
                    VECTOR(static_cast<float>(pathNode->x()),
                           static_cast<float>(pathNode->y()),
                           static_cast<float>(pathNode->id())),
                    ANGLE(0), nullptr, false, false);
                list->append(created);
            }
            result = pathBufferSizeRef();
        }
        return result;
    }

    int SPRITE::pathBufferReachesSecondaryTarget(core::WeakController* pathNode) noexcept
    {
        SPRITE* const tail = engineChainTail();
        core::WeakController* const secondaryNode = tail->secondaryPathNode();
        core::WeakController* const secondaryTarget = secondaryNode
            ? secondaryNode->links()[static_cast<std::size_t>(tail->secondaryPathEdgeIndex())].target
            : nullptr;

        core::WeakController* walker = pathNode;
        const int count = pathBufferSizeRef();
        for (int index = 0; index < count; ++index)
        {
            const unsigned int edgeIndex = pathBufferData()[static_cast<std::size_t>(index)];
            if (edgeIndex < static_cast<unsigned int>(walker->linkCount()))
            {
                walker = walker->links()[edgeIndex].target;
                if (walker == secondaryTarget)
                    return 1;
            }
        }
        return 0;
    }

    int SPRITE::evaluateEngineTargetRangeState() noexcept
    {
        SPRITE* const owner = goalSprite();
        if (!owner)
            return 2;

        const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        if (action != 112 && action != 116)
            return 2;

        auto linkedChild = [](SPRITE* node) noexcept -> SPRITE*
        {
            SPRITE* const child = node->childChain();
            if (!child)
                return nullptr;
            VID* const childVid = child->Vid();
            VID* const nodeVid = node->Vid();
            if (childVid != nodeVid->linkedVid())
                return nullptr;
            if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
                return nullptr;
            return child;
        };

        if (SPRITE* const ref = engineCommandReferenceOwnerRef())
        {
            if (SPRITE* const child = linkedChild(ref))
            {
                VID* const childVid = child->Vid();
                // approximatePlanarDistance, then compares the live x87 result against
                // [WEAPON+0x18]-10 with TEST AH,41h (<= or unordered).
                const float dx = owner->m_xyz.x - child->m_xyz.x;
                const float dy = owner->m_xyz.y - child->m_xyz.y;
                return metricWithinFromRoundedDeltas(
                           dx, dy, childVid->weaponBattleRange()) ? 1 : 0;
            }

            VID* const refVid = ref->Vid();
            if (refVid->nvid() != 35)
                return 2;
            return metricWithinPositions(
                       owner->m_xyz.x, owner->m_xyz.y,
                       ref->m_xyz.x, ref->m_xyz.y,
                       refVid->weaponBattleRange()) ? 1 : 0;
        }

        int result = 2;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
        {
            if (SPRITE* const child = linkedChild(node))
            {
                VID* const childVid = child->Vid();
                // Loop routes 0x44CDB2+ keep the coordinate subtraction live
                // in x87 instead of spilling the deltas before the metric.
                if (!metricWithinPositions(
                        owner->m_xyz.x, owner->m_xyz.y,
                        child->m_xyz.x, child->m_xyz.y,
                        childVid->weaponBattleRange()))
                    return 0;
                result = 1;
                continue;
            }

            VID* const nodeVid = node->Vid();
            if (nodeVid->nvid() == 35)
            {
                if (!metricWithinPositions(
                        owner->m_xyz.x, owner->m_xyz.y,
                        node->m_xyz.x, node->m_xyz.y,
                        nodeVid->weaponBattleRange()))
                    return 0;
                result = 1;
            }
        }
        return result;
    }

    void SPRITE::updateEngineChainRoute() noexcept
    {
        core::PathPosition pathSnapshot{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};

        auto liveNode = [this]() noexcept -> core::WeakController* { return primaryPathNodeRef(); };
        auto liveIndex = [this]() noexcept -> int { return primaryPathEdgeIndexRef(); };

        if (!liveNode() || !secondaryPathNodeRef() || engineChainPreviousRef())
            return;

        VID* const vid = Vid();
        if (vid->nvid() != 85)
        {
            core::WeakController* const node = liveNode();
            core::WeakController::Link& edge =
                node->links()[static_cast<std::size_t>(liveIndex())];
            if (edge.target &&
                static_cast<int>(edge.target->routeClassTag()) - 4 ==
                    armyIndex() &&
                primaryPathProgressRef() > static_cast<int>(edge.length) / 2)
            {
                resetEngineChainMovement();
                m_speed = 0.0f;
                reverseEngineChain();
                return;
            }
        }

        int seenBit0 = 0;
        SPRITE* scan = this;
        while (scan)
        {
            core::WeakController* const node = scan->primaryPathNodeRef();
            if (node &&
                node->selectedLinkIndex() == scan->primaryPathEdgeIndexRef() &&
                node->pushLineValue() != 0u)
            {
                for (SPRITE* mark = this; mark; mark = mark->engineChainNextRef())
                    mark->pushLineActiveRef() = 1;
                break;
            }

            bool reciprocalSelected = false;
            if (node)
            {
                core::WeakController::Link& edge =
                    node->links()[static_cast<std::size_t>(scan->primaryPathEdgeIndexRef())];
                if (edge.target)
                    reciprocalSelected = edge.target->selectedLinkIndex() == edge.reciprocalIndex;
            }

            if (reciprocalSelected)
            {
                if (m_speed != 0.0f)
                {
                    const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                    if (action == 92)
                    {
                        bool resetAction = pathBufferSizeRef() == 0;
                        if (!resetAction)
                        {
                            const unsigned char bufferedIndex = pathBufferData()[0];
                            core::WeakController* const bufferedTarget =
                                liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target;
                            core::WeakController* const currentTarget =
                                liveNode()->links()[static_cast<std::size_t>(liveIndex())].target;
                            resetAction = bufferedTarget == currentTarget;
                        }
                        if (resetAction)
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                        else
                            m_runtimeFlags &= ~MovementStartedFlag;
                    }
                    else
                    {
                        m_runtimeFlags &= ~MovementStartedFlag;
                    }
                    m_speed = 0.0f;
                    playSfxAtWorldPosition(148);
                }

                if (engineTargetSpeedRef() > 0.0f)
                    engineTargetSpeedRef() = -engineTargetSpeedRef();
                reverseEngineChain();
                return;
            }

            scan->pushLineActiveRef() = 0;
            seenBit0 |= static_cast<int>(scan->m_runtimeFlags & 1u);
            scan = scan->engineChainNextRef();
        }

        if (!scan && seenBit0 != 0)
        {
            for (SPRITE* node = this; node; node = node->engineChainNextRef())
            {
                if ((node->m_runtimeFlags & 1u) == 0)
                    continue;
                node->m_runtimeFlags &= ~1u;

                if (node->Vid()->weaponFloatAt(16) != 0.0f)
                {
                }

                ENGINE* const engineNode = static_cast<ENGINE*>(node);
                if (engineNode->productionBatchCompletionPending() != 0)
                {
                    const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(node));
                    (void)core::Application::callScriptFunction(
                        core::scriptCallbackSlot(4u), spriteArg, 0);
                    engineNode->setProductionBatchCompletionPending(0);
                }
            }
        }

        if (pushLineActiveRef() == 0)
        {
            if ((m_runtimeFlags & MovementStartedFlag) == 0 && m_speed == 0.0f)
            {
                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action == 92 || action == 104 || action == 108 || action == 100)
                    StartMove();
                if (action == 96)
                {
                    SPRITE* node = engineChainHead();
                    while (node &&
                           (node->Vid()->nvid() != 85 || node->routeActionReadyRef() != 0))
                        node = node->engineChainNextRef();
                    if (node)
                        StartMove();
                }
            }

            if ((m_runtimeFlags & MovementStartedFlag) == 0)
            {
                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if ((action == 112 || action == 116) && (std::rand() % 5) == 0 && evaluateEngineTargetRangeState() != 1)
                    StartMove();
            }

            if (engineTargetSpeedRef() == 0.0f && (m_runtimeFlags & MovementStartedFlag) != 0)
            {
                updateEngineChainSpeedTarget();
                if (engineTargetSpeedRef() == 0.0f && m_speed == 0.0f)
                    dispatchEnginePrivateCommand(0, 0, 0, 0);
            }
        }

        SPRITE* const routeTarget = engineCommandArgument0Ref() != 0 ? nullptr : goalSprite();
        float speed = std::fabs(m_speed);
        approachEngineTargetSpeed(&speed);
        if (speed < 0.0f)
        {
            m_speed = 0.0f;
            reverseEngineChain();
            updateEngineChainSpeedTarget();
            return;
        }

        core::WeakController::Link& currentEdge =
            liveNode()->links()[static_cast<std::size_t>(liveIndex())];

        BASE_CONSTANTS* const constants = GlobalBaseConstants();
        float speedLimit = 0.0f;
        std::memcpy(&speedLimit, &constants->raw[6], sizeof(speedLimit));
        if (liveNode()->pathEventFlag() != 0 && speed > speedLimit)
        {
            if (Vid()->nvid() != 85)
                speed = speedLimit;

            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            VID* effectVid = nullptr;
            if (table.count() > 588)
                effectVid = table.slot(588);
            if (!effectVid)
                effectVid = MAP::NullVid();
            mapOwner()->CreateSprite(effectVid, m_xyz, ANGLE(0), this, false, false);
        }

        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const std::int64_t deltaFixed = static_cast<std::int64_t>(
            static_cast<double>(static_cast<std::int32_t>(deltaMs)) *
            static_cast<double>(speed) * 64000.0);
        const std::int64_t accumulated = deltaFixed + static_cast<std::int64_t>(primaryPathAuxiliaryRef());
        primaryPathAuxiliaryRef() = static_cast<int>(accumulated);
        if (primaryPathAuxiliaryRef() < 0)
        {
            primaryPathAuxiliaryRef() = 0;
        }
        else if (primaryPathAuxiliaryRef() > 65535)
        {
            primaryPathProgressRef() += primaryPathAuxiliaryRef() >> 16;
            primaryPathAuxiliaryRef() &= 65535;
        }

        if (primaryPathProgressRef() > static_cast<int>(currentEdge.length))
        {
            if (liveNode()->pathEventFlag() != 0)
            {
                const float minX = static_cast<float>(liveNode()->x() - 100);
                const float minY = static_cast<float>(liveNode()->y() - 60);
                const float maxX = static_cast<float>(liveNode()->x() + 100);
                const float maxY = static_cast<float>(liveNode()->y() + 60);
                SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
                for (SPRITE* candidate = hash->firstSpriteInBox(minX, minY, maxX, maxY);
                     candidate;
                     candidate = hash->nextSpriteInBox())
                {
                    if (candidate->Vid()->spriteClassId() == 22u)
                        static_cast<RAIL*>(candidate)->handleRailNodeReleased(reinterpret_cast<std::uintptr_t>(liveNode()));
                }
                liveNode()->setPathEventFlag(0);
            }

            core::WeakController* const targetNode = currentEdge.target;
            if (targetNode->linkCount() < 2)
            {
                speed = 0.0f;
                primaryPathProgressRef() = currentEdge.length;

                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action == 92)
                {
                    if (pathBufferSizeRef() != 0)
                    {
                        const unsigned char bufferedIndex = pathBufferData()[0];
                        const bool sameTarget =
                            liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target == targetNode;
                        if (!sameTarget)
                            resetEngineChainMovement();
                        else
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                    }
                    else
                    {
                        dispatchEnginePrivateCommand(0, 0, 0, 0);
                    }
                }
                else
                {
                    resetEngineChainMovement();
                }
            }
            else
            {
                core::PathPosition livePath{
                    primaryPathNodeRef(), primaryPathProgressRef(),
                    primaryPathAuxiliaryRef(), primaryPathEdgeIndexRef()};
                const int routeResult =
                    core::advancePathPosition(&livePath, engineCommandArgument0Node(), routeTarget, this);
                primaryPathNodeRef() = livePath.node;
                primaryPathProgressRef() = livePath.progress;
                primaryPathAuxiliaryRef() = livePath.auxiliary;
                primaryPathEdgeIndexRef() = livePath.edgeIndex;

                SPRITE* const controlled =
                    mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                if (isInEngineChain(controlled) && (controlled->m_runtimeFlags & ArmyBitsMask) == 0)
                    createRouteMarkerSprites(liveNode());

                const int actionBeforeOwnerCheck = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (actionBeforeOwnerCheck != 108)
                {
                    SPRITE* const nodeOwner = liveNode()->ownerSprite();
                    if (nodeOwner)
                        nodeOwner->isInEngineChain(goalSprite());
                }

                core::WeakController* const currentTarget =
                    liveNode()->links()[static_cast<std::size_t>(liveIndex())].target;
                SPRITE* const currentTargetOwner = currentTarget->ownerSprite();
                if (!currentTargetOwner || currentTargetOwner->isInEngineChain(goalSprite()))
                {
                    const int actionBeforeSign = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                    if ((actionBeforeSign == 108 || routeResult != 0) && routeResult < 0 && engineTargetSpeedRef() > 0.0f)
                        engineTargetSpeedRef() = -engineTargetSpeedRef();
                }

                if (Vid()->nvid() != 85 && currentTarget &&
                    static_cast<int>(currentTarget->routeClassTag()) - 4 ==
                        armyIndex())
                {
                    speed *= 0.5f;
                    resetEngineChainMovement();
                }

                core::WeakController* const actionTarget = engineCommandArgument0Node();
                const bool primaryRoute =
                    (actionTarget && actionTarget == currentTarget) ||
                    routeResult == 0 || currentTarget == core::pathBestNode();

                const int finalAction = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (primaryRoute)
                {
                    if (finalAction == 96)
                    {
                        for (SPRITE* node = this; node; node = node->engineChainNextRef())
                        {
                            if (node->Vid()->nvid() == 85)
                            {
                                node->routeActionReadyRef() = 1;
                                resetEngineChainMovement();
                            }
                        }
                        if ((m_runtimeFlags & MovementStartedFlag) != 0)
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                    }
                    else if (finalAction == 92)
                    {
                        if (Vid()->nvid() != 85 || !liveNode() || !currentTarget ||
                            static_cast<int>(currentTarget->routeClassTag()) - 4 !=
                                armyIndex())
                        {
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                            const int selfArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this));
                            (void)core::Application::callScriptFunction(
                                core::scriptCallbackSlot(8u), selfArg, 0);
                        }
                    }
                    else if (finalAction == 100)
                    {
                        resetEngineChainMovement();
                        StartMove();
                    }
                    else if ((finalAction == 112 || finalAction == 116) && evaluateEngineTargetRangeState() == 1)
                    {
                        resetEngineChainMovement();
                    }
                }
                else
                {
                    if (finalAction == 112 || finalAction == 116)
                    {
                        if (evaluateEngineTargetRangeState() == 1)
                            resetEngineChainMovement();
                    }
                    else if (liveNode()->linkCount() < 2 ||
                             currentTarget->linkCount() < 2)
                    {
                        if (finalAction == 92)
                        {
                            if (pathBufferSizeRef() != 0)
                            {
                                const unsigned char bufferedIndex = pathBufferData()[0];
                                if (liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target == currentTarget)
                                    dispatchEnginePrivateCommand(0, 0, 0, 0);
                                else
                                    resetEngineChainMovement();
                            }
                            else
                            {
                                dispatchEnginePrivateCommand(0, 0, 0, 0);
                            }
                        }
                        else
                        {
                            resetEngineChainMovement();
                        }
                    }
                    else if (currentTarget->ownerSprite() &&
                             !currentTarget->ownerSprite()->isInEngineChain(goalSprite()))
                    {
                        resetEngineChainMovement();
                    }
                }
            }
        }

        resolveEngineChainPathInteraction(&pathSnapshot, &speed);
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
            node->clearPathNodeOwnership();
        applyEngineChainPathMovement(&pathSnapshot, speed, engineAccelerationDelayRef());
    }

    void SPRITE::initializeEnginePathEndpoints() noexcept
    {
        VID* const vid = Vid();
        const float radius = vid->weaponRadius() * 0.5f;
        const int baseDirection = directionIndex();

        core::WeakController* const seed =
            core::findNearestLinkedNode3D(&core::globalWeakControllerMap(),
                             spriteFtolLow32(static_cast<long double>(m_xyz.x)),
                             spriteFtolLow32(static_cast<long double>(m_xyz.y)),
                             spriteFtolLow32(static_cast<long double>(m_xyz.z)));
        if (!seed || seed->linkCount() == 0)
            return;

        int facing = 0;
        int edgeIndex = seed->linkCount() - 1;
        while (edgeIndex >= 0)
        {
            const core::WeakController::Link& entry =
                seed->links()[static_cast<std::size_t>(edgeIndex)];
            const unsigned char edgeFacing = static_cast<unsigned char>(entry.facing);
            const unsigned char deltaA =
                static_cast<unsigned char>(baseDirection - edgeFacing);
            const unsigned char deltaB =
                static_cast<unsigned char>(edgeFacing - baseDirection);
            const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;

            if (delta > 108 || delta < 20)
                break;
            --edgeIndex;
        }

        if (edgeIndex >= 0)
            facing = baseDirection;
        else
            facing = seed->firstLinkFacing();

        auto loadPair = [](core::WeakController* node, int progress, int pad, int index) noexcept
        {
            core::PathPosition pair;
            pair.node = node;
            pair.progress = progress;
            pair.auxiliary = pad;
            pair.edgeIndex = index;
            return pair;
        };

        auto storePrimary = [this](const core::PathPosition& pair) noexcept
        {
            primaryPathNodeRef() = pair.node;
            primaryPathProgressRef() = pair.progress;
            primaryPathAuxiliaryRef() = pair.auxiliary;
            primaryPathEdgeIndexRef() = pair.edgeIndex;
        };

        auto storeSecondary = [this](const core::PathPosition& pair) noexcept
        {
            secondaryPathNodeRef() = pair.node;
            secondaryPathProgressRef() = pair.progress;
            secondaryPathAuxiliaryRef() = pair.auxiliary;
            secondaryPathEdgeIndexRef() = pair.edgeIndex;
        };

        auto repairToCloserTarget = [this](core::PathPosition& pair) noexcept
        {
            core::WeakController* const node = pair.node;
            const core::WeakController::Link& entry =
                node->links()[static_cast<std::size_t>(pair.edgeIndex)];
            core::WeakController* const target = entry.target;

            const double nodeDx = static_cast<double>(node->x()) - m_xyz.x;
            const double nodeDy = static_cast<double>(node->y()) - m_xyz.y;
            const double nodeDz = static_cast<double>(node->id()) - m_xyz.z;
            const double targetDx = static_cast<double>(target->x()) - m_xyz.x;
            const double targetDy = static_cast<double>(target->y()) - m_xyz.y;
            const double targetDz = static_cast<double>(target->id()) - m_xyz.z;

            const double nodeDistance = std::sqrt(nodeDx * nodeDx + nodeDy * nodeDy + nodeDz * nodeDz);
            const double targetDistance = std::sqrt(targetDx * targetDx + targetDy * targetDy + targetDz * targetDz);

            if (targetDistance < nodeDistance ||
                std::isnan(targetDistance) || std::isnan(nodeDistance))
            {
                pair.progress = static_cast<int>(entry.length) - pair.progress;
                pair.node = target;
                pair.edgeIndex = entry.reciprocalIndex;
            }
        };

        core::PathPosition primary =
            loadPair(primaryPathNodeRef(),
                     primaryPathProgressRef(),
                     primaryPathAuxiliaryRef(),
                     primaryPathEdgeIndexRef());

        core::findNearestPathPosition(seed,
                         spriteFtolLow32(static_cast<long double>(radius) *
                                             directionSin(facing) +
                                         static_cast<long double>(m_xyz.x)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.y) -
                                         static_cast<long double>(radius) *
                                             directionCos(facing)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.z)),
                         &primary);
        repairToCloserTarget(primary);
        storePrimary(primary);

        const int reverseFacing = static_cast<unsigned char>(facing - 128);
        core::PathPosition secondary =
            loadPair(secondaryPathNodeRef(),
                     secondaryPathProgressRef(),
                     secondaryPathAuxiliaryRef(),
                     secondaryPathEdgeIndexRef());

        core::findNearestPathPosition(seed,
                         spriteFtolLow32(static_cast<long double>(radius) *
                                             directionSin(reverseFacing) +
                                         static_cast<long double>(m_xyz.x)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.y) -
                                         static_cast<long double>(radius) *
                                             directionCos(reverseFacing)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.z)),
                         &secondary);
        repairToCloserTarget(secondary);
        storeSecondary(secondary);

        core::PathPosition primaryForBCF0{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};
        updateSecondaryPathPosition(&primaryForBCF0);
        updatePositionFromPathEndpoints();
        SPRITE* const owner = findEnginePathRelationSprite();
        attachEngineChain(owner);
        claimPathNodeOwnership();
    }

    void SPRITE::updatePositionFromPathEndpoints() noexcept
    {
        core::WeakController* const firstNode = primaryPathNodeRef();
        core::WeakController* firstTarget = nullptr;
        int firstDuration = 0;
        if (firstNode)
        {
            const core::WeakController::Link& firstEdge =
                firstNode->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())];
            firstTarget = firstEdge.target;
            firstDuration = static_cast<int>(firstEdge.length);
        }

        const int firstProgress = primaryPathProgressRef();
        const int firstTargetX = firstTarget->x();
        const int firstNodeX = firstNode->x();
        const float firstX = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetX - firstNodeX,
                                          firstDuration),
            firstNodeX);

        const int firstTargetY = firstTarget->y();
        const int firstNodeY = firstNode->y();
        const float firstY = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetY - firstNodeY,
                                          firstDuration),
            firstNodeY);

        const int firstTargetZ = firstTarget->id();
        const int firstNodeZ = firstNode->id();
        const float firstZ = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetZ - firstNodeZ,
                                          firstDuration),
            firstNodeZ);

        core::WeakController* const secondNode = secondaryPathNodeRef();
        if (secondaryPathEdgeIndexRef() >= secondNode->linkCount())
        {
            LOG::ResourceError("ENGINE %i", 10, kMissingLinkResourceError, 0,
                               Vid() ? Vid()->nvid() : -1);
            secondaryPathEdgeIndexRef() = secondNode->linkCount() - 1;
        }

        const core::WeakController::Link& secondEdge =
            secondNode->links()[static_cast<std::size_t>(secondaryPathEdgeIndexRef())];
        core::WeakController* const secondTarget = secondEdge.target;
        if (!secondTarget)
        {
            LOG::ResourceError("ENGINE %i", 10, kMissingTailDot2ResourceError, 0,
                               Vid() ? Vid()->nvid() : -1);
            return;
        }

        const int secondDuration = static_cast<int>(secondEdge.length);
        const int secondProgress = secondaryPathProgressRef();
        const int secondNodeX = secondNode->x();
        const int secondNodeY = secondNode->y();
        const int secondNodeZ = secondNode->id();

        const float secondX = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->x() - secondNodeX,
                                          secondDuration),
            secondNodeX);
        const float secondY = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->y() - secondNodeY,
                                          secondDuration),
            secondNodeY);
        const float secondZ = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->id() - secondNodeZ,
                                          secondDuration),
            secondNodeZ);

        ChangeCoor(pathAverageCoordinate(secondX, firstX),
                   pathAverageCoordinate(secondY, firstY),
                   pathAverageCoordinate(secondZ, firstZ));

        const bool reverseDirection = (derivedStateValue(0) & 1) != 0;
        const int directionY = pathDirectionDeltaYToInt(
            reverseDirection ? secondY : firstY,
            reverseDirection ? firstY : secondY);
        const int directionX = pathDirectionDeltaXToInt(
            reverseDirection ? secondX : firstX,
            reverseDirection ? firstX : secondX);
        ChangeDirection(AngleFromXY(directionX, directionY, nullptr));
    }

    void SPRITE::splitEngineChainAtPosition(float x, float y) noexcept
    {
        SPRITE* const first = engineChainHead();
        SPRITE* const last = engineChainTail();
        playSfxAtWorldPosition(15);

        bool preferFirst = false;
        if (engineChainPreviousRef() && engineChainNextRef())
        {
            preferFirst = preferFirstTrainEndpoint(
                x, y,
                engineChainPreviousRef()->X(), engineChainPreviousRef()->Y(),
                engineChainNextRef()->X(), engineChainNextRef()->Y());
        }

        if (!engineChainNextRef() || preferFirst)
        {
            if (engineChainPreviousRef())
            {
                engineChainPreviousRef()->engineChainNextRef() = nullptr;
                engineChainPreviousRef() = nullptr;
            }
        }
        else
        {
            engineChainNextRef()->engineChainPreviousRef() = nullptr;
            engineChainNextRef() = nullptr;
        }

        if (last != first)
        {
            last->resetEngineChainMovement();
            if (x87IsZeroOrUnordered(last->m_speed))
            {
                last->reverseEngineChain();
                SPRITE* const head = last->engineChainHead();
                head->m_speed = (static_cast<std::uint32_t>(head->derivedStateValue(0)) & 1u) != 0u ? -0.01f : 0.01f;
            }

            if (x87IsZeroOrUnordered(first->m_speed))
            {
                SPRITE* const head = first->engineChainHead();
                head->m_speed = (static_cast<std::uint32_t>(head->derivedStateValue(0)) & 1u) != 0u ? -0.01f : 0.01f;
            }
            else
            {
                first->updateEngineChainSpeedTarget();
            }
        }
    }

    int SPRITE::scaledEngineChainLength() noexcept
    {
        int count = 0;
        SPRITE* walker = engineChainHead();
        while (walker)
        {
            walker = walker->engineChainNextRef();
            ++count;
        }

        const long double value =
            static_cast<long double>(count) * static_cast<long double>(1.33f) +
            static_cast<long double>(0.5f);
        return spriteFtolLow32(value);
    }

    void SPRITE::updateEngineChainSpeedTarget() noexcept
    {
        SPRITE* head = this;
        if (head->engineChainPreviousRef())
        {
            do
            {
                head = head->engineChainHead();
            }
            while (head->engineChainPreviousRef());
        }

        EngineChainMetrics range;
        range.categoryFlags = 0;
        range.collectEngineChainMetrics(head);

        if (x87EqualOrUnordered(head->engineTargetSpeedRef(), 0.0f) &&
            !x87EqualOrUnordered(range.weapon0CSum, 0.0f) &&
            spriteFdivMulFtolLow32(range.weapon10Sum,
                                   range.weapon0CSum, 8.0f) > 7)
        {
            const std::uint32_t flags = head->m_runtimeFlags;
            if ((flags & MovementStartedFlag) != 0u && core::BulkSpriteDeleteActive() == 0u)
            {
                core::WeakController* const routeNode = head->engineCommandArgument0Node();
                SPRITE* const routeSprite = routeNode ? nullptr : head->goalSprite();

                core::PathPosition path{
                    head->primaryPathNodeRef(),
                    head->primaryPathProgressRef(),
                    head->primaryPathAuxiliaryRef(),
                    head->primaryPathEdgeIndexRef()};
                const int projection = core::scoreNextPathStep(&path,
                                                        routeNode,
                                                        routeSprite,
                                                        static_cast<int>((flags >> CommandBitsShift) & CommandValueMask),
                                                        head);

                SPRITE* const controlled =
                    head->mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                if (head->isInEngineChain(controlled))
                {
                    SPRITE* const controlledAgain =
                        head->mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                    if ((controlledAgain->m_runtimeFlags & ArmyBitsMask) == 0u)
                    {
                        core::WeakController* pathTarget = nullptr;
                        if (head->primaryPathNodeRef())
                        {
                            pathTarget = head->primaryPathNodeRef()->links()
                                [static_cast<std::size_t>(head->primaryPathEdgeIndexRef())].target;
                        }
                        head->createRouteMarkerSprites(pathTarget);
                    }
                }

                head->engineTargetSpeedRef() = projection >= 0 ? 0.001f : -0.001f;
            }
        }

        if (x87LessOrUnordered(head->engineTargetSpeedRef(), 0.0f))
        {
            const int negativeDelay = static_cast<int>(0u - static_cast<std::uint32_t>(range.movementDelayMs));
            head->engineTargetSpeedRef() = spriteFildMulStoreFloat(negativeDelay, 0.001f);
            return;
        }

        if (x87OrderedGreater(head->engineTargetSpeedRef(), 0.0f))
        {
            head->engineTargetSpeedRef() = spriteFildMulStoreFloat(range.movementDelayMs, 0.001f);
            const int delay = x87EqualOrUnordered(range.weapon0CSum, 0.0f)
                ? 0
                : spriteFdivMulFtolLow32(range.weapon10Sum,
                                         range.weapon0CSum, 8.0f);
            head->engineAccelerationDelayRef() = delay;
            if (delay == 0)
                head->m_runtimeFlags &= ~MovementStartedFlag;
        }
    }

    void SPRITE::approachEngineTargetSpeed(float* speedOut) noexcept
    {
        constexpr float immediateSpeed = 0.03500000014901161f;
        constexpr float tickScale = 0.000001f;

        if (pushLineActiveRef() != 0 &&
            x87EqualOrUnordered(engineTargetSpeedRef(), 0.0f))
        {
            *speedOut = immediateSpeed;
            engineAccelerationDelayRef() = 0;
            return;
        }

        const float target = engineTargetSpeedRef();
        // target vs speed, TEST AH,41h: acceleration runs only for an
        // ordered target > speed comparison.
        if (!x87LessEqualOrUnordered(target, *speedOut))
        {
            if (engineAccelerationDelayRef() == 0)
                updateEngineChainSpeedTarget();

            const int delay = engineAccelerationDelayRef();
            if (delay != 0)
            {
                const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                const std::uint32_t product =
                    static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(delay);
                const double step = static_cast<double>(product) * static_cast<double>(tickScale);
                *speedOut = static_cast<float>(static_cast<double>(*speedOut) + step + step);
            }

            if (*speedOut >= target)
            {
                *speedOut = target;
                engineAccelerationDelayRef() = 0;
            }
            return;
        }

        if (x87LessOrUnordered(target, *speedOut))
        {
            const int delay = spriteFtolLow32(
                (static_cast<long double>(*speedOut) * 1000.0L + 10.0L) * -0.5L);
            engineAccelerationDelayRef() = delay;

            const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const std::uint32_t product =
                static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(delay);
            const std::int32_t signedProduct = static_cast<std::int32_t>(product);
            const double step = static_cast<double>(signedProduct) * static_cast<double>(tickScale);
            *speedOut = static_cast<float>(static_cast<double>(*speedOut) + step + step);

            if (x87LessEqualOrUnordered(*speedOut, target))
            {
                *speedOut = target;
                engineAccelerationDelayRef() = 0;
            }
            return;
        }

        engineAccelerationDelayRef() = 0;
    }

    void SPRITE::resetEngineChainMovement() noexcept
    {
        SPRITE* node = this;
        for (;;)
        {
            SPRITE* const controlledPlayer =
                node->mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
            if (node->isInEngineChain(controlledPlayer))
                g_spriteWorkList.releaseRepeatedReferencesRetail();

            if (!node->engineChainPreviousRef())
                break;
            node = node->engineChainHead();
        }

        const DWORD flags = node->m_runtimeFlags;
        if ((flags & CommandBitsMask) == 0x64u)
        {
            if ((flags & MovementStartedFlag) != 0u)
            {
                node->engineCommandArgument0Ref() = node->engineCommandArgument1Ref();
                node->m_runtimeFlags = flags | MovementStartedFlag;
                node->engineCommandArgument1Ref() = node->engineCommandArgument2Ref();
                node->engineCommandArgument2Ref() = node->engineCommandArgument0Ref();
                node->engineTargetSpeedRef() = 0.0f;
                node->engineAccelerationDelayRef() = 0;

                for (SPRITE* child = node->engineChainNextRef();
                     child;
                     child = child->engineChainNextRef())
                {
                    child->engineCommandArgument0Ref() = node->engineCommandArgument0Ref();
                    child->engineCommandArgument1Ref() = node->engineCommandArgument1Ref();
                    child->engineCommandArgument2Ref() = node->engineCommandArgument2Ref();
                    child->m_runtimeFlags |= MovementStartedFlag;
                    child->engineTargetSpeedRef() = 0.0f;
                    child->engineAccelerationDelayRef() = 0;
                }
            }
            return;
        }

        for (SPRITE* iter = node; iter; iter = iter->engineChainNextRef())
        {
            iter->engineTargetSpeedRef() = 0.0f;
            iter->m_runtimeFlags &= ~MovementStartedFlag;

            if (!x87EqualOrUnordered(node->Speed(), 0.0f))
                iter->engineAccelerationDelayRef() = animationDelayFromSpeed(node->Speed());
        }
    }

    void SPRITE::dispatchEnginePrivateCommandAtPathPoint(int opcode, int x, int y) noexcept
    {
        core::WeakController* const node =
            core::findNearestLinkedNode2D(&core::globalWeakControllerMap(), x, y);
        dispatchEnginePrivateCommand(opcode, 0,
            static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node))), 0);
    }

    void SPRITE::dispatchEnginePrivateCommand(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        int action = opcode;
        SPRITE* const owner = this;
        int forcedTerminalZero = 0;

        if (action == 0x1E)
        {
            action = 0;
            forcedTerminalZero = 1;
        }

        SPRITE* target = reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(argument1));
        int actionArgument2 = argument2;
        const int actionArgument3 = argument3;

        if (!target && actionArgument2 == 0 && action != 0x1D)
        {
            action = 0;
        }
        else if (action == 0x18)
        {
            SPRITE* scan = engineChainHead();
            while (scan)
            {
                if (scan->m_vid->nvid() == 85 && scan->ammoFixedPoint() / 64 > 0)
                    break;
                scan = scan->engineChainNextRef();
            }
            if (!scan)
            {
                action = 0;
                target = nullptr;
                actionArgument2 = 0;
            }
        }

        core::WeakController* resolvedB4 = nullptr;
        int resolvedB8 = 0;
        if (action == 0x19)
        {
            resolvedB4 = actionArgument3
                ? reinterpret_cast<core::WeakController*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(actionArgument3)))
                : core::findNearestLinkedNode3D(&core::globalWeakControllerMap(),
                                   spriteFtolLow32(static_cast<long double>(m_xyz.x)),
                                   spriteFtolLow32(static_cast<long double>(m_xyz.y)),
                                   spriteFtolLow32(static_cast<long double>(m_xyz.z)));
            resolvedB8 = actionArgument2;
        }

        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
        {
            SPRITE* const refOwner = node->engineCommandReferenceOwnerRef();
            if (refOwner)
            {
                const int nextRef = refOwner->m_listReferenceCount - 1;
                refOwner->m_listReferenceCount = nextRef;
                if (nextRef < 0)
                {
                    const int nvid = refOwner->m_vid ? refOwner->m_vid->nvid() : -1;
                    LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, nvid);
                }
                else if (nextRef == 0)
                {
                    DeleteSpriteThroughVirtualDeletingDestructor(refOwner);
                }
                node->engineCommandReferenceOwnerRef() = nullptr;
            }

            if ((action == 0x1C || action == 0x1D) &&
                !engineCommandReferenceBlockedRetail(owner))
            {
                node->engineCommandReferenceOwnerRef() = owner;
                ++owner->m_listReferenceCount;
            }

            node->SetCommandWithoutLink(action, target);
            node->engineCommandArgument1Ref() = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolvedB4)));
            node->engineCommandArgument0Ref() = actionArgument2;
            node->engineCommandArgument2Ref() = resolvedB8;
            node->engineTargetSpeedRef() = 0.0f;
            node->routeActionStartTimeRef() = 0;
            node->routeActionReadyRef() = 0;

            SPRITE* const child = node->m_childChain;
            if (!child)
                continue;

            VID* const linkVid = node->m_vid->linkedVid();
            if (child->m_vid != linkVid ||
                child->m_vid->hasWeaponChildDescriptor() == 0u ||
                child->m_vid->weaponCount() == 0u ||
                forcedTerminalZero != 0)
            {
                continue;
            }

            if (!engineCommandReferenceBlockedRetail(owner) &&
                node != owner)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            if (owner->canWeaponAffectTarget(target) == 0)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            if (action == 0x1C)
            {
                child->SetCommandWithoutLink(3, target);
                continue;
            }

            if (action != 0x1D)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            VID* weaponOwner = node->m_vid;
            if (child && child->m_vid == node->m_vid->linkedVid() &&
                child->m_vid->hasWeaponChildDescriptor() != 0u &&
                child->m_vid->weaponCount() != 0u)
            {
                weaponOwner = child->m_vid;
            }

            if (weaponOwner->weaponTypeMask() == 8 && target)
            {
                SPRITE* helper = new (std::nothrow) SPRITE(
                    mapOwner(),
                    MAP::NullVid(),
                    VECTOR(target->m_xyz.x, target->m_xyz.y + 70.0f, target->m_xyz.z + 70.0f),
                    ANGLE(0),
                    nullptr);
                child->SetCommandWithoutLink(4, helper);
            }
            else
            {
                child->SetCommandWithoutLink(4, target);
            }
        }

        if (action == 0x17 || action == 0x1A || action == 0x1B || action == 0x19)
        {
            engineChainHead()->StartMove();
            return;
        }

        if (action == 0x18)
        {
            const std::uint32_t c8Low = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(primaryPathNodeRef()));
            if (c8Low == static_cast<std::uint32_t>(engineCommandArgument0Ref()))
            {
                engineChainHead()->resetEngineChainMovement();
                for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
                {
                    if (node->m_vid->nvid() == 85 && ammoFixedPoint() / 64 != 0)
                        node->routeActionReadyRef() = 1;
                }
                return;
            }

            engineChainHead()->StartMove();
            return;
        }

        if (action == 0)
            engineChainHead()->resetEngineChainMovement();
    }

    int SPRITE::inheritAdjacentEngineCommand() noexcept
    {
        SPRITE* const next = engineChainNextRef();
        if (next && (next->m_runtimeFlags & SPRITE::CommandBitsMask) != 0u)
            return SetCommand(next->commandIndex(), next->m_goalSprite);

        SPRITE* const previous = engineChainPreviousRef();
        if (previous && (previous->m_runtimeFlags & SPRITE::CommandBitsMask) != 0u)
        {
            return SetCommand(next->commandIndex(),
                              next->m_goalSprite);
        }

        return SetCommand(0, nullptr);
    }

    void SPRITE::CreateChildForAnimation()
    {
        VID* childVid = m_vid->childVid[m_currentAnimation];
        const std::int32_t rawChildCount = static_cast<std::int32_t>(m_vid->noChild[m_currentAnimation]);
        const std::int32_t sign = rawChildCount < 0 ? -1 : 0;
        int childCount = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(rawChildCount) ^ static_cast<std::uint32_t>(sign)) -
            static_cast<std::uint32_t>(sign));
        // nChildVid is a layout selector consumed inside the loop; zero is a
        // valid deterministic layout and must not suppress child creation.
        if (!childVid)
            return;
        if (childVid->isNotCreateAsChild())
            return;

        MAP* const map = mapOwner();

        const int steppedDirection =
            (childVid->properties() & P_NOTCHANGELINKERCOOR) != 0u
                ? 0
                : quantizeDirectionForVid(
                      m_direction.Int(),
                      static_cast<int>(m_vid->directionQuantizationOffset()),
                      static_cast<int>(m_vid->noDir));

        int startOrdinal = 0;
        const WEAPON* const weaponRecord = m_vid->weaponRecord();
        if (childCount == 2 && weaponRecord != nullptr &&
            (*reinterpret_cast<const std::int32_t*>(weaponRecord->raw.data() + 4) & 0x10) != 0)
        {
            DWORD nextFlags = m_runtimeFlags;
            if ((nextFlags & ChildSpawnToggleFlag) != 0)
            {
                nextFlags &= ~0x00000200u;
                startOrdinal = 1;
            }
            else
                childCount = 1;
            nextFlags ^= ChildSpawnToggleFlag;
            m_runtimeFlags = nextFlags;
        }

        for (int ordinal = startOrdinal; ordinal < childCount; ++ordinal)
        {
            VECTOR offset{};
            float projectileBaseX = 0.0f;
            float projectileBaseY = 0.0f;
            const float childX = m_vid->childX[m_currentAnimation];
            const float childY = m_vid->childY[m_currentAnimation];
            const float primarySin = directionSin(steppedDirection);
            const float primaryCos = directionCos(steppedDirection);
            const float auxiliarySin = directionSinAux(steppedDirection);
            const float auxiliaryCos = directionCosAux(steppedDirection);
            if (static_cast<std::int32_t>(m_vid->nChildVid[m_currentAnimation]) >= 0)
            {
                if (ordinal == 1)
                {
                    projectileBaseX = -(primaryCos * childX);
                    projectileBaseY = -(auxiliarySin * childX);
                    offset.x = projectileBaseX + primarySin * childY;
                    offset.y = projectileBaseY - auxiliaryCos * childY;
                }
                else if (ordinal == 2)
                {
                    offset.x = primarySin * childY;
                    offset.y = auxiliaryCos * childY;
                }
                else
                {
                    projectileBaseX = primaryCos * childX;
                    projectileBaseY = auxiliarySin * childX;
                    offset.x = projectileBaseX + primarySin * childY;
                    offset.y = projectileBaseY - auxiliaryCos * childY;
                }
                offset.z = m_vid->childZ[m_currentAnimation];
            }
            else
            {
                constexpr float kRand32767 = 0.000030518509f;
                if (m_vid->spriteClassId() == 23u &&
                    childX == 0.0f &&
                    childY == 0.0f)
                {
                    const REGION* const region = static_cast<const REGION*>(this);
                    if ((region->regionFlags() & REGION::FullViewportFlag) == 0u)
                    {
                        const float width = region->regionWidth();
                        const float height = region->regionHeight();
                        offset.x = width * 0.5f - static_cast<float>(std::rand()) * width * kRand32767;
                        offset.y = height * 0.5f - static_cast<float>(std::rand()) * height * kRand32767 + Z();
                    }
                    else
                    {
                        offset.x = static_cast<float>(std::rand()) * map->SizeX() * kRand32767 - X();
                        offset.y = static_cast<float>(std::rand()) * map->SizeY() * kRand32767 - Y() + Z();
                    }
                    offset.z = m_vid->childZ[m_currentAnimation];
                }
                else
                {
                    const float localX = childX - static_cast<float>(std::rand()) * (childX + childX) * kRand32767;
                    const float localY = childY - static_cast<float>(std::rand()) * (childY + childY) * kRand32767;
                    projectileBaseX = -(localX * primaryCos);
                    projectileBaseY = -(localX * auxiliarySin);
                    offset.x = localY * primarySin + projectileBaseX;
                    offset.y = projectileBaseY - localY * auxiliaryCos;
                    offset.z = m_vid->childZ[m_currentAnimation];
                }
            }
            const VECTOR target(m_xyz.x + offset.x, m_xyz.y + offset.y, m_xyz.z + offset.z);

            if (childVid->spriteClassId() == B_UNIT &&
                GlobalHashQueryCellCollisionByVid(*map, childVid, target.x, target.y, target.z) != nullptr)
            {
                continue;
            }

            if (m_currentAnimation == 8 && goalSprite() == nullptr)
            {
                continue;
            }

            ANGLE childDirection = m_direction;
            if ((childVid->property & P_RANDBIRTH) != 0)
                childDirection = ANGLE(std::rand() & 0xFF);

            SPRITE* child = map->CreateSpriteViaFactory(childVid,
                                                                    target,
                                                                    childDirection,
                                                                    this,
                                                                    false);
            if (child)
            {
                if (m_currentAnimation == 8)
                {
                    SPRITE* const goal = goalSprite();
                    const bool hasOwner = goal != nullptr;
                    const bool ownerHasRealVid = hasOwner && goal->Vid() != MAP::NullVid();
                    const int parentWeaponFlags =
                        *reinterpret_cast<const std::int32_t*>(weaponRecord->raw.data() + 0x04);
                    const bool weaponActionBit20 = (parentWeaponFlags & 0x20) != 0;
                    if (hasOwner && weaponActionBit20 && ownerHasRealVid)
                    {
                        child->setAttackCommandForTarget(goal);
                        child->StartMove();
                    }
                    else if (hasOwner)
                    {
                        const float projectileRadius =
                            *reinterpret_cast<const float*>(weaponRecord->raw.data() + 0x1C);
                        float helperX = goal->X() + projectileRadius + projectileBaseX;
                        float helperY = goal->Y() + projectileRadius + projectileBaseY;
                        const float ownerZ = goal->Z();

                        for (int attempt = 0; attempt < 5; ++attempt)
                        {
                            const float randomX = static_cast<float>(std::rand()) * projectileRadius * 0.000061037019f;
                            const float randomY = static_cast<float>(std::rand()) * projectileRadius * 0.000061037019f;
                            const float candidateX = goal->X() + projectileRadius + projectileBaseX - randomX;
                            const float candidateY = goal->Y() + projectileRadius + projectileBaseY - randomY;

                            const float candidateGround = map->GetGroundZ(VECTOR2{candidateX, candidateY});
                            if (ownerZ > candidateGround)
                            {
                                const float midX = (goal->X() + candidateX) * 0.5f;
                                const float midY = (goal->Y() + candidateY) * 0.5f;
                                const float midpointGround = map->GetGroundZ(VECTOR2{midX, midY});
                                if (ownerZ > midpointGround)
                                {
                                    helperX = candidateX;
                                    helperY = candidateY;
                                    break;
                                }
                            }

                            helperX = candidateX;
                            helperY = candidateY;
                        }

                        SPRITE* const projectileOwner = new (std::nothrow) SPRITE(
                            map, MAP::NullVid(), VECTOR(helperX, helperY, ownerZ), ANGLE(0), nullptr);

                        child->setAttackCommandForTarget(projectileOwner);
                        child->StartMove();
                    }
                }
            }
        }

        if (m_currentAnimation == 8 && (m_runtimeFlags & ChildSpawnToggleFlag) == 0u)
        {
            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x10u || commandBits == 0x14u)
            {
                const bool requireEndFrame =
                    ((m_vid->properties() & P_TRACK) != 0u) ||
                    ((childVid->properties() & P_BIRTHASSMOKE) != 0u);
                if (!requireEndFrame || m_currentFrame == m_currentFrameEnd)
                {
                    SPRITE* const goal = goalSprite();
                    SPRITE* const parent = childBacklink();
                    if (goal && parent && parent->goalSprite() == goal)
                    {
                        const DWORD parentCommandBits =
                            parent->m_runtimeFlags & SPRITE::CommandBitsMask;
                        if (parentCommandBits == 0x10u || parentCommandBits == 0x14u)
                            parent->SetCommand(0, nullptr);

                        if (parent->Vid()->spriteClassId() == B_ENGINE &&
                            ((parent->m_runtimeFlags & SPRITE::CommandBitsMask) == 0x74u))
                        {
                            parent->dispatchEnginePrivateCommand(0x1E, 0, 0, 0);
                        }
                    }

                    if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u)
                        m_actionTimer = 0u;

                    if (SPRITE* const goal = m_goalSprite)
                    {
                        (void)goal->ReleaseListReference();
                        m_goalSprite = nullptr;
                    }

                    if (SPRITE* const linkedChild = m_childChain)
                    {
                        VID* const linkedChildVid = linkedChild->m_vid;
                        if (linkedChildVid == m_vid->linkVid &&
                            linkedChildVid->hasWeaponChildDescriptor() != 0u &&
                            linkedChildVid->weaponCount() != 0u)
                        {
                            (void)linkedChild->SetCommand(0, nullptr);
                        }
                    }

                    m_runtimeFlags &= ~SPRITE::CommandBitsMask;
                }
            }
        }

        return;
    }

    int SPRITE::dispatchDebugOverlay()
    {
        VID* const vid = Vid();
        if (vid->spriteClassId() == B_REGION)
        {
            DrawDebugOverlay();

            return 0;
        }
        return DrawDebugOverlay(*GRAPH::CurrentGraph());
    }

    void SPRITE::CreateChild()
    {
        CreateChildForAnimation();
    }

    SPRITE::~SPRITE()
    {
        destroyBaseSpriteState();
    }

    void DeleteSpriteThroughVirtualDeletingDestructor(SPRITE* sprite) noexcept
    {
        if (!sprite)
            return;
        if (MAP* const owner = sprite->mapOwner())
            owner->ReleaseSpriteForScalarDeletingDestructor(sprite);
        delete sprite;
    }

    SPRITE* SPRITE::commandSpriteScalarDeletingDestructor(unsigned char flags) noexcept
    {
        SPRITE* const self = this;
        destroyCommandSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SPRITE::destroyCommandSpriteState() noexcept
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (hostState().owner)
            mapOwner()->releaseSpriteReferencesHost(this);
#endif
        destroyBaseSpriteState();
    }

    SPRITE* SPRITE::baseSpriteScalarDeletingDestructor(unsigned char flags) noexcept
    {
        SPRITE* const self = this;
        destroyBaseSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    SPRITE* SPRITE::linkedSpriteScalarDeletingDestructor(unsigned char flags) noexcept
    {
        SPRITE* const self = this;
        detachFromChildChain();
        destroyBaseSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SPRITE::destroyBaseSpriteState()
    {
        VID* const vid = m_vid;
        const bool isEmptyVid = vid == MAP::NullVid();
        const int nvid = vid->nVid;

        // [VID+0x3FC] DESTROY callback.
        const int destroyFunction = vid->destroyScriptFunction();
        if (destroyFunction >= 0)
        {
            const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            (void)core::Application::callScriptFunction(destroyFunction, spriteArg, 0);
        }

        if (vid->gridDotCount() > 0)
            vid->ResetGridZ(this);

        if (isEmptyVid && m_listReferenceCount != 0)
            LOG::ResourceError("SPRITE %i", 4, "noRef for SPRITE with EmptyVid", m_listReferenceCount, nvid);

        if (m_listReferenceCount > 1)
        {
            GlobalSpriteHashMap()->removeSprite(this);
            if (m_listReferenceCount > 1)
            {
#ifdef _WIN32
                win::applicationWinInstance()->transferFrom(this);
#else
                mapOwner()->releaseSpriteReferencesHost(this);
#endif
            }
        }

        vid->decrementSpriteCountForArmy(armyIndex());
        setGoalSprite(nullptr);

        releaseBestTargetSprite();
        deleteChildChain();
        clearChildBacklink();

        if (!isEmptyVid)
        {
            removeFromDrawBucketsRecursive();
            --m_listReferenceCount;
        }

        if (m_listReferenceCount != 0)
            LOG::ResourceError("SPRITE %i", 10, "Reference count non zero after delete", m_listReferenceCount, nvid);

        releaseActionAuxState();

        if (m_bestTargetSprite)
        {
            const int ptrNvid = m_bestTargetSprite->Vid()->nVid;
            LOG::ResourceError("SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, ptrNvid);
        }

        releaseCommandRecordsRetailTail();

        releaseHostState();
    }

    const Gamma& SPRITE::GetGamma() const
    {
        return hostState().gamma;
    }

    Gamma SPRITE::GetUniqueGamma() const
    {
        return hostState().gamma;
    }

    void SPRITE::SetGamma(const Gamma& value)
    {
        hostState().gamma = value;
    }

    void SPRITE::addGamma(const Gamma& gammaDelta)
    {
        hostState().gamma = hostState().gamma.saturatedAdd(gammaDelta);
    }

    void SPRITE::MoveHashBeforeCoordinateWrite(float nextX, float nextY)
    {
        const VECTOR target(nextX, nextY, m_xyz.z);
        GlobalSpriteHashMap()->moveSprite(this, nextX, nextY);
    }

    void SPRITE::performBaseMovementTact() noexcept
    {
        VID* const vid = m_vid;
        if (!vid->movementTactEnabled())
            return;

        float candidateX = X();
        float candidateY = Y();
        float candidateZ = Z();
        computeNextMovementPosition(&candidateX, &candidateY, &candidateZ);

#if defined(_MSC_VER) && defined(_M_IX86)
        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner());
#else
        MAP* const map = mapOwner();
#endif
        const float currentGroundZ = map->sampleTerrainHeight(X(), Y());
        const float candidateGroundZ = map->sampleTerrainHeight(candidateX, candidateY);
        const float moveUpLimitZ = candidateGroundZ + vid->topZValue();

        const DWORD property = vid->properties();
        if ((vid->spriteTypeId() & 0x00000200u) != 0u &&
            (property & P_GRAVITY) != 0u &&
            candidateGroundZ >= candidateZ &&
            Z() >= currentGroundZ)
        {
            candidateZ = Z();
        }
        else if (Z() != candidateZ)
        {
            if ((property & P_GRAVITY) == 0u && moveUpLimitZ != 0.0f)
            {
                bool clampCandidate = false;
                bool clearZSpeed = false;
                if (x87OrderedLess(Z(), moveUpLimitZ))
                {
                    if (!(candidateZ < moveUpLimitZ))
                    {
                        clampCandidate = true;
                        clearZSpeed = true;
                    }
                }
                else if (x87OrderedGreater(Z(), moveUpLimitZ))
                {
                    if (!(candidateZ >= moveUpLimitZ))
                    {
                        clampCandidate = true;
                        clearZSpeed = true;
                    }
                }
                else if ((property & 0x08000000u) == 0u)
                {
                    // Ordered currentZ == moveUpLimitZ.
                    clearZSpeed = true;
                }

                if (clampCandidate)
                    candidateZ = moveUpLimitZ;
                if (clearZSpeed)
                    setZSpeedDirect(0.0f);
            }
        }

        if (m_goalSprite != nullptr && m_speed != 0.0f &&
            (m_runtimeFlags & CrossedGoalAxesMask) == CrossedGoalAxesMask)
        {
            Stop();
        }

        if (X() != candidateX || Y() != candidateY)
        {
            if (CanPlaceWithCrush(candidateX, candidateY, candidateZ) != nullptr)
            {
                m_runtimeFlags |= 0x00000400u;
                setZSpeedDirect(0.0f);
                setSpeedDirect(0.0f);
            }
            else
            {
                ChangeCoor(candidateX, candidateY, candidateZ);
            }
        }

        if (Z() != candidateZ)
            ChangeCoor(X(), Y(), candidateZ);
    }

    int SPRITE::steerAwayFromMapBoundary(float x, float y) noexcept
    {
        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        if (x < 0.0f)
        {
            RotateTact(ANGLE(0x40u), deltaMs);
            return 1;
        }
        if (y < 0.0f)
        {
            RotateTact(ANGLE(0x80u), deltaMs);
            return 1;
        }

        const float appWidth = applicationWorldFloatAt(core::retail_application_layout::MapExtentX);
        const float appHeight = applicationWorldFloatAt(core::retail_application_layout::MapExtentY);
        if (x >= appWidth)
        {
            RotateTact(ANGLE(0xC0u), deltaMs);
            return 1;
        }
        if (y >= appHeight)
        {
            RotateTact(ANGLE(0u), deltaMs);
            return 1;
        }
        return 0;
    }

    void SPRITE::computeNextMovementPosition(float* xOut, float* yOut, float* zOut) noexcept
    {
        m_runtimeFlags &= ~0x00000400u;

        *xOut = m_xyz.x;
        *yOut = m_xyz.y;
        *zOut = m_xyz.z;

        VID* const vid = m_vid;
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 4u && (m_runtimeFlags & MovementStartedFlag) == 0u)
        {
            LOG::ResourceError("SPRITE %i", 10, "Move without StartMove()", 0,
                               vid ? vid->nvid() : -1);
            StartMove();
        }
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 4u && m_goalSprite == nullptr)
        {
            LOG::ResourceError("SPRITE %i", 10, "Move without goal", 0,
                               vid ? vid->nvid() : -1);
            Stop();
        }

        const std::uint32_t rawDelta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const std::int32_t deltaMs = static_cast<std::int32_t>(rawDelta);
        constexpr std::uint32_t kNoSpeedBits = 0x497423F0u; // 999999.0f

        if ((m_runtimeFlags & MovementStartedFlag) != 0u)
        {
            const float maxSpeed = m_actionAuxState
                ? spriteFloatFromBits(m_actionAuxState->maxSpeedBits)
                : vid->maxSpeedValue();

            if (x87OrderedGreater(maxSpeed, m_speed))
            {
                const float accel = vid->accelerationValue();
                if (spriteBitsEqual(accel, kNoSpeedBits))
                {
                    m_speed = maxSpeed;
                }
                else if (advanceAccelerationStep(deltaMs, accel, maxSpeed, m_speed))
                {
                    m_speed = maxSpeed;
                }
            }
        }
        else if (x87OrderedGreater(m_speed, 0.0f))
        {
            const float slow = vid->slowValue();
            if (spriteBitsEqual(slow, kNoSpeedBits) ||
                advanceDecelerationStep(deltaMs, slow, m_speed))
            {
                m_speed = 0.0f;
            }
        }

        GRAPH* const graph = GRAPH::CurrentGraph();
        const bool speedIsZeroRoute = (m_speed == 0.0f);
        const float windSpeed = graph->windSpeed();
        const bool windIsZeroRoute = (windSpeed == 0.0f);
        const DWORD property = vid->properties();
        const bool windProperty = (property & P_WIND) != 0u;
        if (!speedIsZeroRoute || (!windIsZeroRoute && windProperty))
        {
            if (spriteBitsEqual(m_speed, kNoSpeedBits))
            {
                SPRITE* const target = m_goalSprite;
                if (target && ((m_runtimeFlags & CrossedGoalXFlag) == 0u ||
                               (m_runtimeFlags & CrossedGoalYFlag) == 0u))
                {
                    float targetX = target->m_xyz.x;
                    float targetY = target->m_xyz.y;
                    float targetZ = target->m_xyz.z;
                    const int traceHit = traceMovementCollisionTo(&targetX, &targetY, &targetZ);
                    ChangeCoor(targetX, targetY, targetZ);
                    if (traceHit)
                        m_runtimeFlags |= 0x00000400u;
                    *xOut = targetX;
                    *yOut = targetY;
                    *zOut = targetZ;
                    m_runtimeFlags |= CrossedGoalAxesMask;
                    return;
                }
                m_runtimeFlags |= CrossedGoalAxesMask;
            }
            else
            {
                int movementDirection = m_direction.Int();
                if ((property & P_MOVEWITHANYDIRECTION) != 0u && m_goalSprite != nullptr)
                {
                    movementDirection = RetailDirectionFromFloatXY(
                        m_goalSprite->X() - X(), m_goalSprite->Y() - Y()).Int();
                }
                const std::uint32_t direction = static_cast<std::uint32_t>(movementDirection) & 0xFFu;
                advancePlanarPosition(deltaMs, m_speed,
                                    spriteFloatFromBits(g_retailDirectionTrigWindow[512u + direction]),
                                    spriteFloatFromBits(g_retailDirectionTrigWindow[768u + direction]),
                                    *xOut, *yOut);
                if (windProperty)
                {
                    const int windDirection = static_cast<int>(graph->windDirection());
                    advancePlanarPosition(deltaMs, windSpeed,
                                        directionSin(windDirection), directionCos(windDirection),
                                        *xOut, *yOut);
                }
            }
        }

        if ((property & P_GRAVITY) != 0u)
        {
            BASE_CONSTANTS* const constants = GlobalBaseConstants();
            const float gravity = spriteFloatFromBits(constants->raw[2]);
            applyGravityStep(deltaMs, gravity, m_zSpeed);
        }
        else if ((property & P_GRAVITY2) != 0u)
        {
            BASE_CONSTANTS* const constants = GlobalBaseConstants();
            const float gravity = spriteFloatFromBits(constants->raw[3]);
            applyGravityStep(deltaMs, gravity, m_zSpeed);
        }
        advanceVerticalPosition(deltaMs, m_zSpeed, *zOut);

        SPRITE* const target = m_goalSprite;
        if (!target)
            return;

        constexpr float kGoalTolerance = 0.5f;
        const float finishX = *xOut;
        const float goalX = target->m_xyz.x;
        if (finishX > m_xyz.x)
        {
            if (goalX >= m_xyz.x - kGoalTolerance &&
                goalX <= finishX + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalXFlag;
        }
        else
        {
            if (goalX >= finishX - kGoalTolerance &&
                goalX <= m_xyz.x + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalXFlag;
        }

        const float finishY = *yOut;
        const float goalY = target->m_xyz.y;
        if (finishY > m_xyz.y)
        {
            if (goalY >= m_xyz.y - kGoalTolerance &&
                goalY <= finishY + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalYFlag;
        }
        else
        {
            if (goalY >= finishY - kGoalTolerance &&
                goalY <= m_xyz.y + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalYFlag;
        }

    }

    void SPRITE::ChangeCoor(float x, float y, float z) noexcept
    {
        const float deltaX = x - m_xyz.x;
        const float deltaY = y - m_xyz.y;
        const float deltaZ = z - m_xyz.z;

        for (SPRITE* node = this; node; node = node->m_childChain)
        {
            const VECTOR before = node->m_xyz;
            const VECTOR target(before.x + deltaX, before.y + deltaY, before.z + deltaZ);

            if (node->m_vid->gridDotCount() > 0)
                node->m_vid->ResetGridZ(node);

            if ((node->m_vid->property & P_HASH) != 0)
                node->MoveHashBeforeCoordinateWrite(target.x, target.y);

            const std::uint32_t realTime = core::RealTimeMilliseconds();
            if (node->m_actionAuxState && node->m_actionAuxState->lastUpdateTime != realTime)
            {
                node->m_actionAuxState->lastUpdateTime = realTime;
                node->m_actionAuxState->sourceX = node->m_xyz.x;
                node->m_actionAuxState->sourceY = node->m_xyz.y;
                node->m_actionAuxState->sourceZ = node->m_xyz.z;
            }

            node->m_xyz = target;

            if (node->m_vid->gridDotCount() > 0)
                node->m_vid->SetGridZ(node);
        }
    }

    float SPRITE::linkerX() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        float value = 0.0f;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::SharedPrimaryState, sizeof(value));
        return value;
#else
        return hostState().linkerX;
#endif
    }

    float SPRITE::linkerY() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        float value = 0.0f;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::SharedSecondaryState, sizeof(value));
        return value;
#else
        return hostState().linkerY;
#endif
    }

    float SPRITE::linkerZ() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        float value = 0.0f;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::ExtendedStateBase, sizeof(value));
        return value;
#else
        return hostState().linkerZ;
#endif
    }

    int SPRITE::linkerDirection() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        int value = 0;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::LegacyCommandState1, sizeof(value));
        return value;
#else
        return hostState().linkerDirection;
#endif
    }

    SPRITE* SPRITE::linkerOwner() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        SPRITE* value = nullptr;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::AmmoFixedPoint, sizeof(value));
        return value;
#else
        return hostState().linkerOwner;
#endif
    }

    void SPRITE::setLinkerState(float x74, float y78, float z7C, int direction80, SPRITE* owner84) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::SharedPrimaryState, &x74, sizeof(x74));
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::SharedSecondaryState, &y78, sizeof(y78));
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::ExtendedStateBase, &z7C, sizeof(z7C));
        const int direction = direction80 & 0xFF;
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::LegacyCommandState1, &direction, sizeof(direction));
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::AmmoFixedPoint, &owner84, sizeof(owner84));
#else
        hostState().linkerX = x74;
        hostState().linkerY = y78;
        hostState().linkerZ = z7C;
        hostState().linkerDirection = direction80 & 0xFF;
        hostState().linkerOwner = owner84;
#endif
    }

    int SPRITE::AddListReference()
    {
        m_listReferenceCount = spriteAdd32Wrap(m_listReferenceCount, 1);
        return m_listReferenceCount;
    }

    int SPRITE::ReleaseListReference()
    {
        m_listReferenceCount = spriteSub32Wrap(m_listReferenceCount, 1);
        const int refs = m_listReferenceCount;
        if (refs > 0)
            return refs;
        if (refs >= 0)
        {
            DeleteSpriteThroughVirtualDeletingDestructor(this);
            return 0;
        }

        const int nvid = m_vid ? m_vid->nVid : -1;
        (void)logFileLoggerResourceError(
            g_fileLogger,
            "SPRITE[%i](%i,%i,%i)",
            4,
            "noRef at Release",
            refs,
            nvid,
            static_cast<int>(m_xyz.x),
            static_cast<int>(m_xyz.y),
            static_cast<int>(m_xyz.z));
        return 0;
    }

    void SPRITE::setOldAddress(int value)
    {
        hostState().oldAddress = value;
        if (!hostState().number)
            hostState().number = value;
    }

    void SPRITE::setNumber(int value)
    {
        hostState().number = value;
    }

    std::uint32_t SPRITE::rawResolveOldSpriteHandleLow32(int oldAddress) const noexcept
    {
        MAP* const owner = mapOwner();
        if (!owner)
            return 0u;
        SPRITE* const resolved = owner->ResolveOldSpriteHandle(oldAddress);
        return resolved ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved)) : 0u;
    }

    void SPRITE::ChangeAnimation(int animationId)
    {
        if (animationId >= VID::NO_ANIMATION)
        {
            const int nvid = m_vid ? m_vid->nVid : -1;
            LOG::Write(
                "!!!ERROR!!!SPRITE[%i](%i,%i,%i) new_animation(%i) in ChangeAnimation",
                nvid,
                static_cast<int>(m_xyz.x),
                static_cast<int>(m_xyz.y),
                static_cast<int>(m_xyz.z),
                animationId);
            return;
        }

        if (SPRITE* child = childChain())
        {
            VID* childVid = child->m_vid;
            if (childVid == m_vid->linkVid)
            {
                const bool ordinaryChildMayFollow =
                    childVid->weaponCount() == 0u &&
                    (childVid->property & P_WIND) == 0u &&
                    child->m_currentAnimation != 8 &&
                    child->m_currentAnimation < 15;
                if (ordinaryChildMayFollow || animationId == 15 || animationId == 16)
                    child->ChangeAnimation(animationId);
            }
        }

        if (m_currentAnimation == animationId)
            return;

        m_runtimeFlags &= ~0x00000200u;

        int frameDirection = m_direction.Int() & 0xFF;
        if (m_zSpeed != 0.0f && (m_vid->property & P_VERTDIR) != 0)
        {
            int projectedDirection = frameDirection;
            (void)projectVerticalMotionDirection(
                frameDirection, m_speed, m_zSpeed, projectedDirection);
            frameDirection = projectedDirection;
        }

        const std::int32_t baseFrame = static_cast<std::int32_t>(m_vid->animationBaseFrame[animationId]);
        const std::int32_t directionByte =
            static_cast<std::int32_t>((m_vid->directionQuantizationOffset() + frameDirection) & 0xFF);
        const std::uint32_t scaledDirection =
            static_cast<std::uint32_t>(spriteImul32Low(directionByte, static_cast<std::int32_t>(m_vid->noDir))) >> 8;
        const std::int32_t directionFrameOffset = spriteImul32Low(
            static_cast<std::int32_t>(scaledDirection),
            static_cast<std::int32_t>(m_vid->animationFrameCount[animationId]));
        const std::int32_t currentFrame = spriteAdd32Wrap(baseFrame, directionFrameOffset);

        m_currentFrame = currentFrame;
        m_currentAnimation = animationId;

        if (animationId >= 13 && m_vid->noAnimCadr[animationId] == 0)
        {
            m_currentFrameEnd = currentFrame;
            m_currentFrameBegin = currentFrame;
        }
        else
        {
            const std::int32_t count = static_cast<std::int32_t>(m_vid->animationFrameCount[animationId]);
            m_currentFrameEnd = spriteAdd32Wrap(spriteAdd32Wrap(currentFrame, count), -1);
            m_currentFrameBegin = currentFrame;
        }

    }

    void SPRITE::setCurrentAnimation(int value)
    {
        ChangeAnimation(value);
    }

    void SPRITE::ChangeSpeed(float value)
    {
        m_speed = value;
    }

    void SPRITE::ChangeZSpeed(float value)
    {
        m_zSpeed = value;
    }

    void SPRITE::SetTimer(DWORD value)
    {
        m_actionTimer = value;
    }

    void SPRITE::ChangeArmy(int value)
    {
        changeArmyBucket(value);
    }

    void SPRITE::Draw()
    {
        m_vid->Draw(this);
    }

    int SPRITE::DrawDebugOverlay(GRAPH& graph) const
    {
        const VID* const vid = Vid();
        const DWORD spriteType = vid->spriteTypeId();

        constexpr DWORD kBlack  = 0xFF000000u;
        constexpr DWORD kWhite  = 0xFFFFFFFFu;
        constexpr DWORD kGreen  = 0xFF00FF00u;
        constexpr DWORD kPurple = 0xFF8080FFu;
        constexpr DWORD kYellow = 0xFFFFFF00u;
        constexpr DWORD kGray   = 0xFF808080u;
        constexpr DWORD kRed    = 0xFFFF0000u;
        constexpr DWORD kBlue   = 0xFF0000FFu;

        DWORD color = kRed;
        if ((spriteType & U_TERRAIN) != 0u && (vid->properties() & P_HASH) != 0u)
            color = kBlack;
        else if (vid->renderLayer() == 8)
            color = kWhite;
        else if ((spriteType & U_UNIT) != 0u)
            color = kGreen;
        else if ((spriteType & U_AVIA) != 0u)
            color = kPurple;
        else if ((spriteType & U_OBJECT) != 0u)
            color = kYellow;
        else if ((spriteType & U_RAILWAY) == 0u)
        {
            if ((spriteType & U_CANNON) != 0u)
                color = kGray;
            else if (vid->spriteClassId() == B_FRAME)
                color = kWhite;
        }

        const core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        const float baseX = X() - drawState.cameraShiftX();
        const float baseY = Y() - Z() - drawState.cameraShiftY();

        if ((spriteType >= U_TERRAIN && spriteType <= U_CANNON) || spriteType == U_SPRITE)
        {
            graph.DrawRect(
                X() - vid->halfSizeX() - drawState.cameraShiftX(),
                Y() - Z() - vid->halfSizeY() - drawState.cameraShiftY(),
                X() + vid->halfSizeX() - drawState.cameraShiftX(),
                Y() - Z() + vid->halfSizeY() - drawState.cameraShiftY(),
                color);
        }

        if ((spriteType & (U_OBJECT | U_UNIT)) != 0u)
            graph.DrawLine(baseX, baseY - vid->sizeZ(), baseX, baseY, kBlue);

        char nvidText[32] = {};
#if defined(_MSC_VER)
        sprintf_s(nvidText, "%i", vid->nvid());
#else
        std::snprintf(nvidText, sizeof(nvidText), "%i", vid->nvid());
#endif
        return graph.drawTextColored(baseX + 1.0f, baseY, nvidText, kWhite);
    }

    void SPRITE::DrawSelectionOverlay(GRAPH& graph) const
    {
        if (isHiddenByCliping())
            return;

        const float sx = X() - graph.cameraX();
        const float sy = Y() - Z() - graph.cameraY();
        graph.DrawRect(sx - 8.0f, sy - 8.0f, sx + 8.0f, sy + 8.0f, 0x0000FFFFu);
        graph.DrawText(sx + 10.0f, sy - 8.0f, "%i", getNumber());
    }

    int SPRITE::renderFrameOffsetForClock(std::uint32_t clockMilliseconds) const
    {
        if (!m_vid || m_currentAnimation < 0 || m_currentAnimation >= VID::NO_ANIMATION)
            return 0;

        const int count = m_vid->animationFrameCount[m_currentAnimation];
        if (count <= 1)
            return 0;

        const int speed = m_vid->hostFrameSpeedStorage(m_currentAnimation);
        if (speed <= 0)
            return 0;

        return static_cast<int>((clockMilliseconds / static_cast<std::uint32_t>(speed)) % static_cast<std::uint32_t>(count));
    }

    int SPRITE::renderFrameIndexForClock(std::uint32_t clockMilliseconds) const
    {
        if (!m_vid || m_currentAnimation < 0 || m_currentAnimation >= VID::NO_ANIMATION)
            return -1;

        const int count = m_vid->animationFrameCount[m_currentAnimation];
        if (count <= 0 || m_vid->noDir <= 0)
            return -1;

        const int realDirection = m_vid->RealDirection(m_direction);
        const int startFrame = m_vid->animationBaseFrame[m_currentAnimation] + realDirection * count;
        const int frame = startFrame + renderFrameOffsetForClock(clockMilliseconds);
        if (frame < 0 || frame >= m_vid->noCadr)
            return -1;
        return frame;
    }

    int SPRITE::SizeTo(const VECTOR2& target) const
    {
        const int dx = static_cast<int>(target.x - m_xyz.x);
        const int dy = static_cast<int>(target.y - m_xyz.y);
        return IntegerSquareRoot(dx * dx + dy * dy);
    }

    ANGLE SPRITE::DirectionTo(const VECTOR2& target) const
    {
        const int dx = static_cast<int>(target.x - m_xyz.x);
        const int dy = static_cast<int>(target.y - m_xyz.y);
        return ANGLE::FromXY(dx, dy);
    }

    int SPRITE::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchActionOpcode(
            static_cast<std::uint32_t>(opcode),
            static_cast<int>(argument1Carrier),
            argument2Carrier,
            argument3Carrier);
    }

    SpriteCommandRecord SPRITE::buildCommandRecord(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        SpriteCommandRecord out{};
        out.opcode = opcode;
        out.argument1 = static_cast<std::uint32_t>(argument1);
        out.argument2 = static_cast<std::uint32_t>(argument2);
        out.argument3 = static_cast<std::uint32_t>(argument3);
        return out;
    }

    void SPRITE::serializeCommandRecordsText(STRING& out) const
    {
        m_commandStack.serializeCommandRecordsText(out);
    }

    std::string SPRITE::serializeCommandRecordsText() const
    {
        return m_commandStack.serializeCommandRecordsText();
    }

    void SPRITE::parseCommandRecordsText(const STRING& text)
    {
        m_commandStack.parseCommandRecordsText(text);
    }

    void SPRITE::queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        m_commandStack.queueCommandBeforeStopSentinel(opcode, argument1, argument2, argument3);
    }

    int SPRITE::dispatchActionOpcode(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {

        int returnValue = 0;

        switch (opcode & 0xFFu)
        {
        case static_cast<std::uint32_t>(AnimationCode::ANI_DEATH):
        {
            VID* const sourceVid = m_vid;
            const int damageRaw = sourceVid->deathDamageMinimumRawBits();

            returnValue = 0;
            if (damageRaw == 0)
            {

                break;
            }

            setAnimationFrameTime(0);
            const float deathRange = sourceVid->deathRangeValue();
            const float rangeX = sourceVid->halfSizeX() + deathRange;
            const float rangeY = sourceVid->halfSizeY() + deathRange;
            const float sourceSizeZ = sourceVid->sizeZ();
            const float rangeZ = x87LessEqualOrUnordered(20.0f, sourceSizeZ)
                ? sourceSizeZ
                : 20.0f;

            SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
            for (SPRITE* candidate = hash->firstSpriteInBox(
                     X() - rangeX, Y() - rangeY, X() + rangeX, Y() + rangeY);
                 candidate;
                 candidate = hash->nextSpriteInBox())
            {
                if (candidate == this)
                    continue;

                VID* const candidateVid = candidate->Vid();
                if (candidateVid->maximumHp() == 0)
                    continue;

                if ((sourceVid->properties() & P_NOTDAMAGEFORFRIEND) != 0u &&
                    (sameArmy(*candidate)))
                {
                    continue;
                }

                if (!x87SumGreaterThanAbsDiffOrdered(
                        rangeX, candidateVid->halfSizeX(), X(), candidate->X()) ||
                    !x87SumGreaterThanAbsDiffOrdered(
                        rangeY, candidateVid->halfSizeY(), Y(), candidate->Y()) ||
                    !x87SumGreaterThanAbsDiffOrdered(
                        rangeZ, candidateVid->sizeZ(), Z(), candidate->Z()))
                {
                    continue;
                }

                const float midpointX = (candidate->X() + X()) * 0.5f;
                const float midpointY = (candidate->Y() + Y()) * 0.5f;
                const float midpointGround =
                    mapOwner()->GetGroundZ(VECTOR2{midpointX, midpointY});
                if (x87SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), midpointGround))
                    continue;

                const float nearSourceX = spriteWeightedQuarterF32(X(), candidate->X());
                const float nearSourceY = spriteWeightedQuarterF32(Y(), candidate->Y());
                const float nearSourceGround =
                    mapOwner()->GetGroundZ(VECTOR2{nearSourceX, nearSourceY});
                if (x87SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), nearSourceGround))
                    continue;

                const float nearCandidateX = spriteWeightedQuarterF32(candidate->X(), X());
                const float nearCandidateY = spriteWeightedQuarterF32(candidate->Y(), Y());
                const float nearCandidateGround =
                    mapOwner()->GetGroundZ(VECTOR2{nearCandidateX, nearCandidateY});
                if (x87SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), nearCandidateGround))
                    continue;

                int damage = damageRaw;
                if ((sourceVid->properties() & P_RADIALDAMAGE) != 0u)
                {
                    if (!computeFalloffDamage(
                            X(), Y(), candidate->X(), candidate->Y(),
                            deathRange, damageRaw, damage))
                        continue;
                }

                candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE,
                    damage,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                    0);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ATTACK):
        {
            SPRITE* const owner = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            setAttackCommandForTarget(owner);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_MOVE):
        {
            SPRITE* const helper = new (std::nothrow) SPRITE(
                mapOwner(), MAP::NullVid(),
                VECTOR(spriteFildToF32(argument1), spriteFildToF32(argument2), spriteFildToF32(argument3)),
                ANGLE(0), nullptr);
            Move(helper);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO):
        {
            SPRITE* const owner = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            Move(owner);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_BUILD_UNIT):
        {
            int createNvid = argument1;
            if (createNvid == 0)
                createNvid = dispatchVirtualAction(
                    static_cast<std::uint32_t>(InternalActionCode::RandomItemBySpriteType),
                    4, 0, 0);

            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            if (createNvid <= 0 || createNvid >= table.count())
                return 0;

            VID* const createVid = table.slot(createNvid);
            if (!createVid)
                return 0;

            const auto retailSpawnCoordinate = [](float ownerCoordinate, int rawArgument) noexcept -> float
            {
                if (rawArgument == 0)
                    return ownerCoordinate;
                if (rawArgument > 0)
                    return spriteFildToF32(rawArgument);

                const std::uint32_t span =
                    static_cast<std::uint32_t>(1) - static_cast<std::uint32_t>(rawArgument);
                const std::uint32_t randomPart = span != 0u
                    ? static_cast<std::uint32_t>(std::rand()) % span
                    : 0u;
                const std::int32_t doubled = static_cast<std::int32_t>(randomPart * 2u);
                return ownerCoordinate - spriteFildToF32(rawArgument) - spriteFildToF32(doubled);
            };

            const float createX = retailSpawnCoordinate(m_xyz.x, argument2);
            const float createY = retailSpawnCoordinate(m_xyz.y, argument3);
            SPRITE* const created = mapOwner()->CreateSpriteViaFactory(
                createVid,
                VECTOR(createX, createY, m_xyz.z),
                m_direction,
                this,
                false);
            if (created)
                copyCommandPrefixTo(created);
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_COOR_ATTACK):
        {
            VID* metricVid = m_vid;
            if (SPRITE* const child = childChain())
            {
                VID* const childVid = child->Vid();
                if (childVid == m_vid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u)
                {
                    metricVid = childVid;
                }
            }

            const int weaponType = metricVid->weaponTypeMask();
            const float x = spriteFildToF32(argument1);
            float yProbe = spriteFildToF32(argument2);
            float z = 0.0f;
            int helperY = argument2;
            if (weaponType == 8)
            {
                yProbe = spriteFildAddF32(argument2, 80.0f);
                const float ground = mapOwner()->GetGroundZ(VECTOR2{x, yProbe});
                const int zAsInt = spriteAddF32StoreAndFtolLow32(ground, 80.0f, z);
                helperY = spriteAdd32Wrap(helperY, zAsInt);
            }
            else
            {
                const float ground = mapOwner()->GetGroundZ(VECTOR2{x, yProbe});
                const int zAsInt = spriteAddF32StoreAndFtolLow32(ground, 19.0f, z);
                helperY = spriteAdd32Wrap(helperY, spriteAdd32Wrap(zAsInt, -19));
            }

            SPRITE* const helper = new (std::nothrow) SPRITE(
                mapOwner(), MAP::NullVid(), VECTOR(x, spriteFildToF32(helperY), z), ANGLE(0), nullptr);
            SetCommand(4, helper);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ADD_ITEM):
            appendCommandWordValue(argument1);
            return 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_DELETE_ITEM):
            return removeCommandWordValue(argument1);

        case static_cast<std::uint32_t>(ActionCode::ACT_HAVE_ITEM):
            return findLastCommandWord(argument1) >= 0 ? 1 : 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_DELETE_ALL_ITEM):
            clearCommandWordList();
            return 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ITEM):
            return commandWordAt(argument1);

        case static_cast<std::uint32_t>(InternalActionCode::RandomItemBySpriteType):
        {
            const std::uint32_t mask = argument1 != 0
                ? static_cast<std::uint32_t>(argument1)
                : 0x00FFFFFFu;
            if (!m_actionAuxState || !m_actionAuxState->items.values ||
                m_actionAuxState->items.count == 0u)
                return -1;

            const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            std::uint32_t matchCount = 0u;
            for (std::uint32_t i = 0; i < m_actionAuxState->items.count; ++i)
            {
                const int nvid = m_actionAuxState->items.values[i];
                VID* itemVid = MAP::NullVid();
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        itemVid = resolved;
                }
                if ((itemVid->spriteTypeId() & mask) != 0u)
                    ++matchCount;
            }
            if (matchCount == 0u)
                return -1;

            std::uint32_t selected = static_cast<std::uint32_t>(std::rand()) % matchCount;
            for (std::uint32_t i = 0; i < m_actionAuxState->items.count; ++i)
            {
                const int nvid = m_actionAuxState->items.values[i];
                VID* itemVid = MAP::NullVid();
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        itemVid = resolved;
                }
                if ((itemVid->spriteTypeId() & mask) == 0u)
                    continue;
                if (selected-- == 0u)
                    return nvid;
            }
            return -1;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_FLAGMAN_TRIGGER):
        {
            MAP* const firstOwner = MAP::Current();
            SPRITE* const firstControlled = firstOwner->flagmanSpriteForPlayer(
                static_cast<int>(core::ActivePlayerIndex()));
            if (firstControlled)
            {
                MAP* const secondOwner = MAP::Current();
                SPRITE* const controlled = secondOwner->flagmanSpriteForPlayer(
                    static_cast<int>(core::ActivePlayerIndex()));
                if (shouldSuppressFlagmanCommand(
                        argument1, argument2, argument3, controlled->X(), controlled->Y()))
                {

                    break;
                }
            }
            SpriteCommandRecord command = buildCommandRecord(opcode, argument1, argument2, argument3);
            m_commandStack.appendCommandRecord(command);

            break;
        }

        case 74:
        {
            SPRITE* const target = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            SetCommand(static_cast<int>((opcode >> 8) & 0xFFu), target);
            if (target)
            {
                (void)target->ReleaseListReference();
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_DESTROY_UNIT):
        {
            core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            if (argument1 >= 0 &&
                argument1 < vidTable.count() &&
                vidTable.slot(argument1) != nullptr)
            {
                SPRITE* const hit = core::Application::findSpriteAtPointByFilter(
                    *mapOwner(),
                    core::GlobalApplicationDrawDispatcherState(),
                    core::EncodeVidQueryFilter(argument1),
                    spriteFildToF32(argument2),
                    spriteFildToF32(argument3));
                if (hit)
                    DeleteSpriteThroughVirtualDeletingDestructor(hit);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_LOGIC_RUN):
        {
            (void)reinterpret_cast<core::Application*>(core::ApplicationPhysicalOwner())->callScriptFunctionRetail(
                argument1,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                0);

            break;
        }

        case static_cast<std::uint32_t>(AnimationCode::ANI_SALUT):
            if (m_currentAnimation != 8)
                ChangeAnimation(9);

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_RANDOM):
        {
            if ((std::rand() % 5) == 0)
            {
                ChangeAnimation(0);
            }
            else if ((std::rand() % 5) == 0)
            {
                ChangeAnimation(12);
            }
            else
            {
                if (m_currentAnimation == 4 && (std::rand() % 3) != 0)
                {
                    VID* const vid = m_vid;
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->defaultFrameSpeed());
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(spriteSub32Wrap(m_direction.Int(), 64), stepMs);
                    }
                else if (m_currentAnimation == 5 && (std::rand() % 3) != 0)
                {
                    VID* const vid = m_vid;
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->defaultFrameSpeed());
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(spriteAdd32Wrap(m_direction.Int(), 64), stepMs);
                    }
                else if ((std::rand() & 0x3) == 0)
                {
                    const int nextDirection = (std::rand() & 1) != 0
                        ? spriteSub32Wrap(m_direction.Int(), 64)
                        : spriteAdd32Wrap(m_direction.Int(), 64);
                    VID* const vid = m_vid;
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->defaultFrameSpeed());
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(nextDirection, stepMs);
                    }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_STOP):
            Stop();
            if (argument1 != 0)
                m_speed = 0.0f;

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_PAUSE):
        {
            if (!spriteFcompC3(m_speed, 0.0f))
                Stop();

            const int pauseRange = spriteAdd32Wrap(argument2, 1);
            const int pauseDelta = pauseRange != 0 ? std::rand() % pauseRange : 0;
            const int pauseTimer = spriteAdd32Wrap(argument1, pauseDelta);
            m_actionTimer = static_cast<DWORD>(pauseTimer);
            if (pauseTimer == 0)
                SetCommand(0, nullptr);
            else
                SetCommand(18, nullptr);
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ROTATE):
        {
            VID* const vid = m_vid;
            const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
            const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
            const std::uint32_t frameDefault =
                static_cast<std::uint32_t>(vid->defaultFrameSpeed());
            const std::uint32_t delta = now - previous;
            const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
            RotateTact(argument1, stepMs);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CLEAR_COMMAND):
        {
            SetCommand(0, nullptr);

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::CopyCommandPrefixToSprite):
        {
            SPRITE* const target = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            copyCommandPrefixTo(target);
            return 0;
        }

        case static_cast<std::uint32_t>(InternalActionCode::GetCommandStackCount):
            return static_cast<int>(m_commandStack.m_commandRecords.count);

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_DIRECTION):
        {
            ChangeDirection(static_cast<unsigned char>(argument1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_ANIMATION):
        {
            ChangeAnimation(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_VID):
        {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (argument1 < 0 || argument1 >= table.count())
            return 0;

        VID* nextVid = table.slot(argument1);
        if (!nextVid)
            return 0;

        VID* const oldVid = m_vid;
        if (oldVid->nVid == argument1)
            return 0;

        const int oldDirection = m_direction.Int();
        const int oldAnimation = m_currentAnimation;
        const DWORD oldClass = oldVid->spriteClassId();
        const DWORD nextClass = nextVid->spriteClassId();

        if (oldClass != nextClass)
        {
            LOG::ResourceError("SPRITE %i", 4, "ACT_CHANGE_VID", argument1, oldVid->nVid);
        }

        for (VID* link = oldVid->linkedVid(); link; link = link->linkedVid())
            deleteChildByVid(link);

        m_runtimeFlags &= ~ChildSpawnToggleFlag;
        RemoveSpriteFromGlobalHashForActionSwitch(this);
        removeFromDrawBucketsRecursive();

        oldVid->decrementSpriteCountForArmy(armyIndex());

        VID* swapVid = nullptr;
        if (argument1 < table.count())
            swapVid = table.slot(argument1);
        m_vid = swapVid ? swapVid : MAP::NullVid();
        m_vid->setLastSpriteCountChangeTimestamp(core::RealTimeMilliseconds());
        m_vid->incrementSpriteCountForArmy(armyIndex());

        const int requestedAnimation = argument2 >= 0 ? argument2 : oldAnimation;
        m_direction = ANGLE(0);
        m_currentAnimation = 0;
        m_currentFrame = 0;
        m_currentFrameBegin = 0;
        m_currentFrameEnd = m_vid->animationFrameCountFor(0) - 1;

        if (m_vid->actionAuxStateRequired() != 0)
        {
            if (!m_actionAuxState)
            {
                void* const storage = ::operator new(sizeof(ActionAuxState), std::nothrow);
                m_actionAuxState = static_cast<ActionAuxState*>(storage);
                if (m_actionAuxState)
                    initializeActionAuxState(this);
            }
            else
            {
                m_actionAuxState->lifetimeRemaining = static_cast<std::uint32_t>(m_vid->lifetimeValue());
            }
        }

        addToDrawBucketsRecursive();
        AddSpriteToGlobalHashForActionSwitch(this);
        ensureLinkedVidChild();
        ChangeAnimation(requestedAnimation);
        ChangeDirection(oldDirection);

        return 0;
    
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_COOR):
        {
            ChangeCoor(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       spriteFildToF32(argument3));

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::SetAnimationAndDirection):
        {
            ChangeAnimation(argument1);
            ChangeDirection(static_cast<unsigned char>(argument2));

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::ChangeCoordinateXY):
        {
            ChangeCoor(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       m_xyz.z);

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::ChangeCoordinateZ):
        {
            ChangeCoor(m_xyz.x,
                       m_xyz.y,
                       spriteFildToF32(argument1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_INVULNERABLE):
            m_runtimeFlags = (m_runtimeFlags & ~InvulnerableFlag) |
                (argument1 != 0 ? InvulnerableFlag : 0u);
            return 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_INVULNERABLE):
            return (m_runtimeFlags & InvulnerableFlag) != 0u ? 1 : 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_PARENT):
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(m_parentSprite) & 0xFFFFFFFFu);

        case static_cast<std::uint32_t>(ActionCode::ACT_DAMAGE):
        {
            VID* const vid = m_vid;
            const int amount = argument1;
            const int currentFrameTime = animationFrameTime();
            const int maxFrameTime = vid->animationFrameDuration(armyIndex());
            returnValue = 0;

            if (currentFrameTime >= maxFrameTime && amount < 0)
                return 1;
            if (m_currentAnimation >= 15)
                return 0;

            const int interceptScript = vid->damageInterceptScriptFunction();
            if (interceptScript >= 0 &&
                reinterpret_cast<core::Application*>(core::ApplicationPhysicalOwner())->callScriptFunctionRetail(
                    interceptScript,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                    argument2,
                    amount) != 0)
            {
                return 0;
            }

            if (vid->maxHp != 0)
                updateAnimationFrameTime(spriteSub32Wrap(currentFrameTime, amount));

            if (animationFrameTime() > maxFrameTime && amount < 0)
                setAnimationFrameTime(maxFrameTime);

            if (amount <= 0)
                return 0;

            if (vid->noAnimCadr[7] != 0 &&
                (m_currentAnimation == 0 || m_currentAnimation == 2))
            {
                ChangeAnimation(7);
                return 0;
            }

            const int damageAnimationScript = vid->damageScriptFunction();
            if (damageAnimationScript >= 0 &&
                reinterpret_cast<core::Application*>(core::ApplicationPhysicalOwner())->callScriptFunctionRetail(
                    damageAnimationScript,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                    argument2) != 0)
            {
                return 0;
            }

            VID* const postDamageCallbackVid = m_vid;
            const int damageSfx = postDamageCallbackVid->damageSfxId();
            if (damageSfx != 0)
                playSfxAtWorldPosition(damageSfx);

            if (postDamageCallbackVid->hasHitChildVid() != 0)
            {
                const int savedAnimation = m_currentAnimation;
                m_currentAnimation = 7;
                spawnAnimationChild();
                m_currentAnimation = savedAnimation;
            }
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_BEHAVE):
        {

            returnValue = 0;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_REPAIR):
        {

            VID* const vid = m_vid;
            VID* const deleteVid = vid->woundChildVid();
            (void)deleteChildByVid(deleteVid);

            const int bucket = armyIndex();
            const int repairFrameTime = vid->animationFrameDuration(bucket);
            setAnimationFrameTime(repairFrameTime);

            bool childRepairDispatched = false;
            if (SPRITE* const child = childChain())
            {
                VID* const linkVid = vid->linkedVid();
                if (child->Vid() == linkVid)
                {
                    (void)child->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
                    childRepairDispatched = true;
                }
            }

            if (!childRepairDispatched)
                ensureLinkedVidChild();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_HP):
        {

            returnValue = animationFrameTime();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_HP):
        {

            int targetFrameTime = argument1;
            if (targetFrameTime == 0)
            {
                const int bucket = armyIndex();
                targetFrameTime = spriteImul32Low(
                    m_vid->animationFrameDuration(bucket), argument2) / 100;
            }
            updateAnimationFrameTime(targetFrameTime);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_PERCENT_HP):
        {
            const int raw255 = healthRatio255();
            const int percent = raw255 * 100 / 255;

            returnValue = percent;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_GOAL):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(goalSprite()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_AMMO):
        case static_cast<std::uint32_t>(ActionCode::ACT_ADD_AMMO):
        {
            SPRITE* const uplink = childBacklink();
            if (!uplink)
            {

                returnValue = 0;

                break;
            }

            const int partResult = uplink->Action(static_cast<int>(opcode), argument1, argument2, argument3);

            returnValue = partResult;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_BATTLE_RANGE):
            return spriteFtolLow32(static_cast<long double>(weaponBattleRangeRetail()));

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ANIMATION):
        {
            SPRITE* const goal = new (std::nothrow) SPRITE(
                mapOwner(),
                MAP::NullVid(),
                VECTOR(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       spriteFildToF32(argument3)),
                ANGLE(0),
                nullptr);
            setGoalSprite(goal);
            return 0;
        }

        case static_cast<std::uint32_t>(InternalActionCode::GetAnimation):
            return m_currentAnimation;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ARMY):
        {

            returnValue = armyIndex();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_ARMY):
        {
            changeArmyBucket(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_INVISIBLE):
        {
            m_runtimeFlags = argument1 != 0 ? (m_runtimeFlags | DrawSuppressedFlag) : (m_runtimeFlags & ~DrawSuppressedFlag);
            if (SPRITE* child = childChain())
            {
                if (argument1 != 0)
                    child->suppressDrawRecursive();
                else
                    child->restoreDrawRecursive();
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_LINK):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(childChain()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_UPLINK):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(childBacklink()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_TIMER):
        {

            returnValue = static_cast<int>(m_actionTimer);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_TIMER):
        {
            m_actionTimer = static_cast<DWORD>(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ZSPEED):
        {

            returnValue = spriteFtolLow32(
                static_cast<long double>(m_zSpeed) * 1000.0L);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_ZSPEED):
        {
            m_zSpeed = spriteFildMulF32(argument1, 0.001f);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_SPEED):
        {

            returnValue = spriteFtolLow32(
                static_cast<long double>(m_speed) * 1000.0L);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_SPEED):
        {
            m_speed = spriteFildMulF32(argument1, 0.001f);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_COMMAND):
        {

            returnValue = commandIndex();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_DEATH_TIMER):
        {
            const std::uint32_t requestedLifetime = static_cast<std::uint32_t>(argument1);

            if (!m_actionAuxState)
            {
                void* const storage = ::operator new(sizeof(ActionAuxState), std::nothrow);
                m_actionAuxState = static_cast<ActionAuxState*>(storage);
                if (m_actionAuxState)
                    initializeActionAuxState(this);
            }

            m_actionAuxState->lifetimeRemaining = requestedLifetime;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_BACKUP_COMMAND):
        {
            SPRITE* const goal = goalSprite();
            const std::uint32_t backupOpcode = (static_cast<std::uint32_t>(commandIndex()) << 8) + 74u;
            const int goalArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(goal) & 0xFFFFFFFFu);
            const SpriteCommandRecord command = buildCommandRecord(backupOpcode, goalArg, 0, 0);
            m_commandStack.appendCommandRecord(command);

            if (goal)
                goal->setListReferenceCount(
                    spriteAdd32Wrap(goal->listReferenceCount(), 1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CYCLE_STACK):
        {
            const std::uint32_t nextCount = static_cast<std::uint32_t>(argument1) + 1u;
            m_commandStack.setCommandRecordCount(nextCount);
            if (static_cast<std::int32_t>(nextCount) >
                static_cast<std::int32_t>(m_commandStack.m_commandRecords.capacity))
            {
                m_commandStack.ensureCommandRecordCapacityRetail(nextCount);
            }
            m_currentFrameBegin = m_currentFrameEnd;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CLEAR_STACK):
            m_commandStack.clear();

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK):
        {
            const SpriteCommandRecord command = buildCommandRecord(opcode, argument1, argument2, argument3);
            m_commandStack.appendCommandRecord(command);

            (void)dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SAVE):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            m_commandStack.saveCommandRecordsToStream(stream);

            ActionAuxState::ItemList temporary{
                currentCommandWordListVtable(), 0u, 0u, nullptr};

            const auto destroyTemporary = [](ActionAuxState::ItemList& list) noexcept
            {
                if (list.values)
                    ::operator delete(list.values);
                list.values = nullptr;
                list.count = 0u;
            };

            const auto copyAssignRetail = [](ActionAuxState::ItemList& destination,
                                             const ActionAuxState::ItemList& source) noexcept
            {
                if (&destination == &source)
                    return;

                if (destination.values)
                    ::operator delete(destination.values);
                destination.values = nullptr;
                destination.count = 0u;
                destination.capacity = 0u;

                destination.count = source.count;
                destination.capacity = source.capacity;
                const std::size_t allocationBytes = source.capacity > 0x3FFFFFFFu
                    ? static_cast<std::size_t>(-1)
                    : static_cast<std::size_t>(source.capacity) * sizeof(std::int32_t);
                destination.values = static_cast<std::int32_t*>(
                    ::operator new(allocationBytes, std::nothrow));
                if (!destination.values)
                    fatalLogError(g_fileLogger,
                                  "!!!ERROR!!!::LIST: Not enough memory for = %i",
                                  static_cast<int>(destination.capacity));

                for (std::uint32_t i = 0; i < destination.count; ++i)
                    destination.values[i] = source.values[i];
            };

            if (m_actionAuxState)
                copyAssignRetail(temporary, m_actionAuxState->items);

            stream->write(&temporary.count, 4u);
            stream->write(temporary.values, temporary.count << 2);
            destroyTemporary(temporary);
            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_RESTORE):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            m_commandStack.restoreCommandRecordsFromStream(stream, this);

            if (argument2 >= 12)
            {
                ActionAuxState::ItemList temporary{
                    currentCommandWordListVtable(), 0u, 0u, nullptr};

                stream->read(&temporary.count, 4u);
                if (static_cast<std::int32_t>(temporary.count) >
                    static_cast<std::int32_t>(temporary.capacity))
                {
                    const std::size_t allocationBytes = temporary.count > 0x3FFFFFFFu
                        ? static_cast<std::size_t>(-1)
                        : static_cast<std::size_t>(temporary.count) * sizeof(std::int32_t);
                    temporary.values = static_cast<std::int32_t*>(
                        ::operator new(allocationBytes, std::nothrow));
                    if (!temporary.values)
                        fatalLogError(g_fileLogger,
                                      "!!!ERROR!!!::LIST: Not enough memory %i",
                                      static_cast<int>(temporary.count));
                    temporary.capacity = temporary.count;
                }
                stream->read(temporary.values, temporary.count << 2);

                if (temporary.count != 0u && ensureActionAuxStateForLocalAction())
                {
                    ActionAuxState::ItemList& destination = m_actionAuxState->items;
                    if (&destination != &temporary)
                    {
                        if (destination.values)
                            ::operator delete(destination.values);
                        destination.values = nullptr;
                        destination.count = 0u;
                        destination.capacity = 0u;

                        destination.count = temporary.count;
                        destination.capacity = temporary.capacity;
                        const std::size_t allocationBytes = temporary.capacity > 0x3FFFFFFFu
                            ? static_cast<std::size_t>(-1)
                            : static_cast<std::size_t>(temporary.capacity) * sizeof(std::int32_t);
                        destination.values = static_cast<std::int32_t*>(
                            ::operator new(allocationBytes, std::nothrow));
                        if (!destination.values)
                            fatalLogError(g_fileLogger,
                                          "!!!ERROR!!!::LIST: Not enough memory for = %i",
                                          static_cast<int>(destination.capacity));
                        for (std::uint32_t i = 0; i < destination.count; ++i)
                            destination.values[i] = temporary.values[i];
                    }
                }

                if (temporary.values)
                    ::operator delete(temporary.values);
                temporary.values = nullptr;
                temporary.count = 0u;
            }
            break;
        }

        case static_cast<std::uint32_t>(SpriteActConst::ACT_RESTORE_OLD_MAP):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int armyBucket = 0;
            const bool hasArmyBucket = m_commandStack.restoreOldMapCommandRecordsFromStream(stream, argument2, this, &armyBucket);
            if (hasArmyBucket)
                changeArmyBucket(armyBucket);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_UNDO_REMOVE):
        {
            m_runtimeFlags |= SpatialHashRemovedFlag;
            RemoveSpriteFromGlobalHashForActionSwitch(this);
            removeFromDrawBucketsRecursive();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_UNDO_INSERT):
        {
            m_runtimeFlags &= ~SpatialHashRemovedFlag;
            AddSpriteToGlobalHashForActionSwitch(this);
            addToDrawBucketsRecursive();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
                break;

            const float runtimeMaxSpeed = m_actionAuxState
                ? spriteFloatFromBits(m_actionAuxState->maxSpeedBits)
                : m_vid->maxSpeedValue();
            const bool windFacingAllowed =
                m_childBacklink != nullptr || runtimeMaxSpeed == 0.0f;
            if (windFacingAllowed && (m_vid->properties() & P_WIND) != 0u)
            {
                GRAPH* const graph = GRAPH::CurrentGraph();
                const float windSpeed = graph->windSpeed();
                if (windSpeed != 0.0f)
                {
                    const std::uint32_t deltaMs =
                        core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameMs = static_cast<std::uint32_t>(
                        m_vid->frameSpeedForAnimation(m_currentAnimation));
                    const std::uint32_t stepMs = deltaMs > frameMs ? deltaMs : frameMs;
                    RotateTact(ANGLE(static_cast<unsigned char>(graph->windDirection())), stepMs);
                }
            }

            // Active fight animation is allowed to finish before the generic
            // owner performs backlink/stand-go recovery.
            if (m_currentAnimation == 8 && m_currentFrame <= m_currentFrameEnd)
                break;

            if (SPRITE* const backlink = m_childBacklink)
            {
                if (backlink->Vid()->spriteClassId() == 7u)
                {
                    if (m_actionTimer != 0u)
                    {
                        if (m_currentAnimation >= 6 && m_currentAnimation != 10)
                            ChangeAnimation(10);
                    }
                    else if (m_currentAnimation == 10)
                    {
                        ChangeAnimation(0);
                    }
                }
                else
                {
                    bool rotate = false;
                    int targetDirection = 0;
                    if (m_goalSprite)
                    {
                        targetDirection = RetailDirectionFromFloatXY(
                            m_goalSprite->m_xyz.x - m_xyz.x,
                            m_goalSprite->m_xyz.y - m_xyz.y).Int();
                        rotate = true;
                    }
                    else if (m_actionTimer == 0u)
                    {
                        targetDirection = backlink->directionIndex();
                        rotate = true;
                    }

                    if (rotate)
                    {
                        const std::uint32_t deltaMs =
                            core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                        const std::uint32_t frameMs = static_cast<std::uint32_t>(
                            m_vid->frameSpeedForAnimation(m_currentAnimation));
                        const std::uint32_t stepMs = deltaMs > frameMs ? deltaMs : frameMs;
                        RotateTact(targetDirection, stepMs);
                    }
                }
            }

            if (m_currentAnimation != 10)
            {
                // UCOMISS/TEST AH,44: ordered exact zero is the stationary
                // branch; unordered follows the moving branch.
                if (m_speed != 0.0f)
                {
                    if (m_currentAnimation != 2)
                        ChangeAnimation(2);
                }
                else if (m_currentAnimation == 2 || m_currentAnimation >= 6)
                {
                    ChangeAnimation(0);
                }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_PLAY_SFX):
        {
            const int requestSfx = argument1;
            playSfxAtWorldPosition(requestSfx);

            break;
        }

        default:
        {
            const int nvid = m_vid ? m_vid->nvid() : -1;
            LOG::ResourceError(
                "SPRITE %i", 10, "Action() have not this act",
                static_cast<int>(opcode), nvid);
            break;
        }
        }

        return returnValue;
    }

    void SPRITE::MoveTact()
    {
        performBaseMovementTact();
    }

    void SPRITE::DeletePointerToSprite(SPRITE* sprite)
    {
        if (!sprite)
            return;

        if (m_childChain)
            m_childChain->DeletePointerToSprite(sprite);

        if (m_goalSprite == sprite)
        {
            if (m_currentAnimation == 8)
            {
                SPRITE* const replacement = new (std::nothrow) SPRITE(
                    mapOwner(), MAP::NullVid(), sprite->xyz(), ANGLE(0), nullptr);
                SetCommand(4, replacement);
            }
            else
                SetCommand(0, nullptr);
        }

        m_commandStack.clearTargetReferences(sprite);
    }

}
