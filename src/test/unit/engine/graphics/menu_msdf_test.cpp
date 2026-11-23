#include <gtest/gtest.h>

#include <engine/client/qm_menu_msdf.h>

TEST(MenuMsdf, DistinguishesWhitespaceFromUncoveredCharacters)
{
	const auto *pSpace = qm_menu_msdf::Find(0, ' ');
	ASSERT_NE(pSpace, nullptr);
	EXPECT_EQ(pSpace->m_Width, 0);
	EXPECT_EQ(qm_menu_msdf::Find(0, 0x4e2d), nullptr);
	EXPECT_NE(qm_menu_msdf::Find(1, 0x4e2d), nullptr);
}

TEST(MenuMsdf, KeepsOneAtlasEntryWhenScalingTheQuadAboutTheBaseline)
{
	const auto *pGlyph = qm_menu_msdf::Find(0, 'G');
	ASSERT_NE(pGlyph, nullptr);
	const vec2 Baseline(120.0f, 80.0f);
	const vec4 Small = qm_menu_msdf::Quad(*pGlyph, Baseline, 16.0f);
	const vec4 Large = qm_menu_msdf::Quad(*pGlyph, Baseline, 32.0f);
	EXPECT_GT(Small.z, 0.0f);
	EXPECT_GT(Small.w, 0.0f);
	EXPECT_FLOAT_EQ(2.0f * (Small.x - Baseline.x), Large.x - Baseline.x);
	EXPECT_FLOAT_EQ(2.0f * (Small.y - Baseline.y), Large.y - Baseline.y);
	EXPECT_FLOAT_EQ(2.0f * Small.z, Large.z);
	EXPECT_FLOAT_EQ(2.0f * Small.w, Large.w);
}
