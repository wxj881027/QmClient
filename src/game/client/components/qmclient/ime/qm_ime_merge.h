#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_MERGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_MERGE_H

#include <engine/shared/qm_ime_provider.h>

struct SQmImeCandidate
{
	std::string m_Text;
	std::string m_Comment;
	int m_RimeIndex = -1;
};

// 原生首项在前，领域/学习词从第二项开始；重复词沿用原生全局选择下标。
std::vector<SQmImeCandidate> QmImeBuildCandidateList(
	const std::vector<SQmImeCandidate> &vRime,
	const std::vector<SQmImeLexiconEntry> &vDomain);

#endif
