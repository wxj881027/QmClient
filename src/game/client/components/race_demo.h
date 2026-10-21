/* (c) Redix and Sushi */

#ifndef GAME_CLIENT_COMPONENTS_RACE_DEMO_H
#define GAME_CLIENT_COMPONENTS_RACE_DEMO_H

#include <game/client/component.h>
#include <game/client/components/qmclient/race_demo_storage.h>

class CRaceDemo : public CComponent
{
	enum
	{
		RACE_NONE = 0,
		RACE_IDLE,
		RACE_PREPARE,
		RACE_STARTED,
		RACE_FINISHED,
	};

	static const char *ms_pRaceDemoDir;

	char m_aTmpFilename[IO_MAX_PATH_LENGTH];

	int m_RaceState;
	int m_RaceStartTick;
	int m_RecordStopTick;
	int m_Time;

	qm_race_demo::CFinalizer m_Finalizer;

	void GetPath(char *pBuf, int Size) const;
	bool StartRecord() noexcept;
	void StopRecord(int Time = -1, bool Force = false) noexcept;
	void FinishPending(bool Force = false) noexcept;
	void ResetRaceState() noexcept;
	void ReportSaveFailure(bool AfterCommit) noexcept;

public:
	bool m_AllowRestart;

	CRaceDemo();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnMapLoad() override;
	void OnUpdate() override;
	void OnShutdown() override;
	void OnNewSnapshot() override;
};
#endif
