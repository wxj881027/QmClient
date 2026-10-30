/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_DATAFILE_H
#define ENGINE_SHARED_DATAFILE_H

#include "uuid_manager.h"

#include <base/hash.h>
#include <base/types.h>

#include <engine/storage.h>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <vector>

enum
{
	ITEMTYPE_EX = 0xFFFF,
};

/**
 * 数据处理器函数在某数据项首次成功解压后被调用。
 * 函数可以校验数据，出错时返回 `std::make_pair(nullptr, 0)`，成功时返回原始数据。
 * 函数也可以替换数据并返回新的数据指针与大小；新数据必须用 `malloc` 分配。
 * 返回不同数据或出错时，函数必须用 `free` 释放原始数据。
 */
typedef std::function<std::pair<void *, size_t>(void *pData, size_t Size)> FDataProcessor;

// raw datafile access
class CDataFileReader
{
	class CDatafile *m_pDataFile = nullptr;

	int GetExternalItemType(int InternalType, CUuid *pUuid);
	int GetInternalItemType(int ExternalType);

public:
	~CDataFileReader();
	CDataFileReader &operator=(CDataFileReader &&Other);

	[[nodiscard]] bool Open(class IStorage *pStorage, const char *pFilename, int StorageType);
	void Close();
	bool IsOpen() const;
	IOHANDLE File() const;

	int GetDataSize(int Index) const;
	void *GetData(int Index);
	void *GetDataSwapped(int Index); // makes sure that the data is 32bit LE ints when saved
	const char *GetDataString(int Index);
	void AddDataProcessor(int Index, FDataProcessor DataProcessor);
	void UnloadData(int Index);
	int NumData() const;

	int GetItemSize(int Index) const;
	void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr, CUuid *pUuid = nullptr);
	void GetType(int Type, int *pStart, int *pNum);
	int FindItemIndex(int Type, int Id);
	void *FindItem(int Type, int Id);
	bool OverrideItemData(int Index, const void *pData, size_t Size);
	int NumItems() const;

	SHA256_DIGEST Sha256() const;
	unsigned Crc() const;
	int Size() const;
};

// write access
class CDataFileWriter
{
public:
	enum ECompressionLevel
	{
		COMPRESSION_DEFAULT,
		COMPRESSION_BEST,
	};

	class IOperations
	{
	public:
		virtual ~IOperations() = default;
		// 返回的内存由 free 释放；注入对象须比写入器及其移动后的实例存活更久。
		virtual void *Allocate(size_t Size) noexcept;
		virtual bool Compress(void *pOutput, unsigned long &OutputSize, const void *pInput, unsigned long InputSize, int Level);
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size);
		virtual int Error(IOHANDLE File);
		// 即使返回失败也已消费句柄，不能重试关闭。
		virtual int Close(IOHANDLE File) noexcept;
	};

private:
	struct CFreeDeleter
	{
		void operator()(void *pData) const noexcept { std::free(pData); }
	};
	using CBuffer = std::unique_ptr<void, CFreeDeleter>;

	class CDataInfo
	{
	public:
		CBuffer m_pUncompressedData;
		int m_UncompressedSize;
		CBuffer m_pCompressedData;
		int m_CompressedSize;
		ECompressionLevel m_CompressionLevel;
	};

	class CItemInfo
	{
	public:
		int m_Type;
		int m_Id;
		int m_Size;
		int m_Next;
		int m_Prev;
		CBuffer m_pData;
	};

	class CItemTypeInfo
	{
	public:
		int m_Num = 0;
		int m_First = -1;
		int m_Last = -1;
	};

	class CExtendedItemType
	{
	public:
		int m_Type;
		CUuid m_Uuid;
	};

	IOperations m_DefaultOperations;
	IOperations *m_pOperations;
	IOHANDLE m_File = nullptr;
	bool m_Error = false;
	bool m_Finished = false;
	std::map<uint16_t, CItemTypeInfo, std::less<>> m_ItemTypes; // item types must be sorted in ascending order
	std::vector<CItemInfo> m_vItems;
	std::vector<CDataInfo> m_vDatas;
	std::vector<CExtendedItemType> m_vExtendedItemTypes;

	int GetTypeFromIndex(int Index) const;
	int GetExtendedItemTypeIndex(int Type, const CUuid *pUuid);
	bool WriteFile();
	bool Write(const void *pData, unsigned Size);

public:
	explicit CDataFileWriter(IOperations *pOperations = nullptr);
	// NOLINTNEXTLINE(bugprone-exception-escape)
	CDataFileWriter(CDataFileWriter &&Other) :
		m_pOperations(Other.m_pOperations == &Other.m_DefaultOperations ? &m_DefaultOperations : Other.m_pOperations),
		m_File(Other.m_File),
		m_Error(Other.m_Error),
		m_Finished(Other.m_Finished),
		m_ItemTypes(std::move(Other.m_ItemTypes)),
		m_vItems(std::move(Other.m_vItems)),
		m_vDatas(std::move(Other.m_vDatas)),
		m_vExtendedItemTypes(std::move(Other.m_vExtendedItemTypes))
	{
		Other.m_File = nullptr;
		Other.m_Error = true;
		Other.m_Finished = true;
	}
	~CDataFileWriter();

	[[nodiscard]] bool Open(class IStorage *pStorage, const char *pFilename, int StorageType = IStorage::TYPE_SAVE);
	int AddItem(int Type, int Id, size_t Size, const void *pData, const CUuid *pUuid = nullptr);
	int AddData(size_t Size, const void *pData, ECompressionLevel CompressionLevel = COMPRESSION_DEFAULT);
	int AddDataSwapped(size_t Size, const void *pData);
	int AddDataString(const char *pStr);
	// 只完成一次；任何准备、压缩、写入或关闭失败都禁止发布候选文件。
	bool Finish() noexcept;
};

#endif
