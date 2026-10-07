#include <engine/client/qm_font_resource_policy.h>

#include <gtest/gtest.h>

TEST(QmFontResourcePolicy, ResolvesIconStyleToConfiguredThenCurrentThenRegular)
{
	int Regular = 1;
	int Current = 2;
	int Candidate = 3;

	EXPECT_EQ(ResolveFontFaceWithFallback(&Candidate, &Current, &Regular), &Candidate);
	EXPECT_EQ(ResolveFontFaceWithFallback<int *>(nullptr, &Current, &Regular), &Current);
	EXPECT_EQ(ResolveFontFaceWithFallback<int *>(nullptr, nullptr, &Regular), &Regular);
}
