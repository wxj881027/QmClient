#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmTClientConfigStringEdit, LongEscapedValueSurvivesEditingAndApplyingThroughTheConsole)
{
	char aValue[sizeof(g_Config.m_QmColorPickerPalette)];
	char aOldValue[sizeof(aValue)];
	const auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SStringConfigVariable Variable(pConsole.get(), "qm_test_card_text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "", sizeof(aValue), aOldValue);
	Variable.Register();

	std::string Original(sizeof(aValue) - 1, 'x');
	for(size_t i = 0; i + 1 < Original.size(); i += 2)
	{
		Original[i] = '\\';
		Original[i + 1] = '"';
	}
	str_copy(aValue, Original.c_str());

	qm_tclient_cards::CTClientConfigStringInput State(Variable, Variable.m_pStr);
	State.m_Input.SetRange("ab", 0, 2);
	const auto Command = qm_tclient_cards::PrepareTClientConfigStringCommand(*pConsole, Variable, State.m_Input.GetString());
	ASSERT_TRUE(Command.has_value());
	pConsole->ExecuteLine(Command->c_str());

	const std::string Expected = "ab" + Original.substr(2);
	EXPECT_EQ(std::string(aValue), Expected);
	EXPECT_EQ(std::string(aOldValue), Expected);
}

TEST(QmTClientConfigStringEdit, ValueBeyondTheConsoleLimitKeepsTheSavedValueAndDraft)
{
	char aValue[sizeof(g_Config.m_QmBlockWordsList)];
	char aOldValue[sizeof(aValue)];
	const auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SStringConfigVariable Variable(pConsole.get(), "qm_test_card_text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "original", sizeof(aValue), aOldValue);
	Variable.Register();

	std::string Draft;
	for(int i = 0; i < 4096; ++i)
		Draft += "word,";
	qm_tclient_cards::CTClientConfigStringInput State(Variable, Draft.c_str());

	EXPECT_FALSE(qm_tclient_cards::PrepareTClientConfigStringCommand(*pConsole, Variable, State.m_Input.GetString()).has_value());
	EXPECT_EQ(State.m_Input.GetString(), Draft);
	EXPECT_STREQ(aValue, "original");
	EXPECT_STREQ(aOldValue, "original");
}

TEST(QmTClientConfigStringEdit, ShortEditsRespectTheActualVariableCapacity)
{
	char aValue[5], aOldValue[5];
	SStringConfigVariable Variable(nullptr, "qm_test_card_text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "a", sizeof(aValue), aOldValue);
	qm_tclient_cards::CTClientConfigStringInput State(Variable, Variable.m_pStr);

	State.m_Input.Insert("bcdef", 1);

	EXPECT_STREQ(State.m_Input.GetString(), "abcd");
	EXPECT_STREQ(aValue, "a");
}

TEST(QmTClientConfigStringEdit, BoundedHumanReadableCommandKeepsUtf8AndClosingQuote)
{
	char aValue[16], aOldValue[16];
	const auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SStringConfigVariable Variable(pConsole.get(), "text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "abc界", sizeof(aValue), aOldValue);
	Variable.Register();

	char aCommand[12];
	Variable.SerializeHumanReadable(aCommand, sizeof(aCommand));
	EXPECT_STREQ(aCommand, "text \"abc\"");
	pConsole->ExecuteLine(aCommand);
	EXPECT_STREQ(aValue, "abc");
}
