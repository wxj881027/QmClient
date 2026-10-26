#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_STORE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_STORE_H

#include "community_icon_limits.h"

#include <base/borrowed_allocator.h>
#include <base/hash.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/serverbrowser.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace qm_community_icons
{
	class CStore;
}

class CCommunityIcon
{
	friend class qm_community_icons::CStore;

	char m_aCommunityId[CServerInfo::MAX_COMMUNITY_ID_LENGTH] = {};
	SHA256_DIGEST m_Sha256 = {};
	IGraphics::CTextureHandle m_OrgTexture;
	IGraphics::CTextureHandle m_GreyTexture;

public:
	IGraphics::CTextureHandle Texture(bool Active) const { return Active ? m_OrgTexture : m_GreyTexture; }
};

namespace qm_community_icons
{
	enum class EPublishResult
	{
		PUBLISHED,
		DEFERRED,
		FAILED,
		BUDGET_EXCEEDED,
	};

	// 主线程持有双纹理。释放失败的句柄保留到后续Update，析构不调用设备。
	class CStore
	{
		struct SEntry
		{
			CCommunityIcon m_Icon;
			bool m_Published = true;
			std::array<std::size_t, 2> m_aPixelBytes{};
		};
		static_assert(std::is_nothrow_copy_constructible_v<SEntry> && std::is_nothrow_move_assignable_v<SEntry>);
		std::vector<SEntry, CBorrowedAllocator<SEntry>> m_vEntries;
		// 上传期间持有候选对，替换后持有旧对，均不需要再次分配。
		std::array<IGraphics::CTextureHandle, 2> m_aRetired;
		std::array<std::size_t, 2> m_aRetiredPixelBytes{};
		std::size_t m_PixelByteLimit;
		// 成功交回后端删除后扣减，非驱动即时用量。
		std::size_t m_HeldTexturePixelBytes = 0;
		bool m_CleanupPending = false;

		static std::optional<std::size_t> TexturePixelBytes(const CImageInfo &Image) noexcept
		{
			std::size_t BaseBytes = 0;
			if(!Image.m_pData || Image.m_Format != CImageInfo::FORMAT_RGBA || !Image.m_Width || !Image.m_Height || !Image.DataSize(BaseBytes))
				return std::nullopt;
			std::size_t Width = Image.m_Width, Height = Image.m_Height, Total = 0;
			for(;;)
			{
				// 基层DataSize已经校验，后续每层只缩小；累计仍需防溢出。
				const auto LevelBytes = Width * Height * 4;
				if(LevelBytes > std::numeric_limits<std::size_t>::max() - Total)
					return std::nullopt;
				Total += LevelBytes;
				if(Width == 1 && Height == 1)
					return Total;
				Width = std::max<std::size_t>(1, Width / 2);
				Height = std::max<std::size_t>(1, Height / 2);
			}
		}

		bool Release(IGraphics *pGraphics, IGraphics::CTextureHandle &Texture, std::size_t &PixelBytes) noexcept
		{
			if(!Texture.IsValid())
			{
				m_HeldTexturePixelBytes -= PixelBytes;
				PixelBytes = 0;
				return true;
			}
			if(Texture.IsNullTexture())
			{
				Texture.Invalidate();
				m_HeldTexturePixelBytes -= PixelBytes;
				PixelBytes = 0;
				return true;
			}
			if(!pGraphics)
				return false;
			try
			{
				pGraphics->UnloadTexture(&Texture);
				Texture.Invalidate();
				m_HeldTexturePixelBytes -= PixelBytes;
				PixelBytes = 0;
				return true;
			}
			catch(...)
			{
				return false;
			}
		}

	public:
		explicit CStore(IAllocationResource *pMemory = nullptr, std::size_t PixelByteLimit = MAX_STORE_TEXTURE_PIXEL_BYTES) :
			m_vEntries(pMemory), m_PixelByteLimit(PixelByteLimit)
		{
		}
		CStore(const CStore &) = delete;
		CStore &operator=(const CStore &) = delete;
		std::size_t HeldTexturePixelBytes() const { return m_HeldTexturePixelBytes; }

		const CCommunityIcon *Find(const char *pId) const
		{
			const auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) {
				return Entry.m_Published && str_comp(Entry.m_Icon.m_aCommunityId, pId) == 0;
			});
			return It == m_vEntries.end() ? nullptr : &It->m_Icon;
		}

		bool IsCurrent(const char *pId, const SHA256_DIGEST &Sha256) const
		{
			const auto *pIcon = Find(pId);
			return pIcon && pIcon->m_Sha256 == Sha256;
		}

		bool Drain(IGraphics *pGraphics) noexcept
		{
			if(!m_CleanupPending)
				return true;
			bool Clean = true;
			for(std::size_t Index = 0; Index < m_aRetired.size(); ++Index)
				Clean = Release(pGraphics, m_aRetired[Index], m_aRetiredPixelBytes[Index]) && Clean;
			for(auto It = m_vEntries.begin(); It != m_vEntries.end();)
			{
				if(It->m_Published)
				{
					++It;
					continue;
				}
				const bool OrgReleased = Release(pGraphics, It->m_Icon.m_OrgTexture, It->m_aPixelBytes[0]);
				const bool GreyReleased = Release(pGraphics, It->m_Icon.m_GreyTexture, It->m_aPixelBytes[1]);
				if(OrgReleased && GreyReleased)
					It = m_vEntries.erase(It);
				else
				{
					Clean = false;
					++It;
				}
			}
			m_CleanupPending = !Clean;
			return Clean;
		}

		EPublishResult Publish(IGraphics *pGraphics, const char *pId, const SHA256_DIGEST &Sha256, CImageInfo &Info, CImageInfo &Grayscale)
		{
			if(!pGraphics || !Drain(pGraphics))
				return EPublishResult::DEFERRED;
			const auto OrgBytes = TexturePixelBytes(Info), GreyBytes = TexturePixelBytes(Grayscale);
			if(!OrgBytes || !GreyBytes)
				return EPublishResult::FAILED;
			const auto Available = m_PixelByteLimit - m_HeldTexturePixelBytes;
			if(*OrgBytes > Available || *GreyBytes > Available - *OrgBytes)
				return EPublishResult::BUDGET_EXCEEDED;
			const auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) {
				return str_comp(Entry.m_Icon.m_aCommunityId, pId) == 0;
			});
			const size_t Index = It == m_vEntries.end() ? m_vEntries.size() : static_cast<size_t>(It - m_vEntries.begin());
			if(Index == m_vEntries.size() && m_vEntries.size() == m_vEntries.capacity())
			{
				try
				{
					const auto Capacity = m_vEntries.capacity();
					if(Capacity == m_vEntries.max_size())
						return EPublishResult::DEFERRED;
					m_vEntries.reserve(Capacity == 0 ? 1 : Capacity + std::min(Capacity, m_vEntries.max_size() - Capacity));
				}
				catch(...)
				{
					return EPublishResult::DEFERRED;
				}
			}
			SEntry Candidate;
			m_CleanupPending = true;
			try
			{
				m_aRetired[0] = pGraphics->LoadTextureRawMove(Info, 0, pId);
				if(m_aRetired[0].IsValid() && !m_aRetired[0].IsNullTexture())
				{
					m_aRetiredPixelBytes[0] = *OrgBytes;
					m_HeldTexturePixelBytes += *OrgBytes;
				}
				if(m_aRetired[0].IsValid())
				{
					m_aRetired[1] = pGraphics->LoadTextureRawMove(Grayscale, 0, pId);
					if(m_aRetired[1].IsValid() && !m_aRetired[1].IsNullTexture())
					{
						m_aRetiredPixelBytes[1] = *GreyBytes;
						m_HeldTexturePixelBytes += *GreyBytes;
					}
				}
			}
			catch(...)
			{
				Drain(pGraphics);
				return EPublishResult::FAILED;
			}
			if(!m_aRetired[0].IsValid() || !m_aRetired[1].IsValid())
			{
				Drain(pGraphics);
				return EPublishResult::FAILED;
			}
			if(Index == m_vEntries.size())
				m_vEntries.push_back(Candidate); // 容量已准备，固定大小成员的复制不抛异常。
			auto &Icon = m_vEntries[Index].m_Icon;
			std::swap(Icon.m_OrgTexture, m_aRetired[0]);
			std::swap(Icon.m_GreyTexture, m_aRetired[1]);
			m_vEntries[Index].m_aPixelBytes.swap(m_aRetiredPixelBytes);
			str_copy(Icon.m_aCommunityId, pId);
			Icon.m_Sha256 = Sha256;
			Drain(pGraphics);
			return EPublishResult::PUBLISHED;
		}

		bool Prune(std::span<const CCommunity> Communities, IGraphics *pGraphics) noexcept
		{
			for(auto &Entry : m_vEntries)
			{
				if(Entry.m_Published && std::none_of(Communities.begin(), Communities.end(), [&](const auto &Community) {
					   return str_comp(Entry.m_Icon.m_aCommunityId, Community.Id()) == 0;
				   }))
				{
					Entry.m_Published = false;
					m_CleanupPending = true;
				}
			}
			return Drain(pGraphics);
		}

		bool Clear(IGraphics *pGraphics) noexcept
		{
			for(auto &Entry : m_vEntries)
				Entry.m_Published = false;
			m_CleanupPending = true;
			return Drain(pGraphics);
		}
	};
}

#endif
