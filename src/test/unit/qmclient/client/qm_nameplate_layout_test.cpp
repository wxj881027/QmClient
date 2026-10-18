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
