#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
#include <gtest/gtest.h>
#include <test/support/qm_test_terminate_diagnostics.h>
#include <unistd.h>

#include <cstdlib>
#include <new>
#include <stdexcept>

namespace
{
	// 保留真实调用边界，避免把常量抛出/捕获折叠成不经过展开器的直接返回。
	[[gnu::noinline]] void ThrowAcrossNativeFrame(int Kind)
	{
		if(Kind == 0)
			throw std::bad_alloc();
		if(Kind == 1)
			throw std::length_error("native exception recovery probe");
		throw 7;
	}

	template<bool Noexcept>
	[[gnu::noinline]] bool CatchAcrossNativeFrame(int Kind) noexcept(Noexcept)
	{
		try
		{
			ThrowAcrossNativeFrame(Kind);
		}
		catch(const std::bad_alloc &)
		{
			return Kind == 0;
		}
		catch(const std::length_error &)
		{
			return Kind == 1;
		}
		catch(int Value)
		{
			return Kind == 2 && Value == 7;
		}
		return false;
	}

	template<bool Noexcept>
	void RunRecoveryChild(int Kind)
	{
		alarm(5);
		QmInstallMacTestTerminateDiagnostics();
		volatile int RuntimeKind = Kind;
		if(!CatchAcrossNativeFrame<Noexcept>(RuntimeKind))
			std::_Exit(74);
		constexpr char aCaught[] = "QmClient exception recovery: caught\n";
		(void)write(STDERR_FILENO, aCaught, sizeof(aCaught) - 1);
		std::_Exit(73);
	}
}

TEST(MacExceptionRecoveryDeathTest, OrdinaryBoundaryCatchesAllocationLengthAndUnknownExceptions)
{
	for(int Kind = 0; Kind < 3; ++Kind)
	{
		SCOPED_TRACE(Kind);
		EXPECT_EXIT(RunRecoveryChild<false>(Kind), ::testing::ExitedWithCode(73), "QmClient exception recovery: caught");
	}
}

TEST(MacExceptionRecoveryDeathTest, NoexceptBoundaryCatchesAllocationLengthAndUnknownExceptions)
{
	for(int Kind = 0; Kind < 3; ++Kind)
	{
		SCOPED_TRACE(Kind);
		EXPECT_EXIT(RunRecoveryChild<true>(Kind), ::testing::ExitedWithCode(73), "QmClient exception recovery: caught");
	}
}
#endif
