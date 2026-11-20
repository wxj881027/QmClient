#include <game/editor/editor_trackers.h>
#include <game/editor/editor_ui.h>

#include <gtest/gtest.h>

#include <limits>

TEST(EditorValueSelector, LargeIncrementsAndDecrementsStayInRange)
{
	constexpr int Min = std::numeric_limits<int>::min();
	constexpr int Max = std::numeric_limits<int>::max();

	EXPECT_EQ(EditorValueSelector::Adjust(Max, Min, Max, 1, 1.0), Max);
	EXPECT_EQ(EditorValueSelector::Adjust(Min, Min, Max, 1, -1.0), Min);
	EXPECT_EQ(EditorValueSelector::Adjust(0, -100, 100, 1, 1e14), 100);
}

TEST(EditorValueSelector, SteppingKeepsAngleAlignment)
{
	EXPECT_EQ(EditorValueSelector::Adjust(50, 0, 359, 45, 1), 90);
	EXPECT_EQ(EditorValueSelector::Adjust(50, 0, 359, 45, -1), 45);
	EXPECT_EQ(EditorValueSelector::Adjust(92, 91, 359, 45, 1), 135);
	EXPECT_EQ(EditorValueSelector::Adjust(92, 91, 100, 45, 1), 100);
}

// testrunner 未链接编辑器的默认选区查询，这些用例均传入明确的分组和图层索引。
int CPropTrackerHelper::GetDefaultGroupIndex(CEditorMap *)
{
	ADD_FAILURE() << "Expected an explicit group index";
	return -1;
}

int CPropTrackerHelper::GetDefaultLayerIndex(CEditorMap *)
{
	ADD_FAILURE() << "Expected an explicit layer index";
	return -1;
}

namespace
{
	class CObservedPropTracker : public CPropTracker<int, ETilesProp>
	{
	public:
		CObservedPropTracker() :
			CPropTracker(nullptr) {}

		int m_NumEdits = 0;
		int m_Previous = 0;
		int m_Current = 0;
		int m_ShiftBy = 1;

	protected:
		int PropToValue(ETilesProp Prop) override
		{
			return m_pObject ? *m_pObject : m_ShiftBy;
		}

		void OnEnd(ETilesProp Prop, int Value) override
		{
			++m_NumEdits;
			m_Previous = m_OriginalValue;
			m_Current = Value;
		}
	};
} // namespace

TEST(EditorPropTracker, OneGoStartsFreshAfterAnUnfinishedDrag)
{
	CObservedPropTracker Tracker;
	int First = 10;
	Tracker.Begin(&First, ETilesProp::PROP_WIDTH, EEditState::START, 0, 0);
	First = 11;
	Tracker.End(ETilesProp::PROP_WIDTH, EEditState::START, 0, 0);

	int Second = 20;
	Tracker.Begin(&Second, ETilesProp::PROP_HEIGHT, EEditState::ONE_GO, 0, 1);
	Second = 25;
	Tracker.End(ETilesProp::PROP_HEIGHT, EEditState::ONE_GO, 0, 1);

	EXPECT_EQ(Tracker.m_NumEdits, 1);
	EXPECT_EQ(Tracker.m_Previous, 20);
	EXPECT_EQ(Tracker.m_Current, 25);
}

TEST(EditorPropTracker, EndFromAnotherObjectDoesNotRecordTheOldDrag)
{
	CObservedPropTracker Tracker;
	int First = 10;
	Tracker.Begin(&First, ETilesProp::PROP_WIDTH, EEditState::START, 0, 0);
	First = 11;
	Tracker.End(ETilesProp::PROP_WIDTH, EEditState::START, 0, 0);

	const int Second = 20;
	Tracker.Begin(&Second, ETilesProp::PROP_WIDTH, EEditState::END, 0, 1);
	Tracker.End(ETilesProp::PROP_WIDTH, EEditState::END, 0, 1);

	EXPECT_EQ(Tracker.m_NumEdits, 0);
}

TEST(EditorPropTracker, EndFromAnotherGroupDoesNotRecordACommonProperty)
{
	CObservedPropTracker Tracker;
	Tracker.Begin(nullptr, ETilesProp::PROP_SHIFT_BY, EEditState::START, 0, 0);
	Tracker.m_ShiftBy = 2;
	Tracker.End(ETilesProp::PROP_SHIFT_BY, EEditState::START, 0, 0);

	Tracker.Begin(nullptr, ETilesProp::PROP_SHIFT_BY, EEditState::END, 1, 0);
	Tracker.End(ETilesProp::PROP_SHIFT_BY, EEditState::END, 1, 0);

	EXPECT_EQ(Tracker.m_NumEdits, 0);
}

TEST(EditorPropTracker, SameObjectCanFinishAfterMovingToAnotherGroup)
{
	CObservedPropTracker Tracker;
	int Value = 10;
	Tracker.Begin(&Value, ETilesProp::PROP_WIDTH, EEditState::START, 0, 0);
	Value = 11;
	Tracker.End(ETilesProp::PROP_WIDTH, EEditState::START, 1, 0);

	Tracker.Begin(&Value, ETilesProp::PROP_WIDTH, EEditState::END, 1, 0);
	Tracker.End(ETilesProp::PROP_WIDTH, EEditState::END, 1, 0);

	EXPECT_EQ(Tracker.m_NumEdits, 1);
	EXPECT_EQ(Tracker.m_Previous, 10);
	EXPECT_EQ(Tracker.m_Current, 11);
}
