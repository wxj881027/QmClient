#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <memory>
#include <new>

namespace
{
	class CInitializationGraphics : public CIconBenchmarkGraphics
	{
	public:
		enum class EFailure
		{
			NONE,
			REJECT,
			THROW
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Attempts = 0;
		int m_LiveTextures = 0;
		int m_UnloadedTextures = 0;
		bool LoadTextTextures(size_t Width, size_t Height, CTextureHandle &Fill, CTextureHandle &Outline, uint8_t *pFill, uint8_t *pOutline) override
		{
			++m_Attempts;
			if(m_Failure != EFailure::NONE)
			{
				// 设备入口始终接管两份上传缓冲，故障只发生在设备边界。
				free(pFill);
				free(pOutline);
				if(m_Failure == EFailure::THROW)
					throw std::bad_alloc();
				return false;
			}
			const bool Result = CIconBenchmarkGraphics::LoadTextTextures(Width, Height, Fill, Outline, pFill, pOutline);
			if(Result)
				m_LiveTextures += 2;
			return Result;
		}
		bool UnloadTextTextures(CTextureHandle &Fill, CTextureHandle &Outline) override
		{
			const int Count = static_cast<int>(Fill.IsValid()) + static_cast<int>(Outline.IsValid());
			m_LiveTextures -= Count;
			m_UnloadedTextures += Count;
			return CIconBenchmarkGraphics::UnloadTextTextures(Fill, Outline);
		}
	};

	class CInitializationConsole : public CConsole
	{
	public:
		bool m_FailRegistration = false;
		CInitializationConsole() :
			CConsole(CFGFLAG_CLIENT) {}
		void Register(const char *pName, const char *pParams, int Flags, FCommandCallback pfnFunc, void *pUser, const char *pHelp) override
		{
			if(m_FailRegistration && str_comp(pName, "qm_font_diagnostics") == 0)
				throw std::bad_alloc();
			CConsole::Register(pName, pParams, Flags, pfnFunc, pUser, pHelp);
		}
	};

	class QmTextInitialization : public ::testing::Test
	{
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		CInitializationConsole m_Console;
		CInitializationGraphics m_Graphics;
		std::unique_ptr<IEngineTextRender> m_pText;

		void SetUp() override
		{
			g_Config = CConfig();
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_Graphics.m_pStorage = m_pStorage.get();
			m_pKernel->RegisterInterface<IConsole>(&m_Console, false);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGraphics>(&m_Graphics, false);
			m_pText.reset(CreateEngineTextRender());
			m_pKernel->RegisterInterface<IEngineTextRender>(m_pText.get(), false);
		}
		void TearDown() override
		{
			if(m_pText != nullptr)
				m_pText->Shutdown();
			g_Config = m_PreviousConfig;
		}
	};
}

TEST_F(QmTextInitialization, RejectedInitialUploadReturnsFailureAndClearsState)
{
	m_Graphics.m_Failure = CInitializationGraphics::EFailure::REJECT;
	EXPECT_FALSE(m_pText->Init());
	EXPECT_EQ(m_pText->GlyphAtlasRevision(), 0u);
	EXPECT_EQ(m_Graphics.m_LiveTextures, 0);
	EXPECT_EQ(m_Graphics.m_Attempts, 1);
	m_pText->Shutdown();
	EXPECT_EQ(m_Graphics.m_UnloadedTextures, 0);
}

TEST_F(QmTextInitialization, RejectedInitializationCanRetryAndLoadFonts)
{
	m_Graphics.m_Failure = CInitializationGraphics::EFailure::REJECT;
	ASSERT_FALSE(m_pText->Init());
	m_Graphics.m_Failure = CInitializationGraphics::EFailure::NONE;
	ASSERT_TRUE(m_pText->Init());
	ASSERT_TRUE(m_pText->LoadFonts());
	EXPECT_GT(m_pText->CalculateTextWidth("A", 1, 0, 16), 0);
	EXPECT_EQ(m_Graphics.m_LiveTextures, 2);
}

TEST_F(QmTextInitialization, UploadExceptionBecomesAnInitializationFailure)
{
	m_Graphics.m_Failure = CInitializationGraphics::EFailure::THROW;
	EXPECT_FALSE(m_pText->Init());
	EXPECT_EQ(m_pText->GlyphAtlasRevision(), 0u);
	EXPECT_EQ(m_Graphics.m_LiveTextures, 0);
	m_Graphics.m_Failure = CInitializationGraphics::EFailure::NONE;
	EXPECT_TRUE(m_pText->Init());
}

TEST_F(QmTextInitialization, FailureAfterUploadReleasesTexturesBeforeReturning)
{
	m_Console.m_FailRegistration = true;
	EXPECT_FALSE(m_pText->Init());
	EXPECT_EQ(m_Graphics.m_UnloadedTextures, 2);
	EXPECT_EQ(m_Graphics.m_LiveTextures, 0);
	EXPECT_EQ(m_pText->GlyphAtlasRevision(), 0u);
	m_Console.m_FailRegistration = false;
	EXPECT_TRUE(m_pText->Init());
	EXPECT_EQ(m_Graphics.m_LiveTextures, 2);
}

TEST_F(QmTextInitialization, RepeatedInitializationKeepsTheExistingTextures)
{
	ASSERT_TRUE(m_pText->Init());
	const uint64_t Revision = m_pText->GlyphAtlasRevision();
	EXPECT_TRUE(m_pText->Init());
	EXPECT_EQ(m_pText->GlyphAtlasRevision(), Revision);
	EXPECT_EQ(m_Graphics.m_Attempts, 1);
	EXPECT_EQ(m_Graphics.m_LiveTextures, 2);
}

TEST_F(QmTextInitialization, RepeatedShutdownReleasesEachTextureOnce)
{
	ASSERT_TRUE(m_pText->Init());
	m_pText->Shutdown();
	EXPECT_EQ(m_Graphics.m_LiveTextures, 0);
	EXPECT_EQ(m_Graphics.m_UnloadedTextures, 2);
	m_pText->Shutdown();
	EXPECT_EQ(m_Graphics.m_UnloadedTextures, 2);
}
