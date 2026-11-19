#include <base/str.h>

#include <engine/serverbrowser.h>

#include <game/client/components/statboard.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>

class StatboardCsv : public ::testing::Test
{
	CTestInfo m_Info;

protected:
	static constexpr const char *PATH = "stats.csv";
	std::unique_ptr<IStorage> m_pStorage;

	void SetUp() override
	{
		m_pStorage = m_Info.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
	}

	void WriteExisting(const char *pPath, const std::string &Contents)
	{
		IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		const unsigned Written = io_write(File, Contents.data(), static_cast<unsigned>(Contents.size()));
		const int Closed = io_close(File);
		ASSERT_EQ(Written, Contents.size());
		ASSERT_EQ(Closed, 0);
	}

	std::string Read(const char *pPath)
	{
		std::unique_ptr<char, decltype(&free)> pText(m_pStorage->ReadFileStr(pPath, IStorage::TYPE_SAVE), free);
		EXPECT_NE(pText, nullptr);
		return pText ? pText.get() : "";
	}
};

TEST_F(StatboardCsv, KeepsShortCommaSeparatedFieldsAndAllWeaponColumns)
{
	CServerInfo Server{};
	str_copy(Server.m_aGameType, "C,TF");
	CStatboardCsv Csv("A,B", Server.m_aGameType, "Map,One");
	std::array<int, NUM_WEAPONS> aFrags{};
	std::array<int, NUM_WEAPONS> aDeaths{};
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		aFrags[Weapon] = Weapon + 1;
		aDeaths[Weapon] = Weapon + 7;
	}
	const char aName[] = "a,b";
	const char aClan[] = "c,d";
	Csv.AddPlayer({
		.m_Local = true,
		.m_Name = aName,
		.m_Clan = aClan,
		.m_Score = 15,
		.m_Frags = 12,
		.m_Deaths = 4,
		.m_Fpm = 2.5f,
		.m_FragsWith = aFrags,
		.m_DeathsFrom = aDeaths,
	});

	ASSERT_TRUE(Csv.Save(*m_pStorage, PATH));
	const auto Contents = Read(PATH);
	EXPECT_TRUE(Contents.starts_with("Servername,Game-type,Map\nA%2CB,C%2CTF,Map%2COne\n\n"));
	EXPECT_TRUE(Contents.ends_with("1,0,a%2Cb,c%2Cd,15,12,4,0,3.00,8,2.5,0,0,1/7,2/8,3/9,4/10,5/11,6/12,0,0,0\n"));
}

TEST_F(StatboardCsv, SavesAllRowsBeyondTheVanillaBuffer)
{
	CStatboardCsv Csv("Server", "CTF", "Map");
	std::array<int, NUM_WEAPONS> aFrags;
	aFrags.fill(123);
	std::array<int, NUM_WEAPONS> aDeaths;
	aDeaths.fill(456);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		char aName[MAX_NAME_LENGTH];
		str_format(aName, sizeof(aName), "%03d,,,,,,,,,,,,", ClientId);
		Csv.AddPlayer({
			.m_Name = aName,
			.m_Clan = ",,,,,,,,,,,",
			.m_Score = 999,
			.m_Frags = 738,
			.m_Deaths = 2736,
			.m_Suicides = 10,
			.m_Fpm = 4.5f,
			.m_Spree = 15,
			.m_BestSpree = 100,
			.m_FragsWith = aFrags,
			.m_DeathsFrom = aDeaths,
		});
	}

	ASSERT_TRUE(Csv.Save(*m_pStorage, PATH));
	const auto Contents = Read(PATH);
	EXPECT_GT(Contents.size(), 1024 * VANILLA_MAX_CLIENTS);
	EXPECT_EQ(std::count(Contents.begin(), Contents.end(), '\n'), MAX_CLIENTS + 4);
	ASSERT_TRUE(Contents.ends_with('\n'));
	const auto LastRowStart = Contents.rfind('\n', Contents.size() - 2);
	ASSERT_NE(LastRowStart, std::string::npos);
	char aLastPlayer[32];
	str_format(aLastPlayer, sizeof(aLastPlayer), "0,0,%03d%%2C", MAX_CLIENTS - 1);
	EXPECT_EQ(Contents.compare(LastRowStart + 1, str_length(aLastPlayer), aLastPlayer), 0);
}

TEST_F(StatboardCsv, KeepsPreviousExportAndCandidateIfTemporaryPathAlreadyExists)
{
	char aTemporary[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), PATH);
	WriteExisting(PATH, "previous export");
	WriteExisting(aTemporary, "another candidate");

	CStatboardCsv Csv("Server", "CTF", "Map");
	EXPECT_FALSE(Csv.Save(*m_pStorage, PATH));
	EXPECT_EQ(Read(PATH), "previous export");
	EXPECT_EQ(Read(aTemporary), "another candidate");
}

TEST_F(StatboardCsv, FailedPublicationRemovesItsCandidateAndAllowsTheNextSave)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(PATH, IStorage::TYPE_SAVE));
	WriteExisting("stats.csv/keep.txt", "keep");

	CStatboardCsv Csv("Server", "CTF", "Map");
	EXPECT_FALSE(Csv.Save(*m_pStorage, PATH));
	EXPECT_EQ(Read("stats.csv/keep.txt"), "keep");
	char aTemporary[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), PATH);
	EXPECT_FALSE(m_pStorage->FileExists(aTemporary, IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->RemoveFile("stats.csv/keep.txt", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->RemoveFolder(PATH, IStorage::TYPE_SAVE));
	WriteExisting(PATH, "previous export");
	ASSERT_TRUE(Csv.Save(*m_pStorage, PATH));
	EXPECT_TRUE(Read(PATH).starts_with("Servername,Game-type,Map\nServer,CTF,Map\n"));
}
