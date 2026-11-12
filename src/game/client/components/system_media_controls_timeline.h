#ifndef GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_TIMELINE_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace SystemMediaControls
{
	constexpr int64_t HUNDRED_NS_PER_SECOND = 10000000;
	constexpr int64_t MAX_TIMELINE_AGE_100NS = 24LL * 60 * 60 * HUNDRED_NS_PER_SECOND;

	struct STimelineProperties
	{
		int64_t m_Start100ns = 0;
		int64_t m_End100ns = 0;
		int64_t m_Position100ns = 0;
		int64_t m_LastUpdatedUtc100ns = 0;

		bool operator==(const STimelineProperties &Other) const = default;
	};

	struct STimelineSnapshot
	{
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		int64_t m_PositionUpdatedTick = 0;
	};

	class CTimelineGenerationTracker
	{
	public:
		void Reset()
		{
			m_LastProperties = {};
			m_Generation = 0;
			m_HasProperties = false;
		}

		uint64_t Update(const STimelineProperties &Properties)
		{
			if(!m_HasProperties || !(Properties == m_LastProperties))
			{
				m_LastProperties = Properties;
				m_HasProperties = true;
				++m_Generation;
			}
			return m_Generation;
		}

	private:
		STimelineProperties m_LastProperties;
		uint64_t m_Generation = 0;
		bool m_HasProperties = false;
	};

	inline int64_t TimelineAgeToTicks(int64_t Age100ns, int64_t TickFreq)
	{
		if(Age100ns <= 0 || TickFreq <= 0)
			return 0;
		const int64_t WholeSeconds = Age100ns / HUNDRED_NS_PER_SECOND;
		const int64_t Remainder100ns = Age100ns % HUNDRED_NS_PER_SECOND;
		// 先拆频率，保留最终商可表示、但直接乘积不可表示的亚秒部分。
		const int64_t FractionTicks = (TickFreq / HUNDRED_NS_PER_SECOND) * Remainder100ns +
					      (TickFreq % HUNDRED_NS_PER_SECOND) * Remainder100ns / HUNDRED_NS_PER_SECOND;
		constexpr int64_t MaxTicks = std::numeric_limits<int64_t>::max();
		if(WholeSeconds > (MaxTicks - FractionTicks) / TickFreq)
			return MaxTicks;
		return WholeSeconds * TickFreq + FractionTicks;
	}

	inline STimelineSnapshot NormalizeTimelineProperties(const STimelineProperties &Properties, int64_t ObservedUtc100ns, int64_t ObservedSteadyTick, int64_t TickFreq)
	{
		STimelineSnapshot Snapshot;
		// 两个有符号时间点的正差可占满 uint64_t，换成毫秒后仍在 int64_t 范围内。
		const auto PositiveDifference = [](int64_t Value, int64_t Base) -> uint64_t {
			return Value > Base ? static_cast<uint64_t>(Value) - static_cast<uint64_t>(Base) : 0;
		};
		const uint64_t Duration100ns = PositiveDifference(Properties.m_End100ns, Properties.m_Start100ns);
		const uint64_t Position100ns = PositiveDifference(Properties.m_Position100ns, Properties.m_Start100ns);
		Snapshot.m_DurationMs = static_cast<int64_t>(Duration100ns / 10000);
		Snapshot.m_PositionMs = static_cast<int64_t>((Duration100ns > 0 ? std::min(Position100ns, Duration100ns) : Position100ns) / 10000);
		Snapshot.m_PositionUpdatedTick = ObservedSteadyTick;

		if(Properties.m_LastUpdatedUtc100ns > 0 && ObservedUtc100ns >= Properties.m_LastUpdatedUtc100ns)
		{
			const int64_t TimelineAge100ns = ObservedUtc100ns - Properties.m_LastUpdatedUtc100ns;
			if(TimelineAge100ns <= MAX_TIMELINE_AGE_100NS)
			{
				const int64_t AgeTicks = TimelineAgeToTicks(TimelineAge100ns, TickFreq);
				// 无法表示的回溯时间沿用无效时间戳的观测时刻回退。
				if(ObservedSteadyTick >= std::numeric_limits<int64_t>::min() + AgeTicks)
					Snapshot.m_PositionUpdatedTick = ObservedSteadyTick - AgeTicks;
			}
		}
		return Snapshot;
	}

} // namespace SystemMediaControls

#endif
