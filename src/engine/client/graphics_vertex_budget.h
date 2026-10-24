#ifndef ENGINE_CLIENT_GRAPHICS_VERTEX_BUDGET_H
#define ENGINE_CLIENT_GRAPHICS_VERTEX_BUDGET_H

#include <cstddef>

inline constexpr int GRAPHICS_VERTEX_CAPACITY = 32 * 1024;

// 即时绘制会为下一图元预留空间；按六顶点估算，兼容两种四边形表示。
inline bool GraphicsCanRetainTextQuads(size_t Quads)
{
	constexpr int MaxVerticesPerQuad = 6;
	return Quads <= (GRAPHICS_VERTEX_CAPACITY - MaxVerticesPerQuad - 1) / MaxVerticesPerQuad;
}

inline bool GraphicsCanRecolorQuad(size_t QuadOffset, int RetainedVertices, size_t VerticesPerQuad)
{
	return RetainedVertices > 0 && RetainedVertices <= GRAPHICS_VERTEX_CAPACITY && VerticesPerQuad > 0 &&
	       QuadOffset < static_cast<size_t>(RetainedVertices) / VerticesPerQuad;
}

#endif
