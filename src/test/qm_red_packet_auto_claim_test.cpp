#include <game/client/components/qmclient/red_packet_auto_claim.h>

#include <gtest/gtest.h>

TEST(QmRedPacketAutoClaim, ExtractsPasswordFromServerAnnouncement)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「deimos:把钱给我！」即可抢。",
		Password));
	EXPECT_EQ(Password, "deimos:把钱给我！");
}

TEST(QmRedPacketAutoClaim, ExtractsAllPasswordCharactersFromCompatibleAnnouncement)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] taiko 发了 50 币红包，共 50 个。输入口令「\\\" \\\"」即可抢。",
		Password));
	EXPECT_EQ(Password, "\\\" \\\"");
}

TEST(QmRedPacketAutoClaim, AcceptsCompatibleAnnouncementWithAdditionalText)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] 红包提示：输入口令「deimos:把钱给我！」即可抢，先到先得。",
		Password));
	EXPECT_EQ(Password, "deimos:把钱给我！");
}

TEST(QmRedPacketAutoClaim, PreservesWhitespaceOnlyPassword)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] 红包提示：输入口令「 」即可抢。",
		Password));
	EXPECT_EQ(Password, " ");
}

TEST(QmRedPacketAutoClaim, RejectsMalformedAnnouncements)
{
	const char *apInvalidMessages[] = {
		"deimos:把钱给我！",
		"[Tee新葡京] 红包提示：输入口令「」即可抢。",
		"[Tee新葡京] 红包提示：口令「deimos:把钱给我！」即可抢。",
		"[Tee新葡京] 输入口令「deimos:把钱给我！」即可抢。",
		"[Tee新葡京] 红包提示：输入口令「deimos:把钱给我！」。",
		"[Tee新葡京] 红包提示：即可抢。输入口令「deimos:把钱给我！」",
		"[Tee新葡京] 输入口令「deimos:把钱给我！」红包提示，即可抢。",
	};

	for(const char *pMessage : apInvalidMessages)
	{
		CQmRedPacketAutoClaim Claim;
		std::string Password;
		EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password)) << pMessage;
	}
}

TEST(QmRedPacketAutoClaim, RequiresExactServerAndMainPlayerName)
{
	const char *pMessage = "[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「deimos:把钱给我！」即可抢。";
	std::string Password;

	CQmRedPacketAutoClaim WrongServer;
	EXPECT_FALSE(WrongServer.TryPrepare("110.42.41.209:8304", "璇梦", pMessage, Password));

	CQmRedPacketAutoClaim WrongName;
	EXPECT_FALSE(WrongName.TryPrepare("110.42.41.209:8303", "璇夢", pMessage, Password));
}

TEST(QmRedPacketAutoClaim, SendsEachAnnouncementOnlyOncePerConnection)
{
	CQmRedPacketAutoClaim Claim;
	const char *pMessage = "[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「deimos:把钱给我！」即可抢。";
	const char *pAnotherMessage = "[Tee新葡京] taiko 发了 50 币红包，共 50 个。输入口令「deimos:把钱给我！」即可抢。";
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password));
	EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password));
	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pAnotherMessage, Password));

	Claim.Reset();
	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password));
}

TEST(QmRedPacketAutoClaim, BoundsDeduplicationHistoryDuringLongConnections)
{
	CQmRedPacketAutoClaim Claim;
	constexpr size_t DeduplicationHistoryLimit = 64;
	std::string FirstMessage;
	std::string Password;

	for(size_t i = 0; i <= DeduplicationHistoryLimit; ++i)
	{
		const std::string Message = "[Tee新葡京] 红包提示 " + std::to_string(i) + "：输入口令「claim-" + std::to_string(i) + "」即可抢。";
		if(i == 0)
			FirstMessage = Message;
		EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", Message.c_str(), Password));
	}

	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", FirstMessage.c_str(), Password));
	EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", FirstMessage.c_str(), Password));
}

TEST(QmRedPacketAutoClaim, PreservesPasswordAtChatCharacterLimit)
{
	CQmRedPacketAutoClaim Claim;
	std::string ExpectedPassword;
	for(size_t i = 0; i < CQmRedPacketAutoClaim::MAX_PASSWORD_CHARACTERS; ++i)
		ExpectedPassword += "钱";
	const std::string Message = "[Tee新葡京] 红包提示：输入口令「" + ExpectedPassword + "」即可抢。";
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", Message.c_str(), Password));
	EXPECT_EQ(Password, ExpectedPassword);
}

TEST(QmRedPacketAutoClaim, RejectsPasswordThatExceedsChatCharacterLimit)
{
	CQmRedPacketAutoClaim Claim;
	std::string Message = "[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「";
	Message.append(CQmRedPacketAutoClaim::MAX_PASSWORD_CHARACTERS + 1, 'a');
	Message += "」即可抢。";
	std::string Password;

	EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", Message.c_str(), Password));
}
