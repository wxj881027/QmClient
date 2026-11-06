#include <game/client/components/qmclient/demo_camera_file.h>

#include <gtest/gtest.h>

#include <limits>

using namespace qm_demo_camera;

namespace
{
	class CJsonFaults : public CFileOperations
	{
	public:
		size_t m_Calls = 0, m_FailAt = 0;
		bool m_Injected = false;
		void *AllocateJson(size_t Size, int Zero) noexcept override
		{
			if(++m_Calls == m_FailAt)
			{
				m_Injected = true;
				return nullptr;
			}
			return CFileOperations::AllocateJson(Size, Zero);
		}
		void Reset(size_t FailAt = 0)
		{
			m_Calls = 0;
			m_FailAt = FailAt;
			m_Injected = false;
		}
	};
	class QmDemoCameraDecode : public ::testing::Test
	{
	protected:
		SProject m_Previous;
		std::string m_Json;
		void SetUp() override
		{
			m_Previous.m_Enabled = true;
			ASSERT_GE(m_Previous.m_Track.Insert({100.0, {{5.0f, 6.0f}, 1.5f}}), 0);
			SProject Incoming;
			ASSERT_GE(Incoming.m_Track.Insert({100.0, {{50.0f, 60.0f}, 2.0f}}), 0);
			m_Json = Encode(Incoming);
		}
		void ReplaceNumber(std::string &Json, const char *pKey, const char *pValue)
		{
			const std::string Marker = std::string("\"") + pKey + "\":";
			const size_t At = Json.find(Marker);
			ASSERT_NE(At, std::string::npos);
			const size_t Start = At + Marker.size();
			const size_t End = Json.find_first_of(",}", Start);
			ASSERT_NE(End, std::string::npos);
			Json.replace(Start, End - Start, pValue);
		}
		void ExpectPrevious(const SProject &Project)
		{
			EXPECT_TRUE(Project.m_Enabled);
			ASSERT_EQ(Project.m_Track.Frames().size(), 1u);
			EXPECT_EQ(Project.m_Track.Frames()[0].m_View.m_Center, vec2(5.0f, 6.0f));
			EXPECT_FLOAT_EQ(Project.m_Track.Frames()[0].m_View.m_Zoom, 1.5f);
		}
	};
}

TEST(QmDemoCameraFile, RoundTripPreservesEnabledStateAspectAndFractionalKeyframeTimes)
{
	SProject Source;
	Source.m_Enabled = true;
	ASSERT_TRUE(Source.m_Track.SetAspect(1.5f));
	ASSERT_GE(Source.m_Track.Insert({101.125, {{10.0f, -20.0f}, 1.25f}, ETransition::LINEAR}), 0);
	ASSERT_GE(Source.m_Track.Insert({201.5, {{30.0f, 40.0f}, 2.0f}}), 0);
	SProject Restored;
	ASSERT_TRUE(Decode(Encode(Source), Restored));
	EXPECT_TRUE(Restored.m_Enabled);
	EXPECT_FLOAT_EQ(Restored.m_Track.Aspect(), 1.5f);
	ASSERT_EQ(Restored.m_Track.Frames().size(), 2U);
	EXPECT_DOUBLE_EQ(Restored.m_Track.Frames()[0].m_Tick, 101.125);
	const auto RestoredSample = Restored.m_Track.Sample(151.0);
	const auto SourceSample = Source.m_Track.Sample(151.0);
	ASSERT_TRUE(RestoredSample.has_value());
	ASSERT_TRUE(SourceSample.has_value());
	EXPECT_EQ(RestoredSample->m_Center, SourceSample->m_Center);
	EXPECT_FLOAT_EQ(RestoredSample->m_Zoom, SourceSample->m_Zoom);
}

TEST(QmDemoCameraFile, CroppedEasingSurvivesSavingAndReloading)
{
	SProject Source;
	ASSERT_GE(Source.m_Track.Insert({100.0, {{0.0f, 0.0f}, 1.0f}}), 0);
	ASSERT_GE(Source.m_Track.Insert({200.0, {{100.0f, 0.0f}, 2.0f}}), 0);
	const auto Cut = Source.m_Track.Cut({{120, 180}});
	ASSERT_TRUE(Cut.has_value());
	SProject Restored;
	ASSERT_TRUE(Decode(Encode({*Cut, true}), Restored));
	const auto RestoredSample = Restored.m_Track.Sample(140.0);
	const auto SourceSample = Source.m_Track.Sample(140.0);
	ASSERT_TRUE(RestoredSample.has_value());
	ASSERT_TRUE(SourceSample.has_value());
	EXPECT_NEAR(RestoredSample->m_Center.x, SourceSample->m_Center.x, 0.0001f);
}

TEST(QmDemoCameraFile, InvalidOrUnsupportedFilesDoNotReplaceTheCurrentProject)
{
	SProject Project;
	ASSERT_GE(Project.m_Track.Insert({10.0, {{5.0f, 6.0f}, 1.0f}}), 0);
	for(const char *pJson : {
		    "invalid", "[]", "{}",
		    "{\"version\":2,\"enabled\":true,\"aspect\":1.5,\"keyframes\":[]}",
		    "{\"version\":1,\"enabled\":true,\"aspect\":0,\"keyframes\":[]}",
		    "{\"version\":1,\"enabled\":true,\"aspect\":1.5,\"keyframes\":[{}]}"})
	{
		SCOPED_TRACE(pJson);
		EXPECT_FALSE(Decode(pJson, Project));
		ASSERT_EQ(Project.m_Track.Frames().size(), 1U);
		const auto Sample = Project.m_Track.Sample(10.0);
		ASSERT_TRUE(Sample.has_value());
		EXPECT_EQ(Sample->m_Center, vec2(5.0f, 6.0f));
	}
	EXPECT_FALSE(Decode(std::string(512 * 1024 + 1, ' '), Project));
}

TEST(QmDemoCameraFile, EmptyProjectPersistsAUserDisabledPath)
{
	SProject Restored;
	Restored.m_Enabled = true;
	ASSERT_TRUE(Decode(Encode({}), Restored));
	EXPECT_TRUE(Restored.m_Track.Empty());
	EXPECT_FALSE(Restored.m_Enabled);
	EXPECT_TRUE(SidecarPath("").empty());
	EXPECT_EQ(SidecarPath("demos/race.demo"), "demos/race.demo.qm-camera.json");
}

TEST(QmDemoCameraFile, DuplicateTimesAndInvalidZoomAreRejected)
{
	SProject Source;
	ASSERT_GE(Source.m_Track.Insert({10.0, {{5.0f, 6.0f}, 1.0f}}), 0);
	ASSERT_GE(Source.m_Track.Insert({20.0, {{7.0f, 8.0f}, 2.0f}}), 0);
	std::string Json = Encode(Source);
	const size_t Tick = Json.find("\"tick\":20");
	ASSERT_NE(Tick, std::string::npos);
	Json.replace(Tick, 9, "\"tick\":10");
	SProject Restored;
	EXPECT_FALSE(Decode(Json, Restored));
	Json = Encode(Source);
	const size_t Zoom = Json.find("\"zoom\":1");
	ASSERT_NE(Zoom, std::string::npos);
	Json.replace(Zoom, 8, "\"zoom\":0");
	EXPECT_FALSE(Decode(Json, Restored));
}

TEST_F(QmDemoCameraDecode, FiniteNumbersBeyondFloatRangeAreRejectedWithoutReplacingEdits)
{
	for(const char *pKey : {"aspect", "x", "y", "zoom"})
		for(const char *pValue : {"1e300", "-1e300", "3.5e38", "-3.5e38"})
		{
			SCOPED_TRACE(pKey);
			SCOPED_TRACE(pValue);
			std::string Json = m_Json;
			ASSERT_NO_FATAL_FAILURE(ReplaceNumber(Json, pKey, pValue));
			SProject Project = m_Previous;
			EXPECT_EQ(DecodeWithResult(Json, Project), ELoadResult::FAILED);
			ExpectPrevious(Project);
		}
}

TEST_F(QmDemoCameraDecode, TinyCenterValuesRemainLegalInputs)
{
	ASSERT_NO_FATAL_FAILURE(ReplaceNumber(m_Json, "x", "1e-300"));
	ASSERT_NO_FATAL_FAILURE(ReplaceNumber(m_Json, "y", "-1e-300"));
	SProject Project;
	ASSERT_EQ(DecodeWithResult(m_Json, Project), ELoadResult::LOADED);
	ASSERT_EQ(Project.m_Track.Frames().size(), 1u);
	const auto Center = Project.m_Track.Frames()[0].m_View.m_Center;
	EXPECT_LE(std::abs(Center.x), std::numeric_limits<float>::min());
	EXPECT_LE(std::abs(Center.y), std::numeric_limits<float>::min());
}

TEST_F(QmDemoCameraDecode, RepresentableNumbersStillRespectCameraDomainBounds)
{
	const std::pair<const char *, const char *> aValues[] = {{"aspect", "20"}, {"x", "20000000"}, {"y", "-20000000"}, {"zoom", "241"}, {"tick", "2147483648"}};
	for(const auto &[pKey, pValue] : aValues)
	{
		SCOPED_TRACE(pKey);
		std::string Json = m_Json;
		ASSERT_NO_FATAL_FAILURE(ReplaceNumber(Json, pKey, pValue));
		SProject Project = m_Previous;
		EXPECT_EQ(DecodeWithResult(Json, Project), ELoadResult::FAILED);
		ExpectPrevious(Project);
	}
}

TEST_F(QmDemoCameraDecode, EachNativeParserAllocationFailureIsRetryableAndPreservesEdits)
{
	CJsonFaults Operations;
	SProject Probe;
	ASSERT_EQ(DecodeWithResult(m_Json, Probe, &Operations), ELoadResult::LOADED);
	const size_t Calls = Operations.m_Calls;
	ASSERT_GT(Calls, 0u);
	ASSERT_LE(Calls, 256u);
	for(size_t FailAt = 1; FailAt <= Calls; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		Operations.Reset(FailAt);
		SProject Project = m_Previous;
		EXPECT_EQ(DecodeWithResult(m_Json, Project, &Operations), ELoadResult::UNAVAILABLE);
		EXPECT_TRUE(Operations.m_Injected);
		ExpectPrevious(Project);
		Operations.Reset();
		ASSERT_EQ(DecodeWithResult(m_Json, Project, &Operations), ELoadResult::LOADED);
		EXPECT_FALSE(Project.m_Enabled);
		ASSERT_EQ(Project.m_Track.Frames().size(), 1u);
		EXPECT_EQ(Project.m_Track.Frames()[0].m_View.m_Center, vec2(50.0f, 60.0f));
	}
}

TEST_F(QmDemoCameraDecode, MalformedInputRemainsPermanentFailure)
{
	CJsonFaults Operations;
	SProject Project = m_Previous;
	EXPECT_EQ(DecodeWithResult("{", Project, &Operations), ELoadResult::FAILED);
	EXPECT_FALSE(Operations.m_Injected);
	ExpectPrevious(Project);
}

TEST_F(QmDemoCameraDecode, InvalidUtf8RemainsPermanentFailure)
{
	m_Json.pop_back();
	m_Json.pop_back();
	m_Json += ",\"extra\":\"";
	m_Json.push_back(static_cast<char>(0xff));
	m_Json += "\"}";
	CJsonFaults Operations;
	SProject Project = m_Previous;
	EXPECT_EQ(DecodeWithResult(m_Json, Project, &Operations), ELoadResult::FAILED);
	EXPECT_FALSE(Operations.m_Injected);
	ExpectPrevious(Project);
}

TEST_F(QmDemoCameraDecode, ParserBudgetRejectionIsNotReportedAsTransientHeapFailure)
{
	std::string Json = R"({"version":1,"enabled":false,"aspect":1.5,"keyframes":[],"padding":[)";
	Json.reserve(450000);
	for(int i = 0; i < 200000; ++i)
		Json += "0,";
	Json.back() = ']';
	Json += '}';
	ASSERT_LT(Json.size(), 512u * 1024u);
	CJsonFaults Operations;
	SProject Project = m_Previous;
	EXPECT_EQ(DecodeWithResult(Json, Project, &Operations), ELoadResult::FAILED);
	EXPECT_FALSE(Operations.m_Injected);
	ExpectPrevious(Project);
}
