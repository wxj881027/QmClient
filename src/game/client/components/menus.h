/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_H
#define GAME_CLIENT_COMPONENTS_MENUS_H

#include <base/types.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/demo.h>
#include <engine/friends.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/textrender.h>

#include <game/client/component.h>
#include <game/client/components/community_icons.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/menus_ingame_touch_controls.h>
#include <game/client/components/menus_settings_controls.h>
#include <game/client/components/menus_start.h>
#include <game/client/components/skins7.h>
#include <game/client/components/qmclient/warlist.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/voting.h>

#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <vector>

class CImageInfo;
struct CDataSprite;

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	NUMBER_OF_ASSETS_TABS = 6,
};

class CUIRect;
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
	int DoButton_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, unsigned Flags = BUTTONFLAG_LEFT, const char *pImageName = nullptr, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, float FontFactor = 0.0f, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	int DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator = nullptr, const ColorRGBA *pDefaultColor = nullptr, const ColorRGBA *pActiveColor = nullptr, const ColorRGBA *pHoverColor = nullptr, float EdgeRounding = 10.0f, const CCommunityIcon *pCommunityIcon = nullptr);

	int DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, unsigned Flags);
	int DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect);
	int DoButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pText, int *pValue, CUIRect *pRect, float VMargin);
	int DoButton_CheckBox_Number(const void *pId, const char *pText, int Checked, const CUIRect *pRect);

	bool DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags = 0u, const char *pSuffix = "");
	bool DoEditBoxWithLabel(CLineInput *LineInput, const CUIRect *pRect, const char *pLabel, const char *pDefault, char *pBuf, size_t BufSize);
	bool DoLine_RadioMenu(CUIRect &View, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value);
	bool DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand);

private:
	uint64_t UiAnimNodeKey(const char *pScope, uint64_t Id = 0) const;
	void TriggerUiSwitchAnimation(uint64_t NodeKey, float DurationSec = 0.18f);
	float ReadUiSwitchAnimation(uint64_t NodeKey) const;
	float UiSwitchAnimationAlpha(float Strength) const;
	float ApplyUiSwitchOffset(CUIRect &View, float Strength, float Direction, bool Vertical, float RelativeOffset, float MinOffset, float MaxOffset) const;
	float ResolveMenuTabAnimationValue(const void *pButtonId, bool Active, float DurationSec = 0.10f) const;

	CUi::SColorPickerPopupContext m_ColorPickerPopupContext;
	ColorHSLA DoLine_ColorPicker(CButtonContainer *pResetId, float LineSize, float LabelSize, float BottomMargin, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, ColorRGBA DefaultColor, bool CheckBoxSpacing = true, int *pCheckBoxValue = nullptr, bool Alpha = false);
	ColorHSLA DoButton_ColorPicker(const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha);

	void DoLaserPreview(const CUIRect *pRect, ColorHSLA OutlineColor, ColorHSLA InnerColor, int LaserType);
	int DoButton_GridHeader(const void *pId, const char *pText, int Checked, const CUIRect *pRect, int Align = TEXTALIGN_ML);
	int DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect);

	bool m_SkinListScrollToSelected = false;
	std::optional<std::chrono::nanoseconds> m_SkinList7LastRefreshTime;
	std::optional<std::chrono::nanoseconds> m_SkinPartsList7LastRefreshTime;

	int m_DirectionQuadContainerIndex;

	// menus_settings_assets.cpp
public:
	// Async asset loading states
	enum EAssetLoadState
	{
		ASSET_LOAD_STATE_UNLOADED = 0,
		ASSET_LOAD_STATE_LOADING,
		ASSET_LOAD_STATE_LOADED,
	};

private:
	EAssetLoadState m_aAssetLoadStates[NUMBER_OF_ASSETS_TABS] = {
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
		ASSET_LOAD_STATE_UNLOADED,
	};
	std::shared_ptr<IJob> m_apAssetLoadJobs[NUMBER_OF_ASSETS_TABS];

public:
	struct SCustomItem
	{
		IGraphics::CTextureHandle m_RenderTexture;

		char m_aName[50];
		std::shared_ptr<IJob> m_pDecodeJob;

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

	enum
	{
		ASSETS_EDITOR_TYPE_GAME = 0,
		ASSETS_EDITOR_TYPE_EMOTICONS,
		ASSETS_EDITOR_TYPE_ENTITIES,
		ASSETS_EDITOR_TYPE_HUD,
		ASSETS_EDITOR_TYPE_PARTICLES,
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
		char m_aFamilyKey[64] = {0};
		char m_aSourceAsset[64] = {0};
	};

protected:
	std::vector<SCustomEntities> m_vEntitiesList;
	std::vector<SCustomGame> m_vGameList;
	std::vector<SCustomEmoticon> m_vEmoticonList;
	std::vector<SCustomParticle> m_vParticlesList;
	std::vector<SCustomHud> m_vHudList;
	std::vector<SCustomExtras> m_vExtrasList;

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
	void AssetsEditorOpen(int Type);

private:
	struct SAssetsEditorState
	{
		bool m_Open = false;
		bool m_Initialized = false;
		int m_Type = ASSETS_EDITOR_TYPE_GAME;
		int m_aMainAssetIndex[ASSETS_EDITOR_TYPE_COUNT] = {0};
		int m_aDonorAssetIndex[ASSETS_EDITOR_TYPE_COUNT] = {0};
		bool m_ShowGrid = true;
		bool m_ApplySameSize = false;
		bool m_DragActive = false;
		int m_ActiveDraggedSlotIndex = -1;
		char m_aDraggedSourceAsset[64] = {0};
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

	SAssetsEditorState m_AssetsEditorState;
	void RenderAssetsEditorScreen(CUIRect MainView);
	void AssetsEditorClearAssets();
	void AssetsEditorReloadAssets();
	void AssetsEditorReloadAssetsImagesOnly();
	void AssetsEditorResetPartSlots();
	void AssetsEditorEnsureDefaultExportNames();
	void AssetsEditorSyncExportNameFromType();
	void AssetsEditorCommitExportNameForType();
	void AssetsEditorValidateRequiredSlotsForType(int Type);
	bool AssetsEditorComposeImage(CImageInfo &OutputImage);
	bool AssetsEditorExport();
	void AssetsEditorRenderCanvas(const CUIRect &Rect, IGraphics::CTextureHandle Texture, int W, int H, int Type, bool ShowGrid, int HighlightSlot);
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

	bool m_JoinTutorial = false;
	bool m_CreateDefaultFavoriteCommunities = false;
	bool m_ForceRefreshLanPage = false;
	float m_MenuPageTransitionDirection = 0.0f;
	float m_GamePageTransitionDirection = 0.0f;
	float m_BrowserTabTransitionDirection = 0.0f;

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
	bool m_NeedSendinfo;
	bool m_NeedSendDummyinfo;
	int m_SettingPlayerPage;

	// 0.7 skins
	bool m_CustomSkinMenu = false;
	int m_TeePartSelected = protocol7::SKINPART_BODY;
	const CSkins7::CSkin *m_pSelectedSkin = nullptr;
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
	int m_CallvoteSelectedOption;
	int m_CallvoteSelectedPlayer;
	CLineInputBuffered<VOTE_REASON_LENGTH> m_CallvoteReasonInput;
	CLineInputBuffered<64> m_FilterInput;
	bool m_ControlPageOpening;

	// demo
	enum
	{
		SORT_DEMONAME = 0,
		SORT_MARKERS,
		SORT_LENGTH,
		SORT_DATE,
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

		bool m_InfosLoaded;
		bool m_Valid;
		CDemoHeader m_Info;
		CTimelineMarkers m_TimelineMarkers;
		CMapInfo m_MapInfo;

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
			if(!str_comp(m_aFilename, ".."))
				return true;
			if(!str_comp(Other.m_aFilename, ".."))
				return false;
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

	char m_aCurrentDemoFolder[IO_MAX_PATH_LENGTH];
	char m_aCurrentDemoSelectionName[IO_MAX_PATH_LENGTH];
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoRenameInput;
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoSliceInput;
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoSearchInput;
#if defined(CONF_VIDEORECORDER)
	CLineInputBuffered<IO_MAX_PATH_LENGTH> m_DemoRenderInput;
#endif
	int m_DemolistSelectedIndex;
	bool m_DemolistSelectedReveal = false;
	int m_DemolistStorageType;
	bool m_DemolistMultipleStorages = false;
	std::vector<SDemoSelectionEntry> m_vDemoSelection;
	std::vector<SDemoDeleteTarget> m_vDemoDeleteTargets;
	int m_DemoSelectionAnchorIndex = -1;
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
	void DemolistOnUpdate(bool Reset);
	static int DemolistFetchCallback(const char *pName, int IsDir, int StorageType, void *pUser);
	bool EnsureDemoDate(CDemoItem &Item);
	void EnsureAllDemoDates();

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
	std::string m_FriendsCategoryExpandedStateCache;
	bool m_FriendsCategoryExpandedLoaded = false;
	std::vector<std::string> m_vFriendTooltipText;
	int m_FriendAddCategoryIndex = 0;
	CUi::SDropDownState m_FriendsAddCategoryDropDownState;
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
		FRIEND_ACTION_REMOVE,
	};
	CUi::SSelectionPopupContext m_FriendsActionPopupContext;
	std::vector<EFriendAction> m_vFriendsActionEntries;
	bool m_HasFriendAction = false;
	char m_aFriendActionName[MAX_NAME_LENGTH] = {0};
	char m_aFriendActionClan[MAX_CLAN_LENGTH] = {0};
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
	void RenderPlayers(CUIRect MainView);
	void RenderServerInfo(CUIRect MainView);
	void RenderServerInfoMotd(CUIRect Motd);
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
	void Connect(const char *pAddress);
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
	void RenderServerbrowserInfo(CUIRect View);
	void RenderServerbrowserInfoScoreboard(CUIRect View, const CServerInfo *pSelectedServer);
	void RenderServerbrowserFriends(CUIRect View);
	void RenderServerbrowserQm(CUIRect View);
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
	void RenderServerbrowser(CUIRect MainView);
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
	bool RenderLanguageSelection(CUIRect MainView);
	void RenderThemeSelection(CUIRect MainView);
	void RenderSettingsGeneral(CUIRect MainView);
	void RenderSettingsPlayer(CUIRect MainView);
	void RenderSettingsTee(CUIRect MainView);
	void RenderSettingsTee7(CUIRect MainView);
	void RenderSettingsTeeCustom7(CUIRect MainView);
	void RenderSkinSelection7(CUIRect MainView);
	void RenderSkinPartSelection7(CUIRect MainView);
	void RenderSettingsGraphics(CUIRect MainView);
	void RenderSettingsSound(CUIRect MainView);
	void RenderSettings(CUIRect MainView);
	void PrewarmSettingsPages();
	void RenderSettingsCustom(CUIRect MainView);

	// found in menus_settings_controls.cpp
	// TODO: Change PopupConfirm to avoid using a function pointer to a CMenus
	//       member function, to move this function to CMenusSettingsControls
	void ResetSettingsControls();

	std::vector<CButtonContainer> m_vButtonContainersNamePlateShow = {{}, {}, {}, {}};
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
		bool m_IsDirectory;
	};
	class CPopupMapPickerContext
	{
	public:
		std::vector<CMapListItem> m_vMaps;
		char m_aCurrentMapFolder[IO_MAX_PATH_LENGTH] = "";
		static int MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
		void MapListPopulate();
		CMenus *m_pMenus;
		int m_Selection;
	};

	static bool CompareFilenameAscending(const CMapListItem Lhs, const CMapListItem Rhs)
	{
		if(str_comp(Lhs.m_aFilename, "..") == 0)
			return true;
		if(str_comp(Rhs.m_aFilename, "..") == 0)
			return false;
		if(Lhs.m_IsDirectory != Rhs.m_IsDirectory)
			return Lhs.m_IsDirectory;
		return str_comp_filenames(Lhs.m_aFilename, Rhs.m_aFilename) < 0;
	}

	static CUi::EPopupMenuFunctionResult PopupSkinQueuePresetRename(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMapPicker(void *pContext, CUIRect View, bool Active);

	void SetNeedSendInfo();
	void UpdateColors();

	IGraphics::CTextureHandle m_TextureBlob;

public:
	void RenderBackground();

	CMenus();
	int Sizeof() const override { return sizeof(*this); }

	void RenderLoading(const char *pCaption, const char *pContent, int IncreaseCounter);
	void FinishLoading();

	bool IsInit() const { return m_IsInit; }

	bool IsActive() const { return m_MenuActive; }
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
		SETTINGS_PROFILES,
		SETTINGS_CONFIGS,
		SETTINGS_CONTRIBUTORS,

		SETTINGS_LENGTH,
	};

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

	SUIAnimator m_aAnimatorsBigPage[BIG_TAB_LENGTH];
	SUIAnimator m_aAnimatorsSmallPage[SMALL_TAB_LENGTH];
	SUIAnimator m_aAnimatorsSettingsTab[SETTINGS_LENGTH];
	int m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;

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

private:
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
	void RenderSettingsTClient(CUIRect MainView);
	void RenderSettingsTClientSettings(CUIRect MainView);
	void RenderSettingsTClientBindWheel(CUIRect MainView);
	void RenderSettingsTClientChatBinds(CUIRect MainView);
	void RenderSettingsTClientWarList(CUIRect MainView);
	void RenderSettingsTClientInfo(CUIRect MainView);
	void RenderSettingsTClientStatusBar(CUIRect MainView);
	void RenderSettingsTClientProfiles(CUIRect MainView);
	void RenderSettingsTClientConfigs(CUIRect MainView);
	void RenderSettingsTClientSidebar(CUIRect MainView);
	void RenderSettingsQmClient(CUIRect MainView, bool ContributorsPage = false);
	void RenderSettingsQmClientOverview(CUIRect MainView);
	void PrewarmTClientAndQmClientPages();
	void RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha = 1.0f);

	const CWarType *m_pRemoveWarType = nullptr;
	void PopupConfirmRemoveWarType();
	void RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute,
		ColorRGBA ColorFeet = ColorRGBA(0, 0, 0, 0), ColorRGBA ColorBody = ColorRGBA(0, 0, 0, 0));
	void RenderFontIcon(CUIRect Rect, const char *pText, float Size, int Align);
	int DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners = IGraphics::CORNER_ALL);

	ColorHSLA RenderHSLColorPicker(const CUIRect *pRect, unsigned int *pColor, bool Alpha);
	bool RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight);
	int DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake = false, const char *pImageName = nullptr, int Corners = IGraphics::CORNER_ALL, float Rounding = 5.0f, float FontFactor = 0.0f, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
};
#endif
