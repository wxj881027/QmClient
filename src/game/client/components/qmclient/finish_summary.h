#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FINISH_SUMMARY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FINISH_SUMMARY_H

#include "finish_summary_data.h"
#include "finish_summary_state.h"

#include <game/client/component.h>

#include <array>
#include <memory>

class IHttpRequest;

class CQmFinishSummary : public CComponent
{
	enum class EQuery
	{
		MAP,
		PLAYER,
		CHINA,
		IDENTITY,
		RANK,
		POINTS,
		PROGRESS,
		TIMES,
		COUNT
	};
	class CParseJob;
	struct SRequest
	{
		std::shared_ptr<IHttpRequest> m_pHttp;
		std::shared_ptr<CParseJob> m_pJob;
		int m_Attempts = 0;
		bool m_Done = false;
		bool m_Succeeded = false;
		double m_NotBefore = 0.0;
	};
	std::array<SRequest, static_cast<size_t>(EQuery::COUNT)> m_aRequests;
	QmFinishSummary::CState m_State;
	QmFinishSummary::SRemoteData m_Data;
	QmFinishSummary::SRemoteData m_Before;
	QmFinishSummary::ECommunity m_Community = QmFinishSummary::ECommunity::NONE;
	std::string m_Map;
	std::string m_Player;
	int m_ClientId = -1;
	int m_Connection = -1;
	int m_RunStartTick = -1;
	int m_FinishGameTick = -1;
	int64_t m_FinishTimestamp = 0;
	double m_NextRequest = 0.0;
	uint64_t m_Generation = 0;
	std::optional<int64_t> m_PreviousBest;
	bool m_PreviousHistoryKnown = false;
	std::vector<QmFinishSummary::SOnlineTime> m_vFinishOnlineTimes;
	bool m_FinishOnlineTimesKnown = false;

	bool UpdateContext();
	void CancelRequests();
	void PrepareRequests(bool AfterFinish);
	std::string QueryUrl(EQuery Query) const;
	void UpdateRequests(double Now);
	void MergeResult(EQuery Query, const QmFinishSummary::SRemoteData &Result);
	void RefreshSnapshot();
	void StartRun(int StartTick);
	void Finish(int64_t TimeMs, std::optional<int64_t> Difference, bool PersonalRecord, bool Structured);
	static double Now();

public:
	~CQmFinishSummary() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnNewSnapshot() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnReset() override;
	void OnMapLoad() override { OnReset(); }
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override { OnReset(); }
	const QmFinishSummary::SSnapshot *Snapshot() const { return m_State.Snapshot(); }
	bool Visible() const { return m_State.Visible(); }
	void SetExpanded(bool Expanded) { m_State.SetExpanded(Expanded, Now()); }
};

#endif
