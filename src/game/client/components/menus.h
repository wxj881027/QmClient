/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_H
#define GAME_CLIENT_COMPONENTS_MENUS_H

#include <base/system.h>
#include <base/types.h>
#include <base/vmath.h>

#include <engine/client/gpu_upload_limiter.h>
#include <engine/console.h>
#include <engine/demo.h>
#include <engine/friends.h>
#include <engine/image.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/component.h>
#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/community_icons.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/menus_ingame_touch_controls.h>
#include <game/client/components/menus_settings_controls.h>
#include <game/client/components/menus_start.h>
#include <game/client/components/qmclient/settings_perf_windows.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/skins7.h>
#include <game/client/components/tclient/warlist.h>
#include <game/client/frame_scheduler.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/voting.h>

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
struct CDataSprite;

class CHttpRequest;
class CChat;
namespace qm_card_registry
{
	struct SCardNavigationTarget;
}

inline bool QmTextMatchesIncludeExcludeFilter(const char *pText, const char *pInclude, const char *pExclude)
{
	if(pText == nullptr)
		return false;
	const bool Includes = pInclude == nullptr || pInclude[0] == '\0' || str_utf8_find_nocase(pText, pInclude) != nullptr;
	const bool Excludes = pExclude != nullptr && pExclude[0] != '\0' && str_utf8_find_nocase(pText, pExclude) != nullptr;
	return Includes && !Excludes;
}

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_GUI_CURSOR = 5,
	ASSETS_TAB_ARROW = 6,
	ASSETS_TAB_STRONG_WEAK = 7,
	ASSETS_TAB_ENTITY_BG = 8,
	ASSETS_TAB_EXTRAS = 9,
	NUMBER_OF_ASSETS_TABS = 10,
};

class CUIRect;
struct IUiContext;
struct SCardMotionSpec;
struct SSettingsCardDeckVisualOptions;
class CMenus;

namespace NTranslateUiSettings
{
	void RenderTranslateUiModule(CMenus *pMenus, CUIRect &CardContent, float LineHeight, float BodySize, float LineSpacing);
}

class CMenus : public CComponent
{
	friend void NTranslateUiSettings::RenderTranslateUiModule(CMenus *pMenus, CUIRect &CardContent, float LineHeight, float BodySize, float LineSpacing);
	static ColorRGBA ms_GuiColor;
	static ColorRGBA ms_ColorTabbarInactiveOutgame;
	static ColorRGBA ms_ColorTabbarActiveOutgame;
	static ColorRGBA ms_ColorTabbarHoverOutgame;
	static ColorRGBA ms_ColorTabbarInactiveIngame;
	static ColorRGBA ms_ColorTabbarActiveIngame;
	static ColorRGBA ms_ColorTabbarHoverIngame;
	static ColorRGBA ms_ColorTabbarInactive;
	static ColorRGBA ms_ColorTabbarActive;
	static ColorRGBA ms_ColorTabbarHover;

public:
	int DoButton_Toggle(const void *pId, int Checked, const CUIRect *pRect, bool Active, unsigned Flags = BUTTONFLAG_LEFT);
	int DoButton_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, unsigned Flags = BUTTONFLAG_LEFT, const char *pImageName = nullptr, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, float FontFactor = 0.0f, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), CUIElement *pTextUiElement = nullptr, float TextFontSize = -1.0f);
	int DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator = nullptr, const ColorRGBA *pDefaultColor = nullptr, const ColorRGBA *pActiveColor = nullptr, const ColorRGBA *pHoverColor = nullptr, float EdgeRounding = 10.0f, const CCommunityIcon *pCommunityIcon = nullptr, CUIElement *pTextUiElement = nullptr, float FontSize = -1.0f);
	// feat-004: modern menu tab. No lift / height-grow; default hover/active
	// states are tinted by ui_color via the v2 anim runtime.
	int DoMenuTabV2(CButtonContainer *pButtonContainer, const char *pText, bool Active, const CUIRect *pRect, int Corners = IGraphics::CORNER_T, const ColorRGBA *pCustomDefault = nullptr, const ColorRGBA *pCustomActive = nullptr, const ColorRGBA *pCustomHover = nullptr, const CCommunityIcon *pCommunityIcon = nullptr, CUIElement *pTextUiElement = nullptr, float ContentScale = 1.0f);
	ColorRGBA MenuPanelColor(float AlphaScale = 1.0f) const;
	ColorRGBA MenuPanelElevatedColor(float AlphaScale = 1.0f) const;
	ColorRGBA BrowserPanelColor(float AlphaScale = 1.0f) const;
	ColorRGBA BrowserPanelElevatedColor(float AlphaScale = 1.0f) const;
	ColorRGBA SettingsTabbarColor(float AlphaScale = 1.0f) const;

	int DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, unsigned Flags, bool ProcessInput = true);
	int DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect, float BodySize = -1.0f);
	int DoButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pText, int *pValue, CUIRect *pRect, float VMargin);
	int DoButton_CheckBox_Number(const void *pId, const char *pText, int Checked, const CUIRect *pRect);

	bool DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags = 0u, const char *pSuffix = "");
	bool DoLine_RadioMenu(CUIRect &View, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value);
	bool DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand);

private:
	IUiContext SettingsUiContext(const char *pScope, float UiScale = 1.0f);
	int DoSettingsDropDown(CUIRect *pRect, int CurSelection, const char *const *ppStrs, int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties = {});
	SCardMotionSpec SettingsCardMotionSpec() const;
	SSettingsCardDeckVisualOptions SettingsCardDeckVisualOptions() const;
	qm_card_order::CModel &SettingsCardOrderModel();
	qm_card_order::CModel &SettingsCardOrderModelForRenderPass();
	CSettingsCardDeck &SettingsCardDeckForRenderPass();
	void LoadSettingsCardOrderModel();
	bool SaveSettingsCardOrderModel();
	uint64_t UiAnimNodeKey(const char *pScope, uint64_t Id = 0) const;
	void TriggerUiSwitchAnimation(uint64_t NodeKey, float DurationSec = 0.18f);
	float ReadUiSwitchAnimation(uint64_t NodeKey) const;
	float UiSwitchAnimationAlpha(float Strength) const;
	void DrawUiSwitchTransitionOverlay(const CUIRect &Rect, ColorRGBA Color);
	float ApplyUiSwitchOffset(CUIRect &View, float Strength, float Direction, bool Vertical, float RelativeOffset, float MinOffset, float MaxOffset) const;
	float ResolveMenuTabAnimationValue(const void *pButtonId, bool Active, float DurationSec = 0.10f) const;
	void InitSettingsTabLabelCache();
	void UpdateSettingsTabLabels();
	void PrepareSettingsTabLabelCache(float MainViewWidth, float TabBarWidth = -1.0f);
	void PrepareLanguagePageCache(float MainViewWidth, bool ForceComplete);
	void SplitSettingsScrollbarRects(const CUIRect &Rect, unsigned Flags, CUIRect *pLabelRect, CUIRect *pValueRect, CUIRect *pScrollBarRect) const;
	int DoButton_CheckBox_Common_WithLabelElement(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, unsigned Flags, CUIElement *pLabelElement, bool ProcessInput = true, float LabelFontSize = -1.0f);
	int DoSettingsButton_CheckBox(int Page, int Tab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect);
	int DoSettingsButton_CheckBoxAutoVMarginAndSet(int Page, int Tab, const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float RowHeight, float RowSpacing, float BodySize);

	CUi::SColorPickerPopupContext m_ColorPickerPopupContext;
	struct SSettingsAlphaColorPickerState
	{
		unsigned int m_PackedColor = 0;
		bool m_Initialized = false;
	};
	std::unordered_map<const unsigned int *, SSettingsAlphaColorPickerState> m_SettingsAlphaColorPickerStates;
	ColorHSLA DoLine_ColorPicker(CButtonContainer *pResetId, const SSettingsContentMetrics &Metrics, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, ColorRGBA DefaultColor, bool CheckBoxSpacing = true, int *pCheckBoxValue = nullptr, bool Alpha = false, bool TrailingSpacing = true);
	bool DoLine_AlphaColorPicker(CButtonContainer *pResetId, const SSettingsContentMetrics &Metrics, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, int *pOpacity, unsigned int DefaultColor, int DefaultOpacity);
	ColorHSLA DoLine_ColorPicker(CButtonContainer *pResetId, float LineSize, float LabelSize, float BottomMargin, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, ColorRGBA DefaultColor, bool CheckBoxSpacing = true, int *pCheckBoxValue = nullptr, bool Alpha = false);
	ColorHSLA DoButton_ColorPicker(const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha);
	bool DoMessageGradientLine(CChat &Chat, CUIRect *pView, int Tab, const char *pLabelTextId, const char *pLabel, unsigned *pBaseColor, char *pGradient, int GradientSize, ColorRGBA DefaultColor, CButtonContainer *pResetButton, CButtonContainer *pAddButton, CButtonContainer *pRemoveButton, unsigned *pColorValues, bool CheckBoxSpacing = true, int *pCheckBoxValue = nullptr, float LineHeight = ui_token::settings::ROW_HEIGHT, float LineSpacing = ui_token::settings::ROW_GAP, float BodySize = ui_token::font::BODY, float ButtonHeight = -1.0f);

	void DoLaserPreview(const CUIRect *pRect, ColorHSLA OutlineColor, ColorHSLA InnerColor, int LaserType);
	int DoButton_GridHeader(const void *pId, const char *pText, int Checked, const CUIRect *pRect, int Align = TEXTALIGN_ML);
	int DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect);

	bool m_SkinListScrollToSelected = false;
	std::optional<std::chrono::nanoseconds> m_SkinList7LastRefreshTime;
	std::optional<std::chrono::nanoseconds> m_SkinPartsList7LastRefreshTime;
	std::unordered_map<const void *, std::unique_ptr<ui_widget::SNumericFieldState>> m_vpSettingsNumericFieldStates;
	SUiTheme m_SettingsUiTheme;

	int m_DirectionQuadContainerIndex;
	int m_QmCardBgQuadContainerIndex = -1; // 栖梦侧栏卡片背景合批容器（DrawCall 合批：每帧 Reset+AddQuaps+Upload+RenderQuadContainer）

	// menus_settings_assets.cpp
public:
	// Async asset loading states
	enum EAssetLoadState
	{
		ASSET_LOAD_STATE_UNLOADED = 0,
		ASSET_LOAD_STATE_LOADING,
		ASSET_LOAD_STATE_MERGING,
		ASSET_LOAD_STATE_LOADED,
	};

	struct SSettingsAssetMergeEntry
	{
		char m_aName[IO_MAX_PATH_LENGTH];
		bool m_IsDir = false;
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
	};

	struct SSettingsAssetPendingMerge
	{
		int m_Tab = -1;
		int m_Generation = 0;
		size_t m_Cursor = 0;
		bool m_ListReady = false;
		std::vector<SSettingsAssetMergeEntry> m_vEntries;
	};

private:
	EAssetLoadState m_aAssetLoadStates[NUMBER_OF_ASSETS_TABS] = {
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
	};
	std::shared_ptr<IJob> m_apAssetLoadJobs[NUMBER_OF_ASSETS_TABS];
	SSettingsAssetPendingMerge m_aAssetPendingMerges[NUMBER_OF_ASSETS_TABS];
	int m_aAssetLoadGenerations[NUMBER_OF_ASSETS_TABS] = {};
	void PublishSettingsAssetMergeEntries(int Tab, const std::vector<SSettingsAssetMergeEntry> &vEntries);
	void InvalidateSettingsAssetResourcePlan();
	int CurrentSettingsAssetsTab() const;

public:
	struct SCustomItem
	{
		enum EPreviewState
		{
			PREVIEW_STATE_UNLOADED = 0,
			PREVIEW_STATE_LOADING,
			PREVIEW_STATE_READY,
			PREVIEW_STATE_LOADED,
			PREVIEW_STATE_FAILED,
		};

		IGraphics::CTextureHandle m_RenderTexture;

		char m_aName[IO_MAX_PATH_LENGTH];
		char m_aDisplayName[IO_MAX_PATH_LENGTH] = "";
		char m_aAuthor[128] = "";
		std::shared_ptr<IJob> m_pDecodeJob;
		EPreviewState m_PreviewState = PREVIEW_STATE_UNLOADED;
		CImageInfo m_PreviewImage;
		unsigned m_PreviewEpoch = 0;
		size_t m_PreviewListIndex = 0;
		size_t m_PreviewBytes = 0;
		int m_PreviewRequestedTextureSize = 0;
		size_t m_PreviewResidentBytes = 0;
		bool m_PreviewResized = false;
		bool m_PreviewHighPriority = false;

		SCustomItem() = default;

		SCustomItem(const SCustomItem &Other) :
			m_RenderTexture(Other.m_RenderTexture),
			m_pDecodeJob(Other.m_pDecodeJob),
			m_PreviewState(Other.m_PreviewState),
			m_PreviewEpoch(Other.m_PreviewEpoch),
			m_PreviewListIndex(Other.m_PreviewListIndex),
			m_PreviewBytes(Other.m_PreviewBytes),
			m_PreviewRequestedTextureSize(Other.m_PreviewRequestedTextureSize),
			m_PreviewResidentBytes(Other.m_PreviewResidentBytes),
			m_PreviewResized(Other.m_PreviewResized),
			m_PreviewHighPriority(Other.m_PreviewHighPriority)
		{
			str_copy(m_aName, Other.m_aName);
			str_copy(m_aDisplayName, Other.m_aDisplayName);
			str_copy(m_aAuthor, Other.m_aAuthor);
			if(Other.m_PreviewImage.m_pData != nullptr)
				m_PreviewImage = Other.m_PreviewImage.DeepCopy();
		}

		SCustomItem &operator=(const SCustomItem &Other)
		{
			if(this == &Other)
				return *this;

			m_RenderTexture = Other.m_RenderTexture;
			str_copy(m_aName, Other.m_aName);
			str_copy(m_aDisplayName, Other.m_aDisplayName);
			str_copy(m_aAuthor, Other.m_aAuthor);
			m_pDecodeJob = Other.m_pDecodeJob;
			m_PreviewState = Other.m_PreviewState;
			m_PreviewImage.Free();
			if(Other.m_PreviewImage.m_pData != nullptr)
				m_PreviewImage = Other.m_PreviewImage.DeepCopy();
			m_PreviewEpoch = Other.m_PreviewEpoch;
			m_PreviewListIndex = Other.m_PreviewListIndex;
			m_PreviewBytes = Other.m_PreviewBytes;
			m_PreviewRequestedTextureSize = Other.m_PreviewRequestedTextureSize;
			m_PreviewResidentBytes = Other.m_PreviewResidentBytes;
			m_PreviewResized = Other.m_PreviewResized;
			m_PreviewHighPriority = Other.m_PreviewHighPriority;
			return *this;
		}

		SCustomItem(SCustomItem &&Other) = default;
		SCustomItem &operator=(SCustomItem &&Other) = default;

		~SCustomItem()
		{
			m_PreviewImage.Free();
		}

		bool operator<(const SCustomItem &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
	};

	struct SCustomEntities : public SCustomItem
	{
		struct SEntitiesImage
		{
			IGraphics::CTextureHandle m_Texture;
		};
		SEntitiesImage m_aImages[MAP_IMAGE_MOD_TYPE_COUNT];
	};

	struct SCustomGame : public SCustomItem
	{
	};

	struct SCustomEmoticon : public SCustomItem
	{
	};

	struct SCustomParticle : public SCustomItem
	{
	};

	struct SCustomHud : public SCustomItem
	{
	};

	struct SCustomExtras : public SCustomItem
	{
	};

	struct SCustomGuiCursor : public SCustomItem
	{
	};

	struct SCustomArrow : public SCustomItem
	{
	};

	struct SCustomStrongWeak : public SCustomItem
	{
	};

	struct SCustomEntityBg : public SCustomItem
	{
		bool m_IsDirectory = false;
	};

	enum
	{
		ASSETS_EDITOR_TYPE_GAME = 0,
		ASSETS_EDITOR_TYPE_EMOTICONS,
		ASSETS_EDITOR_TYPE_ENTITIES,
		ASSETS_EDITOR_TYPE_SKIN,
		ASSETS_EDITOR_TYPE_HUD,
		ASSETS_EDITOR_TYPE_PARTICLES,
		ASSETS_EDITOR_TYPE_GUI_CURSOR,
		ASSETS_EDITOR_TYPE_ARROW,
		ASSETS_EDITOR_TYPE_STRONG_WEAK,
		ASSETS_EDITOR_TYPE_EXTRAS,
		ASSETS_EDITOR_TYPE_COUNT,
	};

	struct SAssetsEditorAssetEntry
	{
		IGraphics::CTextureHandle m_PreviewTexture;
		int m_PreviewWidth = 0;
		int m_PreviewHeight = 0;
		char m_aName[64] = {0};
		char m_aPath[IO_MAX_PATH_LENGTH] = {0};
		bool m_IsDefault = false;
		bool m_PreviewLoaded = false;
	};

	struct SAssetsEditorPartSlot
	{
		int m_SpriteId = -1;
		int m_SourceSpriteId = -1;
		int m_Group = 0;
		int m_DstX = 0;
		int m_DstY = 0;
		int m_DstW = 0;
		int m_DstH = 0;
		int m_SrcX = 0;
		int m_SrcY = 0;
		int m_SrcW = 0;
		int m_SrcH = 0;
		unsigned int m_Color = color_cast<ColorHSLA>(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)).Pack(true);
		char m_aFamilyKey[64] = {0};
		char m_aSourceAsset[64] = {0};
	};

	struct SAudioPackSlot
	{
		const char *m_pDisplayName = nullptr;
		const char *m_pSetName = nullptr;
		const char *m_pRelativePath = nullptr;
		int m_SetId = -1;
		int m_VariantIndex = 0;
		int m_VariantCount = 0;
	};

	struct SAudioPackCandidateEntry
	{
		std::string m_Path;
		std::string m_DisplayName;
		bool m_IsCurrentFile = false;
		bool m_IsCurrentPackFile = false;
	};

	enum
	{
		ASSETS_EDITOR_COLOR_BLEND_MULTIPLY = 0,
		ASSETS_EDITOR_COLOR_BLEND_NORMAL,
		ASSETS_EDITOR_COLOR_BLEND_SCREEN,
		ASSETS_EDITOR_COLOR_BLEND_OVERLAY,
		ASSETS_EDITOR_COLOR_BLEND_COUNT,
	};

	static void GetStrongWeakEditorGridSize(int &OutGridX, int &OutGridY)
	{
		OutGridX = 3;
		OutGridY = 1;
	}

	static std::vector<SAssetsEditorPartSlot> BuildStrongWeakEditorSlots(const char *pMainAssetName)
	{
		static const char *s_apFamilyKeys[] = {
			"strong_weak:top",
			"strong_weak:middle",
			"strong_weak:bottom",
		};

		std::vector<SAssetsEditorPartSlot> vSlots;
		int GridX = 0;
		int GridY = 0;
		GetStrongWeakEditorGridSize(GridX, GridY);
		vSlots.reserve(GridX);

		const char *pResolvedMainAssetName = pMainAssetName != nullptr ? pMainAssetName : "";
		for(int Index = 0; Index < GridX; ++Index)
		{
			SAssetsEditorPartSlot Slot;
			Slot.m_SpriteId = -1;
			Slot.m_SourceSpriteId = -1;
			Slot.m_Group = 0;
			Slot.m_DstX = Index;
			Slot.m_DstY = 0;
			Slot.m_DstW = 1;
			Slot.m_DstH = GridY;
			Slot.m_SrcX = Index;
			Slot.m_SrcY = 0;
			Slot.m_SrcW = 1;
			Slot.m_SrcH = GridY;
			str_copy(Slot.m_aFamilyKey, s_apFamilyKeys[Index], sizeof(Slot.m_aFamilyKey));
			str_copy(Slot.m_aSourceAsset, pResolvedMainAssetName, sizeof(Slot.m_aSourceAsset));
			vSlots.push_back(Slot);
		}

		return vSlots;
	}

	static std::vector<SAssetsEditorPartSlot> BuildSkinEditorSlots(const char *pMainAssetName)
	{
		struct SSkinSlotDef
		{
			const char *m_pFamilyKey;
			int m_X;
			int m_Y;
			int m_W;
			int m_H;
		};

		static const SSkinSlotDef s_aSlotDefs[] = {
			{"skin:body", 0, 0, 96, 96},
			{"skin:feet", 96, 0, 96, 96},
			{"skin:right_strip_0", 192, 0, 32, 32},
			{"skin:right_strip_1", 224, 0, 32, 32},
			{"skin:right_strip_2", 192, 32, 64, 32},
			{"skin:right_strip_3", 192, 64, 64, 32},
			{"skin:bottom_strip_0", 0, 96, 32, 32},
			{"skin:bottom_strip_1", 32, 96, 32, 32},
			{"skin:bottom_strip_2", 64, 96, 32, 32},
			{"skin:bottom_strip_3", 96, 96, 32, 32},
			{"skin:bottom_strip_4", 128, 96, 32, 32},
			{"skin:bottom_strip_5", 160, 96, 32, 32},
			{"skin:bottom_strip_6", 192, 96, 32, 32},
			{"skin:bottom_strip_7", 224, 96, 32, 32},
		};

		std::vector<SAssetsEditorPartSlot> vSlots;
		vSlots.reserve(sizeof(s_aSlotDefs) / sizeof(s_aSlotDefs[0]));

		const char *pResolvedMainAssetName = pMainAssetName != nullptr ? pMainAssetName : "";
		for(const auto &SlotDef : s_aSlotDefs)
		{
			SAssetsEditorPartSlot Slot;
			Slot.m_SpriteId = -1;
			Slot.m_SourceSpriteId = -1;
			Slot.m_Group = 0;
			Slot.m_DstX = SlotDef.m_X;
			Slot.m_DstY = SlotDef.m_Y;
			Slot.m_DstW = SlotDef.m_W;
			Slot.m_DstH = SlotDef.m_H;
			Slot.m_SrcX = SlotDef.m_X;
			Slot.m_SrcY = SlotDef.m_Y;
			Slot.m_SrcW = SlotDef.m_W;
			Slot.m_SrcH = SlotDef.m_H;
			str_copy(Slot.m_aFamilyKey, SlotDef.m_pFamilyKey, sizeof(Slot.m_aFamilyKey));
			str_copy(Slot.m_aSourceAsset, pResolvedMainAssetName, sizeof(Slot.m_aSourceAsset));
			vSlots.push_back(Slot);
		}

		return vSlots;
	}

	static std::vector<SAudioPackSlot> BuildAudioPackSlots()
	{
		std::vector<SAudioPackSlot> vSlots;
		vSlots.reserve(g_pData->m_NumSounds);

		for(int SetIndex = 0; SetIndex < g_pData->m_NumSounds; ++SetIndex)
		{
			const CDataSoundset &Set = g_pData->m_aSounds[SetIndex];
			for(int SoundIndex = 0; SoundIndex < Set.m_NumSounds; ++SoundIndex)
			{
				const char *pFilename = Set.m_aSounds[SoundIndex].m_pFilename;
				const char *pRelativePath = pFilename;
				if(str_startswith(pRelativePath, "audio/"))
					pRelativePath += str_length("audio/");

				vSlots.push_back({
					pRelativePath,
					Set.m_pName,
					pRelativePath,
					SetIndex,
					SoundIndex,
					Set.m_NumSounds,
				});
			}
		}

		return vSlots;
	}

	static std::string BuildAudioPackExportPath(const char *pPackName, const char *pRelativePath)
	{
		const char *pResolvedPackName = pPackName != nullptr && pPackName[0] != '\0' ? pPackName : "default";
		const char *pResolvedRelativePath = pRelativePath != nullptr ? pRelativePath : "";
		return std::string("audio/") + pResolvedPackName + "/" + pResolvedRelativePath;
	}

	static std::string BuildAudioPackBuiltinCandidatePath(const char *pRelativePath)
	{
		const char *pResolvedRelativePath = pRelativePath != nullptr ? pRelativePath : "";
		if(str_startswith(pResolvedRelativePath, "audio/"))
			pResolvedRelativePath += str_length("audio/");
		return std::string("audio/") + pResolvedRelativePath;
	}

	struct SAudioPackCandidateScanRoot
	{
		const char *m_pScanRoot;
		const char *m_pOutputPrefix;
	};

	static constexpr std::array<SAudioPackCandidateScanRoot, 2> BuildAudioPackCandidateScanRoots()
	{
		return {{
			{"audio", "audio"},
			{"data/audio", "audio"},
		}};
	}

	static bool TryBuildAudioPackCandidatePathFromScan(const char *pOutputPrefix, const char *pRelativePath, std::string &OutPath)
	{
		OutPath.clear();

		const char *pResolvedRelativePath = pRelativePath != nullptr ? pRelativePath : "";
		while(*pResolvedRelativePath == '/')
			++pResolvedRelativePath;

		if(pResolvedRelativePath[0] == '\0' || !str_endswith(pResolvedRelativePath, ".wv"))
			return false;

		const char *pResolvedOutputPrefix = pOutputPrefix != nullptr && pOutputPrefix[0] != '\0' ? pOutputPrefix : "audio";
		OutPath = pResolvedOutputPrefix;
		OutPath += "/";
		OutPath += pResolvedRelativePath;
		return true;
	}

	static std::vector<SAudioPackCandidateEntry> BuildAudioPackCandidateEntries(const std::vector<std::string> &vPaths, const char *pPackName, const char *pCurrentPath)
	{
		std::vector<SAudioPackCandidateEntry> vEntries;
		vEntries.reserve(vPaths.size());

		std::unordered_set<std::string> vSeenPaths;
		const char *pResolvedPackName = pPackName != nullptr && pPackName[0] != '\0' ? pPackName : "default";
		const char *pResolvedCurrentPath = pCurrentPath != nullptr ? pCurrentPath : "";

		char aCurrentPackPrefix[IO_MAX_PATH_LENGTH];
		str_format(aCurrentPackPrefix, sizeof(aCurrentPackPrefix), "audio/%s/", pResolvedPackName);

		for(const std::string &Path : vPaths)
		{
			if(!str_endswith(Path.c_str(), ".wv"))
				continue;
			if(!vSeenPaths.insert(Path).second)
				continue;

			SAudioPackCandidateEntry Entry;
			Entry.m_Path = Path;

			const char *pDisplayName = Path.c_str();
			if(str_startswith(pDisplayName, "audio/"))
				pDisplayName += str_length("audio/");
			Entry.m_DisplayName = pDisplayName;
			Entry.m_IsCurrentFile = pResolvedCurrentPath[0] != '\0' && str_comp(Path.c_str(), pResolvedCurrentPath) == 0;
			Entry.m_IsCurrentPackFile = str_startswith(Path.c_str(), aCurrentPackPrefix) != nullptr;
			vEntries.push_back(std::move(Entry));
		}

		std::sort(vEntries.begin(), vEntries.end(), [](const SAudioPackCandidateEntry &Left, const SAudioPackCandidateEntry &Right) {
			if(Left.m_IsCurrentFile != Right.m_IsCurrentFile)
				return Left.m_IsCurrentFile > Right.m_IsCurrentFile;
			if(Left.m_IsCurrentPackFile != Right.m_IsCurrentPackFile)
				return Left.m_IsCurrentPackFile > Right.m_IsCurrentPackFile;
			return str_comp_nocase(Left.m_DisplayName.c_str(), Right.m_DisplayName.c_str()) < 0;
		});

		return vEntries;
	}

	static int FindAudioPackCandidateEntryIndex(const std::vector<SAudioPackCandidateEntry> &vEntries, const char *pPath)
	{
		if(pPath == nullptr || pPath[0] == '\0')
			return -1;

		for(int Index = 0; Index < (int)vEntries.size(); ++Index)
		{
			if(str_comp(vEntries[Index].m_Path.c_str(), pPath) == 0)
				return Index;
		}
		return -1;
	}

	static std::string ResolveAudioPackPreviewPath(const char *pSelectedCandidatePath, const char *pManualSourcePath)
	{
		if(pSelectedCandidatePath != nullptr && pSelectedCandidatePath[0] != '\0')
			return pSelectedCandidatePath;
		if(pManualSourcePath != nullptr && pManualSourcePath[0] != '\0')
			return pManualSourcePath;
		return {};
	}

	static std::string ResolveAudioPackExportSourcePath(const char *pSelectedCandidatePath, const char *pManualSourcePath)
	{
		if(pManualSourcePath != nullptr && pManualSourcePath[0] != '\0')
			return pManualSourcePath;
		if(pSelectedCandidatePath != nullptr && pSelectedCandidatePath[0] != '\0')
			return pSelectedCandidatePath;
		return {};
	}

	static int ClampAssetsEditorColorBlendMode(int BlendMode)
	{
		if(BlendMode < 0 || BlendMode >= ASSETS_EDITOR_COLOR_BLEND_COUNT)
			return ASSETS_EDITOR_COLOR_BLEND_MULTIPLY;
		return BlendMode;
	}

	static const char *AssetsEditorColorBlendModeName(int BlendMode)
	{
		switch(ClampAssetsEditorColorBlendMode(BlendMode))
		{
		case ASSETS_EDITOR_COLOR_BLEND_NORMAL: return "Normal";
		case ASSETS_EDITOR_COLOR_BLEND_SCREEN: return "Screen";
		case ASSETS_EDITOR_COLOR_BLEND_OVERLAY: return "Overlay";
		default: return "Multiply";
		}
	}

	static ColorRGBA AssetsEditorSlotColorToRgba(unsigned int PackedColor)
	{
		return color_cast<ColorRGBA>(ColorHSLA(PackedColor, true));
	}

	static float AssetsEditorClampColorChannel(float Value)
	{
		return minimum(maximum(Value, 0.0f), 1.0f);
	}

	static float AssetsEditorColorLuma(const ColorRGBA &Base)
	{
		return AssetsEditorClampColorChannel(Base.r * 0.299f + Base.g * 0.587f + Base.b * 0.114f);
	}

	static float AssetsEditorScreenTone(float Luma)
	{
		return AssetsEditorClampColorChannel(Luma * (1.0f + (1.0f - Luma) * 0.65f));
	}

	static float AssetsEditorOverlayTone(float Luma)
	{
		if(Luma <= 0.5f)
			return AssetsEditorClampColorChannel(2.0f * Luma * Luma);
		return AssetsEditorClampColorChannel(1.0f - 2.0f * (1.0f - Luma) * (1.0f - Luma));
	}

	static ColorRGBA AssetsEditorRecolorColor(const ColorRGBA &Base, const ColorRGBA &Tint, float Tone, float DetailPreserve)
	{
		const float BaseLuma = AssetsEditorColorLuma(Base);
		return ColorRGBA(
			AssetsEditorClampColorChannel(Tint.r * Tone + (Base.r - BaseLuma) * DetailPreserve),
			AssetsEditorClampColorChannel(Tint.g * Tone + (Base.g - BaseLuma) * DetailPreserve),
			AssetsEditorClampColorChannel(Tint.b * Tone + (Base.b - BaseLuma) * DetailPreserve),
			Base.a);
	}

	static ColorRGBA AssetsEditorMultiplyColor(const ColorRGBA &Base, const ColorRGBA &Tint)
	{
		return ColorRGBA(Base.r * Tint.r, Base.g * Tint.g, Base.b * Tint.b, Base.a * Tint.a);
	}

	static ColorRGBA AssetsEditorBlendColor(const ColorRGBA &Base, const ColorRGBA &Tint, int BlendMode)
	{
		const int ClampedBlendMode = ClampAssetsEditorColorBlendMode(BlendMode);
		const float BlendStrength = minimum(maximum(Tint.a, 0.0f), 1.0f);
		ColorRGBA Blended = Base;
		const float BaseLuma = AssetsEditorColorLuma(Base);
		switch(ClampedBlendMode)
		{
		case ASSETS_EDITOR_COLOR_BLEND_NORMAL:
			Blended = AssetsEditorRecolorColor(Base, Tint, BaseLuma, 0.18f);
			break;
		case ASSETS_EDITOR_COLOR_BLEND_SCREEN:
			Blended = AssetsEditorRecolorColor(Base, Tint, AssetsEditorScreenTone(BaseLuma), 0.10f);
			break;
		case ASSETS_EDITOR_COLOR_BLEND_OVERLAY:
			Blended = AssetsEditorRecolorColor(Base, Tint, AssetsEditorOverlayTone(BaseLuma), 0.28f);
			break;
		default:
			Blended = ColorRGBA(Base.r * Tint.r, Base.g * Tint.g, Base.b * Tint.b, Base.a);
			break;
		}

		return ColorRGBA(
			Base.r + (Blended.r - Base.r) * BlendStrength,
			Base.g + (Blended.g - Base.g) * BlendStrength,
			Base.b + (Blended.b - Base.b) * BlendStrength,
			Base.a);
	}

	static bool AssetsEditorHasColorOverride(const ColorRGBA &Tint)
	{
		constexpr float Epsilon = 0.001f;
		return absolute(Tint.r - 1.0f) > Epsilon ||
		       absolute(Tint.g - 1.0f) > Epsilon ||
		       absolute(Tint.b - 1.0f) > Epsilon ||
		       absolute(Tint.a - 1.0f) > Epsilon;
	}

	static bool AssetsEditorSlotNeedsProcessing(const SAssetsEditorPartSlot &Slot, const char *pMainAssetName)
	{
		const char *pResolvedMainAssetName = pMainAssetName != nullptr ? pMainAssetName : "";
		const bool UsesMainSourceRect = str_comp(Slot.m_aSourceAsset, pResolvedMainAssetName) == 0 &&
						Slot.m_SrcX == Slot.m_DstX && Slot.m_SrcY == Slot.m_DstY &&
						Slot.m_SrcW == Slot.m_DstW && Slot.m_SrcH == Slot.m_DstH;
		return !UsesMainSourceRect || AssetsEditorHasColorOverride(AssetsEditorSlotColorToRgba(Slot.m_Color));
	}

	static void AssetsEditorApplyColorOverrideToImageRect(CImageInfo &Image, int X, int Y, int W, int H, const ColorRGBA &Tint, int BlendMode)
	{
		if(Image.m_pData == nullptr || Image.m_Format != CImageInfo::FORMAT_RGBA || W <= 0 || H <= 0)
			return;
		if(!AssetsEditorHasColorOverride(Tint))
			return;

		const int ImageWidth = Image.m_Width;
		const int ImageHeight = Image.m_Height;
		if(X < 0 || Y < 0 || X + W > ImageWidth || Y + H > ImageHeight)
			return;

		uint8_t *pData = Image.m_pData;
		for(int PosY = Y; PosY < Y + H; ++PosY)
		{
			for(int PosX = X; PosX < X + W; ++PosX)
			{
				const int Offset = (PosY * ImageWidth + PosX) * 4;
				if(pData[Offset + 3] == 0)
					continue;

				const ColorRGBA Base(
					pData[Offset + 0] / 255.0f,
					pData[Offset + 1] / 255.0f,
					pData[Offset + 2] / 255.0f,
					pData[Offset + 3] / 255.0f);
				const ColorRGBA Result = AssetsEditorBlendColor(Base, Tint, BlendMode);
				pData[Offset + 0] = round_truncate(Result.r * 255.0f);
				pData[Offset + 1] = round_truncate(Result.g * 255.0f);
				pData[Offset + 2] = round_truncate(Result.b * 255.0f);
				pData[Offset + 3] = round_truncate(Result.a * 255.0f);
			}
		}
	}

	static void SplitFriendsCategoryHeaderRects(const CUIRect &Header, CUIRect *pHeaderAction, CUIRect *pManageButton)
	{
		CUIRect HeaderAction = Header;
		CUIRect ManageButton = Header;
		HeaderAction.w = maximum(HeaderAction.w - HeaderAction.h, 0.0f);
		ManageButton.x = HeaderAction.x + HeaderAction.w;
		ManageButton.w = Header.w - HeaderAction.w;
		ManageButton.x += 2.0f;
		ManageButton.y += 2.0f;
		ManageButton.w = maximum(ManageButton.w - 4.0f, 0.0f);
		ManageButton.h = maximum(ManageButton.h - 4.0f, 0.0f);
		if(pHeaderAction != nullptr)
			*pHeaderAction = HeaderAction;
		if(pManageButton != nullptr)
			*pManageButton = ManageButton;
	}
	static float FriendsCategoryEditPopupHeight()
	{
		return 5.0f * 2.0f + 12.0f + 3.0f + 18.0f + 6.0f + 20.0f;
	}
	static float FriendsCategoryActionsPopupHeight()
	{
		return 5.0f * 2.0f + 12.0f + 3.0f + 18.0f + 3.0f + 18.0f + 3.0f + 18.0f;
	}
	static CUIRect SecondaryPanelRect(float AnchorX, float AnchorY, float PreferredWidth, float PreferredHeight, const CUIRect &Screen, float MinWidth = 220.0f, float MinHeight = 48.0f, float MaxWidth = 420.0f, float MaxHeight = 260.0f)
	{
		SSecondaryPanelSpec Spec;
		Spec.m_AnchorX = AnchorX;
		Spec.m_AnchorY = AnchorY;
		Spec.m_PreferredWidth = PreferredWidth;
		Spec.m_PreferredHeight = PreferredHeight;
		Spec.m_MinWidth = MinWidth;
		Spec.m_MinHeight = MinHeight;
		Spec.m_MaxWidth = MaxWidth;
		Spec.m_MaxHeight = MaxHeight;
		Spec.m_ScreenWidth = Screen.w;
		Spec.m_ScreenHeight = Screen.h;
		Spec.m_Margin = 8.0f;
		return SettingsSecondaryPanelRect(Spec);
	}
	static const char *ServerbrowserShortTypeDisplayName(const char *pType)
	{
		if(pType == nullptr || pType[0] == '\0')
			return nullptr;
		if(str_comp_nocase(pType, "DDmaX Easy") == 0)
			return "DDmaX.Easy 古典";
		if(str_comp_nocase(pType, "DDmaX.Easy") == 0)
			return "DDmaX.Easy 古典";
		if(str_comp_nocase(pType, "DDmaX Next") == 0)
			return "DDmaX.Next 古典";
		if(str_comp_nocase(pType, "DDmaX.Next") == 0)
			return "DDmaX.Next 古典";
		if(str_comp_nocase(pType, "DDmaX Pro") == 0)
			return "DDmaX.Pro 古典";
		if(str_comp_nocase(pType, "DDmaX.Pro") == 0)
			return "DDmaX.Pro 古典";
		if(str_comp_nocase(pType, "DDmaX Nut") == 0)
			return "DDmaX.Nut 古典";
		if(str_comp_nocase(pType, "DDmaX.Nut") == 0)
			return "DDmaX.Nut 古典";
		if(str_comp_nocase(pType, "DDmaX") == 0)
			return "DDmaX 古典";
		if(str_comp_nocase(pType, "Oldschool") == 0)
			return "古典图";
		if(str_comp_nocase(pType, "Novice") == 0)
			return "简单图";
		if(str_comp_nocase(pType, "Moderate") == 0)
			return "中阶图";
		if(str_comp_nocase(pType, "Brutal") == 0)
			return "高阶";
		if(str_comp_nocase(pType, "Insane") == 0)
			return "疯狂";
		if(str_comp_nocase(pType, "Dummy") == 0)
			return "分身";
		if(str_comp_nocase(pType, "Solo") == 0)
			return "单人";
		return pType;
	}
	struct SFriendAutoFollowState
	{
		bool m_Active = false;
		char m_aName[MAX_NAME_LENGTH] = {0};
		char m_aClan[MAX_CLAN_LENGTH] = {0};
		char m_aLastAddress[NETADDR_MAXSTRSIZE] = {0};
		char m_aPendingAddress[NETADDR_MAXSTRSIZE] = {0};
		float m_PendingConnectTime = 0.0f;
		int m_JumpCount = 0;
		bool m_HasPendingAddress = false;
	};
	static void StartFriendAutoFollow(SFriendAutoFollowState &State, const char *pName, const char *pClan, const char *pAddress)
	{
		State = SFriendAutoFollowState();
		State.m_Active = true;
		str_copy(State.m_aName, pName);
		str_copy(State.m_aClan, pClan);
		str_copy(State.m_aLastAddress, pAddress);
	}
	static void StopFriendAutoFollow(SFriendAutoFollowState &State)
	{
		State = SFriendAutoFollowState();
	}
	static bool FriendAutoFollowStep(SFriendAutoFollowState &State, bool TargetOnline, const char *pAddress, float Now, int DelaySeconds, int MaxJumps, char *pConnectAddress, int ConnectAddressSize)
	{
		if(pConnectAddress != nullptr && ConnectAddressSize > 0)
			pConnectAddress[0] = '\0';
		if(!State.m_Active)
			return false;
		if(!TargetOnline || pAddress == nullptr || pAddress[0] == '\0')
		{
			StopFriendAutoFollow(State);
			return false;
		}
		if(State.m_aLastAddress[0] == '\0')
			str_copy(State.m_aLastAddress, pAddress);
		if(str_comp(State.m_aLastAddress, pAddress) != 0 && (!State.m_HasPendingAddress || str_comp(State.m_aPendingAddress, pAddress) != 0))
		{
			str_copy(State.m_aPendingAddress, pAddress);
			State.m_PendingConnectTime = Now + maximum(0, DelaySeconds);
			State.m_HasPendingAddress = true;
		}
		if(!State.m_HasPendingAddress || Now < State.m_PendingConnectTime)
			return false;
		if(State.m_JumpCount >= maximum(0, MaxJumps))
		{
			StopFriendAutoFollow(State);
			return false;
		}
		str_copy(State.m_aLastAddress, State.m_aPendingAddress);
		if(pConnectAddress != nullptr && ConnectAddressSize > 0)
			str_copy(pConnectAddress, State.m_aPendingAddress, ConnectAddressSize);
		State.m_HasPendingAddress = false;
		State.m_aPendingAddress[0] = '\0';
		++State.m_JumpCount;
		if(State.m_JumpCount >= maximum(0, MaxJumps))
			State.m_Active = false;
		return true;
	}
	static const char *GetServerbrowserDisplayName(const CServerInfo *pInfo, char *pBuffer, size_t BufferSize)
	{
		if(pInfo == nullptr || pBuffer == nullptr || BufferSize == 0)
			return "";

		pBuffer[0] = '\0';
		const char *pName = pInfo->m_aName;
		if(pName[0] == '\0')
			return pName;

		const auto IsAsciiWordChar = [](char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		};
		const auto IsKogSeparator = [](char c) {
			return c == ' ' || c == '|' || c == '*' || c == '-' || c == ':' || c == '[' || c == ']';
		};
		const auto DifficultyKeyFromText = [](const char *pText) -> const char * {
			if(!pText || pText[0] == '\0')
				return nullptr;
			if(str_find_nocase(pText, "DDmaX"))
			{
				if(str_find_nocase(pText, "Easy"))
					return "DDmaX Easy";
				if(str_find_nocase(pText, "Next"))
					return "DDmaX Next";
				if(str_find_nocase(pText, "Pro"))
					return "DDmaX Pro";
				if(str_find_nocase(pText, "Nut"))
					return "DDmaX Nut";
				return "DDmaX";
			}
			if(str_find_nocase(pText, "Oldschool") || str_find(pText, "古典") || str_find(pText, "传统"))
				return "Oldschool";
			if(str_find_nocase(pText, "Novice") || str_find(pText, "普通") || str_find(pText, "简单"))
				return "Novice";
			if(str_find_nocase(pText, "Moderate") || str_find(pText, "中阶"))
				return "Moderate";
			if(str_find_nocase(pText, "Brutal") || str_find(pText, "高阶") || str_find(pText, "困难"))
				return "Brutal";
			if(str_find_nocase(pText, "Insane") || str_find(pText, "疯狂"))
				return "Insane";
			if(str_find_nocase(pText, "Dummy") || str_find(pText, "分身"))
				return "Dummy";
			if(str_find_nocase(pText, "Solo") || str_find(pText, "单人"))
				return "Solo";
			if(str_find(pText, "活动"))
				return "活动";
			if(str_find(pText, "极限"))
				return "极限";
			if(str_find(pText, "训练"))
				return "训练";
			if(str_find(pText, "娱乐"))
				return "娱乐";
			return nullptr;
		};

		if(str_find_nocase(pInfo->m_aGameType, "gores") && str_find_nocase(pName, "kog"))
		{
			const char *pShortName = pName;
			const char *pScan = pName;
			while(const char *pMatch = str_find_nocase(pScan, "kog"))
			{
				const char Prev = pMatch > pName ? pMatch[-1] : '\0';
				const char Next = pMatch[3];
				if(!IsAsciiWordChar(Prev) && !IsAsciiWordChar(Next))
				{
					pShortName = pMatch + 3;
					while(*pShortName != '\0' && IsKogSeparator(*pShortName))
						++pShortName;
					break;
				}
				pScan = pMatch + 1;
			}

			str_copy(pBuffer, str_skip_whitespaces_const(pShortName), BufferSize);
			if(const char *pSuffix = str_endswith_nocase(pBuffer, "[kog.tw]"))
			{
				char *pSuffixStart = const_cast<char *>(pSuffix);
				while(pSuffixStart > pBuffer && pSuffixStart[-1] == ' ')
					--pSuffixStart;
				*pSuffixStart = '\0';
			}

			char *pHashToken = const_cast<char *>(str_find(pBuffer, " #"));
			if(pHashToken != nullptr)
			{
				char *pDigits = pHashToken + 2;
				if('0' <= *pDigits && *pDigits <= '9')
				{
					while('0' <= *pDigits && *pDigits <= '9')
						++pDigits;
					if(str_startswith(pDigits, " - "))
					{
						const char *pMapName = str_skip_whitespaces_const(pDigits + 3);
						char *pRegionEnd = pHashToken;
						while(pRegionEnd > pBuffer && pRegionEnd[-1] == ' ')
							--pRegionEnd;
						*pRegionEnd = '\0';
						if(pBuffer[0] != '\0' && pMapName[0] != '\0')
						{
							char aRegion[64];
							char aMapName[64];
							str_copy(aRegion, pBuffer, sizeof(aRegion));
							str_copy(aMapName, pMapName, sizeof(aMapName));
							str_format(pBuffer, BufferSize, "%s - %s", aRegion, aMapName);
						}
						else if(pMapName[0] != '\0')
							str_copy(pBuffer, pMapName, BufferSize);
					}
				}
			}
			return pBuffer[0] != '\0' ? pBuffer : pName;
		}

		const char *pDifficulty = DifficultyKeyFromText(pName);
		if(pDifficulty == nullptr)
			return pName;

		if(str_find_nocase(pName, "Axiom"))
		{
			const char *pDash = str_find(pName, " - ");
			const char *pMapName = pDash != nullptr ? str_skip_whitespaces_const(pDash + 3) : nullptr;
			if(pMapName != nullptr && pMapName[0] != '\0')
			{
				str_format(pBuffer, BufferSize, "%s - %s", ServerbrowserShortTypeDisplayName(pDifficulty), pMapName);
				return pBuffer;
			}
		}

		if(str_find_nocase(pName, "DDNet CHN") || str_find_nocase(pName, "CHN DDR") || str_find_nocase(pName, "DDNet Taiwan") || str_find_nocase(pName, "CHN"))
		{
			char aRegion[64];
			str_copy(aRegion, pName, sizeof(aRegion));
			const char *pRegion = aRegion;
			if(const char *pAfterPrefix = str_startswith_nocase(pRegion, "DDNet "))
				pRegion = str_skip_whitespaces_const(pAfterPrefix);
			else if(const char *pAfterChnPrefix = str_startswith_nocase(pRegion, "CHN DDR "))
				pRegion = str_skip_whitespaces_const(pAfterChnPrefix);
			const char *pDashText = str_find(aRegion, " - ");
			if(char *pDash = const_cast<char *>(pDashText))
				*pDash = '\0';
			if(char *pDifficultyStart = const_cast<char *>(str_find_nocase(pRegion, pDifficulty)))
			{
				while(pDifficultyStart > pRegion && pDifficultyStart[-1] == ' ')
					--pDifficultyStart;
				*pDifficultyStart = '\0';
			}
			if(pDifficulty != nullptr && str_startswith_nocase(pDifficulty, "DDmaX") && pDashText != nullptr)
			{
				const char *pSuffix = str_skip_whitespaces_const(pDashText + 3);
				if(pSuffix[0] != '\0')
				{
					char aSuffix[64];
					str_copy(aSuffix, pSuffix, sizeof(aSuffix));
					if(char *pClassic = const_cast<char *>(str_find(aSuffix, "古典")))
					{
						while(pClassic > aSuffix && pClassic[-1] == ' ')
							--pClassic;
						*pClassic = '\0';
					}
					str_format(pBuffer, BufferSize, "%s - %s", ServerbrowserShortTypeDisplayName(aSuffix[0] != '\0' ? aSuffix : pDifficulty), pRegion);
					return pBuffer;
				}
			}
			if(pRegion[0] != '\0')
			{
				str_format(pBuffer, BufferSize, "%s - %s", ServerbrowserShortTypeDisplayName(pDifficulty), pRegion);
				return pBuffer;
			}
			if(pDashText != nullptr)
			{
				const char *pSuffix = str_skip_whitespaces_const(pDashText + 3);
				if(pSuffix[0] != '\0')
				{
					str_format(pBuffer, BufferSize, "%s - %s", ServerbrowserShortTypeDisplayName(pDifficulty), pSuffix);
					return pBuffer;
				}
			}
		}

		return pName;
	}

protected:
	std::vector<SCustomEntities> m_vEntitiesList;
	std::vector<SCustomGame> m_vGameList;
	std::vector<SCustomEmoticon> m_vEmoticonList;
	std::vector<SCustomParticle> m_vParticlesList;
	std::vector<SCustomHud> m_vHudList;
	std::vector<SCustomGuiCursor> m_vGuiCursorList;
	std::vector<SCustomArrow> m_vArrowList;
	std::vector<SCustomStrongWeak> m_vStrongWeakList;
	std::vector<SCustomEntityBg> m_vEntityBgList;
	std::vector<std::string> m_vEntityBgSourceNames;
	std::unordered_map<std::string, EEntityBgHierarchyEntrySource> m_vEntityBgSourceKinds;
	char m_aEntityBgCurrentFolder[IO_MAX_PATH_LENGTH] = "";
	bool m_ShowWorkshopAssets = false;
	std::vector<SCustomExtras> m_vExtrasList;
	std::deque<SSettingsAssetPreviewHandle> m_aaCustomPreviewDecodeQueue[NUMBER_OF_ASSETS_TABS];
	std::deque<SSettingsAssetPreviewHandle> m_aaCustomPreviewReadyQueue[NUMBER_OF_ASSETS_TABS];
	std::unordered_set<std::string> m_aaCustomPreviewReadyQueued[NUMBER_OF_ASSETS_TABS];
	unsigned m_aCustomPreviewEpoch[NUMBER_OF_ASSETS_TABS] = {0};

	bool m_IsInit = false;

	static void LoadEntities(struct SCustomEntities *pEntitiesItem, void *pUser);
	static int EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser);

	static int GameScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int HudScan(const char *pName, int IsDir, int DirType, void *pUser);
	static int ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser);

	static void ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void ClearCustomItems(int CurTab);
	void RefreshEntityBgHierarchyView();
	void SyncEntityBgInstalledWorkshopSources();
	void AssetsEditorOpen(int Type);

private:
	struct SAudioPackEditorState
	{
		bool m_Open = false;
		bool m_Initialized = false;
		int m_SelectedSlotIndex = 0;
		int m_SelectedCandidateIndex = -1;
		CLineInputBuffered<64> m_FilterInput;
		CLineInputBuffered<64> m_CandidateFilterInput;
		CLineInputBuffered<64> m_PackNameInput;
		CLineInputBuffered<IO_MAX_PATH_LENGTH> m_SourcePathInput;
		char m_aStatusMessage[256] = {0};
		bool m_StatusIsError = false;
		int m_PreviewSampleId = -1;
		std::vector<std::string> m_vCandidatePaths;
		std::vector<SAudioPackCandidateEntry> m_vCandidateEntries;
	};

	struct SAssetsEditorState
	{
		bool m_Open = false;
		bool m_Initialized = false;
		int m_Type = ASSETS_EDITOR_TYPE_GAME;
		bool m_aAssetsLoaded[ASSETS_EDITOR_TYPE_COUNT] = {false};
		int m_aMainAssetIndex[ASSETS_EDITOR_TYPE_COUNT] = {0};
		int m_aDonorAssetIndex[ASSETS_EDITOR_TYPE_COUNT] = {0};
		bool m_ShowGrid = true;
		bool m_ApplySameSize = false;
		int m_ColorBlendMode = ASSETS_EDITOR_COLOR_BLEND_MULTIPLY;
		bool m_DragActive = false;
		int m_ActiveDraggedSlotIndex = -1;
		char m_aDraggedSourceAsset[64] = {0};
		bool m_TargetPressPending = false;
		int m_PendingTargetSlotIndex = -1;
		vec2 m_PendingTargetPressPos = vec2(0.0f, 0.0f);
		int64_t m_PendingTargetPressTime = 0;
		int m_ColorPickerSlotIndex = -1;
		unsigned int m_ColorPickerValue = 0;
		unsigned int m_LastColorPickerValue = 0;
		int m_HoveredDonorSlotIndex = -1;
		int m_HoveredTargetSlotIndex = -1;
		bool m_DirtyPreview = true;
		char m_aExportName[64] = {0};
		char m_aaExportNameByType[ASSETS_EDITOR_TYPE_COUNT][64] = {};
		char m_aStatusMessage[256] = {0};
		bool m_StatusIsError = false;
		bool m_HasUnsavedChanges = false;
		bool m_ShowExitConfirm = false;
		bool m_FullscreenOpen = false;
		int m_HoverCycleSlotIndex = -1;
		int m_HoverCyclePositionX = -1;
		int m_HoverCyclePositionY = -1;
		int m_HoverCycleCandidateCursor = 0;
		std::vector<int> m_vHoverCycleCandidates;
		IGraphics::CTextureHandle m_ComposedPreviewTexture;
		int m_ComposedPreviewWidth = 0;
		int m_ComposedPreviewHeight = 0;
		std::vector<SAssetsEditorAssetEntry> m_avAssets[ASSETS_EDITOR_TYPE_COUNT];
		std::vector<SAssetsEditorPartSlot> m_vPartSlots;
	};

	SAudioPackEditorState m_AudioPackEditorState;
	SAssetsEditorState m_AssetsEditorState;
	void AudioPackEditorOpen(const char *pPackName);
	void AudioPackEditorClose();
	void AudioPackEditorSetStatus(const char *pMessage, bool IsError);
	void AudioPackEditorRefreshCandidates();
	void AudioPackEditorRebuildCandidates();
	void AudioPackEditorStopPreview();
	bool AudioPackEditorPlayPreview(const char *pFilename, int StorageType);
	bool AudioPackEditorEnsureStorageDirectories(const char *pStoragePath);
	bool AudioPackEditorCopyFileToStorage(const char *pSourcePath, int SourceStorageType, const char *pStoragePath);
	bool AudioPackEditorCopyAbsoluteFileToStorage(const char *pSourcePath, const char *pStoragePath);
	void RenderAudioPackEditorScreen(CUIRect MainView);
	void RenderAssetsEditorScreen(CUIRect MainView);
	void AssetsEditorClearAssets();
	void AssetsEditorEnsureAssetsLoadedForType(int Type);
	void AssetsEditorEnsurePreviewLoaded(SAssetsEditorAssetEntry &Asset);
	void AssetsEditorReloadAssetType(int Type);
	void AssetsEditorReloadAssets();
	void AssetsEditorReloadAssetsImagesOnly();
	void AssetsEditorResetPartSlots();
	void AssetsEditorEnsureDefaultExportNames();
	void AssetsEditorSyncExportNameFromType();
	void AssetsEditorCommitExportNameForType();
	void AssetsEditorValidateRequiredSlotsForType(int Type);
	bool AssetsEditorComposeImage(CImageInfo &OutputImage);
	bool AssetsEditorExport();
	void AssetsEditorRenderCanvas(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int W, int H, int Type, bool ShowGrid, int HighlightSlot, bool ShowTintFeedback, int PersistentHighlightSlot);
	void AssetsEditorCollectHoveredCandidates(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, std::vector<int> &vOutCandidates) const;
	int AssetsEditorResolveHoveredSlotWithCycle(const CUIRect &Rect, int Type, const std::vector<SAssetsEditorPartSlot> &vSlots, vec2 Mouse, bool ClickedLmb, int PreferredSlotIndex);
	void AssetsEditorCancelDrag();
	void AssetsEditorApplyDrop(int TargetSlotIndex, const char *pDonorName, int SourceSlotIndex, bool ApplyAllSameSize);
	void AssetsEditorUpdatePreviewIfDirty();
	void AssetsEditorRequestClose();
	void AssetsEditorCloseNow();
	void AssetsEditorRenderExitConfirm(const CUIRect &Rect);
	void AssetsEditorBuildFamilyKey(int Type, const CDataSprite *pSprite, char *pOut, int OutSize);
	bool AssetsEditorCopyRectScaledNearest(CImageInfo &Dst, const CImageInfo &Src, int DstX, int DstY, int DstW, int DstH, int SrcX, int SrcY, int SrcW, int SrcH);

protected:
	int m_MenuPage;
	int m_GamePage;
	int m_Popup;
	bool m_ShowStart;
	bool m_MenuActive;

	bool m_DummyNamePlatePreview = false;

	class CJoinTutorial
	{
	public:
		bool m_Queued = false;
		enum class EStatus
		{
			REFRESHING,
			SERVER_LIST_ERROR,
			NO_TUTORIAL_AVAILABLE,
		};
		EStatus m_Status = EStatus::REFRESHING;
		bool m_TryRefresh = false;
		bool m_TriedRefresh = false;
		enum class ELocalServerState
		{
			NOT_TRIED,
			TRY,
			WAITING_STOP,
			WAITING_START,
		};
		ELocalServerState m_LocalServerState = ELocalServerState::NOT_TRIED;
		std::chrono::nanoseconds m_StateChange = std::chrono::nanoseconds(0);
	};
	CJoinTutorial m_JoinTutorial;

	bool m_CreateDefaultFavoriteCommunities = false;
	bool m_ForceRefreshLanPage = false;
	float m_MenuPageTransitionDirection = 0.0f;
	float m_GamePageTransitionDirection = 0.0f;
	float m_BrowserTabTransitionDirection = 0.0f;
	std::chrono::nanoseconds m_LastMenuInteractionTime{0};
	void MarkMenuInteraction();

	char m_aNextServer[256];

	// images
	struct CMenuImage
	{
		char m_aName[64];
		IGraphics::CTextureHandle m_OrgTexture;
		IGraphics::CTextureHandle m_GreyTexture;
	};
	std::vector<CMenuImage> m_vMenuImages;
	static int MenuImageScan(const char *pName, int IsDir, int DirType, void *pUser);
	const CMenuImage *FindMenuImage(const char *pName);

	// loading
	class CLoadingState
	{
	public:
		std::chrono::nanoseconds m_LastRender{0};
		int m_Current;
		int m_Total;
	};
	CLoadingState m_LoadingState;

	//
	char m_aMessageTopic[512];
	char m_aMessageBody[512];
	char m_aMessageButton[512];

	CUIElement m_RefreshButton;
	CUIElement m_ConnectButton;

	// generic popups
	typedef void (CMenus::*FPopupButtonCallback)();
	void DefaultButtonCallback()
	{
		// do nothing
	}
	enum
	{
		BUTTON_CONFIRM = 0, // confirm / yes / close / ok
		BUTTON_CANCEL, // cancel / no
		NUM_BUTTONS
	};
	char m_aPopupTitle[128];
	char m_aPopupMessage[IO_MAX_PATH_LENGTH + 256];
	struct
	{
		char m_aLabel[64];
		int m_NextPopup;
		FPopupButtonCallback m_pfnCallback;
	} m_aPopupButtons[NUM_BUTTONS];

	void PopupMessage(const char *pTitle, const char *pMessage,
		const char *pButtonLabel, int NextPopup = POPUP_NONE, FPopupButtonCallback pfnButtonCallback = &CMenus::DefaultButtonCallback);
	void PopupConfirm(const char *pTitle, const char *pMessage,
		const char *pConfirmButtonLabel, const char *pCancelButtonLabel,
		FPopupButtonCallback pfnConfirmButtonCallback = &CMenus::DefaultButtonCallback, int ConfirmNextPopup = POPUP_NONE,
		FPopupButtonCallback pfnCancelButtonCallback = &CMenus::DefaultButtonCallback, int CancelNextPopup = POPUP_NONE);

	// some settings
	static float ms_ButtonHeight;
	static float ms_ListheaderHeight;
	static float ms_ListitemAdditionalHeight;

	// for settings
	bool m_NeedRestartGraphics;
	bool m_NeedRestartSound;
	bool m_NeedRestartUpdate;
	bool m_SettingsTabLabelElementsInit = false;
	bool m_SettingsTabLabelsInit = false;
	bool m_SettingsTabSixup = false;
	char m_aSettingsTabLanguageFile[IO_MAX_PATH_LENGTH] = "";
	bool m_NeedSendinfo;
	bool m_NeedSendDummyinfo;
	int m_SettingPlayerPage;

	// 0.7 skins
	bool m_CustomSkinMenu = false;
	int m_TeePartSelected = protocol7::SKINPART_BODY;
	std::string m_SelectedSkin7Name;
	CLineInputBuffered<protocol7::MAX_SKIN_ARRAY_SIZE, protocol7::MAX_SKIN_LENGTH> m_SkinNameInput;
	bool m_SkinPartListNeedsUpdate = false;
	void PopupConfirmDeleteSkin7();

	// for map download popup
	int64_t m_DownloadLastCheckTime;
	int m_DownloadLastCheckSize;
	float m_DownloadSpeed;

	// for password popup
	CLineInput m_PasswordInput;

	// for call vote
	enum class ECallvoteMapSort
	{
		ALL,
		STAR_1,
		STAR_2,
		STAR_3,
		STAR_4,
		STAR_5,
		LOW_TO_HIGH,
		HIGH_TO_LOW,
		NUM_MODES,
	};
	int m_CallvoteSelectedOption;
	int m_CallvoteSelectedPlayer;
	ECallvoteMapSort m_CallvoteMapSort = ECallvoteMapSort::ALL;
	CUi::SDropDownState m_CallvoteMapSortDropDownState;
	CLineInputBuffered<VOTE_REASON_LENGTH> m_CallvoteReasonInput;
	CLineInputBuffered<64> m_FilterInput;
	CLineInputBuffered<64> m_ExcludeInput;
	bool m_ControlPageOpening;

	// demo
	enum
	{
		SORT_DEMONAME = 0,
		SORT_MARKERS,
		SORT_LENGTH,
		SORT_DATE,
	};

	enum EDemoBrowserSource
	{
		DEMO_BROWSER_SOURCE_DEMOS = 0,
		DEMO_BROWSER_SOURCE_SCREENSHOTS,
		NUM_DEMO_BROWSER_SOURCES,
	};

	class CDemoItem
	{
	public:
		char m_aFilename[IO_MAX_PATH_LENGTH];
		char m_aName[IO_MAX_PATH_LENGTH];
		bool m_IsDir;
		bool m_IsLink;
		int m_StorageType;
		time_t m_Date;
		bool m_DateLoaded;
		bool m_DateValid;
		int64_t m_Size;
		bool m_SizeLoaded;

		bool m_InfosLoaded;
		bool m_Valid;
		CDemoHeader m_Info;
		CTimelineMarkers m_TimelineMarkers;
		CMapInfo m_MapInfo;

		bool IsDemoFile() const
		{
			return !m_IsDir && str_endswith_nocase(m_aFilename, ".demo") != nullptr;
		}

		int NumMarkers() const
		{
			return std::clamp<int>(bytes_be_to_uint(m_TimelineMarkers.m_aNumTimelineMarkers), 0, MAX_TIMELINE_MARKERS);
		}

		int Length() const
		{
			return bytes_be_to_uint(m_Info.m_aLength);
		}

		unsigned MapSize() const
		{
			return bytes_be_to_uint(m_Info.m_aMapSize);
		}

		bool operator<(const CDemoItem &Other) const
		{
			if(!str_comp(Other.m_aFilename, ".."))
				return false;
			if(!str_comp(m_aFilename, ".."))
				return true;
			if(m_IsDir && !Other.m_IsDir)
				return true;
			if(!m_IsDir && Other.m_IsDir)
				return false;

			const CDemoItem &Left = g_Config.m_BrDemoSortOrder ? Other : *this;
			const CDemoItem &Right = g_Config.m_BrDemoSortOrder ? *this : Other;

			if(g_Config.m_BrDemoSort == SORT_DEMONAME)
				return str_comp_filenames(Left.m_aFilename, Right.m_aFilename) < 0;
			if(g_Config.m_BrDemoSort == SORT_DATE)
				return Left.m_Date < Right.m_Date;
			if(!Left.IsDemoFile() || !Right.IsDemoFile())
				return str_comp_filenames(Left.m_aFilename, Right.m_aFilename) < 0;

			if(!Other.m_InfosLoaded)
				return m_InfosLoaded;
			if(!m_InfosLoaded)
				return !Other.m_InfosLoaded;

			if(g_Config.m_BrDemoSort == SORT_MARKERS)
				return Left.NumMarkers() < Right.NumMarkers();
			if(g_Config.m_BrDemoSort == SORT_LENGTH)
				return Left.Length() < Right.Length();

			// Unknown sort
			return true;
		}
	};

	struct SDemoSelectionEntry
	{
		char m_aFilename[IO_MAX_PATH_LENGTH];
		int m_StorageType;

		bool operator==(const SDemoSelectionEntry &Other) const
		{
			return m_StorageType == Other.m_StorageType && str_comp(m_aFilename, Other.m_aFilename) == 0;
		}
	};

	struct SDemoDeleteTarget
	{
		SDemoSelectionEntry m_Selection;
		bool m_IsDir;
	};

	struct SDemoCutSegment
	{
		int m_StartTick;
		int m_EndTick;
	};

	char m_aCurrentDemoFolder[IO_MAX_PATH_LENGTH];
	char m_aCurrentDemoSelectionName[IO_MAX_PATH_LENGTH];
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoRenameInput;
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoSliceInput;
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoSearchInput;
#if defined(CONF_VIDEORECORDER)
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoRenderInput;
	bool m_HasPendingDemoRenderSource = false;
	char m_aPendingDemoRenderFolder[IO_MAX_PATH_LENGTH] = "";
	char m_aPendingDemoRenderSelectionName[IO_MAX_PATH_LENGTH] = "";
	int m_PendingDemoRenderStorageType = IStorage::TYPE_SAVE;
#endif
	int m_DemolistSelectedIndex;
	bool m_DemolistSelectedReveal = false;
	int m_DemolistStorageType;
	bool m_DemolistMultipleStorages = false;
	EDemoBrowserSource m_DemoBrowserSource;
	std::vector<SDemoSelectionEntry> m_vDemoSelection;
	std::vector<SDemoDeleteTarget> m_vDemoDeleteTargets;
	std::vector<SDemoCutSegment> m_vDemoCutSegments;
	int m_DemoSelectionAnchorIndex = -1;
	bool m_DemoScreenshotPreviewOpen = false;
	bool m_DemoScreenshotPreviewLoadFailed = false;
	char m_aDemoScreenshotPreviewFolder[IO_MAX_PATH_LENGTH] = "";
	SDemoSelectionEntry m_DemoScreenshotPreviewSelection{};
	IGraphics::CTextureHandle m_DemoScreenshotPreviewTexture;
	int m_DemoScreenshotPreviewWidth = 0;
	int m_DemoScreenshotPreviewHeight = 0;
	size_t m_DemoHeaderFetchCursor = 0;
	size_t m_DemoDateFetchCursor = 0;
	bool m_DemoHeaderFetchComplete = true;
	bool m_DemoDateFetchComplete = true;
	bool m_DemoBrowserMetadataBackgroundAllowed = true;
	int m_Speed = 4;
	bool m_StartPaused = false;

	std::chrono::nanoseconds m_DemoPopulateStartTime{0};

	SDemoSelectionEntry DemoSelectionEntryFromItem(const CDemoItem &Item) const;
	bool IsDemoItemSelected(const CDemoItem &Item) const;
	bool IsDemoItemDeletable(const CDemoItem &Item) const;
	bool IsValidDemoIndex(int Index) const { return Index >= 0 && Index < (int)m_vpFilteredDemos.size(); }
	CDemoItem *GetSelectedDemo() const { return IsValidDemoIndex(m_DemolistSelectedIndex) ? m_vpFilteredDemos[m_DemolistSelectedIndex] : nullptr; }
	void SetDemoSelectionSingle(int Index);
	void ToggleDemoSelection(int Index);
	void SelectDemoRange(int StartIndex, int EndIndex, bool Additive);
	void SelectAllDemos();
	void SyncDemoSelection();
	int NumSelectedDemos() const;
	int NumSelectedDeletableDemos() const;
	void PrepareDemoDeleteTargetsFromSelection();
	void ResetDemoScreenshotPreview();
	bool IsDemoScreenshotPreviewItem(const CDemoItem &Item) const;
	void ToggleDemoScreenshotPreview(const CDemoItem &Item);
	void SyncDemoScreenshotPreview();
	bool LoadDemoScreenshotPreviewTexture(const CDemoItem &Item);
	void RenderDemoScreenshotPreview(CUIRect PreviewRect, const CDemoItem &Item);
	void DemolistOnUpdate(bool Reset);
	static int DemolistFetchCallback(const char *pName, int IsDir, int StorageType, void *pUser);
	bool EnsureDemoDate(CDemoItem &Item);
	bool EnsureDemoSize(CDemoItem &Item);
	void EnsureAllDemoDates();
	void ResetDemoBrowserMetadataProgress();
	void AdvanceDemoBrowserMetadata(int HeaderBudget, int DateBudget, const char *pTrigger, int VisibleFirst = -1, int VisibleEnd = -1);
	const char *DemoBrowserBaseFolder() const;
	bool DemoBrowserBrowsingScreenshots() const;
	bool DemoBrowserSupportedFile(const char *pName) const;
	void ResetDemoBrowserFolder();

	// friends
	class CFriendItem
	{
		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		char m_aCategory[IFriends::MAX_FRIEND_CATEGORY_LENGTH];
		const CServerInfo *m_pServerInfo;
		int m_FriendState;
		bool m_IsPlayer;
		bool m_IsAfk;
		// skin info 0.6
		char m_aSkin[MAX_SKIN_LENGTH];
		bool m_CustomSkinColors;
		int m_CustomSkinColorBody;
		int m_CustomSkinColorFeet;
		// skin info 0.7
		char m_aaSkin7[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_LENGTH];
		bool m_aUseCustomSkinColor7[protocol7::NUM_SKINPARTS];
		int m_aCustomSkinColor7[protocol7::NUM_SKINPARTS];

	public:
		CFriendItem(const CFriendInfo *pFriendInfo) :
			m_pServerInfo(nullptr),
			m_IsPlayer(false),
			m_IsAfk(false),
			m_CustomSkinColors(false),
			m_CustomSkinColorBody(0),
			m_CustomSkinColorFeet(0)
		{
			str_copy(m_aName, pFriendInfo->m_aName);
			str_copy(m_aClan, pFriendInfo->m_aClan);
			str_copy(m_aCategory, pFriendInfo->m_aCategory[0] != '\0' ? pFriendInfo->m_aCategory : IFriends::DEFAULT_CATEGORY);
			m_FriendState = m_aName[0] == '\0' ? IFriends::FRIEND_CLAN : IFriends::FRIEND_PLAYER;
			m_aSkin[0] = '\0';
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				m_aaSkin7[Part][0] = '\0';
				m_aUseCustomSkinColor7[Part] = false;
				m_aCustomSkinColor7[Part] = 0;
			}
		}
		CFriendItem(const CServerInfo::CClient &CurrentClient, const CServerInfo *pServerInfo, const char *pCategory) :
			m_pServerInfo(pServerInfo),
			m_FriendState(CurrentClient.m_FriendState),
			m_IsPlayer(CurrentClient.m_Player),
			m_IsAfk(CurrentClient.m_Afk),
			m_CustomSkinColors(CurrentClient.m_CustomSkinColors),
			m_CustomSkinColorBody(CurrentClient.m_CustomSkinColorBody),
			m_CustomSkinColorFeet(CurrentClient.m_CustomSkinColorFeet)
		{
			str_copy(m_aName, CurrentClient.m_aName);
			str_copy(m_aClan, CurrentClient.m_aClan);
			str_copy(m_aCategory, pCategory != nullptr && pCategory[0] != '\0' ? pCategory : IFriends::DEFAULT_CATEGORY);
			str_copy(m_aSkin, CurrentClient.m_aSkin);
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				str_copy(m_aaSkin7[Part], CurrentClient.m_aaSkin7[Part]);
				m_aUseCustomSkinColor7[Part] = CurrentClient.m_aUseCustomSkinColor7[Part];
				m_aCustomSkinColor7[Part] = CurrentClient.m_aCustomSkinColor7[Part];
			}
		}

		const char *Name() const { return m_aName; }
		const char *Clan() const { return m_aClan; }
		const char *Category() const { return m_aCategory; }
		const CServerInfo *ServerInfo() const { return m_pServerInfo; }
		int FriendState() const { return m_FriendState; }
		bool IsPlayer() const { return m_IsPlayer; }
		bool IsAfk() const { return m_IsAfk; }
		// 0.6 skin
		const char *Skin() const { return m_aSkin; }
		bool CustomSkinColors() const { return m_CustomSkinColors; }
		int CustomSkinColorBody() const { return m_CustomSkinColorBody; }
		int CustomSkinColorFeet() const { return m_CustomSkinColorFeet; }
		// 0.7 skin
		const char *Skin7(int Part) const { return m_aaSkin7[Part]; }
		bool UseCustomSkinColor7(int Part) const { return m_aUseCustomSkinColor7[Part]; }
		int CustomSkinColor7(int Part) const { return m_aCustomSkinColor7[Part]; }

		const void *ListItemId() const { return &m_aName; }
		const void *RemoveButtonId() const { return &m_FriendState; }
		const void *CommunityTooltipId() const { return &m_IsPlayer; }
		const void *SkinTooltipId() const { return &m_aSkin; }

		bool operator<(const CFriendItem &Other) const
		{
			const int Result = str_comp_nocase(m_aName, Other.m_aName);
			return Result < 0 || (Result == 0 && str_comp_nocase(m_aClan, Other.m_aClan) < 0);
		}
	};

	std::vector<unsigned char> m_vFriendsCategoryExpanded;
	std::vector<std::string> m_vFriendsCategoryNames;
	std::vector<CButtonContainer> m_vFriendsCategoryManageButtons;
	std::string m_FriendsCategoryExpandedStateCache;
	bool m_FriendsCategoryExpandedLoaded = false;
	std::vector<std::string> m_vFriendTooltipText;
	int m_FriendAddCategoryIndex = 0;
	CUi::SDropDownState m_FriendsAddCategoryDropDownState;
	CButtonContainer m_FriendsAddCategoryCreateButton;
	class CFriendsCategoryPopupContext : public SPopupMenuId
	{
	public:
		enum EMode
		{
			MODE_ACTIONS,
			MODE_ADD,
			MODE_RENAME,
		};

		CMenus *m_pMenus = nullptr;
		int m_CategoryIndex = -1;
		EMode m_Mode = MODE_ACTIONS;
		CLineInputBuffered<IFriends::MAX_FRIEND_CATEGORY_LENGTH> m_NameInput;
		CButtonContainer m_AddButton;
		CButtonContainer m_RenameButton;
		CButtonContainer m_DeleteButton;
		CButtonContainer m_ConfirmButton;
		CButtonContainer m_CancelButton;
	} m_FriendsCategoryPopupContext;
	CUi::SSelectionPopupContext m_FriendsMoveCategoryPopupContext;
	bool m_HasMoveCategoryFriend = false;
	char m_aMoveCategoryFriendName[MAX_NAME_LENGTH] = {0};
	char m_aMoveCategoryFriendClan[MAX_CLAN_LENGTH] = {0};
	enum EFriendAction
	{
		FRIEND_ACTION_MOVE_CATEGORY = 0,
		FRIEND_ACTION_EDIT_NOTE,
		FRIEND_ACTION_CLEAR_NOTE,
		FRIEND_ACTION_FOLLOW,
		FRIEND_ACTION_STOP_FOLLOW,
		FRIEND_ACTION_REMOVE,
	};
	CUi::SSelectionPopupContext m_FriendsActionPopupContext;
	std::vector<EFriendAction> m_vFriendsActionEntries;
	bool m_HasFriendAction = false;
	char m_aFriendActionName[MAX_NAME_LENGTH] = {0};
	char m_aFriendActionClan[MAX_CLAN_LENGTH] = {0};
	char m_aFriendActionAddress[NETADDR_MAXSTRSIZE] = {0};
	int m_FriendActionState = IFriends::FRIEND_NO;
	class CFriendNotePopupContext : public SPopupMenuId
	{
	public:
		CMenus *m_pMenus = nullptr;
		char m_aName[MAX_NAME_LENGTH] = {0};
		char m_aClan[MAX_CLAN_LENGTH] = {0};
		CLineInputBuffered<IFriends::MAX_FRIEND_NOTE_LENGTH> m_NoteInput;
		CButtonContainer m_ConfirmButton;
		CButtonContainer m_CancelButton;
	} m_FriendNotePopupContext;
	bool m_HasRemoveFriend = false;
	char m_aRemoveFriendName[MAX_NAME_LENGTH] = {0};
	char m_aRemoveFriendClan[MAX_CLAN_LENGTH] = {0};
	int m_RemoveFriendState = IFriends::FRIEND_NO;
	SFriendAutoFollowState m_FriendAutoFollowState;

	// found in menus.cpp
	void Render();
	void RenderPopupFullscreen(CUIRect Screen);
	void RenderPopupConnecting(CUIRect Screen);
	void RenderPopupLoading(CUIRect Screen);
#if defined(CONF_VIDEORECORDER)
	void PopupConfirmDemoReplaceVideo();
#endif
	void RenderMenubar(CUIRect Box, IClient::EClientState ClientState);
	void RenderNews(CUIRect MainView);
	void RenderStatistics(CUIRect MainView);
	static void ConchainBackgroundEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainUpdateMusicState(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	void UpdateMusicState();

	// found in menus_demo.cpp
	vec2 m_DemoControlsPositionOffset = vec2(0.0f, 0.0f);
	bool m_PausedBeforeSeeking;
	float m_PrevSeekAmount;
	float m_LastPauseChange = -1.0f;
	float m_LastSpeedChange = -1.0f;
	static constexpr int DEFAULT_SKIP_DURATION_INDEX = 3;
	int m_SkipDurationIndex = DEFAULT_SKIP_DURATION_INDEX;
	static bool DemoFilterChat(const void *pData, int Size, void *pUser);
	bool FetchHeader(CDemoItem &Item);
	void FetchAllHeaders();
	void HandleDemoSeeking(float PositionToSeek, float TimeToSeek);
	void RenderDemoPlayer(CUIRect MainView);
	void RenderDemoPlayerSliceSavePopup(CUIRect MainView);
	bool m_DemoBrowserListInitialized = false;
	void RenderDemoBrowser(CUIRect MainView);
	void RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated);
	void RenderDemoBrowserDetails(CUIRect DetailsView);
	void RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated);
	void PopupConfirmPlayDemo();
	void PopupConfirmDeleteDemo();
	void PopupConfirmDeleteFolder();
	void PopupConfirmDeleteSelectedDemos();
	static void ConchainDemoPlay(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainDemoSpeed(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	// found in menus_ingame.cpp
	STextContainerIndex m_MotdTextContainerIndex;
	struct SIngameServerInfoTextSnapshot
	{
		unsigned m_ServerNameHash = 0;
		unsigned m_AddressHash = 0;
		unsigned m_PingHash = 0;
		unsigned m_VersionHash = 0;
		unsigned m_PasswordHash = 0;
		unsigned m_GameTypeHash = 0;
		unsigned m_MapHash = 0;
		unsigned m_ScoreLimitHash = 0;
		unsigned m_TimeLimitHash = 0;
		unsigned m_RoundHash = 0;
		unsigned m_TeamsHash = 0;
		unsigned m_PlayersHash = 0;
	};
	struct SIngameMotdParagraphCache
	{
		static constexpr int INGAME_MOTD_PARAGRAPH_CHUNK_BYTES = 24;
		unsigned m_TextHash = 0;
		float m_Width = 0.0f;
		float m_FontSize = 0.0f;
		float m_Height = 0.0f;
		int64_t m_UpdateTime = -1;
		bool m_Valid = false;
		float m_LastStableHeight = 0.0f;
		unsigned m_PendingTextHash = 0;
		CUIRect m_PendingRect;
		float m_PendingWidth = 0.0f;
		float m_PendingFontSize = 0.0f;
		int64_t m_PendingUpdateTime = -1;
		uint64_t m_PendingFrame = 0;
		bool m_Pending = false;
		STextContainerIndex m_BuildTextContainerIndex;
		CTextCursor m_BuildCursor;
		int m_BuildByteOffset = 0;
		float m_BuildHeight = 0.0f;
	};
	struct SMenuSnapshotTextKey
	{
		std::string m_Scope;
		unsigned m_TextHash = 0;
		int m_Width = 0;
		int m_FontSize = 0;
		int m_Align = 0;
		uint64_t m_LocaleHash = 0;
		uint64_t m_UiScaleHash = 0;
		bool operator==(const SMenuSnapshotTextKey &Other) const
		{
			return m_Scope == Other.m_Scope &&
			       m_TextHash == Other.m_TextHash &&
			       m_Width == Other.m_Width &&
			       m_FontSize == Other.m_FontSize &&
			       m_Align == Other.m_Align &&
			       m_LocaleHash == Other.m_LocaleHash &&
			       m_UiScaleHash == Other.m_UiScaleHash;
		}
	};
	struct SMenuSnapshotTextKeyHash
	{
		size_t operator()(const SMenuSnapshotTextKey &Key) const
		{
			size_t Hash = std::hash<std::string>{}(Key.m_Scope);
			Hash ^= (size_t)Key.m_TextHash + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= (size_t)Key.m_Width + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= (size_t)Key.m_FontSize + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= (size_t)Key.m_Align + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= (size_t)Key.m_LocaleHash + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= (size_t)Key.m_UiScaleHash + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			return Hash;
		}
	};
	struct SMenuSnapshotTextEntry
	{
		CUIElement m_Element;
		std::string m_Text;
		CUIRect m_Rect;
		float m_Size = 0.0f;
		int m_Align = 0;
		SLabelProperties m_LabelProps;
		bool m_Ready = false;
		CUIElement *m_pLastReadyElement = nullptr;
	};
	std::unordered_map<SMenuSnapshotTextKey, SMenuSnapshotTextEntry, SMenuSnapshotTextKeyHash> m_SnapshotTextCache;
	std::unordered_map<std::string, CUIElement *> m_SnapshotTextLastReadyByScope;
	std::vector<SMenuSnapshotTextKey> m_SnapshotTextPending;
	struct SMenuTextContainerBuildRequest
	{
		CUIElement *m_pElement = nullptr;
		std::string m_Text;
		CUIRect m_Rect;
		float m_Size = 0.0f;
		int m_Align = 0;
		SLabelProperties m_LabelProps;
		int m_StrLen = -1;
		int m_ReadCursorGlyphCount = -1;
	};
	std::vector<SMenuTextContainerBuildRequest> m_vMenuTextContainerBuildRequests;
	SIngameServerInfoTextSnapshot m_IngameServerInfoTextSnapshot;
	SIngameMotdParagraphCache m_IngameMotdParagraphCache;
	enum class EReportScanState
	{
		IDLE,
		SCANNING,
	};

	std::shared_ptr<CHttpRequest> m_pReportScanRequest;
	EReportScanState m_ReportScanState = EReportScanState::IDLE;
	char m_aReportScanAddress[NETADDR_MAXSTRSIZE] = "";
	void ResetReportScan();
	void StartReportScan();
	void UpdateReportScan();
	void RenderGame(CUIRect MainView);
	void PopupConfirmDisconnect();
	void PopupConfirmDisconnectDummy();
	void PopupConfirmDiscardTouchControlsChanges();
	void PopupConfirmResetTouchControls();
	void PopupConfirmImportTouchControlsClipboard();
	void PopupConfirmDeleteButton();
	void PopupCancelDeselectButton();
	void PopupConfirmSelectedNotVisible();
	void PopupConfirmChangeSelectedButton();
	void PopupCancelChangeSelectedButton();
	void PopupConfirmTurnOffEditor();
	void PopupConfirmOpenWiki();
	void RenderPlayers(CUIRect MainView);
	void RenderServerInfo(CUIRect MainView);
	void RenderServerInfoMotd(CUIRect Motd);
	void PrepareIngameServerInfoTextRuntime(const CUIRect *pMainView = nullptr);
	void RenderIngameServerInfoValueCached(const char *pTextId, unsigned &TextHash, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {});
	void RenderSnapshotTextContainer(CUIElement &Element, const CUIRect *pRect);
	bool RequestSnapshotTextContainer(const char *pScope, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, CUIElement **ppReadyElement = nullptr);
	void DrainSnapshotTextContainers();
	bool RequestIngameMotdParagraphCache(CUIRect Motd, float FontSize);
	bool IngameMotdParagraphCacheMatches(CUIRect Motd, float FontSize) const;
	void DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame = false);
	bool RenderIngameMotdStableParagraphCache(CUIRect Motd, float FontSize, CUIRect MotdTextArea);
	void RenderIngameMotdFallbackText(CUIRect MotdTextArea, float FontSize);
	void DrainIngameUiSnapshotTextRuntime();
	void DrainIngameUiTextRuntime(bool AllowCurrentFrame = false);
	void RenderServerControl(CUIRect MainView);
	void RenderUnfinishedMaps(CUIRect MainView);
	bool RenderServerControlKick(CUIRect MainView, bool FilterSpectators, bool UpdateScroll);
	bool RenderServerControlServer(CUIRect MainView, bool UpdateScroll);
	void RenderIngameHint();

	// found in menus_browser.cpp
	int m_SelectedIndex;
	bool m_ServerBrowserShouldRevealSelection;
	std::vector<CUIElement *> m_avpServerBrowserUiElements[IServerBrowser::NUM_TYPES];
	void RenderServerbrowserServerList(CUIRect View, bool &WasListboxItemActivated);
	void RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated);
	enum class EConnectIntent
	{
		Manual,
		AutoFollow,
	};
	void Connect(const char *pAddress, EConnectIntent Intent = EConnectIntent::Manual);
	void PopupConfirmSwitchServer();
	void RenderServerbrowserFilters(CUIRect View);
	void ResetServerbrowserFilters();
	void RenderServerbrowserDDNetFilter(CUIRect View,
		IFilterList &Filter,
		float ItemHeight, int MaxItems, int ItemsPerRow,
		CScrollRegion &ScrollRegion, std::vector<unsigned char> &vItemIds,
		bool UpdateCommunityCacheOnChange,
		const std::function<const char *(int ItemIndex)> &GetItemName,
		const std::function<void(int ItemIndex, CUIRect Item, const void *pItemId, bool Active)> &RenderItem);
	void RenderServerbrowserCommunitiesFilter(CUIRect View);
	void RenderServerbrowserCountriesFilter(CUIRect View);
	void RenderServerbrowserTypesFilter(CUIRect View);
	struct SPopupCountrySelectionContext
	{
		CMenus *m_pMenus;
		int m_Selection;
		bool m_New;
	};
	static CUi::EPopupMenuFunctionResult PopupCountrySelection(void *pContext, CUIRect View, bool Active);
	struct SPopupSettingsCountrySelectionContext
	{
		CMenus *m_pMenus;
		int *m_pCountry;
		int m_Selection;
		bool m_New;
	};
	static CUi::EPopupMenuFunctionResult PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active);
	void RenderServerbrowserInfo(CUIRect View);
	void RenderServerbrowserInfoScoreboard(CUIRect View, const CServerInfo *pSelectedServer);
	void RenderServerbrowserFriends(CUIRect View);
	void RenderServerbrowserQm(CUIRect View);
	void RenderServerbrowserFavoriteMaps(CUIRect View);
	static CUi::EPopupMenuFunctionResult PopupFriendsCategory(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupFriendNote(void *pContext, CUIRect View, bool Active);
	void FriendlistOnUpdate();
	void ApplyFriendsCategoryExpandedState();
	void SaveFriendsCategoryExpandedState();
	void RefreshFriendsCategoryNames();
	void PopupConfirmRemoveFriend();
	void PopupCancelRemoveFriend();
	void RenderServerbrowserTabBar(CUIRect TabBar);
	void RenderServerbrowserToolBox(CUIRect ToolBox);
	void RenderServerbrowser(CUIRect MainView, bool DrawBackground);
	template<typename F>
	bool PrintHighlighted(const char *pName, F &&PrintFn);
	CTeeRenderInfo GetTeeRenderInfo(vec2 Size, const char *pSkinName, bool CustomSkinColors, int CustomSkinColorBody, int CustomSkinColorFeet) const;
	static void ConchainFriendlistUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainFavoritesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainCommunitiesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainUiPageUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	void UpdateCommunityCache(bool Force);

	// found in menus_settings.cpp
	void RenderLanguageSettings(CUIRect MainView);
	bool RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics = nullptr);
	void RenderThemeSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics = nullptr);
	void RenderSettingsGeneral(CUIRect MainView);
	void RenderSettingsPlayer(CUIRect MainView);
	void RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize = ui_token::font::BODY);
	void RenderSettingsTee(CUIRect MainView);
	void RenderSettingsTee7(CUIRect MainView);
	void RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics);
	void RenderSettingsTeeCustom7(CUIRect MainView, const SSettingsContentMetrics &Metrics);
	void RenderSkinSelection7(CUIRect MainView, float BodySize);
	void RenderSkinPartSelection7(CUIRect MainView, float BodySize);
	void RenderSettingsGraphics(CUIRect MainView);
	void RenderSettingsSound(CUIRect MainView);
	void RenderSettings(CUIRect MainView);
	void RenderSettingsCustom(CUIRect MainView);

	// found in menus_settings_controls.cpp
	// TODO: Change PopupConfirm to avoid using a function pointer to a CMenus
	//       member function, to move this function to CMenusSettingsControls
	void ResetSettingsControls();

	std::vector<CButtonContainer> m_vButtonContainersNamePlateShow = {{}, {}, {}, {}};
	std::vector<CButtonContainer> m_vButtonContainersNamePlateHookStrongWeakScope = {{}, {}, {}, {}, {}};
	std::vector<CButtonContainer> m_vButtonContainersNamePlateKeyPresses = {{}, {}, {}, {}};
	class CSkinQueuePresetRenamePopupContext : public SPopupMenuId
	{
	public:
		CMenus *m_pMenus = nullptr;
		int m_Dummy = 0;
		int m_PresetIndex = -1;
		CLineInputBuffered<64> m_NameInput;
		CButtonContainer m_ConfirmButton;
		CButtonContainer m_CancelButton;
	} m_SkinQueuePresetRenamePopupContext;

	class CMapListItem
	{
	public:
		char m_aFilename[IO_MAX_PATH_LENGTH];
		char m_aValuePrefix[IO_MAX_PATH_LENGTH] = "";
		bool m_IsDirectory;
	};
	class CPopupMapPickerContext
	{
	public:
		std::vector<CMapListItem> m_vMaps;
		char m_aCurrentMapFolder[IO_MAX_PATH_LENGTH] = "";
		char m_aRootPath[IO_MAX_PATH_LENGTH] = "maps";
		char m_aFallbackRootPath[IO_MAX_PATH_LENGTH] = "";
		char m_aValuePrefix[IO_MAX_PATH_LENGTH] = "";
		char m_aFallbackValuePrefix[IO_MAX_PATH_LENGTH] = "";
		char m_aListingValuePrefix[IO_MAX_PATH_LENGTH] = "";
		char *m_pTargetConfig = nullptr;
		int m_TargetConfigSize = 0;
		static int MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
		void MapListPopulate();
		CMenus *m_pMenus;
		int m_Selection;
	};

	static bool CompareFilenameAscending(const CMapListItem Lhs, const CMapListItem Rhs)
	{
		if(str_comp(Rhs.m_aFilename, "..") == 0)
			return false;
		if(str_comp(Lhs.m_aFilename, "..") == 0)
			return true;
		if(Lhs.m_IsDirectory != Rhs.m_IsDirectory)
			return Lhs.m_IsDirectory;
		return str_comp_filenames(Lhs.m_aFilename, Rhs.m_aFilename) < 0;
	}

	static CUi::EPopupMenuFunctionResult PopupSkinQueuePresetRename(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMapPicker(void *pContext, CUIRect View, bool Active);

	void SetNeedSendInfo();
	void SetNeedSendInfo(bool Dummy);
	void UpdateColors();

	IGraphics::CTextureHandle m_TextureBlob;

public:
	void RenderBackground();

	CMenus();
	int Sizeof() const override { return sizeof(*this); }

	void StartLoading(int Total);
	void RenderLoading(const char *pCaption, const char *pContent, int IncreaseCounter);
	void FinishLoading();
	void PrewarmSettingsPages();
	void EnsureSettingsBindCache();

	bool IsInit() const { return m_IsInit; }

	bool IsActive() const { return m_MenuActive; }
	bool IsSettingsPageActive() const;
	const char *CurrentQmUiPerfPage() const;
	const char *CurrentQmUiPerfOperation() const;
	int IdleRenderFrameRate() const;
	SSettingsResourceFrameContext SettingsResourceFrameContext() const { return {m_SettingsScrollActive, false, m_SettingsPostScrollRecoveryFrames, m_SettingsHighPrioritySettled}; }
	const SSettingsAdaptiveBudgetOutput &CurrentSettingsUiFrameBudget() const { return m_CurrentSettingsUiFrameBudget; }
	void SetActive(bool Active);

	void OnInterfacesInit(CGameClient *pClient) override;
	void OnInit() override;

	void OnStateChange(int NewState, int OldState) override;
	void OnWindowResize() override;
	void OnReset() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnShutdown() override;

	enum
	{
		PAGE_NEWS = 1,
		PAGE_GAME,
		PAGE_PLAYERS,
		PAGE_SERVER_INFO,
		PAGE_CALLVOTE,
		PAGE_INTERNET,
		PAGE_LAN,
		PAGE_FAVORITES,
		PAGE_FAVORITE_COMMUNITY_1,
		PAGE_FAVORITE_COMMUNITY_2,
		PAGE_FAVORITE_COMMUNITY_3,
		PAGE_FAVORITE_COMMUNITY_4,
		PAGE_FAVORITE_COMMUNITY_5,
		PAGE_FAVORITE_MAPS,
		PAGE_DEMOS,
		PAGE_SETTINGS,
		PAGE_NETWORK,
		PAGE_GHOST,
		PAGE_UNFINISHED_MAPS,
		PAGE_STATS,

		PAGE_LENGTH,
	};

	enum
	{
		SETTINGS_LANGUAGE = 0,
		SETTINGS_GENERAL,
		SETTINGS_PLAYER,
		SETTINGS_TEE,
		SETTINGS_APPEARANCE,
		SETTINGS_CONTROLS,
		SETTINGS_GRAPHICS,
		SETTINGS_SOUND,
		SETTINGS_DDNET,
		SETTINGS_ASSETS,
		SETTINGS_TCLIENT,
		SETTINGS_QMCLIENT,
		SETTINGS_SEARCH,
		SETTINGS_PROFILES,
		SETTINGS_CONFIGS,
		SETTINGS_CONTRIBUTORS,

		SETTINGS_LENGTH,
	};
	static constexpr float TCLIENT_SETTINGS_BODY_FONT_SIZE = 11.2f;

	enum
	{
		QMCLIENT_SETTINGS_TAB_VISUAL = 0,
		QMCLIENT_SETTINGS_TAB_FUNCTION,
		QMCLIENT_SETTINGS_TAB_HUD,
		QMCLIENT_SETTINGS_TAB_CONTRIBUTORS,
		QMCLIENT_SETTINGS_TAB_CONFIG,

		NUMBER_OF_QMCLIENT_SETTINGS_TABS,
	};

	enum
	{
		BIG_TAB_NEWS = 0,
		BIG_TAB_INTERNET,
		BIG_TAB_LAN,
		BIG_TAB_FAVORITES,
		BIG_TAB_FAVORITE_MAPS,
		BIT_TAB_FAVORITE_COMMUNITY_1,
		BIT_TAB_FAVORITE_COMMUNITY_2,
		BIT_TAB_FAVORITE_COMMUNITY_3,
		BIT_TAB_FAVORITE_COMMUNITY_4,
		BIT_TAB_FAVORITE_COMMUNITY_5,
		BIG_TAB_DEMOS,

		BIG_TAB_LENGTH,
	};

	enum
	{
		SMALL_TAB_HOME = 0,
		SMALL_TAB_QUIT,
		SMALL_TAB_SETTINGS,
		SMALL_TAB_EDITOR,
		SMALL_TAB_DEMOBUTTON,
		SMALL_TAB_SERVER,
		SMALL_TAB_BROWSER_FILTER,
		SMALL_TAB_BROWSER_INFO,
		SMALL_TAB_BROWSER_FRIENDS,
		SMALL_TAB_BROWSER_QM,

		SMALL_TAB_LENGTH,
	};

	enum
	{
		APPEARANCE_TAB_HUD = 0,
		APPEARANCE_TAB_CHAT = 1,
		APPEARANCE_TAB_NAME_PLATE = 2,
		APPEARANCE_TAB_HOOK_COLLISION = 3,
		APPEARANCE_TAB_INFO_MESSAGES = 4,
		APPEARANCE_TAB_LASER = 5,
		NUMBER_OF_APPEARANCE_TABS = 6,
	};

	SUIAnimator m_aAnimatorsBigPage[BIG_TAB_LENGTH];
	SUIAnimator m_aAnimatorsSmallPage[SMALL_TAB_LENGTH];
	SUIAnimator m_aAnimatorsSettingsTab[SETTINGS_LENGTH];
	std::array<CButtonContainer, SETTINGS_LENGTH> m_aSettingsTabButtons;
	std::array<CUIElement, SETTINGS_LENGTH> m_aSettingsTabLabelElements;
	std::array<const char *, SETTINGS_LENGTH> m_apSettingsTabs{};
	int m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;
	int m_TClientSettingsTab = 0;
	int m_AppearanceSettingsTab = APPEARANCE_TAB_HUD;
	CLineInputBuffered<128> m_GlobalCardSearchInput;
	void ClearQmClientSettingsSearchInputs();

	// DDRace
	int DoButton_CheckBox_Tristate(const void *pId, const char *pText, TRISTATE Checked, const CUIRect *pRect);
	std::vector<CDemoItem> m_vDemos;
	std::vector<CDemoItem *> m_vpFilteredDemos;
	void DemolistPopulate();
	void RefreshFilteredDemos();
	void DemoSeekTick(IDemoPlayer::ETickOffset TickOffset);
	bool m_Dummy;

	const char *GetCurrentDemoFolder() const { return m_aCurrentDemoFolder; }

	// Ghost
	struct CGhostItem
	{
		char m_aFilename[IO_MAX_PATH_LENGTH];
		char m_aPlayer[MAX_NAME_LENGTH];

		bool m_Failed;
		int m_Time;
		int m_Slot;
		bool m_Own;
		time_t m_Date;

		CGhostItem() :
			m_Slot(-1), m_Own(false) { m_aFilename[0] = 0; }

		bool operator<(const CGhostItem &Other) const { return m_Time < Other.m_Time; }

		bool Active() const { return m_Slot != -1; }
		bool HasFile() const { return m_aFilename[0]; }
	};

	enum
	{
		GHOST_SORT_NONE = -1,
		GHOST_SORT_NAME,
		GHOST_SORT_TIME,
		GHOST_SORT_DATE,
	};

	std::vector<CGhostItem> m_vGhosts;

	std::chrono::nanoseconds m_GhostPopulateStartTime{0};

	void GhostlistPopulate();
	CGhostItem *GetOwnGhost();
	void UpdateOwnGhost(CGhostItem Item);
	void DeleteGhostItem(int Index);
	void SortGhostlist();

	bool CanDisplayWarning() const;

	void PopupWarning(const char *pTopic, const char *pBody, const char *pButton, std::chrono::nanoseconds Duration);

	std::chrono::nanoseconds m_PopupWarningLastTime;
	std::chrono::nanoseconds m_PopupWarningDuration;

	int m_DemoPlayerState;

	enum
	{
		POPUP_NONE = 0,
		POPUP_MESSAGE, // generic message popup (one button)
		POPUP_CONFIRM, // generic confirmation popup (two buttons)
		POPUP_FIRST_LAUNCH,
		POPUP_JOIN_TUTORIAL,
		POPUP_POINTS,
		POPUP_DISCONNECTED,
		POPUP_LANGUAGE,
		POPUP_RENAME_DEMO,
		POPUP_RENDER_DEMO,
		POPUP_RENDER_DONE,
		POPUP_PASSWORD,
		POPUP_QUIT,
		POPUP_RESTART,
		POPUP_WARNING,
		POPUP_SAVE_SKIN,
	};

	enum
	{
		// demo player states
		DEMOPLAYER_NONE = 0,
		DEMOPLAYER_SLICE_SAVE,
	};

	void SetMenuPage(int NewPage);
	void SetGamePage(int NewPage);
	void RefreshBrowserTab(bool Force);
	void ForceRefreshLanPage();
	void SetShowStart(bool ShowStart);
	void ShowQuitPopup();
	void LoadSettingsRuntimeCacheMetadata();
	void SaveSettingsRuntimeCacheMetadata();
	void PrewarmVisibleSettingsResources(CUIRect MainView);
	enum EMenuTextScope
	{
		MENU_TEXT_SCOPE_SETTINGS = 0,
		MENU_TEXT_SCOPE_INGAME,
	};
	enum EMenuTextStyleMode
	{
		MENU_TEXT_STYLE_DEFAULT = 0,
		MENU_TEXT_STYLE_RECT,
		MENU_TEXT_STYLE_EXACT,
		MENU_TEXT_STYLE_ALLOWLIST_DYNAMIC,
	};
	struct SMenuTextStyleKey
	{
		float m_FontSize = 0.0f;
		int m_Align = TEXTALIGN_ML;
		int m_MaxWidthBucket = -1;
		int m_UiScaleBucket = 0;
		int m_HiDpiScaleBucket = 0;
		int m_CompactMode = 0;
	};
	struct SMenuTextPlanItem
	{
		EMenuTextScope m_Scope = MENU_TEXT_SCOPE_SETTINGS;
		int m_Page = -1;
		int m_Tab = -1;
		int m_Subtab = -1;
		std::string m_TextId;
		std::string m_Text;
		SMenuTextStyleKey m_StyleKey{};
		EMenuTextStyleMode m_StyleMode = MENU_TEXT_STYLE_RECT;
		std::string m_AllowlistReason;
		std::string m_SourceTag;
		CUIRect m_Rect{};
		SLabelProperties m_LabelProps{};
		float m_FontSize = 0.0f;
		int m_Align = TEXTALIGN_ML;
		float m_MaxWidth = -1.0f;
	};
	enum ESettingsMenuTextPlanCollectionUnitKind
	{
		MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS = 0,
		MENU_TEXT_PLAN_UNIT_TCLIENT_TAB,
		MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB,
		MENU_TEXT_PLAN_UNIT_BASE_PAGE,
		MENU_TEXT_PLAN_UNIT_INGAME_ESC,
	};
	struct SSettingsMenuTextPlanCollectionUnit
	{
		ESettingsMenuTextPlanCollectionUnitKind m_Kind = MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS;
		int m_Page = -1;
		int m_Tab = -1;
	};
	CUIElement &MenuTextElement(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const SMenuTextStyleKey &StyleKey);
	void CollectMenuTextPlanItem(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect *pRect, float FontSize, int Align, const SLabelProperties &LabelProps, const SMenuTextStyleKey &StyleKey);
	SMenuTextStyleKey BuildMenuTextStyleKey(const CUIRect *pRect, float FontSize, int Align, const SLabelProperties &LabelProps) const;
	SMenuTextStyleKey SettingsMenuTextPlanStyleKey(const SMenuTextPlanItem &Item) const;
	SMenuTextStyleKey BuildSettingsShellTitleTextStyle(const CUIRect &Rect, CUIRect *pOutLabel = nullptr) const;
	SMenuTextPlanItem AddStableTextDefault(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, float Width, float Height, float FontSize, int Align = TEXTALIGN_ML, const char *pSourceTag = nullptr) const;
	SMenuTextPlanItem AddStableTextLabel(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, float FontSize, int Align = TEXTALIGN_ML, const SLabelProperties &LabelProps = {}, const char *pSourceTag = nullptr) const;
	SMenuTextPlanItem AddStableTextCheckbox(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, const char *pSourceTag = nullptr) const;
	SMenuTextPlanItem AddStableTextScrollbar(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, unsigned Flags = 0u, const char *pSourceTag = nullptr) const;
	SMenuTextPlanItem AddStableTextButton(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, const char *pSourceTag = nullptr) const;
	void DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {}, int StrLen = -1, const CTextCursor *pReadCursor = nullptr, bool Render = true);
	int DoIngameMenuTab(CButtonContainer *pButtonContainer, int Page, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Corners);
	int DoIngameMenuButton(int Page, const char *pTextId, CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Flags = BUTTONFLAG_LEFT, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f);
	int DoIngameMenuCheckBox(int Page, const char *pTextId, const void *pId, const char *pText, int Checked, const CUIRect *pRect);
	void DoIngameMenuLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {});
	void DoIngameMenuTitleLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {});
	CUIElement &SettingsTextElement(int Page, int Tab, const char *pTextId);
	CUIElement &SettingsTextElement(int Page, int Tab, const char *pTextId, const SMenuTextStyleKey &StyleKey);
	void DoSettingsLabelStreamed(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {}, int StrLen = -1, const CTextCursor *pReadCursor = nullptr, bool Render = true);
	void DoSettingsLabel(int Page, int Tab, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps = {}, bool Render = true);
	void DoSettingsMenuLabel(int Page, int Tab, int Subtab, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &Props = {}, int MaxWidth = -1);
	int DoSettingsButton_Menu(int Page, int Tab, int Subtab, CButtonContainer *pBC, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Flags = BUTTONFLAG_LEFT, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, const ColorRGBA &Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), float FontFactor = 0.0f, float BodySize = -1.0f);
	int DoSettingsButton_Menu(int Page, int Tab, int Subtab, CButtonContainer *pBC, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SSettingsContentMetrics &Metrics, int Flags = BUTTONFLAG_LEFT, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, const ColorRGBA &Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), float FontFactor = 0.0f);
	int DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect);
	int DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SLabelProperties &LabelProps);
	int DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SLabelProperties &LabelProps, bool ProcessInput, float RequestedFontSize = -1.0f);
	bool DoSettingsScrollbarOption(int Page, int Tab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = "", const char *pMaxText = nullptr);
	bool DoSettingsScrollbarOption(int Page, int Tab, int Subtab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = "", const char *pMaxText = nullptr);

	bool PrepareSettingsNumericFieldLabel(int Page, int Tab, int Subtab, const char *pTextId, const CUIRect &Rect, const char *pLabel, unsigned Flags, ui_widget::SNumericFieldOptions &Options);
	ui_widget::SNumericFieldState *GetSettingsNumericFieldState(const void *pId);
	bool DoSettingsLine_RadioMenu(int Page, int Tab, int Subtab, CUIRect &View, const char *pLabelTextId, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vButtonTextIds, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value, const SSettingsContentMetrics &Metrics);
	void BuildBaseSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);
	void BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);
	void BuildSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems);
	void BuildSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);
	void BuildVisibleSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);
	void PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride);
	void CollectSettingsMenuTextPlanUnit(const SSettingsMenuTextPlanCollectionUnit &Unit, CUIRect Screen, CUIRect SettingsMainView);
	bool AdvanceSettingsMenuTextPlanCollection(int Budget, const char *pOperationOverride);
	bool PrebuildSettingsTextPlanItem(const SMenuTextPlanItem &Item, int &RemainingBudget);
	int CountMissingSettingsMenuTextPlanItems() const;
	void PrebuildSettingsMenuTextPool(int Budget, const char *pScopeOverride = nullptr, const char *pOperationOverride = nullptr);
	int PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride = nullptr);
	void PrebuildIngameEscTextPoolBeforeOpen(int Budget);
	void LogSettingsAdaptiveBudget(const char *pSource, const SSettingsAdaptiveBudgetInput &Input, const SSettingsAdaptiveBudgetOutput &Output) const;
	void PrewarmSettingsTextPoolForLoading(int Budget) { PrebuildSettingsMenuTextPool(Budget, "target_settings", "settings_open"); }
	int SettingsTextPrebuildRemaining() const { return m_SettingsMenuTextLastPrebuildStats.m_Remaining; }
	int SettingsTextPlanCollectionRemaining() const { return m_SettingsMenuTextLastCollectionStats.m_Remaining; }
	void EnsureSettingsMenuTextPlanReadyForVisible();
	void InvalidateSettingsTextPool();
	void InvalidateMenuTextPool(const char *pReason);
	void InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason Reason);
	void FinalizeTeeListDrainPerfSession();
	void StartSettingsPerfFixedWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab, int MaxFrames);
	void StartSettingsPerfScrollWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab);
	void RecordSettingsPerfWindowFrame(double MenuDurationMs);
	void LogSettingsPerfWindowSummary(const SQmSettingsPerfWindowSummary &Summary);
	const char *SettingsPerfContextName() const;
	const char *SettingsPerfActiveOperation() const;
	const char *SettingsPerfStableTextScope(int Page) const;
	int SettingsGpuUploadLimitForFrame(bool TeeSettingsActive, bool AssetsSettingsActive, int TeeSkinGpuUploadLimiterUnits) const
	{
		if(TeeSettingsActive)
			return TeeSkinGpuUploadLimiterUnits;
		if(AssetsSettingsActive)
			return 8;
		return CGpuUploadLimiter::DefaultMaxUploadsPerFrame();
	}
	void ResetSettingsFrameBudgetForFrame(bool TeeSettingsActive, bool AssetsSettingsActive, int TeeSkinGpuUploadsPerFrame = -1)
	{
		const SSettingsResourceFrameContext FrameContext = SettingsResourceFrameContext();
		m_SettingsFrameBudget = SSettingsWarmupFrameBudget{};
		m_CurrentSettingsUiFrameBudget = SSettingsAdaptiveBudgetOutput{};
		SettingsApplyActiveTeeSkinFrameBudget(m_SettingsFrameBudget, TeeSettingsActive);
		if(TeeSettingsActive)
			m_SettingsFrameBudget.m_MaxGpuUploads = TeeSkinGpuUploadsPerFrame >= 0 ? TeeSkinGpuUploadsPerFrame : SettingsSkinGpuUploadFrameUnits(FrameContext, TeeSettingsActive);
		else if(AssetsSettingsActive)
			m_SettingsFrameBudget.m_MaxGpuUploads = 8;
	}
	SSettingsWarmupFrameBudget *SettingsFrameBudget() { return &m_SettingsFrameBudget; }
	int SettingsTextContainerCount();
	int MenuTextPoolSizeForTesting() const;
	void JoinTutorial();

private:
	struct SMenuTextPoolEntry
	{
		CUIElement m_Element;
		SMenuTextStyleKey m_StyleKey{};
		uint64_t m_Generation = 0;
		uint64_t m_LastUsedFrame = 0;
		bool m_Built = false;
	};

	struct SSettingsTextPerfStats
	{
		int m_New = 0;
		int m_Reused = 0;
	};

	struct SSettingsMenuTextPrebuildStats
	{
		int m_Built = 0;
		int m_Reused = 0;
		int m_Remaining = 0;
		int m_Budget = 0;
	};
	struct SSettingsMenuTextPlanCollectionStats
	{
		int m_UnitsDone = 0;
		int m_UnitsTotal = 0;
		int m_Remaining = 0;
		int m_Budget = 0;
		bool m_Complete = false;
		bool m_Dirty = true;
	};

	CQmSettingsPerfWindowTracker m_SettingsPerfWindowTracker;
	int m_SettingsPerfLastPage = -1;
	int m_SettingsPerfLastTClientTab = -1;
	int m_SettingsPerfLastQmClientTab = -1;
	uint64_t m_IngameEscOpenFrame = 0;
	bool m_IngameServerInfoBackgroundPrepareRequested = false;

	class CScopedSettingsTextPerfStats
	{
		CMenus *m_pMenus = nullptr;
		SSettingsTextPerfStats *m_pPrevious = nullptr;
		SSettingsTextPerfStats m_Stats;

	public:
		explicit CScopedSettingsTextPerfStats(CMenus *pMenus) :
			m_pMenus(pMenus),
			m_pPrevious(pMenus->m_pActiveSettingsTextPerfStats)
		{
			m_pMenus->m_pActiveSettingsTextPerfStats = &m_Stats;
		}

		~CScopedSettingsTextPerfStats()
		{
			m_pMenus->m_pActiveSettingsTextPerfStats = m_pPrevious;
		}

		const SSettingsTextPerfStats &Stats() const { return m_Stats; }
	};

	class CScopedMenuTextVisibleGuard
	{
		CMenus *m_pMenus = nullptr;
		bool m_Previous = false;

	public:
		explicit CScopedMenuTextVisibleGuard(CMenus *pMenus);
		~CScopedMenuTextVisibleGuard();
	};

	struct SSettingsRuntimeMetadata
	{
		int m_LastPage = -1;
		int m_LastTClientTab = -1;
		int m_LastQmTab = -1;
		int m_LastScrollPage = -1;
		float m_LastScrollY = 0.0f;
		SSettingsRuntimeCacheKey m_RuntimeKey;
		bool m_Valid = false;
	};

	struct SSettingsScrollRegionFrame
	{
		vec2 m_BeginOffset = vec2(0.0f, 0.0f);
		float m_PreviousOffsetY = 0.0f;
		float m_FinalOffsetY = 0.0f;
	};

	struct SSettingsQmScrollFrame
	{
		vec2 m_Offset = vec2(0.0f, 0.0f);
		CUIRect m_ViewRect;
		CUIRect m_ClipRect;
		SQmScrollContainerStyle m_Style;
		SQmScrollContainerFrame m_Frame;
		float m_PreviousOffsetY = 0.0f;
		bool m_Enabled = false;
	};

	struct SQmSettingsCardStyle
	{
		float m_Padding = 14.0f;
		float m_Spacing = 16.0f;
		float m_CornerRadius = 10.0f; // 12 → 10（macOS 更克制）
		float m_ScrollbarWidth = 20.0f;
		float m_ScrollbarMargin = 5.0f;
		ColorRGBA m_GlassColor = ColorRGBA(0.17f, 0.18f, 0.22f, 0.72f); // 现代移动端深色 surface：更亮清透
		ColorRGBA m_HighlightColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f); // 0.05 → 0.06（hairline 旁维持可读）
		ColorRGBA m_HairlineColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f); // 新增：hairline 边框色
		ColorRGBA m_ShadowColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f); // 保留字段，置透明（不再绘制，为以后内嵌阴影留口子）
	};

	struct SSettingsUiBudgetFrame
	{
		double m_LayoutMs = 0.0;
		double m_TextMs = 0.0;
		int m_TextNew = 0;
		int m_TextReused = 0;
		int m_DrawCalls = 0;
		int m_Vertices = 0;
		int m_Indices = 0;
		int m_HeapAllocs = 0;
		int m_VisibleWidgets = 0;
		int m_Tab = -1;
		int m_Subtab = -1;
	};

	SSettingsScrollRegionFrame BeginSettingsScrollRegion(CScrollRegion &ScrollRegion, CUIRect *pView, const CScrollRegionParams &Params, float PreviousOffsetY);
	void FinishSettingsScrollRegion(CScrollRegion &ScrollRegion, SSettingsScrollRegionFrame &Frame, const CUIRect *pEndRect = nullptr, int Page = -1, bool TrackScrollActive = true);
	SSettingsQmScrollFrame BeginSettingsQmScrollContainer(CQmScrollState &ScrollState, CQmScrollContainer &ScrollContainer, CUIRect *pView, float ContentHeight, const SQmSettingsCardStyle &CardStyle, float UiScale, float PreviousOffsetY, bool Enabled);
	void FinishSettingsQmScrollContainer(CQmScrollState &ScrollState, CQmScrollContainer &ScrollContainer, SSettingsQmScrollFrame &Frame, const CUIRect &EndRect, float *pContentHeight, float *pPreviousOffsetY, bool TrackScrollActive = true);
	SQmSettingsCardStyle QmSettingsCardStyle(float UiScale) const;
	CScrollRegionParams QmSettingsScrollRegionParams(float UiScale) const;
	float SettingsPageUiScale(float ContentWidth) const;
	SSettingsContentMetrics CurrentSettingsContentMetrics() const;
	SSettingsPageLayoutFrame SettingsPageLayout(const CUIRect &ContentView, float UiScale) const;
	bool SetSettingsPageFromCardTab(const char *pTab);
	void NavigateToSettingsCard(const qm_card_registry::SCardNavigationTarget &Target);
	void RequestSettingsCardFocus(const char *pStableId);
	void PrepareSettingsAdaptiveBudgetInput(SSettingsAdaptiveBudgetInput &Input);
	SSettingsAdaptiveBudgetOutput BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer Consumer, const char *pSource, SSettingsAdaptiveBudgetInput Input);
	bool MenuTextContainerNeedsBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, int StrLen, const CTextCursor *pReadCursor);
	bool RequestMenuTextContainerBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, int StrLen, const CTextCursor *pReadCursor);
	void QueueMenuTextContainerBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor);
	void DrainMenuTextContainerBuildRequests();
	void RemoveMenuTextContainerBuildRequest(const CUIElement &Element);
	int TrimMenuTextPoolForInsert(uint64_t CurrentFrame);
	void DrainMenuTextContainerBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated);
	void CountMenuTextImmediateFallback();
	void ClearSettingsAssetsCardMetadataCache();
	void ClearSettingsTeePreviewCache();
	void ClearSettingsLanguageRowCache();
	void LogSettingsUiBudget(const char *pPage, const SSettingsUiBudgetFrame &Frame) const;

	SSettingsRuntimeMetadata m_SettingsRuntimeMetadata;
	SSettingsWarmupFrameBudget m_SettingsFrameBudget;
	SSettingsAdaptiveBudgetOutput m_CurrentSettingsUiFrameBudget;
	SSettingsAdaptiveBudgetOutput m_IngameTextFrameBudget;
	float m_TextContainerCreateMsEwma = 0.0f;
	float m_TextContainerUploadMsEwma = 0.0f;
	float m_GlyphRasterizeMsEwma = 0.0f;
	float m_GlyphUploadMsEwma = 0.0f;
	float m_SettingsTClientCurrentScrollY = 0.0f;
	bool m_SettingsTClientScrollRestorePending = false;
	bool m_SettingsPageSwitchActive = false;
	bool m_SettingsScrollActive = false;
	bool m_MenuUiPerfScrollActive = false;
	int m_SettingsPostScrollRecoveryFrames = 0;
	bool m_SettingsHighPrioritySettled = false;
	int m_SettingsTextContextPage = -1;
	int m_SettingsTextContextTab = -1;
	int m_SettingsTextContextSubtab = -1;
	int *m_pSettingsTextPrebuildBudget = nullptr;
	std::unordered_map<std::string, SMenuTextPoolEntry> m_MenuTextPool;
	uint64_t m_MenuTextLastTrimFrame = ~uint64_t{0};
	CUIElement m_MenuTextFallbackElement;
	uint64_t m_MenuTextPoolGeneration = 1;
	uint64_t m_MenuTextPoolLanguageHash = 0;
	uint64_t m_MenuTextPoolFontHash = 0;
	uint64_t m_MenuTextPoolLayoutHash = 0;
	uint64_t m_MenuTextPoolThemeHash = 0;
	std::string m_MenuTextPoolLastStaleReason;
	bool m_MenuTextPlanCollecting = false;
	std::vector<SMenuTextPlanItem> *m_pMenuTextPlanCollection = nullptr;
	bool m_MenuTextPlanPendingActive = false;
	SMenuTextPlanItem m_MenuTextPlanPendingItem;
	uint64_t m_MenuTextCoverageFrame = 0;
	bool m_MenuTextPoolVisibleGuard = false;
	int m_MenuTextStableCandidatesThisFrame = 0;
	int m_MenuTextStableHitsThisFrame = 0;
	int m_MenuTextStablePoolHitsThisFrame = 0;
	int m_MenuTextStableRenderReadyHitsThisFrame = 0;
	int m_MenuTextStableBuildQueuedThisFrame = 0;
	int m_MenuTextStableFallbackImmediateThisFrame = 0;
	int m_MenuTextStableReusedThisFrame = 0;
	int m_MenuTextStableTextNewThisFrame = 0;
	int m_MenuTextStableTextReusedThisFrame = 0;
	EMenuTextScope m_MenuTextStableScopeThisFrame = MENU_TEXT_SCOPE_SETTINGS;
	int m_MenuTextStablePageThisFrame = -1;
	int m_MenuTextStableTabThisFrame = -1;
	int m_MenuTextStableSubtabThisFrame = -1;
	int m_MenuTextStableMissesThisFrame = 0;
	int m_MenuTextStableStalesThisFrame = 0;
	int m_MenuTextStablePlannedThisFrame = 0;
	int m_MenuTextStableUnplannedThisFrame = 0;
	SSettingsTextPerfStats *m_pActiveSettingsTextPerfStats = nullptr;
	std::vector<SMenuTextPlanItem> m_vSettingsMenuTextPrebuildPlan;
	std::vector<SSettingsMenuTextPlanCollectionUnit> m_vSettingsMenuTextPlanCollectionUnits;
	std::unordered_set<std::string> m_SettingsMenuTextPlannedDescriptors;
	std::unordered_set<std::string> m_SettingsMenuTextPlannedKeys;
	size_t m_SettingsMenuTextPlanCursor = 0;
	size_t m_SettingsMenuTextPlanCollectionCursor = 0;
	uint64_t m_SettingsMenuTextPlanGeneration = 0;
	uint64_t m_SettingsMenuTextPlanCollectionGeneration = 0;
	std::string m_SettingsMenuTextPlanCollectionOperation;
	bool m_SettingsMenuTextPlanMetadataDirty = true;
	bool m_SettingsMenuTextPlanCollectionDirty = true;
	bool m_SettingsMenuTextPlanCollectionComplete = false;
	SSettingsMenuTextPrebuildStats m_SettingsMenuTextLastPrebuildStats;
	SSettingsMenuTextPlanCollectionStats m_SettingsMenuTextLastCollectionStats;
	std::string m_SettingsCardFocusStableId;
	qm_card_order::CModel m_SettingsCardOrderModel;
	bool m_SettingsCardOrderLoaded = false;
	CSettingsCardDeck m_SettingsCardDeck;
	qm_card_order::CModel m_SettingsCardRenderOnlyOrderModel;
	std::string m_SettingsCardRenderOnlyOrderSource;
	bool m_SettingsCardRenderOnlyOrderInitialized = false;
	CSettingsCardDeck m_SettingsCardRenderOnlyDeck;
	uint64_t m_SettingsCardDeckDisplayCycle = 0;
	SSettingsCardDeckDisplayCycleState m_SettingsCardDeckDisplayState;
	SSettingsShellLayoutFrame m_SettingsShellLayout;
	SSettingsContentMetrics m_SettingsContentMetrics;
	bool m_SettingsShellLayoutValid = false;

	CCommunityIcons m_CommunityIcons;
	CMenusIngameTouchControls m_MenusIngameTouchControls;
	friend CMenusIngameTouchControls;
	CMenusSettingsControls m_MenusSettingsControls;
	friend CMenusSettingsControls;
	CMenusStart m_MenusStart;

	static int GhostlistFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);

	// found in menus_ingame.cpp
	void RenderInGameNetwork(CUIRect MainView);
	void RenderGhost(CUIRect MainView);

	// found in menus_settings.cpp
	void RenderSettingsDDNet(CUIRect MainView);
	void RenderSettingsAppearance(CUIRect MainView);

	// found in menus_qmclient.cpp
	void RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly = false);
	void RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly = false);
	SSettingsSection BuildTClientThemeCacheSection();
	SSettingsSection BuildTClientAutoReplyCacheSection();
	SSettingsSection BuildTClientPetCacheSection();
	SSettingsSection BuildTClientHudCacheSection();
	std::vector<SSettingsSection> BuildTClientLeftCacheSections();
	std::vector<SSettingsSection> BuildTClientRightCacheSections();
	void ConfigureSettingsCardSection(SSettingsSection &Section, const char *pTitle, const char *pStableCardId, std::function<float(CUIRect &, bool)> LayoutSection, float TopMargin);
	float RenderTClientCacheSectionFallback(CUIRect &CurrentColumn, float TopMargin, float (CMenus::*pLayoutSection)(CUIRect &, bool));
	void ConfigureSplitCachedStaticLayer(SSettingsSection &Section, const char *pTitle, std::function<float(CUIRect &)> MeasureSection, std::function<float(CUIRect &)> RenderInteractiveSection, float TopMargin);
	void BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab);
	void BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab);
	float LayoutTClientThemeCacheSection(CUIRect &CurrentColumn, bool Render);
	float LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render);
	float LayoutTClientPetCacheSection(CUIRect &CurrentColumn, bool Render);
	float LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render);
	float RenderTClientHudInteractiveLayer(CUIRect &CurrentColumn);
	int DoTClientSettingsButton_CheckBox(const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect);
	int DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float VMargin);
	int DoTClientSettingsButton_Menu(CButtonContainer *pButtonContainer, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Flags = BUTTONFLAG_LEFT, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f);
	void InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason Reason = ESettingsCacheDirtyReason::CONFIG);
	bool PrewarmSettingsPageResources(int Page, int Tab, const CUIRect &ContentView);
	bool PrewarmSettingsAssetResources();
	void RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly = false);
	void RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly);
	void RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly);
	void RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly);
	void RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly);
	void RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly = false);
	void RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly = false);
	void RenderSettingsTClientSidebar(CUIRect MainView);
	void RenderSettingsQmClient(CUIRect MainView, bool ContributorsPage = false, bool PrewarmOnly = false);
	void RenderSettingsGlobalSearch(CUIRect MainView, bool PrewarmOnly = false);
	void RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly = false);
	void RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly);
	void RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly);
	void RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly);
	void RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly);
	void RenderQmSettingsSliderWithValueInput(const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix, bool PrewarmOnly, unsigned Flags = 0u);
	bool RenderQmFunctionCheckbox(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, bool PrewarmOnly);
	bool IsQmNewFeatureRead(const char *pId) const;
	void MarkQmNewFeatureRead(const char *pId);
	void MarkQmNewFeatureHovered(const char *pId, const CUIRect &Rect, bool PrewarmOnly);
	bool RenderQmVisualCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue);
	void RenderQmVisualLabel(const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign = TEXTALIGN_ML, const SLabelProperties &LabelProps = {});
	void RenderQmVisualStreamerContent(CUIRect &Content, float LineHeight, float LineSpacing);
	void RenderQmVisualTranslateUiContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing);
	void RenderQmVisualEntityOverlayContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmVisualCollisionHitboxContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmVisualWeaponAnimationContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly);
	void RenderQmVisualChatBubbleContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmVisualSkinTransitionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmVisualFocusModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth);
	void RenderQmVisualCameraViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	bool RenderQmHudCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue);
	bool HandleQmHudCheckboxInput(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue);
	void RenderQmHudLabel(const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign = TEXTALIGN_ML, const SLabelProperties &LabelProps = {});
	void RenderQmHudKeyBindRow(CUIRect &Content, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pLabel, const char *pCommand, float LineHeight, float BodySize, float LineSpacing, float LabelWidth);
	void RenderQmFunctionKeyBindsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth);
	void RenderQmFunctionGoresActorContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionGoresContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionJumpHintContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionWeaponTrajectoryContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionFriendNotifyContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionMiniFeaturesContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly);
	void RenderQmFunctionBlockWordsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionKeywordReplyContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionTranslateContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmFunctionPieMenuContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CornerRadius, bool PrewarmOnly);
	void RenderQmFunctionFavoriteMapsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly);
	void RenderQmFunctionHJAssistContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudSpeedrunTimerContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudBindStatusContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudDebugGraphContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudDebugModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudInputOverlayContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudDummyMiniViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool Expanded, bool PrewarmOnly);
	void RenderQmHudDynamicIslandContent(CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle);
	void RenderQmHudSystemMediaControlsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly);
	void RenderQmHudLyricsContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly);
	void RenderQmHudNotificationsBasicContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudNotificationsAdvancedContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudPlayerStatsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudCoordsContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudVoiceContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
	void RenderQmHudBackground3DContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
	void RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly = false);
	void RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha = 1.0f);

	const CWarType *m_pRemoveWarType = nullptr;
	void PopupConfirmRemoveWarType();
	void RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute,
		ColorRGBA ColorFeet = ColorRGBA(0, 0, 0, 0), ColorRGBA ColorBody = ColorRGBA(0, 0, 0, 0));
	void RenderFontIcon(CUIRect Rect, const char *pText, float Size, int Align);
	int DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners = IGraphics::CORNER_ALL);

	ColorHSLA RenderHSLColorPicker(const CUIRect *pRect, unsigned int *pColor, bool Alpha);
	bool RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight, const SSettingsContentMetrics &Metrics);
	int DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake = false, const char *pImageName = nullptr, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, float FontFactor = 0.0f, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), float FontSize = -1.0f);
};
#endif
