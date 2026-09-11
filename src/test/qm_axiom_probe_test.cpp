#include <engine/shared/json.h>

#include <game/client/components/qmclient/axiom_scores_data.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

TEST(QmAxiomProbe, DumpParseStages)
{
	const char *pJson = R"({"code":200,"data":{
		"player":{"player_name":"wolf_test","points":1234,"global_rank":56,"team_rank":null,
			"total_play_time":3600,"total_maps_completed":42,"performance_points":900,"mileage":1234},
		"difficultyData":{
			"Novice 简单":{"stats":{"points":100,"global_rank":7,"team_rank":null,"completed_maps":5,"remaining_maps":95,"total_points":4000,"total_maps":120}},
			"Race":{"stats":{"points":0,"global_rank":null,"team_rank":null,"completed_maps":0,"remaining_maps":50,"total_points":null,"total_maps":null}}}}})";

	json_value *pRoot = JsonParse(pJson, std::strlen(pJson));
	ASSERT_NE(pRoot, nullptr) << "root parse failed";
	std::printf("[probe] root type=%d\n", (int)pRoot->type);

	const json_value *pCode = json_object_get(pRoot, "code");
	std::printf("[probe] code type=%d int=%lld\n", (int)pCode->type, (long long)pCode->u.integer);

	const json_value *pData = json_object_get(pRoot, "data");
	std::printf("[probe] data type=%d\n", (int)pData->type);

	const json_value *pPlayer = json_object_get(pData, "player");
	std::printf("[probe] player type=%d\n", (int)pPlayer->type);

	const json_value *pDifficultyData = json_object_get(pData, "difficultyData");
	std::printf("[probe] difficultyData type=%d len=%u\n", (int)pDifficultyData->type, pDifficultyData->u.object.length);

	const char *aFields[] = {"player_name", "points", "global_rank", "team_rank", "total_play_time", "total_maps_completed", "performance_points", "mileage"};
	for(const char *pField : aFields)
	{
		const json_value *pValue = json_object_get(pPlayer, pField);
		std::printf("[probe] player.%s type=%d", pField, (int)pValue->type);
		if(pValue->type == json_integer)
			std::printf(" int=%lld", (long long)pValue->u.integer);
		std::printf("\n");
	}

	for(unsigned i = 0; i < pDifficultyData->u.object.length; ++i)
	{
		const char *pName = pDifficultyData->u.object.values[i].name;
		const json_value *pDiff = pDifficultyData->u.object.values[i].value;
		const json_value *pStats = json_object_get(pDiff, "stats");
		std::printf("[probe] diff[%u] name='%s' valueType=%d statsType=%d len=%u\n", i, pName ? pName : "(null)", (int)pDiff->type, (int)pStats->type, pDiff->type == json_object ? pDiff->u.object.length : 0u);
		if(pStats->type != json_object)
			continue;
		for(unsigned j = 0; j < pStats->u.object.length; ++j)
		{
			const json_value *pValue = pStats->u.object.values[j].value;
			std::printf("[probe]   stats.%s type=%d", pStats->u.object.values[j].name, (int)pValue->type);
			if(pValue->type == json_integer)
				std::printf(" int=%lld", (long long)pValue->u.integer);
			std::printf("\n");
		}
	}

	SQmAxiomModeScore Score;
	const EQmAxiomParseResult Result = QmParseAxiomInfoResponse(pJson, std::strlen(pJson), Score);
	std::printf("[probe] QmParseAxiomInfoResponse = %d\n", (int)Result);
	json_value_free(pRoot);
}
