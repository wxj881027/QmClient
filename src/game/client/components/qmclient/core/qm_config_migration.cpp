/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_config_migration.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>

const std::array<CQmConfigMigration::SRule, CQmConfigMigration::RULE_COUNT> &CQmConfigMigration::Rules()
{
	static constexpr std::array<SRule, RULE_COUNT> s_aRules = {{
		{"tc_player_indicator", "qm_player_indicator", true, 0, 1},
		{"tc_indicator_inteam", "qm_player_indicator_team_only", true, 0, 1},
		{"tc_player_indicator_freeze", "qm_player_indicator_frozen_only", true, 0, 1},
		{"tc_indicator_hide_visible_tees", "qm_player_indicator_hide_visible", true, 0, 1},
		{"tc_indicator_variable_distance", "qm_player_indicator_variable_distance", true, 0, 1},
		{"tc_indicator_offset", "qm_player_indicator_offset", true, 16, 200},
		{"tc_indicator_offset_max", "qm_player_indicator_offset_max", true, 16, 200},
		{"tc_indicator_variable_max_distance", "qm_player_indicator_max_distance", true, 500, 7000},
		{"tc_indicator_radius", "qm_player_indicator_radius", true, 1, 16},
		{"tc_indicator_opacity", "qm_player_indicator_opacity", true, 0, 100},
		{"tc_indicator_tees", "qm_player_indicator_use_tees", true, 0, 1},
		{"tc_indicator_alive", "qm_player_indicator_alive_color", false, 0, 0},
		{"tc_indicator_freeze", "qm_player_indicator_frozen_color", false, 0, 0},
		{"tc_indicator_dead", "qm_player_indicator_unfreezing_color", false, 0, 0},
	}};
	return s_aRules;
}

bool CQmConfigMigration::ShouldApply(const bool CurrentCommandSeen)
{
	return !CurrentCommandSeen;
}

const char *CQmConfigMigration::SkipSpaces(const char *pString)
{
	while(*pString == ' ' || *pString == '\t')
		++pString;
	return pString;
}

std::optional<std::string> CQmConfigMigration::ExtractCommandSegment(const char *pCommand)
{
	if(!pCommand)
		return std::nullopt;

	const char *pEnd = pCommand;
	bool InString = false;
	bool IsEscaping = false;
	while(*pEnd)
	{
		if(IsEscaping)
			IsEscaping = false;
		else if(*pEnd == '"')
			InString = !InString;
		else if(InString && *pEnd == '\\')
			IsEscaping = true;

		if(!InString && (*pEnd == ';' || *pEnd == '#'))
			break;
		if(static_cast<size_t>(pEnd - pCommand) >= IConsole::CMDLINE_LENGTH)
			return std::nullopt;
		++pEnd;
	}

	return std::string(pCommand, pEnd - pCommand);
}

const CQmConfigMigration::SRule *CQmConfigMigration::FindLegacyRule(const char *pCommand, size_t *pRuleIndex)
{
	if(!pCommand)
		return nullptr;

	pCommand = SkipSpaces(pCommand);
	const auto &aRules = Rules();
	for(size_t Index = 0; Index < aRules.size(); ++Index)
	{
		const SRule &Rule = aRules[Index];
		const size_t NameLength = str_length(Rule.m_pLegacyName);
		if(str_comp_nocase_num(pCommand, Rule.m_pLegacyName, NameLength) == 0 &&
			(pCommand[NameLength] == '\0' || pCommand[NameLength] == ' ' || pCommand[NameLength] == '\t' || pCommand[NameLength] == ';' || pCommand[NameLength] == '#'))
		{
			if(pRuleIndex)
				*pRuleIndex = Index;
			return &Rule;
		}
	}
	return nullptr;
}

std::optional<std::string> CQmConfigMigration::TranslateCommand(const char *pCommand, const SRule &Rule)
{
	const char *pTrimmedCommand = SkipSpaces(pCommand);
	const char *pArguments = pTrimmedCommand + str_length(Rule.m_pLegacyName);
	std::string Translated = Rule.m_pCurrentName;
	Translated.append(pArguments);
	if(Translated.empty() || Translated.size() >= IConsole::CMDLINE_LENGTH)
		return std::nullopt;
	return Translated;
}

void CQmConfigMigration::OnConsoleInit(IConsole *pConsole)
{
	if(!pConsole || m_pConsole)
		return;

	m_pConsole = pConsole;
	m_Loading = true;
	const auto &aRules = Rules();
	for(size_t Index = 0; Index < aRules.size(); ++Index)
	{
		m_aChainContexts[Index] = {this, Index};
		pConsole->Chain(aRules[Index].m_pCurrentName, OnCurrentCommand, &m_aChainContexts[Index]);
	}
}

bool CQmConfigMigration::PreserveCommand(const char *pCommand, IConfigManager *pConfigManager, const char *pReason)
{
	m_HasUnmigratedCommands = true;
	log_warn("qm/config", "could not migrate legacy config: %s", pReason);
	if(!pConfigManager)
		return false;

	const std::optional<std::string> Segment = ExtractCommandSegment(pCommand);
	pConfigManager->StoreUnknownCommand(Segment ? Segment->c_str() : pCommand);
	return true;
}

bool CQmConfigMigration::OnUnknownCommand(const char *pCommand, IConfigManager *pConfigManager)
{
	if(!m_Loading || g_Config.m_QmConfigMigrationVersion >= CURRENT_VERSION)
		return false;

	size_t RuleIndex = 0;
	if(!FindLegacyRule(pCommand, &RuleIndex))
		return false;

	const SRule &Rule = Rules()[RuleIndex];
	const std::optional<std::string> Segment = ExtractCommandSegment(pCommand);
	if(!Segment)
		return PreserveCommand(pCommand, pConfigManager, "command is too long");

	if(m_vCapturedCommands.size() >= MAX_CAPTURED_COMMANDS || m_CapturedCommandBytes + Segment->size() > MAX_CAPTURED_BYTES)
		return PreserveCommand(Segment->c_str(), pConfigManager, "capture limit reached");

	m_CapturedCommandBytes += Segment->size();
	m_vCapturedCommands.push_back({RuleIndex, *Segment});
	return true;
}

void CQmConfigMigration::OnCurrentCommand(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	SChainContext *pContext = static_cast<SChainContext *>(pUserData);
	if(pContext->m_pOwner->m_Loading)
		pContext->m_pOwner->m_aCurrentCommandsSeen[pContext->m_RuleIndex] = true;
	if(pfnCallback)
		pfnCallback(pResult, pCallbackUserData);
}

void CQmConfigMigration::OnConfigLoaded(IConsole *pConsole, IConfigManager *pConfigManager)
{
	if(!m_Loading)
		return;
	m_Loading = false;
	if(!pConsole)
		return;
	if(g_Config.m_QmConfigMigrationVersion >= CURRENT_VERSION)
	{
		m_vCapturedCommands.clear();
		m_CapturedCommandBytes = 0;
		return;
	}

	const auto &aRules = Rules();
	for(const SCapturedCommand &Captured : m_vCapturedCommands)
	{
		const SRule &Rule = aRules[Captured.m_RuleIndex];
		if(!ShouldApply(m_aCurrentCommandsSeen[Captured.m_RuleIndex]))
		{
			log_info("qm/config", "skipped legacy config '%s' because current key '%s' was present", Rule.m_pLegacyName, Rule.m_pCurrentName);
			continue;
		}

		const std::optional<std::string> Translated = TranslateCommand(Captured.m_Command.c_str(), Rule);
		if(!Translated || !m_pConsole || !m_pConsole->LineIsValid(Translated->c_str()))
		{
			PreserveCommand(Captured.m_Command.c_str(), pConfigManager, "value is invalid");
			continue;
		}
		pConsole->ExecuteLine(Translated->c_str(), IConsole::CLIENT_ID_UNSPECIFIED, false);
		log_info("qm/config", "migrated legacy config '%s' to '%s'", Rule.m_pLegacyName, Rule.m_pCurrentName);
	}

	m_vCapturedCommands.clear();
	m_CapturedCommandBytes = 0;
	if(!m_HasUnmigratedCommands)
		g_Config.m_QmConfigMigrationVersion = CURRENT_VERSION;
}
