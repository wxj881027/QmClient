#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>

TEST(QmIconColorMigration, LegacyRgbGetsDefaultAlphaAndRepeatedMigrationPreservesColor)
{
	unsigned Color = 0x005A6B7Cu;
	EXPECT_TRUE(MigrateLegacyQmUiIconDuotoneSecondaryColor(Color, 0x80FFFFFFu));
	EXPECT_EQ(Color, 0x805A6B7Cu);
	EXPECT_FALSE(MigrateLegacyQmUiIconDuotoneSecondaryColor(Color, 0xFFFFFFFFu));
	EXPECT_EQ(Color, 0x805A6B7Cu);
}

TEST(QmIconColorMigration, ExplicitTransparentColorIsPreserved)
{
	unsigned Color = 0x005A6B7Cu;
	EXPECT_FALSE(MigrateLegacyQmUiIconDuotoneSecondaryColor(Color, 0xFFFFFFFFu, EColorInputAlphaMode::EXPLICIT));
	EXPECT_EQ(Color, 0x005A6B7Cu);
}

TEST(QmIconColorMigration, PackedPartialAlphaIsPreserved)
{
	unsigned Color = 0x805A6B7Cu;
	EXPECT_FALSE(MigrateLegacyQmUiIconDuotoneSecondaryColor(Color, 0xFFFFFFFFu));
	EXPECT_EQ(Color, 0x805A6B7Cu);
}

TEST(QmIconColorMigration, SignedPackedInputUsesDefaultAlpha)
{
	unsigned Color = 0xFFFFFFFFu;
	EXPECT_TRUE(MigrateLegacyQmUiIconDuotoneSecondaryColor(Color, 0x80FFFFFFu, EColorInputAlphaMode::SIGNED_PACKED));
	EXPECT_EQ(Color, 0x80FFFFFFu);
}
