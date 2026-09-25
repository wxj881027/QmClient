#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_SAVES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_SAVES_H

#include <base/str.h>

#include <engine/shared/protocol.h>

#include <game/teamscore.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace QmLocalSaves
{
	struct SEntry
	{
		std::string m_Time;
		std::string m_Players;
		std::string m_Map;
		std::string m_Code;
		bool m_CompletePlayers = true;
	};

	inline std::string_view Trim(std::string_view Text)
	{
		while(!Text.empty() && str_isspace(Text.front()))
			Text.remove_prefix(1);
		while(!Text.empty() && str_isspace(Text.back()))
			Text.remove_suffix(1);
		return Text;
	}

	enum class EReply
	{
		NONE,
		INVALID,
		YES,
		NO,
	};

	struct SReply
	{
		EReply m_Kind = EReply::NONE;
		int m_Index = 1;
	};

	inline SReply ParseReply(std::string_view Text)
	{
		Text = Trim(Text);
		// str_comp_nocase_num 只读取前 3 字节；Text.size() < 3 的短串已由左侧条件短接，data() 不会越界。
		// NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
		if(Text.size() < 3 || str_comp_nocase_num(Text.data(), "/qm", 3) != 0 || (Text.size() > 3 && !str_isspace(Text[3])))
			return {};
		Text = Trim(Text.substr(3));
		const size_t End = Text.find_first_of(" \t\r\n");
		const std::string Answer(Text.substr(0, End));
		const std::string_view Index = End == std::string_view::npos ? std::string_view() : Trim(Text.substr(End));
		if(str_comp_nocase(Answer.c_str(), "No") == 0 && Index.empty())
			return {EReply::NO, 1};
		if(str_comp_nocase(Answer.c_str(), "Yes") != 0)
			return {EReply::INVALID, 1};
		if(Index.empty())
			return {EReply::YES, 1};
		int Number = 0;
		for(const char Ch : Index)
		{
			if(Ch < '0' || Ch > '9' || Number > (std::numeric_limits<int>::max() - (Ch - '0')) / 10)
				return {EReply::INVALID, 1};
			Number = Number * 10 + Ch - '0';
		}
		return {Number > 0 ? EReply::YES : EReply::INVALID, Number};
	}

	inline std::vector<std::string_view> Lines(std::string_view Text)
	{
		std::vector<std::string_view> Result;
		while(!Text.empty())
		{
			const size_t End = Text.find('\n');
			const size_t Length = End == std::string_view::npos ? Text.size() : End + 1;
			Result.push_back(Text.substr(0, Length));
			Text.remove_prefix(Length);
		}
		return Result;
	}

	inline bool ParseRow(std::string_view Line, std::array<std::string, 4> &Fields)
	{
		Fields = {};
		if(Line.substr(0, 3) == "\xef\xbb\xbf")
			Line.remove_prefix(3);
		while(!Line.empty() && (Line.back() == '\r' || Line.back() == '\n'))
			Line.remove_suffix(1);
		size_t Field = 0;
		bool Quoted = false;
		bool Closed = false;
		for(size_t Pos = 0; Pos < Line.size(); ++Pos)
		{
			const char Ch = Line[Pos];
			if(Ch == '"')
			{
				if(Quoted && Pos + 1 < Line.size() && Line[Pos + 1] == '"')
				{
					Fields[Field] += '"';
					++Pos;
				}
				else if(Quoted)
				{
					Quoted = false;
					Closed = true;
				}
				else if(Fields[Field].empty() && !Closed)
					Quoted = true;
				else
					return false;
			}
			else if(Ch == ',' && !Quoted)
			{
				if(++Field == Fields.size())
					return false;
				Closed = false;
			}
			else if(Closed)
				return false;
			else
				Fields[Field] += Ch;
		}
		return !Quoted && Field + 1 == Fields.size();
	}

	inline std::vector<SEntry> ParseEntries(std::string_view Text)
	{
		std::vector<SEntry> Result;
		bool CompletePlayers = true;
		for(const std::string_view Line : Lines(Text))
		{
			std::array<std::string, 4> Fields;
			if(!ParseRow(Line, Fields))
				continue;
			if(Fields[0] == "Time" && Fields[2] == "Map" && Fields[3] == "Code")
			{
				CompletePlayers = Fields[1] == "Players";
				continue;
			}
			Result.push_back({Fields[0], Fields[1], Fields[2], Fields[3], CompletePlayers});
		}
		return Result;
	}

	inline bool RemoveEntries(std::string_view Text, const std::string &Map, const std::string &Code, std::string &Output)
	{
		Output.clear();
		bool Removed = false;
		for(const std::string_view Line : Lines(Text))
		{
			std::array<std::string, 4> Fields;
			if(!Map.empty() && !Code.empty() && ParseRow(Line, Fields) && Fields[0] != "Time" &&
				str_comp_nocase(Fields[2].c_str(), Map.c_str()) == 0 && Fields[3] == Code)
				Removed = true;
			else
				Output.append(Line);
		}
		return Removed;
	}

	inline bool ParseNames(const std::string &Players, std::array<std::string, 2> &Names)
	{
		const size_t Separator = Players.find(", ");
		if(Separator == std::string::npos || Players.find(", ", Separator + 2) != std::string::npos)
			return false;
		Names = {Players.substr(0, Separator), Players.substr(Separator + 2)};
		for(const std::string &Name : Names)
			if(Name.empty() || Name.size() >= MAX_NAME_LENGTH || !str_utf8_check(Name.c_str()) ||
				std::any_of(Name.begin(), Name.end(), [](unsigned char Ch) { return Ch < 32; }))
				return false;
		return Names[0] != Names[1];
	}

	inline std::array<std::string, 2> AssignNames(std::array<std::string, 2> Names, const std::array<std::string, 2> &Current)
	{
		// 本体优先保留已匹配的名字，其次保留分身；均不匹配时使用记录顺序。
		if(Current[0] == Names[1] || (Current[0] != Names[0] && Current[1] == Names[0]))
			std::swap(Names[0], Names[1]);
		return Names;
	}

	inline std::vector<SEntry> Candidates(const std::vector<SEntry> &Entries, const char *pMap)
	{
		std::vector<SEntry> Result;
		for(auto It = Entries.rbegin(); It != Entries.rend(); ++It)
		{
			std::array<std::string, 2> Names;
			if(It->m_CompletePlayers && !It->m_Code.empty() && str_comp_nocase(It->m_Map.c_str(), pMap) == 0 && ParseNames(It->m_Players, Names))
				Result.push_back(*It);
		}
		std::stable_sort(Result.begin(), Result.end(), [](const SEntry &A, const SEntry &B) { return A.m_Time > B.m_Time; });
		std::vector<SEntry> Unique;
		for(const SEntry &Entry : Result)
			if(std::none_of(Unique.begin(), Unique.end(), [&](const SEntry &Other) { return Other.m_Code == Entry.m_Code; }))
				Unique.push_back(Entry);
		return Unique;
	}

	inline std::string LoadCode(std::string_view Text)
	{
		Text = Trim(Text);
		// str_comp_nocase_num 只读取前 5 字节；Text.size() < 6 的短串已由左侧条件短接，data() 不会越界。
		// NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
		if(Text.size() < 6 || str_comp_nocase_num(Text.data(), "/load", 5) != 0 || !str_isspace(Text[5]))
			return {};
		Text = Trim(Text.substr(5));
		if(Text.empty() || Text.front() != '"')
			return std::string(Text);
		std::string Code;
		for(size_t Pos = 1; Pos < Text.size(); ++Pos)
		{
			if(Text[Pos] == '"')
				return Trim(Text.substr(Pos + 1)).empty() ? Code : std::string();
			if(Text[Pos] == '\\' && Pos + 1 < Text.size())
				++Pos;
			Code += Text[Pos];
		}
		return {};
	}

	inline std::string QuotedArgument(const std::string &Text)
	{
		std::string Result = "\"";
		for(const char Ch : Text)
		{
			if(Ch == '\\' || Ch == '"')
				Result += '\\';
			Result += Ch;
		}
		return Result + '"';
	}

	inline std::string LoadCommand(const std::string &Code) { return "/load " + QuotedArgument(Code); }

	enum class EResult
	{
		NONE,
		SUCCESS,
		FAILED,
	};

	class CConfirmation
	{
	public:
		struct SRequest
		{
			std::string m_Map;
			std::string m_Code;
			int m_Conn = 0;
			int m_Team = -1;
			int m_SentTick = 0;
			bool m_WasRacing = false;
			int64_t m_Deadline = 0;
		};

	private:
		SRequest m_Request;
		bool m_Ambiguous = false;

	public:
		bool Active() const { return !m_Request.m_Code.empty(); }
		const SRequest &Request() const { return m_Request; }
		void Reset() { *this = {}; }
		void Track(const SRequest &Request)
		{
			if(!Active())
				m_Request = Request;
			else if(m_Request.m_Code != Request.m_Code || m_Request.m_Map != Request.m_Map || m_Request.m_Team != Request.m_Team)
				m_Ambiguous = true;
		}

		bool Matches(const char *pMap, int Team, int64_t Now) const
		{
			return Active() && Now <= m_Request.m_Deadline && Team == m_Request.m_Team && str_comp_nocase(pMap, m_Request.m_Map.c_str()) == 0;
		}

		EResult Message(int Conn, int Sender, const char *pMessage, const char *pMap, int Team, int64_t Now) const
		{
			if(Sender != -1 || !pMessage || !Matches(pMap, Team, Now))
				return EResult::NONE;
			if(str_comp(pMessage, "Loading successfully done") == 0 || str_comp(pMessage, "存档载入成功") == 0)
				return m_Ambiguous ? EResult::FAILED : EResult::SUCCESS;
			if(Conn != m_Request.m_Conn)
				return EResult::NONE;
			if(str_endswith(pMessage, "doesn't belong to this team") || str_endswith(pMessage, "has to be in this team"))
				return EResult::FAILED;
			for(const char *pPrefix : {"No such savegame for this map", "Unable to load savegame:", "This save exists, but you are not part of it.", "Team can't be loaded", "You have to wait ", "You have to be in a team", "Team load already in progress", "Too many players in this team, should be ", "本服务器已禁用存档功能", "这张地图没有对应的存档", "无法载入存档", "这个存档存在，但你不在其中", "你还需要等待 "})
				if(str_startswith(pMessage, pPrefix))
					return EResult::FAILED;
			return EResult::NONE;
		}

		bool RestoredRace(const char *pMap, int Team, int Tick, int StartTick, int TickSpeed, int64_t Now) const
		{
			// 新版服务器不发送成功聊天消息。只有原本未计时、回传的起跑时间早于请求，才认为恢复了旧计时。
			return Matches(pMap, Team, Now) && !m_Ambiguous && !m_Request.m_WasRacing && Tick >= m_Request.m_SentTick &&
			       (int64_t)StartTick < (int64_t)m_Request.m_SentTick - 2 * TickSpeed;
		}
	};

	enum class EAction
	{
		WAIT,
		CONNECT,
		RENAME,
		JOIN_MAIN,
		INVITE,
		JOIN_DUMMY,
		LOAD,
		FAILED,
	};

	class CRestore
	{
	public:
		struct SWorld
		{
			std::string m_Map;
			bool m_Online = false;
			bool m_DummyConnected = false;
			bool m_DummyConnecting = false;
			bool m_PlayersReady = false;
			bool m_CharactersReady = false;
			bool m_Racing = false;
			std::array<std::string, 2> m_aNames;
			std::array<int, 2> m_aTeams = {-1, -1};
			std::array<int, NUM_DDRACE_TEAMS> m_aTeamSizes = {};
		};

	private:
		SEntry m_Entry;
		std::array<std::string, 2> m_aNames;
		int m_Team = -1;
		int64_t m_Deadline = 0;
		int64_t m_NextAction = 0;
		bool m_ConnectSent = false;
		bool m_RenameSent = false;
		bool m_MainJoinSent = false;
		bool m_InviteSent = false;
		bool m_DummyJoinSent = false;
		bool m_LoadSent = false;

	public:
		bool Active() const { return !m_Entry.m_Code.empty(); }
		const SEntry &Entry() const { return m_Entry; }
		const std::array<std::string, 2> &Names() const { return m_aNames; }
		int Team() const { return m_Team; }
		void Reset() { *this = {}; }
		void Begin(const SEntry &Entry, const std::array<std::string, 2> &Names, int64_t Now, int64_t Frequency)
		{
			Reset();
			m_Entry = Entry;
			m_aNames = Names;
			m_Deadline = Now + 60 * Frequency;
		}

		EAction Update(const SWorld &World, int64_t Now, int64_t Frequency)
		{
			if(!Active())
				return EAction::WAIT;
			if(!World.m_Online || str_comp_nocase(World.m_Map.c_str(), m_Entry.m_Map.c_str()) != 0 || Now > m_Deadline)
				return EAction::FAILED;
			if(Now < m_NextAction || m_LoadSent)
				return EAction::WAIT;
			const auto Send = [&](bool &Sent, EAction Action) {
				Sent = true;
				m_NextAction = Now + Frequency;
				return Action;
			};
			if(!World.m_DummyConnected)
				return !m_ConnectSent && !World.m_DummyConnecting ? Send(m_ConnectSent, EAction::CONNECT) : EAction::WAIT;
			if(!World.m_PlayersReady)
				return EAction::WAIT;
			if(World.m_Racing)
				return EAction::FAILED;
			if(World.m_aNames != m_aNames)
				return !m_RenameSent ? Send(m_RenameSent, EAction::RENAME) : EAction::WAIT;
			if(!World.m_CharactersReady)
				return EAction::WAIT;
			if(m_Team < 0)
			{
				const int Current = World.m_aTeams[0];
				if(Current > TEAM_FLOCK && Current < TEAM_SUPER && Current == World.m_aTeams[1] && World.m_aTeamSizes[Current] == 2)
					m_Team = Current;
				else
					for(int Team = 1; Team < TEAM_SUPER; ++Team)
						if(World.m_aTeamSizes[Team] == 0)
						{
							m_Team = Team;
							break;
						}
				if(m_Team < 0)
					return EAction::FAILED;
			}
			const int OwnMembers = (World.m_aTeams[0] == m_Team ? 1 : 0) + (World.m_aTeams[1] == m_Team ? 1 : 0);
			if(World.m_aTeamSizes[m_Team] > OwnMembers)
				return EAction::FAILED;
			if(World.m_aTeams[0] != m_Team)
				return !m_MainJoinSent ? Send(m_MainJoinSent, EAction::JOIN_MAIN) : EAction::WAIT;
			if(World.m_aTeams[1] != m_Team)
			{
				if(!m_InviteSent)
					return Send(m_InviteSent, EAction::INVITE);
				return !m_DummyJoinSent ? Send(m_DummyJoinSent, EAction::JOIN_DUMMY) : EAction::WAIT;
			}
			return World.m_aTeamSizes[m_Team] == 2 ? Send(m_LoadSent, EAction::LOAD) : EAction::WAIT;
		}
	};
}

#endif
