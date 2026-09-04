/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_RETENTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_RETENTION_H

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace QmDiagnostics
{
struct SDiagnosticFileEntry
{
	std::string m_Name;
	time_t m_TimeModified = 0;
};

inline void SortDiagnosticFileEntries(std::vector<SDiagnosticFileEntry> &vEntries)
{
	std::sort(vEntries.begin(), vEntries.end(), [](const SDiagnosticFileEntry &Lhs, const SDiagnosticFileEntry &Rhs) {
		if(Lhs.m_TimeModified != Rhs.m_TimeModified)
			return Lhs.m_TimeModified > Rhs.m_TimeModified;
		return Lhs.m_Name > Rhs.m_Name;
	});
}

inline size_t FirstDiagnosticFileToRemove(size_t EntryCount, size_t MaxFiles)
{
	return std::min(EntryCount, MaxFiles);
}
}

#endif
