#include <game/client/components/qmclient/qm_presence_send.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	class CQmPresenceSendTest : public ::testing::Test
	{
	protected:
		std::string m_LastBody = "previous";
		std::string m_Presence = "current";
		std::vector<bool> m_vBuilds;
		std::vector<std::string> m_vSent;
		int m_FailBuild = -1;
		bool m_Accept = true;

		bool Send(bool Hello = false, bool ForceSend = true)
		{
			return TrySendQmPresence(m_LastBody, Hello, ForceSend, [&](bool BuildHello) {
			m_vBuilds.push_back(BuildHello);
			if(static_cast<int>(m_vBuilds.size()) == m_FailBuild)
				throw std::bad_alloc();
			return BuildHello ? std::string("hello") : m_Presence; }, [&](const char *pData, size_t Size) {
				m_vSent.emplace_back(pData, Size);
				return m_Accept; }) == EQmPresenceSendResult::SENT;
		}
	};
}

TEST_F(CQmPresenceSendTest, HelloCachesRegularPresenceAfterAdmission)
{
	ASSERT_TRUE(Send(true));
	EXPECT_EQ(m_vBuilds, (std::vector<bool>{true, false}));
	EXPECT_EQ(m_vSent, (std::vector<std::string>{"hello"}));
	EXPECT_EQ(m_LastBody, "current");
}

TEST_F(CQmPresenceSendTest, HelloBuildFailureDoesNotSendOrReplaceCache)
{
	m_FailBuild = 1;
	EXPECT_FALSE(Send(true));
	EXPECT_TRUE(m_vSent.empty());
	EXPECT_EQ(m_LastBody, "previous");
}

TEST_F(CQmPresenceSendTest, HelloCacheBuildFailureDoesNotSendPreparedHello)
{
	m_FailBuild = 2;
	EXPECT_FALSE(Send(true));
	EXPECT_EQ(m_vBuilds, (std::vector<bool>{true, false}));
	EXPECT_TRUE(m_vSent.empty());
	EXPECT_EQ(m_LastBody, "previous");
}

TEST_F(CQmPresenceSendTest, RejectedHelloPreservesCacheAndCanRetry)
{
	m_Accept = false;
	ASSERT_FALSE(Send(true));
	EXPECT_EQ(m_LastBody, "previous");
	m_Accept = true;
	ASSERT_TRUE(Send(true));
	EXPECT_EQ(m_vSent, (std::vector<std::string>{"hello", "hello"}));
	EXPECT_EQ(m_LastBody, "current");
}

TEST_F(CQmPresenceSendTest, ChangedPresenceSendsWithoutForceAndUpdatesCache)
{
	ASSERT_TRUE(Send(false, false));
	EXPECT_EQ(m_vBuilds, (std::vector<bool>{false}));
	EXPECT_EQ(m_vSent, (std::vector<std::string>{"current"}));
	EXPECT_EQ(m_LastBody, "current");
}

TEST_F(CQmPresenceSendTest, UnchangedPresenceDoesNotSendWithoutForce)
{
	m_LastBody = m_Presence;
	EXPECT_FALSE(Send(false, false));
	EXPECT_TRUE(m_vSent.empty());
	EXPECT_EQ(m_LastBody, "current");
}

TEST_F(CQmPresenceSendTest, ForcedRefreshSendsUnchangedPresence)
{
	m_LastBody = m_Presence;
	ASSERT_TRUE(Send());
	EXPECT_EQ(m_vSent, (std::vector<std::string>{"current"}));
	EXPECT_EQ(m_LastBody, "current");
}

TEST_F(CQmPresenceSendTest, PresenceBuildFailureKeepsCacheAndCanRetry)
{
	m_FailBuild = 1;
	ASSERT_FALSE(Send());
	EXPECT_TRUE(m_vSent.empty());
	EXPECT_EQ(m_LastBody, "previous");
	m_FailBuild = -1;
	ASSERT_TRUE(Send());
	EXPECT_EQ(m_LastBody, "current");
}

TEST_F(CQmPresenceSendTest, RejectedPresenceKeepsCacheAndRemainsChanged)
{
	m_Accept = false;
	ASSERT_FALSE(Send(false, false));
	EXPECT_EQ(m_LastBody, "previous");
	m_Accept = true;
	ASSERT_TRUE(Send(false, false));
	EXPECT_EQ(m_vSent, (std::vector<std::string>{"current", "current"}));
	EXPECT_EQ(m_LastBody, "current");
}

TEST(QmPresenceSend, AdmissionObservesOldCacheAndAllPreparationFinished)
{
	std::string LastBody = "old";
	int Builds = 0;
	ASSERT_EQ(EQmPresenceSendResult::SENT, TrySendQmPresence(LastBody, true, true, [&](bool Hello) {
		++Builds;
		return std::string(Hello ? "hello" : "presence"); }, [&](const char *, size_t) {
			EXPECT_EQ(Builds, 2);
			EXPECT_EQ(LastBody, "old");
			return true; }));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(LastBody, "presence");
}

TEST(QmPresenceSend, PresenceCacheTakesOwnershipOfPreparedLongBuffer)
{
	std::string LastBody = "old";
	const char *pSent = nullptr;
	ASSERT_EQ(EQmPresenceSendResult::SENT, TrySendQmPresence(LastBody, false, true, [](bool) { return std::string(4096, 'x'); }, [&](const char *pData, size_t Size) {
			pSent = pData;
			EXPECT_EQ(Size, 4096u);
			return true; }));
	EXPECT_EQ(LastBody.size(), 4096u);
	EXPECT_EQ(LastBody.data(), pSent);
}

TEST(QmPresenceSend, UnexpectedPreparationExceptionsRemainVisible)
{
	std::string LastBody = "old";
	bool Sent = false;
	EXPECT_THROW(TrySendQmPresence(LastBody, false, true, [](bool) -> std::string { throw std::logic_error("unexpected"); }, [&](const char *, size_t) {
			Sent = true;
			return true; }), std::logic_error);
	EXPECT_FALSE(Sent);
	EXPECT_EQ(LastBody, "old");
}

TEST(QmPresenceSend, UnchangedAndFailedPreparationHaveDifferentResults)
{
	std::string LastBody = "old";
	int Sends = 0;
	auto Send = [&](const char *, size_t) { ++Sends; return true; };
	EXPECT_EQ(TrySendQmPresence(LastBody, false, false, [](bool) { return std::string("old"); }, Send), EQmPresenceSendResult::UNCHANGED);
	EXPECT_EQ(TrySendQmPresence(LastBody, false, false, [](bool) -> std::string { throw std::bad_alloc(); }, Send), EQmPresenceSendResult::FAILED);
	EXPECT_EQ(Sends, 0);
	EXPECT_EQ(LastBody, "old");
}

TEST(QmPresenceSend, HelloSendsEvenWithoutForceWhenRegularCacheMatches)
{
	std::string LastBody = "presence";
	std::string Sent;
	EXPECT_EQ(TrySendQmPresence(LastBody, true, false, [](bool Hello) { return std::string(Hello ? "hello" : "presence"); }, [&](const char *pData, size_t Size) { Sent.assign(pData, Size); return true; }), EQmPresenceSendResult::SENT);
	EXPECT_EQ(Sent, "hello");
	EXPECT_EQ(LastBody, "presence");
}

TEST(QmPresenceSend, HelloCacheTakesOwnershipOfPreparedRegularBuffer)
{
	std::string LastBody = "old";
	const char *pPrepared = nullptr;
	EXPECT_EQ(TrySendQmPresence(LastBody, true, true, [&](bool Hello) {
		std::string Body(Hello ? 2048 : 4096, Hello ? 'h' : 'p');
		if(!Hello)
			pPrepared = Body.data();
		return Body; }, [](const char *, size_t) { return true; }), EQmPresenceSendResult::SENT);
	EXPECT_EQ(LastBody.size(), 4096u);
	EXPECT_EQ(LastBody.data(), pPrepared);
}
