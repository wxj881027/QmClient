#include <base/thread.h>
#include <base/thread_startup.h>
#include <base/windows.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <objbase.h>

#include <chrono>
#include <future>
#include <memory>

namespace
{
	struct CComResults
	{
		bool m_OuterInitialized = false;
		bool m_InnerInitialized = false;
		bool m_Created = false;
		bool m_StartupAccepted = true;
		int m_Entries = 0;
		HRESULT m_Probe = E_UNEXPECTED;
		HRESULT m_After = E_UNEXPECTED;
	};

	// 探测本身取得的 COM 引用立即归还，不干扰被测作用域的引用数。
	HRESULT ProbeCom(DWORD Mode)
	{
		const HRESULT Result = CoInitializeEx(nullptr, Mode);
		if(SUCCEEDED(Result))
			CoUninitialize();
		return Result;
	}
}

TEST(WindowsComLifecycle, FreshApartmentIsReleasedWhenItsGuardEnds)
{
	auto pResults = std::make_shared<CComResults>();
	ASSERT_TRUE(WaitForAsyncIo([pResults] {
		{
			CWindowsComLifecycle Outer(false, CWindowsComLifecycle::EInitMode::CHECKED);
			pResults->m_OuterInitialized = Outer.IsInitialized();
		}
		pResults->m_After = ProbeCom(COINIT_APARTMENTTHREADED);
	}));
	EXPECT_TRUE(pResults->m_OuterInitialized);
	EXPECT_EQ(pResults->m_After, S_OK);
}

TEST(WindowsComLifecycle, NestedSuccessBalancesItsReferenceAndKeepsTheOuterApartment)
{
	auto pResults = std::make_shared<CComResults>();
	ASSERT_TRUE(WaitForAsyncIo([pResults] {
		{
			CWindowsComLifecycle Outer(false, CWindowsComLifecycle::EInitMode::CHECKED);
			pResults->m_OuterInitialized = Outer.IsInitialized();
			if(!Outer.IsInitialized())
				return;
			{
				CWindowsComLifecycle Inner(false, CWindowsComLifecycle::EInitMode::CHECKED);
				pResults->m_InnerInitialized = Inner.IsInitialized();
			}
			pResults->m_Probe = ProbeCom(COINIT_MULTITHREADED);
		}
		pResults->m_After = ProbeCom(COINIT_APARTMENTTHREADED);
	}));
	EXPECT_TRUE(pResults->m_OuterInitialized);
	EXPECT_TRUE(pResults->m_InnerInitialized);
	EXPECT_EQ(pResults->m_Probe, S_FALSE);
	EXPECT_EQ(pResults->m_After, S_OK);
}

TEST(WindowsComLifecycle, ConflictingModeDoesNotUninitializeTheExistingApartment)
{
	auto pResults = std::make_shared<CComResults>();
	ASSERT_TRUE(WaitForAsyncIo([pResults] {
		{
			CWindowsComLifecycle Outer(true, CWindowsComLifecycle::EInitMode::CHECKED);
			pResults->m_OuterInitialized = Outer.IsInitialized();
			if(!Outer.IsInitialized())
				return;
			{
				CWindowsComLifecycle Inner(false, CWindowsComLifecycle::EInitMode::CHECKED);
				pResults->m_InnerInitialized = Inner.IsInitialized();
			}
			pResults->m_Probe = ProbeCom(COINIT_APARTMENTTHREADED);
		}
		pResults->m_After = ProbeCom(COINIT_MULTITHREADED);
	}));
	EXPECT_TRUE(pResults->m_OuterInitialized);
	EXPECT_FALSE(pResults->m_InnerInitialized);
	EXPECT_EQ(pResults->m_Probe, S_FALSE);
	EXPECT_EQ(pResults->m_After, S_OK);
}

TEST(WindowsComLifecycle, RequiredModeAcceptsAnAlreadyInitializedCompatibleApartment)
{
	auto pResults = std::make_shared<CComResults>();
	ASSERT_TRUE(WaitForAsyncIo([pResults] {
		{
			CWindowsComLifecycle Outer(false, CWindowsComLifecycle::EInitMode::CHECKED);
			pResults->m_OuterInitialized = Outer.IsInitialized();
			if(!Outer.IsInitialized())
				return;
			CWindowsComLifecycle Inner(false);
			pResults->m_InnerInitialized = Inner.IsInitialized();
		}
		pResults->m_After = ProbeCom(COINIT_APARTMENTTHREADED);
	}));
	EXPECT_TRUE(pResults->m_OuterInitialized);
	EXPECT_TRUE(pResults->m_InnerInitialized);
	EXPECT_EQ(pResults->m_After, S_OK);
}

TEST(WindowsComLifecycle, ProjectThreadStillEntersWithComInitialized)
{
	auto pResults = std::make_shared<CComResults>();
	ASSERT_TRUE(WaitForAsyncIo([pResults] {
		void *pThread = thread_try_init([](void *pUser) {
			static_cast<CComResults *>(pUser)->m_Probe = ProbeCom(COINIT_MULTITHREADED);
		},
			pResults.get(), "com-ready");
		pResults->m_Created = pThread != nullptr;
		if(pThread)
			thread_wait(pThread);
	}));
	EXPECT_TRUE(pResults->m_Created);
	EXPECT_EQ(pResults->m_Probe, S_FALSE);
}

TEST(WindowsComLifecycle, FailedPreparationReportsFailureWithoutEnteringBusinessCode)
{
	auto pResults = std::make_shared<CComResults>();
	ASSERT_TRUE(WaitForAsyncIo([pResults] {
		CWindowsComLifecycle Outer(true, CWindowsComLifecycle::EInitMode::CHECKED);
		pResults->m_OuterInitialized = Outer.IsInitialized();
		if(!Outer.IsInitialized())
			return;
		CWindowsComLifecycle Inner(false, CWindowsComLifecycle::EInitMode::CHECKED);
		pResults->m_InnerInitialized = Inner.IsInitialized();
		CThreadStartup Startup;
		auto Result = Startup.GetResult();
		Startup.Run(Inner.IsInitialized(), [&] { ++pResults->m_Entries; });
		if(Result.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			pResults->m_StartupAccepted = CThreadStartup::Wait(Result);
	}));
	EXPECT_TRUE(pResults->m_OuterInitialized);
	EXPECT_FALSE(pResults->m_InnerInitialized);
	EXPECT_FALSE(pResults->m_StartupAccepted);
	EXPECT_EQ(pResults->m_Entries, 0);
}

#endif
