#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_WARLIST_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_WARLIST_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/warlist_model.h>

#include <vector>

class CWarList : public CComponent, public CQmWarListModel
{
	// The war list will have these commands
	//
	// Preset Commands
	// .war [name] [reason]
	// .clanwar [name] [reason]
	// .team [name] [reason]
	// .clanteam [name] [reason]
	// .delwar [name]
	// .delteam [name]
	// .delclanwar [name]
	// .delclanteam [team]
	//
	// Generic Commands
	// .name [type] [name] [reason]
	// .clan [type] [clan] [reason]
	// .delname [type] [name]
	// .delclan [type] [clan]
	//
	// Backend commands
	// update_war_group [index] [type] [color]
	// add_war_entry [type] [name] [clan] [reason]

	// Preset war Commands
	static void ConNameIndex(IConsole::IResult *pResult, void *pUserData);
	static void ConClanIndex(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveNameIndex(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveClanIndex(IConsole::IResult *pResult, void *pUserData);

	static void ConNameTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConClanTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveNameWar(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveNameTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveClanWar(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveClanTeam(IConsole::IResult *pResult, void *pUserData);

	// Generic Commands
	static void ConName(IConsole::IResult *pResult, void *pUserData);
	static void ConClan(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveName(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveClan(IConsole::IResult *pResult, void *pUserData);

	// Backend Commands for config file
	static void ConAddWarEntry(IConsole::IResult *pResult, void *pUserData);
	static void ConUpsertWarType(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

public:
	CWarDataCache m_WarPlayers[MAX_CLIENTS];

	int Sizeof() const override { return sizeof(*this); }
	void OnNewSnapshot() override;
	void OnConsoleInit() override;

	void UpdateWarPlayers();

	void AddWarEntryInGame(int WarType, const char *pName, const char *pReason, bool IsClan);
	void RemoveWarEntryInGame(int WarType, const char *pName, bool IsClan);

	ColorRGBA GetPriorityColor(int ClientId);
	ColorRGBA GetNameplateColor(int ClientId);
	ColorRGBA GetClanColor(int ClientId);
	bool GetAnyWar(int ClientId);
	bool GetNameWar(int ClientId);
	bool GetClanWar(int ClientId);
	bool IsEnemy(int ClientId) const;

	void GetReason(char *pReason, int ClientId);
	CWarDataCache &GetWarData(int ClientId);

	void SortWarEntries();
};

#endif
