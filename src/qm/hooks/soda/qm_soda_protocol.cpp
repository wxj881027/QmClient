#include "qm_soda_protocol.h"

#include <algorithm>
#include <cstring>

namespace QmSodaHook
{
	namespace
	{
		uint32_t Crc32(const void *pData, size_t Size)
		{
			const auto *pBytes = static_cast<const uint8_t *>(pData);
			uint32_t Result = 0xFFFFFFFFU;
			for(size_t Index = 0; Index < Size; ++Index)
			{
				Result ^= pBytes[Index];
				for(int Bit = 0; Bit < 8; ++Bit)
					Result = (Result & 1) != 0 ? (Result >> 1) ^ 0xEDB88320U : Result >> 1;
			}
			return Result ^ 0xFFFFFFFFU;
		}

		// 返回完整且严格合法的 UTF-8 codepoint 宽度;0 表示非法。
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
		pSnapshot->m_Magic = 0x514D5344;
		pSnapshot->m_SchemaVersion = PROTOCOL_SCHEMA_VERSION;
		pSnapshot->m_SnapshotSize = (uint16_t)sizeof(SSnapshot);
		pSnapshot->m_Checksum = CalculateChecksum(*pSnapshot);
	}

	bool ValidateSnapshot(const SSnapshot &Snapshot)
	{
		if(Snapshot.m_Magic != 0x514D5344 || Snapshot.m_SchemaVersion != PROTOCOL_SCHEMA_VERSION || Snapshot.m_SnapshotSize != sizeof(SSnapshot))
			return false;
		if(Snapshot.m_Sequence == 0 || (Snapshot.m_Sequence & 1) != 0)
			return false;
		if((Snapshot.m_Flags & ~KNOWN_FLAGS) != 0)
			return false;
		if((Snapshot.m_Flags & FLAG_HAS_SONG) != 0 && Snapshot.m_aMediaId[0] == '\0')
			return false;
		if((Snapshot.m_Flags & FLAG_HAS_SONG) != 0 && Snapshot.m_Generation == 0)
			return false;
		if(Snapshot.m_PositionMs < 0 || Snapshot.m_DurationMs < 0)
			return false;
		if((Snapshot.m_Flags & FLAG_HAS_COVER) != 0 && Snapshot.m_aCoverUrl[0] == '\0')
			return false;
		if((Snapshot.m_Flags & FLAG_HAS_LYRIC_FILE) != 0 && Snapshot.m_aLyricFilePath[0] == '\0')
			return false;
		if(!IsNulTerminatedUtf8(Snapshot.m_aMediaId, sizeof(Snapshot.m_aMediaId)) ||
			!IsNulTerminatedUtf8(Snapshot.m_aTitle, sizeof(Snapshot.m_aTitle)) ||
			!IsNulTerminatedUtf8(Snapshot.m_aArtist, sizeof(Snapshot.m_aArtist)) ||
			!IsNulTerminatedUtf8(Snapshot.m_aAlbum, sizeof(Snapshot.m_aAlbum)) ||
			!IsNulTerminatedUtf8(Snapshot.m_aCoverUrl, sizeof(Snapshot.m_aCoverUrl)) ||
			!IsNulTerminatedUtf8(Snapshot.m_aLyricFilePath, sizeof(Snapshot.m_aLyricFilePath)) ||
			!IsNulTerminatedUtf8(Snapshot.m_aError, sizeof(Snapshot.m_aError)))
			return false;
		return Snapshot.m_Checksum == CalculateChecksum(Snapshot);
	}

	bool IsStableSequence(uint64_t BeginSequence, uint64_t EndSequence)
	{
		return BeginSequence != 0 && (BeginSequence & 1) == 0 && BeginSequence == EndSequence;
	}

	bool IsStale(const SSnapshot &Snapshot, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(Snapshot.m_UpdatedAtTick == 0 || NowTick < Snapshot.m_UpdatedAtTick)
			return true;
		return NowTick - Snapshot.m_UpdatedAtTick > TimeoutMs;
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
} // namespace QmSodaHook
