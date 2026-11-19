#include <engine/client/sixup_system_messages.h>
#include <engine/shared/protocol_ex.h>

#include <gtest/gtest.h>

#include <limits>

TEST(SixupSystemMessages, IncomingPingNeverRoutesToRconCommandRemoval)
{
	const int Handler = sixup_translation::IncomingSystemMessageId(protocol7::NETMSG_PING);
	EXPECT_EQ(Handler, NETMSG_PING);
	EXPECT_NE(Handler, NETMSG_RCON_CMD_REM);
}

TEST(SixupSystemMessages, PingReplyMapsInBothDirections)
{
	const int Reply = sixup_translation::OutgoingSystemMessageId(NETMSG_PING_REPLY);
	EXPECT_EQ(Reply, protocol7::NETMSG_PING_REPLY);
	EXPECT_EQ(sixup_translation::IncomingSystemMessageId(Reply), NETMSG_PING_REPLY);
}

TEST(SixupSystemMessages, IncomingOriginalPayloadsRetainHandlerMappings)
{
	const int aaMappings[][2] = {
		{protocol7::NETMSG_MAP_DATA, NETMSG_MAP_DATA},
		{protocol7::NETMSG_CON_READY, NETMSG_CON_READY},
		{protocol7::NETMSG_SNAP, NETMSG_SNAP},
		{protocol7::NETMSG_SNAPEMPTY, NETMSG_SNAPEMPTY},
		{protocol7::NETMSG_SNAPSINGLE, NETMSG_SNAPSINGLE},
		{protocol7::NETMSG_SNAPSMALL, NETMSG_SNAPSMALL},
		{protocol7::NETMSG_INPUTTIMING, NETMSG_INPUTTIMING},
		{protocol7::NETMSG_RCON_LINE, NETMSG_RCON_LINE},
		{protocol7::NETMSG_RCON_CMD_ADD, NETMSG_RCON_CMD_ADD},
		{protocol7::NETMSG_RCON_CMD_REM, NETMSG_RCON_CMD_REM},
	};
	for(const auto &aMapping : aaMappings)
	{
		SCOPED_TRACE(aMapping[0]);
		EXPECT_EQ(sixup_translation::IncomingSystemMessageId(aMapping[0]), aMapping[1]);
	}
}

TEST(SixupSystemMessages, OutgoingExistingSystemMessagesRetainWireNumbers)
{
	const int aaMappings[][2] = {
		{NETMSG_INFO, NETMSG_INFO},
		{NETMSG_REQUEST_MAP_DATA, NETMSG_REQUEST_MAP_DATA},
		{NETMSG_READY, protocol7::NETMSG_READY},
		{NETMSG_RCON_CMD, protocol7::NETMSG_RCON_CMD},
		{NETMSG_ENTERGAME, protocol7::NETMSG_ENTERGAME},
		{NETMSG_INPUT, protocol7::NETMSG_INPUT},
		{NETMSG_RCON_AUTH, protocol7::NETMSG_RCON_AUTH},
		{NETMSG_PING, protocol7::NETMSG_PING},
	};
	for(const auto &aMapping : aaMappings)
	{
		SCOPED_TRACE(aMapping[0]);
		EXPECT_EQ(sixup_translation::OutgoingSystemMessageId(aMapping[0]), aMapping[1]);
	}
}

TEST(SixupSystemMessages, IncomingClientOnlyReservedAndMapListMessagesAreRejected)
{
	for(int MsgId : {protocol7::NETMSG_NULL, protocol7::NETMSG_INFO,
		    protocol7::NETMSG_READY, protocol7::NETMSG_ENTERGAME, protocol7::NETMSG_INPUT,
		    protocol7::NETMSG_RCON_CMD, protocol7::NETMSG_RCON_AUTH, protocol7::NETMSG_REQUEST_MAP_DATA,
		    protocol7::NETMSG_UNUSED1, protocol7::NETMSG_UNUSED2, protocol7::NETMSG_UNUSED3,
		    protocol7::NETMSG_UNUSED4, protocol7::NETMSG_UNUSED5,
		    protocol7::NETMSG_MAPLIST_ENTRY_ADD, protocol7::NETMSG_MAPLIST_ENTRY_REM})
	{
		SCOPED_TRACE(MsgId);
		EXPECT_EQ(sixup_translation::IncomingSystemMessageId(MsgId), -1);
	}
}

TEST(SixupSystemMessages, RepackedIncomingMessagesAreNotAcceptedAsOriginalPayloads)
{
	for(int MsgId : {protocol7::NETMSG_MAP_CHANGE, protocol7::NETMSG_SERVERINFO,
		    protocol7::NETMSG_RCON_AUTH_ON, protocol7::NETMSG_RCON_AUTH_OFF})
	{
		SCOPED_TRACE(MsgId);
		EXPECT_EQ(sixup_translation::IncomingSystemMessageId(MsgId), -1);
	}
}

TEST(SixupSystemMessages, UnknownIncomingMessageNumbersAreRejected)
{
	for(int MsgId : {std::numeric_limits<int>::min(), -1, 31, 100, OFFSET_UUID - 1})
	{
		SCOPED_TRACE(MsgId);
		EXPECT_EQ(sixup_translation::IncomingSystemMessageId(MsgId), -1);
	}
}

TEST(SixupSystemMessages, OutgoingUnsupportedMessagesAreRejected)
{
	for(int MsgId : {NETMSG_RCON_CMD_ADD, NETMSG_RCON_CMD_REM, NETMSG_RCON_AUTH_STATUS,
		    NETMSG_CON_READY, NETMSG_SNAP})
	{
		SCOPED_TRACE(MsgId);
		EXPECT_EQ(sixup_translation::OutgoingSystemMessageId(MsgId), -1);
	}
}

TEST(SixupSystemMessages, UuidExtensionMessageIdsArePreservedInBothDirections)
{
	const int aMsgIds[] = {OFFSET_UUID, OFFSET_UUID + 1, NETMSG_WHATIS,
		NETMSG_RCON_CMD_GROUP_END, NETMSG_CLIENT_BRANDS, std::numeric_limits<int>::max()};
	for(int MsgId : aMsgIds)
	{
		SCOPED_TRACE(MsgId);
		EXPECT_EQ(sixup_translation::IncomingSystemMessageId(MsgId), MsgId);
		EXPECT_EQ(sixup_translation::OutgoingSystemMessageId(MsgId), MsgId);
	}
}
