#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_INPUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_INPUT_H

#include <generated/protocol.h>

#include <game/client/components/qmclient/input_state.h>

// 分身切换与分身自动上锤的纯判定。
//
// 切换时新受控连接必须继承旧分身的开火位图：否则按住的开火键会在切换
// 瞬间丢失按下状态，玩家要再按一次才能继续开火。自动上锤按固定节拍刷新
// 并按同一份输入重发若干包，单个非关键包丢失时不能吞锤。
// CGameClient 只负责把这里的判定结果写入输入缓冲并发送。

// cl_dummy_resetonswitch：0 关闭；1 重置即将变为分身的槽位；
// 2 重置即将受控的槽位。返回 -1 表示不重置。
inline int QmDummyResetSlot(int ResetMode, int Dummy)
{
	if(ResetMode == 0)
		return -1;
	return ResetMode == 2 ? Dummy : 1 - Dummy;
}

// 分身切换的输入交接。DummyInput 是发往新分身的输入缓冲，
// InactiveInputData 是刚被切出去的连接的历史输入，
// ActiveInputData 是刚受控连接的输入。新受控连接沿用旧分身的开火位图。
inline void QmApplyDummySwap(const CNetObj_PlayerInput &InactiveInputData, CNetObj_PlayerInput &DummyInput, CNetObj_PlayerInput &ActiveInputData)
{
	const int PrevDummyFire = DummyInput.m_Fire;
	DummyInput = InactiveInputData;
	ActiveInputData.m_Fire = PrevDummyFire;
}

// 分身自动上锤：每 QM_DUMMY_HAMMER_INTERVAL 个 SnapInput 刷新一锤，
// 之后的 QM_DUMMY_HAMMER_RESENDS 包重发同一发锤输入。
inline constexpr int QM_DUMMY_HAMMER_INTERVAL = 25;
inline constexpr int QM_DUMMY_HAMMER_RESENDS = 2;

enum class EDummyHammerSnapAction
{
	NONE,
	RESEND,
	REFRESH,
};

struct SDummyHammerSnapDecision
{
	unsigned int m_DummyFire = 0;
	int m_DummyHammerResends = 0;
	EDummyHammerSnapAction m_Action = EDummyHammerSnapAction::NONE;
};

// 一次分身 SnapInput 的节拍判定：非刷新 tick 先消耗重发次数，
// 刷新 tick 生成新的一锤并重置重发窗口。
inline SDummyHammerSnapDecision QmResolveDummyHammerSnap(unsigned int DummyFire, int DummyHammerResends)
{
	SDummyHammerSnapDecision Decision;
	Decision.m_DummyFire = DummyFire + 1;
	if(DummyFire % QM_DUMMY_HAMMER_INTERVAL != 0)
	{
		if(DummyHammerResends > 0)
		{
			Decision.m_DummyHammerResends = DummyHammerResends - 1;
			Decision.m_Action = EDummyHammerSnapAction::RESEND;
		}
		return Decision;
	}
	Decision.m_DummyHammerResends = QM_DUMMY_HAMMER_RESENDS;
	Decision.m_Action = EDummyHammerSnapAction::REFRESH;
	return Decision;
}

#endif
