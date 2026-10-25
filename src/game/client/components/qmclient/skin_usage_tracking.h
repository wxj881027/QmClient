#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_USAGE_TRACKING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_USAGE_TRACKING_H

#include <optional>

// 皮肤名由容器持有且不变；已有使用节点直接移到队首，不重新分配或失效其引用。
template<typename TList>
void QmTouchSkinUsageEntry(TList &Usage, std::optional<typename TList::iterator> &Entry,
	TList &Background, std::optional<typename TList::iterator> &BackgroundEntry, const char *pName)
{
	if(Entry.has_value())
		Usage.splice(Usage.begin(), Usage, Entry.value());
	else
	{
		// 只有新节点成功后才能撤销旧后台身份；分配异常保留原记录。
		Usage.emplace_front(pName);
		Entry = Usage.begin();
	}
	if(BackgroundEntry.has_value())
	{
		Background.erase(BackgroundEntry.value());
		BackgroundEntry.reset();
	}
}

#endif
