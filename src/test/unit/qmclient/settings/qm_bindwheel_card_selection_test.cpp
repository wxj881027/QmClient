#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>

#include <gtest/gtest.h>

using qm_tclient_cards::CTClientBindWheelSelection;

TEST(QmBindWheelCardSelection, RemovingAnEarlierBindInvalidatesTheOldSlot)
{
	std::vector<CBindWheel::CBind> vBinds{{"First", "echo first"}, {"Second", "echo second"}, {"Third", "echo third"}};
	CTClientBindWheelSelection Selection;
	Selection.Select(1, vBinds[1]);
	ASSERT_EQ(Selection.Resolve(vBinds), 1);

	vBinds.erase(vBinds.begin());

	EXPECT_EQ(Selection.Resolve(vBinds), -1);
	Selection.Select(0, vBinds[0]);
	EXPECT_EQ(Selection.Resolve(vBinds), 0);
}

TEST(QmBindWheelCardSelection, ReplacingTheCommandInvalidatesTheOldDraftTarget)
{
	std::vector<CBindWheel::CBind> vBinds{{"First", "echo original"}, {"Second", "echo second"}};
	CTClientBindWheelSelection Selection;
	Selection.Select(0, vBinds[0]);

	str_copy(vBinds[0].m_aCommand, "echo updated");

	EXPECT_EQ(Selection.Resolve(vBinds), -1);
	Selection.Select(0, vBinds[0]);
	EXPECT_EQ(Selection.Resolve(vBinds), 0);
}

TEST(QmBindWheelCardSelection, AppendingAnEntryKeepsTheDraftTarget)
{
	std::vector<CBindWheel::CBind> vBinds{{"Phrase", "say original"}};
	CTClientBindWheelSelection Selection;
	Selection.Select(0, vBinds[0]);

	vBinds.push_back({"Other phrase", "say other"});

	EXPECT_EQ(Selection.Resolve(vBinds), 0);
}

TEST(QmBindWheelCardSelection, RenamingAnEntryRequiresSelectingItAgain)
{
	std::vector<CBindWheel::CBind> vBinds{{"Phrase", "say original"}};
	CTClientBindWheelSelection Selection;
	Selection.Select(0, vBinds[0]);

	str_copy(vBinds[0].m_aName, "Updated phrase");

	EXPECT_EQ(Selection.Resolve(vBinds), -1);
	Selection.Select(0, vBinds[0]);
	EXPECT_EQ(Selection.Resolve(vBinds), 0);
}

TEST(QmBindWheelCardSelection, ClearingSelectionDoesNotReuseTheSameEntry)
{
	std::vector<CBindWheel::CBind> vBinds{{"Phrase", "say original"}};
	CTClientBindWheelSelection Selection;
	Selection.Select(0, vBinds[0]);

	Selection.Clear();

	EXPECT_EQ(Selection.Resolve(vBinds), -1);
}
