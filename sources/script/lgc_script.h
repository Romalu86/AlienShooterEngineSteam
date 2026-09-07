#pragma once
#include "../core/as_string.h"
#include "../graphics/gamma.h"
#include "logic_runtime.h"
#include "retail_raw_array.h"
#include "retail_byte_buffer.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <array>

namespace as1
{
    class VID;
    class SPRITE;
    struct WEAPON;
    namespace core { struct ApplicationDrawDispatcherState; }
    struct ScriptDefinePairRecord
    {
        STRING name;
        STRING value;
    };

    void* scriptDefinePairDeletingDestructor(ScriptDefinePairRecord* self, unsigned char flags) noexcept;
    void destroyScriptDefinePair(ScriptDefinePairRecord* self) noexcept;

    struct ScriptPhysicalLayout
    {
        std::uint32_t stackListVtable = 0;
        std::int32_t stackCount = 0;
        std::int32_t stackCapacity = 0;
        std::uint32_t stackTableToken = 0;
        std::uint32_t functionListVtable = 0;
        std::int32_t functionCount = 0;
        std::int32_t functionCapacity = 0;
        std::uint32_t functionTableToken = 0;
        std::uint32_t defineListVtable = 0;
        std::int32_t defineCount = 0;
        std::int32_t defineCapacity = 0;
        std::uint32_t defineTableToken = 0;
        std::uint32_t scriptFileToken = 0;
        std::uint32_t bytecodeBufferToken = 0;
        std::int32_t bytecodeEnd = 0;
        std::uint32_t sourceCursor = 0;
        std::uint32_t sourceEnd = 0;
        std::uint32_t sourceBufferToken = 0;
        std::int32_t sourceLine = -1;
        std::int32_t conditionalDepth = 0;
        std::int32_t fallbackFunction = -1;
        std::int32_t parseMode = 0;
    };

                                                                                                    
#if defined(_MSC_VER) && defined(_M_IX86)
                                                                                              
                                                                                                         
                                                                                                                    
                                                                                                           
#endif
                                                                                                
                                                                                                     
                                                                                                   
                                                                                                        
                                                                                                 
                                                                                                      
                                                                                                     
                                                                                                         
                                                                                                 
                                                                                                  
                                                                                               
                                                                                                       
                                                                                                
                                                                                                      
                                                                                                      
                                                                                               


    struct ScriptNativeContext
    {
        std::function<void(const STRING& path)> queueMapLoadSlot18Flag40;
    };



    struct ScriptHostState
    {
#if !defined(_MSC_VER) || !defined(_M_IX86)
        script::RetailRawArray<script::StackObject> m_executionStack;
        script::LogicFunctionList m_functionTable;
        script::RetailRawArray<ScriptDefinePairRecord> m_defines;
        STRING m_scriptFile;
        script::RetailByteBuffer m_bytecode;
        void* m_zeroLengthBytecodeAllocation = nullptr;
        script::RetailByteBuffer m_sourceBuffer;
        int m_portableSourceCursorOffset = 0;
        int m_portableSourceEndOffset = 0;
        ScriptNativeContext m_nativeContext;
#endif
    };

    class SCRIPT
    {
    public:
        SCRIPT();
        ~SCRIPT();
        SCRIPT(const SCRIPT&) = delete;
        SCRIPT& operator=(const SCRIPT&) = delete;

        int compileScriptSourceFile(const STRING& scriptFile, const STRING& gameRoot);
        int loadScriptFile(const STRING& scriptFile, const STRING& gameRoot);
        int writeExecutionStackToStream(BaseStream* stream);
        int readExecutionStackFromStream(BaseStream* stream);
        bool isLoaded() const { return m_physical.bytecodeEnd != 0; }

        void SetNativeContext(const ScriptNativeContext& context);

        static constexpr std::uint32_t InitialListCapacity = 0x80u;
        static constexpr std::uint32_t TemporaryBytecodeCapacity = 0x3E800u;
        static constexpr std::uint32_t SourceBufferPadding = 0x1000u;
        static constexpr std::uint32_t SourcePayloadOffset = 0x0FE2u;
        void clearExecutionStack();
        void pushExecutionInt(int value);
        void pushExecutionString(const STRING& value);
        int executionStackCount() const;
        int executionStackCapacity() const;
        const script::StackObject* executionStackAt(int index) const;
        script::StackObject* mutableExecutionStackAt(int index);
        script::StackObject* mutableExecutionStackStorageAt(int index);
        void appendExecutionStackObject(const script::StackObject& value);
        int clearSpriteReferencesFromExecutionStack(SPRITE* sprite);
        void growExecutionStackForAppend();
        void appendExecutionStackRecord(std::uint8_t flags, int value, const STRING& text);
        int functionCount() const;
        int functionCapacity() const;
        const script::LogicFunctionList& functionTable() const;
        const script::LogicFunctionRecord* functionRecordAt(int index) const noexcept;
        script::LogicFunctionRecord* mutableFunctionRecordAt(int index) noexcept;
        int findFunctionRecordByName(const STRING& name) const noexcept;
        int defineCount() const;
        int defineCapacity() const;
        const script::RetailRawArray<ScriptDefinePairRecord>& defineTable() const;
        const STRING& scriptFile() const;
        const script::RetailByteBuffer& bytecodeBuffer() const;
        int bytecodeEnd() const;
        int sourceCursorOffset() const;
        int sourceEndOffset() const;
        const script::RetailByteBuffer& sourceBuffer() const;
        int sourceLineNumber() const;
        int conditionalDepth() const;
        int fallbackFunctionIndex() const;
        int parseMode() const;
        std::uint8_t sourceByteAtCursor() const;
        void setSourceCursorOffset(int offset);
        void reportCompileError(int errorCode, const char* detailText, int detailValue);
        int skipTriviaAndPreprocess();
        int requireSourceToken();
        int readSourceLine(STRING& outLine);
        int readIdentifier(STRING& outName);
        int matchToken(const char* token);
        int requireToken(const char* token);
        int parseConstantIntExpression();
        int readQuotedStringLiteral(char* outText);
        int setLastFunctionElementCount(int argCount);
        void compileIntDeclaration();
        void compileStringDeclaration();
        int compilePrimaryExpression();
        void emitOrFoldBinaryCommand(int bytecodeStart, int opcode);
        void compileMultiplicativeExpression();
        void compileAdditiveExpression();
        void compileComparisonExpression();
        void compileExpression();
        void compileExpressionList();
        void compileStatement(std::int32_t* breakPatchList);
        int compileNextSourceItem();
        int compileDefineDirective();
        int compileUndefDirective();
        int compileIncludeDirective();
        int compileExternDirective();
        int compileFunctionDirective();
        void resetScriptVmState();
        void prepareSourceCompiler(const STRING& scriptFile, BaseStream* stream, std::uint32_t sourceSize);
        void clearFunctionTable();
        void appendFunctionRecord(const STRING& name, std::uint8_t flags, const STRING& text, int bytecodeStart0C, int stackBase10, int argCount14);
        void clearDefines();
        int findDefine(const STRING& name) const;
        int addOrReplaceDefine(const STRING& name, const STRING& value);
        int undefine(const STRING& name);
        int rewriteDefineMacro(int tokenStartOffset, int tokenLength);
        int dispatchNativeFunction(int opcode);
        int popSpriteReferenceValue();
        SPRITE* popSpriteReference();
        void pushSpriteReference(SPRITE* sprite);
        void pushIntegerValue(int value);
        void pushSpriteReferenceValue(int value);
        void pushStringValue(const STRING& value);
        int popIntegerValue();
        GammaRawPair decodePackedGamma(int value);
        as1::VID* popVidValue(const char* errorContext);
        void reportScriptError(int errorCode, const char* text, int value);
        const char* stringTextPointer(const STRING& value) const;
        int writeCStringToStream(const STRING& source, BaseStream* target) const;
        std::size_t writeCStringRecord(const STRING& value, std::FILE* file) const;
        std::FILE* openScriptFile(const STRING& path, const char* mode) const;
        int topValueIsString() const;
        STRING* popStringRetail();
        void pushStringResult(const STRING& value);
        void pushIntegerResult(int value);

        
        STRING getVariableString(const STRING& expression);
        // Compatibility alias: SCRIPT::callFunction.
        int callFunction(int functionIndex, int arg3, int arg4, int arg5);

    private:
        friend class MAP;
#if !defined(_MSC_VER) || !defined(_M_IX86)
        ScriptHostState& host() noexcept;
        const ScriptHostState& host() const noexcept;
#endif
        static std::uint32_t retailPointerToken(const void* pointer) noexcept;
        script::StackObject* executionStackStorage() noexcept;
        const script::StackObject* executionStackStorage() const noexcept;
        script::LogicFunctionRecord* functionRecordStorage() noexcept;
        const script::LogicFunctionRecord* functionRecordStorage() const noexcept;
        ScriptDefinePairRecord* defineRecordStorage() noexcept;
        const ScriptDefinePairRecord* defineRecordStorage() const noexcept;
        STRING& scriptFileStorage() noexcept;
        const STRING& scriptFileStorage() const noexcept;
        std::uint8_t* bytecodeStorage() noexcept;
        const std::uint8_t* bytecodeStorage() const noexcept;
        std::uint8_t* sourceStorage() noexcept;
        const std::uint8_t* sourceStorage() const noexcept;
        void setSourceEndOffset(int offset) noexcept;
        void syncPhysicalBackingPointers() noexcept;
        void syncPhysicalFunctionList() noexcept;
        void syncPhysicalDefineList() noexcept;
        void syncPhysicalSourcePointers() noexcept;
        ScriptPhysicalLayout m_physical{};

    };


                                                                                                      
}
