#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

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

		float MeasureVisualCardHeight(const SSettingsContentMetrics &Metrics, const EQmModuleId Id)
		{
			const auto Rows = [&Metrics](const float Count) { return CardRows(Metrics, Count); };
			switch(Id)
			{
			case EQmModuleId::ChatBubble:
				return g_Config.m_QmChatBubble ? Rows(5.0f) + 2.0f * Metrics.m_LineHeight + 2.0f * Metrics.m_LineSpacing : Rows(1.0f);
			case EQmModuleId::CameraView:
				return Rows(6.0f + (g_Config.m_QmCameraDrift ? 3.0f : 0.0f) + (g_Config.m_QmDynamicFov ? 2.0f : 0.0f) + (g_Config.m_QmAspectPreset == 6 ? 1.0f : 0.0f)) + Metrics.m_BodySize;
			case EQmModuleId::WeaponAnimation:
				return ResolveQmVisualWeaponAnimationHeight(Metrics, g_Config.m_QmWeaponSwitchAnim != 0, g_Config.m_QmWeaponReloadAnim != 0);
			case EQmModuleId::Streamer: return Rows(3.0f);
			case EQmModuleId::FocusMode: return ResolveQmVisualFocusModeHeight(Metrics);
			case EQmModuleId::EntityOverlay: return Rows(9.0f);
			case EQmModuleId::CollisionHitbox:
				return ResolveQmVisualCollisionHitboxHeight(Metrics, g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox);
			default: return Rows(1.0f);
			}
		}

		uint64_t MeasureVisualCardRevision(const EQmModuleId Id)
		{
			switch(Id)
			{
			case EQmModuleId::ChatBubble: return g_Config.m_QmChatBubble ? 1u : 0u;
			case EQmModuleId::CameraView:
				return (g_Config.m_QmCameraDrift ? 1u : 0u) |
				       (g_Config.m_QmDynamicFov ? 2u : 0u) |
				       (g_Config.m_QmAspectPreset == 6 ? 4u : 0u);
			case EQmModuleId::WeaponAnimation:
				return (g_Config.m_QmWeaponSwitchAnim ? 1u : 0u) |
				       (g_Config.m_QmWeaponReloadAnim ? 2u : 0u);
			case EQmModuleId::CollisionHitbox:
				return (g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox ? 1u : 0u) |
				       (g_Config.m_QmHitboxShowMap ? 1u << 1 : 0u) |
				       (g_Config.m_QmHitboxShowTeeCollision ? 1u << 2 : 0u) |
				       (g_Config.m_QmHitboxShowTeeFreeze ? 1u << 3 : 0u) |
				       (g_Config.m_QmHitboxShowTeeDeath ? 1u << 4 : 0u) |
				       (g_Config.m_QmHitboxShowPickups ? 1u << 5 : 0u) |
				       (g_Config.m_QmHitboxShowHammer ? 1u << 6 : 0u) |
				       (g_Config.m_QmHitboxShowProjectiles ? 1u << 7 : 0u) |
				       (g_Config.m_QmHitboxShowLasers ? 1u << 8 : 0u) |
				       (g_Config.m_QmHitboxShowFreezeLasers ? 1u << 9 : 0u) |
				       (g_Config.m_QmHitboxShowHook ? 1u << 10 : 0u);
			default: return 0u;
			}
		}

		FSettingsCardPreLayoutInput BuildVisualPreLayoutInput(const SQmCardBuildContext &Ctx, const EQmModuleId Id)
		{
			if(Ctx.m_ReadOnly)
				return {};
			CMenus *pMenus = Ctx.m_pMenus;
			const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
			const float LineHeight = Metrics.m_LineHeight;
			const float LineSpacing = Metrics.m_LineSpacing;
			switch(Id)
			{
			case EQmModuleId::ChatBubble:
				return [pMenus, LineHeight, LineSpacing](CUIRect Content) {
					return qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmChatBubble, &g_Config.m_QmChatBubble);
				};
			case EQmModuleId::CameraView:
				return [pMenus, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDrift, &g_Config.m_QmCameraDrift);
					if(g_Config.m_QmCameraDrift)
					{
						Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
						Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
						Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmDynamicFov, &g_Config.m_QmDynamicFov) || Changed;
					if(g_Config.m_QmDynamicFov)
					{
						Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
						Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmCinematicCamera, &g_Config.m_QmCinematicCamera) || Changed;
					return Changed;
				};
			case EQmModuleId::WeaponAnimation:
				return [pMenus, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim, &g_Config.m_QmWeaponSwitchAnim);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, &g_Config.m_QmWeaponReloadAnim) || Changed;
					return Changed;
				};
			case EQmModuleId::CollisionHitbox:
				return [Ctx, LineHeight](CUIRect Content) {
					const CUIRect VisibleContent = Content;
					CUIRect Row;
					Content.HSplitTop(LineHeight, &Row, &Content);
					const CUIRect HitRect = Row.Intersection(VisibleContent);
					int HitboxModeEnabled = g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox;
					const bool Changed = HitRect.w > 0.0f && HitRect.h > 0.0f && QmCardRenderHook::DoButtonLogic(Ctx.m_pMenus, &g_Config.m_QmHitboxMode, HitboxModeEnabled, &HitRect, BUTTONFLAG_LEFT) != 0;
					if(Changed)
					{
						HitboxModeEnabled ^= 1;
						g_Config.m_QmHitboxMode = HitboxModeEnabled;
						g_Config.m_QmShowCollisionHitbox = 0;
					}
					return Changed;
				};
			default:
				return {};
			}
		}
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
				Ctx, ModuleId, pStableId, pTitle, pSubtitle, Render,
				[ModuleId, Metrics](float) { return MeasureVisualCardHeight(Metrics, ModuleId); },
				MeasureVisualCardRevision(ModuleId),
				BuildVisualPreLayoutInput(Ctx, ModuleId),
				Out);
		};

		switch(Id)
		{
		case EQmModuleId::ChatBubble:
			Add(Id, "qm:chat_bubble", "Chat Bubble", "Show chat messages above players", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualChatBubbleContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::CameraView:
			Add(Id, "qm:camera_view", "Camera & FOV", "Adjust game camera and FOV settings", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualCameraViewContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::SkinAppearance:
		case EQmModuleId::SkinTransition:
			return BuildSkinCard(Ctx, Id, Out);
		case EQmModuleId::WeaponAnimation:
			Add(Id, "qm:weapon_animation", "Weapon animation", "Play a slide-in rotation animation when switching weapons", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualWeaponAnimationContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::Streamer:
			Add(Id, "qm:streamer", "Streamer Mode", "Protect names and skins while streaming", [pMenus, LineHeight, LineSpacing](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualStreamerContent(pMenus, Content, LineHeight, LineSpacing); });
			return true;
		case EQmModuleId::FocusMode:
			Add(Id, "qm:focus_mode", "Zen Mode", "Hide UI for focused gameplay", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualFocusModeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LineSpacing, LabelWidth); });
			return true;
		case EQmModuleId::EntityOverlay:
			Add(Id, "qm:entity_overlay", "Entity Layer Colors", "Adjust opacity of entity layers", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualEntityOverlayContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::CollisionHitbox:
			Add(Id, "qm:collision_hitbox", "Hitbox mode", "Show collision and weapon interaction", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmVisualCollisionHitboxContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::WaterHammerHighlight:
			return BuildWaterHammerCard(Ctx, Id, Out);
		default:
			return false;
		}
	}
} // namespace qm_card_catalog
