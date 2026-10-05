#include <engine/client/qm_font_resource_policy.h>

#include <gtest/gtest.h>

TEST(QmFontResourcePolicy, RejectsOnlyLegacyBundledIconSubtree)
{
	EXPECT_TRUE(IsLegacyBundledIconFontPath("qmclient/fonts/Phosphor/Phosphor-Regular.ttf"));
	EXPECT_TRUE(IsLegacyBundledIconFontPath("QMCLIENT/FONTS/PHOSPHOR/Phosphor-Bold.ttf"));

	EXPECT_FALSE(IsLegacyBundledIconFontPath(nullptr));
	EXPECT_FALSE(IsLegacyBundledIconFontPath("qmclient/fonts/Phosphor"));
	EXPECT_FALSE(IsLegacyBundledIconFontPath("qmclient/fonts/Phosphor-old/old.ttf"));
	EXPECT_FALSE(IsLegacyBundledIconFontPath("fonts/Phosphor/Phosphor-Regular.ttf"));
}

TEST(QmFontResourcePolicy, ResolvesIconStyleToConfiguredThenCurrentThenRegular)
{
	int Regular = 1;
	int Current = 2;
	int Candidate = 3;

	EXPECT_EQ(ResolveFontFaceWithFallback(&Candidate, &Current, &Regular), &Candidate);
	EXPECT_EQ(ResolveFontFaceWithFallback<int *>(nullptr, &Current, &Regular), &Current);
	EXPECT_EQ(ResolveFontFaceWithFallback<int *>(nullptr, nullptr, &Regular), &Regular);
}
