#include <game/client/components/qmclient/nameplate_text_cache.h>

#include <gtest/gtest.h>

TEST(QmNameplateTextCache, HiddenAndEmptyRowsDoNotRebuildEveryFrame)
{
	CQmNameplateTextCache Cache;
	int Builds = 0;
	const auto Frame = [&](bool Visible, bool Changed) {
		if(Cache.NeedsUpdate(Visible, Changed))
		{
			++Builds;
			// 即使空文字没有生成容器，这次布局尝试也已完成。
			Cache.OnUpdate(1);
		}
	};
	for(int FrameIndex = 0; FrameIndex < 128; ++FrameIndex)
		Frame(false, true);
	EXPECT_EQ(Builds, 0);
	Frame(true, true);
	for(int FrameIndex = 0; FrameIndex < 128; ++FrameIndex)
		Frame(true, false);
	EXPECT_EQ(Builds, 1);
	Frame(false, false);
	Frame(true, false);
	EXPECT_EQ(Builds, 1);
	// 文字/字号变化和窗口容器失效都必须重建，不把空结果永久缓存。
	Frame(true, true);
	EXPECT_EQ(Builds, 2);
	Cache.Reset();
	Frame(false, false);
	Frame(true, false);
	EXPECT_EQ(Builds, 3);
}

TEST(QmNameplateTextCache, CoordinateUpdatesReuseContainerAndRecreateAfterInvalidation)
{
	struct CTextRecorder
	{
		int m_Creates = 0;
		int m_Rewrites = 0;
		std::string m_Text;
		void CreateOrAppendTextContainer(STextContainerIndex &Index, CTextCursor *, const char *pText)
		{
			EXPECT_FALSE(Index.Valid());
			Index.m_Index = 7;
			++m_Creates;
			m_Text = pText;
		}
		void RecreateTextContainerSoft(STextContainerIndex &Index, CTextCursor *, const char *pText)
		{
			EXPECT_EQ(Index.m_Index, 7);
			++m_Rewrites;
			m_Text = pText;
		}
	} Renderer;
	STextContainerIndex Index;
	CTextCursor Cursor;
	QmUpdateNameplateTextContainer(&Renderer, Index, &Cursor, "X:99.99");
	QmUpdateNameplateTextContainer(&Renderer, Index, &Cursor, "X:100.00");
	QmUpdateNameplateTextContainer(&Renderer, Index, &Cursor, "X:-0.01");
	EXPECT_EQ(Renderer.m_Creates, 1);
	EXPECT_EQ(Renderer.m_Rewrites, 2);
	EXPECT_EQ(Renderer.m_Text, "X:-0.01");
	Index.Reset();
	QmUpdateNameplateTextContainer(&Renderer, Index, &Cursor, "X:0.00");
	EXPECT_EQ(Renderer.m_Creates, 2);
	EXPECT_EQ(Renderer.m_Rewrites, 2);
}

TEST(QmNameplateTextCache, AtlasReplacementInvalidatesUnchangedTextWithoutZoom)
{
	CQmNameplateTextCache Cache;
	Cache.OnUpdate(10);
	EXPECT_FALSE(Cache.NeedsUpdate(true, false));
	EXPECT_FALSE(Cache.ResourcesChanged(10));
	// 字体预览清空并重用图集后，容器句柄和文本未变也必须重新创建。
	ASSERT_TRUE(Cache.ResourcesChanged(11));
	Cache.Reset();
	EXPECT_TRUE(Cache.NeedsUpdate(true, false));
	Cache.OnUpdate(11);
	EXPECT_FALSE(Cache.ResourcesChanged(11));
	EXPECT_FALSE(Cache.NeedsUpdate(true, false));
}

TEST(QmNameplateTextCache, HiddenPartRebuildsOnceWhenShownAfterAtlasReplacement)
{
	CQmNameplateTextCache Cache;
	Cache.OnUpdate(10);
	ASSERT_TRUE(Cache.ResourcesChanged(12));
	Cache.Reset();
	for(int Frame = 0; Frame < 32; ++Frame)
	{
		EXPECT_FALSE(Cache.ResourcesChanged(12));
		EXPECT_FALSE(Cache.NeedsUpdate(false, false));
	}
	ASSERT_TRUE(Cache.NeedsUpdate(true, false));
	Cache.OnUpdate(12);
	EXPECT_FALSE(Cache.ResourcesChanged(12));
	EXPECT_FALSE(Cache.NeedsUpdate(true, false));
}

TEST(QmNameplateTextCache, EmptyLayoutRecordsNewAtlasRevisionWithoutRetryLoop)
{
	CQmNameplateTextCache Cache;
	Cache.OnUpdate(10);
	ASSERT_TRUE(Cache.ResourcesChanged(11));
	Cache.Reset();
	ASSERT_TRUE(Cache.NeedsUpdate(true, false));
	Cache.OnUpdate(11);
	for(int Frame = 0; Frame < 32; ++Frame)
	{
		EXPECT_FALSE(Cache.ResourcesChanged(11));
		EXPECT_FALSE(Cache.NeedsUpdate(true, false));
	}
	EXPECT_TRUE(Cache.ResourcesChanged(12));
}
