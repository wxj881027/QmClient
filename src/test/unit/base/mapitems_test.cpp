// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/mapitems.h>

#include <gtest/gtest-printers.h>
#include <gtest/gtest.h>

#include <iterator>

namespace testing::internal
{
	template<>
	class UniversalPrinter<CFixedTime>
	{
	public:
		static void Print(const CFixedTime &FixedTime, std::ostream *pOutputStream)
		{
			*pOutputStream << "CFixedTime with internal value " << FixedTime.GetInternal();
		}
	};

}

TEST(Mapitems, FixedTimeRoundtrip)
{
	for(CFixedTime Fixed = CFixedTime(0); Fixed < CFixedTime(1000000); Fixed += CFixedTime(1))
	{
		ASSERT_EQ(Fixed, CFixedTime::FromSeconds(Fixed.AsSeconds()));
	}
}

TEST(Mapitems, RotatePointsAroundCenter)
{
	ivec2 aPoints[] = {
		ivec2(10, 0),
		ivec2(0, 10),
		ivec2(5, 5),
	};

	RotatePoints(ivec2(0, 0), aPoints, std::size(aPoints), pi / 2.0f);

	EXPECT_EQ(aPoints[0], ivec2(0, 10));
	EXPECT_EQ(aPoints[1], ivec2(-10, 0));
	EXPECT_EQ(aPoints[2], ivec2(-5, 5));
}
