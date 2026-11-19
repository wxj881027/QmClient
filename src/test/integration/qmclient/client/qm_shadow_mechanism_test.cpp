#include <engine/map.h>

#include <game/client/components/qmclient/shadow_gun_replay.h>
#include <game/client/laser_data.h>
#include <game/client/prediction/entities/door.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{
	class CMechanismTestMap final : public IMap
	{
		struct SItem
		{
			int m_Type;
			int m_Id;
			std::vector<int> m_vWords;
		};
		std::vector<SItem> m_vItems;
		std::vector<std::vector<int>> m_vData;

	public:
		template<typename T>
		void AddItem(int Type, int Id, const T &Item)
		{
			std::vector<int> vWords(sizeof(T) / sizeof(int));
			std::memcpy(vWords.data(), &Item, sizeof(T));
			m_vItems.push_back({Type, Id, std::move(vWords)});
		}
		template<typename T>
		int AddData(const std::vector<T> &vData)
		{
			std::vector<int> vWords(vData.size() * sizeof(T) / sizeof(int));
			if(!vData.empty())
				std::memcpy(vWords.data(), vData.data(), vData.size() * sizeof(T));
			m_vData.push_back(std::move(vWords));
			return static_cast<int>(m_vData.size()) - 1;
		}
		int GetDataSize(int Index) const override { return static_cast<int>(m_vData.at(Index).size() * sizeof(int)); }
		void *GetData(int Index) override { return m_vData.at(Index).data(); }
		void *GetDataSwapped(int Index) override { return GetData(Index); }
		const char *GetDataString(int) override { return ""; }
		void UnloadData(int) override {}
		int NumData() const override { return static_cast<int>(m_vData.size()); }
		int GetItemSize(int Index) override { return static_cast<int>(m_vItems.at(Index).m_vWords.size() * sizeof(int)); }
		void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr) override
		{
			auto &Item = m_vItems.at(Index);
			if(pType)
				*pType = Item.m_Type;
			if(pId)
				*pId = Item.m_Id;
			return Item.m_vWords.data();
		}
		void GetType(int Type, int *pStart, int *pNum) override
		{
			*pStart = *pNum = 0;
			for(size_t i = 0; i < m_vItems.size(); ++i)
			{
				if(m_vItems[i].m_Type != Type)
					continue;
				if(*pNum == 0)
					*pStart = static_cast<int>(i);
				++*pNum;
			}
		}
		int FindItemIndex(int Type, int Id) override
		{
			for(size_t i = 0; i < m_vItems.size(); ++i)
				if(m_vItems[i].m_Type == Type && m_vItems[i].m_Id == Id)
					return static_cast<int>(i);
			return -1;
		}
		void *FindItem(int Type, int Id) override
		{
			const int Index = FindItemIndex(Type, Id);
			return Index >= 0 ? GetItem(Index) : nullptr;
		}
		int NumItems() const override { return static_cast<int>(m_vItems.size()); }
	};

	struct SMechanismFixture
	{
		CMechanismTestMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;
		std::vector<CTile> m_vGame = std::vector<CTile>(32 * 32);
		std::vector<CSwitchTile> m_vSwitch = std::vector<CSwitchTile>(32 * 32);
		bool m_WithSwitch = false;

		void Init()
		{
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_NumLayers = m_WithSwitch ? 2 : 1;
			m_Map.AddItem(MAPITEMTYPE_GROUP, 0, Group);

			CMapItemLayerTilemap Game{};
			Game.m_Version = 3;
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Width = Game.m_Height = 32;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Data = m_Map.AddData(m_vGame);
			m_Map.AddItem(MAPITEMTYPE_LAYER, 0, Game);

			if(m_WithSwitch)
			{
				CMapItemLayerTilemap Switch{};
				Switch.m_Version = 3;
				Switch.m_Layer.m_Type = LAYERTYPE_TILES;
				Switch.m_Width = Switch.m_Height = 32;
				Switch.m_Flags = TILESLAYERFLAG_SWITCH;
				Switch.m_Switch = m_Map.AddData(m_vSwitch);
				m_Map.AddItem(MAPITEMTYPE_LAYER, 1, Switch);
			}
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
		}

		void SetGameEntity(int X, int Y, int Entity)
		{
			m_vGame[Y * 32 + X].m_Index = ENTITY_OFFSET + Entity;
		}
		void SetSwitchEntity(int X, int Y, int Entity, int Number)
		{
			CSwitchTile &Tile = m_vSwitch[Y * 32 + X];
			Tile.m_Type = ENTITY_OFFSET + Entity;
			Tile.m_Number = Number;
		}
	};
}

TEST(ShadowMechanism, DoorCollisionOwnershipStaysWithPlayableWorld)
{
	SMechanismFixture F;
	// 正式地图的门碰撞缓冲区由 switch 图层存在与否统一分配；空 switch 图层等价于
	// 全图没有开关的 DDRace 地图。
	F.m_WithSwitch = true;
	F.SetGameEntity(2, 2, ENTITY_DOOR);
	F.SetGameEntity(2, 3, ENTITY_LASER_SHORT);
	F.Init();

	CGameWorld World;
	CTuningParams Tuning;
	World.Init(&F.m_Collision, &Tuning, nullptr);
	CLaserData Data = {};
	Data.m_To = vec2(2 * 32 + 16, 2 * 32 + 16);
	Data.m_From = Data.m_To + vec2(0, 96);
	Data.m_Type = LASERTYPE_DOOR;
	auto *pDoor = new CDoor(&World, 1, &Data);
	pDoor->PrepareRenderGeometry();
	EXPECT_TRUE(pDoor->Active());
	EXPECT_EQ(pDoor->Length(), 96);
	const int DoorIndex = F.m_Collision.GetPureMapIndex(Data.m_To);
	CDoorTile DoorTile;
	F.m_Collision.GetDoorTile(DoorIndex, &DoorTile);
	EXPECT_EQ(DoorTile.m_Index, 0);

	pDoor->ResetCollision();
	F.m_Collision.GetDoorTile(DoorIndex, &DoorTile);
	EXPECT_EQ(DoorTile.m_Index, TILE_STOPA);
	auto *pCopy = new CDoor(*pDoor);
	pCopy->Destroy();
	F.m_Collision.GetDoorTile(DoorIndex, &DoorTile);
	EXPECT_EQ(DoorTile.m_Index, TILE_STOPA);

	pDoor->Destroy();
	F.m_Collision.GetDoorTile(DoorIndex, &DoorTile);
	EXPECT_EQ(DoorTile.m_Index, 0);
}

TEST(ShadowMechanism, GunReplayFiresAtShadowTargetWithoutCharacters)
{
	SMechanismFixture F;
	F.SetGameEntity(2, 2, ENTITY_PLASMA);
	F.Init();

	std::vector<qm_shadow_gun::SGun> vGuns;
	qm_shadow_gun::Load(&F.m_Collision, vGuns);
	ASSERT_EQ(vGuns.size(), 1u);
	std::vector<qm_shadow_gun::SPlasma> vPlasmas;
	qm_shadow_gun::Reset(vGuns, vPlasmas);

	const auto Targets = [](int, std::vector<qm_shadow_gun::STarget> &vTargets) {
		vTargets.push_back({vec2(12 * 32 + 16, 2 * 32 + 16), 0, TEAM_FLOCK});
	};
	const std::vector<SSwitchers> vSwitchers;
	qm_shadow_gun::Tick(vGuns, vPlasmas, 0, vSwitchers, TEAM_FLOCK, &F.m_Collision, Targets);
	EXPECT_TRUE(vPlasmas.empty());
	qm_shadow_gun::Tick(vGuns, vPlasmas, 50, vSwitchers, TEAM_FLOCK, &F.m_Collision, Targets);
	ASSERT_EQ(vPlasmas.size(), 1u);
	const vec2 Start = vPlasmas[0].m_Pos;
	qm_shadow_gun::Tick(vGuns, vPlasmas, 51, vSwitchers, TEAM_FLOCK, &F.m_Collision, Targets);
	ASSERT_EQ(vPlasmas.size(), 1u);
	EXPECT_GT(vPlasmas[0].m_Pos.x, Start.x);
}

TEST(ShadowMechanism, DisabledSwitchLayerGunStaysSilent)
{
	SMechanismFixture F;
	F.m_WithSwitch = true;
	F.SetSwitchEntity(2, 2, ENTITY_PLASMA, 1);
	F.Init();

	std::vector<qm_shadow_gun::SGun> vGuns;
	qm_shadow_gun::Load(&F.m_Collision, vGuns);
	ASSERT_EQ(vGuns.size(), 1u);
	std::vector<qm_shadow_gun::SPlasma> vPlasmas;
	qm_shadow_gun::Reset(vGuns, vPlasmas);
	std::vector<SSwitchers> vSwitchers(2);
	const auto Targets = [](int, std::vector<qm_shadow_gun::STarget> &vTargets) {
		vTargets.push_back({vec2(12 * 32 + 16, 2 * 32 + 16), 0, TEAM_FLOCK});
	};
	qm_shadow_gun::Tick(vGuns, vPlasmas, 0, vSwitchers, TEAM_FLOCK, &F.m_Collision, Targets);
	EXPECT_TRUE(vPlasmas.empty());
}
