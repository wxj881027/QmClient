#ifndef GAME_CLIENT_QMUI_QMCARDREGISTRY_H
#define GAME_CLIENT_QMUI_QMCARDREGISTRY_H

#include <game/client/QmUi/QmCardOrderModel.h>

#include <cstddef>
#include <string>
#include <vector>

// 全局卡片组件注册表（单一事实源）。
// 每张卡声明自己的 stableId、tab 归属、是否全宽，以及面向用户的标题/关键词/描述。
// 默认列与列内序**不再逐张手写**：BuildDefaultEntries() 按注册表顺序（即页面阅读序）
// 做「对称瀑布」分配——半宽卡进当前累计高度更矮的一列（等高放左列），全宽卡独占一行并
// 重置两列高度；没有登记检索表的卡片按等权兜底，绝不引入运行时测量依赖。
// tab 是可变位置维度（非卡片固有归属）；stableId 使用 qm:/tclient:/deck: 命名空间并保持全局唯一。
namespace qm_card_registry
{
	enum class ETClientMainCardsMigrationResult
	{
		NOT_LEGACY,
		PERSIST_FAILED,
		PERSISTED_DIRTY,
		MIGRATED,
	};
	struct STClientMainCardsMigrationCommitPlan
	{
		bool m_PersistSerialized = false;
		bool m_AdvanceVersion = false;
	};
	constexpr STClientMainCardsMigrationCommitPlan TClientMainCardsMigrationCommitPlan(const ETClientMainCardsMigrationResult Result)
	{
		switch(Result)
		{
		case ETClientMainCardsMigrationResult::NOT_LEGACY:
			return {false, true};
		case ETClientMainCardsMigrationResult::PERSIST_FAILED:
			return {false, false};
		case ETClientMainCardsMigrationResult::PERSISTED_DIRTY:
			return {true, true};
		case ETClientMainCardsMigrationResult::MIGRATED:
			return {true, true};
		}
		return {};
	}

	struct SCardSettingSearch
	{
		const char *m_pLabel;
		const char *m_pConfigName;
		const char *m_pAliases = nullptr;
		const char *m_pContext = "";
	};

	struct SCardDefault
	{
		const char *m_pStableId; // 全局唯一 stableId（qm:/tclient:/deck: 命名空间）
		const char *m_pDefaultTab; // 默认归属 tab（nullptr=横跨卡如 info，或暂无归属）
		// 是否横跨两列（独占一行）。半宽卡的左右列由默认瀑布分配，表里不写列号。
		bool m_FullWidth = false;
		const char *m_pTitle; // 面向用户的默认标题（Localize key）
		const char *m_pSearchKeywords; // 搜索补充词，英文小写为主，空格分隔
		const char *m_pDescription = nullptr; // 面向用户的描述（Localize key）
		// 仅影响搜索与导航，不参与 deck 布局和持久化。用于"注册表里有条目、
		// 功能由别的卡或旧设置页承载"的卡片：它们没有自己的 deck 渲染器，
		// 若按 m_pDefaultTab 跳转会落在空 tab 上。设置后搜索按此目标跳转。
		const char *m_pNavigationTab = nullptr; // 覆盖导航 tab（可为旧设置页 key）
		const char *m_pNavigationStableId = nullptr; // 实际承载该功能的卡（可为 nullptr）
	};

	struct SCardNavigationTarget
	{
		const char *m_pTab = nullptr;
		const char *m_pStableId = nullptr;
	};

	struct SCardSearchResult
	{
		const char *m_pStableId = nullptr;
		std::string m_Title;
		std::string m_Description;
		SCardNavigationTarget m_Target;
		// 命中的设置项文案（本地化标签优先，其次是配置名），最多保留前几条。
		std::vector<std::string> m_vMatches;
		// 命中项的原文键（标签键/配置名/本地化文案），供逐行定位匹配。
		std::vector<std::string> m_vMatchKeys;
	};

	// 焦点只消费本帧按下沿，不持有鼠标按下状态，外部释放或输入隔离不会卡住后续点击。
	class CCardFocus
	{
		std::string m_StableId;
		bool m_Pressed = false;
		bool m_Interactive = false;
		bool m_Observed = false;

	public:
		void Reset() { *this = {}; }
		void BeginFrame(bool Pressed, bool Interactive)
		{
			m_Interactive = Interactive;
			m_Pressed = Pressed && Interactive;
			m_Observed = false;
			if(m_Pressed)
				m_StableId.clear();
		}
		void Observe(const char *pStableId, bool PointerInside)
		{
			if(!m_Interactive || pStableId == nullptr || pStableId[0] == '\0')
				return;
			if(m_Pressed && PointerInside)
				m_StableId = pStableId;
			if(m_StableId == pStableId)
				m_Observed = true;
		}
		bool IsFocused(const char *pStableId) const
		{
			return pStableId != nullptr && !m_StableId.empty() && m_StableId == pStableId;
		}
		void EndFrame()
		{
			if(m_Interactive && !m_Observed)
				m_StableId.clear();
		}
	};

	// 全局卡片默认 Placement 表（栖梦 + Tclient + deck）
	const std::vector<SCardDefault> &Defaults();

	// 按 stableId 查默认 Placement（未命中或 nullptr 返回 nullptr）
	const SCardDefault *FindByStableId(const char *pStableId);
	const char *ResolveDescriptionKey(const SCardDefault &Default);
	const char *ResolveLocalizedDescription(const SCardDefault &Default);
	const char *ResolveLocalizedDescription(const char *pStableId);
	SCardNavigationTarget ResolveCardNavigationTarget(const SCardDefault &Default, const qm_card_order::CModel &Model);

	// 栖梦旧 key（持久化 key）→ stableId 迁移映射（从注册表派生，DRY）。
	// 命中返回 "qm:<key>"，未命中或 nullptr 返回 nullptr。
	// UI 名（如 QiaFen 的 keyword_reply）不在注册表故不映射——以持久化 key 为权威，避免迁移丢用户布局。
	const char *MigrateLegacyKey(const char *pLegacyKey);

	// 默认瀑布布局使用的静态高度权重：卡片头 + 由检索表条目数估计的设置行数。
	// 0 表示该条目没有自己的 deck 渲染器（功能由别的卡承载），不占列高。
	// 它只服务默认布局的对称性，运行时布局不做任何界面测量。
	int DefaultCardHeightWeight(const char *pStableId);

	// 构建完整默认全局模型 entries（注册表顺序 + 瀑布列分配），供首次迁移和缺失卡补位复用。
	std::vector<qm_card_order::SEntry> BuildDefaultEntries();
	// 单卡默认 placement（与 BuildDefaultEntries 同一次计算结果）；未命中返回 nullptr。
	const qm_card_order::SEntry *FindDefaultPlacement(const char *pStableId);
	// 回退现代分类后恢复卡片归属，保留列位置和源分类内的相对顺序。
	bool RepairRemovedModernTabs(qm_card_order::CModel &Model);
	// Bind 编辑器从栖梦页迁入控制页后，把旧持久化 tab 归属迁到 controls。
	bool RepairLegacyBindTab(qm_card_order::CModel &Model);
	// 旧贡献者页已删除，加载时先修复归属，避免其它布局迁移提前返回后留下不可见卡片。
	bool RepairLegacyCreditsTabs(qm_card_order::CModel &Model);
	bool RepairLegacyTitleLayout(qm_card_order::CModel &Model);
	// 图像页分组调整：图标与界面动效两张卡搬到「视觉效果」子页签后，
	// 把持久化顺序里仍记着旧 tab 的这两张搬回注册表默认归属（追加到该页首列末尾）。
	bool RepairLegacyGraphicsVisualCards(qm_card_order::CModel &Model);
	bool RepairLegacyNameplateTitleLayout(qm_card_order::CModel &Model);
	bool RepairLegacyTeeLayout(qm_card_order::CModel &Model);
	// 聊天气泡已移入游戏界面的聊天页，恢复仍保存为视觉页的卡片。
	bool RepairLegacyChatBubbleTab(qm_card_order::CModel &Model);
	// DDRace HUD Pro 已移入游戏界面的 HUD 页，恢复仍保存在栖梦 HUD 页的卡片。
	bool RepairLegacyBindStatusHudTab(qm_card_order::CModel &Model);
	// 三张预测卡已从 TClient 页迁到 DDNet 页的「网络补偿」子页签（tab=ddnet-prediction），
	// 把旧存档里仍记在 tclient 下的位置恢复到默认归属。
	bool RepairLegacyPredictionCardTabs(qm_card_order::CModel &Model);
	bool IsTClientMainCardsLegacyLeft(const qm_card_order::CModel &Model);
	bool MoveTClientMainCardsToAlternatingColumns(qm_card_order::CModel &Model);
	ETClientMainCardsMigrationResult MigrateTClientMainCardsToAlternatingColumns(qm_card_order::CModel &Model, char *pSerialized, int SerializedSize);

	// 从单一注册表构建本地化搜索视图，并以当前 model placement 返回导航 tab。
	std::vector<SCardSearchResult> SearchCards(const char *pQuery, const qm_card_order::CModel &Model);
} // namespace qm_card_registry

#endif // GAME_CLIENT_QMUI_QMCARDREGISTRY_H
