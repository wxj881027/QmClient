#include <game/client/components/tclient/tclient.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmTClientFavoriteMapConsole, SavedCommandRoundTripsQuotesAndBackslashes)
{
	const char *pName = "Mountain \"Night\" \\ East";
	char aCommand[IConsole::CMDLINE_LENGTH];
	ASSERT_TRUE(qm_tclient_map_data::FormatFavoriteCommand(pName, aCommand));

	std::string Restored;
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	pConsole->Register("add_favorite_map", "s[map]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		*static_cast<std::string *>(pUser) = pResult->GetString(0);
	}, &Restored, "");
	pConsole->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(Restored, pName);
}

TEST(QmTClientFavoriteMapConsole, RejectsNamesThatDoNotFitAfterEscaping)
{
	const std::string Name(IConsole::CMDLINE_LENGTH / 2, '"');
	char aCommand[IConsole::CMDLINE_LENGTH];
	EXPECT_FALSE(qm_tclient_map_data::FormatFavoriteCommand(Name.c_str(), aCommand));
}
