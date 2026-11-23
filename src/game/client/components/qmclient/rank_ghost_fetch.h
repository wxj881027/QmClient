#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_GHOST_FETCH_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_GHOST_FETCH_H

// rank_ghost 拉取阶段的内部接口。
// demo 校验同时被拉取收尾与“仅下载回放”路径使用，因此单独暴露。
// 只被 rank_ghost.cpp 与 rank_ghost_fetch.cpp 使用，不进入公开头文件。

class IStorage;

namespace rank_ghost_fetch
{
	// 校验缓存文件是否为有效 demo。文件是 gzip 时，会在原路径解包为 demo。
	bool ValidateOrUnpackDemo(IStorage *pStorage, const char *pStoragePath);
}

#endif
