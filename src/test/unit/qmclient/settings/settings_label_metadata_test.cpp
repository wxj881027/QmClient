#include <game/client/QmUi/SettingsLabelMetadata.h>

#include <gtest/gtest.h>

#include <string_view>

TEST(SettingsLabelMetadata, MainAndTipAreLocalizedIndependentlyWithoutChangingTheBoundIdentity)
{
	const auto Translate = [](const char *pSource) {
		return std::string_view(pSource) == "Concurrency" ? "并发数" : "0 表示自动";
	};
	const auto Result = ResolveSettingsLabelPresentation("qmclient-translate-llm-concurrency", "旧说明", Translate);
	EXPECT_STREQ(Result.m_pText, "并发数");
	EXPECT_STREQ(Result.m_pTip, "0 表示自动");
	EXPECT_NE(Result.m_pTipId, nullptr);
	const auto Repeated = ResolveSettingsLabelPresentation("qmclient-translate-llm-concurrency", "不同回退文字", Translate);
	EXPECT_EQ(Repeated.m_pTipId, Result.m_pTipId);
}

TEST(SettingsLabelMetadata, TemporarilyControlledCheckboxLeavesTooltipToTheRowUntilReleased)
{
	const auto Translate = [](const char *pSource) { return pSource; };
	const auto Locked = ResolveSettingsLabelPresentation("tclient-fast-input", "fallback", Translate, false);
	EXPECT_STREQ(Locked.m_pText, "Fast input");
	EXPECT_EQ(Locked.m_pTip, nullptr);
	EXPECT_EQ(Locked.m_pTipId, nullptr);
	const auto Released = ResolveSettingsLabelPresentation("tclient-fast-input", "fallback", Translate);
	EXPECT_STREQ(Released.m_pText, Locked.m_pText);
	EXPECT_NE(Released.m_pTip, nullptr);
	EXPECT_NE(Released.m_pTipId, nullptr);
}

TEST(SettingsLabelMetadata, UnregisteredUnitsNamesAndDynamicStatusesKeepTheirExactText)
{
	const char *pUnit = "距离（格）";
	const char *pName = "Chinese (Traditional)";
	const char *pFailure = "请求失败（HTTP 429）";
	int Translations = 0;
	const auto Translate = [&](const char *pSource) { ++Translations; return pSource; };
	for(const char *pText : {pUnit, pName, pFailure})
	{
		const auto Result = ResolveSettingsLabelPresentation("unknown", pText, Translate);
		EXPECT_EQ(Result.m_pText, pText);
		EXPECT_EQ(Result.m_pTip, nullptr);
	}
	EXPECT_EQ(Translations, 0);
	EXPECT_EQ(ResolveSettingsLabelPresentation(nullptr, pName, Translate).m_pText, pName);
}
