#include <engine/client/backend/metal/metal_types.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

TEST(MetalTypes, UniformsMatchTheMSLBufferLayout)
{
	EXPECT_EQ(alignof(SMetalUniforms), 16U);
	EXPECT_EQ(sizeof(SMetalFloat4x4), 64U);
	EXPECT_EQ(sizeof(SMetalFloat4), 16U);
	EXPECT_EQ(sizeof(SMetalUniforms), 80U);
	EXPECT_EQ(offsetof(SMetalUniforms, m_MVP), 0U);
	EXPECT_EQ(offsetof(SMetalUniforms, m_Color), 64U);
	EXPECT_EQ(alignof(SMetalTextUniforms), 16U);
	EXPECT_EQ(sizeof(SMetalTextUniforms), 112U);
	EXPECT_EQ(offsetof(SMetalTextUniforms, m_OutlineColor), 80U);
	EXPECT_EQ(offsetof(SMetalTextUniforms, m_Params), 96U);
	EXPECT_EQ(sizeof(SMetalTileUniforms), 96U);
	EXPECT_EQ(offsetof(SMetalTileUniforms, m_Transform), 80U);
	EXPECT_EQ(offsetof(SMetalQuadUniforms, m_QuadOffset), 64U + METAL_MAX_QUADS * 32U);
	EXPECT_EQ(sizeof(SMetalQuadContainerUniforms), 96U);
	EXPECT_EQ(offsetof(SMetalQuadContainerUniforms, m_CenterRotation), 64U);
	EXPECT_EQ(offsetof(SMetalQuadContainerUniforms, m_VertexColor), 80U);
	EXPECT_EQ(sizeof(SMetalSpriteMultipleUniforms), 64U + 16U + 16U + METAL_MAX_SPRITES * 16U);
	EXPECT_EQ(offsetof(SMetalSpriteMultipleUniforms, m_aRenderInfo), 96U);
}

TEST(MetalTypes, ComputesRgbaAndTextLayouts)
{
	SMetalTextureLayout Layout;
	ASSERT_TRUE(MetalTextureLayout(3, 5, EMetalTextureFormat::RGBA8, true, Layout));
	EXPECT_EQ(Layout.m_BytesPerPixel, 4U);
	EXPECT_EQ(Layout.m_RowBytes, 12U);
	EXPECT_EQ(Layout.m_DataBytes, 60U);
	EXPECT_EQ(Layout.m_MipLevels, 1U);

	ASSERT_TRUE(MetalTextureLayout(8, 4, EMetalTextureFormat::R8, false, Layout));
	EXPECT_EQ(Layout.m_RowBytes, 8U);
	EXPECT_EQ(Layout.m_DataBytes, 8U * 4U + 4U * 2U + 2U * 1U + 1U * 1U);
	EXPECT_EQ(Layout.m_MipLevels, 4U);

	size_t LayerWidth = 0;
	size_t LayerHeight = 0;
	ASSERT_TRUE(MetalTextureArrayLayout(64, 32, EMetalTextureFormat::RGBA8, true, LayerWidth, LayerHeight, Layout));
	EXPECT_EQ(LayerWidth, 4U);
	EXPECT_EQ(LayerHeight, 2U);
	EXPECT_EQ(Layout.m_DataBytes, 4U * 2U * 4U * METAL_TEXTURE_ARRAY_LAYERS);
}

TEST(MetalTypes, RejectsTextureSizeOverflow)
{
	SMetalTextureLayout Layout;
	EXPECT_FALSE(MetalTextureLayout(std::numeric_limits<size_t>::max(), 2, EMetalTextureFormat::RGBA8, true, Layout));
	EXPECT_EQ(Layout.m_DataBytes, 0U);
	size_t LayerWidth = 0;
	size_t LayerHeight = 0;
	EXPECT_FALSE(MetalTextureArrayLayout(15, 16, EMetalTextureFormat::RGBA8, true, LayerWidth, LayerHeight, Layout));
	EXPECT_EQ(LayerWidth, 0U);
	EXPECT_EQ(LayerHeight, 0U);
}

TEST(MetalTypes, SingleChannelUnmippedTextureAcceptsLargestRepresentableRow)
{
	SMetalTextureLayout Layout;
	const size_t Width = std::numeric_limits<size_t>::max();
	ASSERT_TRUE(MetalTextureLayout(Width, 1, EMetalTextureFormat::R8, true, Layout));
	EXPECT_EQ(Layout.m_RowBytes, Width);
	EXPECT_EQ(Layout.m_DataBytes, Width);
	EXPECT_EQ(Layout.m_MipLevels, 1U);
}

TEST(MetalTypes, MipTotalOverflowClearsPartialLayout)
{
	SMetalTextureLayout Layout{4, 8, 64, 4};
	const size_t Width = std::numeric_limits<size_t>::max() / 2 + 2;
	EXPECT_FALSE(MetalTextureLayout(Width, 1, EMetalTextureFormat::R8, false, Layout));
	EXPECT_EQ(Layout.m_BytesPerPixel, 0U);
	EXPECT_EQ(Layout.m_RowBytes, 0U);
	EXPECT_EQ(Layout.m_DataBytes, 0U);
	EXPECT_EQ(Layout.m_MipLevels, 0U);
}

TEST(MetalTypes, ArrayTotalOverflowClearsDimensionsAndLayout)
{
	SMetalTextureLayout Layout{4, 8, 64, 4};
	size_t LayerWidth = 123;
	size_t LayerHeight = 456;
	const size_t SourceWidth = std::numeric_limits<size_t>::max() / 16 * 16;
	EXPECT_FALSE(MetalTextureArrayLayout(SourceWidth, 16, EMetalTextureFormat::R8, true, LayerWidth, LayerHeight, Layout));
	EXPECT_EQ(LayerWidth, 0U);
	EXPECT_EQ(LayerHeight, 0U);
	EXPECT_EQ(Layout.m_BytesPerPixel, 0U);
	EXPECT_EQ(Layout.m_RowBytes, 0U);
	EXPECT_EQ(Layout.m_DataBytes, 0U);
	EXPECT_EQ(Layout.m_MipLevels, 0U);
}

TEST(MetalTypes, ValidatesSubregionsWithoutOverflow)
{
	EXPECT_TRUE(MetalValidateSubregion(16, 8, 0, 0, 16, 8));
	EXPECT_TRUE(MetalValidateSubregion(16, 8, 4, 2, 12, 6));
	EXPECT_FALSE(MetalValidateSubregion(16, 8, 5, 0, 12, 8));
	EXPECT_FALSE(MetalValidateSubregion(16, 8, 0, 0, 0, 1));
	EXPECT_FALSE(MetalValidateSubregion(16, 8, std::numeric_limits<size_t>::max(), 0, 1, 1));
}

TEST(MetalTypes, ValidatesBufferLayoutsAndRanges)
{
	SMetalBufferLayout Layout;
	ASSERT_TRUE(MetalBufferLayout(128, false, Layout));
	EXPECT_EQ(Layout.m_DataBytes, 128U);
	EXPECT_FALSE(Layout.m_OneTimeUse);
	ASSERT_TRUE(MetalBufferLayout(64, true, Layout));
	EXPECT_TRUE(Layout.m_OneTimeUse);
	EXPECT_FALSE(MetalBufferLayout(0, false, Layout));

	EXPECT_TRUE(MetalValidateBufferRange(128, 0, 128));
	EXPECT_TRUE(MetalValidateBufferRange(128, 64, 64));
	EXPECT_FALSE(MetalValidateBufferRange(128, 129, 1));
	EXPECT_FALSE(MetalValidateBufferRange(128, 127, 2));
	EXPECT_FALSE(MetalValidateBufferRange(128, 0, 0));
}

TEST(MetalTypes, ValidatesVertexAttributeLayoutAgainstStride)
{
	const SMetalVertexAttribute Position{2, METAL_GRAPHICS_TYPE_FLOAT, false, 0, 0};
	EXPECT_TRUE(MetalValidateVertexAttribute(16, Position));
	const SMetalVertexAttribute Color{4, METAL_GRAPHICS_TYPE_UNSIGNED_BYTE, true, 8, 1};
	EXPECT_TRUE(MetalValidateVertexAttribute(16, Color));
	EXPECT_FALSE(MetalValidateVertexAttribute(16, SMetalVertexAttribute{3, METAL_GRAPHICS_TYPE_FLOAT, false, 8, 0}));
	EXPECT_FALSE(MetalValidateVertexAttribute(16, SMetalVertexAttribute{1, 0, false, 0, 0}));
	EXPECT_FALSE(MetalValidateVertexAttribute(16, SMetalVertexAttribute{1, METAL_GRAPHICS_TYPE_FLOAT, false, 0, 2}));
	EXPECT_TRUE(MetalVertexAttributeEquals(Position, 2, METAL_GRAPHICS_TYPE_FLOAT, false, 0, 0));
	EXPECT_FALSE(MetalVertexAttributeEquals(Position, 2, METAL_GRAPHICS_TYPE_FLOAT, true, 0, 0));
}

TEST(MetalTypes, MapsBlendModesToOpenGLCompatibleFactors)
{
	EXPECT_FALSE(MetalBlendState(EMetalBlendMode::NONE).m_Enabled);
	const SMetalBlendState Alpha = MetalBlendState(EMetalBlendMode::ALPHA);
	EXPECT_TRUE(Alpha.m_Enabled);
	EXPECT_EQ(Alpha.m_Source, EMetalBlendFactor::SOURCE_ALPHA);
	EXPECT_EQ(Alpha.m_Destination, EMetalBlendFactor::ONE_MINUS_SOURCE_ALPHA);
	const SMetalBlendState Additive = MetalBlendState(EMetalBlendMode::ADDITIVE);
	EXPECT_TRUE(Additive.m_Enabled);
	EXPECT_EQ(Additive.m_Destination, EMetalBlendFactor::ONE);
}

TEST(MetalTypes, ChoosesOnlySupportedMultisampleCounts)
{
	EXPECT_EQ(MetalSelectSampleCount(0, true, true, true), 0U);
	EXPECT_EQ(MetalSelectSampleCount(3, true, true, true), 2U);
	EXPECT_EQ(MetalSelectSampleCount(4, true, true, false), 4U);
	EXPECT_EQ(MetalSelectSampleCount(8, true, false, false), 2U);
	EXPECT_EQ(MetalSelectSampleCount(8, false, false, false), 0U);
}

TEST(MetalTypes, GaussianBlurUniformsUseFourAlignedFloat4s)
{
	EXPECT_EQ(sizeof(SMetalGaussianBlurUniforms), 64U);
	EXPECT_EQ(alignof(SMetalGaussianBlurUniforms), 16U);
	EXPECT_EQ(offsetof(SMetalGaussianBlurUniforms, m_TexelOffsetRadius), 0U);
	EXPECT_EQ(offsetof(SMetalGaussianBlurUniforms, m_Weights0), 16U);
}

TEST(MetalTypes, PrimitiveVertexCountsAreChecked)
{
	size_t Count = 0;
	EXPECT_TRUE(MetalPrimitiveVertexCount(EMetalPrimitiveType::QUADS, 3, Count));
	EXPECT_EQ(Count, 12U);
	EXPECT_TRUE(MetalPrimitiveVertexCount(EMetalPrimitiveType::LINES, 4, Count));
	EXPECT_EQ(Count, 8U);
	EXPECT_FALSE(MetalPrimitiveVertexCount(EMetalPrimitiveType::TRIANGLES, std::numeric_limits<size_t>::max(), Count));
}
