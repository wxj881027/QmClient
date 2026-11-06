#include <engine/shared/config.h>
#include <engine/shared/packer.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/components/motd_message.h>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace
{
	template<typename THandler, typename TMessage>
	void CheckLongMessage(int MessageType)
	{
		std::array<char, sizeof(g_Config.m_SvMotd)> aBuffer;
		const std::string Prefix(aBuffer.size() - 1, 'x');
		const std::string Encoded = Prefix + "\\n";
		CPacker Packer;
		Packer.Reset();
		Packer.AddString(Encoded.c_str(), -1);
		ASSERT_FALSE(Packer.Error());
		CUnpacker Unpacker;
		Unpacker.Reset(Packer.Data(), Packer.Size());
		THandler Handler;
		const auto *pMessage = static_cast<const TMessage *>(Handler.SecureUnpackMsg(MessageType, &Unpacker));
		ASSERT_NE(pMessage, nullptr);
		ASSERT_FALSE(Unpacker.Error());
		ASSERT_EQ(std::string_view(pMessage->m_pMessage).size(), Encoded.size());

		const auto Message = DecodeMotdMessage(pMessage->m_pMessage, aBuffer.data(), aBuffer.size());
		std::vector<std::string> vLines;
		ForEachMotdLine(Message, [&](std::string_view Line) { vLines.emplace_back(Line); });
		EXPECT_EQ(Message, Prefix);
		EXPECT_EQ(aBuffer.back(), '\0');
		EXPECT_EQ(vLines, (std::vector<std::string>{Prefix}));
	}
}

TEST(MotdMessageProtocol, SixMessageCanExceedLocalConfigCapacityAndRemainsBounded)
{
	CheckLongMessage<CNetObjHandler, CNetMsg_Sv_Motd>(NETMSGTYPE_SV_MOTD);
}

TEST(MotdMessageProtocol, SevenMessageCanExceedLocalConfigCapacityAndRemainsBounded)
{
	CheckLongMessage<protocol7::CNetObjHandler, protocol7::CNetMsg_Sv_Motd>(protocol7::NETMSGTYPE_SV_MOTD);
}
