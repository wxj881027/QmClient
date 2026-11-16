#ifndef GAME_CLIENT_QMUI_QMFINISHSUMMARYPRESENTATION_H
#define GAME_CLIENT_QMUI_QMFINISHSUMMARYPRESENTATION_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace qm_finish_summary
{
	enum class EField
	{
		MAP,
		MAPPER,
		TYPE,
		FINISHED_AT,
		COMPARISON,
		DIFFERENCE,
		LOCAL_RANK,
		CHINA_RANK,
		RANK,
		PLAYER,
		COMPLETION,
		POINTS,
	};

	enum class ENumber
	{
		TIME,
		DIFFERENCE,
		LOCAL_RANK,
		LOCAL_TOTAL,
		CHINA_RANK,
		RANK,
		COMPLETED,
		TOTAL,
		POINTS,
		GAIN,
		PROGRESS,
		COUNT,
	};

	using TNumberTargets = std::array<double, static_cast<size_t>(ENumber::COUNT)>;

	class CPresentation
	{
		static constexpr double NUMBER_DURATION = 0.55;
		static constexpr double INTRO_DURATION = 2.60;
		struct SNumber
		{
			double m_From = 0.0;
			double m_Target = -1.0;
			double m_Start = 0.0;

			double Value(double Now) const
			{
				if(m_Target < 0.0)
					return 0.0;
				const double T = std::clamp((Now - m_Start) / NUMBER_DURATION, 0.0, 1.0);
				return m_From + (m_Target - m_From) * T * T * (3.0 - 2.0 * T);
			}
		};
		std::array<SNumber, static_cast<size_t>(ENumber::COUNT)> m_aNumbers{};
		uint64_t m_Id = 0;
		bool m_Valid = false;
		bool m_Started = false;
		bool m_Settled = false;
		double m_Start = 0.0;
		double m_Now = 0.0;

		static constexpr double FieldStart(EField Field)
		{
			return 0.95 + static_cast<int>(Field) * 0.10;
		}

		static double NumberStart(ENumber Number)
		{
			constexpr std::array<double, static_cast<size_t>(ENumber::COUNT)> s_aStarts = {
				0.0, FieldStart(EField::DIFFERENCE), FieldStart(EField::LOCAL_RANK), FieldStart(EField::LOCAL_RANK),
				FieldStart(EField::CHINA_RANK), FieldStart(EField::RANK), FieldStart(EField::COMPLETION),
				FieldStart(EField::COMPLETION), FieldStart(EField::POINTS), FieldStart(EField::POINTS), FieldStart(EField::COMPLETION)};
			return s_aStarts[static_cast<size_t>(Number)];
		}

	public:
		void Update(uint64_t Id, double Now, bool FullyExpanded, int MotionLevel, const TNumberTargets &Targets)
		{
			if(!m_Valid || m_Id != Id)
			{
				*this = {};
				m_Id = Id;
				m_Valid = true;
			}
			m_Now = std::max(Now, m_Now);
			if(!m_Started && (FullyExpanded || MotionLevel <= 0))
			{
				m_Start = m_Now;
				m_Started = true;
			}
			if(!m_Started)
				return;
			m_Settled = m_Settled || MotionLevel <= 0 || m_Now - m_Start >= INTRO_DURATION;
			for(size_t i = 0; i < m_aNumbers.size(); ++i)
			{
				SNumber &Number = m_aNumbers[i];
				if(Targets[i] < 0.0)
					continue;
				if(MotionLevel <= 0)
				{
					Number.m_From = Targets[i];
					Number.m_Target = Targets[i];
					Number.m_Start = m_Now - NUMBER_DURATION;
				}
				else if(Number.m_Target != Targets[i])
				{
					Number.m_From = Number.Value(m_Now);
					Number.m_Target = Targets[i];
					Number.m_Start = m_Settled ? m_Now : std::max(m_Now, m_Start + NumberStart(static_cast<ENumber>(i)));
				}
			}
		}

		double Number(ENumber Number) const
		{
			return m_aNumbers[static_cast<size_t>(Number)].Value(m_Now);
		}

		float FieldAlpha(EField Field) const
		{
			if(m_Settled)
				return 1.0f;
			if(!m_Started)
				return 0.0f;
			const float T = std::clamp(static_cast<float>((m_Now - m_Start - FieldStart(Field)) / 0.18), 0.0f, 1.0f);
			return T * T * (3.0f - 2.0f * T);
		}

		float TimePlacement() const
		{
			if(m_Settled)
				return 1.0f;
			if(!m_Started)
				return 0.0f;
			const float T = std::clamp(static_cast<float>((m_Now - m_Start - 0.55) / 0.40), 0.0f, 1.0f);
			return T * T * (3.0f - 2.0f * T);
		}

		bool Complete() const { return m_Settled; }
	};
}

#endif
