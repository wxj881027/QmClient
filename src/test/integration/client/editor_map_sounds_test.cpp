#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/editor/mapitems/legacy_sound_source.h>
#include <game/editor/mapitems/load_resources.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace
{
	class EditorMapSounds : public ::testing::Test
	{
	protected:
		struct SSound
		{
			int m_Version = 1;
			size_t m_Size = sizeof(CMapItemSound);
			int m_External = 0;
		};
		struct SItems
		{
			std::vector<SSound> m_vSounds{{}};
			int m_LayerKind = LAYERTYPE_SOUNDS;
			int m_LayerVersion = 2;
			size_t m_LayerSize = sizeof(CMapItemLayerSounds);
			int m_NumSources = 1;
			bool m_InvalidData = false;
			bool m_Referenced = true;
			int m_GroupVersion = 3;
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}
		// 使用真实存储和共享地图加载，只改变声音条目及来源数组。
		void WriteMap(const SItems &Items)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "editor_sounds.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			for(size_t i = 0; i < Items.m_vSounds.size(); ++i)
			{
				const auto &Source = Items.m_vSounds[i];
				CMapItemSound Sound{};
				Sound.m_Version = Source.m_Version;
				Sound.m_External = Source.m_External;
				char aName[32];
				str_format(aName, sizeof(aName), "sound%d", static_cast<int>(i));
				Sound.m_SoundName = Writer.AddDataString(aName);
				const std::array<uint8_t, 4> aBytes{12, 34, 56, 78};
				Sound.m_SoundData = Source.m_External ? -1 : Writer.AddData(aBytes.size(), aBytes.data());
				Sound.m_SoundDataSize = INT_MAX;
				Writer.AddItem(MAPITEMTYPE_SOUND, static_cast<int>(i), Source.m_Size, &Sound);
			}

			CMapItemLayerSounds Layer{};
			Layer.m_Layer.m_Type = Items.m_LayerKind;
			Layer.m_Version = Items.m_LayerVersion;
			Layer.m_NumSources = Items.m_NumSources;
			Layer.m_Sound = Items.m_vSounds.empty() ? -1 : static_cast<int>(Items.m_vSounds.size()) - 1;
			if(Items.m_LayerKind == LAYERTYPE_SOUNDS_DEPRECATED)
			{
				const CSoundSourceDeprecated Source{{123, -456}, 1, 7, 900, 2, 30, 4, -50};
				Layer.m_Data = Writer.AddDataSwapped(sizeof(Source), &Source);
			}
			else
			{
				CSoundSource Source{};
				Source.m_Position.x = 123;
				Layer.m_Data = Writer.AddDataSwapped(sizeof(Source), &Source);
			}
			if(Items.m_InvalidData)
				Layer.m_Data = INT_MAX;
			StrToInts(Layer.m_aName, std::size(Layer.m_aName), "Named");
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, Items.m_LayerSize, &Layer);

			const std::array<CTile, 4> aTiles{};
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = Game.m_Image = -1;
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Game.m_Data = Writer.AddData(sizeof(aTiles), aTiles.data());
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			Writer.AddItem(MAPITEMTYPE_LAYER, 1, sizeof(Game), &Game);

			CMapItemGroup Decoration{};
			Decoration.m_Version = Items.m_GroupVersion;
			Decoration.m_ParallaxX = Decoration.m_ParallaxY = 100;
			Decoration.m_NumLayers = Items.m_Referenced ? 1 : 0;
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Decoration), &Decoration);
			CMapItemGroup GameGroup{};
			GameGroup.m_Version = 3;
			GameGroup.m_ParallaxX = GameGroup.m_ParallaxY = 100;
			GameGroup.m_StartLayer = 1;
			GameGroup.m_NumLayers = 1;
			Writer.AddItem(MAPITEMTYPE_GROUP, 1, sizeof(GameGroup), &GameGroup);
			Writer.Finish();
		}
		std::optional<CMapItemLayerSounds> ReadLayer()
		{
			return MapRenderItems::ReadSoundLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
		}
	};
}

TEST_F(EditorMapSounds, MapWithoutSoundsStillPreparesResources)
{
	SItems Items;
	Items.m_vSounds.clear();
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	EXPECT_TRUE(Resources->m_vSounds.empty());
}

TEST_F(EditorMapSounds, EmbeddedMetadataUsesActualDataRatherThanDeprecatedLength)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vSounds.size(), 1u);
	const auto &Sound = Resources->m_vSounds[0];
	EXPECT_EQ(Sound.m_SoundDataSize, 0);
	ASSERT_NE(m_pMap->GetData(Sound.m_SoundData), nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(Sound.m_SoundData), 4);
	EXPECT_STREQ(m_pMap->GetDataString(Sound.m_SoundName), "sound0");
}

TEST_F(EditorMapSounds, ExternalMetadataDoesNotRequireEmbeddedData)
{
	SItems Items;
	Items.m_vSounds[0].m_External = 1;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vSounds.size(), 1u);
	EXPECT_EQ(Resources->m_vSounds[0].m_External, 1);
	EXPECT_EQ(Resources->m_vSounds[0].m_SoundData, -1);
}

TEST_F(EditorMapSounds, ConsumedAssetPrefixRemainsAcceptedWithoutDeprecatedTail)
{
	SItems Items;
	Items.m_vSounds[0].m_Size = offsetof(CMapItemSound, m_SoundDataSize);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, UnknownAssetVersionRetainsExistingCompatibility)
{
	SItems Items;
	Items.m_vSounds[0].m_Version = 7;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vSounds.size(), 1u);
	EXPECT_EQ(Resources->m_vSounds[0].m_Version, 7);
}

TEST_F(EditorMapSounds, ShortSoundHeaderIsRejectedAfterSharedMapAcceptsCandidate)
{
	SItems Items;
	Items.m_vSounds[0].m_Size = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, LaterShortSoundCannotPublishPartialResourceItems)
{
	SItems Items;
	Items.m_vSounds.push_back({});
	Items.m_vSounds[1].m_Size = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, SoundMetadataAndLayerIndicesKeepTheirOriginalOrder)
{
	SItems Items;
	Items.m_vSounds.push_back({});
	Items.m_vSounds[0].m_External = 1;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vSounds.size(), 2u);
	EXPECT_EQ(Resources->m_vSounds[0].m_External, 1);
	EXPECT_STREQ(m_pMap->GetDataString(Resources->m_vSounds[1].m_SoundName), "sound1");
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	EXPECT_EQ(Layer->m_Sound, 1);
}

TEST_F(EditorMapSounds, PreparedMetadataRemainsOwnedAfterCandidateUnload)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	m_pMap->Unload();
	ASSERT_EQ(Resources->m_vSounds.size(), 1u);
	EXPECT_EQ(Resources->m_vSounds[0].m_Version, 1);
	EXPECT_EQ(Resources->m_vSounds[0].m_External, 0);
}

TEST_F(EditorMapSounds, CorrectedSoundCanBePreparedAfterRejectedCandidate)
{
	SItems Items;
	Items.m_vSounds[0].m_Size = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	ASSERT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
	m_pMap->Unload();
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, ModernLayerReadsItsNameAndSwappedSources)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	char aName[12];
	IntsToStr(Layer->m_aName, std::size(Layer->m_aName), aName, sizeof(aName));
	EXPECT_STREQ(aName, "Named");
	const auto Sources = MapRenderItems::ReadDataItems<CSoundSource>(*m_pMap, Layer->m_Data, Layer->m_NumSources);
	ASSERT_EQ(Sources.size(), 1u);
	EXPECT_EQ(Sources[0].m_Position.x, 123);
}

TEST_F(EditorMapSounds, SupportedLayersRequireTheWholeNameBeforeEditorImport)
{
	for(int Kind : {LAYERTYPE_SOUNDS, LAYERTYPE_SOUNDS_DEPRECATED})
		for(int Version : {1, 2})
			for(size_t Size : {offsetof(CMapItemLayerSounds, m_aName), sizeof(CMapItemLayerSounds) - sizeof(int)})
			{
				SCOPED_TRACE(::testing::Message() << Kind << ":" << Version << ":" << Size);
				SItems Items;
				Items.m_LayerKind = Kind;
				Items.m_LayerVersion = Version;
				Items.m_LayerSize = Size;
				WriteMap(Items);
				ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
				EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
				m_pMap->Unload();
			}
}

TEST_F(EditorMapSounds, LayerWithoutVersionIsRejectedBeforeImport)
{
	SItems Items;
	Items.m_LayerSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, UnknownLayerVersionRetainsSkipBehaviorWithOnlyVersionPrefix)
{
	SItems Items;
	Items.m_LayerVersion = 3;
	Items.m_LayerSize = offsetof(CMapItemLayerSounds, m_NumSources);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, UnreferencedShortLayerIsNotImportedOrRejected)
{
	SItems Items;
	Items.m_Referenced = false;
	Items.m_LayerSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, ShortLayerInUnsupportedGroupKeepsExistingSkipBehavior)
{
	SItems Items;
	Items.m_GroupVersion = 4;
	Items.m_LayerSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapSounds, MissingDataKeepsRecoverableEmptySourceResult)
{
	SItems Items;
	Items.m_InvalidData = true;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(*m_pMap, Layer->m_Data, Layer->m_NumSources).empty());
}

TEST_F(EditorMapSounds, EditorRequiresAllSourcesWhileClientKeepsAvailablePrefix)
{
	SItems Items;
	Items.m_NumSources = 2;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(*m_pMap, Layer->m_Data, Layer->m_NumSources).empty());
	const auto Sources = MapRenderItems::ReadDataItems<CSoundSource>(*m_pMap, Layer->m_Data, Layer->m_NumSources, MapRenderItems::EDataCountPolicy::ALLOW_PARTIAL);
	ASSERT_EQ(Sources.size(), 1u);
	EXPECT_EQ(Sources[0].m_Position.x, 123);
}

TEST_F(EditorMapSounds, WrappingModernSourceCountCannotExposeDataForCopy)
{
	SItems Items;
	Items.m_NumSources = static_cast<int>(std::numeric_limits<uint32_t>::max() / sizeof(CSoundSource) + 1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(*m_pMap, Layer->m_Data, Layer->m_NumSources).empty());
}

TEST_F(EditorMapSounds, WrappingLegacySourceCountCannotExposeDataForConversion)
{
	SItems Items;
	Items.m_LayerKind = LAYERTYPE_SOUNDS_DEPRECATED;
	Items.m_NumSources = static_cast<int>(std::numeric_limits<uint32_t>::max() / sizeof(CSoundSourceDeprecated) + 1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSourceDeprecated>(*m_pMap, Layer->m_Data, Layer->m_NumSources).empty());
}

TEST_F(EditorMapSounds, LegacySourceConversionPreservesTimingEnvelopesAndCircle)
{
	SItems Items;
	Items.m_LayerKind = LAYERTYPE_SOUNDS_DEPRECATED;
	Items.m_LayerVersion = 1;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_sounds.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
	const auto Layer = ReadLayer();
	ASSERT_TRUE(Layer);
	const auto Sources = MapRenderItems::ReadDataItems<CSoundSourceDeprecated>(*m_pMap, Layer->m_Data, Layer->m_NumSources);
	ASSERT_EQ(Sources.size(), 1u);
	const auto Source = EditorMapLoad::ConvertLegacySoundSource(Sources[0]);
	EXPECT_EQ(Source.m_Position.x, 123);
	EXPECT_EQ(Source.m_Position.y, -456);
	EXPECT_EQ(Source.m_Loop, 1);
	EXPECT_EQ(Source.m_TimeDelay, 7);
	EXPECT_EQ(Source.m_PosEnv, 2);
	EXPECT_EQ(Source.m_PosEnvOffset, 30);
	EXPECT_EQ(Source.m_SoundEnv, 4);
	EXPECT_EQ(Source.m_SoundEnvOffset, -50);
	EXPECT_EQ(Source.m_Pan, 1);
	EXPECT_EQ(Source.m_Falloff, 0);
	EXPECT_EQ(Source.m_Shape.m_Type, CSoundShape::SHAPE_CIRCLE);
	EXPECT_EQ(Source.m_Shape.m_Circle.m_Radius, 900);
}
