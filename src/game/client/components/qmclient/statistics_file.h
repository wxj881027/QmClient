#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_FILE_H

#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/storage.h>

enum class EQmStatisticsFileLoadResult
{
	NOT_FOUND,
	VALID,
	INVALID,
};

inline EQmStatisticsFileLoadResult QmLoadStatisticsFile(IStorage *pStorage, const char *pFilename, json_value **ppRoot)
{
	if(ppRoot)
		*ppRoot = nullptr;
	if(!pStorage || !pFilename || pFilename[0] == '\0' || !ppRoot)
		return EQmStatisticsFileLoadResult::INVALID;

	char *pJson = pStorage->ReadFileStr(pFilename, IStorage::TYPE_SAVE);
	if(!pJson)
		return EQmStatisticsFileLoadResult::NOT_FOUND;

	json_value *pRoot = JsonParse(pJson, str_length(pJson));
	free(pJson);
	if(!pRoot || pRoot->type != json_object)
	{
		if(pRoot)
			json_value_free(pRoot);
		return EQmStatisticsFileLoadResult::INVALID;
	}

	const json_value *pLocal = json_object_get(pRoot, "local");
	const json_value *pRemote = json_object_get(pRoot, "remote");
	if(pLocal->type != json_object || pRemote->type != json_object)
	{
		json_value_free(pRoot);
		return EQmStatisticsFileLoadResult::INVALID;
	}

	*ppRoot = pRoot;
	return EQmStatisticsFileLoadResult::VALID;
}

#endif
