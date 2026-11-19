#include "bind_menu_preview.h"

#include "wheel_presets.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/skins.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>

namespace qm_bind_menu_preview
{
	namespace
	{
		float Scaled(float Value, float UiScale)
		{
			return Value * std::max(0.75f, UiScale);
		}

		ColorRGBA SecondaryTextColor(const SUiTheme &Theme)
		{
			return Theme.m_TextBody.WithMultipliedAlpha(ui_token::color::TEXT_SECONDARY.a);
		}

		SUiTheme OptionTextTheme(const SUiTheme &Theme, bool Expanded)
		{
			SUiTheme Result = Theme;
			Result.m_TextTitle = Theme.m_TextTitle.WithMultipliedAlpha(Expanded ? 1.0f : 0.78f);
			Result.m_TextBody = Theme.m_TextBody.WithMultipliedAlpha(Expanded ? 1.0f : 0.86f);
			return Result;
		}

		CUIRect ScaleAbout(const CUIRect &Rect, const vec2 &Anchor, float Scale)
		{
			return CUIRect{Anchor.x + (Rect.x - Anchor.x) * Scale, Anchor.y + (Rect.y - Anchor.y) * Scale,
				Rect.w * Scale, Rect.h * Scale};
		}

		void Label(CUi *pUi, const CUIRect &Rect, const char *pText, float FontSize, int Align, const ColorRGBA &Color, bool Ellipsis = true)
		{
			if(pText == nullptr || pText[0] == '\0' || Rect.w <= 0.0f || Rect.h <= 0.0f)
				return;
			SLabelProperties Props;
			Props.m_MaxWidth = Rect.w;
			Props.m_EllipsisAtEnd = Ellipsis;
			CUIRect DrawRect = Rect;
			pUi->TextRender()->TextColor(Color);
			pUi->DoLabel(&DrawRect, pText, FontSize, Align, Props);
		}

		std::optional<std::string> Value(const qm_wheel_presets::SIdentity &Identity, int Field)
		{
			return Identity.m_aValues[Field];
		}

		int IntValue(const qm_wheel_presets::SIdentity &Identity, int Field, int Default)
		{
			const auto Text = Value(Identity, Field);
			return Text ? str_toint(Text->c_str()) : Default;
		}

		bool HasIdentity(const qm_wheel_presets::SIdentity &Identity)
		{
			return std::any_of(Identity.m_aValues.begin(), Identity.m_aValues.end(), [](const auto &Entry) { return Entry.has_value(); });
		}

		void RenderIdentity(CUi *pUi, CGameClient *pGameClient, const CUIRect &Rect, const qm_wheel_presets::SPreset &Preset,
			const SUiTheme &Theme, float UiScale, float Alpha)
		{
			const bool Main = HasIdentity(Preset.m_aTees[0]);
			const bool Dummy = HasIdentity(Preset.m_aTees[1]);
			const SIdentityLayout Layout = BuildIdentityLayout(Rect, Main, Dummy, UiScale);
			const auto RenderTeeAvatar = [&](int Tee, const CUIRect &Target) {
				const auto &Identity = Preset.m_aTees[Tee];
				const std::string SkinName = Value(Identity, qm_wheel_presets::SKIN).value_or("default");
				CTeeRenderInfo Info;
				Info.Apply(pGameClient->m_Skins.Find(SkinName.c_str()));
				const bool CustomColors = IntValue(Identity, qm_wheel_presets::CUSTOM_COLORS, 0) != 0;
				Info.ApplyColors(CustomColors,
					IntValue(Identity, qm_wheel_presets::BODY_COLOR, 0),
					IntValue(Identity, qm_wheel_presets::FEET_COLOR, 0));
				Info.m_Size = std::max(Scaled(ui_token::font::BODY, UiScale),
					std::min(Target.w, Target.h) * ui_token::bind_menu::IDENTITY_AVATAR_HEIGHT_RATIO);
				const int Emote = IntValue(Identity, qm_wheel_presets::EYES, 0);
				const vec2 Position = Target.Center() + vec2(0.0f, Info.m_Size * 0.08f);
				pGameClient->RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, Emote, vec2(1.0f, 0.0f), Position, Alpha);
			};
			const auto NameFor = [&](int Tee) {
				std::string Name = Value(Preset.m_aTees[Tee], qm_wheel_presets::NAME).value_or("");
				return Name.empty() ? std::string(Localize("Identity")) : Name;
			};

			if(Layout.m_Compact)
			{
				std::string Names;
				std::string Clans;
				std::string LastClan;
				for(int Tee = 0; Tee < 2; ++Tee)
				{
					if(!HasIdentity(Preset.m_aTees[Tee]))
						continue;
					if(!Names.empty())
						Names += " / ";
					Names += NameFor(Tee);
					const std::string Clan = Value(Preset.m_aTees[Tee], qm_wheel_presets::CLAN).value_or("");
					// 本体与分身同战队时只显示一次，避免窄卡片里重复同一段文字。
					if(Clan.empty() || Clan == LastClan)
						continue;
					if(!Clans.empty())
						Clans += " / ";
					Clans += Clan;
					LastClan = Clan;
				}
				int AvatarRow = 0;
				for(int Tee = 0; Tee < 2; ++Tee)
				{
					if(HasIdentity(Preset.m_aTees[Tee]))
						RenderTeeAvatar(Tee, Layout.m_aRows[AvatarRow++].m_Avatar);
				}
				const float NameSize = std::min(Scaled(ui_token::font::BODY, UiScale),
					std::max(Scaled(ui_token::font::CAPTION, UiScale), Layout.m_CompactNames.h * 0.86f));
				const float ClanSize = std::min(Scaled(ui_token::font::SMALL, UiScale),
					std::max(Scaled(ui_token::font::CAPTION, UiScale), Layout.m_CompactClans.h * 0.86f));
				Label(pUi, Layout.m_CompactNames, Names.c_str(), NameSize, TEXTALIGN_MC,
					Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
				Label(pUi, Layout.m_CompactClans, Clans.c_str(), ClanSize, TEXTALIGN_MC,
					SecondaryTextColor(Theme).WithMultipliedAlpha(Alpha));
				return;
			}

			int RowIndex = 0;
			for(int Tee = 0; Tee < 2; ++Tee)
			{
				const auto &Identity = Preset.m_aTees[Tee];
				if(!HasIdentity(Identity))
					continue;
				if(RowIndex >= Layout.m_TeeCount)
					break;
				const auto &Row = Layout.m_aRows[RowIndex++];
				RenderTeeAvatar(Tee, Row.m_Avatar);
				const std::string Clan = Value(Identity, qm_wheel_presets::CLAN).value_or("");
				Label(pUi, Row.m_Name, NameFor(Tee).c_str(), Scaled(ui_token::font::BODY, UiScale), TEXTALIGN_ML,
					Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
				Label(pUi, Row.m_Clan, Clan.c_str(), Scaled(ui_token::font::SMALL, UiScale), TEXTALIGN_ML,
					SecondaryTextColor(Theme).WithMultipliedAlpha(Alpha));
			}
		}

		void RenderChat(CUi *pUi, const CUIRect &Rect, const SOption &Option, const qm_wheel_presets::SPreset &Preset,
			const SUiTheme &Theme, float UiScale, float Alpha)
		{
			if(Preset.m_Chat.empty())
				return;
			const char *pTitle = Option.m_pName[0] != '\0' ? Option.m_pName : Preset.m_Chat.c_str();
			CUIRect TitleRect = Rect;
			TitleRect.h = std::min(Rect.h, Scaled(ui_token::font::BODY * 1.35f, UiScale));
			Label(pUi, TitleRect, pTitle, Scaled(ui_token::font::BODY, UiScale), TEXTALIGN_ML,
				Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
		}

		void RenderAssets(CUi *pUi, const CUIRect &Rect, const SOption &Option,
			const SUiTheme &Theme, float UiScale, float Alpha)
		{
			CUIRect NameRect = Rect;
			NameRect.h = std::min(Rect.h, Scaled(ui_token::font::BODY * 1.35f, UiScale));
			Label(pUi, NameRect, Option.m_pName[0] ? Option.m_pName : Localize("Assets"),
				Scaled(ui_token::font::BODY, UiScale), TEXTALIGN_ML, Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
		}

		void RenderCustom(CUi *pUi, const CUIRect &Rect, const SOption &Option, const SUiTheme &Theme, float UiScale, float Alpha)
		{
			CUIRect NameRect, CommandRect;
			Rect.HSplitMid(&NameRect, &CommandRect);
			NameRect.y += NameRect.h * 0.08f;
			NameRect.h *= 0.52f;
			CommandRect.y = NameRect.y + Scaled(ui_token::font::BODY * 1.35f, UiScale);
			CommandRect.h = std::max(0.0f, Rect.y + Rect.h - CommandRect.y);
			Label(pUi, NameRect, Option.m_pName[0] ? Option.m_pName : Localize("Untitled"), Scaled(ui_token::font::BODY, UiScale), TEXTALIGN_ML,
				Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
			Label(pUi, CommandRect, Option.m_pCommand, Scaled(ui_token::font::SMALL, UiScale), TEXTALIGN_ML,
				SecondaryTextColor(Theme).WithMultipliedAlpha(Alpha));
		}

		const qm_wheel_presets::SPreset *ResolvePreset(const SOption &Option)
		{
			if(Option.m_pPreset != nullptr)
				return Option.m_pPreset;
			static thread_local std::optional<qm_wheel_presets::SPreset> s_Parsed;
			s_Parsed = qm_wheel_presets::ParseCommand(Option.m_pCommand);
			return s_Parsed ? &*s_Parsed : nullptr;
		}

		void RenderOptionContent(CUi *pUi, CGameClient *pGameClient, const CUIRect &Rect, qm_bind_menu::EMenu Menu, const SOption &Option,
			const SUiTheme &Theme, float UiScale, float Alpha)
		{
			if(Menu == qm_bind_menu::EMenu::CUSTOM)
			{
				RenderCustom(pUi, Rect, Option, Theme, UiScale, Alpha);
				return;
			}
			const auto *pPreset = ResolvePreset(Option);
			if(pPreset == nullptr)
			{
				CUIRect NameRect = Rect;
				NameRect.h = Scaled(ui_token::font::BODY * 1.4f, UiScale);
				Label(pUi, NameRect, Option.m_pName[0] ? Option.m_pName : Localize("Untitled"), Scaled(ui_token::font::BODY, UiScale),
					TEXTALIGN_ML, Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
				CUIRect HintRect = {Rect.x, NameRect.y + NameRect.h, Rect.w, std::max(0.0f, Rect.y + Rect.h - NameRect.y - NameRect.h)};
				Label(pUi, HintRect, Localize("Legacy bind script"), Scaled(ui_token::font::SMALL, UiScale), TEXTALIGN_ML,
					SecondaryTextColor(Theme).WithMultipliedAlpha(Alpha));
				return;
			}
			if(Menu == qm_bind_menu::EMenu::IDENTITY)
				RenderIdentity(pUi, pGameClient, Rect, *pPreset, Theme, UiScale, Alpha);
			else if(Menu == qm_bind_menu::EMenu::CHAT)
				RenderChat(pUi, Rect, Option, *pPreset, Theme, UiScale, Alpha);
			else if(Menu == qm_bind_menu::EMenu::ASSETS)
				RenderAssets(pUi, Rect, Option, Theme, UiScale, Alpha);
		}

	} // namespace

	void RenderOption(CUi *pUi, CGameClient *pGameClient, const CUIRect &Rect, qm_bind_menu::EMenu Menu,
		const SOption &Option, float UiScale, float Alpha, bool Hovered)
	{
		const SUiTheme Theme = OptionTextTheme(ResolveConfiguredSecondaryPanelTheme(), Hovered);
		CUIRect Content = Rect;
		Content.Margin(Scaled(ui_token::spacing::SM, UiScale), &Content);
		RenderOptionContent(pUi, pGameClient, Content, Menu, Option, Theme, UiScale, Alpha);
		pUi->TextRender()->TextColor(Theme.m_TextBody);
	}

	void Render(CUi *pUi, CGameClient *pGameClient, const SLayout &Layout, qm_bind_menu::EMenu Menu,
		const SOption *pOptions, int OptionCount, int Hovered, int Scroll, float UiScale, float Alpha)
	{
		pUi->MapScreen();
		pUi->Graphics()->BlendNormal();
		const float RevealScale = RevealScaleFor(Layout.m_Layout, Alpha);
		const SUiTheme Theme = ResolveConfiguredSecondaryPanelTheme();
		const ColorRGBA Surface = ResolveConfiguredInterfaceSurface(Alpha, ui_token::panel::SURFACE_COLOR_SCALE);

		if(Layout.m_Layout == qm_bind_menu::ELayout::WHEEL)
		{
			pUi->Graphics()->TextureClear();
			pUi->Graphics()->QuadsBegin();
			pUi->Graphics()->SetColor(Surface);
			pUi->Graphics()->DrawCircle(Layout.m_Center.x, Layout.m_Center.y, Layout.m_WheelBackgroundOuterRadius * RevealScale, 64);
			pUi->Graphics()->QuadsEnd();
			const float HubWidth = std::min(Layout.m_WheelRadius * ui_token::bind_menu::WHEEL_HUB_WIDTH_RADIUS_RATIO,
				Scaled(ui_token::bind_menu::WHEEL_HUB_MIN_WIDTH * 2.0f, UiScale));
			const float HubHeight = Scaled(ui_token::font::HEADLINE * 1.8f, UiScale);
			CUIRect Hub{Layout.m_Center.x - HubWidth * 0.5f, Layout.m_Center.y - HubHeight * 0.5f, HubWidth, HubHeight};
			Hub = ScaleAbout(Hub, Layout.m_Center, RevealScale);
			Label(pUi, Hub, Localize(qm_bind_menu::LocalizedName(Menu)), Scaled(ui_token::font::HEADLINE, UiScale), TEXTALIGN_MC,
				Theme.m_TextTitle.WithMultipliedAlpha(Alpha));

			for(int Index = 0; Index < OptionCount; ++Index)
			{
				const CUIRect Card = ScaleAbout(WheelItemRect(Layout, Index, OptionCount), Layout.m_Center, RevealScale);
				const bool Active = Index == Hovered;
				CUIRect Content = Card;
				const float CardScale = Layout.m_WheelDoubleRing && (Index % 2) != 0 ?
								ui_token::bind_menu::WHEEL_INNER_CARD_SCALE :
								1.0f;
				Content.Margin(Scaled(ui_token::spacing::SM, UiScale) * RevealScale * CardScale, &Content);
				RenderOptionContent(pUi, pGameClient, Content, Menu, pOptions[Index], OptionTextTheme(Theme, Active), UiScale, Alpha);
				if(Active)
					pGameClient->m_Tooltips.DoToolTipForRectForced(pOptions[Index].m_pCommand, &Card,
						BuildHoverText(Menu, pOptions[Index], pOptions[Index].m_pPreset).c_str());
			}
			if(OptionCount == 0)
			{
				CUIRect Empty = Hub;
				Empty.y = Hub.y + Hub.h + Scaled(ui_token::spacing::SM, UiScale);
				Empty.h = Scaled(ui_token::font::BODY * 1.5f, UiScale);
				Label(pUi, Empty, Localize("No entries yet"), Scaled(ui_token::font::SMALL, UiScale), TEXTALIGN_MC,
					SecondaryTextColor(Theme).WithMultipliedAlpha(Alpha));
			}
			pUi->TextRender()->TextColor(Theme.m_TextBody);
			return;
		}

		const vec2 PanelAnchor = Layout.m_Panel.Center();
		const CUIRect Panel = ScaleAbout(Layout.m_Panel, PanelAnchor, RevealScale);
		const CUIRect Title = ScaleAbout(Layout.m_Title, PanelAnchor, RevealScale);
		const CUIRect Rows = ScaleAbout(Layout.m_Rows, PanelAnchor, RevealScale);
		DrawRoundedSurface(pUi, Panel, Surface, ui_token::color::COLOR_TRANSPARENT, ui_token::radius::CARD);
		Label(pUi, Title, Localize(qm_bind_menu::LocalizedName(Menu)), Scaled(ui_token::font::HEADLINE, UiScale), TEXTALIGN_ML,
			Theme.m_TextTitle.WithMultipliedAlpha(Alpha));
		if(OptionCount == 0)
		{
			Label(pUi, Rows, Localize("No entries yet"), Scaled(ui_token::font::BODY, UiScale), TEXTALIGN_MC,
				SecondaryTextColor(Theme).WithMultipliedAlpha(Alpha));
			pUi->TextRender()->TextColor(Theme.m_TextBody);
			return;
		}

		const int First = std::clamp(Scroll, 0, std::max(0, OptionCount - 1));
		const int Last = std::min(OptionCount, First + Layout.m_VisibleRows);
		for(int Index = First; Index < Last; ++Index)
		{
			const int Visible = Index - First;
			CUIRect Row{Rows.x, Rows.y + Visible * (Layout.m_RowHeight + Layout.m_RowSpacing) * RevealScale,
				Rows.w, Layout.m_RowHeight * RevealScale};
			const bool Active = Index == Hovered;
			CUIRect Content = Row;
			Content.Margin(Scaled(ui_token::spacing::SM, UiScale) * RevealScale, &Content);
			RenderOptionContent(pUi, pGameClient, Content, Menu, pOptions[Index], OptionTextTheme(Theme, Active), UiScale, Alpha);
			if(Active)
				pGameClient->m_Tooltips.DoToolTipForRectForced(pOptions[Index].m_pCommand, &Row,
					BuildHoverText(Menu, pOptions[Index], pOptions[Index].m_pPreset).c_str());
		}
		pUi->TextRender()->TextColor(Theme.m_TextBody);
	}

	std::string BuildHoverText(qm_bind_menu::EMenu Menu, const SOption &Option, const qm_wheel_presets::SPreset *pPreset)
	{
		if(!qm_bind_menu::HasHoverDetails(Menu))
			return {};
		std::string Text;
		const auto Add = [&](const std::string &Line) {
			if(Line.empty())
				return;
			if(!Text.empty())
				Text += '\n';
			Text += Line;
		};
		const char *pTitle = Option.m_pName[0] != '\0' ? Option.m_pName : Localize("Untitled");
		if(Menu == qm_bind_menu::EMenu::CUSTOM || pPreset == nullptr || Option.m_LegacyScript)
		{
			Add(pTitle);
			if(Option.m_pCommand[0] != '\0')
				Add(Option.m_pCommand);
			if(Option.m_LegacyScript)
				Add(Localize("Legacy bind script"));
			return Text;
		}

		Add(pTitle);
		if(Menu == qm_bind_menu::EMenu::CHAT)
		{
			if(pPreset->m_TeamChat)
				Add(Localize("Team chat"));
			Add(pPreset->m_Chat);
		}
		else if(Menu == qm_bind_menu::EMenu::ASSETS)
		{
			for(int Index = 0; Index < qm_wheel_presets::NUM_ASSETS; ++Index)
			{
				const std::string &ValueText = pPreset->m_aAssets[Index];
				Add(std::string(Localize(qm_wheel_presets::AssetCategoryName(Index))) + ": " +
					(ValueText.empty() ? std::string(Localize("Default")) : ValueText));
			}
		}
		return Text;
	}
} // namespace qm_bind_menu_preview
