#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

qm_card_catalog::STClientCardResult CMenus::RunTClientChatBindsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CPerfTimer RenderTimer;
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	IUiContext TClientChatBindsTextInputCtx = SettingsUiContext("settings_tclient_chatbinds_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientChatBindsTextInputCtx.m_pAnim = nullptr;
		TClientChatBindsTextInputCtx.m_pTree = nullptr;
	}

	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindDefault &BindDefault) {
		CUIRect Button, Input, Title;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName = pOldBind == nullptr ? s_aTempName : pOldBind->m_aName;
		Button.VSplitLeft(210.0f, &Title, &Input);
		CUIElement &TitleElement = SettingsTextElement(SETTINGS_TCLIENT, TCLIENT_TAB_BINDCHAT, BindDefault.m_pTitle);
		DoSettingsLabelStreamed(TitleElement, &Title, Localize(BindDefault.m_pTitle), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Title.w));
		BindDefault.m_LineInput.SetBuffer(pName, BINDCHAT_MAX_NAME);
		BindDefault.m_LineInput.SetEmptyText(BindDefault.m_Bind.m_aName);
		if(!ReadOnly && ui_widget::InputField(TClientChatBindsTextInputCtx, &BindDefault.m_LineInput, Input, BindDefault.m_Bind.m_aName, EditBoxFontSize) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName);
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
				GameClient()->m_BindChat.RemoveBind(pName);
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDCHAT, BindDefault.m_Bind.m_aName, &Input, pName, FontSize, TEXTALIGN_ML);
	};

	constexpr const char *apStableIds[] = {
		"deck:tclient-chat-binds-kaomoji",
		"deck:tclient-chat-binds-warlist",
		"deck:tclient-chat-binds-other",
	};
	const auto MeasureCard = [](size_t Index, float) {
		return CBindChat::BIND_DEFAULTS[Index].second.size() * (MarginSmall + LineSize);
	};
	const auto RenderCard = [&](size_t Index, CUIRect &Content) {
		for(CBindChat::CBindDefault &BindDefault : CBindChat::BIND_DEFAULTS[Index].second)
			DoBindchatDefault(Content, BindDefault);
	};
	const uint64_t CardRevision = static_cast<uint64_t>(CBindChat::BIND_DEFAULTS.size());
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	for(size_t Index = 0; Index < std::min(CBindChat::BIND_DEFAULTS.size(), std::size(apStableIds)); ++Index)
	{
		if(str_comp(pStableId, apStableIds[Index]) != 0)
			continue;
		const float Height = MeasureCard(Index, Content.w);
		if(Pass == qm_card_catalog::ETClientCardPass::RENDER)
			RenderCard(Index, Content);
		return {Height, CardRevision};
	}
	return {0.0f, CardRevision};
}
