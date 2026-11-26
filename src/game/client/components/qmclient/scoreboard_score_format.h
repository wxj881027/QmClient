#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_SCORE_FORMAT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_SCORE_FORMAT_H

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>

#include <generated/protocol.h>

#include <cstdint>

namespace scoreboard_score
{
	enum class EKind
	{
		NONE,
		POINTS,
		LEGACY_TIME,
		DDNET_TIME,
		RACE7_TIME,
	};

	struct SValue
	{
		EKind m_Kind;
		// 时间统一用毫秒比较；传统时间保留取反后的符号以兼容旧服排序。
		int64_t m_Value;

		bool IsTime() const { return m_Kind != EKind::NONE && m_Kind != EKind::POINTS; }

		bool Precedes(const SValue &Other) const
		{
			if(IsTime() != Other.IsTime())
				return IsTime();
			if(IsTime())
				return m_Value < Other.m_Value;
			if(m_Kind != Other.m_Kind)
				return m_Kind == EKind::POINTS;
			return m_Value > Other.m_Value;
		}

		// HUD 使用紧凑时间；计分板单独绘制 DDNet 时间的小数部分。
		void Format(char *pBuffer, int BufferSize) const
		{
			if(BufferSize <= 0)
				return;
			if(m_Kind == EKind::NONE)
				pBuffer[0] = '\0';
			else if(m_Kind == EKind::POINTS)
				str_format(pBuffer, BufferSize, "%d", static_cast<int>(m_Value));
			else
				str_time(absolute(m_Value) / 10, m_Kind == EKind::RACE7_TIME ? TIME_MINS_CENTISECS : TIME_HOURS, pBuffer, BufferSize);
		}
	};

	enum class EColumn
	{
		SCORE,
		TIME,
		TIME_AND_SCORE,
	};

	enum class ETitleSource
	{
		TEAM_SCORE,
		MAP_BEST,
		PLAYER_SCORE,
		NONE,
	};

	struct STitleScore
	{
		ETitleSource m_Source;
		int m_TeamScore;
	};

	inline STitleScore ResolveTitleScore(EColumn Column, bool TeamPlay, const CNetObj_GameData *pGameData, int Team)
	{
		if(TeamPlay && pGameData)
			return {ETitleSource::TEAM_SCORE, Team == TEAM_RED ? pGameData->m_TeamscoreRed : pGameData->m_TeamscoreBlue};
		if(Column != EColumn::SCORE)
			return {ETitleSource::MAP_BEST, 0};
		return {TeamPlay ? ETitleSource::NONE : ETitleSource::PLAYER_SCORE, 0};
	}

	struct SMode
	{
		bool m_Race7;
		bool m_TimeScore;
		bool m_Fng;

		bool AllowsRankSort() const { return !m_Fng; }

		bool UseFinishFields(bool ReceivedDDNetPlayer, bool AllFinishTimesSet) const
		{
			return m_Fng ? ReceivedDDNetPlayer : AllFinishTimesSet;
		}

		SValue Resolve(int Score, bool HasFinishTimes, int FinishSeconds, int FinishMillis) const
		{
			if(m_Race7)
				return Score == protocol7::FinishTime::NOT_FINISHED ? SValue{} : SValue{EKind::RACE7_TIME, Score};
			const bool HasFinishField = HasFinishTimes && FinishSeconds != FinishTime::UNSET;
			if(m_TimeScore && HasFinishField && FinishSeconds >= 0)
				return {EKind::DDNET_TIME, static_cast<int64_t>(FinishSeconds) * 1000 + FinishMillis};
			if(Score == FinishTime::NOT_FINISHED_TIMESCORE)
				return {};
			if(!m_TimeScore || (m_Fng && (HasFinishField || Score >= 0)))
				return {EKind::POINTS, Score};
			if(HasFinishField)
				return {};
			return {EKind::LEGACY_TIME, -static_cast<int64_t>(Score) * 1000};
		}

		EColumn Column(bool HasTime, bool HasPoints) const
		{
			if(m_Race7 || (m_TimeScore && !m_Fng))
				return EColumn::TIME;
			if(m_TimeScore && HasTime)
				return HasPoints ? EColumn::TIME_AND_SCORE : EColumn::TIME;
			return EColumn::SCORE;
		}
	};
}

#endif
