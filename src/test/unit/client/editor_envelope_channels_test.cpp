#include <game/editor/mapitems/envelope.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

namespace
{
	int PointIndex(const CEnvelope &Envelope, int Channel, int Millis)
	{
		for(size_t i = 0; i < Envelope.m_vPoints.size(); ++i)
			if(Envelope.m_vPoints[i].HasChannel(Channel) && Envelope.m_vPoints[i].m_Time == CFixedTime(Millis))
				return i;
		return -1;
	}

	ColorRGBA Evaluate(const CEnvelope &Envelope, float Time)
	{
		ColorRGBA Value(0, 0, 0, 0);
		Envelope.Eval(Time, Value, 4);
		return Value;
	}

	class CRuntimeAccess : public IEnvelopePointAccess
	{
		const std::vector<CEnvPoint_runtime> &m_vPoints;

	public:
		explicit CRuntimeAccess(const std::vector<CEnvPoint_runtime> &vPoints) :
			m_vPoints(vPoints) {}
		int NumPoints() const override { return m_vPoints.size(); }
		const CEnvPoint *GetPoint(int Index) const override { return &m_vPoints[Index]; }
		const CEnvPointBezier *GetBezier(int Index) const override { return &m_vPoints[Index].m_Bezier; }
	};

	ColorRGBA EvaluateRuntime(const std::vector<CEnvPoint_runtime> &vPoints, int64_t Micros, int Channels)
	{
		CRuntimeAccess Access(vPoints);
		ColorRGBA Value(0, 0, 0, 0);
		CRenderMap::RenderEvalEnvelope(&Access, std::chrono::microseconds(Micros), Value, Channels);
		return Value;
	}

	CEnvelope PositionEnvelope()
	{
		CEnvelope Envelope(CEnvelope::EType::POSITION);
		Envelope.AddPoint(CFixedTime(0), {0, 0, 0, 0});
		Envelope.AddPoint(CFixedTime(1200), {1024, 2048, 3072, 0});
		return Envelope;
	}
}

TEST(EditorEnvelopeChannels, AddPointOnlyCreatesActiveAxes)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.AddPoint(CFixedTime(300), {256, 512, 768, 0}, 1);
	EXPECT_GE(PointIndex(Envelope, 0, 300), 0);
	EXPECT_EQ(PointIndex(Envelope, 1, 300), -1);
	EXPECT_EQ(PointIndex(Envelope, 2, 300), -1);
	Envelope.AddPoint(CFixedTime(600), {512, 1024, 1536, 0}, 6);
	EXPECT_EQ(PointIndex(Envelope, 0, 600), -1);
	EXPECT_GE(PointIndex(Envelope, 1, 600), 0);
	EXPECT_GE(PointIndex(Envelope, 2, 600), 0);
}

TEST(EditorEnvelopeChannels, NoActiveAxesDoesNotCreatePoint)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.AddPoint(CFixedTime(300), {0, 0, 0, 0}, 0);
	EXPECT_EQ(Envelope.m_vPoints.size(), 6u);
}

TEST(EditorEnvelopeChannels, AddingAtExistingTimeFillsMissingAxesWithoutOverwriting)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.AddPoint(CFixedTime(300), {100, 0, 0, 0}, 1);
	Envelope.AddPoint(CFixedTime(300), {900, 200, 300, 0}, 7);
	EXPECT_EQ(Envelope.m_vPoints[PointIndex(Envelope, 0, 300)].m_aValues[0], 100);
	EXPECT_EQ(Envelope.m_vPoints[PointIndex(Envelope, 1, 300)].m_aValues[1], 200);
	EXPECT_EQ(Envelope.m_vPoints[PointIndex(Envelope, 2, 300)].m_aValues[2], 300);
	EXPECT_EQ(Envelope.m_vPoints.size(), 9u);
}

TEST(EditorEnvelopeChannels, AddingAndEditingXDoesNotSplitOtherCurves)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.m_vPoints[PointIndex(Envelope, 1, 0)].m_Curvetype = CURVETYPE_SMOOTH;
	const auto Before = Evaluate(Envelope, 0.45f);
	Envelope.AddPoint(CFixedTime(300), {1000, 0, 0, 0}, 1);
	const auto After = Evaluate(Envelope, 0.45f);
	EXPECT_NE(After.r, Before.r);
	EXPECT_FLOAT_EQ(After.g, Before.g);
	EXPECT_FLOAT_EQ(After.b, Before.b);
}

TEST(EditorEnvelopeChannels, TimeBoundsIgnoreOtherAxes)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.AddPoint(CFixedTime(300), {256, 0, 0, 0}, 1);
	Envelope.AddPoint(CFixedTime(350), {0, 600, 0, 0}, 2);
	const int X = PointIndex(Envelope, 0, 300);
	EXPECT_EQ(Envelope.ClampPointTime(X, 0, CFixedTime(900)), CFixedTime(900));
	EXPECT_EQ(Envelope.ClampPointTime(X, 0, CFixedTime(1200)), CFixedTime(1199));
	Envelope.m_vPoints[X].m_Time = Envelope.ClampPointTime(X, 0, CFixedTime(900));
	EXPECT_EQ(Envelope.m_vPoints[PointIndex(Envelope, 1, 350)].m_Time, CFixedTime(350));
	const auto Value = Evaluate(Envelope, 0.6f);
	EXPECT_NEAR(Value.r, 1.0f / 6, 0.001f);
}

TEST(EditorEnvelopeChannels, DeletingXPointKeepsYAndRotationAtSameTime)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.AddPoint(CFixedTime(600), {1000, 512, 256, 0});
	const auto Before = Evaluate(Envelope, 0.3f);
	const int X = PointIndex(Envelope, 0, 600);
	Envelope.m_vPoints.erase(Envelope.m_vPoints.begin() + X);
	const auto After = Evaluate(Envelope, 0.3f);
	EXPECT_NE(Before.r, After.r);
	EXPECT_FLOAT_EQ(Before.g, After.g);
	EXPECT_FLOAT_EQ(Before.b, After.b);
	EXPECT_GE(PointIndex(Envelope, 1, 600), 0);
	EXPECT_GE(PointIndex(Envelope, 2, 600), 0);
}

TEST(EditorEnvelopeChannels, MovingFirstXKeyDoesNotShiftOtherAxes)
{
	CEnvelope Envelope = PositionEnvelope();
	const int X = PointIndex(Envelope, 0, 0);
	EXPECT_EQ(Envelope.ClampPointTime(X, 0, CFixedTime(-100)), CFixedTime(0));
	Envelope.m_vPoints[X].m_Time = Envelope.ClampPointTime(X, 0, CFixedTime(200));
	EXPECT_GE(PointIndex(Envelope, 1, 0), 0);
	EXPECT_GE(PointIndex(Envelope, 2, 0), 0);
	EXPECT_EQ(Envelope.m_vPoints[PointIndex(Envelope, 1, 1200)].m_Time, CFixedTime(1200));
	EXPECT_FLOAT_EQ(Evaluate(Envelope, 0.6f).g, 1.0f);
}

TEST(EditorEnvelopeChannels, MissingColorAxisUsesNeutralValueAndHasFiniteRange)
{
	CEnvelope Envelope(CEnvelope::EType::COLOR);
	Envelope.AddPoint(CFixedTime(0), {256, 0, 0, 0}, 1);
	Envelope.AddPoint(CFixedTime(1200), {768, 0, 0, 0}, 1);
	EXPECT_EQ(Envelope.GetValueRange(2), (std::pair{0.0f, 0.0f}));
	EXPECT_FLOAT_EQ(Evaluate(Envelope, 0.6f).g, 1.0f);
	const auto Runtime = Envelope.ExportPoints();
	EXPECT_NEAR(EvaluateRuntime(Runtime, 600000, 4).r, 0.5f, 0.001f);
	EXPECT_FLOAT_EQ(EvaluateRuntime(Runtime, 600000, 4).g, 1.0f);
}

TEST(EditorEnvelopeChannels, ShorterAxisHoldsLastValueUntilSharedEnvelopeLoops)
{
	CEnvelope Envelope(CEnvelope::EType::POSITION);
	Envelope.AddPoint(CFixedTime(0), {0, 0, 0, 0});
	Envelope.AddPoint(CFixedTime(300), {1024, 0, 0, 0}, 1);
	Envelope.AddPoint(CFixedTime(1200), {0, 2048, 0, 0}, 2);
	const auto Runtime = Envelope.ExportPoints();
	EXPECT_FLOAT_EQ(Evaluate(Envelope, 0.9f).r, 1.0f);
	EXPECT_FLOAT_EQ(EvaluateRuntime(Runtime, 900000, 3).r, 1.0f);
	EXPECT_NEAR(EvaluateRuntime(Runtime, 1350000, 3).r, 0.5f, 0.001f);
}

TEST(EditorEnvelopeChannels, AllRgbaChannelsCanHaveDifferentTimesAndCurves)
{
	CEnvelope Envelope(CEnvelope::EType::COLOR);
	Envelope.AddPoint(CFixedTime(0), {0, 0, 0, 1024});
	Envelope.AddPoint(CFixedTime(1200), {1024, 1024, 1024, 0});
	Envelope.m_vPoints[PointIndex(Envelope, 0, 0)].m_Curvetype = CURVETYPE_SLOW;
	Envelope.m_vPoints[PointIndex(Envelope, 1, 0)].m_Curvetype = CURVETYPE_FAST;
	Envelope.m_vPoints[PointIndex(Envelope, 2, 0)].m_Curvetype = CURVETYPE_STEP;
	Envelope.AddPoint(CFixedTime(600), {0, 0, 0, 512}, 8);
	const auto Value = Evaluate(Envelope, 0.6f);
	EXPECT_FLOAT_EQ(Value.r, 0.125f);
	EXPECT_FLOAT_EQ(Value.g, 0.875f);
	EXPECT_FLOAT_EQ(Value.b, 0.0f);
	EXPECT_FLOAT_EQ(Value.a, 0.5f);
}

TEST(EditorEnvelopeChannels, IndependentMetadataRoundTripPreservesKeysAndTangents)
{
	CEnvelope Source = PositionEnvelope();
	Source.AddPoint(CFixedTime(300), {256, 0, 0, 0}, 1);
	auto &Point = Source.m_vPoints[PointIndex(Source, 0, 300)];
	Point.m_Curvetype = CURVETYPE_BEZIER;
	Point.m_Bezier.m_aOutTangentDeltaX[0] = CFixedTime(180);
	Point.m_Bezier.m_aOutTangentDeltaY[0] = 700;
	const auto Runtime = Source.ExportPoints();
	const auto Data = Source.SerializeChannels(Runtime);
	CEnvelope Loaded(CEnvelope::EType::POSITION);
	Loaded.ImportPoints(Runtime);
	ASSERT_TRUE(Loaded.DeserializeChannels(Data.data(), Data.size(), Runtime));
	EXPECT_EQ(Loaded.m_vPoints.size(), Source.m_vPoints.size());
	EXPECT_EQ(PointIndex(Loaded, 1, 300), -1);
	const auto &Restored = Loaded.m_vPoints[PointIndex(Loaded, 0, 300)];
	EXPECT_EQ(Restored.m_Curvetype, CURVETYPE_BEZIER);
	EXPECT_EQ(Restored.m_Bezier.m_aOutTangentDeltaX[0], CFixedTime(180));
	EXPECT_EQ(Restored.m_Bezier.m_aOutTangentDeltaY[0], 700);
	EXPECT_FLOAT_EQ(Evaluate(Loaded, 0.6f).r, Evaluate(Source, 0.6f).r);
}

TEST(EditorEnvelopeChannels, StaleAndMalformedMetadataLeaveImportedAnimationIntact)
{
	CEnvelope Source = PositionEnvelope();
	Source.AddPoint(CFixedTime(300), {256, 0, 0, 0}, 1);
	auto Runtime = Source.ExportPoints();
	auto Data = Source.SerializeChannels(Runtime);
	Runtime.front().m_aValues[1] += 1024;
	CEnvelope Loaded(CEnvelope::EType::POSITION);
	Loaded.ImportPoints(Runtime);
	const auto Before = Evaluate(Loaded, 0.1f);
	EXPECT_FALSE(Loaded.DeserializeChannels(Data.data(), Data.size(), Runtime));
	EXPECT_FLOAT_EQ(Evaluate(Loaded, 0.1f).g, Before.g);
	Data = Loaded.SerializeChannels(Runtime);
	Data[4] = 99;
	EXPECT_FALSE(Loaded.DeserializeChannels(Data.data(), Data.size(), Runtime));
	EXPECT_FLOAT_EQ(Evaluate(Loaded, 0.1f).g, Before.g);
	EXPECT_FALSE(Loaded.DeserializeChannels(Data.data(), 3, Runtime));
}

TEST(EditorEnvelopeChannels, AlignedLegacyAnimationKeepsOriginalRuntimeCurveTypes)
{
	std::vector<CEnvPoint_runtime> Runtime(2);
	Runtime[0].m_Time = CFixedTime(0);
	Runtime[0].m_Curvetype = CURVETYPE_SMOOTH;
	Runtime[1].m_Time = CFixedTime(1200);
	Runtime[1].m_aValues[0] = 1024;
	Runtime[1].m_aValues[1] = 2048;
	Runtime[1].m_aValues[2] = 3072;
	CEnvelope Envelope(CEnvelope::EType::POSITION);
	Envelope.ImportPoints(Runtime);
	const auto Exported = Envelope.ExportPoints();
	ASSERT_EQ(Exported.size(), 2u);
	EXPECT_EQ(Exported.front().m_Curvetype, CURVETYPE_SMOOTH);
	EXPECT_FLOAT_EQ(EvaluateRuntime(Exported, 450000, 3).g, EvaluateRuntime(Runtime, 450000, 3).g);
}

TEST(EditorEnvelopeChannels, CompatiblePlaybackPreservesDifferentPolynomialCurves)
{
	for(int Curve : {CURVETYPE_LINEAR, CURVETYPE_SLOW, CURVETYPE_FAST, CURVETYPE_SMOOTH})
	{
		SCOPED_TRACE(Curve);
		CEnvelope Envelope = PositionEnvelope();
		Envelope.m_vPoints[PointIndex(Envelope, 1, 0)].m_Curvetype = Curve;
		Envelope.AddPoint(CFixedTime(300), {256, 0, 0, 0}, 1);
		const auto Runtime = Envelope.ExportPoints();
		for(int Micros : {10000, 150000, 299000, 300000, 451000, 899000, 1199000, 1300000})
		{
			SCOPED_TRACE(Micros);
			const auto Expected = Evaluate(Envelope, Micros / 1000000.0f);
			const auto Actual = EvaluateRuntime(Runtime, Micros, 3);
			EXPECT_NEAR(Actual.r, Expected.r, 0.003f);
			EXPECT_NEAR(Actual.g, Expected.g, 0.003f);
			EXPECT_NEAR(Actual.b, Expected.b, 0.003f);
		}
	}
}

TEST(EditorEnvelopeChannels, CompatiblePlaybackPreservesStepDiscontinuity)
{
	CEnvelope Envelope = PositionEnvelope();
	Envelope.m_vPoints[PointIndex(Envelope, 1, 0)].m_Curvetype = CURVETYPE_STEP;
	Envelope.AddPoint(CFixedTime(600), {0, 1024, 0, 0}, 2);
	Envelope.m_vPoints[PointIndex(Envelope, 1, 600)].m_Curvetype = CURVETYPE_STEP;
	Envelope.AddPoint(CFixedTime(300), {256, 0, 0, 0}, 1);
	const auto Runtime = Envelope.ExportPoints();
	EXPECT_FLOAT_EQ(EvaluateRuntime(Runtime, 599999, 3).g, 0.0f);
	EXPECT_FLOAT_EQ(EvaluateRuntime(Runtime, 600000, 3).g, 1.0f);
	EXPECT_FLOAT_EQ(EvaluateRuntime(Runtime, 1199999, 3).g, 1.0f);
	EXPECT_FLOAT_EQ(EvaluateRuntime(Runtime, 1200000, 3).g, 0.0f);
}

TEST(EditorEnvelopeChannels, CompatiblePlaybackSplitsBezierWithoutReplacingItsShape)
{
	CEnvelope Envelope = PositionEnvelope();
	auto &First = Envelope.m_vPoints[PointIndex(Envelope, 1, 0)];
	auto &Last = Envelope.m_vPoints[PointIndex(Envelope, 1, 1200)];
	First.m_Curvetype = CURVETYPE_BEZIER;
	First.m_Bezier.m_aOutTangentDeltaX[1] = CFixedTime(240);
	First.m_Bezier.m_aOutTangentDeltaY[1] = 1800;
	Last.m_Bezier.m_aInTangentDeltaX[1] = CFixedTime(-180);
	Last.m_Bezier.m_aInTangentDeltaY[1] = -200;
	Envelope.AddPoint(CFixedTime(450), {384, 0, 0, 0}, 1);
	const auto Runtime = Envelope.ExportPoints();
	for(int Micros : {10000, 200000, 449000, 450000, 600000, 900000, 1199000})
	{
		SCOPED_TRACE(Micros);
		EXPECT_NEAR(EvaluateRuntime(Runtime, Micros, 3).g, Evaluate(Envelope, Micros / 1000000.0f).g, 0.005f);
	}
}
