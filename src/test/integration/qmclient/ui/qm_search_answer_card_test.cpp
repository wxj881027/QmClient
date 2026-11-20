// 执行真实答案卡片、声明布局和 CUi；仅替换输入设备、目录与系统打开边界。
#include <engine/keys.h>

#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/cards/QmSearchAnswerCard.h>
#include <game/client/components/qmclient/search_answers.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	enum class EActionFailure
	{
		NONE,
		ALLOCATION,
		LENGTH,
		OTHER,
	};

	void FailActionOnce(EActionFailure &Failure)
	{
		switch(std::exchange(Failure, EActionFailure::NONE))
		{
		case EActionFailure::ALLOCATION: throw std::bad_alloc();
		case EActionFailure::LENGTH: throw std::length_error("controlled directory resource limit");
		case EActionFailure::OTHER: throw std::logic_error("unrelated directory failure");
		case EActionFailure::NONE: break;
		}
	}

	class CAnswerInput : public qm_ui_test::CTestInput
	{
	public:
		int m_ClipboardWrites = 0;
		void SetClipboardText(const char *pText) override
		{
			++m_ClipboardWrites;
			CTestInput::SetClipboardText(pText);
		}
	};

	class CAnswerClient : public qm_ui_test::CTestUiClient
	{
	public:
		EActionFailure m_Failure = EActionFailure::NONE;
		int m_ViewFileCalls = 0;
		bool m_ViewFileResult = true;
		char m_aLastPath[IO_MAX_PATH_LENGTH]{};
		bool ViewFile(const char *pFilename) override
		{
			++m_ViewFileCalls;
			FailActionOnce(m_Failure);
			str_copy(m_aLastPath, pFilename);
			return m_ViewFileResult;
		}
	};

	// 实际路径仍由隔离的生产存储解析；仅在真实的目录检查分配边界注入故障。
	class CAnswerStorage : public IStorage
	{
		[[noreturn]] static void Unexpected() { throw std::logic_error("unexpected answer storage operation"); }

	public:
		IStorage *m_pStorage = nullptr;
		EActionFailure m_FolderFailure = EActionFailure::NONE;
		int m_FolderCalls = 0;
		int m_PathCalls = 0;
		int m_LastType = IStorage::TYPE_ALL;
		char m_aLastDirectory[IO_MAX_PATH_LENGTH]{};
		bool FolderExists(const char *pDirectory, int Type) override
		{
			++m_FolderCalls;
			m_LastType = Type;
			str_copy(m_aLastDirectory, pDirectory);
			FailActionOnce(m_FolderFailure);
			return m_pStorage->FolderExists(pDirectory, Type);
		}
		void GetCompletePath(int Type, const char *pDirectory, char *pBuffer, unsigned Size) override
		{
			++m_PathCalls;
			m_LastType = Type;
			str_copy(m_aLastDirectory, pDirectory);
			m_pStorage->GetCompletePath(Type, pDirectory, pBuffer, Size);
		}
		int NumPaths() const override { Unexpected(); }
		bool GetDataPath(const char *, char *, unsigned) const override { Unexpected(); }
		bool GetExecutablePath(const char *, char *, unsigned) const override { Unexpected(); }
		void ListDirectory(int, const char *, FS_LISTDIR_CALLBACK, void *) override { Unexpected(); }
		void ListDirectoryInfo(int, const char *, FS_LISTDIR_CALLBACK_FILEINFO, void *) override { Unexpected(); }
		IOHANDLE OpenFile(const char *, int, int, char *, int) override { Unexpected(); }
		bool FileExists(const char *, int) override { Unexpected(); }
		bool ReadFile(const char *, int, void **, unsigned *) override { Unexpected(); }
		char *ReadFileStr(const char *, int) override { Unexpected(); }
		bool RetrieveTimes(const char *, int, time_t *, time_t *) override { Unexpected(); }
		bool CalculateHashes(const char *, int, SHA256_DIGEST *, unsigned *) override { Unexpected(); }
		bool FindFile(const char *, const char *, int, char *, int) override { Unexpected(); }
		size_t FindFiles(const char *, const char *, int, std::set<std::string> *) override { Unexpected(); }
		bool RemoveFile(const char *, int) override { Unexpected(); }
		bool RemoveFolder(const char *, int) override { Unexpected(); }
		bool RenameFile(const char *, const char *, int, bool) override { Unexpected(); }
		bool CreateFolder(const char *, int) override { Unexpected(); }
		bool RemoveBinaryFile(const char *) override { Unexpected(); }
		bool RenameBinaryFile(const char *, const char *) override { Unexpected(); }
		const char *GetBinaryPath(const char *, char *, unsigned) override { Unexpected(); }
		const char *GetBinaryPathAbsolute(const char *, char *, unsigned) override { Unexpected(); }
	};

	class QmSearchAnswerCard : public qm_ui_test::CRealUiFixture
	{
		CTestInfo m_ActionStorageInfo;

	protected:
		std::unique_ptr<IStorage> m_pActionStorage;
		CAnswerStorage m_Storage;
		CAnswerInput m_Input;
		CAnswerClient m_Client;
		CUIRect m_Content{20, 20, 640, 560};
		SSettingsContentMetrics m_Metrics;
		std::shared_ptr<qm_card_catalog::SSearchAnswerCardState> m_pState;
		SSettingsCardDefinition m_Definition;
		CSettingsContentLayoutCache m_Cache;
		std::shared_ptr<const CSettingsContentLayout> m_pLayout;
		const char *m_pSelectedQuestion = nullptr;
		int m_Selections = 0;
		std::function<void(const char *)> m_SelectQuestion;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 两个 CTestInfo 默认使用同一个测试名，动作存储另占隔离子目录。
			str_append(m_ActionStorageInfo.m_aStoragePath, "/answer-actions");
			m_pActionStorage = m_ActionStorageInfo.CreateTestStorage();
			ASSERT_NE(m_pActionStorage, nullptr);
			m_Storage.m_pStorage = m_pActionStorage.get();
			m_pState = qm_card_catalog::CreateSearchAnswerCardState();
			m_Metrics = ResolveSettingsContentMetrics(m_Content.w);
			m_SelectQuestion = [this](const char *pQuestion) {
				m_pSelectedQuestion = pQuestion;
				++m_Selections;
			};
		}

		qm_card_catalog::SSearchAnswerContext Context()
		{
			return {&m_Ui, m_Ui.TextRender(), &m_Input, &m_Storage, &m_Client};
		}
		void Prepare(const qm_search_answers::SResult &Result, bool ReadOnly = false)
		{
			qm_card_catalog::BuildSearchAnswerCard(Context(), Result, ReadOnly, m_Metrics, m_SelectQuestion, m_Definition, m_pState);
		}
		void Resolve()
		{
			m_pLayout = m_Definition.m_ResolveContentLayout(m_Cache, m_Content.w);
		}
		void Build(const char *pQuery, bool ReadOnly = false)
		{
			Prepare(qm_search_answers::Search(pQuery), ReadOnly);
			Resolve();
		}
		void Frame(vec2 Position, bool Pressed = false)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
		vec2 Center(const char *pId)
		{
			const auto *pRow = m_pLayout->Find(pId);
			if(pRow == nullptr)
				throw std::logic_error("answer control is missing from resolved layout");
			return {m_Content.x + pRow->m_Rect.x + pRow->m_Rect.w * 0.5f, m_Content.y + pRow->m_Rect.y + pRow->m_Rect.h * 0.5f};
		}
		void Input()
		{
			m_Ui.BeginPreLayoutInput();
			try
			{
				m_Definition.m_ContentLayoutInput(*m_pLayout, m_Content);
			}
			catch(...)
			{
				m_Ui.EndPreLayoutInput();
				throw;
			}
			m_Ui.EndPreLayoutInput();
		}
		void Draw() { m_Definition.m_ContentLayoutDraw(*m_pLayout, m_Content); }
		void Click(const char *pId)
		{
			const vec2 Position = Center(pId);
			Frame(Position);
			Input();
			Frame(Position, true);
			Input();
			Frame(Position);
			Input();
			Resolve();
		}
		void ExpectText(const char *pId, const char *pText)
		{
			const auto *pRow = m_pLayout->Find(pId);
			ASSERT_NE(pRow, nullptr);
			EXPECT_EQ(pRow->m_Declaration.m_Text, pText);
		}
	};
}

TEST_F(QmSearchAnswerCard, ResolvedAnswerUsesTheStaticQuestionAndWrappedBody)
{
	const auto Result = qm_search_answers::Search("截图");
	ASSERT_NE(Result.m_pAnswer, nullptr);
	Build("截图");
	ExpectText("answer-question", Result.m_pAnswer->m_pQuestion);
	ExpectText("answer-body", Result.m_pAnswer->m_pAnswer);
	const float WideHeight = m_pLayout->Height();
	m_Content.w = 180;
	Resolve();
	EXPECT_GT(m_pLayout->Height(), WideHeight);
	EXPECT_FALSE(m_Ui.RenderOnly());
	EXPECT_EQ(m_Input.m_ClipboardWrites, 0);
	EXPECT_EQ(m_Client.m_ViewFileCalls, 0);
}

TEST_F(QmSearchAnswerCard, ReorderedCandidatesKeepThePressedQuestionIdentity)
{
	auto Result = qm_search_answers::Search("没声音");
	ASSERT_EQ(Result.m_pAnswer, nullptr);
	ASSERT_GE(Result.m_vCandidates.size(), 2u);
	const auto *pPressed = Result.m_vCandidates.front();
	Prepare(Result);
	Resolve();
	Frame(Center(pPressed->m_pId), true);
	Input();
	ASSERT_NE(m_Ui.ActiveItem(), nullptr);
	const void *pActive = m_Ui.ActiveItem();
	std::reverse(Result.m_vCandidates.begin(), Result.m_vCandidates.end());
	Prepare(Result);
	Resolve();
	EXPECT_EQ(m_Ui.ActiveItem(), pActive);
	Frame(Center(pPressed->m_pId));
	Input();
	EXPECT_EQ(m_Selections, 1);
	EXPECT_STREQ(m_pSelectedQuestion, pPressed->m_pQuestion);
	Draw();
	Draw();
	EXPECT_EQ(m_Selections, 1);
}

TEST_F(QmSearchAnswerCard, DeclarationAndDrawingCannotSubmitAnswerActions)
{
	Build("截图");
	ASSERT_TRUE(m_pActionStorage->CreateFolder("screenshots", IStorage::TYPE_SAVE));
	for(const char *pId : {"answer-copy", "answer-open-directory"})
	{
		SCOPED_TRACE(pId);
		Frame(Center(pId), true);
		Resolve();
		Draw();
		Frame(Center(pId));
		Resolve();
		Draw();
	}
	EXPECT_EQ(m_Input.m_ClipboardWrites, 0);
	EXPECT_EQ(m_Storage.m_FolderCalls, 0);
	EXPECT_EQ(m_Client.m_ViewFileCalls, 0);
	EXPECT_FALSE(Graphics().m_vTextSubmissions.empty());
	EXPECT_FALSE(Graphics().m_vRoundedSubmissions.empty());
	EXPECT_FALSE(m_Ui.RenderOnly());
	EXPECT_FALSE(m_Ui.BackgroundRender());
}

TEST_F(QmSearchAnswerCard, ReadOnlyAnswerAndCandidateControlsCannotSubmit)
{
	Build("截图", true);
	Click("answer-copy");
	Click("answer-open-directory");
	Build("toggle_scoreboard_cursor", true);
	Click("answer-copy-command");
	const auto Result = qm_search_answers::Search("没声音");
	ASSERT_FALSE(Result.m_vCandidates.empty());
	Prepare(Result, true);
	Resolve();
	Click(Result.m_vCandidates.front()->m_pId);
	EXPECT_EQ(m_Selections, 0);
	EXPECT_EQ(m_Input.m_ClipboardWrites, 0);
	EXPECT_EQ(m_Storage.m_FolderCalls, 0);
	EXPECT_EQ(m_Client.m_ViewFileCalls, 0);
}

TEST_F(QmSearchAnswerCard, CopyAnswerPersistsItsStatusAcrossRebuildsWithTheSameState)
{
	const auto Result = qm_search_answers::Search("截图");
	ASSERT_NE(Result.m_pAnswer, nullptr);
	Build("截图");
	Click("answer-copy");
	EXPECT_EQ(m_Input.m_ClipboardWrites, 1);
	EXPECT_EQ(m_Input.m_Clipboard, Result.m_pAnswer->m_pAnswer);
	Build("截图");
	ExpectText("answer-status", "已复制答案");
	Draw();
	Draw();
	EXPECT_EQ(m_Input.m_ClipboardWrites, 1);
}

TEST_F(QmSearchAnswerCard, CopyCommandRunsOncePerExplicitClick)
{
	const auto Result = qm_search_answers::Search("toggle_scoreboard_cursor");
	ASSERT_NE(Result.m_pAnswer, nullptr);
	ASSERT_NE(Result.m_pAnswer->m_pCommand, nullptr);
	Build("toggle_scoreboard_cursor");
	Click("answer-copy-command");
	EXPECT_EQ(m_Input.m_Clipboard, Result.m_pAnswer->m_pCommand);
	ExpectText("answer-status", "已复制命令，请检查内容后在控制台使用");
	Frame({0, 0});
	Input();
	Draw();
	Draw();
	EXPECT_EQ(m_Input.m_ClipboardWrites, 1);
	Click("answer-copy-command");
	EXPECT_EQ(m_Input.m_ClipboardWrites, 2);
}

TEST_F(QmSearchAnswerCard, CallbacksKeepTheStateAliveAfterTheBuilderOwnerAndResultLeave)
{
	std::weak_ptr<qm_card_catalog::SSearchAnswerCardState> Weak = m_pState;
	{
		const auto Result = qm_search_answers::Search("截图");
		Prepare(Result);
	}
	m_pState.reset();
	EXPECT_FALSE(Weak.expired());
	Resolve();
	Click("answer-copy");
	ExpectText("answer-status", "已复制答案");
	EXPECT_EQ(m_Input.m_ClipboardWrites, 1);
	m_Ui.SetActiveItem(nullptr);
	m_Ui.SetKeyboardFocus(nullptr);
	m_Definition = {};
	EXPECT_TRUE(Weak.expired());
}

TEST_F(QmSearchAnswerCard, MissingDirectoryKeepsTheExplanationUntilAnotherExplicitClick)
{
	Build("截图");
	ASSERT_FALSE(m_pActionStorage->FolderExists("screenshots", IStorage::TYPE_SAVE));
	Click("answer-open-directory");
	ExpectText("answer-status", "目录尚未生成，请先按说明使用对应功能");
	EXPECT_EQ(m_Client.m_ViewFileCalls, 0);
	EXPECT_EQ(m_Storage.m_PathCalls, 0);
	ASSERT_TRUE(m_pActionStorage->CreateFolder("screenshots", IStorage::TYPE_SAVE));
	Frame({0, 0});
	Input();
	Draw();
	EXPECT_EQ(m_Storage.m_FolderCalls, 1);
	EXPECT_EQ(m_Client.m_ViewFileCalls, 0);
	Click("answer-open-directory");
	char aExpectedPath[IO_MAX_PATH_LENGTH];
	m_pActionStorage->GetCompletePath(IStorage::TYPE_SAVE, "screenshots", aExpectedPath, sizeof(aExpectedPath));
	EXPECT_STREQ(m_Client.m_aLastPath, aExpectedPath);
	EXPECT_STREQ(m_Storage.m_aLastDirectory, "screenshots");
	EXPECT_EQ(m_Storage.m_LastType, IStorage::TYPE_SAVE);
	EXPECT_EQ(m_Client.m_ViewFileCalls, 1);
	ExpectText("answer-status", "已请求打开目录");
}

TEST_F(QmSearchAnswerCard, ExpectedDirectoryResourceFailuresShowFailureAndAllowExplicitRetry)
{
	ASSERT_TRUE(m_pActionStorage->CreateFolder("screenshots", IStorage::TYPE_SAVE));
	EActionFailure *apBoundaries[] = {&m_Storage.m_FolderFailure, &m_Client.m_Failure};
	for(size_t Boundary = 0; Boundary < std::size(apBoundaries); ++Boundary)
	{
		for(const auto Failure : {EActionFailure::ALLOCATION, EActionFailure::LENGTH})
		{
			SCOPED_TRACE(Boundary);
			SCOPED_TRACE(static_cast<int>(Failure));
			Build("截图");
			*apBoundaries[Boundary] = Failure;
			const int PreviousViews = m_Client.m_ViewFileCalls;
			ASSERT_NO_THROW(Click("answer-open-directory"));
			ExpectText("answer-status", "无法打开目录");
			EXPECT_EQ(m_Client.m_ViewFileCalls, PreviousViews + (Boundary == 1 ? 1 : 0));
			const int FailedViews = m_Client.m_ViewFileCalls;
			const int FailedFolders = m_Storage.m_FolderCalls;
			const int FailedPaths = m_Storage.m_PathCalls;
			Frame({0, 0});
			Input();
			Resolve();
			Draw();
			EXPECT_EQ(m_Client.m_ViewFileCalls, FailedViews);
			EXPECT_EQ(m_Storage.m_FolderCalls, FailedFolders);
			EXPECT_EQ(m_Storage.m_PathCalls, FailedPaths);
			Click("answer-open-directory");
			ExpectText("answer-status", "已请求打开目录");
			EXPECT_EQ(m_Client.m_ViewFileCalls, FailedViews + 1);
		}
	}
}

TEST_F(QmSearchAnswerCard, SystemOpenRejectionUsesExistingFailureStatusWithoutReplay)
{
	ASSERT_TRUE(m_pActionStorage->CreateFolder("screenshots", IStorage::TYPE_SAVE));
	Build("截图");
	m_Client.m_ViewFileResult = false;
	Click("answer-open-directory");
	ExpectText("answer-status", "无法打开目录");
	m_Client.m_ViewFileResult = true;
	Frame({0, 0});
	Input();
	Draw();
	EXPECT_EQ(m_Client.m_ViewFileCalls, 1);
	Click("answer-open-directory");
	ExpectText("answer-status", "已请求打开目录");
	EXPECT_EQ(m_Client.m_ViewFileCalls, 2);
}

TEST_F(QmSearchAnswerCard, UnrelatedDirectoryExceptionsPropagateWithoutReplayingTheAction)
{
	ASSERT_TRUE(m_pActionStorage->CreateFolder("screenshots", IStorage::TYPE_SAVE));
	EActionFailure *apBoundaries[] = {&m_Storage.m_FolderFailure, &m_Client.m_Failure};
	for(size_t Boundary = 0; Boundary < std::size(apBoundaries); ++Boundary)
	{
		SCOPED_TRACE(Boundary);
		Build("截图");
		*apBoundaries[Boundary] = EActionFailure::OTHER;
		EXPECT_THROW(Click("answer-open-directory"), std::logic_error);
		EXPECT_EQ(CSettingsContentLayoutScope::Current(), nullptr);
		const int Views = m_Client.m_ViewFileCalls;
		const int Folders = m_Storage.m_FolderCalls;
		const int Paths = m_Storage.m_PathCalls;
		Frame({0, 0});
		Input();
		Resolve();
		Draw();
		EXPECT_EQ(m_Client.m_ViewFileCalls, Views);
		EXPECT_EQ(m_Storage.m_FolderCalls, Folders);
		EXPECT_EQ(m_Storage.m_PathCalls, Paths);
		Click("answer-open-directory");
		ExpectText("answer-status", "已请求打开目录");
	}
}

TEST_F(QmSearchAnswerCard, PreparedInputWithoutAnActionDoesNotAllocateForAnyAnswerShape)
{
	for(const char *pQuery : {"没声音", "截图", "toggle_scoreboard_cursor"})
	{
		SCOPED_TRACE(pQuery);
		Build(pQuery);
		Frame({0, 0});
		bool Threw = false, Triggered = false;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Fault;
			try
			{
				Input();
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Fault.Triggered();
			Attempts = Fault.Attempts();
		}
		EXPECT_FALSE(Threw);
		EXPECT_FALSE(Triggered);
		EXPECT_EQ(Attempts, 0u);
		EXPECT_EQ(m_Input.m_ClipboardWrites, 0);
		EXPECT_EQ(m_Client.m_ViewFileCalls, 0);
	}
}

TEST_F(QmSearchAnswerCard, EveryBuildAllocationFailurePreservesTheOldInteractiveCardForRetry)
{
	const auto OldResult = qm_search_answers::Search("截图");
	const auto NewResult = qm_search_answers::Search("没声音");
	ASSERT_NE(OldResult.m_pAnswer, nullptr);
	ASSERT_EQ(NewResult.m_pAnswer, nullptr);
	ASSERT_GE(NewResult.m_vCandidates.size(), 2u);
	Prepare(OldResult);
	m_pState = qm_card_catalog::CreateSearchAnswerCardState();
	size_t Allocations = 0;
	{
		CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
		Prepare(NewResult);
		Allocations = Count.Attempts();
	}
	ASSERT_GT(Allocations, 0u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		m_Ui.SetActiveItem(nullptr);
		m_Ui.SetKeyboardFocus(nullptr);
		m_pState = qm_card_catalog::CreateSearchAnswerCardState();
		Prepare(OldResult);
		m_pState = qm_card_catalog::CreateSearchAnswerCardState();
		bool Threw = false, Triggered = false;
		{
			CPredictionAllocationFailure Fault(Budget);
			try
			{
				Prepare(NewResult);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Fault.Triggered();
		}
		ASSERT_TRUE(Triggered);
		ASSERT_TRUE(Threw);
		Resolve();
		ExpectText("answer-question", OldResult.m_pAnswer->m_pQuestion);
		ExpectText("answer-body", OldResult.m_pAnswer->m_pAnswer);
		const int Copies = m_Input.m_ClipboardWrites;
		Click("answer-copy");
		EXPECT_EQ(m_Input.m_ClipboardWrites, Copies + 1);
		EXPECT_EQ(m_Input.m_Clipboard, OldResult.m_pAnswer->m_pAnswer);
		ExpectText("answer-status", "已复制答案");
		Draw();
		Prepare(NewResult);
		Resolve();
		EXPECT_EQ(m_pLayout->Find("answer-question"), nullptr);
		const int Selections = m_Selections;
		Click(NewResult.m_vCandidates.front()->m_pId);
		EXPECT_EQ(m_Selections, Selections + 1);
		EXPECT_STREQ(m_pSelectedQuestion, NewResult.m_vCandidates.front()->m_pQuestion);
	}
}

TEST_F(QmSearchAnswerCard, FactoryAllocationFailureCanBeRetriedBeforeBuildingTheCard)
{
	m_pState.reset();
	bool Threw = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		try
		{
			m_pState = qm_card_catalog::CreateSearchAnswerCardState();
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(Threw);
	EXPECT_EQ(m_pState, nullptr);
	m_pState = qm_card_catalog::CreateSearchAnswerCardState();
	Build("截图");
	Click("answer-copy");
	EXPECT_EQ(m_Input.m_ClipboardWrites, 1);
}

TEST_F(QmSearchAnswerCard, ExistingBuildSignatureRetainsItsSharedActionState)
{
	const auto Result = qm_search_answers::Search("截图");
	qm_card_catalog::BuildSearchAnswerCard(Context(), Result, false, m_Metrics, m_SelectQuestion, m_Definition);
	Resolve();
	Click("answer-copy");
	qm_card_catalog::BuildSearchAnswerCard(Context(), Result, false, m_Metrics, m_SelectQuestion, m_Definition);
	Resolve();
	ExpectText("answer-status", "已复制答案");
	Draw();
	EXPECT_EQ(m_Input.m_ClipboardWrites, 1);
}
