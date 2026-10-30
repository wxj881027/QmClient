#include <engine/client/server_info_client.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{
	using EResult = server_info::EClientReadResult;
	class ServerInfoClient : public testing::Test
	{
	protected:
		CPacker m_Input;
		CUnpacker m_Unpacker;
		std::array<unsigned char, CPacker::PACKER_BUFFER_SIZE> m_aData{};
		std::array<int, 6> m_aFieldOffsets{};
		CServerInfo::CClient m_Result{};
		void Cached()
		{
			m_Result = {};
			str_copy(m_Result.m_aName, "cached");
			str_copy(m_Result.m_aClan, "old");
			str_copy(m_Result.m_aSkin, "old-skin");
			m_Result.m_Country = CountryCode::DEFAULT;
			m_Result.m_Score = 17;
			m_Result.m_Player = true;
			m_Result.m_Afk = true;
			m_Result.m_FriendState = 2;
		}
		void ExpectCached()
		{
			EXPECT_STREQ(m_Result.m_aName, "cached");
			EXPECT_STREQ(m_Result.m_aClan, "old");
			EXPECT_STREQ(m_Result.m_aSkin, "old-skin");
			EXPECT_EQ(m_Result.m_Country, CountryCode::DEFAULT);
			EXPECT_EQ(m_Result.m_Score, 17);
			EXPECT_TRUE(m_Result.m_Player);
			EXPECT_TRUE(m_Result.m_Afk);
			EXPECT_EQ(m_Result.m_FriendState, 2);
		}
		void Build(bool Extended = false, const char *pName = "player", const char *pClan = "clan", int Country = 156, int Score = -123, bool Player = true, const char *pExtra = "")
		{
			m_Input.Reset();
			const std::string CountryText = std::to_string(Country), ScoreText = std::to_string(Score);
			const char *apFields[] = {pName, pClan, CountryText.c_str(), ScoreText.c_str(), Player ? "1" : "0", pExtra};
			for(int Index = 0; Index < (Extended ? 6 : 5); ++Index)
			{
				m_aFieldOffsets[Index] = m_Input.Size();
				m_Input.AddString(apFields[Index]);
			}
			ASSERT_FALSE(m_Input.Error());
			mem_copy(m_aData.data(), m_Input.Data(), m_Input.Size());
			m_Unpacker.Reset(m_aData.data(), m_Input.Size());
			Cached();
		}
		EResult Read(bool Extended = false) { return server_info::ReadClient(m_Unpacker, Extended, m_Result); }
	};
}

TEST_F(ServerInfoClient, ExactPacketEndDoesNotCreateAClientOrAnError)
{
	Cached();
	m_Unpacker.Reset(nullptr, 0);
	EXPECT_EQ(Read(), EResult::END);
	EXPECT_FALSE(m_Unpacker.Error());
	ExpectCached();
}

TEST_F(ServerInfoClient, InvalidResetIsNotReportedAsCleanEnd)
{
	Cached();
	m_Unpacker.Reset(nullptr, 1);
	EXPECT_EQ(Read(), EResult::INVALID);
	EXPECT_TRUE(m_Unpacker.Error());
	ExpectCached();
}

TEST_F(ServerInfoClient, CompleteVanillaClientKeepsFieldsAndClearsUnsentDetails)
{
	Build();
	ASSERT_EQ(Read(), EResult::CLIENT);
	EXPECT_STREQ(m_Result.m_aName, "player");
	EXPECT_STREQ(m_Result.m_aClan, "clan");
	EXPECT_EQ(m_Result.m_Country, 156);
	EXPECT_EQ(m_Result.m_Score, -123);
	EXPECT_TRUE(m_Result.m_Player);
	EXPECT_FALSE(m_Result.m_Afk);
	EXPECT_EQ(m_Result.m_FriendState, 0);
	EXPECT_STREQ(m_Result.m_aSkin, "");
	EXPECT_FALSE(m_Unpacker.Error());
	EXPECT_EQ(Read(), EResult::END);
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(ServerInfoClient, CompleteExtendedClientConsumesItsReservedField)
{
	Build(true, "player", "clan", 156, 3, false, "reserved");
	ASSERT_EQ(Read(true), EResult::CLIENT);
	EXPECT_EQ(m_Result.m_Score, 3);
	EXPECT_FALSE(m_Result.m_Player);
	EXPECT_EQ(m_Unpacker.RemainingSize(), 0);
	EXPECT_EQ(Read(true), EResult::END);
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(ServerInfoClient, EmptyExtendedReservedFieldIsAccepted)
{
	Build(true);
	EXPECT_EQ(Read(true), EResult::CLIENT);
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(ServerInfoClient, EveryNonemptyTruncatedVanillaPrefixIsInvalid)
{
	Build();
	for(int Size = 1; Size < m_Input.Size(); ++Size)
	{
		SCOPED_TRACE(Size);
		m_Unpacker.Reset(m_aData.data(), Size);
		Cached();
		EXPECT_EQ(Read(), EResult::INVALID);
		EXPECT_TRUE(m_Unpacker.Error());
		ExpectCached();
	}
}

TEST_F(ServerInfoClient, EveryNonemptyTruncatedExtendedPrefixIsInvalid)
{
	Build(true, "player", "clan", 156, 3, true, "reserved");
	for(int Size = 1; Size < m_Input.Size(); ++Size)
	{
		SCOPED_TRACE(Size);
		m_Unpacker.Reset(m_aData.data(), Size);
		Cached();
		EXPECT_EQ(Read(true), EResult::INVALID);
		EXPECT_TRUE(m_Unpacker.Error());
		ExpectCached();
	}
}

TEST_F(ServerInfoClient, MissingExtendedFieldDoesNotPublishTheOtherwiseCompleteRecord)
{
	Build();
	EXPECT_EQ(Read(true), EResult::INVALID);
	EXPECT_TRUE(m_Unpacker.Error());
	ExpectCached();
}

TEST_F(ServerInfoClient, UnterminatedNameIsNotCleanPacketEnd)
{
	Cached();
	char aName[] = {'b', 'r', 'o', 'k', 'e', 'n'};
	m_Unpacker.Reset(aName, sizeof(aName));
	EXPECT_EQ(Read(), EResult::INVALID);
	EXPECT_TRUE(m_Unpacker.Error());
	ExpectCached();
}

TEST_F(ServerInfoClient, InvalidUtf8InEveryFieldRejectsTheRecord)
{
	for(int Field = 0; Field < 6; ++Field)
	{
		SCOPED_TRACE(Field);
		Build(true, "player", "clan", 156, 3, true, "extra");
		m_aData[m_aFieldOffsets[Field]] = 0xff;
		EXPECT_EQ(Read(true), EResult::INVALID);
		EXPECT_TRUE(m_Unpacker.Error());
		ExpectCached();
	}
}

TEST_F(ServerInfoClient, EmptyNameAndClanRemainValidWhenTheRecordIsComplete)
{
	Build(false, "", "");
	ASSERT_EQ(Read(), EResult::CLIENT);
	EXPECT_STREQ(m_Result.m_aName, "");
	EXPECT_STREQ(m_Result.m_aClan, "");
}

TEST_F(ServerInfoClient, ExistingStringSanitizationAndTruncationArePreserved)
{
	Build(false, "  player\t", "  clan");
	ASSERT_EQ(Read(), EResult::CLIENT);
	EXPECT_STREQ(m_Result.m_aName, "player ");
	EXPECT_STREQ(m_Result.m_aClan, "clan");
	const std::string Name(MAX_NAME_LENGTH + 20, 'n'), Clan(MAX_CLAN_LENGTH + 20, 'c');
	Build(false, Name.c_str(), Clan.c_str());
	ASSERT_EQ(Read(), EResult::CLIENT);
	EXPECT_EQ(std::string(m_Result.m_aName), std::string(MAX_NAME_LENGTH - 1, 'n'));
	EXPECT_EQ(std::string(m_Result.m_aClan), std::string(MAX_CLAN_LENGTH - 1, 'c'));
}

TEST_F(ServerInfoClient, OutOfRangeCountryKeepsTheExistingDefault)
{
	Build(false, "player", "clan", CountryCode::MAXIMUM + 1);
	ASSERT_EQ(Read(), EResult::CLIENT);
	EXPECT_EQ(m_Result.m_Country, CountryCode::DEFAULT);
}

TEST_F(ServerInfoClient, TruncatedNextRecordDoesNotReplaceThePreviousResult)
{
	Build();
	const int CompleteSize = m_Input.Size();
	const char aTail[] = {'b', 'a', 'd'};
	m_Input.AddRaw(aTail, sizeof(aTail));
	ASSERT_FALSE(m_Input.Error());
	mem_copy(m_aData.data(), m_Input.Data(), m_Input.Size());
	m_Unpacker.Reset(m_aData.data(), m_Input.Size());
	ASSERT_EQ(Read(), EResult::CLIENT);
	EXPECT_EQ(m_Unpacker.RemainingSize(), m_Input.Size() - CompleteSize);
	EXPECT_EQ(Read(), EResult::INVALID);
	EXPECT_TRUE(m_Unpacker.Error());
	EXPECT_STREQ(m_Result.m_aName, "player");
	EXPECT_EQ(m_Result.m_Score, -123);
}

TEST_F(ServerInfoClient, FreshResetAllowsValidInputAfterARejectedRecord)
{
	Build(true);
	m_aData[0] = 0xff;
	ASSERT_EQ(Read(true), EResult::INVALID);
	Build(true, "retried");
	ASSERT_EQ(Read(true), EResult::CLIENT);
	EXPECT_STREQ(m_Result.m_aName, "retried");
	EXPECT_FALSE(m_Unpacker.Error());
}
