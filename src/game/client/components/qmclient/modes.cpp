// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "modes.h"

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/video.h>

#include <generated/protocol.h>

#include <algorithm>
#include <limits>

static bool QmTextContainsNoCase(const char *pText, const char *pNeedle)
{
	return pText && pText[0] != '\0' && pNeedle && pNeedle[0] != '\0' && str_find_nocase(pText, pNeedle) != nullptr;
}

static bool QmTextEqualsNoCase(const char *pText, const char *pExpected)
{
	return pText && pExpected && str_comp_nocase(pText, pExpected) == 0;
}

int ApplyQmConfigOverride(SQmConfigOverrideState &State, bool HideActive, int CurrentValue, int HiddenValue, bool &Changed)
{
	Changed = false;
	if(HideActive)
	{
		if(!State.m_WasActive)
		{
			State.m_WasActive = true;
			State.m_SavedValue = CurrentValue;
			State.m_AutoChangedValue = false;
			if(CurrentValue != HiddenValue)
			{
				Changed = true;
				State.m_AutoChangedValue = true;
				return HiddenValue;
			}
		}
		else if(State.m_AutoChangedValue && CurrentValue != HiddenValue)
		{
			// 用户已改过自动写入的值，离开自动状态时不再恢复旧值。
			State.m_AutoChangedValue = false;
		}
		return CurrentValue;
	}
	if(State.m_WasActive)
	{
		State.m_WasActive = false;
		if(State.m_AutoChangedValue && CurrentValue == HiddenValue)
		{
			Changed = true;
			State.m_AutoChangedValue = false;
			return State.m_SavedValue;
		}
		State.m_AutoChangedValue = false;
	}
	else
	{
		State.m_SavedValue = CurrentValue;
	}
	return CurrentValue;
}

int ResetQmConfigOverride(SQmFocusConfigOverrideState &State, int CurrentValue, int OverrideValue, bool &Changed)
{
	Changed = State.m_WasActive && State.m_AutoChangedValue && CurrentValue == OverrideValue;
	if(Changed)
		CurrentValue = State.m_SavedValue;
	State = {};
	return CurrentValue;
}

int ApplyQmGoresLinkedConfig(SQmFocusConfigOverrideState &State, bool GoresActive, bool AutoToggle, int CurrentValue, bool &Changed)
{
	// Gores 只在进入时临时开启快速输入；离开或取消联动时恢复自动改动前的值。
	return ApplyQmFocusConfigOverride(State, GoresActive && AutoToggle, CurrentValue, 1, Changed);
}

int ApplyQmGoresAutoEnableConfig(SQmFocusConfigOverrideState &State, bool GameModeEntered, bool GameModeLeft, bool AutoEnable, int CurrentValue, bool &Changed)
{
	Changed = false;
	if(GameModeEntered && AutoEnable && CurrentValue == 0)
	{
		State.m_WasActive = true;
		State.m_SavedValue = CurrentValue;
		State.m_AutoChangedValue = true;
		State.m_LastValue = 1;
		Changed = true;
		return 1;
	}
	if(State.m_WasActive && State.m_AutoChangedValue && CurrentValue != State.m_LastValue)
		State.m_AutoChangedValue = false;
	State.m_LastValue = CurrentValue;
	if(GameModeLeft)
	{
		// 自动启用是位置相关的一次性语义：只要本次进入是自动启用所致，离开 Gores 服务器
		// 一律恢复进入前的值——期间用户手动开/关不阻止自动关（手动关时恢复值本就是关）。
		const bool Restore = State.m_WasActive;
		if(Restore)
		{
			Changed = true;
			CurrentValue = State.m_SavedValue;
		}
		State = {};
	}
	return CurrentValue;
}

int ApplyQmGoresDummyHammerConfig(SQmFocusConfigOverrideState &State, bool ModeActivated, bool ModeDeactivated, bool DisableRequested, int CurrentValue, bool &Changed)
{
	// 进入 Gores 模式且选项允许时一次性关闭分身锤；期间用户手动改值即释放接管；
	// 退出 Gores 模式时若接管仍在则恢复进入前的值。不持续强制，避免用户重新打开的分身锤被反复压回 0。
	Changed = false;
	if(ModeActivated && DisableRequested && CurrentValue != 0)
	{
		State.m_WasActive = true;
		State.m_SavedValue = CurrentValue;
		State.m_AutoChangedValue = true;
		State.m_LastValue = 0;
		Changed = true;
		return 0;
	}
	if(State.m_WasActive && State.m_AutoChangedValue && CurrentValue != State.m_LastValue)
		State.m_AutoChangedValue = false;
	State.m_LastValue = CurrentValue;
	if(ModeDeactivated && State.m_WasActive && State.m_AutoChangedValue && CurrentValue == 0)
	{
		State.m_AutoChangedValue = false;
		Changed = true;
		return State.m_SavedValue;
	}
	return CurrentValue;
}

bool ShouldKeepQmGoresHammerInFreeze(bool GoresCycleActive, bool InFreeze, bool HammerRequested)
{
	return GoresCycleActive && InFreeze && HammerRequested;
}

bool ShouldTriggerQmGoresHammerWakeup(bool GoresCycleActive, bool HammerRequested, bool ExternalHammerWakeup)
{
	return GoresCycleActive && HammerRequested && ExternalHammerWakeup;
}

int QmGoresHammerWakeupFireState(int CurrentFire)
{
	return ((CurrentFire + 1) | 1) & INPUT_STATE_MASK;
}

bool ShouldReleaseQmGoresHammerWakeupFire(bool PendingRelease, int CurrentFire)
{
	return PendingRelease && (CurrentFire & 1) != 0;
}

int QmGoresHammerWakeupReleaseFireState(int CurrentFire)
{
	return ((CurrentFire + 1) & ~1) & INPUT_STATE_MASK;
}

int GoresRestoreWeaponAfterHammer(int PreHammerWeapon, bool HasPreHammerWeapon)
{
	return HasPreHammerWeapon ? PreHammerWeapon : WEAPON_GUN;
}

bool ShouldPulseGoresHammerOnFire(bool GoresCycleActive, bool FireJustPressed, bool CurrentWeaponIsHammer, bool FreezeWakeupActive)
{
	return GoresCycleActive && FireJustPressed && !CurrentWeaponIsHammer && !FreezeWakeupActive;
}

bool ShouldRestoreGoresWeaponAfterHammer(bool CurrentWeaponIsHammer, bool HasPreHammerWeapon)
{
	return CurrentWeaponIsHammer && HasPreHammerWeapon;
}

bool ShouldShowQmHookStrongWeakScope(int Scope, bool Self, bool Strong, bool Weak)
{
	switch(Scope)
	{
	case QM_HOOK_STRONG_WEAK_SCOPE_SELF:
		return Self;
	case QM_HOOK_STRONG_WEAK_SCOPE_OTHERS:
		return !Self;
	case QM_HOOK_STRONG_WEAK_SCOPE_STRONG:
		return Strong;
	case QM_HOOK_STRONG_WEAK_SCOPE_WEAK:
		return Weak;
	case QM_HOOK_STRONG_WEAK_SCOPE_ALL:
		return true;
	default:
		return false;
	}
}

bool ShouldShowQmNameplateName(int Scope, bool IsCurrentChar, bool IsLocalClient)
{
	// 当前操控角色：只有 IsCurrentChar 那一个。
	if(IsCurrentChar)
		return Scope == QM_NAMEPLATE_SHOW_SCOPE_CURRENT || Scope == QM_NAMEPLATE_SHOW_SCOPE_LOCAL || Scope == QM_NAMEPLATE_SHOW_SCOPE_ALL;
	// 本机其他角色（分身）：本机但不是当前操控角色。
	if(IsLocalClient)
		return Scope == QM_NAMEPLATE_SHOW_SCOPE_LOCAL || Scope == QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL || Scope == QM_NAMEPLATE_SHOW_SCOPE_ALL;
	// 其他玩家：既不是本机、也不是当前操控角色。
	return Scope == QM_NAMEPLATE_SHOW_SCOPE_OTHERS || Scope == QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL || Scope == QM_NAMEPLATE_SHOW_SCOPE_ALL;
}

int QmNameplateShowScopeFromLegacyFlags(bool ShowOthers, bool ShowOwn)
{
	if(ShowOwn && ShowOthers)
		return QM_NAMEPLATE_SHOW_SCOPE_ALL;
	if(ShowOwn)
		return QM_NAMEPLATE_SHOW_SCOPE_LOCAL;
	if(ShowOthers)
		return QM_NAMEPLATE_SHOW_SCOPE_OTHERS;
	return QM_NAMEPLATE_SHOW_SCOPE_OFF;
}

static bool ShouldUseQmNameplateTextPlayingScope(int Scope, bool Self, bool Friend)
{
	switch(Scope)
	{
	case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF:
		return false;
	case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF:
		return Self;
	case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OTHERS:
		return !Self;
	case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_FRIENDS:
		return Friend;
	case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF_FRIENDS:
		return Self || Friend;
	case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_ALL:
		return true;
	default:
		return false;
	}
}

static bool ShouldUseQmNameplateTextSpectateScope(int Scope, bool Friend, bool SpectateTarget)
{
	switch(Scope)
	{
	case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF:
		return false;
	case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_TARGET:
		return SpectateTarget;
	case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OTHERS:
		return !SpectateTarget;
	case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_FRIENDS:
		return Friend;
	case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_TARGET_FRIENDS:
		return SpectateTarget || Friend;
	case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_ALL:
		return true;
	default:
		return false;
	}
}

bool ShouldUseQmNameplateTextEffects(int PlayingScope, int SpectateScope, int DemoMode, int DemoTarget, bool DemoPlayback, bool Spectating, bool Self, bool Friend, bool SpectateTarget, int ClientId)
{
	if(DemoPlayback)
	{
		switch(DemoMode)
		{
		case QM_NAMEPLATE_TEXT_DEMO_MODE_OFF:
			return false;
		case QM_NAMEPLATE_TEXT_DEMO_MODE_SMART:
			return SpectateTarget;
		case QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_TARGET:
			return DemoTarget >= 0 && ClientId == DemoTarget;
		case QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_SCOPE:
			return ShouldUseQmNameplateTextPlayingScope(PlayingScope, Self, Friend);
		default:
			return false;
		}
	}
	if(Spectating)
		return ShouldUseQmNameplateTextSpectateScope(SpectateScope, Friend, SpectateTarget);
	return ShouldUseQmNameplateTextPlayingScope(PlayingScope, Self, Friend);
}

bool ShouldHideGoresGuide(bool GoresEnabled, bool HideGuidesEnabled, bool ManualGuideVisible)
{
	return GoresEnabled && HideGuidesEnabled && !ManualGuideVisible;
}

bool ShouldRenderGoresDebugRoute(bool Online, bool DebugRouteEnabled, bool GoresMapProgressEnabled)
{
	return Online && DebugRouteEnabled && GoresMapProgressEnabled;
}

bool ShouldEnableQmMovingWaterTiles(const char *pGameInfoGameType, const char *pServerInfoGameType, const char *pCommunityId, const char *pCommunityName)
{
	return QmTextContainsNoCase(pGameInfoGameType, "gores") ||
	       QmTextContainsNoCase(pServerInfoGameType, "gores") ||
	       QmTextContainsNoCase(pCommunityId, "axiom") ||
	       QmTextContainsNoCase(pCommunityName, "axiom");
}

bool ServerPrefersTeeMenuSkin(const char *pGameInfoGameType, const char *pServerInfoGameType, const char *pCommunityId, const char *pCommunityName)
{
	return QmTextEqualsNoCase(pGameInfoGameType, "DDRaceNetwork") ||
	       QmTextEqualsNoCase(pGameInfoGameType, "DDNet") ||
	       QmTextEqualsNoCase(pServerInfoGameType, "DDRaceNetwork") ||
	       QmTextEqualsNoCase(pServerInfoGameType, "DDNet") ||
	       QmTextContainsNoCase(pCommunityId, "axiom") ||
	       QmTextContainsNoCase(pCommunityName, "axiom");
}

bool ShouldUseServerControlledLocalSkin(const char *pGameInfoGameType, const char *pServerInfoGameType, const char *pCommunityId, const char *pCommunityName)
{
	// 该判据只回答「服务器是否希望用服务器端下发的皮肤」，与协议版本无关。
	// 协议版本由调用方用 Client()->IsSixup() 单独判定，见 EServerSkinProtocol。
	return !ServerPrefersTeeMenuSkin(pGameInfoGameType, pServerInfoGameType, pCommunityId, pCommunityName);
}

EServerSkinProtocol ResolveServerSkinProtocol(bool Sixup, bool UseServerControlledSkin, bool LocalClientHasServerSkin)
{
	// 皮肤部位只能来自当前连接实际使用的协议：0.6 连接永远不能取 0.7 皮肤部件，
	// 反之亦然。此前仅凭服务器白名单决定，导致 0.6 服务器被渲染成 0.7 皮肤。
	if(Sixup)
	{
		// 0.7 连接只有拿到服务器下发的皮肤部件时才使用七部位皮肤。
		return UseServerControlledSkin && LocalClientHasServerSkin ? EServerSkinProtocol::SEVEN : EServerSkinProtocol::NONE;
	}
	return EServerSkinProtocol::SIX;
}

int ResolveLocalSkinConfigIndex(bool DemoPlayback, int ClientId, int MainClientId, int DummyClientId)
{
	// Demo snapshots already contain the recorded appearance and must not use current local skin settings.
	if(DemoPlayback || ClientId < 0)
		return -1;
	if(ClientId == MainClientId)
		return 0;
	if(ClientId == DummyClientId)
		return 1;
	return -1;
}

bool ConsumeQmBudgetedWork(int &Cursor, int Total, int Budget)
{
	if(Cursor >= Total)
		return false;
	if(Budget <= 0)
		return true;
	Cursor = std::min(Total, Cursor + Budget);
	return Cursor < Total;
}

bool QmStatisticsShouldShowAxiomGores(bool HasLocalAxiomGores, bool IsCurrentAxiomCommunity, bool HasAxiomResult)
{
	return HasLocalAxiomGores || IsCurrentAxiomCommunity || HasAxiomResult;
}

SQmStatisticsModeDisplay ResolveQmStatisticsModeDisplay(int LocalMaps, int64_t LocalPlaytimeSeconds, bool IsAxiomGores, bool HasAxiomStats, int64_t AxiomMaps, int64_t AxiomPlaytimeSeconds, bool IsDdnet, int DdnetFinishes, int64_t DdnetPlaytimeHours)
{
	SQmStatisticsModeDisplay Display;
	Display.m_Maps = std::max(0, LocalMaps);
	Display.m_PlaytimeSeconds = std::max<int64_t>(0, LocalPlaytimeSeconds);
	if(IsAxiomGores && HasAxiomStats)
	{
		Display.m_Maps = AxiomMaps > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : (int)std::max<int64_t>(0, AxiomMaps);
		Display.m_PlaytimeSeconds = std::max<int64_t>(0, AxiomPlaytimeSeconds);
	}
	else if(IsDdnet)
	{
		if(DdnetFinishes >= 0)
			Display.m_Maps = DdnetFinishes;
		// 官方 DDNet 统计的游玩小时数换算成秒，作为该模式的真实时长。
		// 本地统计只记录本机游玩，与官方账号口径不一致，有官方数据时优先用官方。
		if(DdnetPlaytimeHours >= 0)
		{
			constexpr int64_t SECONDS_PER_HOUR = 3600;
			if(DdnetPlaytimeHours > std::numeric_limits<int64_t>::max() / SECONDS_PER_HOUR)
				Display.m_PlaytimeSeconds = std::numeric_limits<int64_t>::max();
			else
				Display.m_PlaytimeSeconds = DdnetPlaytimeHours * SECONDS_PER_HOUR;
		}
	}
	return Display;
}

int64_t QmStatisticsChartWeight(int Maps, int64_t PlaytimeSeconds, bool UseMaps)
{
	return UseMaps ? std::max(0, Maps) : std::max<int64_t>(0, PlaytimeSeconds);
}

bool ShouldRenderFocusSpectatorHud(bool SpectatorActive, bool SpectatorHudEnabled, bool MainHudVisible, bool HideHud)
{
	// 禅模式隐藏主 HUD 时旁观者 HUD 仍然保留：调用方传入已合并总开关的判定。
	return SpectatorActive && SpectatorHudEnabled && HideHud;
}

bool ShouldRenderMapProgressBar(bool MapProgressEnabled, int MapProgressStyle, bool PlayerStatsHudEnabled, bool GoresMapProgressEnabled)
{
	return MapProgressEnabled && !(MapProgressStyle != 0 && PlayerStatsHudEnabled) && GoresMapProgressEnabled;
}

bool ShouldHideFocusGuideLines(bool FocusActive, bool HideGuideLines)
{
	return FocusActive && HideGuideLines;
}

bool ShouldRenderFocusFilteredChatLine(bool FocusHidePlayerMessages, bool FocusHideSystemInfoMessages, bool FocusHideSystemPromptMessages, bool FocusHideEcho, int ClientId, bool ForceVisible, bool ServerMessageIsBasicInfo)
{
	if(ForceVisible)
		return true;
	if(ClientId == -2)
		return !FocusHideEcho;
	if(ClientId == -1)
		return ServerMessageIsBasicInfo ? !FocusHideSystemInfoMessages : !FocusHideSystemPromptMessages;
	if(ClientId >= 0)
		return !FocusHidePlayerMessages;
	if(FocusHideSystemPromptMessages)
		return false;
	return true;
}

bool ShouldRenderAnyFocusFilteredChat(bool FocusHidePlayerMessages, bool FocusHideSystemInfoMessages, bool FocusHideSystemPromptMessages, bool FocusHideEcho, bool HasForceVisibleLine)
{
	return !(FocusHidePlayerMessages && FocusHideSystemInfoMessages && FocusHideSystemPromptMessages && FocusHideEcho) || HasForceVisibleLine;
}

SQmFocusModeConfig QmReadFocusModeConfig(const CConfig &Config)
{
	SQmFocusModeConfig Focus;
	Focus.m_FocusActive = Config.m_QmFocusMode != 0;
	Focus.m_HideHud = Config.m_QmFocusModeHideHud != 0;
	Focus.m_HideMapProgress = Config.m_QmFocusModeHideMapProgress != 0;
	Focus.m_HideInfoMessages = Config.m_QmFocusModeHideInfoMessages != 0;
	Focus.m_HideScoreboard = Config.m_QmFocusModeHideScoreboard != 0;
	Focus.m_HideNames = Config.m_QmFocusModeHideNames != 0;
	Focus.m_HideNameplates = Config.m_QmFocusModeHideNameplates != 0;
	Focus.m_HideDirectionIndicators = Config.m_QmFocusModeHideDirectionIndicators != 0;
	Focus.m_HideGuideLines = Config.m_QmFocusModeHideGuideLines != 0;
	Focus.m_HideJumpEffects = Config.m_QmFocusModeHideJumpEffects != 0;
	Focus.m_HideKillEffects = Config.m_QmFocusModeHideKillEffects != 0;
	Focus.m_HideExplosionEffects = Config.m_QmFocusModeHideExplosionEffects != 0;
	Focus.m_HideFreezeEffects = Config.m_QmFocusModeHideFreezeEffects != 0;
	Focus.m_HideHammerEffects = Config.m_QmFocusModeHideHammerEffects != 0;
	Focus.m_HideMuzzleEffects = Config.m_QmFocusModeHideMuzzleEffects != 0;
	Focus.m_MuteJumpSounds = Config.m_QmFocusModeMuteJumpSounds != 0;
	Focus.m_MuteDeathSounds = Config.m_QmFocusModeMuteDeathSounds != 0;
	Focus.m_MuteHammerSounds = Config.m_QmFocusModeMuteHammerSounds != 0;
	Focus.m_HidePlayerMessages = Config.m_QmFocusModeHideChat != 0;
	Focus.m_HideSystemInfoMessages = Config.m_QmFocusModeHideSystemInfoMessages != 0;
	Focus.m_HideSystemPromptMessages = Config.m_QmFocusModeHideSystemMessages != 0;
	Focus.m_HideEchoMessages = Config.m_QmFocusModeHideEcho != 0;
	Focus.m_SoundEnabled = Config.m_SndGame != 0;
	return Focus;
}

SQmFocusModeDecisions GetQmFocusModeDecisions(const SQmFocusModeConfig &Config)
{
	// 录制视频期间禅模式的一切效果都不进视频：按总开关关闭处理。实时画面不受影响，
	// 配置接管也照常生效，录制路径另行读取用户真实值（见 nameplates.cpp 的 RealValue）。
	const bool FocusActive = Config.m_FocusActive && !Config.m_VideoRecording;
	SQmFocusModeDecisions Decisions;
	Decisions.m_FocusActive = FocusActive;
	Decisions.m_HideHud = FocusActive && Config.m_HideHud;
	Decisions.m_HideMapProgress = FocusActive && Config.m_HideMapProgress;
	Decisions.m_HideScoreboard = FocusActive && Config.m_HideScoreboard;
	Decisions.m_HideNames = FocusActive && Config.m_HideNames;
	Decisions.m_HideNameplates = FocusActive && Config.m_HideNameplates;
	Decisions.m_HideInfoMessages = FocusActive && Config.m_HideInfoMessages;
	Decisions.m_HideDirectionIndicators = FocusActive && Config.m_HideDirectionIndicators;
	Decisions.m_HideGuideLines = FocusActive && Config.m_HideGuideLines;
	Decisions.m_HideKillEffects = FocusActive && Config.m_HideKillEffects;
	Decisions.m_HideExplosionEffects = FocusActive && Config.m_HideExplosionEffects;
	Decisions.m_HideFreezeEffects = FocusActive && Config.m_HideFreezeEffects;
	Decisions.m_HideHammerEffects = FocusActive && Config.m_HideHammerEffects;
	Decisions.m_HideMuzzleEffects = FocusActive && Config.m_HideMuzzleEffects;
	Decisions.m_MuteDeathSounds = FocusActive && Config.m_MuteDeathSounds;
	Decisions.m_MuteHammerSounds = FocusActive && Config.m_MuteHammerSounds;
	Decisions.m_PlayDeathOrSpawnSound = Config.m_SoundEnabled && !Decisions.m_MuteDeathSounds;
	Decisions.m_AirJump.m_SpawnParticles = !(FocusActive && Config.m_HideJumpEffects);
	Decisions.m_AirJump.m_PlaySound = Config.m_SoundEnabled && !(FocusActive && Config.m_MuteJumpSounds);
	Decisions.m_HidePlayerMessages = FocusActive && Config.m_HidePlayerMessages;
	Decisions.m_HideSystemInfoMessages = FocusActive && Config.m_HideSystemInfoMessages;
	Decisions.m_HideSystemPromptMessages = FocusActive && Config.m_HideSystemPromptMessages;
	Decisions.m_HideEchoMessages = FocusActive && Config.m_HideEchoMessages;
	return Decisions;
}

SQmFocusModeDecisions GetQmFocusModeDecisions()
{
	SQmFocusModeConfig Config = QmReadFocusModeConfig(g_Config);
#if defined(CONF_VIDEORECORDER)
	Config.m_VideoRecording = IVideo::Current() != nullptr;
#endif
	return GetQmFocusModeDecisions(Config);
}
