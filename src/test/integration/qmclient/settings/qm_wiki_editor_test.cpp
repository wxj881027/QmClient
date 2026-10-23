// 请抬头享受阳光｜日子很好 我很我---------致咩子
// Wiki 面板的编辑状态机与存储、Markdown 解析的协作：
// 保存到哪里、还原是否真的删文件、草稿在 Esc 与换文档后如何处理，
// 这些都不是纯字符串判断能覆盖的，必须走真实 IStorage 与真实解析器。
#include <game/client/QmUi/QmWikiEditor.h>
#include <game/client/QmUi/QmWikiStore.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

namespace
{
	// 内置兜底文案：与随包文件、用户覆盖三者必须能区分开。
	const char *const s_apBuiltInParagraphs[] = {"Built-in paragraph."};
	const SWikiSection s_aBuiltInSections[] = {{"Built-in heading", s_apBuiltInParagraphs, 1}};
	const char *const s_apSecondParagraphs[] = {"Second built-in paragraph."};
	const SWikiSection s_aSecondSections[] = {{"Second built-in heading", s_apSecondParagraphs, 1}};

	// 生产里由编辑框缓冲大小决定；除超出上限的用例外不关心具体容量。
	constexpr size_t kEditorByteLimit = 1u << 20;

	// 语言目录只影响落盘位置；固定成中文目录，避免测试依赖运行环境的 cl_languagefile。
	class CScopedLanguageFile
	{
		std::string m_Previous;

	public:
		explicit CScopedLanguageFile(const char *pLanguage) :
			m_Previous(g_Config.m_ClLanguagefile)
		{
			str_copy(g_Config.m_ClLanguagefile, pLanguage, sizeof(g_Config.m_ClLanguagefile));
		}
		~CScopedLanguageFile()
		{
			str_copy(g_Config.m_ClLanguagefile, m_Previous.c_str(), sizeof(g_Config.m_ClLanguagefile));
		}
	};

	class CQmWikiEditorTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CScopedLanguageFile m_Language{"simplified_chinese.txt"};
		SWikiDocument m_Fallback{"qm:wiki-editor-test", "Wiki editor", s_aBuiltInSections, 1};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			qm_wiki_store::Init(m_pStorage.get());
		}

		void TearDown() override
		{
			qm_wiki_store::Init(nullptr);
		}

		std::string ReadOverride(const char *pRelativePath) const
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "wiki/simplified_chinese/%s.md", pRelativePath);
			IOHANDLE File = m_pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
			if(File == nullptr)
				return std::string();
			char *pText = io_read_all_str(File);
			io_close(File);
			if(pText == nullptr)
				return std::string();
			std::string Text(pText);
			free(pText);
			return Text;
		}

		bool OverrideExists(const char *pRelativePath) const
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "wiki/simplified_chinese/%s.md", pRelativePath);
			return m_pStorage->FileExists(aPath, IStorage::TYPE_SAVE);
		}

	};
}

TEST_F(CQmWikiEditorTest, LoadingWithoutAnyFileFallsBackToBuiltInDocument)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);

	EXPECT_EQ(State.m_Content.m_RelativePath, "qm/wiki-editor-test");
	EXPECT_FALSE(State.m_Content.m_HasUserOverride);
	EXPECT_FALSE(State.m_Content.m_HasShippedFile);
	EXPECT_EQ(State.m_Content.m_Markdown, qm_wiki_store::ComposeFromDocument(m_Fallback));
	EXPECT_NE(State.m_Content.m_Markdown.find("Built-in paragraph."), std::string::npos);
	EXPECT_FALSE(State.m_Editing);
	EXPECT_FALSE(State.m_EditDirty);
}

TEST_F(CQmWikiEditorTest, SavedOverrideIsPickedUpByTheNextOpen)
{
	{
		SWikiEditorState First;
		First.Load("qm:wiki-editor-test", m_Fallback, true);
		First.BeginEditing(kEditorByteLimit);
		First.SetDraft("## Draft\n\nDraft body.\n");
		ASSERT_TRUE(First.Save());
	}
	// 新会话（新进程里的面板同理）必须从用户目录读到刚才的草稿，而不是内置文案。
	SWikiEditorState Reloaded;
	Reloaded.Load("qm:wiki-editor-test", m_Fallback, true);
	EXPECT_TRUE(Reloaded.m_Content.m_HasUserOverride);
	EXPECT_NE(Reloaded.m_Content.m_Markdown.find("Draft body."), std::string::npos);
	EXPECT_FALSE(Reloaded.m_EditDirty);
	EXPECT_TRUE(Reloaded.m_Draft.empty());
}

TEST_F(CQmWikiEditorTest, SavingWritesUnderLanguageDirectoryAndClearsDraft)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);
	State.BeginEditing(kEditorByteLimit);
	const std::string Draft = "## Saved heading\n\nSaved body.\n";
	State.SetDraft(Draft);

	ASSERT_TRUE(State.Save());

	// 落盘位置，而不是只在内存里换了一份内容。
	EXPECT_EQ(ReadOverride("qm/wiki-editor-test"), Draft);
	EXPECT_EQ(State.m_Content.m_Markdown, Draft);
	EXPECT_TRUE(State.m_Content.m_HasUserOverride);
	EXPECT_FALSE(State.m_EditDirty);
	EXPECT_EQ(State.m_Draft, Draft);
	EXPECT_FALSE(State.m_StatusIsError);
	EXPECT_NE(std::string(State.m_aStatus).find("wiki/simplified_chinese/qm/wiki-editor-test.md"), std::string::npos);
}

TEST_F(CQmWikiEditorTest, ResetToBuiltInRemovesOverrideFile)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);
	State.BeginEditing(kEditorByteLimit);
	State.SetDraft("## Draft\n\nDraft body.\n");
	ASSERT_TRUE(State.Save());
	ASSERT_TRUE(OverrideExists("qm/wiki-editor-test"));

	ASSERT_TRUE(State.ResetToBuiltIn());

	EXPECT_FALSE(OverrideExists("qm/wiki-editor-test"));
	EXPECT_FALSE(State.m_Content.m_HasUserOverride);
	EXPECT_EQ(State.m_Content.m_Markdown, qm_wiki_store::ComposeFromDocument(m_Fallback));
	EXPECT_EQ(State.m_Draft, State.m_Content.m_Markdown);
	EXPECT_FALSE(State.m_EditDirty);
	EXPECT_FALSE(State.m_StatusIsError);
	EXPECT_GT(str_length(State.m_aStatus), 0);
}

TEST_F(CQmWikiEditorTest, LeavingEditingKeepsDraftForTheNextRound)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);
	State.BeginEditing(kEditorByteLimit);
	const char *pDraft = "Unsaved draft that must survive Esc.";
	State.SetDraft(pDraft);
	State.m_EditDirty = true;

	State.LeaveEditing();
	EXPECT_FALSE(State.m_Editing);
	EXPECT_EQ(State.m_Draft, pDraft);

	// 再次进入编辑：草稿优先，不能被磁盘内容覆盖。
	State.BeginEditing(kEditorByteLimit);
	EXPECT_TRUE(State.m_Editing);
	EXPECT_EQ(State.m_Draft, pDraft);
	EXPECT_TRUE(State.m_EditDirty);
}

TEST_F(CQmWikiEditorTest, LoadingAnotherDocumentDropsPreviousDraft)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);
	State.BeginEditing(kEditorByteLimit);
	State.SetDraft("Leaked draft from the first card.");
	State.m_EditDirty = true;

	State.Load("qm:other-document", m_Fallback, true);

	EXPECT_FALSE(State.m_Editing);
	EXPECT_FALSE(State.m_EditDirty);
	EXPECT_TRUE(State.m_Draft.empty());
	EXPECT_EQ(State.m_Content.m_RelativePath, "qm/other-document");
	EXPECT_EQ(State.m_Content.m_Markdown, qm_wiki_store::ComposeFromDocument(m_Fallback));
}

TEST_F(CQmWikiEditorTest, TableOfContentsIsBuiltFromTopThreeHeadingLevelsOnly)
{
	SWikiEditorState Single;
	Single.Load("qm:wiki-editor-test", m_Fallback, true);
	Single.BeginEditing(kEditorByteLimit);
	Single.SetDraft("## Only heading\n\nBody.\n");
	ASSERT_TRUE(Single.Save());
	EXPECT_TRUE(Single.m_vTocBlocks.empty());

	SWikiEditorState Multi;
	Multi.Load("qm:wiki-editor-test", m_Fallback, true);
	Multi.BeginEditing(kEditorByteLimit);
	// 四个标题里只有前三个（级别 <= 3）进目录。
	Multi.SetDraft("## First\n\nBody.\n\n### Second\n\nBody.\n\n#### Too deep\n\nBody.\n\n## Third\n\nBody.\n");
	ASSERT_TRUE(Multi.Save());
	EXPECT_EQ(Multi.m_vTocBlocks.size(), 3u);
}

TEST_F(CQmWikiEditorTest, BeginEditingRefusesDocumentsLongerThanTheBuffer)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);
	ASSERT_GT(State.m_Content.m_Markdown.size(), 8u);

	// 缓冲放不下时宁可不进入编辑：截断的副本一旦保存，后半篇正文就没了。
	EXPECT_FALSE(State.BeginEditing(8));
	EXPECT_FALSE(State.m_Editing);
	EXPECT_TRUE(State.m_StatusIsError);
	EXPECT_TRUE(State.m_Draft.empty());
	EXPECT_NE(std::string(State.m_aStatus).find("8"), std::string::npos);

	// 容量够时正常进入编辑，草稿是完整正文。
	EXPECT_TRUE(State.BeginEditing(State.m_Content.m_Markdown.size()));
	EXPECT_TRUE(State.m_Editing);
	EXPECT_FALSE(State.m_StatusIsError);
	EXPECT_STREQ(State.m_aStatus, "");
	EXPECT_EQ(State.m_Draft, State.m_Content.m_Markdown);
}

TEST_F(CQmWikiEditorTest, SavingWithoutStorageReportsErrorAndKeepsDraft)
{
	SWikiEditorState State;
	State.Load("qm:wiki-editor-test", m_Fallback, true);
	State.BeginEditing(kEditorByteLimit);
	State.SetDraft("Draft that cannot be saved.");
	State.m_EditDirty = true;

	// 存储不可用：保存必须失败、给出错误状态，并保留草稿供用户重试。
	qm_wiki_store::Init(nullptr);
	EXPECT_FALSE(State.Save());
	EXPECT_TRUE(State.m_StatusIsError);
	EXPECT_GT(str_length(State.m_aStatus), 0);
	EXPECT_TRUE(State.m_EditDirty);
	EXPECT_EQ(State.m_Draft, "Draft that cannot be saved.");
}
