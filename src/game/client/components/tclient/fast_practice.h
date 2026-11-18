/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_FAST_PRACTICE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_FAST_PRACTICE_H

#include <base/math.h>

#include <engine/client/enums.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/local_world.h>
#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/components/qmclient/practice_commands.h>
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
	// 预测接管期间武器、手雷、枪火与其他玩家都走预测：四个调用点实际是同一条
	// 「练习开启且不在旁观」判定，统一成一个入口，避免口径在四处漂移。
	bool ForcePredictPractice() const;
	void InvalidateBufferedInputState();
	bool IsPracticeParticipant(int ClientId) const;
	// 练习参与者的表情以分支时间线为准：服务器 AFK 或快照失活时用本地默认眼睛，
	// 手动 /emote 在有效期内覆盖；非参与者仍返回服务器快照值。
	int ResolvePracticeEyeEmote(int ClientId, int ServerEmote, bool ServerActive, bool ServerAfk) const;
	// 非参与者都停在服务器时间线上，练习期间只作画面参照物。影子开关和
	// 不透明度统一管这一整组，不是只管自己的主世界影子。
	float ServerTeeAlpha(int ClientId, float DefaultAlpha) const;
	int CurrentPracticeDummyId() const;
	bool IsPracticeDummy(int ClientId) const;
	int PracticeDummyId() const;
	// 本次练习会话的累计用时（百分之一秒）：只累计分支世界真正推进的 tick，
	// 进入练习起算，/r、传送、重开、死亡回退都不清零。
	int PracticeRunCentiseconds() const;
	struct SPracticeTileFeedbackDecision
	{
		bool m_RecordDeath = false;
		bool m_PlayDeathFeedback = false;
	};
	// 传送 tile 不产生任何命令记录：服务器只把斜杠传送写进 m_LastTeleTee，
	// 地图传送对 /lasttp 与 /back 都是透明的。
	static SPracticeTileFeedbackDecision EvaluatePracticeTileFeedback(bool WasInDeath, bool IsInDeath)
	{
		SPracticeTileFeedbackDecision Decision;
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
		bool m_InvincibleAddedEndlessJump = false;
		SQmPracticeSavedTee m_AutoTee;
		SQmPracticeSavedTee m_ManualTee;
		// /back 与 /lasttp 的记录，写入规则见 practice_commands.h。
		SQmPracticeCommandRecords m_Records;
		// 用户显式设置的重生点，独立于救援点，重开世界后依然保留。
		bool m_HasCheckpoint = false;
		SQmPracticeSavedTee m_CheckpointTee;
		SQmPracticeEyeEmoteState m_EyeEmote;
	};

	bool m_Enabled = false;
	bool m_RequireDummy = false;
	int m_PracticeRunTicks = 0;
	// 启用时本体所在连接：服务器重进换 id 后，用它把参与者重绑回同一条连接。
	int m_EnableLocalConn = -1;
	int m_EnableLocalClientId = -1;
	int m_EnableDummyClientId = -1;
	bool m_PracticeWorldInitialized = false;
	// 旁观期间分支世界继续按自己的时间线推进，玩家不操控时两个参与者使用中性输入。
	// 退出旁观直接接回当前分支状态，不再补跳时钟。
	bool m_SpectatingBranch = false;
	bool m_HasDummyAnchor = false;
	// 分身接入/断开会重新 fork 分支世界。这份待还原的本地 tee 让角色留在
	// 练习分支的位置上，而不是被换回服务器世界里的 tee。
	bool m_HasPendingLocalTee = false;
	SQmPracticeSavedTee m_PendingLocalTee;
	// client id 改变时旧分支的 solo 不能按新 id 找到；重绑时先把状态寄存，
	// 等 InitPracticeWorld 建出新参与者后再写回。
	bool m_HasPendingLocalSolo = false;
	bool m_PendingLocalSolo = false;
	bool m_HasPendingDummySolo = false;
	bool m_PendingDummySolo = false;
	CPracticeRecorder m_Recorder;
	bool m_SuppressFireOnNextPredictTick = false;
	int m_InputSuppressTicks = 0;
	// 分支世界已经创建并消费过预测事件的最高 tick。重新 fork 会重算一段已播过的
	// tick，屏障让这些 tick 不再创建开火声/特效，避免同一次射击播两遍。
	int m_EventBarrierTick = -1;
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
	// 分支世界必须自洽：主世界只按客户端预测开关创建拾取物与激光类实体，
	// 快照又只覆盖镜头附近。这里直接从地图图层重建整张地图的拾取物、门与
	// 拖拉，机关炮由 m_vPracticeGuns 单独模拟。
	void LoadPracticeMapMechanisms(CGameWorld &World);
	// fork 前清掉从主世界复制过来的同类实体，避免与地图重建的机制重复生效。
	void ResetPracticeMapEntities(CGameWorld &World);
	int m_NextPracticeMapEntityId = 500000;
	int DummyClientId() const;
	bool ResolveParticipantInputs(int LocalClientId, int DummyClientId, int &LocalInputConn, int &DummyInputConn) const;
	int CurrentLocalPracticeId() const;
	bool ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const;
	bool IsPracticeServerPauseActive() const;
	void ShiftPracticeWorldTicks(int TickDelta);
	// 只平移世界实体与开关的绝对 tick；/restart 从入口快照恢复时使用，
	// 不碰 LocalWorld 轨迹、事件屏障和当前练习会话的命令状态。
	void ShiftPracticeWorldEntityTicks(CGameWorld &World, int TickDelta);
	// 旁观时 m_PredictedWorld 归服务器预测所有；这里直接推进分支世界，
	// 用中性输入继续计时、机关与轨迹采样，退出旁观无需追赶。
	void TickPracticeWorldDuringSpectate();
	// 分支时钟的对齐目标：取预测 tick 与当前分支 tick 的较大值，绝不倒退。
	int PracticeClockTarget() const;
	void FreezePracticeWorldForServerPause();
	void UpdateGhostData();
	void UpdateGhostForClientId(int ClientId, SGhostData &Ghost);
	int ApplyVisualFastInputPrediction(int FinalTickRegular, int LocalClientId, int DummyClientId, int LocalInputConn, int DummyInputConn);
	void CaptureAnchorsFromSnapshot();
	void CaptureAnchorFromSnapshot(int ClientId, SAnchorData &Anchor);
	bool ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const;
	bool InitPracticeWorld();
	void PrunePracticeWorld(CGameWorld &World) const;
	// solo 区里客户端世界不收非碰撞角色，分支世界按快照补建缺失的本体/分身。
	bool EnsurePracticeParticipants();
	// 分支世界当前是否装着一条活着的练习时间线。旁观、分身断开、重绑都会把
	// m_PracticeWorldInitialized 置 false 等待重建，但那时 base 世界里的开关状态
	// 仍然是有效的；fork 失败后 base 被服务器世界覆盖，这里才置 false。
	// 捕获开关状态看它，不能看 m_PracticeWorldInitialized。
	bool m_PracticeBranchLive = false;
	// 重新 fork 前暂存的开关状态与当时的分支 tick，fork 后由 QmCarryPracticeSwitchers
	// 按 tick 差接回，/setswitch 的改动不因分身接入/断开、旁观返回而丢失。
	bool m_HasCarriedSwitchers = false;
	int m_CarriedSwitchersTick = 0;
	std::vector<SSwitchers> m_CarriedSwitchers;
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
	// 钩子辅助线的碰撞目标每帧从服务器快照重建，而练习参与者活在分支世界里。
	// 位置和 solo 都必须取分支权威值，否则辅助线会按另一条时间线判断，
	// 从画面上紧挨着的分身身上穿过去、颜色也不随 Tee 改变。
	bool ResolvePracticeHookCollTarget(int ClientId, vec2 &Pos, bool &Solo, bool &Super, bool &HookHitDisabled);
	// 分支世界里的练习角色（只对参与者有效，世界未建好时为 nullptr）。
	// 渲染层用它兜住“服务器快照失活、分支还在模拟”的参与者：武器与挥锤相位
	// 不在核心字段里，必须从角色实体取；服务器侧死亡/暂停也不能让人直接消失。
	CCharacter *ResolvePracticeCharacter(int ClientId);
	// 分支世界是当前客户端侧权威世界的时钟。幽灵播放头必须问这里，而不是
	// 直接用 PredGameTick：服务器暂停时练习世界冻结、PredGameTick 继续走，
	// 两者会分叉，幽灵就会相对练习世界超前。返回 false 表示由调用方用默认值。
	bool ResolvePracticePlaybackTick(int &PlaybackTick) const;
	bool PracticeWorldClockActive() const { return m_Enabled && m_PracticeWorldInitialized; }
	// 分支世界的开关状态是这条时间线的权威。渲染、HUD、命中盒和录制都读这一份；
	// 服务器快照只写服务器世界，不会覆盖练习里的 /setswitch 结果。
	// 未启用或世界未建好时返回 nullptr，调用方回落到服务器世界。
	std::vector<SSwitchers> *PracticeSwitchers();
	int PracticeSwitchTeam() const;
	// 练习参与者（本体/分身）的冻结状态来自分支世界：解冻进度条与 HUD 读它，
	// 服务器快照里的 FreezeEnd 属于另一条世界线。返回 false 表示该 client
	// 不在练习分支世界里，调用方回落到服务器快照。
	bool ResolvePracticeCharacterCore(int ClientId, const CCharacterCore **ppCore, int &Tick);
	// 分支世界里重建的静态激光。网络裁剪外的服务器快照没有它们，
	// 渲染层必须自己遍历这棵树，才能让画面与分支碰撞一致。
	CGameWorld *PracticeLightWorld();

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
	// 把传送落地后的整份 tee 存成 /lasttp 记录，对齐服务器 m_LastTeleTee 的保存时机。
	void RecordPracticeCommandTeleport(int ClientId, CCharacter *pChar);
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
