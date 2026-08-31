// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "binds_deepfly_mode.h"

static bool IsCommandWithName(const char *pCommand, const char *pName)
{
	const char *pRest = str_startswith_nocase(pCommand, pName);
	return pRest != nullptr && (*pRest == '\0' || str_isspace(*pRest));
}

bool IsDeepflyFireCommand(const char *pCommand)
{
	return str_comp_nocase(pCommand, "+fire") == 0;
}

bool IsDeepflyDummyHammerToggleCommand(const char *pCommand)
{
	return str_comp_nocase(pCommand, "+toggle cl_dummy_hammer 1 0") == 0;
}

bool IsIndirectDeepflyScriptCommand(const char *pCommand)
{
	// These commands only switch the actual fire bind indirectly, e.g.
	// `bind mouse4 "bind mouse1 \"+fire\""` or `bind t "exec cfg\\clean.cfg"`.
	// The HUD mode is refreshed again when those nested bind commands run, so
	// the wrapper script itself should not count as a deepfly custom bind.
	return IsCommandWithName(pCommand, "bind") ||
	       IsCommandWithName(pCommand, "unbind") ||
	       str_comp_nocase(pCommand, "unbindall") == 0 ||
	       IsCommandWithName(pCommand, "exec");
}

bool IsDeepflyAuxiliaryCommand(const char *pCommand)
{
	if(str_comp_nocase(pCommand, "+weapon1") == 0)
		return true;
	if(str_comp_nocase(pCommand, "dummy_reset") == 0)
		return true;
	if(IsCommandWithName(pCommand, "echo"))
		return true;
	if(str_comp_nocase(pCommand, "+showhookcoll") == 0)
		return true;
	// 表情类命令与深飞判定无关：发射键上常附加 emote/emote_cycle，
	// 不应让这些多命令里的无关部分把 DF/HDF bind 误判为 Custom。
	if(str_comp_nocase(pCommand, "+emote") == 0)
		return true;
	if(IsCommandWithName(pCommand, "emote"))
		return true;
	if(IsCommandWithName(pCommand, "emote_cycle"))
		return true;
	return false;
}

int DetectDeepflyModeFromBindCommand(const char *pCommand)
{
	if(!pCommand || pCommand[0] == '\0')
		return DEEPFLY_MODE_NONE;

	bool HasFire = false;
	bool HasDummyHammerToggle = false;
	bool HasOtherCommand = false;
	bool HasIndirectScriptCommand = false;

	ForEachTopLevelBindCommand(pCommand, [&](const char *pNormalizedCommand) {
		if(IsDeepflyFireCommand(pNormalizedCommand))
			HasFire = true;
		else if(IsDeepflyDummyHammerToggleCommand(pNormalizedCommand))
			HasDummyHammerToggle = true;
		else if(IsIndirectDeepflyScriptCommand(pNormalizedCommand))
			HasIndirectScriptCommand = true;
		else if(!IsDeepflyAuxiliaryCommand(pNormalizedCommand))
			HasOtherCommand = true;
	});

	if(!HasFire && !HasDummyHammerToggle)
		return DEEPFLY_MODE_NONE;
	if(HasOtherCommand || HasIndirectScriptCommand)
		return DEEPFLY_MODE_CUSTOM;
	if(HasFire && HasDummyHammerToggle)
		return DEEPFLY_MODE_DF;
	if(HasDummyHammerToggle)
		return DEEPFLY_MODE_HDF;
	return DEEPFLY_MODE_NORMAL;
}
