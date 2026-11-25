// 饼菜单的选项、目标身份和持续跟随状态，不依赖绘制或网络实例。
#ifndef GAME_CLIENT_COMPONENTS_PIE_MENU_LOGIC_H
#define GAME_CLIENT_COMPONENTS_PIE_MENU_LOGIC_H

#include <base/str.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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

	struct SRenameOption
	{
		std::string m_Label;
		std::string m_Name;
		std::string m_DummyName;

		std::array<const char *, 2> Names(bool UseDummy) const
		{
			// 双名项固定对应连接身份，单名项继续跟随当前控制的角色。
			if(!m_DummyName.empty())
				return {m_Name.c_str(), m_DummyName.c_str()};
			if(UseDummy)
				return {nullptr, m_Name.c_str()};
			return {m_Name.c_str(), nullptr};
		}
	};

	inline std::vector<SRenameOption> ParseRenameQueue(const char *pQueue)
	{
		std::vector<SRenameOption> vOptions;
		if(pQueue == nullptr)
			return vOptions;

		const auto TrimName = [](std::string Name) {
			str_utf8_trim_right(Name.data());
			Name.resize(str_length(Name.c_str()));
			Name.erase(0, str_utf8_skip_whitespaces(Name.c_str()) - Name.c_str());
			return Name;
		};
		const std::string Queue(pQueue);
		for(size_t Start = 0; Start < Queue.size();)
		{
			const size_t End = Queue.find('|', Start);
			SRenameOption Option;
			Option.m_Label = TrimName(Queue.substr(Start, End == std::string::npos ? End : End - Start));
			Start = End == std::string::npos ? Queue.size() : End + 1;
			if(Option.m_Label.empty())
				continue;

			const size_t Comma = Option.m_Label.find(',');
			Option.m_Name = TrimName(Option.m_Label.substr(0, Comma));
			if(Comma != std::string::npos)
			{
				Option.m_DummyName = TrimName(Option.m_Label.substr(Comma + 1));
				// 不完整的双名项不执行，避免把任一角色的名字清空。
				if(Option.m_Name.empty() || Option.m_DummyName.empty())
					continue;
			}
			vOptions.push_back(std::move(Option));
		}
		return vOptions;
	}

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

} // namespace qm_pie_menu

#endif
