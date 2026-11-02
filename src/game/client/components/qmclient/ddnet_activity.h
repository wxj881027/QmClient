#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DDNET_ACTIVITY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DDNET_ACTIVITY_H

#include <engine/shared/json.h>

#include <cstdint>
#include <limits>

// 官方 activity 按日给出整数小时；缺失或无法得到有效合计时返回 -1。
inline int64_t QmDdnetActivityHours(const json_value *pRoot)
{
	if(!pRoot || pRoot->type != json_object)
		return -1;
	const json_value *pActivity = json_object_get(pRoot, "activity");
	if(pActivity->type != json_array)
		return -1;

	int64_t TotalHours = 0;
	bool HasHours = pActivity->u.array.length == 0;
	for(unsigned i = 0; i < pActivity->u.array.length; ++i)
	{
		const json_value &Entry = (*pActivity)[i];
		if(Entry.type != json_object)
			continue;
		const json_value *pHours = json_object_get(&Entry, "hours_played");
		if(pHours->type != json_integer || pHours->u.integer < 0)
			continue;
		if(pHours->u.integer > std::numeric_limits<int64_t>::max() - TotalHours)
			return -1;
		TotalHours += pHours->u.integer;
		HasHours = true;
	}
	return HasHours ? TotalHours : -1;
}

#endif
