#include "ghost.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

static const unsigned char gs_aHeaderMarker[8] = {'T', 'W', 'G', 'H', 'O', 'S', 'T', 0};
static const unsigned char gs_CurVersion = 6;

static const LOG_COLOR LOG_COLOR_GHOST{165, 153, 153};

static bool GhostPathFits(IStorage *pStorage, const char *pFilename)
{
	// 存储前缀也占固定缓冲；截断可能使临时路径与正式路径重合。
	char aRoot[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
	return static_cast<size_t>(str_length(aRoot)) + static_cast<size_t>(str_length(pFilename)) < IO_MAX_PATH_LENGTH;
}

int CGhostHeader::GetTicks() const
{
	return bytes_be_to_uint(m_aNumTicks);
}

int CGhostHeader::GetTime() const
{
	return bytes_be_to_uint(m_aTime);
}

CGhostInfo CGhostHeader::ToGhostInfo() const
{
	CGhostInfo Result;
	str_copy(Result.m_aOwner, m_aOwner);
	str_copy(Result.m_aMap, m_aMap);
	Result.m_NumTicks = GetTicks();
	Result.m_Time = GetTime();
	return Result;
}

IOHANDLE CGhostRecorder::CFileOperations::Open(IStorage *pStorage, const char *pFilename) noexcept
{
	try
	{
		if(!GhostPathFits(pStorage, pFilename))
			return nullptr;
		return pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	}
	catch(...)
	{
		return nullptr;
	}
}

bool CGhostRecorder::CFileOperations::Rename(IStorage *pStorage, const char *pSource, const char *pDestination) noexcept
{
	try
	{
		if(!GhostPathFits(pStorage, pSource) || !GhostPathFits(pStorage, pDestination))
			return false;
		return pStorage->RenameFile(pSource, pDestination, IStorage::TYPE_SAVE, false);
	}
	catch(...)
	{
		return false;
	}
}

bool CGhostRecorder::CFileOperations::Remove(IStorage *pStorage, const char *pFilename) noexcept
{
	try
	{
		if(!GhostPathFits(pStorage, pFilename))
			return false;
		return pStorage->RemoveFile(pFilename, IStorage::TYPE_SAVE);
	}
	catch(...)
	{
		return false;
	}
}

CGhostRecorder::CGhostRecorder(CFileOperations *pOperations) :
	m_File(nullptr), m_pStorage(nullptr), m_pOperations(pOperations ? pOperations : &m_DefaultOperations)
{
	m_aFilename[0] = '\0';
	m_aTemporary[0] = '\0';
	ResetBuffer();
}

CGhostRecorder::~CGhostRecorder()
{
	// 常规退出由Stop收尾；析构只关闭句柄，不依赖存储接口仍然存活。
	if(m_File)
		m_pOperations->Close(m_File);
}

void CGhostRecorder::Init()
{
	m_pStorage = Kernel()->RequestInterface<IStorage>();
}

void CGhostRecorder::Log(const char *pMessage, const char *pFilename) const noexcept
{
	try
	{
		log_info_color(LOG_COLOR_GHOST, "ghost_recorder", "%s '%s'", pMessage, pFilename ? pFilename : m_aFilename);
	}
	catch(...)
	{
	}
}

void CGhostRecorder::Fail(const char *pMessage) noexcept
{
	m_WriteError = true;
	m_LastItem = std::nullopt;
	ResetBuffer();
	Log(pMessage);
}

bool CGhostRecorder::Write(const void *pData, unsigned Size)
{
	if(!m_File || m_WriteError)
		return false;
	if(m_pOperations->Write(m_File, pData, Size) != Size || m_pOperations->Error(m_File) != 0)
	{
		Fail("Failed to write recording to");
		return false;
	}
	return true;
}

int CGhostRecorder::Start(const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, const char *pName)
{
	if(m_File || !m_pStorage || !pFilename || !pFilename[0] || str_length(pFilename) >= static_cast<int>(sizeof(m_aTemporary)) - 4)
		return -1;
	m_WriteError = false;
	m_LastItem = std::nullopt;
	ResetBuffer();
	str_copy(m_aFilename, pFilename);
	str_format(m_aTemporary, sizeof(m_aTemporary), "%s.tmp", pFilename);
	m_File = m_pOperations->Open(m_pStorage, m_aTemporary);
	if(!m_File)
	{
		m_WriteError = true;
		Log("Unable to open recording for");
		m_aTemporary[0] = '\0';
		return -1;
	}

	CGhostHeader Header{};
	mem_copy(Header.m_aMarker, gs_aHeaderMarker, sizeof(Header.m_aMarker));
	Header.m_Version = gs_CurVersion;
	str_copy(Header.m_aOwner, pName);
	str_copy(Header.m_aMap, pMap);
	Header.m_MapSha256 = MapSha256;
	if(!Write(&Header, sizeof(Header)))
	{
		Stop(0, -1);
		return -1;
	}
	Log("Recording to");
	return 0;
}

void CGhostRecorder::ResetBuffer()
{
	m_pBufferPos = m_aBuffer;
	m_pBufferEnd = m_aBuffer + sizeof(m_aBuffer);
	m_BufferNumItems = 0;
}

static void DiffItem(const uint32_t *pPast, const uint32_t *pCurrent, uint32_t *pOut, size_t Size)
{
	while(Size)
	{
		*pOut = *pCurrent - *pPast;
		pOut++;
		pPast++;
		pCurrent++;
		Size--;
	}
}

bool CGhostRecorder::WriteData(int Type, const void *pData, size_t Size)
{
	if(!m_File || m_WriteError)
		return false;
	dbg_assert(Type >= 0 && Type <= (int)std::numeric_limits<unsigned char>::max(), "Type invalid");
	dbg_assert(Size > 0 && Size <= MAX_ITEM_SIZE && Size % sizeof(uint32_t) == 0, "Size invalid");
	if(static_cast<size_t>(m_pBufferEnd - m_pBufferPos) < Size && !FlushChunk())
		return false;
	CGhostItem Data;
	mem_copy(Data.m_aData, pData, Size);
	Data.m_Size = Size;
	Data.m_Type = Type;
	if(m_LastItem.has_value() && m_LastItem->m_Type == Type)
	{
		dbg_assert(m_LastItem->m_Size == Size, "Size mismatch between current and last item");
		DiffItem((const uint32_t *)m_LastItem->m_aData, (const uint32_t *)Data.m_aData, (uint32_t *)m_pBufferPos, Size / sizeof(uint32_t));
	}
	else
	{
		if(!FlushChunk())
			return false;
		mem_copy(m_pBufferPos, Data.m_aData, Size);
	}
	m_LastItem = Data;
	m_pBufferPos += Size;
	++m_BufferNumItems;
	return m_BufferNumItems < NUM_ITEMS_PER_CHUNK || FlushChunk();
}

bool CGhostRecorder::FlushChunk()
{
	if(!m_File || m_WriteError)
		return false;
	int Size = m_pBufferPos - m_aBuffer;
	if(Size == 0 || m_BufferNumItems == 0)
		return true;
	dbg_assert(Size % sizeof(uint32_t) == 0, "Chunk size invalid");
	dbg_assert(m_LastItem.has_value(), "Tried to flush chunk without last item");
	Size = CVariableInt::Compress(m_aBuffer, Size, m_aBufferTemp, sizeof(m_aBufferTemp));
	if(Size <= 0)
	{
		Fail("Failed intpack compression for");
		return false;
	}
	Size = CNetBase::Compress(m_aBufferTemp, Size, m_aBuffer, sizeof(m_aBuffer));
	if(Size <= 0)
	{
		Fail("Failed network compression for");
		return false;
	}
	unsigned char aChunkHeader[4];
	aChunkHeader[0] = m_LastItem->m_Type & 0xff;
	aChunkHeader[1] = m_BufferNumItems & 0xff;
	aChunkHeader[2] = (Size >> 8) & 0xff;
	aChunkHeader[3] = Size & 0xff;
	if(!Write(aChunkHeader, sizeof(aChunkHeader)) || !Write(m_aBuffer, Size))
		return false;
	m_LastItem = std::nullopt;
	ResetBuffer();
	return true;
}

bool CGhostRecorder::Stop(int Ticks, int Time, const char *pFilename)
{
	if(!m_File)
		return false;
	const char *pDestination = pFilename ? pFilename : m_aFilename;
	const bool Keep = Ticks > 0 && Time > 0;
	if(Keep && !m_WriteError)
	{
		if(!pDestination[0] || str_length(pDestination) >= IO_MAX_PATH_LENGTH)
			Fail("Invalid recording destination for");
		else if(FlushChunk())
		{
			if(m_pOperations->Seek(m_File, offsetof(CGhostHeader, m_aNumTicks), IOSEEK_START) != 0)
				Fail("Failed to seek recording header for");
			else
			{
				unsigned char aNumTicks[sizeof(int32_t)], aTime[sizeof(int32_t)];
				uint_to_bytes_be(aNumTicks, Ticks);
				uint_to_bytes_be(aTime, Time);
				if(Write(aNumTicks, sizeof(aNumTicks)))
					Write(aTime, sizeof(aTime));
			}
		}
	}
	if(m_pOperations->Error(m_File) != 0)
		m_WriteError = true;
	IOHANDLE File = m_File;
	m_File = nullptr;
	if(m_pOperations->Close(File) != 0)
		Fail("Failed to close recording for");
	bool Saved = Keep && !m_WriteError;
	if(Saved && !m_pOperations->Rename(m_pStorage, m_aTemporary, pDestination))
	{
		Fail("Failed to publish recording for");
		Saved = false;
	}
	if(!Saved)
		m_pOperations->Remove(m_pStorage, m_aTemporary);
	Log(Saved ? "Saved recording to" : "Recording was not saved to", pDestination);
	m_aFilename[0] = '\0';
	m_aTemporary[0] = '\0';
	m_LastItem = std::nullopt;
	ResetBuffer();
	return Saved;
}

CGhostLoader::CGhostLoader()
{
	m_File = nullptr;
	m_aFilename[0] = '\0';
	ResetBuffer();
}

void CGhostLoader::Init()
{
	m_pStorage = Kernel()->RequestInterface<IStorage>();
}

void CGhostLoader::ResetBuffer()
{
	m_pBufferPos = m_aBuffer;
	m_pBufferEnd = m_aBuffer;
	m_BufferNumItems = 0;
	m_BufferCurItem = 0;
	m_BufferPrevItem = -1;
}

IOHANDLE CGhostLoader::ReadHeader(CGhostHeader &Header, const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc, bool LogMapMismatch) const
{
	IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to open ghost file '%s' for reading", pFilename);
		return nullptr;
	}

	if(io_read(File, &Header, sizeof(Header) - sizeof(SHA256_DIGEST)) != sizeof(Header) - sizeof(SHA256_DIGEST))
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': failed to read header", pFilename);
		io_close(File);
		return nullptr;
	}

	if(!ValidateHeader(Header, pFilename))
	{
		io_close(File);
		return nullptr;
	}

	if(Header.m_Version < 6)
	{
		mem_zero(&Header.m_MapSha256, sizeof(Header.m_MapSha256));
	}
	else if(io_read(File, &Header.m_MapSha256, sizeof(SHA256_DIGEST)) != sizeof(SHA256_DIGEST))
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': failed to read header map SHA256", pFilename);
		io_close(File);
		return nullptr;
	}

	if(!CheckHeaderMap(Header, pFilename, pMap, MapSha256, MapCrc, LogMapMismatch))
	{
		io_close(File);
		return nullptr;
	}

	return File;
}

bool CGhostLoader::ValidateHeader(const CGhostHeader &Header, const char *pFilename) const
{
	if(mem_comp(Header.m_aMarker, gs_aHeaderMarker, sizeof(gs_aHeaderMarker)) != 0)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': invalid header marker", pFilename);
		return false;
	}

	if(Header.m_Version < 4 || Header.m_Version > gs_CurVersion)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': ghost version '%d' is not supported", pFilename, Header.m_Version);
		return false;
	}

	if(!mem_has_null(Header.m_aOwner, sizeof(Header.m_aOwner)) || !str_utf8_check(Header.m_aOwner))
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': owner name is invalid", pFilename);
		return false;
	}

	if(!mem_has_null(Header.m_aMap, sizeof(Header.m_aMap)) || !str_utf8_check(Header.m_aMap))
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': map name is invalid", pFilename);
		return false;
	}

	const int NumTicks = Header.GetTicks();
	if(NumTicks <= 0)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': number of ticks '%d' is invalid", pFilename, NumTicks);
		return false;
	}

	const int Time = Header.GetTime();
	if(Time <= 0)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': time '%d' is invalid", pFilename, Time);
		return false;
	}

	return true;
}

bool CGhostLoader::CheckHeaderMap(const CGhostHeader &Header, const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc, bool LogMapMismatch) const
{
	if(str_comp(Header.m_aMap, pMap) != 0)
	{
		if(LogMapMismatch)
		{
			log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': ghost map name '%s' does not match current map '%s'", pFilename, Header.m_aMap, pMap);
		}
		return false;
	}

	if(Header.m_Version >= 6)
	{
		if(Header.m_MapSha256 != MapSha256 && g_Config.m_ClRaceGhostStrictMap)
		{
			if(LogMapMismatch)
			{
				char aGhostSha256[SHA256_MAXSTRSIZE];
				sha256_str(Header.m_MapSha256, aGhostSha256, sizeof(aGhostSha256));
				char aMapSha256[SHA256_MAXSTRSIZE];
				sha256_str(MapSha256, aMapSha256, sizeof(aMapSha256));
				log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': ghost map SHA256 mismatch (wanted='%s', ghost='%s')", pFilename, aMapSha256, aGhostSha256);
			}
			return false;
		}
	}
	else
	{
		const unsigned GhostMapCrc = bytes_be_to_uint(Header.m_aZeroes);
		if(GhostMapCrc != MapCrc && g_Config.m_ClRaceGhostStrictMap)
		{
			if(LogMapMismatch)
			{
				log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': ghost map CRC mismatch (wanted='%08x', ghost='%08x')", pFilename, MapCrc, GhostMapCrc);
			}
			return false;
		}
	}

	return true;
}

bool CGhostLoader::Load(const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc)
{
	dbg_assert(!m_File, "File already open");

	CGhostHeader Header;
	IOHANDLE File = ReadHeader(Header, pFilename, pMap, MapSha256, MapCrc, true);
	if(!File)
	{
		return false;
	}

	m_File = File;
	str_copy(m_aFilename, pFilename);
	m_Header = Header;
	m_Info = m_Header.ToGhostInfo();
	m_LastItem = std::nullopt;
	ResetBuffer();
	return true;
}

bool CGhostLoader::ReadChunk(int *pType)
{
	if(m_Header.m_Version != 4)
	{
		m_LastItem = std::nullopt;
	}
	ResetBuffer();

	unsigned char aChunkHeader[4];
	const unsigned ReadHeaderSize = io_read(m_File, aChunkHeader, sizeof(aChunkHeader));
	if(ReadHeaderSize != sizeof(aChunkHeader))
	{
		if(ReadHeaderSize != 0)
		{
			log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': chunk header truncated", m_aFilename);
		}
		return false; // EOF
	}

	*pType = aChunkHeader[0];
	int Size = (aChunkHeader[2] << 8) | aChunkHeader[3];
	m_BufferNumItems = aChunkHeader[1];

	if(Size <= 0 || Size > MAX_CHUNK_SIZE)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': invalid chunk header size", m_aFilename);
		return false;
	}

	if(io_read(m_File, m_aBuffer, Size) != (unsigned)Size)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': error reading chunk data", m_aFilename);
		return false;
	}

	Size = CNetBase::Decompress(m_aBuffer, Size, m_aBufferTemp, sizeof(m_aBufferTemp));
	if(Size < 0)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': error during network decompression", m_aFilename);
		return false;
	}

	Size = CVariableInt::Decompress(m_aBufferTemp, Size, m_aBuffer, sizeof(m_aBuffer));
	if(Size < 0)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': error during intpack decompression", m_aFilename);
		return false;
	}

	m_pBufferEnd = m_aBuffer + Size;
	return true;
}

bool CGhostLoader::ReadNextType(int *pType)
{
	dbg_assert((bool)m_File, "File not open");

	if(m_BufferCurItem != m_BufferPrevItem && m_BufferCurItem < m_BufferNumItems)
	{
		dbg_assert(m_LastItem.has_value(), "Buffer contains items but last item is unset");
		*pType = m_LastItem.value().m_Type;
	}
	else if(!ReadChunk(pType))
	{
		return false; // error or EOF
	}

	m_BufferPrevItem = m_BufferCurItem;
	return true;
}

static void UndiffItem(const uint32_t *pPast, const uint32_t *pDiff, uint32_t *pOut, size_t Size)
{
	while(Size)
	{
		*pOut = *pPast + *pDiff;
		pOut++;
		pPast++;
		pDiff++;
		Size--;
	}
}

bool CGhostLoader::ReadData(int Type, void *pData, size_t Size)
{
	dbg_assert((bool)m_File, "File not open");
	dbg_assert(Type >= 0 && Type <= (int)std::numeric_limits<unsigned char>::max(), "Type invalid");
	dbg_assert(Size > 0 && Size <= MAX_ITEM_SIZE && Size % sizeof(uint32_t) == 0, "Size invalid");

	if((size_t)(m_pBufferEnd - m_pBufferPos) < Size)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': not enough data (type='%d', got='%" PRIzu "', wanted='%" PRIzu "')", m_aFilename, Type, (size_t)(m_pBufferEnd - m_pBufferPos), Size);
		return false;
	}

	CGhostItem Data;
	Data.m_Size = Size;
	Data.m_Type = Type;
	if(m_LastItem.has_value())
	{
		// 旧格式跨块保留差分基准，损坏文件中的类型或尺寸不匹配应返回读取失败。
		if(m_LastItem.value().m_Type != Data.m_Type || m_LastItem.value().m_Size != Data.m_Size)
		{
			log_error_color(LOG_COLOR_GHOST, "ghost_loader", "Failed to read ghost file '%s': incompatible delta base", m_aFilename);
			return false;
		}
		UndiffItem((const uint32_t *)m_LastItem.value().m_aData, (const uint32_t *)m_pBufferPos, (uint32_t *)Data.m_aData, Data.m_Size / sizeof(uint32_t));
	}
	else
	{
		mem_copy(Data.m_aData, m_pBufferPos, Data.m_Size);
	}

	mem_copy(pData, Data.m_aData, Data.m_Size);

	m_LastItem = Data;
	m_pBufferPos += Data.m_Size;
	m_BufferCurItem++;
	return true;
}

void CGhostLoader::Close()
{
	if(!m_File)
	{
		return;
	}

	io_close(m_File);
	m_File = nullptr;
	m_aFilename[0] = '\0';
}

bool CGhostLoader::GetGhostInfo(const char *pFilename, CGhostInfo *pGhostInfo, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc)
{
	CGhostHeader Header;
	IOHANDLE File = ReadHeader(Header, pFilename, pMap, MapSha256, MapCrc, false);
	if(!File)
	{
		return false;
	}
	io_close(File);
	*pGhostInfo = Header.ToGhostInfo();
	return true;
}
