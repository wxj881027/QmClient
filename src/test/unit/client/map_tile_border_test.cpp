#include <game/map/render_map.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <vector>

namespace
{
	// 只记录设备收到的四边形和纹理坐标，边界延伸仍由生产渲染器执行。
	class CMapTileGraphics : public CIconBenchmarkGraphics
	{
		bool m_UseTextureArrays;
		vec2 m_ScreenTL = vec2(-64.0f, -64.0f);
		vec2 m_ScreenBR = vec2(160.0f, 160.0f);
		std::array<vec2, 4> m_aTextureCoords{};
		int m_TextureIndex = -1;

	public:
		struct SDraw
		{
			CQuadItem m_Quad;
			std::array<vec2, 4> m_aTextureCoords;
			int m_TextureIndex;
			bool m_UsesTextureArray;
		};

		std::vector<SDraw> m_vDraws;
		int m_ArrayBegins = 0;
		int m_ArrayEnds = 0;
		int m_AtlasBegins = 0;
		int m_AtlasEnds = 0;

		explicit CMapTileGraphics(bool UseTextureArrays) :
			m_UseTextureArrays(UseTextureArrays) {}

		bool HasTextureArraysSupport() const override { return m_UseTextureArrays; }
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = m_ScreenTL.x;
			*pY0 = m_ScreenTL.y;
			*pX1 = m_ScreenBR.x;
			*pY1 = m_ScreenBR.y;
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			m_ScreenTL = vec2(X0, Y0);
			m_ScreenBR = vec2(X1, Y1);
		}
		void QuadsBegin() override { ++m_AtlasBegins; }
		void QuadsEnd() override { ++m_AtlasEnds; }
		void QuadsTex3DBegin() override { ++m_ArrayBegins; }
		void QuadsTex3DEnd() override { ++m_ArrayEnds; }
		void QuadsSetSubsetFree(float X0, float Y0, float X1, float Y1, float X2, float Y2, float X3, float Y3, int Index = -1) override
		{
			m_aTextureCoords = {vec2(X0, Y0), vec2(X1, Y1), vec2(X2, Y2), vec2(X3, Y3)};
			m_TextureIndex = Index;
		}
		// 引擎接口新增的纹理目标标志：测试桩不加载真实纹理，统一返回 0（不启用数组/3D 目标）。
		int TextureLoadFlags() override { return 0; }
		void QuadsDrawTL(const CQuadItem *pArray, int Num) override { Record(pArray, Num, false); }
		void QuadsTex3DDrawTL(const CQuadItem *pArray, int Num) override { Record(pArray, Num, true); }

		const SDraw *FindDraw(float X, float Y) const
		{
			for(const SDraw &Draw : m_vDraws)
			{
				if(Draw.m_Quad.m_X == X && Draw.m_Quad.m_Y == Y)
					return &Draw;
			}
			return nullptr;
		}

	private:
		void Record(const CQuadItem *pArray, int Num, bool UsesTextureArray)
		{
			for(int i = 0; i < Num; ++i)
				m_vDraws.push_back({pArray[i], m_aTextureCoords, m_TextureIndex, UsesTextureArray});
		}
	};

	class CMapTileBorderTest : public ::testing::TestWithParam<bool>
	{
	protected:
		static std::array<CTile, 9> MakeTiles()
		{
			std::array<CTile, 9> aTiles{};
			const std::array<unsigned char, 9> aIndices = {17, 18, 19, 33, 34, 35, 49, 50, 51};
			for(size_t i = 0; i < aTiles.size(); ++i)
			{
				aTiles[i].m_Index = aIndices[i];
				aTiles[i].m_Flags = TILEFLAG_OPAQUE;
			}
			return aTiles;
		}

		void ExpectTexture(const CMapTileGraphics::SDraw &Draw, int Index, int AtlasColumn, int AtlasRow)
		{
			EXPECT_EQ(Draw.m_UsesTextureArray, GetParam());
			if(GetParam())
			{
				EXPECT_EQ(Draw.m_TextureIndex, Index);
			}
			else
			{
				EXPECT_EQ(Draw.m_TextureIndex, -1);
				// 图集坐标应落在预期格内，不依赖生产实现的像素修正公式。
				for(const vec2 Coord : Draw.m_aTextureCoords)
				{
					EXPECT_GT(Coord.x, AtlasColumn / 16.0f);
					EXPECT_LT(Coord.x, (AtlasColumn + 1) / 16.0f);
					EXPECT_GT(Coord.y, AtlasRow / 16.0f);
					EXPECT_LT(Coord.y, (AtlasRow + 1) / 16.0f);
				}
			}
		}

		void ExpectDrawingPath(const CMapTileGraphics &Graphics)
		{
			EXPECT_EQ(Graphics.m_ArrayBegins, GetParam() ? 1 : 0);
			EXPECT_EQ(Graphics.m_ArrayEnds, GetParam() ? 1 : 0);
			EXPECT_EQ(Graphics.m_AtlasBegins, GetParam() ? 0 : 1);
			EXPECT_EQ(Graphics.m_AtlasEnds, GetParam() ? 0 : 1);
		}
	};
}

TEST_P(CMapTileBorderTest, ExtendDrawsAllSidesAndCornersUsingNearestEdgeTile)
{
	CMapTileGraphics Graphics(GetParam());
	CRenderMap RenderMap;
	RenderMap.Init(&Graphics, nullptr);
	auto aTiles = MakeTiles();

	RenderMap.RenderTilemap(aTiles.data(), 3, 3, 32.0f, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_OPAQUE | TILERENDERFLAG_EXTEND);

	struct SExpectedTile
	{
		float m_X;
		float m_Y;
		int m_Index;
		int m_AtlasColumn;
		int m_AtlasRow;
	};
	const std::array<SExpectedTile, 8> aExpected = {{
		{-64, -64, 17, 1, 1},
		{128, -64, 19, 3, 1},
		{-64, 128, 49, 1, 3},
		{128, 128, 51, 3, 3},
		{-64, 32, 33, 1, 2},
		{128, 32, 35, 3, 2},
		{32, -64, 18, 2, 1},
		{32, 128, 50, 2, 3},
	}};
	for(const SExpectedTile &Expected : aExpected)
	{
		SCOPED_TRACE(::testing::Message() << "quad at " << Expected.m_X << ", " << Expected.m_Y);
		const auto *pDraw = Graphics.FindDraw(Expected.m_X, Expected.m_Y);
		ASSERT_NE(pDraw, nullptr);
		EXPECT_FLOAT_EQ(pDraw->m_Quad.m_Width, 32.0f);
		EXPECT_FLOAT_EQ(pDraw->m_Quad.m_Height, 32.0f);
		ExpectTexture(*pDraw, Expected.m_Index, Expected.m_AtlasColumn, Expected.m_AtlasRow);
	}
	ExpectDrawingPath(Graphics);
}

TEST_P(CMapTileBorderTest, WithoutExtendDrawsOnlyTilesInsideMap)
{
	CMapTileGraphics Graphics(GetParam());
	CRenderMap RenderMap;
	RenderMap.Init(&Graphics, nullptr);
	auto aTiles = MakeTiles();

	RenderMap.RenderTilemap(aTiles.data(), 3, 3, 32.0f, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_OPAQUE);

	ASSERT_EQ(Graphics.m_vDraws.size(), 9U);
	for(const auto &Draw : Graphics.m_vDraws)
	{
		EXPECT_GE(Draw.m_Quad.m_X, 0.0f);
		EXPECT_GE(Draw.m_Quad.m_Y, 0.0f);
		EXPECT_LE(Draw.m_Quad.m_X + Draw.m_Quad.m_Width, 96.0f);
		EXPECT_LE(Draw.m_Quad.m_Y + Draw.m_Quad.m_Height, 96.0f);
	}
	const auto *pCenter = Graphics.FindDraw(32.0f, 32.0f);
	ASSERT_NE(pCenter, nullptr);
	ExpectTexture(*pCenter, 34, 2, 2);
	ExpectDrawingPath(Graphics);
}

INSTANTIATE_TEST_SUITE_P(TextureFormats, CMapTileBorderTest, ::testing::Bool(), [](const auto &Info) {
	return Info.param ? "TextureArrays" : "LegacyAtlas";
});
