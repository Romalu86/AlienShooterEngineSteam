#pragma once

#include "unit.h"

#include <array>
#include <cstdint>

namespace as1
{
    class DEPO : public UNIT
    {
    public:
        DEPO(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~DEPO() override;
        DEPO* depoScalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyDepoState() noexcept;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        int enqueueDepoPurchase(int nvid) noexcept;
        void startNextDepoBuild() noexcept;
        int spawnNextDepoUnit(int actionArgument1, int actionArgument2) noexcept;

        void swapQueueEntries(int firstIndex, int secondIndex) noexcept;
        void cancelQueueEntry(int index) noexcept;
        int queueProgress255(int index) const noexcept;
        int queueEntryFlag(int index) const noexcept;
        void moveQueueEntryUp(int index) noexcept;
        int toggleQueueEntryFlag(int index) noexcept;
        void replaceQueueEntryNvid(int index, int nvid) noexcept;
        void moveQueueEntryDown(int index) noexcept;

        std::uint32_t createdEngineSequence() const noexcept { return m_createdEngineSequence; }
        std::uint32_t activeQueueCursor() const noexcept { return m_activeQueueCursor; }
        std::uint32_t queueCapacity() const noexcept { return m_queueCapacity; }
        std::uint32_t queueCount() const noexcept { return m_queueCount; }
        std::uint16_t queuedNvidAt(int index) const noexcept;
        std::uint32_t queuedBuildTimeAt(int index) const noexcept;
        std::uint32_t queuedCompletionFlagAt(int index) const noexcept;

    private:
        friend struct DepoRetailLayoutProbe;
        std::uint32_t m_createdEngineSequence = 0;                 // +0x94
        std::array<std::uint16_t, 20> m_queuedNvids{};   // +0x98..+0xBF
        std::array<std::uint8_t, 0xA0> m_reservedAfterQueuedNvids;
        std::array<std::uint32_t, 20> m_queuedBuildTimes{}; // +0x160..+0x1AF
        std::array<std::uint8_t, 0x140> m_reservedAfterBuildTimes;
        std::array<std::uint32_t, 20> m_queuedCompletionFlags{}; // +0x2F0..+0x33F
        std::array<std::uint8_t, 0x140> m_reservedAfterCompletionFlags;
        std::uint32_t m_activeQueueCursor = 0;
        std::uint32_t m_queueCapacity = 0;
        std::uint32_t m_queueCount = 0;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct DepoRetailLayoutProbe
    {
        static constexpr std::size_t slot94 = offsetof(DEPO, m_createdEngineSequence);
        static constexpr std::size_t word98 = offsetof(DEPO, m_queuedNvids);
        static constexpr std::size_t dword160 = offsetof(DEPO, m_queuedBuildTimes);
        static constexpr std::size_t dword2F0 = offsetof(DEPO, m_queuedCompletionFlags);
        static constexpr std::size_t slot480 = offsetof(DEPO, m_activeQueueCursor);
        static constexpr std::size_t slot484 = offsetof(DEPO, m_queueCapacity);
        static constexpr std::size_t slot488 = offsetof(DEPO, m_queueCount);
    };
                                                                              
                                                                              
                                                                                  
                                                                                  
                                                                                 
                                                                                 
                                                                                 
                                                                                                
#endif
}
