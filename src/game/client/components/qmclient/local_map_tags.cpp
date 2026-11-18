#include "local_map_tags.h"

#include "map_tags.h"

#include <base/system.h>

#include <engine/shared/qm_storage_layout.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstring>

namespace QmMapTags
{
	namespace
	{
		const char *LOCAL_TAGS_FILE = qm_storage::LOCAL_MAP_TAGS_FILE;
		constexpr int MAX_LOCAL_TAGS_PER_MAP = 16;
		constexpr int64_t MAX_FILE_BYTES = 256 * 1024;

		std::string TrimLine(std::string_view Line)
		{
			while(!Line.empty() && (Line.back() == '\r' || Line.back() == '\n' || Line.back() == ' '))
				Line.remove_suffix(1);
			while(!Line.empty() && (Line.front() == ' '))
				Line.remove_prefix(1);
			return std::string(Line);
		}
	} // namespace

	std::string CLocalTags::MakeKey(const char *pCommunity, const char *pMap)
	{
		// 社区 id 可能为空（单机/局域网），此时只用地图名。
		std::string Key = pCommunity != nullptr ? pCommunity : "";
		Key.push_back('\x1f');
		Key += pMap != nullptr ? pMap : "";
		return Key;
	}

	void CLocalTags::Load(IStorage *pStorage)
	{
		if(m_Loaded)
			return;
		m_Loaded = true;
		if(pStorage == nullptr)
			return;
		IOHANDLE File = pStorage->OpenFile(LOCAL_TAGS_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return;
		const int64_t Length = io_length(File);
		std::string Content;
		if(Length > 0 && Length <= MAX_FILE_BYTES)
		{
			Content.resize(static_cast<size_t>(Length));
			if(io_read(File, Content.data(), Content.size()) != Content.size())
				Content.clear();
		}
		io_close(File);
		size_t Pos = 0;
		while(Pos < Content.size())
		{
			size_t End = Content.find('\n', Pos);
			if(End == std::string::npos)
				End = Content.size();
			const std::string Line = TrimLine(std::string_view(Content).substr(Pos, End - Pos));
			Pos = End + 1;
			if(Line.empty())
				continue;
			// community \t map \t tag（tag 里不允许 tab/换行，由 ValidTag 保证）
			const size_t FirstTab = Line.find('\t');
			if(FirstTab == std::string::npos)
				continue;
			const size_t SecondTab = Line.find('\t', FirstTab + 1);
			if(SecondTab == std::string::npos)
				continue;
			const std::string Community = Line.substr(0, FirstTab);
			const std::string Map = Line.substr(FirstTab + 1, SecondTab - FirstTab - 1);
			const std::string Tag = Line.substr(SecondTab + 1);
			if(Tag.empty() || Map.empty() || !ValidTag(Tag.c_str()))
				continue;
			const std::string Key = MakeKey(Community.c_str(), Map.c_str());
			const int Existing = (int)std::count_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) { return Entry.first == Key; });
			if(Existing >= MAX_LOCAL_TAGS_PER_MAP)
				continue;
			if(std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) { return Entry.first == Key && Entry.second == Tag; }) != m_vEntries.end())
				continue;
			m_vEntries.emplace_back(Key, Tag);
		}
	}

	bool CLocalTags::Save(IStorage *pStorage)
	{
		if(pStorage == nullptr)
			return false;
		qm_storage::EnsureFolder(pStorage, qm_storage::STATE_MAPS_DIR);
		IOHANDLE File = pStorage->OpenFile(LOCAL_TAGS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		bool Success = true;
		for(const auto &[Key, Tag] : m_vEntries)
		{
			const size_t Separator = Key.find('\x1f');
			const std::string Community = Separator == std::string::npos ? std::string() : Key.substr(0, Separator);
			const std::string Map = Separator == std::string::npos ? Key : Key.substr(Separator + 1);
			const std::string Line = Community + "\t" + Map + "\t" + Tag + "\n";
			Success = io_write(File, Line.data(), Line.size()) == Line.size() && Success;
		}
		Success = io_sync(File) == 0 && Success;
		Success = io_close(File) == 0 && Success;
		return Success;
	}

	bool CLocalTags::Has(const char *pCommunity, const char *pMap, const char *pTag) const
	{
		if(pTag == nullptr || pTag[0] == '\0')
			return false;
		const std::string Key = MakeKey(pCommunity, pMap);
		return std::any_of(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) {
			return Entry.first == Key && Entry.second == pTag;
		});
	}

	std::vector<std::string> CLocalTags::Get(const char *pCommunity, const char *pMap) const
	{
		const std::string Key = MakeKey(pCommunity, pMap);
		std::vector<std::string> Tags;
		for(const auto &[EntryKey, Tag] : m_vEntries)
		{
			if(EntryKey == Key)
				Tags.push_back(Tag);
		}
		return Tags;
	}

	bool CLocalTags::Add(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag)
	{
		if(pTag == nullptr || !ValidTag(pTag))
			return false;
		if(pMap == nullptr || pMap[0] == '\0')
			return false;
		if(Has(pCommunity, pMap, pTag))
			return false;
		const std::string Key = MakeKey(pCommunity, pMap);
		const int Count = (int)std::count_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) { return Entry.first == Key; });
		if(Count >= MAX_LOCAL_TAGS_PER_MAP)
			return false;
		m_vEntries.emplace_back(Key, pTag);
		Save(pStorage);
		return true;
	}

	bool CLocalTags::Remove(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag)
	{
		if(pTag == nullptr)
			return false;
		const std::string Key = MakeKey(pCommunity, pMap);
		const auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) {
			return Entry.first == Key && Entry.second == pTag;
		});
		if(It == m_vEntries.end())
			return false;
		m_vEntries.erase(It);
		Save(pStorage);
		return true;
	}
	bool CLocalTags::Move(IStorage *pStorage, const char *pCommunity, const char *pMap, int FromIndex, int ToIndex)
	{
		const std::string Key = MakeKey(pCommunity, pMap);
		// m_vEntries 是跨地图混排的扁平表，这里只取「同一张图」的那几条的位置。
		std::vector<size_t> Positions;
		for(size_t Index = 0; Index < m_vEntries.size(); ++Index)
		{
			if(m_vEntries[Index].first == Key)
				Positions.push_back(Index);
		}
		if(FromIndex < 0 || FromIndex >= (int)Positions.size())
			return false;
		// ToIndex 是**最终索引**（0..Count-1）：越界说明调用点算错了槽位，直接拒绝，
		// 不要静默夹到末尾——那会让玩家拖到行外时顺序莫名其妙地变了。
		if(ToIndex < 0 || ToIndex >= (int)Positions.size())
			return false;
		if(FromIndex == ToIndex)
			return false;
		const auto Entry = m_vEntries[Positions[FromIndex]];
		m_vEntries.erase(m_vEntries.begin() + (std::ptrdiff_t)Positions[FromIndex]);
		// 移除后同一张图的条目会前移，重新取一遍位置再插入，避免用旧下标插错。
		Positions.clear();
		for(size_t Index = 0; Index < m_vEntries.size(); ++Index)
		{
			if(m_vEntries[Index].first == Key)
				Positions.push_back(Index);
		}
		const size_t InsertAt = ToIndex >= (int)Positions.size() ? (Positions.empty() ? m_vEntries.size() : Positions.back() + 1) : Positions[ToIndex];
		m_vEntries.insert(m_vEntries.begin() + (std::ptrdiff_t)InsertAt, Entry);
		Save(pStorage);
		return true;
	}

} // namespace QmMapTags
