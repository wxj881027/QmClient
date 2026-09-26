#include <gtest/gtest.h>
#include <qm-nmt-hook/qm_netease_bootstrap.h>
#include <qm-nmt-hook/qm_netease_cdp.h>
#include <qm-nmt-hook/qm_netease_cdp_client.h>

using namespace QmNeteaseCdp;

TEST(QmNeteaseCdp, SelectsOnlyLoopbackOrpheusTargetWithJsonParser)
{
	const std::string Json = R"([{"url":"devtools://devtools","webSocketDebuggerUrl":"ws://127.0.0.1:40001/devtools/page/a"},{"url":"orpheus://main","webSocketDebuggerUrl":"ws://127.0.0.1:40002/devtools/page/b","processId":42},{"url":"orpheus://remote","webSocketDebuggerUrl":"ws://192.168.1.2:40003/devtools/page/c"}])";
	std::vector<SCdpTarget> Targets;
	ASSERT_TRUE(ParseTargetList(Json, &Targets));
	ASSERT_EQ(Targets.size(), 1u);
	EXPECT_EQ(Targets[0].m_Url, "orpheus://main");
	EXPECT_EQ(Targets[0].m_Port, 40002);
	EXPECT_EQ(Targets[0].m_ProcessId, 42u);
}

TEST(QmNeteaseCdp, RequiresTargetPortAndListenerPidToMatchCloudMusic)
{
	SCdpTarget Target;
	Target.m_Url = "orpheus://main";
	Target.m_WebSocketDebuggerUrl = "ws://127.0.0.1:40002/devtools/page/b";
	Target.m_Port = 40002;

	// Chromium 的 /json 通常不提供 processId，此时监听端口 owner 是身份依据。
	EXPECT_TRUE(IsTrustedTargetForProcess(Target, 40002, 42, 42));
	EXPECT_FALSE(IsTrustedTargetForProcess(Target, 40002, 84, 42));
	EXPECT_FALSE(IsTrustedTargetForProcess(Target, 40003, 42, 42));

	Target.m_ProcessId = 84;
	EXPECT_FALSE(IsTrustedTargetForProcess(Target, 40002, 42, 42));
	Target.m_ProcessId = 42;
	EXPECT_TRUE(IsTrustedTargetForProcess(Target, 40002, 42, 42));
}

TEST(QmNeteaseCdp, RejectsMalformedOrNonLoopbackWebSocket)
{
	std::vector<SCdpTarget> Targets;
	EXPECT_FALSE(ParseTargetList("[{\"url\":\"orpheus://main\",\"webSocketDebuggerUrl\":\"ws://127.0.0.1:0/x\"}]", &Targets));
	EXPECT_FALSE(ParseLoopbackWebSocketUrl("ws://localhost:40000/x"));
	EXPECT_TRUE(ParseLoopbackWebSocketUrl("ws://127.0.0.1:40000/x"));
}

TEST(QmNeteaseCdp, DoesNotDuplicateExplicitDebuggingPort)
{
	uint16_t Port = 0;
	EXPECT_TRUE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address=127.0.0.1 --remote-debugging-port=41234", &Port));
	EXPECT_EQ(Port, 41234);
	EXPECT_EQ(AddLoopbackDebuggingPort(L"cloudmusic.exe --remote-debugging-port=41234", 41235), L"cloudmusic.exe --remote-debugging-port=41234");
	EXPECT_EQ(AddLoopbackDebuggingPort(L"cloudmusic.exe", 41235), L"cloudmusic.exe --remote-debugging-address=127.0.0.1 --remote-debugging-port=41235");
}

TEST(QmNeteaseCdp, SkipsMalformedPortBeforeValidPort)
{
	uint16_t Port = 0;
	EXPECT_TRUE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-port=bad --remote-debugging-port=41234", &Port));
	EXPECT_EQ(Port, 41234);
	EXPECT_TRUE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-port --other-option --remote-debugging-port 41235", &Port));
	EXPECT_EQ(Port, 41235);
}

TEST(QmNeteaseCdp, RejectsExplicitNonLoopbackDebuggingAddress)
{
	uint16_t Port = 0;
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address=0.0.0.0 --remote-debugging-port=41234", &Port));
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address=192.168.1.2 --remote-debugging-port=41234", &Port));
}

TEST(QmNeteaseCdp, RejectsMissingExplicitDebuggingAddressValue)
{
	uint16_t Port = 0;
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address --remote-debugging-port=41234", &Port));
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address= --remote-debugging-port=41234", &Port));
}

TEST(QmNeteaseCdp, UsesSharedDynamicPortCandidateSequence)
{
	EXPECT_EQ(DynamicPortCandidate(0, 0), DYNAMIC_PORT_MIN);
	EXPECT_EQ(DynamicPortCandidate(1234, 0), (uint16_t)(DYNAMIC_PORT_MIN + 1234));
	EXPECT_EQ(DynamicPortCandidate(1234, DYNAMIC_PORT_CANDIDATE_COUNT - 1), (uint16_t)(DYNAMIC_PORT_MIN + 1234 + DYNAMIC_PORT_CANDIDATE_COUNT - 1));
	EXPECT_LT(DYNAMIC_PORT_CANDIDATE_COUNT, DYNAMIC_PORT_RANGE);
}

#if defined(_WIN32)
TEST(QmNeteaseBootstrap, BuildsLoopbackArgumentsForMainCloudMusicProcess)
{
	std::wstring Patched;
	EXPECT_TRUE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(L"D:\\CloudMusic\\cloudmusic.exe", L"\"D:\\CloudMusic\\cloudmusic.exe\"", 45380, &Patched));
	EXPECT_EQ(Patched, L"\"D:\\CloudMusic\\cloudmusic.exe\" --remote-debugging-address=127.0.0.1 --remote-debugging-port=45380");
	EXPECT_EQ(QmNeteaseBootstrap::EarlyLoopbackPort(25380), DynamicPortCandidate(25380, 0));
}

TEST(QmNeteaseBootstrap, SkipsCefChildProcessesAndKeepsExplicitPort)
{
	std::wstring Patched;
	EXPECT_FALSE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(L"D:\\CloudMusic\\cloudmusic.exe", L"cloudmusic.exe --type=renderer", 45380, &Patched));
	EXPECT_FALSE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(L"D:\\CloudMusic\\cloudmusic.exe", L"cloudmusic.exe --type gpu-process", 45380, &Patched));
	EXPECT_FALSE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(L"D:\\CloudMusic\\cloudmusic-helper.exe", L"cloudmusic-helper.exe", 45380, &Patched));

	const std::wstring Existing = L"cloudmusic.exe --remote-debugging-address=127.0.0.1 --remote-debugging-port=41234";
	EXPECT_TRUE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(L"D:\\CloudMusic\\cloudmusic.exe", Existing, 45380, &Patched));
	EXPECT_EQ(Patched, Existing);
}
#endif

TEST(QmNeteaseCdp, FrontendScriptUsesWebpackPlayerStateInsteadOfDesktopLyrics)
{
	const std::string Script = BuildInstallHookScript();
	EXPECT_NE(Script.find("getWebpackRequire"), std::string::npos);
	EXPECT_NE(Script.find("audioPlayerPlayProgress$"), std::string::npos);
	EXPECT_NE(Script.find("onlineResourceId"), std::string::npos);
	EXPECT_NE(Script.find("resourceTrackId"), std::string::npos);
	EXPECT_NE(Script.find("playing.playingState === 2"), std::string::npos);
	EXPECT_NE(Script.find("playing.playingState === 1"), std::string::npos);
	EXPECT_NE(Script.find("previousSubscription.unsubscribe()"), std::string::npos);
	EXPECT_NE(Script.find("positionMs: Math.max(0, seconds * 1000)"), std::string::npos);
	EXPECT_NE(Script.find("const bridgeVersion = \"13\""), std::string::npos);
	EXPECT_NE(Script.find("const firstProgress = !window.__QM_NCM_NATIVE_PROGRESS__"), std::string::npos);
	EXPECT_NE(Script.find("reportProgress(firstProgress)"), std::string::npos);
	EXPECT_EQ(Script.find("reportProgress(true)"), std::string::npos);
	EXPECT_EQ(Script.find("setting.showLyric"), std::string::npos);
	EXPECT_EQ(Script.find("DesktopLyrics"), std::string::npos);
	EXPECT_EQ(Script.find("audioplayer.onPlayProgress"), std::string::npos);
	EXPECT_EQ(Script.find("/api/song/lyric"), std::string::npos);
}

TEST(QmNeteaseCdp, FrontendScriptPublishesStableModernStoreLyrics)
{
	const std::string Script = BuildInstallHookScript();
	EXPECT_NE(Script.find("async:lyric"), std::string::npos);
	EXPECT_NE(Script.find("lyricLines"), std::string::npos);
	EXPECT_NE(Script.find("currentUsedLyricVersion"), std::string::npos);
	EXPECT_NE(Script.find("serializeLyricLines"), std::string::npos);
	EXPECT_NE(Script.find("lyric.isLoading"), std::string::npos);
	EXPECT_NE(Script.find("candidateCount < 3"), std::string::npos);
	EXPECT_NE(Script.find("rawLyrics:{lrc}"), std::string::npos);
	EXPECT_NE(Script.find("async:lyric/fetchLyric"), std::string::npos);
	EXPECT_NE(Script.find("payload:{force:true}"), std::string::npos);
	EXPECT_NE(Script.find("fetchRequestedAt"), std::string::npos);
}

TEST(QmNeteaseCdp, FrontendScriptRefreshesLyricsBeforePublishingForCurrentSong)
{
	const std::string Script = BuildInstallHookScript();
	const size_t RequireCurrentSongFetch = Script.find("if (state.fetchSongId !== songId)");
	const size_t WaitForCurrentSongFetch = Script.find("if (state.fetchPending || !state.fetchCompleted)");
	const size_t SerializeLyrics = Script.find("const lrc = serializeLyricLines");
	ASSERT_NE(RequireCurrentSongFetch, std::string::npos);
	ASSERT_NE(WaitForCurrentSongFetch, std::string::npos);
	ASSERT_NE(SerializeLyrics, std::string::npos);
	EXPECT_LT(RequireCurrentSongFetch, SerializeLyrics);
	EXPECT_LT(WaitForCurrentSongFetch, SerializeLyrics);
	EXPECT_NE(Script.find("typeof result.then === \"function\""), std::string::npos);
	EXPECT_NE(Script.find("finishStoreLyricFetch(songId, fetchGeneration, true)"), std::string::npos);
	EXPECT_NE(Script.find("finishStoreLyricFetch(songId, fetchGeneration, false)"), std::string::npos);
}

TEST(QmNeteaseCdp, FrontendScriptRetriesAStuckLyricFetch)
{
	const std::string Script = BuildInstallHookScript();
	EXPECT_EQ(Script.find("state.fetchPending || now - state.fetchRequestedAt < 30000"), std::string::npos);
	EXPECT_NE(Script.find("if (now - state.fetchRequestedAt >= 30000)"), std::string::npos);
}
