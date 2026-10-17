#include <engine/client/video_recorder_lifecycle.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <new>
#include <system_error>

namespace
{
	struct SAudioDeviceBoundary
	{
		bool m_Paused = false;
		int m_Pauses = 0;
		int m_Resumes = 0;
		void PauseAudioDevice()
		{
			EXPECT_FALSE(m_Paused);
			m_Paused = true;
			++m_Pauses;
		}
		void UnpauseAudioDevice()
		{
			EXPECT_TRUE(m_Paused);
			m_Paused = false;
			++m_Resumes;
		}
	};

	enum class EStartResult
	{
		SUCCESS,
		FAILURE,
		ALLOCATION_FAILURE,
		THREAD_FAILURE,
		UNKNOWN_FAILURE,
	};

	struct SRecorderState
	{
		int m_Starts = 0;
		int m_Stops = 0;
		int m_Destructions = 0;
	};

	// 替代编码器边界，创建、启动异常与清理编排由生产入口执行。
	class CRecorderBoundary : public IVideo
	{
		SAudioDeviceBoundary &m_Sound;
		SRecorderState &m_State;
		EStartResult m_Result;

	public:
		CRecorderBoundary(SAudioDeviceBoundary &Sound, SRecorderState &State, EStartResult Result) :
			m_Sound(Sound), m_State(State), m_Result(Result)
		{
			EXPECT_TRUE(m_Sound.m_Paused);
			EXPECT_EQ(Current(), nullptr);
			ms_pCurrentVideo = this;
		}
		~CRecorderBoundary() override
		{
			++m_State.m_Destructions;
			ms_pCurrentVideo = nullptr;
		}
		bool Start() override
		{
			EXPECT_FALSE(m_Sound.m_Paused);
			++m_State.m_Starts;
			switch(m_Result)
			{
			case EStartResult::ALLOCATION_FAILURE: throw std::bad_alloc();
			case EStartResult::THREAD_FAILURE: throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
			case EStartResult::UNKNOWN_FAILURE: throw 1;
			case EStartResult::FAILURE: return false;
			case EStartResult::SUCCESS: return true;
			}
			return false;
		}
		void Stop() override
		{
			++m_State.m_Stops;
			delete this;
		}
		void Pause(bool) override {}
		bool IsRecording() const override { return true; }
		bool HasError() const override { return false; }
		bool HasAudio() const override { return false; }
		void NextVideoFrame() override { ADD_FAILURE() << "unexpected video frame"; }
		void NextVideoFrameThread() override { ADD_FAILURE() << "unexpected video worker"; }
		void NextAudioFrame(ISoundMixFunc) override { ADD_FAILURE() << "unexpected audio frame"; }
		void NextAudioFrameTimeline(ISoundMixFunc) override { ADD_FAILURE() << "unexpected audio timeline"; }
	};

	class VideoRecorderLifecycle : public ::testing::Test
	{
	protected:
		SAudioDeviceBoundary m_Sound;
		SRecorderState m_State;
		void SetUp() override { ASSERT_EQ(IVideo::Current(), nullptr); }
		void TearDown() override
		{
			if(IVideo::Current())
				IVideo::Current()->Stop();
			EXPECT_FALSE(m_Sound.m_Paused);
			EXPECT_EQ(m_Sound.m_Pauses, m_Sound.m_Resumes);
		}
		bool Start(EStartResult Result)
		{
			return StartVideoRecorder(m_Sound, [&] { return new CRecorderBoundary(m_Sound, m_State, Result); });
		}
		void ExpectReleasedFailure()
		{
			EXPECT_EQ(IVideo::Current(), nullptr);
			EXPECT_EQ(m_State.m_Starts, 1);
			EXPECT_EQ(m_State.m_Stops, 1);
			EXPECT_EQ(m_State.m_Destructions, 1);
		}
	};

	struct SWorkerState
	{
		std::atomic<int> m_Exited{0};
		std::atomic<bool> m_TimedOut{false};
		int m_Destroyed = 0;
	};

	class CObservedRecorderWorker : public CVideoRecorderWorker
	{
		SWorkerState &m_State;

	public:
		explicit CObservedRecorderWorker(SWorkerState &State) :
			m_State(State) {}
		~CObservedRecorderWorker()
		{
			EXPECT_FALSE(m_Thread.joinable());
			++m_State.m_Destroyed;
		}
		void Start()
		{
			m_Thread = std::thread([this] {
				std::unique_lock<std::mutex> Lock(m_Mutex);
				if(!m_Cond.wait_for(Lock, std::chrono::seconds(2), [this] { return m_Finished; }))
					m_State.m_TimedOut = true;
				++m_State.m_Exited;
			});
		}
	};
}

TEST_F(VideoRecorderLifecycle, SuccessfulStartResumesAudioBeforeStartingAndKeepsTheRecorder)
{
	ASSERT_TRUE(Start(EStartResult::SUCCESS));
	ASSERT_NE(IVideo::Current(), nullptr);
	EXPECT_EQ(m_State.m_Starts, 1);
	EXPECT_EQ(m_State.m_Stops, 0);
	EXPECT_EQ(m_State.m_Destructions, 0);
	EXPECT_EQ(m_Sound.m_Resumes, 1);
}

TEST_F(VideoRecorderLifecycle, ConstructionAllocationFailureResumesAudioWithoutStoppingAnAbsentRecorder)
{
	EXPECT_FALSE(StartVideoRecorder(m_Sound, [&]() -> IVideo * {
		EXPECT_TRUE(m_Sound.m_Paused);
		throw std::bad_alloc();
	}));
	EXPECT_EQ(IVideo::Current(), nullptr);
	EXPECT_EQ(m_State.m_Stops, 0);
	EXPECT_EQ(m_Sound.m_Resumes, 1);
}

TEST_F(VideoRecorderLifecycle, FalseStartReleasesThePartiallyInitializedRecorder)
{
	EXPECT_FALSE(Start(EStartResult::FAILURE));
	ExpectReleasedFailure();
}

TEST_F(VideoRecorderLifecycle, BufferAllocationExceptionReleasesThePartiallyInitializedRecorder)
{
	EXPECT_FALSE(Start(EStartResult::ALLOCATION_FAILURE));
	ExpectReleasedFailure();
}

TEST_F(VideoRecorderLifecycle, ThreadCreationExceptionReleasesThePartiallyInitializedRecorder)
{
	EXPECT_FALSE(Start(EStartResult::THREAD_FAILURE));
	ExpectReleasedFailure();
}

TEST_F(VideoRecorderLifecycle, UnknownStartExceptionStillReleasesTheRecorder)
{
	EXPECT_FALSE(Start(EStartResult::UNKNOWN_FAILURE));
	ExpectReleasedFailure();
}

TEST_F(VideoRecorderLifecycle, ANewRecordingCanStartAfterThePreviousStartupFailed)
{
	ASSERT_FALSE(Start(EStartResult::THREAD_FAILURE));
	ASSERT_EQ(IVideo::Current(), nullptr);
	ASSERT_TRUE(Start(EStartResult::SUCCESS));
	ASSERT_NE(IVideo::Current(), nullptr);
	IVideo::Current()->Stop();
	EXPECT_EQ(IVideo::Current(), nullptr);
	EXPECT_EQ(m_State.m_Stops, 2);
	EXPECT_EQ(m_State.m_Destructions, 2);
}

TEST(VideoRecorderWorkers, EmptyAndMissingWorkersCanBeReleasedRepeatedly)
{
	std::vector<std::unique_ptr<CVideoRecorderWorker>> vpWorkers(3);
	StopVideoRecorderThreads(vpWorkers);
	EXPECT_TRUE(vpWorkers.empty());
	StopVideoRecorderThreads(vpWorkers);
	EXPECT_TRUE(vpWorkers.empty());
}

TEST(VideoRecorderWorkers, AllocatedWorkersWithoutThreadsAreDestroyedWithoutJoining)
{
	SWorkerState State;
	std::vector<std::unique_ptr<CObservedRecorderWorker>> vpWorkers;
	for(int i = 0; i < 3; ++i)
		vpWorkers.push_back(std::make_unique<CObservedRecorderWorker>(State));
	StopVideoRecorderThreads(vpWorkers);
	EXPECT_TRUE(vpWorkers.empty());
	EXPECT_EQ(State.m_Destroyed, 3);
	EXPECT_EQ(State.m_Exited, 0);
}

TEST(VideoRecorderWorkers, PartialStartupJoinsRunningWorkersBeforeDestroyingTheirState)
{
	SWorkerState State;
	std::vector<std::unique_ptr<CObservedRecorderWorker>> vpWorkers(4);
	vpWorkers[0] = std::make_unique<CObservedRecorderWorker>(State);
	vpWorkers[2] = std::make_unique<CObservedRecorderWorker>(State);
	vpWorkers[3] = std::make_unique<CObservedRecorderWorker>(State);
	vpWorkers[0]->Start();
	vpWorkers[2]->Start();
	StopVideoRecorderThreads(vpWorkers);
	EXPECT_TRUE(vpWorkers.empty());
	EXPECT_EQ(State.m_Exited, 2);
	EXPECT_EQ(State.m_Destroyed, 3);
	EXPECT_FALSE(State.m_TimedOut);
}
