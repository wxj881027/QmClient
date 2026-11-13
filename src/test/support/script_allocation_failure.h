#ifndef TEST_SUPPORT_SCRIPT_ALLOCATION_FAILURE_H
#define TEST_SUPPORT_SCRIPT_ALLOCATION_FAILURE_H

// 只由独立脚本分配测试目标使用，不接入客户端或通用 testrunner。
class CQmScriptAllocationFailure
{
	inline static thread_local CQmScriptAllocationFailure *s_pActive = nullptr;
	CQmScriptAllocationFailure *m_pPrevious;
	bool m_Triggered = false;

public:
	CQmScriptAllocationFailure() noexcept :
		m_pPrevious(s_pActive)
	{
		s_pActive = this;
	}
	~CQmScriptAllocationFailure() { s_pActive = m_pPrevious; }
	CQmScriptAllocationFailure(const CQmScriptAllocationFailure &) = delete;
	CQmScriptAllocationFailure &operator=(const CQmScriptAllocationFailure &) = delete;

	bool Triggered() const { return m_Triggered; }
	static bool FailNextAllocation() noexcept
	{
		if(!s_pActive || s_pActive->m_Triggered)
			return false;
		s_pActive->m_Triggered = true;
		return true;
	}
};

#endif
