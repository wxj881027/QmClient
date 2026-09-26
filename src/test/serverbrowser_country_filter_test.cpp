// 国家筛选名单的行为回归。
// 国家筛选是"排除名单"语义：名单只覆盖用户操作那一刻可选的国家，社区列表更新后新出现的国家
// 不能被当成"用户保留可见"的国家，否则"只看某几个国家"的结果会随时间漂移。
#include "test.h"

#include <engine/client/serverbrowser.h>

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <vector>

namespace
{

	// 只实现筛选逻辑需要的接口，可选国家由测试直接给出。
	class CTestCommunityCache : public ICommunityCache
	{
	public:
		std::vector<const CCommunityCountry *> m_vpSelectableCountries;
		const char *m_pCountryTypeFilterKey = IServerBrowser::COMMUNITY_DDNET;

		void Update(bool Force) override {}
		const std::vector<const CCommunity *> &SelectedCommunities() const override { return m_vpSelectedCommunities; }
		const std::vector<const CCommunityCountry *> &SelectableCountries() const override { return m_vpSelectableCountries; }
		const std::vector<const CCommunityType *> &SelectableTypes() const override { return m_vpSelectableTypes; }
		bool AnyRanksAvailable() const override { return false; }
		bool CountriesTypesFilterAvailable() const override { return true; }
		const char *CountryTypeFilterKey() const override { return m_pCountryTypeFilterKey; }

	private:
		std::vector<const CCommunity *> m_vpSelectedCommunities;
		std::vector<const CCommunityType *> m_vpSelectableTypes;
	};

	// 持有国家对象，保证 SelectableCountries 里的指针在测试期间始终有效。
	class CTestCountries
	{
	public:
		CCommunityCountry *Add(const char *pName, int FlagId = 1)
		{
			m_vpCountries.emplace_back(std::make_unique<CCommunityCountry>(pName, FlagId));
			return m_vpCountries.back().get();
		}

	private:
		std::vector<std::unique_ptr<CCommunityCountry>> m_vpCountries;
	};

} // namespace

TEST(ServerBrowserCountryFilter, NoFilterLeavesListUntouched)
{
	CTestCountries Countries;
	CTestCommunityCache Cache;
	Cache.m_vpSelectableCountries = {Countries.Add("CHN"), Countries.Add("GER")};
	CExcludedCommunityCountryFilterList Filter(&Cache);

	EXPECT_FALSE(Filter.AutoExcludeNewCountries());
	EXPECT_FALSE(Filter.Filtered("CHN"));
	EXPECT_FALSE(Filter.Filtered("GER"));
}

TEST(ServerBrowserCountryFilter, NewlyAppearingCountryIsExcluded)
{
	// 用户"只看中国"：排除德国，并记录中国是保留可见的。
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	Cache.m_vpSelectableCountries = {pChina, pGermany};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add("GER");
	Filter.AddAllowed("CHN");
	EXPECT_TRUE(Filter.Filtered("GER"));
	EXPECT_FALSE(Filter.Filtered("CHN"));

	// 社区列表之后新增了国家（新服务器上线或该国重新出现）。
	CCommunityCountry *pTurkey = Countries.Add("TUR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pTurkey};

	EXPECT_TRUE(Filter.AutoExcludeNewCountries());
	EXPECT_TRUE(Filter.Filtered("TUR"));
	// 用户保留的国家不受影响，"只看中国"的结果保持稳定。
	EXPECT_FALSE(Filter.Filtered("CHN"));
}

TEST(ServerBrowserCountryFilter, LegacyConfigSeedsBaselineWithoutChangingVisibleCountries)
{
	// 旧配置只有排除名单，没有保留基线：无法区分"新国家"和"用户保留的国家"，只能先建立基线。
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	Cache.m_vpSelectableCountries = {pChina, pGermany};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add("GER");

	EXPECT_FALSE(Filter.AutoExcludeNewCountries());
	EXPECT_FALSE(Filter.Filtered("CHN"));

	// 基线已按当前可见集合建立，之后新出现的国家会被补进排除名单。
	CCommunityCountry *pTurkey = Countries.Add("TUR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pTurkey};
	EXPECT_TRUE(Filter.AutoExcludeNewCountries());
	EXPECT_TRUE(Filter.Filtered("TUR"));
	EXPECT_FALSE(Filter.Filtered("CHN"));
}

TEST(ServerBrowserCountryFilter, ManuallyEnabledCountryStaysVisible)
{
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	CCommunityCountry *pKorea = Countries.Add("KOR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pKorea};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add("GER");
	Filter.Add("KOR");
	Filter.AddAllowed("CHN");

	// 用户又把韩国点开：这是显式保留，之后不能被自动排除。
	Filter.Remove("KOR");
	EXPECT_FALSE(Filter.Filtered("KOR"));

	CCommunityCountry *pTurkey = Countries.Add("TUR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pKorea, pTurkey};
	EXPECT_TRUE(Filter.AutoExcludeNewCountries());
	EXPECT_TRUE(Filter.Filtered("TUR"));
	EXPECT_FALSE(Filter.Filtered("CHN"));
	EXPECT_FALSE(Filter.Filtered("KOR"));
}

TEST(ServerBrowserCountryFilter, EnablingCountryWithoutBaselineKeepsOtherVisibleCountries)
{
	// 没有基线时点亮一个国家，必须按"当时的可见集合"整体建立基线，
	// 否则其他可见国家会在下一次同步时被当成新国家排除掉。
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	CCommunityCountry *pKorea = Countries.Add("KOR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pKorea};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add("GER");
	Filter.Add("KOR");

	Filter.Remove("KOR");
	EXPECT_FALSE(Filter.Filtered("KOR"));

	CCommunityCountry *pTurkey = Countries.Add("TUR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pKorea, pTurkey};
	EXPECT_TRUE(Filter.AutoExcludeNewCountries());
	EXPECT_TRUE(Filter.Filtered("TUR"));
	EXPECT_FALSE(Filter.Filtered("CHN"));
	EXPECT_FALSE(Filter.Filtered("KOR"));
}

TEST(ServerBrowserCountryFilter, UnclassifiedNoneCountryIsNeverAutoExcluded)
{
	// 与筛选网格一致：未分类(none) 不参与排除，否则未知国家的服务器会被连带隐藏。
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	CCommunityCountry *pNone = Countries.Add(IServerBrowser::COMMUNITY_COUNTRY_NONE, -1);
	Cache.m_vpSelectableCountries = {pChina, pGermany, pNone};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add("GER");
	Filter.AddAllowed("CHN");

	EXPECT_FALSE(Filter.AutoExcludeNewCountries());
	EXPECT_FALSE(Filter.Filtered(IServerBrowser::COMMUNITY_COUNTRY_NONE));
}

TEST(ServerBrowserCountryFilter, CleanKeepsUserFilterIntact)
{
	// 社区国家列表由当时的服务器聚合而成，某个国家的服务器临时下线时不能按它裁剪名单条目，
	// 否则该国回来后"只看某国"的结果会永久漂移。这里保证清理不会动用户设置。
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	CCommunityCountry *pTurkey = Countries.Add("TUR");
	Cache.m_vpSelectableCountries = {pChina, pGermany, pTurkey};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add("GER");
	Filter.Add("TUR");
	Filter.AddAllowed("CHN");

	// 社区仍然存在（ddnet），清理不该删除任何国家条目。
	Filter.CleanCountries({CCommunityId(IServerBrowser::COMMUNITY_DDNET)});

	EXPECT_TRUE(Filter.Filtered("GER"));
	EXPECT_TRUE(Filter.Filtered("TUR"));
	EXPECT_FALSE(Filter.Filtered("CHN"));
}

TEST(ServerBrowserCountryFilter, CleanDropsEntriesOfRemovedCommunities)
{
	CTestCountries Countries;
	CTestCommunityCache Cache;
	CCommunityCountry *pChina = Countries.Add("CHN");
	CCommunityCountry *pGermany = Countries.Add("GER");
	Cache.m_vpSelectableCountries = {pChina, pGermany};
	CExcludedCommunityCountryFilterList Filter(&Cache);
	Filter.Add(IServerBrowser::COMMUNITY_ALL, "GER");
	Filter.Add(IServerBrowser::COMMUNITY_DDNET, "GER");
	EXPECT_TRUE(Filter.Filtered("GER"));

	// ddnet 社区已不存在，只有 all 保留。
	Filter.CleanCountries({CCommunityId(IServerBrowser::COMMUNITY_ALL)});

	EXPECT_FALSE(Filter.Filtered("GER")); // 当前 key 是 ddnet，条目已被清理

	Cache.m_pCountryTypeFilterKey = IServerBrowser::COMMUNITY_ALL;
	EXPECT_TRUE(Filter.Filtered("GER"));
}
