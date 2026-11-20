#ifndef GAME_CLIENT_COMPONENTS_STATBOARD_H
#define GAME_CLIENT_COMPONENTS_STATBOARD_H

#include <base/io.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

class CStatboardCsv
{
	std::string m_Content = "Servername,Game-type,Map\n";

	void AppendField(std::string_view Field)
	{
		for(char Byte : Field)
		{
			if(Byte == ',')
				m_Content.append("%2C");
			else
				m_Content.push_back(Byte);
		}
	}

public:
	struct SPlayer
	{
		bool m_Local = false;
		int m_Team = 0;
		std::string_view m_Name;
		std::string_view m_Clan;
		int m_Score = 0;
		int m_Frags = 0;
		int m_Deaths = 0;
		int m_Suicides = 0;
		float m_Fpm = 0.0f;
		int m_Spree = 0;
		int m_BestSpree = 0;
		std::span<const int, NUM_WEAPONS> m_FragsWith;
		std::span<const int, NUM_WEAPONS> m_DeathsFrom;
		bool m_GameWithFlags = false;
		int m_FlagGrabs = 0;
		int m_FlagCaptures = 0;
	};

	CStatboardCsv(std::string_view ServerName, std::string_view GameType, std::string_view Map)
	{
		AppendField(ServerName);
		m_Content.push_back(',');
		AppendField(GameType);
		m_Content.push_back(',');
		AppendField(Map);
		m_Content.append("\n\nLocal-player,Team,Name,Clan,Score,Frags,Deaths,Suicides,F/D-ratio,Net,FPM,Spree,Best,Hammer-F/D,Gun-F/D,Shotgun-F/D,Grenade-F/D,Laser-F/D,Ninja-F/D,GameWithFlags,Flag-grabs,Flag-captures\n");
	}

	void AddPlayer(const SPlayer &Player)
	{
		char aFields[256];
		str_format(aFields, sizeof(aFields), "%d,%d,", Player.m_Local ? 1 : 0, Player.m_Team);
		m_Content.append(aFields);
		AppendField(Player.m_Name);
		m_Content.push_back(',');
		AppendField(Player.m_Clan);
		const float Ratio = Player.m_Deaths != 0 ? static_cast<float>(Player.m_Frags) / Player.m_Deaths : 0.0f;
		str_format(aFields, sizeof(aFields), ",%d,%d,%d,%d,%.2f,%d,%.1f,%d,%d",
			Player.m_Score, Player.m_Frags, Player.m_Deaths, Player.m_Suicides, Ratio,
			Player.m_Frags - Player.m_Deaths, Player.m_Fpm, Player.m_Spree, Player.m_BestSpree);
		m_Content.append(aFields);
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			str_format(aFields, sizeof(aFields), ",%d/%d", Player.m_FragsWith[Weapon], Player.m_DeathsFrom[Weapon]);
			m_Content.append(aFields);
		}
		str_format(aFields, sizeof(aFields), ",%d,%d,%d\n", Player.m_GameWithFlags ? 1 : 0, Player.m_FlagGrabs, Player.m_FlagCaptures);
		m_Content.append(aFields);
	}

	bool Save(IStorage &Storage, const char *pPath) const noexcept
	try
	{
		if(!pPath || !pPath[0] || m_Content.size() > std::numeric_limits<unsigned>::max())
			return false;

		char aTemporary[IO_MAX_PATH_LENGTH];
		IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), pPath);
		const char *pSuffix = str_startswith(aTemporary, pPath);
		if(!pSuffix || !str_endswith(pSuffix, ".tmp"))
			return false;
		char aComplete[IO_MAX_PATH_LENGTH + 1] = "";
		Storage.GetCompletePath(IStorage::TYPE_SAVE, aTemporary, aComplete, sizeof(aComplete));
		if(str_length(aComplete) >= IO_MAX_PATH_LENGTH)
			return false;

		struct SCandidate
		{
			IStorage &m_Storage;
			const char *m_pPath;
			bool m_Owned = false;

			~SCandidate()
			{
				if(m_Owned)
				{
					try
					{
						m_Storage.RemoveFile(m_pPath, IStorage::TYPE_SAVE);
					}
					catch(...)
					{
					}
				}
			}
		} Candidate{Storage, aTemporary};

		std::unique_ptr<void, decltype(&io_close)> File(Storage.OpenFile(aTemporary, IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE), io_close);
		if(!File)
			return false;
		Candidate.m_Owned = true;
		const auto Size = static_cast<unsigned>(m_Content.size());
		if(io_write(File.get(), m_Content.data(), Size) != Size || io_sync(File.get()) != 0 || io_error(File.get()) != 0)
			return false;
		if(io_close(File.release()) != 0 || !Storage.RenameFile(aTemporary, pPath, IStorage::TYPE_SAVE, false))
			return false;
		Candidate.m_Owned = false;
		return true;
	}
	catch(...)
	{
		return false;
	}
};

class CStatboard : public CComponent
{
private:
	bool m_Active;
	bool m_ScreenshotTaken;
	int64_t m_ScreenshotTime;
	static void ConKeyStats(IConsole::IResult *pResult, void *pUserData);
	void RenderGlobalStats();
	void AutoStatScreenshot();
	void AutoStatCSV();

	CStatboardCsv FormatStats();

public:
	CStatboard();
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnConsoleInit() override;
	void OnRender() override;
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool IsActive() const;
};

#endif // GAME_CLIENT_COMPONENTS_STATBOARD_H
