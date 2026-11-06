#ifndef ENGINE_CLIENT_DEMOEDIT_H
#define ENGINE_CLIENT_DEMOEDIT_H

#include <engine/shared/demo.h>
#include <engine/shared/demo_output.h>
#include <engine/shared/jobs.h>
#include <engine/shared/snapshot.h>

class IStorage;

class CDemoEdit : public IJob
{
public:
	class COperations : public demo_output::COperations
	{
	public:
		virtual bool RemoveSource(IStorage &Storage, const char *pPath);
	};

private:
	rust::Box<CSnapshotDelta> m_pSnapshotDelta;
	rust::Box<CSnapshotDelta> m_pSnapshotDeltaSixup;
	IStorage *m_pStorage;
	COperations *m_pOperations;
	COperations &Operations();

	CDemoEditor m_DemoEditor;

	char m_aDemo[IO_MAX_PATH_LENGTH];
	char m_aDst[IO_MAX_PATH_LENGTH];
	int m_StartTick;
	int m_EndTick;
	bool m_Success = false;

protected:
	void Run() override;

public:
	// 存储与操作对象为借用，须覆盖作业完成；快照差分器在构造时克隆。
	CDemoEdit(const char *pNetVersion, CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, IStorage *pStorage, const char *pDemo, const char *pDst, int StartTick, int EndTick, COperations *pOperations = nullptr);
	const char *Destination() const { return m_aDst; }
	bool Success() const { return m_Success; }
};
#endif
