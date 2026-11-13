#ifndef GAME_CLIENT_QMUI_QMDEMOENCODERCHOICES_H
#define GAME_CLIENT_QMUI_QMDEMOENCODERCHOICES_H

#include <base/borrowed_allocator.h>
#include <base/str.h>

#include <engine/client/video_encoder.h>

#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// 仅渲染线程持有；名称和值同属一个条目，指针视图在全部字符串稳定后才发布。
class CQmDemoEncoderChoices
{
	using CString = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;
	struct CEntry
	{
		CString m_Name;
		CString m_Encoder;
		CEntry(const char *pName, const char *pEncoder, IAllocationResource *pResource) :
			m_Name(pName, CBorrowedAllocator<char>{pResource}),
			m_Encoder(pEncoder, CBorrowedAllocator<char>{pResource}) {}
	};
	IAllocationResource *m_pResource;
	std::vector<CEntry, CBorrowedAllocator<CEntry>> m_vEntries;
	std::vector<const char *, CBorrowedAllocator<const char *>> m_vpNames;
	bool m_Built = false;

public:
	explicit CQmDemoEncoderChoices(IAllocationResource *pResource = nullptr) :
		m_pResource(pResource), m_vEntries(pResource), m_vpNames(pResource) {}
	CQmDemoEncoderChoices(const CQmDemoEncoderChoices &) = delete;
	CQmDemoEncoderChoices &operator=(const CQmDemoEncoderChoices &) = delete;

	// 探测未就绪或候选准备失败时不缓存默认项；下帧可以从完整快照重新准备。
	bool TryBuild(std::span<const video_encoder::SProbeResult> Probes, const char *pDefaultLabel)
	{
		if(m_Built)
			return true;
		if(Probes.empty() || Probes.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
			return false;
		try
		{
			decltype(m_vEntries) vEntries{m_vEntries.get_allocator()};
			decltype(m_vpNames) vpNames{m_vpNames.get_allocator()};
			vEntries.reserve(Probes.size() + 1);
			vEntries.emplace_back(pDefaultLabel, "", m_pResource);
			for(const auto &Probe : Probes)
			{
				if(!Probe.m_Available || str_comp(Probe.m_aName, "libx264") == 0)
					continue;
				vEntries.emplace_back(Probe.m_aDescription, Probe.m_aName, m_pResource);
			}
			vpNames.reserve(vEntries.size());
			for(const auto &Entry : vEntries)
				vpNames.push_back(Entry.m_Name.c_str());
			m_vEntries.swap(vEntries);
			m_vpNames.swap(vpNames);
			m_Built = true;
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
	int Num() const { return static_cast<int>(m_vEntries.size()); }
	const char *const *Names() const { return m_vpNames.data(); }
	const char *Encoder(int Selection) const
	{
		if(Selection < 0 || static_cast<size_t>(Selection) >= m_vEntries.size())
			return nullptr;
		return m_vEntries[Selection].m_Encoder.c_str();
	}
	int FindSelection(const char *pEncoder) const
	{
		if(pEncoder)
			for(size_t i = 0; i < m_vEntries.size(); ++i)
				if(str_comp(m_vEntries[i].m_Encoder.c_str(), pEncoder) == 0)
					return static_cast<int>(i);
		return 0;
	}
};

#endif
