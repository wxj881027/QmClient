#include <engine/client/sixup_server_info.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>

namespace
{
	class CSixupServerInfoTest : public testing::Test
	{
	protected:
		CServerInfo m_Info{};
		NETADDR m_Address{};
		CPacker m_Input;
		CUnpacker m_Unpacker;
		const CServerInfo::CClient *m_pClients = nullptr;

		void SetUp() override
		{
			str_copy(m_Info.m_aAddress, "old-address");
			str_copy(m_Info.m_aVersion, "old-version");
			str_copy(m_Info.m_aName, "old-name");
			str_copy(m_Info.m_aGameType, "old-type");
			str_copy(m_Info.m_aMap, "loaded-map");
			m_Info.m_NumPlayers = 1;
			m_Info.m_MaxPlayers = 10;
			m_Info.m_NumClients = 2;
			m_Info.m_MaxClients = 12;
			m_Info.m_Flags = SERVER_FLAG_PASSWORD | SERVER_FLAG_TIMESCORE | 128;
			m_Info.m_HasRank = CServerInfo::RANK_RANKED;
			m_Info.m_Latency = 27;
			m_Info.m_QmClientCount = 3;
			m_Info.m_vClients.resize(1);
			str_copy(m_Info.m_vClients[0].m_aName, "cached-player");
			m_pClients = m_Info.m_vClients.data();
			ASSERT_EQ(net_addr_from_str(&m_Address, "127.0.0.1:8303"), 0);
			PrepareInput();
		}

		void PrepareInput(std::array<int, 4> Counts = {3, 12, 4, 16}, int Flags = 0,
			const char *pVersion = "  v0.7", const char *pName = "  new   server  ", const char *pType = " DDNet")
		{
			m_Input.Reset();
			m_Input.AddString(pVersion);
			m_Input.AddString(pName);
			m_Input.AddString("hostname");
			m_Input.AddString("advertised-map");
			m_Input.AddString(pType);
			m_Input.AddInt(Flags);
			m_Input.AddInt(2);
			for(int Count : Counts)
				m_Input.AddInt(Count);
			m_Unpacker.Reset(m_Input.Data(), m_Input.Size());
		}

		void ExpectAncillaryInfo()
		{
			EXPECT_STREQ(m_Info.m_aMap, "loaded-map");
			EXPECT_EQ(m_Info.m_HasRank, CServerInfo::RANK_RANKED);
			EXPECT_EQ(m_Info.m_Latency, 27);
			EXPECT_EQ(m_Info.m_QmClientCount, 3);
			EXPECT_EQ(m_Info.m_vClients.data(), m_pClients);
			ASSERT_EQ(m_Info.m_vClients.size(), 1U);
			EXPECT_STREQ(m_Info.m_vClients[0].m_aName, "cached-player");
		}

		void ExpectOldInfo()
		{
			EXPECT_STREQ(m_Info.m_aAddress, "old-address");
			EXPECT_STREQ(m_Info.m_aVersion, "old-version");
			EXPECT_STREQ(m_Info.m_aName, "old-name");
			EXPECT_STREQ(m_Info.m_aGameType, "old-type");
			EXPECT_EQ(m_Info.m_NumPlayers, 1);
			EXPECT_EQ(m_Info.m_MaxPlayers, 10);
			EXPECT_EQ(m_Info.m_NumClients, 2);
			EXPECT_EQ(m_Info.m_MaxClients, 12);
			EXPECT_EQ(m_Info.m_Flags, SERVER_FLAG_PASSWORD | SERVER_FLAG_TIMESCORE | 128);
			ExpectAncillaryInfo();
		}
	};
}

TEST_F(CSixupServerInfoTest, CompleteMessageUpdatesOnlyAdvertisedFields)
{
	ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	EXPECT_STREQ(m_Info.m_aAddress, "127.0.0.1:8303");
	EXPECT_STREQ(m_Info.m_aVersion, "v0.7");
	EXPECT_STREQ(m_Info.m_aName, "new server");
	EXPECT_STREQ(m_Info.m_aGameType, "DDNet");
	EXPECT_EQ(m_Info.m_NumPlayers, 3);
	EXPECT_EQ(m_Info.m_MaxPlayers, 12);
	EXPECT_EQ(m_Info.m_NumClients, 4);
	EXPECT_EQ(m_Info.m_MaxClients, 16);
	ExpectAncillaryInfo();
}

TEST_F(CSixupServerInfoTest, EveryTruncatedPrefixPreservesCurrentInformation)
{
	for(int Size = 0; Size < m_Input.Size(); ++Size)
	{
		SCOPED_TRACE(Size);
		m_Unpacker.Reset(m_Input.Data(), Size);
		EXPECT_FALSE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
		ExpectOldInfo();
	}
}

TEST_F(CSixupServerInfoTest, InvalidUtf8PreservesCurrentInformation)
{
	CPacker Bad;
	Bad.Reset();
	const unsigned char aInvalidVersion[] = {0xff, 0};
	Bad.AddRaw(aInvalidVersion, sizeof(aInvalidVersion));
	constexpr int VersionBytes = sizeof("  v0.7");
	Bad.AddRaw(m_Input.Data() + VersionBytes, m_Input.Size() - VersionBytes);
	m_Unpacker.Reset(Bad.Data(), Bad.Size());
	EXPECT_FALSE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	EXPECT_TRUE(m_Unpacker.Error());
	ExpectOldInfo();
}

TEST_F(CSixupServerInfoTest, ExistingUnpackerErrorPreservesCurrentInformation)
{
	m_Unpacker.GetRaw(m_Input.Size() + 1);
	ASSERT_TRUE(m_Unpacker.Error());
	EXPECT_FALSE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	ExpectOldInfo();
}

TEST_F(CSixupServerInfoTest, NegativeOrInconsistentCountsPreserveCurrentInformation)
{
	const std::array<std::array<int, 4>, 6> aCounts{{{-1, 12, 4, 16}, {3, -1, 4, 16}, {3, 12, -1, 16},
		{3, 12, 4, -1}, {5, 12, 4, 16}, {3, 17, 4, 16}}};
	for(size_t i = 0; i < aCounts.size(); ++i)
	{
		SCOPED_TRACE(i);
		PrepareInput(aCounts[i]);
		EXPECT_FALSE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
		EXPECT_FALSE(m_Unpacker.Error());
		ExpectOldInfo();
	}
}

TEST_F(CSixupServerInfoTest, PasswordCanBeEnabledThenClearedWithoutChangingOtherFlags)
{
	PrepareInput({3, 12, 4, 16}, SERVER_FLAG_PASSWORD);
	ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	EXPECT_EQ(m_Info.m_Flags, SERVER_FLAG_PASSWORD | SERVER_FLAG_TIMESCORE | 128);
	PrepareInput();
	ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	EXPECT_EQ(m_Info.m_Flags, SERVER_FLAG_TIMESCORE | 128);
}

TEST_F(CSixupServerInfoTest, IncomingNonPasswordFlagsDoNotReplaceBrowserFlags)
{
	m_Info.m_Flags = 128;
	PrepareInput({3, 12, 4, 16}, SERVER_FLAG_TIMESCORE | 256);
	ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	EXPECT_EQ(m_Info.m_Flags, 128);
}

TEST_F(CSixupServerInfoTest, CompleteMessageCanRecoverAfterAnInvalidUpdate)
{
	PrepareInput({-1, 12, 4, 16});
	EXPECT_FALSE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	ExpectOldInfo();
	PrepareInput({5, 20, 6, 24});
	ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	EXPECT_EQ(m_Info.m_NumPlayers, 5);
	EXPECT_EQ(m_Info.m_MaxClients, 24);
	ExpectAncillaryInfo();
}

TEST_F(CSixupServerInfoTest, ZeroLargeAndOverCapacityCountsKeepExistingBrowserSemantics)
{
	constexpr int Max = std::numeric_limits<int>::max();
	const std::array<std::array<int, 4>, 3> aCounts{{{0, 0, 0, 0}, {2, 2, 3, 2}, {Max, Max, Max, Max}}};
	for(size_t i = 0; i < aCounts.size(); ++i)
	{
		SCOPED_TRACE(i);
		PrepareInput(aCounts[i]);
		ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
		EXPECT_EQ(m_Info.m_NumPlayers, aCounts[i][0]);
		EXPECT_EQ(m_Info.m_MaxPlayers, aCounts[i][1]);
		EXPECT_EQ(m_Info.m_NumClients, aCounts[i][2]);
		EXPECT_EQ(m_Info.m_MaxClients, aCounts[i][3]);
	}
}

TEST_F(CSixupServerInfoTest, StoredStringsAreBoundedAndIndependentOfInputLifetime)
{
	const std::string LongVersion(100, 'v');
	const std::string LongName(150, 'n');
	const std::string LongType(50, 't');
	PrepareInput({3, 12, 4, 16}, 0, LongVersion.c_str(), LongName.c_str(), LongType.c_str());
	ASSERT_TRUE(sixup_translation::ApplyServerInfo(m_Info, m_Address, m_Unpacker));
	m_Input.Reset();
	m_Input.AddString("overwritten packet");
	EXPECT_EQ(str_length(m_Info.m_aVersion), static_cast<int>(sizeof(m_Info.m_aVersion)) - 1);
	EXPECT_EQ(str_length(m_Info.m_aName), static_cast<int>(sizeof(m_Info.m_aName)) - 1);
	EXPECT_EQ(str_length(m_Info.m_aGameType), static_cast<int>(sizeof(m_Info.m_aGameType)) - 1);
	EXPECT_EQ(m_Info.m_aVersion[0], 'v');
	EXPECT_EQ(m_Info.m_aName[0], 'n');
	EXPECT_EQ(m_Info.m_aGameType[0], 't');
}
