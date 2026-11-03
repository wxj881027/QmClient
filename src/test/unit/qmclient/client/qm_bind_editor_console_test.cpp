#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_overview.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

using qm_bind_editor::ECommandCategory;

namespace
{
	struct SBinding
	{
		std::string m_Key;
		std::string m_Command;
	};

	void CaptureBind(IConsole::IResult *pResult, void *pUser)
	{
		static_cast<std::vector<SBinding> *>(pUser)->push_back({pResult->GetString(0), pResult->GetString(1)});
	}
}

TEST(QmBindEditorConsole, CatalogContainsLocalCommandsAndTheirParameterMetadata)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto IgnoreCommand = [](IConsole::IResult *, void *) {};
	const std::string LocalHelp = "Local action";
	pConsole->Register("qm_local_action", "s[key] ?r[command]", CFGFLAG_CLIENT, IgnoreCommand, nullptr, LocalHelp.c_str());
	pConsole->Register("qm_shared_action", "i[value]", CFGFLAG_CLIENT | CFGFLAG_SERVER, IgnoreCommand, nullptr, "");
	pConsole->Register("qm_server_action", "", CFGFLAG_SERVER, IgnoreCommand, nullptr, "");
	pConsole->RegisterTemp("qm_remote_action", "", CFGFLAG_CLIENT, "");
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	const auto Find = [&Options](const char *pName) {
		return std::find_if(Options.begin(), Options.end(), [pName](const auto &Option) { return !Option.m_Preset && Option.m_Command.m_Name == pName; });
	};
	const auto Local = Find("qm_local_action");
	ASSERT_NE(Local, Options.end());
	EXPECT_EQ(Local->m_Label, "qm_local_action");
	EXPECT_EQ(Local->m_Command.m_Parameters, "s[key] ?r[command]");
	EXPECT_EQ(Local->m_Command.m_Help, LocalHelp);
	EXPECT_EQ(Local->m_Category, ECommandCategory::CONSOLE);
	EXPECT_NE(Find("qm_shared_action"), Options.end());
	EXPECT_EQ(Find("qm_server_action"), Options.end());
	EXPECT_EQ(Find("qm_remote_action"), Options.end());
}

TEST(QmBindEditorConsole, CatalogRefreshIncludesCommandsRegisteredAfterThePreviousSelection)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Before = qm_bind_editor::CommandOptions(*pConsole);
	pConsole->Register("qm_later_action", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {}, nullptr, "");
	const auto After = qm_bind_editor::CommandOptions(*pConsole);
	EXPECT_EQ(After.size(), Before.size() + 1);
	EXPECT_TRUE(std::any_of(After.begin(), After.end(), [](const auto &Option) { return !Option.m_Preset && Option.m_Command.m_Name == "qm_later_action"; }));
}

TEST(QmBindEditorConsole, PresetCatalogKeepsKnownActionsInTheirPlayerCategories)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	const struct
	{
		ECommandCategory m_Category;
		std::vector<const char *> m_vCommands;
	} aExpected[] = {
		{ECommandCategory::MOVEMENT, {"+left", "+right", "+jump", "+hook"}},
		{ECommandCategory::CAMERA, {"+showhookcoll", "zoom+", "zoom-", "zoom", "say /showothers", "say /showall", "toggle cl_dyncam 0 1", "toggle cl_race_show_ghost 0 1", "+spectate", "spectate_next", "spectate_previous", "toggle cl_overlay_entities 0 100", "toggle cl_showhud 0 1"}},
		{ECommandCategory::DUMMY, {"toggle cl_dummy 0 1", "+toggle_restore cl_dummy_jump 1", "+toggle_restore cl_dummy_fire 1", "+toggle_restore cl_dummy_hook 1", "toggle cl_dummy_copy_moves 0 1", "toggle cl_dummy_control 1 0"}},
		{ECommandCategory::WEAPON, {"+fire", "+weapon1", "+weapon2", "+weapon3", "+weapon4", "+weapon5", "+nextweapon", "+prevweapon"}},
		{ECommandCategory::CHAT, {"+show_chat; chat all", "+show_chat; chat team", "+show_chat; chat all /c ", "+show_chat", "+qm_repeat", "+qm_voice_ptt", "+emote"}},
		{ECommandCategory::VOTING, {"vote yes", "vote no"}},
		{ECommandCategory::MISCELLANEOUS, {"say /pause", "kill", "qm_timeout_disconnect", "toggle_local_console", "screenshot", "+scoreboard", "+statboard", "+pie_menu"}},
	};
	EXPECT_EQ(std::count_if(Options.begin(), Options.end(), [](const auto &Option) { return Option.m_Preset && !Option.m_Command.m_Name.empty(); }), std::size(s_aKnownBindActions));
	for(const auto &Category : aExpected)
	{
		for(const char *pCommand : Category.m_vCommands)
		{
			SCOPED_TRACE(pCommand);
			const auto Preset = std::find_if(Options.begin(), Options.end(), [pCommand](const auto &Option) { return Option.m_Preset && Option.m_Command.m_Name == pCommand; });
			ASSERT_NE(Preset, Options.end());
			EXPECT_EQ(Preset->m_Category, Category.m_Category);
		}
	}
}

TEST(QmBindEditorConsole, PresetCatalogUsesTheSharedControlsActionTable)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	for(const SKnownBindAction &Action : s_aKnownBindActions)
	{
		const auto Matches = std::count_if(Options.begin(), Options.end(), [&Action](const auto &Option) {
			return Option.m_Preset && Option.m_Command.m_Name == Action.m_pCommand;
		});
		EXPECT_EQ(Matches, 1) << Action.m_pCommand;
	}
}

TEST(QmBindEditorConsole, CustomCommandIsAvailableInEveryCategory)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	ASSERT_FALSE(Options.empty());
	EXPECT_EQ(Options.front().m_Label, "Custom command");
	EXPECT_TRUE(Options.front().m_Preset);
	EXPECT_TRUE(Options.front().m_Command.m_Name.empty());
	EXPECT_EQ(Options.front().m_Category, ECommandCategory::ALL);
	EXPECT_EQ(qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::ALL, "").size(), Options.size());
	for(int Category = 0; Category < static_cast<int>(ECommandCategory::NUM_CATEGORIES); ++Category)
	{
		const auto Filtered = qm_bind_editor::FilterCommandOptions(Options, static_cast<ECommandCategory>(Category), "Custom command");
		ASSERT_EQ(Filtered.size(), 1u) << Category;
		EXPECT_EQ(Filtered.front(), 0u) << Category;
	}
}

TEST(QmBindEditorConsole, CategoryAndSearchReturnOnlyTheirIntersection)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	const auto Filtered = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::MOVEMENT, "JUMP");
	ASSERT_EQ(Filtered.size(), 1u);
	EXPECT_EQ(Options[Filtered.front()].m_Command.m_Name, "+jump");
	const auto Dummy = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::DUMMY, "JUMP");
	ASSERT_EQ(Dummy.size(), 1u);
	EXPECT_EQ(Options[Dummy.front()].m_Command.m_Name, "+toggle_restore cl_dummy_jump 1");
}

TEST(QmBindEditorConsole, SearchMatchesLocalizedLabelsAndHelpAsWellAsCommandNames)
{
	const std::string SearchHelp = "Search action help";
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	pConsole->Register("qm_search_action", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {}, nullptr, SearchHelp.c_str());
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	const auto Localize = [](const char *pText) -> const char * {
		if(std::string_view(pText) == "Move left")
			return "向左移动";
		if(std::string_view(pText) == "Search action help")
			return "查找动作说明";
		return pText;
	};
	const auto LabelMatches = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::MOVEMENT, "向左", Localize);
	ASSERT_EQ(LabelMatches.size(), 1u);
	EXPECT_EQ(Options[LabelMatches.front()].m_Command.m_Name, "+left");
	const auto HelpMatches = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::CONSOLE, "说明", Localize);
	ASSERT_EQ(HelpMatches.size(), 1u);
	EXPECT_EQ(Options[HelpMatches.front()].m_Command.m_Name, "qm_search_action");
	const auto CommandMatches = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::CONSOLE, "QM_SEARCH_ACTION", Localize);
	ASSERT_EQ(CommandMatches.size(), 1u);
	EXPECT_EQ(CommandMatches.front(), HelpMatches.front());
}

TEST(QmBindEditorConsole, ClearingSearchAfterNoResultsRestoresTheSelectedCategory)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	EXPECT_TRUE(qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::MOVEMENT, "no such action").empty());
	const auto Restored = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::MOVEMENT, "");
	ASSERT_EQ(Restored.size(), 5u);
	EXPECT_EQ(Options[Restored[0]].m_Label, "Custom command");
	EXPECT_EQ(Options[Restored[1]].m_Command.m_Name, "+left");
	EXPECT_EQ(Options[Restored[2]].m_Command.m_Name, "+right");
	EXPECT_EQ(Options[Restored[3]].m_Command.m_Name, "+jump");
	EXPECT_EQ(Options[Restored[4]].m_Command.m_Name, "+hook");
}

TEST(QmBindEditorConsole, UnknownRegisteredCommandsStayInConsoleWithoutNameInference)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	pConsole->Register("qm_dummy_camera_action", "?i[mode]", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {}, nullptr, "");
	const auto Options = qm_bind_editor::CommandOptions(*pConsole);
	EXPECT_TRUE(qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::DUMMY, "qm_dummy_camera_action").empty());
	EXPECT_TRUE(qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::CAMERA, "qm_dummy_camera_action").empty());
	const auto ConsoleMatches = qm_bind_editor::FilterCommandOptions(Options, ECommandCategory::CONSOLE, "qm_dummy_camera_action");
	ASSERT_EQ(ConsoleMatches.size(), 1u);
	EXPECT_FALSE(Options[ConsoleMatches.front()].m_Preset);
	EXPECT_EQ(Options[ConsoleMatches.front()].m_Command.m_Parameters, "?i[mode]");
}

TEST(QmBindEditorConsole, NestedBindRoundTripsThroughTheRealConsole)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::vector<SBinding> vBindings;
	pConsole->Register("bind", "s[key] r[command]", CFGFLAG_CLIENT, CaptureBind, &vBindings, "");
	const std::string Action = R"(+fire; echo "a; b"; exec cfg\test.cfg)";
	const std::string Inner = qm_bind_editor::BindCommand("mouse1", Action);
	const std::string Outer = qm_bind_editor::BindCommand("ctrl+q", Inner);
	pConsole->ExecuteLine(Outer.c_str());
	ASSERT_EQ(vBindings.size(), 1u);
	EXPECT_EQ(vBindings[0].m_Key, "ctrl+q");
	EXPECT_EQ(vBindings[0].m_Command, Inner);
	pConsole->ExecuteLine(Inner.c_str());
	ASSERT_EQ(vBindings.size(), 2u);
	EXPECT_EQ(vBindings[1].m_Key, "mouse1");
	EXPECT_EQ(vBindings[1].m_Command, Action);
}

TEST(QmBindEditorConsole, MaximumAcceptedConfigLineIsNotTruncated)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::vector<SBinding> vBindings;
	pConsole->Register("bind", "s[key] r[command]", CFGFLAG_CLIENT, CaptureBind, &vBindings, "");
	const std::string Key = "ctrl+alt+shift+gui+q";
	const std::string Action(qm_bind_editor::MAX_CONFIG_LINE_BYTES - qm_bind_editor::BindCommand(Key, "").size(), 'x');
	ASSERT_TRUE(qm_bind_editor::FitsConfigLine(Key, Action));
	pConsole->ExecuteLine(qm_bind_editor::BindCommand(Key, Action).c_str());
	ASSERT_EQ(vBindings.size(), 1u);
	EXPECT_EQ(vBindings[0].m_Command, Action);
}

TEST(QmBindEditorConsole, ParameterEditorBuildsAndPersistsALongNestedBind)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::vector<SBinding> vBindings;
	pConsole->Register("bind", "s[key] r[command]", CFGFLAG_CLIENT, CaptureBind, &vBindings, "");
	const std::string Action = "echo \"" + std::string(512, 'x') + R"(; quoted text"; exec cfg\test.cfg)";
	std::string Nested;
	ASSERT_TRUE(qm_bind_editor::ComposeCommand("bind", qm_bind_editor::ParseParameters("s[key] r[command]"), {"mouse1", Action}, Nested));
	ASSERT_TRUE(pConsole->LineIsValid(Nested.c_str()));
	std::string Combined;
	ASSERT_TRUE(qm_bind_editor::AppendCommand("q", "echo before", Nested, Combined));
	pConsole->ExecuteLine(qm_bind_editor::BindCommand("q", Combined).c_str());
	ASSERT_EQ(vBindings.size(), 1u);
	EXPECT_EQ(vBindings[0].m_Command, Combined);
	const std::string Restored = vBindings[0].m_Command;
	pConsole->ExecuteLine(Restored.c_str());
	ASSERT_EQ(vBindings.size(), 2u);
	EXPECT_EQ(vBindings[1].m_Key, "mouse1");
	EXPECT_EQ(vBindings[1].m_Command, Action);
	EXPECT_GT(vBindings[1].m_Command.size(), 255u);
}
