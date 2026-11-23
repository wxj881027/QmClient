#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_INPUT_LOCK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_INPUT_LOCK_H

#include <game/client/components/qmclient/practice_branch_state.h>

#include <array>

// 发往真实服务器的输入锁。练习期间服务器只看到进入练习时的输入状态，
// 退出后的短时间内再压制动作请求，避免落地瞬间挥锤/开枪。
//
// 以前这四个数组和两段分支直接写在 CFastPractice 的发送路径里，规则只能靠
// 实机复现验证；抽出来后 PrepareForSend 成为唯一入口，可以被行为测试直接覆盖。
class CPracticeInputLock
{
public:
	// 会话重置：锁定输入与退出后压制窗口清空。
	// 释放后的开火位图保留——Disable 先清锁、再用它压制缓冲输入，最后才重置。
	void Reset()
	{
		ClearLocks();
		m_aPostPracticeSuppressTicks.fill(0);
	}

	void ClearLocks()
	{
		m_aLockedInputs = {};
		m_aHasLockedInputs.fill(false);
	}

	// 记录服务器输入锁；已经锁过的槽位由调用方判断是否跳过。
	bool Capture(int Slot, const CNetObj_PlayerInput &ServerInput)
	{
		if(Slot < 0 || Slot >= NUM_DUMMIES)
			return false;
		m_aLockedInputs[Slot] = QmBuildPracticeServerLockedInput(ServerInput);
		m_aHasLockedInputs[Slot] = true;
		return true;
	}

	bool HasLockedInput(int Slot) const
	{
		return Slot >= 0 && Slot < NUM_DUMMIES && m_aHasLockedInputs[Slot];
	}

	// 传入服务器当时的开火位图，内部存成“已释放”版本，退出后压制时直接复用。
	void SetReleasedFireState(int Slot, int FireState)
	{
		if(Slot >= 0 && Slot < NUM_DUMMIES)
			m_aReleasedFireStates[Slot] = QmPracticeReleasedFireState(FireState);
	}

	int ReleasedFireState(int Slot) const
	{
		return Slot >= 0 && Slot < NUM_DUMMIES ? m_aReleasedFireStates[Slot] : 0;
	}

	// 退出练习后按槽位压制的 tick 数；每个槽位独立倒计时。
	void BeginPostPracticeSuppression(int Ticks)
	{
		m_aPostPracticeSuppressTicks.fill(Ticks);
	}

	// 发送路径唯一入口：
	//   练习中  —— 有锁定输入就用锁定输入，否则发中性输入；
	//   已退出  —— 在压制窗口内改写动作请求并递减倒计时。
	void PrepareForSend(CNetObj_PlayerInput &Input, int Slot, bool Enabled)
	{
		if(Slot < 0 || Slot >= NUM_DUMMIES)
			return;
		if(!Enabled)
		{
			if(m_aPostPracticeSuppressTicks[Slot] > 0)
			{
				QmPracticeSuppressActionInput(Input, m_aReleasedFireStates[Slot]);
				m_aPostPracticeSuppressTicks[Slot]--;
			}
			return;
		}
		if(m_aHasLockedInputs[Slot])
			Input = m_aLockedInputs[Slot];
		else
			QmNeutralizePracticeInput(Input);
	}

private:
	std::array<CNetObj_PlayerInput, NUM_DUMMIES> m_aLockedInputs{};
	std::array<bool, NUM_DUMMIES> m_aHasLockedInputs{};
	std::array<int, NUM_DUMMIES> m_aReleasedFireStates{};
	std::array<int, NUM_DUMMIES> m_aPostPracticeSuppressTicks{};
};

#endif
