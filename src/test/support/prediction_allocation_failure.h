#ifndef TEST_SUPPORT_PREDICTION_ALLOCATION_FAILURE_H
#define TEST_SUPPORT_PREDICTION_ALLOCATION_FAILURE_H

#include <cstddef>

// 只由独立客户端预测测试程序使用；每个作用域仅使指定的一次分配失败。
class CPredictionAllocationFailure
{
	inline static thread_local CPredictionAllocationFailure *s_pActive = nullptr;
	CPredictionAllocationFailure *m_pPrevious;
	std::size_t m_Remaining;
	std::size_t m_Attempts = 0;
	bool m_Triggered = false;

public:
	explicit CPredictionAllocationFailure(std::size_t BeforeFailure = 0) noexcept :
		m_pPrevious(s_pActive), m_Remaining(BeforeFailure)
	{
		s_pActive = this;
	}
	~CPredictionAllocationFailure() { s_pActive = m_pPrevious; }
	CPredictionAllocationFailure(const CPredictionAllocationFailure &) = delete;
	CPredictionAllocationFailure &operator=(const CPredictionAllocationFailure &) = delete;

	bool Triggered() const { return m_Triggered; }
	std::size_t Attempts() const { return m_Attempts; }
	static bool FailNextAllocation() noexcept
	{
		if(!s_pActive)
			return false;
		++s_pActive->m_Attempts;
		if(s_pActive->m_Triggered)
			return false;
		if(s_pActive->m_Remaining > 0)
		{
			--s_pActive->m_Remaining;
			return false;
		}
		s_pActive->m_Triggered = true;
		return true;
	}
};

#endif
