#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_LOAD_BUDGET_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_LOAD_BUDGET_H

#include <chrono>

inline bool QmSkinCanFinalize(int Processed, std::chrono::nanoseconds Elapsed, std::chrono::nanoseconds Budget)
{
	// 首个已就绪皮肤始终取得进展，后续皮肤按时间预算留到下一轮处理。
	return Processed == 0 || Elapsed < Budget;
}

class CQmSkinUploadFrameBudget
{
public:
	// 多次逻辑更新共享真实上传时间；允许在同一帧内继续上传多个精灵。
	bool CanUpload() const { return m_UploadTime < std::chrono::milliseconds(1); }
	void RecordUpload(std::chrono::nanoseconds Duration) { m_UploadTime += Duration; }
	void Reset() { m_UploadTime = {}; }

private:
	std::chrono::nanoseconds m_UploadTime{};
};

#endif
