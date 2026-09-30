#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/bind_editor.h>

#include <gtest/gtest.h>

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
