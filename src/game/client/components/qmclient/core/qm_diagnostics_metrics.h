/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_METRICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_METRICS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace QmDiagnostics
{
inline double Average(const std::vector<int64_t> &vSamples)
{
	if(vSamples.empty())
		return 0.0;
	double Sum = 0.0;
	for(const int64_t Sample : vSamples)
		Sum += static_cast<double>(Sample);
	return Sum / static_cast<double>(vSamples.size()) / 1000000.0;
}

inline double Percentile(const std::vector<int64_t> &vSamples, double Quantile)
{
	if(vSamples.empty())
		return 0.0;
	Quantile = std::clamp(Quantile, 0.0, 1.0);
	std::vector<int64_t> vSorted = vSamples;
	std::sort(vSorted.begin(), vSorted.end());
	const size_t Index = Quantile <= 0.0 ? 0 : std::min(vSorted.size() - 1, static_cast<size_t>(std::ceil(Quantile * vSorted.size()) - 1));
	return static_cast<double>(vSorted[Index]) / 1000000.0;
}

inline double OnePercentLow(const std::vector<int64_t> &vSamples)
{
	if(vSamples.empty())
		return 0.0;
	std::vector<int64_t> vSorted = vSamples;
	std::sort(vSorted.begin(), vSorted.end());
	const size_t Count = std::max<size_t>(1, static_cast<size_t>(std::ceil(vSorted.size() * 0.01)));
	int64_t WorstFrameTime = 0;
	for(size_t i = vSorted.size() - Count; i < vSorted.size(); ++i)
		WorstFrameTime += vSorted[i];
	const double AverageNanoseconds = static_cast<double>(WorstFrameTime) / Count;
	return AverageNanoseconds > 0.0 ? 1000000000.0 / AverageNanoseconds : 0.0;
}
}

#endif
