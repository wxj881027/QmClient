#include <game/client/components/qmclient/video_preset_labels.h>

#include <gtest/gtest.h>

// 上游 libx264 固定 preset 名称序列：0=ultrafast … 9=placebo。
TEST(QmVideoPresetLabels, PresetNamesMatchUpstreamSequence)
{
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(0), "ultrafast");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(1), "superfast");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(2), "veryfast");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(3), "faster");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(4), "fast");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(5), "medium");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(6), "slow");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(7), "slower");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(8), "veryslow");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(9), "placebo");
}

// 越界输入钳制到合法区间，不越界访问。
TEST(QmVideoPresetLabels, PresetNameClampsOutOfRange)
{
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(-1), "ultrafast");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(-100), "ultrafast");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(10), "placebo");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetName(1000), "placebo");
}

// 提示键未加载语言库时回退英文源键；推荐档 medium 说明含推荐标记，placebo 标注仅供测试。
TEST(QmVideoPresetLabels, PresetHintKeys)
{
	EXPECT_STREQ(qm_video_encode::QmVideoPresetHint(5), "Balanced (recommended)");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetHint(9), "Extremely slow, for testing only");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetHint(0), "Fastest encode, largest files");
	// 越界输入钳制后回退到合法键，不为空指针。
	EXPECT_STREQ(qm_video_encode::QmVideoPresetHint(-1), "Fastest encode, largest files");
	EXPECT_STREQ(qm_video_encode::QmVideoPresetHint(10), "Extremely slow, for testing only");
}

// CRF 默认 18 落在高（推荐）档位；档位区间 0-15 / 16-23 / 24-31 / 32+ 正确分界。
TEST(QmVideoPresetLabels, CrfDefaultFallsIntoRecommendedTier)
{
	EXPECT_EQ(qm_video_encode::CRF_DEFAULT, 18);
	EXPECT_EQ(qm_video_encode::PRESET_RECOMMENDED, 5);

	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(0), "Very high");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(15), "Very high");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(16), "High (recommended)");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(qm_video_encode::CRF_DEFAULT), "High (recommended)");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(23), "High (recommended)");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(24), "Medium");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(31), "Medium");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(32), "Low");
	EXPECT_STREQ(qm_video_encode::QmVideoCrfTierLabel(51), "Low");
}
