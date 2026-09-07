#include "lgc_script.h"
#if !defined(_MSC_VER) || !defined(_M_IX86)
#include <map>
#include <memory>
#endif
#include <cstdint>
#include <cstring>

namespace as1
{
    namespace
    {
#if !defined(_MSC_VER) || !defined(_M_IX86)
        struct ScriptHostLookupCache
        {
            const SCRIPT* owner = nullptr;
            ScriptHostState* state = nullptr;
        };

        ScriptHostLookupCache& scriptHostLookupCache() noexcept
        {
            static ScriptHostLookupCache cache;
            return cache;
        }

        std::map<const SCRIPT*, std::unique_ptr<ScriptHostState>>& scriptHostStates()
        {
            static auto* states = new std::map<const SCRIPT*, std::unique_ptr<ScriptHostState>>();
            return *states;
        }

        ScriptHostState& ensureScriptHostState(const SCRIPT* owner)
        {
            ScriptHostLookupCache& cache = scriptHostLookupCache();
            if (cache.owner == owner && cache.state != nullptr)
                return *cache.state;
            auto& states = scriptHostStates();
            auto it = states.find(owner);
            if (it == states.end())
                it = states.emplace(owner, std::make_unique<ScriptHostState>()).first;
            cache.owner = owner;
            cache.state = it->second.get();
            return *cache.state;
        }

        void eraseScriptHostState(const SCRIPT* owner) noexcept
        {
            ScriptHostLookupCache& cache = scriptHostLookupCache();
            if (cache.owner == owner)
            {
                cache.owner = nullptr;
                cache.state = nullptr;
            }
            scriptHostStates().erase(owner);
        }
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
        using ScriptListDeletingDestructor = void* (__fastcall*)(void*, void*, unsigned char);
        void* __fastcall scriptStackListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);
        void* __fastcall scriptFunctionListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);
        void* __fastcall scriptDefineListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);

        ScriptListDeletingDestructor scriptStackListVtable[] = { &scriptStackListDeletingDestructor };
        ScriptListDeletingDestructor scriptFunctionListInitialVtable[] = { &scriptFunctionListDeletingDestructor };
        ScriptListDeletingDestructor scriptDefineListInitialVtable[] = { &scriptDefineListDeletingDestructor };
        ScriptListDeletingDestructor scriptFunctionListFinalVtable[] = { &scriptFunctionListDeletingDestructor };
        ScriptListDeletingDestructor scriptDefineListFinalVtable[] = { &scriptDefineListDeletingDestructor };

        std::uint32_t currentPointerToken(const void* pointer) noexcept
        {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
        }

        void destroyScriptFunctionListSubobject(void* rawThis) noexcept
        {
            auto* owner = reinterpret_cast<SCRIPT*>(static_cast<unsigned char*>(rawThis) - 0x10u);
            auto* physical = reinterpret_cast<ScriptPhysicalLayout*>(owner);
            physical->functionListVtable = currentPointerToken(scriptFunctionListFinalVtable);
            auto* records = reinterpret_cast<script::LogicFunctionRecord*>(
                static_cast<std::uintptr_t>(physical->functionTableToken));
            if (records)
                script::destroyLogicFunctionRecordStorage(records, 3);
            physical->functionTableToken = 0u;
            physical->functionCount = 0;
        }

        void destroyScriptDefineListSubobject(void* rawThis) noexcept
        {
            auto* owner = reinterpret_cast<SCRIPT*>(static_cast<unsigned char*>(rawThis) - 0x20u);
            auto* physical = reinterpret_cast<ScriptPhysicalLayout*>(owner);
            physical->defineListVtable = currentPointerToken(scriptDefineListFinalVtable);
            auto* records = reinterpret_cast<ScriptDefinePairRecord*>(
                static_cast<std::uintptr_t>(physical->defineTableToken));
            if (records)
                scriptDefinePairDeletingDestructor(records, 3);
            physical->defineTableToken = 0u;
            physical->defineCount = 0;
        }

        void* __fastcall scriptStackListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            auto* owner = reinterpret_cast<SCRIPT*>(rawThis);
            auto* physical = reinterpret_cast<ScriptPhysicalLayout*>(owner);
            physical->stackListVtable = currentPointerToken(scriptStackListVtable);
            auto* records = reinterpret_cast<script::StackObject*>(
                static_cast<std::uintptr_t>(physical->stackTableToken));
            if (records)
            {
                std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(records) - 1;
                const std::uint32_t count = *header;
                for (std::uint32_t i = count; i != 0; --i)
                    records[i - 1u].~StackObject();
                ::operator delete(static_cast<void*>(header));
            }
            physical->stackTableToken = 0;
            physical->stackCount = 0;
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }

        void* __fastcall scriptFunctionListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            // Deleting-destructor sibling of destroyScriptFunctionListSubobject: identical base
            // teardown plus the flag&1 scalar delete tail.
            destroyScriptFunctionListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }

        void* __fastcall scriptDefineListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            // Deleting-destructor sibling of destroyScriptDefineListSubobject.
            destroyScriptDefineListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }
#endif

        void* initializeScriptFunctionListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[1] = 0u;
            slot[2] = 0u;
#if defined(_MSC_VER) && defined(_M_IX86)
            slot[0] = currentPointerToken(scriptFunctionListInitialVtable);
#else
            slot[0] = 0u;
#endif
            return rawThis;
        }

        void* initializeScriptDefineListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[1] = 0u;
            slot[2] = 0u;
#if defined(_MSC_VER) && defined(_M_IX86)
            slot[0] = currentPointerToken(scriptDefineListInitialVtable);
#else
            slot[0] = 0u;
#endif
            return rawThis;
        }

        void* initializeScriptStackListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
#if defined(_MSC_VER) && defined(_M_IX86)
            slot[0] = currentPointerToken(scriptStackListVtable);
#else
            slot[0] = 0u;
#endif
            slot[1] = 0u;
            slot[2] = 0u;
            return rawThis;
        }
    }

    SCRIPT::SCRIPT()
    {
        
        std::memset(&m_physical, 0, sizeof(m_physical));
        initializeScriptStackListSubobject(&m_physical.stackListVtable);
        initializeScriptFunctionListSubobject(&m_physical.functionListVtable);
        initializeScriptDefineListSubobject(&m_physical.defineListVtable);
        m_physical.scriptFileToken = retailPointerToken(as1::STRING::SharedEmptyText());
        m_physical.sourceLine = -1;
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    ScriptHostState& SCRIPT::host() noexcept
    {
        return ensureScriptHostState(this);
    }

    const ScriptHostState& SCRIPT::host() const noexcept
    {
        return ensureScriptHostState(this);
    }
#endif

    std::uint32_t SCRIPT::retailPointerToken(const void* pointer) noexcept
    {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) & 0xFFFFFFFFu);
    }

    script::StackObject* SCRIPT::executionStackStorage() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<script::StackObject*>(static_cast<std::uintptr_t>(m_physical.stackTableToken));
#else
        return host().m_executionStack.data();
#endif
    }

    const script::StackObject* SCRIPT::executionStackStorage() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<const script::StackObject*>(static_cast<std::uintptr_t>(m_physical.stackTableToken));
#else
        return host().m_executionStack.data();
#endif
    }

    script::LogicFunctionRecord* SCRIPT::functionRecordStorage() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<script::LogicFunctionRecord*>(static_cast<std::uintptr_t>(m_physical.functionTableToken));
#else
        return host().m_functionTable.items().data();
#endif
    }

    const script::LogicFunctionRecord* SCRIPT::functionRecordStorage() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<const script::LogicFunctionRecord*>(static_cast<std::uintptr_t>(m_physical.functionTableToken));
#else
        return host().m_functionTable.items().data();
#endif
    }

    ScriptDefinePairRecord* SCRIPT::defineRecordStorage() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<ScriptDefinePairRecord*>(static_cast<std::uintptr_t>(m_physical.defineTableToken));
#else
        return host().m_defines.data();
#endif
    }

    const ScriptDefinePairRecord* SCRIPT::defineRecordStorage() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<const ScriptDefinePairRecord*>(static_cast<std::uintptr_t>(m_physical.defineTableToken));
#else
        return host().m_defines.data();
#endif
    }

    STRING& SCRIPT::scriptFileStorage() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return *reinterpret_cast<STRING*>(&m_physical.scriptFileToken);
#else
        return host().m_scriptFile;
#endif
    }

    const STRING& SCRIPT::scriptFileStorage() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return *reinterpret_cast<const STRING*>(&m_physical.scriptFileToken);
#else
        return host().m_scriptFile;
#endif
    }

    std::uint8_t* SCRIPT::bytecodeStorage() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(m_physical.bytecodeBufferToken));
#else
        return host().m_bytecode.data();
#endif
    }

    const std::uint8_t* SCRIPT::bytecodeStorage() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(m_physical.bytecodeBufferToken));
#else
        return host().m_bytecode.data();
#endif
    }

    std::uint8_t* SCRIPT::sourceStorage() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(m_physical.sourceBufferToken));
#else
        return host().m_sourceBuffer.data();
#endif
    }

    const std::uint8_t* SCRIPT::sourceStorage() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(m_physical.sourceBufferToken));
#else
        return host().m_sourceBuffer.data();
#endif
    }

    void SCRIPT::syncPhysicalFunctionList() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return;
#else
        const auto& table = host().m_functionTable;
        m_physical.functionCount = table.count();
        m_physical.functionCapacity = table.capacity();
        const auto& items = table.items();
        m_physical.functionTableToken = items.empty() ? 0u : retailPointerToken(items.data());
#endif
    }

    void SCRIPT::syncPhysicalDefineList() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return;
#else
        const auto& items = host().m_defines;
        m_physical.defineCount = static_cast<std::int32_t>(items.size());
        m_physical.defineTableToken = items.empty() ? 0u : retailPointerToken(items.data());
#endif
    }

    void SCRIPT::setSourceEndOffset(int offset) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        m_physical.sourceEnd = m_physical.sourceBufferToken == 0u
            ? 0u
            : m_physical.sourceBufferToken + static_cast<std::uint32_t>(offset < 0 ? 0 : offset);
#else
        host().m_portableSourceEndOffset = offset;
        syncPhysicalSourcePointers();
#endif
    }

    void SCRIPT::syncPhysicalSourcePointers() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return;
#else
        auto& source = host().m_sourceBuffer;
        if (source.empty())
        {
            m_physical.sourceBufferToken = 0u;
            m_physical.sourceCursor = 0u;
            m_physical.sourceEnd = 0u;
            return;
        }
        const std::uint8_t* const base = source.data();
        const int cursor = host().m_portableSourceCursorOffset;
        const int end = host().m_portableSourceEndOffset;
        m_physical.sourceBufferToken = retailPointerToken(base);
        m_physical.sourceCursor = retailPointerToken(base + (cursor < 0 ? 0 : cursor));
        m_physical.sourceEnd = retailPointerToken(base + (end < 0 ? 0 : end));
#endif
    }

    void SCRIPT::syncPhysicalBackingPointers() noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        return;
#else
        auto& h = host();
        m_physical.stackTableToken = h.m_executionStack.empty()
            ? 0u : retailPointerToken(h.m_executionStack.data());
        m_physical.scriptFileToken = retailPointerToken(h.m_scriptFile.c_str());
        m_physical.bytecodeBufferToken = !h.m_bytecode.empty()
            ? retailPointerToken(h.m_bytecode.data())
            : (h.m_zeroLengthBytecodeAllocation
                ? retailPointerToken(h.m_zeroLengthBytecodeAllocation) : 0u);
        syncPhysicalFunctionList();
        syncPhysicalDefineList();
        syncPhysicalSourcePointers();
#endif
    }

    SCRIPT::~SCRIPT()
    {
        resetScriptVmState();
#if defined(_MSC_VER) && defined(_M_IX86)
        destroyScriptDefineListSubobject(&m_physical.defineListVtable);
        destroyScriptFunctionListSubobject(&m_physical.functionListVtable);
        m_physical.stackListVtable = currentPointerToken(scriptStackListVtable);
        m_physical.stackTableToken = 0u;
        m_physical.stackCount = 0;
        destroyStringStorage(scriptFileStorage());
#else
        eraseScriptHostState(this);
#endif
    }

    void SCRIPT::SetNativeContext(const ScriptNativeContext& context)
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        (void)context;
#else
        host().m_nativeContext = context;
#endif
    }
}
