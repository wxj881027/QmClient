// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmCardWiki.h>
#include <game/client/QmUi/QmWikiDocs.h>
#include <game/client/QmUi/SettingsCardGeometry.h>
#include <game/client/QmUi/SettingsCardInfo.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <set>
#include <string>

TEST(CardWikiTest, EveryEntryCarriesIdAndThreeReadableSections)
{
	ASSERT_GT(CardWikiCopyCount(), 0u);
	std::set<std::string> SeenIds;
	for(size_t Index = 0; Index < CardWikiCopyCount(); ++Index)
	{
		const SCardWikiCopy *pCopy = CardWikiCopyAt(Index);
		ASSERT_NE(pCopy, nullptr);
		ASSERT_NE(pCopy->m_pStableId, nullptr);
		EXPECT_FALSE(pCopy->m_pStableId[0] == '\0') << "entry " << Index << " has an empty stableId";
		EXPECT_TRUE(SeenIds.insert(pCopy->m_pStableId).second) << "duplicate stableId: " << pCopy->m_pStableId;
		ASSERT_NE(pCopy->m_pWhat, nullptr);
		ASSERT_NE(pCopy->m_pHow, nullptr);
		ASSERT_NE(pCopy->m_pAdvice, nullptr);
		EXPECT_GT(str_length(pCopy->m_pWhat), 0) << pCopy->m_pStableId;
		EXPECT_GT(str_length(pCopy->m_pHow), 0) << pCopy->m_pStableId;
		EXPECT_GT(str_length(pCopy->m_pAdvice), 0) << pCopy->m_pStableId;
		// 首段同时是 ⓘ 的悬浮摘要，过长会撑满卡片提示；三段都不允许写成一段。
		EXPECT_LE(str_length(pCopy->m_pWhat), 220) << pCopy->m_pStableId;
		EXPECT_LE(str_length(pCopy->m_pHow), 260) << pCopy->m_pStableId;
		EXPECT_LE(str_length(pCopy->m_pAdvice), 220) << pCopy->m_pStableId;
	}
}

TEST(CardWikiTest, EveryEntryPointsAtARegisteredCard)
{
	ASSERT_GT(CardWikiCopyCount(), 0u);
	for(size_t Index = 0; Index < CardWikiCopyCount(); ++Index)
	{
		const SCardWikiCopy *pCopy = CardWikiCopyAt(Index);
		ASSERT_NE(pCopy, nullptr);
		// 拼错的 stableId 不会报错，只会让 ⓘ 永远不出现；这里把它变成测试失败。
		const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pCopy->m_pStableId);
		ASSERT_NE(pDefault, nullptr) << pCopy->m_pStableId;
		ASSERT_NE(pDefault->m_pTitle, nullptr) << pCopy->m_pStableId;
		EXPECT_GT(str_length(pDefault->m_pTitle), 0) << pCopy->m_pStableId;
	}
}

TEST(CardWikiTest, LookupIsStableAndIgnoresUnknownIds)
{
	const SCardWikiCopy *pFirst = CardWikiCopyAt(0);
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(FindCardWikiCopy(pFirst->m_pStableId), pFirst);
	EXPECT_TRUE(HasCardWikiCopy(pFirst->m_pStableId));
	EXPECT_EQ(FindCardWikiCopy("deck:this-card-does-not-exist"), nullptr);
	EXPECT_FALSE(HasCardWikiCopy("deck:this-card-does-not-exist"));
	EXPECT_EQ(FindCardWikiCopy(nullptr), nullptr);
	EXPECT_FALSE(HasCardWikiCopy(nullptr));
	EXPECT_EQ(FindCardWikiCopy(""), nullptr);
}

TEST(CardWikiTest, EntriesAreSortedByStableId)
{
	// 查表用二分，排序是它的前提；顺序错乱必须在这里失败，而不是悄悄查不到。
	ASSERT_GT(CardWikiCopyCount(), 1u);
	for(size_t Index = 1; Index < CardWikiCopyCount(); ++Index)
	{
		const SCardWikiCopy *pPrevious = CardWikiCopyAt(Index - 1);
		const SCardWikiCopy *pCurrent = CardWikiCopyAt(Index);
		ASSERT_NE(pPrevious, nullptr);
		ASSERT_NE(pCurrent, nullptr);
		EXPECT_LT(str_comp(pPrevious->m_pStableId, pCurrent->m_pStableId), 0)
			<< "顺序错误: " << pPrevious->m_pStableId << " 应排在 " << pCurrent->m_pStableId << " 之后";
	}
}

TEST(CardWikiTest, CatalogCardsAndWikiEntriesMatchExactly)
{
	const std::vector<const char *> *apCatalogLists[] = {
		&qm_card_catalog::VisualCardStableIds(),
		&qm_card_catalog::FunctionCardStableIds(),
		&qm_card_catalog::HudCardStableIds(),
		&qm_card_catalog::BindCardStableIds(),
		&qm_card_catalog::NameplateCardStableIds(),
		&qm_card_catalog::TitleCardStableIds(),
		&qm_card_catalog::TeeCardStableIds(),
		&qm_card_catalog::GeneralCardStableIds(),
		&qm_card_catalog::ControlsCardStableIds(),
	};
	std::set<std::string> CatalogIds;
	for(const std::vector<const char *> *pList : apCatalogLists)
	{
		for(const char *pStableId : *pList)
		{
			ASSERT_NE(pStableId, nullptr);
			CatalogIds.insert(pStableId);
		}
	}
	// 标准设置页（图像、声音、玩家、DDNet、外观、TClient 等）的卡片由注册表组织，
	// 没有像 Qm 页那样的目录访问器；全部纳入覆盖，避免新增卡片漏文案。
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		ASSERT_NE(Default.m_pStableId, nullptr);
		// 只为搜索导航存在、会跳到别的 tab 的别名不是卡片，不要求自己的文案。
		if(Default.m_pNavigationTab != nullptr && Default.m_pDefaultTab != nullptr &&
			str_comp(Default.m_pNavigationTab, Default.m_pDefaultTab) != 0)
			continue;
		CatalogIds.insert(Default.m_pStableId);
	}
	ASSERT_GT(CatalogIds.size(), 0u);
	// 全覆盖：目录里的每张卡都必须有 Wiki 条目，否则 ⓘ 不会出现。
	for(const std::string &StableId : CatalogIds)
		EXPECT_TRUE(HasCardWikiCopy(StableId.c_str())) << "缺少 Wiki 条目: " << StableId;
	// 反向：条目必须对应真实卡片，避免拼错 id 后留下永远不会显示的文案。
	for(size_t Index = 0; Index < CardWikiCopyCount(); ++Index)
	{
		const SCardWikiCopy *pCopy = CardWikiCopyAt(Index);
		ASSERT_NE(pCopy, nullptr);
		ASSERT_NE(pCopy->m_pStableId, nullptr);
		EXPECT_TRUE(CatalogIds.count(pCopy->m_pStableId) != 0) << "条目对应的卡片不在目录里: " << pCopy->m_pStableId;
	}
}

TEST(WikiDocumentTest, VideoExportDocumentIsComplete)
{
	const SWikiDocument &Document = qm_wiki_docs::VideoExport();
	ASSERT_NE(Document.m_pId, nullptr);
	EXPECT_GT(str_length(Document.m_pId), 0);
	ASSERT_NE(Document.m_pTitle, nullptr);
	EXPECT_GT(str_length(Document.m_pTitle), 0);
	ASSERT_NE(Document.m_pSections, nullptr);
	// 导出弹窗的说明至少覆盖分辨率、编码器、画质、采样、容器、预设、GOP 与范围。
	EXPECT_GE(Document.m_SectionCount, 5);
	for(int SectionIndex = 0; SectionIndex < Document.m_SectionCount; ++SectionIndex)
	{
		const SWikiSection &Section = Document.m_pSections[SectionIndex];
		ASSERT_NE(Section.m_pHeading, nullptr) << SectionIndex;
		EXPECT_GT(str_length(Section.m_pHeading), 0) << SectionIndex;
		ASSERT_NE(Section.m_apParagraphs, nullptr) << Section.m_pHeading;
		EXPECT_GE(Section.m_ParagraphCount, 1) << Section.m_pHeading;
		for(int ParagraphIndex = 0; ParagraphIndex < Section.m_ParagraphCount; ++ParagraphIndex)
		{
			const char *pParagraph = Section.m_apParagraphs[ParagraphIndex];
			ASSERT_NE(pParagraph, nullptr) << Section.m_pHeading;
			EXPECT_GT(str_length(pParagraph), 0) << Section.m_pHeading;
			// 段落整段绘制，过长会让滚动区长得没有尽头，也不适合一眼读完。
			EXPECT_LE(str_length(pParagraph), 420) << Section.m_pHeading;
		}
	}
}

TEST(WikiDocumentTest, VideoExportSectionHeadingsAreUnique)
{
	const SWikiDocument &Document = qm_wiki_docs::VideoExport();
	ASSERT_NE(Document.m_pSections, nullptr);
	std::set<std::string> Headings;
	for(int SectionIndex = 0; SectionIndex < Document.m_SectionCount; ++SectionIndex)
	{
		const char *pHeading = Document.m_pSections[SectionIndex].m_pHeading;
		ASSERT_NE(pHeading, nullptr);
		EXPECT_TRUE(Headings.insert(pHeading).second) << "duplicate heading: " << pHeading;
	}
}

TEST(CardWikiTest, InfoEntryFollowsWikiTableAndInlineText)
{
	const SCardWikiCopy *pFirst = CardWikiCopyAt(0);
	ASSERT_NE(pFirst, nullptr);
	// Wiki 条目单独就能让 ⓘ 出现，不需要卡片补 m_pInfo。
	const SSettingsCardSpec WikiOnly{pFirst->m_pStableId, "Title", "Subtitle"};
	EXPECT_TRUE(HasSettingsCardInfo(WikiOnly));
	// 未登记的卡片保持没有入口，避免空白面板。
	const SSettingsCardSpec Unknown{"deck:this-card-does-not-exist", "Title", "Subtitle"};
	EXPECT_FALSE(HasSettingsCardInfo(Unknown));
	// 既有内联说明仍然有效，Wiki 不是唯一来源。
	const SSettingsCardSpec InlineText{"deck:this-card-does-not-exist", "Title", "Subtitle", "Inline info"};
	EXPECT_TRUE(HasSettingsCardInfo(InlineText));
	const SSettingsCardSpec NoId{nullptr, "Title", "Subtitle"};
	EXPECT_FALSE(HasSettingsCardInfo(NoId));
	const SSettingsCardSpec EmptyId{"", "Title", "Subtitle"};
	EXPECT_FALSE(HasSettingsCardInfo(EmptyId));
}
