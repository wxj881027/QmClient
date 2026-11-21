#include <game/editor/editor_history.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	class CRecordedAction : public IEditorAction
	{
	public:
		CRecordedAction() :
			IEditorAction(nullptr)
		{
		}

		void Undo() override { m_Applied = false; }
		void Redo() override { m_Applied = true; }

		bool m_Applied = false;
	};

	class CImportAction : public CRecordedAction
	{
	public:
		bool TryRedo() override
		{
			if(!m_InputAvailable)
				return false;
			return CRecordedAction::TryRedo();
		}

		bool m_InputAvailable = false;
	};
} // namespace

TEST(EditorHistory, FailedImportRedoKeepsThePendingActionForRetry)
{
	CEditorHistory History(nullptr);
	const auto pEarlierUndo = std::make_shared<CRecordedAction>();
	const auto pLaterRedo = std::make_shared<CRecordedAction>();
	const auto pImport = std::make_shared<CImportAction>();
	History.m_vpUndoActions.push_back(pEarlierUndo);
	History.m_vpRedoActions.push_back(pLaterRedo);
	History.m_vpRedoActions.push_back(pImport);

	EXPECT_FALSE(History.Redo());
	EXPECT_FALSE(pImport->m_Applied);
	EXPECT_FALSE(pLaterRedo->m_Applied);
	ASSERT_EQ(History.m_vpUndoActions.size(), 1u);
	EXPECT_EQ(History.m_vpUndoActions.back(), pEarlierUndo);
	ASSERT_EQ(History.m_vpRedoActions.size(), 2u);
	EXPECT_EQ(History.m_vpRedoActions.back(), pImport);

	pImport->m_InputAvailable = true;
	ASSERT_TRUE(History.Redo());
	EXPECT_TRUE(pImport->m_Applied);
	EXPECT_FALSE(pLaterRedo->m_Applied);
	ASSERT_EQ(History.m_vpUndoActions.size(), 2u);
	EXPECT_EQ(History.m_vpUndoActions.back(), pImport);
	ASSERT_EQ(History.m_vpRedoActions.size(), 1u);
	EXPECT_EQ(History.m_vpRedoActions.back(), pLaterRedo);
}

TEST(EditorHistory, SuccessfulDefaultActionMovesToUndoHistory)
{
	CEditorHistory History(nullptr);
	const auto pAction = std::make_shared<CRecordedAction>();
	History.m_vpRedoActions.push_back(pAction);

	ASSERT_TRUE(History.Redo());
	EXPECT_TRUE(pAction->m_Applied);
	EXPECT_FALSE(History.CanRedo());
	ASSERT_TRUE(History.CanUndo());
	EXPECT_EQ(History.m_vpUndoActions.back(), pAction);
}
