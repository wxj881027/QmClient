#ifndef ENGINE_CLIENT_GHOST_H
#define ENGINE_CLIENT_GHOST_H

#include <base/system.h>

#include <engine/ghost.h>

#include <cstdint>
#include <optional>

class IStorage;

enum
{
	MAX_ITEM_SIZE = 128,
	NUM_ITEMS_PER_CHUNK = 50,
	MAX_CHUNK_SIZE = MAX_ITEM_SIZE * NUM_ITEMS_PER_CHUNK,
};
static_assert(MAX_CHUNK_SIZE % sizeof(uint32_t) == 0, "Chunk size must be aligned with uint32_t");

// version 4-6
struct CGhostHeader
{
	unsigned char m_aMarker[8];
	unsigned char m_Version;
	char m_aOwner[MAX_NAME_LENGTH];
	char m_aMap[64];
	unsigned char m_aZeroes[sizeof(int32_t)]; // Crc before version 6
	unsigned char m_aNumTicks[sizeof(int32_t)];
	unsigned char m_aTime[sizeof(int32_t)];
	SHA256_DIGEST m_MapSha256;

	int GetTicks() const;
	int GetTime() const;
	CGhostInfo ToGhostInfo() const;
};

class CGhostItem
{
public:
	alignas(uint32_t) unsigned char m_aData[MAX_ITEM_SIZE];
	size_t m_Size;
	int m_Type;
};

class CGhostRecorder : public IGhostRecorder
{
public:
	// 文件操作覆盖只替换外部I/O，回调不得重入且必须不抛异常。
	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual IOHANDLE Open(class IStorage *pStorage, const char *pFilename) noexcept;
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept { return io_write(File, pData, Size); }
		virtual int Seek(IOHANDLE File, int64_t Offset, ESeekOrigin Origin) noexcept { return io_seek(File, Offset, Origin); }
		virtual int Error(IOHANDLE File) noexcept { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		virtual bool Rename(class IStorage *pStorage, const char *pSource, const char *pDestination) noexcept;
		virtual bool Remove(class IStorage *pStorage, const char *pFilename) noexcept;
	};

private:
	IOHANDLE m_File;
	char m_aFilename[IO_MAX_PATH_LENGTH];
	char m_aTemporary[IO_MAX_PATH_LENGTH];
	class IStorage *m_pStorage;
	CFileOperations m_DefaultOperations;
	CFileOperations *m_pOperations;
	bool m_WriteError = false;

	alignas(uint32_t) char m_aBuffer[MAX_CHUNK_SIZE];
	alignas(uint32_t) char m_aBufferTemp[MAX_CHUNK_SIZE];
	char *m_pBufferPos;
	const char *m_pBufferEnd;
	int m_BufferNumItems;
	std::optional<CGhostItem> m_LastItem;

	void ResetBuffer();
	bool FlushChunk();
	bool Write(const void *pData, unsigned Size);
	void Fail(const char *pMessage) noexcept;
	void Log(const char *pMessage, const char *pFilename = nullptr) const noexcept;

public:
	// 覆盖的操作对象须存活至记录器析构。
	explicit CGhostRecorder(CFileOperations *pOperations = nullptr);
	~CGhostRecorder() override;
	CGhostRecorder(const CGhostRecorder &) = delete;
	CGhostRecorder &operator=(const CGhostRecorder &) = delete;

	void Init();

	int Start(const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, const char *pName) override;
	bool Stop(int Ticks, int Time, const char *pFilename = nullptr) override;

	bool WriteData(int Type, const void *pData, size_t Size) override;
	bool IsRecording() const override { return m_File != nullptr; }
};

class CGhostLoader : public IGhostLoader
{
	IOHANDLE m_File;
	char m_aFilename[IO_MAX_PATH_LENGTH];
	class IStorage *m_pStorage;

	CGhostHeader m_Header;
	CGhostInfo m_Info;

	alignas(uint32_t) char m_aBuffer[MAX_CHUNK_SIZE];
	alignas(uint32_t) char m_aBufferTemp[MAX_CHUNK_SIZE];
	char *m_pBufferPos;
	const char *m_pBufferEnd;
	int m_BufferNumItems;
	int m_BufferCurItem;
	int m_BufferPrevItem;
	std::optional<CGhostItem> m_LastItem;

	void ResetBuffer();
	IOHANDLE ReadHeader(CGhostHeader &Header, const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc, bool LogMapMismatch) const;
	bool ValidateHeader(const CGhostHeader &Header, const char *pFilename) const;
	bool CheckHeaderMap(const CGhostHeader &Header, const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc, bool LogMapMismatch) const;
	bool ReadChunk(int *pType);

public:
	CGhostLoader();

	void Init();

	bool Load(const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc) override;
	void Close() override;
	const CGhostInfo *GetInfo() const override { return &m_Info; }

	bool ReadNextType(int *pType) override;
	bool ReadData(int Type, void *pData, size_t Size) override;

	bool GetGhostInfo(const char *pFilename, CGhostInfo *pGhostInfo, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc) override;
};
#endif
