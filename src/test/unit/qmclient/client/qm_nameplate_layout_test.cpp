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
	};
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
