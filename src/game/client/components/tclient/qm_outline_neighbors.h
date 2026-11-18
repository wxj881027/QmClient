#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_QM_OUTLINE_NEIGHBORS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_QM_OUTLINE_NEIGHBORS_H

// 低四位保留地图类型（含可钩与不可钩墙体），高位缓存静态邻接关系。
// 地图尺寸须为正，X/Y 及其相邻坐标须可表示，回调返回未带缓存的类型。
template<typename F>
int QmOutlineCachedNeighbors(int &Tile, int X, int Y, int Width, int Height, F &&GetTile)
{
	constexpr int TYPE_BITS = 4;
	constexpr int VALID = 1 << 12;
	const bool Inside = X >= 0 && Y >= 0 && X < Width && Y < Height;
	if(Inside && (Tile & VALID))
		return (Tile >> TYPE_BITS) & 255;
	const int Type = Tile & ((1 << TYPE_BITS) - 1);
	const int aDx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
	const int aDy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
	int Neighbors = 0;
	for(int i = 0; i < 8; ++i)
		Neighbors |= (GetTile(X + aDx[i], Y + aDy[i]) >= Type) << i;
	// 地图外的延伸边界依赖原始坐标，不能复用被夹取 tile 的缓存。
	if(Inside)
		Tile = Type | (Neighbors << TYPE_BITS) | VALID;
	return Neighbors;
}

#endif
