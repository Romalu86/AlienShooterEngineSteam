#pragma once
#include "../core/as_string.h"
#include "../core/resource_filter.h"
#include "retail_raw_array.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace as1
{
    namespace script
    {
        enum StackObjectFlags : std::uint8_t
        {
            STACK_OBJECT_STRING      = 1u << 0,
            STACK_OBJECT_INT         = 1u << 1,
            STACK_OBJECT_ARRAY       = 1u << 2,
            STACK_OBJECT_HAS_PAYLOAD = 1u << 3,
            STACK_OBJECT_REF         = 1u << 4,
            STACK_OBJECT_DYNAMIC     = 1u << 5,
            STACK_OBJECT_CHAR_WRITE  = 1u << 6,
        };

        enum class BinaryCommand : std::uint8_t
        {
            Divide        = 6,
            Modulo        = 7,
            Add           = 8,
            Subtract      = 9,
            BitwiseXor    = 10,
            BitwiseOr     = 11,
            BitwiseAnd    = 12,
            Equal         = 13,
            LogicalOr     = 14,
            Greater       = 15,
            Less          = 16,
            GreaterEqual  = 17,
            LessEqual     = 18,
            Multiply      = 19,
            NotEqual      = 20,
            LogicalAnd    = 21,
            ShiftRight    = 22,
            ShiftLeft     = 23,
        };

        enum class VmOpcode : std::uint8_t
        {
            PushInteger        = 1,
            PushString         = 2,
            Negate             = 3,
            BitwiseNot         = 4,
            LogicalNot         = 5,
            If                 = 24,
            StatementEnd       = 25,
            Pop                = 26,
            Jump               = 28,
            IfFalseChain       = 29,
            CallScriptFunction = 30,
            Return             = 31,
            PostIncrement      = 32,
            PostDecrement      = 33,
            PreIncrement       = 34,
            PreDecrement       = 35,
            ReadVariable       = 36,
            AddressOf          = 37,
            Assign             = 38,
            CompoundAssign     = 39,
            ArrayIndex         = 40,
        };

        constexpr std::uint8_t opcodeValue(BinaryCommand command) noexcept
        {
            return static_cast<std::uint8_t>(command);
        }

        constexpr std::uint8_t opcodeValue(VmOpcode command) noexcept
        {
            return static_cast<std::uint8_t>(command);
        }

        struct StackObject
        {
            std::uint8_t flags = 0;
            int intValue = 0;
            STRING text;

            StackObject();
            explicit StackObject(int value);
            StackObject(std::uint8_t initialFlags, int value, const STRING& initialText);

            int numericValue() const;
            void assignInt(int value);
            void assignString(const STRING& value);
            void assignFields(std::uint8_t initialFlags, int value, const STRING& initialText);
            StackObject* initializeReferenceValue(int value) noexcept;
            void copyStorageFrom(const StackObject& other);
            void copyFrom(const StackObject& other);
            void readFromStream(BaseStream* stream);
            void writeToStream(BaseStream* stream) const;
            void applyBinaryCommand(std::uint8_t opcode, const StackObject& rhs);
        };

        STRING IntToStackString(int value);
        int ParseStackIntegerText(const char* text);

        class StackObjectList
        {
        public:
            void reserveExact(int capacity);
            void appendFields(std::uint8_t flags, int value, STRING text);
            void appendValueCopy(const StackObject& value);
            void appendCopy(const StackObject& value);
            int clearReferencesTo(int refIndex);

            int count() const { return m_count; }
            int capacity() const { return m_capacity; }
#if defined(_MSC_VER) && defined(_M_IX86)
            StackObject* data() noexcept
            {
                return reinterpret_cast<StackObject*>(static_cast<std::uintptr_t>(m_tableToken));
            }
            const StackObject* data() const noexcept
            {
                return reinterpret_cast<const StackObject*>(static_cast<std::uintptr_t>(m_tableToken));
            }
            const StackObject& operator[](int index) const { return data()[static_cast<std::size_t>(index)]; }
            StackObject& operator[](int index) { return data()[static_cast<std::size_t>(index)]; }
#else
            const StackObject& operator[](int index) const { return m_items.at(static_cast<size_t>(index)); }
            StackObject& operator[](int index) { return m_items.at(static_cast<size_t>(index)); }
            const std::vector<StackObject>& items() const { return m_items; }
#endif

        private:
#if defined(_MSC_VER) && defined(_M_IX86)
            std::uint32_t m_vtableToken = 0;
            int m_count = 0;
            int m_capacity = 0;
            std::uint32_t m_tableToken = 0;
#else
            int m_count = 0;
            int m_capacity = 0;
            std::vector<StackObject> m_items;
#endif
        };
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                                             
#endif

        struct LogicFunctionRecord
        {
            STRING name;
            std::uint8_t flags = 0;
            STRING text;
            int value0;
            int value1;
            int value2;

            LogicFunctionRecord();
            void copyFrom(const LogicFunctionRecord& other);
            void resetRetailDefaults();
        };

        // Compiler-generated vector/scalar deleting destructor
        // for the 24-byte SCRIPT function record.
        void* destroyLogicFunctionRecordStorage(LogicFunctionRecord* self, unsigned char flags) noexcept;

        class LogicFunctionList
        {
        public:
            void append(STRING name, std::uint8_t flags, STRING text, int value0, int value1, int value2);
            void clearRecords();
            void reserveExact(int capacity);
            void truncateCount(int count);
            void setLastValue2(int value);
            int findLastByName(const STRING& name) const;
            bool appendLabelDefinition(const STRING& name, int bytecodeOffset);
            bool resolvePendingLabel(int index, int bytecodeOffset, int& patchOffset);
            int count() const { return m_count; }
            int capacity() const { return static_cast<int>(m_items.size()); }
            RetailRawArray<LogicFunctionRecord>& items() { return m_items; }
            const RetailRawArray<LogicFunctionRecord>& items() const { return m_items; }

        private:
            int m_count = 0;
            RetailRawArray<LogicFunctionRecord> m_items;
        };

        struct LogicStringPair
        {
            STRING left;
            STRING right;

            LogicStringPair();
            LogicStringPair* copyPairFrom(const LogicStringPair& other);
            void copyFrom(const LogicStringPair& other);
            void resetRetailDefaults();
        };


        class LogicRangeStream
        {
        public:
            void beginWrite(std::uint8_t firstByte, int position, FILE* file);
            int encodeStep(int lowCount, int base, int shift);
            int finishWrite();

            int beginRead(FILE* file);
            int beginRead(as1::BaseStream& stream);
            int decodeTarget(int shift);
            int removeDecodedRange(int count, int base, unsigned total);

            std::uint32_t low() const { return m_low; }
            std::uint32_t range() const { return m_range; }
            std::uint32_t step() const { return m_step; }
            std::uint32_t position() const { return m_position; }
            std::uint8_t currentByte() const { return m_currentByte; }
            FILE* file() const { return m_file; }
            as1::BaseStream* stream() const { return m_readStream; }

        private:
            friend class LogicAdaptiveCodec;
            void normalizeWrite();
            void putByte(int value);
            int readByte();

            std::uint32_t m_low = 0;
            std::uint32_t m_range = 0;
            std::uint32_t m_step = 0;
            std::uint8_t m_currentByte = 0;
            std::uint32_t m_position = 0;
            FILE* m_file = nullptr;
            as1::BaseStream* m_readStream = nullptr;
        };


        struct LogicProbabilityRange
        {
            int count = 0;
            int base = 0;
        };

        class LogicProbabilityModel
        {
        public:
            LogicProbabilityModel();
            ~LogicProbabilityModel();

            void releaseTables();
            unsigned int rescaleOrRefresh();
            unsigned int setup(int symbolCount, int bits, int maxFrequency, const std::int16_t* initialFrequencyEveryDword);
            unsigned int resetDistribution(const std::int16_t* initialFrequencyEveryDword);
            LogicProbabilityRange intervalForSymbol(int symbol) const;
            int symbolFromTarget(int target) const;
            std::uint16_t* updateSymbol(int symbol);

            int symbolCount() const { return m_symbolCount; }
            int countdown() const { return m_countdown; }
            int carry() const { return m_carry; }
            int rescaleStep() const { return m_rescaleStep; }
            int maxFrequency() const { return m_maxFrequency; }
            int increment() const { return m_increment; }
            int lookupShift() const { return m_lookupShift; }
            const std::uint16_t* cumulative() const { return m_cumulative; }
            const std::uint16_t* frequencies() const { return m_frequencies; }
            const std::uint16_t* lookup() const { return m_lookup; }

        private:
            unsigned int rebuildLookupTable();

            int m_symbolCount = 0;
            int m_countdown = 0;
            int m_carry = 0;
            int m_rescaleStep = 0;
            int m_maxFrequency = 0;
            int m_increment = 0;
            int m_lookupShift = 0;
            std::uint16_t* m_cumulative = nullptr;
            std::uint16_t* m_frequencies = nullptr;
            std::uint16_t* m_lookup = nullptr;
        };

        class LogicAdaptiveCodec : public as1::Filter
        {
        public:
            LogicAdaptiveCodec();

            unsigned int resetModels();
            std::size_t writeCompressed(const void* data, std::size_t size, FILE* file);
            std::size_t readCompressed(void* data, std::size_t size, FILE* file);
            std::size_t ReadDecoded(void* destination, std::size_t size, as1::BaseStream& stream) override;

            std::size_t WriteEncoded(const void* source, std::size_t size, as1::BaseStream& stream) override;

            void setMode(int mode) { m_mode = mode; }
            int mode() const { return m_mode; }
            LogicProbabilityModel& model(int index) { return m_models[static_cast<std::size_t>(index & 0xFF)]; }
            const LogicProbabilityModel& model(int index) const { return m_models[static_cast<std::size_t>(index & 0xFF)]; }

        private:
            int m_mode = 0;
            LogicProbabilityModel m_models[256];
        };
#if defined(_WIN32) && !defined(_WIN64)
                                                                                                               
                                                                                                           
#endif

        int decodeLogicKeyNameImpl(STRING name);
        int decodeLogicKeyName(STRING name);

        class LogicStringPairList
        {
        public:
            void append(STRING left, STRING right);
            void clearPairs();
            int count() const { return m_count; }
            int capacity() const { return static_cast<int>(m_items.size()); }
            const std::vector<LogicStringPair>& items() const { return m_items; }

        private:
            int m_count = 0;
            std::vector<LogicStringPair> m_items;
        };
    }
}
