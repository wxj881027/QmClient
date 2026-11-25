#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/cards/QmCardCatalogSkinMetrics.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>

#include <gtest/gtest.h>

namespace
{
	void ExpectInside(const CUIRect &Outer, const CUIRect &Inner)
	{
		EXPECT_GE(Inner.w, 0.0f);
		EXPECT_GT(Inner.h, 0.0f);
		EXPECT_GE(Inner.x, Outer.x - 0.01f);
		EXPECT_GE(Inner.y, Outer.y - 0.01f);
		EXPECT_LE(Inner.x + Inner.w, Outer.x + Outer.w + 0.01f);
		EXPECT_LE(Inner.y + Inner.h, Outer.y + Outer.h + 0.01f);
	}

	void ExpectSeparate(const CUIRect &First, const CUIRect &Second)
	{
		EXPECT_TRUE(First.x + First.w <= Second.x + 0.01f || Second.x + Second.w <= First.x + 0.01f ||
			    First.y + First.h <= Second.y + 0.01f || Second.y + Second.h <= First.y + 0.01f);
	}
}

TEST(SettingsTeeLayout, EditorControlsStayInsideMeasuredHeightWithoutOverlapping)
{
	for(const float Width : {200.0f, 320.0f, 540.0f, 699.0f, 700.0f, 1000.0f})
	{
		SCOPED_TRACE(Width);
		const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Width);
		for(const bool Colors : {false, true})
		{
			SCOPED_TRACE(Colors);
			const auto Layout = ResolveSettingsTeeEditorLayout({10.0f, 20.0f, Width, 0.0f}, Metrics, Colors);
			const CUIRect Bounds{10.0f, 20.0f, Width, Layout.m_Height};
			std::vector<CUIRect> vControls{Layout.m_aPreviews[0], Layout.m_aPreviews[1], Layout.m_PreviewMode, Layout.m_ShowNinja, Layout.m_TargetLabel,
				Layout.m_Identity, Layout.m_SkinLabel, Layout.m_SkinInput, Layout.m_RandomSkin,
				Layout.m_Eyes, Layout.m_CustomColors, Layout.m_RandomColors};
			vControls.insert(vControls.end(), Layout.m_aSkinTransfers.begin(), Layout.m_aSkinTransfers.end());
			if(Colors)
			{
				vControls.push_back(Layout.m_Colors.m_BodyGroup);
				vControls.push_back(Layout.m_Colors.m_FeetGroup);
			}
			for(size_t Index = 0; Index < vControls.size(); ++Index)
			{
				SCOPED_TRACE(Index);
				ExpectInside(Bounds, vControls[Index]);
				for(size_t Other = Index + 1; Other < vControls.size(); ++Other)
					ExpectSeparate(vControls[Index], vControls[Other]);
			}
			EXPECT_FLOAT_EQ(Layout.m_aPreviews[0].w, Layout.m_aPreviews[1].w);
			EXPECT_FLOAT_EQ(Layout.m_aPreviews[0].h, Layout.m_aPreviews[1].h);
			const auto Eyes = ResolveSettingsTeeEmoteSliderLayout(Layout.m_Eyes, Metrics);
			ExpectInside(Layout.m_Eyes, Eyes.m_TrackRect);
			ExpectSeparate(Eyes.m_TrackRect, Layout.m_CustomColors);
		}
	}
}

TEST(SettingsTeeLayout, PreviewHeadingClearsRoundedCornersAndReservesTheSkinNameLine)
{
	for(const float Scale : {0.75f, 1.0f, 1.5f})
	{
		SSettingsContentMetrics Metrics;
		Metrics.m_UiScale = Scale;
		Metrics.m_LineHeight *= Scale;
		Metrics.m_LineSpacing *= Scale;
		for(const float Width : {80.0f, 160.0f, 360.0f})
		{
			const CUIRect Preview{10.0f, 20.0f, Width * Scale, 112.0f * Scale};
			const auto Layout = ResolveSettingsTeePreviewLayout(Preview, Metrics, Scale);
			ExpectInside(Preview, Layout.m_Heading);
			ExpectInside(Preview, Layout.m_Flag);
			ExpectInside(Preview, Layout.m_Tee);
			ExpectInside(Preview, Layout.m_SkinName);
			ExpectSeparate(Layout.m_Heading, Layout.m_Flag);
			ExpectSeparate(Layout.m_Heading, Layout.m_Tee);
			ExpectSeparate(Layout.m_Tee, Layout.m_SkinName);
			EXPECT_GE(Layout.m_Heading.y - Preview.y, ui_token::settings::CARD_PADDING * Scale);
			EXPECT_GE(Layout.m_Heading.x - Preview.x, Layout.m_Radius * 0.3f);
			EXPECT_LT(Layout.m_Radius, std::min(Preview.w, Preview.h) * 0.5f);
			EXPECT_FLOAT_EQ(Layout.m_SkinName.h, Metrics.m_LineHeight);
		}
	}
}

TEST(SettingsTeeLayout, WideEditorUsesAvailableWidthAndNarrowEditorStacksFields)
{
	const SSettingsContentMetrics Metrics;
	const auto Narrow = ResolveSettingsTeeEditorLayout({0.0f, 0.0f, 320.0f, 0.0f}, Metrics, false);
	const auto Wide = ResolveSettingsTeeEditorLayout({0.0f, 0.0f, 900.0f, 0.0f}, Metrics, false);
	EXPECT_GE(Narrow.m_TargetLabel.y, Narrow.m_aPreviews[0].y + Narrow.m_aPreviews[0].h);
	EXPECT_GT(Wide.m_TargetLabel.x, Wide.m_aPreviews[1].x + Wide.m_aPreviews[1].w);
	EXPECT_LT(Wide.m_Height, Narrow.m_Height);
	EXPECT_TRUE(Narrow.m_StackIdentity);
	const auto Identity = ResolveSettingsTeeIdentityFieldsLayout(Narrow.m_Identity, Metrics, true);
	EXPECT_GE(Identity.m_ClanInput.y, Identity.m_NameInput.y + Identity.m_NameInput.h);
	ExpectInside(Narrow.m_Identity, Identity.m_NameInput);
	ExpectInside(Narrow.m_Identity, Identity.m_ClanInput);
	ExpectInside(Narrow.m_Identity, Identity.m_FlagButton);
}

TEST(SettingsTeeLayout, TransferButtonsStayBetweenPreviewsAtDifferentWidthsAndScales)
{
	for(const float Scale : {0.8f, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		for(const float Width : {200.0f, 540.0f, 900.0f})
		{
			SCOPED_TRACE(Width);
			SSettingsContentMetrics Metrics;
			Metrics.m_UiScale = Scale;
			Metrics.m_LineHeight *= Scale;
			Metrics.m_InputHeight *= Scale;
			Metrics.m_ButtonHeight *= Scale;
			Metrics.m_LineSpacing *= Scale;
			Metrics.m_SectionGap *= Scale;
			const auto Layout = ResolveSettingsTeeEditorLayout({10.0f, 20.0f, Width * Scale, 0.0f}, Metrics, false);
			const CUIRect &Player = Layout.m_aPreviews[0];
			const CUIRect &Dummy = Layout.m_aPreviews[1];
			for(size_t Index = 0; Index < Layout.m_aSkinTransfers.size(); ++Index)
			{
				const auto &Button = Layout.m_aSkinTransfers[Index];
				EXPECT_GT(Button.x, Player.x + Player.w);
				EXPECT_LT(Button.x + Button.w, Dummy.x);
				EXPECT_GE(Button.y, Player.y);
				EXPECT_LE(Button.y + Button.h, Player.y + Player.h);
				EXPECT_FLOAT_EQ(Button.w, Button.h);
				EXPECT_FLOAT_EQ(Button.w, Metrics.m_ButtonHeight);
				if(Index > 0)
					EXPECT_GT(Button.y, Layout.m_aSkinTransfers[Index - 1].y + Button.h);
			}
		}
	}
}

TEST(SettingsTeeLayout, CustomColorsKeepBothPartsVisibleWhenResizing)
{
	const SSettingsContentMetrics Metrics;
	const auto Narrow = ResolveSettingsTeeCustomColorsLayout({0.0f, 0.0f, 320.0f, 0.0f}, true, Metrics);
	const auto Wide = ResolveSettingsTeeCustomColorsLayout({0.0f, 0.0f, 800.0f, 0.0f}, true, Metrics);
	EXPECT_GE(Narrow.m_FeetGroup.y, Narrow.m_BodyGroup.y + Narrow.m_BodyGroup.h);
	EXPECT_FLOAT_EQ(Wide.m_BodyGroup.y, Wide.m_FeetGroup.y);
	EXPECT_GT(Wide.m_FeetGroup.x, Wide.m_BodyGroup.x + Wide.m_BodyGroup.w);
	EXPECT_LT(Wide.m_Height, Narrow.m_Height);
	ExpectInside(Wide.m_BodyGroup, Wide.m_BodyControls);
	ExpectInside(Wide.m_FeetGroup, Wide.m_FeetControls);
}

TEST(SettingsTeeLayout, LibraryToolbarWrapsWithoutShrinkingIconTargets)
{
	const SSettingsContentMetrics Metrics;
	for(const float Width : {200.0f, 320.0f, 540.0f, 1000.0f})
	{
		SCOPED_TRACE(Width);
		const auto Layout = ResolveSettingsTeeToolbarLayout({10.0f, 20.0f, Width, 0.0f}, Metrics);
		const CUIRect Bounds{10.0f, 20.0f, Width, Layout.m_Height};
		ExpectInside(Bounds, Layout.m_Search);
		ExpectInside(Bounds, Layout.m_Collection);
		ExpectInside(Bounds, Layout.m_Count);
		ExpectInside(Bounds, Layout.m_FilterLabel);
		ExpectInside(Bounds, Layout.m_Filter);
		ExpectInside(Bounds, Layout.m_SortLabel);
		ExpectInside(Bounds, Layout.m_Sort);
		ExpectSeparate(Layout.m_Search, Layout.m_Collection);
		ExpectSeparate(Layout.m_Collection, Layout.m_Sort);
		ExpectSeparate(Layout.m_Collection, Layout.m_Count);
		ExpectSeparate(Layout.m_Collection, Layout.m_Filter);
		ExpectSeparate(Layout.m_Count, Layout.m_Filter);
		ExpectSeparate(Layout.m_FilterLabel, Layout.m_Filter);
		ExpectSeparate(Layout.m_SortLabel, Layout.m_Sort);
		ExpectSeparate(Layout.m_Filter, Layout.m_Sort);
		for(const auto &Tool : Layout.m_aTools)
		{
			ExpectInside(Bounds, Tool);
			ExpectSeparate(Layout.m_Search, Tool);
			EXPECT_FLOAT_EQ(Tool.w, Metrics.m_ButtonHeight);
			EXPECT_FLOAT_EQ(Tool.h, Metrics.m_ButtonHeight);
		}
	}
}

TEST(SettingsTeeLayout, WideLibraryToolbarCapsCollectionAndKeepsCountBesideIt)
{
	const SSettingsContentMetrics Metrics;
	const auto Layout = ResolveSettingsTeeToolbarLayout({10.0f, 20.0f, 1400.0f, 0.0f}, Metrics);
	EXPECT_LE(Layout.m_Collection.w, 360.0f * Metrics.m_UiScale + 0.01f);
	EXPECT_FLOAT_EQ(Layout.m_Count.y, Layout.m_Collection.y);
	EXPECT_GE(Layout.m_Count.x, Layout.m_Collection.x + Layout.m_Collection.w);
	EXPECT_FLOAT_EQ(Layout.m_Filter.y, Layout.m_Collection.y);
	EXPECT_FLOAT_EQ(Layout.m_Sort.y, Layout.m_Collection.y);
}

TEST(SettingsTeeLayout, SkinOptionsAndAppearanceGroupsFitTheirMeasuredBounds)
{
	const SSettingsContentMetrics Metrics;
	for(const float Width : {240.0f, 500.0f, 800.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect View{15.0f, 30.0f, Width, 0.0f};
		const auto Options = ResolveSettingsTeeOptionsLayout(View, Metrics);
		const CUIRect OptionsBounds{View.x, View.y, Width, Options.m_Height};
		ExpectInside(OptionsBounds, Options.m_Downloads);
		ExpectInside(OptionsBounds, Options.m_Prefix);
		ExpectSeparate(Options.m_Downloads, Options.m_Prefix);
		const auto Appearance = ResolveSettingsSkinAppearanceLayout(View, Metrics);
		const CUIRect AppearanceBounds{View.x, View.y, Width, Appearance.m_Height};
		ExpectInside(AppearanceBounds, Appearance.m_Outline);
		ExpectInside(AppearanceBounds, Appearance.m_Hue);
		ExpectInside(AppearanceBounds, Appearance.m_Shadow);
		ExpectSeparate(Appearance.m_Outline, Appearance.m_Hue);
		ExpectSeparate(Appearance.m_Hue, Appearance.m_Shadow);
		ExpectSeparate(Appearance.m_Outline, Appearance.m_Shadow);
	}
}

TEST(SettingsTeeLayout, EmptyQueueDoesNotReserveSpaceForCappedVisibleRows)
{
	const SSettingsContentMetrics Metrics;
	const auto Empty = ResolveSettingsTeeQueuePanelGeometry(Metrics, 0, 2, 400.0f);
	const auto Full = ResolveSettingsTeeQueuePanelGeometry(Metrics, 8, 2, 400.0f);
	const auto More = ResolveSettingsTeeQueuePanelGeometry(Metrics, 50, 2, 400.0f);
	EXPECT_EQ(Empty.m_VisibleQueueRows, 1);
	// 队列可见行数封顶在 token 上；空队列只占 1 行，封顶后高度不再随条目数增长。
	EXPECT_EQ(Full.m_VisibleQueueRows, ui_token::list::SKIN_QUEUE_MAX_VISIBLE_ROWS);
	EXPECT_EQ(More.m_VisibleQueueRows, ui_token::list::SKIN_QUEUE_MAX_VISIBLE_ROWS);
	EXPECT_EQ(Empty.m_VisiblePresetRows, 2);
	const float CappedQueueViewport = ResolveSettingsListViewportHeight(ui_token::list::SKIN_QUEUE_MAX_VISIBLE_ROWS, Metrics.m_ListRowHeight, ui_token::list::ROW_SPACING);
	const float SingleRowViewport = ResolveSettingsListViewportHeight(1, Metrics.m_ListRowHeight, ui_token::list::ROW_SPACING);
	EXPECT_FLOAT_EQ(Full.m_ContentHeight - Empty.m_ContentHeight, CappedQueueViewport - SingleRowViewport);
	EXPECT_FLOAT_EQ(More.m_ContentHeight, Full.m_ContentHeight);
	const auto Narrow = ResolveSettingsTeeQueuePanelGeometry(Metrics, 0, 2, 200.0f);
	EXPECT_TRUE(Narrow.m_StackInterval);
	EXPECT_FALSE(Empty.m_StackInterval);
	EXPECT_GT(Narrow.m_ContentHeight, Empty.m_ContentHeight);
}

TEST(SettingsTeeLayout, LibraryColumnsAdaptToUsableWidth)
{
	EXPECT_EQ(ResolveSettingsTeeSkinColumns(220.0f, 1.0f), 1);
	EXPECT_EQ(ResolveSettingsTeeSkinColumns(550.0f, 1.0f), 3);
	EXPECT_EQ(ResolveSettingsTeeSkinColumns(730.0f, 1.0f), 4);
	EXPECT_EQ(ResolveSettingsTeeSkinColumns(1500.0f, 1.0f), 6);
	EXPECT_EQ(ResolveSettingsTeeSkinColumns(550.0f * 0.8f, 0.8f), 3);
}

TEST(SettingsTeeLayout, LeadingCardsPreserveTheOrderOfTheRemainingColumns)
{
	const std::array<std::vector<int>, 3> Columns{{{30, 31, 32}, {10, 11}, {20}}};
	const auto Order = [&](int Leading) {
		std::vector<int> Result;
		ForEachSettingsCardDeckVisualOrder(Columns, [&](int Index, int) { Result.push_back(Index); }, Leading);
		return Result;
	};
	EXPECT_EQ(Order(0), (std::vector<int>{10, 20, 30, 11, 31, 32}));
	EXPECT_EQ(Order(2), (std::vector<int>{30, 31, 10, 20, 32, 11}));
	EXPECT_EQ(Order(-1), Order(0));
	EXPECT_EQ(Order(99), (std::vector<int>{30, 31, 32, 10, 20, 11}));
}

TEST(SettingsTeeLayout, GlowHeightIncludesSlidersAndActualColorButtonHeight)
{
	for(const float Scale : {0.75f, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		SSettingsContentMetrics Metrics;
		Metrics.m_LineHeight = 20.0f * Scale;
		Metrics.m_ButtonHeight = 28.0f * Scale;
		Metrics.m_LineSpacing = 5.0f * Scale;
		CUIRect Remaining{0.0f, 0.0f, 240.0f * Scale, ResolveSettingsTeeGlowHeight(Metrics, true)};
		for(int Row = 0; Row < 4; ++Row)
		{
			if(Row > 0)
				Remaining.HSplitTop(Metrics.m_LineSpacing, nullptr, &Remaining);
			Remaining.HSplitTop(Metrics.m_LineHeight, nullptr, &Remaining);
		}
		EXPECT_FLOAT_EQ(Remaining.h, Metrics.m_LineSpacing + Metrics.m_ButtonHeight);
		Remaining.HSplitTop(Metrics.m_LineSpacing, nullptr, &Remaining);
		const auto Color = ResolveSettingsColorRowLayout(Remaining, Metrics, false, false);
		EXPECT_FLOAT_EQ(Color.m_ConsumedHeight, Remaining.h);
		EXPECT_FLOAT_EQ(ResolveSettingsTeeGlowHeight(Metrics, false), Remaining.y - Metrics.m_LineSpacing);
	}
}
