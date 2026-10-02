// 饼菜单的选项、目标身份和持续跟随状态，不依赖绘制或网络实例。
#ifndef GAME_CLIENT_COMPONENTS_PIE_MENU_LOGIC_H
#define GAME_CLIENT_COMPONENTS_PIE_MENU_LOGIC_H

#include <base/str.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace qm_pie_menu
{
	// 显示生命周期独立于业务交互；提交前先封闭输入，避免回调重入重复执行。
	class CMenuLifecycle
	{
	public:
		enum class EState
		{
			INACTIVE,
			OPENING,
			ACTIVE,
			CLOSING
		};

		enum class EInputKind
		{
			ACTION,
			CANCEL,
			OTHER
		};

	private:
		EState m_State = EState::INACTIVE;

	public:
		EState State() const { return m_State; }
		bool IsVisible() const { return m_State != EState::INACTIVE; }
		bool IsInteractive() const { return m_State == EState::OPENING || m_State == EState::ACTIVE; }
		// 关闭期只吞掉本菜单动作，开菜单绑定仍可下传到 binds。
		bool CapturesClosingInput(EInputKind Kind) const { return m_State == EState::CLOSING && Kind != EInputKind::OTHER; }
		void Open() { m_State = EState::OPENING; }
		void FinishOpening()
		{
			if(m_State == EState::OPENING)
				m_State = EState::ACTIVE;
		}
		void Cancel() { m_State = EState::INACTIVE; }
		bool BeginCommit()
		{
			if(!IsInteractive())
				return false;
			m_State = EState::CLOSING;
			return true;
		}
	};

	enum class EOption
	{
		FRIEND = 0,
		WHISPER,
		MENTION,
		COPY_SKIN,
		SWAP,
		SPECTATE,
		INVITE_TEAM,
		JOIN_TEAM,
		FOLLOW,
		SCORE,
		COPY_NAME,
		NUM_OPTIONS,
	};

	constexpr size_t OPTION_COUNT = static_cast<size_t>(EOption::NUM_OPTIONS);

	inline int BuildVisibleOptions(const std::array<bool, OPTION_COUNT> &Enabled, std::array<EOption, OPTION_COUNT> &Out)
	{
		int Count = 0;
		for(size_t Index = 0; Index < OPTION_COUNT; ++Index)
		{
			if(Enabled[Index])
				Out[Count++] = static_cast<EOption>(Index);
		}
		return Count;
	}

	inline int SectorAtAngle(float Angle, float StartAngle, int SectorCount)
	{
		if(SectorCount <= 0 || !std::isfinite(Angle) || !std::isfinite(StartAngle))
			return -1;
		float RelativeAngle = std::fmod(Angle - StartAngle, 360.0f);
		if(RelativeAngle < 0.0f)
			RelativeAngle += 360.0f;
		return std::min(static_cast<int>(RelativeAngle / (360.0f / SectorCount)), SectorCount - 1);
	}

	inline bool MatchesPlayer(const char *pName, const char *pClan, const char *pExpectedName, const char *pExpectedClan, bool IgnoreClan = false)
	{
		return pName != nullptr && pClan != nullptr && pExpectedName != nullptr && pExpectedClan != nullptr &&
		       pExpectedName[0] != '\0' && str_comp(pName, pExpectedName) == 0 && (IgnoreClan || str_comp(pClan, pExpectedClan) == 0);
	}

	inline std::string QuotedPlayerCommand(const char *pCommand, const char *pName)
	{
		if(pCommand == nullptr || pName == nullptr || pName[0] == '\0')
			return {};
		std::string EscapedName(static_cast<size_t>(str_length(pName)) * 2 + 1, '\0');
		char *pEnd = EscapedName.data();
		str_escape(&pEnd, pName, EscapedName.data() + EscapedName.size());
		EscapedName.resize(pEnd - EscapedName.data());
		return std::string(pCommand) + " \"" + EscapedName + "\"";
	}

	enum class ETeamActionStatus
	{
		READY,
		UNSUPPORTED,
		LOCAL_NEEDS_TEAM,
		TARGET_NEEDS_TEAM,
		ALREADY_TOGETHER,
	};

	inline ETeamActionStatus TeamActionStatus(EOption Option, bool HasRaceTeams, int LocalTeam, int TargetTeam, int TeamSuper)
	{
		if(!HasRaceTeams || (Option != EOption::INVITE_TEAM && Option != EOption::JOIN_TEAM))
			return ETeamActionStatus::UNSUPPORTED;
		if(Option == EOption::INVITE_TEAM && (LocalTeam <= 0 || LocalTeam >= TeamSuper))
			return ETeamActionStatus::LOCAL_NEEDS_TEAM;
		if(Option == EOption::JOIN_TEAM && (TargetTeam < 0 || TargetTeam >= TeamSuper))
			return ETeamActionStatus::TARGET_NEEDS_TEAM;
		if(LocalTeam == TargetTeam)
			return ETeamActionStatus::ALREADY_TOGETHER;
		return ETeamActionStatus::READY;
	}

	struct SFollowState
	{
		bool m_Active = false;
		std::string m_Name;
		std::string m_Clan;
		std::string m_PendingAddress;
		double m_PendingConnectTime = 0.0;
		double m_RetryNotBefore = 0.0;
	};

	inline void StartFollow(SFollowState &State, const char *pName, const char *pClan)
	{
		State = SFollowState();
		State.m_Active = pName != nullptr && pName[0] != '\0';
		if(!State.m_Active)
			return;
		State.m_Name = pName;
		State.m_Clan = pClan != nullptr ? pClan : "";
	}

	inline void StopFollow(SFollowState &State)
	{
		State = SFollowState();
	}

	inline bool FollowRefreshDue(bool Refreshing, double Now, int IntervalSeconds, double &NextRefresh)
	{
		const int Interval = std::max(5, IntervalSeconds);
		if(Refreshing)
		{
			// 慢请求完成后先读取结果，避免立刻再次刷新而一直跳过目标扫描。
			NextRefresh = Now + Interval;
			return false;
		}
		if(Now < NextRefresh)
			return false;
		NextRefresh = Now + Interval;
		return true;
	}

	// 以实际连接地址确认跳转成功；离线只清除候选地址，失败连接按间隔重试。
	inline bool FollowStep(SFollowState &State, bool TargetOnline, const char *pAddress, const char *pCurrentAddress, bool Connecting, double Now, int DelaySeconds, std::string &ConnectAddress)
	{
		ConnectAddress.clear();
		if(!State.m_Active)
			return false;
		if(!TargetOnline || pAddress == nullptr || pAddress[0] == '\0' || (pCurrentAddress != nullptr && str_comp(pAddress, pCurrentAddress) == 0))
		{
			State.m_PendingAddress.clear();
			State.m_PendingConnectTime = 0.0;
			return false;
		}
		if(State.m_PendingAddress != pAddress)
		{
			State.m_PendingAddress = pAddress;
			State.m_PendingConnectTime = Now + std::max(0, DelaySeconds);
		}
		if(Connecting || Now < State.m_PendingConnectTime || Now < State.m_RetryNotBefore)
			return false;

		ConnectAddress = State.m_PendingAddress;
		State.m_RetryNotBefore = Now + std::max(5, DelaySeconds);
		return true;
	}
} // namespace qm_pie_menu

#endif
