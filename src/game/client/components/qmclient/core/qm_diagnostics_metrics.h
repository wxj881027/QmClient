/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_METRICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_METRICS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace QmDiagnostics
{
class CSampleWindow final
{
	std::vector<int64_t> m_vSamples;
	size_t m_Start = 0;

public:
	void Prepare(size_t Capacity)
	{
		m_vSamples.reserve(Capacity);
	}

	void Clear()
	{
		m_vSamples.clear();
		m_Start = 0;
	}

	void Push(int64_t Sample, size_t Capacity)
	{
		if(m_vSamples.size() < Capacity)
		{
			m_vSamples.push_back(Sample);
			return;
		}
		if(Capacity == 0)
			return;
		m_vSamples[m_Start] = Sample;
		m_Start = (m_Start + 1) % Capacity;
	}

	bool empty() const { return m_vSamples.empty(); }
	size_t size() const { return m_vSamples.size(); }

	std::vector<int64_t> Ordered() const
	{
		std::vector<int64_t> vOrdered;
		vOrdered.reserve(m_vSamples.size());
		if(m_vSamples.empty())
			return vOrdered;
		for(size_t i = 0; i < m_vSamples.size(); ++i)
			vOrdered.push_back(m_vSamples[(m_Start + i) % m_vSamples.size()]);
		return vOrdered;
	}
};

inline double Average(const std::vector<int64_t> &vSamples)
{
	if(vSamples.empty())
		return 0.0;
	double Sum = 0.0;
	for(const int64_t Sample : vSamples)
		Sum += static_cast<double>(Sample);
	return Sum / static_cast<double>(vSamples.size()) / 1000000.0;
}

inline double Average(const CSampleWindow &Samples)
{
	return Average(Samples.Ordered());
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

inline double Percentile(const CSampleWindow &Samples, double Quantile)
{
	return Percentile(Samples.Ordered(), Quantile);
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

inline double OnePercentLow(const CSampleWindow &Samples)
{
	return OnePercentLow(Samples.Ordered());
}
}

#endif
