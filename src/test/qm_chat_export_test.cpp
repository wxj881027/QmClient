#include "test.h"

#include <engine/storage.h>

#include <game/client/components/qmclient/qm_chat_export.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace
{

	// 固定度量的等宽假字形：每个码点 10 像素宽、8 像素高，便于精确断言换行与排版。
	QmChatExport::TGlyphs MakeGlyphs(std::initializer_list<int> FontSizes, int Advance = 10, int GlyphWidth = 8, int GlyphHeight = 8)
	{
		QmChatExport::TGlyphs Glyphs;
		for(const int FontSize : FontSizes)
		{
			QmChatExport::SGlyph Glyph;
			Glyph.m_Width = GlyphWidth;
			Glyph.m_Height = GlyphHeight;
			Glyph.m_Advance = Advance;
			Glyph.m_vAlpha.assign((size_t)GlyphWidth * GlyphHeight, 255);
			Glyph.m_OffsetY = 0;
			Glyphs.emplace(std::make_pair(FontSize, (int)'?'), Glyph);
			Glyphs.emplace(std::make_pair(FontSize, (int)' '), Glyph);
			for(char Character = 'a'; Character <= 'z'; ++Character)
				Glyphs.emplace(std::make_pair(FontSize, (int)Character), Glyph);
		}
		return Glyphs;
	}

	QmChatExport::SLine MakeLine(const std::string &Sender, const std::string &Message, bool Local = false)
	{
		QmChatExport::SLine Line;
		Line.m_Raw = Sender + ": " + Message;
		Line.m_Sender = Sender;
		Line.m_Message = Message;
		Line.m_Time = "12:00:00";
		Line.m_Local = Local;
		return Line;
	}

	uint8_t PageAlphaAt(const std::vector<uint8_t> &Pixels, int X, int Y)
	{
		return Pixels[((size_t)Y * QmChatExport::IMAGE_WIDTH + X) * 4 + 3];
	}

	std::array<uint8_t, 3> PageColorAt(const std::vector<uint8_t> &Pixels, int X, int Y)
	{
		const size_t Offset = ((size_t)Y * QmChatExport::IMAGE_WIDTH + X) * 4;
		return {Pixels[Offset], Pixels[Offset + 1], Pixels[Offset + 2]};
	}

	// 每个测试用独立的隔离存储目录，避免依赖真实用户目录。
	class QmChatExportTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			// 导出本身不建目录：调用方（后台任务）负责先准备好落盘目录。
			ASSERT_TRUE(m_pStorage->CreateFolder("qm_chat_export_test", IStorage::TYPE_SAVE));
		}
	};

} // namespace

TEST(QmChatExportLayout, AdvanceFallsBackToTheUnknownGlyphAndThenToASizeBasedDefault)
{
	auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE}, 12);
	// 已存在的字形用自己的步进。
	EXPECT_EQ(QmChatExport::Advance(Glyphs, QmChatExport::FONT_MESSAGE, 'a'), 12);
	// 没有的字形退回 '?' 的度量。
	EXPECT_EQ(QmChatExport::Advance(Glyphs, QmChatExport::FONT_MESSAGE, 0x4e2d), 12);
	// 连 '?' 都没有时给出与字号相关的兜底宽度，且不返回 0（否则会死循环）。
	const QmChatExport::TGlyphs Empty;
	EXPECT_EQ(QmChatExport::Advance(Empty, QmChatExport::FONT_MESSAGE, 'a'), QmChatExport::FONT_MESSAGE / 2);
}

TEST(QmChatExportLayout, TextWidthCountsEveryCodepointIncludingMultibyteOnes)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE}, 10);
	EXPECT_EQ(QmChatExport::TextWidth("abc", Glyphs, QmChatExport::FONT_MESSAGE), 30);
	// 多字节码点也必须整体计一次宽度，不能按字节算。
	EXPECT_EQ(QmChatExport::TextWidth("中", Glyphs, QmChatExport::FONT_MESSAGE), 10);
	EXPECT_EQ(QmChatExport::TextWidth("a中b", Glyphs, QmChatExport::FONT_MESSAGE), 30);
}

TEST(QmChatExportLayout, WrapBreaksOnMeasuredWidthAndKeepsExplicitNewlines)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE}, 10);

	// 每个字 10 宽，上限 35：每行最多 3 个字符。
	const auto vWrapped = QmChatExport::Wrap("abcdefg", Glyphs, QmChatExport::FONT_MESSAGE, 35);
	ASSERT_EQ(vWrapped.size(), 3u);
	EXPECT_EQ(vWrapped[0], "abc");
	EXPECT_EQ(vWrapped[1], "def");
	EXPECT_EQ(vWrapped[2], "g");

	// 显式换行必须保留空行，'\r' 被丢弃。
	const auto vExplicit = QmChatExport::Wrap("a\r\nb", Glyphs, QmChatExport::FONT_MESSAGE, 1000);
	ASSERT_EQ(vExplicit.size(), 2u);
	EXPECT_EQ(vExplicit[0], "a");
	EXPECT_EQ(vExplicit[1], "b");
}

TEST(QmChatExportLayout, WrapStopsEarlyWhenCancelled)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE}, 10);
	std::atomic<bool> Cancelled{true};
	EXPECT_TRUE(QmChatExport::Wrap("abcdef", Glyphs, QmChatExport::FONT_MESSAGE, 35, &Cancelled).empty());
}

TEST(QmChatExportLayout, BuildPagesSplitsLongMessagesWithoutTruncatingThem)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME}, 10);
	// 足够多的行，使单条记录必须按图片高度上限切分成多条记录。
	std::string Message;
	for(int i = 0; i < 2000; ++i)
		Message += "abcdefghij";

	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", Message)};
	const auto vPages = QmChatExport::BuildPages(vLines, Glyphs);
	ASSERT_FALSE(vPages.empty());

	// 全文的换行结果必须完整落在各页记录里，不能被截断。
	const auto vAllWrapped = QmChatExport::Wrap(Message, Glyphs, QmChatExport::FONT_MESSAGE, QmChatExport::BUBBLE_MAX_WIDTH - QmChatExport::BUBBLE_PADDING * 2);
	size_t TotalLines = 0;
	for(const auto &Page : vPages)
	{
		EXPECT_LE(Page.m_Height, QmChatExport::MAX_IMAGE_HEIGHT);
		for(const auto &Record : Page.m_vRecords)
			TotalLines += Record.m_vMessageLines.size();
	}
	EXPECT_EQ(TotalLines, vAllWrapped.size());
}

TEST(QmChatExportLayout, BuildPagesStartsANewPageInsteadOfOverflowingTheImage)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME}, 10);
	std::vector<QmChatExport::SLine> vLines;
	for(int i = 0; i < 400; ++i)
		vLines.push_back(MakeLine("Alice", "abcdefghij"));

	const auto vPages = QmChatExport::BuildPages(vLines, Glyphs);
	ASSERT_GT(vPages.size(), 1u);
	for(const auto &Page : vPages)
		EXPECT_LE(Page.m_Height, QmChatExport::MAX_IMAGE_HEIGHT);

	// 所有消息都必须出现且顺序不变。
	size_t TotalRecords = 0;
	for(const auto &Page : vPages)
	{
		for(const auto &Record : Page.m_vRecords)
			EXPECT_LT(Record.m_LineIndex, vLines.size());
		TotalRecords += Page.m_vRecords.size();
	}
	EXPECT_EQ(TotalRecords, vLines.size());
}

TEST(QmChatExportHtml, AppendEscapedNeutralizesMarkup)
{
	std::string Output;
	QmChatExport::AppendEscaped(Output, "<b>\"x\" & 'y'</b>");
	EXPECT_EQ(Output, "&lt;b&gt;&quot;x&quot; &amp; &#39;y&#39;&lt;/b&gt;");
}

TEST(QmChatExportHtml, BuildHtmlEmbedsMessagesAndMarksLocalLines)
{
	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", "<script>"), MakeLine("Bob", "hi", true)};
	QmChatExport::SLabels Labels;
	Labels.m_Title = "T";
	Labels.m_Total = "Total";
	Labels.m_Messages = "Messages";

	std::string Html;
	ASSERT_TRUE(QmChatExport::BuildHtml(vLines, Labels, Html));
	// 正文必须转义，不能把日志内容当标记注入。
	EXPECT_NE(Html.find("&lt;script&gt;"), std::string::npos);
	EXPECT_EQ(Html.find("<script>"), std::string::npos);
	// 本地发言带上 local 标记以右对齐。
	EXPECT_NE(Html.find("class=\"msg local\""), std::string::npos);
	EXPECT_NE(Html.find("Total 2 Messages"), std::string::npos);
	// 头像内联成 data URI，导出文件自身可独立打开。
	EXPECT_NE(Html.find("data:image/png;base64,"), std::string::npos);
}

TEST(QmChatExportHtml, BuildTxtKeepsTheRawLogLines)
{
	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", "one"), MakeLine("Bob", "two")};
#if defined(CONF_FAMILY_WINDOWS)
	EXPECT_EQ(QmChatExport::BuildTxt(vLines), "Alice: one\r\nBob: two\r\n");
#else
	EXPECT_EQ(QmChatExport::BuildTxt(vLines), "Alice: one\nBob: two\n");
#endif
}

TEST(QmChatExportRender, BlendWritesOnlyInsideTheImage)
{
	std::vector<uint8_t> Pixels((size_t)QmChatExport::IMAGE_WIDTH * 4 * 4, 0);
	QmChatExport::Blend(Pixels, 4, 1, 1, {255, 0, 0});
	const size_t Offset = ((size_t)1 * QmChatExport::IMAGE_WIDTH + 1) * 4;
	EXPECT_EQ(Pixels[Offset], 255);
	EXPECT_EQ(Pixels[Offset + 3], 0); // Blend 只改颜色，alpha 由绘制前的底色决定

	// 越界坐标必须被丢弃而不是写坏内存。
	QmChatExport::Blend(Pixels, 4, -1, 0, {255, 255, 255});
	QmChatExport::Blend(Pixels, 4, QmChatExport::IMAGE_WIDTH, 0, {255, 255, 255});
	QmChatExport::Blend(Pixels, 4, 0, 4, {255, 255, 255});
	EXPECT_EQ(Pixels[0], 0);
}

TEST(QmChatExportRender, RenderPageDrawsTheAvatarBubbleAndTextWithinBounds)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME, QmChatExport::FONT_TIME}, 10);
	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", "hello")};
	const auto vPages = QmChatExport::BuildPages(vLines, Glyphs);
	ASSERT_EQ(vPages.size(), 1u);

	std::atomic<bool> Cancelled{false};
	const auto Pixels = QmChatExport::RenderPage(vPages[0], vLines, Glyphs, Cancelled);
	ASSERT_EQ(Pixels.size(), (size_t)QmChatExport::IMAGE_WIDTH * vPages[0].m_Height * 4);

	// 画布底色不透明。
	EXPECT_EQ(PageAlphaAt(Pixels, 0, 0), 255);
	// 左侧头像区域有内容，且画布右侧（本地消息才占用的位置）仍是底色。
	const auto AvatarColor = PageColorAt(Pixels, 40 + QmChatAvatar::SIZE / 2, QmChatExport::PAGE_PADDING + QmChatAvatar::SIZE / 2);
	const auto BackgroundColor = PageColorAt(Pixels, QmChatExport::IMAGE_WIDTH - 5, QmChatExport::PAGE_PADDING);
	EXPECT_NE(AvatarColor, BackgroundColor);
}

TEST(QmChatExportRender, RenderPagePlacesLocalMessagesOnTheRight)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME, QmChatExport::FONT_TIME}, 10);
	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", "hello", true)};
	const auto vPages = QmChatExport::BuildPages(vLines, Glyphs);
	ASSERT_EQ(vPages.size(), 1u);

	std::atomic<bool> Cancelled{false};
	const auto Pixels = QmChatExport::RenderPage(vPages[0], vLines, Glyphs, Cancelled);
	// 本地发言的头像贴在右边，左侧留白。
	const auto LeftColor = PageColorAt(Pixels, 45, QmChatExport::PAGE_PADDING + QmChatAvatar::SIZE / 2);
	const auto RightColor = PageColorAt(Pixels, QmChatExport::IMAGE_WIDTH - 40 - QmChatAvatar::SIZE / 2, QmChatExport::PAGE_PADDING + QmChatAvatar::SIZE / 2);
	EXPECT_NE(LeftColor, RightColor);
}

TEST(QmChatExportRender, RenderPageStopsWhenCancelled)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME, QmChatExport::FONT_TIME}, 10);
	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", "hello")};
	const auto vPages = QmChatExport::BuildPages(vLines, Glyphs);
	ASSERT_EQ(vPages.size(), 1u);

	std::atomic<bool> Cancelled{true};
	EXPECT_TRUE(QmChatExport::RenderPage(vPages[0], vLines, Glyphs, Cancelled).empty());
}

TEST_F(QmChatExportTest, ExportWritesTxtHtmlAndASinglePngForOneShortPage)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME, QmChatExport::FONT_TIME}, 10);
	const std::vector<QmChatExport::SLine> vLines = {MakeLine("Alice", "hello"), MakeLine("Bob", "world", true)};
	QmChatExport::SLabels Labels;
	Labels.m_Title = "Chat";
	Labels.m_Total = "Total";
	Labels.m_Messages = "Messages";

	std::atomic<bool> Cancelled{false};
	std::atomic<int> CompletedPages{0};
	ASSERT_TRUE(QmChatExport::Export(m_pStorage.get(), "qm_chat_export_test/out", vLines, Glyphs, Labels, Cancelled, CompletedPages));
	EXPECT_EQ(CompletedPages.load(), 1);

	for(const char *pFilename : {"qm_chat_export_test/out.txt", "qm_chat_export_test/out.html", "qm_chat_export_test/out.png"})
		EXPECT_TRUE(m_pStorage->FileExists(pFilename, IStorage::TYPE_SAVE)) << pFilename;
}

TEST_F(QmChatExportTest, ExportNumbersPagesWhenContentSpansSeveralImages)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME, QmChatExport::FONT_TIME}, 10);
	std::vector<QmChatExport::SLine> vLines;
	for(int i = 0; i < 400; ++i)
		vLines.push_back(MakeLine("Alice", "abcdefghij"));
	QmChatExport::SLabels Labels;

	std::atomic<bool> Cancelled{false};
	std::atomic<int> CompletedPages{0};
	ASSERT_TRUE(QmChatExport::Export(m_pStorage.get(), "qm_chat_export_test/multi", vLines, Glyphs, Labels, Cancelled, CompletedPages));
	EXPECT_GT(CompletedPages.load(), 1);
	EXPECT_TRUE(m_pStorage->FileExists("qm_chat_export_test/multi_001.png", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("qm_chat_export_test/multi.png", IStorage::TYPE_SAVE));
}

TEST_F(QmChatExportTest, ExportRemovesEveryFileItWroteWhenCancelled)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE, QmChatExport::FONT_NAME, QmChatExport::FONT_TIME}, 10);
	std::vector<QmChatExport::SLine> vLines;
	for(int i = 0; i < 400; ++i)
		vLines.push_back(MakeLine("Alice", "abcdefghij"));
	QmChatExport::SLabels Labels;

	std::atomic<bool> Cancelled{true};
	std::atomic<int> CompletedPages{7};
	EXPECT_FALSE(QmChatExport::Export(m_pStorage.get(), "qm_chat_export_test/cancel", vLines, Glyphs, Labels, Cancelled, CompletedPages));
	// 取消时不能留下半成品，进度也要回到 0。
	EXPECT_EQ(CompletedPages.load(), 0);
	EXPECT_FALSE(m_pStorage->FileExists("qm_chat_export_test/cancel.txt", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("qm_chat_export_test/cancel.html", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("qm_chat_export_test/cancel_001.png", IStorage::TYPE_SAVE));
}

TEST_F(QmChatExportTest, ExportRefusesEmptyInput)
{
	const auto Glyphs = MakeGlyphs({QmChatExport::FONT_MESSAGE}, 10);
	const std::vector<QmChatExport::SLine> vLines;
	QmChatExport::SLabels Labels;

	std::atomic<bool> Cancelled{false};
	std::atomic<int> CompletedPages{0};
	EXPECT_FALSE(QmChatExport::Export(m_pStorage.get(), "qm_chat_export_test/empty", vLines, Glyphs, Labels, Cancelled, CompletedPages));
	EXPECT_EQ(CompletedPages.load(), 0);
}
