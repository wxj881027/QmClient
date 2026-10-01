/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MAPIMAGES_H
#define GAME_CLIENT_COMPONENTS_MAPIMAGES_H

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/map/render_interfaces.h>
#include <game/mapitems.h>

#include <memory>

class IJob;

enum EMapImageModType
{
	MAP_IMAGE_MOD_TYPE_DDNET = 0,
	MAP_IMAGE_MOD_TYPE_DDRACE,
	MAP_IMAGE_MOD_TYPE_RACE,
	MAP_IMAGE_MOD_TYPE_BLOCKWORLDS,
	MAP_IMAGE_MOD_TYPE_FNG,
	MAP_IMAGE_MOD_TYPE_VANILLA,
	MAP_IMAGE_MOD_TYPE_FDDRACE,

	MAP_IMAGE_MOD_TYPE_COUNT,
};

constexpr const char *const gs_apModEntitiesNames[] = {
	"ddnet",
	"ddrace",
	"race",
	"blockworlds",
	"fng",
	"vanilla",
	"f-ddrace",
};

class CMapImages : public CComponent, public IMapImages
{
	friend class CBackground;
	friend class CMenuBackground;

	IGraphics::CTextureHandle m_aTextures[MAX_MAPIMAGES];
	int m_Count;

	char m_aEntitiesPath[IO_MAX_PATH_LENGTH];
	// 当前生效的透明状态：选中 blank 且关闭自动回退时造全透明层。
	bool m_EntitiesIsBlank = false;

public:
	CMapImages();
	int Sizeof() const override { return sizeof(*this); }

	IGraphics::CTextureHandle Get(int Index) const override { return m_aTextures[Index]; }
	int Num() const override { return m_Count; }

	void OnMapLoadImpl(class CLayers *pLayers, class IMap *pMap);
	void OnMapLoad() override;
	void OnInit() override;
	void Unload();
	void LoadBackground(class CLayers *pLayers, class IMap *pMap);

	// DDRace
	IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType EntityLayerType) override;
	IGraphics::CTextureHandle GetSpeedupArrow() override;
	IGraphics::CTextureHandle GetTuneColors() override;

	IGraphics::CTextureHandle GetOverlayBottom() override;
	IGraphics::CTextureHandle GetOverlayTop() override;
	IGraphics::CTextureHandle GetOverlayCenter() override;

	void SetTextureScale(int Scale);
	int GetTextureScale() const;

	void ChangeEntitiesPath(const char *pPath);

	// 钩子碰撞预览 tile 的取图来源。
	enum class EHookPreviewTileSource
	{
		// 小图就绪：用返回的纹理画单个 tile quad，菜单会话无需加载完整 entities。
		SMALL_TEXTURE,
		// 完整 entities 纹理已在显存：调用方沿用 GetEntities+RenderTile 原路径（零加载成本）。
		FULL_ENTITIES_LOADED,
		// 空白包 / 包缺失 / 后台解码中：跳过 tile 绘制。
		UNAVAILABLE,
	};
	// 取钩子碰撞预览 tile 纹理；返回来源状态，SMALL_TEXTURE 时填充 Texture。
	EHookPreviewTileSource GetHookPreviewTileSource(int TileIndex, IGraphics::CTextureHandle &Texture);
	// 空闲预热：确保预览小图后台解码 job 在跑（幂等），把解码成本挪离首次点击。
	void RequestHookPreviewTileTextures();

	void OnRender() override;

private:
	bool m_aEntitiesIsLoaded[MAP_IMAGE_MOD_TYPE_COUNT * 2];
	bool m_SpeedupArrowIsLoaded;
	bool m_TuneColorsIsLoaded;
	IGraphics::CTextureHandle m_aaEntitiesTextures[MAP_IMAGE_MOD_TYPE_COUNT * 2][MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT];
	IGraphics::CTextureHandle m_SpeedupArrowTexture;
	IGraphics::CTextureHandle m_TuneColorMapTexture;
	IGraphics::CTextureHandle m_OverlayBottomTexture;
	IGraphics::CTextureHandle m_OverlayTopTexture;
	IGraphics::CTextureHandle m_OverlayCenterTexture;
	int m_TextureScale;

	// 钩子碰撞预览小图（owner：CMapImages；解码在 worker，纹理上传/卸载仅在主线程）。
	// 缓存 key = (m_aEntitiesPath, mod type, masked)；key 变化即失效并后台重解码。
	char m_aHookPreviewEntitiesPath[IO_MAX_PATH_LENGTH] = "";
	int m_HookPreviewModType = -1;
	bool m_HookPreviewMasked = false;
	bool m_HookPreviewFailed = false; // 当前 key 下解码失败（包缺失/损坏），key 变化前不再重试
	bool m_HookPreviewJobPending = false;
	IGraphics::CTextureHandle m_aHookPreviewTileTextures[2]; // [0]=TILE_NOHOOK [1]=TILE_SOLID
	std::shared_ptr<IJob> m_pHookPreviewJob;

	static void ConchainClTextEntitiesSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	void InitOverlayTextures();
	void ReloadEntitiesTextures();
	bool HookPreviewKeyMatches(int ModType, bool Masked) const;
	void HookPreviewKickJob();
	void HookPreviewPollJob();
	void HookPreviewInvalidate();
	IGraphics::CTextureHandle UploadEntityLayerText(int TextureSize, int MaxWidth, int YOffset);
	void UpdateEntityLayerText(CImageInfo &TextImage, int TextureSize, int MaxWidth, int YOffset, int NumbersPower, int MaxNumber = -1);
};

#endif
