#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/bind_editor.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

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
	const auto Commands = qm_bind_editor::RegisteredCommands(*pConsole);
	const auto Find = [&Commands](const char *pName) {
		return std::find_if(Commands.begin(), Commands.end(), [pName](const auto &Command) { return Command.m_Name == pName; });
	};
	const auto Local = Find("qm_local_action");
	ASSERT_NE(Local, Commands.end());
	EXPECT_EQ(Local->m_Parameters, "s[key] ?r[command]");
	EXPECT_EQ(Local->m_Help, LocalHelp);
	EXPECT_NE(Find("qm_shared_action"), Commands.end());
	EXPECT_EQ(Find("qm_server_action"), Commands.end());
	EXPECT_EQ(Find("qm_remote_action"), Commands.end());
}

TEST(QmBindEditorConsole, CatalogRefreshIncludesCommandsRegisteredAfterThePreviousSelection)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	const auto Before = qm_bind_editor::RegisteredCommands(*pConsole);
	pConsole->Register("qm_later_action", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {}, nullptr, "");
	const auto After = qm_bind_editor::RegisteredCommands(*pConsole);
	EXPECT_EQ(After.size(), Before.size() + 1);
	EXPECT_TRUE(std::any_of(After.begin(), After.end(), [](const auto &Command) { return Command.m_Name == "qm_later_action"; }));
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
