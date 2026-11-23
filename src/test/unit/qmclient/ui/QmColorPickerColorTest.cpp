#include <game/client/QmUi/ColorPickerColor.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
	void ExpectRgbaNear(const ColorRGBA &Actual, const ColorRGBA &Expected, float Tolerance = 1.0e-4f)
	{
		EXPECT_NEAR(Actual.r, Expected.r, Tolerance);
		EXPECT_NEAR(Actual.g, Expected.g, Tolerance);
		EXPECT_NEAR(Actual.b, Expected.b, Tolerance);
		EXPECT_NEAR(Actual.a, Expected.a, Tolerance);
	}
}

TEST(QmColorPickerColor, OklchRoundTripsSrgbSamples)
{
	for(const ColorRGBA Color : {
		    ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f),
		    ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f),
		    ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f),
		    ColorRGBA(0.12f, 0.56f, 0.83f, 0.4f),
	    })
	{
		SCOPED_TRACE(testing::Message() << Color.r << "," << Color.g << "," << Color.b);
		const qm_picker_color::SColorOKLCH Oklch = qm_picker_color::FromRgba(Color);
		ExpectRgbaNear(qm_picker_color::ToRgba(Oklch), Color, 2.0e-4f);
	}
}

TEST(QmColorPickerColor, OklchMapsOutOfGamutChromaWithoutChangingLightnessOrHue)
{
	const qm_picker_color::SColorOKLCH Requested{0.70f, 0.40f, 0.12f, 1.0f};
	ASSERT_FALSE(qm_picker_color::IsInSrgbGamut(Requested));

	const ColorRGBA Mapped = qm_picker_color::ToRgba(Requested);
	const qm_picker_color::SColorOKLCH Actual = qm_picker_color::FromRgba(Mapped);
	EXPECT_NEAR(Actual.m_L, Requested.m_L, 0.01f);
	EXPECT_NEAR(Actual.m_H, Requested.m_H, 0.01f);
	EXPECT_LT(Actual.m_C, Requested.m_C);
	EXPECT_TRUE(qm_picker_color::IsInSrgbGamut(Actual));
}

TEST(QmColorPickerColor, HsvTracksUseTheCurrentValueAndSaturation)
{
	const ColorHSVA Hsv(0.25f, 0.5f, 0.4f, 1.0f);
	const ColorRGBA Rgb = color_cast<ColorRGBA>(Hsv);
	const ColorHSLA Hsl = color_cast<ColorHSLA>(Hsv);
	const qm_picker_color::SColorOKLCH Oklch = qm_picker_color::FromRgba(Rgb);

	// 色相轨必须是 6 个 60° 段：段内 sRGB 线性插值与 HSV 色相完全一致，
	// 首尾同色让整圈首尾相接。
	const auto Hue = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::HSVA, 0, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Hue.m_NumStops, 7);
	ExpectRgbaNear(Hue.m_aStops.front(), ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	ExpectRgbaNear(Hue.m_aStops[Hue.m_NumStops - 1], ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));

	const auto Saturation = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::HSVA, 1, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Saturation.m_NumStops, 2);
	ExpectRgbaNear(Saturation.m_aStops[0], color_cast<ColorRGBA>(ColorHSVA(Hsv.h, 0.0f, Hsv.v, 1.0f)));
	ExpectRgbaNear(Saturation.m_aStops[1], color_cast<ColorRGBA>(ColorHSVA(Hsv.h, 1.0f, Hsv.v, 1.0f)));

	const auto Value = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::HSVA, 2, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Value.m_NumStops, 2);
	ExpectRgbaNear(Value.m_aStops[0], ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
	ExpectRgbaNear(Value.m_aStops[1], color_cast<ColorRGBA>(ColorHSVA(Hsv.h, Hsv.s, 1.0f, 1.0f)));
}

TEST(QmColorPickerColor, RgbTracksKeepTheOtherChannels)
{
	const ColorRGBA Rgb(0.3f, 0.2f, 0.8f, 1.0f);
	const ColorHSLA Hsl = color_cast<ColorHSLA>(Rgb);
	const ColorHSVA Hsv = color_cast<ColorHSVA>(Hsl);
	const qm_picker_color::SColorOKLCH Oklch = qm_picker_color::FromRgba(Rgb);

	const auto Red = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::RGBA, 0, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Red.m_NumStops, 2);
	ExpectRgbaNear(Red.m_aStops[0], ColorRGBA(0.0f, Rgb.g, Rgb.b, 1.0f));
	ExpectRgbaNear(Red.m_aStops[1], ColorRGBA(1.0f, Rgb.g, Rgb.b, 1.0f));

	const auto Blue = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::RGBA, 2, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Blue.m_NumStops, 2);
	ExpectRgbaNear(Blue.m_aStops[0], ColorRGBA(Rgb.r, Rgb.g, 0.0f, 1.0f));
	ExpectRgbaNear(Blue.m_aStops[1], ColorRGBA(Rgb.r, Rgb.g, 1.0f, 1.0f));
}

TEST(QmColorPickerColor, HslaLightnessTrackKeepsTheSaturatedMiddleStop)
{
	const ColorHSLA Hsl(0.0f, 1.0f, 0.35f, 1.0f);
	const ColorRGBA Rgb = color_cast<ColorRGBA>(Hsl);
	const ColorHSVA Hsv = color_cast<ColorHSVA>(Hsl);
	const qm_picker_color::SColorOKLCH Oklch = qm_picker_color::FromRgba(Rgb);

	const auto Lightness = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::HSLA, 2, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Lightness.m_NumStops, 3);
	ExpectRgbaNear(Lightness.m_aStops[0], ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
	ExpectRgbaNear(Lightness.m_aStops[1], ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	ExpectRgbaNear(Lightness.m_aStops[2], ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

TEST(QmColorPickerColor, HslaTracksApplyTheSkinsLightnessClamp)
{
	const ColorHSLA Hsl(0.0f, 1.0f, 0.5f, 1.0f);
	const ColorRGBA Rgb = color_cast<ColorRGBA>(Hsl);
	const ColorHSVA Hsv = color_cast<ColorHSVA>(Hsl);
	const qm_picker_color::SColorOKLCH Oklch = qm_picker_color::FromRgba(Rgb);
	const qm_picker_color::STrackOptions Options{0.5f, true};

	const auto Saturation = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::HSLA, 1, Hsv, Rgb, Hsl, Oklch, Options);
	ASSERT_EQ(Saturation.m_NumStops, 2);
	ExpectRgbaNear(Saturation.m_aStops[0], color_cast<ColorRGBA>(ColorHSLA(0.0f, 0.0f, 0.75f, 1.0f)));
	ExpectRgbaNear(Saturation.m_aStops[1], color_cast<ColorRGBA>(ColorHSLA(0.0f, 1.0f, 0.75f, 1.0f)));
}

TEST(QmColorPickerColor, OklchChannelTracksUseTheirOwnAxis)
{
	const qm_picker_color::SColorOKLCH Oklch{0.7f, 0.1f, 0.3f, 1.0f};
	const ColorRGBA Rgb = qm_picker_color::ToRgba(Oklch);
	const ColorHSLA Hsl = color_cast<ColorHSLA>(Rgb);
	const ColorHSVA Hsv = color_cast<ColorHSVA>(Hsl);

	const auto Lightness = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::OKLCH, 0, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Lightness.m_NumStops, qm_picker_color::TRACK_MAX_STOPS);
	ExpectRgbaNear(Lightness.m_aStops.front(), ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
	ExpectRgbaNear(Lightness.m_aStops[Lightness.m_NumStops - 1], ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	const auto Chroma = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::OKLCH, 1, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Chroma.m_NumStops, qm_picker_color::TRACK_MAX_STOPS);
	const float ChromaTrackLightness = std::max(Oklch.m_L, qm_picker_color::OKLCH_CHROMA_TRACK_MIN_LIGHTNESS);
	ExpectRgbaNear(Chroma.m_aStops.front(), qm_picker_color::ToRgba({ChromaTrackLightness, 0.0f, Oklch.m_H, 1.0f}));
	ExpectRgbaNear(Chroma.m_aStops[Chroma.m_NumStops - 1], qm_picker_color::ToRgba({ChromaTrackLightness, qm_picker_color::OKLCH_MAX_CHROMA, Oklch.m_H, 1.0f}));

	const auto Hue = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::OKLCH, 2, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Hue.m_NumStops, qm_picker_color::TRACK_MAX_STOPS);
	ExpectRgbaNear(Hue.m_aStops.front(), qm_picker_color::ToRgba({qm_picker_color::OKLCH_HUE_REFERENCE_LIGHTNESS, qm_picker_color::OKLCH_HUE_REFERENCE_CHROMA, 0.0f, 1.0f}));
	ExpectRgbaNear(Hue.m_aStops[Hue.m_NumStops - 1], qm_picker_color::ToRgba({qm_picker_color::OKLCH_HUE_REFERENCE_LIGHTNESS, qm_picker_color::OKLCH_HUE_REFERENCE_CHROMA, 1.0f, 1.0f}));
}

TEST(QmColorPickerColor, OklchHueAndChromaTracksStayVisibleAtBlack)
{
	// 当前颜色是纯黑时，H 轨仍必须是完整色相谱，C 轨仍必须是灰度→彩色；
	// 只有 L 轨可以为黑，否则三个滑条会一起消失。
	const qm_picker_color::SColorOKLCH Oklch{0.0f, 0.0f, 0.0f, 1.0f};
	const ColorRGBA Rgb = qm_picker_color::ToRgba(Oklch);
	const ColorHSLA Hsl = color_cast<ColorHSLA>(Rgb);
	const ColorHSVA Hsv = color_cast<ColorHSVA>(Hsl);
	ExpectRgbaNear(Rgb, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));

	for(const int Channel : {1, 2})
	{
		const auto Track = qm_picker_color::ResolveChannelTrack(
			qm_picker_color::ETrackSpace::OKLCH, Channel, Hsv, Rgb, Hsl, Oklch);
		ASSERT_EQ(Track.m_NumStops, qm_picker_color::TRACK_MAX_STOPS);
		for(int Index = 0; Index < Track.m_NumStops; Index++)
		{
			const ColorRGBA &Stop = Track.m_aStops[Index];
			const float BrightestChannel = std::max({Stop.r, Stop.g, Stop.b});
			EXPECT_GT(BrightestChannel, 0.1f) << "channel " << Channel << " stop " << Index;
		}
	}
}

TEST(QmColorPickerColor, OklchChannelTracksStayOpaqueAtTheCurrentAlpha)
{
	const qm_picker_color::SColorOKLCH Oklch{0.62f, 0.16f, 0.05f, 0.32f};
	const ColorRGBA Rgb = qm_picker_color::ToRgba(Oklch);
	const ColorHSLA Hsl = color_cast<ColorHSLA>(Rgb);
	const ColorHSVA Hsv = color_cast<ColorHSVA>(Hsl);
	EXPECT_NEAR(Rgb.a, 0.32f, 1.0e-6f);

	// L/C/H 轨必须是不透明色轴：当前 alpha 只允许由 A 轨表达，
	// 否则轨道会透出弹窗底色，黑色也会显示成脏灰条。
	for(int Channel = 0; Channel < 3; Channel++)
	{
		const auto Track = qm_picker_color::ResolveChannelTrack(
			qm_picker_color::ETrackSpace::OKLCH, Channel, Hsv, Rgb, Hsl, Oklch);
		ASSERT_GT(Track.m_NumStops, 1);
		for(int Index = 0; Index < Track.m_NumStops; Index++)
			EXPECT_FLOAT_EQ(Track.m_aStops[Index].a, 1.0f) << "channel " << Channel << " stop " << Index;
	}

	const auto Alpha = qm_picker_color::ResolveChannelTrack(
		qm_picker_color::ETrackSpace::OKLCH, 3, Hsv, Rgb, Hsl, Oklch);
	ASSERT_EQ(Alpha.m_NumStops, 2);
	EXPECT_FLOAT_EQ(Alpha.m_aStops[0].a, 0.0f);
	EXPECT_FLOAT_EQ(Alpha.m_aStops[1].a, 1.0f);
}
