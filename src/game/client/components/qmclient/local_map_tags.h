#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_MAP_TAGS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_MAP_TAGS_H

#include <string>
#include <utility>
#include <vector>

class IStorage;

namespace QmMapTags
{
	// 用户自己的地图标签：**只存在本地，不上传、别人看不到**。
	//
	// 与服务器返回的已审核标签是两回事：服务器那套要走审核、所有人可见；这份是
	// 玩家自己的记事本——进图给自己标记「这图我练过 / 难 / 喜欢」，随时可点上传
	// 把某个词提交给服务器审核。标签按社区 ID 和地图名归组。这两项相同的服务器
	// 共用一组标签，社区 ID 为空时也按地图名归组。
	//
	// 持久化到用户数据目录的 `qm_local_map_tags.txt`，行格式 `community\tmap\ttag`。
	// 颜色不存：标签颜色由 `ui_tag::RANDOM_FIXED` 按文本哈希派生，同一个词永远
	// 同一色，跨会话稳定，因此不需要额外存一份颜色表（也不会因为配置迁移变色）。
	class CLocalTags
	{
	public:
		// 首次尝试读盘。读取失败时在下一次编辑前重试。
		void Load(IStorage *pStorage);
		// 有存储时，编辑只有成功落盘才更新内存。空存储只操作内存。
		bool Add(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag);
		bool Remove(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag);
		// 调整同一张图上自己的标签顺序：把该图的第 FromIndex 个标签移到最终第 ToIndex 位。
		// 有存储时，重新加载仍按保存的顺序返回。
		bool Move(IStorage *pStorage, const char *pCommunity, const char *pMap, int FromIndex, int ToIndex);
		bool Has(const char *pCommunity, const char *pMap, const char *pTag) const;
		std::vector<std::string> Get(const char *pCommunity, const char *pMap) const;
		bool Loaded() const { return m_Loaded; }

	private:
		using TTagEntries = std::vector<std::pair<std::string, std::string>>;
		void LoadFromStorage(IStorage &Storage);
		static bool Save(IStorage &Storage, const TTagEntries &vEntries);
		template<typename TEdit>
		bool Edit(IStorage *pStorage, TEdit &&Apply);
		// 保留插入顺序（先加的排前面），所以用扁平数组而不是 map。
		TTagEntries m_vEntries; // {key, tag}
		bool m_LoadAttempted = false;
		bool m_Loaded = false;
	};
} // namespace QmMapTags

#endif
