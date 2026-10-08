#include <game/client/QmUi/QmUiLifetime.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>

TEST(QmUiLifetime, EmptyReferenceIsNotAlive)
{
	const CQmUiLifetime::CWeakRef Reference;
	EXPECT_FALSE(Reference.IsAlive());
}

TEST(QmUiLifetime, PendingReferenceExpiresWhenOwnerIsDestroyed)
{
	CQmUiLifetime::CWeakRef Request;
	{
		CQmUiLifetime Element;
		Request = Element.WeakRef();
		ASSERT_TRUE(Request.IsAlive());
	}
	EXPECT_FALSE(Request.IsAlive());
}

TEST(QmUiLifetime, CopiedRequestsDoNotKeepOwnerAlive)
{
	auto pElement = std::make_unique<CQmUiLifetime>();
	const auto FirstRequest = pElement->WeakRef();
	const auto SecondRequest = FirstRequest;
	const auto RepeatedRequest = pElement->WeakRef();
	ASSERT_TRUE(SecondRequest.IsAlive());
	ASSERT_TRUE(RepeatedRequest.IsAlive());
	pElement.reset();
	EXPECT_FALSE(FirstRequest.IsAlive());
	EXPECT_FALSE(SecondRequest.IsAlive());
	EXPECT_FALSE(RepeatedRequest.IsAlive());
}

TEST(QmUiLifetime, ReusingOwnerAddressDoesNotReviveExpiredRequests)
{
	alignas(CQmUiLifetime) unsigned char aStorage[sizeof(CQmUiLifetime)];
	CQmUiLifetime *pFirstElement = new(aStorage) CQmUiLifetime();
	const auto OldRequest = pFirstElement->WeakRef();
	pFirstElement->~CQmUiLifetime();
	ASSERT_FALSE(OldRequest.IsAlive());

	CQmUiLifetime *pNewElement = new(aStorage) CQmUiLifetime();
	const auto NewRequest = pNewElement->WeakRef();
	EXPECT_FALSE(OldRequest.IsAlive());
	EXPECT_TRUE(NewRequest.IsAlive());
	pNewElement->~CQmUiLifetime();
	EXPECT_FALSE(NewRequest.IsAlive());
}
