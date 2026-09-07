#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>
#ifndef _WIN32
#include <vector>
#endif
#ifdef _WIN32
#include "d3d8.h"
#endif

namespace as1
{
    class SPRITE;

    struct VID_TEXCOOR_VERTEX
    {

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

#ifndef _WIN32
    struct VidTexcoorBufferState
    {
        bool recorded = false;
        int vertexCount = 0;
        int indexCount = 0;
        std::uint32_t vertexStrideBytes = 0x14u;
        std::uint32_t vertexShaderFvf = 0x102u;
        std::uint32_t vertexBufferUsage = 8u;
        std::uint32_t vertexBufferPool = 1u;
        std::uint32_t indexBufferUsage = 8u;
        std::uint32_t indexBufferFormat = 0x65u;
        std::uint32_t indexBufferPool = 1u;
        std::size_t vertexBufferBytes = 0u;
        std::size_t indexBufferBytes = 0u;
        bool vertexBufferCreated = false;
        bool indexBufferCreated = false;
    };

    struct VidTexcoorLockState
    {
        bool recorded = false;
        bool vertexBuffer = false;
        bool indexBuffer = false;
        bool lockActive = false;
        std::size_t byteCount = 0u;
        void* pointer = nullptr;
    };

    struct VidTexcoorDrawState
    {
        bool recorded = false;
        int vertexCount = 0;
        int indexCount = 0;
        int primitiveType = 4;
        int primitiveCount = 0;
        int minIndex = 0;
        int numVertices = 0;
        int startIndex = 0;
        std::uint32_t vertexShaderFvf = 0x102u;
        std::uint32_t vertexStrideBytes = 0x14u;
        std::uint32_t indexFormat = 0x65u;
        int noDir = 0;
        int incDir = 0;
        int directionByte = 0;
        int quantizedDirectionByte = 0;
        int residualDirectionByte = 0;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float worldZ = 0.0f;
        float worldMatrix[16] = {};
    };
#endif

    struct VID_TEXCOOR_SURFACE_VERTEX_WORDS
    {
        WORD screenX = 0;
        WORD screenY = 0;
        WORD depthCode = 0;
        WORD texU = 0;
        WORD texV = 0;
    };

    class VID_TEXCOOR
    {
    public:
        VID_TEXCOOR(int vertexCount, int indexCount);
        virtual ~VID_TEXCOOR();

        VID_TEXCOOR(const VID_TEXCOOR&) = delete;
        VID_TEXCOOR& operator=(const VID_TEXCOOR&) = delete;

        virtual VID_TEXCOOR_VERTEX* lockVertexBuffer();
        virtual int unlockVertexBuffer();
        virtual WORD* lockIndexBuffer();
        virtual int unlockIndexBuffer();

        virtual int drawTexcoorMesh(const SPRITE& sprite) const;

        bool fillSurfaceVertexIndexWords(const VID_TEXCOOR_SURFACE_VERTEX_WORDS* vertices,
                                         int sourceVertexCount,
                                         const WORD* indices,
                                         int sourceIndexCount,
                                         int vidSizeX,
                                         int vidSizeY,
                                         int textureWidth,
                                         int textureHeight);

        int vertexCount() const { return m_vertexCount; }
        int indexCount() const { return m_indexCount; }

        static constexpr std::size_t RetailObjectSize = 0x14u;
#ifdef _WIN32
        IDirect3DIndexBuffer8* indexBuffer() const { return m_nativeIndexBuffer; }
        IDirect3DVertexBuffer8* vertexBuffer() const { return m_nativeVertexBuffer; }
        bool hasVertexBuffer() const { return m_nativeVertexBuffer != nullptr; }
        bool hasIndexBuffer() const { return m_nativeIndexBuffer != nullptr; }
#else
        bool hasVertexBuffer() const { return !m_vertexBuffer.empty(); }
        bool hasIndexBuffer() const { return !m_indexBuffer.empty(); }
        const std::vector<VID_TEXCOOR_VERTEX>& vertexBuffer() const { return m_vertexBuffer; }
        const std::vector<WORD>& indexBuffer() const { return m_indexBuffer; }
        const VidTexcoorBufferState& bufferState() const { return m_bufferState; }
        const VidTexcoorLockState& lastVertexLockState() const { return m_lastVertexLockState; }
        const VidTexcoorLockState& lastIndexLockState() const { return m_lastIndexLockState; }
        const VidTexcoorDrawState& drawState() const { return m_drawState; }
#endif

    private:
#ifdef _WIN32
        int m_vertexCount;
        int m_indexCount;
        IDirect3DIndexBuffer8* m_nativeIndexBuffer;
        IDirect3DVertexBuffer8* m_nativeVertexBuffer;
#else
        int m_vertexCount = 0;
        int m_indexCount = 0;
        std::vector<VID_TEXCOOR_VERTEX> m_vertexBuffer;
        std::vector<WORD> m_indexBuffer;
        bool m_vertexLocked = false;
        bool m_indexLocked = false;
        VidTexcoorBufferState m_bufferState{};
        VidTexcoorLockState m_lastVertexLockState{};
        VidTexcoorLockState m_lastIndexLockState{};
        mutable VidTexcoorDrawState m_drawState{};
#endif
    };

                                                                                                         
#if defined(_M_IX86)
                                                                                                                      
#endif
}
