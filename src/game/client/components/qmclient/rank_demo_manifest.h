#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_DEMO_MANIFEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_DEMO_MANIFEST_H

#include <base/str.h>

#include <engine/shared/json.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace qmclient::rank_demo
{
	struct SEntry
	{
		std::string m_Map;
		int m_Rank = 0;
		std::string m_Time;
		std::string m_Demo;
		std::string m_Names;
		int m_Cid = 0;
		int64_t m_Ts = std::numeric_limits<int64_t>::min();
		// watchable.jsonl 的补充字段：模式（solo/team）、队伍号、完成者、标识
		std::string m_Kind;
		int m_Team = 0;
		std::vector<int> m_vFinishers;
		std::string m_Uuid;
		std::string m_Rev;
	};

	// 条目是否为 team 模式记录
	inline bool IsTeamEntry(const SEntry &Entry)
	{
		if(Entry.m_Kind.empty())
			return Entry.m_vFinishers.size() > 1;
		return Entry.m_Kind == "team";
	}

	inline const json_value *Field(const json_value *pObject, const char *pName)
	{
		if(pObject == nullptr || pObject->type != json_object)
			return nullptr;
		return json_object_get(pObject, pName);
	}

	inline bool ReadString(const json_value *pObject, const char *pName, std::string &Out, size_t MaxLength)
	{
		const json_value *pValue = Field(pObject, pName);
		if(pValue == nullptr || pValue->type != json_string)
			return false;
		const char *pString = json_string_get(pValue);
		if(pString == nullptr || pString[0] == '\0' || str_length(pString) > MaxLength)
			return false;
		Out = pString;
		return true;
	}

	inline bool ReadInt(const json_value *pObject, const char *pName, int64_t &Out)
	{
		const json_value *pValue = Field(pObject, pName);
		if(pValue == nullptr || pValue->type != json_integer)
			return false;
		Out = pValue->u.integer;
		return true;
	}

	inline bool ReadTime(const json_value *pObject, const char *pName, std::string &Out)
	{
		const json_value *pValue = Field(pObject, pName);
		if(pValue == nullptr)
			return false;
		if(pValue->type == json_string)
		{
			const char *pString = json_string_get(pValue);
			if(pString == nullptr || pString[0] == '\0' || str_length(pString) > 32)
				return false;
			Out = pString;
			return true;
		}
		if(pValue->type == json_double || pValue->type == json_integer)
		{
			char aBuffer[32];
			if(pValue->type == json_double)
				str_format(aBuffer, sizeof(aBuffer), "%.2f", pValue->u.dbl);
			else
				str_format(aBuffer, sizeof(aBuffer), "%lld", (long long)pValue->u.integer);
			Out = aBuffer;
			return true;
		}
		return false;
	}

	inline bool IsSafeDemoName(const std::string &Demo)
	{
		if(Demo.empty() || Demo.size() > 256)
			return false;
		for(const char Character : Demo)
		{
			if(static_cast<unsigned char>(Character) < 0x20 || Character == '/' || Character == '\\' || Character == '?' || Character == '#')
				return false;
		}
		return true;
	}

	inline bool ParseEntry(const json_value *pRoot, SEntry &Entry)
	{
		std::string Status;
		if(!ReadString(pRoot, "status", Status, 16) || Status != "ok")
			return false;
		if(!ReadString(pRoot, "map", Entry.m_Map, 128) || !ReadString(pRoot, "demo", Entry.m_Demo, 256) || !IsSafeDemoName(Entry.m_Demo))
			return false;

		int64_t Value = 0;
		if(!ReadInt(pRoot, "rank", Value) || Value <= 0 || Value > 9999)
			return false;
		Entry.m_Rank = static_cast<int>(Value);
		if(ReadTime(pRoot, "time", Entry.m_Time) == false)
			Entry.m_Time.clear();
		if(ReadInt(pRoot, "cid", Value) && Value >= 0 && Value <= 63)
			Entry.m_Cid = static_cast<int>(Value);
		if(ReadInt(pRoot, "team", Value) && Value >= 0 && Value <= 255)
			Entry.m_Team = static_cast<int>(Value);
		if(ReadInt(pRoot, "ts", Value))
			Entry.m_Ts = Value;

		// 模式与标识字段都是可选的（老记录或 error 行可能缺失）
		if(!ReadString(pRoot, "kind", Entry.m_Kind, 16))
			Entry.m_Kind.clear();
		if(!ReadString(pRoot, "uuid", Entry.m_Uuid, 64))
			Entry.m_Uuid.clear();
		if(!ReadString(pRoot, "rev", Entry.m_Rev, 32))
			Entry.m_Rev.clear();

		const json_value *pFinishers = Field(pRoot, "finishers");
		if(pFinishers != nullptr && pFinishers->type == json_array)
		{
			for(unsigned i = 0; i < pFinishers->u.array.length; ++i)
			{
				const json_value *pFinisher = pFinishers->u.array.values[i];
				if(pFinisher == nullptr || pFinisher->type != json_integer || pFinisher->u.integer < 0 || pFinisher->u.integer > 63)
					continue;
				Entry.m_vFinishers.push_back(static_cast<int>(pFinisher->u.integer));
			}
		}

		const json_value *pNames = Field(pRoot, "names");
		if(pNames != nullptr && pNames->type == json_array)
		{
			for(unsigned i = 0; i < pNames->u.array.length && Entry.m_Names.size() < 256; ++i)
			{
				const json_value *pName = pNames->u.array.values[i];
				if(pName == nullptr || pName->type != json_string)
					continue;
				const char *pString = json_string_get(pName);
				if(pString == nullptr || pString[0] == '\0' || str_length(pString) > 64)
					continue;
				if(!Entry.m_Names.empty())
					Entry.m_Names += ", ";
				Entry.m_Names += pString;
			}
		}
		return true;
	}

	inline bool ParseManifest(const unsigned char *pData, size_t DataSize, std::vector<SEntry> &Entries)
	{
		Entries.clear();
		if(pData == nullptr || DataSize == 0)
			return false;

		const char *pCurrent = reinterpret_cast<const char *>(pData);
		const char *pEnd = pCurrent + DataSize;
		if(pEnd - pCurrent >= 3 && (unsigned char)pCurrent[0] == 0xef && (unsigned char)pCurrent[1] == 0xbb && (unsigned char)pCurrent[2] == 0xbf)
			pCurrent += 3;
		while(pCurrent < pEnd)
		{
			const char *pLineEnd = static_cast<const char *>(memchr(pCurrent, '\n', static_cast<size_t>(pEnd - pCurrent)));
			if(pLineEnd == nullptr)
				pLineEnd = pEnd;
			const char *pLineBegin = pCurrent;
			while(pLineBegin < pLineEnd && (pLineBegin[0] == ' ' || pLineBegin[0] == '\t' || pLineBegin[0] == '\r'))
				++pLineBegin;
			const char *pTrimmedEnd = pLineEnd;
			while(pTrimmedEnd > pLineBegin && (pTrimmedEnd[-1] == ' ' || pTrimmedEnd[-1] == '\t' || pTrimmedEnd[-1] == '\r'))
				--pTrimmedEnd;
			const size_t LineSize = static_cast<size_t>(pTrimmedEnd - pLineBegin);
			if(LineSize > 1 && pLineBegin[0] == '{')
			{
				json_value *pRoot = JsonParse(pLineBegin, LineSize);
				if(pRoot != nullptr)
				{
					SEntry Entry;
					if(ParseEntry(pRoot, Entry))
						Entries.push_back(std::move(Entry));
					json_value_free(pRoot);
				}
			}
			if(pLineEnd == pEnd)
				break;
			pCurrent = pLineEnd + 1;
		}
		return !Entries.empty();
	}

	// 去掉 demo 名的 .gz / .demo 后缀（大小写不敏感）。
	// 旧版 demo 浏览器用 str_endswith_nocase 生成落盘名，这里必须保持同一规则：
	// 规则一变，同一场次在升级前后会算出两个文件名，旧缓存永远命不中。
	inline void StripDemoSuffixNoCase(std::string &Name, const char *pSuffix)
	{
		const size_t Len = str_length(pSuffix);
		if(Len == 0 || Name.size() <= Len)
			return;
		if(str_comp_nocase(Name.c_str() + Name.size() - Len, pSuffix) != 0)
			return;
		Name.resize(Name.size() - Len);
	}

	// Demo 浏览器下载 Rank 1 回放时的文件名：
	//   <安全地图名>_rank<N>_<ts>_<去掉 .gz/.demo 后缀并清理的 demo 名>.demo
	// 浏览器写盘与 Rank 1 页面复用必须用同一份完整身份；只按地图名前缀匹配
	// 会把同一地图的其它场次误认成当前条目。
	inline std::string BrowserDemoFileName(const char *pMap, const char *pDemo, int64_t Ts, int Rank)
	{
		if(pMap == nullptr || pDemo == nullptr || pMap[0] == '\0' || pDemo[0] == '\0')
			return {};

		char aSafeMap[128];
		str_copy(aSafeMap, pMap, sizeof(aSafeMap));
		str_sanitize_filename(aSafeMap);

		std::string Demo = pDemo;
		StripDemoSuffixNoCase(Demo, ".gz");
		StripDemoSuffixNoCase(Demo, ".demo");
		char aSafeDemo[256];
		str_copy(aSafeDemo, Demo.c_str(), sizeof(aSafeDemo));
		str_sanitize_filename(aSafeDemo);

		const int64_t SafeTs = Ts == std::numeric_limits<int64_t>::min() ? 0 : Ts;
		char aName[512];
		const int Written = str_format(aName, sizeof(aName), "%s_rank%d_%lld_%s.demo",
			aSafeMap, Rank, (long long)SafeTs, aSafeDemo);
		if(Written < 0 || Written >= (int)sizeof(aName))
			return {};
		return aName;
	}

	inline const SEntry *FindLatest(const std::vector<SEntry> &Entries, const char *pMap, int Rank)
	{
		const SEntry *pBest = nullptr;
		for(const SEntry &Entry : Entries)
		{
			if(Entry.m_Rank != Rank || pMap == nullptr || str_comp_nocase(Entry.m_Map.c_str(), pMap) != 0)
				continue;
			if(pBest == nullptr || Entry.m_Ts >= pBest->m_Ts)
				pBest = &Entry;
		}
		return pBest;
	}

	// 把成员名合并进逗号分隔的名字列表（已存在则跳过）
	inline void AppendUniqueName(std::string &Names, const std::string &Name)
	{
		if(Name.empty())
			return;
		if(!Names.empty())
		{
			if(Names == Name)
				return;
			// 已作为整项或列表中间项出现时跳过
			if(Names.size() >= Name.size() &&
				(Names.compare(0, Name.size(), Name) == 0 ||
					Names.compare(Names.size() - Name.size(), Name.size(), Name) == 0 ||
					Names.find(", " + Name + ", ") != std::string::npos))
				return;
			Names += ", ";
		}
		Names += Name;
	}

	// 收集指定地图与名次的全部条目：同一 demo 的多条成员记录合并为一条
	// （成员名并集、时间取最新），solo 在前、team 在后，各自按时间倒序。
	// 同一地图经常同时存在 solo rank1 与 team rank1，且 team 成绩的每个
	// 成员各有一条指向同一 demo 的记录。
	inline void CollectRankEntries(const std::vector<SEntry> &Entries, const char *pMap, int Rank, std::vector<SEntry> &Out)
	{
		Out.clear();
		if(pMap == nullptr || pMap[0] == '\0')
			return;

		std::map<std::string, size_t> DemoToIndex;
		for(const SEntry &Entry : Entries)
		{
			if(Entry.m_Rank != Rank || str_comp_nocase(Entry.m_Map.c_str(), pMap) != 0)
				continue;

			auto It = DemoToIndex.find(Entry.m_Demo);
			if(It != DemoToIndex.end())
			{
				SEntry &Existing = Out[It->second];
				// 同一 demo 的其他成员记录：合并名字与完成者
				std::string Copy = Entry.m_Names;
				size_t Start = 0;
				while(Start < Copy.size())
				{
					size_t End = Copy.find(", ", Start);
					const size_t Len = End == std::string::npos ? Copy.size() - Start : End - Start;
					AppendUniqueName(Existing.m_Names, Copy.substr(Start, Len));
					if(End == std::string::npos)
						break;
					Start = End + 2;
				}
				for(int Finisher : Entry.m_vFinishers)
				{
					if(std::find(Existing.m_vFinishers.begin(), Existing.m_vFinishers.end(), Finisher) == Existing.m_vFinishers.end())
						Existing.m_vFinishers.push_back(Finisher);
				}
				if(Entry.m_Ts > Existing.m_Ts)
					Existing.m_Ts = Entry.m_Ts;
				continue;
			}

			Out.push_back(Entry);
			DemoToIndex[Entry.m_Demo] = Out.size() - 1;
		}

		std::stable_sort(Out.begin(), Out.end(), [](const SEntry &Left, const SEntry &Right) {
			const bool LeftTeam = IsTeamEntry(Left);
			const bool RightTeam = IsTeamEntry(Right);
			if(LeftTeam != RightTeam)
				return !LeftTeam; // solo 在前
			return Right.m_Ts < Left.m_Ts;
		});
	}

	// 全清单搜索：按地图名子串（大小写不敏感）匹配指定名次的条目，按 demo 去重。
	// 不在这里按展示条数提前退出：先收集全部匹配项，调用方排序后再截断，
	// 否则表头排序只是清单前 N 条的局部排序。
	inline void CollectSearchEntries(const std::vector<SEntry> &Entries, const char *pQuery, int Rank, std::vector<SEntry> &Out)
	{
		Out.clear();
		if(pQuery == nullptr || pQuery[0] == '\0')
			return;
		std::set<std::string> SeenDemos;
		for(const SEntry &Entry : Entries)
		{
			if(Entry.m_Rank != Rank)
				continue;
			if(str_find_nocase(Entry.m_Map.c_str(), pQuery) == nullptr)
				continue;
			if(!SeenDemos.insert(Entry.m_Demo).second)
				continue;
			Out.push_back(Entry);
		}
	}
} // namespace qmclient::rank_demo

#endif
