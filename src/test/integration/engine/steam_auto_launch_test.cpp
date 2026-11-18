#include <base/detect.h>

#if defined(CONF_PLATFORM_LINUX)

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/steam.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <test/test.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
	class CSteamPathOverride
	{
		char m_aOriginal[sizeof(g_Config.m_QmSteamClientPath)];

	public:
		explicit CSteamPathOverride(const char *pPath)
		{
			str_copy(m_aOriginal, g_Config.m_QmSteamClientPath);
			str_copy(g_Config.m_QmSteamClientPath, pPath);
		}

		~CSteamPathOverride()
		{
			str_copy(g_Config.m_QmSteamClientPath, m_aOriginal);
		}
	};
}

TEST(SteamAutoLaunch, ExitedClientProcessIsReaped)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto Executable = std::filesystem::absolute(Info.StoragePath()) / "steam.sh";
	const auto PidFile = Executable.string() + ".pid";
	{
		std::ofstream Script(Executable);
		Script << "#!/bin/sh\nprintf '%s\\n%s\\n' \"$$\" \"$1\" > \"$0.pid\"\n";
		Script.close();
		ASSERT_TRUE(Script);
	}
	ASSERT_EQ(chmod(Executable.c_str(), S_IRUSR | S_IWUSR | S_IXUSR), 0);
	ASSERT_LT(Executable.native().size(), sizeof(g_Config.m_QmSteamClientPath));
	const CSteamPathOverride Override(Executable.c_str());

	char aSelected[IO_MAX_PATH_LENGTH];
	ESteamClientSource Source;
	ASSERT_TRUE(SteamFindClient(aSelected, sizeof(aSelected), &Source, Executable.c_str()));
	ASSERT_EQ(Source, ESteamClientSource::MANUAL);
	ASSERT_STREQ(aSelected, Executable.c_str());
	ASSERT_TRUE(SteamOpenClient());

	using namespace std::chrono_literals;
	const auto Deadline = std::chrono::steady_clock::now() + 5s;
	pid_t Child = 0;
	std::string Argument;
	bool Reaped = false;
	while(std::chrono::steady_clock::now() < Deadline)
	{
		std::ifstream ProcessOutput(PidFile);
		if(ProcessOutput >> Child >> Argument && Child > 0 && kill(Child, 0) == -1 && errno == ESRCH)
		{
			Reaped = true;
			break;
		}
		std::this_thread::sleep_for(10ms);
	}
	if(!Reaped && Child > 0)
		waitpid(Child, nullptr, WNOHANG);
	EXPECT_GT(Child, 0) << "The test Steam process did not report its PID";
	EXPECT_EQ(Argument, "-silent");
	EXPECT_TRUE(Reaped) << "The exited Steam process remained in the process table";
}

#endif
