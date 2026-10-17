#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_EXECUTION_SCOPE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_EXECUTION_SCOPE_H

class CQmScriptExecutionScope
{
	// 同一线程的 include 与新解释器共用预算，计数持续到错误处理和释放结束。
	inline static thread_local int s_ActiveDepth = 0;
	const bool m_Entered;

public:
	static constexpr int MAX_DEPTH = 16;
	static constexpr const char *ERROR = "Maximum script file nesting depth exceeded";

	static bool CanEnter() noexcept { return s_ActiveDepth < MAX_DEPTH; }

	CQmScriptExecutionScope() noexcept :
		m_Entered(CanEnter())
	{
		if(m_Entered)
			++s_ActiveDepth;
	}
	~CQmScriptExecutionScope()
	{
		if(m_Entered)
			--s_ActiveDepth;
	}
	CQmScriptExecutionScope(const CQmScriptExecutionScope &) = delete;
	CQmScriptExecutionScope &operator=(const CQmScriptExecutionScope &) = delete;

	bool Entered() const { return m_Entered; }
};

#endif
