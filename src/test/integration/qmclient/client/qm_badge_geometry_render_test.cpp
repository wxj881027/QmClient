#include <game/client/components/qmclient/qm_developer_badge.h>
#include <game/client/components/qmclient/qm_title_badge.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <cstring>
#include <unordered_set>
#include <vector>

namespace
{
	// 只替换设备边界；SDF 和 PNG 读取仍沿用会报错的默认替身，意外回到旧路径会直接失败。
	class CBadgeGraphics : public CIconBenchmarkGraphics
	{
	public:
		struct SDraw
		{
			ColorRGBA m_Color;
			vec2 m_Scale;
		};

		uint32_t m_Version = 0;
		int m_Creates = 0;
		int m_Deletes = 0;
		bool m_FailCreate = false;
		std::unordered_set<int> m_Live;
		std::vector<SDraw> m_vDraws;
		std::vector<ColorRGBA> m_vCapsules;
		ColorRGBA m_Color;
		std::vector<CFreeformItem> m_vImmediate;
		using CIconBenchmarkGraphics::LoadPng;
		bool LoadPng(CImageInfo &Image, const char *pPath, int StorageType) override
		{
			// 距离场 Logo 是当前正常读取的贴图；几何用例不加载它，让它失败后渲染
			// 退回几何胶囊。其它 PNG 仍然代表回退到旧贴图管线。
			if(strcmp(pPath, "qmclient/title_badges/logo_sdf.png") == 0)
				return false;
			ADD_FAILURE() << "徽章几何不应读取 PNG: " << pPath;
			return false;
		}

		uint32_t GraphicsResourcesResetVersion() const override { return m_Version; }
		int CreateQuadContainer(bool AutomaticUpload) override
		{
			if(m_FailCreate)
				return -1;
			const int Id = ++m_Creates;
			m_Live.insert(Id);
			return Id;
		}
		void QuadContainerChangeAutomaticUpload(int Container, bool AutomaticUpload) override { EXPECT_EQ(m_Live.count(Container), 1u); }
		void QuadContainerUpload(int Container) override { EXPECT_EQ(m_Live.count(Container), 1u); }
		int QuadContainerAddQuads(int Container, CFreeformItem *pItems, int Num) override
		{
			EXPECT_EQ(m_Live.count(Container), 1u);
			return 0;
		}
		void DeleteQuadContainer(int &Container) override
		{
			EXPECT_EQ(m_Live.erase(Container), 1u);
			++m_Deletes;
			Container = -1;
		}
		void SetColor(ColorRGBA Color) override { m_Color = Color; }
		void SetColor(float R, float G, float B, float A) override { m_Color = ColorRGBA(R, G, B, A); }
		void RenderQuadContainerEx(int Container, int Offset, int Count, float X, float Y, float ScaleX, float ScaleY) override
		{
			EXPECT_EQ(m_Live.count(Container), 1u);
			EXPECT_GT(Count, 0);
			m_vDraws.push_back({m_Color, vec2(ScaleX, ScaleY)});
		}
		void DrawRect(float X, float Y, float W, float H, ColorRGBA Color, int Corners, float Radius) override { m_vCapsules.push_back(Color); }
		void DrawCircle(float X, float Y, float Radius, int Segments) override { EXPECT_GT(Radius, 0.0f); }
		void QuadsDrawFreeform(const CFreeformItem *pItems, int Count) override { m_vImmediate.insert(m_vImmediate.end(), pItems, pItems + Count); }
	};
}

TEST(QmBadgeGeometryRender, BothBadgesUseTheSharedGeometricCapsuleWithoutTextureLoading)
{
	CBadgeGraphics Graphics;
	CQmDeveloperBadge Developer;
	CQmTitleBadge Sponsor;
	Sponsor.Init(&Graphics);
	// 开发者徽章现在把标题 Logo 画在中间，需要先有一枚可用的标题徽章。
	Developer.Init(&Graphics, Sponsor);
	Developer.Render(&Graphics, {0, 0, 91, 24}, 1.0f, 0.0);
	ASSERT_EQ(Graphics.m_vCapsules.size(), 2u);
	EXPECT_FALSE(Graphics.m_vDraws.empty());
	Sponsor.RenderSurface(&Graphics, {0, 0, 117, 24}, {}, 1.0f);
	EXPECT_EQ(Graphics.m_vCapsules.size(), 5u);
	Developer.Shutdown(&Graphics);
	Sponsor.Shutdown(&Graphics);
	EXPECT_TRUE(Graphics.m_Live.empty());
}

TEST(QmBadgeGeometryRender, RepeatedRenderingReusesUploadedGeometry)
{
	CBadgeGraphics Graphics;
	CQmTitleBadge Sponsor;
	Sponsor.Init(&Graphics);
	const int Creates = Graphics.m_Creates;
	for(int Frame = 0; Frame < 5; ++Frame)
		Sponsor.RenderSurface(&Graphics, {10, 20, 160, 24}, {}, 1.0f);
	EXPECT_EQ(Graphics.m_Creates, Creates);
	Sponsor.Shutdown(&Graphics);
}

TEST(QmBadgeGeometryRender, DeviceResetRebuildsGeometryWithoutDeletingOldDeviceIds)
{
	CBadgeGraphics Graphics;
	CQmDeveloperBadge Developer;
	CQmTitleBadge TitleBadge;
	TitleBadge.Init(&Graphics);
	Developer.Init(&Graphics, TitleBadge);
	Graphics.m_Live.clear();
	const int CreatesBeforeReset = Graphics.m_Creates;
	++Graphics.m_Version;
	Developer.Render(&Graphics, {0, 0, 91, 24}, 1.0f, 0.5);
	// 设备重建：几何重新上传（创建数增加），旧设备句柄一个都不删（删除数为 0）；
	// 具体创建几个容器由徽章内部结构决定，这里只锁重建与不删旧句柄两条不变量。
	EXPECT_GT(Graphics.m_Creates, CreatesBeforeReset);
	EXPECT_EQ(Graphics.m_Deletes, 0);
	EXPECT_FALSE(Graphics.m_Live.empty());
	Developer.Shutdown(&Graphics);
	// 关闭时释放重建后仍由徽章持有的容器。
	EXPECT_GT(Graphics.m_Deletes, 0);
}

TEST(QmBadgeGeometryRender, ColourGroupsPreserveDeveloperAccentAndTextOnLegacyBackends)
{
	CBadgeGraphics Graphics;
	CQmDeveloperBadge Developer;
	CQmTitleBadge TitleBadge;
	TitleBadge.Init(&Graphics);
	Developer.Init(&Graphics, TitleBadge);
	Developer.Render(&Graphics, {0, 0, 91, 24}, 0.5f, 0.0);
	bool HasAccent = false;
	bool HasText = false;
	for(const auto &Draw : Graphics.m_vDraws)
	{
		EXPECT_LE(Draw.m_Color.a, 0.5f);
		HasAccent |= Draw.m_Color.r == ColorRGBA(0x8bdfcf).r;
		HasText |= Draw.m_Color.r == ColorRGBA(0xd8fff5).r;
	}
	EXPECT_TRUE(HasAccent);
	EXPECT_TRUE(HasText);
	Developer.Shutdown(&Graphics);
}

TEST(QmBadgeGeometryRender, SponsorEndPatternsKeepTheirAspectRatioForLongTitles)
{
	CBadgeGraphics Graphics;
	CQmTitleBadge Sponsor;
	Sponsor.Init(&Graphics);
	Sponsor.RenderSurface(&Graphics, {0, 0, 250, 48}, {}, 1.0f);
	ASSERT_EQ(Graphics.m_vDraws.size(), 4u);
	EXPECT_EQ(Graphics.m_vDraws[0].m_Scale, vec2(2, 2));
	EXPECT_EQ(Graphics.m_vDraws[2].m_Scale, vec2(2, 2));
	EXPECT_EQ(Graphics.m_vDraws[3].m_Scale, vec2(2, 2));
	Sponsor.Shutdown(&Graphics);
}

TEST(QmBadgeGeometryRender, HiddenAndTransparentBadgesDoNotDraw)
{
	CBadgeGraphics Graphics;
	CQmTitleBadge Sponsor;
	Sponsor.Init(&Graphics);
	Sponsor.RenderSurface(&Graphics, {0, 0, 117, 24}, {}, 0.0f);
	Sponsor.RenderSurface(&Graphics, {0, 0, 117, 24}, {EQmTitleBadgeStyle::MOON, 0, 0}, 1.0f);
	Sponsor.RenderSurface(&Graphics, {0, 0, 0, 24}, {}, 1.0f);
	EXPECT_TRUE(Graphics.m_vCapsules.empty());
	EXPECT_TRUE(Graphics.m_vDraws.empty());
	Sponsor.Shutdown(&Graphics);
}

TEST(QmBadgeGeometryRender, GeometryAllocationFailureLeavesNoAvailableDeveloperBadgeOrResources)
{
	CBadgeGraphics Graphics;
	Graphics.m_FailCreate = true;
	CQmDeveloperBadge Developer;
	CQmTitleBadge TitleBadge;
	TitleBadge.Init(&Graphics);
	Developer.Init(&Graphics, TitleBadge);
	EXPECT_FALSE(Developer.Available());
	Developer.Render(&Graphics, {0, 0, 91, 24}, 1.0f, 0.0);
	EXPECT_TRUE(Graphics.m_vDraws.empty());
	Developer.Shutdown(&Graphics);
	EXPECT_TRUE(Graphics.m_Live.empty());
}

TEST(QmBadgeGeometryRender, ReinitializationAndRepeatedShutdownReleaseEachContainerOnce)
{
	CBadgeGraphics Graphics;
	CQmTitleBadge Sponsor;
	Sponsor.Init(&Graphics);
	Sponsor.Init(&Graphics);
	EXPECT_EQ(Graphics.m_Creates, 14);
	EXPECT_EQ(Graphics.m_Deletes, 7);
	Sponsor.Shutdown(&Graphics);
	Sponsor.Shutdown(&Graphics);
	EXPECT_TRUE(Graphics.m_Live.empty());
	EXPECT_EQ(Graphics.m_Creates, Graphics.m_Deletes);
}

TEST(QmBadgeGeometryRender, ShortTitleCropsTheLeftDecorationWithoutCompressingIt)
{
	CBadgeGraphics Graphics;
	CQmTitleBadge Sponsor;
	Sponsor.Init(&Graphics);
	Sponsor.RenderSurface(&Graphics, {0, 0, 65, 24}, {}, 1.0f);
	ASSERT_FALSE(Graphics.m_vImmediate.empty());
	for(const auto &Q : Graphics.m_vImmediate)
		EXPECT_LE(std::max({Q.m_X0, Q.m_X1, Q.m_X2, Q.m_X3}), 33.0001f);
	ASSERT_EQ(Graphics.m_vDraws.size(), 2u);
	EXPECT_EQ(Graphics.m_vDraws[0].m_Scale, vec2(1, 1));
	EXPECT_EQ(Graphics.m_vDraws[1].m_Scale, vec2(1, 1));
	Sponsor.Shutdown(&Graphics);
}
