#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDMEASUREREVISION_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDMEASUREREVISION_H

#include "QmCardCatalog.h"

#include <engine/shared/config.h>

#include <algorithm>

namespace qm_card_catalog
{
	// 分类页、搜索页和卡片定义共用一份高度依赖，避免页面缓存挡住目录中的重测版本。
	inline uint64_t MeasureModuleCardRevision(const qm_module::EQmModuleId Id, const SQmFunctionCardLayoutState &Layout = {})
	{
		using qm_module::EQmModuleId;
		const bool DummyMiniViewExpanded = g_Config.m_QmDummyMiniView != 0;
		const bool DynamicIslandOriginalStyle = g_Config.m_QmHudIslandUseOriginalStyle != 0;
		switch(Id)
		{
		case EQmModuleId::GoresActor:
			return g_Config.m_QmFreezeChatEnabled ? 1u | (g_Config.m_QmFreezeChatEmoticon ? 2u : 0u) : 0u;
		case EQmModuleId::Gores:
			return (g_Config.m_QmAxiomAutoLogin ? 1u : 0u) |
			       ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 2u : 0u);
		case EQmModuleId::Emoticons:
			return (g_Config.m_QmShowOtherSuperEmotes ? 1u : 0u) |
			       (g_Config.m_QmShowOtherLaunchEmotes ? 2u : 0u);
		case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory != 0 ? 1u : 0u;
		case EQmModuleId::FriendNotify:
			return (g_Config.m_QmFriendOnlineAutoRefresh ? 1u : 0u) |
			       (g_Config.m_QmFriendEnterBroadcast ? 2u : 0u) |
			       (g_Config.m_QmFriendEnterAutoGreet ? 4u : 0u);
		case EQmModuleId::BlockWords: return Layout.m_BlockWordsRevision * 2u + (g_Config.m_QmBlockWordsAction == 0 ? 0u : 1u);
		case EQmModuleId::Translate:
		{
			uint64_t Revision = 0;
			if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
				Revision = 1u;
			else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
				Revision = 2u;
			else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
				Revision = 3u;
			else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
				Revision = 4u;
			// mymemory 与 deepl 各有专属说明行/密钥行，切换后端即改变内容高度；
			// 版本必须两两不同，否则 Deck 的测量缓存不失效，卡片高度不随服务切换更新。
			else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "mymemory") == 0)
				Revision = 5u;
			else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0)
				Revision = 6u;
			else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "baidu") == 0)
				Revision = 7u;
			// 高级选项会增删多行控件，provider 还会影响端点和提示文案的可见性。
			Revision |= static_cast<uint64_t>(g_Config.m_QmTranslateShowAdvanced != 0) << 8;
			Revision |= static_cast<uint64_t>(std::clamp(g_Config.m_QmTranslateLlmProvider, 0, 15)) << 9;
			Revision |= static_cast<uint64_t>(g_Config.m_QmTranslateLlmEnableThinking != 0) << 13;
			return Revision;
		}
		case EQmModuleId::QiaFen: return Layout.m_KeywordRulesRevision;
		case EQmModuleId::PieMenu: return g_Config.m_QmPieMenuEnabled ? 1u : 0u;
		case EQmModuleId::FavoriteMaps: return Layout.m_FavoriteMapsRevision;
		case EQmModuleId::MapUpload: return 1u;
		case EQmModuleId::HJAssist: return (g_Config.m_QmAutoTeamLock ? 1u : 0u) | (g_Config.m_QmPausedSpectatorFade ? 2u : 0u);
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
		case EQmModuleId::DummyMiniView: return DummyMiniViewExpanded ? 1u : 0u;
		case EQmModuleId::Ime: return (g_Config.m_QmNewIme != 0 ? 1u : 0u) | (g_Config.m_QmImeAutoManage != 0 ? 2u : 0u);
		case EQmModuleId::PlayerStats: return (g_Config.m_QmPlayerStatsMapProgress ? 1u : 0u) | (g_Config.m_QmPlayerStatsMapProgressStyle ? 2u : 0u);
		case EQmModuleId::InputOverlay: return g_Config.m_QmInputOverlay ? 1u : 0u;
		case EQmModuleId::Tooltip: return static_cast<uint64_t>(g_Config.m_QmTooltipFontSize);
		case EQmModuleId::HudNotifications:
		{
			uint64_t Revision = g_Config.m_QmHudNotificationsShowAdvanced ? 1u : 0u;
			if(g_Config.m_QmHudNotificationsShowAdvanced && g_Config.m_QmHudNotificationsUseCategoryFilters)
				Revision |= 2u;
			return Revision;
		}
		case EQmModuleId::Voice: return ResolveQmHudVoiceRevision(g_Config.m_QmVoiceEnable != 0, g_Config.m_QmVoiceShowAdvanced != 0, g_Config.m_QmVoiceShowConnectionStatus != 0, g_Config.m_QmVoiceNoiseSuppressEnable, g_Config.m_QmVoiceVadEnable != 0, g_Config.m_QmVoiceStereo != 0);
		case EQmModuleId::DynamicIsland: return (DynamicIslandOriginalStyle ? 1u : 0u) | (g_Config.m_QmSwitchCountdown ? 2u : 0u);
		case EQmModuleId::SystemMediaControls: return g_Config.m_QmSmtcEnable ? 1u : 0u;
		case EQmModuleId::Lyrics: return (g_Config.m_QmSpotifyEnable ? 1u : 0u) | (g_Config.m_QmKugouHookEnable ? 2u : 0u) | (g_Config.m_QmQQMusicHookEnable ? 4u : 0u); // 来源附加行影响布局高度
		case EQmModuleId::Background3D: return ResolveQmHudBackground3DRevision(g_Config.m_Qm3DParticles != 0, g_Config.m_Qm3DParticlesColorMode == 1, g_Config.m_Qm3DParticlesGlow != 0, g_Config.m_Qm3DParticlesTrail != 0, g_Config.m_Qm3DParticlesPulse != 0, g_Config.m_Qm3DParticlesTwinkle != 0);
		case EQmModuleId::GoresDrownBoard: return g_Config.m_QmGoresDrownBoard != 0 ? 1u : 0u;
		case EQmModuleId::SkinTransition: return g_Config.m_QmSkinChangeTransition ? 1u : 0u;
		default: return 0u;
		}
	}

	inline uint64_t MeasureModuleCardsRevision(const SQmFunctionCardLayoutState &Layout = {})
	{
		uint64_t Revision = 0;
		for(int Index = 0; Index < static_cast<int>(qm_module::QmModuleCount); ++Index)
			Revision = Revision * 1099511628211ULL ^ MeasureModuleCardRevision(static_cast<qm_module::EQmModuleId>(Index), Layout);
		return Revision;
	}
} // namespace qm_card_catalog

#endif // GAME_CLIENT_QMUI_CARDS_QMCARDMEASUREREVISION_H
