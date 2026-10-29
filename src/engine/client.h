/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_H
#define ENGINE_CLIENT_H
#include "graphics.h"
#include "kernel.h"
#include "message.h"

#include <base/hash.h>
#include <base/types.h>

#include <engine/client/enums.h>
#include <engine/demo.h>
#include <engine/friends.h>
#include <engine/shared/translation_context.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <chrono>
#include <functional>
#include <optional>

#define CONNECTLINK_DOUBLE_SLASH "ddnet://"
#define CONNECTLINK_NO_SLASH "ddnet:"

class CSnapshot;
class CSnapshotBuffer;
class IMap;
struct SWarning;

struct SClientSnapshotStats
{
	uint64_t m_SnapshotCount = 0;
	uint64_t m_PartCount = 0;
	uint64_t m_PayloadBytes = 0;
	// Current wall-clock gap since the last complete, validated snapshot.
	float m_CurrentGapMs = -1.0f;
	int m_LastTickGap = -1;
};

enum
{
	RECORDER_MANUAL = 0,
	RECORDER_AUTO = 1,
	RECORDER_RACE = 2,
	RECORDER_REPLAYS = 3,
	RECORDER_MAX = 4,
};

enum class EDemoMarkerResult
{
	NONE,
	ADDED,
};

typedef bool (*CLIENTFUNC_FILTER)(const void *pData, int DataSize, void *pUser);
struct CChecksumData;

class IClient : public IInterface
{
	MACRO_INTERFACE("client")
public:
	/* Constants: Client States
		STATE_OFFLINE - The client is offline.
		STATE_CONNECTING - The client is trying to connect to a server.
		STATE_LOADING - The client has connected to a server and is loading resources.
		STATE_ONLINE - The client is connected to a server and running the game.
		STATE_DEMOPLAYBACK - The client is playing a demo
		STATE_QUITTING - The client is quitting.
	*/

	enum EClientState
	{
		STATE_OFFLINE = 0,
		STATE_CONNECTING,
		STATE_LOADING,
		STATE_ONLINE,
		STATE_DEMOPLAYBACK,
		STATE_QUITTING,
		STATE_RESTARTING,
	};

	/**
	 * More precise state for @see STATE_LOADING
	 * Sets what is actually happening in the client right now
	 */
	enum ELoadingStateDetail
	{
		LOADING_STATE_DETAIL_INITIAL,
		LOADING_STATE_DETAIL_LOADING_MAP,
		LOADING_STATE_DETAIL_LOADING_DEMO,
		LOADING_STATE_DETAIL_SENDING_READY,
		LOADING_STATE_DETAIL_GETTING_READY,
	};

	enum ELoadingCallbackDetail
	{
		LOADING_CALLBACK_DETAIL_MAP,
		LOADING_CALLBACK_DETAIL_DEMO,
	};
	typedef std::function<void(ELoadingCallbackDetail Detail)> TLoadingCallback;
	CTranslationContext m_TranslationContext;

protected:
	// quick access to state of the client
	EClientState m_State = IClient::STATE_OFFLINE;
	ELoadingStateDetail m_LoadingStateDetail = LOADING_STATE_DETAIL_INITIAL;
	int64_t m_StateStartTime;

	// quick access to time variables
	int m_aPrevGameTick[NUM_DUMMIES] = {0, 0};
	int m_aCurGameTick[NUM_DUMMIES] = {0, 0};
	float m_aGameIntraTick[NUM_DUMMIES] = {0.0f, 0.0f};
	float m_aGameTickTime[NUM_DUMMIES] = {0.0f, 0.0f};
	float m_aGameIntraTickSincePrev[NUM_DUMMIES] = {0.0f, 0.0f};

	int m_aPredTick[NUM_DUMMIES] = {0, 0};
	float m_aPredIntraTick[NUM_DUMMIES] = {0.0f, 0.0f};

	float m_LocalTime = 0.0f;
	float m_GlobalTime = 0.0f;
	float m_RenderFrameTime = 0.0001f;
	float m_FrameTimeAverage = 0.0001f;
	uint64_t m_PerfFrame = 0;

	TLoadingCallback m_LoadingCallback = nullptr;

	char m_aNews[3000] = "";
	int m_Points = -1;
	int64_t m_ReconnectTime = 0;

public:
	class CSnapItem
	{
	public:
		int m_Type;
		int m_Id;
		const void *m_pData;
		int m_DataSize;
	};

	enum
	{
		CONN_MAIN = 0,
		CONN_DUMMY,
		CONN_CONTACT,
		NUM_CONNS,
	};

	enum
	{
		CONNECTIVITY_UNKNOWN,
		CONNECTIVITY_CHECKING,
		CONNECTIVITY_UNREACHABLE,
		CONNECTIVITY_REACHABLE,
		// Different global IP address has been detected for UDP and
		// TCP connections.
		CONNECTIVITY_DIFFERING_UDP_TCP_IP_ADDRESSES,
	};

	//
	EClientState State() const { return m_State; }
	ELoadingStateDetail LoadingStateDetail() const { return m_LoadingStateDetail; }
	int64_t StateStartTime() const { return m_StateStartTime; }
	void SetLoadingStateDetail(ELoadingStateDetail LoadingStateDetail) { m_LoadingStateDetail = LoadingStateDetail; }

	void SetLoadingCallback(TLoadingCallback &&Func) { m_LoadingCallback = std::move(Func); }

	// Game time.
	//
	// There are 50 ticks per second, by default we only send snapshot on
	// every second tick.

	// Tick of the second to most recently received snapshot (usually 2
	// less than `GameTick`).
	int PrevGameTick(int Conn) const { return m_aPrevGameTick[Conn]; }
	// Tick of most recently received snapshot.
	int GameTick(int Conn) const { return m_aCurGameTick[Conn]; }
	// The tick we should predict to. Comes from a magic black box called
	// "smooth time".
	int PredGameTick(int Conn) const { return m_aPredTick[Conn]; }
	// Linear interpolation parameter between `PrevGameTick` (0) and
	// `GameTick` (1). Can be outside the interval [0, 1].
	float IntraGameTick(int Conn) const { return m_aGameIntraTick[Conn]; }
	// Linear interpolation parameter between `PredGameTick - 1` (0) and
	// `PredGameTick` (1). Can be outside the interval [0, 1].
	float PredIntraGameTick(int Conn) const { return m_aPredIntraTick[Conn]; }
	// (Fractional) ticks since `PrevGameTick`.
	float IntraGameTickSincePrev(int Conn) const { return m_aGameIntraTickSincePrev[Conn]; }
	// Time in seconds since the second to most recently received snapshot.
	float GameTickTime(int Conn) const { return m_aGameTickTime[Conn]; }
	// 50
	int GameTickSpeed() const { return SERVER_TICK_SPEED; }

	// Other time.

	// Time in seconds since a map was joined, or `GlobalTime` if that
	// hasn't happened yet.
	float LocalTime() const { return m_LocalTime; }
	// Time in seconds since the client was opened.
	float GlobalTime() const { return m_GlobalTime; }

	// Render statistics.

	// Duration in seconds of the previous render cycle.
	float RenderFrameTime() const { return m_RenderFrameTime; }
	// Exponentially weighted average of frame times.
	float FrameTimeAverage() const { return m_FrameTimeAverage; }
	uint64_t PerfFrame() const { return m_PerfFrame; }

	// actions
	virtual void Connect(const char *pAddress, const char *pPassword = nullptr) = 0;
	virtual void Disconnect() = 0;

	// dummy
	virtual void DummyDisconnect(const char *pReason) = 0;
	virtual void DummyConnect() = 0;
	virtual bool DummyConnected() const = 0;
	virtual bool DummyConnecting() const = 0;
	virtual bool DummyConnectingDelayed() const = 0;
	virtual bool DummyAllowed() const = 0;

	virtual void Restart() = 0;
	virtual void Quit() = 0;
	virtual const char *DemoPlayer_Play(const char *pFilename, int StorageType) = 0;
#if defined(CONF_VIDEORECORDER)
	virtual const char *DemoPlayer_Render(const char *pFilename, int StorageType, const char *pVideoName, int SpeedIndex, bool StartPaused = false, float RangeStartSec = -1.0f, float RangeEndSec = -1.0f) = 0;
	// 分段导出的后台拼接是否仍未结束：UI 借此把"渲染完成"推迟到成片真正写出之后。
	// 默认实现供不支持分段导出的实现（含测试替身）复用。
	virtual bool VideoExportConcatPending() const { return false; }
	// 最近一次拼接是否失败：失败时 UI 不得宣称渲染完成。
	virtual bool VideoExportConcatFailed() const { return false; }
#endif
	virtual void DemoRecorder_Start(const char *pFilename, bool WithTimestamp, int Recorder) = 0;
	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual void DemoRecorder_UpdateReplayRecorder() = 0;
	virtual class IDemoRecorder *DemoRecorder(int Recorder) = 0;

	// 把一份外部合成的快照当作当前快照装进客户端（合影渲染用）。
	// pData/Size 是完整的 CSnapshot 序列化数据；Tick 用于时间轴。
	// 默认实现什么都不做，供测试替身与不支持该路径的实现复用。
	virtual bool ApplySyntheticSnapshot(const void *pData, int Size, int Tick) { return false; }
	// 退出合成快照：清空快照槽，恢复普通渲染。
	virtual void ClearSyntheticSnapshot() {}
	// 合影截图请求：本帧渲染完成后把画面存成 PNG，再退出合成快照模式。
	// Scale > 1 时整帧渲染到 N 倍离屏目标再读回，输出尺寸 = 窗口渲染尺寸 × N。
	// 返回 false 表示没有挂起的请求可取消。
	virtual bool RequestPoseScreenshot(const char *pFilename, int Scale = 1, bool KeepSession = false)
	{
		(void)Scale;
		return false;
	}
	// 合影静帧抓帧窗口：为真时渲染的是「成片」，HUD 与聊天必须让位。
	// 只覆盖真正出图的那几帧（含跳过菜单帧的预热），不改变观看模式。
	virtual bool PoseCaptureActive() const { return false; }

	virtual void AutoScreenshot_Start() = 0;
	virtual void AutoStatScreenshot_Start() = 0;
	virtual void AutoCSV_Start() = 0;
	virtual void ServerBrowserUpdate() = 0;

	// gfx
	virtual void Notify(const char *pTitle, const char *pMessage) = 0;
	virtual void OnWindowResize() = 0;

	virtual void UpdateAndSwap() = 0;

	// networking
	virtual void EnterGame(int Conn) = 0;

	//
	virtual const NETADDR *ServerAddress() const = 0;
	virtual int ConnectNetTypes() const = 0;
	virtual const char *ConnectAddressString() const = 0;
	virtual const char *MapDownloadName() const = 0;
	virtual int MapDownloadAmount() const = 0;
	virtual int MapDownloadTotalsize() const = 0;

	// input
	virtual int *GetInput(int Tick, int IsDummy = 0) const = 0;

	// remote console
	virtual void RconAuth(const char *pUsername, const char *pPassword, bool Dummy) = 0;
	virtual bool RconAuthed() const = 0;
	virtual bool UseTempRconCommands() const = 0;
	virtual void Rcon(const char *pLine) = 0;
	virtual bool ReceivingRconCommands() const = 0;
	virtual float GotRconCommandsPercentage() const = 0;
	virtual bool ReceivingMaplist() const = 0;
	virtual float GotMaplistPercentage() const = 0;
	virtual const std::vector<std::string> &MaplistEntries() const = 0;

	// server info
	virtual const class CServerInfo &ServerInfo() const = 0;
	virtual void GetServerInfo(class CServerInfo *pServerInfo) const = 0;
	virtual bool ServerCapAnyPlayerFlag() const = 0;

	enum class EPredictionMarginState
	{
		STABLE,
		IGNORED_SPIKE,
		UNSTABLE,
	};

	virtual int GetPredictionTime() = 0;
	virtual int GetPredictionTick() = 0;
	virtual EPredictionMarginState PredictionMarginState() const = 0;
	virtual float PingMs() const = 0;
	virtual float PredictionLeadMs() const = 0;
	virtual float PredictionMarginMs() const = 0;
	virtual float PredictionJitterMs() const = 0;
	virtual float GameTimeMarginMs() const = 0;
	virtual bool IsGameConnectionAlive() const = 0;
	virtual void NetStatsSnapshot(NETSTATS &Prev, NETSTATS &Current, std::chrono::nanoseconds &LastUpdate) const = 0;
	virtual void SnapshotStats(SClientSnapshotStats &Stats) const = 0;
	virtual int PendingResendCount() const = 0;

	// snapshot interface

	enum
	{
		SNAP_CURRENT = 0,
		SNAP_PREV = 1,
		NUM_SNAPSHOT_TYPES = 2,
	};

	// TODO: Refactor: should redo this a bit i think, too many virtual calls
	virtual int SnapNumItems(int SnapId) const = 0;
	virtual const void *SnapFindItem(int SnapId, int Type, int Id) const = 0;
	virtual CSnapItem SnapGetItem(int SnapId, int Index) const = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;
	virtual void SnapSetStaticsize7(int ItemType, int Size) = 0;

	virtual int SendMsg(int Conn, CMsgPacker *pMsg, int Flags) = 0;
	virtual int SendMsgActive(CMsgPacker *pMsg, int Flags) = 0;

	template<class T>
	int SendPackMsgActive(T *pMsg, int Flags, bool NoTranslate = false)
	{
		CMsgPacker Packer(T::ms_MsgId, false, NoTranslate);
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsgActive(&Packer, Flags);
	}

	template<class T>
	int SendPackMsg(int Conn, T *pMsg, int Flags, bool NoTranslate = false)
	{
		CMsgPacker Packer(T::ms_MsgId, false, NoTranslate);
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(Conn, &Packer, Flags);
	}

	//
	virtual const char *PlayerName() const = 0;
	virtual const char *DummyName() = 0;
	virtual const char *ErrorString() const = 0;
	virtual const char *LatestVersion() const = 0;
	virtual bool ConnectionProblems() const = 0;
	virtual float PacketLoss() const = 0;

	virtual IGraphics::CTextureHandle GetDebugFont() const = 0; // TODO: remove this function

	// DDRace

	virtual const char *GetCurrentMap() const = 0;
	virtual const char *GetCurrentMapPath() const = 0;

	// 合影会话：用合成快照占住回放态，和 demo 播放互斥。
	virtual bool PoseSessionActive() const { return false; }

	// .poses 不含地图字段。引擎在没有可用地图时把这份合影留在这里，
	// 界面取走后弹出地图选择框再执行同一条 poses_open；默认实现给测试替身用。
	virtual const char *PendingPoseMapPath() const { return ""; }
	virtual void ClearPendingPoseMapPath() {}

	// 合影预览的取景归会话持有：预览 HUD 直接改它，相机每帧从这里取。
	// 这样缩放/平移不必发明控制台命令（玩家记不住），也不会被常规跟随逻辑拉回角色。
	virtual bool PoseCameraView(vec2 *pCenter, float *pZoom) const { return false; }
	virtual void PoseCameraZoomBy(float Factor) {}
	virtual void PoseCameraPanFraction(float FracX, float FracY) {}
	virtual void PoseCameraFit() {}
	// 相对适配取景的放大倍数：1.0 = 全部 Tee 在画面内，2.0 = 放大到两倍。
	virtual float PoseCameraZoomLevel() const { return 1.0f; }
	// 当前会话的 .poses 路径：HUD 用它拼输出文件名，而不是让玩家再挑一次文件。
	virtual const char *PoseSource() const { return ""; }
	// 在当前取景上存一张。KeepSession=true 是 HUD 的「拍一张」（可以连拍、看完再决定），
	// false 是「渲染」（出图即收尾，和 poses_render 的语义一致）。
	// Sharpen：成片锐化强度 0-100，0 = 不处理。属于拍摄参数而不是全局配置，
	// 所以跟着这次请求走，不额外发明一个需要同步的状态。
	// EntityLayer：这次拍摄是否用实体层铺满背景。必须显式传，
	// 不能依赖 g_Config 的副作用——预览 HUD 的开关和实际渲染曾经对不上。
	virtual bool RequestPoseCapture(const char *pFilename, int Scale, bool KeepSession, int Sharpen = 0, bool EntityLayer = false)
	{
		(void)pFilename;
		(void)Scale;
		(void)KeepSession;
		(void)Sharpen;
		(void)EntityLayer;
		return false;
	}

	// 合影皮肤准备度：快照里引用的皮肤是否都已可绘制。
	// 合成快照会让 CPlayers 按名字取皮肤，而皮肤是异步加载的；没等加载完就抓帧，
	// 社区皮肤还停在 PENDING，CPlayers 会回退 default——成片与预览就对不上了。
	// 引擎在抓帧前调用它（并顺带驱动加载），返回 false 表示还要再等一帧。
	virtual bool PoseSkinsReady() { return true; }

	const char *News() const { return m_aNews; }
	int Points() const { return m_Points; }
	int64_t ReconnectTime() const { return m_ReconnectTime; }
	void SetReconnectTime(int64_t ReconnectTime) { m_ReconnectTime = ReconnectTime; }

	virtual bool IsSixup() const = 0;

	virtual void RaceRecord_Start(const char *pFilename) = 0;
	virtual void RaceRecord_Stop() = 0;
	virtual bool RaceRecord_IsRecording() = 0;
	virtual EDemoMarkerResult AddDemoMarker() = 0;

	virtual void DemoSliceBegin() = 0;
	virtual void DemoSliceEnd() = 0;
	virtual bool DemoSlice(const char *pDstPath, CLIENTFUNC_FILTER pfnFilter, void *pUser) = 0;
	virtual bool DemoSlice(const char *pDstPath, const std::vector<SDemoSliceSegment> &vSegments, CLIENTFUNC_FILTER pfnFilter, void *pUser) = 0;
	virtual void SaveReplay(int Length, const char *pFilename = "") = 0;

	enum class EInfoState
	{
		LOADING,
		SUCCESS,
		ERROR,
	};
	virtual EInfoState InfoState() const = 0;
	virtual void RequestDDNetInfo() = 0;
	virtual bool EditorHasUnsavedData() const = 0;

	virtual void GenerateTimeoutSeed() = 0;

	virtual IFriends *Foes() = 0;

	virtual void GetSmoothTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount) = 0;
	// TClient
	virtual void GetSmoothFreezeTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount) = 0;

	virtual void AddWarning(const SWarning &Warning) = 0;
	virtual std::optional<SWarning> CurrentWarning() = 0;

	virtual CChecksumData *ChecksumData() = 0;
	virtual int UdpConnectivity(int NetType) = 0;

	/**
	 * Opens a link in the browser.
	 *
	 * @param pLink The link to open in a browser.
	 *
	 * @return `true` on success, `false` on failure.
	 *
	 * @remark This may not be called with untrusted input or it'll result in arbitrary code execution, especially on Windows.
	 */
	virtual bool ViewLink(const char *pLink) = 0;
	/**
	 * Opens a file or directory with the default program.
	 *
	 * @param pFilename The file or folder to open with the default program.
	 *
	 * @return `true` on success, `false` on failure.
	 *
	 * @remark This may not be called with untrusted input or it'll result in arbitrary code execution, especially on Windows.
	 */
	virtual bool ViewFile(const char *pFilename) = 0;

#if defined(CONF_FAMILY_WINDOWS)
	virtual void ShellRegister() = 0;
	virtual void ShellUnregister() = 0;
#endif

	virtual std::optional<int> ShowMessageBox(const IGraphics::CMessageBox &MessageBox) = 0;
	virtual void GetGpuInfoString(char (&aGpuInfo)[512]) = 0;

	// TClient
	bool m_IsLocalFrozen = false;
};

class IGameClient : public IInterface
{
	MACRO_INTERFACE("gameclient")
protected:
public:
	// TClient
	virtual bool CheckNewInput() = 0;
	virtual bool IsFastInputActive() const = 0;
	virtual void SetConnectInfo(const NETADDR *pAddress) = 0;

	virtual void OnConsoleInit() = 0;

	virtual void OnRconType(bool UsernameReq) = 0;
	virtual void OnRconLine(const char *pLine) = 0;
	virtual void OnInit() = 0;
	virtual void InvalidateSnapshot() = 0;
	virtual void OnNewSnapshot(bool DummySwapped) = 0;
	virtual void OnEnterGame() = 0;
	virtual void OnShutdown() = 0;
	virtual void OnRender() = 0;
	virtual void OnUpdate() = 0;
	// 连接资源在常规主循环推进，编辑器占用游戏更新时也不能暂停准备。
	virtual void OnConnectionResourceUpdate() {}
	// 每次替换地图前都通知，即使客户端仍处于加载状态。
	virtual void OnMapLoadStart() {}
	// Render-only idle throttle requested by the game UI; 0 keeps the configured rate.
	virtual int RenderThrottleRefreshRate() const = 0;
	virtual void OnStateChange(int NewState, int OldState) = 0;
	virtual void OnScreenshotTaken(class CImageInfo &&Image, const char *pPath) = 0;
	virtual IGraphics::FScreenshotProcessor ScreenshotProcessor() = 0;
	// QmClient 合影文件首次打开时设置自动取景；具体相机实现属于游戏客户端层。
	virtual void SetPoseCamera(vec2 Center, float Zoom) = 0;
	// 合影皮肤准备度：合成快照里引用的皮肤是否都已可绘制。
	// 合影没有 demo 播放，皮肤只能按名字异步加载；没等加载完就抓帧，
	// 社区皮肤会以 default 出现在成片里。返回 false 表示还要再等。
	virtual bool PoseSkinsReady() { return true; }
	virtual void OnConnected() = 0;
	virtual void OnMessage(int MsgId, CUnpacker *pUnpacker, int Conn, bool Dummy) = 0;
	virtual void OnClientBrandsMessage(CUnpacker *pUnpacker) = 0;
	virtual bool OnDemoPlaybackMessage(int MsgId, CUnpacker *pUnpacker) = 0;
	virtual void ResetDemoPlaybackState() = 0;
	virtual void OnPredict() = 0;
	virtual void OnActivateEditor() = 0;
	virtual void OnWindowResize() = 0;

	virtual int OnSnapInput(int *pData, bool Dummy, bool Force) = 0;
	virtual void PrepareInputForSend(int *pData, int Size, bool Dummy) = 0;
	virtual void OnDummySwap() = 0;
	virtual void SendDummyInfo(bool Start) = 0;

	virtual const char *GetItemName(int Type) const = 0;
	virtual const char *Version() const = 0;
	virtual const char *NetVersion() const = 0;
	virtual const char *NetVersion7() const = 0;
	virtual int DDNetVersion() const = 0;
	virtual const char *DDNetVersionStr() const = 0;

	virtual void OnDummyDisconnect() = 0;
	virtual void OnDummyManualDisconnect() = 0;
	virtual void DummyResetInput() = 0;
	virtual void Echo(const char *pString) = 0;

	virtual bool CanDisplayWarning() const = 0;
	virtual bool PrepareForShutdown(bool Force) = 0;
	virtual void RenderShutdownMessage() = 0;

	virtual CNetObjHandler *GetNetObjHandler() = 0;
	virtual protocol7::CNetObjHandler *GetNetObjHandler7() = 0;

	virtual int ClientVersion7() const = 0;

	virtual void ApplySkin7InfoFromSnapObj(const protocol7::CNetObj_De_ClientInfo *pObj, int ClientId) = 0;
	virtual int OnDemoRecSnap7(CSnapshot *pFrom, CSnapshotBuffer *pTo, int Conn) = 0;
	virtual int TranslateSnap(CSnapshotBuffer *pSnapDstSix, CSnapshot *pSnapSrcSeven, int Conn, bool Dummy) = 0;
	virtual void ProcessDemoSnapshot(CSnapshot *pSnap) = 0;

	virtual void InitializeLanguage() = 0;

	virtual void ForceUpdateConsoleRemoteCompletionSuggestions() = 0;
	virtual void RenderQmMonitoringHud(float GraphX, float GraphSpacing) = 0;
};

extern IGameClient *CreateGameClient();
#endif
