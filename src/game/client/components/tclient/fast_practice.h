/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_FAST_PRACTICE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_FAST_PRACTICE_H

#include <base/math.h>

#include <engine/client/enums.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/local_world.h>
#include <game/client/components/qmclient/practice_hook_input.h>
#include <game/client/components/qmclient/practice_input_lock.h>
#include <game/client/components/qmclient/practice_predict_plan.h>
#include <game/client/components/qmclient/practice_recorder.h>
#include <game/client/components/qmclient/practice_rescue.h>
#include <game/client/prediction/gameworld.h>
#include <game/gamecore.h>

#include <array>
#include <string>
#include <vector>

class CFastPractice : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	bool Enabled() const { return m_Enabled; }
	bool CanEnable() const;
	void Toggle(bool PreserveHeldInput = false);
	void Enable(bool PreserveHeldInput = false);
	void Disable();
	bool ConsumeKillCommand();
	void PrepareForSpectating();
	bool ConsumePracticeChatCommand(int Team, const char *pLine);
	void ResetPracticeToAnchor();
	// 把练习分支世界恢复到启用练习那一刻的快照（记录保留，只切段）。
	bool RestartPracticeFromStart();
	// 重生点：显式保存/回溯当前 tee 状态，与 /r 的救援点相互独立。
	void SetPracticeCheckpoint();
	void RestorePracticeCheckpoint();
	// 录制是全手动的：开始/继续、暂停、结束并直接落盘成影子组。
	void StartPracticeRecording();
	void PausePracticeRecording();
	void StopPracticeRecording();

	void PrepareInputForSend(int *pData, int Size, bool Dummy);
	bool OverridePredict();
	bool ForcePredictWeapons() const;
	bool ForcePredictGrenade() const;
	bool ForcePredictGunfire() const;
	bool ForcePredictPlayers() const;
	void InvalidateBufferedInputState();
	bool IsPracticeParticipant(int ClientId) const;
	int CurrentPracticeDummyId() const;
	bool IsPracticeDummy(int ClientId) const;
	int PracticeDummyId() const;
	// 当前这段练习用时（百分之一秒）：只累计分支世界真正推进的 tick，
	// 位置突变（/r、传送、重开、死亡回退）后从零开始。
	int PracticeRunCentiseconds() const;
	struct SPracticeTileFeedbackDecision
	{
		bool m_RecordTeleport = false;
		bool m_RecordDeath = false;
		bool m_PlayDeathFeedback = false;
	};
	static SPracticeTileFeedbackDecision EvaluatePracticeTileFeedback(bool PathContainsTeleport, bool WasInDeath, bool IsInDeath, float DistanceMoved)
	{
		SPracticeTileFeedbackDecision Decision;
		Decision.m_RecordTeleport = PathContainsTeleport && DistanceMoved > 0.001f;
		Decision.m_RecordDeath = IsInDeath && !WasInDeath;
		Decision.m_PlayDeathFeedback = Decision.m_RecordDeath;
		return Decision;
	}
	void OnReset() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnRender() override;
	void OnConsoleInit() override;

private:
	struct SGhostData
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Direction = vec2(1.0f, 0.0f);
		int m_Angle = 0;
		int m_Weapon = WEAPON_HAMMER;
		int m_HookState = HOOK_RETRACTED;
		vec2 m_HookPos = vec2(0.0f, 0.0f);
	};

	struct SAnchorData
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		CNetObj_Character m_Char = {};
		CNetObj_DDNetCharacter m_DDNet = {};
		bool m_HasDDNet = false;
	};

	struct SPracticeInputFilterState
	{
		bool m_LeftHeld = false;
		bool m_LeftReleased = true;
		bool m_RightHeld = false;
		bool m_RightReleased = true;
		bool m_JumpReleased = true;
		CQmPracticeHookInput m_Hook;
	};

	struct SPracticeCommandState
	{
		bool m_RescueManual = false;
		bool m_HasRescueAuto = false;
		bool m_HasRescueManual = false;
		bool m_HasLastTeleport = false;
		bool m_HasLastDeath = false;
		bool m_InvincibleAddedEndlessJump = false;
		vec2 m_LastTeleportPos = vec2(0.0f, 0.0f);
		vec2 m_LastDeathPos = vec2(0.0f, 0.0f);
		SQmPracticeSavedTee m_AutoTee;
		SQmPracticeSavedTee m_ManualTee;
		// 用户显式设置的重生点，独立于救援点，重开世界后依然保留。
		bool m_HasCheckpoint = false;
		SQmPracticeSavedTee m_CheckpointTee;
	};

	bool m_Enabled = false;
	bool m_RequireDummy = false;
	int m_PracticeRunTicks = 0;
	// 启用时本体所在连接：服务器重进换 id 后，用它把参与者重绑回同一条连接。
	int m_EnableLocalConn = -1;
	int m_EnableLocalClientId = -1;
	int m_EnableDummyClientId = -1;
	bool m_PracticeWorldInitialized = false;
	bool m_HasDummyAnchor = false;
	CPracticeRecorder m_Recorder;
	bool m_SuppressFireOnNextPredictTick = false;
	int m_InputSuppressTicks = 0;
	// 角色/输入解析瞬态失败的连续 tick 数；服务器重进、分身重连期间不立刻退出。
	int m_TransientFailureTicks = 0;
	int m_LastResolvedLocalClientId = -1;
	int m_LastResolvedDummyClientId = -1;
	int m_LastResolvedLocalInputConn = -1;
	int m_LastResolvedDummyInputConn = -1;
	CPracticeInputLock m_InputLock;
	std::array<SPracticeInputFilterState, NUM_DUMMIES> m_aPracticeInputFilterStates{};

	SGhostData m_MainGhost;
	SGhostData m_DummyGhost;
	SAnchorData m_MainAnchor;
	SAnchorData m_DummyAnchor;
	std::array<int, MAX_CLIENTS> m_aLastAttackTick{};
	std::array<SPracticeCommandState, MAX_CLIENTS> m_aPracticeCommandState{};

	CGameWorld m_PracticeBaseWorld;
	// 启用练习那一刻的分支世界。主世界持续推进，之后无法再从 m_GameWorld 取回这一刻的
	// 机关、移动方块与开关状态，所以“重开练习”只能回到这里的深拷贝。
	CGameWorld m_PracticeStartWorld;
	bool m_HasPracticeStartSnapshot = false;

	// 分支世界里的机关炮（地图实体层的 ENTITY_PLASMA*）。服务器由 CGun 驱动，
	// 客户端预测世界没有这套实体，练习世界必须自己复刻发射逻辑。
	struct SPracticeGun
	{
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Core = vec2(0.0f, 0.0f);
		bool m_Freeze = false;
		bool m_Explosive = false;
		int m_Layer = 0;
		int m_Number = 0;
		int m_EvalTick = 0;
		int m_aLastFireTeam[MAX_CLIENTS] = {};
		int m_aLastFireSolo[MAX_CLIENTS] = {};
	};

	std::vector<SPracticeGun> m_vPracticeGuns;
	bool m_PracticeGunsLoaded = false;
	int m_NextPracticePlasmaId = 100000;
	void LoadPracticeGuns();
	void TickPracticeGuns(CGameWorld &World, int Tick);
	void FirePracticeGun(CGameWorld &World, SPracticeGun &Gun, int Tick);

	static void ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData);
	// 控制页可绑定的"保存练习线路"按键：不设默认键，只提供命令。
	static void ConSavePracticeGhost(IConsole::IResult *pResult, void *pUserData);
	// 控制页可绑定的"重开练习世界"按键：不设默认键，只提供命令。
	static void ConPracticeRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeEnter(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeExit(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeSetCheckpoint(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeRestoreCheckpoint(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeRecordStart(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeRecordPause(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeRecordStop(IConsole::IResult *pResult, void *pUserData);
	static void ConPracticeViewGhost(IConsole::IResult *pResult, void *pUserData);
	// 诊断：一次性打印客户端侧世界状态（分支 tick、轨迹、开关、查看会话）。
	static void ConLocalWorldDump(IConsole::IResult *pResult, void *pUserData);

	void ResetPracticeState();
	void ResetCommandState();
	void SyncPracticeWorldConfig();
	int DummyClientId() const;
	bool ResolveParticipantInputs(int LocalClientId, int DummyClientId, int &LocalInputConn, int &DummyInputConn) const;
	int CurrentLocalPracticeId() const;
	bool ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const;
	bool IsPracticeServerPauseActive() const;
	void ShiftPracticeWorldTicks(int TickDelta);
	void FreezePracticeWorldForServerPause();
	void UpdateGhostData();
	void UpdateGhostForClientId(int ClientId, SGhostData &Ghost);
	int ApplyVisualFastInputPrediction(int FinalTickRegular, int LocalClientId, int DummyClientId, int LocalInputConn, int DummyInputConn);
	void CaptureAnchorsFromSnapshot();
	void CaptureAnchorFromSnapshot(int ClientId, SAnchorData &Anchor);
	bool ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const;
	bool InitPracticeWorld();
	void PrunePracticeWorld(CGameWorld &World) const;
	// 练习线路录制：Rank 1 是另一个平行时空，只能看；这里录的是我们自己这条
	// 分叉时空里的 Tee（本体 + 分身多轨），连同世界里发生的交互事件一起存，
	// 回放时走 Rank 1 同款 demo 式查看模式，而不是普通跟跑影子。
	void BeginPracticeTrack(int StartTick);
	void RecordPracticeFrames(CGameWorld &World, int Tick, int LocalClientId, int DummyClientId);
	void RecordPracticeEvents(CGameWorld &World, int Tick);
	void RecordPracticeSwitchState(CGameWorld &World, int Tick);
	// 任何位置突变（/r、传送、死亡、重进练习）都要切段，段内保证无瞬移。
	void OnPracticePositionJump();
	bool SavePracticeGhost(const char *pName);
	// fork 后清理指向被裁掉玩家的钩子状态：HOOK_GRABBED 会按旧世界的 HookPos
	// 继续施加拖拽力，不清洗就会表现为人被钩子甩飞。
	bool SanitizePracticeWorldHooks(CGameWorld &World) const;
	// /tc 与 /tp（无参）的落点。按当前视角选取与 HUD 准心同源的基准点。
	vec2 ResolvePracticeCursorTarget();

public:
	// 跟随旁观时把服务器快照里的位置替换为分支世界里的坐标。
	// 分支世界与服务器世界各自推进，快照位置对练习世界没有意义。
	// 只有旁观目标就在分支世界内（本体/分身）时才替换，返回是否生效。
	bool ResolvePracticeSpectatorPosition(vec2 &Position);
	// 分支世界的 solo 状态才是权威：练习里的 /solo /unsolo 只改预测世界，
	// 服务器快照的 solo 标志不会跟着变，渲染层据此仍把分身画成实体。
	bool ResolvePracticeSoloState(int ClientId, bool &Solo) const;
	// 分支世界是当前客户端侧权威世界的时钟。幽灵播放头必须问这里，而不是
	// 直接用 PredGameTick：服务器暂停时练习世界冻结、PredGameTick 继续走，
	// 两者会分叉，幽灵就会相对练习世界超前。返回 false 表示由调用方用默认值。
	bool ResolvePracticePlaybackTick(int &PlaybackTick) const;
	bool PracticeWorldClockActive() const { return m_Enabled && m_PracticeWorldInitialized; }

private:
	// 服务器重进会换 client id，重新 fork 前必须把参与者 id 换成本次连接的实际 id。
	bool TryRebindPracticeParticipants();
	// 过渡期内角色/输入暂时解析失败时冻结练习世界等待恢复，宽限超时才真正退出。
	bool SuspendPracticeForTransientFailure(const char *pReason);
	void HoldPracticeWorldForSuspension();
	void ResetAttackTickHistory();
	void TrackPracticeTileFeedback(int ClientId, CCharacter *pChar, const vec2 &BeforePos);
	void StandbyCharacter(CCharacter *pChar) const;
	void RenderGhost(const SGhostData &Ghost, float Alpha) const;
	void ReleaseBufferedInputState();
	void CaptureServerReleasedFireStates();
	void ReleaseBufferedActionInputState();
	void CapturePracticeInputFilterStates(bool PreserveHeldHook = false);
	void PreserveCurrentPracticeInputFilterStates();
	void FilterPracticeInput(CNetObj_PlayerInput &Input, int InputConn, bool Commit);

	void EchoPractice(const char *pFormat, ...) const;
	static bool ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs);
	static bool ParseCoordinateToken(const char *pToken, float Base, float &Out);
	int FindClientByName(const char *pName) const;
	void NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const;
	void NormalizeWeaponSelectionInput(CCharacter *pChar) const;
	void TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const;
	void StoreLastTeleport(int ClientId, const vec2 &Pos);
	void StoreLastDeathPosition(int ClientId, const vec2 &Pos);
	void CaptureServerLockedInputs();
	bool TryAttachDummyFromSnapshot();
	bool IsInFreezePickupRange(CCharacter *pChar) const;
	void CapturePracticeRescueTee(const CCharacter *pChar, int NowTick, SQmPracticeSavedTee &Tee) const;
	void ApplyPracticeRescueTee(CCharacter *pChar, const SQmPracticeSavedTee &Tee) const;
	void TrackSafeRescuePosition(int ClientId, CCharacter *pChar);
	void ResetPracticeTimelineState();
	// 把一份分支世界整体铺到渲染/预测读取面：两个预测世界、角色核心与预测 tick。
	// 启用、延迟接入分身、进入旁观、服务器暂停冻结与复位都走同一入口。
	void PublishPracticeWorldSnapshot(CGameWorld &World, int LocalClientId, int DummyClientId);
	void PublishPracticeWorldState();
	// 两条预测循环共用的预测状态发布。主循环追赶真实 tick，视觉循环外推快速输入，
	// 两者必须写出同一套 m_PrevPredictedWorld / m_PredictedChar / m_aClients 状态，
	// 否则镜头插值、分身预测和位置回查会在两条循环之间跳变。
	void PublishPracticePredictionBeforeStep(CGameWorld &World, const SPracticePredictTickPlan &Plan, int Tick, bool CopyWorldToPredicted, int LocalClientId);
	void PublishPracticePredictionAfterStep(CGameWorld &World, const SPracticePredictTickPlan &Plan, int Tick, int LocalClientId);
	void PublishPracticePredictedPositions(CGameWorld &World, int Tick);
	// 新预测 tick 的跳跃/钩子反馈。本体与分身走同一入口，按输入槽位去重。
	// 返回 true 表示该 tick 被本槽位消费（调用方据此置 m_NewPredictedTick）。
	bool PublishPracticeNewPredictedFeedback(int Slot, CCharacter *pChar, int Tick);
	bool FindFinishPosition(vec2 &OutPos) const;
	void NormalizeDefaultPracticeWeapons(CCharacter *pChar, bool HasExplicitWeaponFlags) const;
	bool ExecutePracticeCommand(int Team, int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated);
};

#endif
