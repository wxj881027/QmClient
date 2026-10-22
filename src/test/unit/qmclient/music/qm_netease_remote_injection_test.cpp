#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_remote_injection.h>

#include <new>

using QmNeteaseHelper::CRemoteInjection;

namespace
{
	class COperations : public CRemoteInjection::COperations
	{
		int m_ProcessToken = 0, m_PathToken = 0, m_ThreadToken = 0;
		int m_Order = 0;

	public:
		enum class EStage
		{
			NONE,
			OPEN,
			ALLOCATE,
			WRITE,
			THREAD
		};
		EStage m_Failure = EStage::NONE;
		bool m_ReturnNullProcess = false, m_ReturnNullPath = false, m_WriteResult = true, m_ReturnNullThread = false;
		CRemoteInjection::EWait m_Wait = CRemoteInjection::EWait::COMPLETE;
		int m_Opens = 0, m_Allocations = 0, m_Threads = 0, m_Frees = 0, m_ClosedThreads = 0, m_ClosedProcesses = 0;
		int m_FreeOrder = 0, m_CloseThreadOrder = 0, m_CloseProcessOrder = 0;
		uint32_t m_LastTimeout = 0;
		size_t m_Bytes = 0;
		std::wstring m_Path;
		void Fail(EStage Stage)
		{
			if(m_Failure == Stage)
				throw std::bad_alloc();
		}
		void *OpenProcess(uint32_t) override
		{
			Fail(EStage::OPEN);
			++m_Opens;
			return m_ReturnNullProcess ? nullptr : &m_ProcessToken;
		}
		void *AllocatePath(void *, size_t Bytes) override
		{
			Fail(EStage::ALLOCATE);
			++m_Allocations;
			m_Bytes = Bytes;
			return m_ReturnNullPath ? nullptr : &m_PathToken;
		}
		bool WritePath(void *, void *, const wchar_t *pPath, size_t Bytes) override
		{
			Fail(EStage::WRITE);
			m_Path.assign(pPath, Bytes / sizeof(wchar_t) - 1);
			EXPECT_EQ(pPath[Bytes / sizeof(wchar_t) - 1], L'\0');
			return m_WriteResult;
		}
		void *CreateThread(void *, void *) override
		{
			Fail(EStage::THREAD);
			++m_Threads;
			return m_ReturnNullThread ? nullptr : &m_ThreadToken;
		}
		CRemoteInjection::EWait Wait(void *, uint32_t TimeoutMs) noexcept override
		{
			m_LastTimeout = TimeoutMs;
			return m_Wait;
		}
		void FreePath(void *, void *) noexcept override
		{
			++m_Frees;
			m_FreeOrder = ++m_Order;
		}
		void Close(void *pHandle) noexcept override
		{
			if(pHandle == &m_ThreadToken)
			{
				++m_ClosedThreads;
				m_CloseThreadOrder = ++m_Order;
			}
			else if(pHandle == &m_ProcessToken)
			{
				++m_ClosedProcesses;
				m_CloseProcessOrder = ++m_Order;
			}
		}
	};
}

TEST(QmNeteaseRemoteInjection, InvalidPidOrEmptyPathDoesNotOpenAProcess)
{
	COperations Operations;
	CRemoteInjection Injection(Operations);
	EXPECT_FALSE(Injection.Start(0, L"hook.dll"));
	EXPECT_FALSE(Injection.Start(42, L""));
	EXPECT_EQ(Operations.m_Opens, 0);
}

TEST(QmNeteaseRemoteInjection, FailedProcessOpenDoesNotAllocateOrStartAThread)
{
	COperations Operations;
	Operations.m_ReturnNullProcess = true;
	CRemoteInjection Injection(Operations);
	EXPECT_FALSE(Injection.Start(42, L"hook.dll"));
	EXPECT_EQ(Operations.m_Allocations, 0);
	EXPECT_EQ(Operations.m_ClosedProcesses, 0);
}

TEST(QmNeteaseRemoteInjection, FailedAllocationClosesOnlyTheOwnedProcess)
{
	COperations Operations;
	Operations.m_ReturnNullPath = true;
	CRemoteInjection Injection(Operations);
	EXPECT_FALSE(Injection.Start(42, L"hook.dll"));
	EXPECT_EQ(Operations.m_Frees, 0);
	EXPECT_EQ(Operations.m_ClosedProcesses, 1);
}

TEST(QmNeteaseRemoteInjection, FailedWriteFreesPathBeforeClosingProcessWithoutStartingThread)
{
	COperations Operations;
	Operations.m_WriteResult = false;
	CRemoteInjection Injection(Operations);
	EXPECT_FALSE(Injection.Start(42, L"hook.dll"));
	EXPECT_EQ(Operations.m_Threads, 0);
	EXPECT_EQ(Operations.m_Frees, 1);
	EXPECT_LT(Operations.m_FreeOrder, Operations.m_CloseProcessOrder);
}

TEST(QmNeteaseRemoteInjection, FailedThreadCreationReleasesPreparedPath)
{
	COperations Operations;
	Operations.m_ReturnNullThread = true;
	CRemoteInjection Injection(Operations);
	EXPECT_FALSE(Injection.Start(42, L"hook.dll"));
	EXPECT_EQ(Operations.m_Frees, 1);
	EXPECT_EQ(Operations.m_ClosedProcesses, 1);
	EXPECT_EQ(Operations.m_ClosedThreads, 0);
}

TEST(QmNeteaseRemoteInjection, PreparationExceptionsRemainInsideOwnerAndReleaseAcquiredResources)
{
	for(auto Stage : {COperations::EStage::OPEN, COperations::EStage::ALLOCATE, COperations::EStage::WRITE, COperations::EStage::THREAD})
	{
		SCOPED_TRACE(static_cast<int>(Stage));
		COperations Operations;
		Operations.m_Failure = Stage;
		CRemoteInjection Injection(Operations);
		EXPECT_FALSE(Injection.Start(42, L"hook.dll"));
		EXPECT_FALSE(Injection.Pending());
		EXPECT_EQ(Operations.m_ClosedProcesses, Stage == COperations::EStage::OPEN ? 0 : 1);
		EXPECT_EQ(Operations.m_Frees, Stage == COperations::EStage::WRITE || Stage == COperations::EStage::THREAD ? 1 : 0);
	}
}

TEST(QmNeteaseRemoteInjection, CompletedThreadReleasesPathAndHandlesInOwnershipOrder)
{
	COperations Operations;
	CRemoteInjection Injection(Operations);
	ASSERT_TRUE(Injection.Start(42, L"目录/hook.dll"));
	EXPECT_FALSE(Injection.Pending());
	EXPECT_EQ(Operations.m_LastTimeout, 10000U);
	EXPECT_EQ(Operations.m_Path, L"目录/hook.dll");
	EXPECT_EQ(Operations.m_Bytes, (Operations.m_Path.size() + 1) * sizeof(wchar_t));
	EXPECT_LT(Operations.m_FreeOrder, Operations.m_CloseThreadOrder);
	EXPECT_LT(Operations.m_CloseThreadOrder, Operations.m_CloseProcessOrder);
}

TEST(QmNeteaseRemoteInjection, TimeoutPreservesPathAndRejectsDuplicateWorkUntilCompletion)
{
	COperations Operations;
	Operations.m_Wait = CRemoteInjection::EWait::PENDING;
	CRemoteInjection Injection(Operations);
	ASSERT_TRUE(Injection.Start(42, L"hook.dll"));
	EXPECT_TRUE(Injection.Pending());
	EXPECT_EQ(Operations.m_Frees, 0);
	EXPECT_EQ(Operations.m_ClosedThreads, 0);
	EXPECT_FALSE(Injection.Start(84, L"other.dll"));
	EXPECT_EQ(Operations.m_Opens, 1);
	Operations.m_Wait = CRemoteInjection::EWait::COMPLETE;
	Injection.Poll();
	Injection.Poll();
	EXPECT_FALSE(Injection.Pending());
	EXPECT_EQ(Operations.m_Frees, 1);
	EXPECT_EQ(Operations.m_ClosedThreads, 1);
	ASSERT_TRUE(Injection.Start(84, L"other.dll"));
	EXPECT_EQ(Operations.m_Opens, 2);
}

TEST(QmNeteaseRemoteInjection, WaitFailureDoesNotProveThatTheThreadStopped)
{
	COperations Operations;
	Operations.m_Wait = CRemoteInjection::EWait::ERROR;
	{
		CRemoteInjection Injection(Operations);
		ASSERT_TRUE(Injection.Start(42, L"hook.dll"));
		Injection.Poll();
		EXPECT_TRUE(Injection.Pending());
	}
	EXPECT_EQ(Operations.m_Frees, 0);
	EXPECT_EQ(Operations.m_ClosedThreads, 1);
	EXPECT_EQ(Operations.m_ClosedProcesses, 1);
}

TEST(QmNeteaseRemoteInjection, DestructionWhileThreadRunsClosesLocalHandlesAndKeepsBorrowedRemoteMemory)
{
	COperations Operations;
	Operations.m_Wait = CRemoteInjection::EWait::PENDING;
	{
		CRemoteInjection Injection(Operations);
		ASSERT_TRUE(Injection.Start(42, L"hook.dll"));
	}
	EXPECT_EQ(Operations.m_Frees, 0);
	EXPECT_EQ(Operations.m_ClosedThreads, 1);
	EXPECT_EQ(Operations.m_ClosedProcesses, 1);
}
