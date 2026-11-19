#include <engine/shared/config.h>
#include <engine/shared/console.h>

#include <game/client/components/console.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>

#include <new>
#include <string>
#include <vector>

TEST(ConsoleTempAllocation, InitialFailureDoesNotPublishAnIncompleteCommandAndCanRetry)
{
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	Console.RegisterTemp("remote", "?r", CFGFLAG_SERVER, "remote help");
	EXPECT_EQ(Console.GetCommandInfo("remote", CFGFLAG_SERVER, true), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	Console.RegisterTemp("remote", "?r", CFGFLAG_SERVER, "remote help");
	const auto *pInfo = Console.GetCommandInfo("remote", CFGFLAG_SERVER, true);
	ASSERT_NE(pInfo, nullptr);
	EXPECT_STREQ(pInfo->Name(), "remote");
	EXPECT_STREQ(pInfo->Help(), "remote help");
	EXPECT_STREQ(pInfo->Params(), "?r");
}

TEST(ConsoleTempAllocation, GrowthFailurePreservesAllEarlierMetadataAndCanRetryTheRejectedName)
{
	CHeapAllocationProbe Probe;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	std::vector<std::string> vAccepted;
	std::string Rejected;
	for(int Index = 0; Index < 1024; ++Index)
	{
		const std::string Name = "remote_" + std::to_string(Index);
		Console.RegisterTemp(Name.c_str(), "i[value]", CFGFLAG_SERVER, "stable help");
		if(!Console.GetCommandInfo(Name.c_str(), CFGFLAG_SERVER, true))
		{
			Rejected = Name;
			break;
		}
		vAccepted.push_back(Name);
	}
	ASSERT_FALSE(vAccepted.empty());
	ASSERT_FALSE(Rejected.empty());
	for(const auto &Name : vAccepted)
	{
		SCOPED_TRACE(Name);
		const auto *pInfo = Console.GetCommandInfo(Name.c_str(), CFGFLAG_SERVER, true);
		ASSERT_NE(pInfo, nullptr);
		EXPECT_STREQ(pInfo->Help(), "stable help");
		EXPECT_STREQ(pInfo->Params(), "i[value]");
	}
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	Console.RegisterTemp(Rejected.c_str(), "r", CFGFLAG_SERVER, "retry help");
	const auto *pRetry = Console.GetCommandInfo(Rejected.c_str(), CFGFLAG_SERVER, true);
	ASSERT_NE(pRetry, nullptr);
	EXPECT_STREQ(pRetry->Help(), "retry help");
}

TEST(ConsoleTempAllocation, RecycledCommandNeedsNoNewHeapBlockWhenAllocationIsUnavailable)
{
	CHeapAllocationProbe Probe;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	Console.RegisterTemp("old", "i", CFGFLAG_SERVER, "old help");
	ASSERT_NE(Console.GetCommandInfo("old", CFGFLAG_SERVER, true), nullptr);
	Console.DeregisterTemp("old");
	const int Before = Probe.m_Requests;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	Console.RegisterTemp("new", "r", CFGFLAG_SERVER, "new help");
	EXPECT_EQ(Probe.m_Requests, Before);
	EXPECT_EQ(Console.GetCommandInfo("old", CFGFLAG_SERVER, true), nullptr);
	const auto *pNew = Console.GetCommandInfo("new", CFGFLAG_SERVER, true);
	ASSERT_NE(pNew, nullptr);
	EXPECT_STREQ(pNew->Help(), "new help");
	EXPECT_STREQ(pNew->Params(), "r");
}

TEST(ConsoleTempAllocation, ClearWithFailedHeapResetKeepsPermanentCommandsAndAllowsLaterTempRecovery)
{
	CHeapAllocationProbe Probe;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	Console.Register("permanent", "", CFGFLAG_SERVER, [](IConsole::IResult *, void *) {}, nullptr, "permanent help");
	Console.RegisterTemp("remote", "", CFGFLAG_SERVER, "help");
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	Console.DeregisterTempAll();
	EXPECT_EQ(Console.GetCommandInfo("remote", CFGFLAG_SERVER, true), nullptr);
	ASSERT_NE(Console.GetCommandInfo("permanent", CFGFLAG_SERVER, false), nullptr);
	Console.RegisterTemp("retry", "", CFGFLAG_SERVER, "retry help");
	EXPECT_EQ(Console.GetCommandInfo("retry", CFGFLAG_SERVER, true), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	Console.RegisterTemp("retry", "", CFGFLAG_SERVER, "retry help");
	ASSERT_NE(Console.GetCommandInfo("retry", CFGFLAG_SERVER, true), nullptr);
	EXPECT_STREQ(Console.GetCommandInfo("permanent", CFGFLAG_SERVER, false)->Help(), "permanent help");
}

TEST(ConsoleTempAllocation, AllocationExceptionDoesNotPublishTemporaryCommand)
{
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::BAD_ALLOC;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	Console.RegisterTemp("remote", "", CFGFLAG_SERVER, "help");
	EXPECT_EQ(Console.GetCommandInfo("remote", CFGFLAG_SERVER, true), nullptr);
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	Console.RegisterTemp("remote", "", CFGFLAG_SERVER, "help");
	EXPECT_NE(Console.GetCommandInfo("remote", CFGFLAG_SERVER, true), nullptr);
}

TEST(ConsoleTempAllocation, UnknownAllocationExceptionIsContainedBeforeRegistryMutation)
{
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::UNKNOWN;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	Console.RegisterTemp("remote", "", CFGFLAG_SERVER, "help");
	EXPECT_EQ(Console.GetCommandInfo("remote", CFGFLAG_SERVER, true), nullptr);
	EXPECT_NE(Console.GetCommandInfo("echo", CFGFLAG_SERVER, false), nullptr);
}

TEST(ConsoleTempAllocation, MetadataTruncationKeepsExistingLimitsAndFlagFiltering)
{
	CHeapAllocationProbe Probe;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	const std::string Name(128, 'n'), Help(384, 'h'), Params(192, 'p');
	Console.RegisterTemp(Name.c_str(), Params.c_str(), CFGFLAG_SERVER, Help.c_str());
	const std::string StoredName(IConsole::TEMPCMD_NAME_LENGTH - 1, 'n');
	const auto *pInfo = Console.GetCommandInfo(StoredName.c_str(), CFGFLAG_SERVER, true);
	ASSERT_NE(pInfo, nullptr);
	EXPECT_EQ(std::string(pInfo->Help()), std::string(IConsole::TEMPCMD_HELP_LENGTH - 1, 'h'));
	EXPECT_EQ(std::string(pInfo->Params()), std::string(IConsole::TEMPCMD_PARAMS_LENGTH - 1, 'p'));
	EXPECT_EQ(Console.GetCommandInfo(StoredName.c_str(), CFGFLAG_CLIENT, true), nullptr);
}

TEST(ConsoleTempAllocation, StoredMetadataDoesNotBorrowCallerBuffers)
{
	CHeapAllocationProbe Probe;
	CConsole Console(CFGFLAG_SERVER, CHeapAllocationProbe::Allocate);
	char aName[] = "remote", aHelp[] = "help", aParams[] = "r";
	Console.RegisterTemp(aName, aParams, CFGFLAG_SERVER, aHelp);
	aName[0] = aHelp[0] = aParams[0] = 'X';
	const auto *pInfo = Console.GetCommandInfo("remote", CFGFLAG_SERVER, true);
	ASSERT_NE(pInfo, nullptr);
	EXPECT_STREQ(pInfo->Help(), "help");
	EXPECT_STREQ(pInfo->Params(), "r");
}

TEST(ConsoleCompletionOwnership, SavedSuggestionSurvivesReuseOfTemporaryCommandStorage)
{
	CConsole Console(CFGFLAG_SERVER);
	Console.RegisterTemp("temporary_first", "", CFGFLAG_SERVER, "");
	std::vector<std::string> vSuggestions;
	ASSERT_EQ(Console.PossibleCommands("temporary_", CFGFLAG_SERVER, true, QmConsoleCompletion::CollectSuggestion, &vSuggestions), 1);

	Console.DeregisterTemp("temporary_first");
	Console.RegisterTemp("temporary_next", "", CFGFLAG_SERVER, "");
	ASSERT_NE(Console.GetCommandInfo("temporary_next", CFGFLAG_SERVER, true), nullptr);

	ASSERT_EQ(vSuggestions.size(), 1u);
	EXPECT_EQ(vSuggestions.front(), "temporary_first");
}

TEST(ConsoleCompletionOwnership, ArgumentCollectionFailureRejectsTheWholeRefreshAndCanRetry)
{
	CConsole Console(CFGFLAG_SERVER);
	Console.RegisterTemp("remote_old", "", CFGFLAG_SERVER, "");
	Console.RegisterTemp("argument_first", "", CFGFLAG_SERVER, "");
	Console.RegisterTemp("argument_second", "", CFGFLAG_SERVER, "");

	struct SArgumentCollection
	{
		std::vector<std::string> *m_pResult;
		bool m_RejectSecond;
	};
	bool RejectSecond = false;
	const auto Collect = [&](QmConsoleCompletion::SSuggestions &Result) {
		Console.PossibleCommands("remote_", CFGFLAG_SERVER, true, QmConsoleCompletion::CollectSuggestion, &Result.m_vCommands);
		SArgumentCollection Collection{&Result.m_vArguments, RejectSecond};
		Console.PossibleCommands("argument_", CFGFLAG_SERVER, true, [](int Index, const char *pText, void *pUser) {
			const auto &State = *static_cast<SArgumentCollection *>(pUser);
			if(Index == 1 && State.m_RejectSecond)
				throw std::bad_alloc();
			QmConsoleCompletion::CollectSuggestion(Index, pText, State.m_pResult);
		}, &Collection);
	};

	const auto Original = QmConsoleCompletion::TryCollectSuggestions(Collect);
	ASSERT_TRUE(Original);
	ASSERT_EQ(Original->m_vCommands.size(), 1u);
	ASSERT_EQ(Original->m_vCommands.front(), "remote_old");

	Console.DeregisterTemp("remote_old");
	Console.RegisterTemp("remote_new", "", CFGFLAG_SERVER, "");
	RejectSecond = true;
	EXPECT_FALSE(QmConsoleCompletion::TryCollectSuggestions(Collect));

	RejectSecond = false;
	const auto Retry = QmConsoleCompletion::TryCollectSuggestions(Collect);
	ASSERT_TRUE(Retry);
	ASSERT_EQ(Retry->m_vCommands.size(), 1u);
	EXPECT_EQ(Retry->m_vCommands.front(), "remote_new");
	EXPECT_EQ(Retry->m_vArguments.size(), 2u);
}
