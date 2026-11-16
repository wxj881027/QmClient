#include <game/client/components/qmclient/nameplate_layout.h>

#include <gtest/gtest.h>

namespace
{
	class CPartLayout : public CQmNameplatePartLayout
	{
	public:
		CPartLayout() :
			CQmNameplatePartLayout(vec2(5.0f, 5.0f))
		{
			m_Size = vec2(80.0f, 28.0f);
		}
		void SetEnabled(bool Enabled) { m_Visible = Enabled; }
		void SetPrivacyHidden(bool Hidden) { m_HiddenForPrivacy = Hidden; }
		void SetSize(vec2 Size) { m_Size = Size; }
		void SetShiftOnInvis(bool Shift) { m_ShiftOnInvis = Shift; }
		void SetReserveLineHeight(bool Reserve) { m_ReserveLineHeight = Reserve; }
		void SetReserveBubbleTop(bool Reserve) { m_ReserveBubbleTop = Reserve; }
	};
}

TEST(QmNameplatePartLayout, DirectionBubbleTopStaysStableWhenKeyIsReleased)
{
	CPartLayout Part;
	Part.SetReserveBubbleTop(true);
	Part.SetShiftOnInvis(true);
	const float PressedTop = Part.BubbleTop(100.0f, 33.0f);
	Part.SetEnabled(false);
	EXPECT_FLOAT_EQ(Part.BubbleTop(100.0f, 33.0f), PressedTop);
	Part.SetShiftOnInvis(false);
	EXPECT_EQ(Part.BubbleTop(100.0f, 33.0f), std::numeric_limits<float>::infinity());
}

TEST(QmNameplatePartLayout, PrivacyHiddenContentKeepsMeasuredSpaceWithoutDrawing)
{
	CPartLayout Part;
	ASSERT_TRUE(Part.Visible());
	const vec2 Before = Part.LayoutSize();
	Part.SetPrivacyHidden(true);
	EXPECT_FALSE(Part.Visible());
	EXPECT_TRUE(Part.HasLayout());
	EXPECT_TRUE(Part.ShiftOnInvis());
	EXPECT_EQ(Part.LayoutSize(), Before);
}

TEST(QmNameplatePartLayout, DisabledContentDoesNotLeavePrivacyPlaceholder)
{
	CPartLayout Part;
	Part.SetPrivacyHidden(true);
	Part.SetEnabled(false);
	EXPECT_FALSE(Part.Visible());
	EXPECT_FALSE(Part.HasLayout());
	EXPECT_FALSE(Part.ShiftOnInvis());
	EXPECT_EQ(Part.LayoutSize(), vec2(0.0f, 0.0f));
}

TEST(QmNameplatePartLayout, HiddenContentUsesCurrentSizeAfterFirstAppearanceOrResize)
{
	CPartLayout Part;
	Part.SetPrivacyHidden(true);
	EXPECT_EQ(Part.LayoutSize(), vec2(85.0f, 33.0f));
	Part.SetSize(vec2(120.0f, 42.0f));
	EXPECT_FALSE(Part.Visible());
	EXPECT_EQ(Part.LayoutSize(), vec2(125.0f, 47.0f));
}

TEST(QmNameplatePartLayout, RepeatedPrivacyChangesRestoreDrawingWithoutChangingSpace)
{
	CPartLayout Part;
	const vec2 Before = Part.LayoutSize();
	for(int Toggle = 0; Toggle < 4; ++Toggle)
	{
		Part.SetPrivacyHidden(true);
		EXPECT_FALSE(Part.Visible());
		EXPECT_EQ(Part.LayoutSize(), Before);
		Part.SetPrivacyHidden(false);
		EXPECT_TRUE(Part.Visible());
		EXPECT_EQ(Part.LayoutSize(), Before);
	}
}

TEST(QmNameplatePartLayout, InactiveDirectionKeepsHorizontalAndVerticalSpace)
{
	CPartLayout Part;
	Part.SetEnabled(false);
	Part.SetShiftOnInvis(true);
	EXPECT_FALSE(Part.Visible());
	EXPECT_TRUE(Part.HasLayout());
	EXPECT_TRUE(Part.ShiftOnInvis());
	EXPECT_EQ(Part.LayoutSize(), vec2(85.0f, 33.0f));
}

TEST(QmNameplatePartLayout, HookPlaceholderReservesOnlyVerticalSpace)
{
	CPartLayout Part;
	Part.SetEnabled(false);
	Part.SetReserveLineHeight(true);
	EXPECT_FALSE(Part.Visible());
	EXPECT_TRUE(Part.HasLayout());
	EXPECT_FALSE(Part.ShiftOnInvis());
	EXPECT_EQ(Part.LayoutSize(), vec2(0.0f, 33.0f));
}

TEST(QmNameplatePartLayout, VisibleTopFollowsTheRenderedLineCenter)
{
	CPartLayout Part;
	EXPECT_FLOAT_EQ(Part.VisibleTop(100.0f, 33.0f), 69.5f);
	// 同行有更高的部件时，文字依旧按整行高度居中。
	EXPECT_FLOAT_EQ(Part.VisibleTop(100.0f, 60.0f), 56.0f);
}

TEST(QmNameplatePartLayout, MovingTheRowMovesItsVisibleTopByTheSameDistance)
{
	CPartLayout Part;
	const float OriginalTop = Part.VisibleTop(100.0f, 33.0f);
	EXPECT_FLOAT_EQ(Part.VisibleTop(140.0f, 33.0f), OriginalTop + 40.0f);
	EXPECT_FLOAT_EQ(Part.VisibleTop(80.0f, 33.0f), OriginalTop - 20.0f);
}

TEST(QmNameplatePartLayout, HiddenPlaceholdersDoNotRaiseTheVisibleTop)
{
	CPartLayout Part;
	Part.SetPrivacyHidden(true);
	EXPECT_EQ(Part.VisibleTop(100.0f, 33.0f), std::numeric_limits<float>::infinity());
	Part.SetPrivacyHidden(false);
	Part.SetEnabled(false);
	Part.SetReserveLineHeight(true);
	EXPECT_TRUE(Part.HasLayout());
	EXPECT_EQ(Part.VisibleTop(100.0f, 33.0f), std::numeric_limits<float>::infinity());
	Part.SetEnabled(true);
	EXPECT_FLOAT_EQ(Part.VisibleTop(100.0f, 33.0f), 69.5f);
}

namespace
{
	std::array<vec2, kNameplateCoreRowCount> NameplateKeyLayoutSizes(float IconSize = 18.0f)
	{
		std::array<vec2, kNameplateCoreRowCount> aSizes{};
		aSizes[static_cast<size_t>(ENameplateCoreRow::NAME)] = vec2(100.0f, 20.0f);
		aSizes[static_cast<size_t>(ENameplateCoreRow::HOOK)] = vec2(0.0f, 29.0f);
		aSizes[static_cast<size_t>(ENameplateCoreRow::KEYS)] = vec2((IconSize + 5.0f) * 3.0f, IconSize * 1.5f);
		aSizes[static_cast<size_t>(ENameplateCoreRow::WATER_HAMMER)] = vec2(IconSize + 5.0f, IconSize * 1.5f);
		aSizes[static_cast<size_t>(ENameplateCoreRow::WATER_HOOK)] = vec2(IconSize + 5.0f, IconSize * 1.5f);
		return aSizes;
	}
}

TEST(QmNameplateKeyLayout, DefaultsPlaceHammerAndHookBesideDirectionKeys)
{
	const auto aPositions = QmNameplateCoreRowPositions(NameplateKeyLayoutSizes());
	EXPECT_EQ(aPositions[static_cast<size_t>(ENameplateCoreRow::KEYS)], vec2(0.0f, -49.0f));
	EXPECT_EQ(aPositions[static_cast<size_t>(ENameplateCoreRow::WATER_HAMMER)], vec2(-46.0f, -49.0f));
	EXPECT_EQ(aPositions[static_cast<size_t>(ENameplateCoreRow::WATER_HOOK)], vec2(46.0f, -49.0f));
}

TEST(QmNameplateKeyLayout, MovingEachInputOnlyChangesItsOwnPosition)
{
	const auto aSizes = NameplateKeyLayoutSizes();
	const auto aOriginal = QmNameplateCoreRowPositions(aSizes);
	for(const auto Target : {ENameplateCoreRow::KEYS, ENameplateCoreRow::WATER_HAMMER, ENameplateCoreRow::WATER_HOOK})
	{
		SCOPED_TRACE(static_cast<size_t>(Target));
		std::array<vec2, kNameplateCoreRowCount> aOffsets{};
		aOffsets[static_cast<size_t>(Target)] = vec2(30.0f, -15.0f);
		const auto aMoved = QmNameplateCoreRowPositions(aSizes, aOffsets);
		for(size_t Row = 0; Row < aMoved.size(); ++Row)
		{
			if(Row == static_cast<size_t>(Target))
				EXPECT_EQ(aMoved[Row], aOriginal[Row] + vec2(30.0f, -15.0f));
			else
				EXPECT_EQ(aMoved[Row], aOriginal[Row]);
		}
	}
}

TEST(QmNameplateKeyLayout, ClearingIndependentOffsetsRestoresDefaultPositions)
{
	const auto aSizes = NameplateKeyLayoutSizes();
	std::array<vec2, kNameplateCoreRowCount> aOffsets{};
	aOffsets[static_cast<size_t>(ENameplateCoreRow::WATER_HAMMER)] = vec2(100.0f, -30.0f);
	aOffsets[static_cast<size_t>(ENameplateCoreRow::WATER_HOOK)] = vec2(-75.0f, 40.0f);
	const auto aMoved = QmNameplateCoreRowPositions(aSizes, aOffsets);
	EXPECT_EQ(aMoved[static_cast<size_t>(ENameplateCoreRow::WATER_HAMMER)], vec2(54.0f, -79.0f));
	EXPECT_EQ(aMoved[static_cast<size_t>(ENameplateCoreRow::WATER_HOOK)], vec2(-29.0f, -9.0f));
	aOffsets = {};
	EXPECT_EQ(QmNameplateCoreRowPositions(aSizes, aOffsets), QmNameplateCoreRowPositions(aSizes));
}

TEST(QmNameplateKeyLayout, LargerKeyIconsKeepTheSameVerticalBaseline)
{
	const auto aSmall = QmNameplateCoreRowPositions(NameplateKeyLayoutSizes(10.0f));
	const auto aLarge = QmNameplateCoreRowPositions(NameplateKeyLayoutSizes(38.0f));
	EXPECT_EQ(aSmall[static_cast<size_t>(ENameplateCoreRow::WATER_HAMMER)], vec2(-30.0f, -49.0f));
	EXPECT_EQ(aLarge[static_cast<size_t>(ENameplateCoreRow::WATER_HAMMER)], vec2(-86.0f, -49.0f));
	EXPECT_EQ(aSmall[static_cast<size_t>(ENameplateCoreRow::WATER_HOOK)], vec2(30.0f, -49.0f));
	EXPECT_EQ(aLarge[static_cast<size_t>(ENameplateCoreRow::WATER_HOOK)], vec2(86.0f, -49.0f));
}

TEST(QmNameplateKeyLayout, DisabledWaterIconsDoNotMoveOtherRowsOrGrowPreview)
{
	auto aSizes = NameplateKeyLayoutSizes();
	const auto aWithIcons = QmNameplateCoreRowPositions(aSizes);
	std::array<float, kNameplateCoreRowCount> aHeights{};
	for(size_t Row = 0; Row < aSizes.size(); ++Row)
		aHeights[Row] = aSizes[Row].y;
	const float WithIconsSpan = QmNameplatePreviewContentSpan(aHeights);
	for(const auto Row : {ENameplateCoreRow::WATER_HAMMER, ENameplateCoreRow::WATER_HOOK})
	{
		aSizes[static_cast<size_t>(Row)] = vec2(0.0f, 0.0f);
		aHeights[static_cast<size_t>(Row)] = 0.0f;
	}
	const auto aWithoutIcons = QmNameplateCoreRowPositions(aSizes);
	for(size_t Row = 0; Row < kNameplateStackedRowCount; ++Row)
		EXPECT_EQ(aWithoutIcons[Row], aWithIcons[Row]);
	EXPECT_FLOAT_EQ(QmNameplatePreviewContentSpan(aHeights), WithIconsSpan);
}
