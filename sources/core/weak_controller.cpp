#include "weak_controller.h"
#include "../graph.h"
#include "../graphics/angle.h"
#include "../sprite.h"
#include "resource.h"
#include "application.h"
#include "log.h"
#include "file_logger.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <array>
#include <new>

#if defined(_MSC_VER) && defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace as1
{
    namespace core
    {
        namespace
        {
            constexpr int kWeakControllerMaxLinks = 6;


            int g_pathResultScore = 0;
            int g_pathSecondaryBestCost = 0;
            int g_pathDepthLimit = 0;
            WeakController* g_pathOriginNode = nullptr;
            int g_pathEndpointBIsVid85 = 0;
            int g_pathParity = 0;
            SPRITE* g_pathTargetSprite = nullptr;
            int g_pathEdgeStackDepth = 0;
            std::array<int, 95> g_pathEdgeStack{};
            int g_pathEndpointAIsVid85 = 0;
            WeakController* g_pathBestNode = nullptr;
            int g_pathBranchScore = 0;
            int g_pathTurnPenalty = 0;
            unsigned char* g_pathOutputBuffer = nullptr;
            SPRITE* g_pathRouteOwner = nullptr;
            int g_pathActionBucket = 0;
            int g_pathMinimumDistance = 0;
            int g_pathDepth = 0;
            int g_pathCost = 0;
            int g_pathDurationCost = 0;
            std::array<unsigned char, 2500> g_pathEdgeScratch{};

            std::array<int, 100 * 100 * 30> g_pathSpatialGrid{};

            int approxDistanceXY(int ax, int ay, int bx, int by) noexcept
            {
                auto wrappedAbs = [](std::int32_t lhs, std::int32_t rhs) noexcept -> std::int32_t {
                    const std::int32_t value = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
                    const std::uint32_t u = static_cast<std::uint32_t>(value);
                    const std::uint32_t mask = value < 0 ? 0xFFFFFFFFu : 0u;
                    return static_cast<std::int32_t>((u ^ mask) - mask);
                };
                auto div2TowardZero = [](std::int32_t value) noexcept -> std::int32_t {
                    const std::uint32_t adjusted = static_cast<std::uint32_t>(value) + (value < 0 ? 1u : 0u);
                    return static_cast<std::int32_t>(static_cast<std::int32_t>(adjusted) >> 1);
                };
                const std::int32_t dx = wrappedAbs(ax, bx);
                const std::int32_t dy = wrappedAbs(ay, by);
                const std::int32_t major = dx <= dy ? dy : dx;
                const std::int32_t minor = dx <= dy ? dx : dy;
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(major) +
                                                 static_cast<std::uint32_t>(div2TowardZero(minor)));
            }

            float approxDistanceSpriteToDot(const SPRITE* sprite, const WeakController* dot) noexcept
            {
                const float dx = std::fabs(sprite->X() - static_cast<float>(dot->x()));
                const float dy = std::fabs(sprite->Y() - static_cast<float>(dot->y()));
                // floating-point comparison dx,dy / ordered comparison takes the half-dx branch for <= *and*
                // unordered.  !(dx > dy) preserves the unordered branch choice.
                const float result = !(dx > dy)
                    ? (dx * 0.5f + dy)
                    : (dx + dy * 0.5f);
                return result;
            }

            int weakTruncateFloatToInt32(float value) noexcept
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                return _mm_cvtt_ss2si(_mm_set_ss(value));
#else
                // truncating float-to-int conversion returns the integer-indefinite value for NaN/out-of-range.
                if (!std::isfinite(value) ||
                    value < -2147483648.0f ||
                    value >= 2147483648.0f)
                    return static_cast<int>(0x80000000u);
                return static_cast<int>(value);
#endif
            }

            std::int32_t sub32Wrap(std::int32_t a, std::int32_t b) noexcept
            {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
            }

            std::int32_t add32Wrap(std::int32_t a, std::int32_t b) noexcept
            {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
            }

            std::int32_t abs32Wrapped(std::int32_t value) noexcept
            {
                const std::uint32_t u = static_cast<std::uint32_t>(value);
                const std::uint32_t mask = value < 0 ? 0xFFFFFFFFu : 0u;
                return static_cast<std::int32_t>((u ^ mask) - mask);
            }

            int weakConvertFloatToInt32(float value) noexcept
            {
                const long double d = static_cast<long double>(value);
                if (!std::isfinite(d) ||
                    d < static_cast<long double>(INT64_MIN) ||
                    d > static_cast<long double>(INT64_MAX))
                    return 0;
                const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
                return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
            }

            int weakConvertFloatToInt32(double value) noexcept
            {
                if (!std::isfinite(value) ||
                    value < static_cast<double>(INT64_MIN) ||
                    value > static_cast<double>(INT64_MAX))
                    return 0;
                const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
                return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
            }

            std::int32_t imul32Low(std::int32_t a, std::int32_t b) noexcept
            {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) * static_cast<std::uint32_t>(b)));
            }

            constexpr std::uint32_t kWeakControllerSinTableBits[256] =
            {
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
            };

            constexpr std::uint32_t kWeakControllerCosTableBits[256] =
            {
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
            };

            float weakControllerFloatFromBits(std::uint32_t bits) noexcept
            {
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            std::int32_t signedDiv2TowardZero(std::int32_t value) noexcept
            {
                const std::int32_t bias = value < 0 ? 1 : 0;
                return static_cast<std::int32_t>((value + bias) >> 1);
            }

            std::int32_t signedDiv4TowardZero(std::int32_t value) noexcept
            {

                const std::int32_t bias = value < 0 ? 3 : 0;
                return static_cast<std::int32_t>((value + bias) >> 2);
            }
        }

        WeakController::WeakController() noexcept
        {

            m_pathEventFlag = 0;
            m_routeClassTag = 0;
            m_pushLineValue = 0;
            m_selectedLinkIndex = -1;
            m_refCount = 0;
            m_ownerSprite = nullptr;
            m_linkCount = 0;
        }

        WeakController::Link* WeakController::linkAt(int index) noexcept
        {
            return index >= 0 && index < m_linkCount
                ? &m_links[static_cast<std::size_t>(index)]
                : nullptr;
        }

        const WeakController::Link* WeakController::linkAt(int index) const noexcept
        {
            return index >= 0 && index < m_linkCount
                ? &m_links[static_cast<std::size_t>(index)]
                : nullptr;
        }

        void WeakController::setCoordinatesAndId(int x, int y, int id) noexcept
        {
            m_x = x;
            m_y = y;
            m_id = id;
        }


        int AS1_WEAK_STDCALL addNodeToSpatialGrid(int x, int y, int width, int height, int dotIndex) noexcept
        {

            int result = x;
            if (x < width && y < height && x >= 0 && y >= 0)
            {
                const std::int32_t xTimes5 = add32Wrap(x, static_cast<std::int32_t>(static_cast<std::uint32_t>(x) << 2));
                const std::int32_t xTimes25 = add32Wrap(xTimes5, static_cast<std::int32_t>(static_cast<std::uint32_t>(xTimes5) << 2));
                const std::int32_t cell = add32Wrap(y, static_cast<std::int32_t>(static_cast<std::uint32_t>(xTimes25) << 2));
                const std::uint32_t baseIndex = static_cast<std::uint32_t>(cell) * 30u;
                const std::size_t base = static_cast<std::size_t>(baseIndex);

                const std::int32_t newCount = add32Wrap(g_pathSpatialGrid[base], 1);
                g_pathSpatialGrid[base] = newCount;
                if (newCount < 30)
                {
                    const std::uint32_t storageIndex =
                        static_cast<std::uint32_t>(newCount) + static_cast<std::uint32_t>(cell) * 30u;
                    result = static_cast<std::int32_t>(storageIndex);
                    g_pathSpatialGrid[static_cast<std::size_t>(storageIndex)] = dotIndex;
                }
                else
                {
                    LOG::ResourceError("R_MAP", 10, "AddDotToArray() a[x][y][0]>=ARR_SIZE", 0);
                    result = sub32Wrap(g_pathSpatialGrid[base], 1);
                    g_pathSpatialGrid[base] = result;
                }
            }
            return result;
        }

        void AS1_WEAK_STDCALL markCrossingLinks(WeakController* a1, WeakController* a2, WeakController* a3, WeakController* a4) noexcept
        {

            if (a1 == a3 || a1 == a4 || a2 == a3 || a2 == a4)
                return;
            if (a1->m_pathDepthByEdge[0] || a2->m_pathDepthByEdge[0] ||
                a3->m_pathDepthByEdge[0] || a4->m_pathDepthByEdge[0])
                return;

            const std::int32_t x1 = a1->m_x;
            const std::int32_t y1 = a1->m_y;
            const std::int32_t x2 = a2->m_x;
            const std::int32_t y2 = a2->m_y;
            const std::int32_t x3 = a3->m_x;
            const std::int32_t y3 = a3->m_y;
            const std::int32_t x4 = a4->m_x;
            const std::int32_t y4 = a4->m_y;

            const std::int32_t dx12 = sub32Wrap(x2, x1);
            const std::int32_t dy12 = sub32Wrap(y2, y1);
            const std::int32_t aLine1 = sub32Wrap(imul32Low(y1, dx12), imul32Low(x1, dy12));
            const std::int32_t dy34Reverse = sub32Wrap(y3, y4);
            const std::int32_t dx34 = sub32Wrap(x4, x3);
            const std::int32_t dy34 = sub32Wrap(y4, y3);
            const std::int32_t aLine2 = sub32Wrap(imul32Low(y3, dx34), imul32Low(x3, dy34));
            const std::int32_t denominator = sub32Wrap(
                imul32Low(sub32Wrap(y1, y2), dx34),
                imul32Low(dx12, dy34Reverse));
            if (denominator == 0)
                return;

            const std::int32_t xNumerator = sub32Wrap(imul32Low(aLine1, dx34), imul32Low(dx12, aLine2));

            int ix = 0;
            int iy = 0;
            const long double intersectionX = static_cast<long double>(xNumerator) / static_cast<long double>(denominator);
            long double intersectionY = 0.0L;
            if (dx12 != 0)
            {
                intersectionY = (static_cast<long double>(aLine1) -
                                 static_cast<long double>(dy12) * intersectionX) /
                                static_cast<long double>(dx12);
            }
            else if (dx34 != 0)
            {
                intersectionY = (static_cast<long double>(aLine2) -
                                 static_cast<long double>(dy34Reverse) * intersectionX) /
                                static_cast<long double>(dx34);
            }
            ix = static_cast<int>(intersectionX);
            iy = static_cast<int>(intersectionY);

            if (x1 >= x2)
            {
                if (ix < x2 || ix > x1) return;
            }
            else if (ix < x1 || ix > x2) return;

            if (y1 >= y2)
            {
                if (iy < y2 || iy > y1) return;
            }
            else if (iy < y1 || iy > y2) return;

            if (x3 >= x4)
            {
                if (ix < x4 || ix > x3) return;
            }
            else if (ix < x3 || ix > x4) return;

            if (y3 >= y4)
            {
                if (iy < y4 || iy > y3) return;
            }
            else if (iy < y3 || iy > y4) return;

            const int i12 = findLinkIndex(a1, a2);
            const int i21 = findLinkIndex(a2, a1);
            const int i34 = findLinkIndex(a3, a4);
            const int i43 = findLinkIndex(a4, a3);
            if (i12 >= 0 && i21 >= 0 && i34 >= 0 && i43 >= 0)
            {
                WeakController::Link* const cross34 = &a3->m_links[static_cast<std::size_t>(i34)];
                const std::uint32_t cross34Raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cross34) & 0xFFFFFFFFu);
                a1->m_links[static_cast<std::size_t>(i12)].crossingLinkToken = cross34Raw;
                a2->m_links[static_cast<std::size_t>(i21)].crossingLinkToken = cross34Raw;

                WeakController::Link* const cross12 = &a1->m_links[static_cast<std::size_t>(i12)];
                const std::uint32_t cross12Raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cross12) & 0xFFFFFFFFu);
                a3->m_links[static_cast<std::size_t>(i34)].crossingLinkToken = cross12Raw;
                a4->m_links[static_cast<std::size_t>(i43)].crossingLinkToken = cross12Raw;
            }
        }

        int buildCrossingLinkGrid(WeakControllerMap* self) noexcept
        {

            int xCount = 2 * (self->m_maxX / 150) + 10;
            int yCount = 2 * (self->m_maxY / 150) + 10;
            if (xCount >= 100) xCount = 100;
            if (yCount >= 100) yCount = 100;

            if (xCount > 0)
            {
                for (int x = 0; x < xCount; ++x)
                {
                    if (yCount > 0)
                    {
                        for (int y = 0; y < yCount; ++y)
                            g_pathSpatialGrid[(static_cast<std::size_t>(x) * 100u + static_cast<std::size_t>(y)) * 30u] = 0;
                    }
                }
            }

            int result = self->dotCount();
            for (int i = 0; i < result; ++i)
            {
                WeakController* const dot = self->m_dots[static_cast<std::size_t>(i)];
                dot->m_pathDepthByEdge[0] = 0;
                const int cellX = dot->m_x / 75;
                const int cellY = dot->m_y / 75;
                addNodeToSpatialGrid(cellX,     cellY,     xCount, yCount, i);
                addNodeToSpatialGrid(cellX - 1, cellY,     xCount, yCount, i);
                addNodeToSpatialGrid(cellX,     cellY - 1, xCount, yCount, i);
                addNodeToSpatialGrid(cellX - 1, cellY - 1, xCount, yCount, i);
                result = self->dotCount();
            }

            if (xCount > 0)
            {
                result = 0;
                for (int x = 0; x < xCount; ++x)
                {
                    if (yCount > 0)
                    {
                        for (int y = 0; y < yCount; ++y)
                        {
                            const std::size_t base = (static_cast<std::size_t>(x) * 100u + static_cast<std::size_t>(y)) * 30u;
                            const int count = g_pathSpatialGrid[base];
                            if (count >= 1)
                            {
                                for (int firstSlot = 1; firstSlot < count; ++firstSlot)
                                {
                                    const int firstIndex = g_pathSpatialGrid[base + static_cast<std::size_t>(firstSlot)];
                                    for (int secondSlot = firstSlot + 1; secondSlot <= count; ++secondSlot)
                                    {
                                        const int secondIndex = g_pathSpatialGrid[base + static_cast<std::size_t>(secondSlot)];
                                        WeakController* const first = self->m_dots[static_cast<std::size_t>(firstIndex)];
                                        WeakController* const second = self->m_dots[static_cast<std::size_t>(secondIndex)];
                                        for (int firstEdge = 0; firstEdge < first->m_linkCount; ++firstEdge)
                                        {
                                            for (int secondEdge = 0; secondEdge < second->m_linkCount; ++secondEdge)
                                            {
                                                markCrossingLinks(first,
                                                           first->m_links[static_cast<std::size_t>(firstEdge)].target,
                                                           second,
                                                           second->m_links[static_cast<std::size_t>(secondEdge)].target);
                                            }
                                        }
                                    }
                                }
                            }
                            result += 30;
                        }
                    }
                    result = (x + 1) * 3000;
                }
            }
            return result;
        }


        int searchPushLineRecursive(WeakController* self) noexcept
        {

            int result = -2;
            if (self->m_pathDepthByEdge[0] <= g_pathBranchScore)
                return result;
            self->m_pathDepthByEdge[0] = g_pathBranchScore;

            WeakController* const origin = g_pathOriginNode;
            if (self != origin)
            {
                SPRITE* const testSprite = g_pathTargetSprite;
                if (testSprite && self->m_ownerSprite && self->m_ownerSprite->isInEngineChain(testSprite))
                {
                    g_pathBestNode = self;
                    g_pathResultScore = g_pathBranchScore;
                    return -1;
                }

                if (self->m_ownerSprite && g_pathResultScore >= 0xFFFF)
                {
                    bool replace = false;
                    if (origin)
                    {
                        if (!g_pathBestNode)
                            replace = true;
                        else
                            replace = approxDistanceXY(origin->m_x, origin->m_y,
                                                       g_pathBestNode->m_x, g_pathBestNode->m_y) >
                                      approxDistanceXY(origin->m_x, origin->m_y,
                                                       self->m_x, self->m_y);
                    }
                    else if (testSprite)
                    {
                        if (!g_pathBestNode)
                            replace = true;
                        else
                            replace = approxDistanceSpriteToDot(testSprite, g_pathBestNode) >
                                      approxDistanceSpriteToDot(testSprite, self);
                    }
                    if (replace)
                    {
                        g_pathBestNode = self;
                        result = -1;
                    }
                }

                if (g_pathBranchScore < g_pathResultScore)
                {
                    ++g_pathBranchScore;
                    for (int edgeIndex = 0; edgeIndex < self->m_linkCount; ++edgeIndex)
                    {
                        if (searchPushLineRecursive(self->m_links[static_cast<std::size_t>(edgeIndex)].target) >= -1)
                            result = edgeIndex;
                    }
                    --g_pathBranchScore;
                }
                return result;
            }

            g_pathBestNode = self;
            g_pathResultScore = g_pathBranchScore;
            return -1;
        }

        int setPushLine(WeakControllerMap* self, int x1, int y1, int x2, int y2, int value) noexcept
        {

            WeakController* start = findNearestLinkedNode2D(self, x1, y1);
            WeakController* end = findNearestLinkedNode2D(self, x2, y2);
            if (!start)
                return static_cast<int>(writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't found dot in %i,%i", x1, y1));
            if (!end)
                return static_cast<int>(writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't found dot in %i,%i", x2, y2));
            if (start->m_linkCount <= 0)
                return static_cast<int>(writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't SetPushLine in %i,%i", x1, y1));

            if (end == start)
            {
                std::int32_t bestDistance = 999999;
                for (int i = 0; i < start->m_linkCount; ++i)
                {
                    WeakController* const target = start->m_links[static_cast<std::size_t>(i)].target;
                    const std::int32_t dx = sub32Wrap(target->x(), x2);
                    std::int32_t dy = sub32Wrap(target->y(), target->id());
                    dy = sub32Wrap(dy, y2);
                    const std::int32_t distance = add32Wrap(imul32Low(dx, dx), imul32Low(dy, dy));
                    if (distance < bestDistance)
                    {
                        end = target;
                        bestDistance = distance;
                    }
                }
            }

            initializePathSearch(&globalWeakControllerMap(), start, nullptr, 0, nullptr);
            int result = searchPushLineRecursive(end);
            if (result < 0)
                return static_cast<int>(writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't PushLineFindDot in %i,%i", x1, y1));

            WeakController* current = start;
            for (;;)
            {
                if (current == end)
                    return result;

                const int count = current->m_linkCount;
                result = 0;
                if (count > 0)
                {
                    const int currentPath = current->m_pathDepthByEdge[0];
                    int edgeIndex = 0;
                    for (; edgeIndex < count; ++edgeIndex)
                    {
                        WeakController* const target = current->m_links[static_cast<std::size_t>(edgeIndex)].target;
                        if (currentPath == add32Wrap(target->m_pathDepthByEdge[0], 1))
                            break;
                    }
                    if (edgeIndex < count)
                    {
                        result = edgeIndex;
                        current->m_selectedLinkIndex = edgeIndex;
                        current->m_pushLineValue = static_cast<std::uint32_t>(value);
                        current = current->m_links[static_cast<std::size_t>(edgeIndex)].target;
                    }
                }

                if (!current)
                    return result;
            }
        }

        WeakController* setNearestLinkValue(WeakControllerMap* self, int x, int y, int value, int unused) noexcept
        {

            (void)unused;
            WeakController* const dot = findNearestLinkedNode2D(self, x, y);
            if (!dot)
                return nullptr;

            int bestIndex = -1;
            double bestDistance = 10000.0;
            for (int i = 0; i < dot->m_linkCount; ++i)
            {
                WeakController* const target = dot->m_links[static_cast<std::size_t>(i)].target;
                const std::int32_t dxInt = sub32Wrap(x, target->x());
                const std::int32_t dyInt = sub32Wrap(y, target->y());
                const double dx2 = squareDistanceComponent(static_cast<double>(dxInt));
                const double dy2 = squareDistanceComponent(static_cast<double>(dyInt));
                const double distance = std::sqrt(dx2 + dy2);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestIndex = i;
                }
            }
            if (bestIndex < 0)
            {
                const std::intptr_t loggerEax = logFileLoggerResourceError(
                    g_fileLogger, "R_MAP", 10,
                    "нету связей у точки - такого быть не может", 0);
                return reinterpret_cast<WeakController*>(static_cast<std::uintptr_t>(loggerEax));
            }

            WeakController* const target = dot->m_links[static_cast<std::size_t>(bestIndex)].target;
            dot->m_routeClassTag = static_cast<std::uint32_t>(value);
            if (target)
                target->m_routeClassTag = static_cast<std::uint32_t>(value);
            return target;
        }

        int findLinkIndex(WeakController* self, WeakController* target) noexcept
        {

            const int count = self->m_linkCount;
            for (int index = 0; index < count; ++index)
            {
                if (self->m_links[static_cast<std::size_t>(index)].target == target)
                    return index;
            }
            return -1;
        }

        void removeLinkTo(WeakController* self, WeakController* target) noexcept
        {

            const int count = self->m_linkCount;
            if (count <= 0)
                return;

            int found = -1;
            for (int index = 0; index < count; ++index)
            {
                if (self->m_links[static_cast<std::size_t>(index)].target == target)
                {
                    found = index;
                    break;
                }
            }
            if (found < 0)
                return;

            if (self->m_selectedLinkIndex == found)
                self->m_selectedLinkIndex = -1;

            const int newCount = count - 1;
            self->m_linkCount = newCount;
            const std::size_t foundPos = static_cast<std::size_t>(found);
            const std::size_t lastPos = static_cast<std::size_t>(newCount);
            self->m_links[foundPos] = self->m_links[lastPos];
            WeakController::Link& moved = self->m_links[foundPos];
            moved.target->m_links[static_cast<std::size_t>(moved.reciprocalIndex)].reciprocalIndex = found;

            if (self->m_selectedLinkIndex == self->m_linkCount)
                self->m_selectedLinkIndex = found;
        }

        void connectBidirectional(WeakController* self, WeakController* target) noexcept
        {
            // Create the reciprocal 0x14-byte
            // link pair, logging each owner independently when its six-link
            // fixed array is full.
            if (!target || findLinkIndex(self, target) >= 0)
                return;

            const std::int32_t dirX = sub32Wrap(target->x(), self->m_x);
            const std::int32_t dirY = sub32Wrap(target->y(), self->m_y);
            const std::uint32_t angleByte = static_cast<std::uint32_t>(AngleFromXY(dirX, dirY, nullptr).Int());

            const std::int32_t metricDy = sub32Wrap(self->m_y, target->y());
            const std::int32_t metricDySquare = imul32Low(metricDy, metricDy);
            const std::int32_t nineDySquare = add32Wrap(metricDySquare,
                static_cast<std::int32_t>(static_cast<std::uint32_t>(metricDySquare) * 8u));
            const std::int32_t quarterNineDySquare = signedDiv4TowardZero(nineDySquare);
            const std::int32_t metricDx = sub32Wrap(self->m_x, target->x());
            const std::int32_t metricDxSquare = imul32Low(metricDx, metricDx);
            const int distance = IntegerSquareRoot(add32Wrap(quarterNineDySquare, metricDxSquare));

            WeakController::Link link{};
            link.target = target;
            link.length = static_cast<std::uint32_t>(distance);
            link.reciprocalIndex = target->m_linkCount;
            link.crossingLinkToken = 0;
            link.facing = angleByte;

            if (self->m_linkCount >= kWeakControllerMaxLinks)
            {
                LOG::Write("!!!ERROR!!!R_DOT: Too many links in %i,%i,%i",
                    self->m_x, self->m_y, self->m_id);
            }
            else
            {
                self->m_links[static_cast<std::size_t>(self->m_linkCount)] = link;
                ++self->m_linkCount;
            }

            link.target = self;
            link.reciprocalIndex = self->m_linkCount - 1;
            link.facing = (angleByte - 0x80u) & 0xFFu;

            if (target->m_linkCount >= kWeakControllerMaxLinks)
            {
                LOG::Write("!!!ERROR!!!R_DOT: Too many links2 in %i,%i,%i",
                    target->x(), target->y(), target->id());
            }
            else
            {
                target->m_links[static_cast<std::size_t>(target->m_linkCount)] = link;
                ++target->m_linkCount;
            }
        }

        int projectDistanceAlongLink(WeakController* self, int x, int y, int z, int edgeIndex) noexcept
        {
            (void)z;

            const WeakController::Link& edge =
                self->m_links[static_cast<std::size_t>(edgeIndex)];
            const unsigned int tableIndex =
                (static_cast<unsigned char>(edge.facing) - 64u) & 255u;

            const std::int32_t dy = sub32Wrap(static_cast<std::int32_t>(y), self->m_y);
            const std::int32_t threeDy = add32Wrap(dy, add32Wrap(dy, dy));
            const std::int32_t dxValue = sub32Wrap(static_cast<std::int32_t>(x), self->m_x);

            // The game logic: integer-to-float conversion -> single-precision multiplication -> single-precision multiplication 0.5
            // + single-precision multiplication -> single-precision addition -> truncating float-to-int conversion.  Keep the projection binary32.
            float projectedY = static_cast<float>(threeDy);
            projectedY *= SPRITE::rawDirectionSin(static_cast<int>(tableIndex));
            projectedY *= 0.5f;
            float projectedX = static_cast<float>(dxValue);
            projectedX *= SPRITE::rawDirectionCos(static_cast<int>(tableIndex));
            return weakTruncateFloatToInt32(projectedY + projectedX);
        }

        int distanceToLink(WeakController* self, int x, int y, int z, int edgeIndex) noexcept
        {

            const WeakController::Link& edge =
                self->m_links[static_cast<std::size_t>(edgeIndex)];
            WeakController* const target = edge.target;
            const int projected = projectDistanceAlongLink(self, x, y, z, edgeIndex);

            if (projected < 0 || projected >= static_cast<int>(edge.length))
            {
                const std::int32_t zSum = add32Wrap(self->m_id, target->id());
                const std::int32_t zMid = signedDiv2TowardZero(zSum);
                const std::int32_t dz = add32Wrap(sub32Wrap(zMid, z), 4);

                const std::int32_t ySum = add32Wrap(self->m_y, target->y());
                const std::int32_t yMid = signedDiv2TowardZero(ySum);
                const std::int32_t dy = sub32Wrap(yMid, y);

                const std::int32_t xSum = add32Wrap(self->m_x, target->x());
                const std::int32_t xMid = signedDiv2TowardZero(xSum);
                const std::int32_t dx = sub32Wrap(xMid, x);

                const std::int32_t metric = add32Wrap(
                    add32Wrap(imul32Low(dz, dz), imul32Low(dy, dy)),
                    imul32Low(dx, dx));
                return IntegerSquareRoot(metric);
            }

            const std::int32_t baseX = self->m_x;
            const std::int32_t baseY = self->m_y;
            const std::int32_t baseZ = self->m_id;
            const std::int32_t pointX = sub32Wrap(x, baseX);
            const std::int32_t pointY = sub32Wrap(y, baseY);
            const std::int32_t pointZ = sub32Wrap(z, baseZ);
            const std::int32_t edgeX = sub32Wrap(target->x(), baseX);
            const std::int32_t edgeY = sub32Wrap(target->y(), baseY);
            const std::int32_t edgeZ = sub32Wrap(target->id(), baseZ);

            const std::int32_t crossZ = sub32Wrap(imul32Low(edgeY, pointX), imul32Low(edgeX, pointY));
            const std::int32_t crossX = sub32Wrap(imul32Low(pointY, edgeZ), imul32Low(edgeY, pointZ));
            const std::int32_t crossY = sub32Wrap(imul32Low(edgeX, pointZ), imul32Low(pointX, edgeZ));

            const std::int32_t lineMetric = add32Wrap(
                add32Wrap(imul32Low(edgeZ, edgeZ), imul32Low(edgeY, edgeY)),
                imul32Low(edgeX, edgeX));
            const int lineLength = IntegerSquareRoot(lineMetric);

            const std::int32_t crossMetric = add32Wrap(
                add32Wrap(imul32Low(crossY, crossY), imul32Low(crossX, crossX)),
                imul32Low(crossZ, crossZ));
            const int crossLength = IntegerSquareRoot(crossMetric);
            return crossLength / lineLength;
        }

        int findNearestPathPosition(WeakController* self, int x, int y, int z, PathPosition* out) noexcept
        {

            int result = self->m_linkCount;
            if (!result)
                return result;

            int bestMetric = 65535;
            for (int outer = 0; outer < self->m_linkCount; ++outer)
            {
                WeakController* const node =
                    self->m_links[static_cast<std::size_t>(outer)].target;
                for (int edgeIndex = 0; edgeIndex < node->linkCount(); ++edgeIndex)
                {
                    const int metric = distanceToLink(node, x, y, z, edgeIndex);
                    if (metric < bestMetric)
                    {
                        bestMetric = metric;
                        out->node = node;
                        out->edgeIndex = edgeIndex;
                    }
                }
            }

            const int projected = projectDistanceAlongLink(out->node, x, y, z, out->edgeIndex);
            out->progress = projected;
            if (projected < 0)
                out->progress = 0;

            result = static_cast<int>(reinterpret_cast<std::intptr_t>(out->node));
            int duration = 0;
            if (out->node)
                duration = static_cast<int>(
                    out->node->m_links[static_cast<std::size_t>(out->edgeIndex)].length);

            if (out->progress >= duration)
            {
                if (out->node)
                {
                    result = duration - 1;
                    out->progress = result;
                }
                else
                {
                    result = -1;
                    out->progress = -1;
                }
            }
            return result;
        }

        int findClosestFacingLink(WeakController* self, unsigned char facing) noexcept
        {

            int bestIndex = 0;
            if (self->m_linkCount <= 1)
                return bestIndex;

            unsigned char currentFacing =
                static_cast<unsigned char>(self->m_links[0].facing);
            unsigned char bestDeltaA = static_cast<unsigned char>(facing - currentFacing);
            unsigned char bestDeltaB = static_cast<unsigned char>(currentFacing - facing);
            unsigned char bestDelta = bestDeltaA < bestDeltaB ? bestDeltaA : bestDeltaB;

            for (int index = 1; index < self->m_linkCount; ++index)
            {
                currentFacing =
                    static_cast<unsigned char>(self->m_links[static_cast<std::size_t>(index)].facing);
                const unsigned char deltaA = static_cast<unsigned char>(facing - currentFacing);
                const unsigned char deltaB = static_cast<unsigned char>(currentFacing - facing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta < bestDelta)
                {
                    bestDelta = delta;
                    bestIndex = index;
                }
            }
            return bestIndex;
        }


        int publishPathCandidate(WeakController* self, unsigned int pathSize, int score, int minimumDistance, int* resultIndex) noexcept
        {
            g_pathBestNode = self;
            g_pathResultScore = score;
            *resultIndex = -1;
            std::uintptr_t result = reinterpret_cast<std::uintptr_t>(g_pathOutputBuffer);
            if (g_pathOutputBuffer && static_cast<int>(pathSize) < 2500)
            {
                result = reinterpret_cast<std::uintptr_t>(g_pathRouteOwner);
                if (g_pathRouteOwner)
                    g_pathRouteOwner->setPathBufferSize(static_cast<int>(pathSize));
                std::memcpy(g_pathOutputBuffer, g_pathEdgeScratch.data(), pathSize);
            }
            (void)minimumDistance;
            return static_cast<int>(result);
        }

        int validatePathLink(WeakController* self, int edgeIndex, SPRITE* routeOwner) noexcept
        {
            if (edgeIndex < 0 || edgeIndex >= self->m_linkCount)
                return 0;

            const WeakController::Link& edge = self->m_links[static_cast<std::size_t>(edgeIndex)];
            if (routeOwner)
            {
                const int routeBucket = static_cast<int>(((routeOwner->runtimeFlags() >> 12) & 3u) + 4u);
                if (static_cast<int>(self->m_routeClassTag) == routeBucket &&
                    static_cast<int>(edge.target->m_routeClassTag) == static_cast<int>(self->m_routeClassTag))
                    return 0;

                SPRITE* const edgeOwner = edge.target->m_ownerSprite;
                if (edgeOwner && !routeOwner->isInEngineChain(edgeOwner))
                {
                    if (g_pathActionBucket == 26 && edgeOwner->isInEngineChain(g_pathTargetSprite))
                        return 0;

                    WeakController* ownerC8Target = nullptr;
                    WeakController* const ownerC8 = edgeOwner->primaryPathNode();
                    const int ownerC8Index = edgeOwner->primaryPathEdgeIndex();
                    if (ownerC8)
                        ownerC8Target = ownerC8->m_links[static_cast<std::size_t>(ownerC8Index)].target;

                    if ((std::fabs(static_cast<double>(edgeOwner->Speed())) < 0.03 || ownerC8Target == self) &&
                        (!edgeOwner->goalSprite() || edgeOwner->goalSprite() != routeOwner->goalSprite()) &&
                        (edgeOwner->engineCommandArgument0Value() == 0 || edgeOwner->engineCommandArgument0Value() != routeOwner->engineCommandArgument0Value()))
                        return 0;
                }
            }

            return edge.target->m_selectedLinkIndex != edge.reciprocalIndex ? 1 : 0;
        }

        int searchFacingLimitedPath(WeakController* self, int depth, WeakController* excludedA, WeakController* excludedB,
                       SPRITE* routeOwner, unsigned char facing) noexcept
        {
            if (!depth)
                return 1;

            for (int edgeIndex = 0; edgeIndex < self->m_linkCount; ++edgeIndex)
            {
                const WeakController::Link& edge = self->m_links[static_cast<std::size_t>(edgeIndex)];
                if (edge.target == excludedA || edge.target == excludedB || !routeOwner)
                    continue;
                if (!validatePathLink(self, edgeIndex, routeOwner))
                    continue;
                if (!validatePathLink(edge.target, edge.reciprocalIndex, routeOwner))
                    continue;

                const unsigned char edgeFacing = static_cast<unsigned char>(edge.facing);
                const unsigned char deltaA = static_cast<unsigned char>(facing - edgeFacing);
                const unsigned char deltaB = static_cast<unsigned char>(edgeFacing - facing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta > 31u)
                    continue;

                if (g_pathEdgeStackDepth >= 95)
                {
                    LOG::ResourceError("R_DOT %i,%i", 10, "dots_num is large",
                                       g_pathEdgeStackDepth, self->m_x, self->m_y);
                }
                else
                {
                    g_pathEdgeStack[static_cast<std::size_t>(g_pathEdgeStackDepth++)] = edgeIndex;
                }

                if (searchFacingLimitedPath(edge.target, depth - 1, self, excludedB, routeOwner, edgeFacing))
                    return 1;
                --g_pathEdgeStackDepth;
            }
            return 0;
        }

        int searchPathRecursive(WeakController* self, int incomingEdgeIndex, int incomingFacing) noexcept
        {
            const int savedPathParity = g_pathParity;
            int resultIndex = -2;
            if (g_pathDepth >= g_pathDepthLimit)
            {
                g_pathParity = savedPathParity;
                return resultIndex;
            }

            if (incomingEdgeIndex >= 0)
            {
                self->m_pathDepthByEdge[static_cast<std::size_t>(incomingEdgeIndex)] = g_pathDepth;
                self->m_pathCostByEdge[static_cast<std::size_t>(incomingEdgeIndex)] = g_pathCost;
                self->m_pathDurationByEdge[static_cast<std::size_t>(incomingEdgeIndex)] = g_pathDurationCost;
            }

            if (self == g_pathOriginNode)
            {
                publishPathCandidate(self, static_cast<unsigned int>(g_pathDepth), g_pathCost, g_pathMinimumDistance, &resultIndex);
            }
            else
            {
                if (g_pathTargetSprite)
                {
                    SPRITE* const owner = self->m_ownerSprite;
                    if (owner && owner->isInEngineChain(g_pathTargetSprite))
                    {
                        if (g_pathActionBucket != 26)
                        {
                            publishPathCandidate(self, static_cast<unsigned int>(g_pathDepth), g_pathCost, g_pathMinimumDistance, &resultIndex);
                        }
                        else if (incomingEdgeIndex >= 0)
                        {
                            const WeakController::Link& incoming = self->m_links[static_cast<std::size_t>(incomingEdgeIndex)];
                            bool blocked = owner->engineChainPrevious() != nullptr;
                            WeakController* c8Target = nullptr;
                            if (owner->primaryPathNode())
                                c8Target = owner->primaryPathNode()->m_links[static_cast<std::size_t>(owner->primaryPathEdgeIndex())].target;
                            if (!blocked && c8Target != incoming.target && c8Target != self)
                                blocked = true;
                            if (blocked)
                            {
                                if (owner->engineChainNext())
                                {
                                    g_pathParity = savedPathParity;
                                    return -2;
                                }
                                WeakController* d8Target = nullptr;
                                if (owner->secondaryPathNode())
                                    d8Target = owner->secondaryPathNode()->m_links[static_cast<std::size_t>(owner->secondaryPathEdgeIndex())].target;
                                if (d8Target != incoming.target && d8Target != self)
                                {
                                    g_pathParity = savedPathParity;
                                    return -2;
                                }
                            }
                            publishPathCandidate(self, static_cast<unsigned int>(g_pathDepth), g_pathCost, g_pathMinimumDistance, &resultIndex);
                        }
                    }
                }

                if ((g_pathActionBucket == 28 || g_pathActionBucket == 29) && g_pathTargetSprite &&
                    (!self->m_ownerSprite || (g_pathRouteOwner && g_pathRouteOwner->isInEngineChain(self->m_ownerSprite))))
                {
                    const int currentDistance = weakTruncateFloatToInt32(
                        approxDistanceSpriteToDot(g_pathTargetSprite, self));
                    if (currentDistance <= g_pathMinimumDistance && g_pathResultScore > g_pathCost)
                    {
                        // The game logic still evaluates/spills this distance when a
                        // best node exists, but the value is not part of the publish gate.
                        if (g_pathBestNode)
                        {
                            volatile float retailUnusedBestDistance =
                                approxDistanceSpriteToDot(g_pathTargetSprite, g_pathBestNode);
                            (void)retailUnusedBestDistance;
                        }
                        publishPathCandidate(self, static_cast<unsigned int>(g_pathDepth),
                                             g_pathCost, g_pathMinimumDistance, &resultIndex);
                    }
                }
            }

            if (g_pathResultScore >= 65535)
            {
                if (g_pathOriginNode)
                {
                    bool keepBest = false;
                    if (g_pathBestNode)
                    {
                        const int oldDistance = approxDistanceXY(g_pathOriginNode->m_x, g_pathOriginNode->m_y,
                                                                 g_pathBestNode->m_x, g_pathBestNode->m_y);
                        const int newDistance = approxDistanceXY(g_pathOriginNode->m_x, g_pathOriginNode->m_y,
                                                                 self->m_x, self->m_y);
                        keepBest = oldDistance <= newDistance &&
                                   (g_pathBestNode != self || g_pathSecondaryBestCost <= g_pathCost);
                    }
                    if (!keepBest)
                    {
                        g_pathBestNode = self;
                        g_pathSecondaryBestCost = g_pathCost;
                        if (g_pathOutputBuffer && g_pathDepth < 2500)
                        {
                            if (g_pathRouteOwner)
                                g_pathRouteOwner->setPathBufferSize(g_pathDepth);
                            std::memcpy(g_pathOutputBuffer, g_pathEdgeScratch.data(), static_cast<std::size_t>(g_pathDepth));
                        }
                        resultIndex = -1;
                    }
                }

                if (g_pathTargetSprite)
                {
                    bool replaceBest = g_pathBestNode == nullptr;
                    if (g_pathBestNode)
                    {
                        const float oldDistance = approxDistanceSpriteToDot(g_pathTargetSprite, g_pathBestNode);
                        const float newDistance = approxDistanceSpriteToDot(g_pathTargetSprite, self);
                        replaceBest = oldDistance > newDistance ||
                                      (g_pathBestNode == self && g_pathSecondaryBestCost > g_pathCost);
                    }
                    if (replaceBest)
                    {
                        g_pathBestNode = self;
                        g_pathSecondaryBestCost = g_pathCost;
                        if (g_pathOutputBuffer && g_pathDepth < 2500)
                        {
                            if (g_pathRouteOwner)
                                g_pathRouteOwner->setPathBufferSize(g_pathDepth);
                            std::memcpy(g_pathOutputBuffer, g_pathEdgeScratch.data(), static_cast<std::size_t>(g_pathDepth));
                        }
                        resultIndex = -1;
                    }
                }
            }

            int incomingPass = 1;
            if (incomingEdgeIndex >= 0 && g_pathRouteOwner)
            {
                const WeakController::Link& incoming = self->m_links[static_cast<std::size_t>(incomingEdgeIndex)];
                incomingPass = validatePathLink(incoming.target, incoming.reciprocalIndex, g_pathRouteOwner);
            }

            if (g_pathCost < g_pathResultScore)
            {
                ++g_pathDepth;
                ++g_pathCost;
                for (int edgeIndex = 0; edgeIndex < self->m_linkCount; ++edgeIndex)
                {
                    if (!incomingPass && !(g_pathDepth == 1 && incomingEdgeIndex == edgeIndex))
                        continue;
                    if (incomingEdgeIndex == edgeIndex && g_pathDepth >= 3)
                        continue;

                    const WeakController::Link& edge = self->m_links[static_cast<std::size_t>(edgeIndex)];
                    if (incomingEdgeIndex >= 0 &&
                        edge.target->m_pathCostByEdge[static_cast<std::size_t>(edge.reciprocalIndex)] <= g_pathCost)
                        continue;

                    int addedBudget = 0;
                    g_pathParity = savedPathParity;
                    int facingPass = 1;
                    if (incomingEdgeIndex >= 0 && incomingFacing >= 0)
                    {
                        const unsigned char edgeFacing = static_cast<unsigned char>(edge.facing);
                        const unsigned char deltaA = static_cast<unsigned char>(incomingFacing - edgeFacing);
                        const unsigned char deltaB = static_cast<unsigned char>(edgeFacing - incomingFacing);
                        const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                        if (delta > 31u)
                        {
                            if ((g_pathDepth > 1 || !g_pathRouteOwner || g_pathRouteOwner->Speed() != 0.0f) &&
                                incomingEdgeIndex != edgeIndex)
                            {
                                if (g_pathTurnPenalty)
                                {
                                    g_pathEdgeStackDepth = 0;
                                    const WeakController::Link& incoming = self->m_links[static_cast<std::size_t>(incomingEdgeIndex)];
                                    facingPass = searchFacingLimitedPath(self, g_pathTurnPenalty, incoming.target, edge.target,
                                                          g_pathRouteOwner, static_cast<unsigned char>(incomingFacing));
                                }
                                addedBudget = 1;
                            }
                            g_pathParity ^= 1u;
                        }
                    }

                    g_pathDurationCost += static_cast<int>(edge.length);
                    if (addedBudget)
                        g_pathCost += g_pathTurnPenalty;

                    int constraintPass = 1;
                    if (edge.crossingLinkToken != 0u)
                    {
                        struct RawConstraintPath { WeakController* node; int pad04; int edgeIndex08; };
                        const RawConstraintPath* const constraint = reinterpret_cast<const RawConstraintPath*>(static_cast<std::uintptr_t>(edge.crossingLinkToken));
                        WeakController* const constraintNode = constraint->node;
                        WeakController* const constraintTarget =
                            constraintNode->m_links[static_cast<std::size_t>(constraint->edgeIndex08)].target;
                        SPRITE* const constraintOwner = constraintNode->m_ownerSprite;
                        SPRITE* const targetOwner = constraintTarget->m_ownerSprite;
                        if ((constraintOwner && std::fabs(static_cast<double>(constraintOwner->Speed())) < 0.03 &&
                             !constraintOwner->isInEngineChain(g_pathRouteOwner)) ||
                            (targetOwner && std::fabs(static_cast<double>(targetOwner->Speed())) < 0.03 &&
                             !targetOwner->isInEngineChain(g_pathRouteOwner)))
                            constraintPass = 0;
                    }

                    if (facingPass && constraintPass)
                    {
                        if (g_pathDepth - 1 < 2500)
                            g_pathEdgeScratch[static_cast<std::size_t>(g_pathDepth - 1)] = static_cast<unsigned char>(edgeIndex);
                        if (searchPathRecursive(edge.target, edge.reciprocalIndex, static_cast<unsigned char>(edge.facing)) >= -1)
                            resultIndex = edgeIndex;
                    }

                    g_pathDurationCost -= static_cast<int>(edge.length);
                    if (addedBudget)
                        g_pathCost -= g_pathTurnPenalty;
                }
                --g_pathDepth;
                --g_pathCost;
            }

            g_pathParity = savedPathParity;
            return resultIndex;
        }

        void initializePathSearch(WeakControllerMap* self, WeakController* firstNode, SPRITE* secondSprite,
                        int actionBucket, SPRITE* routeOwner) noexcept
        {
            g_pathResultScore = 65535;
            g_pathSecondaryBestCost = 65535;
            g_pathDepthLimit = 65535;
            int minimumDistance = 0;
            g_pathOriginNode = firstNode;
            g_pathBestNode = nullptr;
            g_pathRouteOwner = routeOwner;

            if (routeOwner)
            {
                routeOwner->setPathBufferSize(0);
                g_pathTurnPenalty = routeOwner->scaledEngineChainLength() + 1;
                g_pathOutputBuffer = routeOwner->pathBufferData();
                g_pathEndpointAIsVid85 = routeOwner->engineChainHead()->Vid()->nvid() == 85 ? 1 : 0;
                g_pathEndpointBIsVid85 = routeOwner->engineChainTail()->Vid()->nvid() == 85 ? 1 : 0;
                if (actionBucket == 28 || actionBucket == 29)
                {
                    minimumDistance = routeOwner->minimumEngineWeaponRange() - 150 - 20 * g_pathTurnPenalty;
                    if (minimumDistance <= 20)
                        minimumDistance = 20;
                }
            }
            else
            {
                g_pathTurnPenalty = 0;
                g_pathOutputBuffer = nullptr;
                g_pathEndpointAIsVid85 = 0;
                g_pathEndpointBIsVid85 = 0;
            }

            g_pathActionBucket = actionBucket;
            g_pathMinimumDistance = minimumDistance;
            g_pathTargetSprite = secondSprite;
            g_pathParity = 0;

            for (int mapIndex = self->dotCount() - 1; mapIndex >= 0; --mapIndex)
            {
                WeakController* const dot = self->m_dots[mapIndex];
                for (int edgeIndex = 0; edgeIndex < dot->m_linkCount; ++edgeIndex)
                {
                    dot->m_pathDepthByEdge[static_cast<std::size_t>(edgeIndex)] = 0x0FFFFFFF;
                    dot->m_pathCostByEdge[static_cast<std::size_t>(edgeIndex)] = 0x0FFFFFFF;
                    dot->m_pathDurationByEdge[static_cast<std::size_t>(edgeIndex)] = 0x0FFFFFFF;
                }
            }
        }

        int advancePathPosition(PathPosition* self, WeakController* firstNode, SPRITE* secondSprite, SPRITE* routeOwner) noexcept
        {
            WeakController* const oldNode = self->node;
            unsigned char oldFacing = 0;
            if (oldNode)
                oldFacing = static_cast<unsigned char>(oldNode->m_links[static_cast<std::size_t>(self->edgeIndex)].facing);

            int duration = 0;
            if (oldNode)
                duration = static_cast<int>(oldNode->m_links[static_cast<std::size_t>(self->edgeIndex)].length);
            if (self->progress > duration)
                self->progress -= duration;

            self->node = oldNode
                ? oldNode->m_links[static_cast<std::size_t>(self->edgeIndex)].target
                : nullptr;

            if (firstNode || secondSprite)
            {
                initializePathSearch(&globalWeakControllerMap(), firstNode, secondSprite,
                           static_cast<int>((routeOwner->runtimeFlags() >> 2) & 31u), routeOwner);

                // The game logic keeps this path-depth heuristic in binary32,
                // divides by 10.0f and converts with truncating float-to-int conversion.  Extended precision
                // or multiplying by a reciprocal changes boundary results.
                const float dx = firstNode
                    ? std::fabs(routeOwner->X() - static_cast<float>(firstNode->m_x))
                    : std::fabs(routeOwner->X() - secondSprite->X());
                const float dy = firstNode
                    ? std::fabs(routeOwner->Y() - static_cast<float>(firstNode->m_y))
                    : std::fabs(routeOwner->Y() - secondSprite->Y());
                const float approx = !(dx > dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
                g_pathDepthLimit = weakTruncateFloatToInt32(approx / 10.0f);

                const int oldToNew = findLinkIndex(oldNode, self->node);
                const unsigned char facing = static_cast<unsigned char>(oldNode->m_links[static_cast<std::size_t>(oldToNew)].facing);
                const int newToOld = findLinkIndex(self->node, oldNode);
                self->edgeIndex = searchPathRecursive(self->node, newToOld, facing);

                if (g_pathResultScore == 65535)
                {
                    initializePathSearch(&globalWeakControllerMap(), firstNode, secondSprite,
                               static_cast<int>((routeOwner->runtimeFlags() >> 2) & 31u), routeOwner);
                    const int retryOldToNew = findLinkIndex(oldNode, self->node);
                    const unsigned char retryFacing = static_cast<unsigned char>(oldNode->m_links[static_cast<std::size_t>(retryOldToNew)].facing);
                    const int retryNewToOld = findLinkIndex(self->node, oldNode);
                    self->edgeIndex = searchPathRecursive(self->node, retryNewToOld, retryFacing);
                }
            }
            else
            {
                self->edgeIndex = -1;
            }

            if (self->edgeIndex < 0)
            {
                self->edgeIndex = findClosestFacingLink(self->node, oldFacing);
            }
            else
            {
                WeakController* const currentNode = self->node;
                const unsigned char selectedFacing = currentNode
                    ? static_cast<unsigned char>(currentNode->m_links[static_cast<std::size_t>(self->edgeIndex)].facing)
                    : 0;
                const unsigned char deltaA = static_cast<unsigned char>(oldFacing - selectedFacing);
                const unsigned char deltaB = static_cast<unsigned char>(selectedFacing - oldFacing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta > 31u)
                {
                    WeakController* const excludedB = currentNode
                        ? currentNode->m_links[static_cast<std::size_t>(self->edgeIndex)].target
                        : nullptr;
                    self->edgeIndex = findClosestFacingLink(currentNode, oldFacing);
                    if (g_pathTurnPenalty)
                    {
                        g_pathEdgeStackDepth = 0;
                        if (searchFacingLimitedPath(currentNode, g_pathTurnPenalty, oldNode, excludedB, routeOwner, oldFacing))
                            self->edgeIndex = g_pathEdgeStack[0];
                        else
                            g_pathResultScore = 65535;
                    }
                    if (routeOwner && g_pathBestNode && routeOwner->pathBufferReachesSecondaryTarget(currentNode))
                        g_pathResultScore = -g_pathResultScore;
                }
            }

            int selectedDuration = 0;
            if (self->node)
                selectedDuration = static_cast<int>(self->node->m_links[static_cast<std::size_t>(self->edgeIndex)].length);
            if (self->progress > selectedDuration)
                self->progress = self->node ? selectedDuration : 0;
            return g_pathResultScore;
        }

        int scoreNextPathStep(PathPosition* self, WeakController* firstNode, SPRITE* secondSprite,
                       int actionBucket, SPRITE* routeOwner) noexcept
        {
            if (!routeOwner)
                return 65536;
            if (!firstNode && !secondSprite)
                return 65536;

            initializePathSearch(&globalWeakControllerMap(), firstNode, secondSprite, actionBucket, routeOwner);
            WeakController* const node = self->node;
            const unsigned char facing = node
                ? static_cast<unsigned char>(node->m_links[static_cast<std::size_t>(self->edgeIndex)].facing)
                : 0;
            WeakController* const nextNode = node
                ? node->m_links[static_cast<std::size_t>(self->edgeIndex)].target
                : nullptr;
            const int reciprocal = findLinkIndex(nextNode, node);
            const int selected = searchPathRecursive(nextNode, reciprocal, facing);
            if (selected >= 0)
            {
                const unsigned char selectedFacing = static_cast<unsigned char>(nextNode->m_links[static_cast<std::size_t>(selected)].facing);
                const unsigned char deltaA = static_cast<unsigned char>(facing - selectedFacing);
                const unsigned char deltaB = static_cast<unsigned char>(selectedFacing - facing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta > 31u && g_pathBestNode && routeOwner->pathBufferReachesSecondaryTarget(nextNode))
                    g_pathResultScore = -g_pathResultScore;
            }
            return g_pathResultScore;
        }

        int pathResultScore() noexcept
        {
            return g_pathResultScore;
        }

        int pathSecondaryBestCost() noexcept
        {
            return g_pathSecondaryBestCost;
        }

        WeakController* pathBestNode() noexcept
        {
            return g_pathBestNode;
        }

        namespace
        {
#pragma pack(push, 4)
            struct WeakControllerPointerListStorage
            {
                std::uint32_t vtableToken;
                std::int32_t count;
                std::int32_t capacity;
                void* data;
            };
#pragma pack(pop)

#if UINTPTR_MAX == 0xFFFFFFFFu
                                                                           
                                                                                                                         
#endif
        }

        void* destroyWeakControllerPointerListStorage(void* self, unsigned char flags) noexcept
        {

            auto* const owner = static_cast<WeakControllerPointerListStorage*>(self);
            if (owner->data)
                ::operator delete(owner->data);
            owner->data = nullptr;
            owner->count = 0;
            if ((flags & 1u) != 0u)
                ::operator delete(self);
            return self;
        }

        WeakControllerMap::~WeakControllerMap() noexcept
        {

#if UINTPTR_MAX == 0xFFFFFFFFu
            (void)destroyWeakControllerPointerListStorage(static_cast<void*>(&m_dotListOwnerVtableToken), 0u);
#else
            if (m_dots)
                ::operator delete(m_dots);
            m_dots = nullptr;
            m_dotCount = 0;
#endif
        }

        WeakControllerMap& globalWeakControllerMap() noexcept
        {
            static WeakControllerMap map;
            return map;
        }

        int serializePathPosition(PathPosition* self, RESOURCE* resource) noexcept
        {
            // Serialize the current
            // path node XYZ as three WORDs, then the selected edge target XYZ,
            // followed by [PathPosition+0x04] shifted left by 16.
            WeakController* const node = self->node;
            std::int16_t value = static_cast<std::int16_t>(node->x());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(node->y());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(node->id());
            resource->write(&value, 2);

            WeakController* const target = node->links()[static_cast<std::size_t>(self->edgeIndex)].target;
            value = static_cast<std::int16_t>(target->x());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(target->y());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(target->id());
            resource->write(&value, 2);

            std::uint32_t fixedProgress = static_cast<std::uint32_t>(self->progress) << 16;
            return resource->write(&fixedProgress, 4);
        }

        int PathPosition::deserializePathPosition(RESOURCE* resource) noexcept
        {
            PathPosition* const self = this;
            // Deserialize the saved path state. The function
            // has exactly one stack argument (RESOURCE*); the legacy signature
            // prototype that exposed extra register arguments is incorrect.
            std::int16_t x = 0;
            std::int16_t y = 0;
            std::int16_t id = 0;
            resource->read(&x, 2);
            resource->read(&y, 2);
            resource->read(&id, 2);
            self->node = findNodeNearCoordinates(&globalWeakControllerMap(), x, y, id);

            std::int16_t targetX = 0;
            std::int16_t targetY = 0;
            std::int16_t targetId = 0;
            resource->read(&targetX, 2);
            resource->read(&targetY, 2);
            resource->read(&targetId, 2);
            if (self->node)
            {
                WeakController* const target = findNodeNearCoordinates(&globalWeakControllerMap(), targetX, targetY, targetId);
                self->edgeIndex = findLinkIndex(self->node, target);
            }
            if (self->edgeIndex < 0)
                LOG::Write("!!!ERROR!!!RAIL: Read error");

            resource->read(&self->progress, 4);
            self->progress >>= 16;
            return self->progress;
        }

        WeakController* findNodeNearCoordinates(WeakControllerMap* self, int x, int y, int id) noexcept
        {

            const int count = self->dotCount();
            for (int index = 0; index < count; ++index)
            {
                WeakController* dot = self->m_dots[static_cast<std::size_t>(index)];
                const std::int32_t dx = sub32Wrap(dot->x(), x);
                if (abs32Wrapped(dx) > 1)
                    continue;

                const std::int32_t dy = sub32Wrap(dot->y(), y);
                if (abs32Wrapped(dy) > 1)
                    continue;

                if (dot->m_id == id)
                    return dot;
            }
            return nullptr;
        }

        WeakController* createOrRetainNode(WeakControllerMap* self, float x, float y, float id)
        {

            const int ix = weakConvertFloatToInt32(x);
            const int iy = weakConvertFloatToInt32(y);
            const int iid = weakConvertFloatToInt32(id);

            if (WeakController* existing = findNodeNearCoordinates(self, ix, iy, iid))
            {
                existing->m_refCount += 1;
                return existing;
            }

            void* const rawDot = ::operator new(sizeof(WeakController), std::nothrow);
            if (!rawDot)
                fatalLogError(g_fileLogger, "!!!R_MAP::CreateDot- Not enough memory");
            WeakController* const dot = new (rawDot) WeakController();
            dot->setCoordinatesAndId(ix, iy, iid);

            bool alreadyInList = false;
            for (int index = self->dotCount() - 1; index >= 0; --index)
            {
                if (self->m_dots[index] == dot)
                {
                    alreadyInList = true;
                    break;
                }
            }
            if (!alreadyInList)
            {
                if (self->dotCount() >= self->m_dotCapacity)
                {
                    const int oldCapacity = self->m_dotCapacity;
                    const int nextCapacity = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(oldCapacity) * 2u + 4u);
                    if (nextCapacity > oldCapacity)
                    {
                        WeakController** const oldList = self->m_dots;
                        const std::uint32_t allocationBytes = static_cast<std::uint32_t>(nextCapacity) * 4u;
                        WeakController** const newList = static_cast<WeakController**>(
                            ::operator new(static_cast<std::size_t>(allocationBytes), std::nothrow));
                        if (!newList)
                            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", nextCapacity);
                        self->m_dots = newList;
                        for (int index = 0; index < oldCapacity; ++index)
                            newList[index] = oldList[index];
                        ::operator delete(oldList);
                        self->m_dotCapacity = nextCapacity;
                    }
                }
                self->m_dots[self->m_dotCount++] = dot;
            }

            if (dot->x() > self->m_maxX)
                self->m_maxX = dot->x();
            else if (dot->x() < self->m_minX)
                self->m_minX = dot->x();

            if (dot->y() > self->m_maxY)
                self->m_maxY = dot->y();
            else if (dot->y() < self->m_minY)
                self->m_minY = dot->y();

            dot->m_refCount += 1;
            return dot;
        }


        WeakController* findNearestLinkedNode2D(WeakControllerMap* self, int x, int y) noexcept
        {

            WeakController* result = nullptr;
            std::int32_t best = 0x0FFFFFFF;
            const int count = self->dotCount();
            for (int index = 0; index < count; ++index)
            {
                WeakController* const dot = self->m_dots[static_cast<std::size_t>(index)];
                if (dot->m_linkCount <= 0)
                    continue;

                const std::int32_t dx = sub32Wrap(dot->x(), x);
                const std::int32_t dy = sub32Wrap(sub32Wrap(dot->y(), dot->m_id), y);
                const std::int32_t metric = add32Wrap(imul32Low(dx, dx), imul32Low(dy, dy));
                if (metric < best)
                {
                    best = metric;
                    result = dot;
                }
            }
            return result;
        }

        WeakController* findNearestLinkedNode3D(WeakControllerMap* self, int x, int y, int z) noexcept
        {

            WeakController* result = nullptr;
            std::int32_t best = 0x0FFFFFFF;
            const int count = self->dotCount();
            for (int index = 0; index < count; ++index)
            {
                WeakController* const dot = self->m_dots[static_cast<std::size_t>(index)];
                if (dot->m_linkCount <= 0)
                    continue;

                const std::int32_t dx = sub32Wrap(dot->x(), x);
                const std::int32_t dy = sub32Wrap(dot->y(), y);
                const std::int32_t dz = sub32Wrap(z, dot->m_id);
                const std::int32_t metric = add32Wrap(add32Wrap(imul32Low(dx, dx), imul32Low(dy, dy)), imul32Low(dz, dz));
                if (metric < best)
                {
                    best = metric;
                    result = dot;
                }
            }
            return result;
        }


        void AS1_WEAK_FASTCALL releaseWeakController(WeakController* self) noexcept
        {

            if (self->m_refCount == 0)
                return;

            --self->m_refCount;
            if (self->m_refCount != 0)
                return;

            self->m_ownerSprite = nullptr;
            const int count = self->m_linkCount;
            for (int index = 0; index < count; ++index)
            {
                WeakController* const other = self->m_links[static_cast<std::size_t>(index)].target;
                removeLinkTo(other, self);
            }
            self->m_linkCount = 0;

            WeakControllerMap& mapOwner = globalWeakControllerMap();
            int registryIndex = mapOwner.m_dotCount;
            if (registryIndex != 0)
            {
                while (registryIndex != 0)
                {
                    --registryIndex;
                    if (mapOwner.m_dots[registryIndex] == self)
                    {
                        if (registryIndex >= 0 && registryIndex < mapOwner.m_dotCount)
                            mapOwner.m_dots[registryIndex] =
                                mapOwner.m_dots[--mapOwner.m_dotCount];
                        break;
                    }
                }
            }

            releaseWeakControllerThunk(self);
            ::operator delete(self);
        }

        void AS1_WEAK_FASTCALL releaseWeakControllerThunk(WeakController* self) noexcept
        {
            // Non-MSVC/non-32-bit builds use the portable fallback.
            releaseWeakController(self);
        }

        double squareDistanceComponent(double value) noexcept
        {
            // Squared distance component used by path scoring.
            return value * value;
        }

        int drawWeakControllerMapDebug(WeakControllerMap* self) noexcept
        {
            // The owner is the global WeakController map, not a generic MAP summary.
            // Draw every graph node and edge from the controller-map state.
            GRAPH* const graph = GRAPH::CurrentGraph();
            const auto& drawState = as1::core::GlobalApplicationDrawDispatcherState();

            auto pack16 = [](DWORD color) noexcept -> std::uint16_t {
                return static_cast<std::uint16_t>(
                    (static_cast<unsigned char>(color) >> 3u) |
                    (g_color16RedMask & (color >> (16u - g_color16RedShift))) |
                    (g_color16GreenMask & (color >> (8u - g_color16GreenShift))));
            };
            auto expand16 = [](std::uint16_t color) noexcept -> DWORD {
                return static_cast<DWORD>(
                    8u * (color & 0x1Fu) |
                    ((static_cast<DWORD>(color) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                    ((static_cast<DWORD>(color) << (16u - g_color16RedShift)) & 0x00FF0000u));
            };

            const std::uint16_t ownerColor16 = pack16(0xFFFF0000u);
            int result = self->dotCount();
            for (int nodeIndex = 0; nodeIndex < result; ++nodeIndex)
            {
                WeakController* const node = self->dots()[static_cast<std::size_t>(nodeIndex)];
                const float x = static_cast<float>(node->x()) - drawState.cameraShiftX();
                const float y = static_cast<float>(node->y() - node->id()) - drawState.cameraShiftY();
                if (x > -50.0f && y > -50.0f && x < 1000.0f && y < 1000.0f)
                {
                    graph->DrawLine(x - 1.0f, y, x - 1.0f, y + static_cast<float>(node->id()), 0xFF0000FFu);
                    graph->DrawLine(x - 2.0f, y + static_cast<float>(node->id()), x, y + static_cast<float>(node->id()), 0xFF0000FFu);

                    const std::uint16_t nodeColor16 = node->ownerSprite() ? ownerColor16 : pack16(0xFFFFFFFFu);
                    for (int edgeIndex = 0; edgeIndex < node->linkCount(); ++edgeIndex)
                    {
                        const WeakController::Link& edge = node->links()[static_cast<std::size_t>(edgeIndex)];
                        WeakController* const target = edge.target;
                        std::uint16_t edgeColor16 = pack16(0xFFFFFFFFu);
                        if (node->routeClassTag() > 3u && target->routeClassTag() > 3u)
                            edgeColor16 = pack16(0xFF000000u);
                        if (node->pathEventFlag() != 0u && target->pathEventFlag() != 0u)
                            edgeColor16 = ownerColor16;
                        if (node->selectedLinkIndex() == edgeIndex || target->selectedLinkIndex() == edge.reciprocalIndex)
                            edgeColor16 = pack16(0xFFFF8080u);

                        const float tx = static_cast<float>(target->x()) - drawState.cameraShiftX();
                        const float ty = static_cast<float>(target->y() - target->id()) - drawState.cameraShiftY();
                        graph->DrawLine(x, y, tx, ty, expand16(edgeColor16));

#if UINTPTR_MAX == 0xFFFFFFFFu
                        if (edge.crossingLinkToken != 0u)
                        {
                            const WeakController* const cross = reinterpret_cast<const WeakController*>(static_cast<std::uintptr_t>(edge.crossingLinkToken));
                            graph->DrawLine(x, y,
                                            static_cast<float>(weakControllerScreenX(cross)),
                                            static_cast<float>(weakControllerScreenY(cross)),
                                            0xFF00FF00u);
                        }
#endif
                    }

                    const DWORD nodeColor = expand16(nodeColor16);
                    graph->DrawLine(x - 2.0f, y - 2.0f, x + 2.0f, y + 2.0f, nodeColor);
                    graph->DrawLine(x - 2.0f, y + 2.0f, x + 2.0f, y - 2.0f, nodeColor);
                }
                result = self->dotCount();
            }
            return result;
        }

        double weakControllerScreenX(const WeakController* self) noexcept
        {
            return static_cast<double>(self->x()) -
                   static_cast<double>(as1::core::GlobalApplicationDrawDispatcherState().cameraShiftX());
        }

        double weakControllerScreenY(const WeakController* self) noexcept
        {
            const std::int32_t yMinusId = sub32Wrap(self->y(), self->id());
            return static_cast<double>(yMinusId) -
                   static_cast<double>(as1::core::GlobalApplicationDrawDispatcherState().cameraShiftY());
        }

    }
}
