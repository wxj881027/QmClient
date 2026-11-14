#include "MapTagsPanel.h"

#include "QmServerInfoGeometry.h"
#include "UiButtons.h"
#include "UiForms.h"
#include "UiSurfaceText.h"

#include <base/color.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/map_tags.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

void CQmMapTagsPanel::Render(CGameClient *pGameClient, CUIRect View)
{
	CUi *pUi = pGameClient->Ui();
	const auto &Server = pGameClient->Client()->ServerInfo();
	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	if(LocalId < 0 || pGameClient->Client()->State() != IClient::STATE_ONLINE)
		return;
	const char *pPlayer = pGameClient->m_aClients[LocalId].m_aName;
	const std::string Context = std::string(Server.m_aCommunityId) + "\n" + Server.m_aMap + "\n" + pPlayer;
	if(Context != m_Context)
	{
		m_Context = Context;
		m_Input.Clear();
		m_TagButtons.clear();
	}
	auto &Tags = pGameClient->m_QmClient.m_MapTags;
	Tags.SetContext(Server.m_aCommunityId, Server.m_aMap, pPlayer);
	Tags.Update(pGameClient->Http(), time_get() / (double)time_freq());
	const auto &Snapshot = Tags.Snapshot();
	const IUiContext Ctx = ui_widget::ControlContext(pUi);
	const float Scale = QmServerInfoGeometry::FitScale(View, 280.0f, 160.0f);
	if(Scale <= 0.0f)
		return;
	pUi->ClipEnable(&View);
	SLabelProperties LabelProps;
	LabelProps.m_MinimumFontSize = 0.1f;
	// 此面板的控件随可用空间缩放，按钮文字与命中区域使用相同的尺寸。
	auto DoButton = [&](CButtonContainer *pButton, const char *pText, CUIRect Rect, bool Disabled, bool Primary = false) {
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Role = Primary ? EUiButtonRole::PRIMARY : EUiButtonRole::SECONDARY;
		Options.m_Enabled = !Disabled;
		// 胶囊端头：圆角随按钮高度走，缩放后仍是完整胶囊。
		Options.m_Radius = Rect.h * 0.5f;
		const ColorRGBA Fill = ui_widget::DrawButtonSurface(Ctx, pButton, Rect, Options);
		CUiScopedSurfaceText SurfaceText(pGameClient->TextRender(), Fill);
		if(Disabled)
			pGameClient->TextRender()->TextColor(pGameClient->TextRender()->GetTextColor().WithMultipliedAlpha(0.65f));
		pUi->DoLabel(&Rect, pText, ui_token::font::BODY, TEXTALIGN_MC, LabelProps);
		if(Disabled && !pUi->RenderOnly() && pUi->CheckActiveItem(pButton))
			pUi->SetActiveItem(nullptr);
		const bool Clicked = !Disabled && !pUi->RenderOnly() && pUi->DoButtonLogic(pButton, 0, &Rect, BUTTONFLAG_LEFT) != 0;
		if(Clicked)
			pUi->SetKeyboardFocus(pButton);
		return Clicked;
	};
	CUIRect Header, RefreshSlot, Refresh;
	View.HSplitTop(ui_token::server_info::TAGS_HEADER_HEIGHT * Scale, &Header, &View);
	// 刷新改圆形图标按钮：图标按钮在别处也是圆形，文字按钮只留给主要动作。
	Header.VSplitRight(ui_token::server_info::TAGS_HEADER_HEIGHT * Scale, &Header, &RefreshSlot);
	RefreshSlot.VMargin((RefreshSlot.h - ui_token::server_info::TAGS_ICON_SIZE * Scale) * 0.5f, &Refresh);
	Refresh.h = ui_token::server_info::TAGS_ICON_SIZE * Scale;
	pUi->DoLabel(&Header, Localize("Map tags"), ui_token::font::TITLE, TEXTALIGN_ML, LabelProps);
	if(pUi->DoButton_QmIcon(&m_RefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0, &Refresh, BUTTONFLAG_LEFT))
		Tags.Refresh();
	View.HSplitTop(ui_token::server_info::TAGS_HEADER_GAP * Scale, nullptr, &View);
	CUIRect List, Footer;
	View.HSplitBottom(ui_token::server_info::TAGS_FOOTER_HEIGHT * Scale, &List, &Footer);
	List.HSplitBottom(ui_token::server_info::TAGS_LIST_GAP * Scale, &List, nullptr);
	float MaxTagWidth = 1.0f;
	for(const auto &Tag : Snapshot.m_vTags)
	{
		char aLabel[128];
		str_format(aLabel, sizeof(aLabel), "%s ×%d%s", Tag.m_Text.c_str(), Tag.m_Count, Tag.m_Voted ? "" : "  +1");
		MaxTagWidth = std::max(MaxTagWidth, pGameClient->TextRender()->TextWidth(ui_token::font::BODY, aLabel) + ui_token::spacing::MD);
	}
	const auto Grid = QmServerInfoGeometry::TagGrid(List, (int)Snapshot.m_vTags.size(), MaxTagWidth, Scale);
	int Index = 0;
	for(const auto &Tag : Snapshot.m_vTags)
	{
		char aLabel[128];
		str_format(aLabel, sizeof(aLabel), "%s ×%d%s", Tag.m_Text.c_str(), Tag.m_Count, Tag.m_Voted ? "" : "  +1");
		const CUIRect Capsule = QmServerInfoGeometry::TagRect(List, Grid, Index++);
		auto &Button = m_TagButtons[Tag.m_Id];
		const bool Enabled = Snapshot.m_Eligible && !Tag.m_Voted && !Tags.Busy();
		if(Enabled && pUi->DoButtonLogic(&Button, 0, &Capsule, BUTTONFLAG_LEFT))
			Tags.Submit(pGameClient->Http(), Tag.m_Text.c_str());
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Color = color_cast<ColorRGBA>(ColorHSLA(QmMapTags::ColorHue(Tag.m_Color), 0.55f, 0.43f, 0.9f));
		Options.m_Enabled = Enabled;
		Options.m_Selected = Tag.m_Voted;
		// 标签是胶囊：圆角取自身高度一半，不再用固定的 13。
		Options.m_Radius = Capsule.h * 0.5f;
		ui_widget::DrawButtonSurface(Ctx, &Button, Capsule, Options);
		SLabelProperties Properties;
		Properties.m_MinimumFontSize = 0.1f;
		pUi->DoLabel(&Capsule, aLabel, ui_token::font::BODY, TEXTALIGN_MC, Properties);
	}
	if(Snapshot.m_vTags.empty())
	{
		pUi->DoLabel(&List, Tags.Ready() ? Localize("No approved tags yet.") : Localize("Loading map tags…"), ui_token::font::BODY, TEXTALIGN_TL, LabelProps);
	}
	CUIRect PresetRow, PresetLabel, Presets;
	Footer.HSplitTop(ui_token::server_info::TAGS_PRESET_ROW_HEIGHT * Scale, &PresetRow, &Footer);
	PresetRow.VSplitLeft(ui_token::server_info::TAGS_PRESET_LABEL_WIDTH * Scale, &PresetLabel, &Presets);
	// 这四个是服务端内置的常用标签建议，不是「当前地图已有的标签」，
	// 不给说明文字玩家会以为它们是已审核标签。
	pUi->DoLabel(&PresetLabel, Localize("Suggested tags:"), ui_token::font::SMALL, TEXTALIGN_ML, LabelProps);
	static constexpr const char *PRESETS[] = {"hj", "fly", "Gores", "Hook"};
	for(size_t Index = 0; Index < std::size(PRESETS); ++Index)
	{
		CUIRect Button;
		Presets.VSplitLeft(std::min(60.0f * Scale, Presets.w / (std::size(PRESETS) - Index)), &Button, &Presets);
		Button.VMargin(2.0f * Scale, &Button);
		if(DoButton(&m_aPresetButtons[Index], PRESETS[Index], Button, !Snapshot.m_Eligible || Tags.Busy()))
			m_Input.Set(PRESETS[Index]);
	}
	Footer.HSplitTop(5.0f * Scale, nullptr, &Footer);
	CUIRect Composer, Input, Submit;
	Footer.HSplitTop(22.0f * Scale, &Composer, &Footer);
	Composer.VSplitRight(70.0f * Scale, &Input, &Submit);
	Input.VSplitRight(5.0f * Scale, &Input, nullptr);
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_pPlaceholder = Localize("New tag (up to 10 characters)");
	InputOptions.m_FontSize = ui_token::font::BODY * Scale;
	ui_widget::InputField(Ctx, &m_Input, Input, InputOptions);
	if(DoButton(&m_SubmitButton, Localize("Submit"), Submit, !Snapshot.m_Eligible || Tags.Busy() || !QmMapTags::ValidTag(m_Input.GetString()), true))
		Tags.Submit(pGameClient->Http(), m_Input.GetString());
	Footer.HSplitTop(5.0f * Scale, nullptr, &Footer);
	const char *pStatus = Tags.Status();
	if(!pStatus[0])
	{
		if(!Tags.Ready())
			pStatus = "";
		else if(Snapshot.m_Eligible)
			pStatus = Localize("New tags require review. Click a tag to support it.");
		else if(Snapshot.m_EligibilityUnavailable)
			pStatus = Localize("Completion records unavailable. Try refreshing.");
		else
			pStatus = Localize("Finish this map before submitting tags.");
	}
	pUi->DoLabel(&Footer, Localize(pStatus), ui_token::font::SMALL, TEXTALIGN_TL, LabelProps);
	pUi->ClipDisable();
}
