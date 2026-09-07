#include "script/lgc_script.h"
#include "script/native_function_codes.h"
#include "script/vid_data_codes.h"

#include "core/application.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "core/configuration.h"
#include "core/crc32.h"
#include "core/file_stream.h"
#include "core/file_logger.h"
#include "core/log.h"
#include "core/profile_p.h"
#include "core/msvc_array_helpers.h"
#include "core/retail_stack_abi.h"
#include "mouse.h"
#include "input.h"
#include "map.h"
#include "engine.h"
#include "graph.h"
#include "file_data.h"
#include "steam_store.h"
#include "core/weak_controller.h"
#include "sprite.h"
#include "sprite_collector_hash.h"
#include "sound/engine.h"
#include "vid/vid.h"
#ifdef _WIN32
#include "win/application_win.h"
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <string>
#if defined(_MSC_VER)
#include <io.h>
#endif

namespace as1
{

    namespace
    {
        std::uint32_t retailFileLength32(const FileStream& stream) noexcept
        {
#if defined(_MSC_VER)
            const std::FILE* file = stream.nativeFile();
            if (!file)
                return 0xFFFFFFFFu;
            const int fd = _fileno(const_cast<std::FILE*>(file));
            if (fd < 0)
                return 0xFFFFFFFFu;
            return static_cast<std::uint32_t>(_filelength(fd));
#else
            return static_cast<std::uint32_t>(stream.length());
#endif
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        template <class T>
        T* allocateRetailScriptRecords(int capacity)
        {
            if (capacity <= 0)
                return nullptr;
            const std::size_t bytes = sizeof(std::uint32_t) +
                static_cast<std::size_t>(capacity) * sizeof(T);
            auto* raw = static_cast<std::uint8_t*>(::operator new(bytes));
            *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(capacity);
            T* const records = reinterpret_cast<T*>(raw + sizeof(std::uint32_t));
            int constructed = 0;
            try
            {
                for (; constructed < capacity; ++constructed)
                    ::new (static_cast<void*>(records + constructed)) T();
            }
            catch (...)
            {
                while (constructed > 0)
                {
                    --constructed;
                    records[constructed].~T();
                }
                ::operator delete(static_cast<void*>(raw));
                throw;
            }
            return records;
        }
#endif

        const char Class[] = "";

        STRING lpFile;

        void shutdownGlobalScriptPathString() noexcept
        {
            destroyStringStorage(lpFile);
        }

        struct RetailLpFileShutdownRegistration
        {
            ~RetailLpFileShutdownRegistration() noexcept
            {
                shutdownGlobalScriptPathString();
                lpFile.ResetSharedEmptyWithoutRelease();
            }
        };

        RetailLpFileShutdownRegistration g_retailLpFileShutdownRegistration;

        
        int g_scriptUnitIteratorCursor = 0;
        int g_scriptUnitIteratorTypeMask = 0;
        int g_scriptSpriteIteratorPass = 0;
        int g_scriptSpriteIteratorCursor = 0;

        std::FILE* scriptNativeFileFromInt(int value)
        {
            return reinterpret_cast<std::FILE*>(static_cast<std::intptr_t>(value));
        }

        int scriptNativeIntFromFile(std::FILE* file)
        {
            return static_cast<int>(reinterpret_cast<std::intptr_t>(file));
        }

        const input::InputMessageState& scriptApplicationInputState254() noexcept
        {
            if (void* const owner = core::ApplicationPhysicalOwner())
                return *reinterpret_cast<const input::InputMessageState*>(
                    static_cast<const std::uint8_t*>(owner) + core::retail_application_layout::InputState);
            static const input::InputMessageState zero{};
            return zero;
        }


        void scriptNativeDecodeGammaIndex(int value, std::uint32_t& diffuse, std::uint32_t& specular)
        {
            diffuse = 0;
            specular = 0;
            const std::uint32_t packed = static_cast<std::uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t byteValue = (packed >> shift) & 0xFFu;
                const std::uint32_t component = ((byteValue & 0x80u) != 0)
                    ? (((~byteValue) & 0x7Fu) << 1)
                    : ((byteValue & 0x7Fu) << 1);
                if ((byteValue & 0x80u) != 0)
                    specular |= (component & 0xFFu) << shift;
                else
                    diffuse |= (component & 0xFFu) << shift;
            }
        }

        int interpolateGammaComponent(int a1, int a2, int time)
        {
            int first = a1;
            if (first >= 0x80)
                first -= 0xFE;
            int second = a2;
            if (second >= 0x80)
                second -= 0xFE;
            const std::uint32_t rawDelta =
                static_cast<std::uint32_t>(second - first) * static_cast<std::uint32_t>(time);
            std::int32_t delta = 0;
            std::memcpy(&delta, &rawDelta, sizeof(delta));
            const int step = delta / 255;
            return (step + first) & 0xFF;
        }

        int interpolateGammaColor(int g1, int g2, int time)
        {
            const int b0 = interpolateGammaComponent(g1 & 0xFF, g2 & 0xFF, time) & 0xFF;
            const int b1 = interpolateGammaComponent((g1 >> 8) & 0xFF, (g2 >> 8) & 0xFF, time) & 0xFF;
            const int b2 = interpolateGammaComponent((g1 >> 16) & 0xFF, (g2 >> 16) & 0xFF, time) & 0xFF;
            return b0 | (b1 << 8) | (b2 << 16);
        }

        int scriptNativeEncodeGammaIndex(std::uint32_t diffuse, std::uint32_t specular)
        {
            std::uint32_t out = (diffuse >> 1) & 0x7F7F7F7Fu;
            for (int shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t specByte = (specular >> shift) & 0xFFu;
                if (specByte == 0)
                    continue;
                const std::uint32_t packedByte = 0x80u | (((~specByte) & 0xFEu) >> 1);
                out = (out & ~(0xFFu << shift)) | ((packedByte & 0xFFu) << shift);
            }
            return static_cast<int>(out);
        }
    }


    int SCRIPT::compileScriptSourceFile(const STRING& scriptFile, const STRING& gameRoot)
    {
        (void)gameRoot;

        FileStream stream(scriptFile.str(), "rb");
        resetScriptVmState();
        assignStringFromString(scriptFileStorage(), scriptFile);
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        const std::uint32_t fileLength = retailFileLength32(stream);
        prepareSourceCompiler(scriptFile, &stream, fileLength);

        int status = compileNextSourceItem();
        while (status == 0)
            status = compileNextSourceItem();

        writeLogLine(
            g_fileLogger,
            "LoadScript::ByteCode=%i varNo=%i DefineNo=%i stackNo=%i",
            m_physical.bytecodeEnd,
            functionCount(),
            defineCount(),
            executionStackCount());

        clearDefines();

        if (m_physical.bytecodeEnd != 0)
        {
            if (m_physical.bytecodeEnd > static_cast<int>(TemporaryBytecodeCapacity))
            {
                reportCompileError(2, "byte code size", m_physical.bytecodeEnd);
            }

            // freshly allocated buffer of exactly bytecodeEnd bytes when
            // bytecodeEnd is non-zero, including the full 0x3E800 case and
            // the post-diagnostic oversized path.
            try
            {
                const std::size_t finalSize = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(m_physical.bytecodeEnd));
#if defined(_MSC_VER) && defined(_M_IX86)
                void* const finalBytecode = ::operator new(finalSize);
                if (finalSize != 0)
                    std::memcpy(finalBytecode, bytecodeStorage(), finalSize);
                ::operator delete(static_cast<void*>(bytecodeStorage()));
                m_physical.bytecodeBufferToken = retailPointerToken(finalBytecode);
#else
                script::RetailByteBuffer finalBytecode(finalSize);
                if (finalSize != 0)
                    std::memcpy(finalBytecode.data(), bytecodeStorage(), finalSize);
                host().m_bytecode.swap(finalBytecode);
                syncPhysicalBackingPointers();
#endif
            }
            catch (...)
            {
                // Failed exact-bytecode load leaves the compiled buffer empty.
                // allocation reports compiler error 2 / "tmp" and exits.
                reportCompileError(2, "tmp", 0);
                std::exit(1);
            }
        }
        else
        {
            resetScriptVmState();
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        ::operator delete(static_cast<void*>(sourceStorage()));
        m_physical.sourceBufferToken = 0u;
#else
        host().m_sourceBuffer.clear();
        script::RetailByteBuffer().swap(host().m_sourceBuffer);
        setSourceCursorOffset(0);
        setSourceEndOffset(0);
        m_physical.sourceBufferToken = 0;
#endif
        stream.close();
        return 0;
    }

    int SCRIPT::loadScriptFile(const STRING& scriptFile, const STRING& gameRoot)
    {
        std::uint32_t firstDword = static_cast<std::uint32_t>(m_physical.stackCount);
        FileStream stream(scriptFile.str(), "rb");
        resetScriptVmState();
        assignStringFromString(scriptFileStorage(), scriptFile);
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        stream.read_new(&firstDword, sizeof(firstDword));

        if ((firstDword & 0xFF000000u) != 0)
        {
            const int result = compileScriptSourceFile(scriptFileStorage(), gameRoot);
            stream.close();
            return result;
        }

        const int stackCount = static_cast<int>(firstDword);

        if (m_physical.stackCapacity < static_cast<int>(InitialListCapacity))
        {
            try
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                script::StackObject* const records = allocateRetailScriptRecords<script::StackObject>(static_cast<int>(InitialListCapacity));
                m_physical.stackTableToken = retailPointerToken(records);
#else
                host().m_executionStack.resize(InitialListCapacity);
#endif
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                    static_cast<int>(InitialListCapacity));
            }
            m_physical.stackCapacity = static_cast<int>(InitialListCapacity);
#if !defined(_MSC_VER) || !defined(_M_IX86)
            syncPhysicalBackingPointers();
#endif
        }
        m_physical.stackCount = stackCount;
        if (stackCount > m_physical.stackCapacity)
        {
            try
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                const int oldCapacity = m_physical.stackCapacity;
                script::StackObject* const oldRecords = executionStackStorage();
                script::StackObject* const records = allocateRetailScriptRecords<script::StackObject>(stackCount);
                for (int i = 0; i < oldCapacity; ++i)
                    records[i].copyStorageFrom(oldRecords[i]);
                if (oldRecords)
                    msvcStringRecordDeletingDestructor(static_cast<void*>(oldRecords), 3);
                m_physical.stackTableToken = retailPointerToken(records);
#else
                host().m_executionStack.resize(static_cast<std::size_t>(stackCount));
#endif
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", stackCount);
            }
            m_physical.stackCapacity = stackCount;
#if !defined(_MSC_VER) || !defined(_M_IX86)
            syncPhysicalBackingPointers();
#endif
        }
        readExecutionStackFromStream(&stream);

        std::uint32_t bytecodeSize = 0;
        stream.read_new(&bytecodeSize, sizeof(bytecodeSize));

#if !defined(_MSC_VER) || !defined(_M_IX86)
        host().m_bytecode.clear();
        script::RetailByteBuffer().swap(host().m_bytecode);
#endif
        try
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            void* const bytecodeOwner = ::operator new(static_cast<std::size_t>(bytecodeSize));
            m_physical.bytecodeBufferToken = retailPointerToken(bytecodeOwner);
#else
            if (bytecodeSize != 0)
            {
                host().m_bytecode.resize(static_cast<std::size_t>(bytecodeSize));
            }
            else
            {
                host().m_zeroLengthBytecodeAllocation = ::operator new(0);
            }
#endif
        }
        catch (...)
        {
            reportCompileError(2, "data2", 0);
            std::exit(1);
        }
        m_physical.bytecodeEnd = static_cast<int>(bytecodeSize);
        syncPhysicalBackingPointers();
        if (bytecodeSize != 0)
            stream.read(bytecodeStorage(), bytecodeSize);

        for (int i = 0; i < m_physical.functionCount; ++i)
        {
            const script::LogicFunctionRecord* const rec = functionRecordAt(i);
            if (rec && std::strcmp(rec->name.c_str(), "main") == 0)
                m_physical.fallbackFunction = i;
        }

        stream.close();
        return 0;
    }

    int SCRIPT::writeExecutionStackToStream(BaseStream* stream)
    {
        for (int i = 0; i < m_physical.stackCount; ++i)
            executionStackStorage()[static_cast<std::size_t>(i)].writeToStream(stream);

        stream->write(&m_physical.functionCount, 4);
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = 0; i < m_physical.functionCount; ++i)
        {
            const script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            stream->write(rec.name.c_str(), static_cast<unsigned>(std::strlen(rec.name.c_str()) + 1u));
            stream->write(reinterpret_cast<const std::uint8_t*>(&rec) + 4u, 20u);
        }
        for (int i = 0; i < m_physical.functionCount; ++i)
        {
            const char* const text = records[static_cast<std::size_t>(i)].text.c_str();
            stream->write(text, static_cast<unsigned>(std::strlen(text) + 1u));
        }
        return m_physical.functionCount;
    }

    int SCRIPT::readExecutionStackFromStream(BaseStream* stream)
    {
        for (int i = 0; i < m_physical.stackCount; ++i)
            executionStackStorage()[static_cast<std::size_t>(i)].readFromStream(stream);

        int serializedFunctionCount = 0;
        stream->read(&serializedFunctionCount, 4);
        if (serializedFunctionCount > m_physical.functionCapacity)
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            script::LogicFunctionRecord* replacement = nullptr;
            try
            {
                replacement = allocateRetailScriptRecords<script::LogicFunctionRecord>(serializedFunctionCount);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", serializedFunctionCount);
            }
            script::LogicFunctionRecord* const oldRecords = functionRecordStorage();
            for (int i = 0; i < m_physical.functionCapacity; ++i)
                replacement[i].copyFrom(oldRecords[i]);
            if (oldRecords)
                script::destroyLogicFunctionRecordStorage(oldRecords, 3);
            m_physical.functionTableToken = retailPointerToken(replacement);
            m_physical.functionCapacity = serializedFunctionCount;
#else
            host().m_functionTable.reserveExact(serializedFunctionCount);
            syncPhysicalFunctionList();
#endif
        }
        m_physical.functionCount = serializedFunctionCount;

        script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = 0; i < m_physical.functionCount; ++i)
        {
            script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            rec.name.Read(stream);
            stream->read(reinterpret_cast<std::uint8_t*>(&rec) + 4u, 20u);
        }
        for (int i = 0; i < m_physical.functionCount; ++i)
        {
            script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            rec.text.ResetSharedEmptyWithoutRelease();
            rec.text.Read(stream);
        }
        return m_physical.functionCount;
    }

    void SCRIPT::clearExecutionStack()
    {
        m_physical.stackCapacity = 0;
        m_physical.stackCount = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
        script::StackObject* const records = executionStackStorage();
        if (records)
            msvcStringRecordDeletingDestructor(static_cast<void*>(records), 3);
#else
        host().m_executionStack.clear();
        script::RetailRawArray<script::StackObject>().swap(host().m_executionStack);
#endif
        m_physical.stackTableToken = 0;
    }

    void SCRIPT::pushExecutionInt(int value)
    {
        script::StackObject obj;
        obj.assignInt(value);
        appendExecutionStackObject(obj);
    }

    void SCRIPT::pushExecutionString(const STRING& value)
    {
        script::StackObject obj;
        obj.assignString(value);
        appendExecutionStackObject(obj);
    }

    int SCRIPT::executionStackCount() const
    {
        return m_physical.stackCount;
    }

    int SCRIPT::executionStackCapacity() const
    {
        return m_physical.stackCapacity;
    }

    const script::StackObject* SCRIPT::executionStackAt(int index) const
    {
        if (index < 0 || index >= m_physical.stackCount ||
            index >= m_physical.stackCapacity || executionStackStorage() == nullptr)
            return nullptr;
        return &executionStackStorage()[static_cast<std::size_t>(index)];
    }

    script::StackObject* SCRIPT::mutableExecutionStackAt(int index)
    {
        if (index < 0 || index >= m_physical.stackCount ||
            index >= m_physical.stackCapacity || executionStackStorage() == nullptr)
            return nullptr;
        return &executionStackStorage()[static_cast<std::size_t>(index)];
    }

    script::StackObject* SCRIPT::mutableExecutionStackStorageAt(int index)
    {
        return &executionStackStorage()[static_cast<std::size_t>(index)];
    }

    void SCRIPT::growExecutionStackForAppend()
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        auto* const stackList = reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable);
        if (stackList->count() >= stackList->capacity())
        {
            const int newCapacity = stackList->capacity() * 2 + 4;
            if (newCapacity > stackList->capacity())
                stackList->reserveExact(newCapacity);
        }
#else
        const int oldCapacity = m_physical.stackCapacity;
        if (m_physical.stackCount < oldCapacity)
            return;
        const int newCapacity = oldCapacity * 2 + 4;
        if (newCapacity <= oldCapacity)
            return;
        script::RetailRawArray<script::StackObject> newItems;
        try
        {
            newItems.resize(static_cast<std::size_t>(newCapacity));
        }
        catch (...)
        {
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
        }
        for (int i = 0; i < oldCapacity; ++i)
            newItems[static_cast<std::size_t>(i)].copyStorageFrom(executionStackStorage()[static_cast<std::size_t>(i)]);
        host().m_executionStack.swap(newItems);
        m_physical.stackTableToken = host().m_executionStack.empty()
            ? 0u : retailPointerToken(host().m_executionStack.data());
        m_physical.stackCapacity = newCapacity;
#endif
    }

    int SCRIPT::clearSpriteReferencesFromExecutionStack(SPRITE* sprite)
    {
        const int target = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu));
        int cleared = 0;
        for (int i = 0; i < m_physical.stackCount; ++i)
        {
            script::StackObject& value = executionStackStorage()[static_cast<std::size_t>(i)];
            if ((value.flags & script::STACK_OBJECT_REF) != 0 && value.intValue == target)
            {
                value.intValue = 0;
                value.flags = static_cast<std::uint8_t>(value.flags & ~script::STACK_OBJECT_REF);
                ++cleared;
            }
        }
        return cleared;
    }

    void SCRIPT::appendExecutionStackObject(const script::StackObject& value)
    {
        script::StackObject localCopy;
        localCopy.copyStorageFrom(value);
        appendExecutionStackRecord(localCopy.flags, localCopy.intValue, localCopy.text);
    }

    void SCRIPT::appendExecutionStackRecord(std::uint8_t flags, int value, const STRING& text)
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
            flags, value, text);
#else
        growExecutionStackForAppend();
        executionStackStorage()[static_cast<std::size_t>(m_physical.stackCount)].assignFields(
            flags, value, text);
        ++m_physical.stackCount;
#endif
    }

    int SCRIPT::functionCount() const
    {
        return m_physical.functionCount;
    }

    int SCRIPT::functionCapacity() const
    {
        return m_physical.functionCapacity;
    }

    const script::LogicFunctionList& SCRIPT::functionTable() const
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        static const script::LogicFunctionList empty;
        return empty;
#else
        return host().m_functionTable;
#endif
    }

    const script::LogicFunctionRecord* SCRIPT::functionRecordAt(int index) const noexcept
    {
        if (index < 0 || index >= m_physical.functionCount || functionRecordStorage() == nullptr)
            return nullptr;
        return functionRecordStorage() + index;
    }

    script::LogicFunctionRecord* SCRIPT::mutableFunctionRecordAt(int index) noexcept
    {
        if (index < 0 || index >= m_physical.functionCount || functionRecordStorage() == nullptr)
            return nullptr;
        return functionRecordStorage() + index;
    }

    int SCRIPT::findFunctionRecordByName(const STRING& name) const noexcept
    {
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        if (!records)
            return -1;
        for (int i = m_physical.functionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                return i;
        }
        return -1;
    }

    int SCRIPT::defineCount() const
    {
        return m_physical.defineCount;
    }

    int SCRIPT::defineCapacity() const
    {
        return m_physical.defineCapacity;
    }

    const script::RetailRawArray<ScriptDefinePairRecord>& SCRIPT::defineTable() const
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        static const script::RetailRawArray<ScriptDefinePairRecord> empty;
        return empty;
#else
        return host().m_defines;
#endif
    }

    const STRING& SCRIPT::scriptFile() const
    {
        return scriptFileStorage();
    }

    const script::RetailByteBuffer& SCRIPT::bytecodeBuffer() const
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        static const script::RetailByteBuffer empty;
        return empty;
#else
        return host().m_bytecode;
#endif
    }

    int SCRIPT::bytecodeEnd() const
    {
        return m_physical.bytecodeEnd;
    }

    int SCRIPT::sourceCursorOffset() const
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        if (m_physical.sourceBufferToken == 0u || m_physical.sourceCursor == 0u)
            return 0;
        return static_cast<int>(m_physical.sourceCursor - m_physical.sourceBufferToken);
#else
        return host().m_portableSourceCursorOffset;
#endif
    }

    int SCRIPT::sourceEndOffset() const
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        if (m_physical.sourceBufferToken == 0u || m_physical.sourceEnd == 0u)
            return 0;
        return static_cast<int>(m_physical.sourceEnd - m_physical.sourceBufferToken);
#else
        return host().m_portableSourceEndOffset;
#endif
    }

    const script::RetailByteBuffer& SCRIPT::sourceBuffer() const
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        static const script::RetailByteBuffer empty;
        return empty;
#else
        return host().m_sourceBuffer;
#endif
    }

    int SCRIPT::sourceLineNumber() const
    {
        return m_physical.sourceLine;
    }

    int SCRIPT::conditionalDepth() const
    {
        return m_physical.conditionalDepth;
    }

    int SCRIPT::fallbackFunctionIndex() const
    {
        return m_physical.fallbackFunction;
    }

    int SCRIPT::parseMode() const
    {
        return m_physical.parseMode;
    }

    void destroyScriptDefinePair(ScriptDefinePairRecord* self) noexcept
    {
        destroyStringStorage(self->value);
        destroyStringStorage(self->name);
    }

    void* scriptDefinePairDeletingDestructor(ScriptDefinePairRecord* self, unsigned char flags) noexcept
    {
#if defined(_WIN32) && defined(_M_IX86)
        if ((flags & 0x02u) != 0)
        {
            std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(self) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
                destroyScriptDefinePair(self + (i - 1u));
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(header));
            return header;
        }
#endif

        destroyScriptDefinePair(self);
        if ((flags & 0x01u) != 0)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SCRIPT::resetScriptVmState()
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        if (bytecodeStorage())
            ::operator delete(static_cast<void*>(bytecodeStorage()));
        m_physical.bytecodeBufferToken = 0u;
        if (sourceStorage())
            ::operator delete(static_cast<void*>(sourceStorage()));
        m_physical.sourceBufferToken = 0u;
        m_physical.sourceCursor = 0u;
#else
        if (host().m_zeroLengthBytecodeAllocation)
        {
            ::operator delete(host().m_zeroLengthBytecodeAllocation);
            host().m_zeroLengthBytecodeAllocation = nullptr;
        }
        host().m_bytecode.clear();
        script::RetailByteBuffer().swap(host().m_bytecode);
        m_physical.bytecodeBufferToken = 0;
        host().m_sourceBuffer.clear();
        script::RetailByteBuffer().swap(host().m_sourceBuffer);
        setSourceCursorOffset(0);
        setSourceEndOffset(0);
        m_physical.sourceBufferToken = 0;
#endif

        clearExecutionStack();

        clearFunctionTable();

        clearDefines();

        m_physical.bytecodeEnd = 0;
        m_physical.conditionalDepth = 0;
        m_physical.fallbackFunction = -1;
        m_physical.sourceCursor = 0;
        m_physical.parseMode = 0;
    }

    void SCRIPT::prepareSourceCompiler(const STRING& scriptFile, BaseStream* stream, std::uint32_t sourceSize)
    {
        (void)scriptFile;
        try
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            m_physical.bytecodeBufferToken = retailPointerToken(::operator new(TemporaryBytecodeCapacity));
#else
            host().m_bytecode.clear();
            host().m_bytecode.resize(TemporaryBytecodeCapacity);
#endif
        }
        catch (...)
        {
            reportCompileError(2, "data", 0);
            std::exit(1);
        }
        m_physical.bytecodeEnd = 0;
        syncPhysicalBackingPointers();

        try
        {
            const std::uint32_t allocationSize = sourceSize + static_cast<std::uint32_t>(SourceBufferPadding);
#if defined(_MSC_VER) && defined(_M_IX86)
            m_physical.sourceBufferToken = retailPointerToken(::operator new(static_cast<std::size_t>(allocationSize)));
#else
            host().m_sourceBuffer.clear();
            host().m_sourceBuffer.resize(static_cast<std::size_t>(allocationSize));
#endif
        }
        catch (...)
        {
            reportCompileError(2, "ini", 0);
            std::exit(1);
        }
        syncPhysicalBackingPointers();
        setSourceCursorOffset(static_cast<int>(SourcePayloadOffset));
        const std::uint32_t sourceEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + sourceSize;
        setSourceEndOffset(static_cast<std::int32_t>(sourceEndOffset));
        if (sourceSize != 0)
        {
            stream->read(sourceStorage() + SourcePayloadOffset,
                static_cast<unsigned>(sourceSize));
        }
        syncPhysicalSourcePointers();

        if (executionStackCapacity() < static_cast<int>(InitialListCapacity))
        {
            try
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                script::StackObject* const records = allocateRetailScriptRecords<script::StackObject>(static_cast<int>(InitialListCapacity));
                m_physical.stackTableToken = retailPointerToken(records);
#else
                host().m_executionStack.resize(InitialListCapacity);
#endif
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                    static_cast<int>(InitialListCapacity));
            }
            m_physical.stackCapacity = static_cast<int>(InitialListCapacity);
#if !defined(_MSC_VER) || !defined(_M_IX86)
            syncPhysicalBackingPointers();
#endif
        }
        m_physical.stackCount = 0;

        if (functionCapacity() < static_cast<int>(InitialListCapacity))
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            try
            {
                script::LogicFunctionRecord* const records = allocateRetailScriptRecords<script::LogicFunctionRecord>(static_cast<int>(InitialListCapacity));
                if (functionRecordStorage())
                    script::destroyLogicFunctionRecordStorage(functionRecordStorage(), 3);
                m_physical.functionTableToken = retailPointerToken(records);
                m_physical.functionCapacity = static_cast<int>(InitialListCapacity);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(InitialListCapacity));
            }
#else
            host().m_functionTable.reserveExact(InitialListCapacity);
            syncPhysicalFunctionList();
#endif
        }

        m_physical.sourceLine = 0;
        m_physical.conditionalDepth = 0;
        m_physical.parseMode = 0;
    }

    void SCRIPT::clearFunctionTable()
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        if (functionRecordStorage())
            script::destroyLogicFunctionRecordStorage(functionRecordStorage(), 3);
        m_physical.functionCount = 0;
        m_physical.functionCapacity = 0;
        m_physical.functionTableToken = 0u;
#else
        host().m_functionTable.clearRecords();
        syncPhysicalFunctionList();
#endif
    }

    void SCRIPT::appendFunctionRecord(const STRING& name, std::uint8_t flags, const STRING& text, int bytecodeStart0C, int stackBase10, int argCount14)
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        if (m_physical.functionCount >= m_physical.functionCapacity)
        {
            const int oldCapacity = m_physical.functionCapacity;
            const int newCapacity = oldCapacity * 2 + 4;
            script::LogicFunctionRecord* replacement = nullptr;
            try
            {
                replacement = allocateRetailScriptRecords<script::LogicFunctionRecord>(newCapacity);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
            }
            script::LogicFunctionRecord* const oldRecords = functionRecordStorage();
            for (int i = 0; i < oldCapacity; ++i)
                replacement[i].copyFrom(oldRecords[i]);
            if (oldRecords)
                script::destroyLogicFunctionRecordStorage(oldRecords, 3);
            m_physical.functionTableToken = retailPointerToken(replacement);
            m_physical.functionCapacity = newCapacity;
        }
        script::LogicFunctionRecord temp;
        temp.name = name;
        temp.flags = flags;
        temp.text = text;
        temp.value0 = bytecodeStart0C;
        temp.value1 = stackBase10;
        temp.value2 = argCount14;
        functionRecordStorage()[static_cast<std::size_t>(m_physical.functionCount)].copyFrom(temp);
        ++m_physical.functionCount;
#else
        host().m_functionTable.append(name, flags, text, bytecodeStart0C, stackBase10, argCount14);
        syncPhysicalFunctionList();
#endif
    }

    void SCRIPT::clearDefines()
    {
        m_physical.defineCount = 0;
        m_physical.defineCapacity = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
        ScriptDefinePairRecord* const records = defineRecordStorage();
        if (records)
            scriptDefinePairDeletingDestructor(records, 3);
#else
        host().m_defines.clear();
        script::RetailRawArray<ScriptDefinePairRecord>().swap(host().m_defines);
#endif
        m_physical.defineTableToken = 0;
    }

    int SCRIPT::findDefine(const STRING& name) const
    {
        // compileNextSourceItem #define/#undef scans the define table backwards.
        const ScriptDefinePairRecord* const records = defineRecordStorage();
        for (int i = m_physical.defineCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                return i;
        }
        return -1;
    }

    int SCRIPT::addOrReplaceDefine(const STRING& name, const STRING& value)
    {
        const int existing = findDefine(name);
        if (existing >= 0)
        {
            defineRecordStorage()[static_cast<std::size_t>(existing)].value = value;
            return existing;
        }

        if (defineCount() >= m_physical.defineCapacity)
        {
            const int oldCapacity = m_physical.defineCapacity;
            const int capacity = oldCapacity * 2 + 4;
            try
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                ScriptDefinePairRecord* const oldRecords = defineRecordStorage();
                ScriptDefinePairRecord* const replacement = allocateRetailScriptRecords<ScriptDefinePairRecord>(capacity);
                for (int i = 0; i < oldCapacity; ++i)
                {
                    replacement[i].name = oldRecords[i].name;
                    replacement[i].value = oldRecords[i].value;
                }
                if (oldRecords)
                    scriptDefinePairDeletingDestructor(oldRecords, 3);
                m_physical.defineTableToken = retailPointerToken(replacement);
#else
                host().m_defines.reserve(static_cast<std::size_t>(capacity));
#endif
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);
            }
            m_physical.defineCapacity = capacity;
#if !defined(_MSC_VER) || !defined(_M_IX86)
            syncPhysicalDefineList();
#endif
        }

        ScriptDefinePairRecord rec;
        rec.name = name;
        rec.value = value;
#if defined(_MSC_VER) && defined(_M_IX86)
        ScriptDefinePairRecord& dst = defineRecordStorage()[static_cast<std::size_t>(m_physical.defineCount)];
        dst.name = rec.name;
        dst.value = rec.value;
        ++m_physical.defineCount;
#else
        host().m_defines.push_back(rec);
        syncPhysicalDefineList();
#endif
        return m_physical.defineCount - 1;
    }

    int SCRIPT::undefine(const STRING& name)
    {
        const int index = findDefine(name);
        if (index < 0)
            return -1;

#if defined(_MSC_VER) && defined(_M_IX86)
        ScriptDefinePairRecord* const records = defineRecordStorage();
        for (int i = index; i + 1 < m_physical.defineCount; ++i)
        {
            records[i].name = records[i + 1].name;
            records[i].value = records[i + 1].value;
        }
        if (m_physical.defineCount > 0)
        {
            records[m_physical.defineCount - 1].name = STRING();
            records[m_physical.defineCount - 1].value = STRING();
            --m_physical.defineCount;
        }
        if (m_physical.defineCount == 0)
            clearDefines();
#else
        host().m_defines.erase(host().m_defines.begin() + index);
        if (host().m_defines.empty())
            clearDefines();
        else
            syncPhysicalDefineList();
#endif
        return index;
    }

    int SCRIPT::rewriteDefineMacro(int tokenStartOffset, int tokenLength)
    {
        std::string token(reinterpret_cast<const char*>(sourceStorage() + tokenStartOffset),
            static_cast<std::size_t>(tokenLength));

        int found = -1;
        const ScriptDefinePairRecord* const records = defineRecordStorage();
        for (int i = m_physical.defineCount - 1; i >= 0; --i)
        {
            if (records[static_cast<std::size_t>(i)].name.str() == token)
            {
                found = i;
                break;
            }
        }
        if (found < 0)
            return -1;

        const std::string value = records[static_cast<std::size_t>(found)].value.str();
        const int valueLength = static_cast<int>(value.size());
        const int newCursor = tokenStartOffset + tokenLength - valueLength;
        setSourceCursorOffset(newCursor);
        if (valueLength > 0)
        {
            std::copy(value.begin(), value.end(), sourceStorage() + newCursor);
        }
        return found;
    }

    std::uint8_t SCRIPT::sourceByteAtCursor() const
    {
        return sourceStorage()[static_cast<std::size_t>(sourceCursorOffset())];
    }

    void SCRIPT::setSourceCursorOffset(int offset)
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        m_physical.sourceCursor = m_physical.sourceBufferToken == 0u
            ? 0u
            : m_physical.sourceBufferToken + static_cast<std::uint32_t>(offset);
#else
        host().m_portableSourceCursorOffset = offset;
        syncPhysicalSourcePointers();
#endif
    }

    void SCRIPT::reportCompileError(int errorCode, const char* detailText, int detailValue)
    {
        logFileLoggerResourceError(g_fileLogger,
            "LOGIC '%s' line %i",
            errorCode,
            detailText,
            detailValue,
            scriptFileStorage().c_str(),
            m_physical.sourceLine + 1);

        if (m_physical.sourceCursor == 0)
            return;

        char window[61];
        const int start = sourceCursorOffset() - 30;
        for (int i = 0; i < 60; ++i)
        {
            unsigned char c = sourceStorage()[static_cast<std::size_t>(start + i)];
            if (c == '\n' || c == '\r' || c == '\t')
                c = '?';
            window[i] = static_cast<char>(c);
        }
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);

        for (int i = 0; i < 60; ++i)
            window[i] = (i == 30) ? '^' : ' ';
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);
    }

    namespace
    {
        int retailCtypeInput41BC50(unsigned char c) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return static_cast<int>(static_cast<signed char>(c));
#else
            // Portable builds keep the standard ctype precondition; Win32/x86
            // preserves the signed-byte runtime contract.
            return static_cast<int>(c);
#endif
        }

        bool isIdentifierStart(unsigned char c)
        {
            return std::isalpha(retailCtypeInput41BC50(c)) != 0 || c == '_';
        }

        bool isIdentifierChar(unsigned char c)
        {
            return std::isalnum(retailCtypeInput41BC50(c)) != 0 || c == '_';
        }

        bool isScriptWhitespace(unsigned char c)
        {
            return std::isspace(retailCtypeInput41BC50(c)) != 0;
        }
    }

    int SCRIPT::skipTriviaAndPreprocess()
    {
        int skipDepth = 0;
        int commentState = 0; // 0 none, 1 //, 2 /* */

        while (sourceCursorOffset() < sourceEndOffset())
        {
            if (commentState != 0)
            {
                const unsigned char c = sourceByteAtCursor();
                if (commentState == 1)
                {
                    if (c == '\n')
                        commentState = 0;
                }
                else if (commentState == 2)
                {
                    if (c == '/' &&
                        sourceStorage()[static_cast<std::size_t>(sourceCursorOffset() - 1)] == '*')
                        commentState = 0;
                }
                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (c == '\n')
                    ++m_physical.sourceLine;
                continue;
            }

            unsigned char c = sourceByteAtCursor();

            if (isIdentifierStart(c) && m_physical.parseMode == 0)
            {
                const int tokenStart = sourceCursorOffset();
                int tokenLength = 0;
                while (isIdentifierChar(sourceStorage()[static_cast<std::size_t>(tokenStart + tokenLength)]))
                {
                    if (tokenLength >= 0x0FFF)
                    {
                        reportCompileError(10, "Very long name", 0);
                        std::exit(1);
                    }
                    ++tokenLength;
                }
                if (rewriteDefineMacro(tokenStart, tokenLength) >= 0)
                    continue;
            }

            if (c == '#')
            {
                const char* cur = reinterpret_cast<const char*>(sourceStorage()) + sourceCursorOffset();

                if (std::strncmp(cur, "#ifdef", 6) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 6);
                    ++m_physical.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_physical.parseMode = 1;
                        readIdentifier(name);
                        m_physical.parseMode = 0;
                        if (findFunctionRecordByName(name) < 0 && findDefine(name) < 0)
                            skipDepth = m_physical.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#ifndef", 7) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 7);
                    ++m_physical.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_physical.parseMode = 1;
                        readIdentifier(name);
                        m_physical.parseMode = 0;
                        if (findFunctionRecordByName(name) >= 0 || findDefine(name) >= 0)
                            skipDepth = m_physical.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#endif", 6) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 6);
                    if (skipDepth == m_physical.conditionalDepth)
                        skipDepth = 0;
                    --m_physical.conditionalDepth;
                    if (m_physical.conditionalDepth < 0)
                        reportCompileError(10, "#endif without #ifdef", 0);
                    continue;
                }

                if (std::strncmp(cur, "#else", 5) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 5);
                    if (skipDepth == 0)
                    {
                        if (m_physical.conditionalDepth > 0)
                            skipDepth = m_physical.conditionalDepth;
                    }
                    else if (skipDepth == m_physical.conditionalDepth)
                        skipDepth = 0;

                    if (m_physical.conditionalDepth <= 0)
                        reportCompileError(10, "#else without #ifdef", 0);
                    continue;
                }
            }

            if (c == '/')
            {
                const std::size_t nextOff = static_cast<std::size_t>(sourceCursorOffset() + 1);
                const unsigned char next = sourceStorage()[nextOff];
                if (next == '/')
                {
                    commentState = 1;
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    continue;
                }
                if (next == '*')
                {
                    commentState = 2;
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    continue;
                }
            }

            if (c == '?')
            {
                reportCompileError(10, "?: not supported in this version", 0);
                std::exit(1);
            }

            if (skipDepth == 0 && !isScriptWhitespace(c) && c != 0)
                return 0;

            setSourceCursorOffset(sourceCursorOffset() + 1);
            if (c == '\n')
                ++m_physical.sourceLine;
        }

        if (m_physical.conditionalDepth > 0)
            reportCompileError(10, "#ifdef without #endif", m_physical.conditionalDepth);
        return 1;
    }

    int SCRIPT::requireSourceToken()
    {
        if (skipTriviaAndPreprocess())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }
        return 0;
    }

    int SCRIPT::readSourceLine(STRING& outLine)
    {
        if (requireSourceToken())
            return 1;

        std::string line;
        while (true)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '\n' || c == '\r')
                break;
            if (line.size() >= 4095)
            {
                reportCompileError(10, "Very long line", 0);
                std::exit(1);
            }
            line.push_back(static_cast<char>(c));
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }

        if (line.empty())
        {
            reportCompileError(10, "empty line", 0);
            std::exit(1);
        }

        const std::size_t comment = line.find("//");
        if (comment != std::string::npos)
            line.erase(comment);

        const std::size_t last = line.find_last_not_of(" \n\r\t");
        if (last == std::string::npos)
            line.clear();
        else
            line.erase(last + 1);

        outLine = STRING(line);
        return requireSourceToken();
    }

    int SCRIPT::readIdentifier(STRING& outName)
    {
        if (requireSourceToken())
            return 0;
        std::string name;
        while (true)
        {
            const unsigned char c = sourceByteAtCursor();
            if (!isIdentifierChar(c))
                break;
            if (name.size() >= 4095)
            {
                reportCompileError(10, "Very long name", 0);
                std::exit(1);
            }
            name.push_back(static_cast<char>(c));
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        outName = STRING(name);
        if (name.empty())
        {
            reportCompileError(4, "name", 0);
            std::exit(1);
        }
        requireSourceToken();
        return static_cast<int>(name.size());
    }

    int SCRIPT::matchToken(const char* token)
    {
        const std::size_t len = std::strlen(token);
        requireSourceToken();
        const char* cur = reinterpret_cast<const char*>(sourceStorage() + sourceCursorOffset());
        if (std::strncmp(cur, token, len) != 0)
            return 0;
        const unsigned char first = static_cast<unsigned char>(token[0]);
        const unsigned char after = static_cast<unsigned char>(cur[len]);
        if ((std::isalpha(first) || first == '#') && isIdentifierChar(after))
            return 0;
        setSourceCursorOffset(sourceCursorOffset() + static_cast<int>(len));
        skipTriviaAndPreprocess();
        return 1;
    }

    int SCRIPT::requireToken(const char* token)
    {
        if (matchToken(token))
            return 1;
        reportCompileError(13, token, 0);
        std::exit(1);
    }

    int SCRIPT::parseConstantIntExpression()
    {
        requireSourceToken();
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileExpression();

        if (bytecodeStorage()[static_cast<std::size_t>(bytecodeStart)] == 1 &&
            m_physical.bytecodeEnd - bytecodeStart == 5)
        {
            int value = 0;
            std::memcpy(&value,
                bytecodeStorage() + static_cast<std::size_t>(bytecodeStart + 1),
                sizeof(value));
            m_physical.bytecodeEnd -= 5;
            return value;
        }

        reportCompileError(4, "constant int value", 0);
        std::exit(1);
    }


    int SCRIPT::readQuotedStringLiteral(char* outText)
    {
        char* dst = outText;
        if (sourceByteAtCursor() != '"')
            return 0;

        setSourceCursorOffset(sourceCursorOffset() + 1);
        if (sourceByteAtCursor() != '"')
        {
            while (true)
            {
                if (sourceCursorOffset() >= sourceEndOffset())
                    break;

                unsigned char c = sourceByteAtCursor();
                if (c == '\\')
                {
                    const int nextOffset = sourceCursorOffset() + 1;
                    const unsigned char next = sourceStorage()[static_cast<std::size_t>(nextOffset)];

                    if (next == '\r')
                    {
                        const int afterCrOffset = sourceCursorOffset() + 2;
                        const unsigned char afterCr = sourceStorage()[static_cast<std::size_t>(afterCrOffset)];
                        if (afterCr == '\n')
                        {
                            setSourceCursorOffset(afterCrOffset);
                            ++m_physical.sourceLine;
                        }
                        else
                        {
                            setSourceCursorOffset(nextOffset);
                            *dst++ = static_cast<char>(next);
                        }
                    }
                    else if (next == '\n')
                    {
                        setSourceCursorOffset(nextOffset);
                        ++m_physical.sourceLine;
                    }
                    else if (next == 'n')
                    {
                        *dst++ = '\n';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else if (next == 'r')
                    {
                        *dst++ = '\r';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else
                    {
                        setSourceCursorOffset(nextOffset);
                        *dst++ = static_cast<char>(next);
                    }
                }
                else
                {
                    *dst++ = static_cast<char>(c);
                }

                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (sourceByteAtCursor() == '"')
                    break;
            }
        }

        const int quoteOffset = sourceCursorOffset();
        setSourceCursorOffset(sourceCursorOffset() + 1);
        if (quoteOffset >= sourceEndOffset())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }

        *dst++ = '\0';
        return static_cast<int>(dst - outText);
    }

    int SCRIPT::setLastFunctionElementCount(int argCount)
    {
        if (m_physical.functionCount > 0 && functionRecordStorage())
            functionRecordStorage()[static_cast<std::size_t>(m_physical.functionCount - 1)].value2 = argCount;
        return functionCount() * 3;
    }


    void SCRIPT::compileIntDeclaration()
    {
        int declaredCount = 0;
        int rawFlags = 0;
        STRING name;

        requireSourceToken();
        if (sourceByteAtCursor() == '*')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            rawFlags = script::STACK_OBJECT_DYNAMIC;
        }

        readIdentifier(name);

        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = functionCount() - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "int redefinition '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t functionScratch10;
        std::uint32_t functionScratch14;
#else
        std::uint32_t functionScratch10 = 0;
        std::uint32_t functionScratch14 = 0;
#endif
        appendFunctionRecord(
            name, 1, STRING(), stackBase,
            static_cast<int>(core::retailReadStackDword(&functionScratch10)),
            static_cast<int>(core::retailReadStackDword(&functionScratch14)));

        auto appendIntStackRecord = [&](std::uint8_t extraFlags, int value, bool hasPayload) -> int
        {
            script::StackObject temp;
            temp.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), 0, STRING());
            appendExecutionStackObject(temp);
            const int index = executionStackCount() - 1;
            script::StackObject& record = executionStackStorage()[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>(record.flags | extraFlags);
            if (hasPayload)
            {
                record.intValue = value;
                record.flags = static_cast<std::uint8_t>(record.flags | script::STACK_OBJECT_HAS_PAYLOAD);
            }
            return index;
        };

        auto markUninitialized = [&](int index)
        {
            script::StackObject& record = executionStackStorage()[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>(record.flags | static_cast<std::uint8_t>(rawFlags) | script::STACK_OBJECT_CHAR_WRITE);
        };

        auto markInitialized = [&](int index, int value)
        {
            script::StackObject& record = executionStackStorage()[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>((record.flags & ~script::STACK_OBJECT_CHAR_WRITE) | script::STACK_OBJECT_HAS_PAYLOAD);
            record.intValue = value;
        };

        if (matchToken("["))
        {
            rawFlags |= script::STACK_OBJECT_ARRAY;
            if (sourceByteAtCursor() == ']')
            {
                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (!matchToken("="))
                {
                    reportCompileError(10, "for [] need initialisation", 0);
                    std::exit(1);
                }
                requireToken("{");
                for (;;)
                {
                    const int index = appendIntStackRecord(static_cast<std::uint8_t>(rawFlags), 0, false);
                    const int value = parseConstantIntExpression();
                    markInitialized(index, value);
                    ++declaredCount;
                    if (!matchToken(","))
                        break;
                }
                requireToken("}");
                setLastFunctionElementCount(declaredCount);
                return;
            }

            declaredCount = parseConstantIntExpression();
            requireToken("]");
        }
        else
        {
            declaredCount = 1;
        }

        if (declaredCount > 0)
        {
            for (int i = 0; i < declaredCount; ++i)
            {
                const int index = appendIntStackRecord(0, 0, false);
                markUninitialized(index);
            }
        }

        if (matchToken("="))
        {
            if ((rawFlags & script::STACK_OBJECT_ARRAY) != 0)
            {
                requireToken("{");
                int initIndex = 0;
                if (declaredCount > 0)
                {
                    for (;;)
                    {
                        if (initIndex >= declaredCount)
                            break;
                        const int stackIndex = executionStackCount() - declaredCount + initIndex;
                        const int value = parseConstantIntExpression();
                        markInitialized(stackIndex, value);
                        ++initIndex;
                        if (!matchToken(","))
                            break;
                        if (initIndex >= declaredCount)
                        {
                            reportCompileError(10, "too many initializers", 0);
                            std::exit(1);
                        }
                    }
                }
                else
                {
                    reportCompileError(10, "too many initializers", 0);
                    std::exit(1);
                }
                requireToken("}");
            }
            else
            {
                const int stackIndex = executionStackCount() - 1;
                const int value = parseConstantIntExpression();
                markInitialized(stackIndex, value);
            }
        }

        setLastFunctionElementCount(declaredCount);
    }


    void SCRIPT::compileStringDeclaration()
    {
        int declaredCount = 0;
        int rawFlags = 0;
        STRING name;

        readIdentifier(name);

        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = functionCount() - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "string redifinition '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
        }

        if (matchToken("["))
        {
            rawFlags = script::STACK_OBJECT_ARRAY;
            declaredCount = parseConstantIntExpression();
            requireToken("]");
        }
        else
        {
            declaredCount = 1;
        }

        const int stackBase = executionStackCount();
#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t functionScratch10;
        std::uint32_t functionScratch14;
#else
        std::uint32_t functionScratch10 = 0;
        std::uint32_t functionScratch14 = 0;
#endif
        appendFunctionRecord(
            name, 1, STRING(), stackBase,
            static_cast<int>(core::retailReadStackDword(&functionScratch10)),
            static_cast<int>(core::retailReadStackDword(&functionScratch14)));

        auto appendStringStackRecord = [&](std::uint8_t extraFlags) -> int
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::uint32_t stringScratch04;
#else
            std::uint32_t stringScratch04 = 0;
#endif
            appendExecutionStackRecord(
                static_cast<std::uint8_t>(script::STACK_OBJECT_STRING),
                static_cast<int>(core::retailReadStackDword(&stringScratch04)),
                STRING());
            const int index = executionStackCount() - 1;
            script::StackObject& record = executionStackStorage()[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>(record.flags | extraFlags);
            return index;
        };

        if (declaredCount > 0)
        {
            for (int i = 0; i < declaredCount; ++i)
                appendStringStackRecord(static_cast<std::uint8_t>(rawFlags));
        }

        if (matchToken("="))
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            char literal[4096];
#else
            char literal[4096] = {};
#endif
            readQuotedStringLiteral(literal);

            const int stackIndex = executionStackCount() - 1;
            script::StackObject& record = executionStackStorage()[static_cast<std::size_t>(stackIndex)];
            record.text = STRING(literal);
            record.flags = static_cast<std::uint8_t>((record.flags | script::STACK_OBJECT_HAS_PAYLOAD) & ~script::STACK_OBJECT_CHAR_WRITE);
        }

        setLastFunctionElementCount(declaredCount);
    }


    int SCRIPT::compilePrimaryExpression()
    {
        std::uint8_t unaryOpcode = 0;
        std::uint8_t prefixOpcode = script::opcodeValue(script::VmOpcode::ReadVariable);

        const int cursorBeforeUnary = sourceCursorOffset();
        const bool nextIsMinus =
            sourceStorage()[static_cast<std::size_t>(cursorBeforeUnary + 1)] == '-';
        if (!nextIsMinus && matchToken("-"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::Negate);
        else if (matchToken("~"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::BitwiseNot);
        else if (matchToken("!"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::LogicalNot);

        if (matchToken("--"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreDecrement);
        else if (matchToken("++"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreIncrement);
        else if (matchToken("&"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::AddressOf);

        auto canWrite = [&](int byteCount) -> bool
        {
            (void)byteCount;
            return true;
        };
        auto emitByte = [&](std::uint8_t value) -> bool
        {
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd)] = value;
            ++m_physical.bytecodeEnd;
            return true;
        };
        auto emitIntObject = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_physical.bytecodeEnd);
            bytecodeStorage()[off] = script::opcodeValue(script::VmOpcode::PushInteger);
            std::memcpy(bytecodeStorage() + off + 1, &value, sizeof(value));
            m_physical.bytecodeEnd += 5;
            return true;
        };
        auto emitIntPayload = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_physical.bytecodeEnd);
            std::memcpy(bytecodeStorage() + off, &value, sizeof(value));
            m_physical.bytecodeEnd += 4;
            return true;
        };
        auto emitByteAndIntPayload = [&](std::uint8_t opcode, int value) -> bool
        {
            if (!emitByte(opcode))
                return false;
            return emitIntPayload(value);
        };
        auto emitTrailingUnary = [&]() -> bool
        {
            if (unaryOpcode == 0)
                return true;
            return emitByte(static_cast<std::uint8_t>(unaryOpcode));
        };

        
        const unsigned char current = sourceByteAtCursor();
        if (std::isdigit(current))
        {
            STRING token;
            readIdentifier(token);
            int value = 0;
            std::sscanf(token.c_str(), "%i", &value);
            if (unaryOpcode == script::opcodeValue(script::VmOpcode::Negate))
            {
                value = -value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
            {
                value = ~value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::LogicalNot))
            {
                value = value ? 0 : 1;
                unaryOpcode = 0;
            }
            if (!emitIntObject(value))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '"')
        {
            if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                return 1;
            const std::size_t outOff = static_cast<std::size_t>(m_physical.bytecodeEnd);
            const int written = readQuotedStringLiteral(reinterpret_cast<char*>(bytecodeStorage() + outOff));
            m_physical.bytecodeEnd += written;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '\'')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            const signed char ch = static_cast<signed char>(sourceByteAtCursor());
            setSourceCursorOffset(sourceCursorOffset() + 1);
            if (!emitIntObject(static_cast<int>(ch)))
                return 1;
            if (sourceByteAtCursor() != '\'')
            {
                reportCompileError(13, "second '", 0);
                std::exit(1);
            }
            setSourceCursorOffset(sourceCursorOffset() + 1);
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("sizeof"))
        {
            if (!matchToken("("))
            {
                reportCompileError(13, "'(' for sizeof", 0);
                std::exit(1);
            }

            if (!emitByte(script::opcodeValue(script::VmOpcode::PushInteger)))
                return 1;

            int sizeofValue = 4;
            if (!matchToken("int") && !matchToken("string"))
            {
                STRING sizeofName;
                readIdentifier(sizeofName);
                int foundIndex = -1;
                const int count = functionCount();
                const script::LogicFunctionRecord* const records = functionRecordStorage();
                for (int i = count - 1; i >= 0; --i)
                {
                    if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), sizeofName.c_str()) == 0)
                    {
                        foundIndex = i;
                        break;
                    }
                }
                if (foundIndex < 0 || records[static_cast<std::size_t>(foundIndex)].flags != 1)
                {
                    reportCompileError(4, "sizeof parameter", 0);
                    std::exit(1);
                }
                sizeofValue = records[static_cast<std::size_t>(foundIndex)].value2 << 2;
            }

            if (!emitIntPayload(sizeofValue))
                return 1;
            requireToken(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("static"))
        {
            if (matchToken("int"))
            {
                do
                {
                    compileIntDeclaration();
                }
                while (matchToken(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            if (matchToken("string"))
            {
                do
                {
                    compileStringDeclaration();
                }
                while (matchToken(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (matchToken("int"))
        {
            do
            {
                compileIntDeclaration();
            }
            while (matchToken(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("string"))
        {
            do
            {
                compileStringDeclaration();
            }
            while (matchToken(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("return"))
        {
            compileExpression();
            if (!emitByte(script::opcodeValue(script::VmOpcode::Return)))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("("))
        {
            compileExpression();
            requireToken(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (std::isalpha(current))
        {
            auto sourceCursorChar = [&]() -> int
            {
                if (sourceCursorOffset() < 0 || sourceCursorOffset() >= sourceEndOffset())
                    return -1;
                return sourceByteAtCursor();
            };
            auto emitFormattedPrimaryDiagnostic = [&](int code, const char* fmt, const STRING& name)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), fmt, name.c_str());
                reportCompileError(code, buffer, 0);
                std::exit(1);
            };

            STRING name;
            readIdentifier(name);

            const int foundIndex = findFunctionRecordByName(name);
            if (foundIndex < 0)
            {
                if (sourceCursorChar() == ':')
                {
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    appendFunctionRecord(name, 7, STRING(), m_physical.bytecodeEnd, 0, 0);
                    return compilePrimaryExpression();
                }
                emitFormattedPrimaryDiagnostic(10, "Undeclared identifier '%s'", name);
            }

            const script::LogicFunctionRecord* const records = functionRecordStorage();
            const std::uint8_t flags = records[static_cast<std::size_t>(foundIndex)].flags;
            if (flags == 8)
            {
                if (sourceCursorChar() != ':')
                    emitFormattedPrimaryDiagnostic(10, "Incorrect use label '%s'", name);
                setSourceCursorOffset(sourceCursorOffset() + 1);
                int patchOffset = 0;
                script::LogicFunctionRecord* const label = mutableFunctionRecordAt(foundIndex);
                if (label)
                {
                    patchOffset = label->value0;
                    label->flags = 7;
                    label->value0 = m_physical.bytecodeEnd;
                    const int delta = m_physical.bytecodeEnd - patchOffset;
                    std::memcpy(bytecodeStorage() + patchOffset, &delta, sizeof(delta));
                }
                return compilePrimaryExpression();
            }

            if (flags == 7)
            {
                if (sourceCursorChar() == ':')
                    emitFormattedPrimaryDiagnostic(10, "Label redefinition '%s'", name);
                emitFormattedPrimaryDiagnostic(10, "Incorrect use label '%s'", name);
            }

            if (flags == 2)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].value1;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].value2;
                const int externOpcode = records[static_cast<std::size_t>(foundIndex)].value0;

                requireToken("(");
                int parsedCount = 0;
                if (!matchToken(")"))
                {
                    for (;;)
                    {
                        compileExpression();
                        matchToken(",");
                        ++parsedCount;
                        if (matchToken(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;
                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::ReadVariable), parameterBase + parsedCount))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "extern function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByte(static_cast<std::uint8_t>(externOpcode)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 3)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].value1;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].value2;
                const int functionOffset = records[static_cast<std::size_t>(foundIndex)].value0;

                requireToken("(");
                int parsedCount = 0;
                if (!matchToken(")"))
                {
                    for (;;)
                    {
                        compileExpression();
                        matchToken(",");
                        if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                            return 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                            return 1;
                        ++parsedCount;
                        if (matchToken(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;

                    if ((defaultRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        const std::size_t textLen = std::strlen(defaultRecord->text.c_str()) + 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                            return 1;
                        if (!canWrite(static_cast<int>(textLen)))
                            return 1;
                        std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                            defaultRecord->text.c_str(), textLen);
                        m_physical.bytecodeEnd += static_cast<int>(textLen);
                    }
                    else
                    {
                        if (!emitIntObject(defaultRecord->intValue))
                            return 1;
                    }

                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                        return 1;
                    if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::CallScriptFunction), functionOffset))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 4)
            {
                const STRING& textValue = records[static_cast<std::size_t>(foundIndex)].text;
                const std::size_t textLen = std::strlen(textValue.c_str()) + 1;
                if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                    return 1;
                if (!canWrite(static_cast<int>(textLen)))
                    return 1;
                std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                    textValue.c_str(), textLen);
                m_physical.bytecodeEnd += static_cast<int>(textLen);
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 5)
            {
                const int value = records[static_cast<std::size_t>(foundIndex)].value0;
                if (!emitIntObject(value))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 1)
            {
                const int stackIndex = records[static_cast<std::size_t>(foundIndex)].value0;
                const script::StackObject* stackRecord = mutableExecutionStackStorageAt(stackIndex);

                int indexBytecodeStart = 0;
                int savedIndexBytecodeSize = 0;
                std::uint8_t* savedIndexBytecode = nullptr;
                if (matchToken("["))
                {
                    indexBytecodeStart = m_physical.bytecodeEnd;
                    if ((stackRecord->flags & (script::STACK_OBJECT_STRING |
                                               script::STACK_OBJECT_ARRAY |
                                               script::STACK_OBJECT_DYNAMIC)) == 0)
                    {
                        reportCompileError(10, "[] for not array", 0);
                        std::exit(1);
                    }

                    compileExpression();
                    if (!emitByte(script::opcodeValue(script::VmOpcode::ArrayIndex)))
                        return 1;
                    savedIndexBytecodeSize = m_physical.bytecodeEnd - indexBytecodeStart;
                    savedIndexBytecode = static_cast<std::uint8_t*>(
                        ::operator new(static_cast<std::size_t>(savedIndexBytecodeSize)));
                    std::memcpy(savedIndexBytecode,
                        bytecodeStorage() + static_cast<std::size_t>(indexBytecodeStart),
                        static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_physical.bytecodeEnd = indexBytecodeStart;
                    requireToken("]");
                }

                std::uint8_t assignmentOpcode = prefixOpcode;
                script::BinaryCommand compoundCommand = script::BinaryCommand::Add;
                auto sourceCharAtOffset = [&](int delta) -> int
                {
                    const int pos = sourceCursorOffset() + delta;
                    return sourceStorage()[static_cast<std::size_t>(pos)];
                };

                if (sourceCharAtOffset(1) != '=' && matchToken("="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::Assign);
                }
                else if (matchToken("+="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Add;
                }
                else if (matchToken("-="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Subtract;
                }
                else if (matchToken("/="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Divide;
                }
                else if (matchToken("*="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Multiply;
                }
                else if (matchToken("%="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Modulo;
                }
                else if (matchToken("&="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseAnd;
                }
                else if (matchToken("|="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseOr;
                }
                else if (matchToken("^="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseXor;
                }
                else if (matchToken("<<="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftLeft;
                }
                else if (matchToken(">>="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftRight;
                }
                else if (matchToken("++"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostIncrement);
                }
                else if (matchToken("--"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostDecrement);
                }

                if (savedIndexBytecode)
                {
                    std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                        savedIndexBytecode, static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_physical.bytecodeEnd += savedIndexBytecodeSize;
                    ::operator delete(savedIndexBytecode);
                    savedIndexBytecode = nullptr;
                }
                else if ((stackRecord->flags & (script::STACK_OBJECT_ARRAY | script::STACK_OBJECT_DYNAMIC)) != 0)
                {
                    if (assignmentOpcode == script::opcodeValue(script::VmOpcode::PostIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PostDecrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreDecrement))
                    {
                        reportCompileError(10, "Increment or decrement for array", 0);
                        std::exit(1);
                    }
                    if (assignmentOpcode != script::opcodeValue(script::VmOpcode::ReadVariable))
                    {
                        reportCompileError(4, "operation for array", assignmentOpcode);
                        std::exit(1);
                    }
                }

                if (!emitByteAndIntPayload(static_cast<std::uint8_t>(assignmentOpcode), stackIndex))
                    return 1;
                if (assignmentOpcode == script::opcodeValue(script::VmOpcode::CompoundAssign) && !emitByte(script::opcodeValue(compoundCommand)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            return emitTrailingUnary() ? 0 : 1;
        }

        if (unaryOpcode != 0)
        {
            reportCompileError(10, "error symbol", 0);
            std::exit(1);
        }
        return 0;
    }

    void SCRIPT::emitOrFoldBinaryCommand(int bytecodeStart, int opcode)
    {
        const std::size_t start = static_cast<std::size_t>(bytecodeStart);
        const bool canReadTwoConstants =
            bytecodeStorage()[start] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            bytecodeStorage()[start + 5] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            m_physical.bytecodeEnd - bytecodeStart == 10;

        if (canReadTwoConstants)
        {
            int lhs = 0;
            int rhs = 0;
            std::memcpy(&lhs, bytecodeStorage() + start + 1, sizeof(lhs));
            std::memcpy(&rhs, bytecodeStorage() + start + 6, sizeof(rhs));

            bool hasFoldedValue = true;
            int folded = lhs;
            switch (static_cast<script::BinaryCommand>(opcode))
            {
            case script::BinaryCommand::Multiply:
                folded = lhs * rhs;
                break;
            case script::BinaryCommand::Divide:
                folded = lhs / rhs;
                break;
            case script::BinaryCommand::Modulo:
                folded = lhs % rhs;
                break;
            case script::BinaryCommand::Add:
                folded = lhs + rhs;
                break;
            case script::BinaryCommand::Subtract:
                folded = lhs - rhs;
                break;
            case script::BinaryCommand::ShiftRight:
                folded = lhs >> (rhs & 31);
                break;
            case script::BinaryCommand::ShiftLeft:
                folded = lhs << (rhs & 31);
                break;
            case script::BinaryCommand::BitwiseXor:
                folded = lhs ^ rhs;
                break;
            case script::BinaryCommand::BitwiseAnd:
                folded = lhs & rhs;
                break;
            case script::BinaryCommand::BitwiseOr:
                folded = lhs | rhs;
                break;
            default:
                hasFoldedValue = false;
                break;
            }

            if (hasFoldedValue)
                std::memcpy(bytecodeStorage() + start + 1, &folded, sizeof(folded));
            m_physical.bytecodeEnd -= 5;
            return;
        }

        const std::size_t writeOffset = static_cast<std::size_t>(m_physical.bytecodeEnd);
        bytecodeStorage()[writeOffset] = static_cast<std::uint8_t>(opcode);
        ++m_physical.bytecodeEnd;
    }



    void SCRIPT::compileMultiplicativeExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compilePrimaryExpression();
        for (;;)
        {
            if (matchToken("*"))
            {
                compilePrimaryExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Multiply));
                continue;
            }
            if (matchToken("/"))
            {
                compilePrimaryExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Divide));
                continue;
            }
            if (matchToken("%"))
            {
                compilePrimaryExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Modulo));
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileAdditiveExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileMultiplicativeExpression();
        for (;;)
        {
            if (matchToken("+"))
            {
                compileMultiplicativeExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Add));
                continue;
            }
            if (matchToken("-"))
            {
                compileMultiplicativeExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Subtract));
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileComparisonExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileAdditiveExpression();
        for (;;)
        {
            if (matchToken(">="))
            {
                compileAdditiveExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::GreaterEqual);
                continue;
            }
            if (matchToken(">>"))
            {
                compileAdditiveExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftRight));
                continue;
            }
            if (matchToken(">"))
            {
                compileAdditiveExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Greater);
                continue;
            }
            if (matchToken("<="))
            {
                compileAdditiveExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LessEqual);
                continue;
            }
            if (matchToken("<<"))
            {
                compileAdditiveExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftLeft));
                continue;
            }
            if (matchToken("<"))
            {
                compileAdditiveExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Less);
                continue;
            }
            if (matchToken("=="))
            {
                compileAdditiveExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Equal);
                continue;
            }
            if (matchToken("!="))
            {
                compileAdditiveExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::NotEqual);
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileComparisonExpression();
        for (;;)
        {
            if (matchToken("^"))
            {
                compileComparisonExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseXor));
                continue;
            }
            if (matchToken("&&"))
            {
                compileComparisonExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LogicalAnd);
                continue;
            }
            if (matchToken("&"))
            {
                compileComparisonExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseAnd));
                continue;
            }
            if (matchToken("||"))
            {
                compileComparisonExpression();
                bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LogicalOr);
                continue;
            }
            if (matchToken("|"))
            {
                compileComparisonExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseOr));
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileExpressionList()
    {
        for (;;)
        {
            compileExpression();
            const std::size_t writeOffset = static_cast<std::size_t>(m_physical.bytecodeEnd);
            bytecodeStorage()[writeOffset] = script::opcodeValue(script::VmOpcode::StatementEnd);
            ++m_physical.bytecodeEnd;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                &m_physical.sourceLine, sizeof(m_physical.sourceLine));
            m_physical.bytecodeEnd += 4;
            if (!matchToken(","))
                break;
        }
    }

    void SCRIPT::compileStatement(std::int32_t* breakPatchList)
    {
        const int iffMatched = matchToken("iff");
        if (iffMatched || matchToken("if"))
        {
            requireToken("(");
            compileExpression();
            requireToken(")");

            const script::VmOpcode branchOpcode = iffMatched
                ? script::VmOpcode::IfFalseChain
                : script::VmOpcode::If;
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(branchOpcode);
            const int firstPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            compileStatement(breakPatchList);
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(firstPatchOffset)) = (m_physical.bytecodeEnd - firstPatchOffset);

            if (!matchToken("else"))
                return;

            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(firstPatchOffset)) = (m_physical.bytecodeEnd - firstPatchOffset + 5);
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int elsePatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            compileStatement(breakPatchList);
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(elsePatchOffset)) = (m_physical.bytecodeEnd - elsePatchOffset);
            return;
        }

        if (matchToken("while"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            requireToken("(");
            const int loopConditionStart = m_physical.bytecodeEnd;
            compileExpression();
            requireToken(")");
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            compileStatement(localBreakPatchList);
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int jumpBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (loopConditionStart - jumpBackPatchOffset);
            m_physical.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(conditionPatchOffset)) = (m_physical.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_physical.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("do"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            const int loopBodyStart = m_physical.bytecodeEnd;
            compileStatement(localBreakPatchList);

            requireToken("while");
            requireToken("(");
            compileExpression();
            requireToken(")");
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::LogicalNot);
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int branchBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (loopBodyStart - branchBackPatchOffset);
            m_physical.bytecodeEnd += 4;

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_physical.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("for"))
        {
            std::int32_t localBreakPatchList[0x80] = {};

            requireToken("(");
            compileExpressionList();
            requireToken(";");

            const int conditionStart = m_physical.bytecodeEnd;
            compileExpression();
            requireToken(";");
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int skipUpdatePatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            const int updateStart = m_physical.bytecodeEnd;
            compileExpressionList();
            requireToken(")");
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int updateJumpBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (conditionStart - updateJumpBackPatchOffset);
            m_physical.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(skipUpdatePatchOffset)) = (m_physical.bytecodeEnd - skipUpdatePatchOffset);

            compileStatement(localBreakPatchList);
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int bodyJumpBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (updateStart - bodyJumpBackPatchOffset);
            m_physical.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(conditionPatchOffset)) = (m_physical.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_physical.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("break"))
        {
            requireToken(";");
            if (!breakPatchList)
            {
                reportCompileError(10, "'break' without loop", 0);
                std::exit(1);
            }

            int slot = 0;
            while (slot < 0x80 && breakPatchList[slot] != 0)
                ++slot;
            if (slot >= 0x80)
            {
                reportCompileError(10, "Too many 'break'", 0);
                std::exit(1);
            }
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            breakPatchList[slot] = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;
            breakPatchList[slot + 1] = 0;
            return;
        }

        if (matchToken("goto"))
        {
            STRING name;
            readIdentifier(name);

            int labelIndex = findFunctionRecordByName(name);
            const bool appendedPendingLabel = labelIndex < 0;
            if (appendedPendingLabel)
            {
                appendFunctionRecord(name, 8, STRING(), m_physical.bytecodeEnd + 1, 0, 0);
                labelIndex = functionCount() - 1;
            }

            const script::LogicFunctionRecord* const records = functionRecordStorage();
            const script::LogicFunctionRecord& labelRecord = records[static_cast<std::size_t>(labelIndex)];

            if (labelRecord.flags == 8 && !appendedPendingLabel)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "second use undefined label '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            if (labelRecord.flags != 7 && labelRecord.flags != 8)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "'%s' is not label", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int payloadOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (labelRecord.value0 - payloadOffset);
            m_physical.bytecodeEnd += 4;
            return;
        }

        if (matchToken("{"))
        {
            const int savedFunctionCount = functionCount();
            while (!matchToken("}"))
                compileStatement(breakPatchList);
#if defined(_MSC_VER) && defined(_M_IX86)
            m_physical.functionCount = savedFunctionCount;
#else
            host().m_functionTable.truncateCount(savedFunctionCount);
            syncPhysicalFunctionList();
#endif
            return;
        }

        compileExpressionList();
        requireToken(";");
    }

    int SCRIPT::compileNextSourceItem()
    {
        if (skipTriviaAndPreprocess())
            return 1;

        const std::size_t cursor = static_cast<std::size_t>(sourceCursorOffset());
        auto starts = [&](const char* token) -> bool
        {
            return std::strncmp(reinterpret_cast<const char*>(sourceStorage() + cursor),
                token, std::strlen(token)) == 0;
        };

        if (starts("#define"))
            return compileDefineDirective();
        if (starts("#undef"))
            return compileUndefDirective();
        if (matchToken("#include"))
            return compileIncludeDirective();
        if (matchToken("extern"))
            return compileExternDirective();

        if (matchToken("static"))
        {
            if (matchToken("int"))
            {
                do
                {
                    compileIntDeclaration();
                }
                while (matchToken(","));
                requireToken(";");
                return skipTriviaAndPreprocess();
            }
            if (matchToken("string"))
            {
                do
                {
                    compileStringDeclaration();
                }
                while (matchToken(","));
                requireToken(";");
                return skipTriviaAndPreprocess();
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (matchToken("int"))
        {
            do
            {
                compileIntDeclaration();
            }
            while (matchToken(","));
            requireToken(";");
            return skipTriviaAndPreprocess();
        }
        if (matchToken("string"))
        {
            do
            {
                compileStringDeclaration();
            }
            while (matchToken(","));
            requireToken(";");
            return skipTriviaAndPreprocess();
        }

        return compileFunctionDirective();
    }

    int SCRIPT::compileDefineDirective()
    {
        setSourceCursorOffset(sourceCursorOffset() + 7);
        STRING name;
        m_physical.parseMode = 1;
        readIdentifier(name);
        m_physical.parseMode = 0;

        STRING value;
        readSourceLine(value);

        addOrReplaceDefine(name, value);

        destroyStringStorage(value);
        value.ResetSharedEmptyWithoutRelease();
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileUndefDirective()
    {
        setSourceCursorOffset(sourceCursorOffset() + 6);
        STRING name;
        m_physical.parseMode = 1;
        readIdentifier(name);
        m_physical.parseMode = 0;

        if (undefine(name) < 0)
        {
            reportCompileError(4, "#undef parameters", 0);
            std::exit(1);
        }
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileIncludeDirective()
    {
        STRING savedScriptFile = scriptFileStorage();

        const unsigned char delimiter = sourceByteAtCursor();
        if (delimiter != '"' && delimiter != '<')
        {
            reportCompileError(13, "include file name", 0);
            std::exit(1);
        }

        setSourceCursorOffset(sourceCursorOffset() + 1);

        char includeName[0x400];
        int includeNameLength = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '"' || c == '>')
                break;
            if (sourceCursorOffset() >= sourceEndOffset())
            {
                reportCompileError(10, "End of file", 0);
                std::exit(1);
            }
            includeName[includeNameLength++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        includeName[includeNameLength] = '\0';
        setSourceCursorOffset(sourceCursorOffset() + 1);

        FileStream includeStream(includeName, "rb");
        if (!includeStream.isOpen())
        {
            reportCompileError(7, includeName, 0);
            std::exit(1);
        }

        const std::uint32_t includeLength = retailFileLength32(includeStream);
        const int savedCursor = sourceCursorOffset();
        const int savedEnd = sourceEndOffset();
        const int savedLine = m_physical.sourceLine;
        const int savedConditionalDepth = m_physical.conditionalDepth;

#if defined(_MSC_VER) && defined(_M_IX86)
        void* includeSourceOwner = nullptr;
        try
        {
            const std::uint32_t includeAllocationSize = includeLength + static_cast<std::uint32_t>(SourceBufferPadding);
            includeSourceOwner = ::operator new(static_cast<std::size_t>(includeAllocationSize));
        }
        catch (...)
        {
            reportCompileError(2, "include", 0);
            std::exit(1);
        }
        const std::uint32_t savedSourceToken = m_physical.sourceBufferToken;
        m_physical.sourceBufferToken = retailPointerToken(includeSourceOwner);
#else
        script::RetailByteBuffer includeSourceOwner;
        try
        {
            const std::uint32_t includeAllocationSize = includeLength + static_cast<std::uint32_t>(SourceBufferPadding);
            includeSourceOwner.resize(static_cast<std::size_t>(includeAllocationSize));
        }
        catch (...)
        {
            reportCompileError(2, "include", 0);
            std::exit(1);
        }
        script::RetailByteBuffer savedSourceOwner;
        savedSourceOwner.swap(host().m_sourceBuffer);
        host().m_sourceBuffer.swap(includeSourceOwner);
        syncPhysicalSourcePointers();
#endif
        setSourceCursorOffset(static_cast<int>(SourcePayloadOffset));
        const std::uint32_t includeEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + includeLength;
        setSourceEndOffset(static_cast<std::int32_t>(includeEndOffset));
        if (includeLength != 0)
        {
            includeStream.read(sourceStorage() + SourcePayloadOffset,
                includeLength);
        }

        m_physical.sourceLine = 0;
        assignStringFromCString(scriptFileStorage(), includeName);
        m_physical.conditionalDepth = 0;

        int status = compileNextSourceItem();
        while (status == 0)
            status = compileNextSourceItem();

        includeStream.close();

        // Free the include allocation first, then put back the untouched parent
        // allocation and exact cursor/end offsets.
#if defined(_MSC_VER) && defined(_M_IX86)
        ::operator delete(static_cast<void*>(sourceStorage()));
        m_physical.sourceBufferToken = savedSourceToken;
#else
        host().m_sourceBuffer.clear();
        script::RetailByteBuffer().swap(host().m_sourceBuffer);
        host().m_sourceBuffer.swap(savedSourceOwner);
        syncPhysicalSourcePointers();
#endif
        setSourceCursorOffset(savedCursor);
        setSourceEndOffset(savedEnd);
        m_physical.sourceLine = savedLine;
        m_physical.conditionalDepth = savedConditionalDepth;
        assignStringFromString(scriptFileStorage(), savedScriptFile);

        destroyStringStorage(savedScriptFile);
        savedScriptFile.ResetSharedEmptyWithoutRelease();
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileExternDirective()
    {
        const int savedFunctionCount = functionCount();

        STRING name;
        readIdentifier(name);

        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
        requireToken("(");
        for (;;)
        {
            if (matchToken("int"))
            {
                compileIntDeclaration();
            }
            else if (matchToken("string"))
            {
                compileStringDeclaration();
            }

            if (!matchToken(","))
                break;
        }
        requireToken(")");

        const int parameterCount = executionStackCount() - stackBase;
        const int nativeCode = parseConstantIntExpression();

        const unsigned char previous = sourceStorage()[static_cast<std::size_t>(sourceCursorOffset() - 1)];
        if (!std::isdigit(retailCtypeInput41BC50(previous)))
        {
            reportCompileError(13, "extern function code", 0);
            std::exit(1);
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        m_physical.functionCount = savedFunctionCount;
#else
        host().m_functionTable.truncateCount(savedFunctionCount);
        syncPhysicalFunctionList();
#endif
        appendFunctionRecord(name, 2, STRING(), nativeCode, stackBase, parameterCount);
        requireToken(";");
        return skipTriviaAndPreprocess();
    }


    int SCRIPT::compileFunctionDirective()
    {
        const int savedFunctionCount = functionCount();
        STRING name;
        readIdentifier(name);

        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
        const int bytecodeStart = m_physical.bytecodeEnd;

        requireToken("(");
        for (;;)
        {
            if (matchToken("int"))
            {
                compileIntDeclaration();
            }
            else if (matchToken("string"))
            {
                compileStringDeclaration();
            }

            if (!matchToken(","))
                break;
        }
        requireToken(")");

        const int parameterCount = executionStackCount() - stackBase;
        requireToken("{");
        while (!matchToken("}"))
            compileStatement(nullptr);

        bytecodeStorage()[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Return);

#if defined(_MSC_VER) && defined(_M_IX86)
        m_physical.functionCount = savedFunctionCount;
#else
        host().m_functionTable.truncateCount(savedFunctionCount);
        syncPhysicalFunctionList();
#endif
        appendFunctionRecord(name, 3, STRING(), bytecodeStart, stackBase, parameterCount);
        if (std::strcmp(name.c_str(), "main") == 0)
            m_physical.fallbackFunction = functionCount() - 1;

        return skipTriviaAndPreprocess();
    }


    namespace
    {
        bool readVmDword(const std::uint8_t* bytecode, int offset, int& value)
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, bytecode + static_cast<std::size_t>(offset), sizeof(raw));
            value = static_cast<int>(raw);
            return true;
        }

        int stackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? script::ParseStackIntegerText(value.text.c_str())
                : value.intValue;
        }

        int stackObjectNumeric41F2D0(const script::StackObject& value)
        {
            return stackValueToInteger(value);
        }


        int scriptSpritePointerValue(SPRITE* sprite) noexcept
        {
            return sprite
                ? static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sprite)))
                : 0;
        }

        SPRITE* scriptResolveSpriteReference(int value) noexcept
        {
            if (value == 0)
                return nullptr;
            return reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        bool isValidNvid(int nvid) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            return nvid >= 0 && nvid < table.count() &&
                   table.slot(nvid) != nullptr;
        }

        VID* resolveVidByNvid(int nvid) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            if (nvid >= 0 && nvid < table.count())
            {
                if (VID* const vid = table.slot(nvid))
                    return vid;
            }
            return MAP::NullVid();
        }

        PLAYER* scriptPlayerSlot(int index) noexcept
        {
#ifdef _WIN32
            return win::applicationWinInstance()->playerSlotByIndex(index);
#else
            (void)index;
            return nullptr;
#endif
        }

        SPRITE* beginReverseDrawPassIteration(core::ApplicationDrawDispatcherState& drawState, int pass, int* cursor)
        {
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            *cursor = bucket.count() - 1;
            while (*cursor >= 0 && !bucket.spriteAt(*cursor))
                --(*cursor);
            return *cursor >= 0 ? bucket.spriteAt(*cursor) : nullptr;
        }

        int g_unitIteratorArmyBucket = 0;
        int g_unitIteratorOrdinal = 0;

        SPRITE* findRootUnitByArmyOrdinal(int a1, int a2, int* outOrdinal)
        {
            SPRITE_COLLECTOR_HASH_MAP* const map = GlobalSpriteHashMap();
            int skipped = 0;
            int matched = 0;
            int count = map->overflowCount();
            if (!count)
            {
                *outOrdinal = 0;
                return nullptr;
            }

            int index = count - 1;
            SPRITE* sprite = map->overflowSpriteAt(index);
            if (!sprite)
            {
                *outOrdinal = 0;
                return nullptr;
            }

            for (;;)
            {
                if (sprite->Vid()->spriteClassId() == 21 && !sprite->engineChainPrevious())
                {
                    if (a1 == 4 || sprite->armyIndex() == a1)
                    {
                        ++skipped;
                        if (++matched == a2)
                        {
                            *outOrdinal = skipped;
                            SPRITE* const special = static_cast<ENGINE*>(sprite)->findEngineChainSpecialWeaponNode();
                            if (!special)
                                return sprite;
                            if ((sprite->runtimeFlags() ^ special->runtimeFlags()) & SPRITE::ArmyBitsMask)
                                --matched;
                            else
                                return special;
                        }
                    }
                    else
                    {
                        ++skipped;
                    }
                }

                if (index > map->overflowCount())
                    index = map->overflowCount();
                --index;
                if (index < 0)
                {
                    *outOrdinal = 0;
                    return nullptr;
                }
                sprite = map->overflowSpriteAt(index);
                if (!sprite)
                {
                    *outOrdinal = 0;
                    return nullptr;
                }
            }
        }

        SPRITE* beginRootUnitArmyIteration(int a1)
        {
            if (a1 < 0)
                return nullptr;
            int bucket = a1;
            if (bucket >= 4)
                bucket = 3;
            g_unitIteratorArmyBucket = bucket;
            g_unitIteratorOrdinal = 1;
            return findRootUnitByArmyOrdinal(bucket, 1, &a1);
        }

        SPRITE* continueRootUnitArmyIteration()
        {
            int value = 0;
            return findRootUnitByArmyOrdinal(g_unitIteratorArmyBucket, ++g_unitIteratorOrdinal, &value);
        }

        
        constexpr std::uint32_t scriptSinTableBits[256] =
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

        constexpr std::uint32_t scriptCosTableBits[256] =
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

        float scriptNativeFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        int scriptNativeTable1024ToInt(float value)
        {
            return static_cast<int>(value * 1024.0f);
        }

        int scriptNativeSin1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::rawDirectionSin(angle));
        }

        int scriptNativeCos1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::rawDirectionCos(angle));
        }
    }

    STRING SCRIPT::getVariableString(const STRING& expression)
    {
        STRING variableName;
        constructLeftOfFirstMarker(expression, variableName, "[");
        const int functionIndex = findFunctionRecordByName(variableName);
        if (functionIndex < 0)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Can't find variable '%s' in GetVariableString", expression.c_str());
            return STRING();
        }

        STRING indexText;
        constructRightOfFirstMarker(expression, indexText, "[");
        const int elementIndex = script::ParseStackIntegerText(indexText.c_str());
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        const int stackIndex = records[static_cast<std::size_t>(functionIndex)].value0 + elementIndex;
        script::StackObject* value = mutableExecutionStackStorageAt(stackIndex);

        if ((value->flags & script::STACK_OBJECT_INT) != 0)
        {
            value->text = script::IntToStackString(value->intValue);
        }
        return STRING(value->text.c_str());
    }

    int SCRIPT::callFunction(int functionIndex, int arg3, int arg4, int arg5)
    {
        if (m_physical.bytecodeBufferToken == 0u)
            return 0;

        int resolvedFunction = functionIndex;
        if (resolvedFunction < 0)
            resolvedFunction = m_physical.fallbackFunction;

        if (resolvedFunction < 0 || resolvedFunction >= m_physical.functionCount)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Call unexisted function %i", resolvedFunction);
            return 0;
        }

        const script::LogicFunctionRecord& fn = functionRecordStorage()[static_cast<std::size_t>(resolvedFunction)];
        if (fn.flags != 3)
        {
            LOG::Write("!!!ERROR!!!LOGIC: Call unexisted function %s()", fn.name.c_str());
            return 0;
        }

        const int savedStackCount = m_physical.stackCount;
        const int bytecodeLimit = m_physical.bytecodeEnd;
        int cursor = fn.value0;
        int controlAnchor = cursor;
        int result = 0;

        pushIntegerValue(savedStackCount);
        pushIntegerValue(bytecodeLimit);
        int frameBase = static_cast<int>(
            static_cast<std::uint32_t>(savedStackCount) + 2u);

        if (fn.value2 >= 1)
        {
            script::StackObject* arg = mutableExecutionStackStorageAt(fn.value1);
            arg->assignFields(static_cast<std::uint8_t>(arg3 ? (script::STACK_OBJECT_INT | script::STACK_OBJECT_REF) : script::STACK_OBJECT_INT), arg3, STRING());
        }
        if (fn.value2 >= 2)
        {
            script::StackObject* arg = mutableExecutionStackStorageAt(fn.value1 + 1);
            arg->assignFields(static_cast<std::uint8_t>(arg4 ? (script::STACK_OBJECT_INT | script::STACK_OBJECT_REF) : script::STACK_OBJECT_INT), arg4, STRING());
        }
        if (fn.value2 >= 3)
        {
            script::StackObject* arg = mutableExecutionStackStorageAt(fn.value1 + 2);
            arg->assignInt(arg5);
        }






        int arrayVmActive = 0;
        int arrayVmOffset = 0;


        while (cursor < bytecodeLimit)
        {
            if (m_physical.stackCount < frameBase)
            {
                LOG::Write("C!!!ERROR!!!LOGIC: '%s' stack error %i", "pop, but not push", cursor);
                std::exit(1);
            }

            const std::uint8_t opcode = bytecodeStorage()[static_cast<std::size_t>(cursor++)];
            const bool isBinaryCommand =
                opcode >= script::opcodeValue(script::BinaryCommand::Divide) &&
                opcode <= script::opcodeValue(script::BinaryCommand::ShiftLeft);
            const bool isVariableCommand =
                opcode >= script::opcodeValue(script::VmOpcode::PostIncrement) &&
                opcode <= script::opcodeValue(script::VmOpcode::CompoundAssign);
            if (isBinaryCommand || isVariableCommand)
            {
                if (isBinaryCommand)
                {
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                    script::StackObject* lhs = mutableExecutionStackStorageAt(m_physical.stackCount - 2);
                    lhs->applyBinaryCommand(opcode, *rhs);
                    --m_physical.stackCount;
                    continue;
                }

                int operandIndex = 0;
                readVmDword(bytecodeStorage(), cursor, operandIndex);

                script::StackObject* operandRecord = mutableExecutionStackStorageAt(operandIndex);
                int targetIndex = operandIndex;
                if ((operandRecord->flags & script::STACK_OBJECT_DYNAMIC) != 0 && arrayVmActive != 0)
                    targetIndex = operandRecord->intValue;
                targetIndex += arrayVmOffset;

                script::StackObject* target = mutableExecutionStackStorageAt(targetIndex);

                const script::VmOpcode variableCommand = static_cast<script::VmOpcode>(opcode);

                switch (variableCommand)
                {
                case script::VmOpcode::PostIncrement:
#if defined(_MSC_VER) && defined(_M_IX86)
                    reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
#else
                    appendExecutionStackObject(*target);
#endif
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                        target->intValue += 1;
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PostDecrement:
#if defined(_MSC_VER) && defined(_M_IX86)
                    reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
#else
                    appendExecutionStackObject(*target);
#endif
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                        target->intValue -= 1;
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PreIncrement:
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                        target->intValue += 1;
#if defined(_MSC_VER) && defined(_M_IX86)
                    reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
#else
                    appendExecutionStackObject(*target);
#endif
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PreDecrement:
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                        target->intValue -= 1;
#if defined(_MSC_VER) && defined(_M_IX86)
                    reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
#else
                    appendExecutionStackObject(*target);
#endif
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::ReadVariable:
                    if (arrayVmActive == 0 && (target->flags & script::STACK_OBJECT_ARRAY) != 0)
                    {
                        pushIntegerValue(operandIndex);
                    }
                    else
                    {
#if defined(_MSC_VER) && defined(_M_IX86)
                        reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                            target->flags, target->intValue, target->text);
#else
                        appendExecutionStackObject(*target);
#endif
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::AddressOf:
                {
                    int addressValue = 0;
                    if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        const auto rawAddress = reinterpret_cast<std::uintptr_t>(&target->text);
                        addressValue = static_cast<int>(rawAddress & 0xFFFFFFFFu);
                    }
                    pushIntegerValue(addressValue);
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                case script::VmOpcode::Assign:
                {
                    script::StackObject* source = mutableExecutionStackStorageAt(m_physical.stackCount - 1);

                    script::StackObject* baseRecord = mutableExecutionStackStorageAt(operandIndex);
                    if ((baseRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        if (arrayVmActive != 0 && (baseRecord->flags & script::STACK_OBJECT_ARRAY) == 0)
                        {
                            std::string text = baseRecord->text.str();
                            const int numeric = stackObjectNumeric41F2D0(*source);
                            text[static_cast<std::size_t>(arrayVmOffset)] = static_cast<char>(numeric & 0xFF);
                            baseRecord->text.AssignBytes(text.data(), text.size());
                        }
                        else
                        {
                            if ((source->flags & script::STACK_OBJECT_INT) != 0)
                                target->text = script::IntToStackString(source->intValue);
                            else
                                target->text = source->text;
                        }
                    }
                    else
                    {
                        const int numeric = stackObjectNumeric41F2D0(*source);
                        target->flags = static_cast<std::uint8_t>(target->flags & ~static_cast<std::uint8_t>(script::STACK_OBJECT_REF | script::STACK_OBJECT_CHAR_WRITE));
                        target->intValue = numeric;
                        if ((source->flags & script::STACK_OBJECT_REF) != 0)
                            target->flags = static_cast<std::uint8_t>(target->flags | script::STACK_OBJECT_REF);
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                case script::VmOpcode::CompoundAssign:
                {
                    --m_physical.stackCount;
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_physical.stackCount);
                    const std::uint8_t compoundOpcode = bytecodeStorage()[static_cast<std::size_t>(cursor + 4)];
                    target->applyBinaryCommand(compoundOpcode, *rhs);
#if defined(_MSC_VER) && defined(_M_IX86)
                    reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
#else
                    appendExecutionStackObject(*target);
#endif
                    cursor += 5;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                default:
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
            }

            switch (static_cast<script::VmOpcode>(opcode))
            {
            case script::VmOpcode::PushInteger:
            {
                int value = 0;
                readVmDword(bytecodeStorage(), cursor, value);
                pushIntegerValue(value);
                cursor += 4;
                break;
            }
            case script::VmOpcode::PushString:
            {
                const char* text = reinterpret_cast<const char*>(bytecodeStorage() + static_cast<std::size_t>(cursor));
                pushStringValue(STRING(text));
                cursor += static_cast<int>(std::strlen(text)) + 1;
                break;
            }
            case script::VmOpcode::Negate:
            case script::VmOpcode::BitwiseNot:
            case script::VmOpcode::LogicalNot:
            {
                script::StackObject* top = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                const int value = stackObjectNumeric41F2D0(*top);
                top->flags = script::STACK_OBJECT_INT;
                if (opcode == script::opcodeValue(script::VmOpcode::Negate))
                    top->intValue = -value;
                else if (opcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
                    top->intValue = ~value;
                else
                    top->intValue = value == 0 ? 1 : 0;
                break;
            }
            case script::VmOpcode::If:
            {
                --m_physical.stackCount;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_physical.stackCount);
                const int condValue = stackObjectNumeric41F2D0(*cond);
                int payload = 0;
                readVmDword(bytecodeStorage(), cursor, payload);
                cursor += condValue ? 4 : payload;
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("C!!!ERROR!!!LOGIC: '%s' stack error %i", "if", cursor);
                controlAnchor = cursor;
                m_physical.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::StatementEnd:
            {
                cursor += 4;
                controlAnchor = cursor;
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("C!!!ERROR!!!LOGIC: '%s' stack error %i", ";", cursor);
                m_physical.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::Pop:
                
                --m_physical.stackCount;
                break;
            case script::VmOpcode::Jump:
            {
                int payload = 0;
                readVmDword(bytecodeStorage(), cursor, payload);
                cursor += payload;
                break;
            }
            case script::VmOpcode::IfFalseChain:
            {
                --m_physical.stackCount;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_physical.stackCount);
                const int condValue = stackObjectNumeric41F2D0(*cond);
                int payload = 0;
                readVmDword(bytecodeStorage(), cursor, payload);

                if (condValue != 0)
                {
                    const int previousAnchor = controlAnchor;
                    bytecodeStorage()[static_cast<std::size_t>(previousAnchor)] = script::opcodeValue(script::VmOpcode::Jump);
                    const int patchedRelative = payload - previousAnchor + cursor - 1;
                    std::memcpy(bytecodeStorage() + static_cast<std::size_t>(previousAnchor + 1),
                                &patchedRelative, sizeof(patchedRelative));
                    cursor += 4;
                }
                else
                {
                    cursor += payload;
                }

                controlAnchor = cursor;
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("C!!!ERROR!!!LOGIC: '%s' stack error %i", "iff", cursor);
                m_physical.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::CallScriptFunction:
            {
                int targetBytecodeOffset = 0;
                readVmDword(bytecodeStorage(), cursor, targetBytecodeOffset);

                pushIntegerValue(frameBase);
                pushIntegerValue(cursor + 4);

                frameBase = m_physical.stackCount;
                cursor = targetBytecodeOffset;
                controlAnchor = cursor;
                break;
            }
            case script::VmOpcode::Return:
            {
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("C!!!ERROR!!!LOGIC: '%s' stack error %i", "return", cursor - 1);

                script::StackObject returnedValue;
                bool hasReturnedValue = false;
                if (m_physical.stackCount > frameBase)
                {
                    script::StackObject* top = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                    returnedValue.copyFrom(*top);
                    hasReturnedValue = true;
                    --m_physical.stackCount;
                }

                m_physical.stackCount = frameBase;

                --m_physical.stackCount;
                script::StackObject* returnCursorObject = mutableExecutionStackStorageAt(m_physical.stackCount);
                const int returnCursor = stackObjectNumeric41F2D0(*returnCursorObject);

                --m_physical.stackCount;
                script::StackObject* savedFrameObject = mutableExecutionStackStorageAt(m_physical.stackCount);
                frameBase = stackObjectNumeric41F2D0(*savedFrameObject);
                cursor = returnCursor;
                controlAnchor = cursor;

                if (hasReturnedValue)
                {
#if defined(_MSC_VER) && defined(_M_IX86)
                    reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable)->appendFields(
                        returnedValue.flags, returnedValue.intValue, returnedValue.text);
#else
                    appendExecutionStackObject(returnedValue);
#endif

                    if (returnCursor >= bytecodeLimit && result == 0)
                        result = stackObjectNumeric41F2D0(returnedValue);
                }
                break;
            }
            case script::VmOpcode::ArrayIndex:
            {
                --m_physical.stackCount;
                script::StackObject* const indexObject = mutableExecutionStackStorageAt(m_physical.stackCount);
                arrayVmOffset = stackObjectNumeric41F2D0(*indexObject);
                arrayVmActive = 1;
                break;
            }
            default:
            {
                
                constexpr std::uint8_t kRetailResultProbeOpcode = 84;
                if (opcode == kRetailResultProbeOpcode)
                {
                    const script::StackObject* top = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                    if (stackObjectNumeric41F2D0(*top) == arg3)
                        result = 1;
                }
                dispatchNativeFunction(static_cast<int>(opcode));
                break;
            }
            }
        }

        m_physical.stackCount = savedStackCount;
        if (savedStackCount > m_physical.stackCapacity)
        {
            auto* const stackList = reinterpret_cast<script::StackObjectList*>(&m_physical.stackListVtable);
            stackList->reserveExact(savedStackCount);
        }
        return result;
    }


    const char* SCRIPT::stringTextPointer(const STRING& value) const
    {
        return value.c_str();
    }

    int SCRIPT::writeCStringToStream(const STRING& source, BaseStream* target) const
    {
        const char* const text = source.c_str();
        return target->write(text, static_cast<unsigned>(std::strlen(text) + 1));
    }

    std::size_t SCRIPT::writeCStringRecord(const STRING& value, std::FILE* file) const
    {
        const char* text = value.c_str();
        const std::size_t sizeWithNul = std::strlen(text) + 1;
        return std::fwrite(text, sizeWithNul, 1, file);
    }

    std::FILE* SCRIPT::openScriptFile(const STRING& path, const char* mode) const
    {
        
        const char* text = path.c_str();
        if (text[0] == '\0')
            return nullptr;
        return std::fopen(text, mode);
    }


    int SCRIPT::popSpriteReferenceValue()
    {
        const int oldIndex = m_physical.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);

        if (top->intValue != 0 && (top->flags & script::STACK_OBJECT_REF) == 0)
            LOG::ResourceError("LOGIC", 10, "this variable is not unit", 0);

        --m_physical.stackCount;
        return stackValueToInteger(*top);
    }

    void SCRIPT::pushIntegerValue(int value)
    {
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        appendExecutionStackObject(obj);
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    void SCRIPT::pushSpriteReferenceValue(int value)
    {
        script::StackObject obj;
        obj.initializeReferenceValue(value);
        appendExecutionStackObject(obj);
    }
#endif

    void SCRIPT::pushStringValue(const STRING& value)
    {
        STRING ebxTemp;
        ebxTemp.AssignAllocatedCopyWithoutRelease(value.c_str());

#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t retailScratch;
#else
        std::uint32_t retailScratch = 0;
#endif
        const int rawScratch = static_cast<int>(core::retailReadStackDword(&retailScratch));

        appendExecutionStackRecord(
            static_cast<std::uint8_t>(script::STACK_OBJECT_STRING),
            rawScratch,
            ebxTemp);

        ebxTemp.ReleaseOwnedStorage();
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    int SCRIPT::popIntegerValue()
    {
        const int oldIndex = m_physical.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
        --m_physical.stackCount;
        return stackValueToInteger(*top);
    }
#endif



    GammaRawPair SCRIPT::decodePackedGamma(int value)
    {
        const std::uint32_t ecx = static_cast<std::uint32_t>(value);
        std::uint32_t diffuse = 0;
        std::uint32_t specular = 0;

        std::uint32_t edx = ecx;
        if ((ecx & 0x00000080u) != 0)
            specular |= (((~edx) & 0x0000007Fu) << 1);
        else
            diffuse |= ((edx & 0x0000007Fu) << 1);

        edx = ecx;
        if ((ecx & 0x00008000u) != 0)
            specular |= (((~edx) & 0x00007F80u) << 1);
        else
            diffuse |= ((edx & 0x00007F80u) << 1);

        edx = ecx;
        if ((ecx & 0x00800000u) != 0)
            specular |= (((~edx) & 0x007F8000u) << 1);
        else
            diffuse |= ((edx & 0x007F8000u) << 1);

        if ((ecx & 0x80000000u) != 0)
            specular |= (((~ecx) & 0xFF800000u) << 1);
        else
            diffuse |= ((ecx & 0xFF800000u) << 1);

        return GammaRawPair{diffuse, specular};
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    VID* SCRIPT::popVidValue(const char* errorContext)
    {
        
        const int nvid = popIntegerValue();
        VID* vid = MAP::NullVid();
        core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        if (nvid >= 0 && nvid < vidTable.count())
        {
            if (VID* const slot = vidTable.slot(nvid))
                vid = slot;
        }
        if (vid == MAP::NullVid())
        {
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", errorContext, nvid);
            return MAP::NullVid();
        }
        return vid;
    }
#endif

    void SCRIPT::reportScriptError(int errorCode, const char* text, int value)
    {
        
        LOG::ResourceError("SCRIPT", errorCode, text, value);
    }


    int SCRIPT::topValueIsString() const
    {
        const script::StackObject& top = executionStackStorage()[static_cast<std::size_t>(m_physical.stackCount - 1)];
        return (top.flags & script::STACK_OBJECT_STRING) != 0 ? 1 : 0;
    }

    STRING* SCRIPT::popStringRetail()
    {
        const int newIndex = m_physical.stackCount - 1;
        m_physical.stackCount = newIndex;
        script::StackObject* const top = mutableExecutionStackStorageAt(newIndex);

        if ((top->flags & script::STACK_OBJECT_INT) != 0)
        {
            char numericTextBuffer[0x80];
            std::memset(numericTextBuffer, 0, sizeof(numericTextBuffer));
#if defined(_MSC_VER)
            _itoa(top->intValue, numericTextBuffer, 10);
#else
            std::snprintf(numericTextBuffer, sizeof(numericTextBuffer), "%d", top->intValue);
#endif

            STRING convertedText;
            convertedText.AssignAllocatedCopyWithoutRelease(numericTextBuffer);
            assignStringFromString(top->text, convertedText);
            convertedText.ReleaseOwnedStorage();
        }

        return &top->text;
    }



    void SCRIPT::pushStringResult(const STRING& value)
    {
        
        pushStringValue(value);
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    void SCRIPT::pushIntegerResult(int value)
    {
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        appendExecutionStackObject(obj);
    }
#endif



    int MAP::scriptPopIntegerRetail()
    {
#if defined(_WIN32)
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScriptRuntime);
#else
        SCRIPT* const script = &scriptRuntime();
#endif
        const int oldIndex = script->m_physical.stackCount - 1;
        script::StackObject* const top = script->mutableExecutionStackStorageAt(oldIndex);
        script->m_physical.stackCount = oldIndex;
        return stackValueToInteger(*top);
    }

    VID* MAP::scriptPopVidRetail(const char* errorContext)
    {
        const int nvid = scriptPopIntegerRetail();
#if defined(_WIN32)
        auto* const owner = reinterpret_cast<std::uint8_t*>(this);
        const int count = *reinterpret_cast<const int*>(
            owner + core::retail_application_layout::VidCount);
        VID* vid = MAP::NullVid();
        if (nvid >= 0 && nvid < count)
        {
            VID* const slot = *reinterpret_cast<VID**>(
                owner + core::retail_application_layout::VidTable +
                static_cast<std::size_t>(nvid) * sizeof(VID*));
            if (slot)
                vid = slot;
        }
#else
        VID* vid = resolveVidByNvid(nvid);
#endif
        if (vid == MAP::NullVid())
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", errorContext, nvid);
        return vid;
    }

    void MAP::scriptPushIntegerRetail(int value)
    {
#if defined(_WIN32)
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScriptRuntime);
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        reinterpret_cast<script::StackObjectList*>(&script->m_physical.stackListVtable)->appendFields(
            obj.flags, obj.intValue, obj.text);
#else
        scriptRuntime().pushIntegerValue(value);
#endif
    }

    void MAP::scriptPushSpriteReferenceRetail(int value)
    {
#if defined(_WIN32)
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScriptRuntime);
        script::StackObject obj;
        obj.initializeReferenceValue(value);
        reinterpret_cast<script::StackObjectList*>(&script->m_physical.stackListVtable)->appendFields(
            obj.flags, obj.intValue, obj.text);
#else
        scriptRuntime().pushSpriteReferenceValue(value);
#endif
    }

    int SCRIPT::dispatchNativeFunction(int opcode)
    {
#if defined(_WIN32)
        return reinterpret_cast<MAP*>(core::ApplicationPhysicalOwner())->ExecFunc(opcode);
#else
        MAP* const owner = MAP::Current();
        return owner ? owner->ExecFunc(opcode) : 0;
#endif
    }

    int MAP::ExecFunc(int opcode)
    {
#if defined(_WIN32)
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScriptRuntime);
        core::ApplicationDrawDispatcherState& drawState =
            *reinterpret_cast<core::ApplicationDrawDispatcherState*>(this);
#else
        SCRIPT* const script = &scriptRuntime();
        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
#endif
        switch (static_cast<script::NativeFunctionCode>(opcode))
        {
        case script::NativeFunctionCode::CreateSprite:
{
                const int parentHandle = script->popSpriteReferenceValue();
                const int direction = scriptPopIntegerRetail();
                const int z = scriptPopIntegerRetail();
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for CreateSprite()");
                if (vid == MAP::NullVid())
                {
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                MAP* const map = MAP::Current();
                SPRITE* const parent = scriptResolveSpriteReference(parentHandle);
                SPRITE* const created = map->CreateSpriteViaFactory(
                    vid,
                    VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                    ANGLE(direction),
                    parent,
                    false);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(created));
                return 0;
            }
        case script::NativeFunctionCode::Flagman:
{
                        const int army = scriptPopIntegerRetail();
                        SPRITE* sprite = nullptr;
                #ifdef _WIN32
                        sprite = win::applicationWinInstance()->controlledSpriteForPlayer(army);
                #else
                        sprite = MAP::Current()->flagmanSpriteForPlayer(army);
                #endif
                        scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                        return 0;
            }
        case script::NativeFunctionCode::FirstUnit:
{
                g_scriptUnitIteratorTypeMask = scriptPopIntegerRetail();
                g_scriptUnitIteratorCursor = 0;
                SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
                SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
                SPRITE* sprite = list.beginReverseIteration(&g_scriptUnitIteratorCursor);
                while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & g_scriptUnitIteratorTypeMask) == 0))
                    sprite = list.continueReverseIteration(&g_scriptUnitIteratorCursor);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::NextUnit:
{
                SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
                SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
                SPRITE* sprite = list.continueReverseIteration(&g_scriptUnitIteratorCursor);
                while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & g_scriptUnitIteratorTypeMask) == 0))
                    sprite = list.continueReverseIteration(&g_scriptUnitIteratorCursor);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::GetSprite:
{
                SPRITE* const previous = script->popSpriteReference();
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                const int type = scriptPopIntegerRetail();
                SPRITE* const sprite = core::Application::findSpriteAtPointByBounds(
                    *MAP::Current(), core::GlobalApplicationDrawDispatcherState(),
                    type, static_cast<float>(x), static_cast<float>(y), previous);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::GetSpriteScr:
{
                const int screenY = scriptPopIntegerRetail();
                const int screenX = scriptPopIntegerRetail();
                const int type = scriptPopIntegerRetail();
                SPRITE* const sprite = core::Application::findSpriteAtPointByFilter(
                    *MAP::Current(), core::GlobalApplicationDrawDispatcherState(),
                    type, static_cast<float>(screenX), static_cast<float>(screenY));
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::FindNearestSprite:
{
                SPRITE* const previous = script->popSpriteReference();
                const int radius = scriptPopIntegerRetail();
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                const int type = scriptPopIntegerRetail();
                SPRITE* const sprite = core::Application::findNearestSpriteByFilter(
                    *MAP::Current(), core::GlobalApplicationDrawDispatcherState(),
                    type, static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius), previous);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::FirstInBox:
{
                const int bottom = scriptPopIntegerRetail();
                const int right = scriptPopIntegerRetail();
                const int top = scriptPopIntegerRetail();
                const int left = scriptPopIntegerRetail();
                SPRITE* sprite = GlobalHashFirstInBoxAroundDot(
                    static_cast<float>(left),
                    static_cast<float>(top),
                    static_cast<float>(right),
                    static_cast<float>(bottom));
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::NextInBox:
{
                SPRITE* sprite = GlobalHashNextInBoxAroundDot();
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::FirstSprite:
{
                g_scriptSpriteIteratorPass = 0;
                g_scriptSpriteIteratorCursor = drawState.drawPassBucket(0).count();
                SPRITE* sprite = core::Application::previousSpriteInDrawPass(drawState, 0, &g_scriptSpriteIteratorCursor);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::NextSprite:
{
                SPRITE* sprite = nullptr;

                --g_scriptSpriteIteratorCursor;
                if (g_scriptSpriteIteratorCursor >= 0)
                {
                    const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(g_scriptSpriteIteratorPass);
                    while (g_scriptSpriteIteratorCursor >= 0)
                    {
                        sprite = bucket.spriteAt(g_scriptSpriteIteratorCursor);
                        if (sprite)
                            break;
                        --g_scriptSpriteIteratorCursor;
                    }
                }

                while (!sprite && g_scriptSpriteIteratorPass < 13)
                {
                    ++g_scriptSpriteIteratorPass;
                    g_scriptSpriteIteratorCursor = drawState.drawPassBucket(g_scriptSpriteIteratorPass).count();
                    sprite = core::Application::previousSpriteInDrawPass(drawState, g_scriptSpriteIteratorPass, &g_scriptSpriteIteratorCursor);
                }

                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::Action:
{
                const int var3 = scriptPopIntegerRetail();
                const int var2 = scriptPopIntegerRetail();
                const int var1 = scriptPopIntegerRetail();
                const int act = scriptPopIntegerRetail();
                SPRITE* const actionSprite = scriptResolveSpriteReference(script->popSpriteReferenceValue());

                if (!actionSprite)
                {
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                if (act < 17)
                {
                    actionSprite->ChangeAnimation(act);
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                if (act == 121)
                {
                    const int rawStringOwner = actionSprite->dispatchVirtualAction(ActionCode::ACT_GET_TEXT_DESC, var1, var2, var3);
                    const char* const* const owner = reinterpret_cast<const char* const*>(
                        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(rawStringOwner)));
                    STRING value(*owner);
                    script->pushStringValue(value);
                    return 0;
                }

                if (act == 90 || act == 156 || act == 155 || act == 154 || act == 101 || act == 103)
                {
                    const int result = actionSprite->dispatchVirtualAction(
                        static_cast<std::uint32_t>(act), var1, var2, var3);
                    scriptPushSpriteReferenceRetail(result);
                    return 0;
                }

                if ((act == 33 || act == 32 || act == 36 || act == 34 || act == 150 || act == 151) &&
                    actionSprite->Vid()->spriteClassId() == 21u &&
                    static_cast<unsigned char>(actionSprite->runtimeFlags() & SPRITE::CommandBitsMask) == 104u)
                {
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                const int result = actionSprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(act), var1, var2, var3);
                scriptPushIntegerRetail(result);
                return 0;
            }
        case script::NativeFunctionCode::SizeTo:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                int value = 0xEA60;
                if (sprite)
                {
                    const long double distance = approximatePlanarDistance(
                        static_cast<float>(x) - sprite->X(),
                        static_cast<float>(y) - sprite->Y());
                    value = static_cast<int>(distance);
                }
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::AddCommand:
{
                const int var3 = scriptPopIntegerRetail();
                const int var2 = scriptPopIntegerRetail();
                const int var1 = scriptPopIntegerRetail();
                const int act = scriptPopIntegerRetail();
                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;
                scriptResolveSpriteReference(handle)->queueCommandBeforeStopSentinel(static_cast<std::uint32_t>(act), var1, var2, var3);
                return 0;
            }
        case script::NativeFunctionCode::GetUnitVid:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                scriptPushIntegerRetail(sprite && sprite->vidPointer() ? sprite->vidPointer()->nVid : 0);
                return 0;
            }
        case script::NativeFunctionCode::Destroy:
{
                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                return 0;
            }
        case script::NativeFunctionCode::GetX:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                scriptPushIntegerRetail(sprite ? static_cast<int>(sprite->xCoordinateValue()) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetY:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                scriptPushIntegerRetail(sprite ? static_cast<int>(sprite->yCoordinateValue()) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetZ:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                scriptPushIntegerRetail(sprite ? static_cast<int>(sprite->Z()) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetDirection:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                scriptPushIntegerRetail(sprite ? sprite->Direction().Int() : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetAnimation:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                scriptPushIntegerRetail(sprite ? sprite->currentAnimation() : 0);
                return 0;
            }
        case script::NativeFunctionCode::DirectionTo:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                int value = 0;
                if (sprite)
                    value = sprite->DirectionTo(VECTOR2{static_cast<float>(x), static_cast<float>(y)}).Int();
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::ViewXMin:
{
                GRAPH* const graph = GRAPH::CurrentGraph();
                scriptPushIntegerRetail(graph ? static_cast<int>(static_cast<float>(graph->getViewportLeft())) : 0);
                return 0;
            }
        case script::NativeFunctionCode::ViewYMin:
{
                GRAPH* const graph = GRAPH::CurrentGraph();
                scriptPushIntegerRetail(graph ? static_cast<int>(static_cast<float>(graph->getViewportTop())) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetCommands:
{
                assignStringFromCString(lpFile, Class);
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                if (!sprite)
                    return (script->pushStringValue(lpFile), 0);

                const DWORD spriteClass = sprite->Vid()->spriteClass;
                if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
                {
                    STRING commandWordsText;
                    sprite->serializeCommandWordsText(commandWordsText);
                    assignStringFromString(lpFile, commandWordsText);
                    commandWordsText.ReleaseOwnedStorage();
                }

                STRING commandRecordsText;
                sprite->serializeCommandRecordsText(commandRecordsText);
                appendStringOwner(lpFile, commandRecordsText);
                commandRecordsText.ReleaseOwnedStorage();

                return (script->pushStringValue(lpFile), 0);
            }
        case script::NativeFunctionCode::SetCommands:
{
                assignStringFromString(lpFile, *script->popStringRetail());

                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;

                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                if (!sprite)
                    return 0;

                const DWORD spriteClass = sprite->Vid()->spriteClass;
                if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
                    sprite->parseCommandWordsText(lpFile);

                static const char kCommandSectionDelimiter[] = { '\x02', '\0' };
                if (std::strstr(lpFile.c_str(), kCommandSectionDelimiter))
                {
                    STRING commandRecordsOnlyText;
                    constructRightOfFirstMarker(lpFile, commandRecordsOnlyText, kCommandSectionDelimiter);
                    assignStringFromString(lpFile, commandRecordsOnlyText);
                    commandRecordsOnlyText.ReleaseOwnedStorage();
                }

                sprite->parseCommandRecordsText(lpFile);

                return 0;
            }
        case script::NativeFunctionCode::Load:
{

                        const STRING path = *script->popStringRetail();
                        const std::uint32_t flags = core::ApplicationFlags() | application_flags::PendingCommandOrLoad;
                        core::SetApplicationFlags(flags);
                #ifdef _WIN32
                        win::applicationWinInstance()->setPendingCommand(path);
                        win::applicationWinInstance()->setFlags(flags);
                #else
                        if (script->host().m_nativeContext.queueMapLoadSlot18Flag40)
                            script->host().m_nativeContext.queueMapLoadSlot18Flag40(path);
                #endif
                        return 0;
            }
        case script::NativeFunctionCode::Save:
{
                        const STRING path = *script->popStringRetail();
                #ifdef _WIN32
                        win::applicationWinInstance()->saveMap(path);
                #else
                        if (MAP* const map = MAP::Current())
                            map->saveMapHost(path);
                #endif
                        return 0;
            }
        case script::NativeFunctionCode::SaveDemo:
{
                RESOURCE& demoResource = MAP::Current()->demoResource();
                if (demoResource.isOpen())
                    return 0;

                const STRING& path = *script->popStringRetail();
                openResourceFileForWrite(demoResource, path, RESOURCE::ResTypes::DEMO);
                return 0;
            }
        case script::NativeFunctionCode::MenuFind:
{
                const int ndir = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for MenuFind");

                SPRITE* found = nullptr;
                if (vid != MAP::NullVid() && vid->totalSpriteCount() != 0)
                {
                    SPRITE_LIST& list = applicationFrameSpriteList();
                    const int count = list.activeCount();
                    for (int i = 0; i < count; ++i)
                    {
                        SPRITE* const sprite = list.at(i);
                        if (!sprite || sprite->Vid() != vid)
                            continue;
                        if (ndir != 999999)
                        {
                            const std::uint32_t directionByte =
                                static_cast<std::uint32_t>(sprite->directionIndex()
                                    + vid->directionQuantizationOffset()) & 0xFFu;
                            const int directionIndex = static_cast<int>(
                                (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                            if (directionIndex != ndir)
                                continue;
                        }
                        found = sprite;
                        break;
                    }
                }
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(found));
                return 0;
            }
        case script::NativeFunctionCode::MenuLoad:
{
                const int options = scriptPopIntegerRetail();
                const STRING path = *script->popStringRetail();
                (void)applicationMenu().Load(path, options);
                return 0;
            }
        case script::NativeFunctionCode::MenuRelease:
{
                const STRING path = *script->popStringRetail();
                if (path.isEmpty())
                    applicationFrameSpriteList().deleteAllSprites();
                else
                    (void)applicationMenu().DeleteFromFile(path);
                return 0;
            }
        case script::NativeFunctionCode::MenuNvidUnderCursor:
{
                scriptPushIntegerRetail(applicationMenu().SelectedNvid());
                return 0;
            }
        case script::NativeFunctionCode::MenuNdirUnderCursor:
{
                scriptPushIntegerRetail(applicationMenu().SelectedDirectionFrame());
                return 0;
            }
        case script::NativeFunctionCode::MenuAction:
{
                const int var3 = scriptPopIntegerRetail();
                const int var2 = scriptPopIntegerRetail();
                const int var1 = scriptPopIntegerRetail();
                const int action = scriptPopIntegerRetail();
                const int ndir = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for MenuAction");
                if (vid == MAP::NullVid())
                    return 0;

                SPRITE_LIST& list = applicationFrameSpriteList();
                const int count = list.activeCount();
                for (int i = 0; i < count; ++i)
                {
                    SPRITE* const sprite = list.at(i);
                    if (!sprite || sprite->Vid() != vid)
                        continue;
                    if (ndir != 999999)
                    {
                        const std::uint32_t directionByte =
                            static_cast<std::uint32_t>(sprite->directionIndex()
                                + vid->directionQuantizationOffset()) & 0xFFu;
                        const int directionIndex = static_cast<int>(
                            (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                        if (directionIndex != ndir)
                            continue;
                    }

                    if (action < 0x11)
                        sprite->ChangeAnimation(action);
                    else
                        (void)sprite->dispatchVirtualAction(
                            static_cast<std::uint32_t>(action), var1, var2, var3);
                }
                return 0;
            }
        case script::NativeFunctionCode::MenuCreate:
{
                const int z = scriptPopIntegerRetail();
                const int yDelta = scriptPopIntegerRetail();
                const int y = z + yDelta;
                const int x = scriptPopIntegerRetail();
                const int directionSource = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for MenuCreate");
                if (vid == MAP::NullVid())
                {
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                const int direction = ((directionSource << 8) / static_cast<int>(vid->directionCount())) & 0xFF;
                SPRITE* created = nullptr;
                if (MAP* const map = MAP::Current())
                    created = map->CreateSpriteViaFactory(
                        vid, VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                        ANGLE(direction), nullptr, false);
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(created));
                return 0;
            }
        case script::NativeFunctionCode::MenuLeftClick:
{
                MENU& list = applicationMenu();
                SPRITE* const selected = (list.controlFlags() & 1u) != 0u
                    ? list.selectedSprite()
                    : nullptr;
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(selected));
                return 0;
            }
        case script::NativeFunctionCode::GetInputX:
{
                scriptPushIntegerRetail(static_cast<int>(scriptApplicationInputState254().worldX));
                return 0;
            }
        case script::NativeFunctionCode::GetInputY:
{
                scriptPushIntegerRetail(static_cast<int>(scriptApplicationInputState254().worldY));
                return 0;
            }
        case script::NativeFunctionCode::GetKey:
{
                const int value = static_cast<int>(scriptApplicationInputState254().lastCode);
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::MenuMode:
{
                const int value = scriptPopIntegerRetail();
                if (value != 0)
                {
                    core::Application::beginBucketTimingSnapshot(drawState);
                    if (mouseInstanceRef())
                        mouseInstanceRef()->setCursorId(0);
                }
                else
                {
                    core::Application::endBucketTimingSnapshot(drawState);
                }
                return 0;
            }
        case script::NativeFunctionCode::SetCursor:
{
                const int cursorId = scriptPopIntegerRetail();
                MOUSE* mouse = mouseInstanceRef();
                if (!mouse)
                    return 0;

                if (cursorId == -1)
                {
                    mouse->HardwareOff();
                    return 0;
                }
                if (cursorId == 0x100)
                {
                    mouse->enableHardwareCursorModeRetail();
                    return 0;
                }
                if (cursorId == 0x101)
                {
                    mouse->disableHardwareCursorModeRetail();
                    return 0;
                }

                if (!mouse->cursorHandlesLoaded())
                    mouse->HardwareOn();
                mouse = mouseInstanceRef();
                if (mouse)
                    mouse->setCursorId(cursorId);
                return 0;
            }
        case script::NativeFunctionCode::MessageText:
{
                        const int y = scriptPopIntegerRetail();
                        const int x = scriptPopIntegerRetail();
                        STRING text = *script->popStringRetail();
                #ifdef _WIN32
                        if (win::ApplicationWin* const app = win::applicationWinInstance())
                        {
                            PLAYER* const player = app->startupPlayerSlotByIndex(
                                static_cast<int>(app->activeStartupPlayerIndex()));
                            if (player)
                                player->submitPathCoordinate(&text, static_cast<float>(x), static_cast<float>(y));
                        }
                #endif
                        return 0;
            }
        case script::NativeFunctionCode::GetInputState:
{
                const std::uint32_t state = scriptApplicationInputState254().flags;
                const int bitOrder[] = {15, 14, 9, 10, 8, 7, 12, 11, 6, 5, 2, 0};
                int packed = 0;
                for (int bit : bitOrder)
                    packed = (packed << 1) | static_cast<int>((state >> bit) & 1u);
                scriptPushIntegerRetail(packed);
                return 0;
            }
        case script::NativeFunctionCode::SetShiftCoor:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                if (MAP* const map = MAP::Current())
                    map->SetShiftCoor(static_cast<float>(x), static_cast<float>(y), 0);
                return 0;
            }
        case script::NativeFunctionCode::SetScrollType:
{
                core::SetApplicationScrollType(static_cast<std::uint32_t>(scriptPopIntegerRetail()));
                return 0;
            }
        case script::NativeFunctionCode::GetScrollType:
{
                scriptPushIntegerRetail(static_cast<int>(core::ApplicationScrollType()));
                return 0;
            }
        case script::NativeFunctionCode::ScreenX:
{
                const int value = GRAPH::CurrentGraph()->SizeX();
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::ScreenY:
{
                const int value = GRAPH::CurrentGraph()->SizeY();
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::SetApplicationFlag7:
{
                const int value = scriptPopIntegerRetail();
                std::uint32_t flags = core::ApplicationFlags();
                flags = (flags & ~application_flags::ScriptControlBit7) |
                    (value != 0 ? application_flags::ScriptControlBit7 : 0u);
                core::SetApplicationFlags(flags);
                return 0;
            }
        case script::NativeFunctionCode::PlayerNoop:
{
                (void)scriptPopIntegerRetail();
                // Both active PLAYER virtual slots are intentional no-op handlers.
                return 0;
            }
        case script::NativeFunctionCode::GetString:
{
                assignStringFromString(lpFile, STRING());
                assignStringFromString(lpFile, *script->popStringRetail());

                const STRING section = *script->popStringRetail();

                const STRING& profilePath = core::StartupStringsIniPath();

                STRING defaultValue;
                STRING profileValue;
                core::profile_p::readProfileStringInto(profileValue, profilePath, section, lpFile, defaultValue);
                script->pushStringValue(profileValue);
                profileValue.ReleaseOwnedStorage();
                if (defaultValue.isEmpty())
                    return 0;
                defaultValue.ReleaseOwnedStorage();
                return 0;
            }
        case script::NativeFunctionCode::Exit:
{
                        const STRING reason = *script->popStringRetail();
                        (void)reason;
                #ifdef _WIN32
                        if (win::ApplicationWin* const app = win::applicationWinInstance())
                            if (HWND hwnd = app->nativeWindow())
                                ::PostMessageA(hwnd, WM_CLOSE, 0, 0);
                #endif
                        return 0;
            }
        case script::NativeFunctionCode::ToScreenX:
{
                const int x = scriptPopIntegerRetail();
                const int value = static_cast<int>(static_cast<float>(x) - core::GlobalApplicationDrawDispatcherState().cameraShiftX());
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::ToScreenY:
{
                const int z = scriptPopIntegerRetail();
                const int y = scriptPopIntegerRetail();
                const int value = static_cast<int>(static_cast<float>(y) - static_cast<float>(z) - core::GlobalApplicationDrawDispatcherState().cameraShiftY());
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::MenuRightClick:
{
                MENU& list = applicationMenu();
                SPRITE* const selected = (list.controlFlags() & 2u) != 0u
                    ? list.selectedSprite()
                    : nullptr;
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(selected));
                return 0;
            }
        case script::NativeFunctionCode::SetMouseClick:
{
                const int vkButton = scriptPopIntegerRetail();
                const int firstOrSecond = scriptPopIntegerRetail();
                input::InputControlKeys& keys = input::inputControlKeys();
                if (firstOrSecond == 0x400)
                {
                    keys.first0 = static_cast<std::uint32_t>(vkButton);
                    keys.first1 = static_cast<std::uint32_t>(vkButton);
                    return 0;
                }
                if (firstOrSecond == 0x800)
                {
                    keys.second0 = static_cast<std::uint32_t>(vkButton);
                    keys.second1 = static_cast<std::uint32_t>(vkButton);
                    return 0;
                }
                return 0;
            }
        case script::NativeFunctionCode::CursorAction:
{
                const int var1 = scriptPopIntegerRetail();
                const int var2 = scriptPopIntegerRetail();
                if (Mouse)
                    Mouse->Action(63, static_cast<std::intptr_t>(var2), var1, 0);
                return 0;
            }
        case script::NativeFunctionCode::SetSoundVolume:
{
                const int volume = scriptPopIntegerRetail();
                sound::GlobalSoundEngine()->setMasterVolumePercent(volume);
                return 0;
            }
        case script::NativeFunctionCode::SetMusicVolume:
{
                const int volume = scriptPopIntegerRetail();
                sound::GlobalSoundEngine()->setMusicVolumePercent(volume);
                return 0;
            }
        case script::NativeFunctionCode::PlaySfx:
{
                const int nsfx = scriptPopIntegerRetail();
                sound::GlobalSoundEngine()->enqueueSoundRequest(nsfx, 0, 0);
                return 0;
            }
        case script::NativeFunctionCode::StopSfx:
{
                const int nsfx = scriptPopIntegerRetail();
                sound::GlobalSoundEngine()->stopSoundNumber(nsfx);
                return 0;
            }
        case script::NativeFunctionCode::StopMusic:
{
                sound::GlobalSoundEngine()->closeMusicPath();
                return 0;
            }
        case script::NativeFunctionCode::PlaySfxFromCoor:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                const int nsfx = scriptPopIntegerRetail();
                GRAPH* const graph = GRAPH::CurrentGraph();
                const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
                const float halfScreenX = static_cast<float>(graph->SizeX()) * 0.5f;
                const float halfScreenY = static_cast<float>(graph->SizeY()) * 0.5f;
                const float soundX = static_cast<float>(x) - drawState.cameraShiftX() - halfScreenX;
                const float soundY = static_cast<float>(y) - drawState.cameraShiftY() - halfScreenY;
                sound::GlobalSoundEngine()->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);
                return 0;
            }
        case script::NativeFunctionCode::PlayMusicFile:
{
                const int loop = scriptPopIntegerRetail();

                const STRING filename = *script->popStringRetail();
                sound::GlobalSoundEngine()->playMusicFile(filename.c_str(), loop);
                return 0;
            }
        case script::NativeFunctionCode::Effect:
{
                const int duration = scriptPopIntegerRetail();
                const int var2 = scriptPopIntegerRetail();
                const int var1 = scriptPopIntegerRetail();
                const int effect = scriptPopIntegerRetail();
                (void)GRAPH::CurrentGraph()->setEffect(effect, var1, var2, duration);
                return 0;
            }
        case script::NativeFunctionCode::SetEnvironment:
{
                const int value = scriptPopIntegerRetail();
                (void)GRAPH::CurrentGraph()->updateRenderFlags(static_cast<std::uint32_t>(value));
                return 0;
            }
        case script::NativeFunctionCode::SetGraphDetail:
        case script::NativeFunctionCode::SetAutoReBirth:
{
                (void)scriptPopIntegerRetail();
                return 0;
            }
        case script::NativeFunctionCode::SetGamma:
{
                const int gammaIndex = scriptPopIntegerRetail();
                std::uint32_t diffuse = 0;
                std::uint32_t specular = 0;
                scriptNativeDecodeGammaIndex(gammaIndex, diffuse, specular);
                GRAPH::CurrentGraph()->setGamma(diffuse, specular);
                return 0;
            }
        case script::NativeFunctionCode::SetWind:
{
                const int direct = scriptPopIntegerRetail();
                const int wind = scriptPopIntegerRetail();
                GRAPH::CurrentGraph()->SetWind(static_cast<std::uint32_t>(direct) & 0xFFu,
                                               static_cast<float>(wind) * 0.001f);
                return 0;
            }
        case script::NativeFunctionCode::PlayMovie:
{
                const STRING filename = *script->popStringRetail();
                GRAPH::CurrentGraph()->playMovieCentered(filename);
                return 0;
            }
        case script::NativeFunctionCode::IsPlayMovie:
{
                const int value = GRAPH::CurrentGraph()->movieComObject(0) != nullptr ? 1 : 0;
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::StopMovie:
{
                GRAPH::CurrentGraph()->releaseMoviePlayback();
                return 0;
            }
        case script::NativeFunctionCode::IsPlayMusic:
{
                const int value = sound::GlobalSoundEngine()->isMusicPlaying() ? 1 : 0;
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::CountGamma:
{
                const int time = scriptPopIntegerRetail();
                const int g2 = scriptPopIntegerRetail();
                const int g1 = scriptPopIntegerRetail();
                scriptPushIntegerRetail(interpolateGammaColor(g1, g2, time));
                return 0;
            }
        case script::NativeFunctionCode::GetGamma:
{
                const GammaRawPair& gamma = GRAPH::CurrentGraph()->rawGammaPair();
                std::uint32_t out = (gamma.first >> 1) & 0x7F7F7F7Fu;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const std::uint32_t specByte = (gamma.second >> shift) & 0xFFu;
                    if (specByte != 0)
                    {
                        const std::uint32_t packedByte = 0x80u | (((~specByte) & 0xFEu) >> 1);
                        out = (out & ~(0xFFu << shift)) | ((packedByte & 0xFFu) << shift);
                    }
                }
                scriptPushIntegerRetail(static_cast<int>(out));
                return 0;
            }
        case script::NativeFunctionCode::GetEffectState:
{
                const int effect = scriptPopIntegerRetail();
                scriptPushIntegerRetail(GRAPH::CurrentGraph()->getEffectState(effect));
                return 0;
            }
        case script::NativeFunctionCode::GetPrevMapName:
{
                script->pushStringValue(core::ApplicationPreviousMapName());
                return 0;
            }
        case script::NativeFunctionCode::SetGraphScreen:
{
                const int fullscreen = scriptPopIntegerRetail();
                const int sizeY = scriptPopIntegerRetail();
                const int sizeX = scriptPopIntegerRetail();
                if (GRAPH* const graph = GRAPH::CurrentGraph())
                    graph->queueSteamDisplayChange(sizeX, sizeY, fullscreen);
                return 0;
            }
        case script::NativeFunctionCode::GetMapName:
{
                script->pushStringValue(core::ApplicationCurrentMapName());
                return 0;
            }
        case script::NativeFunctionCode::ExecuteShellFile:
{
                lpFile.Assign(script->popStringRetail()->c_str());
                LOG::Write("Exec '%s'", lpFile.c_str());

                STRING shellParameters = lpFile.RightOfFirst(" ");
                STRING shellExecutable = lpFile.LeftOfFirst(" ");
#ifdef _WIN32
                ShellExecuteA(nullptr,
                              nullptr,
                              shellExecutable.c_str(),
                              shellParameters.c_str(),
                              nullptr,
                              5);
#endif
                return 0;
            }
        case script::NativeFunctionCode::CharAt:
{
                const int index = scriptPopIntegerRetail();
                const STRING text = *script->popStringRetail();

                const int value = static_cast<int>(static_cast<signed char>(text.c_str()[index]));
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::Log:
{

                const STRING text = *script->popStringRetail();
                LOG::Write(text.c_str());
                return 0;
            }
        case script::NativeFunctionCode::Random:
{
                const int maxValue = scriptPopIntegerRetail();
                const int divisor = maxValue + 1;
                const int value = std::rand() % divisor;
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::ChangeZUnit:
{
                const int z = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for ChangeZUnit");
                if (vid == MAP::NullVid())
                    return 0;

                const int pass = vid->renderLayer();
                int cursor = drawState.drawPassBucket(pass).count();
                SPRITE* sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == vid)
                        sprite->ChangeCoor(sprite->X(), sprite->Y(), static_cast<float>(z));
                    sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::GetTime:
{
                scriptPushIntegerRetail(static_cast<int>(core::CurrentTimeMilliseconds()));
                return 0;
            }
        case script::NativeFunctionCode::GetGroundZ:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                const int value = static_cast<int>(MAP::Current()->GetGroundZ(VECTOR2{static_cast<float>(x), static_cast<float>(y)}));
                scriptPushIntegerRetail(value);
                return 0;
            }
        case script::NativeFunctionCode::StringLength:
        case script::NativeFunctionCode::StringLengthCompat:
{
                const STRING& text = *script->popStringRetail();
                scriptPushIntegerRetail(text.Length());
                return 0;
            }
        case script::NativeFunctionCode::SetFlagman:
{
                const int spriteHandle = script->popSpriteReferenceValue();
                const int playerIndex = scriptPopIntegerRetail();
                MAP::Current()->SetFlagman(playerIndex, scriptResolveSpriteReference(spriteHandle));
                return 0;
            }
        case script::NativeFunctionCode::AskPlace:
{
                const int z = scriptPopIntegerRetail();
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for CanPlace");

                int handle = 0;
                if (MAP* const map = MAP::Current())
                {
                    if (SPRITE* const hit = GlobalHashQueryCellCollisionByVid(
                            *map, vid, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)))
                    {
                        handle = scriptSpritePointerValue(hit);
                    }
                }
                scriptPushSpriteReferenceRetail(handle);
                return 0;
            }
        case script::NativeFunctionCode::GetVidData:
{
                const int type = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for GetVid");
                if (vid == MAP::NullVid())
                {
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                switch (static_cast<script::VidDataCode>(type))
                {
                case script::VidDataCode::MaxHp:
                    scriptPushIntegerRetail(static_cast<int>(vid->maxHp));
                    return 0;
                case script::VidDataCode::BattleRange:
                    scriptPushIntegerRetail(static_cast<int>(vid->weaponBattleRange()));
                    return 0;
                case script::VidDataCode::Ammo:
                    scriptPushIntegerRetail(vid->activeWeaponAmmoCapacity());
                    return 0;
                case script::VidDataCode::Name:
                    script->pushStringResult(vid->scriptName());
                    return 0;
                case script::VidDataCode::Count:
                    scriptPushIntegerRetail(vid->totalSpriteCount());
                    return 0;
                case script::VidDataCode::KilledUnit:
                    scriptPushIntegerRetail(vid->totalKilledUnitCount());
                    return 0;
                case script::VidDataCode::KilledUnitArmy0:
                case script::VidDataCode::KilledUnitArmy1:
                case script::VidDataCode::KilledUnitArmy2:
                case script::VidDataCode::KilledUnitArmy3:
                    scriptPushIntegerRetail(vid->killedUnitCountForArmy(type - script::toInt(script::VidDataCode::KilledUnitArmy0)));
                    return 0;
                case script::VidDataCode::CountArmy0:
                case script::VidDataCode::CountArmy1:
                case script::VidDataCode::CountArmy2:
                case script::VidDataCode::CountArmy3:
                    scriptPushIntegerRetail(vid->spriteCountForBucket(type - script::toInt(script::VidDataCode::CountArmy0)));
                    return 0;
                case script::VidDataCode::MaxHpArmy0:
                case script::VidDataCode::MaxHpArmy1:
                case script::VidDataCode::MaxHpArmy2:
                case script::VidDataCode::MaxHpArmy3:
                    scriptPushIntegerRetail(vid->frameTimeForBucket(type - script::toInt(script::VidDataCode::MaxHpArmy0)));
                    return 0;
                case script::VidDataCode::SpriteType:
                    scriptPushIntegerRetail(static_cast<int>(vid->spriteTypeId()));
                    return 0;
                case script::VidDataCode::Class:
                    scriptPushIntegerRetail(static_cast<int>(vid->spriteClassId()));
                    return 0;
                case script::VidDataCode::Speed:
                    scriptPushIntegerRetail(vid->maxSpeedValue() == 999999.0f
                        ? 999999
                        : static_cast<int>(vid->maxSpeedValue() * 1000.0f));
                    return 0;
                case script::VidDataCode::Lifetime:
                    scriptPushIntegerRetail(vid->lifetimeValue());
                    return 0;
                case script::VidDataCode::DetectRange:
                    scriptPushIntegerRetail(static_cast<int>(vid->weaponDetectRange()));
                    return 0;
                case script::VidDataCode::WeaponAim:
                    scriptPushIntegerRetail(static_cast<int>(vid->weaponAim()));
                    return 0;
                case script::VidDataCode::DirectionCount:
                    scriptPushIntegerRetail(vid->directionCount());
                    return 0;
                case script::VidDataCode::MoveMask:
                    scriptPushIntegerRetail(static_cast<int>(vid->movementMask()));
                    return 0;
                case script::VidDataCode::BuildTime:
                    scriptPushIntegerRetail(vid->weaponBuildTime());
                    return 0;
                case script::VidDataCode::Hide:
                    scriptPushIntegerRetail(vid->hasPropertyBit400());
                    return 0;
                case script::VidDataCode::NotCreateAsChild:
                    scriptPushIntegerRetail(vid->notCreateAsChild());
                    return 0;
                case script::VidDataCode::FrameSpeed:
                    scriptPushIntegerRetail(static_cast<int>(vid->defaultFrameSpeed()));
                    return 0;
                case script::VidDataCode::Link:
                    scriptPushIntegerRetail(vid->linkedVid() ? vid->linkedVid()->nvid() : 0);
                    return 0;
                case script::VidDataCode::Damage:
                    scriptPushIntegerRetail(vid->deathDamageMinimumRawBits());
                    return 0;
                case script::VidDataCode::RecolorUnit:
                    scriptPushIntegerRetail(vid->totalRecolorUnitCount());
                    return 0;
                case script::VidDataCode::RecolorUnitArmy0:
                case script::VidDataCode::RecolorUnitArmy1:
                case script::VidDataCode::RecolorUnitArmy2:
                case script::VidDataCode::RecolorUnitArmy3:
                    scriptPushIntegerRetail(vid->recolorUnitCountForArmy(type - script::toInt(script::VidDataCode::RecolorUnitArmy0)));
                    return 0;
                default:
                    break;
                }


                if (type >= script::VidChildFirst && type < script::VidChildEnd)
                {
                    VID* child = vid->childVidForDataCode(type);
                    if (!child)
                        return (scriptPushIntegerRetail(0), 0);
                    scriptPushIntegerRetail(child->nvid());
                    return 0;
                }

                if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
                {
                    scriptPushIntegerRetail(vid->noChildValueForDataCode(type));
                    return 0;
                }

                script->reportScriptError(0x0E, "GetVid type", type);
                return 0;

            }
        case script::NativeFunctionCode::SetVidData:
{
                const int value = scriptPopIntegerRetail();
                const int type = scriptPopIntegerRetail();
                VID* vid = scriptPopVidRetail("for SetVid");
                if (vid == MAP::NullVid())
                    return 0;

                switch (static_cast<script::VidDataCode>(type))
                {
                case script::VidDataCode::MaxHp:
                    vid->maxHp = value;
                    return 0;
                case script::VidDataCode::Ammo:
                {
                    VID* target = vid;
                    VID* link = vid->linkedVid();
                    if (link && link->CanFight() != 0)
                        target = link;
                    target->setWeaponRecordAmmoCapacity(value);
                    return 0;
                }
                case script::VidDataCode::KilledUnit:
                    vid->setKilledUnitCountForArmy(3, value);
                    vid->setKilledUnitCountForArmy(2, value);
                    vid->setKilledUnitCountForArmy(1, value);
                case script::VidDataCode::KilledUnitArmy0:
                    vid->setKilledUnitCountForArmy(0, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy1:
                    vid->setKilledUnitCountForArmy(1, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy2:
                    vid->setKilledUnitCountForArmy(2, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy3:
                    vid->setKilledUnitCountForArmy(3, value);
                    return 0;
                case script::VidDataCode::MaxHpArmy0:
                case script::VidDataCode::MaxHpArmy1:
                case script::VidDataCode::MaxHpArmy2:
                case script::VidDataCode::MaxHpArmy3:
                    vid->setBucketFrameTime(type - script::toInt(script::VidDataCode::MaxHpArmy0), value);
                    return 0;
                case script::VidDataCode::HpCoeffArmy0:
                case script::VidDataCode::HpCoeffArmy1:
                case script::VidDataCode::HpCoeffArmy2:
                case script::VidDataCode::HpCoeffArmy3:
                    vid->setBucketFramePercent(type - script::toInt(script::VidDataCode::HpCoeffArmy0), value);
                    return 0;
                case script::VidDataCode::Speed:
                {
                    vid->setMaxSpeedValue(value == 999999 ? 999999.0f : static_cast<float>(value) * 0.001f);
                    const core::ApplicationDrawPassBucket& bucket =
                        core::GlobalApplicationDrawDispatcherState().drawPassBucket(vid->renderLayer());
                    SPRITE* const* slots = bucket.data();
                    for (int i = bucket.count() - 1; i >= 0; --i)
                    {
                        SPRITE* const sprite = slots ? slots[i] : nullptr;
                        if (sprite && sprite->Vid() == vid)
                            sprite->syncActionAuxMaxSpeedFromVid();
                    }
                    return 0;
                }
                case script::VidDataCode::Lifetime:
                {
                    if (vid->nvid() == 0)
                        return 0;
                    vid->setLifetimeValue(value);
                    vid->setActionAuxStateRequired(1);
                    return 0;
                }
                case script::VidDataCode::DetectRange:
                    vid->setWeaponDetectRange(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::WeaponAim:
                    vid->setWeaponAim(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::ExchangeVid:
                    if (!isValidNvid(value))
                    {
                        script->reportScriptError(4, "SetVid get_image", value);
                        return 0;
                    }
                    (void)MAP::Current()->swapVidReferences(vid, resolveVidByNvid(value));
                    return 0;
                case script::VidDataCode::MoveMask:
                    vid->setMovementMask(static_cast<DWORD>(value));
                    return 0;
                case script::VidDataCode::BuildTime:
                    vid->setWeaponBuildTime(value);
                    return 0;
                case script::VidDataCode::Hide:
                    vid->setLinkedPropertyBit400(value);
                    return 0;
                case script::VidDataCode::NotCreateAsChild:
                    vid->setNotCreateAsChild(value);
                    return 0;
                case script::VidDataCode::FrameSpeed:
                    vid->setAllFrameSpeeds(value);
                    return 0;
                case script::VidDataCode::Link:
                {
                    vid->setLinkedVid(resolveVidByNvid(value));
                    return 0;
                }
                case script::VidDataCode::Damage:
                    vid->setDeathDamageMinimumRawBits(value);
                    return 0;
                default:
                    break;
                }


                if (!vid)
                    return 0;

                if (type >= script::VidChildFirst && type < script::VidChildEnd)
                {
                    if (value == 0)
                    {
                        vid->setChildNvidForDataCode(type, 0);
                        vid->setChildVidForDataCode(type, nullptr);
                        return 0;
                    }

                    const int absValue = value < 0 ? -value : value;
                    const bool validChildSlot = isValidNvid(absValue);
                    if (!validChildSlot)
                    {
                        script->reportScriptError(4, "SetVid child", value);
                        return 0;
                    }

                    VID* childForStore = resolveVidByNvid(absValue);
                    vid->setChildNvidForDataCode(type, value);
                    vid->setChildVidForDataCode(type, childForStore);
                    VID* childForFlag = resolveVidByNvid(absValue);
                    if (!childForFlag || childForFlag->PropBirthAsSmoke() == 0)
                        return 0;
                    vid->setActionAuxStateRequired(vid->actionAuxStateRequired() | 1);
                    return 0;
                }

                if (type >= script::toInt(script::VidDataCode::Gamma0) && type <= script::toInt(script::VidDataCode::Gamma3))
                {
                    const GammaRawPair rawGamma = script->decodePackedGamma(value);
                    vid->SetGammaRaw(rawGamma, static_cast<unsigned>(type - script::toInt(script::VidDataCode::Gamma0)));
                    return 0;
                }

                if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
                {
                    vid->setNoChildValueForDataCode(type, value);
                    return 0;
                }

                script->reportScriptError(0x0E, "SetVid type", type);
                return 0;

            }
        case script::NativeFunctionCode::IntToString:
{
                const int value = scriptPopIntegerRetail();
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%d", value);
                script->pushStringResult(STRING(buffer));
                return 0;
            }
        case script::NativeFunctionCode::Sin:
{
                const int angle = scriptPopIntegerRetail();
                scriptPushIntegerRetail(scriptNativeSin1024(angle));
                return 0;
            }
        case script::NativeFunctionCode::Cos:
{
                const int angle = scriptPopIntegerRetail();
                scriptPushIntegerRetail(scriptNativeCos1024(angle));
                return 0;
            }
        case script::NativeFunctionCode::MapSizeX:
{
                #ifdef _WIN32
                        const int value = static_cast<int>(win::applicationWinInstance()->mapExtentX());
                #else
                        const int value = static_cast<int>(core::ApplicationMapWidth());
                #endif
                        scriptPushIntegerRetail(value);
                        return 0;
            }
        case script::NativeFunctionCode::MapSizeY:
{
                #ifdef _WIN32
                        const int value = static_cast<int>(win::applicationWinInstance()->mapExtentY());
                #else
                        const int value = static_cast<int>(core::ApplicationMapHeight());
                #endif
                        scriptPushIntegerRetail(value);
                        return 0;
            }
        case script::NativeFunctionCode::Genocide:
{
                VID* const vid = scriptPopVidRetail("for Genocide");
                if (!vid || vid == MAP::NullVid())
                    return 0;

                const int pass = vid->renderLayer();
                int cursor = 0;
                SPRITE* sprite = beginReverseDrawPassIteration(drawState, pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == vid)
                        DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                    sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::ReplaceUnit:
{
                VID* const replacement = scriptPopVidRetail("for Replace Unit 2");
                VID* const source = scriptPopVidRetail("for Replace Unit 1");
                if (replacement == MAP::NullVid() || source == MAP::NullVid())
                    return 0;

                const int pass = source->renderLayer();
                int cursor = 0;
                SPRITE* sprite = beginReverseDrawPassIteration(drawState, pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == source)
                    {
                        if (MAP* const map = MAP::Current())
                            map->CreateSpriteViaFactory(
                                replacement, VECTOR(sprite->X(), sprite->Y(), sprite->Z()),
                                ANGLE(sprite->directionIndex()), nullptr, false);
                        DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                    }
                    sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::Crc:
{
                const STRING text = *script->popStringRetail();
                const Crc32 crc(text.c_str(), static_cast<unsigned int>(text.Length()));
                scriptPushIntegerRetail(static_cast<int>(crc.Value()));
                return 0;
            }
        case script::NativeFunctionCode::Printf:
{
                if (script->topValueIsString())
                {
                    const STRING value = *script->popStringRetail();
                    STRING localValue;
                    copyConstructString(localValue, value);

                    const char* valueText = script->stringTextPointer(localValue);
                    const STRING format = *script->popStringRetail();
                    const char* formatText = script->stringTextPointer(format);

                    const STRING formatted = STRING::Format(formatText, valueText);
                    script->pushStringResult(formatted);
                    return 0;
                }

                const int value = scriptPopIntegerRetail();
                const STRING format = *script->popStringRetail();
                const char* formatText = script->stringTextPointer(format);
                const STRING formatted = STRING::Format(formatText, value);
                script->pushStringResult(formatted);
                return 0;
            }
        case script::NativeFunctionCode::ReloadVid:
{

                (void)MAP::Current()->reloadGameResourceParameters();
                return 0;
            }
        case script::NativeFunctionCode::FileWrite:
{
                const STRING value = *script->popStringRetail();
                const int fileValue = scriptPopIntegerRetail();
                if (fileValue == 0)
                    return 0;

                std::FILE* file = scriptNativeFileFromInt(fileValue);
                if (!file)
                    return 0;

                script->writeCStringRecord(value, file);
                std::fseek(file, -1, SEEK_CUR);
                std::fputs("\n", file);
                return 0;
            }
        case script::NativeFunctionCode::FileRead:
{
                const int fileValue = scriptPopIntegerRetail();
                STRING lpFile;
                RESOURCE& demoResource = MAP::Current()->demoResource();

                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                {
                    readStringLineFromStream(lpFile, &demoResource);
                }
                else if (fileValue != 0)
                {
                    readStringLineFromFile(lpFile, scriptNativeFileFromInt(fileValue));
                }

                if ((core::ApplicationFlags() & application_flags::DemoWriteToResource) != 0)
                    script->writeCStringToStream(lpFile, &demoResource);

                script->pushStringResult(lpFile);
                return 0;
            }
        case script::NativeFunctionCode::FileOpen:
{
                const STRING filename = *script->popStringRetail();
                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                {
                    scriptPushIntegerRetail(0);
                    return 0;
                }

                std::FILE* file = script->openScriptFile(filename, "r+t");
                if (!file)
                    LOG::ResourceError("SCRIPT", 7, script->stringTextPointer(filename), 0);
                scriptPushIntegerRetail(scriptNativeIntFromFile(file));
                return 0;
            }
        case script::NativeFunctionCode::FileClose:
{
                const int fileValue = scriptPopIntegerRetail();
                if (fileValue == 0)
                    return 0;
                if (std::FILE* file = scriptNativeFileFromInt(fileValue))
                    std::fclose(file);
                return 0;
            }
        case script::NativeFunctionCode::FileCreate:
{
                const STRING filename = *script->popStringRetail();
                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                    return (scriptPushIntegerRetail(0), 0);

                std::FILE* file = script->openScriptFile(filename, "w+t");
                scriptPushIntegerRetail(scriptNativeIntFromFile(file));
                return 0;
            }
        case script::NativeFunctionCode::FileEof:
{
                const int fileValue = scriptPopIntegerRetail();
                if (fileValue == 0)
                    return (scriptPushIntegerRetail(1), 0);

                std::FILE* file = scriptNativeFileFromInt(fileValue);
                scriptPushIntegerRetail(file && std::feof(file) ? 0x10 : 0);
                return 0;
            }
        case script::NativeFunctionCode::FileDataSave:
{
                const STRING value = *script->popStringRetail();
                const STRING path = *script->popStringRetail();
                FileDataSave(path, value);
                return 0;
            }
        case script::NativeFunctionCode::FileDataLoad:
{
                const STRING defaultValue = *script->popStringRetail();
                const STRING path = *script->popStringRetail();
                script->pushStringResult(FileDataLoad(path, defaultValue));
                return 0;
            }
        case script::NativeFunctionCode::FileExists:
{
                scriptPushIntegerRetail(FileDataFileExists(*script->popStringRetail()));
                return 0;
            }
        case script::NativeFunctionCode::SaveFolder:
{
                script->pushStringResult(STRING(FileDataSaveFolder()));
                return 0;
            }
        case script::NativeFunctionCode::StringLower:
{
                script->pushStringValue(script->popStringRetail()->ToLower());
                return 0;
            }
        case script::NativeFunctionCode::StringUpper:
{
                script->pushStringValue(script->popStringRetail()->ToUpper());
                return 0;
            }
        case script::NativeFunctionCode::ToBase64:
{
                const int key = scriptPopIntegerRetail();
                const STRING text = *script->popStringRetail();
                script->pushStringValue(text.ToBase64(key));
                return 0;
            }
        case script::NativeFunctionCode::StoreSetAchievement:
{
                const STRING id = *script->popStringRetail();
                steam::SetAchievement(id.c_str());
                return 0;
            }
        case script::NativeFunctionCode::StoreGetAchievement:
{
                const STRING id = *script->popStringRetail();
                scriptPushIntegerRetail(steam::GetAchievement(id.c_str()));
                return 0;
            }
        case script::NativeFunctionCode::StoreClearAchievement:
{
                const STRING achievement = *script->popStringRetail();
                steam::ClearAchievement(achievement.c_str());
                return 0;
            }
        case script::NativeFunctionCode::StoreResetAllStats:
{
                steam::ResetAllStats();
                return 0;
            }
        case script::NativeFunctionCode::StoreSetStat:
{
                const int value = scriptPopIntegerRetail();
                const STRING id = *script->popStringRetail();
                steam::SetStat(id.c_str(), value);
                return 0;
            }
        case script::NativeFunctionCode::StoreGetStat:
{
                const STRING id = *script->popStringRetail();
                scriptPushIntegerRetail(steam::GetStat(id.c_str()));
                return 0;
            }
        case script::NativeFunctionCode::StoreSaveStatsIfNeeded:
{
                steam::SaveStatsIfNeeded();
                return 0;
            }
        case script::NativeFunctionCode::StoreActivateGameOverlayToStore:
{
                steam::ActivateStore(scriptPopIntegerRetail());
                return 0;
            }
        case script::NativeFunctionCode::StoreInitLeaderboards:
{
                const STRING names = *script->popStringRetail();
                steam::InitLeaderboards(names.c_str());
                return 0;
            }
        case script::NativeFunctionCode::StoreUpdateLeaderboard:
{
                const int score = scriptPopIntegerRetail();
                const STRING name = *script->popStringRetail();
                steam::UpdateLeaderboard(name.c_str(), score);
                return 0;
            }
        case script::NativeFunctionCode::StoreDownloadLeaderboardEntries:
{
                const int offset = scriptPopIntegerRetail();
                const int count = scriptPopIntegerRetail();
                const STRING name = *script->popStringRetail();
                scriptPushIntegerRetail(steam::DownloadLeaderboardEntries(name.c_str(), count, offset));
                return 0;
            }
        case script::NativeFunctionCode::StoreGetLeaderboardEntriesName:
{
                script->pushStringResult(STRING(steam::LeaderboardEntryName(scriptPopIntegerRetail())));
                return 0;
            }
        case script::NativeFunctionCode::StoreGetLeaderboardEntriesScore:
{
                scriptPushIntegerRetail(steam::LeaderboardEntryScore(scriptPopIntegerRetail()));
                return 0;
            }
        case script::NativeFunctionCode::StoreGetUpdateLeaderboardRank:
{
                scriptPushIntegerRetail(steam::LastUploadRank());
                return 0;
            }
        case script::NativeFunctionCode::SetSemaphore:
{
                const int unused = scriptPopIntegerRetail();
                const int value = scriptPopIntegerRetail();
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                core::setNearestLinkValue(&core::globalWeakControllerMap(), x, y, value, unused);
                return 0;
            }
        case script::NativeFunctionCode::BreakTrain:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                if (SPRITE* const sprite = script->popSpriteReference())
                    sprite->splitEngineChainAtPosition(static_cast<float>(x), static_cast<float>(y));
                return 0;
            }
        case script::NativeFunctionCode::FirstTrain:
{
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(beginRootUnitArmyIteration(scriptPopIntegerRetail())));
                return 0;
            }
        case script::NativeFunctionCode::NextTrain:
{
                scriptPushSpriteReferenceRetail(scriptSpritePointerValue(continueRootUnitArmyIteration()));
                return 0;
            }
        case script::NativeFunctionCode::PatrolEngine:
{
                const int y = scriptPopIntegerRetail();
                const int x = scriptPopIntegerRetail();
                SPRITE* const sprite = script->popSpriteReference();
                if (sprite && sprite->Vid()->spriteClassId() == 21)
                {
                    sprite->dispatchEnginePrivateCommandAtPathPoint(25, x, y);
                    return 0;
                }
                LOG::Write(u8"Борис, у тебя в PatrolTrain - train неверный %X",
                           static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sprite)));
                return 0;
            }
        case script::NativeFunctionCode::SetPushLine:
{
                const int value = scriptPopIntegerRetail();
                const int y2 = scriptPopIntegerRetail();
                const int x2 = scriptPopIntegerRetail();
                const int y1 = scriptPopIntegerRetail();
                const int x1 = scriptPopIntegerRetail();
                core::setPushLine(&core::globalWeakControllerMap(), x1, y1, x2, y2, value);
                return 0;
            }
        case script::NativeFunctionCode::GetScreenInputX:
{
                scriptPushIntegerRetail(static_cast<int>(scriptApplicationInputState254().clientX));
                return 0;
            }
        case script::NativeFunctionCode::GetScreenInputY:
{
                scriptPushIntegerRetail(static_cast<int>(scriptApplicationInputState254().clientY));
                return 0;
            }
        case script::NativeFunctionCode::PlayerPathFlag:
{
                const int value = scriptPopIntegerRetail();
                scriptPlayerSlot(1)->setPathSecondaryFlag(value);
                return 0;
            }
        case script::NativeFunctionCode::AddUnitLimit:
{
                const int index = scriptPopIntegerRetail();
                VID* const vid = scriptPopVidRetail("for AddUnitLimit");
                const int value = scriptPopIntegerRetail();
                if (vid != MAP::NullVid())
                    vid->setUnitLimit(index, value);
                return 0;
            }
        case script::NativeFunctionCode::SetEnemyCanAttackNeutralTrains:
{
                const int value = scriptPopIntegerRetail();
                const std::uint32_t bit = (value != 0) ? application_flags::EnemyCanAttackNeutralTrains : 0u;
                const std::uint32_t flags = (core::ApplicationFlags() & ~application_flags::EnemyCanAttackNeutralTrains) | bit;
                core::SetApplicationFlags(flags);
                return 0;
            }
        case script::NativeFunctionCode::SetMoney:
{
                const int value = scriptPopIntegerRetail();
                const int playerIndex = scriptPopIntegerRetail();
                scriptPlayerSlot(playerIndex)->setMoney(static_cast<DWORD>(value));
                return 0;
            }
        case script::NativeFunctionCode::GetMoney:
{
                const int playerIndex = scriptPopIntegerRetail();
                scriptPushIntegerRetail(static_cast<int>(scriptPlayerSlot(playerIndex)->getMoney()));
                return 0;
            }
        case script::NativeFunctionCode::Noop253:
{
                (void)scriptPopIntegerRetail();
                (void)scriptPopIntegerRetail();
                (void)script->popSpriteReference();
                return 0;
            }
        case script::NativeFunctionCode::Noop254:
{
                (void)script->popSpriteReference();
                (void)script->popSpriteReference();
                return 0;
            }
        default:
            writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: Unknown extern Function %i", opcode);
            return 0;
        }
    }


    SPRITE* SCRIPT::popSpriteReference()
    {
        return scriptResolveSpriteReference(popSpriteReferenceValue());
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    void SCRIPT::pushSpriteReference(SPRITE* sprite)
    {
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
    }
#endif






























































































































































































}
