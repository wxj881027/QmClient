#include <base/io.h>
#include <base/str.h>

#include <engine/engine.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	class CTextWorkerTestEngine final : public IEngine
	{
		CJobPool m_Pool;
		bool m_Initialized = false;
		bool m_HoldJobs = false;
		std::vector<std::shared_ptr<IJob>> m_vHeldJobs;

	public:
		void Init() override
		{
			if(m_Initialized)
				return;
			m_Pool.Init(2);
			m_Initialized = true;
		}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			if(m_HoldJobs)
			{
				m_vHeldJobs.push_back(std::move(pJob));
				return true;
			}
			return m_Initialized && m_Pool.TryAdd(std::move(pJob));
		}
		void ShutdownJobs() override
		{
			m_vHeldJobs.clear();
			if(m_Initialized)
			{
				m_Pool.Shutdown();
				m_Initialized = false;
			}
		}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}

		void SetHoldJobs(bool Hold) { m_HoldJobs = Hold; }
		size_t HeldJobCount() const { return m_vHeldJobs.size(); }
		void RunHeldJobs()
		{
			m_HoldJobs = false;
			for(const auto &pJob : m_vHeldJobs)
				(void)m_Pool.TryAdd(pJob);
			m_vHeldJobs.clear();
		}
		void DropHeldJobs() { m_vHeldJobs.clear(); }
	};

	class CTextWorkerTestConsole final : public CConsole
	{
	public:
		CTextWorkerTestConsole() :
			CConsole(CFGFLAG_CLIENT) {}
	};

	class QmTextGlyphWorker : public ::testing::Test
	{
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IKernel> m_pKernel;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		CTextWorkerTestConsole m_Console;
		CIconBenchmarkGraphics m_Graphics;
		CTextWorkerTestEngine m_Engine;
		std::unique_ptr<IEngineTextRender> m_pText;

		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmTextRasterMode = 1;
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_Graphics.m_pStorage = m_pStorage.get();
			m_Engine.Init();
			m_pKernel->RegisterInterface<IConsole>(&m_Console, false);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGraphics>(&m_Graphics, false);
			m_pKernel->RegisterInterface<IEngine>(&m_Engine, false);
			m_pText.reset(CreateEngineTextRender());
			m_pKernel->RegisterInterface<IEngineTextRender>(m_pText.get(), false);
			ASSERT_TRUE(m_pText->Init());
			ASSERT_TRUE(m_pText->LoadFonts());
		}
		void TearDown() override
		{
			// 被 hold 的 job 永远不会 Run；先销毁它们，避免文本 renderer 等 active job 归零。
			m_Engine.DropHeldJobs();
			if(m_pText != nullptr)
				m_pText->Shutdown();
			m_Engine.ShutdownJobs();
			g_Config = m_PreviousConfig;
		}

		bool WaitForPrewarm(const char *pText, int FontSize)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while(std::chrono::steady_clock::now() < Deadline)
			{
				m_pText->QmTextFrameEnd();
				if(m_pText->QmQueueTextGlyphsForPrewarm(pText, FontSize, 64))
					return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return false;
		}
	};
} // namespace

TEST_F(QmTextGlyphWorker, BackgroundPrewarmCompletesWithoutBuildingVisibleText)
{
	ASSERT_TRUE(m_pText->QmGlyphWorkerEnabled());
	const char *pText = "AQW";
	EXPECT_FALSE(m_pText->QmQueueTextGlyphsForPrewarm(pText, 18, 64));
	EXPECT_GT(m_pText->QmGlyphWorkerPendingCount(), 0);
	ASSERT_TRUE(WaitForPrewarm(pText, 18));
	EXPECT_EQ(m_pText->QmGlyphWorkerPendingCount(), 0);
	EXPECT_GT(m_pText->CalculateTextWidth(pText, 3, 0, 18), 0.0f);
}

// TextWidth 只取排版度量，不能因为冷字形把位图光栅化塞进测量调用。
// 这条约束保证设置页计划收集、卡片测量和后台预热的主线程成本可预测。
TEST_F(QmTextGlyphWorker, TextWidthMeasurementDoesNotQueueGlyphRendering)
{
	m_Engine.SetHoldJobs(true);
	const char *pText = "冷字形测量";
	EXPECT_GT(m_pText->CalculateTextWidth(pText, str_length(pText), 0, 18), 0.0f);
	EXPECT_EQ(m_pText->QmGlyphWorkerPendingCount(), 0);
	m_Engine.DropHeldJobs();
}

TEST_F(QmTextGlyphWorker, RepeatedPrewarmRequestsDoNotDuplicatePendingGlyphs)
{
	m_Engine.SetHoldJobs(true);
	const char *pText = "A";
	EXPECT_FALSE(m_pText->QmQueueTextGlyphsForPrewarm(pText, 18, 64));
	const int Pending = m_pText->QmGlyphWorkerPendingCount();
	EXPECT_GT(Pending, 0);
	EXPECT_FALSE(m_pText->QmQueueTextGlyphsForPrewarm(pText, 18, 64));
	EXPECT_EQ(m_pText->QmGlyphWorkerPendingCount(), Pending);
	m_Engine.DropHeldJobs();
}

// 关键约束：当前可见文本命中 QUEUED 时必须由主线程当场补齐，不能等待 worker。
// 用 hold 的 job 模拟 worker 完全停摆；若这里走异步等待，测试会直接卡住。
TEST_F(QmTextGlyphWorker, VisibleContainerBuildsSynchronouslyWhenWorkerIsHeld)
{
	m_Engine.SetHoldJobs(true);
	const char *pText = "W";
	EXPECT_FALSE(m_pText->QmQueueTextGlyphsForPrewarm(pText, 18, 64));
	EXPECT_GT(m_pText->QmGlyphWorkerPendingCount(), 0);

	STextContainerIndex Container;
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_FontSize = 18;
	Cursor.m_LineWidth = 200.0f;
	ASSERT_TRUE(m_pText->CreateTextContainer(Container, &Cursor, pText));
	EXPECT_TRUE(Container.Valid());
	EXPECT_GT(Cursor.m_GlyphCount, 0);
	EXPECT_NO_THROW(m_pText->RenderTextContainer(Container, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)));
	m_pText->DeleteTextContainer(Container);

	m_Engine.DropHeldJobs();
}

TEST_F(QmTextGlyphWorker, BackgroundSubmissionStaysBoundedWhenJobsAreHeld)
{
	m_Engine.SetHoldJobs(true);
	char aText[160 * 4 + 1];
	char *pCursor = aText;
	for(int Codepoint = 0x4E00; Codepoint < 0x4E00 + 160; ++Codepoint)
		pCursor += str_utf8_encode(pCursor, Codepoint);
	*pCursor = '\0';

	EXPECT_FALSE(m_pText->QmQueueTextGlyphsForPrewarm(aText, 18, 256));
	EXPECT_GT(m_pText->QmGlyphWorkerPendingCount(), 128);

	m_pText->QmTextFrameEnd();
	EXPECT_EQ(m_Engine.HeldJobCount(), 2u);
	m_pText->QmTextFrameEnd();
	EXPECT_EQ(m_Engine.HeldJobCount(), 4u);
	m_pText->QmTextFrameEnd();
	EXPECT_EQ(m_Engine.HeldJobCount(), 4u);
	m_Engine.DropHeldJobs();
}

// 待处理队列满时不能继续推进最近字形游标，否则被拒绝的条目会被永久跳过，
// 下次可见文本仍会在主线程同步光栅化。这里用 2048 个不同字号制造超容量输入。
TEST_F(QmTextGlyphWorker, RecentPrewarmStopsAtPendingCapacityWithoutSkippingEntries)
{
	constexpr const char *PATH = "qmclient/glyph_prewarm_worker_test.txt";
	ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	std::string Content;
	for(int Chr = 'A'; Chr <= 'H'; ++Chr)
	{
		for(int Size = 6; Size < 262; ++Size)
		{
			char aLine[24];
			str_format(aLine, sizeof(aLine), "%d %d\n", Chr, Size);
			Content.append(aLine);
		}
	}
	IOHANDLE File = m_pStorage->OpenFile(PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File, Content.data(), static_cast<unsigned>(Content.size())), Content.size());
	ASSERT_EQ(io_close(File), 0);

	m_pText->QmLoadRecentGlyphs(m_pStorage.get(), PATH);
	m_Engine.SetHoldJobs(true);
	EXPECT_EQ(static_cast<size_t>(m_pText->QmPrewarmRecentGlyphs(2048)), QM_GLYPH_WORKER_PENDING_CAPACITY);
	EXPECT_EQ(static_cast<size_t>(m_pText->QmGlyphWorkerPendingCount()), QM_GLYPH_WORKER_PENDING_CAPACITY);
	EXPECT_EQ(m_pText->QmPrewarmRecentGlyphs(1), 0);
	EXPECT_FALSE(m_pText->QmRecentGlyphPrewarmComplete());
	m_Engine.DropHeldJobs();
}

TEST_F(QmTextGlyphWorker, DisabledModeKeepsSynchronousPrewarmPath)
{
	g_Config.m_QmTextRasterMode = 0;
	EXPECT_FALSE(m_pText->QmGlyphWorkerEnabled());
	EXPECT_TRUE(m_pText->QmQueueTextGlyphsForPrewarm("A", 18, 64));
	EXPECT_GT(m_pText->CalculateTextWidth("A", 1, 0, 18), 0.0f);
}
