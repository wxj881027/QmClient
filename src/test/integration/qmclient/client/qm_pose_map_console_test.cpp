#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/pose_map.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

TEST(QmPoseMapConsoleIntegration, RenderPreservesQuotedPathArguments)
{
	constexpr const char *POSES = R"(demos/group "two".poses)";
	constexpr const char *MAP = R"(C:\maps\group "two".map)";
	constexpr const char *OUTPUT = R"(album\morning)";
	std::array<std::string, 5> aArguments;
	int CallCount = 0;
	struct SCapture
	{
		int &m_CallCount;
		std::array<std::string, 5> &m_aArguments;
	} Capture{CallCount, aArguments};

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	pConsole->Register("poses_render", "s[poses] ?s[output] ?s[map] ?i[scale] ?i[entities]", CFGFLAG_CLIENT,
		[](IConsole::IResult *pResult, void *pUser) {
			auto &Result = *static_cast<SCapture *>(pUser);
			++Result.m_CallCount;
			ASSERT_EQ(pResult->NumArguments(), static_cast<int>(Result.m_aArguments.size()));
			for(size_t Index = 0; Index < Result.m_aArguments.size(); ++Index)
				Result.m_aArguments[Index] = pResult->GetString(static_cast<int>(Index));
		},
		&Capture, "");

	const auto Command = qm_pose_map::BuildCommand(qm_pose_map::EAction::RENDER, POSES, MAP, OUTPUT, qm_pose_map::EOutput::WEBP, 4, true);
	pConsole->ExecuteLine(Command.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, false);

	EXPECT_EQ(CallCount, 1);
	EXPECT_EQ(aArguments[0], POSES);
	EXPECT_EQ(aArguments[1], std::string(OUTPUT) + ".webp");
	EXPECT_EQ(aArguments[2], MAP);
	EXPECT_EQ(aArguments[3], "4");
	EXPECT_EQ(aArguments[4], "1");
}
