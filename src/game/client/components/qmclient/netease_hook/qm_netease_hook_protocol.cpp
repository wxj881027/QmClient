#include "qm_netease_hook_protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace QmNeteaseHook
{
	namespace
	{
		// 表只依赖固定多项式，在编译期准备，读取快照时无需一次性同步。
		constexpr std::array<uint32_t, 256> MakeCrcTable()
		{
			std::array<uint32_t, 256> Table{};
			for(uint32_t i = 0; i < Table.size(); ++i)
			{
				uint32_t Value = i;
				for(int Bit = 0; Bit < 8; ++Bit)
					Value = (Value & 1) != 0 ? (Value >> 1) ^ 0xEDB88320U : Value >> 1;
				Table[i] = Value;
			}
			return Table;
		}
		constexpr auto CrcTable = MakeCrcTable();

		// 返回一个完整且严格合法的 UTF-8 codepoint 宽度。网易云前端数据
		// 直接来自 JS，不能把过长编码、代理区或超出 Unicode 范围的字节
		// 当作普通文本发布到跨进程 ABI。
		size_t Utf8CodepointWidth(const char *pText, size_t Size, size_t Offset)
		{
			if(pText == nullptr || Offset >= Size)
				return 0;
			const uint8_t Lead = (uint8_t)pText[Offset];
			if(Lead <= 0x7F)
				return 1;
			size_t Width = 0;
			uint32_t Codepoint = 0;
			uint32_t Minimum = 0;
			if((Lead & 0xE0) == 0xC0)
			{
				Width = 2;
				Codepoint = Lead & 0x1F;
				Minimum = 0x80;
			}
			else if((Lead & 0xF0) == 0xE0)
			{
				Width = 3;
				Codepoint = Lead & 0x0F;
				Minimum = 0x800;
			}
			else if((Lead & 0xF8) == 0xF0)
			{
				Width = 4;
				Codepoint = Lead & 0x07;
				Minimum = 0x10000;
			}
			else
				return 0;
			if(Width > Size - Offset)
				return 0;
			for(size_t Byte = 1; Byte < Width; ++Byte)
			{
				const uint8_t Continuation = (uint8_t)pText[Offset + Byte];
				if((Continuation & 0xC0) != 0x80)
					return 0;
				Codepoint = (Codepoint << 6) | (Continuation & 0x3F);
			}
			if(Codepoint < Minimum || Codepoint > 0x10FFFF || (Codepoint >= 0xD800 && Codepoint <= 0xDFFF))
				return 0;
			return Width;
		}

		bool IsNulTerminatedUtf8(const char *pText, size_t Capacity)
		{
			if(pText == nullptr || Capacity == 0)
				return false;
			size_t Length = 0;
			while(Length < Capacity && pText[Length] != '\0')
				++Length;
			if(Length == Capacity)
				return false;
			for(size_t Offset = 0; Offset < Length;)
			{
				const size_t Width = Utf8CodepointWidth(pText, Length, Offset);
				if(Width == 0)
					return false;
				Offset += Width;
			}
			return true;
		}
	}

	uint32_t Crc32(const void *pData, size_t Size)
	{
		const auto *pBytes = static_cast<const uint8_t *>(pData);
		uint32_t Result = 0xFFFFFFFFU;
		for(size_t i = 0; i < Size; ++i)
			Result = CrcTable[(Result ^ pBytes[i]) & 0xFFU] ^ (Result >> 8);
		return Result ^ 0xFFFFFFFFU;
	}

	uint32_t CalculateChecksum(const SSnapshot &Snapshot)
	{
		SSnapshot Copy = Snapshot;
		Copy.m_Checksum = 0;
		return Crc32(&Copy, sizeof(Copy));
	}

	void FinalizeSnapshot(SSnapshot *pSnapshot)
	{
		if(pSnapshot == nullptr)
			return;
		pSnapshot->m_Magic = PROTOCOL_MAGIC;
		pSnapshot->m_SchemaVersion = PROTOCOL_SCHEMA_VERSION;
		pSnapshot->m_SnapshotSize = sizeof(SSnapshot);
		pSnapshot->m_Checksum = CalculateChecksum(*pSnapshot);
	}

	bool ValidateSnapshot(const SSnapshot &Snapshot)
	{
		if(Snapshot.m_Magic != PROTOCOL_MAGIC || Snapshot.m_SchemaVersion != PROTOCOL_SCHEMA_VERSION || Snapshot.m_SnapshotSize != sizeof(SSnapshot))
			return false;
		if(Snapshot.m_PlayerBuild != TARGET_BUILD || Snapshot.m_PlayerPatch != TARGET_PATCH)
			return false;
		if(Snapshot.m_Sequence == 0 || (Snapshot.m_Sequence & 1) != 0)
			return false;
		if(!std::isfinite(Snapshot.m_PlaybackRate) || Snapshot.m_PlaybackRate < 0.0 || Snapshot.m_PlaybackRate > 8.0)
			return false;
		if((Snapshot.m_Status & STATUS_HAS_COVER) != 0 && !HasCover(Snapshot))
			return false;
		// 先确认两端非负且有序再相减，保留一秒容差而不对最大时长继续相加。
		const auto ExceedsDuration = [&Snapshot](int64_t Value) {
			return Snapshot.m_DurationMs > 0 && Value > Snapshot.m_DurationMs && Value - Snapshot.m_DurationMs > 1000;
		};
		if(Snapshot.m_DurationMs < 0 || Snapshot.m_PositionMs < 0 || ExceedsDuration(Snapshot.m_PositionMs))
			return false;
		if(HasCurrentLine(Snapshot))
		{
			const bool UnknownBoundaries = Snapshot.m_CurrentLineStartMs == -1 && Snapshot.m_CurrentLineEndMs == -1;
			if(!UnknownBoundaries && (Snapshot.m_CurrentLineStartMs < 0 || Snapshot.m_CurrentLineEndMs <= Snapshot.m_CurrentLineStartMs ||
							 ExceedsDuration(Snapshot.m_CurrentLineStartMs) || ExceedsDuration(Snapshot.m_CurrentLineEndMs)))
				return false;
		}
		return Snapshot.m_Checksum == CalculateChecksum(Snapshot);
	}

	bool HasMedia(const SSnapshot &Snapshot)
	{
		return (Snapshot.m_Status & STATUS_HAS_MEDIA) != 0;
	}

	bool HasCurrentLine(const SSnapshot &Snapshot)
	{
		return (Snapshot.m_Status & STATUS_HAS_CURRENT_LINE) != 0 && Snapshot.m_aCurrentLine[0] != '\0';
	}

	bool HasCover(const SSnapshot &Snapshot)
	{
		return Snapshot.m_aCoverPath[0] != '\0' || Snapshot.m_aCoverUrl[0] != '\0';
	}

	bool IsStableSequence(uint64_t BeginSequence, uint64_t EndSequence)
	{
		return BeginSequence != 0 && (BeginSequence & 1) == 0 && BeginSequence == EndSequence;
	}

	uint32_t CalculateChecksumV5(const SSnapshotV5 &Snapshot)
	{
		SSnapshotV5 Copy = Snapshot;
		Copy.m_Checksum = 0;
		return Crc32(&Copy, sizeof(Copy));
	}

	void FinalizeSnapshotV5(SSnapshotV5 *pSnapshot)
	{
		if(pSnapshot == nullptr)
			return;
		pSnapshot->m_Magic = PROTOCOL_MAGIC;
		pSnapshot->m_SchemaVersion = PROTOCOL_SCHEMA_VERSION_V5;
		pSnapshot->m_SnapshotSize = (uint16_t)sizeof(SSnapshotV5);
		pSnapshot->m_aCurrentLyric[V5_MAX_LYRIC_BYTES - 1] = '\0';
		pSnapshot->m_Checksum = CalculateChecksumV5(*pSnapshot);
	}

	bool ValidateSnapshotV5(const SSnapshotV5 &Snapshot)
	{
		if(Snapshot.m_Magic != PROTOCOL_MAGIC || Snapshot.m_SchemaVersion != PROTOCOL_SCHEMA_VERSION_V5 || Snapshot.m_SnapshotSize != sizeof(SSnapshotV5))
			return false;
		if(Snapshot.m_Sequence == 0 || (Snapshot.m_Sequence & 1) != 0)
			return false;
		if(Snapshot.m_CloudMusicPid == 0)
			return false;
		if((Snapshot.m_Flags & ~V5_KNOWN_FLAGS) != 0)
			return false;
		if(!IsValidLyricSource(Snapshot.m_LyricSource))
			return false;
		if((Snapshot.m_Flags & V5_FLAG_HAS_SONG) != 0 && Snapshot.m_SongId == 0)
			return false;
		if((Snapshot.m_Flags & V5_FLAG_HAS_SONG) == 0 && Snapshot.m_SongId != 0)
			return false;
		if((Snapshot.m_Flags & V5_FLAG_HAS_SONG) != 0 && Snapshot.m_Generation == 0)
			return false;
		if((Snapshot.m_Flags & V5_FLAG_LYRIC_VALID) != 0)
		{
			if((Snapshot.m_Flags & V5_FLAG_HAS_SONG) == 0 || Snapshot.m_aCurrentLyric[0] == '\0')
				return false;
			if(Snapshot.m_LyricSource == (uint32_t)ENeteaseLyricSource::None)
				return false;
		}
		else
		{
			if(Snapshot.m_aCurrentLyric[0] != '\0' || Snapshot.m_LineStartMs != -1 || Snapshot.m_LineEndMs != -1)
				return false;
			if(Snapshot.m_LyricSource == (uint32_t)ENeteaseLyricSource::DesktopLyricsFallback ||
				(Snapshot.m_LyricSource != (uint32_t)ENeteaseLyricSource::None && (Snapshot.m_Flags & V5_FLAG_HAS_SONG) == 0))
				return false;
		}
		if(Snapshot.m_PositionMs < 0 || Snapshot.m_LineStartMs < -1 || Snapshot.m_LineEndMs < -1)
			return false;
		if(Snapshot.m_LineStartMs == -1 && Snapshot.m_LineEndMs != -1)
			return false;
		if(Snapshot.m_LineStartMs >= 0 && Snapshot.m_LineEndMs >= 0 && Snapshot.m_LineEndMs <= Snapshot.m_LineStartMs)
			return false;
		if(!IsNulTerminatedUtf8(Snapshot.m_aCurrentLyric, sizeof(Snapshot.m_aCurrentLyric)))
			return false;
		return Snapshot.m_Checksum == CalculateChecksumV5(Snapshot);
	}

	bool IsValidLyricSource(uint32_t Source)
	{
		return Source == (uint32_t)ENeteaseLyricSource::None ||
		       Source == (uint32_t)ENeteaseLyricSource::Frontend ||
		       Source == (uint32_t)ENeteaseLyricSource::DesktopLyricsFallback;
	}

	bool IsStableSequenceV5(uint64_t BeginSequence, uint64_t EndSequence)
	{
		return BeginSequence != 0 && (BeginSequence & 1) == 0 && BeginSequence == EndSequence;
	}

	bool IsStaleV5(const SSnapshotV5 &Snapshot, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(Snapshot.m_UpdatedAtTick == 0 || NowTick < Snapshot.m_UpdatedAtTick)
			return true;
		return NowTick - Snapshot.m_UpdatedAtTick > TimeoutMs;
	}

	bool ShouldPreserveDesktopFallbackV5(const SSnapshotV5 &Existing, const SSnapshotV5 &Candidate, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(!ValidateSnapshotV5(Existing) || IsStaleV5(Existing, NowTick, TimeoutMs))
			return false;
		if(Existing.m_LyricSource != (uint32_t)ENeteaseLyricSource::DesktopLyricsFallback ||
			(Existing.m_Flags & V5_FLAG_LYRIC_VALID) == 0)
			return false;
		if(Candidate.m_LyricSource != (uint32_t)ENeteaseLyricSource::None || Existing.m_CloudMusicPid != Candidate.m_CloudMusicPid)
			return false;
		if((Candidate.m_Flags & V5_FLAG_HAS_SONG) == 0)
			return true;
		return (Existing.m_Flags & V5_FLAG_HAS_SONG) != 0 && Existing.m_SongId == Candidate.m_SongId;
	}

	bool CanPublishDesktopFallbackV5(const SSnapshotV5 &Existing, const SSnapshotV5 &Fallback, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(!ValidateSnapshotV5(Existing) || IsStaleV5(Existing, NowTick, TimeoutMs))
			return true;
		if(Existing.m_LyricSource != (uint32_t)ENeteaseLyricSource::None)
			return Existing.m_LyricSource == (uint32_t)ENeteaseLyricSource::DesktopLyricsFallback;
		if((Existing.m_Flags & V5_FLAG_HAS_SONG) == 0)
			return true;
		return (Fallback.m_Flags & V5_FLAG_HAS_SONG) != 0 && Existing.m_CloudMusicPid == Fallback.m_CloudMusicPid && Existing.m_SongId == Fallback.m_SongId;
	}

	size_t CopyUtf8Truncated(char *pDestination, size_t DestinationSize, const char *pSource, size_t SourceSize)
	{
		if(pDestination == nullptr || DestinationSize == 0)
			return 0;
		pDestination[0] = '\0';
		if(pSource == nullptr || SourceSize == 0 || DestinationSize == 1)
			return 0;
		const size_t Limit = std::min(SourceSize, DestinationSize - 1);
		size_t CopySize = 0;
		while(CopySize < Limit && pSource[CopySize] != '\0')
		{
			const size_t Width = Utf8CodepointWidth(pSource, SourceSize, CopySize);
			if(Width == 0)
				break;
			if(CopySize + Width > Limit)
				break;
			CopySize += Width;
		}
		if(CopySize > 0)
			std::memcpy(pDestination, pSource, CopySize);
		pDestination[CopySize] = '\0';
		return CopySize;
	}

} // namespace QmNeteaseHook
