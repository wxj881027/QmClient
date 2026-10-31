#ifndef TEST_SUPPORT_QM_IME_TEST_PROVIDER_H
#define TEST_SUPPORT_QM_IME_TEST_PROVIDER_H

#include <engine/shared/qm_ime_provider.h>

#include <utility>

// 只替换解码器边界；路由、生命周期和提交仍使用生产输入会话。
class CTestImeProvider final : public IImeProvider
{
public:
	SQmImeProviderState m_State;
	FImeCommitCallback m_Commit;
	std::function<bool(int, int, bool, bool)> m_OnKey;
	std::function<void()> m_OnFrame;
	bool m_Enabled = false;
	bool m_RefuseActivation = false;
	int m_Activations = 0;
	int m_Deactivations = 0;
	int m_FocusLosses = 0;

	void SetCommitCallback(FImeCommitCallback Callback) override { m_Commit = std::move(Callback); }
	void Activate() override
	{
		++m_Activations;
		m_State.m_Active = m_Enabled && !m_RefuseActivation;
	}
	void Deactivate() override
	{
		++m_Deactivations;
		m_State = {};
	}
	bool IsActive() const override { return m_State.m_Active; }
	bool OnKey(int Key, int Modifiers, bool Press, bool Repeat) override { return m_OnKey && m_OnKey(Key, Modifiers, Press, Repeat); }
	void OnFocusLost() override
	{
		++m_FocusLosses;
		const bool Active = IsActive();
		m_State = {};
		m_State.m_Active = Active;
	}
	void OnFrame() override
	{
		if(m_OnFrame)
			m_OnFrame();
	}
	const SQmImeProviderState &State() const override { return m_State; }
	void SetEnabled(bool Enabled) override { m_Enabled = Enabled; }
	bool IsEnabled() const override { return m_Enabled; }
};

#endif
