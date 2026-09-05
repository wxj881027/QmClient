#include <game/client/components/qmclient/core/qm_ui_model.h>
#include <game/client/components/qmclient/core/qm_i18n.h>

#include <gtest/gtest.h>

#include <deque>

TEST(QmUiModel, RegistersUniqueCards)
{
	CQmUiModel Model;
	SQmFeatureModel Diagnostics{"qm.diagnostics", "qm.diagnostics.title", true, true};
	EXPECT_TRUE(Model.RegisterFeature(Diagnostics));
	EXPECT_TRUE(Model.RegisterCard({EQmUiPage::HOME, "qm.diagnostics", "activity", &Diagnostics}));
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::SEARCH, "qm.diagnostics", "activity", &Diagnostics}));
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "", "activity", &Diagnostics}));
	ASSERT_NE(Model.FindCard("qm.diagnostics"), nullptr);
	EXPECT_EQ(Model.FindCard("qm.diagnostics")->m_Page, EQmUiPage::HOME);
	EXPECT_EQ(Model.FindCard("qm.diagnostics")->m_pFeature, &Diagnostics);
	Diagnostics.m_Enabled = false;
	EXPECT_FALSE(Model.FindCard("qm.diagnostics")->m_pFeature->m_Enabled);
}

TEST(QmUiModel, FiltersCardsByPage)
{
	CQmUiModel Model;
	SQmFeatureModel Diagnostics{"qm.diagnostics", "qm.diagnostics.title", true, true};
	SQmFeatureModel Search{"qm.search", "qm.search.title", false, true};
	ASSERT_TRUE(Model.RegisterFeature(Diagnostics));
	ASSERT_TRUE(Model.RegisterFeature(Search));
	ASSERT_TRUE(Model.RegisterCard({EQmUiPage::HOME, "qm.diagnostics", "activity", &Diagnostics}));
	ASSERT_TRUE(Model.RegisterCard({EQmUiPage::SEARCH, "qm.search", "magnifying-glass", &Search}));
	const auto vHomeCards = Model.CardsForPage(EQmUiPage::HOME);
	const auto vSearchCards = Model.CardsForPage(EQmUiPage::SEARCH);
	ASSERT_EQ(vHomeCards.size(), 1);
	ASSERT_EQ(vSearchCards.size(), 1);
	EXPECT_EQ(vHomeCards.front()->m_Id, "qm.diagnostics");
	EXPECT_EQ(vSearchCards.front()->m_Id, "qm.search");
}

TEST(QmUiModel, KeepsFindCardPointersStableAndFreezesRegistration)
{
	CQmUiModel Model;
	SQmFeatureModel First{"qm.first", "qm.first.title", false, true};
	ASSERT_TRUE(Model.RegisterFeature(First));
	ASSERT_TRUE(Model.RegisterCard({EQmUiPage::HOME, "qm.first", "circle", &First}));
	const SQmUiCard *pFirst = Model.FindCard("qm.first");
	ASSERT_NE(pFirst, nullptr);
	std::deque<std::string> Ids;
	std::deque<SQmFeatureModel> Features;

	for(int i = 0; i < 32; ++i)
	{
		const std::string Id = "qm.card" + std::to_string(i);
		// 让 model 与 card 都由外部 feature owner 持有，模拟真实注册生命周期。
		Ids.push_back(Id);
		Features.push_back({Ids.back().c_str(), "qm.card.title", false, true});
		ASSERT_TRUE(Model.RegisterFeature(Features.back()));
		ASSERT_TRUE(Model.RegisterCard({EQmUiPage::HOME, Ids.back().c_str(), "circle", &Features.back()}));
	}
	EXPECT_EQ(Model.FindCard("qm.first"), pFirst);

	Model.Freeze();
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "qm.after_freeze", "circle", &First}));
}

TEST(QmUiModel, RejectsUnstableIdsAndUnknownFeatures)
{
	CQmUiModel Model;
	SQmFeatureModel Uppercase{"qm.Bad", "title", false, true};
	EXPECT_FALSE(Model.RegisterFeature(Uppercase));

	SQmFeatureModel Feature{"qm.valid", "title", false, true};
	SQmFeatureModel Unknown{"qm.unknown", "title", false, true};
	ASSERT_TRUE(Model.RegisterFeature(Feature));
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "qm.valid", "circle", &Unknown}));
	SQmFeatureModel SameId{"qm.valid", "other-title", false, true};
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "qm.same-model", "circle", &SameId}));
	SQmFeatureModel NoId{nullptr, "title", false, true};
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "qm.no-feature-id", "circle", &NoId}));
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "qm.valid ", "circle", &Feature}));
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "", "circle", &Feature}));
	EXPECT_FALSE(Model.RegisterCard({EQmUiPage::HOME, "qm.", "circle", &Feature}));
	EXPECT_FALSE(Model.RegisterCard({static_cast<EQmUiPage>(99), "qm.other", "circle", &Feature}));
}

namespace
{
const char *QmTestLookup(const char *pKey, const char *pContext)
{
	if(pContext && pContext[0] != '\0')
		return pContext;
	if(pKey && std::string(pKey) == "qm.translated")
		return "已翻译";
	return pKey;
}
}

TEST(QmI18n, UsesLookupAndFallback)
{
	CQmI18n I18n;
	EXPECT_STREQ(I18n.Text("qm.missing", "Fallback"), "Fallback");
	I18n.SetLookup(QmTestLookup);
	EXPECT_STREQ(I18n.Text("qm.translated", "Fallback"), "已翻译");
	EXPECT_STREQ(I18n.Text("qm.missing", "Fallback"), "Fallback");
	EXPECT_STREQ(I18n.Text("qm.translated", "Fallback", "Context"), "Context");
	EXPECT_STREQ(I18n.Text(nullptr, "Fallback"), "Fallback");
}
