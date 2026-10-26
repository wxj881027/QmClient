#include <engine/message.h>
#include <engine/shared/packer.h>

#include <game/client/components/qmclient/translated_game_message.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{
	template<typename TMessage, typename TCheck>
	void CheckPacket(const TMessage &Message, TCheck &&Check)
	{
		CMsgPacker Packet(TMessage::ms_MsgId);
		ASSERT_FALSE(Message.Pack(&Packet));
		CUnpacker Unpacker;
		Unpacker.Reset(Packet.Data(), Packet.Size());
		CNetObjHandler Handler;
		const auto *pDecoded = static_cast<const TMessage *>(Handler.SecureUnpackMsg(Packet.m_MsgId, &Unpacker));
		ASSERT_NE(pDecoded, nullptr);
		ASSERT_FALSE(Unpacker.Error());
		Check(*pDecoded);
	}

	template<typename TMessage>
	void CheckMessageAlignment(CQmTranslatedGameMessage &Storage)
	{
		EXPECT_EQ(reinterpret_cast<std::uintptr_t>(Storage.Reset<TMessage>()) % alignof(TMessage), 0u);
	}

	template<typename... TMessages>
	void CheckAlignment(CQmTranslatedGameMessage &Storage)
	{
		(CheckMessageAlignment<TMessages>(Storage), ...);
	}
}

TEST(TranslatedGameMessage, EveryNativeTypeRemainsAlignedInsideAPrefixedOwner)
{
	struct SOwner
	{
		char m_Prefix = 0;
		CQmTranslatedGameMessage m_Storage;
	} Owner;
	CheckAlignment<CNetMsg_Sv_Motd, CNetMsg_Sv_Broadcast, CNetMsg_Cl_SetTeam,
		CNetMsg_Sv_WeaponPickup, CNetMsg_Sv_RaceFinish, CNetMsg_Sv_DDRaceTime,
		CNetMsg_Sv_CommandInfoRemove, CNetMsg_Sv_CommandInfo, CNetMsg_Sv_VoteClearOptions,
		CNetMsg_Sv_VoteOptionAdd, CNetMsg_Sv_VoteOptionRemove, CNetMsg_Sv_VoteOptionListAdd,
		CNetMsg_Sv_VoteSet, CNetMsg_Sv_VoteStatus, CNetMsg_Sv_ReadyToEnter,
		CNetMsg_Sv_Emoticon, CNetMsg_Sv_KillMsg, CNetMsg_Sv_Chat>(Owner.m_Storage);
}

TEST(TranslatedGameMessage, SwitchingTypesAndReusingVoteSetCreatesFreshValues)
{
	CQmTranslatedGameMessage Storage;
	auto *pVote = Storage.Reset<CNetMsg_Sv_VoteSet>();
	pVote->m_Timeout = 15;
	pVote->m_pDescription = "old vote";
	pVote->m_pReason = "old reason";
	auto *pChat = Storage.Reset<CNetMsg_Sv_Chat>();
	pChat->m_ClientId = 7;
	pChat->m_pMessage = "chat";
	auto *pFresh = Storage.Reset<CNetMsg_Sv_VoteSet>();
	EXPECT_EQ(pFresh->m_Timeout, 0);
	EXPECT_EQ(pFresh->m_pDescription, nullptr);
	EXPECT_EQ(pFresh->m_pReason, nullptr);
}

TEST(TranslatedGameMessage, VoteSetPacksAndUnpacksWithOriginalFields)
{
	CQmTranslatedGameMessage Storage;
	auto *pVote = Storage.Reset<CNetMsg_Sv_VoteSet>();
	pVote->m_Timeout = 30;
	pVote->m_pDescription = "restart";
	pVote->m_pReason = "retry";
	CheckPacket(*pVote, [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_Timeout, 30);
		EXPECT_STREQ(Decoded.m_pDescription, "restart");
		EXPECT_STREQ(Decoded.m_pReason, "retry");
	});
}

TEST(TranslatedGameMessage, VoteOptionListPreservesAllDescriptionSlots)
{
	CQmTranslatedGameMessage Storage;
	auto *pList = Storage.Reset<CNetMsg_Sv_VoteOptionListAdd>();
	pList->m_NumOptions = 15;
	pList->m_pDescription0 = "first";
	pList->m_pDescription1 = "";
	pList->m_pDescription2 = "two";
	pList->m_pDescription3 = "three";
	pList->m_pDescription4 = "four";
	pList->m_pDescription5 = "five";
	pList->m_pDescription6 = "six";
	pList->m_pDescription7 = "seven";
	pList->m_pDescription8 = "eight";
	pList->m_pDescription9 = "nine";
	pList->m_pDescription10 = "ten";
	pList->m_pDescription11 = "eleven";
	pList->m_pDescription12 = "twelve";
	pList->m_pDescription13 = "thirteen";
	pList->m_pDescription14 = "last";
	CheckPacket(*pList, [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_NumOptions, 15);
		EXPECT_STREQ(Decoded.m_pDescription0, "first");
		EXPECT_STREQ(Decoded.m_pDescription1, "");
		EXPECT_STREQ(Decoded.m_pDescription2, "two");
		EXPECT_STREQ(Decoded.m_pDescription3, "three");
		EXPECT_STREQ(Decoded.m_pDescription4, "four");
		EXPECT_STREQ(Decoded.m_pDescription5, "five");
		EXPECT_STREQ(Decoded.m_pDescription6, "six");
		EXPECT_STREQ(Decoded.m_pDescription7, "seven");
		EXPECT_STREQ(Decoded.m_pDescription8, "eight");
		EXPECT_STREQ(Decoded.m_pDescription9, "nine");
		EXPECT_STREQ(Decoded.m_pDescription10, "ten");
		EXPECT_STREQ(Decoded.m_pDescription11, "eleven");
		EXPECT_STREQ(Decoded.m_pDescription12, "twelve");
		EXPECT_STREQ(Decoded.m_pDescription13, "thirteen");
		EXPECT_STREQ(Decoded.m_pDescription14, "last");
	});
}

TEST(TranslatedGameMessage, RaceFinishKeepsTimeDifferenceAndRecordFlags)
{
	CQmTranslatedGameMessage Storage;
	auto *pFinish = Storage.Reset<CNetMsg_Sv_RaceFinish>();
	pFinish->m_ClientId = 3;
	pFinish->m_Time = 12345;
	pFinish->m_Diff = -300;
	pFinish->m_RecordPersonal = 1;
	pFinish->m_RecordServer = 0;
	CheckPacket(*pFinish, [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_ClientId, 3);
		EXPECT_EQ(Decoded.m_Time, 12345);
		EXPECT_EQ(Decoded.m_Diff, -300);
		EXPECT_TRUE(Decoded.m_RecordPersonal);
		EXPECT_FALSE(Decoded.m_RecordServer);
	});
}

TEST(TranslatedGameMessage, ChatThenKillUsesEachNativeMessageFormat)
{
	CQmTranslatedGameMessage Storage;
	auto *pChat = Storage.Reset<CNetMsg_Sv_Chat>();
	pChat->m_Team = 0;
	pChat->m_ClientId = 3;
	pChat->m_pMessage = "hello";
	ASSERT_NO_FATAL_FAILURE(CheckPacket(*pChat, [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_ClientId, 3);
		EXPECT_STREQ(Decoded.m_pMessage, "hello");
	}));
	auto *pKill = Storage.Reset<CNetMsg_Sv_KillMsg>();
	pKill->m_Killer = 3;
	pKill->m_Victim = 4;
	pKill->m_Weapon = WEAPON_GUN;
	pKill->m_ModeSpecial = 0;
	CheckPacket(*pKill, [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_Killer, 3);
		EXPECT_EQ(Decoded.m_Victim, 4);
		EXPECT_EQ(Decoded.m_Weapon, WEAPON_GUN);
	});
}

TEST(TranslatedGameMessage, CommandInfoBorrowsStringsAndPacksTheirContents)
{
	CQmTranslatedGameMessage Storage;
	std::string Name = "help", Args = "?r[topic]", Help = "show help";
	auto *pCommand = Storage.Reset<CNetMsg_Sv_CommandInfo>();
	pCommand->m_pName = Name.c_str();
	pCommand->m_pArgsFormat = Args.c_str();
	pCommand->m_pHelpText = Help.c_str();
	EXPECT_EQ(pCommand->m_pName, Name.c_str());
	CheckPacket(*pCommand, [](const auto &Decoded) {
		EXPECT_STREQ(Decoded.m_pName, "help");
		EXPECT_STREQ(Decoded.m_pArgsFormat, "?r[topic]");
		EXPECT_STREQ(Decoded.m_pHelpText, "show help");
	});
}

TEST(TranslatedGameMessage, EmptyReadyAndClearMessagesRemainValidNativeMessages)
{
	CQmTranslatedGameMessage Storage;
	ASSERT_NO_FATAL_FAILURE(CheckPacket(*Storage.Reset<CNetMsg_Sv_ReadyToEnter>(), [](const auto &) {}));
	ASSERT_NO_FATAL_FAILURE(CheckPacket(*Storage.Reset<CNetMsg_Sv_VoteClearOptions>(), [](const auto &) {}));
}
