#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_MAP_TAGS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_MAP_TAGS_H

#include <string>
#include <vector>

class IStorage;

namespace QmMapTags
{
	// 用户自己的地图标签：**只存在本地，不上传、别人看不到**。
	//
	// 与服务器返回的已审核标签是两回事：服务器那套要走审核、所有人可见；这份是
	// 玩家自己的记事本——进图给自己标记「这图我练过 / 难 / 喜欢」，随时可点上传
	// 把某个词提交给服务器审核。键是「社区 id + 地图名」，换图/换服互不串。
	//
	// 持久化到用户数据目录的 `qm_local_map_tags.txt`，行格式 `community\tmap\ttag`。
	// 颜色不存：标签颜色由 `ui_tag::RANDOM_FIXED` 按文本哈希派生，同一个词永远
	// 同一色，跨会话稳定，因此不需要额外存一份颜色表（也不会因为配置迁移变色）。
	class CLocalTags
	{
	public:
		// 只在首次调用时读盘；之后都在内存里，改动立即落盘。
		void Load(IStorage *pStorage);
		// 添加一个标签；已存在或非法返回 false。
		bool Add(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag);
		bool Remove(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag);
		// 调整同一张图上自己的标签顺序：把该图的第 FromIndex 个标签移到最终第 ToIndex 位。
		// 顺序是玩家自己排的（拖动排序），必须落盘，否则下次进图又回到插入顺序。
		bool Move(IStorage *pStorage, const char *pCommunity, const char *pMap, int FromIndex, int ToIndex);
		bool Has(const char *pCommunity, const char *pMap, const char *pTag) const;
		std::vector<std::string> Get(const char *pCommunity, const char *pMap) const;
		bool Loaded() const { return m_Loaded; }

	private:
		static std::string MakeKey(const char *pCommunity, const char *pMap);
		bool Save(IStorage *pStorage);
		// 保留插入顺序（先加的排前面），所以用扁平数组而不是 map。
		std::vector<std::pair<std::string, std::string>> m_vEntries; // {key, tag}
		bool m_Loaded = false;
	};
} // namespace QmMapTags

#endif
