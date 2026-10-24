#ifndef ENGINE_CLIENT_QUAD_CONTAINER_RENDER_H
#define ENGINE_CLIENT_QUAD_CONTAINER_RENDER_H

#include <algorithm>
#include <cstddef>
#include <limits>

// 六个索引的后端有符号绘制计数及字节偏移都必须可表示；这不是一次CPU暂存的容量。
inline constexpr size_t GRAPHICS_MAX_CONTAINER_QUADS = std::min(
	static_cast<size_t>(std::numeric_limits<int>::max() / 6),
	std::numeric_limits<size_t>::max() / (6 * sizeof(unsigned int)));

inline bool GraphicsCanAppendQuads(size_t Current, int Count)
{
	return Count >= 0 && Current <= GRAPHICS_MAX_CONTAINER_QUADS &&
	       static_cast<size_t>(Count) <= GRAPHICS_MAX_CONTAINER_QUADS - Current;
}

inline bool GraphicsResolveQuadRange(size_t Available, int Offset, int &Count)
{
	if(Available > GRAPHICS_MAX_CONTAINER_QUADS || Offset < 0 || Count < -1 || static_cast<size_t>(Offset) > Available)
		return false;

	const size_t Remaining = Available - static_cast<size_t>(Offset);
	if(Count == -1)
		Count = static_cast<int>(Remaining);
	return Count > 0 && static_cast<size_t>(Count) <= Remaining;
}

inline unsigned int GraphicsQuadIndicesToNotify(size_t EndQuad, size_t InitialQuadCapacity)
{
	return EndQuad > InitialQuadCapacity && EndQuad <= GRAPHICS_MAX_CONTAINER_QUADS ? static_cast<unsigned int>(EndQuad * 6) : 0;
}

// 回调同步消费暂存区，不能修改源容器；提交失败继续传播，不重放已提交前缀。
template<typename TQuads, typename TVertex, size_t Capacity, typename TTransform, typename TSubmit>
bool StreamGraphicsQuads(const TQuads &Quads, int Offset, int Count, bool Triangles,
	TVertex (&aVertices)[Capacity], TTransform &&Transform, TSubmit &&Submit)
{
	static_assert(Capacity >= 6 && Capacity <= static_cast<size_t>(std::numeric_limits<int>::max()));
	if(!GraphicsResolveQuadRange(Quads.size(), Offset, Count))
		return false;

	const int VerticesPerQuad = Triangles ? 6 : 4;
	const int QuadsPerBatch = static_cast<int>(Capacity / VerticesPerQuad);
	for(int Done = 0; Done < Count;)
	{
		const int Batch = std::min(Count - Done, QuadsPerBatch);
		for(int i = 0; i < Batch; ++i)
		{
			const auto &Quad = Quads[static_cast<size_t>(Offset) + Done + i];
			auto *pVertices = &aVertices[i * VerticesPerQuad];
			if(Triangles)
			{
				constexpr int aOrder[] = {0, 1, 2, 0, 2, 3};
				for(int n = 0; n < 6; ++n)
					pVertices[n] = Quad.m_aVertices[aOrder[n]];
			}
			else
				std::copy_n(Quad.m_aVertices, 4, pVertices);
			Transform(pVertices, VerticesPerQuad);
		}
		Submit(Batch * VerticesPerQuad);
		Done += Batch;
	}
	return true;
}

#endif
