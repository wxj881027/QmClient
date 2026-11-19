#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_bootstrap.h>
#include <qm/hooks/netease/qm_netease_cdp.h>

#if defined(_WIN32)
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <memory>
#endif

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

TEST(QmNeteaseCdp, RejectsTargetListWithRawNul)
{
	const std::string Json = R"([{"url":"orpheus://main","webSocketDebuggerUrl":"ws://127.0.0.1:40002/devtools/page/b"}])";
	std::vector<SCdpTarget> Targets;
	ASSERT_TRUE(ParseTargetList(Json, &Targets));

	std::string PaddedJson = Json;
	PaddedJson.append("\0\n", 2);
	std::string Error;
	EXPECT_FALSE(ParseTargetList(PaddedJson, &Targets, &Error));
	EXPECT_TRUE(Targets.empty());
	EXPECT_EQ(Error, "invalid JSON");

	EXPECT_TRUE(ParseTargetList(Json, &Targets));
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

TEST(QmNeteaseCdp, RejectsRawSpaceInWebSocketPath)
{
	uint16_t Port = 0;
	EXPECT_FALSE(ParseLoopbackWebSocketUrl("ws://127.0.0.1:40002/devtools/page/with space", &Port));
	EXPECT_EQ(Port, 0);
	EXPECT_TRUE(ParseLoopbackWebSocketUrl("ws://127.0.0.1:40002/devtools/page/with%20space", &Port));
	EXPECT_EQ(Port, 40002);
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

TEST(QmNeteaseCdp, FindsDebuggingPortOutsideQuotedArgument)
{
	uint16_t Port = 0;
	const std::wstring_view CommandLine = LR"("D:\Cloud Music\cloudmusic.exe" --user-data-dir="D:\Music profiles --remote-debugging-port=41000 cache" --remote-debugging-port=41234)";
	ASSERT_TRUE(ParseDebuggingPort(CommandLine, &Port));
	EXPECT_EQ(Port, 41234);
}

TEST(QmNeteaseCdp, ReadsQuotedDebuggingSwitches)
{
	uint16_t Port = 0;
	const std::wstring_view CommandLine = LR"("D:\Cloud Music\cloudmusic.exe" "--remote-debugging-address"="LOCALHOST" "--remote-debugging-port=41234")";
	ASSERT_TRUE(ParseDebuggingPort(CommandLine, &Port));
	EXPECT_EQ(Port, 41234);
}

TEST(QmNeteaseCdp, RejectsExplicitNonLoopbackDebuggingAddress)
{
	uint16_t Port = 0;
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address=0.0.0.0 --remote-debugging-port=41234", &Port));
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-address=192.168.1.2 --remote-debugging-port=41234", &Port));
	EXPECT_FALSE(ParseDebuggingPort(L"cloudmusic.exe --remote-debugging-port=41234 --remote-debugging-address=0.0.0.0", &Port));
	EXPECT_EQ(Port, 0);
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

TEST(QmNeteaseBootstrap, IgnoresSwitchTextInExecutablePath)
{
	constexpr wchar_t EXECUTABLE[] = LR"(D:\CloudMusic --type=portable\cloudmusic.exe)";
	const std::wstring Command = LR"("D:\CloudMusic --type=portable\cloudmusic.exe")";
	std::wstring Patched;
	ASSERT_TRUE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(EXECUTABLE, Command, 45380, &Patched));
	EXPECT_EQ(Patched, Command + L" --remote-debugging-address=127.0.0.1 --remote-debugging-port=45380");
}

TEST(QmNeteaseBootstrap, IgnoresSwitchTextInQuotedArgumentValue)
{
	const std::wstring Command = LR"(cloudmusic.exe --profile="profile --remote-debugging-port=41234")";
	std::wstring Patched;
	ASSERT_TRUE(QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(L"cloudmusic.exe", Command, 45380, &Patched));
	EXPECT_EQ(Patched, Command + L" --remote-debugging-address=127.0.0.1 --remote-debugging-port=45380");
}

TEST(QmNeteaseBootstrap, RetriesAfterTargetSharingViolation)
{
	using namespace QmNeteaseBootstrap;
	const auto Parent = std::filesystem::current_path() / L"tmp" / L"tests";
	std::filesystem::create_directories(Parent);
	const auto Directory = Parent / (L"netease-bootstrap-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	ASSERT_TRUE(std::filesystem::create_directory(Directory));
	struct SCleanup
	{
		const std::filesystem::path &m_Directory;

		~SCleanup()
		{
			std::error_code Error;
			std::filesystem::remove_all(m_Directory, Error);
		}
	} Cleanup{Directory};

	const auto Source = Directory / BOOTSTRAP_SOURCE_NAME;
	const auto Target = Directory / BOOTSTRAP_TARGET_NAME;
	const auto WriteBootstrap = [](const std::filesystem::path &Path, const char *pRevision) {
		std::ofstream File(Path, std::ios::binary);
		File << BOOTSTRAP_MARKER << pRevision;
		File.close();
		return File.good();
	};
	ASSERT_TRUE(WriteBootstrap(Source, "new"));
	ASSERT_TRUE(WriteBootstrap(Target, "old"));
	{
		const HANDLE hLock = CreateFileW(Target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		ASSERT_NE(hLock, INVALID_HANDLE_VALUE) << GetLastError();
		const std::unique_ptr<void, decltype(&CloseHandle)> Lock(hLock, CloseHandle);
		EXPECT_EQ(EnsureInstalled(Directory.native(), Source.native()), EInstallResult::Failed);
	}
	EXPECT_EQ(EnsureInstalled(Directory.native(), Source.native()), EInstallResult::Updated);
	EXPECT_EQ(EnsureInstalled(Directory.native(), Source.native()), EInstallResult::AlreadyInstalled);
}
#endif
