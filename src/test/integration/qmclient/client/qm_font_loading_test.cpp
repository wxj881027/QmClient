#include <test/support/qm_real_ui_fixture.h>

#if defined(CONF_FAMILY_UNIX)
#include <base/logger.h>

#include <filesystem>
#include <string_view>
#endif

class QmFontLoading : public qm_ui_test::CRealUiFixture
{
};

TEST_F(QmFontLoading, InvalidPreviewKeepsInstalledFontsAndExistingTextUsable)
{
	auto *pText = m_Ui.TextRender();
	ASSERT_TRUE(pText->QmHasCustomFace("DejaVu Sans"));
	const auto vFamilies = *pText->GetCustomFaces();
	const float Width = pText->TextWidth(16.0f, "Font lifetime");
	ASSERT_GT(Width, 0.0f);
	// 隔离存储中的随包索引是有效文件，但其内容不是字体。
	EXPECT_FALSE(pText->QmEnsurePreviewFace("Uninstalled preview fixture", "fonts/index.json"));
	EXPECT_EQ(*pText->GetCustomFaces(), vFamilies);
	EXPECT_TRUE(pText->QmHasCustomFace("DejaVu Sans"));
	EXPECT_FLOAT_EQ(pText->TextWidth(16.0f, "Font lifetime"), Width);
}

TEST_F(QmFontLoading, RepeatedReloadPreservesFamilyAndStyleLists)
{
	auto *pText = m_Ui.TextRender();
	const auto vFamilies = *pText->GetCustomFaces();
	const auto vStyles = *pText->GetCustomFontStyles("DejaVu Sans");
	ASSERT_FALSE(vFamilies.empty());
	ASSERT_FALSE(vStyles.empty());
	pText->ReloadCustomFonts();
	pText->ReloadCustomFonts();
	EXPECT_EQ(*pText->GetCustomFaces(), vFamilies);
	EXPECT_EQ(*pText->GetCustomFontStyles("DejaVu Sans"), vStyles);
	EXPECT_GT(pText->TextWidth(16.0f, "Reloaded font"), 0.0f);
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(QmFontLoading, ReloadSkipsAnOverlongFontPathAndKeepsTheBundledFallback)
{
	auto *pStorage = Graphics().m_pStorage;
	ASSERT_TRUE(pStorage->CreateFolder("fonts", IStorage::TYPE_SAVE));
	char aFontsDirectory[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "fonts", aFontsDirectory, sizeof(aFontsDirectory));
	std::string Directory = std::string(aFontsDirectory) + "/font-path-limit";
	ASSERT_LT(Directory.size(), IO_MAX_PATH_LENGTH - 35U);
	const size_t ComponentLength = (IO_MAX_PATH_LENGTH - 35 - Directory.size()) / 3;
	for(int i = 0; i < 3; ++i)
		Directory += '/' + std::string(ComponentLength, 'd');
	ASSERT_TRUE(std::filesystem::create_directories(Directory));

	// 保持目录可枚举，仅让子文件的完整路径越过存储上限。
	const std::string FontPath = Directory + '/' + std::string(48, 'f') + ".ttf";
	ASSERT_GE(FontPath.size(), IO_MAX_PATH_LENGTH);
	IOHANDLE File = io_open(FontPath.c_str(), IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_close(File), 0);

	class CFontScanLog final : public ILogger
	{
	public:
		bool m_RejectedDirectory = false;

		void Log(const CLogMessage *pMessage) override
		{
			const std::string_view Message(pMessage->Message());
			if(Message.starts_with("Failed to scan custom font directory") &&
				Message.find("font-path-limit") != std::string_view::npos)
				m_RejectedDirectory = true;
		}
	};
	CFontScanLog Logger;
	const CLogScope LogScope(&Logger);
	auto *pText = m_Ui.TextRender();
	pText->ReloadCustomFonts();

	EXPECT_TRUE(Logger.m_RejectedDirectory);
	EXPECT_TRUE(pText->QmHasCustomFace("DejaVu Sans"));
	EXPECT_GT(pText->TextWidth(16.0f, "Bundled fallback"), 0.0f);
}
#endif
