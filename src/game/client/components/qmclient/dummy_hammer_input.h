#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_HAMMER_INPUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_HAMMER_INPUT_H

#include <base/vmath.h>

#include <generated/protocol.h>

// 每条连接独立保留按下事件、Fire 计数和补发输入，不依赖某一帧的配置最终值。
class CQmDummyHammerInput
{
	bool m_Enabled = false;
	unsigned int m_PendingPresses = 0;
	bool m_OwnsFire = false;
	bool m_ReleaseSent = false;
	int m_Fire = 0;
	int m_TicksUntilHammer = 0;
	int m_Resends = 0;
	CNetObj_PlayerInput m_HammerInput{};

public:
	void Reset();
	void ObserveManualInput(int Fire);
	void SetEnabled(bool Enabled);
	void SynchronizeEnabled(bool Enabled);
	bool SnapInput(CNetObj_PlayerInput &Output, CNetObj_PlayerInput &DummyInput, vec2 Direction, bool RestoreWeapon, bool Force);
	const CNetObj_PlayerInput &HammerInput() const { return m_HammerInput; }
};

#endif
