#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RECENT_TEE_SKINS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RECENT_TEE_SKINS_H

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct SQmRecentTeeSkin
{
	std::string m_Name;
	bool m_UseCustomColor = false;
	unsigned m_ColorBody = 0;
	unsigned m_ColorFeet = 0;

	bool operator==(const SQmRecentTeeSkin &Other) const
	{
		return m_Name == Other.m_Name && m_UseCustomColor == Other.m_UseCustomColor &&
		       (!m_UseCustomColor || (m_ColorBody == Other.m_ColorBody && m_ColorFeet == Other.m_ColorFeet));
	}
};

inline SQmRecentTeeSkin QmCurrentTeeSkin(const CConfig &Config, bool Dummy)
{
	const char *pName = Dummy ? Config.m_ClDummySkin : Config.m_ClPlayerSkin;
	return {pName[0] == '\0' ? "default" : pName,
		(Dummy ? Config.m_ClDummyUseCustomColor : Config.m_ClPlayerUseCustomColor) != 0,
		Dummy ? Config.m_ClDummyColorBody : Config.m_ClPlayerColorBody,
		Dummy ? Config.m_ClDummyColorFeet : Config.m_ClPlayerColorFeet};
}

// 已显示的条目不因首次点击而移位，避免双击的第二次点击命中另一款皮肤。
inline std::vector<SQmRecentTeeSkin> QmStableRecentTeeSkinOrder(const std::vector<SQmRecentTeeSkin> &Recent, const std::vector<SQmRecentTeeSkin> &Previous)
{
	std::vector<SQmRecentTeeSkin> Result;
	Result.reserve(Recent.size());
	for(const auto &Entry : Recent)
		if(std::find(Previous.begin(), Previous.end(), Entry) == Previous.end())
			Result.push_back(Entry);
	for(const auto &Entry : Previous)
		if(std::find(Recent.begin(), Recent.end(), Entry) != Recent.end())
			Result.push_back(Entry);
	return Result;
}

// 只接受显式应用；资源加载和自动轮换不调用此模型。
class CQmRecentTeeSkins
{
public:
	static constexpr size_t LIMIT = 20;
	static_assert(std::is_nothrow_move_constructible<SQmRecentTeeSkin>::value && std::is_nothrow_move_assignable<SQmRecentTeeSkin>::value,
		"Recent appearance moves must preserve Record exception safety");
	const std::vector<SQmRecentTeeSkin> &Entries() const { return m_vEntries; }
	uint64_t Revision() const { return m_Revision; }

	void Stage(SQmRecentTeeSkin Entry) { m_Pending = std::move(Entry); }
	void CommitPending()
	{
		if(!m_Pending.has_value())
			return;
		// 参数复制或 Record 扩容失败时，仍保留待提交的原值供后续重试。
		Record(*m_Pending);
		m_Pending.reset();
	}

	bool Record(SQmRecentTeeSkin Entry)
	{
		if(Entry.m_Name.size() >= MAX_SKIN_LENGTH || !str_valid_filename(Entry.m_Name.c_str()))
			return false;
		Entry.m_ColorBody = Entry.m_UseCustomColor ? Entry.m_ColorBody & 0xffffffu : 0;
		Entry.m_ColorFeet = Entry.m_UseCustomColor ? Entry.m_ColorFeet & 0xffffffu : 0;
		if(!m_vEntries.empty() && m_vEntries.front() == Entry)
			return false;
		m_vEntries.erase(std::remove(m_vEntries.begin(), m_vEntries.end(), Entry), m_vEntries.end());
		m_vEntries.insert(m_vEntries.begin(), std::move(Entry));
		if(m_vEntries.size() > LIMIT)
			m_vEntries.resize(LIMIT);
		++m_Revision;
		return true;
	}

	void Clear()
	{
		m_Pending.reset();
		if(m_vEntries.empty())
			return;
		m_vEntries.clear();
		++m_Revision;
	}

	// 配置按最旧到最新重放，Record 的去重与容量规则同时适用于加载。
	template<typename F>
	void ForEachSavedEntry(F &&Callback) const
	{
		for(auto It = m_vEntries.rbegin(); It != m_vEntries.rend(); ++It)
			Callback(*It);
	}

private:
	std::vector<SQmRecentTeeSkin> m_vEntries;
	std::optional<SQmRecentTeeSkin> m_Pending;
	uint64_t m_Revision = 0;
};

enum class EQmTeeSkinCollection
{
	ALL,
	FAVORITES,
	RECENT,
};

struct SQmTeeSkinCollectionRequest
{
	uint64_t m_SourceRevision;
	uint64_t m_HistoryRevision;
	EQmTeeSkinCollection m_Collection;
	std::string_view m_Filter;
	const SQmRecentTeeSkin &m_Main;
	const SQmRecentTeeSkin &m_Dummy;
};

// 设置页只提供源列表和皮肤条目转换；顺序、筛选和失败发布由同一模型维护。
template<typename TEntry>
class CQmTeeSkinCollectionCache
{
	struct SKey
	{
		uint64_t m_SourceRevision = 0;
		uint64_t m_HistoryRevision = 0;
		EQmTeeSkinCollection m_Collection = EQmTeeSkinCollection::ALL;
		std::string m_Filter;
		SQmRecentTeeSkin m_Main;
		SQmRecentTeeSkin m_Dummy;

		SKey() = default;
		explicit SKey(const SQmTeeSkinCollectionRequest &Request) :
			m_SourceRevision(Request.m_SourceRevision), m_HistoryRevision(Request.m_HistoryRevision), m_Collection(Request.m_Collection), m_Filter(Request.m_Filter), m_Main(Request.m_Main), m_Dummy(Request.m_Dummy) {}

		bool Matches(const SQmTeeSkinCollectionRequest &Request) const
		{
			return m_SourceRevision == Request.m_SourceRevision && m_HistoryRevision == Request.m_HistoryRevision &&
			       m_Collection == Request.m_Collection && std::string_view(m_Filter) == Request.m_Filter &&
			       m_Main == Request.m_Main && m_Dummy == Request.m_Dummy;
		}
	};

	struct SState
	{
		SKey m_Key;
		std::vector<SQmRecentTeeSkin> m_vRecentOrder;
		std::vector<TEntry> m_vEntries;
		uint64_t m_Revision = 0;
		bool m_Initialized = false;
	};
	static_assert(std::is_nothrow_move_assignable<SKey>::value && std::is_nothrow_move_constructible<TEntry>::value && std::is_nothrow_destructible<TEntry>::value,
		"Skin collection publication must not allocate or throw");

	SState m_State;
	bool m_Invalidated = false;
	bool m_ResetRecentOrder = false;

public:
	uint64_t Revision() const { return m_State.m_Revision; }
	void Invalidate(bool ResetRecentOrder = false) noexcept
	{
		m_Invalidated = true;
		m_ResetRecentOrder = m_ResetRecentOrder || ResetRecentOrder;
	}

	template<typename FResolveRecent>
	std::vector<TEntry> &Resolve(const SQmTeeSkinCollectionRequest &Request, const std::vector<SQmRecentTeeSkin> &History, std::vector<TEntry> &Source, FResolveRecent &&ResolveRecent)
	{
		if(m_Invalidated || !m_State.m_Initialized || !m_State.m_Key.Matches(Request))
		{
			SState Candidate;
			Candidate.m_Key = SKey(Request);
			if(Request.m_Collection == EQmTeeSkinCollection::FAVORITES)
			{
				for(const auto &Entry : Source)
					if(Entry.IsFavorite())
						Candidate.m_vEntries.push_back(Entry);
			}
			else if(Request.m_Collection == EQmTeeSkinCollection::RECENT)
			{
				Candidate.m_vRecentOrder = !m_ResetRecentOrder && m_State.m_Key.m_Collection == EQmTeeSkinCollection::RECENT ?
								   QmStableRecentTeeSkinOrder(History, m_State.m_vRecentOrder) :
								   History;
				for(const SQmRecentTeeSkin &Entry : Candidate.m_vRecentOrder)
				{
					if(!Candidate.m_Key.m_Filter.empty() && str_utf8_find_nocase(Entry.m_Name.c_str(), Candidate.m_Key.m_Filter.c_str()) == nullptr)
						continue;
					auto ListEntry = ResolveRecent(Entry);
					if(ListEntry.has_value())
						Candidate.m_vEntries.push_back(std::move(*ListEntry));
				}
			}
			Candidate.m_Revision = m_State.m_Revision + 1;
			// 复用已有容量时保留条目地址，避免更新历史后打断双击和活动控件身份。
			// 其余分支只交换已准备的缓冲；此后没有可能失败的分配或元素移动。
			if(Candidate.m_vEntries.size() <= m_State.m_vEntries.capacity())
			{
				m_State.m_vEntries.clear();
				for(auto &Entry : Candidate.m_vEntries)
					m_State.m_vEntries.emplace_back(std::move(Entry));
			}
			else
				m_State.m_vEntries.swap(Candidate.m_vEntries);
			m_State.m_vRecentOrder.swap(Candidate.m_vRecentOrder);
			m_State.m_Key = std::move(Candidate.m_Key);
			m_State.m_Revision = Candidate.m_Revision;
			m_State.m_Initialized = true;
			m_Invalidated = false;
			m_ResetRecentOrder = false;
		}
		return Request.m_Collection == EQmTeeSkinCollection::ALL ? Source : m_State.m_vEntries;
	}
};

#endif
