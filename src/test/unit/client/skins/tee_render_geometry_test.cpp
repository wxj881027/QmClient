#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

namespace
{
	class CScopedGeometryConfig
	{
		int m_FatSkins = g_Config.m_ClFatSkins;
		int m_Dummy = g_Config.m_ClDummy;

	public:
		CScopedGeometryConfig() { g_Config.m_ClFatSkins = g_Config.m_ClDummy = 0; }
		~CScopedGeometryConfig()
		{
			g_Config.m_ClFatSkins = m_FatSkins;
			g_Config.m_ClDummy = m_Dummy;
		}
		CScopedGeometryConfig(const CScopedGeometryConfig &) = delete;
		CScopedGeometryConfig &operator=(const CScopedGeometryConfig &) = delete;
	};

	CTeeRenderInfo MakeInfo()
	{
		CTeeRenderInfo Info;
		Info.m_Size = 64;
		auto &Body = Info.m_SkinMetrics.m_Body;
		Body.m_Width = 64;
		Body.m_Height = 32;
		Body.m_OffsetX = 16;
		Body.m_OffsetY = 8;
		Body.m_MaxWidth = Body.m_MaxHeight = 128;
		auto &Feet = Info.m_SkinMetrics.m_Feet;
		Feet.m_Width = 24;
		Feet.m_Height = 16;
		Feet.m_OffsetX = 8;
		Feet.m_OffsetY = 4;
		Feet.m_MaxWidth = 64;
		Feet.m_MaxHeight = 32;
		return Info;
	}

	// 只提供纹理句柄，不创建图形设备或替换几何计算。
	class CGeometryTextureFactory : public CIconBenchmarkGraphics
	{
	public:
		CTextureHandle BodyTexture() { return CreateTextureHandle(1, 1); }
	};
}

TEST(TeeRenderGeometry, BaseSizeAndAnimationScaleFollowRequestedSize)
{
	const CScopedGeometryConfig Config;
	const struct
	{
		float m_Size;
		float m_Scale;
	} aCases[] = {{0, 0}, {32, 0.5f}, {64, 1}, {96, 1.5f}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Size);
		auto Info = MakeInfo();
		Info.m_Size = Case.m_Size;
		float Scale = -1, Base = -1;
		CRenderTools::GetRenderTeeAnimScaleAndBaseSize(&Info, Scale, Base);
		EXPECT_FLOAT_EQ(Base, Case.m_Size);
		EXPECT_FLOAT_EQ(Scale, Case.m_Scale);
	}
}

TEST(TeeRenderGeometry, BodyMetricsProduceMeasuredSizeAndOffset)
{
	const CScopedGeometryConfig Config;
	const CAnimState Animation{};
	const auto Info = MakeInfo();
	vec2 Offset;
	float Width, Height;
	CRenderTools::GetRenderTeeBodySize(&Animation, &Info, Offset, Width, Height);
	EXPECT_FLOAT_EQ(Width, 32);
	EXPECT_FLOAT_EQ(Height, 16);
	EXPECT_EQ(Offset, vec2(8, 4));
}

TEST(TeeRenderGeometry, FeetMetricsScaleWithTeeSize)
{
	const CScopedGeometryConfig Config;
	const CAnimState Animation{};
	auto Info = MakeInfo();
	Info.m_Size = 32;
	vec2 Offset;
	float Width, Height;
	CRenderTools::GetRenderTeeFeetSize(&Animation, &Info, Offset, Width, Height);
	EXPECT_FLOAT_EQ(Width, 12);
	EXPECT_FLOAT_EQ(Height, 8);
	EXPECT_EQ(Offset, vec2(4, 2));
}

TEST(TeeRenderGeometry, FatSkinChangesBodyMetricsAndKeepsFeetMetrics)
{
	const CScopedGeometryConfig Config;
	const CAnimState Animation{};
	const auto Info = MakeInfo();
	g_Config.m_ClFatSkins = 1;
	vec2 Offset;
	float Width, Height;
	CRenderTools::GetRenderTeeBodySize(&Animation, &Info, Offset, Width, Height);
	EXPECT_FLOAT_EQ(Width, 41.6f);
	EXPECT_FLOAT_EQ(Height, 20.8f);
	EXPECT_FLOAT_EQ(Offset.x, 10.4f);
	EXPECT_FLOAT_EQ(Offset.y, 5.2f);
	CRenderTools::GetRenderTeeFeetSize(&Animation, &Info, Offset, Width, Height);
	EXPECT_FLOAT_EQ(Width, 24);
	EXPECT_FLOAT_EQ(Height, 16);
	EXPECT_EQ(Offset, vec2(8, 4));
}

TEST(TeeRenderGeometry, LowerFootExtentMovesClassicVerticalCenter)
{
	const CScopedGeometryConfig Config;
	const CAnimState Animation{};
	auto Info = MakeInfo();
	Info.m_SkinMetrics.m_Body.m_Height = 64;
	Info.m_SkinMetrics.m_Body.m_OffsetY = 0;
	Info.m_SkinMetrics.m_Feet.m_OffsetY = 0;
	vec2 Offset;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(&Animation, &Info, Offset);
	EXPECT_EQ(Offset, vec2(0, 16));
	Info.m_SkinMetrics.m_Feet.m_Height = 32;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(&Animation, &Info, Offset);
	EXPECT_EQ(Offset, vec2(0, 8));
}

TEST(TeeRenderGeometry, SixupBodyOffsetUsesSelectedConnection)
{
	const CScopedGeometryConfig Config;
	const CAnimState Animation{};
	auto Info = MakeInfo();
	CGeometryTextureFactory Textures;
	Info.m_aSixup[1].m_aOriginalTextures[protocol7::SKINPART_BODY] = Textures.BodyTexture();
	g_Config.m_ClDummy = 1;
	vec2 Offset;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(&Animation, &Info, Offset);
	EXPECT_FLOAT_EQ(Offset.x, 0);
	EXPECT_FLOAT_EQ(Offset.y, 7.68f);
	g_Config.m_ClDummy = 0;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(&Animation, &Info, Offset);
	EXPECT_EQ(Offset, vec2(0, 12));
}

TEST(Skins, RenderedTeeOffsetKeepsGlobalHorizontalOffsetStable)
{
	const CScopedGeometryConfig Config;
	const CAnimState Animation{};
	CTeeRenderInfo Info;
	Info.m_Size = 64;
	auto &Body = Info.m_SkinMetrics.m_Body;
	Body.m_Width = 80;
	Body.m_Height = 82;
	Body.m_OffsetX = 16;
	Body.m_OffsetY = 14;
	Body.m_MaxWidth = Body.m_MaxHeight = 96;
	auto &Feet = Info.m_SkinMetrics.m_Feet;
	Feet.m_Width = 44;
	Feet.m_Height = 23;
	Feet.m_OffsetX = 20;
	Feet.m_OffsetY = 9;
	Feet.m_MaxWidth = 64;
	Feet.m_MaxHeight = 32;
	vec2 Offset;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(&Animation, &Info, Offset);
	EXPECT_FLOAT_EQ(Offset.x, 0);
	EXPECT_NEAR(Offset.y, -14.0f / 3.0f, 0.0001f);
}
