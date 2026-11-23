#ifndef GAME_CLIENT_QMUI_SETTINGSTEXTPLANUNITS_H
#define GAME_CLIENT_QMUI_SETTINGSTEXTPLANUNITS_H

#include <base/str.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum ESettingsMenuTextPlanCollectionUnitKind
{
	MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS = 0,
	MENU_TEXT_PLAN_UNIT_TCLIENT_TAB,
	MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB,
	MENU_TEXT_PLAN_UNIT_CONTRIBUTORS_TAB,
	MENU_TEXT_PLAN_UNIT_BASE_PAGE,
	MENU_TEXT_PLAN_UNIT_BASE_PAGE_TAB,
	MENU_TEXT_PLAN_UNIT_INGAME_ESC,
};
struct SSettingsMenuTextPlanSubtabGroup
{
	int m_Page = -1;
	int m_NumTabs = 0;
};
struct SSettingsMenuTextPlanCollectionUnit
{
	ESettingsMenuTextPlanCollectionUnitKind m_Kind = MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS;
	int m_Page = -1;
	int m_Tab = -1;
};

inline bool QmSettingsTextPlanItemMatchesVisible(int ItemPage, int ItemTab, int VisiblePage, int VisibleTab)
{
	if(ItemPage < 0 || ItemPage != VisiblePage)
		return false;
	return VisibleTab < 0 ? ItemTab < 0 : ItemTab == VisibleTab;
}

inline std::vector<SSettingsMenuTextPlanCollectionUnit> QmSettingsTextPlanUnits(bool IngameEsc, int CurrentPage, int CurrentTab,
	bool PreferQmClient, int TClientPage, int QmClientPage, int LastTClientTab, int LastQmClientTab,
	int NumTClientTabs, int NumQmClientTabs, int HiddenQmClientTab,
	int ContributorsPage, int NumContributorsTabs, std::span<const int> BasePages, std::span<const SSettingsMenuTextPlanSubtabGroup> SubtabGroups = {})
{
	// ESC 预热只收集游戏菜单，不能借空闲更新渲染隐藏设置页并触发字体目录等冷加载。
	if(IngameEsc)
		return {{MENU_TEXT_PLAN_UNIT_INGAME_ESC, -1, -1}};
	std::vector<SSettingsMenuTextPlanCollectionUnit> Units;
	const auto HasUnit = [&Units](int Page, int Tab) {
		return std::any_of(Units.begin(), Units.end(), [Page, Tab](const SSettingsMenuTextPlanCollectionUnit &Unit) {
			return Unit.m_Page == Page && Unit.m_Tab == Tab;
		});
	};
	const auto AddUnit = [&Units, &HasUnit](ESettingsMenuTextPlanCollectionUnitKind Kind, int Page, int Tab) {
		// 当前可见页会作为独立首单元收集；同一 page/tab 不再按基础页或页签
		// 重复渲染。单个页面实测可达 13 ms，重复单元直接拖慢计划覆盖速度。
		if(HasUnit(Page, Tab))
			return;
		Units.push_back({Kind, Page, Tab});
	};
	const auto AddTClientTabs = [&]() {
		for(int Tab = 0; Tab < NumTClientTabs; ++Tab)
			AddUnit(MENU_TEXT_PLAN_UNIT_TCLIENT_TAB, TClientPage, Tab);
	};
	const auto AddQmClientTabs = [&]() {
		for(int Tab = 0; Tab < NumQmClientTabs; ++Tab)
			if(Tab != HiddenQmClientTab)
				AddUnit(MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB, QmClientPage, Tab);
	};
	const auto AddContributorTabs = [&]() {
		// 每个贡献者子页签持有不同的卡片和 descriptor；基础页只渲染当时选中的
		// 子页签，因此所有子页签必须各自成为收集单元，不能只收当前页。
		for(int Tab = 0; Tab < NumContributorsTabs; ++Tab)
			AddUnit(MENU_TEXT_PLAN_UNIT_CONTRIBUTORS_TAB, ContributorsPage, Tab);
	};
	const auto AddSubtabGroup = [&AddUnit](const SSettingsMenuTextPlanSubtabGroup &Group) {
		for(int Tab = 0; Tab < Group.m_NumTabs; ++Tab)
			AddUnit(MENU_TEXT_PLAN_UNIT_BASE_PAGE_TAB, Group.m_Page, Tab);
	};

	AddUnit(MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS, CurrentPage, CurrentTab);
	// 当前页面自身的兄弟子页签紧跟首屏；切换子页签时其余子页签的 descriptor
	// 先补齐，再让其它一级页面占用收集预算。
	if(CurrentPage == ContributorsPage)
		AddContributorTabs();
	else if(CurrentPage == TClientPage)
		AddTClientTabs();
	else if(CurrentPage == QmClientPage)
		AddQmClientTabs();
	for(const SSettingsMenuTextPlanSubtabGroup &Group : SubtabGroups)
		if(CurrentPage == Group.m_Page)
			AddSubtabGroup(Group);
	if(PreferQmClient)
	{
		AddQmClientTabs();
		AddTClientTabs();
	}
	else
	{
		AddTClientTabs();
		AddQmClientTabs();
	}
	AddContributorTabs();
	for(int Page : BasePages)
		if(Page != ContributorsPage)
		{
			const auto Group = std::find_if(SubtabGroups.begin(), SubtabGroups.end(), [Page](const SSettingsMenuTextPlanSubtabGroup &Candidate) { return Candidate.m_Page == Page; });
			if(Group != SubtabGroups.end())
			{
				AddSubtabGroup(*Group);
				continue;
			}
			AddUnit(MENU_TEXT_PLAN_UNIT_BASE_PAGE, Page, -1);
		}
	AddUnit(MENU_TEXT_PLAN_UNIT_INGAME_ESC, -1, -1);
	return Units;
}

struct SSettingsTextPlanGlyphDemandInput
{
	const char *m_pText = nullptr;
	int m_PixelSize = 0;
	uint8_t m_Priority = 2;
};

struct SSettingsGlyphDemandEntry
{
	int m_Codepoint = 0;
	int m_PixelSize = 0;
	uint32_t m_Count = 0;
	uint8_t m_Priority = 2;
};

struct SSettingsGlyphPrewarmBatch
{
	std::string m_Text;
	int m_PixelSize = 0;
	size_t m_GlyphCount = 0;
};

// 设置页全部计划文本的静态字形需求表。键为 (codepoint, pixel size)，
// 值为出现次数；可见页优先，其次按频次、字号排序。动态 use/miss 热力图
// 只能由调用方在静态批次完成后作为次级来源，不能改变本表的排序。
class CSettingsGlyphDemandTable
{
	std::vector<SSettingsGlyphDemandEntry> m_vEntries;
	std::vector<SSettingsGlyphPrewarmBatch> m_vBatches;
	uint64_t m_TotalDemands = 0;
	size_t m_DistinctGlyphs = 0;

	static uint64_t GlyphKey(int Codepoint, int PixelSize)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(PixelSize)) << 32) | static_cast<uint32_t>(Codepoint);
	}

	static bool BuildBatches(const std::vector<SSettingsGlyphDemandEntry> &vEntries, size_t MaxBatchGlyphs, std::vector<SSettingsGlyphPrewarmBatch> &vBatches)
	{
		if(MaxBatchGlyphs == 0)
			return false;
		vBatches.clear();
		std::array<std::map<int, std::vector<const SSettingsGlyphDemandEntry *>>, 3> aPriorityBuckets;
		for(const SSettingsGlyphDemandEntry &Entry : vEntries)
			aPriorityBuckets[std::min<size_t>(Entry.m_Priority, aPriorityBuckets.size() - 1)][Entry.m_PixelSize].push_back(&Entry);

		for(auto &PriorityBuckets : aPriorityBuckets)
		{
			std::vector<std::pair<int, std::vector<const SSettingsGlyphDemandEntry *> *>> vOrderedBuckets;
			vOrderedBuckets.reserve(PriorityBuckets.size());
			for(auto &[PixelSize, vBucket] : PriorityBuckets)
			{
				std::sort(vBucket.begin(), vBucket.end(), [](const SSettingsGlyphDemandEntry *pLeft, const SSettingsGlyphDemandEntry *pRight) {
					if(pLeft->m_Count != pRight->m_Count)
						return pLeft->m_Count > pRight->m_Count;
					return pLeft->m_Codepoint < pRight->m_Codepoint;
				});
				vOrderedBuckets.emplace_back(PixelSize, &vBucket);
			}
			std::stable_sort(vOrderedBuckets.begin(), vOrderedBuckets.end(), [](const auto &Left, const auto &Right) {
				const auto Weight = [](const std::vector<const SSettingsGlyphDemandEntry *> &vBucket) {
					uint64_t Total = 0;
					for(const SSettingsGlyphDemandEntry *pEntry : vBucket)
						Total += pEntry->m_Count;
					return Total;
				};
				const uint64_t LeftWeight = Weight(*Left.second);
				const uint64_t RightWeight = Weight(*Right.second);
				return LeftWeight != RightWeight ? LeftWeight > RightWeight : Left.first > Right.first;
			});
			for(const auto &[PixelSize, pBucket] : vOrderedBuckets)
			{
				for(size_t Start = 0; Start < pBucket->size(); Start += MaxBatchGlyphs)
				{
					SSettingsGlyphPrewarmBatch Batch;
					Batch.m_PixelSize = PixelSize;
					const size_t End = std::min(pBucket->size(), Start + MaxBatchGlyphs);
					for(size_t Index = Start; Index < End; ++Index)
					{
						char aUtf8[5] = {};
						const int Length = str_utf8_encode(aUtf8, (*pBucket)[Index]->m_Codepoint);
						if(Length <= 0)
							continue;
						Batch.m_Text.append(aUtf8, Length);
						++Batch.m_GlyphCount;
					}
					if(Batch.m_GlyphCount > 0)
						vBatches.push_back(std::move(Batch));
				}
			}
		}
		return true;
	}

public:
	void Clear() noexcept
	{
		m_vEntries.clear();
		m_vBatches.clear();
		m_TotalDemands = 0;
		m_DistinctGlyphs = 0;
	}

	const std::vector<SSettingsGlyphDemandEntry> &Entries() const { return m_vEntries; }
	const std::vector<SSettingsGlyphPrewarmBatch> &PrewarmBatches() const { return m_vBatches; }
	uint64_t TotalGlyphDemands() const { return m_TotalDemands; }
	size_t DistinctGlyphs() const { return m_DistinctGlyphs; }

	bool Build(std::span<const SSettingsTextPlanGlyphDemandInput> Inputs, size_t MaxBatchGlyphs = 64) noexcept
	{
		if(MaxBatchGlyphs == 0)
			return false;
		try
		{
			std::vector<SSettingsGlyphDemandEntry> vCandidate;
			std::unordered_map<uint64_t, size_t> Index;
			uint64_t TotalDemands = 0;
			for(const SSettingsTextPlanGlyphDemandInput &Input : Inputs)
			{
				if(Input.m_pText == nullptr || Input.m_PixelSize <= 0)
					continue;
				const uint8_t Priority = Input.m_Priority > 2 ? 2 : Input.m_Priority;
				for(const char *pCursor = Input.m_pText; *pCursor != '\0';)
				{
					const int Codepoint = str_utf8_decode(&pCursor);
					if(Codepoint <= 0)
						break;
					if(Codepoint < 0x20 || Codepoint > 0x10FFFF)
						continue;
					const uint64_t Key = GlyphKey(Codepoint, Input.m_PixelSize);
					const auto It = Index.find(Key);
					if(It != Index.end())
					{
						SSettingsGlyphDemandEntry &Entry = vCandidate[It->second];
						if(Entry.m_Count < std::numeric_limits<uint32_t>::max())
							++Entry.m_Count;
						Entry.m_Priority = std::min(Entry.m_Priority, Priority);
					}
					else
					{
						Index.emplace(Key, vCandidate.size());
						vCandidate.push_back({Codepoint, Input.m_PixelSize, 1, Priority});
					}
					++TotalDemands;
				}
			}
			std::sort(vCandidate.begin(), vCandidate.end(), [](const SSettingsGlyphDemandEntry &Left, const SSettingsGlyphDemandEntry &Right) {
				if(Left.m_Priority != Right.m_Priority)
					return Left.m_Priority < Right.m_Priority;
				if(Left.m_Count != Right.m_Count)
					return Left.m_Count > Right.m_Count;
				if(Left.m_PixelSize != Right.m_PixelSize)
					return Left.m_PixelSize > Right.m_PixelSize;
				return Left.m_Codepoint < Right.m_Codepoint;
			});
			std::vector<SSettingsGlyphPrewarmBatch> vBatches;
			if(!BuildBatches(vCandidate, MaxBatchGlyphs, vBatches))
				return false;
			m_vEntries.swap(vCandidate);
			m_vBatches.swap(vBatches);
			m_TotalDemands = TotalDemands;
			m_DistinctGlyphs = m_vEntries.size();
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool BuildPrewarmBatches(size_t MaxBatchGlyphs = 64) noexcept
	{
		if(MaxBatchGlyphs == 0)
			return false;
		try
		{
			std::vector<SSettingsGlyphPrewarmBatch> vBatches;
			if(!BuildBatches(m_vEntries, MaxBatchGlyphs, vBatches))
				return false;
			m_vBatches.swap(vBatches);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
};
#endif
