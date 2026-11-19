// QmClient 侧汽水音乐 Hook 快照读取器。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SODA_HOOK_PROVIDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SODA_HOOK_PROVIDER_H

#include <qm/hooks/soda/qm_soda_protocol.h>

#include <cstddef>
#include <cstdint>
#include <memory>

// 读取汽水音乐 Helper 发布的共享内存快照,并负责启动/停止 helper 子进程。
class CQmSodaHookProvider
{
public:
	CQmSodaHookProvider();
	~CQmSodaHookProvider();

	CQmSodaHookProvider(const CQmSodaHookProvider &) = delete;
	CQmSodaHookProvider &operator=(const CQmSodaHookProvider &) = delete;

	// true 表示来源已接纳（包括存活、节流或正常启动失败），不保证进程启动成功。
	// 资源准备失败返回 false，调用方应跳过本帧读取，避免沿旧来源打开映射。
	bool Start(const char *pHelperPath, const char *pSource = "soda");
	bool RunKugouSetup(bool Restore);
	bool GetStatus(char *pBuffer, size_t BufferSize) const;
	void Stop();
	bool Read(QmSodaHook::SSnapshot *pSnapshot, int TimeoutMs);
	bool IsRunning() const;

private:
	struct SImpl;
	std::unique_ptr<SImpl> m_pImpl;
};

#endif
