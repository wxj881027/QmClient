#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGINTERNAL_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGINTERNAL_H

#include "QmCardCatalog.h"

#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/SettingsCard.h>

#include <cstdint>

namespace qm_card_catalog
{
	// 卡片模块文件共享的 definition 装配：标题/描述取自 QmCardRegistry（同一份事实源），
	// 折叠状态与折叠按钮由页面注入，卡片模块只提供自己的测量、重测版本、预布局输入与内容渲染。
	// Render 为 FSettingsCardRenderMeasured（可消费 CUIRect &Content），与各页面既有内容函数签名一致。
	void MakeModuleCard(
		const SQmCardBuildContext &Ctx,
		qm_module::EQmModuleId Id,
		const char *pStableId,
		const char *pTitle,
		const char *pSubtitle,
		const FSettingsCardRenderMeasured &Render,
		FSettingsCardMeasure Measure,
		uint64_t MeasureRevision,
		FSettingsCardPreLayoutInput PreLayoutInput,
		SSettingsCardDefinition &Out);

	// 皮肤外观与切换动画的独立全局卡片构造入口。
	bool BuildSkinCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);
	bool BuildWaterHammerCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);

	// 卡片模块共享的行高/行距推导（LineHeight + LineSpacing 的整数倍）。
	inline float CardRows(const SSettingsContentMetrics &Metrics, const float Count)
	{
		return Count * (Metrics.m_LineHeight + Metrics.m_LineSpacing);
	}

	inline float CardRow(const SSettingsContentMetrics &Metrics, const float Spacing = 1.0f)
	{
		return Metrics.m_LineHeight + Metrics.m_LineSpacing * Spacing;
	}

	// 灵动岛开关倒计时位置复选框的控件 id：渲染函数（menus_qmclient.cpp）与卡片预布局输入
	// 必须共用同一组 id，否则同一次点击会被两条路径各处理一次。权威定义在卡片模块文件里。
	const void *SwitchCountdownFollowTeeId();
	const void *SwitchCountdownMediaIslandId();
} // namespace qm_card_catalog

#endif // GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGINTERNAL_H
