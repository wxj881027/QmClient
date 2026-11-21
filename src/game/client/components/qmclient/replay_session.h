#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_REPLAY_SESSION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_REPLAY_SESSION_H

// 回放会话的统一查询面。
//
// 现在有两套播放实现：CGhost 的手动回放（存档幽灵、练习组）与
// CRankGhost 的查看模式（官方 Rank 1 回放）。查看模式底层仍委托 CGhost
// 的手动回放，但 HUD、相机、菜单各自读取不同组件的布尔量，判断标准容易分叉。
//
// 这里只定义只读状态与纯策略：输入全是外部提供的标量，输出回答
// 「当前有没有回放、是否在播、播到哪」。不持有任何组件引用，便于行为测试。

namespace qmclient::replay
{
	enum class EReplaySessionKind
	{
		NONE,
		// CGhost 手动回放：存档幽灵的独立时间线
		GHOST_MANUAL,
		// CRankGhost 查看模式：官方 Rank 1 回放（内部同样走手动回放）
		VIEW_MODE,
	};

	struct SReplaySessionInputs
	{
		// CRankGhost::IsViewModeActive()
		bool m_ViewModeActive = false;
		// CGhost::ManualModeActive()
		bool m_ManualActive = false;
		// CGhost::ManualPlaying()
		bool m_ManualPlaying = false;
		// CGhost::ManualPlaybackTick()
		int m_ManualTick = 0;
		// CGhost::ManualEndTick()
		int m_ManualEndTick = 0;
		// CGhost::ManualSpeed()
		float m_ManualSpeed = 1.0f;
	};

	struct SReplaySessionStatus
	{
		EReplaySessionKind m_Kind = EReplaySessionKind::NONE;
		bool m_Active = false;
		bool m_Playing = false;
		// 底层手动回放是否开启。查看模式同样依赖它，因此与 m_Kind 分开表达。
		bool m_ManualActive = false;
		int m_Tick = 0;
		int m_EndTick = 0;
		float m_Speed = 1.0f;

		bool ViewMode() const { return m_Kind == EReplaySessionKind::VIEW_MODE; }
		bool ManualGhost() const { return m_Kind == EReplaySessionKind::GHOST_MANUAL; }
		bool ManualActive() const { return m_ManualActive; }
	};

	// 查看模式的优先级高于普通手动回放：查看模式在底层也会打开手动回放，
	// 先判断查看模式，避免把同一次播放报成两种会话。
	inline SReplaySessionStatus QmResolveReplaySessionStatus(const SReplaySessionInputs &Inputs)
	{
		SReplaySessionStatus Status;
		if(Inputs.m_ViewModeActive)
			Status.m_Kind = EReplaySessionKind::VIEW_MODE;
		else if(Inputs.m_ManualActive)
			Status.m_Kind = EReplaySessionKind::GHOST_MANUAL;
		else
			return Status;

		Status.m_Active = true;
		Status.m_ManualActive = Inputs.m_ManualActive;
		Status.m_Playing = Inputs.m_ManualPlaying;
		Status.m_Tick = Inputs.m_ManualTick;
		Status.m_EndTick = Inputs.m_ManualEndTick;
		Status.m_Speed = Inputs.m_ManualSpeed;
		return Status;
	}
}

#endif
