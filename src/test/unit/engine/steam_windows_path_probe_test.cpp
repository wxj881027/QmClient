#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/str.h>
#include <base/windows.h>

#include <gtest/gtest.h>
#include <windows.h>

#include <cstdlib>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace steam_probe
{
	bool FindClientInDefaultPath(char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile);
	bool FindClientInPath(std::wstring_view Path, char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile);
} // namespace steam_probe

TEST(SteamWindowsPathProbe, SkipsNetworkEntriesAndKeepsLocalSearchOrder)
{
	wchar_t aWindowsDirectory[1024];
	const UINT Length = GetWindowsDirectoryW(aWindowsDirectory, std::size(aWindowsDirectory));
	ASSERT_GE(Length, 3u);
	ASSERT_LT(Length, std::size(aWindowsDirectory));
	const std::wstring Root(aWindowsDirectory, 3);
	const std::wstring Path = L"\\\\unavailable\\steam;" + Root + L"Missing;\"" + Root + L"Tools;Steam\";" + Root + L"Later";
	const auto Missing = windows_wide_to_utf8((Root + L"Missing\\steam.exe").c_str());
	const auto Found = windows_wide_to_utf8((Root + L"Tools;Steam\\steam.exe").c_str());
	ASSERT_TRUE(Missing.has_value());
	ASSERT_TRUE(Found.has_value());

	char aResult[1024] = {};
	std::vector<std::string> vChecked;
	ASSERT_TRUE(steam_probe::FindClientInPath(Path, aResult, sizeof(aResult), [&](const char *pCandidate) {
		vChecked.emplace_back(pCandidate);
		return vChecked.back() == *Found;
	}));

	const std::vector<std::string> vExpected{*Missing, *Found};
	EXPECT_EQ(vChecked, vExpected);
	EXPECT_STREQ(aResult, Found->c_str());
}

TEST(SteamWindowsPathProbe, SearchesApplicationDirectoryFirst)
{
	wchar_t aImagePath[1024];
	const DWORD Length = GetModuleFileNameW(nullptr, aImagePath, std::size(aImagePath));
	ASSERT_GT(Length, 0u);
	ASSERT_LT(Length, std::size(aImagePath));
	std::wstring ExpectedPath(aImagePath, Length);
	const size_t Separator = ExpectedPath.find_last_of(L'\\');
	ASSERT_NE(Separator, std::wstring::npos);
	ExpectedPath.replace(Separator + 1, std::wstring::npos, L"steam.exe");
	const auto Expected = windows_wide_to_utf8(ExpectedPath.c_str());
	ASSERT_TRUE(Expected.has_value());

	char aResult[1024] = {};
	ASSERT_TRUE(steam_probe::FindClientInDefaultPath(aResult, sizeof(aResult), [](const char *) {
		return 1;
	}));
	EXPECT_STRCASEEQ(aResult, Expected->c_str());
}

#if GTEST_HAS_DEATH_TEST
namespace
{
	void CheckProcessSearchMode(decltype(&SetSearchPathMode) pSetSearchPathMode)
	{
		wchar_t aSystemDirectory[1024];
		const UINT Length = GetSystemDirectoryW(aSystemDirectory, std::size(aSystemDirectory));
		ASSERT_GT(Length, 3u);
		ASSERT_LT(Length, std::size(aSystemDirectory));
		const std::wstring Root(aSystemDirectory, 3);
		ASSERT_NE(SetCurrentDirectoryW(Root.c_str()), 0);
		ASSERT_NE(SetEnvironmentVariableW(L"PATH", (Root + L"QmSteamTestPath").c_str()), 0);

		const auto Current = windows_wide_to_utf8((Root + L"steam.exe").c_str());
		const auto System = windows_wide_to_utf8((std::wstring(aSystemDirectory, Length) + L"\\steam.exe").c_str());
		const auto Path = windows_wide_to_utf8((Root + L"QmSteamTestPath\\steam.exe").c_str());
		ASSERT_TRUE(Current.has_value());
		ASSERT_TRUE(System.has_value());
		ASSERT_TRUE(Path.has_value());

		const auto IsFile = [&](const char *pCandidate) {
			return str_comp_nocase(pCandidate, Current->c_str()) == 0 ||
			       str_comp_nocase(pCandidate, System->c_str()) == 0 ||
			       str_comp_nocase(pCandidate, Path->c_str()) == 0;
		};
		char aResult[1024] = {};
		ASSERT_NE(pSetSearchPathMode(BASE_SEARCH_PATH_DISABLE_SAFE_SEARCHMODE), 0);
		ASSERT_TRUE(steam_probe::FindClientInDefaultPath(aResult, sizeof(aResult), IsFile));
		EXPECT_STRCASEEQ(aResult, Current->c_str());

		ASSERT_NE(pSetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE), 0);
		ASSERT_TRUE(steam_probe::FindClientInDefaultPath(aResult, sizeof(aResult), IsFile));
		EXPECT_STRCASEEQ(aResult, System->c_str());

		ASSERT_TRUE(steam_probe::FindClientInDefaultPath(aResult, sizeof(aResult), [&](const char *pCandidate) {
			return str_comp_nocase(pCandidate, Path->c_str()) == 0;
		}));
		EXPECT_STRCASEEQ(aResult, Path->c_str());
	}
} // namespace

TEST(SteamWindowsPathProbeDeathTest, UsesProcessSearchModeBeforePath)
{
	const HMODULE Module = GetModuleHandleW(L"ntdll.dll");
	const auto pSetSearchPathMode = reinterpret_cast<decltype(&SetSearchPathMode)>(
		GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetSearchPathMode"));
	if(!pSetSearchPathMode || !Module || !GetProcAddress(Module, "RtlGetSearchPath") || !GetProcAddress(Module, "RtlReleasePath"))
		GTEST_SKIP() << "Process search order export is unavailable";

	// 当前目录、PATH 和搜索模式只在测试子进程内修改。
	ASSERT_EXIT(
		{
			CheckProcessSearchMode(pSetSearchPathMode);
			std::exit(testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
		},
		testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif
#endif
