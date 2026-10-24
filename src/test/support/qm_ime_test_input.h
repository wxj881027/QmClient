#ifndef TEST_SUPPORT_QM_IME_TEST_INPUT_H
#define TEST_SUPPORT_QM_IME_TEST_INPUT_H

#include "qm_ui_test_input.h"

#include <engine/shared/qm_ime_policy.h>

namespace qm_ui_test
{
	// 场景持有候选来源；这里只提供输入设备查询，不实现候选发布或展示逻辑。
	class CTestImeInput final : public CTestInput
	{
		const SQmImeCandidateList &m_Candidates;

	public:
		std::string m_Composition;
		mutable int m_CandidateCountReads = 0;
		mutable int m_CandidateReads = 0;

		explicit CTestImeInput(const SQmImeCandidateList &Candidates) :
			m_Candidates(Candidates) {}
		const char *GetComposition() const override { return m_Composition.c_str(); }
		bool HasComposition() const override { return !m_Composition.empty(); }
		int GetCompositionLength() const override { return m_Composition.size(); }
		const char *GetCandidate(int Index) const override
		{
			++m_CandidateReads;
			return m_Candidates.m_vCandidates.at(Index).c_str();
		}
		int GetCandidateCount() const override
		{
			++m_CandidateCountReads;
			return m_Candidates.m_vCandidates.size();
		}
		int GetCandidateSelectedIndex() const override { return m_Candidates.m_SelectedIndex; }
		int GetCandidatePageStart() const override { return m_Candidates.m_PageStart; }
		int GetCandidatePageSize() const override { return m_Candidates.m_PageSize; }
		int GetCandidateTotalCount() const override { return m_Candidates.m_TotalCount; }
	};
}

#endif
