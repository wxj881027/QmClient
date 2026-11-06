#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/editor/mapitems/load_info.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <climits>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
	class EditorMapInfo : public ::testing::Test
	{
	protected:
		struct SItems
		{
			bool m_HasInfo = true;
			int m_InfoId = 0;
			int m_InfoVersion = 1;
			size_t m_InfoSize = sizeof(CMapItemInfoSettings);
			std::optional<int> m_SettingsIndex;
			std::string m_SettingsData = std::string("first\0second\0", sizeof("first\0second\0") - 1);
		};
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;
		int m_SettingsDataIndex = -1;

		void SetUp() override
		{
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}
		void WriteMap(const SItems &Items)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "editor_info.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

			m_SettingsDataIndex = Writer.AddData(Items.m_SettingsData.size(), Items.m_SettingsData.data());
			CMapItemInfoSettings Info{};
			Info.m_Version = Items.m_InfoVersion;
			Info.m_Author = Writer.AddDataString("Author");
			Info.m_MapVersion = Writer.AddDataString("v1");
			Info.m_Credits = Writer.AddDataString("Credits");
			Info.m_License = Writer.AddDataString("License");
			Info.m_Settings = Items.m_SettingsIndex.value_or(m_SettingsDataIndex);
			if(Items.m_HasInfo)
				Writer.AddItem(MAPITEMTYPE_INFO, Items.m_InfoId, Items.m_InfoSize, &Info);

			const std::array<CTile, 4> aTiles{};
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = -1;
			Game.m_Image = -1;
			Game.m_Data = Writer.AddData(aTiles.size() * sizeof(CTile), aTiles.data());
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Game), &Game);
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 1;
			StrToInts(Group.m_aName, std::size(Group.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
			Writer.Finish();
		}
	};
}

TEST_F(EditorMapInfo, UnloadedCandidateCannotPrepareInfo)
{
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, MissingInfoKeepsLegacyEmptyInfoAndSettings)
{
	SItems Items;
	Items.m_HasInfo = false;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	EXPECT_FALSE(Info->m_Info);
	EXPECT_TRUE(Info->m_vSettings.empty());
}

TEST_F(EditorMapInfo, UnusedInfoIdIsIgnoredEvenWithShortPayload)
{
	SItems Items;
	Items.m_InfoId = 7;
	Items.m_InfoSize = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	EXPECT_FALSE(Info->m_Info);
	EXPECT_TRUE(Info->m_vSettings.empty());
}

TEST_F(EditorMapInfo, LegacyBaseWithoutSettingsTailRemainsAccepted)
{
	SItems Items;
	Items.m_InfoSize = sizeof(CMapItemInfo);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_TRUE(Info->m_Info);
	EXPECT_STREQ(m_pMap->GetDataString(Info->m_Info->m_Author), "Author");
	EXPECT_TRUE(Info->m_vSettings.empty());
}

TEST_F(EditorMapInfo, BaseFieldIndicesRetainTheirOriginalMeaning)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_TRUE(Info->m_Info);
	EXPECT_STREQ(m_pMap->GetDataString(Info->m_Info->m_Author), "Author");
	EXPECT_STREQ(m_pMap->GetDataString(Info->m_Info->m_MapVersion), "v1");
	EXPECT_STREQ(m_pMap->GetDataString(Info->m_Info->m_Credits), "Credits");
	EXPECT_STREQ(m_pMap->GetDataString(Info->m_Info->m_License), "License");
}

TEST_F(EditorMapInfo, OtherInfoVersionDoesNotDecodeItsSettingsTail)
{
	SItems Items;
	Items.m_InfoVersion = 2;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	int Reads = 0;
	m_pMap->GetReader()->AddDataProcessor(m_SettingsDataIndex, [&](void *pData, size_t) {
		++Reads;
		std::free(pData);
		return std::pair<void *, size_t>{nullptr, 0};
	});
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_TRUE(Info->m_Info);
	EXPECT_EQ(Info->m_Info->m_Version, 2);
	EXPECT_TRUE(Info->m_vSettings.empty());
	EXPECT_EQ(Reads, 0);
}

TEST_F(EditorMapInfo, AbsentSettingsIndexKeepsAnEmptyList)
{
	SItems Items;
	Items.m_SettingsIndex = -1;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	EXPECT_TRUE(Info->m_vSettings.empty());
}

TEST_F(EditorMapInfo, OtherNegativeSettingsSentinelKeepsLegacySkipBehavior)
{
	SItems Items;
	Items.m_SettingsIndex = -17;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	EXPECT_TRUE(Info->m_vSettings.empty());
}

TEST_F(EditorMapInfo, ShortInfoBaseIsRejectedBeforeReadingFieldIndices)
{
	SItems Items;
	Items.m_InfoSize = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, MissingLastInfoFieldIsRejected)
{
	SItems Items;
	Items.m_InfoSize = sizeof(CMapItemInfo) - sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, InvalidSettingsIndexIsRejected)
{
	SItems Items;
	Items.m_SettingsIndex = INT_MAX;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, SettingsKeepCommandOrderIncludingEmptyEntries)
{
	SItems Items;
	Items.m_SettingsData = std::string("one\0\0two\0\0", sizeof("one\0\0two\0\0") - 1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_EQ(Info->m_vSettings.size(), 4u);
	EXPECT_STREQ(Info->m_vSettings[0].m_aCommand, "one");
	EXPECT_STREQ(Info->m_vSettings[1].m_aCommand, "");
	EXPECT_STREQ(Info->m_vSettings[2].m_aCommand, "two");
	EXPECT_STREQ(Info->m_vSettings[3].m_aCommand, "");
}

TEST_F(EditorMapInfo, TerminatorAtExactDataEndIsAccepted)
{
	SItems Items;
	Items.m_SettingsData = std::string("x\0", 2);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_EQ(Info->m_vSettings.size(), 1u);
	EXPECT_STREQ(Info->m_vSettings[0].m_aCommand, "x");
}

TEST_F(EditorMapInfo, UnterminatedFirstCommandIsRejected)
{
	SItems Items;
	Items.m_SettingsData = "missing";
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, UnterminatedLastCommandDoesNotPublishPartialSettings)
{
	SItems Items;
	Items.m_SettingsData = std::string("first\0last", sizeof("first\0last") - 1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, NullDataResultIsRejectedWithoutDereferencing)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	ASSERT_GT(m_pMap->GetDataSize(m_SettingsDataIndex), 0);
	m_pMap->GetReader()->AddDataProcessor(m_SettingsDataIndex, [](void *pData, size_t) {
		std::free(pData);
		return std::pair<void *, size_t>{nullptr, 0};
	});
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, RepeatedPreparationKeepsFailedDecoderRejected)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	int Reads = 0;
	m_pMap->GetReader()->AddDataProcessor(m_SettingsDataIndex, [&](void *pData, size_t) {
		++Reads;
		std::free(pData);
		return std::pair<void *, size_t>{nullptr, 0};
	});
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
	EXPECT_EQ(Reads, 1);
}

TEST_F(EditorMapInfo, ProcessedDataUsesItsCurrentSizeInsteadOfTheDeclaredSize)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	m_pMap->GetReader()->AddDataProcessor(m_SettingsDataIndex, [](void *pData, size_t) {
		return std::pair<void *, size_t>{pData, sizeof("first")};
	});
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_EQ(Info->m_vSettings.size(), 1u);
	EXPECT_STREQ(Info->m_vSettings[0].m_aCommand, "first");
}

TEST_F(EditorMapInfo, ProcessedSizeCannotBorrowATerminatorFromTheOldTail)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	m_pMap->GetReader()->AddDataProcessor(m_SettingsDataIndex, [](void *pData, size_t) {
		return std::pair<void *, size_t>{pData, sizeof("first") - 1};
	});
	EXPECT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
}

TEST_F(EditorMapInfo, ProcessedEmptyDataKeepsAnEmptySettingsList)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	m_pMap->GetReader()->AddDataProcessor(m_SettingsDataIndex, [](void *pData, size_t) {
		return std::pair<void *, size_t>{pData, 0};
	});
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	EXPECT_TRUE(Info->m_vSettings.empty());
}

TEST_F(EditorMapInfo, PreparedValuesRemainOwnedAfterCandidateUnload)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	m_pMap->Unload();
	ASSERT_TRUE(Info->m_Info);
	EXPECT_EQ(Info->m_Info->m_Version, 1);
	ASSERT_EQ(Info->m_vSettings.size(), 2u);
	EXPECT_STREQ(Info->m_vSettings[0].m_aCommand, "first");
	EXPECT_STREQ(Info->m_vSettings[1].m_aCommand, "second");
}

TEST_F(EditorMapInfo, LongCommandUsesExistingLimitAndStillAdvancesToNextCommand)
{
	SItems Items;
	Items.m_SettingsData = std::string(300, 'x') + '\0' + std::string("next\0", sizeof("next\0") - 1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_EQ(Info->m_vSettings.size(), 2u);
	EXPECT_EQ(str_length(Info->m_vSettings[0].m_aCommand), 255);
	EXPECT_STREQ(Info->m_vSettings[1].m_aCommand, "next");
}

TEST_F(EditorMapInfo, CorrectedSettingsCanBePreparedAfterRejectedCandidate)
{
	SItems Items;
	Items.m_SettingsData = "missing";
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	ASSERT_FALSE(EditorMapLoad::ReadInfoItems(*m_pMap));
	m_pMap->Unload();
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	const auto Info = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(Info);
	ASSERT_EQ(Info->m_vSettings.size(), 2u);
	EXPECT_STREQ(Info->m_vSettings[1].m_aCommand, "second");
}

TEST_F(EditorMapInfo, RepeatedPreparationProducesIndependentCommandValues)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_info.map", IStorage::TYPE_SAVE));
	auto First = EditorMapLoad::ReadInfoItems(*m_pMap);
	const auto Second = EditorMapLoad::ReadInfoItems(*m_pMap);
	ASSERT_TRUE(First);
	ASSERT_TRUE(Second);
	ASSERT_EQ(First->m_vSettings.size(), 2u);
	ASSERT_EQ(Second->m_vSettings.size(), 2u);
	str_copy(First->m_vSettings[0].m_aCommand, "edited");
	EXPECT_STREQ(Second->m_vSettings[0].m_aCommand, "first");
}
