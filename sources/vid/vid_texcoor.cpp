#include "vid/vid_texcoor.h"
#include "sprite.h"
#include "vid/vid.h"
#include "graph.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include "d3d8.h"
#endif

namespace as1
{
    namespace
    {

#ifdef _WIN32
        IDirect3DDevice8* currentVidTexcoorDevice()
        {
            return static_cast<IDirect3DDevice8*>(GRAPH::CurrentDevice());
        }
#endif

        int normalizedCount(int value)
        {
            return value > 0 ? value : 0;
        }
    }

    VID_TEXCOOR::VID_TEXCOOR(int vertexCount, int indexCount)
    {

#ifdef _WIN32
        m_vertexCount = vertexCount;
        m_indexCount = indexCount;
        m_nativeIndexBuffer = nullptr;
        m_nativeVertexBuffer = nullptr;

        IDirect3DDevice8* device = currentVidTexcoorDevice();
        HRESULT result = device->CreateVertexBuffer(
            static_cast<UINT>(vertexCount) * 0x14u,
            8u,
            0x102u,
            static_cast<D3DPOOL>(1u),
            &m_nativeVertexBuffer, nullptr);
        if (result != D3D_OK)
            LOG::ResourceError("MESH", 3, "VertexBuffer", static_cast<int>(result));

        if (indexCount != 0)
        {
            result = device->CreateIndexBuffer(
                static_cast<UINT>(indexCount) * 2u,
                8u,
                static_cast<D3DFORMAT>(0x65u),
                static_cast<D3DPOOL>(1u),
                &m_nativeIndexBuffer, nullptr);
            if (result != D3D_OK)
                LOG::ResourceError("MESH", 3, "IndexBuffer", static_cast<int>(result));
        }
#else
        m_vertexCount = normalizedCount(vertexCount);
        m_indexCount = normalizedCount(indexCount);
        m_vertexBuffer.assign(static_cast<std::size_t>(m_vertexCount), VID_TEXCOOR_VERTEX{});
        m_indexBuffer.assign(static_cast<std::size_t>(m_indexCount), WORD{});
        m_bufferState.recorded = true;
        m_bufferState.vertexCount = m_vertexCount;
        m_bufferState.indexCount = m_indexCount;
        m_bufferState.vertexBufferBytes = static_cast<std::size_t>(m_vertexCount) * sizeof(VID_TEXCOOR_VERTEX);
        m_bufferState.indexBufferBytes = static_cast<std::size_t>(m_indexCount) * sizeof(WORD);
        m_bufferState.vertexBufferCreated = m_vertexCount > 0;
        m_bufferState.indexBufferCreated = m_indexCount > 0;
#endif
    }

    VID_TEXCOOR::~VID_TEXCOOR()
    {

#ifdef _WIN32
        IDirect3DDevice8* device = currentVidTexcoorDevice();
        device->SetIndices(nullptr);
        device->SetStreamSource(0, nullptr, 0u, 0x14u);

        if (m_nativeIndexBuffer)
        {
            const ULONG releaseCount = m_nativeIndexBuffer->Release();
            if (releaseCount != 0u)
                LOG::ResourceError("MESH", 10, "Index release count !=0", static_cast<int>(releaseCount));
        }
        if (m_nativeVertexBuffer)
        {
            const ULONG releaseCount = m_nativeVertexBuffer->Release();
            if (releaseCount != 0u)
                LOG::ResourceError("MESH", 10, "Vertex release count !=0", static_cast<int>(releaseCount));
        }
#endif
    }

    VID_TEXCOOR* vidTexcoorScalarDeletingDestructor(VID_TEXCOOR* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_TEXCOOR();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    VID_TEXCOOR_VERTEX* VID_TEXCOOR::lockVertexBuffer()
    {

#ifdef _WIN32

        void* locked = nullptr;
        m_nativeVertexBuffer->Lock(0, 0, &locked, 0x2000u);
        return static_cast<VID_TEXCOOR_VERTEX*>(locked);
#else
        m_lastVertexLockState = VidTexcoorLockState{};
        m_lastVertexLockState.recorded = true;
        m_lastVertexLockState.vertexBuffer = true;
        m_lastVertexLockState.byteCount = static_cast<std::size_t>(m_vertexCount) * sizeof(VID_TEXCOOR_VERTEX);
        m_vertexLocked = !m_vertexBuffer.empty();
        m_lastVertexLockState.lockActive = m_vertexLocked;
        m_lastVertexLockState.pointer = m_vertexLocked ? static_cast<void*>(m_vertexBuffer.data()) : nullptr;
        return m_vertexLocked ? m_vertexBuffer.data() : nullptr;
#endif
    }

    int VID_TEXCOOR::unlockVertexBuffer()
    {

#ifdef _WIN32
        return static_cast<int>(m_nativeVertexBuffer->Unlock());
#else
        m_vertexLocked = false;
        if (m_lastVertexLockState.recorded)
            m_lastVertexLockState.lockActive = false;
        return 0;
#endif
    }

    WORD* VID_TEXCOOR::lockIndexBuffer()
    {

#ifdef _WIN32

        void* locked = nullptr;
        m_nativeIndexBuffer->Lock(0, 0, &locked, 0x2000u);
        return static_cast<WORD*>(locked);
#else
        m_lastIndexLockState = VidTexcoorLockState{};
        m_lastIndexLockState.recorded = true;
        m_lastIndexLockState.indexBuffer = true;
        m_lastIndexLockState.byteCount = static_cast<std::size_t>(m_indexCount) * sizeof(WORD);
        m_indexLocked = !m_indexBuffer.empty();
        m_lastIndexLockState.lockActive = m_indexLocked;
        m_lastIndexLockState.pointer = m_indexLocked ? static_cast<void*>(m_indexBuffer.data()) : nullptr;
        return m_indexLocked ? m_indexBuffer.data() : nullptr;
#endif
    }

    int VID_TEXCOOR::unlockIndexBuffer()
    {

#ifdef _WIN32
        return static_cast<int>(m_nativeIndexBuffer->Unlock());
#else
        m_indexLocked = false;
        if (m_lastIndexLockState.recorded)
            m_lastIndexLockState.lockActive = false;
        return 0;
#endif
    }

    int VID_TEXCOOR::drawTexcoorMesh(const SPRITE& sprite) const
    {
        (void)sprite;

#ifdef _WIN32
        GRAPH* const graph = GRAPH::CurrentGraph();
        graph->setRenderStateCached(7u, 1u);
        graph->setRenderStateCached(0x17u, 5u);
        graph->setRenderStateCached(0x0Eu, 1u);
        graph->setRenderStateCached(0x16u, 1u);
        graph->setRenderStateCached(0x0Fu, 1u);
        graph->setRenderStateCached(0x18u, 0x20u);
        graph->setRenderStateCached(0x19u, 5u);

        IDirect3DDevice8* const device = static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        HRESULT hr = D3D_OK;
        if (indexCount() != 0)
        {
            hr = device->SetIndices(m_nativeIndexBuffer);
            if (FAILED(hr))
                LOG::ResourceError("%s", 8, "Indices", static_cast<int>(hr), "MESH");
        }

        hr = device->SetStreamSource(0, m_nativeVertexBuffer, 0u, 0x14u);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "Vertex", static_cast<int>(hr), "MESH");

        hr = device->SetFVF(0x102u);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "VertexShader", static_cast<int>(hr), "MESH");

        if (indexCount() != 0)
        {
            hr = device->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST,
                0,
                0,
                static_cast<UINT>(vertexCount()),
                0,
                static_cast<UINT>(indexCount() / 3));
        }
        else
        {
            hr = device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0u, 2u);
        }

        if (FAILED(hr))
            logFileLoggerResourceError(&GlobalFileLogger(), "%s", 10, "Draw", static_cast<int>(hr), "MESH");

        graph->setRenderStateCached(0x0Fu, 0u);
        return static_cast<int>(hr);
#else
        m_drawState = VidTexcoorDrawState{};
        m_drawState.recorded = true;
        m_drawState.vertexCount = vertexCount();
        m_drawState.indexCount = indexCount();
        m_drawState.numVertices = vertexCount();
        m_drawState.primitiveCount = indexCount() / 3;
        return 0;
#endif
    }

    bool VID_TEXCOOR::fillSurfaceVertexIndexWords(const VID_TEXCOOR_SURFACE_VERTEX_WORDS* vertices,
                                                  int sourceVertexCount,
                                                  const WORD* indices,
                                                  int sourceIndexCount,
                                                  int vidSizeX,
                                                  int vidSizeY,
                                                  int textureWidth,
                                                  int textureHeight)
    {

        if (!vertices || sourceVertexCount < m_vertexCount || textureWidth <= 0 || textureHeight <= 0)
            return false;
        if (m_indexCount > 0 && (!indices || sourceIndexCount < m_indexCount))
            return false;

        auto s16 = [](WORD value) -> int
        {
            return static_cast<int>(static_cast<std::int16_t>(value));
        };

        VID_TEXCOOR_VERTEX* dstVertex = lockVertexBuffer();
        if (!dstVertex && m_vertexCount > 0)
            return false;

        for (int i = 0; i < m_vertexCount; ++i)
        {
            const VID_TEXCOOR_SURFACE_VERTEX_WORDS& src = vertices[i];
            const float z = (static_cast<float>(s16(src.depthCode) - 1024) * 0.125f);
            dstVertex[i].x = static_cast<float>(s16(src.screenX)) - static_cast<float>(vidSizeX) * 0.5f;
            dstVertex[i].z = z;
            dstVertex[i].y = (static_cast<float>(s16(src.screenY)) - static_cast<float>(vidSizeY) * 0.5f + z) * 1.5f;
            dstVertex[i].u = (static_cast<float>(s16(src.texU)) + 0.5f) / static_cast<float>(textureWidth);
            dstVertex[i].v = (static_cast<float>(s16(src.texV)) + 0.5f) / static_cast<float>(textureHeight);
        }
        unlockVertexBuffer();

        if (m_indexCount > 0)
        {
            WORD* dstIndex = lockIndexBuffer();
            if (!dstIndex)
                return false;
            for (int i = 0; i < m_indexCount; ++i)
                dstIndex[i] = indices[i];
            unlockIndexBuffer();
        }
        return true;
    }
}
