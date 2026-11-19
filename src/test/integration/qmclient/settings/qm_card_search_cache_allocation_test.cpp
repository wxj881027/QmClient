#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/cards/QmCardCatalogSearch.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <vector>

#if GTEST_HAS_DEATH_TEST
namespace
{
	bool SameText(const char *pLeft, const char *pRight)
	{
		return pLeft == nullptr || pRight == nullptr ? pLeft == pRight : str_comp(pLeft, pRight) == 0;
	}

	bool SameResults(const std::vector<qm_card_registry::SCardSearchResult> &Left, const std::vector<qm_card_registry::SCardSearchResult> &Right)
	{
		if(Left.size() != Right.size())
			return false;
		for(size_t Index = 0; Index < Left.size(); ++Index)
		{
			const auto &A = Left[Index];
			const auto &B = Right[Index];
			if(!SameText(A.m_pStableId, B.m_pStableId) || A.m_Title != B.m_Title || A.m_Description != B.m_Description ||
				!SameText(A.m_Target.m_pTab, B.m_Target.m_pTab) || !SameText(A.m_Target.m_pStableId, B.m_Target.m_pStableId) ||
				A.m_vMatches != B.m_vMatches || A.m_vMatchKeys != B.m_vMatchKeys)
				return false;
		}
		return true;
	}

	int ProbeColdSearch(size_t BeforeFailure)
	try
	{
		std::set_terminate([] { std::_Exit(6); });
		g_Config.m_ClLanguagefile[0] = '\0';
		if(!g_Localization.Languages().empty())
			return 2;
		const auto &Defaults = qm_card_registry::Defaults();
		qm_card_order::CModel Model;
		Model.SetEntries(qm_card_registry::BuildDefaultEntries());
		// 预热注册元数据与模型，但不能先调用 SearchCards 暖起待测的静态缓存。
		const char *pQuery = nullptr;
		for(const auto &Default : Defaults)
		{
			const qm_card_registry::SCardSettingSearch *pSettings = nullptr;
			size_t Count = 0;
			qm_card_catalog::FindSearchMetadata(Default.m_pStableId, &pSettings, &Count);
			for(size_t Index = 0; Index < Count; ++Index)
				if(pSettings[Index].m_pConfigName != nullptr && pSettings[Index].m_pConfigName[0] != '\0')
					pQuery = pSettings[Index].m_pConfigName;
		}
		if(pQuery == nullptr)
			return 2;
		std::vector<qm_card_registry::SCardSearchResult> Attempt;
		bool Triggered = false;
		bool Caught = false;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			try
			{
				Attempt = qm_card_registry::SearchCards(pQuery, Model);
			}
			catch(const std::bad_alloc &)
			{
				Caught = true;
			}
			Triggered = Failure.Triggered();
		}
		const auto Retried = qm_card_registry::SearchCards(pQuery, Model);
		// 只改变检索缓存键，不加载译文；冷进程的空语言数据库保持相同搜索语义。
		str_copy(g_Config.m_ClLanguagefile, "allocation-probe-complete-index");
		const auto Expected = qm_card_registry::SearchCards(pQuery, Model);
		if(Expected.empty() || !SameResults(Retried, Expected))
			return 3;
		if(!Triggered)
			return !Caught && SameResults(Attempt, Expected) ? 1 : 4;
		// stable_sort 可能在临时缓冲拒收后原地完成，成功仍需是完整结果。
		if(!Caught && !SameResults(Attempt, Expected))
			return 4;
		return 0;
	}
	catch(...)
	{
		return 5;
	}

	struct SProbeExit
	{
		bool *m_pComplete;
		bool operator()(int Status) const
		{
			if(::testing::ExitedWithCode(1)(Status))
			{
				*m_pComplete = true;
				return true;
			}
			return ::testing::ExitedWithCode(0)(Status);
		}
	};

	class QmCardSearchCacheAllocationDeathTest : public ::testing::Test
	{
		std::string m_PreviousStyle;

	protected:
		void SetUp() override
		{
			m_PreviousStyle = GTEST_FLAG_GET(death_test_style);
			GTEST_FLAG_SET(death_test_style, "threadsafe");
		}
		void TearDown() override { GTEST_FLAG_SET(death_test_style, m_PreviousStyle); }
	};
}

TEST_F(QmCardSearchCacheAllocationDeathTest, SampledColdIndexAllocationFailuresRetryToTheCompleteResults)
{
	// 每次 reexec 都从空语言键与目录开始；对数采样避免按全部 new 启动数千个进程。
	constexpr size_t MAX_BUDGET = 65536;
	bool Complete = false;
	for(size_t Budget = 0; Budget <= MAX_BUDGET && !Complete; Budget = Budget == 0 ? 1 : Budget * 2)
	{
		SCOPED_TRACE(Budget);
		ASSERT_EXIT(std::_Exit(ProbeColdSearch(Budget)), SProbeExit{&Complete}, "");
	}
	EXPECT_TRUE(Complete);
}
#endif
