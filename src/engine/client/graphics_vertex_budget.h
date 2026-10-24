#ifndef ENGINE_CLIENT_GRAPHICS_VERTEX_BUDGET_H
#define ENGINE_CLIENT_GRAPHICS_VERTEX_BUDGET_H

#include <algorithm>
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

// 写入完成后的提交保持原提前Flush规则；无效数量不污染当前计数。
template<int Capacity = GRAPHICS_VERTEX_CAPACITY, typename TFlush>
bool CommitGraphicsVertices(int Count, int &Pending, TFlush &&Flush)
{
	static_assert(Capacity > 0 && Capacity <= GRAPHICS_VERTEX_CAPACITY);
	if(Pending < 0 || Pending > Capacity || Count < 0 || Count > Capacity - Pending)
		return false;
	Pending += Count;
	if(Pending + Count >= Capacity)
		Flush();
	return true;
}

// Write同步生成一个完整图元批次并提交计数；Flush成功必须清空暂存。
// 回调不能重入修改源；异常直接传播，不重放已提交前缀。
template<int Capacity = GRAPHICS_VERTEX_CAPACITY, typename TWrite, typename TFlush>
bool WriteGraphicsVertexBatches(size_t Count, int VerticesPerItem, int &Pending, TWrite &&Write, TFlush &&Flush)
{
	static_assert(Capacity > 0 && Capacity <= GRAPHICS_VERTEX_CAPACITY);
	if(VerticesPerItem <= 0 || VerticesPerItem > Capacity || Pending < 0 || Pending > Capacity)
		return false;
	for(size_t Done = 0; Done < Count;)
	{
		int Room = (Capacity - Pending) / VerticesPerItem;
		if(Room == 0)
		{
			Flush();
			if(Pending != 0)
				return false;
			Room = Capacity / VerticesPerItem;
		}
		const int Batch = static_cast<int>(std::min(Count - Done, static_cast<size_t>(Room)));
		Write(Done, Batch);
		if(Pending < 0 || Pending > Capacity)
			return false;
		Done += Batch;
	}
	return true;
}

#endif
