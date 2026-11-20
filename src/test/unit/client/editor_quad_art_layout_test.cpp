#include <game/editor/quadart.h>

#include <gtest/gtest.h>

TEST(EditorQuadArtLayout, PartialRowsAndColumnsAreIncludedInTheWarningCount)
{
	CImageInfo Image;
	Image.m_Width = 633;
	Image.m_Height = 633;
	CQuadArtParameters Parameters{};
	Parameters.m_ImagePixelSize = 2;
	Parameters.m_QuadPixelSize = 4;

	EXPECT_EQ(Parameters.MaximumQuadCount(Image), 100489u);
}

TEST(EditorQuadArtLayout, ImageSmallerThanTheStepStillGeneratesOneCell)
{
	CImageInfo Image;
	Image.m_Width = 1;
	Image.m_Height = 2;
	CQuadArtParameters Parameters{};
	Parameters.m_ImagePixelSize = 3;
	Parameters.m_QuadPixelSize = 4;

	EXPECT_EQ(Parameters.MaximumQuadCount(Image), 1u);
}

TEST(EditorQuadArtLayout, RejectsUnrepresentableQuadCoordinatesOnEitherAxis)
{
	CImageInfo Image;
	Image.m_Width = 2047;
	Image.m_Height = 2047;
	CQuadArtParameters Parameters{};
	Parameters.m_ImagePixelSize = 1;
	Parameters.m_QuadPixelSize = 1024;

	EXPECT_EQ(Parameters.MaximumQuadCount(Image), 4190209u);
	Image.m_Width = 2048;
	EXPECT_FALSE(Parameters.MaximumQuadCount(Image).has_value());
	Image.m_Width = 2047;
	Image.m_Height = 2048;
	EXPECT_FALSE(Parameters.MaximumQuadCount(Image).has_value());
}

TEST(EditorQuadArtLayout, PartialRightEdgeDoesNotMarkTheNextRowsFirstCell)
{
	CImageInfo Image;
	Image.m_Width = 5;
	Image.m_Height = 3;
	std::vector<bool> vVisited(15, false);

	quad_art::MarkVisitedPixels(vVisited, Image, {4, 0}, {2, 2});

	EXPECT_TRUE(vVisited[4]);
	EXPECT_TRUE(vVisited[9]);
	EXPECT_FALSE(vVisited[10]);
}
