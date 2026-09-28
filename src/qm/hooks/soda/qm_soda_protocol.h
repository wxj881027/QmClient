#ifndef QM_HOOKS_SODA_QM_SODA_PROTOCOL_H
#define QM_HOOKS_SODA_QM_SODA_PROTOCOL_H

#include <cstddef>
#include <cstdint>

// 汽水音乐 Hook 与 QmClient 之间的共享内存协议(仿网易云 v5 的 seqlock 设计)。
// 只承载「当前播放身份 + 进度 + 完整歌词文本引用」;完整歌词因可能较大,
// 由 Helper 写入本地 JSON 文件,快照只发布文件路径与版本号,客户端按需读取。
namespace QmSodaHook
{
	constexpr uint16_t PROTOCOL_SCHEMA_VERSION = 1;
	constexpr char PROTOCOL_MAPPING_NAME[] = "Local\\QmClient.SodaHook.v1";
	constexpr wchar_t PROTOCOL_MAPPING_NAME_W[] = L"Local\\QmClient.SodaHook.v1";
	constexpr wchar_t PROTOCOL_WRITER_MUTEX_NAME_W[] = L"Local\\QmClient.SodaHook.v1.Writer";

	constexpr uint32_t MAX_MEDIA_ID_BYTES = 64;
	constexpr uint32_t MAX_TITLE_BYTES = 256;
	constexpr uint32_t MAX_ARTIST_BYTES = 256;
	constexpr uint32_t MAX_ALBUM_BYTES = 256;
	constexpr uint32_t MAX_COVER_URL_BYTES = 2048;
	constexpr uint32_t MAX_LYRIC_FILE_PATH_BYTES = 1024;
	constexpr uint32_t MAX_ERROR_BYTES = 256;

	constexpr uint32_t FLAG_HAS_SONG = 1U << 0;
	constexpr uint32_t FLAG_HAS_LYRIC_FILE = 1U << 1;
	constexpr uint32_t FLAG_POSITION_VALID = 1U << 2;
	constexpr uint32_t FLAG_PLAYING = 1U << 3;
	constexpr uint32_t FLAG_LOADING = 1U << 4;
	constexpr uint32_t FLAG_HAS_COVER = 1U << 5;
	constexpr uint32_t FLAG_THROTTLED = 1U << 6;
	constexpr uint32_t KNOWN_FLAGS = (1U << 7) - 1U;

// 自然 8 字节对齐;共享内存中不放指针、句柄或 STL 对象。
#pragma pack(push, 8)
	struct SSnapshot
	{
		uint32_t m_Magic = 0x514D5344; // "QMSD"
		uint16_t m_SchemaVersion = PROTOCOL_SCHEMA_VERSION;
		uint16_t m_SnapshotSize = 0;
		uint64_t m_Sequence = 0;
		uint32_t m_SodaMusicPid = 0;
		uint32_t m_Flags = 0;
		uint64_t m_Generation = 0;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		uint64_t m_UpdatedAtTick = 0;
		char m_aMediaId[MAX_MEDIA_ID_BYTES] = {};
		char m_aTitle[MAX_TITLE_BYTES] = {};
		char m_aArtist[MAX_ARTIST_BYTES] = {};
		char m_aAlbum[MAX_ALBUM_BYTES] = {};
		char m_aCoverUrl[MAX_COVER_URL_BYTES] = {};
		// 完整歌词数据文件(JSON:歌词 + 逐字时间轴 + 翻译),Helper 原子写入。
		char m_aLyricFilePath[MAX_LYRIC_FILE_PATH_BYTES] = {};
		char m_aError[MAX_ERROR_BYTES] = {};
		uint32_t m_Checksum = 0;
		uint32_t m_Reserved = 0;
	};

	struct SSharedBlock
	{
		// Writer seqlock:odd 表示正在写入,even 表示稳定。
		volatile uint64_t m_Sequence = 0;
		SSnapshot m_Snapshot{};
	};
#pragma pack(pop)

	static_assert(sizeof(SSnapshot) % 8 == 0, "soda snapshot must stay aligned");
	static_assert(offsetof(SSnapshot, m_Sequence) % 8 == 0, "soda sequence must be aligned");

	uint32_t CalculateChecksum(const SSnapshot &Snapshot);
	void FinalizeSnapshot(SSnapshot *pSnapshot);
	bool ValidateSnapshot(const SSnapshot &Snapshot);
	bool IsStableSequence(uint64_t BeginSequence, uint64_t EndSequence);
	bool IsStale(const SSnapshot &Snapshot, uint64_t NowTick, uint64_t TimeoutMs);
	// UTF-8 字符串复制:只在完整 codepoint 边界截断,并保证 NUL 结尾。
	size_t CopyUtf8Truncated(char *pDestination, size_t DestinationSize, const char *pSource, size_t SourceSize);
} // namespace QmSodaHook

#endif
