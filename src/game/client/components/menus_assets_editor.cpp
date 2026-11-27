/* Copyright © 2026 BestProject Team */
#include <base/math.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

using namespace FontIcons;

bool SaveSkinfileFromParts(IStorage *pStorage, const char *pName, const char *pBodyPartName, const char *pMarkingPartName, const char *pDecorationPartName, const char *pHandsPartName, const char *pFeetPartName, const char *pEyesPartName);

namespace
{
	struct SAssetsEditorPartDef
	{
		int m_SpriteId;
		int m_Group;
	};

	struct SAssetsEditorScanContext
	{
		std::vector<CMenus::SAssetsEditorAssetEntry> *m_pAssets;
		IStorage *m_pStorage;
		const char *m_pAssetType;
		int m_Type;
	};

	struct SAssetsEditorImageCacheEntry
	{
		int m_Type = CMenus::ASSETS_EDITOR_TYPE_GAME;
		char m_aName[64] = {0};
		CImageInfo m_Image;
	};

	static std::vector<SAssetsEditorImageCacheEntry> gs_vAssetsEditorImageCache;

	static bool AssetsEditorIsSingleImageType(int Type)
	{
		return Type == CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR ||
		       Type == CMenus::ASSETS_EDITOR_TYPE_ARROW ||
		       Type == CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK;
	}

	static const char *AssetsEditorDefaultSingleImageBuiltinPath(int Type)
	{
		const char *pCategoryId = nullptr;
		switch(Type)
		{
		case CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR: pCategoryId = "gui_cursor"; break;
		case CMenus::ASSETS_EDITOR_TYPE_ARROW: pCategoryId = "arrow"; break;
		case CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK: pCategoryId = "strong_weak"; break;
		default: break;
		}

		if(pCategoryId == nullptr)
			return nullptr;

		const SAssetResourceCategory *pCategory = FindAssetResourceCategory(pCategoryId);
		if(pCategory == nullptr)
			return nullptr;

		return BuiltinSingleFileAssetFilename(*pCategory);
	}

	static int AssetsEditorFindSpriteIdByName(const char *pName, int ImageId)
	{
		if(pName == nullptr || pName[0] == '\0')
			return -1;

		const CDataImage *pImage = ImageId >= 0 ? &g_pData->m_aImages[ImageId] : nullptr;
		for(int SpriteId = 0; SpriteId < NUM_SPRITES; ++SpriteId)
		{
			const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
			if(Sprite.m_pName == nullptr || str_comp(Sprite.m_pName, pName) != 0)
				continue;
			if(Sprite.m_W <= 0 || Sprite.m_H <= 0 || Sprite.m_pSet == nullptr)
				continue;
			if(pImage != nullptr && Sprite.m_pSet->m_pImage != pImage)
				continue;
			return SpriteId;
		}
		return -1;
	}

	static bool AssetsEditorHasAssetName(const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, const char *pName)
	{
		for(const auto &Asset : vAssets)
		{
			if(str_comp(Asset.m_aName, pName) == 0)
				return true;
		}
		return false;
	}

	static bool AssetsEditorSlotSameNormalizedSize(const CMenus::SAssetsEditorPartSlot &Left, const CMenus::SAssetsEditorPartSlot &Right)
	{
		return Left.m_DstW == Right.m_DstW && Left.m_DstH == Right.m_DstH;
	}

	static void AssetsEditorStripTrailingDigits(const char *pIn, char *pOut, int OutSize)
	{
		str_copy(pOut, pIn, OutSize);
		int Len = str_length(pOut);
		while(Len > 0 && isdigit((unsigned char)pOut[Len - 1]))
			pOut[--Len] = '\0';
	}

	static int AssetsEditorScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		SAssetsEditorScanContext *pContext = static_cast<SAssetsEditorScanContext *>(pUser);
		if(pName[0] == '.')
			return 0;

		CMenus::SAssetsEditorAssetEntry Entry;
		Entry.m_IsDefault = false;

		if(IsDir)
		{
			if(pContext->m_Type == CMenus::ASSETS_EDITOR_TYPE_SKIN)
				return 0;
			if(str_comp(pName, "default") == 0)
				return 0;

			str_copy(Entry.m_aName, pName);
			if(pContext->m_Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
			{
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/entities/%s/ddnet.png", pName);
				if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
				{
					str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/entities/%s.png", pName);
					if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
						return 0;
				}
			}
			else
			{
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/%s/%s/%s.png", pContext->m_pAssetType, pName, pContext->m_pAssetType);
				if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
				{
					str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/%s/%s.png", pContext->m_pAssetType, pName);
					if(!pContext->m_pStorage->FileExists(Entry.m_aPath, IStorage::TYPE_ALL))
						return 0;
				}
			}
		}
		else
		{
			if(!str_endswith(pName, ".png"))
				return 0;

			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;
			str_copy(Entry.m_aName, aName);
			if(pContext->m_Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/entities/%s.png", aName);
			else if(pContext->m_Type == CMenus::ASSETS_EDITOR_TYPE_SKIN)
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "skins/%s.png", aName);
			else
				str_format(Entry.m_aPath, sizeof(Entry.m_aPath), "assets/%s/%s.png", pContext->m_pAssetType, aName);
		}

		if(AssetsEditorHasAssetName(*pContext->m_pAssets, Entry.m_aName))
			return 0;

		pContext->m_pAssets->push_back(Entry);
		return 0;
	}

	static int AssetsEditorTypeImageId(int Type)
	{
		switch(Type)
		{
		case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return IMAGE_EMOTICONS;
		case CMenus::ASSETS_EDITOR_TYPE_HUD: return IMAGE_HUD;
		case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return IMAGE_PARTICLES;
		case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return IMAGE_EXTRAS;
		case CMenus::ASSETS_EDITOR_TYPE_SKIN:
		case CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR:
		case CMenus::ASSETS_EDITOR_TYPE_ARROW:
		case CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK:
		case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return -1;
		default: return IMAGE_GAME;
		}
	}

	static int AssetsEditorGridSpriteId(int Type)
	{
		switch(Type)
		{
		case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return SPRITE_OOP;
		case CMenus::ASSETS_EDITOR_TYPE_HUD: return SPRITE_HUD_AIRJUMP;
		case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return SPRITE_PART_SLICE;
		case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return SPRITE_PART_SNOWFLAKE;
		case CMenus::ASSETS_EDITOR_TYPE_SKIN:
		case CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR:
		case CMenus::ASSETS_EDITOR_TYPE_ARROW:
		case CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK: return -1;
		default: return SPRITE_HEALTH_FULL;
		}
	}

	static int AssetsEditorGridX(int Type)
	{
		if(Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
			return 16;
		if(Type == CMenus::ASSETS_EDITOR_TYPE_SKIN)
			return 256;
		if(Type == CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK)
		{
			int GridX = 0;
			int GridY = 0;
			CMenus::GetStrongWeakEditorGridSize(GridX, GridY);
			return GridX;
		}
		if(AssetsEditorIsSingleImageType(Type))
			return 1;

		const int SpriteId = AssetsEditorGridSpriteId(Type);
		if(SpriteId < 0)
			return 1;
		const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
		if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_Gridx <= 0)
			return 1;
		return Sprite.m_pSet->m_Gridx;
	}

	static int AssetsEditorGridY(int Type)
	{
		if(Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES)
			return 16;
		if(Type == CMenus::ASSETS_EDITOR_TYPE_SKIN)
			return 128;
		if(Type == CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK)
		{
			int GridX = 0;
			int GridY = 0;
			CMenus::GetStrongWeakEditorGridSize(GridX, GridY);
			return GridY;
		}
		if(AssetsEditorIsSingleImageType(Type))
			return 1;

		const int SpriteId = AssetsEditorGridSpriteId(Type);
		if(SpriteId < 0)
			return 1;
		const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
		if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_Gridy <= 0)
			return 1;
		return Sprite.m_pSet->m_Gridy;
	}

	static void AssetsEditorCollectPartDefs(int Type, std::vector<SAssetsEditorPartDef> &vPartDefs)
	{
		vPartDefs.clear();
		if(Type == CMenus::ASSETS_EDITOR_TYPE_ENTITIES || Type == CMenus::ASSETS_EDITOR_TYPE_SKIN)
			return;
		if(AssetsEditorIsSingleImageType(Type))
			return;

		const int ImageId = AssetsEditorTypeImageId(Type);
		if(ImageId < 0)
			return;

		const CDataImage *pImage = &g_pData->m_aImages[ImageId];
		const bool DeduplicateByGeometry = Type == CMenus::ASSETS_EDITOR_TYPE_GAME || Type == CMenus::ASSETS_EDITOR_TYPE_PARTICLES;
		auto HasMatchingGeometry = [&vPartDefs](const CDataSprite &Candidate) {
			for(const auto &PartDef : vPartDefs)
			{
				const CDataSprite &Existing = g_pData->m_aSprites[PartDef.m_SpriteId];
				if(Existing.m_X == Candidate.m_X && Existing.m_Y == Candidate.m_Y &&
					Existing.m_W == Candidate.m_W && Existing.m_H == Candidate.m_H)
					return true;
			}
			return false;
		};
		for(int SpriteId = 0; SpriteId < NUM_SPRITES; ++SpriteId)
		{
			const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
			if(Sprite.m_pSet == nullptr || Sprite.m_pSet->m_pImage != pImage || Sprite.m_W <= 0 || Sprite.m_H <= 0)
				continue;
			if(DeduplicateByGeometry && HasMatchingGeometry(Sprite))
				continue;
			vPartDefs.push_back({SpriteId, 0});
		}
	}

	static const CMenus::SAssetsEditorAssetEntry *AssetsEditorFindAssetByName(const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, const char *pName)
	{
		for(const auto &Asset : vAssets)
		{
			if(str_comp(Asset.m_aName, pName) == 0)
				return &Asset;
		}
		return nullptr;
	}

	static int AssetsEditorFindAssetIndexByName(const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, const char *pName)
	{
		for(size_t i = 0; i < vAssets.size(); ++i)
		{
			if(str_comp(vAssets[i].m_aName, pName) == 0)
				return (int)i;
		}
		return -1;
	}

	static void AssetsEditorClearImageCache()
	{
		for(auto &Entry : gs_vAssetsEditorImageCache)
		{
			Entry.m_Image.Free();
		}
		gs_vAssetsEditorImageCache.clear();
	}

	static void AssetsEditorClearImageCacheForType(int Type)
	{
		for(size_t Index = 0; Index < gs_vAssetsEditorImageCache.size();)
		{
			if(gs_vAssetsEditorImageCache[Index].m_Type != Type)
			{
				++Index;
				continue;
			}

			gs_vAssetsEditorImageCache[Index].m_Image.Free();
			gs_vAssetsEditorImageCache.erase(gs_vAssetsEditorImageCache.begin() + Index);
		}
	}

	static bool AssetsEditorCalcFittedRect(const CUIRect &Rect, int SourceWidth, int SourceHeight, CUIRect &OutRect)
	{
		if(SourceWidth <= 0 || SourceHeight <= 0 || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return false;

		float DrawW = Rect.w;
		float DrawH = DrawW * ((float)SourceHeight / (float)SourceWidth);
		if(DrawH > Rect.h)
		{
			DrawH = Rect.h;
			DrawW = DrawH * ((float)SourceWidth / (float)SourceHeight);
		}

		OutRect.x = Rect.x + (Rect.w - DrawW) / 2.0f;
		OutRect.y = Rect.y + (Rect.h - DrawH) / 2.0f;
		OutRect.w = DrawW;
		OutRect.h = DrawH;
		return true;
	}

	static bool AssetsEditorGetSlotRectInFitted(const CUIRect &FittedRect, int Type, const CMenus::SAssetsEditorPartSlot &Slot, CUIRect &OutRect)
	{
		const int GridX = maximum(1, AssetsEditorGridX(Type));
		const int GridY = maximum(1, AssetsEditorGridY(Type));
		if(Slot.m_DstW <= 0 || Slot.m_DstH <= 0)
			return false;

		const float X = (float)Slot.m_DstX / GridX;
		const float Y = (float)Slot.m_DstY / GridY;
		const float W = (float)Slot.m_DstW / GridX;
		const float H = (float)Slot.m_DstH / GridY;

		OutRect.x = FittedRect.x + X * FittedRect.w;
		OutRect.y = FittedRect.y + Y * FittedRect.h;
		OutRect.w = W * FittedRect.w;
		OutRect.h = H * FittedRect.h;
		return true;
	}

	static bool AssetsEditorDrawTextureFitted(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int SourceWidth, int SourceHeight, IGraphics *pGraphics, CUIRect *pOutFittedRect = nullptr)
	{
		if(!Texture.IsValid())
			return false;

		CUIRect FittedRect;
		if(!AssetsEditorCalcFittedRect(Rect, SourceWidth, SourceHeight, FittedRect))
			return false;

		if(pOutFittedRect != nullptr)
			*pOutFittedRect = FittedRect;

		pGraphics->WrapClamp();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(ui_token::color::NEUTRAL_WHITE);
		const IGraphics::CQuadItem Quad(FittedRect.x, FittedRect.y, FittedRect.w, FittedRect.h);
		pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
		return true;
	}

	static void AssetsEditorDrawSlotFromTexture(const CUIRect &Rect, IGraphics::CTextureHandle Texture, const CMenus::SAssetsEditorPartSlot &Slot, int Type, IGraphics *pGraphics)
	{
		if(!Texture.IsValid() || Slot.m_DstW <= 0 || Slot.m_DstH <= 0)
			return;

		const int GridX = maximum(1, AssetsEditorGridX(Type));
		const int GridY = maximum(1, AssetsEditorGridY(Type));
		const float U0 = (float)Slot.m_DstX / GridX;
		const float V0 = (float)Slot.m_DstY / GridY;
		const float U1 = (float)(Slot.m_DstX + Slot.m_DstW) / GridX;
		const float V1 = (float)(Slot.m_DstY + Slot.m_DstH) / GridY;

		pGraphics->WrapClamp();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(ui_token::color::NEUTRAL_WHITE);
		pGraphics->QuadsSetSubset(U0, V0, U1, V1);
		const IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
		pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsSetSubset(0, 0, 1, 1);
		pGraphics->QuadsEnd();
		pGraphics->TextureClear();
		pGraphics->WrapNormal();
	}

	static const CImageInfo *AssetsEditorGetCachedImage(int Type, const char *pAssetName, const std::vector<CMenus::SAssetsEditorAssetEntry> &vAssets, IGraphics *pGraphics)
	{
		for(const auto &Entry : gs_vAssetsEditorImageCache)
		{
			if(Entry.m_Type == Type && str_comp(Entry.m_aName, pAssetName) == 0)
				return &Entry.m_Image;
		}

		const CMenus::SAssetsEditorAssetEntry *pAsset = AssetsEditorFindAssetByName(vAssets, pAssetName);
		if(pAsset == nullptr)
			return nullptr;

		CImageInfo Loaded;
		if(!pGraphics->LoadPng(Loaded, pAsset->m_aPath, IStorage::TYPE_ALL))
			return nullptr;

		if(!pGraphics->CheckImageDivisibility(pAsset->m_aPath, Loaded, AssetsEditorGridX(Type), AssetsEditorGridY(Type), true))
		{
			Loaded.Free();
			return nullptr;
		}

		ConvertToRgba(Loaded);
		if(Loaded.m_pData == nullptr || Loaded.m_Format != CImageInfo::FORMAT_RGBA)
			return nullptr;

		SAssetsEditorImageCacheEntry &NewEntry = gs_vAssetsEditorImageCache.emplace_back();
		NewEntry.m_Type = Type;
		str_copy(NewEntry.m_aName, pAssetName);
		NewEntry.m_Image = std::move(Loaded);
		return &NewEntry.m_Image;
	}

	static int AssetsEditorTypeToCustomTab(int Type)
	{
		switch(Type)
		{
		case CMenus::ASSETS_EDITOR_TYPE_GAME: return 1;
		case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return 2;
		case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return 0;
		case CMenus::ASSETS_EDITOR_TYPE_HUD: return 4;
		case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return 3;
		case CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR: return ASSETS_TAB_GUI_CURSOR;
		case CMenus::ASSETS_EDITOR_TYPE_ARROW: return ASSETS_TAB_ARROW;
		case CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK: return ASSETS_TAB_STRONG_WEAK;
		case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return 5;
		default: return -1;
		}
	}
}

void CMenus::AssetsEditorOpen(int Type)
{
	AssetsEditorCloseNow();

	g_Config.m_UiSettingsPage = SETTINGS_ASSETS;
	m_AssetsEditorState.m_Open = true;
	m_AssetsEditorState.m_Initialized = false;
	m_AssetsEditorState.m_Type = std::clamp(Type, 0, ASSETS_EDITOR_TYPE_COUNT - 1);
	m_AssetsEditorState.m_FullscreenOpen = false;
	AssetsEditorEnsureDefaultExportNames();
	AssetsEditorSyncExportNameFromType();
}

void CMenus::AssetsEditorClearAssets()
{
	// The color picker stores a pointer into m_vPartSlots. Close it before any state reset.
	Ui()->ClosePopupMenu(&m_ColorPickerPopupContext);

	auto UnloadAssets = [this](std::vector<SAssetsEditorAssetEntry> &vAssets) {
		for(auto &Asset : vAssets)
		{
			Graphics()->UnloadTexture(&Asset.m_PreviewTexture);
		}
		vAssets.clear();
	};

	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
	{
		UnloadAssets(m_AssetsEditorState.m_avAssets[Type]);
		m_AssetsEditorState.m_aAssetsLoaded[Type] = false;
	}
	Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
	m_AssetsEditorState.m_vPartSlots.clear();
	AssetsEditorCancelDrag();
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_ColorPickerValue = 0;
	m_AssetsEditorState.m_Initialized = false;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	m_AssetsEditorState.m_PendingAction.reset();
	m_AssetsEditorState.m_SelectedTargetSlotIndex = -1;
	m_AssetsEditorState.m_ComposedPreviewWidth = 0;
	m_AssetsEditorState.m_ComposedPreviewHeight = 0;
	m_AssetsEditorState.m_ColorPickerSlotIndex = -1;
	m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
	m_AssetsEditorState.m_HoverCyclePositionX = -1;
	m_AssetsEditorState.m_HoverCyclePositionY = -1;
	m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
	m_AssetsEditorState.m_vHoverCycleCandidates.clear();
	m_AssetsEditorState.m_aStatusMessage[0] = '\0';
	m_AssetsEditorState.m_StatusIsError = false;
	AssetsEditorClearImageCache();
}

void CMenus::AssetsEditorEnsureAssetsLoadedForType(int Type)
{
	if(Type < 0 || Type >= ASSETS_EDITOR_TYPE_COUNT)
		return;
	if(m_AssetsEditorState.m_aAssetsLoaded[Type])
		return;
	AssetsEditorReloadAssetType(Type);
}

void CMenus::AssetsEditorEnsurePreviewLoaded(SAssetsEditorAssetEntry &Asset)
{
	if(Asset.m_PreviewLoaded)
		return;
	Asset.m_PreviewLoaded = true;

	Asset.m_PreviewTexture = Graphics()->LoadTexture(Asset.m_aPath, IStorage::TYPE_ALL);
	CImageInfo PreviewInfo;
	if(Graphics()->LoadPng(PreviewInfo, Asset.m_aPath, IStorage::TYPE_ALL))
	{
		Asset.m_PreviewWidth = PreviewInfo.m_Width;
		Asset.m_PreviewHeight = PreviewInfo.m_Height;
		PreviewInfo.Free();
	}
}

void CMenus::AssetsEditorReloadAssetType(int Type)
{
	if(Type < 0 || Type >= ASSETS_EDITOR_TYPE_COUNT)
		return;

	AssetsEditorClearImageCacheForType(Type);

	auto &vAssets = m_AssetsEditorState.m_avAssets[Type];
	char aPrevMainName[64] = {};
	char aPrevDonorName[64] = {};
	const int MainIndex = m_AssetsEditorState.m_aMainAssetIndex[Type];
	const int DonorIndex = m_AssetsEditorState.m_aDonorAssetIndex[Type];
	if(MainIndex >= 0 && MainIndex < (int)vAssets.size())
		str_copy(aPrevMainName, vAssets[MainIndex].m_aName);
	if(DonorIndex >= 0 && DonorIndex < (int)vAssets.size())
		str_copy(aPrevDonorName, vAssets[DonorIndex].m_aName);

	for(auto &Asset : vAssets)
		Graphics()->UnloadTexture(&Asset.m_PreviewTexture);
	vAssets.clear();

	const char *pAssetType = AssetsEditorTypeName(Type);
	SAssetsEditorAssetEntry DefaultAsset;
	DefaultAsset.m_IsDefault = true;
	str_copy(DefaultAsset.m_aName, "default");
	if(Type == ASSETS_EDITOR_TYPE_ENTITIES)
		str_copy(DefaultAsset.m_aPath, "editor/entities_clear/ddnet.png");
	else if(Type == ASSETS_EDITOR_TYPE_SKIN)
		str_copy(DefaultAsset.m_aPath, "skins/default.png");
	else if(AssetsEditorIsSingleImageType(Type))
	{
		str_format(DefaultAsset.m_aPath, sizeof(DefaultAsset.m_aPath), "assets/%s/default.png", pAssetType);
		if(!Storage()->FileExists(DefaultAsset.m_aPath, IStorage::TYPE_ALL))
		{
			if(const char *pBuiltinPath = AssetsEditorDefaultSingleImageBuiltinPath(Type))
				str_copy(DefaultAsset.m_aPath, pBuiltinPath, sizeof(DefaultAsset.m_aPath));
		}
	}
	else
	{
		const int DefaultImageId = AssetsEditorTypeImageId(Type);
		str_copy(DefaultAsset.m_aPath, g_pData->m_aImages[DefaultImageId].m_pFilename);
	}
	vAssets.push_back(DefaultAsset);

	SAssetsEditorScanContext Context;
	Context.m_pAssets = &vAssets;
	Context.m_pStorage = Storage();
	Context.m_pAssetType = pAssetType;
	Context.m_Type = Type;

	char aPath[128];
	if(Type == ASSETS_EDITOR_TYPE_ENTITIES)
		str_copy(aPath, "assets/entities");
	else if(Type == ASSETS_EDITOR_TYPE_SKIN)
		str_copy(aPath, "skins");
	else
		str_format(aPath, sizeof(aPath), "assets/%s", pAssetType);
	Storage()->ListDirectory(IStorage::TYPE_ALL, aPath, AssetsEditorScanCallback, &Context);

	std::sort(vAssets.begin(), vAssets.end(), [](const SAssetsEditorAssetEntry &Left, const SAssetsEditorAssetEntry &Right) {
		if(Left.m_IsDefault != Right.m_IsDefault)
			return Left.m_IsDefault;
		return str_comp(Left.m_aName, Right.m_aName) < 0;
	});

	const int NewMainIndex = AssetsEditorFindAssetIndexByName(vAssets, aPrevMainName);
	const int NewDonorIndex = AssetsEditorFindAssetIndexByName(vAssets, aPrevDonorName);
	m_AssetsEditorState.m_aMainAssetIndex[Type] = NewMainIndex >= 0 ? NewMainIndex : 0;
	m_AssetsEditorState.m_aDonorAssetIndex[Type] = NewDonorIndex >= 0 ? NewDonorIndex : m_AssetsEditorState.m_aMainAssetIndex[Type];
	m_AssetsEditorState.m_aAssetsLoaded[Type] = true;
}

void CMenus::AssetsEditorReloadAssets()
{
	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
		AssetsEditorReloadAssetType(Type);

	AssetsEditorCancelDrag();
	m_AssetsEditorState.m_DirtyPreview = true;
}

void CMenus::AssetsEditorReloadAssetsImagesOnly()
{
	const int Type = m_AssetsEditorState.m_Type;
	AssetsEditorReloadAssetType(Type);
	AssetsEditorCancelDrag();

	auto &vSlots = m_AssetsEditorState.m_vPartSlots;
	const auto &vReloadedAssets = m_AssetsEditorState.m_avAssets[Type];
	const int MainIndex = m_AssetsEditorState.m_aMainAssetIndex[Type];
	const char *pMainAssetName = MainIndex >= 0 && MainIndex < (int)vReloadedAssets.size() ? vReloadedAssets[MainIndex].m_aName : "default";
	int FixedSlots = 0;
	for(auto &Slot : vSlots)
	{
		if(AssetsEditorFindAssetIndexByName(vReloadedAssets, Slot.m_aSourceAsset) >= 0)
			continue;
		str_copy(Slot.m_aSourceAsset, pMainAssetName);
		Slot.m_SourceSpriteId = Slot.m_SpriteId;
		Slot.m_SrcX = Slot.m_DstX;
		Slot.m_SrcY = Slot.m_DstY;
		Slot.m_SrcW = Slot.m_DstW;
		Slot.m_SrcH = Slot.m_DstH;
		++FixedSlots;
	}
	if(FixedSlots > 0)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage),
			Localize("Reloaded images. %d slot(s) fell back to main asset."), FixedSlots);
		m_AssetsEditorState.m_StatusIsError = false;
	}
	else
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Reloaded images."));
		m_AssetsEditorState.m_StatusIsError = false;
	}
	m_AssetsEditorState.m_DirtyPreview = true;
}

void CMenus::AssetsEditorResetPartSlots()
{
	// 重建部件会使取色器目标和当前选择失效。
	Ui()->ClosePopupMenu(&m_ColorPickerPopupContext);
	m_AssetsEditorState.m_SelectedTargetSlotIndex = -1;
	m_AssetsEditorState.m_ColorPickerSlotIndex = -1;
	m_AssetsEditorState.m_ColorPickerValue = 0;

	const auto &vAssets = m_AssetsEditorState.m_avAssets[m_AssetsEditorState.m_Type];
	int &MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[m_AssetsEditorState.m_Type];
	int &DonorAssetIndex = m_AssetsEditorState.m_aDonorAssetIndex[m_AssetsEditorState.m_Type];
	if(vAssets.empty())
	{
		m_AssetsEditorState.m_vPartSlots.clear();
		AssetsEditorCancelDrag();
		return;
	}

	MainAssetIndex = std::clamp(MainAssetIndex, 0, (int)vAssets.size() - 1);
	DonorAssetIndex = std::clamp(DonorAssetIndex, 0, (int)vAssets.size() - 1);
	const char *pMainAssetName = vAssets[MainAssetIndex].m_aName;

	m_AssetsEditorState.m_vPartSlots.clear();
	if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_ENTITIES)
	{
		for(int Y = 0; Y < 16; ++Y)
		{
			for(int X = 0; X < 16; ++X)
			{
				SAssetsEditorPartSlot Slot;
				Slot.m_Group = 0;
				Slot.m_DstX = X;
				Slot.m_DstY = Y;
				Slot.m_DstW = 1;
				Slot.m_DstH = 1;
				Slot.m_SrcX = X;
				Slot.m_SrcY = Y;
				Slot.m_SrcW = 1;
				Slot.m_SrcH = 1;
				str_format(Slot.m_aFamilyKey, sizeof(Slot.m_aFamilyKey), "entities:tile_%03d", Y * 16 + X);
				str_copy(Slot.m_aSourceAsset, pMainAssetName);
				m_AssetsEditorState.m_vPartSlots.push_back(Slot);
			}
		}
	}
	else if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_STRONG_WEAK)
	{
		m_AssetsEditorState.m_vPartSlots = BuildStrongWeakEditorSlots(pMainAssetName);
	}
	else if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_SKIN)
	{
		m_AssetsEditorState.m_vPartSlots = BuildSkinEditorSlots(pMainAssetName);
	}
	else if(AssetsEditorIsSingleImageType(m_AssetsEditorState.m_Type))
	{
		SAssetsEditorPartSlot Slot;
		Slot.m_SpriteId = -1;
		Slot.m_SourceSpriteId = -1;
		Slot.m_Group = 0;
		Slot.m_DstX = 0;
		Slot.m_DstY = 0;
		Slot.m_DstW = 1;
		Slot.m_DstH = 1;
		Slot.m_SrcX = 0;
		Slot.m_SrcY = 0;
		Slot.m_SrcW = 1;
		Slot.m_SrcH = 1;
		str_copy(Slot.m_aFamilyKey, "full_image", sizeof(Slot.m_aFamilyKey));
		str_copy(Slot.m_aSourceAsset, pMainAssetName);
		m_AssetsEditorState.m_vPartSlots.push_back(Slot);
	}
	else
	{
		std::vector<SAssetsEditorPartDef> vPartDefs;
		AssetsEditorCollectPartDefs(m_AssetsEditorState.m_Type, vPartDefs);
		for(const auto &PartDef : vPartDefs)
		{
			SAssetsEditorPartSlot Slot;
			Slot.m_SpriteId = PartDef.m_SpriteId;
			Slot.m_SourceSpriteId = PartDef.m_SpriteId;
			Slot.m_Group = PartDef.m_Group;

			const CDataSprite &Sprite = g_pData->m_aSprites[PartDef.m_SpriteId];
			Slot.m_DstX = Sprite.m_X;
			Slot.m_DstY = Sprite.m_Y;
			Slot.m_DstW = Sprite.m_W;
			Slot.m_DstH = Sprite.m_H;
			Slot.m_SrcX = Sprite.m_X;
			Slot.m_SrcY = Sprite.m_Y;
			Slot.m_SrcW = Sprite.m_W;
			Slot.m_SrcH = Sprite.m_H;
			AssetsEditorBuildFamilyKey(m_AssetsEditorState.m_Type, &Sprite, Slot.m_aFamilyKey, sizeof(Slot.m_aFamilyKey));
			str_copy(Slot.m_aSourceAsset, pMainAssetName);
			m_AssetsEditorState.m_vPartSlots.push_back(Slot);
		}

		if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_GAME)
		{
			struct SSyntheticSlotDef
			{
				const char *m_pName;
				int m_X;
				int m_Y;
				int m_W;
				int m_H;
			};
			static const SSyntheticSlotDef s_aSyntheticSlots[] = {
				{"ninja_bar_full_left", 21, 4, 1, 2},
				{"ninja_bar_full", 22, 4, 1, 2},
				{"ninja_bar_empty", 23, 4, 1, 2},
				{"ninja_bar_empty_right", 24, 4, 1, 2},
			};

			for(const auto &Synthetic : s_aSyntheticSlots)
			{
				bool Exists = false;
				for(const auto &ExistingSlot : m_AssetsEditorState.m_vPartSlots)
				{
					if(ExistingSlot.m_DstX == Synthetic.m_X && ExistingSlot.m_DstY == Synthetic.m_Y &&
						ExistingSlot.m_DstW == Synthetic.m_W && ExistingSlot.m_DstH == Synthetic.m_H)
					{
						Exists = true;
						break;
					}
				}
				if(Exists)
					continue;

				SAssetsEditorPartSlot Slot;
				Slot.m_SpriteId = -1;
				Slot.m_SourceSpriteId = -1;
				Slot.m_Group = 0;
				Slot.m_DstX = Synthetic.m_X;
				Slot.m_DstY = Synthetic.m_Y;
				Slot.m_DstW = Synthetic.m_W;
				Slot.m_DstH = Synthetic.m_H;
				Slot.m_SrcX = Synthetic.m_X;
				Slot.m_SrcY = Synthetic.m_Y;
				Slot.m_SrcW = Synthetic.m_W;
				Slot.m_SrcH = Synthetic.m_H;
				str_copy(Slot.m_aFamilyKey, Synthetic.m_pName, sizeof(Slot.m_aFamilyKey));
				str_copy(Slot.m_aSourceAsset, pMainAssetName);
				m_AssetsEditorState.m_vPartSlots.push_back(Slot);
			}
		}
	}

	AssetsEditorValidateRequiredSlotsForType(m_AssetsEditorState.m_Type);
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	AssetsEditorCancelDrag();
}

void CMenus::AssetsEditorEnsureDefaultExportNames()
{
	for(int Type = 0; Type < ASSETS_EDITOR_TYPE_COUNT; ++Type)
	{
		if(m_AssetsEditorState.m_aaExportNameByType[Type][0] != '\0')
			continue;
		char aDefaultName[64];
		str_format(aDefaultName, sizeof(aDefaultName), Localize("my_%s"), AssetsEditorTypeName(Type));
		str_copy(m_AssetsEditorState.m_aaExportNameByType[Type], aDefaultName);
	}
}

void CMenus::AssetsEditorSyncExportNameFromType()
{
	AssetsEditorEnsureDefaultExportNames();
	str_copy(m_AssetsEditorState.m_aExportName, m_AssetsEditorState.m_aaExportNameByType[m_AssetsEditorState.m_Type]);
}

void CMenus::AssetsEditorCommitExportNameForType()
{
	AssetsEditorEnsureDefaultExportNames();
	str_copy(m_AssetsEditorState.m_aaExportNameByType[m_AssetsEditorState.m_Type], m_AssetsEditorState.m_aExportName);
}

void CMenus::AssetsEditorValidateRequiredSlotsForType(int Type)
{
	auto &vSlots = m_AssetsEditorState.m_vPartSlots;
	const auto &vAssets = m_AssetsEditorState.m_avAssets[Type];
	const int MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[Type];
	const char *pMainAssetName = MainAssetIndex >= 0 && MainAssetIndex < (int)vAssets.size() ? vAssets[MainAssetIndex].m_aName : "default";
	const int ImageId = AssetsEditorTypeImageId(Type);
	int AddedSlots = 0;

	auto HasSlotByName = [&](const char *pName) {
		for(const auto &Slot : vSlots)
		{
			if(Slot.m_SpriteId >= 0 && Slot.m_SpriteId < NUM_SPRITES)
			{
				const CDataSprite &Sprite = g_pData->m_aSprites[Slot.m_SpriteId];
				if(Sprite.m_pName != nullptr && str_comp(Sprite.m_pName, pName) == 0)
					return true;
			}
			if(str_comp(Slot.m_aFamilyKey, pName) == 0)
				return true;
		}
		return false;
	};

	auto HasSlotByGeometry = [&](int X, int Y, int W, int H) {
		for(const auto &Slot : vSlots)
		{
			if(Slot.m_DstX == X && Slot.m_DstY == Y && Slot.m_DstW == W && Slot.m_DstH == H)
				return true;
		}
		return false;
	};

	auto AddSlotByName = [&](const char *pName, const char *pAliasName, int FallbackX, int FallbackY, int FallbackW, int FallbackH) {
		if(HasSlotByGeometry(FallbackX, FallbackY, FallbackW, FallbackH))
			return;
		if(HasSlotByName(pName) || (pAliasName != nullptr && pAliasName[0] != '\0' && HasSlotByName(pAliasName)))
			return;

		SAssetsEditorPartSlot Slot;
		Slot.m_SpriteId = AssetsEditorFindSpriteIdByName(pName, ImageId);
		if(Slot.m_SpriteId < 0 && pAliasName != nullptr && pAliasName[0] != '\0')
			Slot.m_SpriteId = AssetsEditorFindSpriteIdByName(pAliasName, ImageId);
		Slot.m_SourceSpriteId = Slot.m_SpriteId;
		Slot.m_Group = 0;
		if(Slot.m_SpriteId >= 0)
		{
			const CDataSprite &Sprite = g_pData->m_aSprites[Slot.m_SpriteId];
			Slot.m_DstX = Sprite.m_X;
			Slot.m_DstY = Sprite.m_Y;
			Slot.m_DstW = Sprite.m_W;
			Slot.m_DstH = Sprite.m_H;
			Slot.m_SrcX = Sprite.m_X;
			Slot.m_SrcY = Sprite.m_Y;
			Slot.m_SrcW = Sprite.m_W;
			Slot.m_SrcH = Sprite.m_H;
			AssetsEditorBuildFamilyKey(Type, &Sprite, Slot.m_aFamilyKey, sizeof(Slot.m_aFamilyKey));
		}
		else
		{
			Slot.m_DstX = FallbackX;
			Slot.m_DstY = FallbackY;
			Slot.m_DstW = FallbackW;
			Slot.m_DstH = FallbackH;
			Slot.m_SrcX = FallbackX;
			Slot.m_SrcY = FallbackY;
			Slot.m_SrcW = FallbackW;
			Slot.m_SrcH = FallbackH;
			str_copy(Slot.m_aFamilyKey, pName, sizeof(Slot.m_aFamilyKey));
		}
		str_copy(Slot.m_aSourceAsset, pMainAssetName);
		vSlots.push_back(Slot);
		++AddedSlots;
	};

	if(Type == ASSETS_EDITOR_TYPE_GAME)
	{
		AddSlotByName("pickup_health", "pickup_heart", 10, 2, 2, 2);
		AddSlotByName("pickup_armor", nullptr, 12, 2, 2, 2);
		AddSlotByName("pickup_armor_shotgun", nullptr, 15, 2, 2, 2);
		AddSlotByName("ninja_bar_full_left", nullptr, 21, 4, 1, 2);
		AddSlotByName("ninja_bar_full", nullptr, 22, 4, 1, 2);
		AddSlotByName("ninja_bar_empty", nullptr, 23, 4, 1, 2);
		AddSlotByName("ninja_bar_empty_right", nullptr, 24, 4, 1, 2);
	}
	else if(Type == ASSETS_EDITOR_TYPE_PARTICLES)
	{
		AddSlotByName("part_slice", nullptr, 0, 0, 1, 1);
		AddSlotByName("part_ball", nullptr, 1, 0, 1, 1);
		AddSlotByName("part_splat01", nullptr, 2, 0, 1, 1);
		AddSlotByName("part_splat02", nullptr, 3, 0, 1, 1);
		AddSlotByName("part_splat03", nullptr, 4, 0, 1, 1);
		AddSlotByName("part_smoke", nullptr, 0, 1, 1, 1);
		AddSlotByName("part_shell", nullptr, 0, 2, 2, 2);
		AddSlotByName("part_expl01", nullptr, 0, 4, 4, 4);
		AddSlotByName("part_airjump", nullptr, 2, 2, 2, 2);
		AddSlotByName("part_hit01", nullptr, 4, 1, 2, 2);
	}
	else if(AssetsEditorIsSingleImageType(Type))
	{
		return;
	}

	if(AddedSlots > 0)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Added %d missing required slot(s)."), AddedSlots);
		m_AssetsEditorState.m_StatusIsError = false;
	}
}

void CMenus::AssetsEditorBuildFamilyKey(int Type, const CDataSprite *pSprite, char *pOut, int OutSize)
{
	if(pOut == nullptr || OutSize <= 0)
		return;

	if(pSprite == nullptr || pSprite->m_pName == nullptr)
	{
		str_copy(pOut, "part", OutSize);
		return;
	}

	const char *pName = pSprite->m_pName;
	if(Type == ASSETS_EDITOR_TYPE_GAME)
	{
		if(str_comp_num(pName, "weapon_", 7) == 0)
		{
			const char *pAfterWeapon = pName + 7;
			const char *pLastUnderscore = str_rchr(pAfterWeapon, '_');
			if(pLastUnderscore != nullptr && pLastUnderscore[1] != '\0')
			{
				char aPart[64];
				AssetsEditorStripTrailingDigits(pLastUnderscore + 1, aPart, sizeof(aPart));
				str_format(pOut, OutSize, "weapon:*:%s", aPart);
				return;
			}
		}
	}
	else if(Type == ASSETS_EDITOR_TYPE_HUD)
	{
		if(str_find(pName, "_hit_disabled") != nullptr)
		{
			str_copy(pOut, "hud:*_hit_disabled", OutSize);
			return;
		}
		if(str_comp_num(pName, "hud_freeze_bar_", 15) == 0)
		{
			str_copy(pOut, "hud:freeze_bar", OutSize);
			return;
		}
		if(str_comp_num(pName, "hud_ninja_bar_", 14) == 0)
		{
			str_copy(pOut, "hud:ninja_bar", OutSize);
			return;
		}
	}
	else if(AssetsEditorIsSingleImageType(Type))
	{
		str_copy(pOut, "single_image", OutSize);
		return;
	}

	char aNormalized[64];
	AssetsEditorStripTrailingDigits(pName, aNormalized, sizeof(aNormalized));
	str_copy(pOut, aNormalized, OutSize);
}

bool CMenus::AssetsEditorCopyRectScaledNearest(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH)
{
	if(Dst.m_pData == nullptr || Src.m_pData == nullptr || Dst.m_Format != CImageInfo::FORMAT_RGBA || Src.m_Format != CImageInfo::FORMAT_RGBA)
		return false;
	if(DstW <= 0 || DstH <= 0 || SrcW <= 0 || SrcH <= 0)
		return false;
	if(DstX < 0 || DstY < 0 || SrcX < 0 || SrcY < 0)
		return false;
	const int DstWidth = (int)Dst.m_Width;
	const int DstHeight = (int)Dst.m_Height;
	const int SrcWidth = (int)Src.m_Width;
	const int SrcHeight = (int)Src.m_Height;
	if(DstX + DstW > DstWidth || DstY + DstH > DstHeight)
		return false;
	if(SrcX + SrcW > SrcWidth || SrcY + SrcH > SrcHeight)
		return false;

	uint8_t *pDstData = static_cast<uint8_t *>(Dst.m_pData);
	const uint8_t *pSrcData = static_cast<const uint8_t *>(Src.m_pData);
	for(int Y = 0; Y < DstH; ++Y)
	{
		const int SampleY = SrcY + ((int64_t)Y * SrcH) / DstH;
		for(int X = 0; X < DstW; ++X)
		{
			const int SampleX = SrcX + ((int64_t)X * SrcW) / DstW;
			const int DstOff = ((DstY + Y) * Dst.m_Width + (DstX + X)) * 4;
			const int SrcOff = (SampleY * Src.m_Width + SampleX) * 4;
			pDstData[DstOff + 0] = pSrcData[SrcOff + 0];
			pDstData[DstOff + 1] = pSrcData[SrcOff + 1];
			pDstData[DstOff + 2] = pSrcData[SrcOff + 2];
			pDstData[DstOff + 3] = pSrcData[SrcOff + 3];
		}
	}
	return true;
}

static bool AssetsEditorExtractSubImage(const CImageInfo &Src, CImageInfo &Dst, int X, int Y, int W, int H)
{
	if(Src.m_pData == nullptr || Src.m_Format != CImageInfo::FORMAT_RGBA || W <= 0 || H <= 0)
		return false;
	if(X < 0 || Y < 0 || X + W > (int)Src.m_Width || Y + H > (int)Src.m_Height)
		return false;
	if((size_t)W > std::numeric_limits<size_t>::max() / (size_t)H / 4)
		return false;

	Dst = CImageInfo();
	Dst.m_Width = W;
	Dst.m_Height = H;
	Dst.m_Format = CImageInfo::FORMAT_RGBA;
	Dst.m_pData = static_cast<uint8_t *>(malloc((size_t)W * H * 4));
	if(Dst.m_pData == nullptr)
		return false;

	const uint8_t *pSrc = static_cast<const uint8_t *>(Src.m_pData);
	uint8_t *pDst = static_cast<uint8_t *>(Dst.m_pData);
	for(int Row = 0; Row < H; ++Row)
	{
		const size_t SrcOffset = ((size_t)(Y + Row) * Src.m_Width + X) * 4;
		const size_t DstOffset = (size_t)Row * W * 4;
		mem_copy(&pDst[DstOffset], &pSrc[SrcOffset], (size_t)W * 4);
	}
	return true;
}

std::optional<int> CMenus::AssetsEditorComposeImage(CImageInfo &OutputImage)
{
	const auto &vAssets = m_AssetsEditorState.m_avAssets[m_AssetsEditorState.m_Type];
	const int MainAssetIndex = m_AssetsEditorState.m_aMainAssetIndex[m_AssetsEditorState.m_Type];
	if(vAssets.empty() || MainAssetIndex < 0 || MainAssetIndex >= (int)vAssets.size())
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("No assets available for composition."));
		m_AssetsEditorState.m_StatusIsError = true;
		return std::nullopt;
	}

	const SAssetsEditorAssetEntry &MainAsset = vAssets[MainAssetIndex];
	const CImageInfo *pBaseImage = AssetsEditorGetCachedImage(m_AssetsEditorState.m_Type, MainAsset.m_aName, vAssets, Graphics());
	if(pBaseImage == nullptr)
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Failed to load main asset: %s"), MainAsset.m_aName);
		m_AssetsEditorState.m_StatusIsError = true;
		return std::nullopt;
	}

	CImageInfo BaseImageStable = pBaseImage->DeepCopy();
	OutputImage = BaseImageStable.DeepCopy();
	if(BaseImageStable.m_pData == nullptr || OutputImage.m_pData == nullptr)
	{
		BaseImageStable.Free();
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to copy main asset image."));
		m_AssetsEditorState.m_StatusIsError = true;
		return std::nullopt;
	}
	if(OutputImage.m_Format != CImageInfo::FORMAT_RGBA)
		ConvertToRgba(OutputImage);
	if(OutputImage.m_pData == nullptr || OutputImage.m_Format != CImageInfo::FORMAT_RGBA)
	{
		BaseImageStable.Free();
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to prepare main asset image."));
		m_AssetsEditorState.m_StatusIsError = true;
		return std::nullopt;
	}

	struct SPreparedDonor
	{
		char m_aName[64] = {0};
		CImageInfo m_Image;
	};
	std::vector<SPreparedDonor> vPreparedDonors;

	auto FindPreparedDonor = [&](const char *pName) -> const CImageInfo * {
		for(const auto &Prepared : vPreparedDonors)
		{
			if(str_comp(Prepared.m_aName, pName) == 0)
				return &Prepared.m_Image;
		}
		return nullptr;
	};

	auto GetPreparedDonor = [&](const char *pName) -> const CImageInfo * {
		if(str_comp(pName, MainAsset.m_aName) == 0)
			return &BaseImageStable;

		if(const CImageInfo *pPrepared = FindPreparedDonor(pName))
			return pPrepared;

		const CImageInfo *pCachedDonor = AssetsEditorGetCachedImage(m_AssetsEditorState.m_Type, pName, vAssets, Graphics());
		if(pCachedDonor == nullptr)
			return nullptr;

		SPreparedDonor &Prepared = vPreparedDonors.emplace_back();
		str_copy(Prepared.m_aName, pName);
		Prepared.m_Image = pCachedDonor->DeepCopy();
		if(Prepared.m_Image.m_pData == nullptr)
			return nullptr;
		if(Prepared.m_Image.m_Format != CImageInfo::FORMAT_RGBA)
			ConvertToRgba(Prepared.m_Image);
		if(Prepared.m_Image.m_pData == nullptr || Prepared.m_Image.m_Format != CImageInfo::FORMAT_RGBA)
			return nullptr;
		return &Prepared.m_Image;
	};

	const int GridX = maximum(1, AssetsEditorGridX(m_AssetsEditorState.m_Type));
	const int GridY = maximum(1, AssetsEditorGridY(m_AssetsEditorState.m_Type));
	const int DestGridW = OutputImage.m_Width / GridX;
	const int DestGridH = OutputImage.m_Height / GridY;
	int SkippedSlots = 0;

	for(const auto &Slot : m_AssetsEditorState.m_vPartSlots)
	{
		const ColorRGBA SlotTint = AssetsEditorSlotTint(Slot);
		const bool HasColorOverride = AssetsEditorHasColorOverride(SlotTint, Slot.m_ColorBlendMode);
		const bool NeedsSourceCopy = str_comp(Slot.m_aSourceAsset, MainAsset.m_aName) != 0 ||
					     Slot.m_SrcX != Slot.m_DstX || Slot.m_SrcY != Slot.m_DstY ||
					     Slot.m_SrcW != Slot.m_DstW || Slot.m_SrcH != Slot.m_DstH;
		if(!AssetsEditorSlotNeedsProcessing(Slot, MainAsset.m_aName))
			continue;

		if(DestGridW <= 0 || DestGridH <= 0 || Slot.m_DstW <= 0 || Slot.m_DstH <= 0)
		{
			++SkippedSlots;
			continue;
		}

		const int DestX = Slot.m_DstX * DestGridW;
		const int DestY = Slot.m_DstY * DestGridH;
		const int DestW = Slot.m_DstW * DestGridW;
		const int DestH = Slot.m_DstH * DestGridH;
		if(DestW <= 0 || DestH <= 0)
		{
			++SkippedSlots;
			continue;
		}

		if(NeedsSourceCopy)
		{
			const CImageInfo *pDonorImage = GetPreparedDonor(Slot.m_aSourceAsset);
			if(pDonorImage == nullptr)
			{
				++SkippedSlots;
				continue;
			}

			const int SrcGridW = pDonorImage->m_Width / GridX;
			const int SrcGridH = pDonorImage->m_Height / GridY;
			if(SrcGridW <= 0 || SrcGridH <= 0 || Slot.m_SrcW <= 0 || Slot.m_SrcH <= 0)
			{
				++SkippedSlots;
				continue;
			}

			const int SrcX = Slot.m_SrcX * SrcGridW;
			const int SrcY = Slot.m_SrcY * SrcGridH;
			const int SrcW = Slot.m_SrcW * SrcGridW;
			const int SrcH = Slot.m_SrcH * SrcGridH;
			if(SrcW <= 0 || SrcH <= 0)
			{
				++SkippedSlots;
				continue;
			}

			if(!AssetsEditorCopyRectScaledNearest(OutputImage, *pDonorImage, DestX, DestY, DestW, DestH, SrcX, SrcY, SrcW, SrcH))
			{
				++SkippedSlots;
				continue;
			}
		}

		if(HasColorOverride)
			AssetsEditorApplyColorOverrideToImageRect(OutputImage, DestX, DestY, DestW, DestH, SlotTint, Slot.m_ColorBlendMode);
	}

	for(auto &Prepared : vPreparedDonors)
		Prepared.m_Image.Free();
	BaseImageStable.Free();

	return SkippedSlots;
}

bool CMenus::AssetsEditorExport()
{
	AssetsEditorCommitExportNameForType();
	if(m_AssetsEditorState.m_aExportName[0] == '\0')
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Choose export name first."));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}
	if(str_comp(m_AssetsEditorState.m_aExportName, "default") == 0)
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("The name \"default\" is reserved."));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}
	if(str_find(m_AssetsEditorState.m_aExportName, "/") || str_find(m_AssetsEditorState.m_aExportName, "\\") || str_find(m_AssetsEditorState.m_aExportName, ".."))
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Export name contains invalid characters."));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	CImageInfo OutputImage;
	const auto SkippedSlots = AssetsEditorComposeImage(OutputImage);
	if(!SkippedSlots)
		return false;
	if(!qmclient::assets_editor::KeepCompleteExportImage(OutputImage, *SkippedSlots))
	{
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Export failed. Could not compose %d edited slot(s)."), *SkippedSlots);
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_SKIN)
	{
		const char *pExportName = m_AssetsEditorState.m_aExportName;
		char aAtlasPath[IO_MAX_PATH_LENGTH];
		char aBodyPath[IO_MAX_PATH_LENGTH];
		char aFeetPath[IO_MAX_PATH_LENGTH];
		char aHandsPath[IO_MAX_PATH_LENGTH];
		char aJsonPath[IO_MAX_PATH_LENGTH];
		str_format(aAtlasPath, sizeof(aAtlasPath), "skins/%s.png", pExportName);
		str_format(aBodyPath, sizeof(aBodyPath), "skins7/body/%s.png", pExportName);
		str_format(aFeetPath, sizeof(aFeetPath), "skins7/feet/%s.png", pExportName);
		str_format(aHandsPath, sizeof(aHandsPath), "skins7/hands/%s.png", pExportName);
		str_format(aJsonPath, sizeof(aJsonPath), "skins7/%s.json", pExportName);
		if(Storage()->FileExists(aAtlasPath, IStorage::TYPE_SAVE) ||
			Storage()->FileExists(aBodyPath, IStorage::TYPE_SAVE) || Storage()->FileExists(aFeetPath, IStorage::TYPE_SAVE) ||
			Storage()->FileExists(aHandsPath, IStorage::TYPE_SAVE) || Storage()->FileExists(aJsonPath, IStorage::TYPE_SAVE))
		{
			OutputImage.Free();
			str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Asset with this name already exists."));
			m_AssetsEditorState.m_StatusIsError = true;
			return false;
		}

		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("skins7", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("skins7/body", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("skins7/feet", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("skins7/hands", IStorage::TYPE_SAVE);

		IOHANDLE AtlasFile = Storage()->OpenFile(aAtlasPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		const bool SavedAtlas = AtlasFile && CImageLoader::SavePng(AtlasFile, aAtlasPath, OutputImage);

		auto SaveSkinPart = [this, &OutputImage](const char *pPath, int X, int Y, int W, int H) {
			CImageInfo PartImage;
			if(!AssetsEditorExtractSubImage(OutputImage, PartImage, X, Y, W, H))
				return false;
			IOHANDLE File = Storage()->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			const bool Success = File && CImageLoader::SavePng(File, pPath, PartImage);
			PartImage.Free();
			return Success;
		};

		const bool SavedBody = SaveSkinPart(aBodyPath, 0, 0, 96, 96);
		const bool SavedFeet = SaveSkinPart(aFeetPath, 96, 0, 96, 96);
		const bool SavedHands = SaveSkinPart(aHandsPath, 192, 0, 32, 32);
		const bool SavedSkinfile = SaveSkinfileFromParts(Storage(), pExportName, pExportName, "", "", pExportName, pExportName, "standard");
		OutputImage.Free();
		if(!SavedAtlas || !SavedBody || !SavedFeet || !SavedHands || !SavedSkinfile)
		{
			if(SavedAtlas)
				Storage()->RemoveFile(aAtlasPath, IStorage::TYPE_SAVE);
			if(SavedBody)
				Storage()->RemoveFile(aBodyPath, IStorage::TYPE_SAVE);
			if(SavedFeet)
				Storage()->RemoveFile(aFeetPath, IStorage::TYPE_SAVE);
			if(SavedHands)
				Storage()->RemoveFile(aHandsPath, IStorage::TYPE_SAVE);
			if(SavedSkinfile)
				Storage()->RemoveFile(aJsonPath, IStorage::TYPE_SAVE);
			str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to export skin files."));
			m_AssetsEditorState.m_StatusIsError = true;
			return false;
		}

		AssetsEditorReloadAssetsImagesOnly();
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX | CSkinDescriptor::FLAG_SEVEN);
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Exported to %s"), aAtlasPath);
		m_AssetsEditorState.m_StatusIsError = false;
		m_AssetsEditorState.m_HasUnsavedChanges = false;
		return true;
	}

	char aFolder[IO_MAX_PATH_LENGTH];
	char aPngPath[IO_MAX_PATH_LENGTH];
	char aAssetFolderPath[IO_MAX_PATH_LENGTH];
	if(m_AssetsEditorState.m_Type == ASSETS_EDITOR_TYPE_ENTITIES)
	{
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("assets/entities", IStorage::TYPE_SAVE);
		str_format(aPngPath, sizeof(aPngPath), "assets/entities/%s.png", m_AssetsEditorState.m_aExportName);
		str_format(aAssetFolderPath, sizeof(aAssetFolderPath), "assets/entities/%s", m_AssetsEditorState.m_aExportName);
	}
	else
	{
		str_format(aFolder, sizeof(aFolder), "assets/%s", AssetsEditorTypeName(m_AssetsEditorState.m_Type));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
		str_format(aPngPath, sizeof(aPngPath), "assets/%s/%s.png", AssetsEditorTypeName(m_AssetsEditorState.m_Type), m_AssetsEditorState.m_aExportName);
		str_format(aAssetFolderPath, sizeof(aAssetFolderPath), "assets/%s/%s", AssetsEditorTypeName(m_AssetsEditorState.m_Type), m_AssetsEditorState.m_aExportName);
	}

	if(Storage()->FileExists(aPngPath, IStorage::TYPE_SAVE) || Storage()->FolderExists(aAssetFolderPath, IStorage::TYPE_SAVE))
	{
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Asset with this name already exists."));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	IOHANDLE File = Storage()->OpenFile(aPngPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File || !CImageLoader::SavePng(File, aPngPath, OutputImage))
	{
		OutputImage.Free();
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Failed to write PNG export."));
		m_AssetsEditorState.m_StatusIsError = true;
		return false;
	}

	OutputImage.Free();
	AssetsEditorReloadAssetsImagesOnly();
	const int RefreshTab = AssetsEditorTypeToCustomTab(m_AssetsEditorState.m_Type);
	if(RefreshTab >= 0)
		ClearCustomItems(RefreshTab);
	str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Exported to %s"), aPngPath);
	m_AssetsEditorState.m_StatusIsError = false;
	m_AssetsEditorState.m_HasUnsavedChanges = false;
	return true;
}

void CMenus::AssetsEditorCancelDrag()
{
	m_AssetsEditorState.m_Gesture.Reset();
	m_AssetsEditorState.m_DragSourceIndex = -1;
	m_AssetsEditorState.m_aDraggedSourceAsset[0] = '\0';
	m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
	m_AssetsEditorState.m_HoverCyclePositionX = -1;
	m_AssetsEditorState.m_HoverCyclePositionY = -1;
	m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
	m_AssetsEditorState.m_vHoverCycleCandidates.clear();
}

void CMenus::AssetsEditorRequestAction(SAssetsEditorAction Action)
{
	auto &State = m_AssetsEditorState;
	if(State.m_PendingAction)
		return;
	AssetsEditorCancelDrag();
	State.m_PendingAction = std::move(Action);
	if(!State.m_HasUnsavedChanges)
		AssetsEditorExecutePendingAction();
}

void CMenus::AssetsEditorExecutePendingAction()
{
	auto &State = m_AssetsEditorState;
	const auto Action = std::move(*State.m_PendingAction);
	State.m_PendingAction.reset();
	switch(Action.m_Kind)
	{
	case EAssetsEditorAction::CLOSE:
		AssetsEditorCloseNow();
		break;
	case EAssetsEditorAction::SWITCH_TYPE:
		AssetsEditorCommitExportNameForType();
		State.m_Type = Action.m_Type;
		AssetsEditorEnsureAssetsLoadedForType(State.m_Type);
		AssetsEditorSyncExportNameFromType();
		AssetsEditorResetPartSlots();
		break;
	case EAssetsEditorAction::SWITCH_MAIN_ASSET:
	{
		// 保存会重新扫描并排序目录，执行切换时按名称重新定位。
		const int Index = AssetsEditorFindAssetIndexByName(State.m_avAssets[State.m_Type], Action.m_MainAssetName.c_str());
		if(Index >= 0)
		{
			State.m_aMainAssetIndex[State.m_Type] = Index;
			AssetsEditorResetPartSlots();
		}
		break;
	}
	}
}

void CMenus::AssetsEditorCloseNow()
{
	Ui()->CloseInteractionSource(&m_AssetsEditorState, "assets_editor");
	AssetsEditorClearAssets();
	m_AssetsEditorState = SAssetsEditorState();
}

void CMenus::AssetsEditorCollectHoveredCandidates(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, std::vector<int> &vOutCandidates) const
{
	vOutCandidates.clear();
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	if(Mouse.x < Rect.x || Mouse.x > Rect.x + Rect.w || Mouse.y < Rect.y || Mouse.y > Rect.y + Rect.h)
		return;

	struct SCandidate
	{
		int m_SlotIndex;
		float m_Area;
	};
	std::vector<SCandidate> vCandidates;
	vCandidates.reserve(vSlots.size());
	for(size_t SlotIndex = 0; SlotIndex < vSlots.size(); ++SlotIndex)
	{
		const SAssetsEditorPartSlot &Slot = vSlots[SlotIndex];
		CUIRect SlotRect;
		if(!AssetsEditorGetSlotRectInFitted(Rect, Type, Slot, SlotRect))
			continue;
		if(Mouse.x < SlotRect.x || Mouse.x >= SlotRect.x + SlotRect.w || Mouse.y < SlotRect.y || Mouse.y >= SlotRect.y + SlotRect.h)
			continue;

		SCandidate &Candidate = vCandidates.emplace_back();
		Candidate.m_SlotIndex = (int)SlotIndex;
		Candidate.m_Area = SlotRect.w * SlotRect.h;
	}

	std::stable_sort(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
		if(Left.m_Area != Right.m_Area)
			return Left.m_Area < Right.m_Area;
		return Left.m_SlotIndex < Right.m_SlotIndex;
	});

	vOutCandidates.reserve(vCandidates.size());
	for(const auto &Candidate : vCandidates)
		vOutCandidates.push_back(Candidate.m_SlotIndex);
}

int CMenus::AssetsEditorResolveHoveredSlotWithCycle(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, bool ClickedLmb, int PreferredSlotIndex)
{
	std::vector<int> vCandidates;
	AssetsEditorCollectHoveredCandidates(Rect, Type, vSlots, Mouse, vCandidates);
	if(vCandidates.empty())
	{
		if(PreferredSlotIndex < 0)
		{
			m_AssetsEditorState.m_HoverCycleSlotIndex = -1;
			m_AssetsEditorState.m_HoverCyclePositionX = -1;
			m_AssetsEditorState.m_HoverCyclePositionY = -1;
			m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
			m_AssetsEditorState.m_vHoverCycleCandidates.clear();
		}
		return -1;
	}

	if(PreferredSlotIndex >= 0 && PreferredSlotIndex < (int)vSlots.size())
	{
		const SAssetsEditorPartSlot &PreferredSlot = vSlots[PreferredSlotIndex];
		for(const int Candidate : vCandidates)
		{
			const SAssetsEditorPartSlot &CandidateSlot = vSlots[Candidate];
			if(str_comp(CandidateSlot.m_aFamilyKey, PreferredSlot.m_aFamilyKey) != 0)
				continue;
			if(!AssetsEditorSlotSameNormalizedSize(CandidateSlot, PreferredSlot))
				continue;
			return Candidate;
		}
		return vCandidates.front();
	}

	const int MousePosX = (int)(Mouse.x * 10.0f);
	const int MousePosY = (int)(Mouse.y * 10.0f);
	const bool SamePosition = m_AssetsEditorState.m_HoverCyclePositionX == MousePosX && m_AssetsEditorState.m_HoverCyclePositionY == MousePosY;
	const bool SameCandidates = m_AssetsEditorState.m_vHoverCycleCandidates.size() == vCandidates.size() &&
				    std::equal(m_AssetsEditorState.m_vHoverCycleCandidates.begin(), m_AssetsEditorState.m_vHoverCycleCandidates.end(), vCandidates.begin());
	if(!SamePosition || !SameCandidates)
	{
		m_AssetsEditorState.m_HoverCyclePositionX = MousePosX;
		m_AssetsEditorState.m_HoverCyclePositionY = MousePosY;
		m_AssetsEditorState.m_HoverCycleCandidateCursor = 0;
		m_AssetsEditorState.m_vHoverCycleCandidates = vCandidates;
	}
	else if(ClickedLmb && vCandidates.size() > 1)
	{
		m_AssetsEditorState.m_HoverCycleCandidateCursor = (m_AssetsEditorState.m_HoverCycleCandidateCursor + 1) % (int)vCandidates.size();
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Selected candidate %d/%d under cursor."), m_AssetsEditorState.m_HoverCycleCandidateCursor + 1, (int)vCandidates.size());
		m_AssetsEditorState.m_StatusIsError = false;
	}

	m_AssetsEditorState.m_HoverCycleCandidateCursor = std::clamp(m_AssetsEditorState.m_HoverCycleCandidateCursor, 0, (int)vCandidates.size() - 1);
	const int Selected = vCandidates[m_AssetsEditorState.m_HoverCycleCandidateCursor];
	m_AssetsEditorState.m_HoverCycleSlotIndex = Selected;
	return Selected;
}

void CMenus::AssetsEditorApplyDrop(int TargetSlotIndex, const char *pDonorName, int SourceSlotIndex, bool ApplyAllSameSize)
{
	if(TargetSlotIndex < 0 || TargetSlotIndex >= (int)m_AssetsEditorState.m_vPartSlots.size() || SourceSlotIndex < 0 ||
		SourceSlotIndex >= (int)m_AssetsEditorState.m_vPartSlots.size() || pDonorName == nullptr || pDonorName[0] == '\0')
		return;

	const SAssetsEditorPartSlot SourceSlot = m_AssetsEditorState.m_vPartSlots[SourceSlotIndex];
	const SAssetsEditorPartSlot &TargetSlot = m_AssetsEditorState.m_vPartSlots[TargetSlotIndex];
	const bool SameSize = AssetsEditorSlotSameNormalizedSize(SourceSlot, TargetSlot);
	if(ApplyAllSameSize && !SameSize)
	{
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Can only drop onto same-size parts."));
		m_AssetsEditorState.m_StatusIsError = true;
		return;
	}

	auto AssignSlot = [pDonorName, &SourceSlot](SAssetsEditorPartSlot &Slot) {
		str_copy(Slot.m_aSourceAsset, pDonorName);
		Slot.m_SourceSpriteId = SourceSlot.m_SpriteId;
		Slot.m_SrcX = SourceSlot.m_DstX;
		Slot.m_SrcY = SourceSlot.m_DstY;
		Slot.m_SrcW = SourceSlot.m_DstW;
		Slot.m_SrcH = SourceSlot.m_DstH;
	};

	if(ApplyAllSameSize)
	{
		int Changed = 0;
		for(auto &Slot : m_AssetsEditorState.m_vPartSlots)
		{
			if(str_comp(Slot.m_aFamilyKey, TargetSlot.m_aFamilyKey) != 0)
				continue;
			if(!AssetsEditorSlotSameNormalizedSize(Slot, TargetSlot))
				continue;
			AssignSlot(Slot);
			++Changed;
		}
		str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Applied to %d same-family part(s)."), Changed);
		m_AssetsEditorState.m_StatusIsError = false;
	}
	else
	{
		AssignSlot(m_AssetsEditorState.m_vPartSlots[TargetSlotIndex]);
		str_copy(m_AssetsEditorState.m_aStatusMessage, Localize("Part updated."));
		m_AssetsEditorState.m_StatusIsError = false;
	}

	m_AssetsEditorState.m_SelectedTargetSlotIndex = TargetSlotIndex;
	m_AssetsEditorState.m_DirtyPreview = true;
	m_AssetsEditorState.m_HasUnsavedChanges = true;
}

void CMenus::AssetsEditorUpdatePreviewIfDirty()
{
	if(!m_AssetsEditorState.m_DirtyPreview)
		return;

	CImageInfo ComposedImage;
	if(const auto SkippedSlots = AssetsEditorComposeImage(ComposedImage))
	{
		if(*SkippedSlots > 0)
		{
			str_format(m_AssetsEditorState.m_aStatusMessage, sizeof(m_AssetsEditorState.m_aStatusMessage), Localize("Preview built with %d skipped slot(s)."), *SkippedSlots);
			m_AssetsEditorState.m_StatusIsError = false;
		}
		m_AssetsEditorState.m_ComposedPreviewWidth = ComposedImage.m_Width;
		m_AssetsEditorState.m_ComposedPreviewHeight = ComposedImage.m_Height;
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
		m_AssetsEditorState.m_ComposedPreviewTexture = Graphics()->LoadTextureRawMove(ComposedImage, 0, "assets_editor_preview");
	}
	else
	{
		Graphics()->UnloadTexture(&m_AssetsEditorState.m_ComposedPreviewTexture);
		m_AssetsEditorState.m_ComposedPreviewWidth = 0;
		m_AssetsEditorState.m_ComposedPreviewHeight = 0;
	}

	m_AssetsEditorState.m_DirtyPreview = false;
}

void CMenus::AssetsEditorRenderCanvas(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int W, int H, int Type, bool ShowGrid, int HighlightSlot, bool ShowTintFeedback, int PersistentHighlightSlot, const SSettingsContentMetrics &Metrics)
{
	using namespace ui_token::assets_editor;
	const float Cell = ui_token::spacing::MD * Metrics.m_UiScale;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(CHECKER_DARK);
	const IGraphics::CQuadItem Base(Rect.x, Rect.y, Rect.w, Rect.h);
	Graphics()->QuadsDrawTL(&Base, 1);
	Graphics()->SetColor(CHECKER_LIGHT);
	IGraphics::CQuadItem aTiles[64];
	int TileCount = 0;
	for(int Row = 0; Row * Cell < Rect.h; ++Row)
	{
		for(int Col = Row % 2; Col * Cell < Rect.w; Col += 2)
		{
			const float X = Col * Cell;
			const float Y = Row * Cell;
			aTiles[TileCount++] = {Rect.x + X, Rect.y + Y, std::min(Cell, Rect.w - X), std::min(Cell, Rect.h - Y)};
			if(TileCount == (int)std::size(aTiles))
			{
				Graphics()->QuadsDrawTL(aTiles, TileCount);
				TileCount = 0;
			}
		}
	}
	if(TileCount)
		Graphics()->QuadsDrawTL(aTiles, TileCount);
	Graphics()->QuadsEnd();

	if(!Texture.IsValid() || W <= 0 || H <= 0)
	{
		Ui()->DoLabel(&Rect, Localize("No preview"), Metrics.m_BodySize, TEXTALIGN_MC);
		return;
	}

	CUIRect FittedRect;
	if(!AssetsEditorDrawTextureFitted(Rect, Texture, W, H, Graphics(), &FittedRect))
	{
		Ui()->DoLabel(&Rect, Localize("No preview"), Metrics.m_BodySize, TEXTALIGN_MC);
		return;
	}

	Graphics()->TextureClear();
	if(ShowGrid)
	{
		const int GridX = AssetsEditorGridX(Type);
		const int GridY = AssetsEditorGridY(Type);
		if(GridX > 0 && GridY > 0)
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(GRID);
			for(int X = 0; X <= GridX; ++X)
			{
				const float Px = FittedRect.x + (FittedRect.w * X) / GridX;
				const IGraphics::CLineItem Line(Px, FittedRect.y, Px, FittedRect.y + FittedRect.h);
				Graphics()->LinesDraw(&Line, 1);
			}
			for(int Y = 0; Y <= GridY; ++Y)
			{
				const float Py = FittedRect.y + (FittedRect.h * Y) / GridY;
				const IGraphics::CLineItem Line(FittedRect.x, Py, FittedRect.x + FittedRect.w, Py);
				Graphics()->LinesDraw(&Line, 1);
			}
			Graphics()->LinesEnd();
		}
	}

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	auto DrawSlot = [&](int SlotIndex) {
		const SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vPartSlots[SlotIndex];
		CUIRect SlotRect;
		if(!AssetsEditorGetSlotRectInFitted(FittedRect, Type, Slot, SlotRect))
			return;

		const bool IsHighlighted = SlotIndex == HighlightSlot;
		const bool IsPersistentHighlight = SlotIndex == PersistentHighlightSlot;
		const ColorRGBA SlotTint = AssetsEditorSlotTint(Slot);
		const bool HasTintOverride = ShowTintFeedback && AssetsEditorHasColorOverride(SlotTint, Slot.m_ColorBlendMode);
		if(IsHighlighted && ShowTintFeedback && m_AssetsEditorState.m_Gesture.Dragging())
		{
			const bool Valid = !m_AssetsEditorState.m_ApplySameSize || AssetsEditorSlotSameNormalizedSize(Slot, m_AssetsEditorState.m_vPartSlots[m_AssetsEditorState.m_DragSourceIndex]);
			Graphics()->SetColor(Valid ? DROP_VALID : DROP_INVALID);
		}
		else if(IsPersistentHighlight)
			Graphics()->SetColor(SELECTED);
		else if(IsHighlighted)
			Graphics()->SetColor(HOVER);
		else if(HasTintOverride)
			Graphics()->SetColor(SlotTint.WithAlpha(1.0f));
		else
			Graphics()->SetColor(SLOT);

		IGraphics::CLineItem aLines[4] = {
			IGraphics::CLineItem(SlotRect.x, SlotRect.y, SlotRect.x + SlotRect.w, SlotRect.y),
			IGraphics::CLineItem(SlotRect.x + SlotRect.w, SlotRect.y, SlotRect.x + SlotRect.w, SlotRect.y + SlotRect.h),
			IGraphics::CLineItem(SlotRect.x + SlotRect.w, SlotRect.y + SlotRect.h, SlotRect.x, SlotRect.y + SlotRect.h),
			IGraphics::CLineItem(SlotRect.x, SlotRect.y + SlotRect.h, SlotRect.x, SlotRect.y),
		};
		Graphics()->LinesDraw(aLines, 4);
	};
	// 共边和重叠槽位先画普通框，落点与持久选框始终可见。
	for(int i = 0; i < (int)m_AssetsEditorState.m_vPartSlots.size(); ++i)
	{
		if(i != PersistentHighlightSlot && i != HighlightSlot)
			DrawSlot(i);
	}
	if(PersistentHighlightSlot >= 0 && PersistentHighlightSlot != HighlightSlot)
		DrawSlot(PersistentHighlightSlot);
	if(HighlightSlot >= 0)
		DrawSlot(HighlightSlot);
	Graphics()->LinesEnd();
}

bool CMenus::AssetsEditorHasSlotChanges() const
{
	const auto &State = m_AssetsEditorState;
	const auto &vAssets = State.m_avAssets[State.m_Type];
	if(vAssets.empty())
		return false;
	const char *pMainName = vAssets[State.m_aMainAssetIndex[State.m_Type]].m_aName;
	return std::any_of(State.m_vPartSlots.begin(), State.m_vPartSlots.end(), [pMainName](const auto &Slot) { return !Slot.IsResetTo(pMainName); });
}

void CMenus::AssetsEditorResetSlot(int SlotIndex)
{
	auto &State = m_AssetsEditorState;
	if(SlotIndex < 0 || SlotIndex >= (int)State.m_vPartSlots.size())
		return;
	const auto &vAssets = State.m_avAssets[State.m_Type];
	const int DefaultAssetIndex = AssetsEditorFindAssetIndexByName(vAssets, "default");
	const char *pResetAssetName = vAssets[DefaultAssetIndex >= 0 ? DefaultAssetIndex : State.m_aMainAssetIndex[State.m_Type]].m_aName;
	const bool Changed = State.m_vPartSlots[SlotIndex].ResetTo(pResetAssetName);
	State.m_SelectedTargetSlotIndex = SlotIndex;
	if(Changed || State.m_HasUnsavedChanges)
		State.m_HasUnsavedChanges = AssetsEditorHasSlotChanges();
	if(Changed)
	{
		State.m_DirtyPreview = true;
		str_copy(State.m_aStatusMessage, Localize("Part reset to default asset."));
		State.m_StatusIsError = false;
	}
}

void CMenus::AssetsEditorOpenColorPicker(int SlotIndex)
{
	if(SlotIndex < 0 || SlotIndex >= (int)m_AssetsEditorState.m_vPartSlots.size())
		return;
	SAssetsEditorPartSlot &Slot = m_AssetsEditorState.m_vPartSlots[SlotIndex];
	m_AssetsEditorState.m_SelectedTargetSlotIndex = SlotIndex;
	const ColorRGBA SlotColor = AssetsEditorSlotColorToRgba(Slot.m_Color);
	m_AssetsEditorState.m_ColorPickerValue = ColorHSLA(Slot.m_Color, true).Pack(false);
	m_ColorPickerPopupContext.m_pHslaColor = &m_AssetsEditorState.m_ColorPickerValue;
	m_ColorPickerPopupContext.m_HslaColor = color_cast<ColorHSLA>(SlotColor);
	m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(m_ColorPickerPopupContext.m_HslaColor);
	m_ColorPickerPopupContext.m_RgbaColor = SlotColor;
	m_ColorPickerPopupContext.m_Alpha = false;
	m_ColorPickerPopupContext.m_AlphaPercent = false;
	m_AssetsEditorState.m_ColorPickerSlotIndex = SlotIndex;
	Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
}

void CMenus::AssetsEditorRenderCanvases(CUIRect Left, CUIRect Right, CUIRect Viewport, bool Blocked, const SSettingsContentMetrics &Metrics)
{
	using namespace qmclient::assets_editor;
	auto &State = m_AssetsEditorState;
	const int Type = State.m_Type;
	auto &vAssets = State.m_avAssets[Type];
	if(vAssets.empty())
		return;
	auto &Donor = vAssets[State.m_aDonorAssetIndex[Type]];
	if(!Ui()->RenderOnly())
	{
		AssetsEditorEnsurePreviewLoaded(Donor);
		AssetsEditorUpdatePreviewIfDirty();
	}

	CUIRect DonorFit{}, TargetFit{};
	const bool HasDonor = AssetsEditorCalcFittedRect(Left, Donor.m_PreviewWidth, Donor.m_PreviewHeight, DonorFit);
	const bool HasTarget = AssetsEditorCalcFittedRect(Right, State.m_ComposedPreviewWidth, State.m_ComposedPreviewHeight, TargetFit);
	auto &Gesture = State.m_Gesture;
	const vec2 Mouse = Ui()->MousePos();
	const auto &vSlots = State.m_vPartSlots;
	SCanvasHit Hit;
	if(!Ui()->RenderOnly())
	{
		if(!Blocked && HasDonor && Ui()->MouseHovered(&DonorFit))
			Hit.m_Index = AssetsEditorResolveHoveredSlotWithCycle(DonorFit, Type, vSlots, Mouse, false, -1);
		else if(!Blocked && HasTarget && Ui()->MouseHovered(&TargetFit))
		{
			Hit.m_Target = true;
			Hit.m_Index = AssetsEditorResolveHoveredSlotWithCycle(TargetFit, Type, vSlots, Mouse, false, Gesture.Dragging() ? State.m_DragSourceIndex : -1);
		}
		if(!Blocked && Ui()->MouseButtonClicked(1) && Hit.m_Target && Hit.m_Index >= 0)
		{
			AssetsEditorCancelDrag();
			AssetsEditorResetSlot(Hit.m_Index);
		}
		const auto Origin = Gesture.Origin();
		const auto Action = Gesture.Update(Hit, State.m_vHoverCycleCandidates, Mouse, Ui()->MouseButtonClicked(0), Ui()->MouseButton(0), Blocked, Client()->LocalTime(), ui_token::spacing::XS * Metrics.m_UiScale);
		switch(Action)
		{
		case ECanvasAction::BEGIN_DRAG:
		{
			const auto &Slot = vSlots[Origin.m_Index];
			State.m_DragSourceIndex = Origin.m_Index;
			if(Origin.m_Target)
			{
				// 合成槽可能已从其他位置替换，继续拖动时沿用实际来源切片。
				const auto Source = std::find_if(vSlots.begin(), vSlots.end(), [&](const auto &Part) {
					return Part.m_DstX == Slot.m_SrcX && Part.m_DstY == Slot.m_SrcY && Part.m_DstW == Slot.m_SrcW && Part.m_DstH == Slot.m_SrcH;
				});
				if(Source != vSlots.end())
					State.m_DragSourceIndex = Source - vSlots.begin();
			}
			str_copy(State.m_aDraggedSourceAsset, Origin.m_Target ? Slot.m_aSourceAsset : Donor.m_aName);
			break;
		}
		case ECanvasAction::SELECT:
			State.m_SelectedTargetSlotIndex = Hit.m_Index;
			break;
		case ECanvasAction::PICK_COLOR:
			AssetsEditorOpenColorPicker(Hit.m_Index);
			break;
		case ECanvasAction::CYCLE:
			Hit.m_Index = AssetsEditorResolveHoveredSlotWithCycle(DonorFit, Type, vSlots, Mouse, true, -1);
			break;
		case ECanvasAction::QUICK_REPLACE:
		{
			const int Target = QuickReplaceTarget(State.m_SelectedTargetSlotIndex, Hit.m_Index);
			AssetsEditorApplyDrop(Target, Donor.m_aName, Hit.m_Index, State.m_ApplySameSize);
			break;
		}
		case ECanvasAction::DROP:
			AssetsEditorApplyDrop(Hit.m_Index, State.m_aDraggedSourceAsset, State.m_DragSourceIndex, State.m_ApplySameSize);
			break;
		case ECanvasAction::NONE: break;
		}
		AssetsEditorUpdatePreviewIfDirty();
	}

	if(Ui()->DrawingSuppressed())
		return;
	AssetsEditorRenderCanvas(Left, Donor.m_PreviewTexture, Donor.m_PreviewWidth, Donor.m_PreviewHeight, Type, State.m_ShowGrid, Hit.m_Target ? -1 : Hit.m_Index, false, -1, Metrics);
	AssetsEditorRenderCanvas(Right, State.m_ComposedPreviewTexture, State.m_ComposedPreviewWidth, State.m_ComposedPreviewHeight, Type, State.m_ShowGrid, Hit.m_Target ? Hit.m_Index : -1, true, State.m_SelectedTargetSlotIndex, Metrics);
	if(!Gesture.Dragging() && Hit.m_Index >= 0 && !Ui()->IsPopupOpen())
	{
		const auto &Slot = vSlots[Hit.m_Index];
		CUIRect Anchor;
		if(AssetsEditorGetSlotRectInFitted(Hit.m_Target ? TargetFit : DonorFit, Type, Slot, Anchor))
		{
			const char *pName = Slot.m_SpriteId >= 0 ? g_pData->m_aSprites[Slot.m_SpriteId].m_pName : Slot.m_aFamilyKey;
			const int Width = Hit.m_Target ? State.m_ComposedPreviewWidth : Donor.m_PreviewWidth;
			const int Height = Hit.m_Target ? State.m_ComposedPreviewHeight : Donor.m_PreviewHeight;
			char aHint[256];
			str_format(aHint, sizeof(aHint), "%s · %d × %d px", pName, Slot.m_DstW * Width / AssetsEditorGridX(Type), Slot.m_DstH * Height / AssetsEditorGridY(Type));
			if(State.m_vHoverCycleCandidates.size() > 1)
			{
				char aCandidate[40];
				str_format(aCandidate, sizeof(aCandidate), " · %d/%d", State.m_HoverCycleCandidateCursor + 1, (int)State.m_vHoverCycleCandidates.size());
				str_append(aHint, aCandidate);
			}
			GameClient()->m_Tooltips.DoToolTipForRect(Hit.m_Target ? &Slot.m_DstX : &Slot.m_SrcX, &Anchor, aHint);
		}
	}
	if(!Gesture.Dragging())
		return;
	const auto Origin = Gesture.Origin();
	const auto &Slot = vSlots[Origin.m_Index];
	const float Line = Metrics.m_LineHeight;
	const float Padding = ui_token::spacing::XS * Metrics.m_UiScale;
	CUIRect Ghost{Mouse.x + Padding, Mouse.y + Padding, std::min(Viewport.w, Line * 8), std::min(Viewport.h, Line * 2)};
	Ghost.x = std::clamp(Ghost.x, Viewport.x, Viewport.x + (Viewport.w - Ghost.w));
	Ghost.y = std::clamp(Ghost.y, Viewport.y, Viewport.y + (Viewport.h - Ghost.h));
	DrawRoundedSurface(Ui(), Ghost, ui_token::assets_editor::GHOST, ui_token::color::BORDER_SUBTLE, ui_token::radius::MENU * Metrics.m_UiScale, ui_token::assets_editor::BORDER_WIDTH);
	Ghost.Margin(std::min({Padding, Ghost.h / 2, Ghost.w / 2}), &Ghost);
	CUIRect Thumbnail, Fitted;
	Ghost.VSplitLeft(std::min(Ghost.h, Ghost.w), &Thumbnail, &Ghost);
	const int AtlasW = Origin.m_Target ? State.m_ComposedPreviewWidth : Donor.m_PreviewWidth;
	const int AtlasH = Origin.m_Target ? State.m_ComposedPreviewHeight : Donor.m_PreviewHeight;
	if(AssetsEditorCalcFittedRect(Thumbnail, Slot.m_DstW * AtlasW / AssetsEditorGridX(Type), Slot.m_DstH * AtlasH / AssetsEditorGridY(Type), Fitted))
		AssetsEditorDrawSlotFromTexture(Fitted, Origin.m_Target ? State.m_ComposedPreviewTexture : Donor.m_PreviewTexture, Slot, Type, Graphics());
	char aLabel[160];
	str_format(aLabel, sizeof(aLabel), Localize("%s from %s"), Slot.m_SpriteId >= 0 ? g_pData->m_aSprites[Slot.m_SpriteId].m_pName : Slot.m_aFamilyKey, State.m_aDraggedSourceAsset);
	SLabelProperties Props;
	Props.m_MaxWidth = Ghost.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Ghost, aLabel, Metrics.m_SmallSize, TEXTALIGN_MC, Props);
}
