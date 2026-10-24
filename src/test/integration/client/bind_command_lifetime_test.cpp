#include <engine/shared/config.h>

#include <game/client/components/binds.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	struct SExecution
	{
		CBindStorage m_Binds;
		std::string m_Source;
		int m_Markers = 0;
		std::vector<int> m_vStrokes;
	};

	void Mark(IConsole::IResult *, void *pUser)
	{
		++static_cast<SExecution *>(pUser)->m_Markers;
	}

	void OverwriteSource(IConsole::IResult *, void *pUser)
	{
		auto &Source = static_cast<SExecution *>(pUser)->m_Source;
		std::fill(Source.begin(), Source.end(), '#');
	}

	void ReplaceBinding(IConsole::IResult *, void *pUser)
	{
		auto &State = *static_cast<SExecution *>(pUser);
		EXPECT_TRUE(State.m_Binds.Bind(KEY_X, "+hook"));
	}

	void ClearBindings(IConsole::IResult *, void *pUser)
	{
		static_cast<SExecution *>(pUser)->m_Binds.UnbindAll();
	}

	void RecordStroke(IConsole::IResult *pResult, void *pUser)
	{
		static_cast<SExecution *>(pUser)->m_vStrokes.push_back(pResult->GetInteger(0));
	}
}

TEST(BindCommandLifetime, CallbackOverwritingSourceCannotReplaceRemainingCommands)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	State.m_Source = "mutate;mark";
	pConsole->Register("mutate", "", CFGFLAG_CLIENT, OverwriteSource, &State, "");
	pConsole->Register("mark", "", CFGFLAG_CLIENT, Mark, &State, "");

	CBinds::ExecuteCommand(pConsole.get(), State.m_Source.c_str(), false, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_Source, std::string(11, '#'));
	EXPECT_EQ(State.m_Markers, 1);
}

TEST(BindCommandLifetime, RebindingOwnSlotPreservesOriginalCommandTail)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	ASSERT_TRUE(State.m_Binds.Bind(KEY_X, "replace;mark"));
	pConsole->Register("replace", "", CFGFLAG_CLIENT, ReplaceBinding, &State, "");
	pConsole->Register("mark", "", CFGFLAG_CLIENT, Mark, &State, "");

	CBinds::ExecuteCommand(pConsole.get(), State.m_Binds.Get(KEY_X, KeyModifier::NONE), false, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_STREQ(State.m_Binds.Get(KEY_X, KeyModifier::NONE), "+hook");
	EXPECT_EQ(State.m_Markers, 1);
}

TEST(BindCommandLifetime, UnbindAllDuringExecutionPreservesOriginalCommandTail)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	ASSERT_TRUE(State.m_Binds.Bind(KEY_X, "clear;mark"));
	ASSERT_TRUE(State.m_Binds.Bind(KEY_Y, "+left"));
	pConsole->Register("clear", "", CFGFLAG_CLIENT, ClearBindings, &State, "");
	pConsole->Register("mark", "", CFGFLAG_CLIENT, Mark, &State, "");

	CBinds::ExecuteCommand(pConsole.get(), State.m_Binds.Get(KEY_X, KeyModifier::NONE), false, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_STREQ(State.m_Binds.Get(KEY_X, KeyModifier::NONE), "");
	EXPECT_STREQ(State.m_Binds.Get(KEY_Y, KeyModifier::NONE), "");
	EXPECT_EQ(State.m_Markers, 1);
}

TEST(BindCommandLifetime, ReleaseStrokeKeepsTailAfterSourceChanges)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	State.m_Source = "+mutate;+fire";
	pConsole->Register("+mutate", "", CFGFLAG_CLIENT, OverwriteSource, &State, "");
	pConsole->Register("+fire", "", CFGFLAG_CLIENT, RecordStroke, &State, "");

	CBinds::ExecuteCommand(pConsole.get(), State.m_Source.c_str(), false, 0, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_vStrokes, (std::vector<int>{0}));
}

TEST(BindCommandLifetime, GoresAddsPreviousWeaponOnlyWhenMissing)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	pConsole->Register("+fire", "", CFGFLAG_CLIENT, RecordStroke, &State, "");
	pConsole->Register("+prevweapon", "", CFGFLAG_CLIENT, RecordStroke, &State, "");

	CBinds::ExecuteCommand(pConsole.get(), "+fire", true, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_vStrokes, (std::vector<int>{1, 1}));
	State.m_vStrokes.clear();
	CBinds::ExecuteCommand(pConsole.get(), "+fire;+prevweapon", true, 0, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_vStrokes, (std::vector<int>{0, 0}));
}

TEST(BindCommandLifetime, GoresDisabledKeepsFireStrokeAlone)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	pConsole->Register("+fire", "", CFGFLAG_CLIENT, RecordStroke, &State, "");
	pConsole->Register("+prevweapon", "", CFGFLAG_CLIENT, RecordStroke, &State, "");

	CBinds::ExecuteCommand(pConsole.get(), "+fire", false, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_vStrokes, (std::vector<int>{1}));
}

TEST(BindCommandLifetime, NullAndEmptyCommandsDoNotInvokeCallbacks)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	pConsole->Register("mark", "", CFGFLAG_CLIENT, Mark, &State, "");
	CBinds::ExecuteCommand(pConsole.get(), nullptr, true, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	CBinds::ExecuteCommand(pConsole.get(), "", true, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_Markers, 0);
}

TEST(BindCommandLifetime, RefreshKeepsStrokedTailAfterMutationAndSkipsOtherCommands)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	State.m_Source = "+mutate;mark;+fire";
	pConsole->Register("+mutate", "", CFGFLAG_CLIENT, OverwriteSource, &State, "");
	pConsole->Register("mark", "", CFGFLAG_CLIENT, Mark, &State, "");
	pConsole->Register("+fire", "", CFGFLAG_CLIENT, RecordStroke, &State, "");

	CBinds::RefreshCommand(pConsole.get(), State.m_Source.c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(State.m_vStrokes, (std::vector<int>{1}));
	EXPECT_EQ(State.m_Markers, 0);
}

TEST(BindCommandLifetime, RefreshSurvivesRebindingOwnSlot)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SExecution State;
	ASSERT_TRUE(State.m_Binds.Bind(KEY_X, "+replace;+fire"));
	pConsole->Register("+replace", "", CFGFLAG_CLIENT, ReplaceBinding, &State, "");
	pConsole->Register("+fire", "", CFGFLAG_CLIENT, RecordStroke, &State, "");

	CBinds::RefreshCommand(pConsole.get(), State.m_Binds.Get(KEY_X, KeyModifier::NONE), IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_STREQ(State.m_Binds.Get(KEY_X, KeyModifier::NONE), "+hook");
	EXPECT_EQ(State.m_vStrokes, (std::vector<int>{1}));
}
