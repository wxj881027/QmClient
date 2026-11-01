#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BG_DRAW_QUAD_CONTAINERS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BG_DRAW_QUAD_CONTAINERS_H

#include <engine/graphics.h>

#include <vector>

// 沿用画笔原有的OpenGL单容器分组上限。
inline constexpr int QM_BG_DRAW_MAX_QUADS_PER_CONTAINER = 512;

// 图形调用不能重入或修改本组owner；Create抛出前不得遗留已发布资源。
// QuadCount只记录尾容器已成功追加的项数，范围为[0,512]。
template<typename TGraphics>
void QmBgDrawAppendQuads(TGraphics &Graphics, std::vector<int> &vIndexes, int &QuadCount,
	IGraphics::CFreeformItem *pQuads, int Count, ColorRGBA Color)
{
	if(Count <= 0 || Count > QM_BG_DRAW_MAX_QUADS_PER_CONTAINER || pQuads == nullptr)
		return;

	if(vIndexes.empty() || Count > QM_BG_DRAW_MAX_QUADS_PER_CONTAINER - QuadCount)
	{
		const int Previous = vIndexes.empty() ? -1 : vIndexes.back();
		// 先准备owner位置；创建成功后的int赋值不能再因登记分配失败而丢句柄。
		vIndexes.emplace_back(-1);
		try
		{
			if(Previous != -1)
				Graphics.QuadContainerUpload(Previous);
			vIndexes.back() = Graphics.CreateQuadContainer(false);
		}
		catch(...)
		{
			vIndexes.pop_back();
			throw;
		}
		QuadCount = 0;
	}

	Graphics.SetColor(Color);
	if(Graphics.QuadContainerAddQuads(vIndexes.back(), pQuads, Count) < 0)
		return;
	QuadCount += Count;
}

template<typename TGraphics>
void QmBgDrawClearQuadContainers(TGraphics &Graphics, std::vector<int> &vIndexes, int &QuadCount)
{
	for(int &Index : vIndexes)
	{
		if(Index != -1)
		{
			Graphics.DeleteQuadContainer(Index);
			// 每次成功即清身份，后续项抛出后重试不会再次释放此前的索引。
			Index = -1;
		}
	}
	vIndexes.clear();
	QuadCount = 0;
}

#endif
