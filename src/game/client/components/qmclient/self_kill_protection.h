#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SELF_KILL_PROTECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SELF_KILL_PROTECTION_H

#include <base/types.h>

// 自杀保护的状态机。
//
// 语义：默认不拦截，只拦客户端主动发起的 K 键 / kill 命令；服务端命令不受影响。
// 拦截状态只在玩家自己按下绑定键（qm_self_kill_off / qm_self_kill_on）或
// 在设置卡里勾选后成立，不会"自己打开"。
//
// "开局 N 分钟后自动开启"是可选项，默认关闭（DelaySeconds = 0 = 永不自动开启）。
// 自动开启只改本局的运行时状态，不写回配置：否则一次触发会留在存档里，
// 下一次启动就变成"莫名其妙自己开着"。配置只记录玩家的意图（键位、默认状态、延迟）。
class CQmSelfKillProtection
{
public:
	// 读取配置与开局状态。Enabled 是配置里的默认状态，DelaySeconds 是自动开启延迟。
	// 配置值本身变了（玩家在设置卡或控制台改了）就当作一次手动开关立即生效，
	// 对局中改也照做；只有"配置没变"时才靠 BeginRound 取默认值。
	void Configure(bool Enabled, int DelaySeconds)
	{
		m_AutoDelaySeconds = DelaySeconds > 0 ? DelaySeconds : 0;
		if(Enabled != m_ConfiguredEnabled)
		{
			m_ConfiguredEnabled = Enabled;
			m_Blocking = Enabled;
			return;
		}
		m_ConfiguredEnabled = Enabled;
		if(!m_RoundActive)
			m_Blocking = Enabled;
	}

	// 进入一局：本局状态回到配置默认值，自动开启重新计时。
	void BeginRound()
	{
		m_RoundActive = true;
		m_RoundSeconds = 0.0f;
		m_AutoFiredThisRound = false;
		m_Blocking = m_ConfiguredEnabled;
	}

	// 离开对局 / 切换服务器：回到配置默认值并停止计时。
	void EndRound()
	{
		m_RoundActive = false;
		m_RoundSeconds = 0.0f;
		m_AutoFiredThisRound = false;
		m_Blocking = m_ConfiguredEnabled;
	}

	// 玩家手动开关：写运行时状态（并按需写回配置由调用方处理）。
	void SetEnabled(bool Enabled) { m_Blocking = Enabled; }
	bool Enabled() const { return m_Blocking; }

	// 是否拦截客户端的自杀请求。
	bool BlocksKill() const { return m_Blocking; }

	// 每帧推进；返回 true 表示这一帧刚好自动开启，调用方据此回显提示。
	// 本局只触发一次：之后玩家手动关掉，不会被同一次延迟再打开。
	bool Update(float FrameSeconds)
	{
		if(!m_RoundActive || m_AutoDelaySeconds <= 0 || m_AutoFiredThisRound)
			return false;
		if(FrameSeconds > 0.0f)
			m_RoundSeconds += FrameSeconds;
		if(m_RoundSeconds < static_cast<float>(m_AutoDelaySeconds))
			return false;
		m_AutoFiredThisRound = true;
		// 已经开着就不再重复回显，但延迟仍然算用掉。
		const bool Changed = !m_Blocking;
		m_Blocking = true;
		return Changed;
	}

	// 距自动开启还剩几秒；未启用、已触发或不在对局中时返回 0。
	float SecondsUntilAutoEnable() const
	{
		if(!m_RoundActive || m_AutoDelaySeconds <= 0 || m_AutoFiredThisRound)
			return 0.0f;
		const float Remaining = static_cast<float>(m_AutoDelaySeconds) - m_RoundSeconds;
		return Remaining > 0.0f ? Remaining : 0.0f;
	}

	int AutoDelaySeconds() const { return m_AutoDelaySeconds; }
	bool AutoEnableScheduled() const { return m_AutoDelaySeconds > 0 && !m_AutoFiredThisRound; }

private:
	bool m_ConfiguredEnabled = false;
	int m_AutoDelaySeconds = 0;
	bool m_RoundActive = false;
	bool m_Blocking = false;
	float m_RoundSeconds = 0.0f;
	bool m_AutoFiredThisRound = false;
};

#endif
