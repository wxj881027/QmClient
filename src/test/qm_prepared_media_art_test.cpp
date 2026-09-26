#include <game/client/components/qmclient/prepared_media_art.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

TEST(QmPreparedMediaArt, OwnsIndependentOriginalAndCircularPixels)
{
	std::vector<uint8_t> vOriginal = {1, 2, 3, 4, 5, 6, 7, 8};
	std::vector<uint8_t> vCircular = {1, 2, 3, 0, 5, 6, 7, 31};
	CQmPreparedMediaArt Prepared(vOriginal, vCircular, 2, 1);
	ASSERT_NE(Prepared.m_Original.m_pData, nullptr);
	ASSERT_NE(Prepared.m_Circular.m_pData, nullptr);
	EXPECT_EQ(Prepared.m_Original.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(Prepared.m_Circular.m_Width, 2u);
	vOriginal[3] = 99;
	vCircular[7] = 99;
	EXPECT_EQ(Prepared.m_Original.m_pData[3], 4);
	EXPECT_EQ(Prepared.m_Circular.m_pData[7], 31);
}

TEST(QmPreparedMediaArt, MovedOriginalOutlivesPendingPacket)
{
	CImageInfo Transferred;
	const std::vector<uint8_t> vPixels = {11, 22, 33, 44};
	{
		CQmPreparedMediaArt Prepared(vPixels, vPixels, 1, 1);
		Transferred = std::move(Prepared.m_Original);
		EXPECT_EQ(Prepared.m_Original.m_pData, nullptr);
	}
	ASSERT_NE(Transferred.m_pData, nullptr);
	EXPECT_EQ(Transferred.m_pData[2], 33);
	Transferred.Free();
}

TEST(QmPreparedMediaArt, InvalidCircularPixelsDoNotDiscardOriginal)
{
	const std::vector<uint8_t> vPixels = {11, 22, 33, 44};
	CQmPreparedMediaArt Partial(vPixels, {}, 1, 1);
	EXPECT_NE(Partial.m_Original.m_pData, nullptr);
	EXPECT_EQ(Partial.m_Circular.m_pData, nullptr);
	CQmPreparedMediaArt Invalid(vPixels, vPixels, 0, 1);
	EXPECT_EQ(Invalid.m_Original.m_pData, nullptr);
	EXPECT_EQ(Invalid.m_Circular.m_pData, nullptr);
}
