#include <game/client/components/qmclient/qm_developer_badge.h>
#include <game/client/components/qmclient/qm_title_badge.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace
{
	// 仅替换纹理和 GPU 提交边界，资源生命周期、能力选择与 Logo 布局都使用生产实现。
	class CTitleBadgeGraphics final : public CIconBenchmarkGraphics
	{
		CTextureHandle m_Texture;

	public:
		struct STexture
		{
			int m_Id;
			std::string m_Path;
			int m_Flags;
		};
		struct SGeometryDraw
		{
			int m_Container;
			vec2 m_Position;
			vec2 m_Scale;
		};
		struct SDistanceDraw
		{
			int m_Texture;
			SRoundedRectSdfParams m_Params;
		};
		bool m_HasSdf = true;
		bool m_HasSdfTexture = true;
		std::string m_MissingPath;
		std::string m_LoadPath;
		std::vector<STexture> m_vTextures;
		std::vector<int> m_vUnloaded;
		std::vector<int> m_vCreatedContainers;
		std::vector<SGeometryDraw> m_vGeometryDraws;
		std::vector<SDistanceDraw> m_vDistanceDraws;
		float m_Zoom = 1.0f;
		uint32_t m_ResetVersion = 0;
		uint32_t GraphicsResourcesResetVersion() const override { return m_ResetVersion; }

		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = *pY0 = 0.0f;
			*pX1 = ScreenWidth() / m_Zoom;
			*pY1 = ScreenHeight() / m_Zoom;
		}
		int CreateQuadContainer(bool) override
		{
			const int Id = m_NextId++;
			m_Containers.emplace(Id, 0);
			m_vCreatedContainers.push_back(Id);
			return Id;
		}
		int QuadContainerAddQuads(int Id, CFreeformItem *, int Num) override
		{
			const int Offset = m_Containers.at(Id);
			m_Containers.at(Id) += Num;
			return Offset;
		}
		void QuadContainerUpload(int Id) override
		{
			(void)m_Containers.at(Id);
			++m_Uploads;
		}
		void QuadContainerChangeAutomaticUpload(int, bool) override {}
		void DeleteQuadContainer(int &Id) override
		{
			EXPECT_EQ(m_Containers.erase(Id), 1u);
			Id = -1;
		}

		bool LoadPng(CImageInfo &Image, const char *pPath, int) override
		{
			m_LoadPath = pPath;
			if(m_MissingPath == pPath)
				return false;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = static_cast<uint8_t *>(std::calloc(4, 1));
			return Image.m_pData != nullptr;
		}
		CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pName) override
		{
			const auto Texture = CIconBenchmarkGraphics::LoadTextureRawMove(Image, Flags, pName);
			m_vTextures.push_back({Texture.Id(), m_LoadPath, Flags});
			return Texture;
		}
		void UnloadTexture(CTextureHandle *pTexture) override
		{
			EXPECT_TRUE(pTexture->IsValid());
			m_vUnloaded.push_back(pTexture->Id());
			pTexture->Invalidate();
		}
		void TextureSet(CTextureHandle Texture) override { m_Texture = Texture; }
		void TextureClear() override { m_Texture.Invalidate(); }
		bool HasRoundedRectSdf() override { return m_HasSdf; }
		bool HasRoundedRectSdfTexture() override { return m_HasSdfTexture; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override { m_vDistanceDraws.push_back({m_Texture.Id(), Params}); }
		void DrawRect(float, float, float, float, ColorRGBA, int, float) override {}
		void RenderQuadContainerEx(int Id, int, int, float X, float Y, float ScaleX, float ScaleY) override
		{
			m_vGeometryDraws.push_back({Id, vec2(X, Y), vec2(ScaleX, ScaleY)});
		}
	};

	class CQmTitleBadgeRender : public testing::Test
	{
	protected:
		CTitleBadgeGraphics m_Graphics;
		CQmTitleBadge m_Badge;
		const CUIRect m_Rect{10, 20, 117, 24};

		void TearDown() override { m_Badge.Shutdown(&m_Graphics); }
	};
}

TEST_F(CQmTitleBadgeRender, DistanceLogoUsesItsOwnTextureWithoutColorMipmaps)
{
	m_Badge.Init(&m_Graphics);
	ASSERT_EQ(m_Graphics.m_vTextures.size(), 1u);
	const auto &Distance = m_Graphics.m_vTextures[0];
	EXPECT_EQ(Distance.m_Path, "qmclient/title_badges/logo_sdf.png");
	EXPECT_NE(Distance.m_Flags & IGraphics::TEXLOAD_NO_MIPMAPS, 0);
	m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
	ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 1u);
	EXPECT_EQ(m_Graphics.m_vDistanceDraws[0].m_Texture, Distance.m_Id);
	for(const auto &Draw : m_Graphics.m_vGeometryDraws)
		EXPECT_NE(Draw.m_Container, m_Graphics.m_vCreatedContainers[0]);
}

TEST_F(CQmTitleBadgeRender, DistanceFieldPaddingKeepsTheOriginalLogoSizeAndPosition)
{
	m_Badge.Init(&m_Graphics);
	m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
	ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 1u);
	const auto &Params = m_Graphics.m_vDistanceDraws[0].m_Params;
	EXPECT_EQ(Params.m_Rect, vec4(17, 22, 20, 20));
	EXPECT_FLOAT_EQ(Params.m_TextureParams0.x, IGraphics::ROUNDED_RECT_SDF_TEXTURE_DISTANCE);
	// 原图左上角在外扩后的纹理中占 10%；映射回屏幕仍为 (19, 24)。
	EXPECT_FLOAT_EQ(Params.m_Rect.x + Params.m_Rect.z * 0.1f, 19.0f);
	EXPECT_FLOAT_EQ(Params.m_Rect.y + Params.m_Rect.w * 0.1f, 24.0f);
	EXPECT_FLOAT_EQ(Params.m_TextureParams0.y * Params.m_Rect.z, 1.0f);
	EXPECT_FLOAT_EQ(Params.m_TextureParams0.z * Params.m_Rect.w, 1.0f);
	EXPECT_FLOAT_EQ(Params.m_TextureParams1.z, 4.0f / 20.0f);
}

TEST_F(CQmTitleBadgeRender, LogoColorAndFadeApplyOnceForBothStyles)
{
	m_Badge.Init(&m_Graphics);
	for(const auto Style : {EQmTitleBadgeStyle::MOON, EQmTitleBadgeStyle::SUN})
	{
		m_Graphics.m_vDistanceDraws.clear();
		const SQmTitleAppearance Appearance{Style, QmTitleBadgeDefaultColor(Style), 40};
		m_Badge.RenderSurface(&m_Graphics, m_Rect, Appearance, 0.5f);
		ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 1u);
		const auto &Params = m_Graphics.m_vDistanceDraws[0].m_Params;
		const auto Icon = QmTitleBadgePalette(Appearance, 0.5f).m_Icon;
		EXPECT_EQ(Params.m_FillColor, vec4(Icon.r, Icon.g, Icon.b, 0.2f));
		EXPECT_FLOAT_EQ(Params.m_TextureParams1.y, 1.0f);
	}
}

TEST_F(CQmTitleBadgeRender, CameraScaleReusesTheLoadedLogoWithOneSubmission)
{
	m_Badge.Init(&m_Graphics);
	const auto Uploads = m_Graphics.m_Uploads;
	const auto Bytes = m_Graphics.m_UploadBytes;
	for(const float Zoom : {0.375f, 0.75f, 1.0f, 4.0f, 0.375f})
	{
		m_Graphics.m_Zoom = Zoom;
		m_Graphics.m_vDistanceDraws.clear();
		m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
		ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 1u);
		const auto &Rect = m_Graphics.m_vDistanceDraws[0].m_Params.m_Rect;
		EXPECT_FLOAT_EQ(Rect.z, 20.0f);
		EXPECT_FLOAT_EQ(Rect.w, 20.0f);
		EXPECT_FLOAT_EQ(m_Graphics.m_vDistanceDraws[0].m_Params.m_Params.y, 1.0f / Zoom);
		EXPECT_EQ(m_Graphics.m_Uploads, Uploads);
		EXPECT_EQ(m_Graphics.m_UploadBytes, Bytes);
	}
}

TEST_F(CQmTitleBadgeRender, UnsupportedTexturePipelineKeepsTheExistingGeometry)
{
	m_Badge.Init(&m_Graphics);
	for(const bool HasSdf : {false, true})
	{
		m_Graphics.m_HasSdf = HasSdf;
		m_Graphics.m_HasSdfTexture = false;
		m_Graphics.m_vGeometryDraws.clear();
		m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
		EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
		ASSERT_FALSE(m_Graphics.m_vGeometryDraws.empty());
		const auto &Logo = m_Graphics.m_vGeometryDraws.back();
		EXPECT_EQ(Logo.m_Container, m_Graphics.m_vCreatedContainers[0]);
		EXPECT_EQ(Logo.m_Position, vec2(19, 24));
		EXPECT_EQ(Logo.m_Scale, vec2(1, 1));
	}
}

TEST_F(CQmTitleBadgeRender, MissingDistanceAssetKeepsTheExistingGeometry)
{
	m_Graphics.m_MissingPath = "qmclient/title_badges/logo_sdf.png";
	m_Badge.Init(&m_Graphics);
	m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
	EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
	ASSERT_FALSE(m_Graphics.m_vGeometryDraws.empty());
	EXPECT_EQ(m_Graphics.m_vGeometryDraws.back().m_Container, m_Graphics.m_vCreatedContainers[0]);
}

TEST_F(CQmTitleBadgeRender, TransparentOrEmptyBadgesDoNotSubmitTheLogo)
{
	m_Badge.Init(&m_Graphics);
	SQmTitleAppearance Transparent;
	Transparent.m_Opacity = 0;
	m_Badge.RenderSurface(&m_Graphics, m_Rect, Transparent, 1.0f);
	m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 0.0f);
	m_Badge.RenderSurface(&m_Graphics, {0, 0, 0, 24}, {}, 1.0f);
	EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
	EXPECT_TRUE(m_Graphics.m_vGeometryDraws.empty());
}

TEST_F(CQmTitleBadgeRender, DeviceResetFallsBackToGeometryUntilResourcesAreInitializedAgain)
{
	m_Badge.Init(&m_Graphics);
	// 设备旧容器已丢失，生产几何组件应按新代际重新创建。
	m_Graphics.m_Containers.clear();
	++m_Graphics.m_ResetVersion;
	m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
	EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
	ASSERT_FALSE(m_Graphics.m_vGeometryDraws.empty());
	EXPECT_EQ(m_Graphics.m_vGeometryDraws.back().m_Container, m_Graphics.m_vCreatedContainers.back());
	m_Badge.Init(&m_Graphics);
	m_Badge.RenderSurface(&m_Graphics, m_Rect, {}, 1.0f);
	EXPECT_EQ(m_Graphics.m_vDistanceDraws.size(), 1u);
}

TEST_F(CQmTitleBadgeRender, ReinitializationAndRepeatedShutdownReleaseEachTextureOnce)
{
	m_Badge.Init(&m_Graphics);
	m_Badge.Init(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_vUnloaded.size(), 1u);
	m_Badge.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_vUnloaded.size(), 2u);
	m_Badge.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_vUnloaded.size(), 2u);
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	for(size_t i = 0; i < m_Graphics.m_vUnloaded.size(); ++i)
		EXPECT_EQ(m_Graphics.m_vUnloaded[i], m_Graphics.m_vTextures[i].m_Id);
}

namespace
{
	class CQmDeveloperBadgeRender : public testing::Test
	{
	protected:
		CTitleBadgeGraphics m_Graphics;
		CQmTitleBadge m_Title;
		CQmDeveloperBadge m_Badge;
		const CUIRect m_Rect{10, 20, 91, 24};
		int m_CircuitContainer = -1;
		int m_LetteringContainer = -1;
		int m_LogoTexture = -1;

		void Init()
		{
			m_Title.Init(&m_Graphics);
			m_LogoTexture = m_Graphics.m_vTextures.back().m_Id;
			const auto FirstContainer = m_Graphics.m_vCreatedContainers.size();
			m_Badge.Init(&m_Graphics, m_Title);
			m_CircuitContainer = m_Graphics.m_vCreatedContainers[FirstContainer];
			m_LetteringContainer = m_Graphics.m_vCreatedContainers.back();
		}
		void Render(float Alpha = 1.0f)
		{
			// 本组只验证静态字形提交，光点轨迹由独立的运动测试负责。
			m_Badge.Render(&m_Graphics, m_Rect, Alpha, std::numeric_limits<double>::quiet_NaN());
		}
		void TearDown() override
		{
			m_Badge.Shutdown(&m_Graphics);
			m_Title.Shutdown(&m_Graphics);
		}
	};
}

TEST_F(CQmDeveloperBadgeRender, WordmarkAndCodeUseSeparateDistanceTexturesWithoutColorMipmaps)
{
	Init();
	ASSERT_EQ(m_Graphics.m_vTextures.size(), 3u);
	EXPECT_EQ(m_Graphics.m_vTextures[1].m_Path, "qmclient/title_badges/developer_wordmark_sdf.png");
	EXPECT_EQ(m_Graphics.m_vTextures[2].m_Path, "qmclient/title_badges/developer_code_sdf.png");
	for(size_t i = 1; i < m_Graphics.m_vTextures.size(); ++i)
		EXPECT_NE(m_Graphics.m_vTextures[i].m_Flags & IGraphics::TEXLOAD_NO_MIPMAPS, 0);
	Render(0.4f);
	ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 3u);
	const auto &Wordmark = m_Graphics.m_vDistanceDraws[0];
	const auto &Code = m_Graphics.m_vDistanceDraws[1];
	EXPECT_EQ(Wordmark.m_Texture, m_Graphics.m_vTextures[1].m_Id);
	EXPECT_EQ(Code.m_Texture, m_Graphics.m_vTextures[2].m_Id);
	const ColorRGBA WordmarkColor(0xd8fff5);
	const ColorRGBA CodeColor(0x8bdfcf);
	EXPECT_EQ(Wordmark.m_Params.m_FillColor, vec4(WordmarkColor.r, WordmarkColor.g, WordmarkColor.b, 0.4f));
	EXPECT_EQ(Code.m_Params.m_FillColor, vec4(CodeColor.r, CodeColor.g, CodeColor.b, 0.55f * 0.4f));
	// 正常管线只绘制电路线网格，文字回退网格不能与距离场叠加。
	ASSERT_EQ(m_Graphics.m_vGeometryDraws.size(), 1u);
	EXPECT_EQ(m_Graphics.m_vGeometryDraws[0].m_Container, m_CircuitContainer);
}

TEST_F(CQmDeveloperBadgeRender, FieldsKeepTheOriginalDesignCoordinates)
{
	Init();
	Render();
	ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 3u);
	const auto &Wordmark = m_Graphics.m_vDistanceDraws[0].m_Params;
	const auto &Code = m_Graphics.m_vDistanceDraws[1].m_Params;
	EXPECT_EQ(Wordmark.m_Rect, vec4(44, 16, 32, 32));
	EXPECT_EQ(Code.m_Rect, vec4(75, 22, 20, 20));
	EXPECT_FLOAT_EQ(Wordmark.m_TextureParams0.y * Wordmark.m_Rect.z, 1.0f);
	EXPECT_FLOAT_EQ(Wordmark.m_TextureParams0.z * Wordmark.m_Rect.w, 1.0f);
	EXPECT_FLOAT_EQ(Wordmark.m_TextureParams1.z, 4.0f / 32.0f);
	EXPECT_FLOAT_EQ(Code.m_TextureParams1.z, 4.0f / 20.0f);
}

TEST_F(CQmDeveloperBadgeRender, CameraZoomReusesBothFieldsWithoutUploadsOrContainerRebuilds)
{
	Init();
	const auto Uploads = m_Graphics.m_Uploads;
	const auto Containers = m_Graphics.m_vCreatedContainers.size();
	for(const float Zoom : {0.375f, 0.75f, 1.0f, 4.0f, 0.375f})
	{
		m_Graphics.m_Zoom = Zoom;
		m_Graphics.m_vDistanceDraws.clear();
		Render();
		ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 3u);
		EXPECT_FLOAT_EQ(m_Graphics.m_vDistanceDraws[0].m_Params.m_Params.y, 1.0f / Zoom);
		EXPECT_FLOAT_EQ(m_Graphics.m_vDistanceDraws[1].m_Params.m_Params.y, 1.0f / Zoom);
		EXPECT_EQ(m_Graphics.m_Uploads, Uploads);
		EXPECT_EQ(m_Graphics.m_vTextures.size(), 3u);
		EXPECT_EQ(m_Graphics.m_vCreatedContainers.size(), Containers);
	}
}

TEST_F(CQmDeveloperBadgeRender, MissingEitherFieldFallsBackToTheCompleteLettering)
{
	for(const char *pMissing : {"qmclient/title_badges/developer_wordmark_sdf.png", "qmclient/title_badges/developer_code_sdf.png"})
	{
		SCOPED_TRACE(pMissing);
		m_Graphics.m_MissingPath = pMissing;
		Init();
		m_Graphics.m_vDistanceDraws.clear();
		m_Graphics.m_vGeometryDraws.clear();
		Render();
		// 仅剩共享兔子使用距离场，Dev 和代码符号整体回退。
		ASSERT_EQ(m_Graphics.m_vDistanceDraws.size(), 1u);
		EXPECT_EQ(m_Graphics.m_vDistanceDraws[0].m_Texture, m_LogoTexture);
		ASSERT_EQ(m_Graphics.m_vGeometryDraws.size(), 3u);
		EXPECT_EQ(m_Graphics.m_vGeometryDraws[1].m_Container, m_LetteringContainer);
		EXPECT_EQ(m_Graphics.m_vGeometryDraws[2].m_Container, m_LetteringContainer);
	}
}

TEST_F(CQmDeveloperBadgeRender, UnsupportedTexturePipelineKeepsBothOriginalGlyphLayers)
{
	Init();
	for(const bool HasSdf : {false, true})
	{
		m_Graphics.m_HasSdf = HasSdf;
		m_Graphics.m_HasSdfTexture = false;
		m_Graphics.m_vGeometryDraws.clear();
		Render();
		EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
		ASSERT_EQ(m_Graphics.m_vGeometryDraws.size(), 4u);
		EXPECT_EQ(m_Graphics.m_vGeometryDraws[1].m_Container, m_LetteringContainer);
		EXPECT_EQ(m_Graphics.m_vGeometryDraws[2].m_Container, m_LetteringContainer);
	}
}

TEST_F(CQmDeveloperBadgeRender, DeviceResetUsesGeometryUntilLetteringIsInitializedAgain)
{
	Init();
	m_Graphics.m_Containers.clear();
	++m_Graphics.m_ResetVersion;
	Render();
	EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
	EXPECT_EQ(m_Graphics.m_vGeometryDraws.size(), 4u);
	Init();
	m_Graphics.m_vDistanceDraws.clear();
	Render();
	EXPECT_EQ(m_Graphics.m_vDistanceDraws.size(), 3u);
}

TEST_F(CQmDeveloperBadgeRender, ReinitializationAndRepeatedShutdownReleaseBothFieldsOnce)
{
	Init();
	m_Badge.Init(&m_Graphics, m_Title);
	EXPECT_EQ(m_Graphics.m_vUnloaded.size(), 2u);
	m_Badge.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_vUnloaded.size(), 4u);
	m_Badge.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_vUnloaded.size(), 4u);
	EXPECT_FALSE(m_Badge.Available());
	m_Graphics.m_vDistanceDraws.clear();
	Render();
	EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
}

TEST_F(CQmDeveloperBadgeRender, InvisibleOrEmptyBadgesDoNotSubmitTheLettering)
{
	Init();
	Render(0.0f);
	m_Badge.Render(&m_Graphics, {0, 0, 0, 24}, 1.0f, 0.0);
	EXPECT_TRUE(m_Graphics.m_vDistanceDraws.empty());
	EXPECT_TRUE(m_Graphics.m_vGeometryDraws.empty());
}
