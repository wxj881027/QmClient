#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_SCAN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_SCAN_H

#include <base/hash.h>
#include <base/system.h>

#include <engine/serverbrowser.h>

#include <algorithm>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace qm_community_icons
{
	struct SRequest
	{
		char m_aId[CServerInfo::MAX_COMMUNITY_ID_LENGTH] = {};
		char m_aUrl[128] = {};
		SHA256_DIGEST m_Sha256 = {};
		bool m_Handled = false;

		bool Matches(const char *pId, const char *pUrl, const SHA256_DIGEST &Sha256) const
		{
			return str_comp(m_aId, pId) == 0 && str_comp(m_aUrl, pUrl) == 0 && m_Sha256 == Sha256;
		}
	};

	enum class EActive
	{
		NONE,
		SAME,
		OTHER,
	};

	inline bool AcceptsResult(bool MetadataAvailable, const CCommunity *pCommunity, const SHA256_DIGEST &Sha256)
	{
		return !MetadataAvailable || (pCommunity != nullptr && (!pCommunity->IconSha256().has_value() || pCommunity->IconSha256().value() == Sha256));
	}

	// 外部开始前先登记owner；提交返回false或抛异常须表示尚未接管。
	template<typename TQueue, typename TCreate, typename TSubmit>
	bool ObserveAndSubmit(TQueue &Queue, TCreate &&Create, TSubmit &&Submit) noexcept
	{
		try
		{
			auto pJob = Create();
			if(!pJob)
				return false;
			Queue.push_back(pJob);
			bool Accepted = false;
			try
			{
				Accepted = Submit(pJob);
			}
			catch(...)
			{
			}
			if(!Accepted)
				Queue.pop_back();
			return Accepted;
		}
		catch(...)
		{
			return false;
		}
	}

	class CScan
	{
		std::optional<SHA256_DIGEST> m_InfoSha256;
		std::pmr::vector<SRequest> m_vRequests;
		std::size_t m_Pending = 0;

	public:
		// 内存源借用到计划存储释放；只在主线程使用，回调不得重入或重建本计划。
		explicit CScan(std::pmr::memory_resource *pMemory = std::pmr::new_delete_resource()) :
			m_vRequests(pMemory) {}
		bool Matches(const SHA256_DIGEST &InfoSha256) const { return m_InfoSha256 == InfoSha256; }
		std::size_t Pending() const { return m_Pending; }
		void Reset() noexcept
		{
			m_InfoSha256.reset();
			m_vRequests.clear();
			m_Pending = 0;
		}

		bool Prepare(const SHA256_DIGEST &InfoSha256, std::span<const CCommunity> Communities) noexcept
		{
			if(Matches(InfoSha256))
				return true;
			try
			{
				std::pmr::vector<SRequest> vRequests(m_vRequests.get_allocator().resource());
				const auto Count = std::count_if(Communities.begin(), Communities.end(), [](const CCommunity &Community) { return Community.IconSha256().has_value(); });
				vRequests.reserve(static_cast<std::size_t>(Count));
				for(const auto &Community : Communities)
				{
					if(!Community.IconSha256().has_value())
						continue;
					SRequest Request;
					str_copy(Request.m_aId, Community.Id());
					str_copy(Request.m_aUrl, Community.IconUrl());
					Request.m_Sha256 = Community.IconSha256().value();
					vRequests.push_back(Request);
				}
				m_vRequests.swap(vRequests);
				m_InfoSha256 = InfoSha256;
				m_Pending = m_vRequests.size();
				return true;
			}
			catch(...)
			{
				return false;
			}
		}

		// 一份信息中每个目标只安排一次；旧版本同ID请求未结束时保留当前待办。
		template<typename THasCurrent, typename TActive, typename TStart>
		void Pump(std::size_t Active, std::size_t Limit, THasCurrent &&HasCurrent, TActive &&FindActive, TStart &&Start) noexcept
		{
			if(m_Pending == 0)
				return;
			try
			{
				for(auto &Request : m_vRequests)
				{
					if(Request.m_Handled)
						continue;
					bool Handled = HasCurrent(Request);
					if(!Handled)
					{
						const EActive Existing = FindActive(Request);
						if(Existing == EActive::OTHER)
							continue;
						Handled = Existing == EActive::SAME;
						if(!Handled)
						{
							if(Active >= Limit || !Start(Request))
								return;
							++Active;
						}
					}
					Request.m_Handled = true;
					--m_Pending;
				}
			}
			catch(...)
			{
				// 准备或外部提交失败时不消耗当前目标，下一帧可继续。
			}
		}
	};
}

#endif
