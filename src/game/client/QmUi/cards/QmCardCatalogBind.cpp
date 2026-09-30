#include "QmCardCatalogInternal.h"

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/localization.h>

namespace qm_card_catalog
{
	bool BuildBindCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr)
			return false;

		const bool ReadOnly = Ctx.m_ReadOnly;
		CMenus *pMenus = Ctx.m_pMenus;
		const FSettingsCardRenderMeasured Render = [pMenus, ReadOnly](CUIRect &Content) {
			QmCardRenderHook::RenderQmBindEditorContent(pMenus, Content, ReadOnly);
		};

		Out = {};
		Out.m_Spec = {"qm:bind_editor", Localize("Bind"), qm_card_registry::ResolveLocalizedDescription("qm:bind_editor")};
		Out.m_Measure = [pMenus, Render](const float ContentWidth) {
			return QmCardRenderHook::MeasureContent(pMenus, Render, ContentWidth);
		};
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = Render;
		// 绑定、选中键位和命令筛选都能独立变化，不能依赖注册表版本触发重测。
		Out.m_MeasureEachFrame = true;
		return true;
	}
} // namespace qm_card_catalog
