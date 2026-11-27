#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsLabelMetadata.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

namespace
{
	std::string FindRuntimeTranslation(const std::string &LanguageData, const char *pKey)
	{
		const std::string Prefix = std::string(pKey) + "\n== ";
		size_t SearchPos = 0;
		while(true)
		{
			const size_t TranslationStart = LanguageData.find(Prefix, SearchPos);
			if(TranslationStart == std::string::npos)
				return {};
			if(TranslationStart == 0 || LanguageData[TranslationStart - 1] == '\n')
			{
				const size_t ValueStart = TranslationStart + Prefix.size();
				const size_t ValueEnd = LanguageData.find('\n', ValueStart);
				return LanguageData.substr(ValueStart, ValueEnd - ValueStart);
			}
			SearchPos = TranslationStart + 1;
		}
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

TEST(SettingsCardTranslationContract, ChineseScoreboardPlaytimeHeadersUseHoursWithoutParentheses)
{
	const char *pKey = "Playtime (h)";
	EXPECT_EQ(FindRuntimeTranslation(ReadTestSourceFile("data/languages/simplified_chinese.txt"), pKey), "游玩小时");
	EXPECT_EQ(FindRuntimeTranslation(ReadTestSourceFile("data/languages/traditional_chinese.txt"), pKey), "遊玩小時");
}

TEST(SettingsCardTranslationContract, AuditedUiLabelsHaveSimplifiedChineseRuntimeTranslations)
{
	const std::string SimplifiedChinese = ReadTestSourceFile("data/languages/simplified_chinese.txt");
	static const char *const s_apKeys[] = {"Deprecated compatibility alias for qm_graphics_trace (macOS signposts)", "Enable enhanced scoreboard presentation", "Smooth the mouse-driven camera in live and demo free spectating", "Smooth free spectator camera", "Global UI size percentage", "Gores", "Hide chat messages from players marked as enemies", "Interface surface", "Ping", "Relative X position of the draggable back button", "Relative Y position of the draggable back button", "RTT", "Show draggable virtual back button", "UI icon custom color", "Word filter action: 0=replace matching words, 1=hide entire message"};
	for(const char *pKey : s_apKeys)
	{
		SCOPED_TRACE(pKey);
		const std::string Translation = FindRuntimeTranslation(SimplifiedChinese, pKey);
		ASSERT_FALSE(Translation.empty());
		EXPECT_NE(Translation, pKey);
	}
	for(const char *pId : {"tclient-mini-debug", "tclient-fast-input", "tclient-antiping-negative-buffer"})
	{
		SCOPED_TRACE(pId);
		const auto *pMetadata = SettingsLabelMetadata(pId);
		ASSERT_NE(pMetadata, nullptr);
		const std::string Translation = FindRuntimeTranslation(SimplifiedChinese, pMetadata->m_pTip);
		EXPECT_FALSE(Translation.empty());
		EXPECT_NE(Translation, pMetadata->m_pTip);
	}
}
