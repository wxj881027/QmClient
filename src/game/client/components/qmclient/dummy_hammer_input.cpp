#include "dummy_hammer_input.h"

namespace
{
	constexpr int HAMMER_INTERVAL = 25;
	constexpr int HAMMER_RESENDS = 2;

	int ReleasedFire(int Fire)
	{
		return ((Fire + 1) & ~1) & INPUT_STATE_MASK;
	}
}

void CQmDummyHammerInput::Reset()
{
	*this = CQmDummyHammerInput{};
}

void CQmDummyHammerInput::ObserveManualInput(int Fire)
{
	// 切回手动控制后不补发上一轮分身输入，后续锤击从这条连接的实际输入继续计数。
	Reset();
	m_Fire = Fire & INPUT_STATE_MASK;
	m_HammerInput.m_Fire = m_Fire;
}

void CQmDummyHammerInput::SetEnabled(bool Enabled)
{
	if(Enabled && !m_Enabled)
		++m_PendingPresses;
	m_Enabled = Enabled;
}

void CQmDummyHammerInput::SynchronizeEnabled(bool Enabled)
{
	// 直接覆写配置的停用可以取消未发出的请求；真实松键已由命令链记录，不会走此分支。
	if(!Enabled && m_Enabled)
		m_PendingPresses = 0;
	SetEnabled(Enabled);
}

bool CQmDummyHammerInput::SnapInput(CNetObj_PlayerInput &Output, CNetObj_PlayerInput &DummyInput, vec2 Direction, bool RestoreWeapon, bool Force)
{
	if(m_Enabled && m_TicksUntilHammer > 0)
		--m_TicksUntilHammer;

	if(m_PendingPresses != 0 || (m_Enabled && m_TicksUntilHammer == 0))
	{
		const unsigned int AdditionalPresses = m_PendingPresses != 0 ? m_PendingPresses - 1 : 0;
		// 从上一份输出推进到新的按下；同一采样间隔内的多次点击也保留在协议计数中。
		m_Fire = static_cast<int>((static_cast<unsigned int>((m_Fire + 1) | 1) + 2u * AdditionalPresses) & INPUT_STATE_MASK);
		if(!m_Enabled)
			m_Fire = ReleasedFire(m_Fire);
		m_HammerInput = {};
		m_HammerInput.m_Fire = m_Fire;
		m_HammerInput.m_WantedWeapon = WEAPON_HAMMER + 1;
		m_HammerInput.m_TargetX = static_cast<int>(Direction.x);
		m_HammerInput.m_TargetY = static_cast<int>(Direction.y);
		if(!RestoreWeapon)
			DummyInput.m_WantedWeapon = WEAPON_HAMMER + 1;
		DummyInput.m_Fire = m_Fire;
		m_PendingPresses = 0;
		m_OwnsFire = true;
		m_ReleaseSent = !m_Enabled;
		m_TicksUntilHammer = HAMMER_INTERVAL;
		m_Resends = HAMMER_RESENDS;
		Output = m_HammerInput;
		return true;
	}

	if(m_OwnsFire)
	{
		DummyInput.m_Fire = m_Fire;
		if(!m_Enabled && !m_ReleaseSent)
		{
			// 松开立即结束按住状态，同时保留这次锤击的武器和瞄准，让剩余补发可以补回首包。
			m_Fire = ReleasedFire(m_Fire);
			m_HammerInput.m_Fire = m_Fire;
			DummyInput.m_Fire = m_Fire;
			m_ReleaseSent = true;
			if(m_Resends > 0)
				--m_Resends;
			Output = m_HammerInput;
			return true;
		}
		if(m_Resends > 0)
		{
			--m_Resends;
			Output = m_HammerInput;
			return true;
		}
		if(m_Enabled)
			return false;

		// 补发结束后主动交还普通输入，不让武器恢复或 Fire 松开依赖 Force 轮到本次采样。
		m_OwnsFire = false;
		Output = DummyInput;
		return true;
	}

	if(!Force && !DummyInput.m_Direction && !DummyInput.m_Jump && !DummyInput.m_Hook)
		return false;
	Output = DummyInput;
	m_Fire = Output.m_Fire & INPUT_STATE_MASK;
	return true;
}
