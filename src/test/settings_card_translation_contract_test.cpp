#include <game/client/QmUi/QmCardRegistry.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

namespace
{
	std::string FindRuntimeTranslation(const std::string &LanguageData, const char *pKey)
	{
		const std::string Prefix = std::string(pKey) + "\n== ";
		const size_t TranslationStart = LanguageData.find(Prefix);
		if(TranslationStart == std::string::npos)
			return {};
		const size_t ValueStart = TranslationStart + Prefix.size();
		const size_t ValueEnd = LanguageData.find('\n', ValueStart);
		return LanguageData.substr(ValueStart, ValueEnd - ValueStart);
	}
}

TEST(SettingsCardTranslationContract, EveryCardDescriptionHasSimplifiedChineseRuntimeTranslation)
{
	const std::string SimplifiedChinese = ReadTestSourceFile("data/languages/simplified_chinese.txt");
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Default.m_pStableId);
		const char *pDescription = qm_card_registry::ResolveDescriptionKey(Default);
		const std::string Translation = FindRuntimeTranslation(SimplifiedChinese, pDescription);
		ASSERT_FALSE(Translation.empty()) << pDescription;
		EXPECT_NE(Translation, pDescription);
	}
}

TEST(SettingsCardTranslationContract, AuditedUiLabelsHaveSimplifiedChineseRuntimeTranslations)
{
	const std::string SimplifiedChinese = ReadTestSourceFile("data/languages/simplified_chinese.txt");
	static const char *const s_apKeys[] = {"Card height animation", "Card list entry animation", "Card reflow animation", "Enable client stutter diagnostics at startup", "Enable enhanced scoreboard presentation", "Enable smooth cinematic camera while free spectating", "Global UI size percentage", "Gores", "Hide chat messages from players marked as enemies", "Interface surface", "Map browser surface", "Ping", "Relative X position of the draggable back button", "Relative Y position of the draggable back button", "RTT", "Scoreboard surface", "Text input focus ring color", "Presentation animations", "Show draggable virtual back button", "UI rounded corner segments (even numbers recommended)", "UI icon custom color", "Word filter action: 0=replace matching words, 1=hide entire message"};
	for(const char *pKey : s_apKeys)
	{
		SCOPED_TRACE(pKey);
		const std::string Translation = FindRuntimeTranslation(SimplifiedChinese, pKey);
		ASSERT_FALSE(Translation.empty());
		EXPECT_NE(Translation, pKey);
	}
}
