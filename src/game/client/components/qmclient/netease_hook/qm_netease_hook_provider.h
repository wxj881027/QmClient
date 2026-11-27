// QmClient 侧网易云 Hook 快照读取器。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_HOOK_QM_NETEASE_HOOK_PROVIDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_HOOK_QM_NETEASE_HOOK_PROVIDER_H

#include "qm_netease_hook_protocol.h"

#include <cstdint>
#include <memory>

class CQmNeteaseHookProvider
{
public:
	CQmNeteaseHookProvider();
	~CQmNeteaseHookProvider();

	CQmNeteaseHookProvider(const CQmNeteaseHookProvider &) = delete;
	CQmNeteaseHookProvider &operator=(const CQmNeteaseHookProvider &) = delete;

	// 启动与停止都只发布后台状态：建映射、拉起 helper、读取共享内存
	// 都可能被系统安全扫描拖慢，不能占用渲染或逻辑线程。
	void Start(const char *pHelperPath, int TimeoutMs);
	void RequestStop();
	void Stop();
	bool Read(QmNeteaseHook::SSnapshot *pSnapshot, int TimeoutMs);
	// v5 只承载网易云私有歌词桥接数据，不覆盖 SMTC 状态。
	bool ReadV5(QmNeteaseHook::SSnapshotV5 *pSnapshot, int TimeoutMs);
	bool IsRunning() const;

private:
	struct SImpl;
	std::unique_ptr<SImpl> m_pImpl;
};

#endif
