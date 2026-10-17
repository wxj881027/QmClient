#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/wheel_presets.h>

#include <gtest/gtest.h>

#include <array>
#include <map>

using namespace qm_wheel_presets;

namespace
{
	struct SCapture
	{
		std::map<std::string, std::string> *m_pValues;
		const char *m_pName;
		static void OnCommand(IConsole::IResult *pResult, void *pUser)
		{
			const auto &Capture = *static_cast<SCapture *>(pUser);
			(*Capture.m_pValues)[Capture.m_pName] = pResult->GetString(0);
		}
	};
}

TEST(QmWheelPresetsConsole, IdentityAppliesDifferentValuesToMainAndDummy)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::map<std::string, std::string> Values;
	std::array<SCapture, 4> aCapture{{{&Values, "player_name"}, {&Values, "dummy_name"}, {&Values, "player_clan"}, {&Values, "player_use_custom_color"}}};
	for(auto &Capture : aCapture)
		pConsole->Register(Capture.m_pName, "s[value]", CFGFLAG_CLIENT, SCapture::OnCommand, &Capture, "");
	SPreset Preset;
	Preset.m_Type = EType::IDENTITY;
	Preset.m_aTees[0].m_aValues[NAME] = "DYL";
	Preset.m_aTees[1].m_aValues[NAME] = "YL";
	Preset.m_aTees[0].m_aValues[CLAN] = "";
	Preset.m_aTees[0].m_aValues[CUSTOM_COLORS] = "0";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	pConsole->ExecuteLine(Command.c_str());
	EXPECT_EQ(Values.size(), 4U);
	EXPECT_EQ(Values["player_name"], "DYL");
	EXPECT_EQ(Values["dummy_name"], "YL");
	EXPECT_EQ(Values["player_clan"], "");
	EXPECT_EQ(Values["player_use_custom_color"], "0");
}

TEST(QmWheelPresetsConsole, ChatTextCannotExecuteAnAdditionalConsoleCommand)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::map<std::string, std::string> Values;
	SCapture Capture{&Values, "say_team"};
	pConsole->Register("say_team", "r[text]", CFGFLAG_CLIENT, SCapture::OnCommand, &Capture, "");
	int KillCalls = 0;
	pConsole->Register("kill", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { ++*static_cast<int *>(pUser); }, &KillCalls, "");
	SPreset Preset;
	Preset.m_Type = EType::CHAT;
	Preset.m_TeamChat = true;
	Preset.m_Chat = "\"; kill; # \\你好";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	pConsole->ExecuteLine(Command.c_str());
	EXPECT_EQ(KillCalls, 0);
	EXPECT_EQ(Values["say_team"], Preset.m_Chat);
}

TEST(QmWheelPresetsConsole, WheelConfigRoundTripPreservesQuotedLabelAndCommand)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::array<std::string, 2> Restored;
	pConsole->Register("add_bindwheel", "s[name] s[command]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto &Values = *static_cast<std::array<std::string, 2> *>(pUser);
		Values = {pResult->GetString(0), pResult->GetString(1)}; }, &Restored, "");
	SPreset Preset;
	Preset.m_Type = EType::ASSETS;
	Preset.m_aAssets[0] = "实体; \"蓝\"";
	Preset.m_aAssets[1] = "folder\\name";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const std::string Label = "\"资源\"; \\";
	const std::string Line = qm_bind_editor::WheelConfigLine(Label, Command);
	pConsole->ExecuteLine(Line.c_str());
	EXPECT_EQ(Restored[0], Label);
	EXPECT_EQ(Restored[1], Command);
	const auto Parsed = ParseCommand(Restored[1]);
	ASSERT_TRUE(Parsed);
	EXPECT_EQ(Parsed->m_aAssets, Preset.m_aAssets);
}

TEST(QmWheelPresetsConsole, FullWheelCommandSurvivesConfigurationEscaping)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::array<std::string, 2> Restored;
	pConsole->Register("add_bindwheel", "s[name] s[command]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) { *static_cast<std::array<std::string, 2> *>(pUser) = {pResult->GetString(0), pResult->GetString(1)}; }, &Restored, "");
	const std::string Name(63, '"');
	const std::string Command(1023, '\\');
	pConsole->ExecuteLine(qm_bind_editor::WheelConfigLine(Name, Command).c_str());
	EXPECT_EQ(Restored[0], Name);
	EXPECT_EQ(Restored[1], Command);
}
