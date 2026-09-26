#ifndef GAME_CLIENT_QMUI_QMCARDREGISTRY_H
#define GAME_CLIENT_QMUI_QMCARDREGISTRY_H

#include <game/client/QmUi/QmCardOrderModel.h>

#include <string>
#include <vector>

// 全局卡片组件注册表（单一事实源）。
// 每张卡的默认 Placement {stableId, tab, column, order}，是默认值、迁移兜底、新卡补位的唯一依据。
// tab 是可变位置维度（非卡片固有归属）；数据债卡（laser/nameplate_text）的 tab 在本表补齐。
// stableId 使用 qm:/tclient:/deck: 命名空间并保持全局唯一。
namespace qm_card_registry
{
	enum class ECardColumn
	{
		Left,
		Right,
		Full,
	};
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

	struct SCardDefault
	{
		const char *m_pStableId; // 全局唯一 stableId（qm:/tclient:/deck: 命名空间）
		const char *m_pDefaultTab; // 默认归属 tab（nullptr=横跨卡如 info，或暂无归属）
		ECardColumn m_DefaultColumn; // 默认列
		int m_DefaultOrder; // 列内默认序
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

	// 构建完整默认全局模型 entries（从注册表派生），供首次迁移和缺失卡补位复用。
	std::vector<qm_card_order::SEntry> BuildDefaultEntries();
	bool IsTClientMainCardsLegacyLeft(const qm_card_order::CModel &Model);
	bool MoveTClientMainCardsToAlternatingColumns(qm_card_order::CModel &Model);
	ETClientMainCardsMigrationResult MigrateTClientMainCardsToAlternatingColumns(qm_card_order::CModel &Model, char *pSerialized, int SerializedSize);

	// 从单一注册表构建本地化搜索视图，并以当前 model placement 返回导航 tab。
	std::vector<SCardSearchResult> SearchCards(const char *pQuery, const qm_card_order::CModel &Model);
} // namespace qm_card_registry

#endif // GAME_CLIENT_QMUI_QMCARDREGISTRY_H
