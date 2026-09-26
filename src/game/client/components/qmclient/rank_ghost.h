// 官方 Rank 影子：从 ddnet.org/watch 的预生成回放里提取指定名次玩家的轨迹，
// 转换成本地 ghost 后加载渲染，让玩家正常跑图时跟随对照（不打断游戏连接）。
//
// 数据来源与官方 watch 页面（https://ddnet.org/watch/）一致：
//   https://ddnet.org/watch/watchable.jsonl   预生成回放索引（地图 + 名次）
//   https://ddnet.org/watch/demos/<file>      回放文件（标准 demo，内嵌地图）
//
// 处理流程：下载索引 -> 按地图/名次查找 -> 下载回放 -> 顺序解析 run 区间轨迹
//           -> 写 ghost 文件 -> 交给 CGhost 加载（出发时自动播放）。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_GHOST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_GHOST_H

#include "rank_demo_manifest.h"

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/http.h>
#include <engine/shared/demo.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <vector>

class IStorage;
class CSnapshot;

class CRankGhost : public CComponent, private CDemoPlayer::IListener
{
public:
	// 查看模式状态快照（供计分板控制条等 UI 读取）
	struct SViewState
	{
		bool m_Active = false;
		bool m_Playing = false;
		float m_Progress = 0.0f; // [0, 1]
		float m_CurSeconds = 0.0f;
		float m_TotalSeconds = 0.0f;
		bool m_Finished = false;
		float m_FinishSeconds = 0.0f;
		// 终点成绩差值（回放里 SV_RACEFINISH / SV_DDRACETIME 的真实值，单位秒）；
		// false = 回放没有带到差值，HUD 只显示成绩
		bool m_HasFinishDiff = false;
		float m_FinishDiffSeconds = 0.0f;
		bool m_FinishIsRecord = false;
		// 最近一次检查点差值（与原生 HUD 的 m_TimeCpDiff 同语义，单位秒）
		bool m_HasCpDiff = false;
		float m_CpDiffSeconds = 0.0f;
		float m_CpDiffAgeSeconds = 0.0f;
	};

	CRankGhost();
	~CRankGhost() override;

	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnMapLoad() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnGhostsUnloaded();
	void OnGhostLoaded(const char *pStoragePath, int Slot);
	void OnGhostUnloaded(int Slot);

	// 供菜单按钮调用：请求当前地图的官方 rank 影子
	void RequestCurrentMapGhost(int Rank = 1);

	// ===== Rank 1 页面接口 =====
	// 清单加载状态（页面据此显示加载中/失败）
	enum class EManifestState
	{
		UNKNOWN,
		LOADING,
		READY,
		FAILED,
	};
	EManifestState ManifestState() const;
	// 幂等：清单缺失或过期时自动拉取（不打断进行中的任务）
	void EnsureManifest();
	// 强制重新拉取清单（显式刷新操作，可打断当前任务）
	void RefreshManifest();
	// 收集指定地图与名次的条目：同一 demo 的多条成员记录合并为一条，solo 在前
	std::vector<qmclient::rank_demo::SEntry> CollectRankEntries(const char *pMap, int Rank) const;
	// 全清单搜索：按地图名子串（大小写不敏感）匹配指定名次的条目，Limit 限制结果数
	std::vector<qmclient::rank_demo::SEntry> CollectSearchEntries(const char *pQuery, int Rank, int Limit) const;
	// 创建 QmClient rank 回放缓存目录（层级创建，可安全重复调用）
	static void EnsureFolders(IStorage *pStorage);
	// 按 manifest 的 demo 文件名请求：下载并转换为影子（不退出服务器）
	void RequestGhostForDemo(const char *pDemoName);
	// 卸载当前加载的 rank 影子（qm_rank_ghost_off 的编程接口）
	void RequestGhostOff();
	// 按 manifest 的 demo 文件名仅下载回放到缓存（供回放播放/分享）
	void RequestDemoDownload(const char *pDemoName);
	// 影子/下载任务是否进行中
	bool IsBusy() const;
	// 当前任务的进度描述（下载/转换百分比）；空闲时返回 false
	bool DescribeTask(char *pBuf, size_t BufSize) const;

	// ===== 查看模式：影子按独立时间线播放（游戏内 demo 播放器，不与跑图同步） =====
	// 请求加载该 demo 的影子并直接进入查看模式；已加载时直接切换
	void RequestGhostViewForDemo(const char *pDemoName);
	bool IsViewModeActive() const;
	// 计分板控制条状态快照（false = 当前无可用播放状态）
	bool GetViewState(SViewState &Out) const;
	void ViewPlayPause();
	// Fraction ∈ [0, 1]
	void ViewSeek(float Fraction);
	// 退出查看模式：影子保持加载，恢复跑图同步模式
	void ViewStop();
	// ===== 旁观模式成员面板：列出虚影成员并锁定镜头跟随 =====
	// 成员顺序与多轨缓存文件顺序一致（00 = 主选手）
	int ViewSelectedMember() const { return m_ViewSelected; }
	void ViewSelectMember(int Index);
	// 查看模式相机：跟随选中成员 / 自由视角（镜头停在原地）/ 多人同框（框住全部成员并自动缩放）
	enum class EViewCameraMode
	{
		MEMBER,
		FREE,
		ALL_MEMBERS,
	};
	EViewCameraMode ViewCameraMode() const { return m_ViewCameraMode; }
	void ViewSetCameraMode(EViewCameraMode Mode);
	// 多人同框：自动取景之外的用户倍率（单位=zoom 档位，正=放大）。
	// 语义对齐 DDNet multiview 的 personal zoom：自动取景负责框住全部成员，
	// 用户倍率叠加其上，使 zoom+/- 在自动取景模式下仍然可用
	float ViewZoomPersonal() const { return m_ViewZoomPersonal; }
	void ViewAdjustZoomPersonal(float DeltaSteps);
	void ViewSetZoomPersonal(float Steps);
	// 多人同框：全部已加载成员的包围盒（中心 + 尺寸）；false = 无可用成员
	bool ViewFocusAllMembers(vec2 *pCenter, vec2 *pSize) const;
	// 自由视角：以鼠标增量平移相机（查看面板打开时由光标驱动）
	bool ViewFreeCameraCenter(vec2 *pOut) const;
	void ViewFreeCameraPan(float Dx, float Dy);
	// 查看模式下的成员数量（与已加载影子槽位一致）；非查看模式为 0
	int ViewMemberCount() const { return IsViewModeActive() ? (int)m_vLoadedSlots.size() : 0; }
	// 成员名字（ghost 文件头的所有者）；Index 越界返回 false
	bool ViewMemberName(int Index, char *pBuf, size_t BufSize) const;
	// 当前选中成员的插值位置（供镜头跟随）；false = 暂无可用焦点
	bool ViewFocus(vec2 *pOut) const;
	// 是否有影子处于加载激活状态（含查看模式）
	bool IsGhostLoaded() const { return !m_vLoadedSlots.empty(); }
	// 当前加载的影子数量（多轨回放为参与者数）
	int LoadedGhostCount() const { return (int)m_vLoadedSlots.size(); }
	// 属于该条目的已加载数量（多轨组按目录前缀统计，排除玩家自录影子）
	int LoadedGhostCountForEntry(const qmclient::rank_demo::SEntry &Entry) const;
	// 条目缓存状态查询（路径出参返回可读缓存路径）
	bool IsEntryDemoCached(const qmclient::rank_demo::SEntry &Entry, char *pDemoPath, size_t DemoPathSize) const;
	// demo 浏览器下载按钮去重：查缓存目录里该图指定名次的最新 demo（false = 未缓存）
	bool FindCachedRankDemo(const char *pMap, int Rank, char *pDemoPath, size_t DemoPathSize) const;
	bool IsEntryGhostCached(const qmclient::rank_demo::SEntry &Entry, char *pGhostPath, size_t GhostPathSize) const;
	// 该条目的影子是否已加载激活
	bool IsEntryGhostActive(const qmclient::rank_demo::SEntry &Entry) const;
	// 删除该条目的 demo 与 ghost 缓存（激活中的影子会先卸载）
	void DeleteEntryCache(const qmclient::rank_demo::SEntry &Entry);
	// 由条目构建可读的缓存路径：<map>_rank<N>_<kind>_<time>s_<uuid8>.demo/.gho
	static void BuildEntryCachePaths(const qmclient::rank_demo::SEntry &Entry, char *pDemoPath, size_t DemoPathSize, char *pGhostPath, size_t GhostPathSize);

	// QmClient 专属目录：rank 回放相关数据与玩家自有 ghosts/demos 完全分离
	static constexpr const char *RANK1_ROOT = "qmclient/rank1";
	static constexpr const char *GHOST_SUBDIR = "ghosts"; // qmclient/rank1/ghosts
	static constexpr const char *DEMO_CACHE_DIR = "qmclient/rank1/demos";
	// 旧版本目录（一次性迁移/清理）
	static constexpr const char *LEGACY_GHOST_DIR = "ghosts/rank_ghost";
	static constexpr const char *LEGACY_DEMO_DIR = "demos/rank_ghost";

private:
	// watchable.jsonl 中的一条预生成回放记录（解析实现与 demo 浏览器共用）
	using SEntry = qmclient::rank_demo::SEntry;

	enum class EStage
	{
		IDLE,
		FETCH_MANIFEST,
		FETCH_DEMO,
		PARSE,
		// 仅下载回放（Rank 1 页面的“下载回放”动作），完成后不解析
		FETCH_DEMO_ONLY,
	};

	// 本次请求的目标：按地图名次自动挑选，或按 manifest 的 demo 名精确匹配
	enum class EPendingMode
	{
		GHOST,
		DEMO_ONLY,
	};

	// 解析期间的状态，定义在 cpp（避免头文件依赖 ghost/snapshot 数据结构）
	struct SParseState;

	static constexpr const char *MANIFEST_URL = "https://ddnet.org/watch/watchable.jsonl";
	static constexpr const char *DEMO_URL_PREFIX = "https://ddnet.org/watch/demos";
	// 每帧最多推进的 demo tick 数，避免长时间阻塞渲染
	static constexpr int PARSE_TICKS_PER_FRAME = 2500;

	// 本次请求
	std::string m_PendingMap;
	int m_PendingRank = 1;
	// 按 manifest 的 demo 名精确请求时非空（优先于地图名次匹配）
	std::string m_PendingDemo;
	EPendingMode m_PendingMode = EPendingMode::GHOST;
	// 命令可能在 autoexec / 客户端初始化早期执行，此时不触碰网络与聊天组件，
	// 只登记请求，实际任务在主循环里启动
	bool m_StartPending = false;
	bool m_UnloadPending = false;
	std::string m_PendingNotify;

	// 索引
	std::vector<SEntry> m_vEntries;
	bool m_ManifestLoaded = false;
	int64_t m_ManifestLoadedAt = 0;
	bool m_ManifestFailed = false;
	int64_t m_ManifestFailedAt = 0;
	std::shared_ptr<IHttpRequest> m_pManifestRequest;

	// 当前阶段输入
	SEntry m_ActiveEntry;
	std::shared_ptr<IHttpRequest> m_pDemoRequest;
	char m_aDemoStoragePath[IO_MAX_PATH_LENGTH] = "";
	char m_aGhostStoragePath[IO_MAX_PATH_LENGTH] = "";
	// 多轨写出时的当前轨迹文件路径（临时缓冲）
	char m_aGhostWritePath[IO_MAX_PATH_LENGTH] = "";

	EStage m_Stage = EStage::IDLE;
	std::unique_ptr<SParseState> m_pParse;

	// 已加载的 ghost 槽位与对应存储路径（多轨回放为多条）
	std::vector<int> m_vLoadedSlots;
	std::vector<std::string> m_vLoadedGhostPaths;
	// 组加载期间接受的前缀（目录内所有 .gho 都登记）
	char m_aLoadingGroupPrefix[IO_MAX_PATH_LENGTH] = "";
	// 影子已按哪一次跑图（LastRaceTick）对齐过；-1 表示尚未对齐
	int m_LastAlignedRaceTick = -1;
	// ghost 已缓存但地图还没加载时，等待地图出现再加载
	bool m_RetryLoadPending = false;
	int64_t m_RetryLoadDeadline = 0;
	int64_t m_RetryLoadNextAttempt = 0;

	// 查看模式
	bool m_ViewMode = false;
	// 旁观面板选中的跟随成员（多轨顺序索引，0 = 主选手）
	int m_ViewSelected = 0;
	// 相机跟随选中成员；关闭为自由视角（相机停在当前位置）
	EViewCameraMode m_ViewCameraMode = EViewCameraMode::MEMBER;
	// 自由视角的相机位置（进入模式时锚定当前镜头，之后随鼠标平移）
	vec2 m_ViewFreeCameraCenter = vec2(0.0f, 0.0f);
	bool m_ViewFreeCameraValid = false;
	// 多人同框的用户倍率（zoom 档位）；离开查看模式时归零，避免影响下一次取景
	float m_ViewZoomPersonal = 0.0f;
	// 用户倍率上限（档位）：约 0.23x ~ 4.3x，防止自动取景被推到极端
	static constexpr float VIEW_ZOOM_PERSONAL_LIMIT = 10.0f;
	// 加载完成后自动进入查看模式（Rank 1 页“画面内回放”）
	bool m_PendingView = false;

	// ===== 查看模式时间线：demo 交互事件与地图开关状态 =====
	// 影子只存了轨迹，demo 浏览器里的交互反馈（锤击/爆炸/伤害指示/世界音效）
	// 和开关类机关的表现都来自快照。解析时按 tick 顺手记录，查看模式下按
	// 轨迹相对 tick 重放，行为对齐 demo 浏览器。坐标为服务端世界坐标，直接可播。
	struct SViewEvent
	{
		int m_RelTick;
		int m_Type;
		int m_aData[3];
	};
	// 开关状态稀疏记录：服务端每份快照都全量下发，仅在状态变化时记一条
	struct SViewSwitchState
	{
		int m_RelTick;
		int m_HighestSwitchNumber;
		unsigned m_aStatus[8];
	};
	std::vector<SViewEvent> m_vViewEvents;
	std::vector<SViewSwitchState> m_vViewSwitchStates;

	// 消息类时间线：终点成绩/差值、检查点差值与全局音效只存在于 demo 的网络
	// 消息流里（快照与事件都没有），解析时按消息记录，查看模式按播放头重放。
	enum class EViewMessageType
	{
		RACE_FINISH, // {ClientId, TimeMs, DiffMs, RecordPersonal, RecordServer}
		RACE_TIME, // {TimeMs, CheckCs, Finish}（0.7 的 Checkpoint 已换算成同一形态）
		SOUND_GLOBAL, // {SoundId}
		MAP_SOUND_GLOBAL, // {SoundId}
	};
	struct SViewMessage
	{
		int m_RelTick;
		EViewMessageType m_Type;
		int m_aData[5];
	};
	std::vector<SViewMessage> m_vViewMessages;
	// 消息播放游标：全局音效只播一次，状态类消息只保留最后一条
	size_t m_ViewMessageCursor = 0;

	// 播放游标：下一条待派发事件下标 / 已应用的状态记录下标
	size_t m_ViewEventCursor = 0;
	int m_ViewAppliedSwitch = -1;
	int m_ViewLastTick = -1;
	// 旧缓存补齐：只重放回放收集时间线，不重写、不重载影子
	bool m_EventRebuildOnly = false;

	static void ConRankGhost(IConsole::IResult *pResult, void *pUserData);
	static void ConRankGhostOff(IConsole::IResult *pResult, void *pUserData);
	static void ConRankGhostView(IConsole::IResult *pResult, void *pUserData);

	void StartLookup(const char *pMap, int Rank);
	void StartPendingLookup(const char *pDemoName, EPendingMode Mode);
	void LookupInManifest();

	void StartManifestFetch();
	void StartDemoFetch();
	// demo 下载完成后的公共收尾：校验并按请求模式收尾（仅下载 → 提示；影子 → 解析）
	void FinishDemoFetch();
	void StartParse();
	void FinishParse();

	// ===== 查看模式时间线：录制 / 缓存 / 回放 =====
	// 解析时从快照收集交互事件与开关状态
	void RecordViewTimeline(const CSnapshot *pSnapshot, int RelTick);
	// 解析时从 demo 消息流收集终点/检查点成绩与全局音效
	void RecordViewMessage(int RelTick, EViewMessageType Type, const int *pData, int NumInts);
	// 最近一条不晚于 RelTick 的成绩类消息（终点/检查点）；无则返回 nullptr
	const SViewMessage *FindLatestViewMessage(EViewMessageType Type, int RelTick) const;
	// 时间线随影子缓存到 sidecar 文件（.qmevt），重看无需重新解析
	void WriteViewTimeline();
	bool LoadViewTimeline();
	bool ViewTimelinePath(char *pBuf, size_t BufSize) const;
	// sidecar 缓存是否已存在且为当前版本；旧版本返回 false，由补齐流程重建
	bool ViewTimelineCached() const;
	// 旧缓存缺时间线时后台重放一次回放补齐（不重写影子，不打断查看）
	void KickViewTimelineRebuild();
	// 查看模式每帧：按播放头派发事件、应用开关状态
	void UpdateViewTimeline();
	void DispatchViewEvent(const SViewEvent &Event);
	void DispatchViewMessage(const SViewMessage &Msg);
	void ApplyViewSwitchState(const SViewSwitchState &State);

	void UpdateManifestStage();
	void UpdateDemoStage();
	void UpdateParseStage();

	bool ParseManifest(const unsigned char *pData, size_t DataSize);

	// 按条目加载缓存：多轨目录优先，其次单文件
	bool LoadGhostTarget();
	bool LoadGhostFile(const char *pStoragePath);
	bool LoadGhostGroup(const char *pDir);
	void RegisterLoadedGhost(const char *pStoragePath, int Slot);
	void UnloadGhost();
	// 加载成功后的公共收尾：按需进入查看模式
	void AfterGhostLoaded();
	void EnterViewMode();

	// 加载后把 Ghost 页列表刷新一遍，并关联我们占用的槽位
	void RefreshGhostList();
	// 玩家已在跑图时，让影子立即按当前进度对齐播放；此后每次重新出发都会重新对齐
	void AlignToCurrentRun();
	void NotifyLoaded(const char *pOwner, const char *pTimeText);

	void Fail(const char *pMessage);
	void AbortTask();
	void Echo(const char *pMessage) const;

	// CDemoPlayer::IListener
	void OnDemoPlayerSnapshot(void *pData, int Size) override;
	void OnDemoPlayerMessage(void *pData, int Size) override;
};

#endif
