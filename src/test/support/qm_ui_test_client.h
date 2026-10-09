// 只提供独立 UI 测试需要的客户端时钟；其它客户端职责若被调用则立即失败。
#ifndef TEST_SUPPORT_QM_UI_TEST_CLIENT_H
#define TEST_SUPPORT_QM_UI_TEST_CLIENT_H

#include <engine/client.h>

#include <stdexcept>

namespace qm_ui_test
{
	class CTestUiClient final : public IClient
	{
	public:
		void AdvanceFrame()
		{
			++m_PerfFrame;
			m_RenderFrameTime = 1.0f / 60.0f;
			m_LocalTime += m_RenderFrameTime;
			m_GlobalTime += m_RenderFrameTime;
		}

		void Connect(const char *pAddress, const char *pPassword = nullptr) override { throw std::logic_error("unexpected UI client boundary call"); }
		void Disconnect() override { throw std::logic_error("unexpected UI client boundary call"); }
		void DummyDisconnect(const char *pReason) override { throw std::logic_error("unexpected UI client boundary call"); }
		void DummyConnect() override { throw std::logic_error("unexpected UI client boundary call"); }
		bool DummyConnected() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool DummyConnecting() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool DummyConnectingDelayed() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool DummyAllowed() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void Restart() override { throw std::logic_error("unexpected UI client boundary call"); }
		void Quit() override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *DemoPlayer_Play(const char *pFilename, int StorageType) override { throw std::logic_error("unexpected UI client boundary call"); }
#if defined(CONF_VIDEORECORDER)
		const char *DemoPlayer_Render(const char *pFilename, int StorageType, const char *pVideoName, int SpeedIndex, bool StartPaused = false) override { throw std::logic_error("unexpected UI client boundary call"); }
#endif
		void DemoRecorder_Start(const char *pFilename, bool WithTimestamp, int Recorder) override { throw std::logic_error("unexpected UI client boundary call"); }
		void DemoRecorder_HandleAutoStart() override { throw std::logic_error("unexpected UI client boundary call"); }
		void DemoRecorder_UpdateReplayRecorder() override { throw std::logic_error("unexpected UI client boundary call"); }
		class IDemoRecorder *DemoRecorder(int Recorder) override { throw std::logic_error("unexpected UI client boundary call"); }
		void AutoScreenshot_Start() override { throw std::logic_error("unexpected UI client boundary call"); }
		void AutoStatScreenshot_Start() override { throw std::logic_error("unexpected UI client boundary call"); }
		void AutoCSV_Start() override { throw std::logic_error("unexpected UI client boundary call"); }
		void ServerBrowserUpdate() override { throw std::logic_error("unexpected UI client boundary call"); }
		void Notify(const char *pTitle, const char *pMessage) override { throw std::logic_error("unexpected UI client boundary call"); }
		void OnWindowResize() override {}
		void UpdateAndSwap() override { throw std::logic_error("unexpected UI client boundary call"); }
		void EnterGame(int Conn) override { throw std::logic_error("unexpected UI client boundary call"); }
		const NETADDR *ServerAddress() const override { throw std::logic_error("unexpected UI client boundary call"); }
		int ConnectNetTypes() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *ConnectAddressString() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *MapDownloadName() const override { throw std::logic_error("unexpected UI client boundary call"); }
		int MapDownloadAmount() const override { throw std::logic_error("unexpected UI client boundary call"); }
		int MapDownloadTotalsize() const override { throw std::logic_error("unexpected UI client boundary call"); }
		int *GetInput(int Tick, int IsDummy = 0) const override { throw std::logic_error("unexpected UI client boundary call"); }
		void RconAuth(const char *pUsername, const char *pPassword, bool Dummy) override { throw std::logic_error("unexpected UI client boundary call"); }
		bool RconAuthed() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool UseTempRconCommands() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void Rcon(const char *pLine) override { throw std::logic_error("unexpected UI client boundary call"); }
		bool ReceivingRconCommands() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float GotRconCommandsPercentage() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool ReceivingMaplist() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float GotMaplistPercentage() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const std::vector<std::string> &MaplistEntries() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const class CServerInfo &ServerInfo() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void GetServerInfo(class CServerInfo *pServerInfo) const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool ServerCapAnyPlayerFlag() const override { throw std::logic_error("unexpected UI client boundary call"); }
		int GetPredictionTime() override { throw std::logic_error("unexpected UI client boundary call"); }
		int GetPredictionTick() override { throw std::logic_error("unexpected UI client boundary call"); }
		EPredictionMarginState PredictionMarginState() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float PingMs() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float PredictionLeadMs() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float PredictionMarginMs() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float PredictionJitterMs() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float GameTimeMarginMs() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool IsGameConnectionAlive() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void NetStatsSnapshot(NETSTATS &Prev, NETSTATS &Current, std::chrono::nanoseconds &LastUpdate) const override { throw std::logic_error("unexpected UI client boundary call"); }
		void SnapshotStats(SClientSnapshotStats &Stats) const override { throw std::logic_error("unexpected UI client boundary call"); }
		int PendingResendCount() const override { throw std::logic_error("unexpected UI client boundary call"); }
		int SnapNumItems(int SnapId) const override { throw std::logic_error("unexpected UI client boundary call"); }
		const void *SnapFindItem(int SnapId, int Type, int Id) const override { throw std::logic_error("unexpected UI client boundary call"); }
		CSnapItem SnapGetItem(int SnapId, int Index) const override { throw std::logic_error("unexpected UI client boundary call"); }
		void SnapSetStaticsize(int ItemType, int Size) override { throw std::logic_error("unexpected UI client boundary call"); }
		void SnapSetStaticsize7(int ItemType, int Size) override { throw std::logic_error("unexpected UI client boundary call"); }
		int SendMsg(int Conn, CMsgPacker *pMsg, int Flags) override { throw std::logic_error("unexpected UI client boundary call"); }
		int SendMsgActive(CMsgPacker *pMsg, int Flags) override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *PlayerName() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *DummyName() override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *ErrorString() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *LatestVersion() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool ConnectionProblems() const override { throw std::logic_error("unexpected UI client boundary call"); }
		float PacketLoss() const override { throw std::logic_error("unexpected UI client boundary call"); }
		IGraphics::CTextureHandle GetDebugFont() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *GetCurrentMap() const override { throw std::logic_error("unexpected UI client boundary call"); }
		const char *GetCurrentMapPath() const override { throw std::logic_error("unexpected UI client boundary call"); }
		bool IsSixup() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void RaceRecord_Start(const char *pFilename) override { throw std::logic_error("unexpected UI client boundary call"); }
		void RaceRecord_Stop() override { throw std::logic_error("unexpected UI client boundary call"); }
		bool RaceRecord_IsRecording() override { throw std::logic_error("unexpected UI client boundary call"); }
		EDemoMarkerResult AddDemoMarker() override { throw std::logic_error("unexpected UI client boundary call"); }
		void DemoSliceBegin() override { throw std::logic_error("unexpected UI client boundary call"); }
		void DemoSliceEnd() override { throw std::logic_error("unexpected UI client boundary call"); }
		bool DemoSlice(const char *pDstPath, CLIENTFUNC_FILTER pfnFilter, void *pUser) override { throw std::logic_error("unexpected UI client boundary call"); }
		bool DemoSlice(const char *pDstPath, const std::vector<SDemoSliceSegment> &vSegments, CLIENTFUNC_FILTER pfnFilter, void *pUser) override { throw std::logic_error("unexpected UI client boundary call"); }
		void SaveReplay(int Length, const char *pFilename = "") override { throw std::logic_error("unexpected UI client boundary call"); }
		EInfoState InfoState() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void RequestDDNetInfo() override { throw std::logic_error("unexpected UI client boundary call"); }
		bool EditorHasUnsavedData() const override { throw std::logic_error("unexpected UI client boundary call"); }
		void GenerateTimeoutSeed() override { throw std::logic_error("unexpected UI client boundary call"); }
		IFriends *Foes() override { throw std::logic_error("unexpected UI client boundary call"); }
		void GetSmoothTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount) override { throw std::logic_error("unexpected UI client boundary call"); }
		void GetSmoothFreezeTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount) override { throw std::logic_error("unexpected UI client boundary call"); }
		void AddWarning(const SWarning &Warning) override { throw std::logic_error("unexpected UI client boundary call"); }
		std::optional<SWarning> CurrentWarning() override { throw std::logic_error("unexpected UI client boundary call"); }
		CChecksumData *ChecksumData() override { throw std::logic_error("unexpected UI client boundary call"); }
		int UdpConnectivity(int NetType) override { throw std::logic_error("unexpected UI client boundary call"); }
		bool ViewLink(const char *pLink) override { throw std::logic_error("unexpected UI client boundary call"); }
		bool ViewFile(const char *pFilename) override { throw std::logic_error("unexpected UI client boundary call"); }
#if defined(CONF_FAMILY_WINDOWS)
		void ShellRegister() override { throw std::logic_error("unexpected UI client boundary call"); }
		void ShellUnregister() override { throw std::logic_error("unexpected UI client boundary call"); }
#endif
		std::optional<int> ShowMessageBox(const IGraphics::CMessageBox &MessageBox) override { throw std::logic_error("unexpected UI client boundary call"); }
		void GetGpuInfoString(char (&aGpuInfo)[512]) override { throw std::logic_error("unexpected UI client boundary call"); }
	};
}

#endif
