#include <engine/shared/websocket_native_state.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <memory_resource>
#include <new>

namespace
{
	class CNodeFaultResource : public std::pmr::memory_resource
	{
		void *do_allocate(size_t Bytes, size_t Alignment) override
		{
			if(++m_Calls == m_FailOnCall)
			{
				if(m_Unknown)
					throw 7;
				throw std::bad_alloc();
			}
			auto *pData = std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
			++m_Live;
			return pData;
		}
		void do_deallocate(void *pData, size_t Bytes, size_t Alignment) override
		{
			--m_Live;
			std::pmr::new_delete_resource()->deallocate(pData, Bytes, Alignment);
		}
		bool do_is_equal(const std::pmr::memory_resource &Other) const noexcept override { return this == &Other; }

	public:
		int m_Calls = 0;
		int m_FailOnCall = 0;
		int m_Live = 0;
		bool m_Unknown = false;
	};

	class CWebsocketNativeOwnerTest : public ::testing::Test
	{
	protected:
		CNodeFaultResource m_Resource;
		std::array<int, 3> m_aTokens{};
		std::unique_ptr<CWebsocketNativeState> m_pState;
		void SetUp() override { m_pState = std::make_unique<CWebsocketNativeState>(&m_Resource); }
		void TearDown() override
		{
			m_pState.reset();
			EXPECT_EQ(m_Resource.m_Live, 0);
		}
		// 只提供不被解引用的连接身份，不模拟LWS分配、回调或事件循环。
		lws *Wsi(size_t Index = 0) { return reinterpret_cast<lws *>(&m_aTokens[Index]); }
		NETADDR Addr(unsigned short Port = 8303)
		{
			NETADDR Result = NETADDR_ZEROED;
			Result.type = NETTYPE_WEBSOCKET_IPV4;
			Result.ip[0] = 127;
			Result.ip[3] = 1;
			Result.port = Port;
			return Result;
		}
	};
}

TEST_F(CWebsocketNativeOwnerTest, UnknownLookupDoesNotCreateEntries)
{
	EXPECT_EQ(m_pState->Find(Wsi()), nullptr);
	EXPECT_EQ(m_pState->Find(Addr()), nullptr);
	EXPECT_EQ(m_pState->SessionCount(), 0u);
	EXPECT_TRUE(m_pState->Ports().empty());
	EXPECT_EQ(m_Resource.m_Calls, 0);
}

TEST_F(CWebsocketNativeOwnerTest, RegisteredCppSessionIsFoundByBothIdentities)
{
	auto *pSession = m_pState->TryRegister(Wsi(), Addr());
	ASSERT_NE(pSession, nullptr);
	EXPECT_EQ(pSession->m_pWsi, Wsi());
	EXPECT_EQ(m_pState->Find(Wsi()), pSession);
	EXPECT_EQ(m_pState->Find(Addr()), pSession);
	EXPECT_EQ(m_pState->SessionCount(), 1u);
}

TEST_F(CWebsocketNativeOwnerTest, RepeatedEstablishmentKeepsExistingOwner)
{
	auto *pSession = m_pState->TryRegister(Wsi(), Addr());
	ASSERT_NE(pSession, nullptr);
	const int Calls = m_Resource.m_Calls;
	EXPECT_EQ(m_pState->TryRegister(Wsi(), Addr()), pSession);
	EXPECT_EQ(m_Resource.m_Calls, Calls);
	EXPECT_EQ(m_pState->SessionCount(), 1u);
}

TEST_F(CWebsocketNativeOwnerTest, OwnerNodeFailureLeavesNoRegistrationAndCanRetry)
{
	m_Resource.m_FailOnCall = 1;
	EXPECT_EQ(m_pState->TryRegister(Wsi(), Addr()), nullptr);
	EXPECT_EQ(m_pState->SessionCount(), 0u);
	EXPECT_TRUE(m_pState->Ports().empty());
	EXPECT_EQ(m_Resource.m_Live, 0);
	m_Resource.m_FailOnCall = 0;
	EXPECT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
}

TEST_F(CWebsocketNativeOwnerTest, PortNodeFailureRollsBackCandidateWithoutChangingExistingSession)
{
	auto *pOld = m_pState->TryRegister(Wsi(), Addr());
	ASSERT_NE(pOld, nullptr);
	const int Live = m_Resource.m_Live;
	m_Resource.m_FailOnCall = m_Resource.m_Calls + 2;
	EXPECT_EQ(m_pState->TryRegister(Wsi(1), Addr(8304)), nullptr);
	EXPECT_EQ(m_pState->Find(Wsi(1)), nullptr);
	EXPECT_EQ(m_pState->Find(Addr(8304)), nullptr);
	EXPECT_EQ(m_pState->Find(Addr()), pOld);
	EXPECT_EQ(m_pState->SessionCount(), 1u);
	EXPECT_EQ(m_Resource.m_Live, Live);
	m_Resource.m_FailOnCall = 0;
	EXPECT_NE(m_pState->TryRegister(Wsi(1), Addr(8304)), nullptr);
}

TEST_F(CWebsocketNativeOwnerTest, UnknownNodeExceptionAlsoRollsBackRegistration)
{
	m_Resource.m_FailOnCall = 2;
	m_Resource.m_Unknown = true;
	EXPECT_EQ(m_pState->TryRegister(Wsi(), Addr()), nullptr);
	EXPECT_EQ(m_Resource.m_Live, 0);
	EXPECT_EQ(m_pState->SessionCount(), 0u);
	EXPECT_TRUE(m_pState->Ports().empty());
}

TEST_F(CWebsocketNativeOwnerTest, OldConnectionCloseKeepsReplacementAddressIndex)
{
	ASSERT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
	auto *pNew = m_pState->TryRegister(Wsi(1), Addr());
	ASSERT_NE(pNew, nullptr);
	m_pState->Remove(Wsi());
	EXPECT_EQ(m_pState->Find(Addr()), pNew);
	EXPECT_EQ(m_pState->Find(Wsi(1)), pNew);
	EXPECT_EQ(m_pState->SessionCount(), 1u);
}

TEST_F(CWebsocketNativeOwnerTest, DuplicateAndUnknownCloseLeaveOtherSessionUntouched)
{
	auto *pOther = m_pState->TryRegister(Wsi(1), Addr(8304));
	ASSERT_NE(pOther, nullptr);
	ASSERT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
	m_pState->Remove(Wsi());
	m_pState->Remove(Wsi());
	m_pState->Remove(Wsi(2));
	EXPECT_EQ(m_pState->Find(Addr(8304)), pOther);
	EXPECT_EQ(m_pState->SessionCount(), 1u);
}

TEST_F(CWebsocketNativeOwnerTest, CloseDestroysRingCallbackCaptures)
{
	auto *pSession = m_pState->TryRegister(Wsi(), Addr());
	ASSERT_NE(pSession, nullptr);
	auto pOwner = std::make_shared<int>(5);
	std::weak_ptr<int> Observer = pOwner;
	pSession->m_SendBuffer.SetPopCallback([pOwner](SWebsocketNativeChunk *) {});
	pOwner.reset();
	ASSERT_FALSE(Observer.expired());
	m_pState->Remove(Wsi());
	EXPECT_TRUE(Observer.expired());
}

TEST_F(CWebsocketNativeOwnerTest, ResetClearsOwnersAndIndexesAndAllowsReuse)
{
	ASSERT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
	ASSERT_NE(m_pState->TryRegister(Wsi(1), Addr(8304)), nullptr);
	m_pState->Reset();
	EXPECT_EQ(m_pState->SessionCount(), 0u);
	EXPECT_TRUE(m_pState->Ports().empty());
	EXPECT_EQ(m_Resource.m_Live, 0);
	EXPECT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
}

TEST_F(CWebsocketNativeOwnerTest, ContextOwnerDestructionDestroysRemainingSessionCaptures)
{
	auto *pSession = m_pState->TryRegister(Wsi(), Addr());
	ASSERT_NE(pSession, nullptr);
	auto pOwner = std::make_shared<int>(5);
	std::weak_ptr<int> Observer = pOwner;
	pSession->m_SendBuffer.SetPopCallback([pOwner](SWebsocketNativeChunk *) {});
	pOwner.reset();
	m_pState.reset();
	EXPECT_TRUE(Observer.expired());
	EXPECT_EQ(m_Resource.m_Live, 0);
}

TEST_F(CWebsocketNativeOwnerTest, NormalCallbackReturnPreservesSessionAndResult)
{
	auto *pSession = m_pState->TryRegister(Wsi(), Addr());
	ASSERT_NE(pSession, nullptr);
	EXPECT_EQ(m_pState->Dispatch(Wsi(), [] { return 17; }), 17);
	EXPECT_EQ(m_pState->Find(Wsi()), pSession);
}

TEST_F(CWebsocketNativeOwnerTest, ThrowingCallbackRollsBackItsRegistrationBeforeReturningClose)
{
	EXPECT_EQ(m_pState->Dispatch(Wsi(), [&]() -> int {
		EXPECT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
		throw std::bad_alloc();
	}),
		-1);
	EXPECT_EQ(m_pState->Find(Wsi()), nullptr);
	EXPECT_TRUE(m_pState->Ports().empty());
	EXPECT_EQ(m_Resource.m_Live, 0);
}

TEST_F(CWebsocketNativeOwnerTest, UnknownCallbackExceptionDoesNotRemoveOtherConnection)
{
	auto *pOther = m_pState->TryRegister(Wsi(1), Addr(8304));
	ASSERT_NE(pOther, nullptr);
	EXPECT_EQ(m_pState->Dispatch(Wsi(), []() -> int { throw 7; }), -1);
	EXPECT_EQ(m_pState->Find(Addr(8304)), pOther);
}

TEST_F(CWebsocketNativeOwnerTest, CallbackFailureAfterReplacementKeepsNewConnection)
{
	ASSERT_NE(m_pState->TryRegister(Wsi(), Addr()), nullptr);
	EXPECT_EQ(m_pState->Dispatch(Wsi(), [&]() -> int {
		m_pState->Remove(Wsi());
		EXPECT_NE(m_pState->TryRegister(Wsi(1), Addr()), nullptr);
		throw 7;
	}),
		-1);
	EXPECT_EQ(m_pState->Find(Addr()), m_pState->Find(Wsi(1)));
	EXPECT_EQ(m_pState->SessionCount(), 1u);
}

TEST_F(CWebsocketNativeOwnerTest, MissingHandleOrInvalidAddressCannotRegister)
{
	EXPECT_EQ(m_pState->TryRegister(nullptr, Addr()), nullptr);
	EXPECT_EQ(m_pState->TryRegister(Wsi(), NETADDR_ZEROED), nullptr);
	EXPECT_EQ(m_Resource.m_Calls, 0);
}
