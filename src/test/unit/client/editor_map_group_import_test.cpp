#include <engine/shared/map.h>

#include <game/editor/mapitems/load_resources.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

namespace
{
	// 在已解析条目边界验证编辑器预检，不需要创建地图文件。
	class CParsedGroups : public CMap
	{
	public:
		struct SGroup
		{
			int m_Version;
			std::vector<int> m_vTileLayerFlags;
		};

		explicit CParsedGroups(std::initializer_list<SGroup> Groups)
		{
			for(const SGroup &Spec : Groups)
			{
				auto &Group = m_vGroups.emplace_back();
				Group.m_Version = Spec.m_Version;
				Group.m_StartLayer = static_cast<int>(m_vLayers.size());
				Group.m_NumLayers = static_cast<int>(Spec.m_vTileLayerFlags.size());
				for(int Flags : Spec.m_vTileLayerFlags)
				{
					auto &Layer = m_vLayers.emplace_back();
					Layer.m_Layer.m_Type = LAYERTYPE_TILES;
					Layer.m_Flags = Flags;
				}
			}
		}

		bool IsLoaded() const override { return true; }

		int NumItems() const override { return static_cast<int>(m_vGroups.size() + m_vLayers.size()); }

		void GetType(int Type, int *pStart, int *pNum) override
		{
			*pStart = 0;
			*pNum = 0;
			if(Type == MAPITEMTYPE_GROUP)
				*pNum = static_cast<int>(m_vGroups.size());
			else if(Type == MAPITEMTYPE_LAYER)
			{
				*pStart = static_cast<int>(m_vGroups.size());
				*pNum = static_cast<int>(m_vLayers.size());
			}
		}

		int GetItemSize(int Index) override
		{
			if(Index < 0 || Index >= NumItems())
				return 0;
			return static_cast<int>(Index < static_cast<int>(m_vGroups.size()) ? sizeof(CMapItemGroup) : sizeof(CMapItemLayerTilemap));
		}

		void *GetItem(int Index, int *pType, int *pId) override
		{
			if(Index < 0 || Index >= NumItems())
				return nullptr;
			const int GroupCount = static_cast<int>(m_vGroups.size());
			const bool IsGroup = Index < GroupCount;
			const int Id = IsGroup ? Index : Index - GroupCount;
			if(pType)
				*pType = IsGroup ? MAPITEMTYPE_GROUP : MAPITEMTYPE_LAYER;
			if(pId)
				*pId = Id;
			if(IsGroup)
				return &m_vGroups[Id];
			return &m_vLayers[Id];
		}

	private:
		std::vector<CMapItemGroup> m_vGroups;
		std::vector<CMapItemLayerTilemap> m_vLayers;
	};
}

TEST(EditorMapGroupImport, SkippedGroupCannotSupplyTheGameLayer)
{
	CParsedGroups Map{{3, {0}}, {4, {TILESLAYERFLAG_GAME}}};
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(Map).has_value());
}

TEST(EditorMapGroupImport, ActivePhysicsLayerMustBelongToTheActiveGameGroup)
{
	CParsedGroups Map{{3, {TILESLAYERFLAG_GAME}}, {3, {TILESLAYERFLAG_TELE}}};
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(Map).has_value());
}

TEST(EditorMapGroupImport, SkippedNonGameGroupDoesNotPreventImport)
{
	CParsedGroups Map{{1, {TILESLAYERFLAG_GAME, TILESLAYERFLAG_SWITCH}}, {4, {TILESLAYERFLAG_TELE}}};
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(Map).has_value());
}

TEST(EditorMapGroupImport, LastImportedInstanceOfEachPhysicsLayerSelectsItsGroup)
{
	CParsedGroups Map{{3, {TILESLAYERFLAG_GAME, TILESLAYERFLAG_TELE}}, {3, {TILESLAYERFLAG_GAME, TILESLAYERFLAG_TELE}}};
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(Map).has_value());
}
