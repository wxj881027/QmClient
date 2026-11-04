#include "MapTagsPanel.h"

#include "UiButtons.h"
#include "UiForms.h"

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
	CUIRect Header, Refresh;
	View.HSplitTop(26.0f, &Header, &View);
	Header.VSplitRight(65.0f, &Header, &Refresh);
	pUi->DoLabel(&Header, Localize("Map tags"), 20.0f, TEXTALIGN_ML);
	if(ui_widget::SecondaryButton(Ctx, &m_RefreshButton, Localize("Refresh"), Refresh, Tags.Busy()))
		Tags.Refresh();
	View.HSplitTop(8.0f, nullptr, &View);
	CUIRect List, Footer;
	View.HSplitBottom(90.0f, &List, &Footer);
	List.HSplitBottom(5.0f, &List, nullptr);
	vec2 Offset(0, 0);
	m_Scroll.Begin(&List, &Offset);
	float X = List.x;
	float Y = List.y + Offset.y;
	for(const auto &Tag : Snapshot.m_vTags)
	{
		char aLabel[128];
		str_format(aLabel, sizeof(aLabel), "%s ×%d%s", Tag.m_Text.c_str(), Tag.m_Count, Tag.m_Voted ? "" : "  +1");
		const float Width = std::min(List.w, pGameClient->TextRender()->TextWidth(13.0f, aLabel) + 22.0f);
		if(X > List.x && X + Width > List.x + List.w)
		{
			X = List.x;
			Y += 32.0f;
		}
		CUIRect Capsule{X, Y, Width, 26.0f};
		m_Scroll.AddRect(Capsule);
		auto &Button = m_TagButtons[Tag.m_Id];
		const bool Enabled = Snapshot.m_Eligible && !Tag.m_Voted && !Tags.Busy();
		if(Enabled && pUi->DoButtonLogic(&Button, 0, &Capsule, BUTTONFLAG_LEFT))
			Tags.Submit(pGameClient->Http(), Tag.m_Text.c_str());
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Color = color_cast<ColorRGBA>(ColorHSLA(QmMapTags::ColorHue(Tag.m_Color), 0.55f, 0.43f, 0.9f));
		Options.m_Enabled = Enabled;
		Options.m_Selected = Tag.m_Voted;
		Options.m_Radius = 13.0f;
		ui_widget::DrawButtonSurface(Ctx, &Button, Capsule, Options);
		SLabelProperties Properties;
		Properties.m_StopAtEnd = true;
		pUi->DoLabel(&Capsule, aLabel, 13.0f, TEXTALIGN_MC, Properties);
		X += Width + 6.0f;
	}
	if(Snapshot.m_vTags.empty())
	{
		CUIRect Empty{List.x, Y, List.w, 24.0f};
		pUi->DoLabel(&Empty, Tags.Ready() ? Localize("No approved tags yet.") : Localize("Loading map tags…"), 13.0f, TEXTALIGN_ML);
		m_Scroll.AddRect(Empty);
	}
	m_Scroll.End();
	CUIRect Presets;
	Footer.HSplitTop(22.0f, &Presets, &Footer);
	static constexpr const char *PRESETS[] = {"hj", "fly", "Gores", "Hook"};
	for(size_t Index = 0; Index < std::size(PRESETS); ++Index)
	{
		CUIRect Button;
		Presets.VSplitLeft(std::min(60.0f, Presets.w / (std::size(PRESETS) - Index)), &Button, &Presets);
		Button.VMargin(2.0f, &Button);
		if(ui_widget::SecondaryButton(Ctx, &m_aPresetButtons[Index], PRESETS[Index], Button, !Snapshot.m_Eligible || Tags.Busy()))
			m_Input.Set(PRESETS[Index]);
	}
	Footer.HSplitTop(5.0f, nullptr, &Footer);
	CUIRect Composer, Input, Submit;
	Footer.HSplitTop(22.0f, &Composer, &Footer);
	Composer.VSplitRight(70.0f, &Input, &Submit);
	Input.VSplitRight(5.0f, &Input, nullptr);
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_pPlaceholder = Localize("New tag (up to 10 characters)");
	InputOptions.m_FontSize = 13.0f;
	ui_widget::InputField(Ctx, &m_Input, Input, InputOptions);
	if(ui_widget::PrimaryButton(Ctx, &m_SubmitButton, Localize("Submit"), Submit, !Snapshot.m_Eligible || Tags.Busy() || !QmMapTags::ValidTag(m_Input.GetString())))
		Tags.Submit(pGameClient->Http(), m_Input.GetString());
	Footer.HSplitTop(5.0f, nullptr, &Footer);
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
	SLabelProperties StatusProperties;
	StatusProperties.m_StopAtEnd = true;
	pUi->DoLabel(&Footer, Localize(pStatus), 12.0f, TEXTALIGN_TL, StatusProperties);
}
