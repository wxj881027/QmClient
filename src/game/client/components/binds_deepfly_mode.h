// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_BINDS_DEEPFLY_MODE_H
#define GAME_CLIENT_COMPONENTS_BINDS_DEEPFLY_MODE_H

#include "qmclient/bind_editor.h"

#include <base/system.h>

#include <algorithm>
#include <cstddef>

// NOLINTNEXTLINE(misc-use-internal-linkage)
enum EDeepflyMode
{
	DEEPFLY_MODE_NONE = -1,
	DEEPFLY_MODE_NORMAL = 0,
	DEEPFLY_MODE_DF = 1,
	DEEPFLY_MODE_HDF = 2,
	DEEPFLY_MODE_CUSTOM = 3,
};

static void NormalizeBindCommand(const char *pCommand, char *pNormalized, size_t NormalizedSize, const char *pEnd = nullptr)
{
	if(NormalizedSize == 0)
		return;

	size_t OutLen = 0;
	bool PendingSpace = false;
	for(; pCommand != pEnd && *pCommand != '\0' && OutLen + 1 < NormalizedSize; ++pCommand)
	{
		if(str_isspace(*pCommand))
		{
			PendingSpace = OutLen > 0;
			continue;
		}

		if(PendingSpace)
		{
			if(OutLen + 2 >= NormalizedSize)
				break;
			pNormalized[OutLen++] = ' ';
			PendingSpace = false;
		}
		pNormalized[OutLen++] = *pCommand;
	}

	pNormalized[OutLen] = '\0';
}

template<typename F>
static void ForEachTopLevelBindCommand(const char *pCommand, F &&Fn)
{
	if(!pCommand)
		return;

	if(const char *pWithoutPrefix = str_startswith(pCommand, "mc;"))
		pCommand = pWithoutPrefix;

	const char *pStart = pCommand;
	bool InQuotes = false;
	const auto FlushCommand = [&](const char *pEnd) {
		char aNormalized[qm_bind_editor::MAX_CONFIG_LINE_BYTES + 1];
		const size_t Length = std::min(static_cast<size_t>(pEnd - pStart), sizeof(aNormalized) - 1);
		NormalizeBindCommand(pStart, aNormalized, sizeof(aNormalized), pStart + Length);
		if(aNormalized[0] != '\0')
			Fn(aNormalized);
	};

	for(;; ++pCommand)
	{
		const char c = *pCommand;
		if(c == '\0' || (!InQuotes && (c == ';' || c == '#')))
		{
			FlushCommand(pCommand);
			if(c != ';')
				break;
			pStart = pCommand + 1;
		}
		else if(c == '\\' && (pCommand[1] == '\\' || pCommand[1] == '"'))
		{
			++pCommand;
		}
		else if(c == '"')
		{
			InQuotes = !InQuotes;
		}
	}
}

bool IsDeepflyFireCommand(const char *pCommand);
bool IsDeepflyDummyHammerToggleCommand(const char *pCommand);
bool IsIndirectDeepflyScriptCommand(const char *pCommand);
bool IsDeepflyAuxiliaryCommand(const char *pCommand);
int DetectDeepflyModeFromBindCommand(const char *pCommand);

#endif
