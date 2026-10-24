#include <engine/client/backend/backend_base.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	class CDiagnosticFragment : public CCommandProcessorFragment_GLBase
	{
		bool GetPresentedImageData(uint32_t &, uint32_t &, CImageInfo::EImageFormat &, std::vector<uint8_t> &) override { return false; }

	public:
		ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *) override { return RUN_COMMAND_COMMAND_HANDLED; }
		void SetError()
		{
			m_Error = {GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, {{false, std::string(4096, 'd')}, {true, "driver advice"}}};
		}
		void SetWarning()
		{
			m_Warning = {GFX_WARNING_TYPE_INIT_FAILED, {std::string(4096, 'w'), "fallback advice"}};
		}
	};
}

TEST(GraphicsDiagnosticTransfer, ErrorMovesExistingStorageAndKeepsTheFailureMarker)
{
	CDiagnosticFragment Fragment;
	Fragment.SetError();
	const auto *pStorage = Fragment.GetError().m_vErrors.data();
	const auto *pText = Fragment.GetError().m_vErrors[0].m_Err.data();
	const auto Error = Fragment.TakeError();
	ASSERT_EQ(Error.m_vErrors.size(), 2U);
	EXPECT_EQ(Error.m_ErrorType, GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER);
	EXPECT_EQ(Error.m_vErrors.data(), pStorage);
	EXPECT_EQ(Error.m_vErrors[0].m_Err.data(), pText);
	EXPECT_FALSE(Error.m_vErrors[0].m_RequiresTranslation);
	EXPECT_TRUE(Error.m_vErrors[1].m_RequiresTranslation);
	EXPECT_EQ(Error.m_vErrors[1].m_Err, "driver advice");
	EXPECT_TRUE(Fragment.GetError().m_vErrors.empty());
	EXPECT_EQ(Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER);
}

TEST(GraphicsDiagnosticTransfer, TransferredErrorOutlivesTheFragment)
{
	SGfxErrorContainer Error;
	{
		CDiagnosticFragment Fragment;
		Fragment.SetError();
		Error = Fragment.TakeError();
	}
	ASSERT_EQ(Error.m_vErrors.size(), 2U);
	EXPECT_EQ(Error.m_vErrors[0].m_Err, std::string(4096, 'd'));
	EXPECT_EQ(Error.m_vErrors[1].m_Err, "driver advice");
}

TEST(GraphicsDiagnosticTransfer, ErrorPayloadIsDeliveredOnceAndCanBeReplaced)
{
	CDiagnosticFragment Fragment;
	Fragment.SetError();
	const auto First = Fragment.TakeError();
	EXPECT_TRUE(Fragment.TakeError().m_vErrors.empty());
	Fragment.SetError();
	const auto Next = Fragment.TakeError();
	EXPECT_EQ(First.m_vErrors, Next.m_vErrors);
	EXPECT_EQ(Next.m_ErrorType, GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER);
}

TEST(GraphicsDiagnosticTransfer, WarningMovesExistingStorageAndKeepsTheWarningMarker)
{
	CDiagnosticFragment Fragment;
	Fragment.SetWarning();
	const auto *pStorage = Fragment.GetWarning().m_vWarnings.data();
	const auto *pText = Fragment.GetWarning().m_vWarnings[0].data();
	const auto Warning = Fragment.TakeWarning();
	ASSERT_EQ(Warning.m_vWarnings.size(), 2U);
	EXPECT_EQ(Warning.m_WarningType, GFX_WARNING_TYPE_INIT_FAILED);
	EXPECT_EQ(Warning.m_vWarnings.data(), pStorage);
	EXPECT_EQ(Warning.m_vWarnings[0].data(), pText);
	EXPECT_EQ(Warning.m_vWarnings[1], "fallback advice");
	EXPECT_TRUE(Fragment.GetWarning().m_vWarnings.empty());
	EXPECT_EQ(Fragment.GetWarning().m_WarningType, GFX_WARNING_TYPE_INIT_FAILED);
}

TEST(GraphicsDiagnosticTransfer, TransferredWarningOutlivesTheFragment)
{
	SGfxWarningContainer Warning;
	{
		CDiagnosticFragment Fragment;
		Fragment.SetWarning();
		Warning = Fragment.TakeWarning();
	}
	EXPECT_EQ(Warning.m_vWarnings, (std::vector<std::string>{std::string(4096, 'w'), "fallback advice"}));
}

TEST(GraphicsDiagnosticTransfer, EmptyFragmentHasNoDiagnosticPayload)
{
	CDiagnosticFragment Fragment;
	const auto Error = Fragment.TakeError();
	const auto Warning = Fragment.TakeWarning();
	EXPECT_EQ(Error.m_ErrorType, GFX_ERROR_TYPE_NONE);
	EXPECT_TRUE(Error.m_vErrors.empty());
	EXPECT_EQ(Warning.m_WarningType, GFX_WARNING_TYPE_NONE);
	EXPECT_TRUE(Warning.m_vWarnings.empty());
}
