#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RECENT_TEE_SKINS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RECENT_TEE_SKINS_H

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
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
	const std::vector<SQmRecentTeeSkin> &Entries() const { return m_vEntries; }
	uint64_t Revision() const { return m_Revision; }

	void Stage(SQmRecentTeeSkin Entry) { m_Pending = std::move(Entry); }
	void CommitPending()
	{
		if(!m_Pending.has_value())
			return;
		SQmRecentTeeSkin Entry = std::move(*m_Pending);
		m_Pending.reset();
		Record(std::move(Entry));
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

#endif
