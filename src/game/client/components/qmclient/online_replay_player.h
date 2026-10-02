#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_ONLINE_REPLAY_PLAYER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_ONLINE_REPLAY_PLAYER_H

#include <base/system.h>

#include <engine/demo.h>
#include <engine/keys.h>

#include <game/client/components/binds_deepfly_mode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

// 只识别顶层旁观命令，避免把聊天文本、嵌套 bind 或相似命令当成镜头操作。
inline bool OnlineReplayHasSpectatorBind(const char *pBind)
{
	bool Found = false;
	ForEachTopLevelBindCommand(pBind, [&](const char *pCommand) {
		const char *apNames[] = {"spectate", "spectate_next", "spectate_previous", "spectate_closest", "+spectate"};
		for(const char *pName : apNames)
		{
			const int Length = str_length(pName);
			if(str_comp_nocase_num(pCommand, pName, Length) == 0 && (pCommand[Length] == '\0' || pCommand[Length] == ' '))
				Found = true;
		}
	});
	return Found;
}

// 按输入帧记录归旁观绑定的键；镜头已切成跟随后，本帧也不能再触发时间轴。
class COnlineReplayShortcutClaims
{
	uint64_t m_Frame = 0;
	std::array<bool, KEY_LAST> m_aKeys{};

public:
	void Claim(uint64_t Frame, int Key)
	{
		if(Frame != m_Frame)
		{
			m_aKeys.fill(false);
			m_Frame = Frame;
		}
		if(Key > KEY_UNKNOWN && Key < KEY_LAST)
			m_aKeys[Key] = true;
	}
	bool Claimed(uint64_t Frame, int Key) const
	{
		return Frame == m_Frame && Key > KEY_UNKNOWN && Key < KEY_LAST && m_aKeys[Key];
	}
};

// 在线时间线与普通 demo 共用播放器接口；不接触服务器快照、聊天或网络旁观状态。
class COnlineReplayPlayer final : public IDemoPlayer
{
public:
	class ISource
	{
	public:
		virtual ~ISource() = default;
		virtual CInfo PlaybackInfo() const = 0;
		virtual bool PlaybackActive() const = 0;
		virtual int PlaybackTickSpeed() const = 0;
		virtual void PlaybackSeek(int Tick) = 0;
		virtual int PlaybackAdjacentTick(int Tick, ETickOffset Offset) const
		{
			return Tick + (Offset == TICK_PREVIOUS ? -1 : Offset == TICK_NEXT ? 1 :
											    0);
		}
		virtual void PlaybackSetPlaying(bool Playing) = 0;
		virtual void PlaybackSetSpeed(float Speed) = 0;
		virtual const char *PlaybackFilename() const = 0;
		virtual IDemoPlayer *PlaybackMetadataReader() const = 0;
	};

private:
	ISource &m_Source;
	mutable CInfo m_Info{};

public:
	explicit COnlineReplayPlayer(ISource &Source) : m_Source(Source) {}

	const CInfo *BaseInfo() const override
	{
		m_Info = m_Source.PlaybackInfo();
		return &m_Info;
	}
	bool IsPlaying() const override { return m_Source.PlaybackActive(); }
	void Pause() override
	{
		m_Source.PlaybackSetPlaying(false);
		BaseInfo();
	}
	void Unpause() override
	{
		m_Source.PlaybackSetPlaying(true);
		BaseInfo();
	}
	void SetSpeed(float Speed) override
	{
		if(std::isfinite(Speed))
			m_Source.PlaybackSetSpeed(std::clamp(Speed, (float)DEMO_SPEEDS[0], (float)DEMO_SPEEDS[std::size(DEMO_SPEEDS) - 1]));
		BaseInfo();
	}
	void SetSpeedIndex(int Index) override
	{
		SetSpeed((float)DEMO_SPEEDS[std::clamp(Index, 0, (int)std::size(DEMO_SPEEDS) - 1)]);
	}
	void AdjustSpeedIndex(int Offset) override
	{
		const float Speed = BaseInfo()->m_Speed;
		int Index = 0;
		for(int i = 0; i < (int)std::size(DEMO_SPEEDS); ++i)
		{
			if(Speed >= DEMO_SPEEDS[i])
				Index = i;
		}
		SetSpeedIndex((int)std::clamp((int64_t)Index + Offset, (int64_t)0, (int64_t)std::size(DEMO_SPEEDS) - 1));
	}
	bool SetPos(int Tick) override
	{
		if(!IsPlaying())
			return false;
		const CInfo Info = *BaseInfo();
		m_Source.PlaybackSeek(std::clamp(Tick, Info.m_FirstTick, std::max(Info.m_FirstTick, Info.m_LastTick)));
		BaseInfo();
		return true;
	}
	bool SeekPercent(float Percent) override
	{
		if(!std::isfinite(Percent) || Percent < 0.0f || Percent > 1.0f)
			return false;
		const CInfo Info = *BaseInfo();
		return SetPos(Info.m_FirstTick + (int)((Info.m_LastTick - Info.m_FirstTick) * (double)Percent));
	}
	bool SeekTime(float Seconds) override
	{
		if(!std::isfinite(Seconds))
			return false;
		const CInfo Info = *BaseInfo();
		const double Tick = Info.m_CurrentTick + (double)Seconds * std::max(1, m_Source.PlaybackTickSpeed());
		return SetPos((int)std::clamp(Tick, (double)Info.m_FirstTick, (double)std::max(Info.m_FirstTick, Info.m_LastTick)));
	}
	bool SeekTick(ETickOffset Offset) override
	{
		const CInfo Info = *BaseInfo();
		return SetPos(m_Source.PlaybackAdjacentTick(Info.m_CurrentTick, Offset));
	}
	const char *ErrorMessage() const override { return ""; }
	const char *Filename() const override { return m_Source.PlaybackFilename(); }
	void GetDemoName(char *pBuffer, size_t BufferSize) const override
	{
		const char *pBase = strrchr(Filename(), '/');
		fs_split_file_extension(pBase ? pBase + 1 : Filename(), pBuffer, BufferSize);
	}
	bool GetDemoInfo(IStorage *pStorage, IConsole *pConsole, const char *pFilename, int StorageType, CDemoHeader *pHeader, CTimelineMarkers *pMarkers, CMapInfo *pMap, IOHANDLE *pFile = nullptr, char *pError = nullptr, size_t ErrorSize = 0) const override
	{
		IDemoPlayer *pReader = m_Source.PlaybackMetadataReader();
		return pReader && pReader->GetDemoInfo(pStorage, pConsole, pFilename, StorageType, pHeader, pMarkers, pMap, pFile, pError, ErrorSize);
	}
};

// 跳到实际采样帧，稀疏轨迹不把空 tick 当作下一帧。
inline int OnlineReplayAdjacentTick(const std::vector<int> &vTicks, int Current, IDemoPlayer::ETickOffset Offset)
{
	if(vTicks.empty() || Offset == IDemoPlayer::TICK_CURRENT)
		return Current;
	if(Offset == IDemoPlayer::TICK_NEXT)
	{
		const auto Next = std::upper_bound(vTicks.begin(), vTicks.end(), Current);
		return Next == vTicks.end() ? vTicks.back() : *Next;
	}
	const auto Previous = std::lower_bound(vTicks.begin(), vTicks.end(), Current);
	return Previous == vTicks.begin() ? vTicks.front() : *std::prev(Previous);
}

// 单调时间驱动的回放时钟，暂停、变速和跳转都保留同一播放头。
class COnlineReplayClock
{
	double m_Position = 0.0;
	double m_Anchor = 0.0;
	int m_End = 0;
	int m_TickSpeed = 50;
	float m_Speed = 1.0f;
	bool m_Playing = false;

public:
	void Start(double Now, int End, int TickSpeed)
	{
		m_Position = 0.0;
		m_Anchor = Now;
		m_End = std::max(0, End);
		m_TickSpeed = std::max(1, TickSpeed);
		m_Speed = 1.0f;
		m_Playing = m_End > 0;
	}
	double Position(double Now) const
	{
		return std::clamp(m_Position + (m_Playing ? std::max(0.0, Now - m_Anchor) * m_TickSpeed * m_Speed : 0.0), 0.0, (double)m_End);
	}
	int Tick(double Now) const { return (int)Position(Now); }
	float Intra(double Now) const
	{
		const double Pos = Position(Now);
		return (float)(Pos - std::floor(Pos));
	}
	bool Playing() const { return m_Playing; }
	float Speed() const { return m_Speed; }
	void SetPlaying(bool Playing, double Now)
	{
		m_Position = Position(Now);
		m_Anchor = Now;
		m_Playing = Playing && m_Position < m_End;
	}
	void SetSpeed(float Speed, double Now)
	{
		if(!std::isfinite(Speed))
			return;
		m_Position = Position(Now);
		m_Anchor = Now;
		m_Speed = std::clamp(Speed, (float)DEMO_SPEEDS[0], (float)DEMO_SPEEDS[std::size(DEMO_SPEEDS) - 1]);
	}
	void Seek(int Tick, double Now)
	{
		m_Position = std::clamp(Tick, 0, m_End);
		m_Anchor = Now;
		if(m_Position >= m_End)
			m_Playing = false;
	}
};

// 多人视角的选择集合属于回放，不写服务器玩家的多人视角数组。
class COnlineReplayMembers
{
	std::vector<bool> m_Selected;

public:
	void Reset(size_t Count) { m_Selected.assign(Count, true); }
	void Remove(size_t Index)
	{
		if(Index < m_Selected.size())
			m_Selected.erase(m_Selected.begin() + Index);
		if(!m_Selected.empty() && std::find(m_Selected.begin(), m_Selected.end(), true) == m_Selected.end())
			m_Selected[0] = true;
	}
	bool Selected(size_t Index) const { return Index < m_Selected.size() && m_Selected[Index]; }
	bool Toggle(size_t Index)
	{
		if(Index >= m_Selected.size())
			return false;
		if(m_Selected[Index] && std::count(m_Selected.begin(), m_Selected.end(), true) == 1)
			return false;
		m_Selected[Index] = !m_Selected[Index];
		return true;
	}
};

// Esc 的控制层状态独立于播放会话；打开菜单不产生停止或镜头重置命令。
class COnlineReplayUiState
{
	bool m_PanelOpen = false;
	bool m_EscapeDown = false;
	bool m_Armed = false;
	bool m_PanelBeforeEscape = false;
	float m_LastEscapeTime = 0.0f;

public:
	enum class EAction
	{
		NONE,
		TOGGLE_PANEL,
		OPEN_MENU
	};
	bool PanelOpen() const { return m_PanelOpen; }
	void Reset() { *this = {}; }
	void ReleaseEscape() { m_EscapeDown = false; }
	void CancelEscape()
	{
		m_Armed = false;
		m_EscapeDown = false;
	}
	EAction PressEscape(float Now)
	{
		if(m_EscapeDown)
			return EAction::NONE;
		m_EscapeDown = true;
		if(m_Armed && Now >= m_LastEscapeTime && Now - m_LastEscapeTime <= 0.4f)
		{
			m_Armed = false;
			m_PanelOpen = m_PanelBeforeEscape;
			return EAction::OPEN_MENU;
		}
		m_PanelBeforeEscape = m_PanelOpen;
		m_PanelOpen = !m_PanelOpen;
		m_Armed = true;
		m_LastEscapeTime = Now;
		return EAction::TOGGLE_PANEL;
	}
};

#endif
