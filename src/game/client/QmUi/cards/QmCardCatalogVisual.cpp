#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

// 视觉分类卡片入口（10 张，含恢复的禅模式与卡锤高亮）：皮肤卡与卡锤卡委托独立模块，其他卡在此提供测量、输入和渲染。
// 页面（栖梦「视觉」页、搜索页）只声明"这一页有这些卡"。
namespace qm_card_catalog
{
	namespace
	{
		using qm_module::EQmModuleId;

	} // namespace

	void QmCardRenderHook::RenderDummyDistanceLink(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, bool ReadOnly)
	{
		CUiScopedMeasurement Guard(pMenus->Ui(), ReadOnly && !pMenus->Ui()->RenderOnly());
		RenderQmVisualCheckbox(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmDummyDistanceLink, "dummy-distance-link-enable", Localize("Show dummy distance link"), &g_Config.m_QmDummyDistanceLink);
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row, Label, Control;
		if(Flow.Control("dummy-distance-link-style", Metrics.m_LineHeight, Row))
		{
			SplitSettingsControlRow(Row, Metrics, &Label, &Control);
			pMenus->RenderQmVisualLabel("dummy-distance-link-style", &Label, Localize("Link style"), Metrics.m_BodySize);
			static CButtonContainer s_aButtons[2];
			const char *apLabels[] = {Localize("Line"), Localize("Droplet")};
			const int Style = pMenus->DoSegmentedChoice(s_aButtons, apLabels, 2, g_Config.m_QmDummyDistanceLinkStyle, Control, ui_token::radius::BASE, nullptr, Metrics.m_BodySize, &g_Config.m_QmDummyDistanceLinkStyle);
			if(Flow.Input() && !ReadOnly && !pMenus->Ui()->RenderOnly() && Style >= 0)
				g_Config.m_QmDummyDistanceLinkStyle = Style;
		}
		Flow.Space(Metrics.m_LineSpacing);
	}

	bool BuildVisualCard(const SQmCardBuildContext &Ctx, const EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const float LineHeight = Metrics.m_LineHeight;
		const float BodySize = Metrics.m_BodySize;
		const float LineSpacing = Metrics.m_LineSpacing;
		const float LabelWidth = Ctx.m_LabelWidth;
		const bool ReadOnly = Ctx.m_ReadOnly;

		const auto Add = [&](const EQmModuleId ModuleId, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			MakeModuleCard(
				Ctx,
				ModuleId,
				pStableId,
				pTitle,
				pSubtitle,
				Render,
				Out);
			UseDeclaredModuleContent(Ctx, Out);
		};

		switch(Id)
		{
		case EQmModuleId::ChatBubble:
			Add(Id, "qm:chat_bubble", "Chat Bubble", "Show chat messages above players", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualChatBubbleContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::CameraView:
			Add(Id, "qm:camera_view", "Camera & FOV", "Adjust game camera and FOV settings", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualCameraViewContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::SpectatorMode:
			Add(Id, "qm:spectator_mode", "Spectate mode", "Smooth the mouse-driven camera in live and demo free spectating", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { QmCardRenderHook::RenderQmVisualSpectatorModeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::SkinAppearance:
			return BuildSkinCard(Ctx, Id, Out);
		case EQmModuleId::WeaponAnimation:
			Add(Id, "qm:weapon_animation", "Weapon animation", "Play a slide-in rotation animation when switching weapons", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualWeaponAnimationContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::Streamer:
			Add(Id, "qm:streamer", "Streamer Mode", "Protect names and skins while streaming", [pMenus, LineHeight, LineSpacing](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualStreamerContent(pMenus, Content, LineHeight, LineSpacing); });
			return true;
		case EQmModuleId::FocusMode:
		{
			Add(Id, "qm:focus_mode", "Zen Mode", "Hide UI for focused gameplay", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualFocusModeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LineSpacing, LabelWidth); });
			return true;
		}
		case EQmModuleId::EntityOverlay:
			Add(Id, "qm:entity_overlay", "Entity layer adjustments", "Adjust opacity of entity layers", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualEntityOverlayContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::CollisionHitbox:
			Add(Id, "qm:collision_hitbox", "Hitbox mode", "Show collision and weapon interaction", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualCollisionHitboxContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DummyDistanceLink:
			Add(Id, "qm:dummy_distance_link", "Dummy distance link", "Show the distance and direction from your main tee to your dummy", [pMenus, Metrics, ReadOnly](CUIRect &Content) { QmCardRenderHook::RenderDummyDistanceLink(pMenus, Content, Metrics, ReadOnly); });
			return true;
		case EQmModuleId::WaterHammerHighlight:
			return QmCardRenderHook::BuildWaterHammerCard(Ctx, Id, Out);
		case EQmModuleId::AppearancePreset:
			return BuildAppearancePresetCard(Ctx, Out);
		case EQmModuleId::Tooltip:
			return BuildTooltipCard(Ctx, Out);
		default:
			return false;
		}
	}
} // namespace qm_card_catalog
