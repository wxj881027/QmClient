#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_CATALOG_PARSE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_CATALOG_PARSE_H

#include <engine/shared/json.h>

#include <game/client/components/qmclient/skin_name_index.h>
#include <game/client/components/qmclient/skin_source.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qm_skin_catalog
{
	enum class EFormat
	{
		DDNET_JSON,
		DDSTATS_HTML,
		DIRECTORY_HTML,
	};

	struct SEntry
	{
		std::string m_Name;
		ESkinSourceMask m_DisplaySources = ESkinSourceMask::NONE;
		ESkinSourceMask m_RequestSources = ESkinSourceMask::NONE;
		int m_OfficialReleaseDate = 0;
		std::string m_OfficialCreator;
	};

	using TEntryMap = std::unordered_map<std::string, SEntry, SSkinNameHash, SSkinNameEqual>;

	inline void MergeEntry(TEntryMap &Entries, SEntry Entry)
	{
		const std::string Key = CSkinNameIndex::Key(Entry.m_Name.c_str());
		const auto Existing = Entries.find(Key);
		if(Existing == Entries.end())
		{
			Entries.emplace(Key, std::move(Entry));
			return;
		}

		SEntry &Merged = Existing->second;
		Merged.m_DisplaySources |= Entry.m_DisplaySources;
		Merged.m_RequestSources |= Entry.m_RequestSources;
		if(Entry.m_OfficialReleaseDate > Merged.m_OfficialReleaseDate)
			Merged.m_OfficialReleaseDate = Entry.m_OfficialReleaseDate;
		if(!Entry.m_OfficialCreator.empty())
			Merged.m_OfficialCreator = std::move(Entry.m_OfficialCreator);
		if(SkinNameHasUpperCase(Entry.m_Name.c_str()) && !SkinNameHasUpperCase(Merged.m_Name.c_str()))
			Merged.m_Name = std::move(Entry.m_Name);
	}

	inline int HtmlEntityHexValue(char Value)
	{
		if(Value >= '0' && Value <= '9')
			return Value - '0';
		if(Value >= 'a' && Value <= 'f')
			return Value - 'a' + 10;
		if(Value >= 'A' && Value <= 'F')
			return Value - 'A' + 10;
		return -1;
	}

	inline std::string DecodeHtmlAttribute(std::string_view Text)
	{
		std::string Result;
		Result.reserve(Text.size());
		for(std::size_t Index = 0; Index < Text.size();)
		{
			if(Text[Index] != '&')
			{
				Result.push_back(Text[Index++]);
				continue;
			}

			const std::size_t EntityEnd = Text.find(';', Index + 1);
			if(EntityEnd == std::string_view::npos)
			{
				Result.push_back(Text[Index++]);
				continue;
			}

			const std::string_view Entity = Text.substr(Index + 1, EntityEnd - Index - 1);
			int Codepoint = -1;
			if(Entity == "amp")
				Codepoint = '&';
			else if(Entity == "quot")
				Codepoint = '"';
			else if(Entity == "apos")
				Codepoint = '\'';
			else if(Entity == "lt")
				Codepoint = '<';
			else if(Entity == "gt")
				Codepoint = '>';
			else if(Entity.size() >= 2 && Entity[0] == '#')
			{
				const bool Hex = Entity[1] == 'x' || Entity[1] == 'X';
				std::size_t Cursor = Hex ? 2 : 1;
				int Value = 0;
				bool Valid = Cursor < Entity.size();
				for(; Valid && Cursor < Entity.size(); ++Cursor)
				{
					const int Digit = Hex ? HtmlEntityHexValue(Entity[Cursor]) : (Entity[Cursor] >= '0' && Entity[Cursor] <= '9' ? Entity[Cursor] - '0' : -1);
					if(Digit < 0 || Value > 0x10ffff / (Hex ? 16 : 10))
					{
						Valid = false;
						break;
					}
					Value = Value * (Hex ? 16 : 10) + Digit;
				}
				if(Valid && Value > 0 && Value <= 0x10ffff)
					Codepoint = Value;
			}

			if(Codepoint < 0)
			{
				Result.append(Text.substr(Index, EntityEnd - Index + 1));
			}
			else if(Codepoint <= 0x7f)
			{
				Result.push_back(static_cast<char>(Codepoint));
			}
			else if(Codepoint <= 0x7ff)
			{
				Result.push_back(static_cast<char>(0xc0 | (Codepoint >> 6)));
				Result.push_back(static_cast<char>(0x80 | (Codepoint & 0x3f)));
			}
			else if(Codepoint <= 0xffff)
			{
				Result.push_back(static_cast<char>(0xe0 | (Codepoint >> 12)));
				Result.push_back(static_cast<char>(0x80 | ((Codepoint >> 6) & 0x3f)));
				Result.push_back(static_cast<char>(0x80 | (Codepoint & 0x3f)));
			}
			else
			{
				Result.push_back(static_cast<char>(0xf0 | (Codepoint >> 18)));
				Result.push_back(static_cast<char>(0x80 | ((Codepoint >> 12) & 0x3f)));
				Result.push_back(static_cast<char>(0x80 | ((Codepoint >> 6) & 0x3f)));
				Result.push_back(static_cast<char>(0x80 | (Codepoint & 0x3f)));
			}
			Index = EntityEnd + 1;
		}
		return Result;
	}

	inline int UrlHexValue(char Value)
	{
		if(Value >= '0' && Value <= '9')
			return Value - '0';
		if(Value >= 'a' && Value <= 'f')
			return Value - 'a' + 10;
		if(Value >= 'A' && Value <= 'F')
			return Value - 'A' + 10;
		return -1;
	}

	inline std::string DecodeUrlComponent(std::string_view Text)
	{
		std::string Result;
		Result.reserve(Text.size());
		for(std::size_t Index = 0; Index < Text.size();)
		{
			if(Text[Index] != '%' || Index + 2 >= Text.size())
			{
				Result.push_back(Text[Index++]);
				continue;
			}
			const int High = UrlHexValue(Text[Index + 1]);
			const int Low = UrlHexValue(Text[Index + 2]);
			if(High < 0 || Low < 0)
			{
				Result.push_back(Text[Index++]);
				continue;
			}
			Result.push_back(static_cast<char>((High << 4) | Low));
			Index += 3;
		}
		return Result;
	}

	inline bool EndsWithNoCase(std::string_view Text, std::string_view Suffix)
	{
		if(Text.size() < Suffix.size())
			return false;
		const std::string_view Tail = Text.substr(Text.size() - Suffix.size());
		for(std::size_t Index = 0; Index < Tail.size(); ++Index)
		{
			const char Left = Tail[Index] >= 'A' && Tail[Index] <= 'Z' ? Tail[Index] - 'A' + 'a' : Tail[Index];
			const char Right = Suffix[Index] >= 'A' && Suffix[Index] <= 'Z' ? Suffix[Index] - 'A' + 'a' : Suffix[Index];
			if(Left != Right)
				return false;
		}
		return true;
	}

	inline std::string SkinNameFromUrl(std::string_view Url)
	{
		const std::size_t Query = Url.find_first_of("?#");
		if(Query != std::string_view::npos)
			Url = Url.substr(0, Query);
		const std::size_t Slash = Url.find_last_of('/');
		if(Slash != std::string_view::npos)
			Url = Url.substr(Slash + 1);
		std::string Name = DecodeUrlComponent(Url);
		if(EndsWithNoCase(Name, ".png"))
			Name.resize(Name.size() - 4);
		return Name;
	}

	inline std::vector<std::string> ParseDdStatsCatalogNames(std::string_view Html)
	{
		static constexpr std::string_view ATTRIBUTE = "skin_name=\"";
		std::vector<std::string> vNames;
		std::size_t SearchFrom = 0;
		while(SearchFrom < Html.size())
		{
			const std::size_t Attribute = Html.find(ATTRIBUTE, SearchFrom);
			if(Attribute == std::string_view::npos)
				break;
			const std::size_t ValueStart = Attribute + ATTRIBUTE.size();
			const std::size_t ValueEnd = Html.find('"', ValueStart);
			if(ValueEnd == std::string_view::npos)
				break;
			std::string Name = DecodeHtmlAttribute(Html.substr(ValueStart, ValueEnd - ValueStart));
			if(!Name.empty())
				vNames.push_back(std::move(Name));
			SearchFrom = ValueEnd + 1;
		}
		return vNames;
	}

	inline std::vector<std::string> ParseDirectoryCatalogNames(std::string_view Html)
	{
		static constexpr std::string_view ATTRIBUTE = "href=";
		std::vector<std::string> vNames;
		std::size_t SearchFrom = 0;
		while(SearchFrom < Html.size())
		{
			const std::size_t Attribute = Html.find(ATTRIBUTE, SearchFrom);
			if(Attribute == std::string_view::npos)
				break;

			std::size_t ValueStart = Attribute + ATTRIBUTE.size();
			if(ValueStart >= Html.size())
				break;
			const char Quote = Html[ValueStart];
			if(Quote != '"' && Quote != '\'')
			{
				SearchFrom = ValueStart;
				continue;
			}
			++ValueStart;
			const std::size_t ValueEnd = Html.find(Quote, ValueStart);
			if(ValueEnd == std::string_view::npos)
				break;

			const std::string Url = DecodeHtmlAttribute(Html.substr(ValueStart, ValueEnd - ValueStart));
			const std::string Name = SkinNameFromUrl(Url);
			const std::size_t Query = Url.find_first_of("?#");
			const std::string_view Path = std::string_view(Url).substr(0, Query == std::string::npos ? Url.size() : Query);
			if(!Name.empty() && EndsWithNoCase(Path, ".png"))
				vNames.push_back(Name);
			SearchFrom = ValueEnd + 1;
		}
		return vNames;
	}

	inline int ParseReleaseDateKey(const char *pDate)
	{
		if(pDate == nullptr)
			return 0;
		// 先确认长度恰好是 YYYY-MM-DD，后面的定长下标访问才有边界保证；
		// 调用方可能传入短字面量（如 "2024-1-1"），不能直接读到第 10 个字符。
		const std::string_view Date(pDate);
		if(Date.size() != 10)
			return 0;
		int Key = 0;
		for(int Index = 0; Index < 10; ++Index)
		{
			const char Character = Date[Index];
			if(Index == 4 || Index == 7)
			{
				if(Character != '-')
					return 0;
				continue;
			}
			if(Character < '0' || Character > '9')
				return 0;
			Key = Key * 10 + (Character - '0');
		}
		return Key;
	}

	inline std::vector<SEntry> ParseDdnetJson(const char *pJson, std::size_t JsonSize)
	{
		std::vector<SEntry> vEntries;
		json_value *pRoot = JsonParse(static_cast<const json_char *>(pJson), JsonSize);
		if(pRoot == nullptr)
			return vEntries;

		const json_value &Skins = (*pRoot)["skins"];
		if(Skins.type == json_array)
		{
			vEntries.reserve(json_array_length(&Skins));
			for(int Index = 0; Index < json_array_length(&Skins); ++Index)
			{
				const json_value *pValue = json_array_get(&Skins, Index);
				if(pValue == nullptr || pValue->type != json_object)
					continue;
				const char *pName = json_string_get(json_object_get(pValue, "name"));
				const char *pType = json_string_get(json_object_get(pValue, "type"));
				if(pName == nullptr || pName[0] == '\0' || pType == nullptr)
					continue;

				SEntry Entry;
				Entry.m_Name = pName;
				if(str_comp(pType, "normal") == 0)
				{
					Entry.m_DisplaySources = ESkinSourceMask::OFFICIAL;
					Entry.m_RequestSources = ESkinSourceMask::OFFICIAL;
				}
				else if(str_comp(pType, "community") == 0)
				{
					Entry.m_DisplaySources = ESkinSourceMask::COMMUNITY;
					Entry.m_RequestSources = ESkinSourceMask::COMMUNITY;
				}
				else
				{
					continue;
				}
				Entry.m_OfficialReleaseDate = ParseReleaseDateKey(json_string_get(json_object_get(pValue, "date")));
				const char *pCreator = json_string_get(json_object_get(pValue, "creator"));
				if(pCreator != nullptr)
					Entry.m_OfficialCreator = pCreator;
				vEntries.push_back(std::move(Entry));
			}
		}
		json_value_free(pRoot);
		return vEntries;
	}

	inline std::vector<SEntry> MakeCatalogEntries(std::vector<std::string> vNames, ESkinSourceMask DisplaySources, ESkinSourceMask RequestSources)
	{
		std::vector<SEntry> vEntries;
		vEntries.reserve(vNames.size());
		for(std::string &Name : vNames)
		{
			if(Name.empty())
				continue;
			SEntry Entry;
			Entry.m_Name = std::move(Name);
			Entry.m_DisplaySources = DisplaySources;
			Entry.m_RequestSources = RequestSources;
			vEntries.push_back(std::move(Entry));
		}
		return vEntries;
	}
} // namespace qm_skin_catalog

#endif
