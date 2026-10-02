#include <game/client/components/nameplate_text_effects.h>

#include <gtest/gtest.h>

TEST(QmNameplateTextEffects, DisabledEffectsReserveNoPadding)
{
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(0, 4, 12), 0.0f);
}

TEST(QmNameplateTextEffects, BorderPaddingUsesItsClampedRenderedExtent)
{
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 0, 12), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 1, 12), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 8, 12), 4.0f);
}

TEST(QmNameplateTextEffects, GlowPaddingUsesItsClampedRenderedExtent)
{
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 0), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 7), 7.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 20), 12.0f);
}

TEST(QmNameplateTextEffects, CombinedEffectsReserveTheLargerExtent)
{
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER | QM_TEXT_EFFECT_GLOW, 3, 7), 7.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER | QM_TEXT_EFFECT_GLOW, 4, 2), 4.0f);
}
