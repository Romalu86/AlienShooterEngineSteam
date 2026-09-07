#pragma once
#include "core/types.h"
#include "graphics/rect.h"

namespace as1
{
    struct RECTANGLE_VERTEX
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float rhw = 1.0f;
        DWORD diffuse = 0xFFFFFFFFu;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct RECTANGLE_VERTEXES
    {
        RECTANGLE_VERTEX v[4];
    };

    struct RECTANGLE_VERTEX_D3D
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float rhw = 1.0f;
        DWORD diffuse = 0xFFFFFFFFu;
        DWORD specular = 0u;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct RECTANGLE_VERTEXES_D3D
    {
        RECTANGLE_VERTEX_D3D v[4];
    };

    RECTANGLE_VERTEXES_D3D makeFixedDepthRectangleVertexes(const RECTI& dst, const RECTI& src, int textureWidth, int textureHeight, DWORD color0, DWORD color1);
    RECTANGLE_VERTEXES_D3D makeDepthRectangleVertexes(const RECTI& dst, const RECTI& src, int textureWidth, int textureHeight, DWORD zTopRaw, DWORD zBottomRaw, DWORD color0, DWORD color1);
}
