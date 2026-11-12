#ifndef GAME_CLIENT_QMUI_QMCARDORDERMODEL_H
#define GAME_CLIENT_QMUI_QMCARDORDERMODEL_H

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// 全部设置卡片共享的唯一顺序与列归属数据源。
// 页面只通过 CSettingsCardDeck 投影和更新该模型，不再维护页面私有顺序。
namespace qm_card_order
{
	class CModel;

	struct SEntry
	{
		const char *m_pStableId = nullptr; // 稳定 id（栖梦用 m_pKey 如 "chat_bubble"，Tclient 用 stable id）
		const char *m_pDefaultTab = nullptr; // 该卡当前归属 tab（tab 是可变位置维度而非固有归属；运行时为注册表默认 tab 的快照）
		int m_Column = 0; // 列号（0=Full, 1=Left, 2=Right，或泛化）
		int m_OrderInColumn = 0; // 列内排序
	};

	enum class EExplicitLayoutStatus
	{
		MATCH,
		NOT_MATCH,
		INVALID,
	};

	// 保留既有全局配置中不匹配 pStableIdPrefix 的 token，并用 vReplacementEntries 替换该命名空间。
	bool SerializeMergedReplacingPrefix(const char *pExistingGlobalOrder, const char *pStableIdPrefix, const std::vector<SEntry> &vReplacementEntries, char *pOut, int OutSize);
	bool TabContainsOnlyStableIds(const CModel &Model, const char *pTab, const std::vector<const char *> &vAllowedStableIds);
	EExplicitLayoutStatus ClassifyExplicitLayout(const char *pSerialized, const std::vector<SEntry> &vExpectedLayout, const std::vector<const char *> &vRequiredStableIds);
	bool MigrateExactLayout(CModel &Model, const char *pTab, const std::vector<SEntry> &vExpectedLayout, const std::vector<SEntry> &vTargetLayout, const std::vector<const char *> &vAllowedStableIds);

	class CModel
	{
	public:
		void SetEntries(std::vector<SEntry> Entries);
		// 与完整候选交换内容而不分配；两个模型各自推进版本，保留条目与 tab 的所有权配对。
		void SwapContent(CModel &Other) noexcept;
		int Count() const { return (int)m_vEntries.size(); }
		const SEntry &Entry(int Index) const { return m_vEntries[Index]; }
		// 单调布局版本：布局改变或 entry 内容发布时递增，供渲染投影缓存失效。
		uint64_t LayoutRevision() const { return m_LayoutRevision; }
		// stableId 到 state index 的注册表版本。SetEntries/Parse 建表或 SwapContent 发布时递增，
		// 但 Move 只改变布局，不改变 state index，避免打断拖拽和布局动画。
		uint64_t StateIndexRevision() const { return m_StateIndexRevision; }

		int FindByStableId(const char *pStableId) const;

		// 核心操作：把 pStableId 移到 ToColumn 的 ToOrder 位置
		void Move(const char *pStableId, int ToColumn, int ToOrder);
		// 跨 tab/列移动：tab 是可变位置维度，组件编辑器跨页搬卡时使用。
		void MoveToTab(const char *pStableId, const char *pToTab, int ToColumn, int ToOrder);

		// order 连续化（消除空洞），按列 stable_sort
		void NormalizeColumns();

		// 按序返回某列的 entry index
		std::vector<int> ColumnIndices(int Column) const;

		// 按序返回某 tab 某 column 的 entry index（组件编辑器按"页是展示层"筛选：tab=页/画布，column=列）
		std::vector<int> ColumnIndices(const char *pTab, int Column) const;
		// 按 stableId 前缀 + tab + column 导出顺序，供页面运行时复用全局模型解析结果。
		std::vector<std::string> StableIdOrder(const char *pStableIdPrefix, const char *pTab, int Column) const;

		// 构建 stableId→连续 index 注册表（让位 lerp 保持 O(1) 查找的性能地基；SetEntries/Parse 后自动维护）
		void BuildStateIndex();
		// O(1) 查询 stableId 的 state index（未命中或 nullptr 返回 -1）
		int StateIndexForStableId(const char *pStableId) const;

		// 持久化：格式 "stableId|tab|column|order;"；兼容旧 "id:col:order" 解析。
		bool Serialize(char *pBuf, int BufSize) const;
		// 容错解析：未知/重复/非法 key 跳过（照搬 ParseQmModuleLayout）
		bool Parse(const char *pStr, const std::vector<const char *> &vValidIds, bool RejectDuplicateValidIds = false);
		// 仅加载用户显式配置：vDefaults 只提供合法 stableId 集合，不补齐缺失卡。
		bool LoadExplicit(const char *pStr, const std::vector<SEntry> &vDefaults);
		// 默认全集 + 用户配置覆盖：缺失卡保留默认，未知/非法残留跳过；返回是否至少解析到一条有效用户配置。
		bool LoadMerged(const char *pStr, const std::vector<SEntry> &vDefaults);

		bool IsDirty() const { return m_Dirty; }
		void ClearDirty() { m_Dirty = false; }

	private:
		std::vector<SEntry> m_vEntries;
		std::deque<std::string> m_vOwnedTabs;
		std::unordered_map<std::string, int> m_StableIdToState; // stableId→连续 index 注册表（让位 lerp O(1) 查找）
		uint64_t m_LayoutRevision = 0;
		uint64_t m_StateIndexRevision = 0;
		bool m_Dirty = false;
	};
} // namespace qm_card_order

#endif // GAME_CLIENT_QMUI_QMCARDORDERMODEL_H
