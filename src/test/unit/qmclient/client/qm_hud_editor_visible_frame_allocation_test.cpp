#include <game/client/components/hud_editor.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(QmHudVisibleFrameAllocation, ColdFailureDisablesTheWholeFrameAndCanRetryNextFrame)
{
	QmHudEditor::CVisibleFrame Frame;
	QmHudEditor::SVisibleElement Element;
	Element.m_Rect = {10, 20, 30, 40};
	bool First = true, Second = true, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		First = Frame.Register(Element);
		Second = Frame.Register(Element);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(First);
	EXPECT_FALSE(Second);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_FALSE(Frame.Complete());
	EXPECT_TRUE(Frame.Elements().empty());
	Frame.BeginFrame();
	EXPECT_TRUE(Frame.Complete());
	ASSERT_TRUE(Frame.Register(Element));
	ASSERT_EQ(Frame.Elements().size(), 1u);
	EXPECT_FLOAT_EQ(Frame.Elements()[0].m_Rect.x, 10);
}

TEST(QmHudVisibleFrameAllocation, GrowthFailureDiscardsEarlierElementsAndStopsFurtherAdmission)
{
	QmHudEditor::CVisibleFrame Frame;
	QmHudEditor::SVisibleElement Element;
	Element.m_Element = EHudEditorElement::MediaIsland;
	ASSERT_TRUE(Frame.Register(Element));
	bool Triggered = false, RejectedLater = false;
	size_t BeforeGrowth = 0, Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		for(int Index = 0; Index < 128 && !Failure.Triggered(); ++Index)
		{
			BeforeGrowth = Frame.Elements().size();
			Frame.Register(Element);
		}
		Triggered = Failure.Triggered();
		RejectedLater = !Frame.Register(Element);
		Attempts = Failure.Attempts();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_GT(BeforeGrowth, 0u);
	EXPECT_TRUE(RejectedLater);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_FALSE(Frame.Complete());
	EXPECT_TRUE(Frame.Elements().empty());
	Frame.BeginFrame();
	for(size_t Index = 0; Index <= BeforeGrowth; ++Index)
		ASSERT_TRUE(Frame.Register(Element));
	EXPECT_TRUE(Frame.Complete());
	EXPECT_EQ(Frame.Elements().size(), BeforeGrowth + 1);
}

TEST(QmHudVisibleFrameAllocation, ReadyCapacityAndFrameResetDoNotAllocate)
{
	QmHudEditor::CVisibleFrame Frame;
	QmHudEditor::SVisibleElement Element;
	for(int Index = 0; Index < 8; ++Index)
		ASSERT_TRUE(Frame.Register(Element));
	const auto *pStorage = Frame.Elements().data();
	bool Accepted = true, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Frame.BeginFrame();
		for(int Index = 0; Index < 8; ++Index)
			Accepted = Frame.Register(Element) && Accepted;
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Accepted);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Frame.Elements().data(), pStorage);
	EXPECT_EQ(Frame.Elements().size(), 8u);
	EXPECT_TRUE(Frame.Complete());
}
