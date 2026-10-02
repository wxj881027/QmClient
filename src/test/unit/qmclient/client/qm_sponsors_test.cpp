#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qm_sponsors.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

TEST(QmSponsors, ParsesListItemsWithoutInterpretingInlineMarkdown)
{
	const auto vNames = qm_sponsors::ParseNames("\xef\xbb\xbf# Thanks\r\n- Alice\r\n* B **old**\n+ Cara\n12. Dee\nplain text\n- \n");
	ASSERT_EQ(vNames.size(), 4u);
	EXPECT_EQ(vNames[0], "Alice");
	EXPECT_EQ(vNames[1], "B **old**");
	EXPECT_EQ(vNames[2], "Cara");
	EXPECT_EQ(vNames[3], "Dee");
}

TEST(QmSponsors, DiscardsIncompleteLineAtByteLimit)
{
	std::string Markdown = "- valid\n";
	Markdown += std::string(64 * 1024 - Markdown.size() - 2, ' ');
	Markdown += "- x";
	Markdown += "tail\n";
	const auto vNames = qm_sponsors::ParseNames(Markdown.c_str());
	ASSERT_EQ(vNames.size(), 1u);
	EXPECT_EQ(vNames[0], "valid");
}

TEST(QmSponsors, ConsumesWebSocketPayloadAndKeepsNewerVersion)
{
	qm_sponsors::CSnapshot Snapshot;
	bool Changed = false;
	SQmRealtimeMessage Message;
	const char *pFirst = R"({"type":"sponsors","data":{"version":4,"markdown":"- Alice\n- Bob"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pFirst, std::strlen(pFirst), Message));
	ASSERT_EQ(Message.m_Event, EQmRealtimeEvent::SPONSORS);
	ASSERT_TRUE(Message.m_HasRealtimeData);
	ASSERT_TRUE(Snapshot.Apply(Message.m_pPayload.get(), Changed));
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Snapshot.Names().size(), 2u);
	EXPECT_EQ(Snapshot.Revision(), 1);

	const char *pOld = R"({"type":"sponsors","data":{"version":3,"markdown":"- Old"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pOld, std::strlen(pOld), Message));
	ASSERT_TRUE(Snapshot.Apply(Message.m_pPayload.get(), Changed));
	EXPECT_FALSE(Changed);
	EXPECT_EQ(Snapshot.Names()[0], "Alice");
	EXPECT_EQ(Snapshot.Revision(), 1);

	const char *pNew = R"({"type":"sponsors","data":{"version":5,"markdown":""}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pNew, std::strlen(pNew), Message));
	ASSERT_TRUE(Snapshot.Apply(Message.m_pPayload.get(), Changed));
	EXPECT_TRUE(Changed);
	EXPECT_TRUE(Snapshot.Names().empty());
	EXPECT_EQ(Snapshot.Revision(), 2);
}

TEST(QmSponsors, RejectsMalformedPayloadWithoutChangingSnapshot)
{
	qm_sponsors::CSnapshot Snapshot;
	bool Changed = false;
	const json_value *pUnused = nullptr;
	EXPECT_FALSE(Snapshot.Apply(pUnused, Changed));
	EXPECT_FALSE(Changed);

	SQmRealtimeMessage Message;
	const char *pInvalid = R"({"type":"sponsors","data":{"version":-1,"markdown":"- Bad"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pInvalid, std::strlen(pInvalid), Message));
	EXPECT_FALSE(Snapshot.Apply(Message.m_pPayload.get(), Changed));
	EXPECT_EQ(Snapshot.Version(), -1);
}
