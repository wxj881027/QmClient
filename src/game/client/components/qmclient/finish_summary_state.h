#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FINISH_SUMMARY_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FINISH_SUMMARY_STATE_H

#include "finish_summary_data.h"

#include <optional>
#include <utility>

namespace QmFinishSummary
{
	class CState
	{
		std::optional<SSnapshot> m_Snapshot;
		std::optional<SSnapshot> m_BestPreview;
		bool m_Previewing = false;
		uint64_t m_NextId = 0;
		int m_RunStartTick = -1;
		double m_ReceivedAt = 0.0;
		std::optional<double> m_ExpandedAt;
		bool m_Visible = false;
		bool m_FinishedCurrentRun = false;

		bool IsDuplicate(const SSnapshot &Incoming, double Now, int RunStartTick) const
		{
			if(!m_FinishedCurrentRun || !m_Snapshot || m_Snapshot->m_Community != Incoming.m_Community ||
				m_Snapshot->m_Map != Incoming.m_Map || m_Snapshot->m_Player != Incoming.m_Player ||
				m_Snapshot->m_ClientId != Incoming.m_ClientId)
				return false;

			// 已知起跑 tick 能识别同一轮，也能区分很快开始的下一轮。
			if(m_RunStartTick > 0 && RunStartTick > 0)
				return m_RunStartTick == RunStartTick;

			// 聊天播报只有厘秒精度；未知起跑 tick 时在短窗口内合并两个消息来源。
			return MatchesFinish(Incoming.m_ClientId, Incoming.m_TimeMs) &&
			       Now >= m_ReceivedAt && Now - m_ReceivedAt < 1.0;
		}

	public:
		bool Begin(SSnapshot Snapshot, double Now, int RunStartTick)
		{
			if(IsDuplicate(Snapshot, Now, RunStartTick))
			{
				if(m_RunStartTick <= 0 && RunStartTick > 0)
					m_RunStartTick = RunStartTick;
				return false;
			}

			m_Previewing = false;
			m_BestPreview.reset();
			Snapshot.m_Id = ++m_NextId;
			m_Snapshot = std::move(Snapshot);
			m_RunStartTick = RunStartTick;
			m_ReceivedAt = Now;
			m_ExpandedAt.reset();
			m_Visible = true;
			m_FinishedCurrentRun = true;
			return true;
		}

		bool Preview(SSnapshot Best)
		{
			if(!m_Snapshot && Best.m_TimeMs <= 0)
				return false;
			if(!m_Snapshot)
				m_BestPreview = std::move(Best);
			(m_Snapshot ? *m_Snapshot : *m_BestPreview).m_Id = ++m_NextId;
			m_ExpandedAt.reset();
			m_Visible = true;
			m_Previewing = true;
			return true;
		}

		bool Previewing() const { return m_Previewing; }
		bool ShowingBest() const { return m_BestPreview.has_value(); }

		void SetExpanded(bool Expanded, double Now)
		{
			// 首次完整展开才开始计时，后续形变或内容更新不延长展示时间。
			if(m_Visible && Expanded && !m_ExpandedAt)
				m_ExpandedAt = Now;
		}

		void Advance(double Now)
		{
			if(m_Visible && m_ExpandedAt && Now - *m_ExpandedAt >= 12.0)
				Dismiss();
		}

		void Dismiss()
		{
			// 收起阶段继续借用快照，重复通关消息也继续由这一份身份去重。
			m_Visible = false;
		}

		void StartRun()
		{
			// 新一轮起跑后保留收起快照，但停止使用上一轮成绩去重。
			Dismiss();
			m_FinishedCurrentRun = false;
		}

		void Reset()
		{
			m_Snapshot.reset();
			m_BestPreview.reset();
			m_Previewing = false;
			m_RunStartTick = -1;
			m_ReceivedAt = 0.0;
			m_ExpandedAt.reset();
			m_Visible = false;
			m_FinishedCurrentRun = false;
		}

		bool Visible() const { return m_Visible; }
		bool MatchesFinish(int ClientId, int64_t TimeMs) const
		{
			if(!m_Snapshot || ClientId < 0 || m_Snapshot->m_ClientId != ClientId || TimeMs <= 0 || m_Snapshot->m_TimeMs <= 0)
				return false;
			const int64_t Difference = m_Snapshot->m_TimeMs - TimeMs;
			return Difference > -10 && Difference < 10;
		}
		const SSnapshot *Snapshot() const { return m_BestPreview ? &*m_BestPreview : (m_Snapshot ? &*m_Snapshot : nullptr); }
		SSnapshot *MutableBestPreview() { return m_BestPreview ? &*m_BestPreview : nullptr; }
		SSnapshot *MutableSnapshot() { return m_Snapshot ? &*m_Snapshot : nullptr; }
	};
}

#endif
