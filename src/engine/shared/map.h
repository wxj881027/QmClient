/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_MAP_H
#define ENGINE_SHARED_MAP_H

#include "datafile.h"

#include <base/types.h>

#include <engine/map.h>

#include <functional>
#include <set>

class CMapItemLayerTilemap;
class CMapItemLayerTilemap_v2;

class CMap : public IEngineMap
{
	CDataFileReader m_DataFile;
	IStorage *m_pStorage;

public:
	explicit CMap(IStorage *pStorage = nullptr);

	CDataFileReader *GetReader() { return &m_DataFile; }

	int GetDataSize(int Index) const override;
	void *GetData(int Index) override;
	void *GetDataSwapped(int Index) override;
	const char *GetDataString(int Index) override;
	void UnloadData(int Index) override;
	int NumData() const override;

	int GetItemSize(int Index) override;
	void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr) override;
	void GetType(int Type, int *pStart, int *pNum) override;
	int FindItemIndex(int Type, int Id) override;
	void *FindItem(int Type, int Id) override;
	int NumItems() const override;

	[[nodiscard]] bool Load(const char *pMapName, int StorageType) override;
	void Adopt(CMap &&Other);
	void Unload() override;
	bool IsLoaded() const override;
	IOHANDLE File() const override;

	SHA256_DIGEST Sha256() const override;
	unsigned Crc() const override;
	int Size() const override;

	// map_generator 等本地工具需要直接解包 tileskip 数据
	static bool ExtractTiles(class CTile *pDest, size_t DestSize, const class CTile *pSrc, size_t SrcSize);
	// 完整校验压缩流后才分配；分配器返回可交由 free 回收的缓冲，成功时由调用方接管。
	static class CTile *DecodeTileskip(size_t DestSize, const class CTile *pSrc, size_t SrcSize, const std::function<void *(size_t)> &Allocate = {});

private:
	static bool ValidateTileskip(size_t DestSize, const class CTile *pSrc, size_t SrcSize);
	static void ExtractValidatedTiles(class CTile *pDest, size_t DestSize, const class CTile *pSrc, size_t SrcSize);
	static bool ValidateMapVersion(CDataFileReader &NewDataFile);
	bool UpgradeAndValidateTilesLayerItem(CDataFileReader &NewDataFile, int GroupIndex, int LayerIndex,
		CMapItemLayerTilemap_v2 *pLayerTilemapBase, int LayerItemIndex, size_t LayerItemSize);
	bool ValidateAndUnpackTilesLayerData(CDataFileReader &NewDataFile, int GroupIndex, int LayerIndex, const CMapItemLayerTilemap *pLayerTilemap,
		const CMapItemLayerTilemap &GameLayer, std::set<int> &UsedDataIndices);
};

#endif
