#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/chat_bind_config.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
	CBindChat::CBind Bind(const char *pName, const char *pCommand)
	{
		CBindChat::CBind Result{};
		str_copy(Result.m_aName, pName);
		str_copy(Result.m_aCommand, pCommand);
		return Result;
	}

	struct SReceiver
	{
		std::vector<std::array<std::string, 2>> m_Binds;
		std::vector<std::string> m_Removed;
		int m_KillCalls = 0;
		std::unique_ptr<IConsole> m_pConsole = CreateConsole(CFGFLAG_CLIENT);

		SReceiver()
		{
			m_pConsole->Register("bindchat", "s[name] r[command]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) { static_cast<SReceiver *>(pUser)->m_Binds.push_back({pResult->GetString(0), pResult->GetString(1)}); }, this, "");
			m_pConsole->Register("unbindchat", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) { static_cast<SReceiver *>(pUser)->m_Removed.emplace_back(pResult->GetString(0)); }, this, "");
			m_pConsole->Register("kill", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { ++static_cast<SReceiver *>(pUser)->m_KillCalls; }, this, "");
		}

		void Execute(const std::vector<std::string> &Lines)
		{
			for(const auto &Line : Lines)
				m_pConsole->ExecuteLine(Line.c_str());
		}
	};
}

TEST(QmChatBindConfigConsole, MaximumEscapedFieldsRoundTripThroughTheActualRestParameter)
{
	SReceiver Receiver;
	const std::string Name(63, '\\'), Command(1023, '\"');
	Receiver.Execute(QmPrepareChatBindConfig({Bind(Name.c_str(), Command.c_str())}, {}));
	ASSERT_EQ(Receiver.m_Binds.size(), 1u);
	EXPECT_EQ(Receiver.m_Binds[0][0], Name);
	EXPECT_EQ(Receiver.m_Binds[0][1], Command);
	EXPECT_EQ(Receiver.m_KillCalls, 0);
}

TEST(QmChatBindConfigConsole, QuotesSlashesSemicolonsAndUtf8RemainInsideTheBinding)
{
	SReceiver Receiver;
	const std::string Name = "!name\\\"";
	const std::string Command = "echo \"你好\"; kill; echo path\\end";
	Receiver.Execute(QmPrepareChatBindConfig({Bind(Name.c_str(), Command.c_str())}, {}));
	ASSERT_EQ(Receiver.m_Binds.size(), 1u);
	EXPECT_EQ(Receiver.m_Binds[0][0], Name);
	EXPECT_EQ(Receiver.m_Binds[0][1], Command);
	EXPECT_EQ(Receiver.m_KillCalls, 0);
}

TEST(QmChatBindConfigConsole, DefaultsOverridesAndRemovalsKeepTheirExistingCommands)
{
	SReceiver Receiver;
	const auto Unchanged = Bind("!same", "echo same"), Overridden = Bind("!changed", "echo old"), Missing = Bind("!missing", "echo missing");
	Receiver.Execute(QmPrepareChatBindConfig({Unchanged, Bind("!changed", "echo custom")}, {std::cref(Unchanged), std::cref(Overridden), std::cref(Missing)}));
	ASSERT_EQ(Receiver.m_Binds.size(), 1u);
	EXPECT_EQ(Receiver.m_Binds[0][0], "!changed");
	EXPECT_EQ(Receiver.m_Binds[0][1], "echo custom");
	ASSERT_EQ(Receiver.m_Removed.size(), 1u);
	EXPECT_EQ(Receiver.m_Removed[0], "!missing");
}

TEST(QmChatBindConfigConsole, OwnedPreparedLinesSurviveRuntimeAndDefaultChanges)
{
	SReceiver Receiver;
	std::vector<CBindChat::CBind> vBinds{Bind("!old", "echo old")};
	auto Missing = Bind("!missing", "echo missing");
	const auto Lines = QmPrepareChatBindConfig(vBinds, {std::cref(Missing)});
	vBinds.clear();
	str_copy(Missing.m_aName, "!new");
	Receiver.Execute(Lines);
	ASSERT_EQ(Receiver.m_Binds.size(), 1u);
	EXPECT_EQ(Receiver.m_Binds[0][0], "!old");
	EXPECT_EQ(Receiver.m_Binds[0][1], "echo old");
	ASSERT_EQ(Receiver.m_Removed.size(), 1u);
	EXPECT_EQ(Receiver.m_Removed[0], "!missing");
}
