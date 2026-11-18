#include "local_map_tags.h"

#include "map_tags.h"

#include <base/io.h>
#include <base/log.h>
#include <base/str.h>

#include <engine/storage.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <string_view>
#include <unordered_map>

namespace QmMapTags
{
	namespace
	{
		constexpr char LOCAL_TAGS_FILE[] = "qm_local_map_tags.txt";
		constexpr int MAX_LOCAL_TAGS_PER_MAP = 16;
		constexpr unsigned MAX_FILE_BYTES = 256 * 1024;
		constexpr char KEY_SEPARATOR = '\x1f';

		std::string_view AsView(const char *pText)
		{
			return pText != nullptr ? pText : "";
		}

		bool ValidKeyFields(std::string_view Community, std::string_view Map)
		{
			return !Map.empty() && Community.find_first_of("\t\r\n\x1f") == std::string_view::npos &&
			       Map.find_first_of("\t\r\n") == std::string_view::npos;
		}

		std::string MakeKey(std::string_view Community, std::string_view Map)
		{
			std::string Key(Community);
			Key.push_back(KEY_SEPARATOR);
			Key.append(Map);
			return Key;
		}

		bool MatchesKey(std::string_view Key, std::string_view Community, std::string_view Map)
		{
			const size_t Separator = Community.size();
			return Key.size() > Separator && Key.size() - Separator - 1 == Map.size() &&
			       Key[Separator] == KEY_SEPARATOR && Key.starts_with(Community) && Key.substr(Separator + 1) == Map;
		}
	} // namespace

	void CLocalTags::Load(IStorage *pStorage)
	{
		if(pStorage != nullptr && !m_LoadAttempted)
			LoadFromStorage(*pStorage);
	}

	void CLocalTags::LoadFromStorage(IStorage &Storage)
	{
		m_LoadAttempted = true;
		try
		{
			void *pRaw = nullptr;
			unsigned Size = 0;
			using EReadResult = IStorage::EReadFileResult;
			const auto Result = Storage.ReadFileCheckedBounded(LOCAL_TAGS_FILE, IStorage::TYPE_SAVE, &pRaw, &Size, MAX_FILE_BYTES);
			const std::unique_ptr<void, decltype(&std::free)> pContent(pRaw, std::free);
			if(Result == EReadResult::NOT_FOUND)
			{
				m_Loaded = true;
				return;
			}
			if(Result == EReadResult::SUCCESS && pContent)
			{
				TTagEntries vEntries;
				// 索引只借用读取缓冲，避免每行重新扫描已加载的整份记录。
				std::unordered_map<std::string_view, std::vector<std::string_view>> TagsByMap;
				std::string_view Remaining(static_cast<const char *>(pContent.get()), Size);
				while(!Remaining.empty())
				{
					const size_t End = Remaining.find('\n');
					std::string_view Line = Remaining.substr(0, End);
					Remaining.remove_prefix(End == std::string_view::npos ? Remaining.size() : End + 1);
					if(!Line.empty() && Line.back() == '\r')
						Line.remove_suffix(1);
					const size_t FirstTab = Line.find('\t');
					if(FirstTab == std::string_view::npos)
						continue;
					const size_t SecondTab = Line.find('\t', FirstTab + 1);
					if(SecondTab == std::string_view::npos || Line.find('\0') != std::string_view::npos)
						continue;

					const std::string_view Community = Line.substr(0, FirstTab);
					const std::string_view Map = Line.substr(FirstTab + 1, SecondTab - FirstTab - 1);
					const std::string_view Tag = Line.substr(SecondTab + 1);
					if(!ValidKeyFields(Community, Map) || Tag.size() > MAX_CHARACTERS * 4)
						continue;
					std::string Text(Tag);
					if(!ValidTag(Text.c_str()))
						continue;

					auto &vTags = TagsByMap[Line.substr(0, SecondTab)];
					if(vTags.size() >= MAX_LOCAL_TAGS_PER_MAP || std::find(vTags.begin(), vTags.end(), Tag) != vTags.end())
						continue;
					vTags.push_back(Tag);
					vEntries.emplace_back(MakeKey(Community, Map), std::move(Text));
				}
				m_vEntries.swap(vEntries);
				m_Loaded = true;
				return;
			}
		}
		catch(...)
		{
			// 候选还在局部，读取或构建失败后允许下一次编辑重试。
		}
		log_error("map-tags", "could not read local map tags from %s", LOCAL_TAGS_FILE);
	}

	bool CLocalTags::Save(IStorage &Storage, const TTagEntries &vEntries)
	{
		std::string Content;
		for(const auto &[Key, Tag] : vEntries)
		{
			// 键的分隔符换成制表符，再为标签和换行各追加一个分隔符。
			if(Key.size() + Tag.size() + 2 > MAX_FILE_BYTES - Content.size())
				return false;
			const size_t Separator = Key.find(KEY_SEPARATOR);
			Content.append(Key, 0, Separator);
			Content.push_back('\t');
			Content.append(Key, Separator + 1, std::string::npos);
			Content.push_back('\t');
			Content.append(Tag);
			Content.push_back('\n');
		}

		char aTemporary[sizeof(LOCAL_TAGS_FILE) + 32];
		IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), LOCAL_TAGS_FILE);
		char aRoot[IO_MAX_PATH_LENGTH] = "";
		Storage.GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
		if(str_length(aRoot) + str_length(aTemporary) >= IO_MAX_PATH_LENGTH)
			return false;

		std::unique_ptr<void, decltype(&io_close)> pFile(Storage.OpenFile(aTemporary, IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE), &io_close);
		if(!pFile)
			return false;
		const auto Size = static_cast<unsigned>(Content.size());
		const bool Written = io_write(pFile.get(), Content.data(), Size) == Size;
		const bool Synced = Written && io_sync(pFile.get()) == 0;
		const bool Clean = io_error(pFile.get()) == 0;
		const bool Closed = io_close(pFile.release()) == 0;
		try
		{
			if(Synced && Clean && Closed && Storage.RenameFile(aTemporary, LOCAL_TAGS_FILE, IStorage::TYPE_SAVE, false))
				return true;
		}
		catch(...)
		{
			// 发布失败时只清理本次创建的候选文件。
		}
		Storage.RemoveFile(aTemporary, IStorage::TYPE_SAVE);
		return false;
	}

	template<typename TEdit>
	bool CLocalTags::Edit(IStorage *pStorage, TEdit &&Apply)
	{
		try
		{
			if(pStorage != nullptr && !m_Loaded)
			{
				LoadFromStorage(*pStorage);
				if(!m_Loaded)
					return false;
			}
			TTagEntries vEntries = m_vEntries;
			if(!Apply(vEntries))
				return false;
			if(pStorage == nullptr || Save(*pStorage, vEntries))
			{
				m_vEntries.swap(vEntries);
				return true;
			}
		}
		catch(...)
		{
			// 写入确认之前保留原记录和顺序。
		}
		log_error("map-tags", "could not save local map tags to %s", LOCAL_TAGS_FILE);
		return false;
	}

	bool CLocalTags::Has(const char *pCommunity, const char *pMap, const char *pTag) const
	{
		if(pTag == nullptr || pTag[0] == '\0')
			return false;
		const auto Community = AsView(pCommunity);
		const auto Map = AsView(pMap);
		if(!ValidKeyFields(Community, Map))
			return false;
		return std::any_of(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Entry) {
			return MatchesKey(Entry.first, Community, Map) && Entry.second == pTag;
		});
	}

	std::vector<std::string> CLocalTags::Get(const char *pCommunity, const char *pMap) const
	{
		try
		{
			const auto Community = AsView(pCommunity);
			const auto Map = AsView(pMap);
			if(!ValidKeyFields(Community, Map))
				return {};
			std::vector<std::string> vTags;
			for(const auto &[Key, Tag] : m_vEntries)
			{
				if(MatchesKey(Key, Community, Map))
					vTags.push_back(Tag);
			}
			return vTags;
		}
		catch(const std::bad_alloc &)
		{
			return {};
		}
	}

	bool CLocalTags::Add(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag)
	{
		const auto Community = AsView(pCommunity);
		const auto Map = AsView(pMap);
		if(!ValidTag(pTag) || !ValidKeyFields(Community, Map))
			return false;
		return Edit(pStorage, [&](TTagEntries &vEntries) {
			std::string Key = MakeKey(Community, Map);
			int Count = 0;
			for(const auto &Entry : vEntries)
			{
				if(Entry.first == Key && (Entry.second == pTag || ++Count >= MAX_LOCAL_TAGS_PER_MAP))
					return false;
			}
			vEntries.emplace_back(std::move(Key), pTag);
			return true;
		});
	}

	bool CLocalTags::Remove(IStorage *pStorage, const char *pCommunity, const char *pMap, const char *pTag)
	{
		if(pTag == nullptr)
			return false;
		const auto Community = AsView(pCommunity);
		const auto Map = AsView(pMap);
		if(!ValidKeyFields(Community, Map))
			return false;
		return Edit(pStorage, [&](TTagEntries &vEntries) {
			const auto It = std::find_if(vEntries.begin(), vEntries.end(), [&](const auto &Entry) {
				return MatchesKey(Entry.first, Community, Map) && Entry.second == pTag;
			});
			if(It == vEntries.end())
				return false;
			vEntries.erase(It);
			return true;
		});
	}

	bool CLocalTags::Move(IStorage *pStorage, const char *pCommunity, const char *pMap, int FromIndex, int ToIndex)
	{
		if(FromIndex < 0 || ToIndex < 0 || FromIndex == ToIndex)
			return false;
		const auto Community = AsView(pCommunity);
		const auto Map = AsView(pMap);
		if(!ValidKeyFields(Community, Map))
			return false;
		return Edit(pStorage, [&](TTagEntries &vEntries) {
			// 只登记指定地图的标签位置，ToIndex 是移完后的该图索引。
			std::array<size_t, MAX_LOCAL_TAGS_PER_MAP> aPositions;
			int Count = 0;
			for(size_t Index = 0; Index < vEntries.size(); ++Index)
			{
				if(MatchesKey(vEntries[Index].first, Community, Map))
					aPositions[Count++] = Index;
			}
			if(FromIndex >= Count || ToIndex >= Count)
				return false;

			const auto Begin = vEntries.begin();
			const auto From = static_cast<std::ptrdiff_t>(aPositions[FromIndex]);
			if(ToIndex < FromIndex)
				std::rotate(Begin + static_cast<std::ptrdiff_t>(aPositions[ToIndex]), Begin + From, Begin + From + 1);
			else
			{
				const size_t End = ToIndex + 1 == Count ? aPositions[ToIndex] + 1 : aPositions[ToIndex + 1];
				std::rotate(Begin + From, Begin + From + 1, Begin + static_cast<std::ptrdiff_t>(End));
			}
			return true;
		});
	}

} // namespace QmMapTags
