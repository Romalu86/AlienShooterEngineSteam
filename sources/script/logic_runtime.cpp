#include "logic_runtime.h"
#include "../core/base_stream.h"
#include "../core/log.h"
#include "../core/file_logger.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace as1 { namespace script
{
    namespace
    {
        constexpr int DivideByZeroSentinel = 0x0FFFFFFF;

        template <class Container>
        void ensureListCapacity(Container& items, int count)
        {
            if (count <= static_cast<int>(items.size()))
                return;
            try
            {
                items.resize(static_cast<size_t>(count));
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", count);
            }
        }

        template <class Container>
        void ensureAppendCapacity(Container& items, int currentCount)
        {
            const int oldCapacity = static_cast<int>(items.size());
            if (currentCount < oldCapacity)
                return;
            const int newCapacity = oldCapacity * 2 + 4;
            if (newCapacity <= oldCapacity)
                return;
            try
            {
                items.resize(static_cast<size_t>(newCapacity));
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
            }
        }

        bool isStringObject(const StackObject& value)
        {
            return (value.flags & STACK_OBJECT_STRING) != 0;
        }
    }

    int ParseStackIntegerText(const char* text)
    {
        if (text[1] == 'x')
        {
            int value = 0;
            std::sscanf(text, "%i", &value);
            return value;
        }
        return std::atoi(text);
    }

    STRING IntToStackString(int value)
    {
        char buffer[0x80];
        std::snprintf(buffer, sizeof(buffer), "%d", value);
        if (buffer[0] == '\0')
            return STRING();
        return STRING(buffer);
    }

    StackObject::StackObject()
        : flags(0), intValue(0), text()
    {
    }

    StackObject::StackObject(int value)
    {
        assignInt(value);
    }

    StackObject::StackObject(std::uint8_t initialFlags, int value, const STRING& initialText)
    {
        assignFields(initialFlags, value, initialText);
    }

    int StackObject::numericValue() const
    {
        if (flags & STACK_OBJECT_STRING)
            return ParseStackIntegerText(text.c_str());
        return intValue;
    }

    void StackObject::assignInt(int value)
    {
        flags = STACK_OBJECT_INT;
        intValue = value;
        text = STRING();
    }

    void StackObject::assignString(const STRING& value)
    {
        flags = STACK_OBJECT_STRING;
        intValue = 0;
        text = value;
    }

    void StackObject::assignFields(std::uint8_t initialFlags, int value, const STRING& initialText)
    {
        flags = initialFlags;
        intValue = value;
        text.Assign(initialText);
    }

    StackObject* StackObject::initializeReferenceValue(int value) noexcept
    {
        flags = static_cast<std::uint8_t>(STACK_OBJECT_INT | STACK_OBJECT_REF);
        intValue = value;
        text.ResetSharedEmptyWithoutRelease();
        if (value == 0)
            flags = STACK_OBJECT_INT;
        return this;
    }

    void StackObject::copyStorageFrom(const StackObject& other)
    {
        flags = other.flags;
        intValue = other.intValue;
        text.Assign(other.text);
    }

    void StackObject::copyFrom(const StackObject& other)
    {
        copyStorageFrom(other);
    }

    void StackObject::readFromStream(BaseStream* stream)
    {
        stream->read(&flags, 1);
        if ((flags & STACK_OBJECT_HAS_PAYLOAD) == 0)
            return;
        if (flags & STACK_OBJECT_STRING)
        {
            text.Read(stream);
            char* const bytes = const_cast<char*>(text.c_str());
            const std::size_t length = std::strlen(bytes);
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
        }
        else
        {
            stream->read(&intValue, 4);
        }
    }

    void StackObject::writeToStream(BaseStream* stream) const
    {
        stream->write(&flags, 1);
        if ((flags & STACK_OBJECT_HAS_PAYLOAD) == 0)
            return;
        if (flags & STACK_OBJECT_STRING)
        {
            char* const bytes = const_cast<char*>(text.c_str());
            const std::size_t length = std::strlen(bytes);
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
            stream->write(bytes, static_cast<unsigned>(length + 1u));
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
        }
        else
        {
            stream->write(&intValue, 4);
        }
    }

    void StackObject::applyBinaryCommand(std::uint8_t opcode, const StackObject& rhs)
    {
        const int rhsValue = rhs.numericValue();
        if (flags & STACK_OBJECT_STRING)
            intValue = numericValue();

        const auto setNumericResult = [this](int value)
        {
            flags = STACK_OBJECT_INT;
            intValue = value;
        };

        switch (static_cast<BinaryCommand>(opcode))
        {
        case BinaryCommand::Add: // plus / string append
            if (isStringObject(rhs) && isStringObject(*this))
            {
                text += rhs.text;
                flags = STACK_OBJECT_STRING;
            }
            else
                setNumericResult(intValue + rhsValue);
            break;
        case BinaryCommand::Subtract:
            setNumericResult(intValue - rhsValue);
            break;
        case BinaryCommand::Multiply:
            setNumericResult(intValue * rhsValue);
            break;
        case BinaryCommand::Divide:
            setNumericResult(rhsValue != 0 ? intValue / rhsValue : DivideByZeroSentinel);
            break;
        case BinaryCommand::Modulo:
            
            setNumericResult(intValue % rhsValue);
            break;
        case BinaryCommand::BitwiseOr:
            setNumericResult(intValue | rhsValue);
            break;
        case BinaryCommand::BitwiseXor:
            setNumericResult(intValue ^ rhsValue);
            break;
        case BinaryCommand::BitwiseAnd:
            setNumericResult(intValue & rhsValue);
            break;
        case BinaryCommand::ShiftLeft:
            setNumericResult(intValue << (rhsValue & 31));
            break;
        case BinaryCommand::ShiftRight:
            setNumericResult(intValue >> (rhsValue & 31));
            break;
        case BinaryCommand::LogicalAnd:
            setNumericResult((intValue != 0 && rhsValue != 0) ? 1 : 0);
            break;
        case BinaryCommand::LogicalOr:
            setNumericResult((intValue != 0 || rhsValue != 0) ? 1 : 0);
            break;
        case BinaryCommand::Less:
            setNumericResult(intValue < rhsValue ? 1 : 0);
            break;
        case BinaryCommand::LessEqual:
            setNumericResult(intValue <= rhsValue ? 1 : 0);
            break;
        case BinaryCommand::Greater:
            setNumericResult(intValue > rhsValue ? 1 : 0);
            break;
        case BinaryCommand::GreaterEqual:
            setNumericResult(intValue >= rhsValue ? 1 : 0);
            break;
        case BinaryCommand::Equal:
            if (isStringObject(rhs) && isStringObject(*this))
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) == 0 ? 1 : 0);
            else
                setNumericResult(intValue == rhsValue ? 1 : 0);
            break;
        case BinaryCommand::NotEqual:
            if (isStringObject(rhs) && isStringObject(*this))
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) != 0 ? 1 : 0);
            else
                setNumericResult(intValue != rhsValue ? 1 : 0);
            break;
        default:
            LOG::Write("!!!ERROE!!!LOGIC::Unknown Binary command %i", static_cast<int>(opcode));
            flags = STACK_OBJECT_INT;
            break;
        }
    }

    namespace
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        StackObject* allocateRetailStackRecords(int capacity)
        {
            if (capacity <= 0)
                return nullptr;
            const std::size_t bytes = sizeof(std::uint32_t) +
                static_cast<std::size_t>(capacity) * sizeof(StackObject);
            auto* raw = static_cast<std::uint8_t*>(::operator new(bytes));
            *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(capacity);
            StackObject* records = reinterpret_cast<StackObject*>(raw + sizeof(std::uint32_t));
            int constructed = 0;
            try
            {
                for (; constructed < capacity; ++constructed)
                    ::new (static_cast<void*>(records + constructed)) StackObject();
            }
            catch (...)
            {
                while (constructed > 0)
                {
                    --constructed;
                    records[constructed].~StackObject();
                }
                ::operator delete(raw);
                throw;
            }
            return records;
        }

        void destroyRetailStackRecords(StackObject* records) noexcept
        {
            if (!records)
                return;
            std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(records) - 1;
            const std::uint32_t capacity = *header;
            for (std::uint32_t i = capacity; i != 0; --i)
                records[i - 1u].~StackObject();
            ::operator delete(static_cast<void*>(header));
        }
#endif
    }

    void StackObjectList::reserveExact(int capacity)
    {
        if (capacity <= m_capacity)
            return;
#if defined(_MSC_VER) && defined(_M_IX86)
        StackObject* replacement = nullptr;
        try
        {
            replacement = allocateRetailStackRecords(capacity);
        }
        catch (...)
        {
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);
        }
        if (!replacement)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);

        StackObject* const oldRecords = data();
        m_tableToken = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(replacement));
        for (int i = 0; i < m_capacity; ++i)
            replacement[i].copyStorageFrom(oldRecords[i]);
        destroyRetailStackRecords(oldRecords);
        m_capacity = capacity;
#else
        ensureListCapacity(m_items, capacity);
        m_capacity = capacity;
#endif
    }

    void StackObjectList::appendFields(std::uint8_t flags, int value, STRING text)
    {
        if (m_count >= m_capacity)
        {
            const int expandedCapacity = m_capacity * 2 + 4;
            if (expandedCapacity > m_capacity)
                reserveExact(expandedCapacity);
        }

        const int writeIndex = m_count;
        ++m_count;
#if defined(_MSC_VER) && defined(_M_IX86)
        StackObject& destination = data()[static_cast<std::size_t>(writeIndex)];
#else
        StackObject& destination = m_items[static_cast<std::size_t>(writeIndex)];
#endif
        destination.flags = flags;
        destination.intValue = value;
        destination.text.Assign(text);
    }

    void StackObjectList::appendValueCopy(const StackObject& value)
    {
        StackObject localCopy;
        localCopy.copyStorageFrom(value);
        appendFields(localCopy.flags, localCopy.intValue, localCopy.text);
    }

    void StackObjectList::appendCopy(const StackObject& value)
    {
        appendValueCopy(value);
    }

    int StackObjectList::clearReferencesTo(int refIndex)
    {
        int cleared = 0;
        for (int i = 0; i < m_count; ++i)
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            StackObject& value = data()[static_cast<std::size_t>(i)];
#else
            StackObject& value = m_items[static_cast<std::size_t>(i)];
#endif
            if ((value.flags & STACK_OBJECT_REF) && value.intValue == refIndex)
            {
                value.intValue = 0;
                value.flags = static_cast<std::uint8_t>(value.flags & ~STACK_OBJECT_REF);
                ++cleared;
            }
        }
        return cleared;
    }

    LogicFunctionRecord::LogicFunctionRecord()
    {
        resetRetailDefaults();
    }

    void LogicFunctionRecord::resetRetailDefaults()
    {
        name = STRING();
        flags = 0;
        text = STRING();
    }

    void LogicFunctionRecord::copyFrom(const LogicFunctionRecord& other)
    {
        name = other.name;
        flags = other.flags;
        text = other.text;
        value0 = other.value0;
        value1 = other.value1;
        value2 = other.value2;
    }

    void LogicFunctionList::append(STRING name, std::uint8_t flags, STRING text, int value0, int value1, int value2)
    {
        ensureAppendCapacity(m_items, m_count);
        LogicFunctionRecord& rec = m_items[static_cast<size_t>(m_count)];
        LogicFunctionRecord temp;
        temp.name = name;
        temp.flags = flags;
        temp.text = text;
        temp.value0 = value0;
        temp.value1 = value1;
        temp.value2 = value2;
        rec.copyFrom(temp);
        ++m_count;
    }

    void* destroyLogicFunctionRecordStorage(LogicFunctionRecord* self, unsigned char flags) noexcept
    {
#if defined(_WIN32) && defined(_M_IX86)
        if ((flags & 0x02u) != 0)
        {
            std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(self) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
            {
                LogicFunctionRecord& record = self[i - 1u];
                destroyStringStorage(record.text);
                destroyStringStorage(record.name);
            }
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(header));
            return header;
        }
#endif

        destroyStringStorage(self->text);
        destroyStringStorage(self->name);
        if ((flags & 0x01u) != 0)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void LogicFunctionList::clearRecords()
    {
#if defined(_WIN32) && defined(_M_IX86)
        LogicFunctionRecord* const records = m_items.detachRetailAllocation();
        if (records)
            destroyLogicFunctionRecordStorage(records, 3);
#else
        m_items.clear();
        RetailRawArray<LogicFunctionRecord>().swap(m_items);
#endif
        m_count = 0;
    }

    void LogicFunctionList::reserveExact(int capacity)
    {
        ensureListCapacity(m_items, capacity);
    }

    void LogicFunctionList::truncateCount(int count)
    {
        if (count <= 0)
        {
            clearRecords();
            return;
        }
        if (count < m_count)
            m_count = count;
    }

    void LogicFunctionList::setLastValue2(int value)
    {
        m_items[static_cast<size_t>(m_count - 1)].value2 = value;
    }

    int LogicFunctionList::findLastByName(const STRING& name) const
    {
        for (int i = m_count - 1; i >= 0; --i)
        {
            if (std::strcmp(m_items[static_cast<size_t>(i)].name.c_str(), name.c_str()) == 0)
                return i;
        }
        return -1;
    }

    bool LogicFunctionList::appendLabelDefinition(const STRING& name, int bytecodeOffset)
    {
        append(name, 7, STRING(), bytecodeOffset, 0, 0);
        return true;
    }

    bool LogicFunctionList::resolvePendingLabel(int index, int bytecodeOffset, int& patchOffset)
    {
        if (index < 0 || index >= m_count || static_cast<size_t>(index) >= m_items.size())
            return false;
        LogicFunctionRecord& rec = m_items[static_cast<size_t>(index)];
        patchOffset = rec.value0;
        rec.flags = 7;
        rec.value0 = bytecodeOffset;
        return true;
    }

    LogicStringPair::LogicStringPair()
    {
        resetRetailDefaults();
    }

    void LogicStringPair::resetRetailDefaults()
    {
        left = STRING();
        right = STRING();
    }

    LogicStringPair* LogicStringPair::copyPairFrom(const LogicStringPair& other)
    {
        assignStringFromString(left, other.left);
        assignStringFromString(right, other.right);
        return this;
    }

    void LogicStringPair::copyFrom(const LogicStringPair& other)
    {
        (void)copyPairFrom(other);
    }

    void LogicStringPairList::append(STRING left, STRING right)
    {
        ensureAppendCapacity(m_items, m_count);
        LogicStringPair& rec = m_items[static_cast<size_t>(m_count)];
        LogicStringPair temp;
        temp.left = left;
        temp.right = right;
        rec.copyFrom(temp);
        ++m_count;
    }

    void LogicStringPairList::clearPairs()
    {
        m_items.clear();
        m_count = 0;
    }

    void LogicRangeStream::putByte(int value)
    {
        std::fputc(value & 0xFF, m_file);
    }

    void LogicRangeStream::beginWrite(std::uint8_t firstByte, int position, FILE* file)
    {
        if (m_file)
            return;
        m_file = file;
        m_low = 0;
        m_range = 0x80000000u;
        m_currentByte = firstByte;
        m_step = 0;
        m_position = static_cast<std::uint32_t>(position);
    }

    void LogicRangeStream::normalizeWrite()
    {
        while (m_range <= 0x800000u)
        {
            std::uint32_t low = m_low;
            if (low >= 0x7F800000u)
            {
                if (low & 0x80000000u)
                {
                    putByte(static_cast<int>(m_currentByte) + 1);
                    while (m_step)
                    {
                        putByte(0);
                        --m_step;
                    }
                    low = m_low;
                    m_currentByte = static_cast<std::uint8_t>(low >> 23);
                }
                else
                {
                    ++m_step;
                }
            }
            else
            {
                putByte(m_currentByte);
                while (m_step)
                {
                    putByte(0xFF);
                    --m_step;
                }
                low = m_low;
                m_currentByte = static_cast<std::uint8_t>(low >> 23);
            }
            m_range <<= 8;
            m_low = (low & 0x7FFFFFu) << 8;
            ++m_position;
        }
    }

    int LogicRangeStream::encodeStep(int lowCount, int base, int shift)
    {
        normalizeWrite();
        const std::uint32_t oldRange = m_range;
        const std::uint32_t step = oldRange >> (shift & 31);
        const std::uint32_t delta = static_cast<std::uint32_t>(base) * step;
        m_low += delta;
        if ((static_cast<std::uint32_t>(lowCount + base) >> (shift & 31)) != 0)
            m_range = oldRange - delta;
        else
            m_range = static_cast<std::uint32_t>(lowCount) * step;
        return static_cast<int>(step);
    }

    int LogicRangeStream::finishWrite()
    {
        if (!m_file)
            return -1;
        normalizeWrite();
        m_position += 5;
        const std::uint32_t low = m_low;
        std::uint32_t emit = (low & 0x7FFFFFu) >= ((m_position >> 1) & 0x7FFFFFu)
            ? (low >> 23) + 1
            : (low >> 23);
        const std::uint8_t finalByte = static_cast<std::uint8_t>(emit);
        if (emit <= 0xFFu)
        {
            putByte(m_currentByte);
            while (m_step)
            {
                putByte(0xFF);
                --m_step;
            }
        }
        else
        {
            putByte(static_cast<int>(m_currentByte) + 1);
            while (m_step)
            {
                putByte(0);
                --m_step;
            }
        }
        putByte(finalByte);
        putByte(static_cast<int>((m_position >> 16) & 0xFF));
        putByte(static_cast<int>((m_position >> 8) & 0xFF));
        putByte(static_cast<int>(m_position & 0xFF));
        return static_cast<int>(m_position);
    }

    int LogicRangeStream::readByte()
    {
        if (m_readStream)
        {
            std::uint8_t value = 0xFFu;
            if (m_readStream->read_new(&value, 1u) != 1u)
                return EOF;
            return static_cast<int>(value);
        }
        return std::fgetc(m_file);
    }

    int LogicRangeStream::beginRead(FILE* file)
    {
        if (m_file || m_readStream)
            return 0;
        m_file = file;
        const int first = readByte();
        if (first == EOF)
            return -1;
        const int second = readByte();
        m_currentByte = static_cast<std::uint8_t>(second);
        m_low = static_cast<std::uint32_t>(static_cast<std::uint8_t>(second)) >> 1;
        m_range = 0x80u;
        return first != 0 ? -1 : 0;
    }

    int LogicRangeStream::beginRead(as1::BaseStream& stream)
    {
        if (m_file || m_readStream)
            return 0;
        m_readStream = &stream;
        const int first = readByte();
        if (first == EOF)
            return -1;
        const int second = readByte();
        m_currentByte = static_cast<std::uint8_t>(second);
        m_low = static_cast<std::uint32_t>(static_cast<std::uint8_t>(second)) >> 1;
        m_range = 0x80u;
        return first != 0 ? -1 : 0;
    }

    int LogicRangeStream::decodeTarget(int shift)
    {
        while (m_range <= 0x800000u)
        {
            m_low = ((2u * m_low) | (m_currentByte & 1u)) << 7;
            const int ch = readByte();
            m_currentByte = static_cast<std::uint8_t>(ch);
            m_low |= static_cast<std::uint32_t>(m_currentByte) >> 1;
            m_range <<= 8;
        }
        const std::uint32_t step = m_range >> (shift & 31);
        m_step = step;
        std::uint32_t result = m_low / step;
        if ((result >> (shift & 31)) != 0)
            result = (1u << (shift & 31)) - 1u;
        return static_cast<int>(result);
    }

    int LogicRangeStream::removeDecodedRange(int count, int base, unsigned total)
    {
        const std::uint32_t step = m_step;
        const std::uint32_t delta = static_cast<std::uint32_t>(base) * step;
        m_low -= delta;
        if (static_cast<unsigned>(count + base) >= total)
            m_range -= delta;
        else
            m_range = static_cast<std::uint32_t>(count) * step;
        return static_cast<int>(delta);
    }

    namespace
    {
        int rescaleStepFromSymbolCount(int symbolCount)
        {
            int value = symbolCount;
            value = (value & ~0xFF) | ((value & 0xFF) | 0x20);
            return value >> 4;
        }

        std::uint16_t toU16(int value)
        {
            return static_cast<std::uint16_t>(value & 0xFFFF);
        }
    }

    LogicProbabilityModel::LogicProbabilityModel()
    {
        m_cumulative = nullptr;
        m_frequencies = nullptr;
        m_lookup = nullptr;
        setup(257, 12, 2000, nullptr);
    }

    LogicProbabilityModel::~LogicProbabilityModel()
    {
        releaseTables();
    }

    void LogicProbabilityModel::releaseTables()
    {
        if (m_cumulative)
            ::operator delete(m_cumulative);
        m_cumulative = nullptr;
        if (m_frequencies)
            ::operator delete(m_frequencies);
        m_frequencies = nullptr;
        if (m_lookup)
            ::operator delete(m_lookup);
        m_lookup = nullptr;
    }

    unsigned int LogicProbabilityModel::setup(int symbolCount, int bits, int maxFrequency, const std::int16_t* initialFrequencyEveryDword)
    {
        
        m_maxFrequency = maxFrequency;
        m_symbolCount = symbolCount;
        m_lookupShift = bits - 7;
        if (m_lookupShift < 0)
            m_lookupShift = 0;

        if (m_cumulative)
            ::operator delete(m_cumulative);
        const std::uint32_t tableBytes = (static_cast<std::uint32_t>(symbolCount) * 2u) + 2u;
        m_cumulative = static_cast<std::uint16_t*>(::operator new(static_cast<std::size_t>(tableBytes), std::nothrow));

        if (m_frequencies)
            ::operator delete(m_frequencies);
        m_frequencies = static_cast<std::uint16_t*>(::operator new(static_cast<std::size_t>(tableBytes), std::nothrow));

        if (m_lookup)
            ::operator delete(m_lookup);
        m_lookup = static_cast<std::uint16_t*>(::operator new(0x102u, std::nothrow));

        const std::uint32_t one = 1u;
        m_cumulative[symbolCount] = toU16(static_cast<int>(one << (bits & 31)));
        m_cumulative[0] = 0;
        if (m_lookup)
            m_lookup[128] = toU16(symbolCount - 1);
        return resetDistribution(initialFrequencyEveryDword);
    }

    unsigned int LogicProbabilityModel::resetDistribution(const std::int16_t* initialFrequencyEveryDword)
    {
        m_carry = 0;
        m_rescaleStep = rescaleStepFromSymbolCount(m_symbolCount);

        if (initialFrequencyEveryDword)
        {
            const std::int16_t* src = initialFrequencyEveryDword;
            for (int i = 0; i < m_symbolCount; ++i)
            {
                m_frequencies[i] = static_cast<std::uint16_t>(*src);
                src += 2;
            }
            return rescaleOrRefresh();
        }

        const int total = static_cast<int>(m_cumulative[m_symbolCount]);
        const int base = total / m_symbolCount;
        const int remainder = total % m_symbolCount;
        int i = 0;
        for (; i < remainder; ++i)
            m_frequencies[i] = toU16(base + 1);
        for (; i < m_symbolCount; ++i)
            m_frequencies[i] = toU16(base);
        return rescaleOrRefresh();
    }

    unsigned int LogicProbabilityModel::rescaleOrRefresh()
    {
        if (m_carry != 0)
        {
            m_countdown = m_carry;
            m_carry = 0;
            ++m_increment;
            return static_cast<unsigned int>(m_countdown);
        }

        if (m_rescaleStep < m_maxFrequency)
        {
            m_rescaleStep *= 2;
            if (m_rescaleStep > m_maxFrequency)
                m_rescaleStep = m_maxFrequency;
        }

        int totalBefore = static_cast<int>(m_cumulative[m_symbolCount]);
        int running = totalBefore;
        int newTotal = totalBefore;
        for (int index = m_symbolCount - 1; index != 0; --index)
        {
            const int freq = static_cast<int>(m_frequencies[index]);
            running -= freq;
            m_cumulative[index] = toU16(running);
            const int newFreq = (freq | 2) >> 1;
            newTotal -= newFreq;
            m_frequencies[index] = toU16(newFreq);
        }

        const int freq0 = static_cast<int>(m_frequencies[0]);
        if (running != freq0)
        {
            std::fprintf(stderr, "BUG: rescaling left %d total frequency\n",
                static_cast<int>(reinterpret_cast<std::intptr_t>(m_cumulative)));
            std::exit(1);
        }

        m_frequencies[0] = toU16((freq0 | 2) >> 1);
        const int effectiveTotal = newTotal - static_cast<int>(m_frequencies[0]);
        m_increment = effectiveTotal / m_rescaleStep;
        m_carry = effectiveTotal % m_rescaleStep;
        m_countdown = m_rescaleStep - m_carry;

        return rebuildLookupTable();
    }

    unsigned int LogicProbabilityModel::rebuildLookupTable()
    {
        unsigned int result = static_cast<unsigned int>(
            reinterpret_cast<std::uintptr_t>(m_lookup) & 0xFFFFFFFFu);
        if (m_lookup)
        {
            int symbol = m_symbolCount;
            while (symbol != 0)
            {
                const int high =
                    (static_cast<int>(m_cumulative[symbol]) - 1) >>
                    (m_lookupShift & 31);
                --symbol;
                result = static_cast<unsigned int>(
                    static_cast<int>(m_cumulative[symbol]) >>
                    (m_lookupShift & 31));
                while (static_cast<int>(result) <= high)
                {
                    ++result;
                    m_lookup[result - 1] = toU16(symbol);
                }
            }
        }
        return result;
    }

    LogicProbabilityRange LogicProbabilityModel::intervalForSymbol(int symbol) const
    {
        LogicProbabilityRange out;
        out.base = static_cast<int>(m_cumulative[symbol]);
        out.count = static_cast<int>(m_cumulative[symbol + 1]) - out.base;
        return out;
    }

    int LogicProbabilityModel::symbolFromTarget(int target) const
    {
        int bucket = target >> (m_lookupShift & 31);
        int low = static_cast<int>(m_lookup[bucket]);
        int high = static_cast<int>(m_lookup[bucket + 1]) + 1;
        if (low + 1 < high)
        {
            while (low + 1 < high)
            {
                const int mid = (high + low) >> 1;
                const int split = static_cast<int>(m_cumulative[mid]);
                if (target >= split)
                    low = mid;
                else
                    high = mid;
            }
        }
        return low;
    }

    std::uint16_t* LogicProbabilityModel::updateSymbol(int symbol)
    {
        if (m_countdown <= 0)
            rescaleOrRefresh();
        --m_countdown;
        m_frequencies[symbol] =
            toU16(static_cast<int>(m_frequencies[symbol]) + m_increment);
        return m_frequencies + symbol;
    }

    LogicAdaptiveCodec::LogicAdaptiveCodec()
    {
    }

    unsigned int LogicAdaptiveCodec::resetModels()
    {
        unsigned int result = 0;
        for (LogicProbabilityModel& m : m_models)
            result = m.resetDistribution(nullptr);
        return result;
    }

    std::size_t LogicAdaptiveCodec::writeCompressed(const void* data, std::size_t size, FILE* file)
    {
        if (!file || size == 0)
            return 0;
        if (size < 0x0A)
            return std::fwrite(data, 1, size, file);

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        LogicRangeStream stream;
        stream.beginWrite(0, 0, file);
        std::size_t half = (size + 1) >> 1;
        std::size_t physical = 0;
        int previous = 0;
        for (std::size_t logical = 0; logical < size; ++logical)
        {
            int symbol = 0;
            if (m_mode == 2)
            {
                if (logical < half)
                    symbol = bytes[physical];
                else
                    symbol = bytes[physical - 2 * half + 1];
            }
            else
            {
                symbol = bytes[logical];
            }
            physical += 2;
            LogicProbabilityModel& modelRef = model(previous);
            const LogicProbabilityRange range = modelRef.intervalForSymbol(symbol);
            stream.encodeStep(range.count, range.base, 12);
            modelRef.updateSymbol(symbol);
            previous = symbol;
        }

        LogicProbabilityModel& finalModel = model(previous);
        const LogicProbabilityRange eofRange = finalModel.intervalForSymbol(256);
        stream.encodeStep(eofRange.count, eofRange.base, 12);
        return static_cast<std::size_t>(stream.finishWrite());
    }


    std::size_t LogicAdaptiveCodec::ReadDecoded(void* destination, std::size_t size, as1::BaseStream& stream)
    {
        if (size == 0u)
            return 0u;
        if (size < 0x0Au)
            return stream.read_new(destination, size);

        auto* bytes = static_cast<std::uint8_t*>(destination);
        LogicRangeStream rangeStream;
        if (rangeStream.beginRead(stream) < 0)
            return 0u;

        const std::size_t half = (size + 1u) >> 1u;
        std::size_t physical = 0u;
        std::size_t written = 0u;
        int previous = 0;
        for (;;)
        {
            const int target = rangeStream.decodeTarget(12);
            LogicProbabilityModel& modelRef = model(previous);
            const int symbol = modelRef.symbolFromTarget(target);
            if (symbol == 256)
                break;
            if (written >= size)
            {
                std::fprintf(stderr, "!!!ERROR!!! decode");
                break;
            }

            if (m_mode == 2)
            {
                if (written < half)
                    bytes[physical] = static_cast<std::uint8_t>(symbol);
                else
                    bytes[physical - 2u * half + 1u] = static_cast<std::uint8_t>(symbol);
            }
            else
            {
                bytes[written] = static_cast<std::uint8_t>(symbol);
            }

            ++written;
            physical += 2u;
            const LogicProbabilityRange interval = modelRef.intervalForSymbol(symbol);
            rangeStream.removeDecodedRange(interval.count, interval.base, 0x1000u);
            modelRef.updateSymbol(symbol);
            previous = symbol;
        }

        LogicProbabilityModel& finalModel = model(previous);
        const LogicProbabilityRange eofRange = finalModel.intervalForSymbol(256);
        rangeStream.removeDecodedRange(eofRange.count, eofRange.base, 0x1000u);

        if (rangeStream.m_range <= 0x800000u)
        {
            do
            {
                rangeStream.m_low = ((2u * rangeStream.m_low) | (rangeStream.m_currentByte & 1u)) << 7;
                const int ch = rangeStream.readByte();
                rangeStream.m_currentByte = static_cast<std::uint8_t>(ch);
                rangeStream.m_low |= static_cast<std::uint32_t>(rangeStream.m_currentByte) >> 1;
                rangeStream.m_range <<= 8;
            }
            while (rangeStream.m_range <= 0x800000u);
        }
        return written;
    }

    size_t LogicAdaptiveCodec::WriteEncoded(const void* source, size_t size, BaseStream& stream)
    {
        if (!source || size == 0)
            return 0;

        FILE* tmp = std::tmpfile();
        if (!tmp)
            return stream.write_new(source, size);

        const std::size_t encoded = writeCompressed(source, size, tmp);
        std::fflush(tmp);
        std::fseek(tmp, 0, SEEK_SET);

        std::vector<std::uint8_t> buffer(encoded);
        const std::size_t got = encoded ? std::fread(buffer.data(), 1, encoded, tmp) : 0;
        std::fclose(tmp);
        if (got != encoded)
            return 0;
        return stream.write_new(buffer.data(), buffer.size());
    }

    std::size_t LogicAdaptiveCodec::readCompressed(void* data, std::size_t size, FILE* file)
    {
        if (!file || size == 0)
            return 0;
        if (size < 0x0A)
            return std::fread(data, 1, size, file);

        auto* bytes = static_cast<std::uint8_t*>(data);
        LogicRangeStream stream;
        if (stream.beginRead(file) < 0)
            return 0;

        std::size_t half = (size + 1) >> 1;
        std::size_t physical = 0;
        std::size_t written = 0;
        int previous = 0;
        for (;;)
        {
            const int target = stream.decodeTarget(12);
            LogicProbabilityModel& modelRef = model(previous);
            const int symbol = modelRef.symbolFromTarget(target);
            if (symbol == 256)
                break;
            if (written >= size)
            {
                std::fprintf(stderr, "!!!ERROR!!! decode");
                break;
            }

            if (m_mode == 2)
            {
                if (written < half)
                    bytes[physical] = static_cast<std::uint8_t>(symbol);
                else
                    bytes[physical - 2 * half + 1] = static_cast<std::uint8_t>(symbol);
            }
            else
            {
                bytes[written] = static_cast<std::uint8_t>(symbol);
            }

            ++written;
            physical += 2;
            const LogicProbabilityRange range = modelRef.intervalForSymbol(symbol);
            stream.removeDecodedRange(range.count, range.base, 0x1000u);
            modelRef.updateSymbol(symbol);
            previous = symbol;
        }

        LogicProbabilityModel& finalModel = model(previous);
        const LogicProbabilityRange eofRange = finalModel.intervalForSymbol(256);
        stream.removeDecodedRange(eofRange.count, eofRange.base, 0x1000u);

        if (stream.m_range <= 0x800000u)
        {
            do
            {
                stream.m_low = ((2u * stream.m_low) | (stream.m_currentByte & 1u)) << 7;
                const int ch = stream.readByte();
                stream.m_currentByte = static_cast<std::uint8_t>(ch);
                stream.m_low |= static_cast<std::uint32_t>(stream.m_currentByte) >> 1;
                stream.m_range <<= 8;
            }
            while (stream.m_range <= 0x800000u);
        }
        return written;
    }


    int decodeLogicKeyNameImpl(STRING name)
    {
        char* const mutableText = const_cast<char*>(name.c_str());
        for (char* p = mutableText; p && *p; ++p)
        {
            if (*p >= 'a' && *p <= 'z')
                *p = static_cast<char>(*p - ('a' - 'A'));
        }
        const char* const text = name.c_str();
        if (std::strcmp(text, "LBUTTON") == 0)
            return 1;
        if (std::strcmp(text, "RBUTTON") == 0)
            return 2;
        if (std::strcmp(text, "[") == 0)
            return 91;
        if (std::strcmp(text, "]") == 0)
            return 92;
        if (std::strcmp(text, "LEFT") == 0)
            return 37;
        if (std::strcmp(text, "RIGHT") == 0)
            return 39;
        if (std::strcmp(text, "UP") == 0)
            return 38;
        if (std::strcmp(text, "DOWN") == 0)
            return 40;
        if (std::strcmp(text, "INSERT") == 0)
            return 45;
        if (std::strcmp(text, "DELETE") == 0)
            return 46;
        if (std::strcmp(text, "HOME") == 0)
            return 36;
        if (std::strcmp(text, "END") == 0)
            return 35;
        if (std::strcmp(text, "PGUP") == 0)
            return 33;
        if (std::strcmp(text, "PGDN") == 0)
            return 34;
        if (std::strcmp(text, "SHIFT") == 0)
            return 16;
        if (std::strcmp(text, "CTRL") == 0)
            return 17;
        if (std::strcmp(text, "F1") == 0)
            return 112;
        if (std::strcmp(text, "F2") == 0)
            return 113;
        if (std::strcmp(text, "F3") == 0)
            return 114;
        if (std::strcmp(text, "F4") == 0)
            return 115;
        if (std::strcmp(text, "F5") == 0)
            return 116;
        if (std::strcmp(text, "F6") == 0)
            return 117;
        if (std::strcmp(text, "F7") == 0)
            return 118;
        if (std::strcmp(text, "F8") == 0)
            return 119;
        if (std::strcmp(text, "F9") == 0)
            return 120;
        if (std::strcmp(text, "F10") == 0)
            return 121;
        if (std::strcmp(text, "F11") == 0)
            return 122;
        if (std::strcmp(text, "F12") == 0)
            return 123;
        return static_cast<signed char>(text[0]);
    }

    int decodeLogicKeyName(STRING name)
    {
        return decodeLogicKeyNameImpl(name);
    }
} }
