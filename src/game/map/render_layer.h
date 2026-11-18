#ifndef GAME_MAP_RENDER_LAYER_H
#define GAME_MAP_RENDER_LAYER_H

#include <array>
#include <cstdint>

using offset_ptr_size = char *;
using offset_ptr = uintptr_t;
using offset_ptr32 = unsigned int;

#include <base/color.h>

#include <engine/graphics.h>

#include <game/map/envelope_manager.h>
#include <game/map/map_load_task.h>
#include <game/map/render_component.h>
#include <game/map/render_map.h>
#include <game/mapitems.h>
#include <game/mapitems_ex.h>

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

// 一次 RenderTileLayer 提交：起始索引偏移与索引数量。
struct STileLayerDrawRange
{
	offset_ptr m_Offset;
	unsigned int m_Count;
};

// 把可见行区间折算成提交段：整层左右边界都在可见区时所有可见行连续，只有一段；
// 否则逐行合并相邻行。与渲染后端无关，可单独测量。
template<typename TOffsetAt, typename TDrawAt>
void QmBuildTileLayerDrawRanges(int Width, int X0, int X1, int Y0, int Y1, TOffsetAt OffsetAt, TDrawAt DrawAt, std::vector<STileLayerDrawRange> &vRanges)
{
	vRanges.clear();
	if(Width <= 0 || X0 < 0 || X1 > Width || Y0 >= Y1)
		return;

	if(X0 == 0 && X1 == Width)
	{
		const size_t StartIndex = (size_t)Y0 * (size_t)Width;
		const size_t EndIndex = (size_t)Y1 * (size_t)Width - 1;
		const offset_ptr Start = OffsetAt(StartIndex);
		const offset_ptr End = OffsetAt(EndIndex);
		dbg_assert(End >= Start, "Tile offsets are not monotone.");
		const unsigned int Count = (unsigned int)((End - Start) / sizeof(unsigned int)) + (DrawAt(EndIndex) ? 6u : 0u);
		if(Count != 0)
			vRanges.push_back({Start, Count});
		return;
	}

	const int XR = X1 - 1;
	for(int y = Y0; y < Y1; ++y)
	{
		const size_t StartIndex = (size_t)y * (size_t)Width + (size_t)X0;
		const size_t EndIndex = (size_t)y * (size_t)Width + (size_t)XR;
		const offset_ptr RowStart = OffsetAt(StartIndex);
		const offset_ptr RowEnd = OffsetAt(EndIndex);
		dbg_assert(RowEnd >= RowStart, "Tile offsets are not monotone.");
		const unsigned int Count = (unsigned int)((RowEnd - RowStart) / sizeof(unsigned int)) + (DrawAt(EndIndex) ? 6u : 0u);
		if(Count == 0)
			continue;
		if(!vRanges.empty())
		{
			STileLayerDrawRange &Last = vRanges.back();
			if(Last.m_Offset + (offset_ptr)Last.m_Count * sizeof(unsigned int) == RowStart &&
				std::numeric_limits<unsigned int>::max() - Last.m_Count >= Count)
			{
				Last.m_Count += Count;
				continue;
			}
		}
		vRanges.push_back({RowStart, Count});
	}
}

class CMapLayers;
class CMapItemLayerTilemap;
class CMapItemLayerQuads;
class IMap;
class CMapImages;

constexpr int BorderRenderDistance = 201;

class CClipRegion
{
public:
	CClipRegion() = default;
	CClipRegion(float X, float Y, float Width, float Height) :
		m_X(X), m_Y(Y), m_Width(Width), m_Height(Height) {}

	float m_X;
	float m_Y;
	float m_Width;
	float m_Height;
};

class CRenderLayerParams
{
public:
	int m_RenderType;
	int m_EntityOverlayVal;
	vec2 m_Center;
	float m_Zoom;
	bool m_RenderText;
	bool m_RenderInvalidTiles;
	bool m_RenderTileBorder;
	bool m_DebugRenderGroupClips;
	bool m_DebugRenderQuadClips;
	bool m_DebugRenderClusterClips;
	bool m_DebugRenderTileClips;
};

class CRenderLayer : public CRenderComponent
{
public:
	CRenderLayer(int GroupId, int LayerId, int Flags);
	virtual void OnInit(IGraphics *pGraphics, ITextRender *pTextRender, CRenderMap *pRenderMap, std::shared_ptr<CEnvelopeManager> &pEnvelopeManager, IMap *pMap, IMapImages *pMapImages, bool TileAndQuadBuffering);

	virtual CMapLoadTask Init() = 0;
	virtual CMapLoadTask Prepare() { return Init(); }
	virtual void Render(const CRenderLayerParams &Params) = 0;
	virtual bool DoRender(const CRenderLayerParams &Params) = 0;
	virtual bool IsValid() const { return true; }
	virtual bool IsGroup() const { return false; }
	virtual bool IsDesignLayer() const { return true; }
	virtual void Unload() = 0;

	bool IsVisibleInClipRegion(const std::optional<CClipRegion> &ClipRegion) const;
	int GetGroup() const { return m_GroupId; }
	int GetLayer() const { return m_LayerId; }

protected:
	bool m_TileAndQuadBuffering = true;
	int m_GroupId;
	int m_LayerId;
	int m_Flags;

	void UseTexture(IGraphics::CTextureHandle TextureHandle);
	virtual IGraphics::CTextureHandle GetTexture() const = 0;

	class IMap *m_pMap = nullptr;
	IMapImages *m_pMapImages = nullptr;
	std::shared_ptr<CEnvelopeManager> m_pEnvelopeManager;
	std::optional<CClipRegion> m_LayerClip;
};

class CRenderLayerGroup : public CRenderLayer
{
public:
	CRenderLayerGroup(int GroupId, CMapItemGroup *pGroup);
	~CRenderLayerGroup() override = default;
	CMapLoadTask Init() override;
	void Render(const CRenderLayerParams &Params) override;
	bool DoRender(const CRenderLayerParams &Params) override;
	bool IsValid() const override { return m_pGroup != nullptr; }
	bool IsGroup() const override { return true; }
	void Unload() override {}

protected:
	IGraphics::CTextureHandle GetTexture() const override { return IGraphics::CTextureHandle(); }

	std::optional<std::array<float, 4>> ScreenForGroup(const CRenderLayerParams &Params) const;
	CMapItemGroup *m_pGroup;
};

class CRenderLayerTile : public CRenderLayer
{
public:
	CRenderLayerTile(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	~CRenderLayerTile() override = default;
	void Render(const CRenderLayerParams &Params) override;
	bool DoRender(const CRenderLayerParams &Params) override;
	CMapLoadTask Init() override;
	void OnInit(IGraphics *pGraphics, ITextRender *pTextRender, CRenderMap *pRenderMap, std::shared_ptr<CEnvelopeManager> &pEnvelopeManager, IMap *pMap, IMapImages *pMapImages, bool TileAndQuadBuffering) override;

	CMapLoadTask Prepare() override;
	virtual int GetDataIndex() const;
	bool IsValid() const override
	{
		// 设计层上传 GPU 后会释放原始 tile 数据，有效性不能只看 map 数据。
		return m_pLayerTilemap != nullptr && m_pLayerTilemap->m_Width > 0 && m_pLayerTilemap->m_Height > 0 &&
		       (m_pTiles != nullptr || (m_VisualTiles.has_value() && m_VisualTiles->m_BufferContainerIndex != -1) ||
			       (m_pMap != nullptr && m_pMap->GetDataSize(GetDataIndex()) > 0));
	}
	void Unload() override;

protected:
	virtual void *GetRawData() const;
	template<class T>
	T *GetData() const;

	virtual ColorRGBA GetRenderColor(const CRenderLayerParams &Params) const;
	virtual void InitTileData();
	virtual void GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const;
	IGraphics::CTextureHandle GetTexture() const override { return m_TextureHandle; }
	CTile *m_pTiles;

private:
	IGraphics::CTextureHandle m_TextureHandle;

protected:
	class CTileLayerVisuals : public CRenderComponent
	{
	public:
		CTileLayerVisuals()
		{
			m_Width = 0;
			m_Height = 0;
			m_BufferContainerIndex = -1;
			m_IsTextured = false;
		}

		bool Init(unsigned int Width, unsigned int Height);
		void Unload();

		class CTileVisual
		{
		public:
			CTileVisual() :
				m_IndexBufferByteOffset(0) {}

		private:
			offset_ptr32 m_IndexBufferByteOffset;

		public:
			bool DoDraw() const
			{
				return (m_IndexBufferByteOffset & 0x10000000) != 0;
			}

			void Draw(bool SetDraw)
			{
				m_IndexBufferByteOffset = (SetDraw ? 0x10000000 : (offset_ptr32)0) | (m_IndexBufferByteOffset & 0xEFFFFFFF);
			}

			offset_ptr IndexBufferByteOffset() const
			{
				return ((offset_ptr)(m_IndexBufferByteOffset & 0xEFFFFFFF) * 6 * sizeof(uint32_t));
			}

			void SetIndexBufferByteOffset(offset_ptr32 IndexBufferByteOff)
			{
				m_IndexBufferByteOffset = IndexBufferByteOff | (m_IndexBufferByteOffset & 0x10000000);
			}

			void AddIndexBufferByteOffset(offset_ptr32 IndexBufferByteOff)
			{
				m_IndexBufferByteOffset = ((m_IndexBufferByteOffset & 0xEFFFFFFF) + IndexBufferByteOff) | (m_IndexBufferByteOffset & 0x10000000);
			}
		};

		std::vector<CTileVisual> m_vTilesOfLayer;

		CTileVisual m_BorderTopLeft;
		CTileVisual m_BorderTopRight;
		CTileVisual m_BorderBottomRight;
		CTileVisual m_BorderBottomLeft;

		CTileVisual m_BorderKillTile; // end of map kill tile -- game layer only

		std::vector<CTileVisual> m_vBorderTop;
		std::vector<CTileVisual> m_vBorderLeft;
		std::vector<CTileVisual> m_vBorderRight;
		std::vector<CTileVisual> m_vBorderBottom;

		unsigned int m_Width;
		unsigned int m_Height;
		int m_BufferContainerIndex;
		bool m_IsTextured;
	};

	using FTileFilter = std::function<bool(unsigned char Index, unsigned char Flags, int AngleRotate, unsigned int x, unsigned int y, int CurOverlay)>;
	CMapLoadTask UploadTileData(std::optional<CTileLayerVisuals> &VisualsOptional, int CurOverlay, bool AddAsSpeedup, bool IsGameLayer = false, FTileFilter Filter = {});

	virtual void RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params);
	virtual void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params);

	void RenderTileLayer(const ColorRGBA &Color, const CRenderLayerParams &Params, CTileLayerVisuals *pTileLayerVisuals = nullptr);
	void RenderTileBorder(const ColorRGBA &Color, int BorderX0, int BorderY0, int BorderX1, int BorderY1, CTileLayerVisuals *pTileLayerVisuals);
	void RenderKillTileBorder(const ColorRGBA &Color);

	std::vector<char *> m_vpRenderTileLayerIndexOffsets;
	std::vector<unsigned int> m_vRenderTileLayerDrawCounts;
	std::vector<STileLayerDrawRange> m_vRenderTileLayerRanges;

	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTiles;
	CMapItemLayerTilemap *m_pLayerTilemap;
	ColorRGBA m_Color;
};

class CRenderLayerQuads : public CRenderLayer
{
public:
	CRenderLayerQuads(int GroupId, int LayerId, int Flags, CMapItemLayerQuads *pLayerQuads);
	void OnInit(IGraphics *pGraphics, ITextRender *pTextRender, CRenderMap *pRenderMap, std::shared_ptr<CEnvelopeManager> &pEnvelopeManager, IMap *pMap, IMapImages *pMapImages, bool TileAndQuadBuffering) override;
	CMapLoadTask Init() override;
	bool IsValid() const override { return m_pLayerQuads->m_NumQuads > 0 && m_pQuads; }
	void Render(const CRenderLayerParams &Params) override;
	bool DoRender(const CRenderLayerParams &Params) override;
	void Unload() override;

protected:
	IGraphics::CTextureHandle GetTexture() const override { return m_TextureHandle; }

	class CQuadLayerVisuals : public CRenderComponent
	{
	public:
		CQuadLayerVisuals() :
			m_QuadNum(0), m_BufferContainerIndex(-1), m_IsTextured(false) {}
		void Unload();

		int m_QuadNum;
		int m_BufferContainerIndex;
		bool m_IsTextured;
	};
	void RenderQuadLayer(float Alpha, const CRenderLayerParams &Params);

	std::optional<CRenderLayerQuads::CQuadLayerVisuals> m_VisualQuad;
	CMapItemLayerQuads *m_pLayerQuads;

	class CQuadCluster
	{
	public:
		bool m_Grouped;
		int m_StartIndex;
		int m_NumQuads;

		int m_PosEnv;
		float m_PosEnvOffset;
		int m_ColorEnv;
		float m_ColorEnvOffset;

		std::vector<SQuadRenderInfo> m_vQuadRenderInfo;
		std::optional<CClipRegion> m_ClipRegion;
	};
	CMapLoadTask CalculateClipping(CQuadCluster &QuadCluster);
	CMapLoadTask CalculateQuadClipping(const CQuadCluster &QuadCluster, float aQuadOffsetMin[2], float aQuadOffsetMax[2], bool &Available) const;

	std::vector<CQuadCluster> m_vQuadClusters;
	CQuad *m_pQuads;

private:
	IGraphics::CTextureHandle m_TextureHandle;
};

class CRenderLayerEntityBase : public CRenderLayerTile
{
public:
	CRenderLayerEntityBase(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	~CRenderLayerEntityBase() override = default;
	bool DoRender(const CRenderLayerParams &Params) override;

protected:
	ColorRGBA GetRenderColor(const CRenderLayerParams &Params) const override { return ColorRGBA(1.0f, 1.0f, 1.0f, Params.m_EntityOverlayVal / 100.0f); }
	IGraphics::CTextureHandle GetTexture() const override;
	// 实体层在运行期仍要读取 tile 数据，不能在上传 GPU 后释放。
	bool IsDesignLayer() const override { return false; }
};

class CRenderLayerEntityGame final : public CRenderLayerEntityBase
{
public:
	CRenderLayerEntityGame(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	CMapLoadTask Init() override;
	void Unload() override;

protected:
	void RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;

private:
	ColorRGBA GetDeathBorderColor() const;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeath;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesFreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesUnfreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeepFreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeepUnfreeze;
};

class CRenderLayerEntityFront final : public CRenderLayerEntityBase
{
public:
	CRenderLayerEntityFront(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	int GetDataIndex() const override;
	CMapLoadTask Init() override;
	void Unload() override;

protected:
	void RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;

private:
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeath;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesFreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesUnfreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeepFreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeepUnfreeze;
};

class CRenderLayerEntityTele final : public CRenderLayerEntityBase
{
public:
	CRenderLayerEntityTele(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	int GetDataIndex() const override;
	CMapLoadTask Init() override;
	void Unload() override;

protected:
	void InitTileData() override;
	void RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const override;

private:
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualCheckpointTiles;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTeleNumbers;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualCheckpointNumbers;
	CTeleTile *m_pTeleTiles;
};

class CRenderLayerEntitySpeedup final : public CRenderLayerEntityBase
{
public:
	CRenderLayerEntitySpeedup(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	int GetDataIndex() const override;
	CMapLoadTask Init() override;
	void Unload() override;

protected:
	void InitTileData() override;
	void RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const override;
	IGraphics::CTextureHandle GetTexture() const override;

private:
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualForce;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualMaxSpeed;
	CSpeedupTile *m_pSpeedupTiles;
};

class CRenderLayerEntitySwitch final : public CRenderLayerEntityBase
{
public:
	CRenderLayerEntitySwitch(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	int GetDataIndex() const override;
	CMapLoadTask Init() override;
	void Unload() override;

protected:
	void InitTileData() override;
	void RenderTileLayerWithTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const override;
	IGraphics::CTextureHandle GetTexture() const override;

private:
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualSwitchNumberTop;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualSwitchNumberBottom;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesFreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesUnfreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeepFreeze;
	std::optional<CRenderLayerTile::CTileLayerVisuals> m_VisualTilesDeepUnfreeze;
	CSwitchTile *m_pSwitchTiles;
};

class CRenderLayerEntityTune final : public CRenderLayerEntityBase
{
public:
	CRenderLayerEntityTune(int GroupId, int LayerId, int Flags, CMapItemLayerTilemap *pLayerTilemap);
	int GetDataIndex() const override;
	CMapLoadTask Init() override;

protected:
	void InitTileData() override;
	void RenderTileLayerNoTileBuffer(const ColorRGBA &Color, const CRenderLayerParams &Params) override;
	void GetTileData(unsigned char *pIndex, unsigned char *pFlags, int *pAngleRotate, unsigned int x, unsigned int y, int CurOverlay) const override;
	IGraphics::CTextureHandle GetTexture() const override;

private:
	CTuneTile *m_pTuneTiles;
	mutable CTuneColorMapper m_TuneColorMapper;
};
#endif
