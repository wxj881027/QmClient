#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_LOAD_BUDGET_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_LOAD_BUDGET_H

#include <chrono>
#include <cstddef>

inline std::chrono::milliseconds QmSkinContainerUpdateInterval(std::chrono::milliseconds AdaptiveInterval, bool TeeRequestsPending)
{
	return TeeRequestsPending ? std::chrono::milliseconds(25) : AdaptiveInterval;
}

inline bool QmSkinCanFinalize(int Processed, std::chrono::nanoseconds Elapsed, std::chrono::nanoseconds Budget)
{
	// 首个已就绪皮肤始终取得进展，后续皮肤按时间预算留到下一轮处理。
	return Processed == 0 || Elapsed < Budget;
}

class CQmSkinUploadFrameBudget
{
public:
	static constexpr size_t MAX_PREPARED_BYTES_PER_FRAME = 4 * 1024 * 1024;

	bool CanUpload(int MaxAttempts, size_t NextPreparedBytes = 0) const
	{
		if(m_UploadTime >= m_Limit || m_Attempts >= MaxAttempts)
			return false;
		// 大于整帧预算的单张精灵可以独占一帧，否则它永远无法开始上传。
		if(m_Attempts == 0)
			return true;
		return m_PreparedBytes < MAX_PREPARED_BYTES_PER_FRAME &&
		       NextPreparedBytes <= MAX_PREPARED_BYTES_PER_FRAME - m_PreparedBytes;
	}
	void RecordUpload(std::chrono::nanoseconds Duration, bool Submitted = true, size_t PreparedBytes = 0)
	{
		m_UploadTime += Duration;
		m_Attempts++;
		m_Submitted += Submitted;
		if(Submitted)
			m_PreparedBytes += PreparedBytes;
	}
	int Attempts() const { return m_Attempts; }
	int Submitted() const { return m_Submitted; }
	size_t PreparedBytes() const { return m_PreparedBytes; }
	std::chrono::nanoseconds UploadTime() const { return m_UploadTime; }
	std::chrono::nanoseconds Limit() const { return m_Limit; }
	void Reset(std::chrono::nanoseconds Limit = std::chrono::milliseconds(1))
	{
		m_UploadTime = {};
		m_Limit = Limit;
		m_Attempts = 0;
		m_Submitted = 0;
		m_PreparedBytes = 0;
	}

private:
	std::chrono::nanoseconds m_UploadTime{};
	std::chrono::nanoseconds m_Limit = std::chrono::milliseconds(1);
	int m_Attempts = 0;
	int m_Submitted = 0;
	size_t m_PreparedBytes = 0;
};

#endif
