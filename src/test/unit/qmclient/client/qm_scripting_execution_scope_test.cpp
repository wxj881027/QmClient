#include <game/client/components/qmclient/scripting/execution_scope.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>

namespace
{
	using CScopes = std::array<std::optional<CQmScriptExecutionScope>, CQmScriptExecutionScope::MAX_DEPTH>;

	bool EnterAll(CScopes &aScopes)
	{
		for(auto &Scope : aScopes)
		{
			Scope.emplace();
			if(!Scope->Entered())
				return false;
		}
		return true;
	}
}

TEST(QmScriptingExecutionScope, AcceptsLastAvailableLevelAndRejectsTheNext)
{
	CScopes aScopes;
	ASSERT_TRUE(EnterAll(aScopes));
	EXPECT_FALSE(CQmScriptExecutionScope::CanEnter());
	CQmScriptExecutionScope Rejected;
	EXPECT_FALSE(Rejected.Entered());
}

TEST(QmScriptingExecutionScope, DestroyingRejectedScopeDoesNotReleaseAnActiveLevel)
{
	CScopes aScopes;
	ASSERT_TRUE(EnterAll(aScopes));
	{
		CQmScriptExecutionScope Rejected;
		ASSERT_FALSE(Rejected.Entered());
	}
	EXPECT_FALSE(CQmScriptExecutionScope::CanEnter());
}

TEST(QmScriptingExecutionScope, LeavingOneLevelAllowsExactlyOneReplacement)
{
	CScopes aScopes;
	ASSERT_TRUE(EnterAll(aScopes));
	aScopes.back().reset();
	EXPECT_TRUE(CQmScriptExecutionScope::CanEnter());
	CQmScriptExecutionScope Replacement;
	ASSERT_TRUE(Replacement.Entered());
	EXPECT_FALSE(CQmScriptExecutionScope::CanEnter());
}

TEST(QmScriptingExecutionScope, ExceptionUnwindRestoresTheEntireBudget)
{
	const auto FailAtLimit = [] {
		CScopes aScopes;
		EXPECT_TRUE(EnterAll(aScopes));
		throw std::runtime_error("script failed");
	};
	EXPECT_THROW(FailAtLimit(), std::runtime_error);
	CScopes aNextCommand;
	EXPECT_TRUE(EnterAll(aNextCommand));
}

TEST(QmScriptingExecutionScope, SequentialCommandsDoNotAccumulateLevels)
{
	for(int Command = 0; Command < 3; ++Command)
	{
		CScopes aScopes;
		ASSERT_TRUE(EnterAll(aScopes)) << Command;
		EXPECT_FALSE(CQmScriptExecutionScope::CanEnter());
	}
	EXPECT_TRUE(CQmScriptExecutionScope::CanEnter());
}

TEST(QmScriptingExecutionScope, IndependentThreadsHaveIndependentBudgets)
{
	CScopes aScopes;
	ASSERT_TRUE(EnterAll(aScopes));
	auto OtherThread = std::async(std::launch::async, [] {
		CScopes aOtherScopes;
		return EnterAll(aOtherScopes) && !CQmScriptExecutionScope::CanEnter();
	});
	ASSERT_EQ(OtherThread.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_TRUE(OtherThread.get());
	EXPECT_FALSE(CQmScriptExecutionScope::CanEnter());
}
