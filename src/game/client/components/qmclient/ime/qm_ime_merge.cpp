#include "qm_ime_merge.h"

#include <algorithm>
#include <unordered_set>

std::vector<SQmImeCandidate> QmImeBuildCandidateList(
	const std::vector<SQmImeCandidate> &vRime,
	const std::vector<SQmImeLexiconEntry> &vDomain)
{
	std::vector<SQmImeCandidate> vResult;
	vResult.reserve(vRime.size() + vDomain.size());
	std::unordered_set<std::string> Added;
	Added.reserve(vRime.size() + vDomain.size());
	const auto Add = [&](const SQmImeCandidate &Candidate) {
		if(!Candidate.m_Text.empty() && Added.insert(Candidate.m_Text).second)
			vResult.push_back(Candidate);
	};
	if(!vRime.empty())
		Add(vRime.front());
	for(const auto &Domain : vDomain)
	{
		const auto Original = std::find_if(vRime.begin(), vRime.end(), [&](const auto &Candidate) { return Candidate.m_Text == Domain.m_Text; });
		Add(Original != vRime.end() ? *Original : SQmImeCandidate{Domain.m_Text, "", -1});
	}
	for(const auto &Candidate : vRime)
		Add(Candidate);
	return vResult;
}
