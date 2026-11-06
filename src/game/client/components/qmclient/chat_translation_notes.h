#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_TRANSLATION_NOTES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_TRANSLATION_NOTES_H

#include <base/borrowed_allocator.h>
#include <base/str.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// 服务器回显只携带译文；原文只在本客户端以有界、短期附注等待匹配。
class COutgoingChatAnnotations
{
	using TText = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;
	struct SEntry
	{
		int m_Connection;
		int m_ClientId;
		int m_Team;
		TText m_Text;
		std::string m_Original;
		int64_t m_Time;
		int64_t m_Lifetime;
		bool m_Ambiguous = false;

		SEntry(int Connection, int ClientId, int Team, std::string_view Text, const char *pOriginal, int64_t Now, int64_t Lifetime, bool Ambiguous, IAllocationResource *pResource) :
			m_Connection(Connection), m_ClientId(ClientId), m_Team(Team), m_Text(Text.data(), Text.size(), CBorrowedAllocator<char>(pResource)), m_Original(pOriginal != nullptr ? pOriginal : ""), m_Time(Now), m_Lifetime(Lifetime), m_Ambiguous(Ambiguous) {}
		SEntry(SEntry &&) noexcept = default;
	};
	struct SGap
	{
		int64_t m_Time = 0;
		int64_t m_Lifetime = 0;
	};

	static constexpr size_t MAX_ENTRIES = 32;
	// 槽位本身不分配；只在完整候选准备成功后占用槽位或驱逐旧条目。
	std::array<std::optional<SEntry>, MAX_ENTRIES> m_aEntries;
	size_t m_Size = 0;
	std::array<SGap, 2> m_aGaps{};
	IAllocationResource *m_pResource;
	int64_t m_LastTime = -1;

	static std::string_view Normalize(const char *pText) noexcept
	{
		if(pText == nullptr)
			return {};
		const char *pCursor = pText;
		const char *pEnd = pText;
		while(*pCursor)
		{
			if(!str_utf8_isspace(str_utf8_decode(&pCursor)))
				pEnd = pCursor;
		}
		return {pText, static_cast<size_t>(pEnd - pText)};
	}

	static bool Expired(int64_t Time, int64_t Lifetime, int64_t Now) noexcept
	{
		return Now >= Time && static_cast<uint64_t>(Now) - static_cast<uint64_t>(Time) >= static_cast<uint64_t>(Lifetime);
	}

	void Erase(size_t Index) noexcept
	{
		for(; Index + 1 < m_Size; ++Index)
		{
			m_aEntries[Index].reset();
			m_aEntries[Index].emplace(std::move(*m_aEntries[Index + 1]));
		}
		m_aEntries[--m_Size].reset();
	}

	void Prune(int64_t Now) noexcept
	{
		if(Now < m_LastTime)
			Clear();
		m_LastTime = Now;
		for(size_t Index = 0; Index < m_Size;)
		{
			const auto &Entry = *m_aEntries[Index];
			if(Expired(Entry.m_Time, Entry.m_Lifetime, Now))
				Erase(Index);
			else
				++Index;
		}
		for(auto &Gap : m_aGaps)
			if(Gap.m_Lifetime > 0 && Expired(Gap.m_Time, Gap.m_Lifetime, Now))
				Gap = {};
	}

	size_t Find(int Connection, int ClientId, int Team, std::string_view Text) const noexcept
	{
		for(size_t Index = 0; Index < m_Size; ++Index)
		{
			const auto &Entry = *m_aEntries[Index];
			if(Entry.m_Connection == Connection && Entry.m_ClientId == ClientId && Entry.m_Team == Team &&
				std::string_view(Entry.m_Text.data(), Entry.m_Text.size()) == Text)
				return Index;
		}
		return m_Size;
	}

	void RegisterGap(int Connection, int64_t Now, int64_t Lifetime) noexcept
	{
		auto &Gap = m_aGaps[Connection];
		if(Gap.m_Lifetime > 0)
		{
			const uint64_t Remaining = static_cast<uint64_t>(Gap.m_Lifetime) - (static_cast<uint64_t>(Now) - static_cast<uint64_t>(Gap.m_Time));
			if(static_cast<uint64_t>(Lifetime) < Remaining)
				return;
		}
		Gap = {Now, Lifetime};
	}

public:
	static constexpr size_t CAPACITY = MAX_ENTRIES;
	// 内存域只借用，须存活到缓存及其保留的字符串销毁。
	explicit COutgoingChatAnnotations(IAllocationResource *pResource = nullptr) noexcept :
		m_pResource(pResource) {}

	void Clear() noexcept
	{
		for(size_t Index = 0; Index < m_Size; ++Index)
			m_aEntries[Index].reset();
		m_Size = 0;
		m_aGaps = {};
		m_LastTime = -1;
	}

	size_t Size() const { return m_Size; }
	void InvalidateConnection(int Connection) noexcept
	{
		for(size_t Index = 0; Index < m_Size;)
		{
			if(m_aEntries[Index]->m_Connection == Connection)
				Erase(Index);
			else
				++Index;
		}
		if(Connection >= 0 && Connection < static_cast<int>(m_aGaps.size()))
			m_aGaps[Connection] = {};
	}
	bool Record(int Connection, int ClientId, int Team, const char *pText, const char *pOriginal, int64_t Now, int64_t Lifetime) noexcept
	{
		Prune(Now);
		if(Connection < 0 || Connection > 1 || ClientId < 0 || Now < 0 || Lifetime <= 0 || pText == nullptr)
			return false;
		const auto Text = Normalize(pText);
		if(Text.empty())
			return false;
		const size_t Existing = Find(Connection, ClientId, Team, Text);
		if(Existing != m_Size)
		{
			// 网络发送成功不等于服务器接纳。同文未回显时无法判定归属，保留歧义墓碑直到过期。
			auto &Entry = *m_aEntries[Existing];
			Entry.m_Ambiguous = true;
			Entry.m_Original.clear();
			Entry.m_Time = Now;
			Entry.m_Lifetime = Lifetime;
			return true;
		}
		if(str_length(pText) > 4096 || (pOriginal != nullptr && str_length(pOriginal) > 4096))
		{
			RegisterGap(Connection, Now, Lifetime);
			return false;
		}
		std::optional<SEntry> Candidate;
		try
		{
			// 未登记消息可能稍后回显：窗口内的新条目只能作为歧义墓碑。
			const bool Ambiguous = m_aGaps[Connection].m_Lifetime > 0;
			Candidate.emplace(Connection, ClientId, Team, Text, Ambiguous ? nullptr : pOriginal, Now, Lifetime, Ambiguous, m_pResource);
		}
		catch(...)
		{
			RegisterGap(Connection, Now, Lifetime);
			return false;
		}
		if(m_Size == CAPACITY)
		{
			// 被容量淘汰的未回显消息也会留下缺口；不能让后来的同文领取原文。
			const auto &Oldest = *m_aEntries[0];
			const int64_t Remaining = Oldest.m_Lifetime - static_cast<int64_t>(static_cast<uint64_t>(Now) - static_cast<uint64_t>(Oldest.m_Time));
			RegisterGap(Oldest.m_Connection, Now, Remaining);
			Erase(0);
		}
		m_aEntries[m_Size].emplace(std::move(*Candidate));
		++m_Size;
		return true;
	}
	std::string Consume(int Connection, int ClientId, int Team, const char *pText, int64_t Now) noexcept
	{
		Prune(Now);
		const size_t Index = Find(Connection, ClientId, Team, Normalize(pText));
		if(Index == m_Size || m_aEntries[Index]->m_Ambiguous)
			return {};
		std::string Original = std::move(m_aEntries[Index]->m_Original);
		Erase(Index);
		return Original;
	}
};

#endif
