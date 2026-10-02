#include <engine/textrender.h>

#include <gtest/gtest.h>

TEST(TextContainerLifetime, AtlasRepackInvalidatesEveryHandleCopy)
{
	auto pRevision = std::make_shared<uint64_t>(1);
	STextContainerIndex Index;
	Index.m_Index = 7;
	Index.m_UseCount->BindGlyphAtlas(pRevision);
	const STextContainerIndex Copy = Index;
	ASSERT_TRUE(Index.Valid());
	ASSERT_TRUE(Copy.Valid());

	++*pRevision;
	EXPECT_FALSE(Index.Valid());
	EXPECT_FALSE(Copy.Valid());
	// 图集失效仍保留原槽位，以便归还旧资源。
	EXPECT_EQ(Index.m_Index, 7);
}

TEST(TextContainerLifetime, UnchangedAtlasKeepsContainerReusable)
{
	auto pRevision = std::make_shared<uint64_t>(3);
	STextContainerIndex Index;
	Index.m_Index = 2;
	Index.m_UseCount->BindGlyphAtlas(pRevision);
	// 普通字形上传和保留像素坐标的扩容不推进资源版本。
	for(int Frame = 0; Frame < 100; ++Frame)
		EXPECT_TRUE(Index.Valid());
}

TEST(TextContainerLifetime, ReleasedCopyCannotReviveWhenSlotIsReused)
{
	auto pRevision = std::make_shared<uint64_t>(1);
	STextContainerIndex Index;
	Index.m_Index = 4;
	Index.m_UseCount->BindGlyphAtlas(pRevision);
	const STextContainerIndex Copy = Index;
	Index.m_UseCount->Invalidate();
	EXPECT_FALSE(Copy.Valid());

	Index.Reset();
	Index.m_UseCount = std::make_shared<STextContainerUsages>();
	Index.m_UseCount->BindGlyphAtlas(pRevision);
	Index.m_Index = 4;
	EXPECT_TRUE(Index.Valid());
	EXPECT_FALSE(Copy.Valid());
	// 对旧副本重复失效不会影响新容器。
	Copy.m_UseCount->Invalidate();
	EXPECT_TRUE(Index.Valid());
}

TEST(TextContainerLifetime, RebuildAcceptsNewRevisionAndInvalidatesAgainOnNextRepack)
{
	auto pRevision = std::make_shared<uint64_t>(1);
	STextContainerIndex Index;
	Index.m_Index = 5;
	Index.m_UseCount->BindGlyphAtlas(pRevision);
	++*pRevision;
	ASSERT_FALSE(Index.Valid());

	Index.m_UseCount->Invalidate();
	Index.Reset();
	Index.m_UseCount->BindGlyphAtlas(pRevision);
	Index.m_Index = 5;
	EXPECT_TRUE(Index.Valid());
	++*pRevision;
	EXPECT_FALSE(Index.Valid());
}

TEST(TextContainerLifetime, RevisionSourceSurvivesOwnerReleaseWithoutDanglingReference)
{
	STextContainerIndex Index;
	Index.m_Index = 1;
	{
		auto pRevision = std::make_shared<uint64_t>(9);
		Index.m_UseCount->BindGlyphAtlas(pRevision);
	}
	EXPECT_TRUE(Index.Valid());
	Index.m_UseCount->Invalidate();
	EXPECT_FALSE(Index.Valid());
}
