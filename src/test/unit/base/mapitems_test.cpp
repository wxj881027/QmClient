// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/mapitems.h>

#include <gtest/gtest-printers.h>
#include <gtest/gtest.h>

#include <iterator>
#include <limits>

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

TEST(Mapitems, RotatePointsWithOffsetWiderThanInt)
{
	const ivec2 Center(-1073741824, -1073741824);
	ivec2 Point(1610612736, Center.y);

	RotatePoints(Center, &Point, 1, pi / 2.0f);

	EXPECT_EQ(Point.y, 1610612736);
}

TEST(Mapitems, RotatePointsClampCoordinatesOutsideInt)
{
	ivec2 PositivePoint(0, 0);
	ivec2 NegativePoint(0, 0);

	RotatePoints(ivec2(1073741824, 0), &PositivePoint, 1, pi);
	RotatePoints(ivec2(-1610612736, 0), &NegativePoint, 1, pi);

	EXPECT_EQ(PositivePoint.x, std::numeric_limits<int>::max());
	EXPECT_EQ(NegativePoint.x, std::numeric_limits<int>::min());
}

TEST(Mapitems, RotatePointsKeepExactCoordinatesForZeroAngle)
{
	const int Coordinate = 1 << 24;
	const ivec2 OriginalPoint(Coordinate + 1, -Coordinate - 1);
	ivec2 Point = OriginalPoint;

	RotatePoints(ivec2(Coordinate, -Coordinate), &Point, 1, 0.0f);

	EXPECT_EQ(Point, OriginalPoint);
}

TEST(Mapitems, RotatePointsKeepCoordinatesForNonfiniteAngle)
{
	ivec2 Point(5, 7);

	RotatePoints(ivec2(3, 4), &Point, 1, std::numeric_limits<float>::quiet_NaN());

	EXPECT_EQ(Point, ivec2(5, 7));
}
