/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/bytes.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#if defined(CONF_VIDEORECORDER)
#include <engine/shared/video.h>
#endif

#include "compression.h"
#include "demo.h"
#include "demo_output.h"
#include "network.h"
#include "snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

const CUuid SHA256_EXTENSION =
	{{0x6b, 0xe6, 0xda, 0x4a, 0xce, 0xbd, 0x38, 0x0c,
		0x9b, 0x5b, 0x12, 0x89, 0xc8, 0x42, 0xd7, 0x80}};

static const unsigned char gs_CurVersion = 6;
static const unsigned char gs_OldVersion = 3;
static const unsigned char gs_Sha256Version = 6;
static const unsigned char gs_VersionTickCompression = 5; // demo files with this version or higher will use `CHUNKTICKFLAG_TICK_COMPRESSED`

static constexpr LOG_COLOR DEMO_PRINT_COLOR = {191, 178, 178};

bool CDemoHeader::Valid() const
{
	// Check marker and ensure that strings are zero-terminated and valid UTF-8.
	return mem_comp(m_aMarker, gs_aHeaderMarker, sizeof(gs_aHeaderMarker)) == 0 &&
	       mem_has_null(m_aNetversion, sizeof(m_aNetversion)) && str_utf8_check(m_aNetversion) &&
	       mem_has_null(m_aMapName, sizeof(m_aMapName)) && str_utf8_check(m_aMapName) &&
	       mem_has_null(m_aType, sizeof(m_aType)) && str_utf8_check(m_aType) &&
	       mem_has_null(m_aTimestamp, sizeof(m_aTimestamp)) && str_utf8_check(m_aTimestamp);
}

unsigned CDemoRecorder::CFileOperations::Read(IOHANDLE File, void *pData, unsigned Size) noexcept { return io_read(File, pData, Size); }
unsigned CDemoRecorder::CFileOperations::Write(IOHANDLE File, const void *pData, unsigned Size) noexcept { return io_write(File, pData, Size); }
int CDemoRecorder::CFileOperations::Seek(IOHANDLE File, int64_t Offset, ESeekOrigin Origin) noexcept { return io_seek(File, Offset, Origin); }
int64_t CDemoRecorder::CFileOperations::Length(IOHANDLE File) noexcept { return io_length(File); }
int CDemoRecorder::CFileOperations::Flush(IOHANDLE File) noexcept { return io_flush(File); }
int CDemoRecorder::CFileOperations::Error(IOHANDLE File) noexcept { return io_error(File); }
int CDemoRecorder::CFileOperations::Close(IOHANDLE File) noexcept { return io_close(File); }

CDemoRecorder::CFileOperations &CDemoRecorder::FileOperations()
{
	static CFileOperations s_Operations;
	return m_pFileOperations ? *m_pFileOperations : s_Operations;
}

CDemoRecorder::CDemoRecorder(CSnapshotDelta *pSnapshotDelta, bool NoMapData, CFileOperations *pFileOperations) :
	m_pFileOperations(pFileOperations), m_pSnapshotDelta(pSnapshotDelta), m_NoMapData(NoMapData)
{
}

void CDemoRecorder::MarkWriteError() noexcept
{
	if(m_WriteError)
		return;
	m_WriteError = true;
	try
	{
		log_error("demo_recorder", "Demo file I/O failed; the recording cannot be saved as complete");
	}
	catch(...)
	{
	}
}

bool CDemoRecorder::WriteRaw(IOHANDLE File, const void *pData, unsigned Size)
{
	if(!File || m_WriteError)
		return false;
	if(FileOperations().Write(File, pData, Size) != Size || FileOperations().Error(File) != 0)
		MarkWriteError();
	return !m_WriteError;
}

bool CDemoRecorder::SeekRaw(IOHANDLE File, int64_t Offset)
{
	if(m_WriteError)
		return false;
	if(FileOperations().Seek(File, Offset, IOSEEK_START) != 0 || FileOperations().Error(File) != 0)
		MarkWriteError();
	return !m_WriteError;
}

CDemoRecorder::~CDemoRecorder()
{
	dbg_assert(m_File == 0, "Demo recorder was not stopped");
}

// Record
int CDemoRecorder::Start(class IStorage *pStorage, const char *pFilename, const char *pNetVersion, const char *pMap, const SHA256_DIGEST &Sha256, unsigned Crc, const char *pType, unsigned MapSize, unsigned char *pMapData, IOHANDLE MapFile, DEMOFUNC_FILTER pfnFilter, void *pUser) try
{
	dbg_assert(m_File == 0, "Demo recorder already recording");

	m_pStorage = pStorage;
	m_WriteError = false;

	if(!str_valid_filename(fs_filename(pFilename)))
	{
		log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "The name '%s' cannot be used for demos because not all platforms support it", pFilename);
		return -1;
	}

	// 新文件直接录制；已有同名录像要等地图和文件头准备好才替换。
	std::optional<demo_output::CTransaction> Replacement;
	IOHANDLE DemoFile = pStorage->OpenFile(pFilename, IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
	if(!DemoFile && pStorage->FileExists(pFilename, IStorage::TYPE_SAVE_OR_ABSOLUTE))
	{
		Replacement.emplace();
		if(Replacement->Prepare(*pStorage, pFilename))
			DemoFile = pStorage->OpenFile(Replacement->Filename(), IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
	}
	if(!DemoFile)
	{
		log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Unable to open '%s' for recording", pFilename);
		return -1;
	}

	const auto CloseOutput = [this](void *pFile) noexcept {
		if(FileOperations().Close(pFile) != 0)
			MarkWriteError();
	};
	std::unique_ptr<void, decltype(CloseOutput)> DemoGuard(DemoFile, CloseOutput);
	bool CloseMapFile = false;
	const auto ReleaseMap = [this, &CloseMapFile](void *pFile) noexcept {
		if(CloseMapFile ? FileOperations().Close(pFile) != 0 : FileOperations().Seek(pFile, 0, IOSEEK_START) != 0)
			MarkWriteError();
	};
	std::unique_ptr<void, decltype(ReleaseMap)> MapGuard(MapFile, ReleaseMap);

	if(MapFile && !SeekRaw(MapFile, 0))
		return -1;

	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(Sha256, aSha256, sizeof(aSha256));

	if(!pMapData && !MapFile)
	{
		// open mapfile
		char aMapFilename[IO_MAX_PATH_LENGTH];
		// try the downloaded maps
		str_format(aMapFilename, sizeof(aMapFilename), "downloadedmaps/%s_%s.map", pMap, aSha256);
		MapFile = pStorage->OpenFile(aMapFilename, IOFLAG_READ, IStorage::TYPE_ALL);
		if(!MapFile)
		{
			// try the normal maps folder
			str_format(aMapFilename, sizeof(aMapFilename), "maps/%s.map", pMap);
			MapFile = pStorage->OpenFile(aMapFilename, IOFLAG_READ, IStorage::TYPE_ALL);
		}
		if(!MapFile)
		{
			// search for the map within subfolders
			char aBuf[IO_MAX_PATH_LENGTH];
			str_format(aMapFilename, sizeof(aMapFilename), "%s.map", pMap);
			if(pStorage->FindFile(aMapFilename, "maps", IStorage::TYPE_ALL, aBuf, sizeof(aBuf)))
				MapFile = pStorage->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		}
		if(!MapFile)
		{
			log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Unable to open mapfile '%s'", pMap);
			return -1;
		}

		CloseMapFile = true;
		MapGuard.reset(MapFile);
	}

	if(m_NoMapData)
	{
		MapSize = 0;
	}
	else if(MapFile)
	{
		const int64_t MapFileSize = FileOperations().Length(MapFile);
		if(MapFileSize < 0)
		{
			MarkWriteError();
			return -1;
		}
		if(MapFileSize > (int64_t)std::numeric_limits<unsigned>::max())
		{
			MapSize = 0;
			log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Mapfile '%s' too large for demo, recording without it", pMap);
		}
		else
		{
			MapSize = MapFileSize;
		}
	}

	// write header
	CDemoHeader Header;
	mem_zero(&Header, sizeof(Header));
	mem_copy(Header.m_aMarker, gs_aHeaderMarker, sizeof(Header.m_aMarker));
	Header.m_Version = gs_CurVersion;
	str_copy(Header.m_aNetversion, pNetVersion);
	str_copy(Header.m_aMapName, pMap);
	uint_to_bytes_be(Header.m_aMapSize, MapSize);
	uint_to_bytes_be(Header.m_aMapCrc, Crc);
	str_copy(Header.m_aType, pType);
	// Header.m_Length - add this on stop
	str_timestamp(Header.m_aTimestamp, sizeof(Header.m_aTimestamp));
	if(!WriteRaw(DemoFile, &Header, sizeof(Header)))
		return -1;

	CTimelineMarkers TimelineMarkers;
	mem_zero(&TimelineMarkers, sizeof(TimelineMarkers));
	if(!WriteRaw(DemoFile, &TimelineMarkers, sizeof(TimelineMarkers)) || // 停止时填入标记。
		!WriteRaw(DemoFile, SHA256_EXTENSION.m_aData, sizeof(SHA256_EXTENSION.m_aData)) ||
		!WriteRaw(DemoFile, &Sha256, sizeof(SHA256_DIGEST)))
		return -1;

	if(MapSize != 0 && pMapData)
	{
		if(!WriteRaw(DemoFile, pMapData, MapSize))
			return -1;
	}
	else if(MapSize != 0)
	{
		unsigned Remaining = MapSize;
		while(Remaining > 0)
		{
			unsigned char aChunk[1024 * 64];
			const unsigned Size = std::min<unsigned>(Remaining, sizeof(aChunk));
			if(FileOperations().Read(MapFile, aChunk, Size) != Size || FileOperations().Error(MapFile) != 0)
			{
				MarkWriteError();
				return -1;
			}
			if(!WriteRaw(DemoFile, aChunk, Size))
				return -1;
			Remaining -= Size;
		}
	}
	MapGuard.reset();
	if(m_WriteError)
		return -1;

	m_LastKeyFrame = -1;
	m_LastTickMarker = -1;
	m_FirstTick = -1;
	m_NumTimelineMarkers = 0;

	log_info_color(DEMO_PRINT_COLOR, "demo_recorder", "Recording to '%s'", pFilename);

	m_pfnFilter = pfnFilter;
	m_pUser = pUser;

	if(Replacement && (FileOperations().Flush(DemoFile) != 0 || FileOperations().Error(DemoFile) != 0 || !Replacement->Publish()))
	{
		MarkWriteError();
		return -1;
	}
	m_File = DemoGuard.release();
	str_copy(m_aCurrentFilename, pFilename);

	return 0;
}
catch(...)
{
	MarkWriteError();
	return -1;
}

/*
	Tickmarker
		7	= Always set
		6	= Keyframe flag
		0-5	= Delta tick

	Normal
		7 = Not set
		5-6	= Type
		0-4	= Size
*/

enum
{
	CHUNKTYPEFLAG_TICKMARKER = 0x80,
	CHUNKTICKFLAG_KEYFRAME = 0x40, // only when tickmarker is set
	CHUNKTICKFLAG_TICK_COMPRESSED = 0x20, // when we store the tick value in the first chunk

	CHUNKMASK_TICK = 0x1f,
	CHUNKMASK_TICK_LEGACY = 0x3f,
	CHUNKMASK_TYPE = 0x60,
	CHUNKMASK_SIZE = 0x1f,

	CHUNKTYPE_SNAPSHOT = 1,
	CHUNKTYPE_MESSAGE = 2,
	CHUNKTYPE_DELTA = 3,
};

bool CDemoRecorder::WriteTickMarker(int Tick, bool Keyframe)
{
	if(!m_File || m_WriteError)
		return false;
	if(m_LastTickMarker == -1 || Tick - m_LastTickMarker > CHUNKMASK_TICK || Keyframe)
	{
		unsigned char aChunk[sizeof(int32_t) + 1];
		aChunk[0] = CHUNKTYPEFLAG_TICKMARKER;
		uint_to_bytes_be(aChunk + 1, Tick);

		if(Keyframe)
			aChunk[0] |= CHUNKTICKFLAG_KEYFRAME;

		if(!WriteRaw(m_File, aChunk, sizeof(aChunk)))
			return false;
	}
	else
	{
		unsigned char aChunk[1];
		aChunk[0] = CHUNKTYPEFLAG_TICKMARKER | CHUNKTICKFLAG_TICK_COMPRESSED | (Tick - m_LastTickMarker);
		if(!WriteRaw(m_File, aChunk, sizeof(aChunk)))
			return false;
	}

	m_LastTickMarker = Tick;
	if(m_FirstTick < 0)
		m_FirstTick = Tick;
	return true;
}

bool CDemoRecorder::Write(int Type, const void *pData, int Size)
{
	if(!m_File || m_WriteError)
		return false;

	// 官方 be3e5e6a3：负长度是上游 create delta 的失败返回值，必须在这里拒绝，
	// 否则会以 size_t 参与 mem_copy。
	if(Size < 0)
	{
		log_error("demo_recorder", "Dropped chunk of type %d, invalid size %d", Type, Size);
		return false;
	}

	if(Size > 64 * 1024)
	{
		log_error("demo_recorder", "Dropped chunk of type %d, size %d is too large", Type, Size);
		return false;
	}

	/* pad the data with 0 so we get an alignment of 4,
	else the compression won't work and miss some bytes */
	char aBuffer[64 * 1024];
	char aBuffer2[64 * 1024];
	mem_copy(aBuffer2, pData, Size);
	while(Size & 3)
		aBuffer2[Size++] = 0;
	Size = CVariableInt::Compress(aBuffer2, Size, aBuffer, sizeof(aBuffer)); // buffer2 -> buffer
	if(Size < 0)
		return false;

	Size = CNetBase::Compress(aBuffer, Size, aBuffer2, sizeof(aBuffer2)); // buffer -> buffer2
	if(Size < 0)
		return false;

	unsigned char aChunk[3];
	aChunk[0] = ((Type & 0x3) << 5);
	if(Size < 30)
	{
		aChunk[0] |= Size;
		if(!WriteRaw(m_File, aChunk, 1))
			return false;
	}
	else
	{
		if(Size < 256)
		{
			aChunk[0] |= 30;
			aChunk[1] = Size & 0xff;
			if(!WriteRaw(m_File, aChunk, 2))
				return false;
		}
		else
		{
			aChunk[0] |= 31;
			aChunk[1] = Size & 0xff;
			aChunk[2] = Size >> 8;
			if(!WriteRaw(m_File, aChunk, 3))
				return false;
		}
	}

	return WriteRaw(m_File, aBuffer2, Size);
}

void CDemoRecorder::RecordSnapshot(int Tick, const void *pData, int Size)
{
	RecordSnapshot(Tick, pData, Size, false);
}

void CDemoRecorder::RecordSnapshot(int Tick, const void *pData, int Size, bool Keyframe)
{
	if(Keyframe || m_LastKeyFrame == -1 || (Tick - m_LastKeyFrame) > SERVER_TICK_SPEED * 5)
	{
		// write full tickmarker
		if(!WriteTickMarker(Tick, true))
			return;

		// write snapshot。官方 be3e5e6a3：只有 chunk 真正写进文件才推进 delta 基准，
		// 否则回放会用没写进去的快照去解后续 delta。
		if(!Write(CHUNKTYPE_SNAPSHOT, pData, Size))
			return;

		m_LastKeyFrame = Tick;
		mem_copy(&m_LastSnapshotData, pData, Size);
	}
	else
	{
		// write tickmarker
		if(!WriteTickMarker(Tick, false))
			return;

		// create delta
		CSnapshotDeltaBuffer DeltaData;
		const int DeltaSize = m_pSnapshotDelta->CreateDelta(m_LastSnapshotData.AsSnapshot(), (CSnapshot *)pData, DeltaData.Data(), sizeof(DeltaData.m_aData));
		if(DeltaSize > 0)
		{
			// record delta
			if(Write(CHUNKTYPE_DELTA, DeltaData.m_aData, DeltaSize))
				mem_copy(&m_LastSnapshotData, pData, Size);
		}
		else if(DeltaSize < 0)
		{
			log_error("demo_recorder", "Failed to create delta for tick %d, dropping snapshot", Tick);
		}
	}
}

void CDemoRecorder::RecordTickMarker(int Tick, bool Keyframe)
{
	WriteTickMarker(Tick, Keyframe);
}

void CDemoRecorder::RecordMessage(const void *pData, int Size)
{
	if(m_pfnFilter)
	{
		if(m_pfnFilter(pData, Size, m_pUser))
		{
			return;
		}
	}
	Write(CHUNKTYPE_MESSAGE, pData, Size);
}

namespace
{
	bool IsCapturedFile(IOHANDLE Original, const char *pPath)
	{
		std::unique_ptr<void, decltype(&io_close)> Other(io_open_read_regular_checked(pPath, nullptr), &io_close);
		if(!Other)
			return false;
#if defined(CONF_FAMILY_WINDOWS)
		BY_HANDLE_FILE_INFORMATION A{}, B{};
		const auto Info = [](IOHANDLE File, BY_HANDLE_FILE_INFORMATION &Result) {
			const HANDLE Handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(static_cast<FILE *>(File))));
			if(GetFileInformationByHandle(Handle, &Result))
				return true;
			// 录制流可能只有写入权限，按同一个文件对象补开属性句柄。
			const HANDLE Attributes = ReOpenFile(Handle, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
			if(Attributes == INVALID_HANDLE_VALUE)
				return false;
			const bool Read = GetFileInformationByHandle(Attributes, &Result) != 0;
			CloseHandle(Attributes);
			return Read;
		};
		return Info(Original, A) && Info(Other.get(), B) && (A.nFileIndexHigh || A.nFileIndexLow) &&
		       A.dwVolumeSerialNumber == B.dwVolumeSerialNumber && A.nFileIndexHigh == B.nFileIndexHigh && A.nFileIndexLow == B.nFileIndexLow;
#else
		struct stat A{}, B{};
		return fstat(fileno(static_cast<FILE *>(Original)), &A) == 0 && fstat(fileno(static_cast<FILE *>(Other.get())), &B) == 0 &&
		       A.st_dev == B.st_dev && A.st_ino == B.st_ino;
#endif
	}
} // namespace

bool demo_output::CCapturedSource::Claim(IOHANDLE Original, const char *pSource)
{
	if(!IsCapturedFile(Original, pSource))
		return false;
	m_Original = std::filesystem::u8path(pSource);
	std::error_code Error;
	const std::string OriginalName(pSource);
	const size_t NameStart = OriginalName.find_last_of("/\\") + 1;
	// 超长文件名也要能停录；同目录的短名避免超出单段或完整路径限制。
	const std::string Prefix = OriginalName.size() - NameStart > 240 || OriginalName.size() + sizeof(".saving.63/source.demo") > IO_MAX_PATH_LENGTH ?
		OriginalName.substr(0, NameStart) + ".qm-replay" : OriginalName + ".saving";
	for(int Attempt = 0; Attempt < 64; ++Attempt)
	{
		const auto Directory = Attempt ? Prefix + "." + std::to_string(Attempt) : Prefix;
		m_Filename = Directory + "/source.demo";
		if(m_Filename.size() >= IO_MAX_PATH_LENGTH)
			return false;
		m_Directory = std::filesystem::u8path(Directory);
		m_Private = std::filesystem::u8path(m_Filename);
		m_OwnDirectory = std::filesystem::create_directory(m_Directory, Error);
		if(m_OwnDirectory || (Error && Error != std::errc::file_exists))
			break;
	}
	if(!m_OwnDirectory)
		return false;
	std::filesystem::rename(m_Original, m_Private, Error);
	if(Error)
		return false;
	m_Restore = true;
	// 检查与移动之间也可能换入新文件，必须对已接管的目录项再核对一次。
	return IsCapturedFile(Original, Filename());
}

demo_output::CCapturedSource::~CCapturedSource() noexcept
{
	if(!m_OwnDirectory)
		return;
	try
	{
		std::error_code Error;
		if(m_Restore)
		{
			// 原名可被复用，恢复时也只允许创建；恢复失败则保留可找回的源录像。
			std::filesystem::create_hard_link(m_Private, m_Original, Error);
			if(Error && Error != std::errc::file_exists)
				std::filesystem::copy_file(m_Private, m_Original, std::filesystem::copy_options::none, Error);
			if(Error)
			{
				log_warn("replay", "Source recording retained at '%s'", Filename());
				return;
			}
			std::filesystem::remove(m_Private, Error);
		}
		std::filesystem::remove(m_Directory, Error);
	}
	catch(...)
	{
	}
}

int CDemoRecorder::Stop(IDemoRecorder::EStopMode Mode, const char *pTargetFilename)
{
	if(!m_File)
		return -1;

	if(Mode == IDemoRecorder::EStopMode::KEEP_FILE && !m_WriteError)
	{
		// add the demo length to the header
		unsigned char aLength[sizeof(int32_t)];
		uint_to_bytes_be(aLength, Length());
		if(SeekRaw(m_File, offsetof(CDemoHeader, m_aLength)))
			WriteRaw(m_File, aLength, sizeof(aLength));

		// add the timeline markers to the header
		unsigned char aNumMarkers[sizeof(int32_t)];
		uint_to_bytes_be(aNumMarkers, m_NumTimelineMarkers);
		if(SeekRaw(m_File, sizeof(CDemoHeader) + offsetof(CTimelineMarkers, m_aNumTimelineMarkers)))
			WriteRaw(m_File, aNumMarkers, sizeof(aNumMarkers));
		for(int i = 0; i < m_NumTimelineMarkers && !m_WriteError; i++)
		{
			unsigned char aMarker[sizeof(int32_t)];
			uint_to_bytes_be(aMarker, m_aTimelineMarkers[i]);
			WriteRaw(m_File, aMarker, sizeof(aMarker));
		}
	}

	demo_output::CCapturedSource Source;
	std::string Destination;
	const bool Remove = Mode == IDemoRecorder::EStopMode::REMOVE_FILE;
	const bool Rename = !Remove && !m_WriteError && pTargetFilename[0] != '\0';
	bool Claimed = false;
	bool SameFile = false;
	if(Remove || Rename)
	{
		try
		{
			std::string Original;
			if(demo_output::Resolve(*m_pStorage, m_aCurrentFilename, Original) &&
				(!Rename || demo_output::Resolve(*m_pStorage, pTargetFilename, Destination)))
			{
				std::error_code Error;
				SameFile = Rename && (Original == Destination || std::filesystem::equivalent(std::filesystem::u8path(Original), std::filesystem::u8path(Destination), Error));
				Claimed = SameFile ? IsCapturedFile(m_File, Original.c_str()) : Source.Claim(m_File, Original.c_str());
			}
		}
		catch(...)
		{
			Claimed = false;
		}
	}
	const IOHANDLE File = std::exchange(m_File, nullptr);
	if(FileOperations().Flush(File) != 0)
		MarkWriteError();
	if(FileOperations().Error(File) != 0)
		MarkWriteError();
	if(FileOperations().Close(File) != 0)
		MarkWriteError();

	if((Remove || Rename) && !Claimed)
		return -1;
	if(Remove)
	{
		if(!m_pStorage->RemoveFile(Source.Filename(), IStorage::TYPE_ABSOLUTE))
		{
			log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Could not remove demo file '%s'.", m_aCurrentFilename);
			return -1;
		}
		Source.Removed();
	}
	else if(m_WriteError)
	{
		// 关闭并不代表写入完整；损坏的源文件不能移动到成功录像的位置。
		return -1;
	}
	else if(Rename && !SameFile)
	{
		if(!m_pStorage->RenameFile(Source.Filename(), Destination.c_str(), IStorage::TYPE_ABSOLUTE, false))
		{
			log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Could not move demo file '%s' to '%s'.", m_aCurrentFilename, pTargetFilename);
			return -1;
		}
		Source.Removed();
	}

	if(m_WriteError)
		return -1;
	log_info_color(DEMO_PRINT_COLOR, "demo_recorder", "Stopped recording to '%s'", m_aCurrentFilename);

	return 0;
}

bool CDemoRecorder::AddDemoMarker()
{
	if(m_LastTickMarker < 0)
		return false;
	return AddDemoMarker(m_LastTickMarker);
}

bool CDemoRecorder::AddDemoMarker(int Tick)
{
	dbg_assert(Tick >= m_FirstTick && Tick <= m_LastTickMarker, "Invalid marker tick: %d", Tick);

	if(m_NumTimelineMarkers >= MAX_TIMELINE_MARKERS)
	{
		log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Too many timeline markers");
		return false;
	}

	// not more than 1 marker in a second
	if(m_NumTimelineMarkers > 0)
	{
		const int Diff = Tick - m_aTimelineMarkers[m_NumTimelineMarkers - 1];
		if(Diff < (float)SERVER_TICK_SPEED)
		{
			log_error_color(DEMO_PRINT_COLOR, "demo_recorder", "Previous timeline marker too close");
			return false;
		}
	}

	m_aTimelineMarkers[m_NumTimelineMarkers++] = Tick;

	log_info_color(DEMO_PRINT_COLOR, "demo_recorder", "Added timeline marker");
	return true;
}

CSnapshotDelta *CDemoPlayer::SnapshotDelta()
{
	if(IsSixup())
	{
		return m_pSnapshotDeltaSixup;
	}
	return m_pSnapshotDelta;
}

void CDemoPlayer::Construct(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, bool UseVideo)
{
	m_File = nullptr;
	m_SpeedIndex = DEMO_SPEED_INDEX_DEFAULT;

	m_pSnapshotDelta = pSnapshotDelta;
	m_pSnapshotDeltaSixup = pSnapshotDeltaSixup;
	m_LastSnapshotDataSize = -1;
	m_pListener = nullptr;
	m_UseVideo = UseVideo;

	m_aFilename[0] = '\0';
	m_aErrorMessage[0] = '\0';
}

CDemoPlayer::CDemoPlayer(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, bool UseVideo, TUpdateIntraTimesFunc &&UpdateIntraTimesFunc) :
	CDemoPlayer(pSnapshotDelta, pSnapshotDeltaSixup, UseVideo)
{
	m_UpdateIntraTimesFunc = UpdateIntraTimesFunc;
}

unsigned char *CDemoPlayer::CMapDataOperations::Allocate(unsigned Size) noexcept { return static_cast<unsigned char *>(std::malloc(Size)); }
void CDemoPlayer::CMapDataOperations::Free(unsigned char *pData) noexcept { std::free(pData); }
int64_t CDemoPlayer::CMapDataOperations::Tell(IOHANDLE File) noexcept { return io_tell(File); }
int CDemoPlayer::CMapDataOperations::Seek(IOHANDLE File, int64_t Offset) noexcept { return io_seek(File, Offset, IOSEEK_START); }
unsigned CDemoPlayer::CMapDataOperations::Read(IOHANDLE File, void *pData, unsigned Size) noexcept { return io_read(File, pData, Size); }
unsigned CDemoPlayer::CMapDataOperations::ReadChunkHeaderBytes(IOHANDLE File, void *pData, unsigned Size) noexcept { return io_read(File, pData, Size); }
int CDemoPlayer::CMapDataOperations::Error(IOHANDLE File) noexcept { return io_error(File); }

CDemoPlayer::CMapDataOperations &CDemoPlayer::MapDataOperations()
{
	static CMapDataOperations s_Operations;
	return m_pMapDataOperations ? *m_pMapDataOperations : s_Operations;
}

void *CDemoPlayer::CKeyFrameMemory::Allocate(size_t Bytes) { return ::operator new(Bytes); }
void CDemoPlayer::CKeyFrameMemory::Free(void *pMemory, size_t) noexcept { ::operator delete(pMemory); }

CDemoPlayer::CDemoPlayer(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, bool UseVideo, CMapDataOperations *pMapDataOperations, ETimeMode TimeMode, CKeyFrameMemory *pKeyFrameMemory) :
	m_pMapDataOperations(pMapDataOperations), m_TimeMode(TimeMode), m_vKeyFrames(CKeyFrameAllocator<CKeyFrame>(pKeyFrameMemory))
{
	Construct(pSnapshotDelta, pSnapshotDeltaSixup, UseVideo);
}

CDemoPlayer::~CDemoPlayer()
{
	dbg_assert(m_File == 0, "Demo player not stopped");
}

void CDemoPlayer::FailLoad(const char *pErrorMessage) noexcept
{
	const IOHANDLE File = std::exchange(m_File, nullptr);
	if(File)
		io_close(File);
	{
		// 分配失败后释放已准备的索引容量，不能继续占住导致失败的内存。
		decltype(m_vKeyFrames) Empty(m_vKeyFrames.get_allocator());
		m_vKeyFrames.swap(Empty);
	}
	str_copy(m_aFilename, "");
	str_copy(m_aErrorMessage, pErrorMessage);
#if defined(CONF_VIDEORECORDER)
	m_WasRecording = false;
	if(m_UseVideo && IVideo::Current())
	{
		try
		{
			IVideo::Current()->Stop();
		}
		catch(...)
		{
		}
	}
#endif
}

void CDemoPlayer::SetListener(IListener *pListener)
{
	m_pListener = pListener;
}

CDemoPlayer::EReadChunkHeaderResult CDemoPlayer::ReadChunkHeader(int *pType, int *pSize, int *pTick)
{
	*pSize = 0;
	*pType = 0;

	const auto Read = [this](void *pData, unsigned Size, bool AtChunkStart = false) {
		const unsigned Bytes = m_pMapDataOperations ? m_pMapDataOperations->ReadChunkHeaderBytes(m_File, pData, Size) : io_read(m_File, pData, Size);
		if(Bytes == Size)
			return CHUNKHEADER_SUCCESS;
		if(m_pMapDataOperations ? m_pMapDataOperations->Error(m_File) : io_error(m_File))
			return CHUNKHEADER_IO_ERROR;
		return AtChunkStart ? CHUNKHEADER_EOF : CHUNKHEADER_ERROR;
	};

	unsigned char Chunk = 0;
	if(const auto Result = Read(&Chunk, sizeof(Chunk), true); Result != CHUNKHEADER_SUCCESS)
		return Result;

	if(Chunk & CHUNKTYPEFLAG_TICKMARKER)
	{
		// decode tick marker
		int TickdeltaLegacy = Chunk & CHUNKMASK_TICK_LEGACY; // compatibility
		*pType = Chunk & (CHUNKTYPEFLAG_TICKMARKER | CHUNKTICKFLAG_KEYFRAME);

		int NewTick;
		if(m_Info.m_Header.m_Version < gs_VersionTickCompression && TickdeltaLegacy != 0)
		{
			if(*pTick < 0) // initial tick not initialized before a tick delta
				return CHUNKHEADER_ERROR;
			NewTick = *pTick + TickdeltaLegacy;
		}
		else if(Chunk & CHUNKTICKFLAG_TICK_COMPRESSED)
		{
			if(*pTick < 0) // initial tick not initialized before a tick delta
				return CHUNKHEADER_ERROR;
			int Tickdelta = Chunk & CHUNKMASK_TICK;
			NewTick = *pTick + Tickdelta;
		}
		else
		{
			unsigned char aTickdata[sizeof(int32_t)];
			if(const auto Result = Read(aTickdata, sizeof(aTickdata)); Result != CHUNKHEADER_SUCCESS)
				return Result;
			NewTick = bytes_be_to_uint(aTickdata);
		}
		if(NewTick < MIN_TICK || NewTick >= MAX_TICK) // invalid tick
			return CHUNKHEADER_ERROR;
		*pTick = NewTick;
	}
	else
	{
		// decode normal chunk
		*pType = (Chunk & CHUNKMASK_TYPE) >> 5;
		*pSize = Chunk & CHUNKMASK_SIZE;

		if(*pSize == 30)
		{
			unsigned char aSizedata[1];
			if(const auto Result = Read(aSizedata, sizeof(aSizedata)); Result != CHUNKHEADER_SUCCESS)
				return Result;
			*pSize = aSizedata[0];
		}
		else if(*pSize == 31)
		{
			unsigned char aSizedata[2];
			if(const auto Result = Read(aSizedata, sizeof(aSizedata)); Result != CHUNKHEADER_SUCCESS)
				return Result;
			*pSize = (aSizedata[1] << 8) | aSizedata[0];
		}
	}

	return CHUNKHEADER_SUCCESS;
}

CDemoPlayer::EScanFileResult CDemoPlayer::ScanFile()
{
	const int64_t StartPos = io_tell(m_File);
	if(StartPos < 0)
	{
		return EScanFileResult::ERROR_UNRECOVERABLE;
	}

	const auto &ResetToStartPosition = [&](EScanFileResult Result) -> EScanFileResult {
		// Cannot play or seek without at least one keyframe, also when the scan stopped early
		if(io_seek(m_File, StartPos, IOSEEK_START) != 0 || m_vKeyFrames.empty())
		{
			m_vKeyFrames.clear();
			return EScanFileResult::ERROR_UNRECOVERABLE;
		}
		return Result;
	};

	int ChunkTick = -1;
	if(!m_vKeyFrames.empty())
	{
		if(io_seek(m_File, m_vKeyFrames.back().m_Filepos, IOSEEK_START) != 0)
		{
			return ResetToStartPosition(EScanFileResult::ERROR_RECOVERABLE);
		}
		int ChunkType, ChunkSize;
		const EReadChunkHeaderResult Result = ReadChunkHeader(&ChunkType, &ChunkSize, &ChunkTick);
		if(Result == CHUNKHEADER_IO_ERROR)
			return EScanFileResult::ERROR_UNRECOVERABLE;
		if(Result != CHUNKHEADER_SUCCESS ||
			(ChunkSize > 0 && io_skip(m_File, ChunkSize) != 0))
		{
			return ResetToStartPosition(EScanFileResult::ERROR_RECOVERABLE);
		}
	}

	while(true)
	{
		const int64_t CurrentPos = io_tell(m_File);
		if(CurrentPos < 0)
		{
			return ResetToStartPosition(EScanFileResult::ERROR_RECOVERABLE);
		}

		int ChunkType, ChunkSize;
		const EReadChunkHeaderResult Result = ReadChunkHeader(&ChunkType, &ChunkSize, &ChunkTick);
		if(Result == CHUNKHEADER_EOF)
		{
			break;
		}
		else if(Result == CHUNKHEADER_ERROR)
		{
			return ResetToStartPosition(EScanFileResult::ERROR_RECOVERABLE);
		}
		else if(Result == CHUNKHEADER_IO_ERROR)
		{
			return EScanFileResult::ERROR_UNRECOVERABLE;
		}

		if(ChunkType & CHUNKTYPEFLAG_TICKMARKER)
		{
			if(ChunkType & CHUNKTICKFLAG_KEYFRAME)
			{
				try
				{
					m_vKeyFrames.emplace_back(CurrentPos, ChunkTick);
				}
				catch(const std::bad_alloc &)
				{
					FailLoad("Not enough memory to load demo");
					return EScanFileResult::ERROR_UNRECOVERABLE;
				}
				catch(const std::length_error &)
				{
					FailLoad("Demo keyframe index is too large");
					return EScanFileResult::ERROR_UNRECOVERABLE;
				}
				catch(...)
				{
					FailLoad("Error loading demo");
					return EScanFileResult::ERROR_UNRECOVERABLE;
				}
			}
			if(m_Info.m_Info.m_FirstTick == -1)
			{
				m_Info.m_Info.m_FirstTick = ChunkTick;
			}
			m_Info.m_Info.m_LastTick = ChunkTick;
		}
		else if(ChunkSize)
		{
			if(io_skip(m_File, ChunkSize) != 0)
			{
				return ResetToStartPosition(EScanFileResult::ERROR_RECOVERABLE);
			}
		}
	}

	return ResetToStartPosition(EScanFileResult::SUCCESS);
}

bool CDemoPlayer::DoTick()
{
	// update ticks
	m_Info.m_PreviousTick = m_Info.m_Info.m_CurrentTick;
	m_Info.m_Info.m_CurrentTick = m_Info.m_NextTick;
	int ChunkTick = m_Info.m_Info.m_CurrentTick;

	UpdateTimes();

	bool GotSnapshot = false;
	while(true)
	{
		int ChunkType, ChunkSize;
		const EReadChunkHeaderResult Result = ReadChunkHeader(&ChunkType, &ChunkSize, &ChunkTick);
		if(Result == CHUNKHEADER_EOF)
		{
			if(m_Info.m_PreviousTick == -1)
			{
				Stop("Empty demo");
			}
			else
			{
				Pause();
				// Stop rendering when reaching end of file
#if defined(CONF_VIDEORECORDER)
				if(m_UseVideo && IVideo::Current())
					Stop();
#endif
			}
			break;
		}
		else if(Result != CHUNKHEADER_SUCCESS)
		{
			Stop("Error reading chunk header");
			break;
		}

		// read the chunk
		int DataSize = 0;
		if(ChunkSize)
		{
			if(io_read(m_File, m_aCompressedSnapshotData, ChunkSize) != (unsigned)ChunkSize)
			{
				Stop("Error reading chunk data");
				break;
			}

			DataSize = CNetBase::Decompress(m_aCompressedSnapshotData, ChunkSize, m_aDecompressedSnapshotData, sizeof(m_aDecompressedSnapshotData));
			if(DataSize < 0)
			{
				Stop("Error during network decompression");
				break;
			}

			DataSize = CVariableInt::Decompress(m_aDecompressedSnapshotData, DataSize, m_aChunkData, sizeof(m_aChunkData));
			if(DataSize < 0)
			{
				Stop("Error during intpack decompression");
				break;
			}
		}

		if(ChunkType == CHUNKTYPE_DELTA)
		{
			if(m_LastSnapshotDataSize == -1)
			{
				Stop("Delta snapshot before any full snapshot");
				break;
			}

			// process delta snapshot
			DataSize = SnapshotDelta()->UnpackDelta(m_LastSnapshotData.AsSnapshot(), &m_Snapshot, m_aChunkData, DataSize);

			if(DataSize < 0)
			{
				log_debug_color(DEMO_PRINT_COLOR, "demo_player", "Error unpacking snapshot delta. DataSize=%d", DataSize);
			}
			else if(!m_Snapshot.AsSnapshot()->IsValid(DataSize))
			{
				log_debug_color(DEMO_PRINT_COLOR, "demo_player", "Snapshot delta invalid. DataSize=%d", DataSize);
			}
			else
			{
				if(m_pListener)
					m_pListener->OnDemoPlayerSnapshot(m_Snapshot.AsSnapshot(), DataSize);

				m_LastSnapshotDataSize = DataSize;
				mem_copy(&m_LastSnapshotData, &m_Snapshot, DataSize);
				GotSnapshot = true;
			}
		}
		else if(ChunkType == CHUNKTYPE_SNAPSHOT)
		{
			// process full snapshot
			CSnapshot *pSnap = (CSnapshot *)m_aChunkData;
			if(!pSnap->IsValid(DataSize))
			{
				log_debug_color(DEMO_PRINT_COLOR, "demo_player", "Snapshot invalid. DataSize=%d", DataSize);
			}
			else
			{
				GotSnapshot = true;

				m_LastSnapshotDataSize = DataSize;
				mem_copy(&m_LastSnapshotData, m_aChunkData, DataSize);
				if(m_pListener)
					m_pListener->OnDemoPlayerSnapshot(m_aChunkData, DataSize);
			}
		}
		else
		{
			// if there were no snapshots in this tick, replay the last one
			if(!GotSnapshot && m_pListener && m_LastSnapshotDataSize != -1)
			{
				GotSnapshot = true;
				m_pListener->OnDemoPlayerSnapshot(&m_LastSnapshotData, m_LastSnapshotDataSize);
			}

			// check the remaining types
			if(ChunkType & CHUNKTYPEFLAG_TICKMARKER)
			{
				m_Info.m_NextTick = ChunkTick;
				return IsPlaying();
			}
			else if(ChunkType == CHUNKTYPE_MESSAGE)
			{
				if(m_pListener)
					m_pListener->OnDemoPlayerMessage(m_aChunkData, DataSize);
			}
		}
	}
	return false;
}

void CDemoPlayer::Pause()
{
	m_Info.m_Info.m_Paused = true;
#if defined(CONF_VIDEORECORDER)
	if(m_UseVideo && IVideo::Current() && g_Config.m_ClVideoPauseWithDemo)
		IVideo::Current()->Pause(true);
#endif
}

void CDemoPlayer::Unpause()
{
	m_Info.m_Info.m_Paused = false;
#if defined(CONF_VIDEORECORDER)
	if(m_UseVideo && IVideo::Current() && g_Config.m_ClVideoPauseWithDemo)
		IVideo::Current()->Pause(false);
#endif
}

int CDemoPlayer::Load(class IStorage *pStorage, const char *pFilename, int StorageType, bool ResetSliceMarkers) noexcept
{
	dbg_assert(m_File == 0, "Demo player already playing");

	try
	{
		str_copy(m_aFilename, pFilename);
		str_copy(m_aErrorMessage, "");

		log_info_color(DEMO_PRINT_COLOR, "demo_player", "Loading demo '%s'", pFilename);

		// clear the playback info
		mem_zero(&m_Info, sizeof(m_Info));
		m_Info.m_Info.m_FirstTick = -1;
		m_Info.m_Info.m_LastTick = -1;
		m_Info.m_NextTick = -1;
		m_Info.m_Info.m_CurrentTick = -1;
		m_Info.m_PreviousTick = -1;
		m_Info.m_Info.m_Speed = 1;
		m_SpeedIndex = DEMO_SPEED_INDEX_DEFAULT;
		m_LastSnapshotDataSize = -1;

		if(!GetDemoInfo(pStorage, pFilename, StorageType, &m_Info.m_Header, &m_Info.m_TimelineMarkers, &m_MapInfo, &m_File, m_aErrorMessage, sizeof(m_aErrorMessage)))
		{
			str_copy(m_aFilename, "");
			return -1;
		}
		m_Sixup = str_startswith(m_Info.m_Header.m_aNetversion, "0.7");

		// save byte offset of map for later use
		m_MapOffset = io_tell(m_File);
		if(m_MapOffset < 0 || io_skip(m_File, m_MapInfo.m_Size) != 0)
		{
			Stop("Error skipping map data");
			return -1;
		}

		// Scan the file for interesting points
		if(ScanFile() == EScanFileResult::ERROR_UNRECOVERABLE)
		{
			Stop("Error scanning demo file");
			return -1;
		}
		m_Info.m_LiveStateUpdating = true;

		if(m_Info.m_Header.m_Version > gs_OldVersion)
		{
			// get timeline markers
			int Num = bytes_be_to_uint(m_Info.m_TimelineMarkers.m_aNumTimelineMarkers);
			m_Info.m_Info.m_NumTimelineMarkers = std::clamp<int>(Num, 0, MAX_TIMELINE_MARKERS);
			for(int i = 0; i < m_Info.m_Info.m_NumTimelineMarkers; i++)
			{
				m_Info.m_Info.m_aTimelineMarkers[i] = bytes_be_to_uint(m_Info.m_TimelineMarkers.m_aTimelineMarkers[i]);
				if(!in_range(m_Info.m_Info.m_aTimelineMarkers[i], m_Info.m_Info.m_FirstTick, m_Info.m_Info.m_LastTick))
				{
					Stop("Invalid demo timeline marker");
					return -1;
				}
			}
		}

		// reset slice markers
		if(ResetSliceMarkers)
		{
			g_Config.m_ClDemoSliceBegin = -1;
			g_Config.m_ClDemoSliceEnd = -1;
		}

		// ready for playback
		return 0;
	}
	catch(const std::bad_alloc &)
	{
		FailLoad("Not enough memory to load demo");
	}
	catch(const std::length_error &)
	{
		FailLoad("Demo keyframe index is too large");
	}
	catch(...)
	{
		FailLoad("Error loading demo");
	}
	return -1;
}

unsigned char *CDemoPlayer::GetMapData(class IStorage *pStorage)
{
	if(!m_File || !m_MapInfo.m_Size)
		return nullptr;

	CMapDataOperations &Operations = MapDataOperations();
	const int64_t CurSeek = Operations.Tell(m_File);
	if(CurSeek < 0)
		return nullptr;
	const auto FreeMap = [&Operations](unsigned char *pData) noexcept { Operations.Free(pData); };
	std::unique_ptr<unsigned char, decltype(FreeMap)> pMapData(Operations.Allocate(m_MapInfo.m_Size), FreeMap);
	// 分配失败时尚未移动借用游标，也不能把空缓冲交给 io_read。
	if(!pMapData)
		return nullptr;
	bool ReadComplete = false;
	if(Operations.Seek(m_File, m_MapOffset) == 0)
		ReadComplete = Operations.Read(m_File, pMapData.get(), m_MapInfo.m_Size) == m_MapInfo.m_Size && Operations.Error(m_File) == 0;
	// 短读和初次定位失败同样必须尝试恢复，不能让 || 短路跳过恢复。
	if(Operations.Seek(m_File, CurSeek) != 0)
	{
		Stop("Error restoring demo position after reading map data");
		return nullptr;
	}
	return ReadComplete ? pMapData.release() : nullptr;
}

bool CDemoPlayer::ExtractMap(class IStorage *pStorage, demo_output::COperations *pOutputOperations, CDemoRecorder::CFileOperations *pFileOperations) noexcept
{
	try
	{
		std::unique_ptr<unsigned char, decltype(&std::free)> pMapData(GetMapData(pStorage), &std::free);
		if(!pMapData)
			return false;

		// 缓存名由内嵌内容决定，已有摘要必须与实际读到的地图一致。
		const SHA256_DIGEST Sha256 = sha256(pMapData.get(), m_MapInfo.m_Size);
		if(m_MapInfo.m_Sha256 && *m_MapInfo.m_Sha256 != Sha256)
			return false;

		char aSha[SHA256_MAXSTRSIZE], aMapFilename[IO_MAX_PATH_LENGTH];
		sha256_str(Sha256, aSha, sizeof(aSha));
		str_format(aMapFilename, sizeof(aMapFilename), "downloadedmaps/%s_%s.map", m_Info.m_Header.m_aMapName, aSha);

		demo_output::CTransaction Output(pOutputOperations);
		if(!Output.Prepare(*pStorage, aMapFilename))
			return false;
		IOHANDLE MapFile = pStorage->OpenFile(Output.Filename(), IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
		if(!MapFile)
			return false;

		CDemoRecorder::CFileOperations DefaultOperations;
		CDemoRecorder::CFileOperations &Operations = pFileOperations ? *pFileOperations : DefaultOperations;
		// 从打开到关闭只调用 noexcept 文件操作，失败也必须关闭且只能关闭一次。
		const bool Written = Operations.Write(MapFile, pMapData.get(), m_MapInfo.m_Size) == m_MapInfo.m_Size;
		const bool Flushed = Operations.Flush(MapFile) == 0;
		const bool Clean = Operations.Error(MapFile) == 0;
		const bool Closed = Operations.Close(MapFile) == 0;
		if(!Written || !Flushed || !Clean || !Closed || !Output.Publish())
			return false;
		m_MapInfo.m_Sha256 = Sha256;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

int64_t CDemoPlayer::SystemTime() const
{
	return m_TimeMode == ETimeMode::CACHED_FRAME ? time_get() : time_get_impl();
}

int64_t CDemoPlayer::Time()
{
#if defined(CONF_VIDEORECORDER)
	if(m_UseVideo && IVideo::Current())
	{
		if(!m_WasRecording)
		{
			m_WasRecording = true;
			m_Info.m_LastUpdate = IVideo::Current()->Time();
		}
		return IVideo::Current()->Time();
	}
	else
	{
		const int64_t Now = SystemTime();
		if(m_WasRecording)
		{
			m_WasRecording = false;
			m_Info.m_LastUpdate = Now;
		}
		return Now;
	}
#else
	return SystemTime();
#endif
}

bool CDemoPlayer::Play()
{
	if(!IsPlaying())
		return false;
	// Fill in previous and next tick
	while(m_Info.m_PreviousTick == -1)
	{
		DoTick();
		if(!IsPlaying())
		{
			// Empty demo or error playing tick
			return false;
		}
	}

	// Initialize playback time. Using `set_new_tick` is essential so that `Time`
	// returns the updated time, otherwise the delta between `m_LastUpdate` and
	// the value that `Time` returns when called in the `Update` function can be
	// very large depending on the time required to load the demo, which causes
	// demo playback to start later. This ensures it always starts at 00:00.
	if(m_TimeMode == ETimeMode::CACHED_FRAME)
		set_new_tick();
	m_Info.m_CurrentTime = m_Info.m_PreviousTick * time_freq() / SERVER_TICK_SPEED;
	m_Info.m_LastUpdate = Time();
	if(m_Info.m_LiveStateUpdating && m_Info.m_LastScan <= 0)
	{
		m_Info.m_LastScan = m_Info.m_LastUpdate;
	}
	return true;
}

bool CDemoPlayer::SeekPercent(float Percent)
{
	int WantedTick = m_Info.m_Info.m_FirstTick + round_truncate((m_Info.m_Info.m_LastTick - m_Info.m_Info.m_FirstTick) * Percent);
	return SetPos(WantedTick);
}

bool CDemoPlayer::SeekTime(float Seconds)
{
	int WantedTick = m_Info.m_Info.m_CurrentTick + round_truncate(Seconds * (float)SERVER_TICK_SPEED);
	return SetPos(WantedTick);
}

bool CDemoPlayer::SeekTick(ETickOffset TickOffset)
{
	int WantedTick;
	switch(TickOffset)
	{
	case TICK_CURRENT:
		// TODO: https://github.com/ddnet/ddnet/issues/11681
		WantedTick = m_Info.m_Info.m_CurrentTick;
		break;
	case TICK_PREVIOUS:
		WantedTick = m_Info.m_PreviousTick;
		break;
	case TICK_NEXT:
		WantedTick = m_Info.m_NextTick;
		break;
	default:
		dbg_assert_failed("Invalid TickOffset");
	}

	// +1 because SetPos will seek until the given tick is the next tick that
	// will be played back, whereas we want the wanted tick to be played now.
	return SetPos(WantedTick + 1);
}

bool CDemoPlayer::SetPos(int WantedTick)
{
	if(!m_File)
		return false;

	// TODO: Early exit when WantedTick > m_Info.m_Info.m_CurrentTick && WantedTick <= m_Info.m_NextTick with https://github.com/ddnet/ddnet/issues/11681

	int LastSeekableTick = m_Info.m_Info.m_LastTick;
	if(m_Info.m_Info.m_LiveDemo)
	{
		// Make sure we don't seek all the way until the end in a live demo because the chunk data may not be fully written.
		LastSeekableTick -= 2 * SERVER_TICK_SPEED;
	}
	if(LastSeekableTick < m_Info.m_Info.m_FirstTick)
	{
		WantedTick = m_Info.m_Info.m_FirstTick;
	}
	else
	{
		WantedTick = std::clamp(WantedTick, m_Info.m_Info.m_FirstTick, LastSeekableTick);
	}

	// Just the next tick
	if(WantedTick == m_Info.m_NextTick + 1)
	{
		return DoTick() && Play();
	}

	const int KeyFrameWantedTick = WantedTick - 5; // -5 because we have to have a current tick and previous tick when we do the playback
	const int TickSpan = m_Info.m_Info.m_LastTick - m_Info.m_Info.m_FirstTick;
	// 短录像可能得到负比例，单 tick 则无跨度；转为无符号索引前先归入有效范围。
	const float Percent = TickSpan > 0 ? std::clamp((KeyFrameWantedTick - m_Info.m_Info.m_FirstTick) / (float)TickSpan, 0.0f, 1.0f) : 0.0f;

	// get correct key frame
	size_t KeyFrame = std::clamp<size_t>(m_vKeyFrames.size() * Percent, 0, m_vKeyFrames.size() - 1);
	while(KeyFrame < m_vKeyFrames.size() - 1 && m_vKeyFrames[KeyFrame].m_Tick < KeyFrameWantedTick)
		KeyFrame++;
	while(KeyFrame > 0 && m_vKeyFrames[KeyFrame].m_Tick > KeyFrameWantedTick)
		KeyFrame--;

	// TODO Remove `WantedTick <= m_Info.m_NextTick` with https://github.com/ddnet/ddnet/issues/11681
	if(WantedTick <= m_Info.m_Info.m_CurrentTick || // if we are seeking backwards (must be <= for high bandwidth demos) OR
		WantedTick <= m_Info.m_NextTick || // if seeking to current tick OR
		m_Info.m_Info.m_CurrentTick < m_vKeyFrames[KeyFrame].m_Tick || // we are before the wanted KeyFrame OR
		(KeyFrame != m_vKeyFrames.size() - 1 && m_Info.m_Info.m_CurrentTick >= m_vKeyFrames[KeyFrame + 1].m_Tick)) // we are after the wanted KeyFrame
	{
		if(io_seek(m_File, m_vKeyFrames[KeyFrame].m_Filepos, IOSEEK_START) != 0)
		{
			Stop("Error seeking keyframe position");
			return false;
		}
		m_Info.m_NextTick = -1;
		m_Info.m_Info.m_CurrentTick = -1;
		m_Info.m_PreviousTick = -1;
		m_LastSnapshotDataSize = -1;
	}

	// playback everything until we hit our tick
	while(m_Info.m_NextTick < WantedTick)
	{
		if(!DoTick())
		{
			return false;
		}
	}

	return Play();
}

void CDemoPlayer::SetSpeed(float Speed)
{
	if(!std::isnan(Speed))
		m_Info.m_Info.m_Speed = std::clamp(Speed, 0.f, 256.f);
}

void CDemoPlayer::SetSpeedIndex(int SpeedIndex)
{
	dbg_assert(SpeedIndex >= 0 && SpeedIndex < (int)std::size(DEMO_SPEEDS), "invalid SpeedIndex");
	m_SpeedIndex = SpeedIndex;
	SetSpeed(DEMO_SPEEDS[m_SpeedIndex]);
}

void CDemoPlayer::AdjustSpeedIndex(int Offset)
{
	SetSpeedIndex(std::clamp(m_SpeedIndex + Offset, 0, (int)(std::size(DEMO_SPEEDS) - 1)));
}

void CDemoPlayer::Update(bool RealTime)
{
	const int64_t Now = Time();
	const int64_t Freq = time_freq();
	const int64_t DeltaTime = Now - m_Info.m_LastUpdate;
	m_Info.m_LastUpdate = Now;

	if(m_Info.m_LiveStateUpdating)
	{
		// Determine if demo is live and still being written to, by scanning
		// file again and checking if more ticks are available than before.
		if(Now - m_Info.m_LastScan > Freq)
		{
			const int PreviousLastTick = m_Info.m_Info.m_LastTick;
			const EScanFileResult ScanResult = ScanFile();
			if(ScanResult == EScanFileResult::ERROR_UNRECOVERABLE)
			{
				Stop("Unrecoverable error on incrementally scanning demo file to determine live state");
				return;
			}
			else if(ScanResult == EScanFileResult::SUCCESS)
			{
				// Live state is known when ScanFile succeeded.
				m_Info.m_LiveStateUpdating = false;
			}
			else
			{
				m_Info.m_LiveStateFailedCount++;
				if(m_Info.m_LiveStateFailedCount >= 15)
				{
					// ScanFile keeps failing, which should be unlikely, so this is probably
					// not a live demo but a regular demo that is truncated at the end.
					m_Info.m_LiveStateUpdating = false;
				}
			}
			// Check if we got more ticks also when ScanFile failed, because
			// it could still have found more ticks.
			if(m_Info.m_Info.m_LastTick > PreviousLastTick)
			{
				m_Info.m_Info.m_LiveDemo = true;
				m_Info.m_LiveStateUpdating = false;
				m_Info.m_LiveStateUnchangedCount = 0;
			}
			m_Info.m_LastScan = Now;
			// Try again later if ScanFile failed and no more ticks were found.
		}
	}
	else if(m_Info.m_Info.m_LiveDemo)
	{
		// Scan live demo at tick frequency to smoothly update total time.
		if(Now - m_Info.m_LastScan > Freq / SERVER_TICK_SPEED)
		{
			const int PreviousLastTick = m_Info.m_Info.m_LastTick;
			const EScanFileResult ScanResult = ScanFile();
			if(ScanResult == EScanFileResult::ERROR_UNRECOVERABLE)
			{
				Stop("Unrecoverable error on incrementally scanning live demo file");
				return;
			}
			else if(ScanResult == EScanFileResult::SUCCESS &&
				m_Info.m_Info.m_LastTick == PreviousLastTick)
			{
				m_Info.m_LiveStateUnchangedCount++;
				if(m_Info.m_LiveStateUnchangedCount >= 2 * SERVER_TICK_SPEED)
				{
					// Assume demo stopped being live if we scanned the demo
					// successfully for 2 seconds without reading new ticks.
					m_Info.m_Info.m_LiveDemo = false;
				}
			}
			else
			{
				m_Info.m_LiveStateUnchangedCount = 0;
			}
			m_Info.m_LastScan = Now;
		}
	}

	if(!IsPlaying())
	{
		return;
	}

	if(!m_Info.m_Info.m_Paused)
	{
		if(m_Info.m_Info.m_LiveDemo &&
			m_Info.m_Info.m_Speed > 1.0f &&
			m_Info.m_Info.m_LastTick - m_Info.m_Info.m_CurrentTick <= (DeltaTime * (double)m_Info.m_Info.m_Speed / Freq + 2) * (float)SERVER_TICK_SPEED)
		{
			// Reset to default speed if we are fast-forwarding to the end of a live demo,
			// to prevent playback error due to final demo chunk data still being written.
			SetSpeedIndex(DEMO_SPEED_INDEX_DEFAULT);
		}

		m_Info.m_CurrentTime += (int64_t)(DeltaTime * (double)m_Info.m_Info.m_Speed);

		// Do more ticks until we reach the current time.
		while(!m_Info.m_Info.m_Paused)
		{
			const int64_t CurrentTickStart = m_Info.m_Info.m_CurrentTick * Freq / SERVER_TICK_SPEED;
			if(RealTime && CurrentTickStart > m_Info.m_CurrentTime)
			{
				break;
			}
			DoTick();
			if(!IsPlaying())
			{
				return;
			}
		}
	}

	UpdateTimes();
}

void CDemoPlayer::UpdateTimes()
{
	const int64_t Freq = time_freq();
	const int64_t CurrentTickStart = m_Info.m_Info.m_CurrentTick * Freq / SERVER_TICK_SPEED;
	const int64_t PreviousTickStart = m_Info.m_PreviousTick * Freq / SERVER_TICK_SPEED;
	// 预热和重复 tick 没有可插值的区间，客户端时钟回调也应收到有限值。
	const bool HasInterval = m_Info.m_PreviousTick >= 0 && CurrentTickStart > PreviousTickStart;
	const int64_t Elapsed = HasInterval ? m_Info.m_CurrentTime - PreviousTickStart : 0;
	m_Info.m_IntraTick = HasInterval ? Elapsed / (float)(CurrentTickStart - PreviousTickStart) : 0.0f;
	m_Info.m_IntraTickSincePrev = Elapsed / (float)(Freq / SERVER_TICK_SPEED);
	m_Info.m_TickTime = Elapsed / (float)Freq;
	m_Info.m_Info.m_LivePlayback = m_Info.m_Info.m_LastTick - m_Info.m_Info.m_CurrentTick < 3 * SERVER_TICK_SPEED;

	if(m_UpdateIntraTimesFunc)
	{
		m_UpdateIntraTimesFunc();
	}
}

void CDemoPlayer::Stop(const char *pErrorMessage)
{
#if defined(CONF_VIDEORECORDER)
	if(m_UseVideo && IVideo::Current())
		IVideo::Current()->Stop();
	m_WasRecording = false;
#endif

	if(!m_File)
		return;

	if(pErrorMessage[0] == '\0')
	{
		log_info_color(DEMO_PRINT_COLOR, "demo_player", "Stopped playback");
	}
	else
	{
		log_error_color(DEMO_PRINT_COLOR, "demo_player", "Stopped playback due to error: %s", pErrorMessage);
	}

	io_close(m_File);
	m_File = nullptr;
	m_vKeyFrames.clear();
	str_copy(m_aFilename, "");
	str_copy(m_aErrorMessage, pErrorMessage);
}

void CDemoPlayer::GetDemoName(char *pBuffer, size_t BufferSize) const
{
	IStorage::StripPathAndExtension(m_aFilename, pBuffer, BufferSize);
}

bool CDemoPlayer::GetDemoInfo(IStorage *pStorage, const char *pFilename, int StorageType, CDemoHeader *pDemoHeader, CTimelineMarkers *pTimelineMarkers, CMapInfo *pMapInfo, IOHANDLE *pFile, char *pErrorMessage, size_t ErrorMessageSize) const
{
	mem_zero(pDemoHeader, sizeof(CDemoHeader));
	mem_zero(pTimelineMarkers, sizeof(CTimelineMarkers));
	pMapInfo->m_aName[0] = '\0';
	pMapInfo->m_Sha256 = std::nullopt;
	pMapInfo->m_Crc = 0;
	pMapInfo->m_Size = 0;

	std::unique_ptr<void, decltype(&io_close)> pOwnedFile(pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType), &io_close);
	const IOHANDLE File = pOwnedFile.get();
	if(!File)
	{
		if(pErrorMessage != nullptr)
			str_copy(pErrorMessage, "Could not open demo file", ErrorMessageSize);
		return false;
	}

	if(io_read(File, pDemoHeader, sizeof(CDemoHeader)) != sizeof(CDemoHeader) || !pDemoHeader->Valid())
	{
		if(pErrorMessage != nullptr)
			str_copy(pErrorMessage, "Error reading demo header", ErrorMessageSize);
		mem_zero(pDemoHeader, sizeof(CDemoHeader));
		return false;
	}

	if(pDemoHeader->m_Version < gs_OldVersion)
	{
		if(pErrorMessage != nullptr)
			str_format(pErrorMessage, ErrorMessageSize, "Demo version '%d' is not supported", pDemoHeader->m_Version);
		mem_zero(pDemoHeader, sizeof(CDemoHeader));
		return false;
	}
	else if(pDemoHeader->m_Version > gs_OldVersion)
	{
		if(io_read(File, pTimelineMarkers, sizeof(CTimelineMarkers)) != sizeof(CTimelineMarkers))
		{
			if(pErrorMessage != nullptr)
				str_copy(pErrorMessage, "Error reading timeline markers", ErrorMessageSize);
			mem_zero(pDemoHeader, sizeof(CDemoHeader));
			return false;
		}
	}

	std::optional<SHA256_DIGEST> Sha256;
	if(pDemoHeader->m_Version >= gs_Sha256Version)
	{
		CUuid ExtensionUuid = {};
		const unsigned ExtensionUuidSize = io_read(File, &ExtensionUuid.m_aData, sizeof(ExtensionUuid.m_aData));
		if(ExtensionUuidSize == sizeof(ExtensionUuid.m_aData) && ExtensionUuid == SHA256_EXTENSION)
		{
			SHA256_DIGEST ReadSha256;
			if(io_read(File, &ReadSha256, sizeof(SHA256_DIGEST)) != sizeof(SHA256_DIGEST))
			{
				if(pErrorMessage != nullptr)
					str_copy(pErrorMessage, "Error reading SHA256", ErrorMessageSize);
				mem_zero(pDemoHeader, sizeof(CDemoHeader));
				mem_zero(pTimelineMarkers, sizeof(CTimelineMarkers));
				return false;
			}
			Sha256 = ReadSha256;
		}
		else
		{
			// This hopes whatever happened during the version increment didn't add something here
			log_warn_color(DEMO_PRINT_COLOR, "demo_player", "Demo version incremented, but not by DDNet");
			if(io_seek(File, -(int64_t)ExtensionUuidSize, IOSEEK_CUR) != 0)
			{
				if(pErrorMessage != nullptr)
					str_copy(pErrorMessage, "Error rewinding SHA256 extension UUID", ErrorMessageSize);
				mem_zero(pDemoHeader, sizeof(CDemoHeader));
				mem_zero(pTimelineMarkers, sizeof(CTimelineMarkers));
				return false;
			}
		}
	}

	str_copy(pMapInfo->m_aName, pDemoHeader->m_aMapName);
	pMapInfo->m_Sha256 = Sha256;
	pMapInfo->m_Crc = bytes_be_to_uint(pDemoHeader->m_aMapCrc);
	pMapInfo->m_Size = bytes_be_to_uint(pDemoHeader->m_aMapSize);

	if(pFile != nullptr)
		*pFile = pOwnedFile.release();

	return true;
}

namespace
{

	class CDemoRecordingListener : public CDemoPlayer::IListener
	{
	public:
		CDemoRecorder *m_pDemoRecorder;
		CDemoPlayer *m_pDemoPlayer;
		bool m_Stop;
		int m_StartTick;
		int m_EndTick;
		int64_t m_TickOffset;
		int m_FirstRecordedTick;
		int m_LastRecordedTick;
		int m_RecordedTicks = 0;
		bool m_NeedsKeyframe;

		bool AcceptTick(int Tick)
		{
			if(m_EndTick != -1 && Tick > m_EndTick)
			{
				m_Stop = true;
				// 离线 Update 会连续解码，须暂停播放器才能在本段末尾返回。
				m_pDemoPlayer->Pause();
				return false;
			}
			return m_StartTick == -1 || Tick >= m_StartTick;
		}

		void TrackRecordedTick(int Tick)
		{
			// 只需判断是否有播放预热所需的两条 tick，避免长期计数溢出。
			if(m_RecordedTicks < 2)
				++m_RecordedTicks;
			if(m_FirstRecordedTick == -1)
				m_FirstRecordedTick = Tick;
			m_LastRecordedTick = Tick;
		}

		void OnDemoPlayerSnapshot(void *pData, int Size) override
		{
			const CDemoPlayer::CPlaybackInfo *pInfo = m_pDemoPlayer->Info();

			if(AcceptTick(pInfo->m_Info.m_CurrentTick))
			{
				const int OutputTick = static_cast<int>(pInfo->m_Info.m_CurrentTick + m_TickOffset);
				m_pDemoRecorder->RecordSnapshot(OutputTick, pData, Size, m_NeedsKeyframe);
				TrackRecordedTick(OutputTick);
				m_NeedsKeyframe = false;
			}
		}

		void OnDemoPlayerMessage(void *pData, int Size) override
		{
			const CDemoPlayer::CPlaybackInfo *pInfo = m_pDemoPlayer->Info();

			if(AcceptTick(pInfo->m_Info.m_CurrentTick))
			{
				const int OutputTick = static_cast<int>(pInfo->m_Info.m_CurrentTick + m_TickOffset);
				if(OutputTick != m_LastRecordedTick)
				{
					m_pDemoRecorder->RecordTickMarker(OutputTick);
					TrackRecordedTick(OutputTick);
				}
				m_pDemoRecorder->RecordMessage(pData, Size);
			}
		}
	};

} // namespace

void CDemoEditor::Init(CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, class IStorage *pStorage, CDemoPlayer::CMapDataOperations *pMapDataOperations, demo_output::COperations *pOutputOperations, CDemoRecorder::CFileOperations *pRecordOperations)
{
	m_pSnapshotDelta = pSnapshotDelta;
	m_pSnapshotDeltaSixup = pSnapshotDeltaSixup;
	m_pStorage = pStorage;
	m_pMapDataOperations = pMapDataOperations;
	m_pOutputOperations = pOutputOperations;
	m_pRecordOperations = pRecordOperations;
}

bool CDemoEditor::Slice(const char *pDemo, const char *pDst, int StartTick, int EndTick, DEMOFUNC_FILTER pfnFilter, void *pUser) noexcept
{
	try
	{
		return SliceImpl(pDemo, pDst, std::vector<SDemoSliceSegment>{{StartTick, EndTick}}, pfnFilter, pUser);
	}
	catch(...)
	{
		return false;
	}
}

bool CDemoEditor::Slice(const char *pDemo, const char *pDst, const std::vector<SDemoSliceSegment> &vSegments, DEMOFUNC_FILTER pfnFilter, void *pUser) noexcept
{
	try
	{
		return SliceImpl(pDemo, pDst, vSegments, pfnFilter, pUser);
	}
	catch(...)
	{
		return false;
	}
}

bool CDemoEditor::SliceImpl(const char *pDemo, const char *pDst, const std::vector<SDemoSliceSegment> &vSegments, DEMOFUNC_FILTER pfnFilter, void *pUser)
{
	CDemoPlayer DemoPlayer(m_pSnapshotDelta, m_pSnapshotDeltaSixup, false, m_pMapDataOperations, CDemoPlayer::ETimeMode::UNCACHED);
	struct CPlayerGuard
	{
		CDemoPlayer &m_Player;
		~CPlayerGuard() { m_Player.Stop(); }
	} PlayerGuard{DemoPlayer};
	if(DemoPlayer.Load(m_pStorage, pDemo, IStorage::TYPE_ALL_OR_ABSOLUTE, false) == -1)
		return false;

	const CMapInfo *pMapInfo = DemoPlayer.GetMapInfo();
	const CDemoPlayer::CPlaybackInfo *pInfo = DemoPlayer.Info();

	std::vector<SDemoSliceSegment> vNormalizedSegments;
	vNormalizedSegments.reserve(vSegments.size());
	for(const auto &Segment : vSegments)
	{
		const int StartTick = Segment.m_StartTick == -1 ? pInfo->m_Info.m_FirstTick : Segment.m_StartTick;
		const int EndTick = Segment.m_EndTick == -1 ? pInfo->m_Info.m_LastTick : Segment.m_EndTick;
		if(StartTick > EndTick)
			continue;
		if(StartTick < MIN_TICK || EndTick >= MAX_TICK)
			return false;
		vNormalizedSegments.push_back({StartTick, EndTick});
	}
	if(vNormalizedSegments.empty())
		vNormalizedSegments.push_back({pInfo->m_Info.m_FirstTick, pInfo->m_Info.m_LastTick});
	std::sort(vNormalizedSegments.begin(), vNormalizedSegments.end(), [](const SDemoSliceSegment &Left, const SDemoSliceSegment &Right) {
		if(Left.m_StartTick != Right.m_StartTick)
			return Left.m_StartTick < Right.m_StartTick;
		return Left.m_EndTick < Right.m_EndTick;
	});

	// 使用原片段长度拼接，先检查完整输出范围，不能靠整数截断改变时轴。
	int64_t NextOutputTick = vNormalizedSegments[0].m_StartTick;
	for(const auto &Segment : vNormalizedSegments)
	{
		const int64_t Length = static_cast<int64_t>(Segment.m_EndTick) - Segment.m_StartTick + 1;
		if(Length <= 0 || Length > MAX_TICK - NextOutputTick)
			return false;
		NextOutputTick += Length;
	}

	std::optional<SHA256_DIGEST> Sha256 = pMapInfo->m_Sha256;
	if(!Sha256.has_value())
	{
		if(DemoPlayer.ExtractMap(m_pStorage))
		{
			Sha256 = pMapInfo->m_Sha256;
		}
	}
	if(!Sha256.has_value())
	{
		DemoPlayer.Stop();
		log_error_color(DEMO_PRINT_COLOR, "demo/slice", "Failed to start demo slicing because map SHA256 could not be determined.");
		return false;
	}

	demo_output::CTransaction Output(m_pOutputOperations);
	CDemoRecorder DemoRecorder(DemoPlayer.IsSixup() ? m_pSnapshotDeltaSixup : m_pSnapshotDelta, false, m_pRecordOperations);
	struct CRecorderGuard
	{
		CDemoRecorder &m_Recorder;
		~CRecorderGuard()
		{
			if(m_Recorder.IsRecording())
				m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE);
		}
	} RecorderGuard{DemoRecorder};
	std::unique_ptr<unsigned char, decltype(&std::free)> pMapData(DemoPlayer.GetMapData(m_pStorage), &std::free);
	if(pMapInfo->m_Size != 0 && !pMapData)
	{
		DemoPlayer.Stop();
		return false;
	}
	if(!Output.Prepare(*m_pStorage, pDst))
		return false;
	const int Result = DemoRecorder.Start(m_pStorage, Output.Filename(), pInfo->m_Header.m_aNetversion, pMapInfo->m_aName, Sha256.value(), pMapInfo->m_Crc, pInfo->m_Header.m_aType, pMapInfo->m_Size, pMapData.get(), nullptr, pfnFilter, pUser) == -1;
	pMapData.reset();
	if(Result != 0)
	{
		DemoPlayer.Stop();
		return false;
	}

	NextOutputTick = vNormalizedSegments[0].m_StartTick;
	int FirstOutputTick = -1;
	int LastOutputTick = -1;
	int RecordedTicks = 0;
	bool RecordedData = false;
	if(!DemoPlayer.Play())
		return false;
	for(const auto &Segment : vNormalizedSegments)
	{
		CDemoRecordingListener Listener;
		Listener.m_pDemoRecorder = &DemoRecorder;
		Listener.m_pDemoPlayer = &DemoPlayer;
		Listener.m_Stop = false;
		Listener.m_StartTick = Segment.m_StartTick;
		Listener.m_EndTick = Segment.m_EndTick;
		Listener.m_TickOffset = NextOutputTick - Segment.m_StartTick;
		Listener.m_FirstRecordedTick = -1;
		Listener.m_LastRecordedTick = -1;
		Listener.m_NeedsKeyframe = true;
		DemoPlayer.SetListener(&Listener);

		// 离线更新会读到源文件末尾并暂停；开始下一片段时恢复裁剪播放器。
		DemoPlayer.Unpause();
		if(!DemoPlayer.SetPos(Segment.m_StartTick + 1))
			return false;

		while(DemoPlayer.IsPlaying() && !Listener.m_Stop)
		{
			DemoPlayer.Update(false);

			if(pInfo->m_Info.m_Paused)
				break;
		}

		NextOutputTick += static_cast<int64_t>(Segment.m_EndTick) - Segment.m_StartTick + 1;
		RecordedData |= !Listener.m_NeedsKeyframe;
		RecordedTicks = std::min(2, RecordedTicks + Listener.m_RecordedTicks);
		if(Listener.m_FirstRecordedTick != -1)
		{
			if(FirstOutputTick == -1)
				FirstOutputTick = Listener.m_FirstRecordedTick;
			LastOutputTick = Listener.m_LastRecordedTick;
		}
	}
	DemoPlayer.SetListener(nullptr);
	if(!RecordedData || RecordedTicks < 2 || FirstOutputTick > LastOutputTick || DemoRecorder.HasWriteError() || DemoPlayer.ErrorMessage()[0] != '\0')
		return false;

	// 只保留实际输出首尾之间的标记，稀疏快照可能让请求范围比输出更宽。
	int64_t MarkerOutputTick = vNormalizedSegments[0].m_StartTick;
	for(const auto &Segment : vNormalizedSegments)
	{
		for(int i = 0; i < pInfo->m_Info.m_NumTimelineMarkers; i++)
		{
			if(pInfo->m_Info.m_aTimelineMarkers[i] >= Segment.m_StartTick && pInfo->m_Info.m_aTimelineMarkers[i] <= Segment.m_EndTick)
			{
				const int64_t OutputTick = MarkerOutputTick + pInfo->m_Info.m_aTimelineMarkers[i] - Segment.m_StartTick;
				if(OutputTick >= FirstOutputTick && OutputTick <= LastOutputTick)
					DemoRecorder.AddDemoMarker(static_cast<int>(OutputTick));
			}
		}
		MarkerOutputTick += static_cast<int64_t>(Segment.m_EndTick) - Segment.m_StartTick + 1;
	}

	DemoPlayer.Stop();
	return DemoRecorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE) == 0 && Output.Publish();
}
