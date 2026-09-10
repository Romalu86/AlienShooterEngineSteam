#include "vid.h"
#include "vid_software.h"
#include "vid_software16.h"
#include "../core/resource.h"
#include "../core/log.h"
#include "../core/file_logger.h"
#include "../map.h"
#include "../constant.h"
#include "../sprite.h"
#include "../mouse.h"
#include "../graph.h"
#include "../core/application.h"
#include "../sound/engine.h"
#include "../script/vid_data_codes.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <array>
#if !defined(_MSC_VER) || !defined(_M_IX86)
#include <sstream>
#include <unordered_map>
#endif
#include <new>
#include <cmath>
#include <limits>
#include <cstdint>

namespace as1
{

    int g_vidAllocatedBytes = 0;

    namespace
    {
#if !defined(_MSC_VER) || !defined(_M_IX86)
        struct VidHostSidecar
        {
            std::vector<VID::PaletteEntry> palette;
            std::vector<VID::DataFrame> dataFrames;
            std::vector<VID::SurfacePage> surfacePages;
            std::vector<VID::SurfaceRecord> surfaceRecords;
            std::vector<VID::LightFrame> lightFrames;
            std::vector<VID::FrameSurface> frameSurfaces;
#if !defined(_MSC_VER) || !defined(_M_IX86)
            std::vector<std::string> decodeWarnings;
            std::vector<DWORD> gammaPaletteBuffer;
            bool compressedSurfPresent = false;
            size_t compressedSurfBytes = 0;
#endif
        };

        std::unordered_map<const VID*, VidHostSidecar>& vidHostSidecars()
        {

            static auto* sidecars = new std::unordered_map<const VID*, VidHostSidecar>();
            return *sidecars;
        }

        VidHostSidecar& vidHostSidecar(VID* vid)
        {
            return vidHostSidecars()[vid];
        }

        const VidHostSidecar& vidHostSidecar(const VID* vid)
        {
            const auto& sidecars = vidHostSidecars();
            const auto it = sidecars.find(vid);
            if (it != sidecars.end())
                return it->second;
            static const VidHostSidecar empty;
            return empty;
        }

        void eraseVidHostSidecar(const VID* vid)
        {
            vidHostSidecars().erase(vid);
        }

#endif

        constexpr float UNLIMITED = 999999.0f;

#if !defined(_MSC_VER) || !defined(_M_IX86)
        std::uint64_t fnv1aAppend(std::uint64_t h, const void* data, size_t size)
        {
            const BYTE* bytes = static_cast<const BYTE*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                h ^= static_cast<std::uint64_t>(bytes[i]);
                h *= 1099511628211ull;
            }
            return h;
        }

        template <class T>
        std::uint64_t fnv1aAppendValue(std::uint64_t h, const T& value)
        {
            return fnv1aAppend(h, &value, sizeof(value));
        }

#endif

        template <class T>
        void readExact(RESOURCE* res, T& value, const char* field)
        {
            if (res->read(&value, sizeof(value)) != 0)
                throw std::runtime_error(std::string("VID::LoadParameters: failed to read ") + field);
        }

        void readIntArray(RESOURCE* res, int* dst, int count, const char* field)
        {
            for (int i = 0; i < count; ++i)
                readExact(res, dst[i], field);
        }

        std::int32_t multiplyWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs));
        }

        std::int32_t addWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
        }

        std::int32_t subtractWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int signedScaleShift(int numerator, int shift) noexcept
        {
            const int mask = (1 << shift) - 1;
            if (numerator < 0)
                numerator += mask;
            return numerator >> shift;
        }

        int signedDivide32(std::int32_t numerator, std::int32_t denominator) noexcept
        {
            if (denominator == 0)
                std::abort();
            return numerator / denominator;
        }

        int vidConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        bool x87LessOrUnorderedForVid(float lhs, float rhs) noexcept
        {
            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        std::int32_t pointerLow32(const void* p) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)));
        }

        int scaleFrameTimeBySixteenth(int frameTime, int nextDuration, int oldDuration) noexcept
        {
            const std::uint32_t product = static_cast<std::uint32_t>(multiplyWrap32(frameTime, nextDuration));
            const std::int32_t shifted = static_cast<std::int32_t>(product << 4u);
            return signedScaleShift(signedDivide32(shifted, oldDuration), 4);
        }

        int scaleFrameTimeByByteFraction(int frameTime, int nextDuration, int oldDuration) noexcept
        {
            const std::uint32_t product = static_cast<std::uint32_t>(multiplyWrap32(frameTime, nextDuration));
            const std::int32_t shifted = static_cast<std::int32_t>(product << 8u);
            return signedScaleShift(signedDivide32(shifted, oldDuration), 8);
        }

        int percentOfMaxHpDuration(int maxHp, int percentValue) noexcept
        {
            return multiplyWrap32(maxHp, percentValue) / 100;
        }

        int divideBy1000SignedMagic(std::int32_t value) noexcept
        {

            const std::int64_t product = static_cast<std::int64_t>(0x10624DD3) * static_cast<std::int64_t>(value);
            std::int32_t high = static_cast<std::int32_t>(product >> 32);
            std::int32_t result = high >> 6;
            result += static_cast<std::uint32_t>(result) >> 31;
            return result;
        }


#if !defined(_MSC_VER) || !defined(_M_IX86)
        std::vector<BYTE> loadCurrentSubresource(RESOURCE* res)
        {
            void* ptr = nullptr;
            const int size = res->SubLoad(&ptr, nullptr);
            if (size <= 0 || !ptr)
                return {};

            std::unique_ptr<BYTE, void (*)(BYTE*)> holder(
                static_cast<BYTE*>(ptr),
                [](BYTE* loaded) { ::operator delete(loaded); });
            return std::vector<BYTE>(holder.get(), holder.get() + size);
        }

        std::vector<std::vector<BYTE>> loadSubresources(RESOURCE* res, RESOURCE::ResTypes::Type type)
        {
            std::vector<std::vector<BYTE>> out;
            if (!res || !res->isOpen())
                return out;
            if (res->GoBegin(type))
                return out;
            do
            {
                out.push_back(loadCurrentSubresource(res));
            }
            while (!res->GoNextSub(type));
            return out;
        }

        WORD rd16(const std::vector<BYTE>& bytes, size_t off)
        {
            if (off + 2 > bytes.size())
                throw std::runtime_error("truncated WORD");
            return static_cast<WORD>(bytes[off] | (bytes[off + 1] << 8));
        }

        DWORD rd32(const std::vector<BYTE>& bytes, size_t off)
        {
            if (off + 4 > bytes.size())
                throw std::runtime_error("truncated DWORD");
            return static_cast<DWORD>(bytes[off] | (bytes[off + 1] << 8) | (bytes[off + 2] << 16) | (bytes[off + 3] << 24));
        }

        int rdi32(const std::vector<BYTE>& bytes, size_t off)
        {
            return static_cast<int>(rd32(bytes, off));
        }

        bool isSurfType(WORD type)
        {
            return type == 0x0023 || type == 0x0031 || type == 0x0033 || type == 0x0131 || type == 0x0133;
        }

        bool isCompressedSurfType(WORD type)
        {
            return type == 0x0131 || type == 0x0133;
        }


        static uint16_t ceilHalfUnsigned16(uint16_t v)
        {
            uint32_t x = v;
            x |= 2u;
            x >>= 1u;
            return static_cast<uint16_t>(x & 0xFFFFu);
        }

        class As1AdaptiveModel
        {
        public:
            As1AdaptiveModel() { reset(); }

            void reset()
            {
                symbolCount_ = 257u;
                quota_ = 0;
                extraQuota_ = 0;
                bucketSize_ = 0;
                maxTotal_ = 2000u;
                baseIncrement_ = 0;
                lookupShift_ = 5u;
                modelTotal_ = 0x1000u;
                cumulative_.assign(symbolCount_ + 1u, 0);
                freq_.assign(symbolCount_, 0);
                cumulative_[0] = 0;
                cumulative_[symbolCount_] = static_cast<uint16_t>(modelTotal_);
                bucketSize_ = static_cast<int>((symbolCount_ | 0x20u) >> 4u);
                const uint32_t q = modelTotal_ / symbolCount_;
                const uint32_t r = modelTotal_ % symbolCount_;
                for (uint32_t i = 0; i < symbolCount_; ++i)
                    freq_[i] = static_cast<uint16_t>(q + (i < r ? 1u : 0u));
                rebuild();
            }

            void interval(uint32_t symbol, uint32_t& low, uint32_t& span) const
            {
                if (symbol >= symbolCount_)
                    throw std::runtime_error("AS1 adaptive model symbol out of range");
                low = cumulative_[symbol];
                span = static_cast<uint32_t>(cumulative_[symbol + 1u]) - static_cast<uint32_t>(cumulative_[symbol]);
            }

            uint32_t symbolFromScaledCount(uint32_t scaled) const
            {
                if (scaled >= modelTotal_)
                    scaled = modelTotal_ - 1u;
                uint32_t lo = 0;
                uint32_t hi = symbolCount_;
                while (lo + 1u < hi)
                {
                    const uint32_t mid = (lo + hi) >> 1u;
                    if (scaled >= cumulative_[mid])
                        lo = mid;
                    else
                        hi = mid;
                }
                return lo;
            }

            void update(uint32_t symbol)
            {
                if (symbol >= symbolCount_)
                    throw std::runtime_error("AS1 adaptive model update symbol out of range");
                if (quota_ <= 0)
                    rebuild();
                --quota_;
                freq_[symbol] = static_cast<uint16_t>(freq_[symbol] + static_cast<uint16_t>(baseIncrement_ & 0xFFFF));
            }

        private:
            void rebuild()
            {
                if (extraQuota_ != 0)
                {
                    ++baseIncrement_;
                    quota_ = extraQuota_;
                    extraQuota_ = 0;
                    return;
                }

                if (bucketSize_ < static_cast<int>(maxTotal_))
                {
                    bucketSize_ += bucketSize_;
                    if (bucketSize_ > static_cast<int>(maxTotal_))
                        bucketSize_ = static_cast<int>(maxTotal_);
                }

                uint32_t remainder = modelTotal_;
                uint32_t cumulativeCursor = modelTotal_;
                for (int symbol = static_cast<int>(symbolCount_) - 1; symbol > 0; --symbol)
                {
                    const uint16_t oldFreq = freq_[static_cast<size_t>(symbol)];
                    cumulativeCursor -= oldFreq;
                    cumulative_[static_cast<size_t>(symbol)] = static_cast<uint16_t>(cumulativeCursor & 0xFFFFu);
                    const uint16_t halved = ceilHalfUnsigned16(oldFreq);
                    remainder -= halved;
                    freq_[static_cast<size_t>(symbol)] = halved;
                }

                uint16_t f0 = freq_[0];
                if (cumulativeCursor != static_cast<uint32_t>(f0))
                    throw std::runtime_error("AS1 adaptive model rebuild mismatch");
                f0 = ceilHalfUnsigned16(f0);
                freq_[0] = f0;
                remainder -= f0;
                cumulative_[0] = 0;
                cumulative_[symbolCount_] = static_cast<uint16_t>(modelTotal_);

                if (bucketSize_ > 0)
                {
                    baseIncrement_ = static_cast<int>(remainder / static_cast<uint32_t>(bucketSize_));
                    extraQuota_ = static_cast<int>(remainder % static_cast<uint32_t>(bucketSize_));
                    quota_ = bucketSize_ - extraQuota_;
                }
                else
                {
                    baseIncrement_ = 0;
                    extraQuota_ = 0;
                    quota_ = 0;
                }
            }

            uint32_t symbolCount_ = 257u;
            int quota_ = 0;
            int extraQuota_ = 0;
            int bucketSize_ = 0;
            uint32_t maxTotal_ = 2000u;
            int baseIncrement_ = 0;
            uint32_t lookupShift_ = 5u;
            uint32_t modelTotal_ = 0x1000u;
            std::vector<uint16_t> cumulative_;
            std::vector<uint16_t> freq_;
        };

        class As1RangeDecoder
        {
        public:
            bool open(const std::vector<BYTE>& bytes, size_t start, std::string& error)
            {
                input_ = &bytes;
                pos_ = start;
                code_ = 0;
                range_ = 0;
                unit_ = 0;
                cache_ = 0;
                if (pos_ + 2u > input_->size())
                {
                    error = "compressed stream is too short";
                    return false;
                }
                const BYTE first = readByte();
                cache_ = readByte();
                code_ = static_cast<uint32_t>(cache_) >> 1u;
                range_ = 128u;
                if (first != 0)
                {
                    error = "compressed stream has non-zero range prefix";
                    return false;
                }
                return true;
            }

            uint32_t getScaledCount(unsigned bits, std::string& error)
            {
                if (!normalize(error))
                    return 0;
                unit_ = range_ >> bits;
                if (unit_ == 0)
                {
                    error = "range decoder unit became zero";
                    return 0;
                }
                uint32_t scaled = code_ / unit_;
                if (scaled >> bits)
                    scaled = (1u << bits) - 1u;
                return scaled;
            }

            void update(uint32_t span, uint32_t low, uint32_t total)
            {
                const uint32_t lowPart = low * unit_;
                code_ -= lowPart;
                if (low + span >= total)
                    range_ -= lowPart;
                else
                    range_ = span * unit_;
            }

            bool finishEofNormalization(std::string& error)
            {
                return normalize(error);
            }

            size_t position() const { return pos_; }

        private:
            BYTE readByte()
            {
                if (!input_ || pos_ >= input_->size())
                    return 0;
                return (*input_)[pos_++];
            }

            bool normalize(std::string& error)
            {
                while (range_ <= 0x800000u)
                {
                    if (!input_ || pos_ >= input_->size())
                    {
                        error = "compressed stream ended during range normalization";
                        return false;
                    }
                    code_ = ((2u * code_) | (static_cast<uint32_t>(cache_) & 1u)) << 7u;
                    cache_ = readByte();
                    code_ |= static_cast<uint32_t>(cache_) >> 1u;
                    range_ <<= 8u;
                }
                return true;
            }

            const std::vector<BYTE>* input_ = nullptr;
            size_t pos_ = 0;
            uint32_t code_ = 0;
            uint32_t range_ = 0;
            uint32_t unit_ = 0;
            BYTE cache_ = 0;
        };

        using As1SurfCompressionContext = std::array<As1AdaptiveModel, 256>;

        bool decodeMakeVidCompressedBytes(const std::vector<BYTE>& packed,
                                          size_t start,
                                          size_t expectedOutputSize,
                                          bool mode2Interleave,
                                          As1SurfCompressionContext& contexts,
                                          std::vector<BYTE>& out,
                                          size_t& consumed,
                                          std::string& error)
        {
            out.assign(expectedOutputSize, 0);
            consumed = 0;
            if (expectedOutputSize == 0)
                return true;
            if (expectedOutputSize < 0x0Au)
            {
                if (start + expectedOutputSize > packed.size())
                {
                    error = "raw small compressed block exceeds payload";
                    return false;
                }
                std::copy(packed.begin() + static_cast<std::ptrdiff_t>(start),
                          packed.begin() + static_cast<std::ptrdiff_t>(start + expectedOutputSize),
                          out.begin());
                consumed = expectedOutputSize;
                return true;
            }

            As1RangeDecoder decoder;
            if (!decoder.open(packed, start, error))
                return false;

            const size_t half = (expectedOutputSize + 1u) >> 1u;
            uint32_t previous = 0;
            size_t produced = 0;

            for (;;)
            {
                const uint32_t scaled = decoder.getScaledCount(12u, error);
                if (!error.empty())
                    return false;

                As1AdaptiveModel& model = contexts[previous & 0xFFu];
                const uint32_t symbol = model.symbolFromScaledCount(scaled);
                if (symbol == 256u || produced >= expectedOutputSize)
                    break;

                const BYTE b = static_cast<BYTE>(symbol & 0xFFu);
                if (mode2Interleave)
                {
                    if (produced < half)
                        out[produced * 2u] = b;
                    else
                        out[(produced - half) * 2u + 1u] = b;
                }
                else
                {
                    out[produced] = b;
                }

                uint32_t low = 0;
                uint32_t span = 0;
                model.interval(symbol, low, span);
                decoder.update(span, low, 0x1000u);
                model.update(symbol);
                previous = symbol;
                ++produced;
            }

            {
                As1AdaptiveModel& model = contexts[previous & 0xFFu];
                uint32_t low = 0;
                uint32_t span = 0;
                model.interval(256u, low, span);
                decoder.update(span, low, 0x1000u);
            }

            if (!decoder.finishEofNormalization(error))
                return false;
            consumed = decoder.position() - start;

            if (produced != expectedOutputSize)
            {
                std::ostringstream oss;
                oss << "compressed stream produced " << produced << " byte(s), expected " << expectedOutputSize;
                error = oss.str();
                return false;
            }
            return true;
        }
#endif
    }


    float VID::calculateMoveUpZ(float verticalDelta, float projectedXYLength) const noexcept
    {

        const DWORD propertyFlags = properties();
        float result = 0.0f;

        if ((propertyFlags & 0x00000002u) != 0)
        {
            const BASE_CONSTANTS* const constants = GlobalBaseConstants();
            float c08 = 0.0f;
            std::memcpy(&c08, &constants->raw[2], sizeof(c08));
            result = projectedXYLength * c08 / maxSpeedValue() * 0.5f + verticalDelta * maxSpeedValue() / projectedXYLength;
            result *= (result > 0.0f) ? 1.1f : 0.89999998f;
        }
        else if ((propertyFlags & 0x00000004u) != 0)
        {
            const BASE_CONSTANTS* const constants = GlobalBaseConstants();
            float c0C = 0.0f;
            std::memcpy(&c0C, &constants->raw[3], sizeof(c0C));
            result = projectedXYLength * c0C / maxSpeedValue() * 0.5f + verticalDelta * maxSpeedValue() / projectedXYLength;
            result *= (result > 0.0f) ? 1.1f : 0.89999998f;
        }
        else if ((propertyFlags & 0x08000000u) != 0)
        {
            result = maximumZSpeed();
        }
        else
        {
            const float topZGate = topZValue();

            if (topZGate == 0.0f || std::isnan(topZGate))
                result = verticalDelta * maxSpeedValue() / projectedXYLength;
            else
                result = 0.0f;
        }


        if (result > maximumZSpeed())
            result = maximumZSpeed();
        else
        {
            const float negativeMaxZSpeed = -maximumZSpeed();

            if (result < negativeMaxZSpeed || std::isnan(result))
                result = negativeMaxZSpeed;
        }
        return result;
    }

    VID::VID()
    {
        nVid = -1;
        spriteType = 0;
        spriteClass = 6;
        property = 0;
        moveMask = 0;
        sizeXYZ = VECTOR{24.0f, 16.0f, 20.0f};
        maxHp = 0;
        maxSpeed = 0.0f;
        maxZSpeed = 0.0f;
        acceleration = 0.0f;
        slow = 0.0f;
        rotationSpeed = 0.0f;
        nWeapon = 0;
        deathRange = 0.0f;
        deathDamageMin = 0.0f;
        linkXYZ = VECTOR{};
        nLinkVid = 0;
        linkVid = nullptr;
        topZ = 0.0f;
        forMoveUpZ = 0.0f;
        forMoveDownZ = 0.0f;
        lifeTime = 999999;
        noDir = 1;

        std::fill(std::begin(noAnimCadr), std::end(noAnimCadr), 0);
        std::fill(std::begin(sfx), std::end(sfx), 0);
        std::fill(std::begin(frameSpeed), std::end(frameSpeed), 71);
        std::fill(std::begin(childX), std::end(childX), 0.0f);
        std::fill(std::begin(childY), std::end(childY), 0.0f);
        std::fill(std::begin(childZ), std::end(childZ), 0.0f);
        std::fill(std::begin(nChildVid), std::end(nChildVid), 0);
        std::fill(std::begin(childVid), std::end(childVid), nullptr);
        std::fill(std::begin(noChild), std::end(noChild), 0);

        gammaRaw = GammaRawPair{};
        scaleXYZ = VECTOR{1.0f, 1.0f, 1.0f};
        type = 0;
        frameSpeedDefault = 71;
        noCadr = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
        vidSizeX = 0;
        vidSizeY = 0;
        reservedHeaderPadding2 = 0;
#endif
        std::fill(std::begin(animationBaseFrame), std::end(animationBaseFrame), 0);
        std::fill(std::begin(animationFrameCount), std::end(animationFrameCount), 0);
        halfSizeXY = VECTOR2{12.0f, 8.0f};
        layer = 15;
        directionQuantizationOffsetValue = 0;
        unitLimits.fill(0);
        std::fill(std::begin(spriteCountsByArmy), std::end(spriteCountsByArmy), 0u);
        std::fill(std::begin(killedUnitCounters), std::end(killedUnitCounters), 0);
        std::fill(std::begin(recolorUnitCounters), std::end(recolorUnitCounters), 0);
        std::fill(std::begin(animationFrameDurations), std::end(animationFrameDurations), 0);
        std::fill(std::begin(altGammaRaw), std::end(altGammaRaw), GammaRawPair{});
        std::fill(std::begin(scriptFunction), std::end(scriptFunction), -1);
        scriptAux0 = -1;
        scriptAux1 = -1;
        lastSpriteCountChangeTimestampMs = 0;
        weapon = nullptr;
        nextMirror = this;
        exchangedVid = this;
        noGridZ = 0;
        gridZ = nullptr;
        gridCadrShift = nullptr;
        movementTactEnabledValue = 0;
        actionAuxStateRequiredValue = 0;
        vidRuntimeFlags = 0;
        notCreateAsChildFlag = 0;

        initializeRuntimeCountersAndCallbacks();
    }

    VID* VID::CreateMirror()
    {

        return new (std::nothrow) VID();
    }

    VID* baseVidScalarDeletingDestructor(VID* owner, unsigned char deletingFlags) noexcept
    {

        owner->~VID();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    void VID::AddVidToVid(SPRITE*)
    {

    }

    void VID::DrawToVid(SPRITE*, void*, BASE_TEXTURE*, BASE_TEXTURE*)
    {

    }

    void VID::Load(RESOURCE*)
    {

    }

    VID::~VID()
    {

        const DWORD liveSpriteSum = spriteCountAcrossArmies();
        if (liveSpriteSum != 0)
            ReportResourceError(10, "Not all sprites with this VID deleted", static_cast<int>(liveSpriteSum));

        unlinkMirrorRing();

        if (gridZ)
        {
            delete[] gridZ;
            gridZ = nullptr;
        }
        if (gridCadrShift)
        {
            delete[] gridCadrShift;
            gridCadrShift = nullptr;
        }
        noGridZ = 0;

#if !defined(_MSC_VER) || !defined(_M_IX86)
        eraseVidHostSidecar(this);
#endif
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    std::vector<VID::PaletteEntry>& VID::hostPaletteStorage() { return vidHostSidecar(this).palette; }
    std::vector<VID::DataFrame>& VID::hostDataFramesStorage() { return vidHostSidecar(this).dataFrames; }
    std::vector<VID::SurfacePage>& VID::hostSurfacePagesStorage() { return vidHostSidecar(this).surfacePages; }
    std::vector<VID::SurfaceRecord>& VID::hostSurfaceRecordsStorage() { return vidHostSidecar(this).surfaceRecords; }
    std::vector<VID::LightFrame>& VID::hostLightFramesStorage() { return vidHostSidecar(this).lightFrames; }
    std::vector<VID::FrameSurface>& VID::hostFrameSurfacesStorage() { return vidHostSidecar(this).frameSurfaces; }
    std::vector<std::string>& VID::hostDecodeWarningsStorage() { return vidHostSidecar(this).decodeWarnings; }
    std::vector<DWORD>& VID::hostGammaPaletteBufferStorage() { return vidHostSidecar(this).gammaPaletteBuffer; }
    bool& VID::hostCompressedSurfPresentStorage() { return vidHostSidecar(this).compressedSurfPresent; }
    size_t& VID::hostCompressedSurfBytesStorage() { return vidHostSidecar(this).compressedSurfBytes; }
#endif
    int& VID::hostFrameSpeedStorage(int animation) { return frameSpeed[static_cast<std::size_t>(animation) % NO_ANIMATION]; }
    int VID::hostFrameSpeedStorage(int animation) const { return frameSpeed[static_cast<std::size_t>(animation) % NO_ANIMATION]; }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    const std::vector<VID::PaletteEntry>& VID::palette() const { return vidHostSidecar(this).palette; }
    const std::vector<VID::DataFrame>& VID::dataFrames() const { return vidHostSidecar(this).dataFrames; }
    const std::vector<VID::SurfacePage>& VID::surfacePages() const { return vidHostSidecar(this).surfacePages; }
    const std::vector<VID::SurfaceRecord>& VID::surfaceRecords() const { return vidHostSidecar(this).surfaceRecords; }
    const std::vector<VID::LightFrame>& VID::lightFrames() const { return vidHostSidecar(this).lightFrames; }
    const std::vector<VID::FrameSurface>& VID::frameSurfaces() const { return vidHostSidecar(this).frameSurfaces; }
    const std::vector<std::string>& VID::decodeWarnings() const { return vidHostSidecar(this).decodeWarnings; }
    const std::vector<DWORD>& VID::gammaPaletteBuffer() const { return vidHostSidecar(this).gammaPaletteBuffer; }
    bool VID::hasDecodedPalette() const { return !palette().empty(); }
    bool VID::hasDecodedDataFrames() const { return !dataFrames().empty(); }
    bool VID::hasDecodedSurfacePages() const { return !surfacePages().empty(); }
    bool VID::hasDecodedSurfaceRecords() const { return !surfaceRecords().empty(); }
    bool VID::hasDecodedLightFrames() const { return !lightFrames().empty(); }
    bool VID::hasFrameSurfaces() const { return !frameSurfaces().empty(); }
    bool VID::hasCompressedSurf() const { return vidHostSidecar(this).compressedSurfPresent; }
    size_t VID::compressedSurfBytes() const { return vidHostSidecar(this).compressedSurfBytes; }

#endif

    int VID::CanFight() const noexcept
    {

        return (hasWeaponChildDescriptor() != 0u && weaponCount() != 0u) ? 1 : 0;
    }

    int VID::activeWeaponAmmoCapacity() const noexcept
    {

        const VID* owner = this;
        VID* const link = linkedVid();
        if (link && link->CanFight() != 0)
            owner = link;

        return owner->weaponRecordAmmoCapacity();
    }

    int VID::frameTimeForBucket(int bucket) const noexcept
    {

        return animationFrameDuration(bucket);
    }

    int VID::spriteCountForBucket(int bucket) const noexcept
    {

        return static_cast<int>(spriteCountForArmy(bucket));
    }

    int VID::totalSpriteCount() const noexcept
    {

        return static_cast<int>(spriteCountAcrossArmies());
    }

    int VID::killedUnitCountForArmy(int bucket) const noexcept
    {

        return killedUnitCounterValue(bucket);
    }

    int VID::totalKilledUnitCount() const noexcept
    {

        return killedUnitCountForArmy(0) +
               killedUnitCountForArmy(1) +
               killedUnitCountForArmy(2) +
               killedUnitCountForArmy(3);
    }

    int VID::recolorUnitCountForArmy(int bucket) const noexcept
    {

        return recolorUnitCounterValue(bucket);
    }

    int VID::totalRecolorUnitCount() const noexcept
    {

        return recolorUnitCountForArmy(0) +
               recolorUnitCountForArmy(1) +
               recolorUnitCountForArmy(2) +
               recolorUnitCountForArmy(3);
    }

    int VID::notCreateAsChild() const noexcept
    {

        return notCreateAsChildFlag;
    }

    int VID::setNotCreateAsChild(int value) noexcept
    {

        notCreateAsChildFlag = value;
        return notCreateAsChildFlag;
    }

    int VID::PropBirthAsSmoke() const noexcept
    {

        return static_cast<int>(properties() & P_BIRTHASSMOKE);
    }

    int VID::isHidden() const noexcept
    {
        return static_cast<int>((vidRuntimeFlags >> 6u) & 1u);
    }

    int VID::noChildValueForDataCode(int type) const noexcept
    {
        if (type < script::VidNoChildFirst || type >= script::VidNoChildEnd)
            return 0;
        return noChild[static_cast<std::size_t>(type - script::VidNoChildFirst)];
    }

    void VID::setNoChildValueForDataCode(int type, int value) noexcept
    {
        // Use the same no-child table as spawnAnimationChild.
        if (type < script::VidNoChildFirst || type >= script::VidNoChildEnd)
            return;
        noChild[static_cast<std::size_t>(type - script::VidNoChildFirst)] = value;
    }

    int VID::childNvidForDataCode(int type) const noexcept
    {
        if (type < script::VidChildFirst || type >= script::VidChildEnd)
            return 0;
        return nChildVid[static_cast<std::size_t>(type - script::VidChildFirst)];
    }

    void VID::setChildNvidForDataCode(int type, int value) noexcept
    {
        if (type < script::VidChildFirst || type >= script::VidChildEnd)
            return;
        nChildVid[static_cast<std::size_t>(type - script::VidChildFirst)] = value;
    }

    VID* VID::childVidForDataCode(int type) const noexcept
    {
        if (type < script::VidChildFirst || type >= script::VidChildEnd)
            return nullptr;
        return childVid[static_cast<std::size_t>(type - script::VidChildFirst)];
    }

    void VID::setChildVidForDataCode(int type, VID* value) noexcept
    {
        if (type < script::VidChildFirst || type >= script::VidChildEnd)
            return;
        childVid[static_cast<std::size_t>(type - script::VidChildFirst)] = value;
    }

    int VID::weaponIntAt(int offset) const noexcept
    {
        const WEAPON* const w = weaponRecord();
#if defined(_MSC_VER) && defined(_M_IX86)
        return *reinterpret_cast<const std::int32_t*>(w->raw.data() + offset);
#else
        std::int32_t value = 0;
        std::memcpy(&value, w->raw.data() + offset, sizeof(value));
        return static_cast<int>(value);
#endif
    }

    float VID::weaponFloatAt(int offset) const noexcept
    {
        const WEAPON* const w = weaponRecord();
#if defined(_MSC_VER) && defined(_M_IX86)
        return *reinterpret_cast<const float*>(w->raw.data() + offset);
#else
        float value = 0.0f;
        std::memcpy(&value, w->raw.data() + offset, sizeof(value));
        return value;
#endif
    }

    void VID::setWeaponIntAt(int offset, int value) noexcept
    {
        WEAPON* const w = weaponRecord();
        const std::int32_t raw = static_cast<std::int32_t>(value);
        std::memcpy(w->raw.data() + offset, &raw, sizeof(raw));
    }

    void VID::setWeaponFloatAt(int offset, float value) noexcept
    {
        WEAPON* const w = weaponRecord();
        std::memcpy(w->raw.data() + offset, &value, sizeof(value));
    }

    DWORD VID::spriteCountForArmy(int index) const
    {
        return spriteCountsByArmy[index & 3];
    }

    DWORD VID::spriteCountAcrossArmies() const
    {
        return spriteCountsByArmy[0] +
               spriteCountsByArmy[1] +
               spriteCountsByArmy[2] +
               spriteCountsByArmy[3];
    }

    void VID::incrementSpriteCountForArmy(int index)
    {
        ++spriteCountsByArmy[index & 3];
    }

    void VID::decrementSpriteCountForArmy(int index)
    {
        DWORD& counter = spriteCountsByArmy[index & 3];
        if (counter != 0)
            --counter;
    }


    int VID::killedUnitCounterValue(int bucket) const noexcept
    {
        return killedUnitCounters[bucket & 3];
    }

    void VID::setKilledUnitCountForArmy(int bucket, int value) noexcept
    {
        killedUnitCounters[bucket & 3] = value;
    }

    void VID::incrementKilledUnitCountForArmy(int bucket) noexcept
    {
        ++killedUnitCounters[bucket & 3];
    }

    int VID::recolorUnitCounterValue(int bucket) const noexcept
    {
        return recolorUnitCounters[bucket & 3];
    }

    void VID::setRecolorUnitCountForArmy(int bucket, int value) noexcept
    {
        recolorUnitCounters[bucket & 3] = value;
    }

    int VID::animationFrameDuration(int bucket) const noexcept
    {
        return animationFrameDurations[bucket & 3];
    }

    void VID::setAnimationFrameDuration(int bucket, int value) noexcept
    {
        animationFrameDurations[bucket & 3] = value;
    }

    void VID::initializeRuntimeCountersAndCallbacks() noexcept
    {

        vidRuntimeFlags &= ~0x10u;
        std::fill(std::begin(scriptFunction), std::end(scriptFunction), -1);
        scriptAux0 = -1;
        scriptAux1 = -1;
        unitLimits.fill(-1);
        std::fill(std::begin(spriteCountsByArmy), std::end(spriteCountsByArmy), 0u);
        std::fill(std::begin(killedUnitCounters), std::end(killedUnitCounters), 0);
        std::fill(std::begin(recolorUnitCounters), std::end(recolorUnitCounters), 0);
        std::fill(std::begin(animationFrameDurations), std::end(animationFrameDurations), maxHp);
        lastSpriteCountChangeTimestampMs = 0;
    }

    int VID::calculateLinkedContribution() const noexcept
    {

        std::int32_t linkContribution = 0;
        if (const VID* link = linkedVid())
            linkContribution = static_cast<std::int32_t>(link->calculateLinkedContribution());

        std::int32_t deathChildContribution = 0;
        if (const VID* deathChild = deathChildVid())
            deathChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(deathChild->calculateLinkedContribution()),
                static_cast<std::int32_t>(deathNoChildValue()));

        std::int32_t birthChildContribution = 0;
        if (const VID* birthChild = birthChildVid())
            birthChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(birthChild->calculateLinkedContribution()),
                static_cast<std::int32_t>(birthNoChildValue()));

        std::int32_t fightChildContribution = 0;
        if (const VID* fightChild = fightChildVid())
            fightChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(fightChild->calculateLinkedContribution()),
                static_cast<std::int32_t>(fightNoChildValue()));

        std::int32_t contribution = static_cast<std::int32_t>(deathDamageMinimumRawBits());
        contribution = addWrap32(contribution, fightChildContribution);
        contribution = addWrap32(contribution, birthChildContribution);
        contribution = addWrap32(contribution, deathChildContribution);
        contribution = addWrap32(contribution, linkContribution);
        return contribution;
    }

    int VID::productionCost() const noexcept
    {

        const VID* owner = this;
        if (const VID* link = linkedVid())
        {
            if (link->weaponCount() != 0u)
                owner = link;
        }

        const WEAPON* const weapon = owner->weaponRecord();
        std::int32_t value = 0;
        std::memcpy(&value, weapon->raw.data() + 0x24, sizeof(value));
        return divideBy1000SignedMagic(value);
    }

    int VID::setLinkedHidden(int enabled) noexcept
    {
        constexpr unsigned int mask = 0x00000040u;
        const unsigned int bit = enabled != 0 ? mask : 0u;
        for (VID* vid = this; vid; vid = vid->linkedVid())
            vid->vidRuntimeFlags = (vid->vidRuntimeFlags & ~mask) | bit;
        return enabled;
    }

    int VID::setBucketFramePercent(int bucketIndex, int percentValue) noexcept
    {

        const int bucket = bucketIndex & 3;
        std::int32_t returnValue = 0;
        for (VID* vid = this; vid; vid = vid->linkedVid())
        {
            const int oldDuration = vid->animationFrameDuration(bucket);
            if (percentValue >= 0)
                vid->setAnimationFrameDuration(bucket, percentOfMaxHpDuration(vid->maxHp, percentValue));

            returnValue = vid->maxHp;
            if (vid->maxHp == 0)
                continue;

            const int layer = vid->renderLayer();
            returnValue = layer;
            const core::ApplicationDrawPassBucket& passBucket =
                core::GlobalApplicationDrawDispatcherState().drawPassBucket(layer);
            int cursor = passBucket.count() - 1;
            if (cursor < 0)
                continue;

            SPRITE* sprite = nullptr;
            for (;;)
            {
                SPRITE* const* slots = passBucket.data();
                returnValue = pointerLow32(slots);
                while (cursor >= 0 && slots[cursor] == nullptr)
                    --cursor;
                if (cursor < 0)
                    break;

                sprite = slots[cursor];
                if (sprite->Vid() == vid)
                {
                    returnValue = static_cast<std::int32_t>(sprite->armyIndex());
                    if (returnValue == bucket)
                    {
                        const int nextDuration = vid->animationFrameDuration(bucket);
                        const int frameTime = scaleFrameTimeBySixteenth(
                            sprite->animationFrameTime(), nextDuration, oldDuration);
                        sprite->updateAnimationFrameTime(frameTime);
                    }
                }

                returnValue = layer;
                --cursor;
                if (cursor < 0)
                    break;
            }
        }
        return returnValue;
    }

    int VID::setBucketFrameTime(int bucketIndex, int frameTimeValue) noexcept
    {

        const int bucket = bucketIndex & 3;
        for (VID* vid = this; vid; vid = vid->linkedVid())
        {
            const int oldDuration = vid->animationFrameDuration(bucket);
            if (frameTimeValue >= 0)
                vid->setAnimationFrameDuration(bucket, frameTimeValue);

            if (vid->maxHp == 0)
                continue;

            const int layer = vid->renderLayer();
            const core::ApplicationDrawPassBucket& passBucket =
                core::GlobalApplicationDrawDispatcherState().drawPassBucket(layer);
            int cursor = passBucket.count() - 1;
            if (cursor < 0)
                continue;

            for (;;)
            {
                SPRITE* const* slots = passBucket.data();
                while (cursor >= 0 && slots[cursor] == nullptr)
                    --cursor;
                if (cursor < 0)
                    break;
                SPRITE* const sprite = slots[cursor];
                if (sprite->Vid() == vid &&
                    sprite->armyIndex() == bucket)
                {
                    const int nextDuration = vid->animationFrameDuration(bucket);
                    const int frameTime = scaleFrameTimeByByteFraction(
                        sprite->animationFrameTime(), nextDuration, oldDuration);
                    sprite->updateAnimationFrameTime(frameTime);
                }
                --cursor;
                if (cursor < 0)
                    break;
            }
        }
        return 0;
    }

    void VID::unlinkMirrorRing()
    {

        VID* const next = nextMirrorVid();
        if (next == this)
            return;

        VID* previous = next;
        while (previous->nextMirror != this)
            previous = previous->nextMirror;

        previous->nextMirror = next;
    }

    void VID::loadBasicParameters(RESOURCE* globalRes)
    {
        if (!globalRes)
            return;

        short sx = 0;
        short sy = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
        (void)globalRes->read(&frameSpeedDefault, 2);
        (void)globalRes->read(&noCadr, 2);
        (void)globalRes->read(&sx, 2);
        (void)globalRes->read(&sy, 2);
#else
        readExact(globalRes, frameSpeedDefault, "frameSpeedDefault");
        readExact(globalRes, noCadr, "noCadr");
        readExact(globalRes, sx, "vidSizeX");
        readExact(globalRes, sy, "vidSizeY");
#endif
        setVidWidth(sx);
        setVidHeight(sy);
    }

    void VID::LoadParameters(RESOURCE* res)
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        (void)res->read(&spriteType, 4);
        (void)res->read(&spriteClass, 4);
        (void)res->read(&property, 4);
        (void)res->read(&moveMask, 4);
        (void)res->read(&sizeXYZ.x, 4);
        (void)res->read(&sizeXYZ.y, 4);
        (void)res->read(&sizeXYZ.z, 4);
        (void)res->read(&maxHp, 4);
        (void)res->read(&maxSpeed, 4);
        (void)res->read(&maxZSpeed, 4);
        (void)res->read(&acceleration, 4);
        (void)res->read(&slow, 4);
        (void)res->read(&rotationSpeed, 4);
        (void)res->read(&nWeapon, 4);
        (void)res->read(&deathRange, 4);
        (void)res->read(&deathDamageMin, 4);
        (void)res->read(&linkXYZ.x, 4);
        (void)res->read(&linkXYZ.y, 4);
        (void)res->read(&linkXYZ.z, 4);
        (void)res->read(&nLinkVid, 4);
        // The runtime linkVid pointer is not serialized.
        (void)res->read(&topZ, 4);
        (void)res->read(&forMoveUpZ, 4);
        (void)res->read(&forMoveDownZ, 4);
        (void)res->read(&lifeTime, 4);

        res->retailShiftCurrentUnchecked(16);
        (void)res->read(&noDir, 4);

        (void)res->read(noAnimCadr, sizeof(noAnimCadr));
        (void)res->read(sfx, sizeof(sfx));
        (void)res->read(frameSpeed, sizeof(frameSpeed));
        (void)res->read(childX, sizeof(childX));
        (void)res->read(childY, sizeof(childY));
        (void)res->read(childZ, sizeof(childZ));
        (void)res->read(nChildVid, sizeof(nChildVid));
        (void)res->read(noChild, sizeof(noChild));

        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        (void)res->read(&red, 4);
        (void)res->read(&green, 4);
        (void)res->read(&blue, 4);
        (void)res->read(&alpha, 4);

        gammaRaw.setSignedDeltasRetail(alpha, red, green, blue);

        (void)res->read(&scaleXYZ.x, 4);
        (void)res->read(&scaleXYZ.y, 4);
        (void)res->read(&scaleXYZ.z, 4);
#else
        readExact(res, spriteType, "spriteType");
        readExact(res, spriteClass, "spriteClass");
        readExact(res, property, "property");
        readExact(res, moveMask, "moveMask");
        sizeXYZ.read(res);
        readExact(res, maxHp, "maxHp");
        readExact(res, maxSpeed, "maxSpeed");
        readExact(res, maxZSpeed, "maxZSpeed");
        readExact(res, acceleration, "acceleration");
        readExact(res, slow, "slow");
        readExact(res, rotationSpeed, "rotationSpeed");
        readExact(res, nWeapon, "nWeapon");
        readExact(res, deathRange, "deathRange");
        readExact(res, deathDamageMin, "deathDamageMin");
        linkXYZ.read(res);
        readExact(res, nLinkVid, "nLinkVid");
        readExact(res, topZ, "topZ");
        readExact(res, forMoveUpZ, "forMoveUpZ");
        readExact(res, forMoveDownZ, "forMoveDownZ");
        readExact(res, lifeTime, "lifeTime");
        (void)res->shift(16);
        readExact(res, noDir, "noDir");

        readIntArray(res, noAnimCadr, NO_ANIMATION, "noAnimCadr");
        for (int i = 0; i < NO_ANIMATION; ++i)
            readExact(res, sfx[i], "sfx");
        for (int i = 0; i < NO_ANIMATION; ++i)
            readExact(res, hostFrameSpeedStorage(i), "frameSpeed");
        for (int i = 0; i < NO_ANIMATION; ++i)
            readExact(res, childX[i], "childX");
        for (int i = 0; i < NO_ANIMATION; ++i)
            readExact(res, childY[i], "childY");
        for (int i = 0; i < NO_ANIMATION; ++i)
            readExact(res, childZ[i], "childZ");
        readIntArray(res, nChildVid, NO_ANIMATION, "nChildVid");
        for (int i = 0; i < NO_ANIMATION; ++i)
            readExact(res, noChild[i], "noChild");

        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        readExact(res, red, "gamma.red");
        readExact(res, green, "gamma.green");
        readExact(res, blue, "gamma.blue");
        readExact(res, alpha, "gamma.alpha");

        gammaRaw.setSignedDeltasRetail(alpha, red, green, blue);
        scaleXYZ.read(res);
#endif

        if ((formatFlags() & VID_TYPE_ZBUFFER) != 0u &&
            (formatFlags() & VID_TYPE_HARDWARE) != 0u)
            scaleXYZ = VECTOR{1.0f, 1.0f, 1.0f};

        if (rotationSpeed == UNLIMITED)
            rotationSpeed = 0.0f;
        else if (rotationSpeed == 0.0f)
            rotationSpeed = UNLIMITED;
        else
            rotationSpeed = 256.0f / rotationSpeed;

        if (maxSpeed != UNLIMITED)
            maxSpeed /= 1000.0f;
        if (maxZSpeed != UNLIMITED)
            maxZSpeed /= 1000.0f;
        if (acceleration != UNLIMITED)
            acceleration /= 1000000.0f;
        if (slow != UNLIMITED)
            slow /= 1000000.0f;

        setMovementTactEnabled(
            (maxSpeed != 0.0f || maxZSpeed != 0.0f || (property & 0x00001006u) != 0u) ? 1 : 0);

        if (noDir == 0)
        {
            ReportResourceError(4, "NoDir==0", 0);
            std::exit(1);
        }
        for (int i = 0; i < NO_ANIMATION; ++i)
            if (frameSpeed[i] == 0)
                frameSpeed[i] = static_cast<int>(frameSpeedDefault);

        halfSizeXY = { sizeXYZ.x * 0.5f, sizeXYZ.y * 0.5f };
        setDirectionQuantizationOffset(128 / noDir);

        if (noCadr != 0)
        {
            if (noCadr < noDir)
            {
                ReportResourceError(4, "noCadr < noDir", 0);
                noDir = noCadr;
            }
        }
        else
        {
            ReportResourceError(4, "noCadr==0", 0);
        }

        const int noCadrCount = static_cast<int>(static_cast<std::uint16_t>(noCadr));

        int requestedFrames = 0;
        for (int i = 0; i < NO_ANIMATION; ++i)
            requestedFrames += static_cast<int>(noAnimCadr[i]) * static_cast<int>(noDir);

        if (requestedFrames > noCadrCount)
        {
            ReportResourceError(13, "noCadr for noAnimCadr and noDir", 0);
            for (int i = NO_ANIMATION - 1; i >= 0 && requestedFrames > noCadrCount; --i)
            {
                const int currentAnimFrames = static_cast<int>(noAnimCadr[i]) * static_cast<int>(noDir);
                const int withoutCurrent = requestedFrames - currentAnimFrames;
                if (withoutCurrent > noCadrCount)
                {
                    noAnimCadr[i] = 0;
                    requestedFrames = withoutCurrent;
                    continue;
                }

                const int overrun = requestedFrames - noCadrCount;
                noAnimCadr[i] -= overrun / noDir;
                break;
            }
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        const std::uint8_t* const soundOwner =
            reinterpret_cast<const std::uint8_t*>(sound::GlobalSoundEngine());
        const std::uint8_t* const soundTable =
            *reinterpret_cast<const std::uint8_t* const*>(soundOwner + 0x08u);
        if (soundTable != nullptr)
        {
            const int loadedSfxCount =
                *reinterpret_cast<const int*>(soundOwner + 0x04u);
            for (int i = 0; i < NO_ANIMATION; ++i)
            {
                const int nsfx = sfx[i];
                bool sfxLoaded = false;
                if (nsfx >= 0 && nsfx <= loadedSfxCount)
                {
                    constexpr std::size_t kRetailSfxEntrySize = 72u;
                    constexpr std::size_t kRetailFirstBufferOffset = 0x24u;
                    const std::uint8_t* const entry =
                        soundTable + static_cast<std::size_t>(nsfx) * kRetailSfxEntrySize;
                    sfxLoaded = *reinterpret_cast<void* const*>(
                        entry + kRetailFirstBufferOffset) != nullptr;
                }

                if (!sfxLoaded && nVid != -1)
                {
                    ReportResourceError(4, "sfx", nsfx);
                    sfx[i] = 0;
                }
            }
        }
#else
        sound::Engine* const soundEngine = sound::GlobalSoundEngine();
        if (soundEngine != nullptr)
        {
            const int loadedSfxCount = soundEngine->loadedSoundCount();
            for (int i = 0; i < NO_ANIMATION; ++i)
            {
                const int nsfx = sfx[i];
                const bool sfxLoaded =
                    nsfx >= 0 && nsfx <= loadedSfxCount &&
                    soundEngine->loadedSoundBufferCount(nsfx) != 0;
                if (!sfxLoaded && nVid != -1)
                {
                    ReportResourceError(4, "sfx", nsfx);
                    sfx[i] = 0;
                }
            }
        }
#endif

        int totalCadr = 0;
        int firstRealAnim = -1;
        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int animCadr = static_cast<int>(noAnimCadr[i]);
            if (animCadr != 0)
            {
                animationBaseFrame[i] = totalCadr;
                animationFrameCount[i] = animCadr;

                if (firstRealAnim < 0)
                {
                    firstRealAnim = i;
                    for (int j = 0; j < i; ++j)
                    {
                        if (animationFrameCount[j] == 0)
                            animationFrameCount[j] = animCadr;
                    }
                }
            }
            else
            {
                bool copiedClass10OddFallback = false;
                if (spriteClass == 0x0Au && (i & 1) != 0 && i <= 7 &&
                    noAnimCadr[firstRealAnim + 1] != 0)
                {

                    animationBaseFrame[i] = animationBaseFrame[firstRealAnim + 1];
                    animationFrameCount[i] = animationFrameCount[firstRealAnim + 1];
                    copiedClass10OddFallback = true;
                }

                if (!copiedClass10OddFallback)
                {
                    animationBaseFrame[i] = 0;
                    animationFrameCount[i] = animationFrameCount[firstRealAnim];
                }
            }

            totalCadr += animCadr * static_cast<int>(noDir);
            if (totalCadr > noCadrCount)
            {
                ReportResourceError(10, "noCadr and noAnimCadr and noDir", i);
                animationBaseFrame[i] = 0;
                animationFrameCount[i] = animationFrameCount[firstRealAnim];
            }
        }

        if (spriteClass == 8)
        {
            vidRuntimeFlags |= 0x20u;
#if defined(_MSC_VER) && defined(_M_IX86)
            const std::uint8_t* const appOwner =
                static_cast<const std::uint8_t*>(core::ApplicationPhysicalOwner());
            const DWORD appFlags = *reinterpret_cast<const DWORD*>(
                appOwner + core::retail_application_layout::Flags);
#else
            const DWORD appFlags = core::ApplicationFlags();
#endif
            if ((appFlags & 0x00000001u) != 0u)
                spriteClass = 0;
        }

        if ((property & 0x00000008u) != 0u)
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            const int allocationExtent = static_cast<int>(sizeXYZ.x) + 17;
            const int allocationRows = allocationExtent / 8;
            const int scratchCount = (allocationRows * allocationExtent) / 8 + 1;
            const std::size_t scratchBytes =
                static_cast<std::size_t>(scratchCount) * sizeof(VECTOR);
            VECTOR* const scratch =
                static_cast<VECTOR*>(::operator new[](scratchBytes));

            noGridZ = 0;
            for (int y = 0; static_cast<float>(y) < sizeXYZ.y; y += 8)
            {
                for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
                {
                    VECTOR& dot = scratch[noGridZ++];
                    dot.x = static_cast<float>(x) - halfSizeXY.x;
                    dot.y = static_cast<float>(y) - halfSizeXY.y;
                    dot.z = sizeXYZ.z;
                }

                VECTOR& rightDot = scratch[noGridZ++];
                rightDot.x = halfSizeXY.x;
                rightDot.y = static_cast<float>(y) - halfSizeXY.y;
                rightDot.z = sizeXYZ.z;
            }

            for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
            {
                VECTOR& bottomDot = scratch[noGridZ++];
                bottomDot.x = static_cast<float>(x) - halfSizeXY.x;
                bottomDot.y = halfSizeXY.y;
                bottomDot.z = sizeXYZ.z;
            }

            VECTOR& corner = scratch[noGridZ++];
            corner.x = halfSizeXY.x;
            corner.y = halfSizeXY.y;
            corner.z = sizeXYZ.z;

            if (gridZ)
                ::operator delete[](gridZ);

            const std::size_t finalBytes =
                static_cast<std::size_t>(noGridZ) * sizeof(VECTOR);
            gridZ = static_cast<VECTOR*>(::operator new[](finalBytes));
            for (int i = 0; i < noGridZ; ++i)
                gridZ[i] = scratch[i];

            ::operator delete[](scratch);
#else
            calcBuildSizeToGridZ();
#endif
        }
    }

    std::intptr_t VID::logVidResourceError(int errorCode, const char* detailText, int detailValue) const
    {

        return logFileLoggerResourceError(
            g_fileLogger, "VID [%i-%s]", errorCode, detailText, detailValue, nVid, name.c_str());
    }

    void VID::ReportResourceError(int errorCode, const char* detailText, int detailValue) const
    {
        (void)logVidResourceError(errorCode, detailText, detailValue);
    }

    void VID::SetChildAndLink()
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint8_t* const appOwner =
            static_cast<std::uint8_t*>(core::ApplicationPhysicalOwner());

        const int linkedVidId = nLinkVid;
        if (linkedVidId)
        {
            VID* tableVid = nullptr;
            if (linkedVidId >= 0)
            {
                const int vidCount =
                    *reinterpret_cast<const int*>(
                        appOwner + core::retail_application_layout::VidCount);
                if (linkedVidId < vidCount)
                {
                    VID* const candidate =
                        *reinterpret_cast<VID* const*>(
                            appOwner + core::retail_application_layout::VidTable +
                            static_cast<std::size_t>(linkedVidId) * sizeof(VID*));
                    if (candidate)
                        tableVid = candidate;
                }
            }

            if (tableVid)
                linkVid = tableVid->exchangedVid;
            else
                ReportResourceError(4, "LinkVid", linkedVidId);
        }

        const std::uint8_t* weaponLane = weapon->raw.data() + 0x084;
        int weaponLaneCount = 8;
        do
        {
            if (*reinterpret_cast<const std::int32_t*>(weaponLane - 0x020) != 0 ||
                *reinterpret_cast<const std::int32_t*>(weaponLane + 0x000) != 0 ||
                *reinterpret_cast<const std::int32_t*>(weaponLane + 0x020) != 0 ||
                *reinterpret_cast<const std::int32_t*>(weaponLane + 0x040) != 0)
            {
                vidRuntimeFlags |= 0x01u;
            }

            if (*reinterpret_cast<const float*>(weaponLane + 0x060) != 1.0f ||
                *reinterpret_cast<const float*>(weaponLane + 0x080) != 1.0f ||
                *reinterpret_cast<const float*>(weaponLane + 0x0A0) != 1.0f)
            {
                vidRuntimeFlags |= 0x02u;
            }

            if (*reinterpret_cast<const float*>(weaponLane + 0x0C0) != 0.0f ||
                *reinterpret_cast<const float*>(weaponLane + 0x0E0) != 0.0f ||
                *reinterpret_cast<const float*>(weaponLane + 0x100) != 0.0f)
            {
                vidRuntimeFlags |= 0x04u;
            }

            if (*reinterpret_cast<const std::int32_t*>(weaponLane + 0x120) != 0 ||
                *reinterpret_cast<const std::int32_t*>(weaponLane + 0x140) != 0 ||
                *reinterpret_cast<const std::int32_t*>(weaponLane + 0x160) != 0)
            {
                vidRuntimeFlags |= 0x08u;
            }

            weaponLane += 4;
        } while (--weaponLaneCount != 0);

        if (lifeTime != 999999 ||
            (property & 0x00200400u) != 0u ||
            (vidRuntimeFlags & 0x0Fu) != 0u)
        {
            actionAuxStateRequiredValue = 1;
        }

        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int sourceChild = nChildVid[i];
            if (!sourceChild)
                continue;

            const int childIndex = sourceChild < 0 ? -sourceChild : sourceChild;
            VID* tableVid = nullptr;
            if (childIndex >= 0)
            {
                const int vidCount =
                    *reinterpret_cast<const int*>(
                        appOwner + core::retail_application_layout::VidCount);
                if (childIndex < vidCount)
                {
                    VID* const candidate =
                        *reinterpret_cast<VID* const*>(
                            appOwner + core::retail_application_layout::VidTable +
                            static_cast<std::size_t>(childIndex) * sizeof(VID*));
                    if (candidate)
                        tableVid = candidate;
                }
            }

            if (tableVid)
            {
                childVid[i] = tableVid->exchangedVid;
                if ((childVid[i]->property & P_BIRTHASSMOKE) != 0u)
                    actionAuxStateRequiredValue = 1;
            }
            else
            {
                ReportResourceError(4, "child", sourceChild);
            }
        }
#else
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();

        const int linkedVidId = linkedNvid();
        if (linkedVidId)
        {
            VID* tableVid = nullptr;
            if (linkedVidId >= 0 && linkedVidId < table.count())
                tableVid = table.slot(linkedVidId);
            if (tableVid)
                setLinkedVid(tableVid->exchangedVidRef());
            else
                ReportResourceError(4, "LinkVid", linkedVidId);
        }

        if (weaponRecord())
        {
            for (int lane = 0; lane < 8; ++lane)
            {
                const int laneOffset = lane * static_cast<int>(sizeof(float));
                if (weaponIntAt(0x064 + laneOffset) != 0 ||
                    weaponIntAt(0x084 + laneOffset) != 0 ||
                    weaponIntAt(0x0A4 + laneOffset) != 0 ||
                    weaponIntAt(0x0C4 + laneOffset) != 0)
                    vidRuntimeFlags |= 0x01u;

                if (weaponFloatAt(0x0E4 + laneOffset) != 1.0f ||
                    weaponFloatAt(0x104 + laneOffset) != 1.0f ||
                    weaponFloatAt(0x124 + laneOffset) != 1.0f)
                    vidRuntimeFlags |= 0x02u;

                if (weaponFloatAt(0x144 + laneOffset) != 0.0f ||
                    weaponFloatAt(0x164 + laneOffset) != 0.0f ||
                    weaponFloatAt(0x184 + laneOffset) != 0.0f)
                    vidRuntimeFlags |= 0x04u;

                if (weaponIntAt(0x1A4 + laneOffset) != 0 ||
                    weaponIntAt(0x1C4 + laneOffset) != 0 ||
                    weaponIntAt(0x1E4 + laneOffset) != 0)
                    vidRuntimeFlags |= 0x08u;
            }
        }

        if (lifetimeValue() != 999999 ||
            (properties() & 0x00200400u) != 0u ||
            (vidRuntimeFlags & 0x0Fu) != 0u)
        {
            setActionAuxStateRequired(1);
        }

        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int sourceChild = static_cast<int>(nChildVid[i]);
            if (!sourceChild)
                continue;

            const std::uint32_t sourceRaw = static_cast<std::uint32_t>(sourceChild);
            const std::uint32_t signMask = 0u - (sourceRaw >> 31u);
            const int childIndex = static_cast<std::int32_t>((sourceRaw ^ signMask) - signMask);
            VID* tableVid = nullptr;
            if (childIndex >= 0 && childIndex < table.count())
                tableVid = table.slot(childIndex);
            if (tableVid)
            {
                childVid[i] = tableVid->exchangedVidRef();
                if ((childVid[i]->properties() & P_BIRTHASSMOKE) != 0u)
                    setActionAuxStateRequired(1);
            }
            else
            {
                ReportResourceError(4, "child", sourceChild);
            }
        }
#endif
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    void VID::clearDecodedVidData()
    {
        hostPaletteStorage().clear();
        hostDataFramesStorage().clear();
        hostSurfacePagesStorage().clear();
        hostSurfaceRecordsStorage().clear();
        hostLightFramesStorage().clear();
        hostFrameSurfacesStorage().clear();
#if !defined(_MSC_VER) || !defined(_M_IX86)
        hostDecodeWarningsStorage().clear();
        hostCompressedSurfPresentStorage() = false;
        hostCompressedSurfBytesStorage() = 0;
#endif
    }

    void VID::addDecodeWarning(const std::string& warning)
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        (void)warning;
#else
        hostDecodeWarningsStorage().push_back(warning);
#endif
    }

    bool VID::dataPayloadHasZWords() const
    {
        return type == 0x001D || type == 0x0036;
    }

    bool VID::dataPayloadUsesPaletteIndexes() const
    {
        return type == 0x0019 || type == 0x001B || type == 0x001D;
    }

    bool VID::dataPayloadUsesRgb565Words() const
    {
        return type == 0x0011;
    }

    bool VID::dataPayloadUsesRgb444ZWords() const
    {
        return type == 0x0036;
    }

    bool VID::dataPayloadIsLightColorTable() const
    {
        return type == 0x00B0;
    }

    bool VID::dataPayloadIsSurfaceRecordTable() const
    {
        return isSurfType(type);
    }

    void VID::decodePaletteSection(RESOURCE* globalRes)
    {
        if (!hasPalette())
            return;

        const auto subs = loadSubresources(globalRes, RESOURCE::ResTypes::PALETTE);
        if (subs.empty())
            return;

        const auto& pal = subs.front();
        hostPaletteStorage().reserve(256);

        if ((type & VID_TYPE_NEWVERSION) != 0)
        {
            if (pal.size() < 256u * 4u)
            {
                addDecodeWarning("PAL section is smaller than 256 DWORD entries");
                return;
            }

            for (int i = 0; i < 256; ++i)
            {
                PaletteEntry e;
                e.b = pal[size_t(i) * 4u + 0u];
                e.g = pal[size_t(i) * 4u + 1u];
                e.r = pal[size_t(i) * 4u + 2u];
                e.a = pal[size_t(i) * 4u + 3u];
                hostPaletteStorage().push_back(e);
            }
            return;
        }

        if (pal.size() < 256u * 3u)
        {
            addDecodeWarning("PAL section is smaller than 256 RGB entries");
            return;
        }

        for (int i = 0; i < 256; ++i)
        {
            PaletteEntry e;
            // loadCompactSoftwareVidData packs the three source bytes as 0xFFRRGGBB.
            e.r = pal[size_t(i) * 3u + 0u];
            e.g = pal[size_t(i) * 3u + 1u];
            e.b = pal[size_t(i) * 3u + 2u];
            e.a = 0xFFu;
            hostPaletteStorage().push_back(e);
        }
    }

    void VID::decodeSurfaceSection(RESOURCE* globalRes)
    {
        if (!dataPayloadIsSurfaceRecordTable())
            return;

        const auto subs = loadSubresources(globalRes, RESOURCE::ResTypes::SURFACE);
        if (subs.empty())
            return;

        int pageIndex = 0;
        As1SurfCompressionContext surfCompressionContext;
        const bool compressedSurf = isCompressedSurfType(type) || globalRes->CurrentPackedDiff() != 0;

        for (const auto& surfPayload : subs)
        {
            if (surfPayload.size() < 2u)
            {
                addDecodeWarning("SURF payload is too small for surface count");
                continue;
            }
            const int declaredSurfaceCount = rd16(surfPayload, 0);
            size_t pos = 2u;
            int parsedInSubresource = 0;
            while (parsedInSubresource < declaredSurfaceCount && pos + 8 <= surfPayload.size())
            {
                SurfacePage page;
                page.surfaceIndex = pageIndex++;
                try
                {
                    page.width = rd16(surfPayload, pos + 0);
                    page.height = rd16(surfPayload, pos + 2);
                    page.rawBytes = rd32(surfPayload, pos + 4);
                    pos += 8;
                    ++parsedInSubresource;
                    if (page.rawBytes == 0 || page.rawBytes % 2u != 0)
                    {
                        page.error = "SURF page has invalid raw byte count";
                        hostSurfacePagesStorage().push_back(std::move(page));
                        addDecodeWarning("SURF page has invalid raw byte count");
                        break;
                    }

                    std::vector<BYTE> rawPixels;
                    if (compressedSurf)
                    {
#if !defined(_MSC_VER) || !defined(_M_IX86)
                        hostCompressedSurfPresentStorage() = true;
#endif
                        const size_t packedBegin = pos;
                        size_t consumed = 0;
                        if (!decodeMakeVidCompressedBytes(surfPayload, pos, page.rawBytes, true, surfCompressionContext, rawPixels, consumed, page.error))
                        {
                            hostSurfacePagesStorage().push_back(std::move(page));
                            addDecodeWarning("Compressed SURF decode error");
                            break;
                        }
                        pos += consumed;
#if !defined(_MSC_VER) || !defined(_M_IX86)
                        hostCompressedSurfBytesStorage() += consumed;
#endif
                        page.compressed = true;
                        if (rawPixels.size() != page.rawBytes)
                        {
                            std::ostringstream oss;
                            oss << "Compressed SURF decoded " << rawPixels.size() << " byte(s), expected " << page.rawBytes;
                            page.error = oss.str();
                            addDecodeWarning(page.error);
                        }
                        (void)packedBegin;
                    }
                    else
                    {
                        if (pos + page.rawBytes > surfPayload.size())
                        {
                            page.error = "SURF page has invalid raw byte count";
                            hostSurfacePagesStorage().push_back(std::move(page));
                            addDecodeWarning("SURF page has invalid raw byte count");
                            break;
                        }
                        rawPixels.assign(surfPayload.begin() + static_cast<std::ptrdiff_t>(pos), surfPayload.begin() + static_cast<std::ptrdiff_t>(pos + page.rawBytes));
                        pos += page.rawBytes;
                    }

                    const size_t wordCount = rawPixels.size() / 2u;
                    page.pixels16.reserve(wordCount);
                    for (size_t i = 0; i < wordCount; ++i)
                        page.pixels16.push_back(static_cast<WORD>(rawPixels[i * 2u] | (rawPixels[i * 2u + 1u] << 8)));
                    hostSurfacePagesStorage().push_back(std::move(page));
                }
                catch (const std::exception& e)
                {
                    page.error = e.what();
                    hostSurfacePagesStorage().push_back(std::move(page));
                    addDecodeWarning(std::string("SURF decode error: ") + e.what());
                    break;
                }
            }
            if (pos != surfPayload.size())
            {
                std::ostringstream oss;
                oss << "SURF payload has " << (surfPayload.size() - pos) << " trailing byte(s)";
                addDecodeWarning(oss.str());
            }
        }
    }

    void VID::decodeDataSection(RESOURCE* globalRes)
    {
        const auto subs = loadSubresources(globalRes, RESOURCE::ResTypes::DATA);
        if (subs.empty())
            return;

        if (dataPayloadIsLightColorTable())
        {
            for (const auto& sub : subs)
            {
                if (sub.size() % 4u != 0)
                    addDecodeWarning("Light DATA size is not divisible by 4");
                const size_t count = sub.size() / 4u;
                for (size_t i = 0; i < count; ++i)
                {
                    LightFrame lf;
                    lf.frameIndex = static_cast<int>(hostLightFramesStorage().size());
                    lf.b = sub[i * 4u + 0u];
                    lf.g = sub[i * 4u + 1u];
                    lf.r = sub[i * 4u + 2u];
                    lf.a = sub[i * 4u + 3u];
                    hostLightFramesStorage().push_back(lf);
                }
            }
            return;
        }

        if (dataPayloadIsSurfaceRecordTable())
        {
            int recIndex = 0;
            for (const auto& sub : subs)
            {

                const bool use36 = (sub.size() % 36u) == 0u &&
                                   (noCadr <= 0 || static_cast<size_t>(noCadr) == sub.size() / 36u || (sub.size() % 20u) != 0u);
                const bool use20 = !use36 && (sub.size() % 20u) == 0u;
                if (!use36 && !use20)
                {
                    std::ostringstream oss;
                    oss << "SURF DATA table is neither 36-byte nor 20-byte aligned: " << sub.size() << " byte(s)";
                    addDecodeWarning(oss.str());
                }

                if (use36)
                {
                    const size_t count = sub.size() / 36u;
                    for (size_t i = 0; i < count; ++i)
                    {
                        const size_t off = i * 36u;
                        SurfaceRecord rec;
                        rec.recordIndex = recIndex++;
                        rec.marker = rd32(sub, off + 0u);
                        rec.surface = rdi32(sub, off + 4u);
                        rec.srcX = rdi32(sub, off + 8u);
                        rec.srcY = rdi32(sub, off + 12u);
                        rec.width = rdi32(sub, off + 16u);
                        rec.height = rdi32(sub, off + 20u);
                        rec.dstX = rdi32(sub, off + 24u);
                        rec.dstY = rdi32(sub, off + 28u);
                        const int next = rdi32(sub, off + 32u);
                        rec.nextRecord = next > 0 ? next : -1;
                        hostSurfaceRecordsStorage().push_back(rec);
                    }
                }
                else if (use20)
                {
                    const size_t count = sub.size() / 20u;
                    for (size_t i = 0; i < count; ++i)
                    {
                        const size_t off = i * 20u;
                        SurfaceRecord rec;
                        rec.recordIndex = recIndex++;
                        rec.marker = rd32(sub, off + 0u);
                        auto rdS16 = [&](size_t rel) -> int
                        {
                            std::int16_t v = 0;
                            std::memcpy(&v, sub.data() + off + rel, sizeof(v));
                            return static_cast<int>(v);
                        };
                        rec.surface = rdS16(4u);
                        rec.srcX = rdS16(6u);
                        rec.srcY = rdS16(8u);
                        rec.width = rdS16(10u);
                        rec.height = rdS16(12u);
                        rec.dstX = rdS16(14u);
                        rec.dstY = rdS16(16u);
                        const int next = rdS16(18u);
                        rec.nextRecord = next > 0 ? next : -1;
                        hostSurfaceRecordsStorage().push_back(rec);
                    }
                }
            }
            return;
        }

        int frameIndex = 0;
        for (const auto& payload : subs)
        {
            DataFrame frame;
            frame.frameIndex = frameIndex++;
            try
            {
                if (payload.size() < 6u)
                    throw std::runtime_error("DATA payload is too small");
                frame.declaredRawSize = rd32(payload, 0);

                std::vector<BYTE> rawPayload;
                const size_t streamBegin = 4u;
                const size_t streamSize = payload.size() - streamBegin;
                if (frame.declaredRawSize == streamSize)
                {
                    rawPayload.assign(payload.begin() + static_cast<std::ptrdiff_t>(streamBegin), payload.end());
                }
                else
                {

                    std::string decodeError;
                    size_t consumed = 0;
                    As1SurfCompressionContext contexts{};
                    if (!decodeMakeVidCompressedBytes(payload, streamBegin, frame.declaredRawSize, false, contexts, rawPayload, consumed, decodeError))
                    {
                        decodeError.clear();
                        consumed = 0;
                        contexts = As1SurfCompressionContext{};
                        if (!decodeMakeVidCompressedBytes(payload, streamBegin, frame.declaredRawSize, true, contexts, rawPayload, consumed, decodeError))
                            throw std::runtime_error(std::string("DATA packed decode failed: ") + decodeError);
                    }
                    (void)consumed;
                }

                if (rawPayload.size() != frame.declaredRawSize)
                    throw std::runtime_error("DATA decoded raw size mismatch");

                size_t pos = 0u;
                const size_t rawEnd = rawPayload.size();
                bool aliasedFrame = false;
                if (frame.declaredRawSize == 2u)
                {

                    frame.marker = rd16(rawPayload, pos);
                    pos += 2u;
                    if (static_cast<size_t>(frame.marker) >= hostDataFramesStorage().size())
                        throw std::runtime_error("DATA frame alias references an unavailable frame");
                    const DataFrame& sourceFrame = hostDataFramesStorage()[static_cast<size_t>(frame.marker)];
                    frame.top = sourceFrame.top;
                    frame.rowCount = sourceFrame.rowCount;
                    frame.runCount = sourceFrame.runCount;
                    frame.visiblePixels = sourceFrame.visiblePixels;
                    frame.runs = sourceFrame.runs;
                    frame.malformed = sourceFrame.malformed;
                    frame.error = sourceFrame.error;
                    aliasedFrame = true;
                }
                else
                {
                    if (frame.declaredRawSize < 6u)
                        throw std::runtime_error("DATA raw stream is too small for row header");
                    frame.marker = rd16(rawPayload, pos); pos += 2u;
                    frame.top = rd16(rawPayload, pos); pos += 2u;
                    frame.rowCount = rd16(rawPayload, pos); pos += 2u;
                }

                for (int row = 0; !aliasedFrame && row < frame.rowCount; ++row)
                {
                    int x = 0;
                    for (;;)
                    {
                        if (pos + 2u > rawEnd)
                            throw std::runtime_error("DATA ended inside RLE pair");
                        const int skip = rawPayload[pos++];
                        const int run = rawPayload[pos++];
                        if (skip == 0 && run == 0)
                            break;
                        x += skip;
                        DataRun r;
                        r.row = frame.top + row;
                        r.x = x;
                        r.skip = skip;
                        r.count = run;
                        if (dataPayloadHasZWords())
                        {
                            if (pos + size_t(run) * 2u > rawEnd)
                                throw std::runtime_error("DATA ended inside Z run");
                            r.zWords.reserve(run);
                            for (int i = 0; i < run; ++i)
                            {
                                r.zWords.push_back(rd16(rawPayload, pos));
                                pos += 2u;
                            }
                        }
                        if (dataPayloadUsesPaletteIndexes())
                        {
                            if (pos + size_t(run) > rawEnd)
                                throw std::runtime_error("DATA ended inside palette-index run");
                            r.paletteIndexes.insert(r.paletteIndexes.end(), rawPayload.begin() + static_cast<std::ptrdiff_t>(pos), rawPayload.begin() + static_cast<std::ptrdiff_t>(pos + run));
                            pos += size_t(run);
                        }
                        else if (dataPayloadUsesRgb565Words() || dataPayloadUsesRgb444ZWords())
                        {
                            if (pos + size_t(run) * 2u > rawEnd)
                                throw std::runtime_error("DATA ended inside WORD color run");
                            r.colorWords.reserve(run);
                            for (int i = 0; i < run; ++i)
                            {
                                r.colorWords.push_back(rd16(rawPayload, pos));
                                pos += 2u;
                            }
                        }
                        else
                        {
                            throw std::runtime_error("unsupported DATA payload layout for this VID type");
                        }
                        x += run;
                        frame.visiblePixels += run;
                        ++frame.runCount;
                        frame.runs.push_back(std::move(r));
                    }
                }
                if (pos != rawEnd)
                {
                    std::ostringstream oss;
                    oss << "DATA frame has " << (rawEnd - pos) << " trailing raw byte(s)";
                    frame.malformed = true;
                    frame.error = oss.str();
                    addDecodeWarning(oss.str());
                }
            }
            catch (const std::exception& e)
            {
                frame.malformed = true;
                frame.error = e.what();
                addDecodeWarning(std::string("DATA decode error: ") + e.what());
            }
            hostDataFramesStorage().push_back(std::move(frame));
        }
    }


    DWORD VID::paletteIndexToBGRA(BYTE index) const
    {
        if (palette().empty())
        {
            const DWORD v = static_cast<DWORD>(index);
            return 0xFF000000u | (v << 16u) | (v << 8u) | v;
        }
        const PaletteEntry& e = palette()[static_cast<size_t>(index) % palette().size()];
        // loadCompactSoftwareVidData preserves the fourth byte for NEWVERSION+ALPHA palettes.
        // Alpha zero is therefore real transparency, not a missing-alpha
        // sentinel. Non-alpha palette families use an opaque synthesized byte.
        const BYTE alpha = (type & VID_TYPE_ALPHA) != 0 ? e.a : 0xFFu;
        return (static_cast<DWORD>(alpha) << 24u) |
               (static_cast<DWORD>(e.r) << 16u) |
               (static_cast<DWORD>(e.g) << 8u) |
               static_cast<DWORD>(e.b);
    }

    DWORD VID::rgb565ToBGRA(WORD value) const
    {
        const DWORD r5 = (value >> 11u) & 0x1Fu;
        const DWORD g6 = (value >> 5u) & 0x3Fu;
        const DWORD b5 = value & 0x1Fu;
        const DWORD r = (r5 << 3u) | (r5 >> 2u);
        const DWORD g = (g6 << 2u) | (g6 >> 4u);
        const DWORD b = (b5 << 3u) | (b5 >> 2u);
        return 0xFF000000u | (r << 16u) | (g << 8u) | b;
    }

    DWORD VID::a4r4g4b4ToBGRA(WORD value) const
    {
        const DWORD a4 = (value >> 12u) & 0x0Fu;
        const DWORD r4 = (value >> 8u) & 0x0Fu;
        const DWORD g4 = (value >> 4u) & 0x0Fu;
        const DWORD b4 = value & 0x0Fu;
        const DWORD a = (a4 << 4u) | a4;
        const DWORD r = (r4 << 4u) | r4;
        const DWORD g = (g4 << 4u) | g4;
        const DWORD b = (b4 << 4u) | b4;
        return (a << 24u) | (r << 16u) | (g << 8u) | b;
    }

    DWORD VID::rgb444ToBGRA(WORD value) const
    {
        const DWORD r4 = (value >> 8u) & 0x0Fu;
        const DWORD g4 = (value >> 4u) & 0x0Fu;
        const DWORD b4 = value & 0x0Fu;
        const DWORD r = (r4 << 4u) | r4;
        const DWORD g = (g4 << 4u) | g4;
        const DWORD b = (b4 << 4u) | b4;
        return 0xFF000000u | (r << 16u) | (g << 8u) | b;
    }

    DWORD VID::rgb444AlphaToBGRA(WORD value) const
    {
        const DWORD a4 = (value >> 12u) & 0x0Fu;
        const DWORD r4 = (value >> 8u) & 0x0Fu;
        const DWORD g4 = (value >> 4u) & 0x0Fu;
        const DWORD b4 = value & 0x0Fu;
        const DWORD a = (a4 != 0u) ? ((a4 << 4u) | a4) : ((value & 0x0FFFu) ? 0xFFu : 0x00u);
        const DWORD r = (r4 << 4u) | r4;
        const DWORD g = (g4 << 4u) | g4;
        const DWORD b = (b4 << 4u) | b4;
        return (a << 24u) | (r << 16u) | (g << 8u) | b;
    }

    void VID::finalizeFrameSurface(FrameSurface& surface)
    {
        std::uint64_t h = 1469598103934665603ull;
        h = fnv1aAppendValue(h, surface.frameIndex);
        h = fnv1aAppendValue(h, surface.width);
        h = fnv1aAppendValue(h, surface.height);
        h = fnv1aAppendValue(h, surface.originX);
        h = fnv1aAppendValue(h, surface.originY);
        const int kind = static_cast<int>(surface.kind);
        const int format = static_cast<int>(surface.pixelFormat);
        h = fnv1aAppendValue(h, kind);
        h = fnv1aAppendValue(h, format);
        h = fnv1aAppendValue(h, surface.visiblePixels);
        if (!surface.bgra32.empty())
            h = fnv1aAppend(h, surface.bgra32.data(), surface.bgra32.size() * sizeof(surface.bgra32[0]));
        if (!surface.zWords.empty())
            h = fnv1aAppend(h, surface.zWords.data(), surface.zWords.size() * sizeof(surface.zWords[0]));
        surface.contentHash = h ? h : 1ull;
    }

    void VID::buildFrameSurfaces()
    {
        hostFrameSurfacesStorage().clear();

        if (!hostDataFramesStorage().empty())
        {
            for (const auto& frame : hostDataFramesStorage())
                buildSoftwareFrameSurface(frame);
        }

        if (!hostSurfaceRecordsStorage().empty())
        {
            for (const auto& record : hostSurfaceRecordsStorage())
                buildHardwareFrameSurface(record);
        }

        if (!hostLightFramesStorage().empty())
        {
            for (const auto& frame : hostLightFramesStorage())
                buildLightFrameSurface(frame);
        }
    }

    const VID::FrameSurface* VID::FrameSurfaceByRecord(int recordIndex) const
    {
        if (recordIndex < 0)
            return nullptr;
        for (const FrameSurface& surface : frameSurfaces())
        {
            if (surface.kind == FrameSurfaceKind::HardwareSurfRecord &&
                surface.sourceRecord == recordIndex &&
                !surface.malformed && !surface.skipped &&
                surface.width > 0 && surface.height > 0)
                return &surface;
        }
        return nullptr;
    }

    void VID::collectHardwareSurfaceChain(const FrameSurface* first, std::vector<const FrameSurface*>& out) const
    {
        out.clear();
        if (!first)
            return;

        const FrameSurface* current = first;
        int guard = 0;
        int visited[128] = {};
        int visitedCount = 0;
        while (current && guard++ < 128)
        {
            out.push_back(current);
            if (current->kind != FrameSurfaceKind::HardwareSurfRecord || current->nextRecord < 0)
                break;

            bool loop = false;
            for (int i = 0; i < visitedCount; ++i)
            {
                if (visited[i] == current->sourceRecord)
                {
                    loop = true;
                    break;
                }
            }
            if (loop)
                break;
            if (visitedCount < static_cast<int>((sizeof(visited) / sizeof(visited[0]))))
                visited[visitedCount++] = current->sourceRecord;

            current = FrameSurfaceByRecord(current->nextRecord);
        }
    }

    void VID::buildSoftwareFrameSurface(const DataFrame& frame)
    {
        FrameSurface surface;
        surface.frameIndex = frame.frameIndex;
        surface.kind = FrameSurfaceKind::SoftwareData;
        surface.width = std::max(1, static_cast<int>(vidWidth()));
        surface.height = std::max(1, static_cast<int>(vidHeight()));
        surface.originX = 0;

        surface.originY = 0;
        surface.visiblePixels = frame.visiblePixels;
        surface.malformed = frame.malformed;
        surface.error = frame.error;

        if (dataPayloadUsesRgb565Words())
            surface.pixelFormat = FramePixelFormat::R5G6B5;
        else if (dataPayloadUsesRgb444ZWords())
            surface.pixelFormat = FramePixelFormat::A4R4G4B4;
        else
            surface.pixelFormat = FramePixelFormat::BGRA8888;

        if (surface.width <= 0 || surface.height <= 0)
        {
            surface.malformed = true;
            surface.error = surface.error.empty() ? "frame surface has invalid dimensions" : surface.error;
            finalizeFrameSurface(surface);
            hostFrameSurfacesStorage().push_back(std::move(surface));
            return;
        }

        const size_t pixelCount = static_cast<size_t>(surface.width) * static_cast<size_t>(surface.height);
        surface.bgra32.assign(pixelCount, 0);
        if (dataPayloadUsesPaletteIndexes())
            surface.paletteIndexes.assign(pixelCount, 0);
        surface.zWords.assign(pixelCount, 0);

        for (const auto& run : frame.runs)
        {
            if (run.row < 0 || run.row >= surface.height || run.x < 0 || run.x + run.count > surface.width)
            {
                surface.malformed = true;
                if (surface.error.empty())
                    surface.error = "DATA run falls outside frame surface";
                continue;
            }
            const size_t base = static_cast<size_t>(run.row) * static_cast<size_t>(surface.width) + static_cast<size_t>(run.x);
            for (int i = 0; i < run.count; ++i)
            {
                const size_t dst = base + static_cast<size_t>(i);
                if (i < static_cast<int>(run.zWords.size()))
                    surface.zWords[dst] = run.zWords[static_cast<size_t>(i)];
                if (dataPayloadUsesPaletteIndexes())
                {
                    if (i < static_cast<int>(run.paletteIndexes.size()))
                    {
                        const BYTE paletteIndex = run.paletteIndexes[static_cast<size_t>(i)];
                        surface.paletteIndexes[dst] = paletteIndex;
                        surface.bgra32[dst] = paletteIndexToBGRA(paletteIndex);
                    }
                }
                else if (dataPayloadUsesRgb565Words())
                {
                    if (i < static_cast<int>(run.colorWords.size()))
                    {
                        const WORD w = run.colorWords[static_cast<size_t>(i)];
                        surface.pixels16.push_back(w);
                        surface.bgra32[dst] = rgb565ToBGRA(w);
                    }
                }
                else if (dataPayloadUsesRgb444ZWords())
                {
                    if (i < static_cast<int>(run.colorWords.size()))
                    {
                        const WORD w = run.colorWords[static_cast<size_t>(i)];
                        surface.pixels16.push_back(w);
                        surface.bgra32[dst] = rgb444AlphaToBGRA(w);
                    }
                }
            }
        }

        const bool anyZ = std::any_of(surface.zWords.begin(), surface.zWords.end(), [](WORD z) { return z != 0; });
        if (!anyZ)
            surface.zWords.clear();
        finalizeFrameSurface(surface);
        hostFrameSurfacesStorage().push_back(std::move(surface));
    }

    void VID::buildHardwareFrameSurface(const SurfaceRecord& record)
    {
        FrameSurface surface;
        surface.frameIndex = record.recordIndex;
        surface.kind = FrameSurfaceKind::HardwareSurfRecord;
        surface.pixelFormat = (type == 0x0033 || type == 0x0133) ? FramePixelFormat::A4R4G4B4 : FramePixelFormat::R5G6B5;
        surface.width = record.width;
        surface.height = record.height;
        surface.originX = record.dstX;
        surface.originY = record.dstY;
        surface.sourceSurface = record.surface;
        surface.sourceRecord = record.recordIndex;
        surface.nextRecord = record.nextRecord;

        if (surface.width <= 0 || surface.height <= 0)
        {
            // Some AS1 hardware/SURF DATA tables contain empty frame/control records.
            // Keep the record visible to inspection, but do not treat it as a failed decoded frame.
            surface.skipped = true;
            surface.error = "empty SURF DATA record";
            finalizeFrameSurface(surface);
            hostFrameSurfacesStorage().push_back(std::move(surface));
            return;
        }
        if (record.surface < 0 || static_cast<size_t>(record.surface) >= hostSurfacePagesStorage().size())
        {
            surface.skipped = true;
            surface.error = "SURF DATA record references a surface page not decoded in this stage";
            finalizeFrameSurface(surface);
            hostFrameSurfacesStorage().push_back(std::move(surface));
            return;
        }

        const SurfacePage& page = hostSurfacePagesStorage()[static_cast<size_t>(record.surface)];
        if (!page.error.empty())
        {
            surface.skipped = true;
            surface.error = page.error;
            finalizeFrameSurface(surface);
            hostFrameSurfacesStorage().push_back(std::move(surface));
            return;
        }
        if (record.srcX < 0 || record.srcY < 0 || record.srcX + record.width > page.width || record.srcY + record.height > page.height)
        {
            surface.skipped = true;
            surface.error = "SURF DATA record rectangle falls outside surface page";
            finalizeFrameSurface(surface);
            hostFrameSurfacesStorage().push_back(std::move(surface));
            return;
        }

        surface.pixels16.reserve(static_cast<size_t>(record.width) * static_cast<size_t>(record.height));
        surface.bgra32.reserve(static_cast<size_t>(record.width) * static_cast<size_t>(record.height));
        for (int y = 0; y < record.height; ++y)
        {
            const size_t srcRow = static_cast<size_t>(record.srcY + y) * static_cast<size_t>(page.width);
            for (int x = 0; x < record.width; ++x)
            {
                const size_t src = srcRow + static_cast<size_t>(record.srcX + x);
                const WORD w = src < page.pixels16.size() ? page.pixels16[src] : 0;
                surface.pixels16.push_back(w);
                surface.bgra32.push_back(surface.pixelFormat == FramePixelFormat::A4R4G4B4 ? a4r4g4b4ToBGRA(w) : rgb565ToBGRA(w));
            }
        }
        surface.visiblePixels = static_cast<int>(surface.pixels16.size());
        finalizeFrameSurface(surface);
        hostFrameSurfacesStorage().push_back(std::move(surface));
    }

    void VID::buildLightFrameSurface(const LightFrame& frame)
    {
        FrameSurface surface;
        surface.frameIndex = frame.frameIndex;
        surface.kind = FrameSurfaceKind::LightColor;
        surface.pixelFormat = FramePixelFormat::LightBGRA;
        surface.width = std::max(1, static_cast<int>(vidWidth()));
        surface.height = std::max(1, static_cast<int>(vidHeight()));
        surface.visiblePixels = surface.width * surface.height;
        const BYTE alpha = frame.a ? frame.a : 0xFFu;
        surface.bgra32.assign(static_cast<size_t>(surface.width) * static_cast<size_t>(surface.height), 0u);
        const float cx = (static_cast<float>(surface.width) - 1.0f) * 0.5f;
        const float cy = (static_cast<float>(surface.height) - 1.0f) * 0.5f;
        const float radius = std::max(1.0f, std::min(cx > 0.0f ? cx : 1.0f, cy > 0.0f ? cy : 1.0f));
        for (int y = 0; y < surface.height; ++y)
        {
            for (int x = 0; x < surface.width; ++x)
            {
                const float dx = (static_cast<float>(x) - cx) / radius;
                const float dy = (static_cast<float>(y) - cy) / radius;
                const float d2 = dx * dx + dy * dy;
                if (d2 >= 1.0f)
                    continue;
                const float falloff = (1.0f - d2) * (1.0f - d2);
                const BYTE a = static_cast<BYTE>(std::max(0.0f, std::min(255.0f, static_cast<float>(alpha) * falloff)));
                surface.bgra32[static_cast<size_t>(y) * static_cast<size_t>(surface.width) + static_cast<size_t>(x)] =
                    (static_cast<DWORD>(a) << 24u) |
                    (static_cast<DWORD>(frame.r) << 16u) |
                    (static_cast<DWORD>(frame.g) << 8u) |
                    static_cast<DWORD>(frame.b);
            }
        }
        finalizeFrameSurface(surface);
        hostFrameSurfacesStorage().push_back(std::move(surface));
    }


#endif

    void VID::CalcImportantForSync()
    {
        halfSizeXY = { sizeXYZ.x * 0.5f, sizeXYZ.y * 0.5f };
    }
    void VID::SetLayer()
    {

        layer = 0;
    }

    void VID::Draw(const SPRITE* sprite)
    {

        (void)sprite;
    }

    int VID::DrawShadow(const SPRITE* sprite) const
    {
        (void)sprite;

        return 0;
    }

    int VID::HaveShadow() const
    {

        return 0;
    }

    bool VID::transparencyCheck() const
    {
        if (hasAlpha() || isLight())
            return true;
#if !defined(_MSC_VER) || !defined(_M_IX86)
        for (const FrameSurface& surface : frameSurfaces())
        {
            if (surface.pixelFormat == FramePixelFormat::A4R4G4B4 ||
                surface.pixelFormat == FramePixelFormat::LightBGRA ||
                surface.kind == FrameSurfaceKind::LightColor)
                return true;
            for (DWORD px : surface.bgra32)
            {
                if ((px >> 24u) != 0xFFu)
                    return true;
            }
        }
#endif
        return false;
    }

    int VID::PaletteSize() const
    {
#if !defined(_MSC_VER) || !defined(_M_IX86)
        return static_cast<int>(palette().size());
#else
        // Base VID does not own a physical palette buffer.  Palette-bearing
        // concrete classes override/use their physical storage directly.
        return 0;
#endif
    }

    bool VID::isLoaded() const
    {
#if !defined(_MSC_VER) || !defined(_M_IX86)
        return hasFrameSurfaces();
#else
        return false;
#endif
    }

    bool VID::unloadable() const
    {
        return isLoaded();
    }

    int VID::RealDirection(const ANGLE& dir) const { return noDir > 0 ? (((dir.Int() + directionQuantizationOffset()) & 255) * noDir) / 256 : 0; }
    ANGLE VID::SteppedDirection(const ANGLE& dir) const
    {
        if (noDir <= 0)
            return ANGLE(dir.Int() & 0xFF);
        const int real = RealDirection(dir);
        return ANGLE((real * 256) / static_cast<int>(noDir));
    }
    STRING VID::GetNumberName() const { return STRING(std::to_string(nVid)); }
    void VID::SetGamma(const Gamma& g, unsigned n_gamma)
    {

        GammaRawPair raw{};
        raw.setSignedDeltasRetail(g.alpha, g.red, g.green, g.blue);
        if (n_gamma >= 4)
        {
            SetGammaRaw(raw, n_gamma);
            return;
        }

        SetGammaRaw(raw, n_gamma);
    }


    void VID::SetGammaRaw(const GammaRawPair& rawGamma, unsigned n_gamma)
    {

        if (n_gamma >= 4)
        {
            if (n_gamma != 4)
                ReportResourceError(4, "n_gamma in VID::SetGamma", static_cast<int>(n_gamma));
            return;
        }

        altGammaRaw[n_gamma] = rawGamma;
    }

    void VID::SetGammaToPalette(BYTE* palette, const Gamma& g)
    {
        GammaRawPair raw{};
        raw.setSignedDeltasRetail(g.alpha, g.red, g.green, g.blue);
        SetGammaToPaletteRaw(palette, raw);
    }

    void VID::SetGammaToPaletteRaw(BYTE* palette, const GammaRawPair& rawGamma)
    {

        if (!palette || (rawGamma.first == 0 && rawGamma.second == 0))
            return;
        DWORD* entries = reinterpret_cast<DWORD*>(palette);
        for (int i = 0; i < 256; ++i)
            entries[i] = GammaRawBlend(rawGamma, entries[i]);
    }

    void VID::SetGridZ(const SPRITE* sprite)
    {
        if (!sprite || sprite == Mouse || noGridZ <= 0 || !gridZ)
            return;

        int begin = 0;
        int end = noGridZ;
        if (gridCadrShift)
        {
            const int frame = sprite->currentFrame();
            const int frameCount = static_cast<int>(static_cast<std::uint16_t>(noCadr));
            begin = (frame >= 0 && frame < frameCount) ? gridCadrShift[frame] : 0;
            end = (frame >= 0 && frame < frameCount - 1) ? gridCadrShift[frame + 1] : noGridZ;
        }

        MAP* const map = sprite->mapOwner();
        if (!map)
            return;
        const bool permanent = (core::ApplicationFlags() & 1u) == 0u && spriteClass == 8;
        for (int i = begin; i < end; ++i)
        {
            const VECTOR& dot = gridZ[i];
            const float x = sprite->X() + dot.x;
            const float y = sprite->Y() + dot.y;
            const float z = sprite->Z() + dot.z;
            if (permanent)
                map->setTerrainHeightAtWorldPosition(x, y, z);
            else
                map->SetTempGroundZ(x, y, z);
        }
    }

    int VID_SOFTWARE::updateGroundZFromCompactFrame(const SPRITE* sprite) noexcept
    {

        const int spriteZ = vidConvertFloatToInt32(sprite->Z());
        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_ZBUFFER) == 0)
            return spriteZ;

        short localGrid[65536];
        std::memset(localGrid, 0, sizeof(localGrid));

        const int frameIndex = sprite->currentFrame();
        const DWORD frameOffset = frameOffsets()[frameIndex];
        BYTE* frame = frameStorage() + frameOffset;
        const int frameHeaderCount = static_cast<int>(*reinterpret_cast<const short*>(frame));
        BYTE* rowData = frame + 2 + frameHeaderCount * 6;

        const int left = subtractWrap32(vidConvertFloatToInt32(sprite->X()), vidWidth() / 2);
        const int topWithoutZ = subtractWrap32(vidConvertFloatToInt32(sprite->Y()), vidHeight() / 2);
        const int topProjected = subtractWrap32(topWithoutZ, spriteZ);
        const int zBase = subtractWrap32(spriteZ, 128);

        if ((typeFlags & VID_TYPE_PALETTE) != 0 && (typeFlags & VID_TYPE_TEXTURE) != 0)
        {
            int row = static_cast<int>(*reinterpret_cast<const short*>(rowData));
            const int rowEnd = row + static_cast<int>(*reinterpret_cast<const short*>(rowData + 2));
            rowData += 4;

            while (row < rowEnd)
            {
                int x = 0;
                if (*reinterpret_cast<const WORD*>(rowData) != 0)
                {
                    do
                    {
                        x = addWrap32(x, static_cast<int>(*rowData++));
                        const int count = static_cast<int>(*rowData++);
                        const int runStartX = x;
                        const WORD* zWords = reinterpret_cast<const WORD*>(rowData);

                        for (int i = 0; i < count; ++i, ++x)
                        {
                            const int localZ = addWrap32(zBase, static_cast<int>(zWords[i] >> 3u));
                            const int projectedY = addWrap32(row, localZ);
                            if (projectedY >= 0 && projectedY < 2048 && x >= 0 && x < 2048)
                            {
                                const int cell = (x / 8) + ((projectedY / 8) << 8);
                                if (localZ > static_cast<int>(localGrid[cell]))
                                    localGrid[cell] = static_cast<short>(localZ);
                            }
                        }

                        // DATA run storage is count WORD Z values followed by
                        // count color/palette bytes.  updateGroundZFromCompactFrame advances 3*count
                        // bytes and tests the following WORD terminator.
                        x = addWrap32(runStartX, count);
                        rowData += count * 3;
                    }
                    while (*reinterpret_cast<const WORD*>(rowData) != 0);
                }

                row = addWrap32(row, 1);
                rowData += 2;
            }
        }

        MAP* const map = sprite->mapOwner();
        const int leftGrid = left / 8;
        const int topGrid = topProjected / 8;
        const int bucketWidth = vidWidth() / 8;
        const int bucketHeight = vidHeight() / 8;

        for (int localY = bucketHeight - 1; localY >= 0; --localY)
        {
            const int worldYCell = addWrap32(topGrid, localY);
            const float worldY = static_cast<float>(worldYCell) * 8.0f;
            const short* rowCells = localGrid + localY * 256;
            for (int localX = 0; localX < bucketWidth; ++localX)
            {
                const int worldXCell = addWrap32(leftGrid, localX);
                const float worldX = static_cast<float>(worldXCell) * 8.0f;
                const float gridZ = static_cast<float>(rowCells[localX]);
                const float groundZ = map->GetGroundZ(VECTOR2{worldX, worldY});

                if (x87LessOrUnorderedForVid(groundZ, gridZ))
                    map->setTerrainHeightAtWorldPosition(worldX, worldY, gridZ);
            }
        }


        return bucketHeight > 0 ? 0 : (bucketHeight - 1);
    }

    void VID::ResetGridZ(const SPRITE* sprite)
    {
        if (!sprite || sprite == Mouse || noGridZ <= 0 || !gridZ)
            return;
        if ((core::ApplicationFlags() & 1u) == 0u && spriteClass == 8)
            return;

        int begin = 0;
        int end = noGridZ;
        if (gridCadrShift)
        {
            const int frame = sprite->currentFrame();
            const int frameCount = static_cast<int>(static_cast<std::uint16_t>(noCadr));
            begin = (frame >= 0 && frame < frameCount) ? gridCadrShift[frame] : 0;
            end = (frame >= 0 && frame < frameCount - 1) ? gridCadrShift[frame + 1] : noGridZ;
        }

        MAP* const map = sprite->mapOwner();
        if (!map)
            return;
        for (int i = begin; i < end; ++i)
        {
            const VECTOR& dot = gridZ[i];
            map->ClearTempGroundZ(sprite->X() + dot.x, sprite->Y() + dot.y, sprite->Z() + dot.z);
        }
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    void VID::calcBuildSizeToGridZ()
    {
        if (gridZ)
        {
            delete[] gridZ;
            gridZ = nullptr;
        }
        if (gridCadrShift)
        {
            delete[] gridCadrShift;
            gridCadrShift = nullptr;
        }
        noGridZ = 0;

        std::vector<VECTOR> dots;
        for (int y = 0; static_cast<float>(y) < sizeXYZ.y; y += 8)
        {
            for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
                dots.emplace_back(static_cast<float>(x) - halfSizeXY.x, static_cast<float>(y) - halfSizeXY.y, sizeXYZ.z);
            dots.emplace_back(halfSizeXY.x, static_cast<float>(y) - halfSizeXY.y, sizeXYZ.z);
        }
        for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
            dots.emplace_back(static_cast<float>(x) - halfSizeXY.x, halfSizeXY.y, sizeXYZ.z);
        dots.emplace_back(halfSizeXY.x, halfSizeXY.y, sizeXYZ.z);

        noGridZ = static_cast<int>(dots.size());
        if (noGridZ > 0)
        {
            gridZ = new VECTOR[static_cast<std::size_t>(noGridZ)];
            std::copy(dots.begin(), dots.end(), gridZ);
        }
    }
#endif
}
