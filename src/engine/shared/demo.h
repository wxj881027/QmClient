/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_DEMO_H
#define ENGINE_SHARED_DEMO_H

#include "snapshot.h"

#include <base/hash.h>
#include <base/system.h>

#include <engine/demo.h>
#include <engine/shared/protocol.h>

#include <functional>
#include <vector>

typedef std::function<void()> TUpdateIntraTimesFunc;

struct CSnapshotDelta;
class IConsole;
class IStorage;
namespace demo_output
{
	class COperations;
}

class CDemoRecorder : public IDemoRecorder
{
public:
	// 文件句柄由调用方管理；可替换的操作边界用于观察真实文件的读写失败。
	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual unsigned Read(IOHANDLE File, void *pData, unsigned Size) noexcept;
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept;
		virtual int Seek(IOHANDLE File, int64_t Offset, ESeekOrigin Origin) noexcept;
		virtual int64_t Length(IOHANDLE File) noexcept;
		virtual int Flush(IOHANDLE File) noexcept;
		virtual int Error(IOHANDLE File) noexcept;
		virtual int Close(IOHANDLE File) noexcept;
	};

private:
	class IConsole *m_pConsole = nullptr;
	class IStorage *m_pStorage = nullptr;
	// 借用操作对象，须覆盖记录器的使用期；nullptr 使用进程内默认操作。
	CFileOperations *m_pFileOperations = nullptr;
	bool m_WriteError = false;

	IOHANDLE m_File = nullptr;
	char m_aCurrentFilename[IO_MAX_PATH_LENGTH] = "";
	int m_LastTickMarker = -1;
	int m_LastKeyFrame = -1;
	int m_FirstTick = -1;

	CSnapshotBuffer m_LastSnapshotData;
	CSnapshotDelta *m_pSnapshotDelta = nullptr;

	int m_NumTimelineMarkers = 0;
	int m_aTimelineMarkers[MAX_TIMELINE_MARKERS];

	bool m_NoMapData = false;

	DEMOFUNC_FILTER m_pfnFilter = nullptr;
	void *m_pUser = nullptr;

	CFileOperations &FileOperations();
	void MarkWriteError() noexcept;
	bool WriteRaw(IOHANDLE File, const void *pData, unsigned Size);
	bool SeekRaw(IOHANDLE File, int64_t Offset);
	bool WriteTickMarker(int Tick, bool Keyframe);
	bool Write(int Type, const void *pData, int Size);

public:
	CDemoRecorder(CSnapshotDelta *pSnapshotDelta, bool NoMapData = false, CFileOperations *pFileOperations = nullptr);
	CDemoRecorder() = default;
	~CDemoRecorder() override;

	int Start(class IStorage *pStorage, class IConsole *pConsole, const char *pFilename, const char *pNetversion, const char *pMap, const SHA256_DIGEST &Sha256, unsigned MapCrc, const char *pType, unsigned MapSize, unsigned char *pMapData, IOHANDLE MapFile, DEMOFUNC_FILTER pfnFilter, void *pUser);
	int Stop(IDemoRecorder::EStopMode Mode, const char *pTargetFilename = "") override;

	bool AddDemoMarker();
	bool AddDemoMarker(int Tick);

	void RecordSnapshot(int Tick, const void *pData, int Size);
	void RecordSnapshot(int Tick, const void *pData, int Size, bool Keyframe);
	void RecordTickMarker(int Tick, bool Keyframe = false);
	void RecordMessage(const void *pData, int Size);

	bool IsRecording() const override { return m_File != nullptr; }
	bool HasWriteError() const override { return m_WriteError; }
	const char *CurrentFilename() const override { return m_aCurrentFilename; }

	int Length() const override { return (m_LastTickMarker - m_FirstTick) / SERVER_TICK_SPEED; }
};

class CDemoPlayer : public IDemoPlayer
{
public:
	// 内部文件处理不读写主线程的帧时间缓存。
	enum class ETimeMode
	{
		CACHED_FRAME,
		UNCACHED,
	};
	// 借用操作对象；成功返回的地图缓冲须与 free 兼容，所有权交给调用方。
	class CMapDataOperations
	{
	public:
		virtual ~CMapDataOperations() = default;
		virtual unsigned char *Allocate(unsigned Size) noexcept;
		virtual void Free(unsigned char *pData) noexcept;
		virtual int64_t Tell(IOHANDLE File) noexcept;
		virtual int Seek(IOHANDLE File, int64_t Offset) noexcept;
		virtual unsigned Read(IOHANDLE File, void *pData, unsigned Size) noexcept;
		virtual int Error(IOHANDLE File) noexcept;
	};

	class IListener
	{
	public:
		virtual ~IListener() = default;
		virtual void OnDemoPlayerSnapshot(void *pData, int Size) = 0;
		virtual void OnDemoPlayerMessage(void *pData, int Size) = 0;
	};

	class CPlaybackInfo
	{
	public:
		CDemoHeader m_Header;
		CTimelineMarkers m_TimelineMarkers;

		IDemoPlayer::CInfo m_Info;

		int64_t m_LastUpdate;
		int64_t m_LastScan;
		int64_t m_CurrentTime;

		int m_NextTick;
		int m_PreviousTick;

		float m_IntraTick;
		float m_IntraTickSincePrev;
		float m_TickTime;

		bool m_LiveStateUpdating;
		int m_LiveStateFailedCount;
		int m_LiveStateUnchangedCount;
	};

private:
	IListener *m_pListener;
	// 操作对象须覆盖播放器或同步裁剪的使用期；nullptr 使用默认实现。
	CMapDataOperations *m_pMapDataOperations = nullptr;
	CMapDataOperations &MapDataOperations();
	ETimeMode m_TimeMode = ETimeMode::CACHED_FRAME;
	int64_t SystemTime() const;

	TUpdateIntraTimesFunc m_UpdateIntraTimesFunc;

	// Playback
	class CKeyFrame
	{
	public:
		int64_t m_Filepos;
		int m_Tick;

		CKeyFrame(int64_t Filepos, int Tick) :
			m_Filepos(Filepos), m_Tick(Tick)
		{
		}
	};

	class IConsole *m_pConsole;
	IOHANDLE m_File;
	int64_t m_MapOffset;
	char m_aFilename[IO_MAX_PATH_LENGTH];
	char m_aErrorMessage[256];
	std::vector<CKeyFrame> m_vKeyFrames;
	CMapInfo m_MapInfo;
	int m_SpeedIndex;

	CPlaybackInfo m_Info;
	unsigned char m_aCompressedSnapshotData[CSnapshot::MAX_SIZE];
	unsigned char m_aDecompressedSnapshotData[CSnapshot::MAX_SIZE];

	// Depending on the chunk header
	// this is either a full CSnapshot or a CSnapshotDelta.
	unsigned char m_aChunkData[CSnapshot::MAX_SIZE];
	// Storage for the full snapshot
	// where the delta gets unpacked into.
	CSnapshotBuffer m_Snapshot;
	CSnapshotBuffer m_LastSnapshotData;
	int m_LastSnapshotDataSize;
	CSnapshotDelta *m_pSnapshotDelta;
	CSnapshotDelta *m_pSnapshotDeltaSixup;

	bool m_UseVideo;
#if defined(CONF_VIDEORECORDER)
	bool m_WasRecording = false;
#endif

	enum EReadChunkHeaderResult
	{
		CHUNKHEADER_SUCCESS,
		CHUNKHEADER_ERROR,
		CHUNKHEADER_EOF,
	};
	EReadChunkHeaderResult ReadChunkHeader(int *pType, int *pSize, int *pTick);
	void DoTick();
	enum class EScanFileResult
	{
		SUCCESS,
		ERROR_RECOVERABLE,
		ERROR_UNRECOVERABLE,
	};
	EScanFileResult ScanFile();
	void UpdateTimes();

	int64_t Time();
	bool m_Sixup;

	CSnapshotDelta *SnapshotDelta();
	void Construct(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, bool UseVideo);

public:
	CDemoPlayer(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, bool UseVideo, CMapDataOperations *pMapDataOperations = nullptr, ETimeMode TimeMode = ETimeMode::CACHED_FRAME);
	CDemoPlayer(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, bool UseVideo, TUpdateIntraTimesFunc &&UpdateIntraTimesFunc);
	~CDemoPlayer() override;

	void SetListener(IListener *pListener);

	int Load(class IStorage *pStorage, class IConsole *pConsole, const char *pFilename, int StorageType, bool ResetSliceMarkers = true);
	unsigned char *GetMapData(class IStorage *pStorage);
	// 操作对象为本次提取借用；默认使用原文件 I/O 和同目录提交。
	bool ExtractMap(class IStorage *pStorage, demo_output::COperations *pOutputOperations = nullptr, CDemoRecorder::CFileOperations *pFileOperations = nullptr) noexcept;
	void Play();
	void Pause() override;
	void Unpause() override;
	void Stop(const char *pErrorMessage = "");
	void SetSpeed(float Speed) override;
	void SetSpeedIndex(int SpeedIndex) override;
	void AdjustSpeedIndex(int Offset) override;
	bool SeekPercent(float Percent) override;
	bool SeekTime(float Seconds) override;
	bool SeekTick(ETickOffset TickOffset) override;
	bool SetPos(int WantedTick) override;
	const CInfo *BaseInfo() const override { return &m_Info.m_Info; }
	void GetDemoName(char *pBuffer, size_t BufferSize) const override;
	bool GetDemoInfo(class IStorage *pStorage, class IConsole *pConsole, const char *pFilename, int StorageType, CDemoHeader *pDemoHeader, CTimelineMarkers *pTimelineMarkers, CMapInfo *pMapInfo, IOHANDLE *pFile = nullptr, char *pErrorMessage = nullptr, size_t ErrorMessageSize = 0) const override;
	const char *Filename() const override { return m_aFilename; }
	const char *ErrorMessage() const override { return m_aErrorMessage; }

	void Update(bool RealTime = true);
	bool IsSixup() const { return m_Sixup; }

	const CPlaybackInfo *Info() const { return &m_Info; }
	bool IsPlaying() const override { return m_File != nullptr; }
	const CMapInfo *GetMapInfo() const { return &m_MapInfo; }
};

class CDemoEditor : public IDemoEditor
{
	IConsole *m_pConsole;
	IStorage *m_pStorage;
	CSnapshotDelta *m_pSnapshotDelta;
	CSnapshotDelta *m_pSnapshotDeltaSixup;
	CDemoPlayer::CMapDataOperations *m_pMapDataOperations = nullptr;
	demo_output::COperations *m_pOutputOperations = nullptr;
	CDemoRecorder::CFileOperations *m_pRecordOperations = nullptr;
	bool SliceImpl(const char *pDemo, const char *pDst, const std::vector<SDemoSliceSegment> &vSegments, DEMOFUNC_FILTER pfnFilter, void *pUser);

public:
	virtual void Init(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, class IConsole *pConsole, class IStorage *pStorage, CDemoPlayer::CMapDataOperations *pMapDataOperations = nullptr, demo_output::COperations *pOutputOperations = nullptr, CDemoRecorder::CFileOperations *pRecordOperations = nullptr);
	bool Slice(const char *pDemo, const char *pDst, int StartTick, int EndTick, DEMOFUNC_FILTER pfnFilter, void *pUser) noexcept override;
	bool Slice(const char *pDemo, const char *pDst, const std::vector<SDemoSliceSegment> &vSegments, DEMOFUNC_FILTER pfnFilter, void *pUser) noexcept override;
};

#endif
