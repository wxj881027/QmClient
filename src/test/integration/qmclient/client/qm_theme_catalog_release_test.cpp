#include <game/client/components/qmclient/theme_catalog.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <cstdlib>
#include <new>
#include <vector>

namespace
{
	class CThemeReleaseGraphics : public CIconBenchmarkGraphics
	{
	public:
		bool m_Throw = false;
		int m_FailOnAttempt = 0;
		int m_Attempts = 0;
		int m_Releases = 0;
		void UnloadTexture(CTextureHandle *pHandle) override
		{
			++m_Attempts;
			if(m_Throw || m_Attempts == m_FailOnAttempt)
				throw std::bad_alloc();
			if(pHandle->IsValid())
			{
				++m_Releases;
				pHandle->Invalidate();
			}
		}
	};
	class QmThemeCatalogRelease : public ::testing::Test
	{
	protected:
		CThemeReleaseGraphics m_Graphics;
		std::vector<CTheme> m_vThemes;
		void SetUp() override
		{
			for(const char *pName : {"first.png", "second.png"})
			{
				auto &Theme = m_vThemes.emplace_back(pName, true, true);
				CImageInfo Image;
				Image.m_Width = Image.m_Height = 1;
				Image.m_Format = CImageInfo::FORMAT_RGBA;
				Image.m_pData = static_cast<uint8_t *>(malloc(4));
				ASSERT_NE(Image.m_pData, nullptr);
				Theme.m_IconTexture = m_Graphics.LoadTextureRawMove(Image, 0, pName);
				ASSERT_TRUE(Theme.m_IconTexture.IsValid());
			}
		}
		void TearDown() override
		{
			m_Graphics.m_Throw = false;
			m_Graphics.m_FailOnAttempt = 0;
			EXPECT_TRUE(ClearThemeCatalog(m_vThemes, &m_Graphics));
		}
	};
}

TEST_F(QmThemeCatalogRelease, SuccessfulClearReleasesEachTextureOnceAndEmptiesCatalog)
{
	ASSERT_TRUE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	EXPECT_TRUE(m_vThemes.empty());
	EXPECT_EQ(m_Graphics.m_Releases, 2);
	EXPECT_TRUE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	EXPECT_EQ(m_Graphics.m_Attempts, 2);
}

TEST_F(QmThemeCatalogRelease, FailedUnloadsRetainCatalogOwnershipAndCanRecover)
{
	m_Graphics.m_Throw = true;
	EXPECT_FALSE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	ASSERT_EQ(m_vThemes.size(), 2u);
	EXPECT_EQ(m_vThemes[0].m_Name, "first.png");
	EXPECT_TRUE(m_vThemes[0].m_IconTexture.IsValid());
	EXPECT_TRUE(m_vThemes[1].m_IconTexture.IsValid());
	EXPECT_EQ(m_Graphics.m_Releases, 0);
	m_Graphics.m_Throw = false;
	EXPECT_TRUE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	EXPECT_TRUE(m_vThemes.empty());
	EXPECT_EQ(m_Graphics.m_Releases, 2);
	EXPECT_EQ(m_Graphics.m_Attempts, 4);
}

TEST_F(QmThemeCatalogRelease, PartialSuccessIsNotUnloadedAgainOnRetry)
{
	m_Graphics.m_FailOnAttempt = 1;
	EXPECT_FALSE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	ASSERT_EQ(m_vThemes.size(), 2u);
	EXPECT_TRUE(m_vThemes[0].m_IconTexture.IsValid());
	EXPECT_FALSE(m_vThemes[1].m_IconTexture.IsValid());
	EXPECT_EQ(m_Graphics.m_Releases, 1);
	EXPECT_TRUE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	EXPECT_TRUE(m_vThemes.empty());
	EXPECT_EQ(m_Graphics.m_Releases, 2);
	EXPECT_EQ(m_Graphics.m_Attempts, 3);
}

TEST_F(QmThemeCatalogRelease, MissingGraphicsRetainsLiveHandlesForLaterRelease)
{
	EXPECT_FALSE(ClearThemeCatalog(m_vThemes, nullptr));
	ASSERT_EQ(m_vThemes.size(), 2u);
	EXPECT_TRUE(m_vThemes[0].m_IconTexture.IsValid());
	EXPECT_TRUE(m_vThemes[1].m_IconTexture.IsValid());
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
	EXPECT_TRUE(ClearThemeCatalog(m_vThemes, &m_Graphics));
	EXPECT_TRUE(m_vThemes.empty());
	EXPECT_EQ(m_Graphics.m_Releases, 2);
}
