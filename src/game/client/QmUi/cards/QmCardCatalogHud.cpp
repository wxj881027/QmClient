#include "QmCardCatalogInternal.h"
#include "QmCardMeasureRevision.h"

#include <engine/shared/config.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/UiForms.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

// HUD 分类卡片模块（14 张）：卡片的高度测量、重测版本、预布局输入与内容渲染都在这里，
// 页面（栖梦「HUD」页、搜索页）只声明"这一页有这些卡"。
namespace qm_card_catalog
{
	namespace
	{
		using qm_module::EQmModuleId;

		// 开关倒计时显示位置的两个复选框：渲染与卡片预布局必须共用同一组 id，否则同一次点击会被处理两次。
		int s_SwitchCountdownFollowTeeId;
		int s_SwitchCountdownMediaIslandId;

		void ConsumeQmHudRow(CUIRect &Content, const SSettingsContentMetrics &Metrics)
		{
			Content.HSplitTop(Metrics.m_LineHeight, nullptr, &Content);
			Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
		}

		void ConsumeQmHudHeight(CUIRect &Content, const float Height)
		{
			Content.HSplitTop(std::max(0.0f, Height), nullptr, &Content);
		}

		float MeasureHudCardHeight(const SSettingsContentMetrics &Metrics, const EQmModuleId Id, const float ContentWidth)
		{
			const auto Rows = [&Metrics](const float Count) { return CardRows(Metrics, Count); };
			const bool DummyMiniViewExpanded = g_Config.m_QmDummyMiniView != 0;
			const bool DynamicIslandOriginalStyle = g_Config.m_QmHudIslandUseOriginalStyle != 0;
			switch(Id)
			{
			case EQmModuleId::DummyMiniView: return ResolveQmHudDummyMiniViewHeight(Metrics, DummyMiniViewExpanded);
			case EQmModuleId::Coords: return ResolveQmHudCoordsHeight(Metrics);
			case EQmModuleId::PlayerStats: return ResolveQmHudPlayerStatsHeight(Metrics, g_Config.m_QmPlayerStatsMapProgress != 0, g_Config.m_QmPlayerStatsMapProgressStyle != 0);
			case EQmModuleId::DebugGraph: return Rows(2.0f);
			case EQmModuleId::DebugMode: return Rows(5.0f);
			case EQmModuleId::InputOverlay: return ResolveQmHudInputOverlayHeight(Metrics, g_Config.m_QmInputOverlay != 0);
			case EQmModuleId::HudNotifications: return ResolveQmHudNotificationsHeight(Metrics, g_Config.m_QmHudNotificationsShowAdvanced != 0, g_Config.m_QmHudNotificationsUseCategoryFilters != 0);
			case EQmModuleId::Voice: return ResolveQmHudVoiceHeight(Metrics, g_Config.m_QmVoiceEnable != 0, g_Config.m_QmVoiceShowAdvanced != 0, g_Config.m_QmVoiceShowConnectionStatus != 0, g_Config.m_QmVoiceNoiseSuppressEnable, g_Config.m_QmVoiceVadEnable != 0, g_Config.m_QmVoiceStereo != 0);
			case EQmModuleId::DynamicIsland: return ResolveQmHudDynamicIslandHeight(Metrics, DynamicIslandOriginalStyle, g_Config.m_QmSwitchCountdown != 0, ContentWidth);
			case EQmModuleId::SystemMediaControls: return g_Config.m_QmSmtcEnable ? Rows(3.0f) : Rows(1.0f);
			case EQmModuleId::Lyrics:
			{
				// 来源开关、歌词开关和对应来源的接入操作、状态或凭据行。
				size_t HookCount = 0;
				QmMusicHookRegistry(&HookCount);
				return Rows((float)HookCount + 2.0f + (g_Config.m_QmSpotifyEnable != 0 ? 1.0f : 0.0f) + (g_Config.m_QmKugouHookEnable != 0 ? 1.0f : 0.0f) + (g_Config.m_QmKugouHookEnable != 0 || g_Config.m_QmQQMusicHookEnable != 0 ? 1.0f : 0.0f));
			}
			case EQmModuleId::Background3D: return ResolveQmHudBackground3DHeight(Metrics, ContentWidth, g_Config.m_Qm3DParticles != 0, g_Config.m_Qm3DParticlesColorMode == 1, g_Config.m_Qm3DParticlesGlow != 0, g_Config.m_Qm3DParticlesTrail != 0, g_Config.m_Qm3DParticlesPulse != 0, g_Config.m_Qm3DParticlesTwinkle != 0);
			case EQmModuleId::BindStatusHud:
				return Rows(4.0f); // 当前卡片实际渲染 4 个状态开关
			case EQmModuleId::GoresDrownBoard: return Rows(g_Config.m_QmGoresDrownBoard != 0 ? 4.0f : 1.0f);
			default: return Rows(1.0f);
			}
		}

		uint64_t MeasureHudCardRevision(const EQmModuleId Id)
		{
			return MeasureModuleCardRevision(Id);
		}

		FSettingsCardPreLayoutInput BuildHudPreLayoutInput(const SQmCardBuildContext &Ctx, const EQmModuleId Id)
		{
			if(Ctx.m_ReadOnly)
				return {};
			CMenus *pMenus = Ctx.m_pMenus;
			const SSettingsContentMetrics &Metrics = Ctx.m_Metrics;
			const float LineHeight = Metrics.m_LineHeight;
			const float LineSpacing = Metrics.m_LineSpacing;
			switch(Id)
			{
			case EQmModuleId::InputOverlay:
				return [pMenus, Metrics](CUIRect Content) {
					return qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmInputOverlay, &g_Config.m_QmInputOverlay);
				};
			case EQmModuleId::DynamicIsland:
				return [pMenus, Metrics, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandUseOriginalStyle, &g_Config.m_QmHudIslandUseOriginalStyle);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandShowTeam, &g_Config.m_QmHudIslandShowTeam) || Changed;
					if(!g_Config.m_QmHudIslandUseOriginalStyle)
					{
						const SSettingsColorRowLayout ColorLayout = ResolveSettingsColorRowLayout(Content, Metrics, false);
						Content.HSplitTop(ColorLayout.m_ConsumedHeight, nullptr, &Content);
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, &g_Config.m_QmHookCountdown) || Changed;
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmSwitchCountdown, &g_Config.m_QmSwitchCountdown) || Changed;
					if(g_Config.m_QmSwitchCountdown)
					{
						// 两个位置开关各自翻转自己的标志位，再合成模式值，与渲染路径保持一致。
						int FollowTee = QmHudSwitchCountdownShowsFollowTee(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
						int MediaIsland = QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
						bool LocationChanged = qm_card_catalog::QmCardRenderHook::ToggleQmHudCountdownLocation(pMenus, Content, LineHeight, LineSpacing, SwitchCountdownFollowTeeId(), &FollowTee);
						LocationChanged |= qm_card_catalog::QmCardRenderHook::ToggleQmHudCountdownLocation(pMenus, Content, LineHeight, LineSpacing, SwitchCountdownMediaIslandId(), &MediaIsland);
						if(LocationChanged)
						{
							if(FollowTee == 0 && MediaIsland == 0)
							{
								g_Config.m_QmSwitchCountdown = 0;
								FollowTee = 0;
								MediaIsland = 1;
							}
							g_Config.m_QmSwitchCountdownMode = QmHudSwitchCountdownModeFromLocations(FollowTee != 0, MediaIsland != 0, g_Config.m_QmSwitchCountdownMode);
							Changed = true;
						}
					}
					if(!g_Config.m_QmHudIslandUseOriginalStyle)
						Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandAlwaysShowTrackDetails, &g_Config.m_QmHudIslandAlwaysShowTrackDetails) || Changed;
					return Changed;
				};
			case EQmModuleId::PlayerStats:
				return [pMenus, Metrics](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmPlayerStatsHud, &g_Config.m_QmPlayerStatsHud);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmPlayerStatsMapProgress, &g_Config.m_QmPlayerStatsMapProgress) || Changed;
					if(g_Config.m_QmPlayerStatsMapProgress)
					{
						Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmPlayerStatsMapProgressStyle, &g_Config.m_QmPlayerStatsMapProgressStyle) || Changed;
						if(!g_Config.m_QmPlayerStatsMapProgressStyle)
						{
							ConsumeQmHudRow(Content, Metrics);
							for(int Index = 0; Index < 4; ++Index)
								ConsumeQmHudRow(Content, Metrics);
						}
						Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmPlayerStatsMapProgressDbgRoute, &g_Config.m_QmPlayerStatsMapProgressDbgRoute) || Changed;
					}
					qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmPlayerStatsResetOnJoin, &g_Config.m_QmPlayerStatsResetOnJoin);
					return Changed;
				};
			case EQmModuleId::HudNotifications:
				return [pMenus, Metrics](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmHudNotificationsSystem, &g_Config.m_QmHudNotificationsSystem);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmHudNotificationsEcho, &g_Config.m_QmHudNotificationsEcho) || Changed;
					ConsumeQmHudRow(Content, Metrics);
					ConsumeQmHudRow(Content, Metrics);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmHudNotificationsShowAdvanced, &g_Config.m_QmHudNotificationsShowAdvanced) || Changed;
					if(g_Config.m_QmHudNotificationsShowAdvanced)
						Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmHudNotificationsUseCategoryFilters, &g_Config.m_QmHudNotificationsUseCategoryFilters) || Changed;
					return Changed;
				};
			case EQmModuleId::Voice:
				return [pMenus, Metrics, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceEnable, &g_Config.m_QmVoiceEnable);
					if(!g_Config.m_QmVoiceEnable)
						return Changed;
					ConsumeQmHudRow(Content, Metrics); // room password
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceMicMute, &g_Config.m_QmVoiceMicMute) || Changed;
					ConsumeQmHudRow(Content, Metrics); // microphone volume
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceVadEnable, &g_Config.m_QmVoiceVadEnable) || Changed;
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceShowAdvanced, &g_Config.m_QmVoiceShowAdvanced) || Changed;
					if(!g_Config.m_QmVoiceShowAdvanced)
						return Changed;

					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceShowConnectionStatus, &g_Config.m_QmVoiceShowConnectionStatus) || Changed;
					if(g_Config.m_QmVoiceShowConnectionStatus)
					{
						ConsumeQmHudHeight(Content, LineHeight * 1.46f + LineSpacing * 0.75f);
						ConsumeQmHudHeight(Content, 10.0f * (LineHeight + LineSpacing * 0.75f) + LineSpacing * 0.5f);
					}

					for(int Index = 0; Index < 5; ++Index)
						ConsumeQmHudRow(Content, Metrics); // server, input/output device, bitrate and noise mode
					if(g_Config.m_QmVoiceNoiseSuppressEnable != 0)
					{
#if !defined(CONF_RNNOISE)
						if(g_Config.m_QmVoiceNoiseSuppressEnable == 2)
							ConsumeQmHudHeight(Content, LineHeight * 0.78f + LineSpacing * 0.75f);
#endif
						ConsumeQmHudRow(Content, Metrics); // noise reduction strength
					}
					ConsumeQmHudRow(Content, Metrics); // AGC
					if(g_Config.m_QmVoiceVadEnable)
					{
						ConsumeQmHudRow(Content, Metrics);
						ConsumeQmHudRow(Content, Metrics);
					}
					ConsumeQmHudHeight(Content, LineSpacing * 1.15f);
					ConsumeQmHudRow(Content, Metrics); // playback volume
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceStereo, &g_Config.m_QmVoiceStereo) || Changed;
					if(g_Config.m_QmVoiceStereo)
						ConsumeQmHudRow(Content, Metrics);
					return Changed;
				};
			case EQmModuleId::SystemMediaControls:
				return [pMenus, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcEnable, &g_Config.m_QmSmtcEnable);
					return Changed;
				};
			case EQmModuleId::Lyrics:
				return [pMenus, Metrics, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = false;
					// 音乐 Hook 开关互斥:点开一个时自动关闭其余(见 QmMusicHookRegistry)。
					size_t HookCount = 0;
					const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);
					for(size_t i = 0; i < HookCount; ++i)
					{
						const SQmMusicHookEntry &Hook = apHooks[i];
						const bool HookChanged = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, Hook.m_pEnableConfig, Hook.m_pEnableConfig);
						if(HookChanged && *Hook.m_pEnableConfig != 0)
						{
							for(size_t j = 0; j < HookCount; ++j)
							{
								if(j != i)
									*apHooks[j].m_pEnableConfig = 0;
							}
						}
						Changed = HookChanged || Changed;
					}
					// 兜底:配置被外部直接改成多个 Hook 同时开启时,保留第一个,关闭其余。
					int FirstEnabled = -1;
					for(size_t i = 0; i < HookCount; ++i)
					{
						if(*apHooks[i].m_pEnableConfig != 0)
						{
							if(FirstEnabled == -1)
								FirstEnabled = (int)i;
							else
								*apHooks[i].m_pEnableConfig = 0;
						}
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmLyrics, &g_Config.m_QmLyrics) || Changed;
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsInMediaIsland, &g_Config.m_QmLyricsInMediaIsland) || Changed;
					if(g_Config.m_QmKugouHookEnable != 0)
						ConsumeQmHudRow(Content, Metrics); // 酷狗接入与恢复按钮
					if(g_Config.m_QmKugouHookEnable != 0 || g_Config.m_QmQQMusicHookEnable != 0)
						ConsumeQmHudRow(Content, Metrics); // 当前采集状态
					if(g_Config.m_QmSpotifyEnable != 0)
						ConsumeQmHudRow(Content, Metrics); // Spotify sp_dc 输入行(与渲染保持一致)
					return Changed;
				};
			case EQmModuleId::Background3D:
				return [pMenus, Metrics, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticles, &g_Config.m_Qm3DParticles);
					if(!g_Config.m_Qm3DParticles)
						return Changed;
					ConsumeQmHudRow(Content, Metrics); // particle type
					for(int Index = 0; Index < 9; ++Index)
						ConsumeQmHudRow(Content, Metrics); // numeric particle options
					ConsumeQmHudRow(Content, Metrics); // collision
					ConsumeQmHudRow(Content, Metrics); // push radius
					ConsumeQmHudRow(Content, Metrics); // push strength
					const SSettingsRadioRowLayout RadioLayout = ResolveSettingsRadioRowLayout(Content, 2, Metrics);
					ConsumeQmHudHeight(Content, RadioLayout.m_Height + LineSpacing);
					if(g_Config.m_Qm3DParticlesColorMode == 1)
						ConsumeQmHudRow(Content, Metrics); // custom color
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesGlow, &g_Config.m_Qm3DParticlesGlow) || Changed;
					if(g_Config.m_Qm3DParticlesGlow)
					{
						ConsumeQmHudRow(Content, Metrics);
						ConsumeQmHudRow(Content, Metrics);
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesTrail, &g_Config.m_Qm3DParticlesTrail) || Changed;
					if(g_Config.m_Qm3DParticlesTrail)
					{
						ConsumeQmHudRow(Content, Metrics);
						ConsumeQmHudRow(Content, Metrics);
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesPulse, &g_Config.m_Qm3DParticlesPulse) || Changed;
					if(g_Config.m_Qm3DParticlesPulse)
					{
						ConsumeQmHudRow(Content, Metrics);
						ConsumeQmHudRow(Content, Metrics); // pulse strength/speed are two rows
					}
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesTwinkle, &g_Config.m_Qm3DParticlesTwinkle) || Changed;
					return Changed;
				};
			case EQmModuleId::BindStatusHud:
				return [pMenus, LineHeight, LineSpacing](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusReset, &g_Config.m_ClShowhudKeyStatusReset);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusHammer, &g_Config.m_ClShowhudKeyStatusHammer) || Changed;
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusControl, &g_Config.m_ClShowhudKeyStatusControl) || Changed;
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusSync, &g_Config.m_ClShowhudKeyStatusSync) || Changed;
					return Changed;
				};
			case EQmModuleId::GoresDrownBoard:
				return [pMenus, Metrics](CUIRect Content) {
					bool Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmGoresDrownBoard, &g_Config.m_QmGoresDrownBoard);
					if(!g_Config.m_QmGoresDrownBoard)
						return Changed;
					ConsumeQmHudRow(Content, Metrics);
					ConsumeQmHudRow(Content, Metrics);
					Changed = qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(pMenus, Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmGoresDrownBoardShowTee, &g_Config.m_QmGoresDrownBoardShowTee) || Changed;
					return Changed;
				};
			default:
				return {};
			}
		}
	} // namespace

	const void *SwitchCountdownFollowTeeId()
	{
		return &s_SwitchCountdownFollowTeeId;
	}

	const void *SwitchCountdownMediaIslandId()
	{
		return &s_SwitchCountdownMediaIslandId;
	}

	void QmCardRenderHook::RenderQmHudDynamicIslandContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle)
	{
		pMenus->RenderQmHudDynamicIslandContent(Content, LineHeight, LineSpacing, OriginalStyle);
		if(!OriginalStyle)
			pMenus->RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandAlwaysShowTrackDetails, "Always show song title and artist", Localize("Always show song title and artist"), &g_Config.m_QmHudIslandAlwaysShowTrackDetails);
	}

	bool BuildHudCard(const SQmCardBuildContext &Ctx, const EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const float LineHeight = Metrics.m_LineHeight;
		const float BodySize = Metrics.m_BodySize;
		const float LineSpacing = Metrics.m_LineSpacing;
		const float LabelWidth = Ctx.m_LabelWidth;
		const bool ReadOnly = Ctx.m_ReadOnly;

		const auto Add = [&](const EQmModuleId ModuleId, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			const SSettingsContentMetrics MeasureMetrics = Metrics;
			MakeModuleCard(
				Ctx, ModuleId, pStableId, pTitle, pSubtitle, Render,
				[MeasureMetrics, ModuleId](float ContentWidth) { return MeasureHudCardHeight(MeasureMetrics, ModuleId, ContentWidth); },
				MeasureHudCardRevision(ModuleId),
				BuildHudPreLayoutInput(Ctx, ModuleId),
				Out);
		};

		switch(Id)
		{
		case EQmModuleId::DummyMiniView:
			Add(Id, "qm:dummy_miniview", "Dummy Window", "Show a small view of the dummy", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDummyMiniViewContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, g_Config.m_QmDummyMiniView != 0, ReadOnly); });
			return true;
		case EQmModuleId::Coords:
			Add(Id, "qm:coords", "Show Coordinates", "Show coordinates above players", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudCoordsContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::PlayerStats:
			Add(Id, "qm:player_stats", "Player data", "Player stats and info display", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudPlayerStatsContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DebugGraph:
			Add(Id, "qm:debug_graph", "Debug graph", "Debug performance graph panel", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDebugGraphContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DebugMode:
			Add(Id, "qm:debug_mode", "Debug mode", "Enable performance debug logging and diagnostics", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDebugModeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::InputOverlay:
			Add(Id, "qm:input_overlay", "Input overlay", "Input overlay display", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudInputOverlayContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::HudNotifications:
			Add(Id, "qm:hud_notifications", "Notifications", "Show important server prompts and Echo messages as popups", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) {
				qm_card_catalog::QmCardRenderHook::RenderQmHudNotificationsBasicContent(pMenus, Content, Metrics, LabelWidth, ReadOnly);
				if(g_Config.m_QmHudNotificationsShowAdvanced)
					qm_card_catalog::QmCardRenderHook::RenderQmHudNotificationsAdvancedContent(pMenus, Content, Metrics, LabelWidth, ReadOnly);
			});
			return true;
		case EQmModuleId::Voice:
			Add(Id, "qm:voice", "Voice", "Voice chat settings and diagnostics", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudVoiceContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::DynamicIsland:
			Add(Id, "qm:dynamic_island", "Dynamic Island", "HUD island appearance settings", [pMenus, LineHeight, LineSpacing](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudDynamicIslandContent(pMenus, Content, LineHeight, LineSpacing, g_Config.m_QmHudIslandUseOriginalStyle != 0); });
			return true;
		case EQmModuleId::SystemMediaControls:
			Add(Id, "qm:system_media_controls", "SMTC", "System media control", [pMenus, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudSystemMediaControlsContent(pMenus, Content, LineHeight, BodySize, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::Lyrics:
			Add(Id, "qm:lyrics", "Lyrics", "Lyrics sources and display", [pMenus, LineHeight, LineSpacing, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudLyricsContent(pMenus, Content, LineHeight, LineSpacing, ReadOnly); });
			return true;
		case EQmModuleId::Background3D:
			Add(Id, "qm:background_3d", "3D Background", "Configure background 3D particle effects", [pMenus, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudBackground3DContent(pMenus, Content, Metrics, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::BindStatusHud:
			Add(Id, "qm:bind_status_hud", "DDRace HUD Pro", "Dummy key/hammer/control/copy status switches", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudBindStatusContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		case EQmModuleId::GoresDrownBoard:
			Add(Id, "qm:gores_drown_board", "Gores drown board", "Team drown death leaderboard", [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { qm_card_catalog::QmCardRenderHook::RenderQmHudGoresDrownBoardContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
			return true;
		default:
			return false;
		}
	}
} // namespace qm_card_catalog

void CMenus::RenderQmHudInputOverlayContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmInputOverlay, "Show inputs", Localize("Show inputs"), &g_Config.m_QmInputOverlay);
	if(!g_Config.m_QmInputOverlay)
		return;

	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmInputOverlayKeyCounts, "Show key press counts", Localize("Show key press counts"), &g_Config.m_QmInputOverlayKeyCounts);
	CUIRect ResetRow;
	Content.HSplitTop(LineHeight, &ResetRow, &Content);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	static CButtonContainer s_ResetKeyCounts;
	if(DoButton_Menu(&s_ResetKeyCounts, Localize("Reset key press counts"), 0, &ResetRow) && !PrewarmOnly)
		GameClient()->m_InputOverlay.ResetKeyCounts();

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmInputOverlayScaleInputId;
	static int s_QmInputOverlayMouseScaleInputId;
	static int s_QmInputOverlayOpacityInputId;
	static int s_QmInputOverlayPosXInputId;
	static int s_QmInputOverlayPosYInputId;
	RenderValue("qmclient-input-overlay-keyboard-size", Localizable("Keyboard size"), &s_QmInputOverlayScaleInputId, &g_Config.m_QmInputOverlayScale, 1, 200);
	RenderValue("qmclient-input-overlay-mouse-size", Localizable("Mouse size"), &s_QmInputOverlayMouseScaleInputId, &g_Config.m_QmInputOverlayMouseScale, 1, 200);
	RenderValue("qmclient-input-overlay-opacity", Localizable("Opacity"), &s_QmInputOverlayOpacityInputId, &g_Config.m_QmInputOverlayOpacity, 0, 100);
	RenderValue("qmclient-input-overlay-horizontal-position", Localizable("Horizontal position"), &s_QmInputOverlayPosXInputId, &g_Config.m_QmInputOverlayPosX, 0, 100);
	RenderValue("qmclient-input-overlay-vertical-position", Localizable("Vertical position"), &s_QmInputOverlayPosYInputId, &g_Config.m_QmInputOverlayPosY, 0, 100);
	static int s_CountSize;
	RenderValue("qmclient-input-overlay-count-size", Localizable("Key press count font size"), &s_CountSize, &g_Config.m_QmInputOverlayCountSize, 50, 200);
	static CButtonContainer s_CountColor;
	DoLine_ColorPicker(&s_CountColor, Metrics, &Content, Localize("Key press count color"), &g_Config.m_QmInputOverlayCountColor,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmInputOverlayCountColor, true)), false, nullptr, true);
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmHudLabel("qmclient-input-overlay-count-label", &LabelColumn, Localize("Key press count label"), BodySize);
	static CLineInput s_CountLabel(g_Config.m_QmInputOverlayCountLabel, sizeof(g_Config.m_QmInputOverlayCountLabel));
	ui_widget::InputField(SettingsUiContext("settings_input_overlay_count_label", Metrics.m_UiScale), &s_CountLabel, ControlColumn, "", BodySize);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	static CButtonContainer s_CountAppearanceReset;
	if(DoButton_Menu(&s_CountAppearanceReset, Localize("Reset count appearance"), 0, &Row) && !PrewarmOnly && !Ui()->RenderOnly())
	{
		g_Config.m_QmInputOverlayCountColor = DefaultConfig::QmInputOverlayCountColor;
		g_Config.m_QmInputOverlayCountSize = DefaultConfig::QmInputOverlayCountSize;
		str_copy(g_Config.m_QmInputOverlayCountLabel, DefaultConfig::QmInputOverlayCountLabel);
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudPlayerStatsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsHud, "Show player stats HUD", Localize("Show player stats HUD"), &g_Config.m_QmPlayerStatsHud);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgress, "Map progress bar", Localize("Map progress bar"), &g_Config.m_QmPlayerStatsMapProgress);
	if(g_Config.m_QmPlayerStatsMapProgress)
	{
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressStyle, "Use embedded HUD progress bar", Localize("Use embedded HUD progress bar"), &g_Config.m_QmPlayerStatsMapProgressStyle);
		if(g_Config.m_QmPlayerStatsMapProgressStyle == 0)
		{
			static CButtonContainer s_MapProgressColorId;
			DoLine_ColorPicker(&s_MapProgressColorId, CurrentSettingsContentMetrics(), &Content, Localize("Progress bar color"), &g_Config.m_QmPlayerStatsMapProgressColor, ColorRGBA(36.0f / 255.0f, 199.0f / 255.0f, 100.0f / 255.0f, 1.0f), false, nullptr, true);
			CUIRect Row, LabelColumn, ControlColumn;
			auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
				Content.HSplitTop(LineHeight, &Row, &Content);
				Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
				RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
				RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
				Content.HSplitTop(LineSpacing, nullptr, &Content);
			};
			static int s_QmPlayerStatsMapProgressWidthInputId;
			static int s_QmPlayerStatsMapProgressHeightInputId;
			static int s_QmPlayerStatsMapProgressPosXInputId;
			static int s_QmPlayerStatsMapProgressPosYInputId;
			RenderValue("qmclient-player-data-progress-bar-width", Localizable("Progress bar width"), &s_QmPlayerStatsMapProgressWidthInputId, &g_Config.m_QmPlayerStatsMapProgressWidth, 10, 80);
			RenderValue("qmclient-player-data-progress-bar-height", Localizable("Progress bar height"), &s_QmPlayerStatsMapProgressHeightInputId, &g_Config.m_QmPlayerStatsMapProgressHeight, 6, 30);
			RenderValue("qmclient-player-data-horizontal-position", Localizable("Horizontal position"), &s_QmPlayerStatsMapProgressPosXInputId, &g_Config.m_QmPlayerStatsMapProgressPosX, 0, 100, "%");
			RenderValue("qmclient-player-data-vertical-position", Localizable("Vertical position"), &s_QmPlayerStatsMapProgressPosYInputId, &g_Config.m_QmPlayerStatsMapProgressPosY, 0, 100, "%");
		}
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressDbgRoute, "Show dotted map route debug", Localize("Show dotted map route debug"), &g_Config.m_QmPlayerStatsMapProgressDbgRoute);
	}
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsResetOnJoin, "Reset stats when joining a server", Localize("Reset stats when joining a server"), &g_Config.m_QmPlayerStatsResetOnJoin);
}
