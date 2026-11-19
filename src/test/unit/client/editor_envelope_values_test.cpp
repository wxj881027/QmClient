#include <game/editor/envelope_editor.h>

#include <gtest/gtest.h>

#include <limits>

TEST(EditorEnvelopeValues, FixedPointTransformClampsBeforeIntegerConversion)
{
	constexpr int Min = std::numeric_limits<int>::min();
	constexpr int Max = std::numeric_limits<int>::max();

	EXPECT_EQ(EnvelopeEditor::RoundFixedPoint(static_cast<double>(Max) + 1.0), Max);
	EXPECT_EQ(EnvelopeEditor::RoundFixedPoint(static_cast<double>(Min) - 1.0), Min);
	EXPECT_EQ(EnvelopeEditor::RoundFixedPoint(Max - 1.0), Max - 1);
	EXPECT_EQ(EnvelopeEditor::RoundFixedPoint(-2.5), -3);
}
