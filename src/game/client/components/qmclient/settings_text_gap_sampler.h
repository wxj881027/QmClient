#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_TEXT_GAP_SAMPLER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_TEXT_GAP_SAMPLER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// 菜单主线程使用：同一覆盖帧内保存已采样组合，避免 A、B、A 交替时重复输出。
class CQmSettingsTextGapSampler
{
	struct SBucket
	{
		int m_Scope;
		int m_Page;
		int m_Tab;
		int m_Subtab;
		char m_aReason[32];
		char m_aPlanStatus[32];
	};

public:
	static constexpr size_t MAX_BUCKETS = 64;

	bool Allow(uint64_t Session, uint64_t CoverageFrame, int Scope, int Page, int Tab, int Subtab, const char *pReason, const char *pPlanStatus)
	{
		if(m_Session != Session || m_CoverageFrame != CoverageFrame)
		{
			m_Session = Session;
			m_CoverageFrame = CoverageFrame;
			m_Count = 0;
		}
		// 只保留有限样本；容量用完后不替换旧组合，也不在热路径分配内存。
		if(m_Count == m_aBuckets.size())
			return false;
		const char *pSafeReason = pReason != nullptr ? pReason : "unknown";
		const char *pSafePlanStatus = pPlanStatus != nullptr ? pPlanStatus : "unknown";
		const size_t ReasonLength = std::strlen(pSafeReason);
		const size_t PlanStatusLength = std::strlen(pSafePlanStatus);
		// 不截断组合身份，超长分类名保守丢弃，避免截断导致重复采样。
		if(ReasonLength >= sizeof(SBucket::m_aReason) || PlanStatusLength >= sizeof(SBucket::m_aPlanStatus))
			return false;
		for(size_t Index = 0; Index < m_Count; ++Index)
		{
			const SBucket &Bucket = m_aBuckets[Index];
			if(Bucket.m_Scope == Scope && Bucket.m_Page == Page && Bucket.m_Tab == Tab && Bucket.m_Subtab == Subtab &&
				std::strcmp(Bucket.m_aReason, pSafeReason) == 0 && std::strcmp(Bucket.m_aPlanStatus, pSafePlanStatus) == 0)
				return false;
		}
		SBucket &Bucket = m_aBuckets[m_Count++];
		Bucket.m_Scope = Scope;
		Bucket.m_Page = Page;
		Bucket.m_Tab = Tab;
		Bucket.m_Subtab = Subtab;
		std::memcpy(Bucket.m_aReason, pSafeReason, ReasonLength + 1);
		std::memcpy(Bucket.m_aPlanStatus, pSafePlanStatus, PlanStatusLength + 1);
		return true;
	}

private:
	std::array<SBucket, MAX_BUCKETS> m_aBuckets{};
	size_t m_Count = 0;
	uint64_t m_Session = 0;
	uint64_t m_CoverageFrame = 0;
};

#endif
