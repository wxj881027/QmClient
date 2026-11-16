#include "finish_summary_data.h"

#include "map_cover_art.h"

#include <base/detect.h>
#include <base/str.h>

#include <engine/shared/json.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

namespace
{
	using namespace QmFinishSummary;
	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;
	constexpr size_t MAX_JSON_BYTES = 8 * 1024 * 1024;
	constexpr int64_t MAX_TIME_MS = std::numeric_limits<int>::max();

	int64_t Centiseconds(int64_t Milliseconds)
	{
		return Milliseconds / 10 + (Milliseconds % 10 >= 5 ? 1 : 0);
	}

	bool RankReflectsFinish(std::optional<int64_t> Best, int64_t Finish, bool ConfirmedFinish)
	{
		if(!Best)
			return false;
		return *Best <= Finish || (ConfirmedFinish && Centiseconds(*Best) == Centiseconds(Finish));
	}

	TJson Parse(const char *pJson, size_t Length)
	{
		return {pJson && Length > 0 && Length <= MAX_JSON_BYTES ? JsonParse(pJson, Length) : nullptr, json_value_free};
	}

	const json_value *Field(const json_value *pObject, const char *pName)
	{
		return pObject && pObject->type == json_object ? json_object_get(pObject, pName) : &json_value_none;
	}

	bool Text(const json_value *pValue, std::string &Out)
	{
		if(pValue->type != json_string || pValue->u.string.length > 512)
			return false;
		const char *pText = json_string_get(pValue);
		if(!pText || !str_utf8_check(pText) || pValue->u.string.length != static_cast<unsigned>(str_length(pText)))
			return false;
		if(std::any_of(pText, pText + pValue->u.string.length, [](unsigned char Ch) { return Ch < 0x20 || Ch == 0x7f; }))
			return false;
		Out = pText;
		return true;
	}

	bool Matches(const json_value *pObject, const char *pField, const std::string &Expected)
	{
		std::string Value;
		return Text(Field(pObject, pField), Value) && Value == Expected;
	}

	std::optional<int64_t> Integer(const json_value *pValue, int64_t Minimum = 0)
	{
		if(pValue->type != json_integer || pValue->u.integer < Minimum)
			return std::nullopt;
		return pValue->u.integer;
	}

	std::optional<int64_t> TimeMs(const json_value *pValue)
	{
		double Seconds;
		if(pValue->type == json_integer)
			Seconds = static_cast<double>(pValue->u.integer);
		else if(pValue->type == json_double)
			Seconds = pValue->u.dbl;
		else
			return std::nullopt;
		if(!std::isfinite(Seconds) || Seconds <= 0 || Seconds * 1000.0 > MAX_TIME_MS)
			return std::nullopt;
		const int64_t Milliseconds = static_cast<int64_t>(std::llround(Seconds * 1000.0));
		return Milliseconds > 0 ? std::optional<int64_t>(Milliseconds) : std::nullopt;
	}

	std::optional<int64_t> MapFinishes(const json_value *pMap)
	{
		const json_value *pFinishes = Field(pMap, "finishes");
		if(pFinishes->type != json_none)
			return Integer(pFinishes);
		// 部分已部署的玩家接口省略未完成地图的 finishes，仍提供目录分值和总完成数。
		if(Integer(Field(pMap, "points")) && Integer(Field(pMap, "total_finishes")) &&
			Field(pMap, "time")->type == json_none && Field(pMap, "rank")->type == json_none &&
			Field(pMap, "first_finish")->type == json_none)
			return 0;
		return std::nullopt;
	}

	std::optional<int64_t> UnixTimestamp(const json_value *pValue)
	{
		if(pValue->type == json_integer)
		{
			const auto Timestamp = Integer(pValue);
			return Timestamp && *Timestamp < 253402300800LL ? Timestamp : std::nullopt;
		}
		if(pValue->type == json_double && std::isfinite(pValue->u.dbl) && pValue->u.dbl >= 0 && pValue->u.dbl < 253402300800.0)
			return static_cast<int64_t>(pValue->u.dbl);
		return std::nullopt;
	}

	const json_value *AxiomData(const TJson &Root, const char *pMode = nullptr, bool ModeOptional = false)
	{
		const auto Code = Integer(Field(Root.get(), "code"));
		const json_value *pData = Field(Root.get(), "data");
		if(pMode && !(ModeOptional && Field(pData, "mode")->type == json_none) && !Matches(pData, "mode", pMode))
			return &json_value_none;
		return Code == 200 && pData->type == json_object ? pData : &json_value_none;
	}

	bool UserMatches(const json_value *pObject, int64_t UserId)
	{
		return UserId >= 0 && Integer(Field(Field(pObject, "user"), "user_id")) == UserId;
	}

	bool ReadMap(const json_value *pMap, const char *pNameField, const char *pCategoryField, const char *pStarsField, const std::string &Map, SRemoteData &Out)
	{
		if(!Matches(pMap, pNameField, Map))
			return false;
		SRemoteData Result;
		Result.m_Map = Map;
		Text(Field(pMap, "mapper"), Result.m_Mapper);
		Text(Field(pMap, pCategoryField), Result.m_Category);
		const auto Stars = Integer(Field(pMap, pStarsField));
		if(Stars && *Stars <= 5)
			Result.m_Stars = static_cast<int>(*Stars);
		const auto Points = Integer(Field(pMap, "points"));
		if(Points && *Points <= std::numeric_limits<int>::max())
			Result.m_MapPoints = static_cast<int>(*Points);
		Out = std::move(Result);
		return true;
	}

	// API 的 recorded_at 使用带时区的 ISO 日期，按 UTC 转换，避免客户端时区影响确认。
	std::optional<int64_t> IsoTimestamp(const json_value *pValue)
	{
		std::string TextValue;
		if(!Text(pValue, TextValue) || TextValue.size() < 20)
			return std::nullopt;
		std::tm Date{};
		std::istringstream Input(TextValue.substr(0, 19));
		Input >> std::get_time(&Date, "%Y-%m-%dT%H:%M:%S");
		if(Input.fail() || Date.tm_year < 70 || Date.tm_year > 8099 || Date.tm_sec > 59)
			return std::nullopt;
		size_t Offset = 19;
		if(TextValue[Offset] == '.')
		{
			++Offset;
			const size_t Start = Offset;
			while(Offset < TextValue.size() && TextValue[Offset] >= '0' && TextValue[Offset] <= '9')
				++Offset;
			if(Offset == Start || Offset == TextValue.size())
				return std::nullopt;
		}
		int ZoneSeconds = 0;
		if(TextValue.substr(Offset) != "Z")
		{
			if(TextValue.size() != Offset + 6 || (TextValue[Offset] != '+' && TextValue[Offset] != '-') || TextValue[Offset + 3] != ':')
				return std::nullopt;
			for(size_t Index : {Offset + 1, Offset + 2, Offset + 4, Offset + 5})
				if(TextValue[Index] < '0' || TextValue[Index] > '9')
					return std::nullopt;
			const int Hours = (TextValue[Offset + 1] - '0') * 10 + TextValue[Offset + 2] - '0';
			const int Minutes = (TextValue[Offset + 4] - '0') * 10 + TextValue[Offset + 5] - '0';
			if(Hours > 23 || Minutes > 59)
				return std::nullopt;
			ZoneSeconds = (Hours * 3600 + Minutes * 60) * (TextValue[Offset] == '+' ? 1 : -1);
		}
		const std::tm Original = Date;
#if defined(CONF_FAMILY_WINDOWS)
		const int64_t Timestamp = _mkgmtime(&Date);
#else
		const int64_t Timestamp = timegm(&Date);
#endif
		if(Timestamp < 0 || Original.tm_year != Date.tm_year || Original.tm_mon != Date.tm_mon || Original.tm_mday != Date.tm_mday)
			return std::nullopt;
		return Timestamp - ZoneSeconds;
	}

	SValue RemoteValue(std::optional<int64_t> Current, std::optional<int64_t> Previous, bool CurrentReady)
	{
		if(Current)
			return {*Current, CurrentReady ? EStatus::READY : EStatus::STALE};
		if(Previous)
			return {*Previous, EStatus::STALE};
		return {-1, EStatus::PENDING};
	}
}

namespace QmFinishSummary
{
	bool ParseDdnetMap(const char *pJson, size_t Length, const std::string &Map, SRemoteData &Out)
	{
		const auto Root = Parse(pJson, Length);
		if(!ReadMap(Root.get(), "name", "type", "difficulty", Map, Out))
			return false;
		std::string Thumbnail;
		if(Text(Field(Root.get(), "thumbnail"), Thumbnail) && QmMapCover::ValidThumbnailUrl(Thumbnail.c_str()))
			Out.m_Thumbnail = std::move(Thumbnail);
		return true;
	}

	bool ParseDdnetPlayer(const char *pJson, size_t Length, const std::string &Player, const std::string &Map, SRemoteData &Out)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pTypes = Field(Root.get(), "types");
		if(!Matches(Root.get(), "player", Player) || pTypes->type != json_object)
			return false;
		SRemoteData Result;
		Result.m_Points = Integer(Field(Field(Root.get(), "points"), "points"));
		const json_value *pRank = Field(Root.get(), "rank");
		Result.m_RankPoints = Integer(Field(pRank, "points"));
		// DDNet 对未上榜的玩家明确返回 {"rank": null}，字段缺失时仍保留未知。
		if(Field(pRank, "rank")->type == json_null && Field(pRank, "points")->type == json_none)
			Result.m_RankPoints = 0;
		for(unsigned Type = 0; Type < pTypes->u.object.length; ++Type)
		{
			const json_value *pMaps = Field(pTypes->u.object.values[Type].value, "maps");
			const json_value *pMap = Field(pMaps, Map.c_str());
			if(pMap->type != json_object)
				continue;
			// 同名地图出现在多个分类时保持未知，避免选择错误的分类分母。
			if(!Result.m_Map.empty())
				return false;
			Result.m_Map = Map;
			Result.m_Category = pTypes->u.object.values[Type].name;
			if(Result.m_Category.size() > 512 || !str_utf8_check(Result.m_Category.c_str()))
				return false;
			const auto Finishes = MapFinishes(pMap);
			Result.m_BestTimeMs = TimeMs(Field(pMap, "time"));
			Result.m_HistoryKnown = Finishes && (*Finishes == 0 || Result.m_BestTimeMs.has_value());
			Result.m_Rank = Integer(Field(pMap, "rank"), 1);
			const auto Points = Integer(Field(pMap, "points"));
			if(Points && *Points <= std::numeric_limits<int>::max())
				Result.m_MapPoints = static_cast<int>(*Points);
			int64_t Completed = 0;
			bool CompleteInventory = true;
			for(unsigned Index = 0; Index < pMaps->u.object.length; ++Index)
			{
				const auto Count = MapFinishes(pMaps->u.object.values[Index].value);
				if(!Count)
					CompleteInventory = false;
				else if(*Count > 0)
					++Completed;
			}
			if(CompleteInventory)
			{
				Result.m_CompletedMaps = Completed;
				Result.m_TotalMaps = pMaps->u.object.length;
			}
		}
		const json_value *pFinishes = Field(Root.get(), "last_finishes");
		if(pFinishes->type == json_array)
		{
			for(unsigned Index = 0; Index < pFinishes->u.array.length; ++Index)
			{
				const json_value *pFinish = pFinishes->u.array.values[Index];
				const auto Timestamp = UnixTimestamp(Field(pFinish, "timestamp"));
				const auto Time = TimeMs(Field(pFinish, "time"));
				if(Matches(pFinish, "map", Map) && Timestamp && Time && (!Result.m_LatestTimestamp || *Timestamp > *Result.m_LatestTimestamp))
				{
					Result.m_LatestTimestamp = Timestamp;
					Result.m_LatestTimeMs = Time;
				}
			}
		}
		Out = std::move(Result);
		return true;
	}

	bool ParseDdnetChinaRank(const char *pJson, size_t Length, const std::string &Player, const std::string &Map, SRemoteData &Out)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pRanks = Field(Root.get(), "ranks");
		if(!Matches(Root.get(), "name", Map) || pRanks->type != json_array)
			return false;
		SRemoteData Result;
		Result.m_Map = Map;
		Result.m_ChinaRankKnown = true;
		for(unsigned Index = 0; Index < pRanks->u.array.length; ++Index)
		{
			const json_value *pEntry = pRanks->u.array.values[Index];
			if(Matches(pEntry, "player", Player))
			{
				if(Result.m_ChinaRank)
					return false;
				Result.m_ChinaRank = Integer(Field(pEntry, "rank"), 1);
				Result.m_ChinaBestTimeMs = TimeMs(Field(pEntry, "time"));
				if(!Result.m_ChinaRank || !Result.m_ChinaBestTimeMs)
					return false;
			}
		}
		Out = std::move(Result);
		return true;
	}

	bool ParseAxiomSearch(const char *pJson, size_t Length, const std::string &Player, SRemoteData &Out)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pData = AxiomData(Root);
		const json_value *pItems = Field(pData, "items");
		const json_value *pDirectUser = Field(pData, "user");
		if(pItems->type != json_array && pDirectUser->type != json_object)
			return false;
		SRemoteData Result;
		const unsigned Count = pItems->type == json_array ? pItems->u.array.length : 1;
		for(unsigned Index = 0; Index < Count; ++Index)
		{
			const json_value *pUser = pItems->type == json_array ? Field(pItems->u.array.values[Index], "user") : pDirectUser;
			const auto UserId = Integer(Field(pUser, "user_id"));
			const json_value *pRole = Field(pUser, "player");
			if(!Matches(pRole, "name", Player))
				pRole = Field(pUser, "dummy");
			if(!UserId || !Matches(pRole, "name", Player))
				continue;
			if(Result.m_UserId >= 0)
				return false;
			Result.m_UserId = *UserId;
		}
		if(Result.m_UserId < 0)
			return false;
		Out = std::move(Result);
		return true;
	}

	bool ParseAxiomMap(const char *pJson, size_t Length, const std::string &Map, SRemoteData &Out, const char *pMode)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pData = AxiomData(Root, pMode, true);
		SRemoteData Result;
		if(!ReadMap(pData, "map", "difficulty", "stars", Map, Result))
			return false;
		Result.m_BestTimeMs = TimeMs(Field(pData, "player_best_time"));
		// null 同时表示未提供角色 ID，不能据此判定首次完成。
		Result.m_HistoryKnown = Result.m_BestTimeMs.has_value();
		Out = std::move(Result);
		return true;
	}

	bool ParseAxiomTop(const char *pJson, size_t Length, const std::string &Map, int64_t UserId, SRemoteData &Out, const char *pMode)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pData = AxiomData(Root, pMode);
		const json_value *pItems = Field(pData, "items");
		if(UserId < 0 || !Matches(pData, "map", Map) || pItems->type != json_array || pItems->u.array.length > 200)
			return false;
		SRemoteData Result;
		Result.m_Map = Map;
		Result.m_UserId = UserId;
		for(unsigned Index = 0; Index < pItems->u.array.length; ++Index)
		{
			const json_value *pEntry = pItems->u.array.values[Index];
			if(!UserMatches(pEntry, UserId))
				continue;
			if(Result.m_Rank)
				return false;
			Result.m_Rank = Integer(Field(pEntry, "rank"), 1);
			Result.m_AccountBestTimeMs = TimeMs(Field(pEntry, "time"));
			if(!Result.m_Rank || !Result.m_AccountBestTimeMs)
				return false;
			Result.m_AccountHistoryKnown = true;
		}
		Out = std::move(Result);
		return true;
	}

	bool ParseAxiomPoints(const char *pJson, size_t Length, int64_t UserId, SRemoteData &Out, const char *pMode)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pData = AxiomData(Root, pMode);
		const auto Points = Integer(Field(pData, "points"));
		if(!UserMatches(pData, UserId) || !Points)
			return false;
		SRemoteData Result;
		Result.m_UserId = UserId;
		Result.m_Points = Points;
		Out = std::move(Result);
		return true;
	}

	bool ParseAxiomProgress(const char *pJson, size_t Length, int64_t UserId, const std::string &Category, SRemoteData &Out, const char *pMode)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pData = AxiomData(Root, pMode);
		const json_value *pItems = Field(pData, "items");
		if(!UserMatches(pData, UserId) || Category.empty() || pItems->type != json_array)
			return false;
		SRemoteData Result;
		Result.m_UserId = UserId;
		for(unsigned Index = 0; Index < pItems->u.array.length; ++Index)
		{
			const json_value *pItem = pItems->u.array.values[Index];
			if(!Matches(pItem, "difficulty", Category))
				continue;
			if(!Result.m_Category.empty())
				return false;
			Result.m_Category = Category;
			Result.m_CompletedMaps = Integer(Field(pItem, "completed_maps"));
			Result.m_TotalMaps = Integer(Field(pItem, "total_maps"));
			if(!Result.m_CompletedMaps || !Result.m_TotalMaps || *Result.m_CompletedMaps > *Result.m_TotalMaps)
				return false;
		}
		Out = std::move(Result);
		return true;
	}

	bool ParseAxiomTimes(const char *pJson, size_t Length, const std::string &Map, int64_t UserId, SRemoteData &Out, const char *pMode)
	{
		const auto Root = Parse(pJson, Length);
		const json_value *pData = AxiomData(Root, pMode);
		const json_value *pItems = Field(pData, "items");
		if(UserId < 0 || !Matches(pData, "map", Map) || pItems->type != json_array || pItems->u.array.length > 5)
			return false;
		SRemoteData Result;
		Result.m_Map = Map;
		Result.m_UserId = UserId;
		for(unsigned Index = 0; Index < pItems->u.array.length; ++Index)
		{
			const json_value *pItem = pItems->u.array.values[Index];
			if(!UserMatches(pItem, UserId))
				continue;
			const auto Timestamp = IsoTimestamp(Field(pItem, "recorded_at"));
			const auto Time = TimeMs(Field(pItem, "time"));
			if(Timestamp && Time && (!Result.m_LatestTimestamp || *Timestamp > *Result.m_LatestTimestamp))
			{
				Result.m_LatestTimestamp = Timestamp;
				Result.m_LatestTimeMs = Time;
			}
		}
		Out = std::move(Result);
		return true;
	}

	void ResolveComparison(SSnapshot &Snapshot, std::optional<int64_t> PreviousBest, bool HistoryKnown, std::optional<int64_t> Difference, bool PersonalRecord)
	{
		Snapshot.m_Comparison = EComparison::UNKNOWN;
		Snapshot.m_DifferenceMs = 0;
		if(Snapshot.m_TimeMs <= 0 || Snapshot.m_TimeMs > MAX_TIME_MS || (PreviousBest && (*PreviousBest <= 0 || *PreviousBest > MAX_TIME_MS)))
			return;
		if(!Difference && PreviousBest && *PreviousBest > 0)
			Difference = (Centiseconds(Snapshot.m_TimeMs) - Centiseconds(*PreviousBest)) * 10;
		if(Difference && *Difference != 0)
		{
			// 消息和历史时间范围均有限，避免异常输入触发有符号取反溢出。
			if(*Difference < -MAX_TIME_MS || *Difference > MAX_TIME_MS)
				return;
			if(std::abs(*Difference) < 10)
			{
				if(PreviousBest)
				{
					const int64_t DisplayDifference = (Centiseconds(Snapshot.m_TimeMs) - Centiseconds(*PreviousBest)) * 10;
					if((DisplayDifference < 0 && *Difference < 0) || (DisplayDifference > 0 && *Difference > 0))
					{
						Snapshot.m_DifferenceMs = std::abs(DisplayDifference);
						Snapshot.m_Comparison = DisplayDifference < 0 ? EComparison::IMPROVED : EComparison::SLOWER;
						return;
					}
				}
				Snapshot.m_Comparison = *Difference < 0 ? EComparison::BEST : EComparison::TIED;
				return;
			}
			Snapshot.m_DifferenceMs = std::abs(*Difference);
			Snapshot.m_Comparison = *Difference < 0 ? EComparison::IMPROVED : EComparison::SLOWER;
		}
		else if(PreviousBest)
			Snapshot.m_Comparison = EComparison::TIED;
		else if(HistoryKnown)
			Snapshot.m_Comparison = EComparison::FIRST;
		else if(PersonalRecord)
			Snapshot.m_Comparison = EComparison::BEST;
		else if(Difference)
			Snapshot.m_Comparison = EComparison::TIED;
	}

	void ResolveLocalRank(SSnapshot &Snapshot, const std::vector<SOnlineTime> &vTimes, std::optional<int64_t> PreviousBest, bool Precise)
	{
		Snapshot.m_LocalRank = {-1, EStatus::UNAVAILABLE};
		Snapshot.m_LocalRankTotal = {-1, EStatus::UNAVAILABLE};
		if(!Precise || Snapshot.m_TimeMs <= 0 || Snapshot.m_Player.empty())
			return;
		const int64_t Best = PreviousBest && *PreviousBest > 0 ? std::min(*PreviousBest, Snapshot.m_TimeMs) : Snapshot.m_TimeMs;
		std::map<std::string, int64_t> Times;
		Times.emplace(Snapshot.m_Player, Centiseconds(Best));
		for(const SOnlineTime &Entry : vTimes)
		{
			if(Entry.m_Identity.empty() || !Entry.m_Known)
				return;
			if(!Entry.m_TimeMs)
				continue;
			if(*Entry.m_TimeMs <= 0)
				return;
			const int64_t Time = Centiseconds(*Entry.m_TimeMs);
			auto [It, Inserted] = Times.emplace(Entry.m_Identity, Time);
			if(!Inserted)
				It->second = std::min(It->second, Time);
		}
		const int64_t OwnBest = Times.at(Snapshot.m_Player);
		int64_t Rank = 1;
		for(const auto &Entry : Times)
			if(Entry.second < OwnBest)
				++Rank;
		Snapshot.m_LocalRank = {Rank, EStatus::READY};
		Snapshot.m_LocalRankTotal = {static_cast<int64_t>(Times.size()), EStatus::READY};
	}

	void ApplyRemote(SSnapshot &Snapshot, const SRemoteData &Before, const SRemoteData &After, bool ConfirmedFinish)
	{
		if((!Before.m_Map.empty() && Before.m_Map != Snapshot.m_Map) || (!After.m_Map.empty() && After.m_Map != Snapshot.m_Map))
			return;
		const bool Axiom = Snapshot.m_Community == ECommunity::AXIOM_GORES || Snapshot.m_Community == ECommunity::AXIOM_AXRACE;
		const auto PreviousBest = Axiom ? Before.m_AccountBestTimeMs : Before.m_BestTimeMs;
		const bool HistoryKnown = Axiom ? Before.m_AccountHistoryKnown : Before.m_HistoryKnown;
		const auto CurrentBest = Axiom ? After.m_AccountBestTimeMs : After.m_BestTimeMs;
		const bool CurrentHistory = Axiom ? After.m_AccountHistoryKnown : After.m_HistoryKnown;
		Snapshot.m_Mapper = After.m_Mapper.empty() ? Before.m_Mapper : After.m_Mapper;
		Snapshot.m_Thumbnail = Snapshot.m_Community == ECommunity::DDNET ? (After.m_Thumbnail.empty() ? Before.m_Thumbnail : After.m_Thumbnail) : std::string();
		Snapshot.m_Category = After.m_Category.empty() ? Before.m_Category : After.m_Category;
		Snapshot.m_Stars = After.m_Stars >= 0 ? After.m_Stars : Before.m_Stars;
		const bool RankCurrent = RankReflectsFinish(CurrentBest, Snapshot.m_TimeMs, ConfirmedFinish);
		Snapshot.m_Rank = RemoteValue(After.m_Rank, Before.m_Rank, RankCurrent);
		Snapshot.m_ChinaRank = RemoteValue(After.m_ChinaRank, Before.m_ChinaRank, RankReflectsFinish(After.m_ChinaBestTimeMs, Snapshot.m_TimeMs, ConfirmedFinish));
		if(After.m_ChinaRankKnown && !After.m_ChinaRank)
			Snapshot.m_ChinaRank = {-1, EStatus::UNAVAILABLE};
		if(Axiom)
			Snapshot.m_ChinaRank = {-1, EStatus::UNAVAILABLE};
		// 只在本图的本次成绩已同步、个人 Rank 积分确有增长时记录一次，供回看复用。
		if(Snapshot.m_Community == ECommunity::DDNET && Snapshot.m_RankPointsGained == 0 && ConfirmedFinish &&
			Before.m_RankPoints && After.m_RankPoints && *After.m_RankPoints > *Before.m_RankPoints &&
			After.m_BestTimeMs && Centiseconds(*After.m_BestTimeMs) == Centiseconds(Snapshot.m_TimeMs))
			Snapshot.m_RankPointsGained = *After.m_RankPoints - *Before.m_RankPoints;

		const bool HadFinish = HistoryKnown && PreviousBest.has_value();
		const bool FirstFinish = HistoryKnown && !PreviousBest;
		const bool NewFinishVisible = ConfirmedFinish && CurrentHistory && CurrentBest.has_value();
		const int MapPoints = After.m_MapPoints >= 0 ? After.m_MapPoints : Before.m_MapPoints;
		Snapshot.m_PointsGained = {-1, EStatus::PENDING};
		if(HadFinish)
			Snapshot.m_PointsGained = {0, EStatus::READY};
		else if(FirstFinish && NewFinishVisible && MapPoints >= 0)
			Snapshot.m_PointsGained = {MapPoints, EStatus::READY};
		bool PointsCurrent = HadFinish || NewFinishVisible;
		bool ProgressCurrent = HadFinish || NewFinishVisible;
		if(FirstFinish)
		{
			if(Before.m_Points && MapPoints >= 0 && After.m_Points)
				PointsCurrent &= *After.m_Points >= *Before.m_Points && *After.m_Points - *Before.m_Points >= MapPoints;
			if(Before.m_CompletedMaps && After.m_CompletedMaps)
				ProgressCurrent &= *After.m_CompletedMaps > *Before.m_CompletedMaps;
		}
		Snapshot.m_Points = RemoteValue(After.m_Points, Before.m_Points, PointsCurrent);
		Snapshot.m_CompletedMaps = RemoteValue(After.m_CompletedMaps, Before.m_CompletedMaps, ProgressCurrent);
		Snapshot.m_TotalMaps = RemoteValue(After.m_TotalMaps, Before.m_TotalMaps, After.m_TotalMaps.has_value());
	}
}
