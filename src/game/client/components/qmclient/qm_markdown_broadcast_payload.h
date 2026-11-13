#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_PAYLOAD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_BROADCAST_PAYLOAD_H

#include "qm_json_fields.h"
#include "qm_markdown_broadcast.h"

#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace qm_news
{
	struct SJsonDeleter
	{
		void operator()(json_value *pRoot) const { json_value_free(pRoot); }
	};
	using TPayload = std::unique_ptr<json_value, SJsonDeleter>;

	struct SPayloadView
	{
		std::string_view m_Markdown;
		int m_Version;
	};

	// 缓存与发布响应共用正文/版本解释；未知字段不参与判断。
	inline std::optional<SPayloadView> Read(const json_value *pRoot, std::optional<int> CacheVersion = {})
	{
		if(CacheVersion)
		{
			const auto *pCacheVersion = qm_json::Field(pRoot, "cache_version");
			if(pCacheVersion->type != json_integer || pCacheVersion->u.integer != *CacheVersion)
				return std::nullopt;
		}
		const auto *pMarkdown = qm_json::Field(pRoot, "markdown");
		const auto *pVersion = qm_json::Field(pRoot, "version");
		if(pMarkdown->type != json_string || pMarkdown->u.string.length > CQmMarkdownBroadcast::MAX_BYTES || !qm_json::Text(pMarkdown) ||
			pVersion->type != json_integer || pVersion->u.integer < 0 || pVersion->u.integer > std::numeric_limits<int>::max())
			return std::nullopt;
		return SPayloadView{std::string_view(pMarkdown->u.string.ptr, pMarkdown->u.string.length), static_cast<int>(pVersion->u.integer)};
	}

	inline CQmMarkdownBroadcast::EApplyResult Apply(CQmMarkdownBroadcast &Broadcast, const json_value *pRoot, std::optional<int> CacheVersion = {})
	{
		const auto Payload = Read(pRoot, CacheVersion);
		return Payload ? Broadcast.TryApply(Payload->m_Markdown, Payload->m_Version) : CQmMarkdownBroadcast::EApplyResult::REJECTED;
	}

	// 主线程接收器持有尚未成功发布的缓存根树；实时/HTTP有效结果优先于旧缓存。
	class CReceiver
	{
		TPayload m_pCache;
		int m_CacheVersion = 0;
		using EResult = CQmMarkdownBroadcast::EApplyResult;

		void Supersede(EResult Result)
		{
			if(Result == EResult::APPLIED || Result == EResult::UNCHANGED)
				m_pCache.reset();
		}

	public:
		EResult ApplyOwned(CQmMarkdownBroadcast &Broadcast, std::string &&Markdown, int Version)
		{
			const auto Result = Broadcast.ApplyOwned(std::move(Markdown), Version);
			Supersede(Result);
			return Result;
		}

		EResult ApplyResponse(CQmMarkdownBroadcast &Broadcast, const json_value *pRoot)
		{
			const auto Result = Apply(Broadcast, pRoot);
			Supersede(Result);
			return Result;
		}

		EResult LoadCache(CQmMarkdownBroadcast &Broadcast, TPayload Root, int CacheVersion)
		{
			m_pCache = std::move(Root);
			m_CacheVersion = CacheVersion;
			return RetryCache(Broadcast);
		}

		EResult RetryCache(CQmMarkdownBroadcast &Broadcast)
		{
			if(!m_pCache)
				return EResult::REJECTED;
			const auto Result = Apply(Broadcast, m_pCache.get(), m_CacheVersion);
			if(Result != EResult::RETRY)
				m_pCache.reset();
			return Result;
		}

		bool HasPendingCache() const { return m_pCache != nullptr; }
	};
}

#endif
