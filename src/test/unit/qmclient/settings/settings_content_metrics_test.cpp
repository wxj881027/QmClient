// 默认控件尺寸与显式输入下的卡片高度行为；不把主题默认值固化为高度函数的输入。
#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsPageLayout.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <utility>

namespace
{
	// 行高是调用方输入：保留既有 16/20 尺寸用例，默认主题统一高度由独立用例验证。
	SSettingsContentMetrics MetricsWithExplicitControlHeight(float Width, float Height)
	{
		SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Width);
		Metrics.m_LineHeight = Height;
		Metrics.m_InputHeight = Height;
		Metrics.m_ButtonHeight = Height;
		Metrics.m_RowStep = Height + Metrics.m_LineSpacing;
		return Metrics;
	}
}

TEST(SettingsContentMetrics, ContentMetricsReserveControlWidthWithoutPageMagicNumbers)
{
	const SSettingsContentMetrics Narrow = ResolveSettingsContentMetrics(420.0f);
	const SSettingsContentMetrics Wide = ResolveSettingsContentMetrics(900.0f);
	EXPECT_GE(420.0f - Narrow.m_LabelWidth, 160.0f * Narrow.m_UiScale);
	EXPECT_GE(900.0f - Wide.m_LabelWidth, 160.0f * Wide.m_UiScale);
	EXPECT_LT(Narrow.m_LabelWidth, Wide.m_LabelWidth);
}

TEST(SettingsContentMetrics, ContentMetricsShareOneResponsiveScaleContract)
{
	const SSettingsContentMetrics Narrow = ResolveSettingsContentMetrics(640.0f);
	const SSettingsContentMetrics Standard = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(Narrow.m_BodySize, 10.0f);
	EXPECT_FLOAT_EQ(Narrow.m_LineHeight, 16.0f);
	EXPECT_LT(Narrow.m_LineHeight, Standard.m_LineHeight);
	EXPECT_FLOAT_EQ(Narrow.m_LineSpacing, 3.9f);
	EXPECT_FLOAT_EQ(Narrow.m_CardGap, ui_token::settings::CARD_GAP * Narrow.m_UiScale);
	EXPECT_FLOAT_EQ(Standard.m_BodySize, ui_token::font::BODY);
	EXPECT_FLOAT_EQ(Standard.m_LineHeight, ui_token::settings::ROW_HEIGHT);
	EXPECT_FLOAT_EQ(Standard.m_LineSpacing, ui_token::settings::ROW_GAP);
	EXPECT_FLOAT_EQ(Standard.m_CardGap, ui_token::settings::CARD_GAP);
	EXPECT_FLOAT_EQ(Narrow.m_SmallSize, ResolveSettingsSmallFontSize(Narrow.m_UiScale));
	EXPECT_FLOAT_EQ(Standard.m_SmallSize, ResolveSettingsSmallFontSize(Standard.m_UiScale));
}

TEST(SettingsContentMetrics, CardMetricsUseColumnWidthForTwoColumnControls)
{
	const SSettingsContentMetrics PageMetrics = ResolveSettingsContentMetrics(1600.0f);
	const float CardLabelWidth = ResolveSettingsCardLabelWidth(700.0f, PageMetrics);
	EXPECT_LT(CardLabelWidth, PageMetrics.m_LabelWidth);
	EXPECT_GE(700.0f - CardLabelWidth, 160.0f * PageMetrics.m_UiScale);
}

TEST(SettingsContentMetrics, RowStackDoesNotAddSpacingAfterLastRow)
{
	EXPECT_FLOAT_EQ(ResolveSettingsRowsHeight(6, 20.0f, 5.0f), 145.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsRowsHeight(1, 20.0f, 5.0f), 20.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsRowsHeight(0, 20.0f, 5.0f), 0.0f);
}

TEST(SettingsContentMetrics, ColorRowsUseCanonicalControlHeightAndSpacing)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(640.0f);
	const CUIRect View{10.0f, 20.0f, 360.0f, 200.0f};
	const SSettingsColorRowLayout Layout = ResolveSettingsColorRowLayout(View, Metrics, false);

	EXPECT_FLOAT_EQ(Layout.m_RowRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_LabelRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_ColorButtonRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_ResetButtonRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Layout.m_ConsumedHeight, Metrics.m_ButtonHeight + Metrics.m_LineSpacing);
	EXPECT_LE(Layout.m_LabelRect.x + Layout.m_LabelRect.w, Layout.m_ColorButtonRect.x);
	EXPECT_LE(Layout.m_ColorButtonRect.x + Layout.m_ColorButtonRect.w, Layout.m_ResetButtonRect.x);

	const SSettingsColorRowLayout Indented = ResolveSettingsColorRowLayout(View, Metrics, true);
	EXPECT_GT(Indented.m_LabelRect.x, Layout.m_LabelRect.x);
	EXPECT_FLOAT_EQ(Indented.m_ColorButtonRect.h, Layout.m_ColorButtonRect.h);
	EXPECT_FLOAT_EQ(Indented.m_ResetButtonRect.h, Layout.m_ResetButtonRect.h);

	const CUIRect NarrowView{10.0f, 20.0f, 100.0f, 200.0f};
	const SSettingsColorRowLayout Narrow = ResolveSettingsColorRowLayout(NarrowView, Metrics, false);
	EXPECT_GE(Narrow.m_LabelRect.w, 0.0f);
	EXPECT_GE(Narrow.m_ColorButtonRect.w, 0.0f);
	EXPECT_GE(Narrow.m_ResetButtonRect.w, 0.0f);
	EXPECT_GE(Narrow.m_LabelRect.x, NarrowView.x);
	EXPECT_LE(Narrow.m_ResetButtonRect.x + Narrow.m_ResetButtonRect.w, NarrowView.x + NarrowView.w);

	SSettingsContentMetrics NoBottomSpacing = Metrics;
	NoBottomSpacing.m_LineSpacing = 0.0f;
	const SSettingsColorRowLayout NoBottomSpacingLayout = ResolveSettingsColorRowLayout(View, NoBottomSpacing, false);
	EXPECT_FLOAT_EQ(NoBottomSpacingLayout.m_ConsumedHeight, Metrics.m_ButtonHeight);
	EXPECT_LT(NoBottomSpacingLayout.m_LabelRect.x + NoBottomSpacingLayout.m_LabelRect.w, NoBottomSpacingLayout.m_ColorButtonRect.x);
	EXPECT_LT(NoBottomSpacingLayout.m_ColorButtonRect.x + NoBottomSpacingLayout.m_ColorButtonRect.w, NoBottomSpacingLayout.m_ResetButtonRect.x);

	const SSettingsColorRowLayout LastRow = ResolveSettingsColorRowLayout(View, Metrics, false, false);
	EXPECT_FLOAT_EQ(LastRow.m_ConsumedHeight, Metrics.m_ButtonHeight);
}

TEST(SettingsContentMetrics, RadioRowsUseMetricsAndDropdownWhenNarrow)
{
	const auto Metrics = ResolveSettingsContentMetrics(640.0f);
	const auto Inline = ResolveSettingsRadioRowLayout({0, 0, 600, 100}, 3, Metrics);
	EXPECT_FALSE(Inline.m_UseDropdown);
	EXPECT_FLOAT_EQ(Inline.m_Height, Metrics.m_LineHeight);
	EXPECT_FLOAT_EQ(Inline.m_LabelRect.h, Inline.m_Height);
	EXPECT_FLOAT_EQ(Inline.m_ButtonsRect.h, Inline.m_Height);

	const auto Dropdown = ResolveSettingsRadioRowLayout({0, 0, 140, 100}, 3, Metrics);
	EXPECT_TRUE(Dropdown.m_UseDropdown);
	EXPECT_FLOAT_EQ(Dropdown.m_Height, Inline.m_Height);
	EXPECT_FLOAT_EQ(Dropdown.m_ButtonsRect.y, Dropdown.m_LabelRect.y);
}

TEST(SettingsContentMetrics, AppearanceHeightsRespectExplicitRowMetrics)
{
	const SSettingsContentMetrics Compact = MetricsWithExplicitControlHeight(640.0f, 16.0f);
	EXPECT_FLOAT_EQ(Compact.m_UiScale, 0.78f);
	EXPECT_NEAR(ResolveAppearanceChatMessagesHeight(Compact), 437.8f, 0.001f);
	EXPECT_NEAR(ResolveQmHudCoordsHeight(Compact), 155.3f, 0.001f);
	EXPECT_NEAR(ResolveQmHudNotificationsHeight(Compact, false, false), 95.6f, 0.001f);
	EXPECT_NEAR(ResolveQmHudNotificationsHeight(Compact, true, false), 311.4f, 0.001f);
	EXPECT_NEAR(ResolveQmHudNotificationsHeight(Compact, true, true), 391.0f, 0.001f);
	EXPECT_NEAR(ResolveAppearanceLaserColorsHeight(Compact), 319.8f, 0.001f);
	EXPECT_NEAR(ResolveAppearanceLaserEnhancedHeight(Compact, false), 95.6f, 0.001f);
	EXPECT_NEAR(ResolveAppearanceLaserEnhancedHeight(Compact, true), 135.4f, 0.001f);

	const SSettingsContentMetrics Standard = MetricsWithExplicitControlHeight(1000.0f, 20.0f);
	EXPECT_FLOAT_EQ(Standard.m_UiScale, 1.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceChatMessagesHeight(Standard), 550.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudCoordsHeight(Standard), 195.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Standard, false, false), 120.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Standard, true, false), 390.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Standard, true, true), 490.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceLaserColorsHeight(Standard), 400.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceLaserEnhancedHeight(Standard, false), 120.0f);
	EXPECT_FLOAT_EQ(ResolveAppearanceLaserEnhancedHeight(Standard, true), 170.0f);
}

TEST(SettingsContentMetrics, BothSettingsShellsUseFinalContentWidthMetrics)
{
	const SSettingsContentMetrics LegacyMetrics = ResolveSettingsContentMetrics(640.0f);
	const SSettingsContentMetrics NewMetrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsColorRowLayout({0.0f, 0.0f, 640.0f, 100.0f}, LegacyMetrics, false).m_ConsumedHeight, LegacyMetrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveSettingsColorRowLayout({0.0f, 0.0f, 1000.0f, 100.0f}, NewMetrics, false).m_ConsumedHeight, NewMetrics.m_RowStep);
}

TEST(SettingsContentMetrics, QmHudHeightsRespectExplicitMetricsAndSelectedStates)
{
	const SSettingsContentMetrics Metrics = MetricsWithExplicitControlHeight(1000.0f, 20.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudPlayerStatsHeight(Metrics, false, false), 70.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudPlayerStatsHeight(Metrics, true, true), 145.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudPlayerStatsHeight(Metrics, true, false), 245.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudInputOverlayHeight(Metrics, false), 20.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudInputOverlayHeight(Metrics, true), 300.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Metrics, true, false), 390.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudNotificationsHeight(Metrics, true, true), 490.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudDummyMiniViewHeight(Metrics, false), 25.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudDummyMiniViewHeight(Metrics, true), 121.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, false, false, false, 0, false, false), 20.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, false, false, 0, false, false), 145.0f);
	EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, true, false, 0, false, false), 450.75f);
	EXPECT_NEAR(ResolveQmHudVoiceHeight(Metrics, true, true, true, 1, true, true), 823.7f, 0.001f);
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(Metrics, 600.0f, false, false, false, false, false, false), 20.0f);
}

TEST(SettingsContentMetrics, SettingsPagesShareTheQmScaleBaseline)
{
	EXPECT_FLOAT_EQ(ResolveSettingsUiScale(800.0f), 0.85f);
	EXPECT_FLOAT_EQ(ResolveSettingsUiScale(1000.0f), 1.0f);
	EXPECT_LT(ResolveSettingsUiScale(680.0f), ResolveSettingsUiScale(681.0f));
	EXPECT_NEAR(ResolveSettingsUiScale(679.0f), ResolveSettingsUiScale(680.0f), 0.01f);
}

TEST(SettingsContentMetrics, SingleLineControlsShareTheSameHeightAcrossViewportWidths)
{
	for(const float Width : {0.0f, 240.0f, 600.0f, 1000.0f, 1600.0f})
	{
		SCOPED_TRACE(Width);
		const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Width);
		EXPECT_FLOAT_EQ(Metrics.m_InputHeight, Metrics.m_ButtonHeight);
		EXPECT_FLOAT_EQ(Metrics.m_LineHeight, Metrics.m_ButtonHeight);
		EXPECT_FLOAT_EQ(ResolveSettingsRadioButtonHeight(Metrics), Metrics.m_ButtonHeight);
		EXPECT_GE(Metrics.m_ButtonHeight, ResolveSettingsRadioFontSize(Metrics) + 4.0f);
	}
}

TEST(SettingsContentMetrics, LongOptionTextFallsBackToDropdownAtUnchangedHeight)
{
	const auto Metrics = ResolveSettingsContentMetrics(700.0f);
	const CUIRect View{0, 0, 700, 100};
	const auto Short = ResolveSettingsRadioRowLayout(View, 2, Metrics, 90.0f);
	const auto Long = ResolveSettingsRadioRowLayout(View, 2, Metrics, 400.0f);
	EXPECT_FALSE(Short.m_UseDropdown);
	EXPECT_TRUE(Long.m_UseDropdown);
	EXPECT_FLOAT_EQ(Short.m_Height, Long.m_Height);
	EXPECT_FLOAT_EQ(Long.m_LabelRect.y, Long.m_ButtonsRect.y);
}

TEST(SettingsContentMetrics, RadioLayoutHandlesEmptyAndTinyWidthsWithoutOverflow)
{
	const auto Metrics = ResolveSettingsContentMetrics(240.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsRadioRowLayout({0, 0, 240, 100}, 0, Metrics).m_Height, 0);
	for(const float Width : {0.0f, 1.0f, 20.0f, 80.0f})
	{
		const auto Row = ResolveSettingsRadioRowLayout({10, 20, Width, 100}, 2, Metrics);
		EXPECT_GE(Row.m_ButtonsRect.w, 0);
		EXPECT_LE(Row.m_ButtonsRect.x + Row.m_ButtonsRect.w, 10 + Width);
	}
}

TEST(SettingsContentMetrics, ChoiceWidthCacheInvalidatesOnTextFontAndRestoresAfterEmptyOptions)
{
	CSettingsChoiceWidthCache Cache;
	std::vector<const char *> Labels{"A", "Longest"};
	int Measures = 0;
	const auto Measure = [&](const char *pText, float FontSize) { ++Measures; return std::char_traits<char>::length(pText) * FontSize; };
	EXPECT_FLOAT_EQ(Cache.Resolve(Labels, 12, 1, Measure), 84);
	EXPECT_EQ(Measures, 2);
	Cache.Resolve(Labels, 12, 1, Measure);
	EXPECT_EQ(Measures, 2);
	Labels[1] = "Longer label";
	EXPECT_GT(Cache.Resolve(Labels, 12, 1, Measure), 84);
	EXPECT_EQ(Measures, 4);
	Cache.Resolve(Labels, 13, 1, Measure);
	EXPECT_EQ(Measures, 6);
	Cache.Resolve(Labels, 13, 2, Measure);
	EXPECT_EQ(Measures, 8);
	EXPECT_FLOAT_EQ(Cache.Resolve({}, 13, 2, Measure), 0);
	EXPECT_GT(Cache.Resolve(Labels, 13, 2, Measure), 0);
	EXPECT_EQ(Measures, 10);
}

TEST(SettingsContentMetrics, Background3DHeightRespondsToSharedChoiceHeightAndOptionalControls)
{
	const auto Metrics = MetricsWithExplicitControlHeight(1000, 20);
	const float Baseline = ResolveQmHudBackground3DHeight(Metrics, 600, true, false, false, false, false, false);
	EXPECT_GT(Baseline, ResolveQmHudBackground3DHeight(Metrics, 600, false, false, false, false, false, false));
	auto TallerChoice = Metrics;
	TallerChoice.m_ButtonHeight += 8;
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(TallerChoice, 600, true, false, false, false, false, false) - Baseline, 8);
	// 自定义颜色增加一个颜色控件，辉光增加两项参数；验证可见项真实占用的行数。
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(Metrics, 600, true, true, false, false, false, false) - Baseline, Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmHudBackground3DHeight(Metrics, 600, true, false, true, false, false, false) - Baseline, 2 * Metrics.m_RowStep);
}

TEST(SettingsContentMetrics, SingleLineControlsKeepTheExistingResponsiveBaseline)
{
	const std::array<std::pair<float, float>, 4> Cases{{{240, 16}, {600, 16}, {1000, 20}, {1600, 20}}};
	for(const auto &[Width, ExpectedHeight] : Cases)
	{
		SCOPED_TRACE(Width);
		const auto Metrics = ResolveSettingsContentMetrics(Width);
		EXPECT_FLOAT_EQ(Metrics.m_LineHeight, ExpectedHeight);
		EXPECT_FLOAT_EQ(Metrics.m_ButtonHeight, ExpectedHeight);
		EXPECT_FLOAT_EQ(Metrics.m_InputHeight, ExpectedHeight);
	}
	// 放大字号时仍按实际文字需求扩展，不能为了固定基线裁掉文字。
	EXPECT_GE(ResolveSettingsControlHeight(24), 28);
}

TEST(SettingsContentMetrics, ControlRolesShareLabelColumnAndStayInsideTinyRows)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	for(const float Width : {0.0f, 1.0f, 80.0f, 300.0f, 800.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Row{10, 20, Width, Metrics.m_LineHeight};
		const auto Fill = ResolveSettingsControlRowLayout(Row, Metrics);
		for(const auto Role : {ESettingsControlWidth::NUMBER, ESettingsControlWidth::SHORT_CHOICE, ESettingsControlWidth::LONG_CHOICE, ESettingsControlWidth::ACTION})
		{
			const auto Layout = ResolveSettingsControlRowLayout(Row, Metrics, Role, 200);
			EXPECT_FLOAT_EQ(Layout.m_LabelRect.w, Fill.m_LabelRect.w);
			EXPECT_FLOAT_EQ(Layout.m_ControlRect.x, Fill.m_ControlRect.x);
			EXPECT_FLOAT_EQ(Layout.m_ControlRect.h, Metrics.m_LineHeight);
			EXPECT_GE(Layout.m_ControlRect.w, 0);
			EXPECT_LE(Layout.m_ControlRect.x + Layout.m_ControlRect.w, Row.x + Width);
		}
	}
	const CUIRect Row{0, 0, 800, 20};
	const auto Number = ResolveSettingsControlRowLayout(Row, Metrics, ESettingsControlWidth::NUMBER);
	const auto Short = ResolveSettingsControlRowLayout(Row, Metrics, ESettingsControlWidth::SHORT_CHOICE);
	const auto Long = ResolveSettingsControlRowLayout(Row, Metrics, ESettingsControlWidth::LONG_CHOICE);
	EXPECT_LT(Number.m_ControlRect.w, Short.m_ControlRect.w);
	EXPECT_LT(Short.m_ControlRect.w, Long.m_ControlRect.w);
}

TEST(SettingsContentMetrics, ControlColumnSplitSupportsInPlaceRemainder)
{
	const auto Metrics = ResolveSettingsContentMetrics(600);
	CUIRect Row{10, 20, 600, Metrics.m_LineHeight};
	const auto Expected = ResolveSettingsControlRowLayout(Row, Metrics);
	CUIRect Label;
	SplitSettingsControlRow(Row, Metrics, &Label, &Row);
	EXPECT_FLOAT_EQ(Label.w, Expected.m_LabelRect.w);
	EXPECT_FLOAT_EQ(Row.x, Expected.m_ControlRect.x);
	EXPECT_FLOAT_EQ(Row.w, Expected.m_ControlRect.w);
}

TEST(SettingsContentMetrics, ChoiceKeepsAllOptionsOrUsesDropdownWithoutPartialSegments)
{
	const CUIRect Controls{10, 20, 1600, 20};
	for(int Count = 1; Count <= 20; ++Count)
	{
		SCOPED_TRACE(Count);
		const auto Layout = ResolveSettingsChoiceLayout(Controls, Count, 72);
		EXPECT_EQ(Layout.m_UseDropdown, Count > SSettingsChoiceLayout::MAX_SEGMENTS);
		if(Layout.m_UseDropdown)
			EXPECT_EQ(Layout.m_SegmentCount, 0);
		else
		{
			EXPECT_EQ(Layout.m_SegmentCount, Count);
			EXPECT_FLOAT_EQ(Layout.m_aSlots[Count - 1].x + Layout.m_aSlots[Count - 1].w, Controls.x + Controls.w);
			for(int i = 0; i < Count; ++i)
				EXPECT_FLOAT_EQ(Layout.m_aSlots[i].h, Controls.h);
		}
	}
	EXPECT_TRUE(ResolveSettingsChoiceLayout({0, 0, 400, 20}, 3, 200).m_UseDropdown);
	EXPECT_FALSE(ResolveSettingsChoiceLayout({0, 0, 600, 20}, 3, 200).m_UseDropdown);
}
