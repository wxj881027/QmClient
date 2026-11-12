#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

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
