/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "editor.h"
#include "editor_actions.h"

#include <base/color.h>
#include <base/fs.h>

#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui_scrollregion.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>

#include <algorithm>
#include <array>
#include <limits>

using namespace FontIcons;

CUi::EPopupMenuFunctionResult CEditor::CPopupMapTab::Render(void *pContext, CUIRect View, bool Active)
{
	CPopupMapTab *pPopupMapTab = static_cast<CPopupMapTab *>(pContext);
	CEditor *pEditor = pPopupMapTab->m_pEditor;
	if(!pEditor)
		return CUi::POPUP_CLOSE_CURRENT;

	auto MapIt = std::find_if(pEditor->m_vpMaps.begin(), pEditor->m_vpMaps.end(), [pPopupMapTab](const auto &pMap) {
		return pMap.get() == pPopupMapTab->m_pSelectedMap;
	});
	if(MapIt == pEditor->m_vpMaps.end())
		return CUi::POPUP_CLOSE_CURRENT;
	const size_t SelectedMapIndex = MapIt - pEditor->m_vpMaps.begin();
	CEditorMap *pSelectedMap = MapIt->get();
	const bool Saving = pEditor->IsSaving(pSelectedMap);
	const bool Saved = pSelectedMap->m_aFilename[0] != '\0';

	CUIRect Slot;
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&pPopupMapTab->m_CloseButtonId, "Close", Saving ? -1 : 0, &Slot, BUTTONFLAG_LEFT, "Close this map."))
	{
		pEditor->CloseMap(SelectedMapIndex, true);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&pPopupMapTab->m_CopyNameButtonId, "Copy name", Saved ? 0 : -1, &Slot, BUTTONFLAG_LEFT, "Copy the name of this map to the clipboard."))
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		fs_split_file_extension(fs_filename(pSelectedMap->m_aFilename), aFilename, sizeof(aFilename));
		pEditor->Input()->SetClipboardText(aFilename);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&pPopupMapTab->m_CopyPathButtonId, "Copy path", Saved ? 0 : -1, &Slot, BUTTONFLAG_LEFT, "Copy the path of this map to the clipboard."))
	{
		pEditor->Input()->SetClipboardText(pSelectedMap->m_aFilename);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&pPopupMapTab->m_ShowFileButtonId, "Show file", Saved ? 0 : -1, &Slot, BUTTONFLAG_LEFT, "Show this map in the file browser."))
	{
		bool FoundMap = false;
		for(int CheckStorageType = IStorage::TYPE_SAVE; CheckStorageType < pEditor->Storage()->NumPaths(); ++CheckStorageType)
		{
			if(!pEditor->Storage()->FileExists(pSelectedMap->m_aFilename, CheckStorageType))
				continue;

			char aParentDirectory[IO_MAX_PATH_LENGTH];
			str_copy(aParentDirectory, pSelectedMap->m_aFilename);
			if(fs_parent_dir(aParentDirectory) != 0)
			{
				pEditor->ShowFileDialogError("Failed to determine parent folder for map file '%s'.", pSelectedMap->m_aFilename);
				FoundMap = true;
				break;
			}
			char aCompletePath[IO_MAX_PATH_LENGTH];
			pEditor->Storage()->GetCompletePath(CheckStorageType, aParentDirectory, aCompletePath, sizeof(aCompletePath));
			if(!pEditor->Client()->ViewFile(aCompletePath))
				pEditor->ShowFileDialogError("Failed to open the folder '%s'.", aCompletePath);
			FoundMap = true;
			break;
		}
		if(!FoundMap)
			pEditor->ShowFileDialogError("The map file '%s' could not be found.", pSelectedMap->m_aFilename);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

namespace
{
	using SQuadPointArray = std::array<CPoint, 5>;

	static const char *const CURVE_TYPE_NAMES[] = {
		Localizable("Step", "Editor"),
		Localizable("Linear", "Editor"),
		Localizable("Slow", "Editor explanation speed"),
		Localizable("Fast", "Editor explanation speed"),
		Localizable("Smooth", "Editor"),
		Localizable("Bezier", "Editor")};
	static const char *const CURVE_TYPE_CONTEXTS[] = {
		"Editor",
		"Editor",
		"Editor explanation speed",
		"Editor explanation speed",
		"Editor",
		"Editor"};
	static_assert(std::size(CURVE_TYPE_NAMES) == NUM_CURVETYPES);
	static_assert(std::size(CURVE_TYPE_CONTEXTS) == NUM_CURVETYPES);

	SQuadPointArray QuadPoints(const CQuad *pQuad)
	{
		SQuadPointArray aPoints;
		std::copy(std::begin(pQuad->m_aPoints), std::end(pQuad->m_aPoints), aPoints.begin());
		return aPoints;
	}

	void ScaleQuadAroundPivot(CQuad *pQuad, const SQuadPointArray &aOriginalPoints, int ScalePercent)
	{
		const float Scale = ScalePercent / 100.0f;
		const CPoint Pivot = aOriginalPoints[4];
		for(int PointIndex = 0; PointIndex < 4; ++PointIndex)
		{
			pQuad->m_aPoints[PointIndex].x = Pivot.x + f2fx(fx2f(aOriginalPoints[PointIndex].x - Pivot.x) * Scale);
			pQuad->m_aPoints[PointIndex].y = Pivot.y + f2fx(fx2f(aOriginalPoints[PointIndex].y - Pivot.y) * Scale);
		}
		pQuad->m_aPoints[4] = Pivot;
	}
}

CUi::EPopupMenuFunctionResult CEditor::PopupMenuFile(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	static int s_NewMapButton = 0;
	static int s_SaveButton = 0;
	static int s_SaveAsButton = 0;
	static int s_SaveCopyButton = 0;
	static int s_OpenButton = 0;
	static int s_OpenCurrentMapButton = 0;
	static int s_AppendButton = 0;
	static int s_TestMapLocallyButton = 0;
	static int s_ExitButton = 0;

	CUIRect Slot;
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_NewMapButton, Localize("New", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("[Ctrl+N] Create a new map.", "Editor")))
	{
		pEditor->AddDefaultMap();
		pEditor->Reset(false);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_OpenButton, Localize("Load", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("[Ctrl+L] Open a map for editing.", "Editor")))
	{
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::MAP, Localize("Load map", "Editor"), Localize("Load", "Editor"), "maps", "", CallbackOpenMap, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_OpenCurrentMapButton, pEditor->m_QuickActionLoadCurrentMap.Label(), pEditor->m_QuickActionLoadCurrentMap.Disabled() ? -1 : 0, &Slot, BUTTONFLAG_LEFT, pEditor->m_QuickActionLoadCurrentMap.Description()))
	{
		pEditor->m_QuickActionLoadCurrentMap.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_AppendButton, Localize("Append", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("[Ctrl+A] Open a map and add everything from that map to the current one.", "Editor")))
	{
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::MAP, Localize("Append map", "Editor"), Localize("Append", "Editor"), "maps", "", CallbackAppendMap, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_SaveButton, Localize("Save", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("[Ctrl+S] Save the current map.", "Editor")))
	{
		if(pEditor->Map()->m_aFilename[0] != '\0' && pEditor->Map()->m_ValidSaveFilename)
		{
			CallbackSaveMap(pEditor->Map()->m_aFilename, IStorage::TYPE_SAVE, pEditor);
		}
		else
		{
			pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, Localize("Save map", "Editor"), Localize("Save", "Editor"), "maps", "", CallbackSaveMap, pEditor);
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_SaveAsButton, pEditor->m_QuickActionSaveAs.Label(), 0, &Slot, BUTTONFLAG_LEFT, pEditor->m_QuickActionSaveAs.Description()))
	{
		pEditor->m_QuickActionSaveAs.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_SaveCopyButton, Localize("Save copy", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("[Ctrl+Shift+Alt+S] Save a copy of the current map under a new name.", "Editor")))
	{
		char aDefaultName[IO_MAX_PATH_LENGTH];
		fs_split_file_extension(fs_filename(pEditor->Map()->m_aFilename), aDefaultName, sizeof(aDefaultName));
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, Localize("Save map", "Editor"), Localize("Save copy", "Editor"), "maps", aDefaultName, CallbackSaveCopyMap, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&pEditor->m_QuickActionMapDetails, pEditor->m_QuickActionMapDetails.Label(), 0, &Slot, BUTTONFLAG_LEFT, pEditor->m_QuickActionMapDetails.Description()))
	{
		pEditor->m_QuickActionMapDetails.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_TestMapLocallyButton, pEditor->m_QuickActionTestMapLocally.Label(), 0, &Slot, BUTTONFLAG_LEFT, pEditor->m_QuickActionTestMapLocally.Description()))
	{
		pEditor->m_QuickActionTestMapLocally.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(10.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_ExitButton, Localize("Exit", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("[Escape] Exit from the editor.", "Editor")))
	{
		if(pEditor->HasUnsavedData())
		{
			pEditor->m_PopupEventType = POPEVENT_EXIT;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->OnClose();
			g_Config.m_ClEditor = 0;
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupMenuTools(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	CUIRect Slot;
	View.HSplitTop(12.0f, &Slot, &View);
	static int s_RemoveUnusedEnvelopesButton = 0;
	static CUi::SConfirmPopupContext s_ConfirmPopupContext;
	if(pEditor->DoButton_MenuItem(&s_RemoveUnusedEnvelopesButton, Localize("Remove unused envelopes", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Remove all unused envelopes from the map.", "Editor")))
	{
		s_ConfirmPopupContext.Reset();
		s_ConfirmPopupContext.YesNoButtons();
		str_copy(s_ConfirmPopupContext.m_aMessage, Localize("Are you sure that you want to remove all unused envelopes from this map?", "Editor"));
		pEditor->Ui()->ShowPopupConfirm(Slot.x + Slot.w, Slot.y, &s_ConfirmPopupContext);
	}
	if(s_ConfirmPopupContext.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		pEditor->Map()->RemoveUnusedEnvelopes();
	if(s_ConfirmPopupContext.m_Result != CUi::SConfirmPopupContext::UNSET)
	{
		s_ConfirmPopupContext.Reset();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	static int s_BorderButton = 0;
	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_BorderButton, Localize("Place border", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Place tiles in a 2-tile wide border at the edges of the selected tile layer.", "Editor")))
	{
		std::shared_ptr<CLayerTiles> pT = std::static_pointer_cast<CLayerTiles>(pEditor->Map()->SelectedLayerType(0, LAYERTYPE_TILES));
		if(pT && !pT->m_HasTele && !pT->m_HasSpeedup && !pT->m_HasSwitch && !pT->m_HasFront && !pT->m_HasTune)
		{
			pEditor->m_PopupEventType = POPEVENT_PLACE_BORDER_TILES;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->ShowFileDialogError(Localize("No tile layer selected", "Editor"));
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&pEditor->m_QuickActionGotoPosition, pEditor->m_QuickActionGotoPosition.Label(), 0, &Slot, BUTTONFLAG_LEFT, pEditor->m_QuickActionGotoPosition.Description()))
	{
		pEditor->m_QuickActionGotoPosition.Call();
	}

	static int s_TileartButton = 0;
	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_TileartButton, Localize("Add tileart", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Generate tileart from image.", "Editor")))
	{
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, Localize("Add tileart", "Editor"), Localize("Open", "Editor"), "mapres", "", CallbackAddTileart, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	static int s_QuadArtButton = 0;
	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_QuadArtButton, Localize("Add quadart", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Generate quadart from image.", "Editor")))
	{
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, Localize("Add quadart", "Editor"), Localize("Open", "Editor"), "mapres", "", CallbackAddQuadArt, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupCollab(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	CUIRect Slot;
	View.Margin(8.0f, &View);

	View.HSplitTop(16.0f, &Slot, &View);
	pEditor->Ui()->DoLabel(&Slot, Localize("Collaboration", "Editor"), 14.0f, TEXTALIGN_ML);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(28.0f, &Slot, &View);
	SLabelProperties TextProps;
	TextProps.m_MaxWidth = Slot.w;
	pEditor->Ui()->DoLabel(&Slot, Localize("Sync the current editor map snapshot by room code, with up to 4 people collaborating.", "Editor"), 10.0f, TEXTALIGN_ML, TextProps);

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Slot, &View);
	char aRoomStatus[64];
	if(pEditor->m_aCollabRoomCode[0] != '\0')
		str_format(aRoomStatus, sizeof(aRoomStatus), Localize("Current room: %s (%d/%d people)", "Editor"), pEditor->m_aCollabRoomCode, pEditor->m_CollabMemberCount, pEditor->m_CollabMaxMembers);
	else
		str_copy(aRoomStatus, Localize("Not currently in a collaboration room", "Editor"));
	pEditor->Ui()->DoLabel(&Slot, aRoomStatus, 10.0f, TEXTALIGN_ML, TextProps);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(20.0f, &Slot, &View);
	CUIRect Label, Input, Copy;
	Slot.VSplitLeft(48.0f, &Label, &Slot);
	Slot.VSplitRight(64.0f, &Input, &Copy);
	Slot.VSplitRight(6.0f, &Input, nullptr);
	pEditor->Ui()->DoLabel(&Label, Localize("Room code", "Editor"), 10.0f, TEXTALIGN_ML);
	pEditor->DoEditBox(&pEditor->m_CollabRoomInput, &Input, 10.0f, IGraphics::CORNER_ALL, Localize("Enter a room code to join a collaboration room.", "Editor"));
	static int s_CopyRoomCodeButton = 0;
	if(pEditor->DoButton_Editor(&s_CopyRoomCodeButton, Localize("Duplicate", "Editor"), pEditor->m_aCollabRoomCode[0] == '\0' ? -1 : 0, &Copy, BUTTONFLAG_LEFT, Localize("Copy the current collaboration room code.", "Editor")))
	{
		pEditor->Input()->SetClipboardText(pEditor->m_aCollabRoomCode);
		pEditor->SetCollabStatus(Localizable("Room code copied", "Editor"));
	}

	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(22.0f, &Slot, &View);
	CUIRect Create, Join, Leave;
	Slot.VSplitLeft(82.0f, &Create, &Slot);
	Slot.VSplitLeft(6.0f, nullptr, &Slot);
	Slot.VSplitLeft(82.0f, &Join, &Slot);
	Slot.VSplitLeft(6.0f, nullptr, &Slot);
	Slot.VSplitLeft(82.0f, &Leave, &Slot);

	const bool Disconnected = pEditor->m_CollabState == ECollabState::DISCONNECTED;
	static int s_CreateRoomButton = 0;
	if(pEditor->DoButton_Editor(&s_CreateRoomButton, Localize("Create room", "Editor"), Disconnected ? 0 : -1, &Create, BUTTONFLAG_LEFT, Localize("Create a collaboration room for up to 4 people.", "Editor")))
		pEditor->CreateCollabRoom();

	static int s_JoinRoomButton = 0;
	if(pEditor->DoButton_Editor(&s_JoinRoomButton, Localize("Join room", "Editor"), Disconnected ? 0 : -1, &Join, BUTTONFLAG_LEFT, Localize("Use a room code to join a collaboration room.", "Editor")))
		pEditor->JoinCollabRoom();

	static int s_LeaveRoomButton = 0;
	if(pEditor->DoButton_Editor(&s_LeaveRoomButton, Localize("Leave room", "Editor"), Disconnected ? -1 : 0, &Leave, BUTTONFLAG_LEFT, Localize("Leave the current collaboration room.", "Editor")))
		pEditor->LeaveCollabRoom();

	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(36.0f, &Slot, &View);
	pEditor->Ui()->DoLabel(&Slot, pEditor->m_aCollabStatus, 10.0f, TEXTALIGN_ML, TextProps);

	return CUi::POPUP_KEEP_OPEN;
}

static int EntitiesListdirCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	if(!IsDir && str_endswith(pName, ".png"))
	{
		std::string Name = pName;
		pEditor->m_vSelectEntitiesFiles.push_back(Name.substr(0, Name.length() - 4));
	}

	return 0;
}

static const char *EntitiesDisplayName(const char *pName)
{
	if(str_comp_nocase(pName, "DDNet") == 0)
		return Localize("Official", "Editor");
	if(str_comp_nocase(pName, "FNG") == 0)
		return Localize("Freeze grenade", "Editor");
	if(str_comp_nocase(pName, "Race") == 0)
		return Localize("Race", "Editor");
	if(str_comp_nocase(pName, "Vanilla") == 0)
		return Localize("Vanilla", "Editor");
	if(str_comp_nocase(pName, "blockworlds") == 0)
		return Localize("Blockworlds", "Editor");
	return pName;
}

CUi::EPopupMenuFunctionResult CEditor::PopupMenuSettings(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	CUIRect Slot;
	View.HSplitTop(12.0f, &Slot, &View);
	static int s_EntitiesButtonId = 0;
	char aButtonText[64];
	str_format(aButtonText, sizeof(aButtonText), Localize("Entities: %s", "Editor"), EntitiesDisplayName(pEditor->m_SelectEntitiesImage.c_str()));
	if(pEditor->DoButton_MenuItem(&s_EntitiesButtonId, aButtonText, 0, &Slot, BUTTONFLAG_LEFT, Localize("Choose game layer entities image for different gametypes.", "Editor")))
	{
		pEditor->m_vSelectEntitiesFiles.clear();
		pEditor->Storage()->ListDirectory(IStorage::TYPE_ALL, "editor/entities", EntitiesListdirCallback, pEditor);
		std::sort(pEditor->m_vSelectEntitiesFiles.begin(), pEditor->m_vSelectEntitiesFiles.end());
		pEditor->m_vSelectEntitiesFiles.emplace_back(Localize("Custom…", "Editor"));

		static SPopupMenuId s_PopupEntitiesId;
		pEditor->Ui()->DoPopupMenu(&s_PopupEntitiesId, Slot.x, Slot.y + Slot.h, 250, pEditor->m_vSelectEntitiesFiles.size() * 14.0f + 10.0f, pEditor, PopupEntities);
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Brush coloring", "Editor"), 10.0f, TEXTALIGN_ML);
		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !pEditor->m_BrushColorEnabled, &No, BUTTONFLAG_LEFT, Localize("Disable brush coloring.", "Editor"), IGraphics::CORNER_L))
		{
			pEditor->m_BrushColorEnabled = false;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), pEditor->m_BrushColorEnabled, &Yes, BUTTONFLAG_LEFT, Localize("Enable brush coloring.", "Editor"), IGraphics::CORNER_R))
		{
			pEditor->m_BrushColorEnabled = true;
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Allow unused", "Editor"), 10.0f, TEXTALIGN_ML);
		if(pEditor->m_AllowPlaceUnusedTiles != EUnusedEntities::ALLOWED_IMPLICIT)
		{
			static int s_ButtonNo = 0;
			static int s_ButtonYes = 0;
			if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), pEditor->m_AllowPlaceUnusedTiles == EUnusedEntities::NOT_ALLOWED, &No, BUTTONFLAG_LEFT, Localize("[Ctrl+U] Disallow placing unused tiles.", "Editor"), IGraphics::CORNER_L))
			{
				pEditor->m_AllowPlaceUnusedTiles = EUnusedEntities::NOT_ALLOWED;
			}
			if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), pEditor->m_AllowPlaceUnusedTiles == EUnusedEntities::ALLOWED_EXPLICIT, &Yes, BUTTONFLAG_LEFT, Localize("[Ctrl+U] Allow placing unused tiles.", "Editor"), IGraphics::CORNER_R))
			{
				pEditor->m_AllowPlaceUnusedTiles = EUnusedEntities::ALLOWED_EXPLICIT;
			}
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect Off, Dec, Hex;
		Selector.VSplitLeft(Selector.w / 3.0f, &Off, &Selector);
		Selector.VSplitMid(&Dec, &Hex);

		pEditor->Ui()->DoLabel(&Label, Localize("Show info", "Editor"), 10.0f, TEXTALIGN_ML);
		static int s_ButtonOff = 0;
		static int s_ButtonDec = 0;
		static int s_ButtonHex = 0;
		CQuickAction *pAction = &pEditor->m_QuickActionShowInfoOff;
		if(pEditor->DoButton_Ex(&s_ButtonOff, pAction->LabelShort(), pAction->Active(), &Off, BUTTONFLAG_LEFT, pAction->Description(), IGraphics::CORNER_L))
		{
			pAction->Call();
		}
		pAction = &pEditor->m_QuickActionShowInfoDec;
		if(pEditor->DoButton_Ex(&s_ButtonDec, pAction->LabelShort(), pAction->Active(), &Dec, BUTTONFLAG_LEFT, pAction->Description(), IGraphics::CORNER_NONE))
		{
			pAction->Call();
		}
		pAction = &pEditor->m_QuickActionShowInfoHex;
		if(pEditor->DoButton_Ex(&s_ButtonHex, pAction->LabelShort(), pAction->Active(), &Hex, BUTTONFLAG_LEFT, pAction->Description(), IGraphics::CORNER_R))
		{
			pAction->Call();
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Preview quad envelopes", "Editor"), 10.0f, TEXTALIGN_ML);

		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !pEditor->m_ShowEnvelopePreview, &No, BUTTONFLAG_LEFT, Localize("Do not preview the paths of quads with a position envelope when a quad layer is selected.", "Editor"), IGraphics::CORNER_L))
		{
			pEditor->m_ShowEnvelopePreview = false;
			pEditor->m_ActiveEnvelopePreview = EEnvelopePreview::NONE;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), pEditor->m_ShowEnvelopePreview, &Yes, BUTTONFLAG_LEFT, Localize("Preview the paths of quads with a position envelope when a quad layer is selected.", "Editor"), IGraphics::CORNER_R))
		{
			pEditor->m_ShowEnvelopePreview = true;
			pEditor->m_ActiveEnvelopePreview = EEnvelopePreview::NONE;
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Align quads", "Editor"), 10.0f, TEXTALIGN_ML);

		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !g_Config.m_EdAlignQuads, &No, BUTTONFLAG_LEFT, Localize("Do not perform quad alignment to other quads/points when moving quads.", "Editor"), IGraphics::CORNER_L))
		{
			g_Config.m_EdAlignQuads = false;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), g_Config.m_EdAlignQuads, &Yes, BUTTONFLAG_LEFT, Localize("Allow quad alignment to other quads/points when moving quads.", "Editor"), IGraphics::CORNER_R))
		{
			g_Config.m_EdAlignQuads = true;
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Show quads bounds", "Editor"), 10.0f, TEXTALIGN_ML);

		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !g_Config.m_EdShowQuadsRect, &No, BUTTONFLAG_LEFT, Localize("Do not show quad bounds when moving quads.", "Editor"), IGraphics::CORNER_L))
		{
			g_Config.m_EdShowQuadsRect = false;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), g_Config.m_EdShowQuadsRect, &Yes, BUTTONFLAG_LEFT, Localize("Show quad bounds when moving quads.", "Editor"), IGraphics::CORNER_R))
		{
			g_Config.m_EdShowQuadsRect = true;
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Auto map reload", "Editor"), 10.0f, TEXTALIGN_ML);

		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !g_Config.m_EdAutoMapReload, &No, BUTTONFLAG_LEFT, Localize("Do not run 'hot_reload' on the local server while rcon authed on map save.", "Editor"), IGraphics::CORNER_L))
		{
			g_Config.m_EdAutoMapReload = false;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), g_Config.m_EdAutoMapReload, &Yes, BUTTONFLAG_LEFT, Localize("Run 'hot_reload' on the local server while rcon authed on map save.", "Editor"), IGraphics::CORNER_R))
		{
			g_Config.m_EdAutoMapReload = true;
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Select layers by tile", "Editor"), 10.0f, TEXTALIGN_ML);

		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !g_Config.m_EdLayerSelector, &No, BUTTONFLAG_LEFT, Localize("Do not select layers when ctrl+right clicking on a tile.", "Editor"), IGraphics::CORNER_L))
		{
			g_Config.m_EdLayerSelector = false;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), g_Config.m_EdLayerSelector, &Yes, BUTTONFLAG_LEFT, Localize("Select layers when ctrl+right clicking on a tile.", "Editor"), IGraphics::CORNER_R))
		{
			g_Config.m_EdLayerSelector = true;
		}
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	{
		Slot.VMargin(5.0f, &Slot);

		CUIRect Label, Selector;
		Slot.VSplitMid(&Label, &Selector);
		CUIRect No, Yes;
		Selector.VSplitMid(&No, &Yes);

		pEditor->Ui()->DoLabel(&Label, Localize("Show ingame entities", "Editor"), 10.0f, TEXTALIGN_ML);

		static int s_ButtonNo = 0;
		static int s_ButtonYes = 0;
		if(pEditor->DoButton_Ex(&s_ButtonNo, Localize("No", "Editor"), !g_Config.m_EdShowIngameEntities, &No, BUTTONFLAG_LEFT, Localize("Do not show how weapons, shields, snowflakes and flags appear ingame.", "Editor"), IGraphics::CORNER_L))
		{
			g_Config.m_EdShowIngameEntities = false;
		}
		if(pEditor->DoButton_Ex(&s_ButtonYes, Localize("Yes", "Editor"), g_Config.m_EdShowIngameEntities, &Yes, BUTTONFLAG_LEFT, Localize("Show how weapons, shields, snowflakes and flags appear ingame.", "Editor"), IGraphics::CORNER_R))
		{
			g_Config.m_EdShowIngameEntities = true;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupGroup(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	// remove group button
	CUIRect Button;
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_DeleteButton = 0;

	// don't allow deletion of game group
	if(pEditor->Map()->m_pGameGroup != pEditor->Map()->SelectedGroup())
	{
		if(pEditor->DoButton_Editor(&s_DeleteButton, Localize("Delete group", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Delete the group.", "Editor")))
		{
			pEditor->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, true));
			pEditor->Map()->DeleteGroup(pEditor->Map()->m_SelectedGroup);
			pEditor->Map()->m_SelectedGroup = maximum(0, pEditor->Map()->m_SelectedGroup - 1);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else
	{
		if(pEditor->DoButton_Editor(&s_DeleteButton, Localize("Clean up game tiles", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Remove game tiles that aren't based on a layer.", "Editor")))
		{
			// gather all tile layers
			std::vector<std::shared_ptr<CLayerTiles>> vpLayers;
			int GameLayerIndex = -1;
			for(int LayerIndex = 0; LayerIndex < (int)pEditor->Map()->m_pGameGroup->m_vpLayers.size(); LayerIndex++)
			{
				auto &pLayer = pEditor->Map()->m_pGameGroup->m_vpLayers.at(LayerIndex);
				if(pLayer != pEditor->Map()->m_pGameLayer && pLayer->m_Type == LAYERTYPE_TILES)
					vpLayers.push_back(std::static_pointer_cast<CLayerTiles>(pLayer));
				else if(pLayer == pEditor->Map()->m_pGameLayer)
					GameLayerIndex = LayerIndex;
			}

			// search for unneeded game tiles
			std::shared_ptr<CLayerTiles> pGameLayer = pEditor->Map()->m_pGameLayer;
			for(int y = 0; y < pGameLayer->m_Height; ++y)
			{
				for(int x = 0; x < pGameLayer->m_Width; ++x)
				{
					if(pGameLayer->m_pTiles[y * pGameLayer->m_Width + x].m_Index > static_cast<unsigned char>(TILE_NOHOOK))
						continue;

					bool Found = false;
					for(const auto &pLayer : vpLayers)
					{
						if(x < pLayer->m_Width && y < pLayer->m_Height && pLayer->m_pTiles[y * pLayer->m_Width + x].m_Index)
						{
							Found = true;
							break;
						}
					}

					CTile Tile = pGameLayer->GetTile(x, y);
					if(!Found && Tile.m_Index != TILE_AIR)
					{
						Tile.m_Index = TILE_AIR;
						pGameLayer->SetTile(x, y, Tile);
						pEditor->Map()->OnModify();
					}
				}
			}

			if(!pGameLayer->m_TilesHistory.empty())
			{
				if(GameLayerIndex == -1)
				{
					dbg_msg("editor", "failed to record action (GameLayerIndex not found)");
				}
				else
				{
					// record undo
					pEditor->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionTileChanges>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, GameLayerIndex, Localize("Clean up game tiles", "Editor"), pGameLayer->m_TilesHistory));
				}
				pGameLayer->ClearHistory();
			}

			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pTeleLayer)
	{
		// new tele layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddTeleLayer, pEditor->m_QuickActionAddTeleLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddTeleLayer.Description()))
		{
			pEditor->m_QuickActionAddTeleLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pSpeedupLayer)
	{
		// new speedup layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddSpeedupLayer, pEditor->m_QuickActionAddSpeedupLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddSpeedupLayer.Description()))
		{
			pEditor->m_QuickActionAddSpeedupLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pTuneLayer)
	{
		// new tune layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddTuneLayer, pEditor->m_QuickActionAddTuneLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddTuneLayer.Description()))
		{
			pEditor->m_QuickActionAddTuneLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pFrontLayer)
	{
		// new front layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddFrontLayer, pEditor->m_QuickActionAddFrontLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddFrontLayer.Description()))
		{
			pEditor->m_QuickActionAddFrontLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pSwitchLayer)
	{
		// new Switch layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddSwitchLayer, pEditor->m_QuickActionAddSwitchLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddSwitchLayer.Description()))
		{
			pEditor->m_QuickActionAddSwitchLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// new quad layer
	View.HSplitBottom(5.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddQuadsLayer, pEditor->m_QuickActionAddQuadsLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddQuadsLayer.Description()))
	{
		pEditor->m_QuickActionAddQuadsLayer.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// new tile layer
	View.HSplitBottom(5.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddTileLayer, pEditor->m_QuickActionAddTileLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddTileLayer.Description()))
	{
		pEditor->m_QuickActionAddTileLayer.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// new sound layer
	View.HSplitBottom(5.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddSoundLayer, pEditor->m_QuickActionAddSoundLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddSoundLayer.Description()))
	{
		pEditor->m_QuickActionAddSoundLayer.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// group name
	if(!pEditor->Map()->SelectedGroup()->m_GameGroup)
	{
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Button);
		pEditor->Ui()->DoLabel(&Button, Localize("Name:", "Editor property label"), 10.0f, TEXTALIGN_ML);
		Button.VSplitLeft(40.0f, nullptr, &Button);
		static CLineInput s_NameInput;
		s_NameInput.SetBuffer(pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_aName, sizeof(pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_aName));
		if(pEditor->DoEditBox(&s_NameInput, &Button, 10.0f))
			pEditor->Map()->OnModify();
	}

	CProperty aProps[] = {
		{Localize("Order", "Editor"), pEditor->Map()->m_SelectedGroup, PROPTYPE_INT, 0, (int)pEditor->Map()->m_vpGroups.size() - 1},
		{Localize("Pos X", "Editor"), -pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetX, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Pos Y", "Editor"), -pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetY, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Para X", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxX, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Para Y", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxY, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Use Clipping", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_UseClipping, PROPTYPE_BOOL, 0, 1},
		{Localize("Clip X", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipX, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Clip Y", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipY, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Clip W", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipW, PROPTYPE_INT, 0, 1000000},
		{Localize("Clip H", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipH, PROPTYPE_INT, 0, 1000000},
		{nullptr},
	};

	// cut the properties that aren't needed
	if(pEditor->Map()->SelectedGroup()->m_GameGroup)
		aProps[(int)EGroupProp::PROP_POS_X].m_pName = nullptr;

	static int s_aIds[(int)EGroupProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<EGroupProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != EGroupProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->OnModify();
	}

	pEditor->Map()->m_LayerGroupPropTracker.Begin(pEditor->Map()->SelectedGroup().get(), Prop, State);

	if(Prop == EGroupProp::PROP_ORDER)
	{
		pEditor->Map()->m_SelectedGroup = pEditor->Map()->MoveGroup(pEditor->Map()->m_SelectedGroup, NewVal);
	}

	// these can not be changed on the game group
	if(!pEditor->Map()->SelectedGroup()->m_GameGroup)
	{
		if(Prop == EGroupProp::PROP_PARA_X)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxX = NewVal;
		}
		else if(Prop == EGroupProp::PROP_PARA_Y)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxY = NewVal;
		}
		else if(Prop == EGroupProp::PROP_POS_X)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetX = -NewVal;
		}
		else if(Prop == EGroupProp::PROP_POS_Y)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetY = -NewVal;
		}
		else if(Prop == EGroupProp::PROP_USE_CLIPPING)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_UseClipping = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_X)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipX = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_Y)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipY = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_W)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipW = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_H)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipH = NewVal;
		}
	}

	pEditor->Map()->m_LayerGroupPropTracker.End(Prop, State);

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupLayer(void *pContext, CUIRect View, bool Active)
{
	SLayerPopupContext *pPopup = (SLayerPopupContext *)pContext;
	CEditor *pEditor = pPopup->m_pEditor;

	std::shared_ptr<CLayerGroup> pCurrentGroup = pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup];
	std::shared_ptr<CLayer> pCurrentLayer = pEditor->Map()->SelectedLayer(0);

	if(!pCurrentLayer || !pCurrentGroup)
		return CUi::POPUP_CLOSE_CURRENT;

	if(pPopup->m_vpLayers.size() > 1)
	{
		return CLayerTiles::RenderCommonProperties(pPopup->m_CommonPropState, pEditor->Map(), &View, pPopup->m_vpLayers, pPopup->m_vLayerIndices);
	}

	const bool EntitiesLayer = pCurrentLayer->IsEntitiesLayer();

	// delete button
	if(pEditor->Map()->m_pGameLayer != pCurrentLayer) // entities layers except the game layer can be deleted
	{
		CUIRect DeleteButton;
		View.HSplitBottom(12.0f, &View, &DeleteButton);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionDeleteLayer, pEditor->m_QuickActionDeleteLayer.Label(), 0, &DeleteButton, BUTTONFLAG_LEFT, pEditor->m_QuickActionDeleteLayer.Description()))
		{
			pEditor->m_QuickActionDeleteLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// duplicate button
	if(!EntitiesLayer) // entities layers cannot be duplicated
	{
		CUIRect DuplicateButton;
		View.HSplitBottom(4.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &DuplicateButton);
		static int s_DuplicationButton = 0;
		if(pEditor->DoButton_Editor(&s_DuplicationButton, Localize("Duplicate layer", "Editor"), 0, &DuplicateButton, BUTTONFLAG_LEFT, Localize("Create an identical copy of the selected layer.", "Editor")))
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->DuplicateLayer(pEditor->Map()->m_vSelectedLayers[0]);
			pEditor->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, pEditor->Map()->m_vSelectedLayers[0] + 1, true));
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// layer name
	if(!EntitiesLayer) // name cannot be changed for entities layers
	{
		CUIRect Label, EditBox;
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(12.0f, &View, &Label);
		Label.VSplitLeft(40.0f, &Label, &EditBox);
		pEditor->Ui()->DoLabel(&Label, Localize("Name:", "Editor property label"), 10.0f, TEXTALIGN_ML);
		static CLineInput s_NameInput;
		s_NameInput.SetBuffer(pCurrentLayer->m_aName, sizeof(pCurrentLayer->m_aName));
		if(pEditor->DoEditBox(&s_NameInput, &EditBox, 10.0f))
			pEditor->Map()->OnModify();
	}

	// spacing if any button was rendered
	if(!EntitiesLayer || pEditor->Map()->m_pGameLayer != pCurrentLayer)
		View.HSplitBottom(10.0f, &View, nullptr);

	CProperty aProps[] = {
		{Localize("Group", "Editor"), pEditor->Map()->m_SelectedGroup, PROPTYPE_INT, 0, (int)pEditor->Map()->m_vpGroups.size() - 1},
		{Localize("Order", "Editor"), pEditor->Map()->m_vSelectedLayers[0], PROPTYPE_INT, 0, (int)pCurrentGroup->m_vpLayers.size() - 1},
		{Localize("Detail", "Editor"), pCurrentLayer->m_Flags & LAYERFLAG_DETAIL, PROPTYPE_BOOL, 0, 1},
		{nullptr},
	};

	// don't use Group and Detail from the selection if this is an entities layer
	if(EntitiesLayer)
	{
		aProps[0].m_Type = PROPTYPE_NULL;
		aProps[2].m_Type = PROPTYPE_NULL;
	}

	static int s_aIds[(int)ELayerProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<ELayerProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != ELayerProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->OnModify();
	}

	pEditor->Map()->m_LayerPropTracker.Begin(pCurrentLayer.get(), Prop, State);

	if(Prop == ELayerProp::PROP_ORDER)
	{
		pEditor->Map()->SelectLayer(pCurrentGroup->MoveLayer(pEditor->Map()->m_vSelectedLayers[0], NewVal));
	}
	else if(Prop == ELayerProp::PROP_GROUP)
	{
		if(NewVal >= 0 && (size_t)NewVal < pEditor->Map()->m_vpGroups.size() && NewVal != pEditor->Map()->m_SelectedGroup)
		{
			auto Position = std::find(pCurrentGroup->m_vpLayers.begin(), pCurrentGroup->m_vpLayers.end(), pCurrentLayer);
			if(Position != pCurrentGroup->m_vpLayers.end())
				pCurrentGroup->m_vpLayers.erase(Position);
			pEditor->Map()->m_vpGroups[NewVal]->m_vpLayers.push_back(pCurrentLayer);
			pEditor->Map()->m_SelectedGroup = NewVal;
			pEditor->Map()->SelectLayer(pEditor->Map()->m_vpGroups[NewVal]->m_vpLayers.size() - 1);
		}
	}
	else if(Prop == ELayerProp::PROP_HQ)
	{
		pCurrentLayer->m_Flags &= ~LAYERFLAG_DETAIL;
		if(NewVal)
			pCurrentLayer->m_Flags |= LAYERFLAG_DETAIL;
	}

	pEditor->Map()->m_LayerPropTracker.End(Prop, State);

	return pCurrentLayer->RenderProperties(&View);
}

CUi::EPopupMenuFunctionResult CEditor::PopupQuad(void *pContext, CUIRect View, bool Active)
{
	CQuadPopupContext *pQuadPopupContext = static_cast<CQuadPopupContext *>(pContext);
	CEditor *pEditor = pQuadPopupContext->m_pEditor;
	std::vector<CQuad *> vpQuads = pEditor->Map()->SelectedQuads();
	if(!in_range<int>(pQuadPopupContext->m_SelectedQuadIndex, 0, vpQuads.size() - 1))
		return CUi::POPUP_CLOSE_CURRENT;
	CQuad *pCurrentQuad = vpQuads[pQuadPopupContext->m_SelectedQuadIndex];
	std::shared_ptr<CLayerQuads> pLayer = std::static_pointer_cast<CLayerQuads>(pEditor->Map()->SelectedLayerType(0, LAYERTYPE_QUADS));

	CUIRect Button;

	// delete button
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_DeleteButton = 0;
	if(pEditor->DoButton_Editor(&s_DeleteButton, Localize("Delete", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Delete the current quad.", "Editor")))
	{
		if(pLayer)
		{
			pEditor->Map()->OnModify();
			pEditor->Map()->DeleteSelectedQuads();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// aspect ratio button
	View.HSplitBottom(10.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	if(pLayer && pLayer->m_Image >= 0 && (size_t)pLayer->m_Image < pEditor->Map()->m_vpImages.size())
	{
		static int s_AspectRatioButton = 0;
		if(pEditor->DoButton_Editor(&s_AspectRatioButton, Localize("Aspect ratio", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Resize the current quad based on the aspect ratio of its image.", "Editor")))
		{
			pEditor->Map()->m_QuadTracker.BeginQuadTrack(pLayer, pEditor->Map()->m_vSelectedQuads);
			for(auto &pQuad : vpQuads)
			{
				int Top = pQuad->m_aPoints[0].y;
				int Left = pQuad->m_aPoints[0].x;
				int Right = pQuad->m_aPoints[0].x;

				for(int k = 1; k < 4; k++)
				{
					if(pQuad->m_aPoints[k].y < Top)
						Top = pQuad->m_aPoints[k].y;
					if(pQuad->m_aPoints[k].x < Left)
						Left = pQuad->m_aPoints[k].x;
					if(pQuad->m_aPoints[k].x > Right)
						Right = pQuad->m_aPoints[k].x;
				}

				const int Height = (Right - Left) * pEditor->Map()->m_vpImages[pLayer->m_Image]->m_Height / pEditor->Map()->m_vpImages[pLayer->m_Image]->m_Width;

				pQuad->m_aPoints[0].x = Left;
				pQuad->m_aPoints[0].y = Top;
				pQuad->m_aPoints[1].x = Right;
				pQuad->m_aPoints[1].y = Top;
				pQuad->m_aPoints[2].x = Left;
				pQuad->m_aPoints[2].y = Top + Height;
				pQuad->m_aPoints[3].x = Right;
				pQuad->m_aPoints[3].y = Top + Height;
				pEditor->Map()->OnModify();
			}
			pEditor->Map()->m_QuadTracker.EndQuadTrack();

			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// center pivot button
	View.HSplitBottom(6.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_CenterButton = 0;
	if(pEditor->DoButton_Editor(&s_CenterButton, Localize("Center pivot", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Center the pivot of the current quad.", "Editor")))
	{
		pEditor->Map()->m_QuadTracker.BeginQuadTrack(pLayer, pEditor->Map()->m_vSelectedQuads);
		int Top = pCurrentQuad->m_aPoints[0].y;
		int Left = pCurrentQuad->m_aPoints[0].x;
		int Bottom = pCurrentQuad->m_aPoints[0].y;
		int Right = pCurrentQuad->m_aPoints[0].x;

		for(int k = 1; k < 4; k++)
		{
			if(pCurrentQuad->m_aPoints[k].y < Top)
				Top = pCurrentQuad->m_aPoints[k].y;
			if(pCurrentQuad->m_aPoints[k].x < Left)
				Left = pCurrentQuad->m_aPoints[k].x;
			if(pCurrentQuad->m_aPoints[k].y > Bottom)
				Bottom = pCurrentQuad->m_aPoints[k].y;
			if(pCurrentQuad->m_aPoints[k].x > Right)
				Right = pCurrentQuad->m_aPoints[k].x;
		}

		pCurrentQuad->m_aPoints[4].x = Left + (Right - Left) / 2;
		pCurrentQuad->m_aPoints[4].y = Top + (Bottom - Top) / 2;
		pEditor->Map()->m_QuadTracker.EndQuadTrack();
		pEditor->Map()->OnModify();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// align button
	View.HSplitBottom(6.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_AlignButton = 0;
	if(pEditor->DoButton_Editor(&s_AlignButton, Localize("Align", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Align coordinates of the quad points.", "Editor")))
	{
		pEditor->Map()->m_QuadTracker.BeginQuadTrack(pLayer, pEditor->Map()->m_vSelectedQuads);
		for(auto &pQuad : vpQuads)
		{
			for(int k = 1; k < 4; k++)
			{
				pQuad->m_aPoints[k].x = 1000.0f * (pQuad->m_aPoints[k].x / 1000);
				pQuad->m_aPoints[k].y = 1000.0f * (pQuad->m_aPoints[k].y / 1000);
			}
			pEditor->Map()->OnModify();
		}
		pEditor->Map()->m_QuadTracker.EndQuadTrack();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// square button
	View.HSplitBottom(6.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_Button = 0;
	if(pEditor->DoButton_Editor(&s_Button, Localize("Square", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Square the current quad.", "Editor")))
	{
		pEditor->Map()->m_QuadTracker.BeginQuadTrack(pLayer, pEditor->Map()->m_vSelectedQuads);
		for(auto &pQuad : vpQuads)
		{
			int Top = pQuad->m_aPoints[0].y;
			int Left = pQuad->m_aPoints[0].x;
			int Bottom = pQuad->m_aPoints[0].y;
			int Right = pQuad->m_aPoints[0].x;

			for(int k = 1; k < 4; k++)
			{
				if(pQuad->m_aPoints[k].y < Top)
					Top = pQuad->m_aPoints[k].y;
				if(pQuad->m_aPoints[k].x < Left)
					Left = pQuad->m_aPoints[k].x;
				if(pQuad->m_aPoints[k].y > Bottom)
					Bottom = pQuad->m_aPoints[k].y;
				if(pQuad->m_aPoints[k].x > Right)
					Right = pQuad->m_aPoints[k].x;
			}

			pQuad->m_aPoints[0].x = Left;
			pQuad->m_aPoints[0].y = Top;
			pQuad->m_aPoints[1].x = Right;
			pQuad->m_aPoints[1].y = Top;
			pQuad->m_aPoints[2].x = Left;
			pQuad->m_aPoints[2].y = Bottom;
			pQuad->m_aPoints[3].x = Right;
			pQuad->m_aPoints[3].y = Bottom;
			pEditor->Map()->OnModify();
		}
		pEditor->Map()->m_QuadTracker.EndQuadTrack();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// slice button
	View.HSplitBottom(6.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_SliceButton = 0;
	if(pEditor->DoButton_Editor(&s_SliceButton, Localize("Slice", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Enable quad knife mode.", "Editor")))
	{
		pEditor->QuadKnife()->Activate(pQuadPopupContext->m_SelectedQuadIndex);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// proportional scale
	View.HSplitBottom(8.0f, &View, nullptr);
	static int s_ScalePercent = 100;
	CProperty aScaleProps[] = {
		{Localize("Scale %", "Editor"), s_ScalePercent, PROPTYPE_INT, 1, 1000},
		{nullptr},
	};
	static int s_ScaleId = 0;
	static std::vector<SQuadPointArray> s_vOriginalScalePoints;
	int ScalePercent = s_ScalePercent;
	const SEditResult<int> ScaleResult = pEditor->DoPropertiesWithState<int>(&View, aScaleProps, &s_ScaleId, &ScalePercent);
	if(ScaleResult.m_State == EEditState::START || ScaleResult.m_State == EEditState::ONE_GO)
	{
		s_vOriginalScalePoints.clear();
		for(CQuad *pQuad : vpQuads)
			s_vOriginalScalePoints.push_back(QuadPoints(pQuad));
		if(pLayer)
			pEditor->Map()->m_QuadTracker.BeginQuadTrack(pLayer, pEditor->Map()->m_vSelectedQuads);
	}
	if(ScaleResult.m_State != EEditState::NONE && !s_vOriginalScalePoints.empty())
	{
		s_ScalePercent = ScalePercent;
		for(size_t QuadIndex = 0; QuadIndex < vpQuads.size(); ++QuadIndex)
			ScaleQuadAroundPivot(vpQuads[QuadIndex], s_vOriginalScalePoints[QuadIndex], ScalePercent);
	}
	if(ScaleResult.m_State == EEditState::ONE_GO || ScaleResult.m_State == EEditState::END)
	{
		if(pLayer)
			pEditor->Map()->m_QuadTracker.EndQuadTrack();
		pEditor->Map()->OnModify();
		s_vOriginalScalePoints.clear();
		s_ScalePercent = 100;
	}

	const int NumQuads = pLayer ? (int)pLayer->m_vQuads.size() : 0;
	CProperty aProps[] = {
		{Localize("Order", "Editor"), pEditor->Map()->m_vSelectedQuads[pQuadPopupContext->m_SelectedQuadIndex], PROPTYPE_INT, 0, NumQuads},
		{Localize("Pos X", "Editor"), fx2i(pCurrentQuad->m_aPoints[4].x), PROPTYPE_INT, -1000000, 1000000},
		{Localize("Pos Y", "Editor"), fx2i(pCurrentQuad->m_aPoints[4].y), PROPTYPE_INT, -1000000, 1000000},
		{Localize("Pos. Env", "Editor"), pCurrentQuad->m_PosEnv + 1, PROPTYPE_ENVELOPE, 0, 0},
		{Localize("Pos. TO", "Editor"), pCurrentQuad->m_PosEnvOffset, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Color", "Editor"), pQuadPopupContext->m_Color, PROPTYPE_COLOR, 0, 0},
		{Localize("Color Env", "Editor"), pCurrentQuad->m_ColorEnv + 1, PROPTYPE_ENVELOPE, 0, 0},
		{Localize("Color TO", "Editor"), pCurrentQuad->m_ColorEnvOffset, PROPTYPE_INT, -1000000, 1000000},
		{nullptr},
	};

	static int s_aIds[(int)EQuadProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<EQuadProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != EQuadProp::PROP_NONE && (State == EEditState::START || State == EEditState::ONE_GO))
	{
		pEditor->Map()->m_QuadTracker.BeginQuadPropTrack(pLayer, pEditor->Map()->m_vSelectedQuads, Prop);
	}

	const float OffsetX = i2fx(NewVal) - pCurrentQuad->m_aPoints[4].x;
	const float OffsetY = i2fx(NewVal) - pCurrentQuad->m_aPoints[4].y;

	if(Prop == EQuadProp::PROP_ORDER && pLayer)
	{
		const int QuadIndex = pLayer->SwapQuads(pEditor->Map()->m_vSelectedQuads[pQuadPopupContext->m_SelectedQuadIndex], NewVal);
		pEditor->Map()->m_vSelectedQuads[pQuadPopupContext->m_SelectedQuadIndex] = QuadIndex;
	}

	for(auto &pQuad : vpQuads)
	{
		if(Prop == EQuadProp::PROP_POS_X)
		{
			for(auto &Point : pQuad->m_aPoints)
				Point.x += OffsetX;
		}
		else if(Prop == EQuadProp::PROP_POS_Y)
		{
			for(auto &Point : pQuad->m_aPoints)
				Point.y += OffsetY;
		}
		else if(Prop == EQuadProp::PROP_POS_ENV)
		{
			int Index = std::clamp(NewVal - 1, -1, (int)pEditor->Map()->m_vpEnvelopes.size() - 1);
			int StepDirection = Index < pQuad->m_PosEnv ? -1 : 1;
			if(StepDirection != 0)
			{
				for(; Index >= -1 && Index < (int)pEditor->Map()->m_vpEnvelopes.size(); Index += StepDirection)
				{
					if(Index == -1 || pEditor->Map()->m_vpEnvelopes[Index]->GetChannels() == 3)
					{
						pQuad->m_PosEnv = Index;
						break;
					}
				}
			}
		}
		else if(Prop == EQuadProp::PROP_POS_ENV_OFFSET)
		{
			pQuad->m_PosEnvOffset = NewVal;
		}
		else if(Prop == EQuadProp::PROP_COLOR)
		{
			pQuadPopupContext->m_Color = NewVal;
			std::fill(std::begin(pQuad->m_aColors), std::end(pQuad->m_aColors), UnpackColor(NewVal));
		}
		else if(Prop == EQuadProp::PROP_COLOR_ENV)
		{
			int Index = std::clamp(NewVal - 1, -1, (int)pEditor->Map()->m_vpEnvelopes.size() - 1);
			int StepDirection = Index < pQuad->m_ColorEnv ? -1 : 1;
			if(StepDirection != 0)
			{
				for(; Index >= -1 && Index < (int)pEditor->Map()->m_vpEnvelopes.size(); Index += StepDirection)
				{
					if(Index == -1 || pEditor->Map()->m_vpEnvelopes[Index]->GetChannels() == 4)
					{
						pQuad->m_ColorEnv = Index;
						break;
					}
				}
			}
		}
		else if(Prop == EQuadProp::PROP_COLOR_ENV_OFFSET)
		{
			pQuad->m_ColorEnvOffset = NewVal;
		}
	}

	if(Prop != EQuadProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->m_QuadTracker.EndQuadPropTrack(Prop);
		pEditor->Map()->OnModify();
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupSource(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CSoundSource *pSource = pEditor->Map()->SelectedSoundSource();
	if(!pSource)
		return CUi::POPUP_CLOSE_CURRENT;

	CUIRect Button;

	// delete button
	View.HSplitBottom(12.0f, &View, &Button);
	static int s_DeleteButton = 0;
	if(pEditor->DoButton_Editor(&s_DeleteButton, Localize("Delete", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Delete the current sound source.", "Editor")))
	{
		std::shared_ptr<CLayerSounds> pLayer = std::static_pointer_cast<CLayerSounds>(pEditor->Map()->SelectedLayerType(0, LAYERTYPE_SOUNDS));
		if(pLayer)
		{
			pEditor->Map()->m_EditorHistory.Execute(std::make_shared<CEditorActionDeleteSoundSource>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, pEditor->Map()->m_vSelectedLayers[0], pEditor->Map()->m_SelectedSoundSource));
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// Sound shape button
	CUIRect ShapeButton;
	View.HSplitBottom(3.0f, &View, nullptr);
	View.HSplitBottom(12.0f, &View, &ShapeButton);

	const char *s_apShapeNames[CSoundShape::NUM_SHAPES] = {
		Localize("Rectangle", "Editor"),
		Localize("Circle", "Editor")};

	pSource->m_Shape.m_Type = pSource->m_Shape.m_Type % CSoundShape::NUM_SHAPES; // prevent out of array errors

	static int s_ShapeTypeButton = 0;
	if(pEditor->DoButton_Editor(&s_ShapeTypeButton, s_apShapeNames[pSource->m_Shape.m_Type], 0, &ShapeButton, BUTTONFLAG_LEFT, Localize("Change sound source shape.", "Editor")))
	{
		pEditor->Map()->m_EditorHistory.Execute(std::make_shared<CEditorActionEditSoundSourceShape>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, pEditor->Map()->m_vSelectedLayers[0], pEditor->Map()->m_SelectedSoundSource, (pSource->m_Shape.m_Type + 1) % CSoundShape::NUM_SHAPES));
	}

	CProperty aProps[] = {
		{Localize("Pos X", "Editor"), pSource->m_Position.x / 1000, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Pos Y", "Editor"), pSource->m_Position.y / 1000, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Loop", "Editor"), pSource->m_Loop, PROPTYPE_BOOL, 0, 1},
		{Localize("Pan", "Editor"), pSource->m_Pan, PROPTYPE_BOOL, 0, 1},
		{Localize("Delay", "Editor"), pSource->m_TimeDelay, PROPTYPE_INT, 0, 1000000},
		{Localize("Falloff", "Editor"), pSource->m_Falloff, PROPTYPE_INT, 0, 255},
		{Localize("Pos. Env", "Editor"), pSource->m_PosEnv + 1, PROPTYPE_ENVELOPE, 0, 0},
		{Localize("Pos. TO", "Editor"), pSource->m_PosEnvOffset, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Sound Env", "Editor"), pSource->m_SoundEnv + 1, PROPTYPE_ENVELOPE, 0, 0},
		{Localize("Sound. TO", "Editor"), pSource->m_SoundEnvOffset, PROPTYPE_INT, -1000000, 1000000},
		{nullptr},
	};

	static int s_aIds[(int)ESoundProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<ESoundProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != ESoundProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->OnModify();
	}

	pEditor->Map()->m_SoundSourcePropTracker.Begin(pSource, Prop, State);

	if(Prop == ESoundProp::PROP_POS_X)
	{
		pSource->m_Position.x = NewVal * 1000;
	}
	else if(Prop == ESoundProp::PROP_POS_Y)
	{
		pSource->m_Position.y = NewVal * 1000;
	}
	else if(Prop == ESoundProp::PROP_LOOP)
	{
		pSource->m_Loop = NewVal;
	}
	else if(Prop == ESoundProp::PROP_PAN)
	{
		pSource->m_Pan = NewVal;
	}
	else if(Prop == ESoundProp::PROP_TIME_DELAY)
	{
		pSource->m_TimeDelay = NewVal;
	}
	else if(Prop == ESoundProp::PROP_FALLOFF)
	{
		pSource->m_Falloff = NewVal;
	}
	else if(Prop == ESoundProp::PROP_POS_ENV)
	{
		int Index = std::clamp(NewVal - 1, -1, (int)pEditor->Map()->m_vpEnvelopes.size() - 1);
		const int StepDirection = Index < pSource->m_PosEnv ? -1 : 1;
		for(; Index >= -1 && Index < (int)pEditor->Map()->m_vpEnvelopes.size(); Index += StepDirection)
		{
			if(Index == -1 || pEditor->Map()->m_vpEnvelopes[Index]->GetChannels() == 3)
			{
				pSource->m_PosEnv = Index;
				break;
			}
		}
	}
	else if(Prop == ESoundProp::PROP_POS_ENV_OFFSET)
	{
		pSource->m_PosEnvOffset = NewVal;
	}
	else if(Prop == ESoundProp::PROP_SOUND_ENV)
	{
		int Index = std::clamp(NewVal - 1, -1, (int)pEditor->Map()->m_vpEnvelopes.size() - 1);
		const int StepDirection = Index < pSource->m_SoundEnv ? -1 : 1;
		for(; Index >= -1 && Index < (int)pEditor->Map()->m_vpEnvelopes.size(); Index += StepDirection)
		{
			if(Index == -1 || pEditor->Map()->m_vpEnvelopes[Index]->GetChannels() == 1)
			{
				pSource->m_SoundEnv = Index;
				break;
			}
		}
	}
	else if(Prop == ESoundProp::PROP_SOUND_ENV_OFFSET)
	{
		pSource->m_SoundEnvOffset = NewVal;
	}

	pEditor->Map()->m_SoundSourcePropTracker.End(Prop, State);

	// source shape properties
	switch(pSource->m_Shape.m_Type)
	{
	case CSoundShape::SHAPE_CIRCLE:
	{
		CProperty aCircleProps[] = {
			{Localize("Radius", "Editor"), pSource->m_Shape.m_Circle.m_Radius, PROPTYPE_INT, 0, 1000000},
			{nullptr},
		};

		static int s_aCircleIds[(int)ECircleShapeProp::NUM_PROPS] = {0};
		NewVal = 0;
		auto [LocalState, LocalProp] = pEditor->DoPropertiesWithState<ECircleShapeProp>(&View, aCircleProps, s_aCircleIds, &NewVal);
		if(LocalProp != ECircleShapeProp::PROP_NONE && (LocalState == EEditState::END || LocalState == EEditState::ONE_GO))
		{
			pEditor->Map()->OnModify();
		}

		pEditor->Map()->m_SoundSourceCircleShapePropTracker.Begin(pSource, LocalProp, LocalState);

		if(LocalProp == ECircleShapeProp::PROP_CIRCLE_RADIUS)
		{
			pSource->m_Shape.m_Circle.m_Radius = NewVal;
		}

		pEditor->Map()->m_SoundSourceCircleShapePropTracker.End(LocalProp, LocalState);
		break;
	}

	case CSoundShape::SHAPE_RECTANGLE:
	{
		CProperty aRectangleProps[] = {
			{Localize("Width", "Editor"), pSource->m_Shape.m_Rectangle.m_Width / 1024, PROPTYPE_INT, 0, 1000000},
			{Localize("Height", "Editor"), pSource->m_Shape.m_Rectangle.m_Height / 1024, PROPTYPE_INT, 0, 1000000},
			{nullptr},
		};

		static int s_aRectangleIds[(int)ERectangleShapeProp::NUM_PROPS] = {0};
		NewVal = 0;
		auto [LocalState, LocalProp] = pEditor->DoPropertiesWithState<ERectangleShapeProp>(&View, aRectangleProps, s_aRectangleIds, &NewVal);
		if(LocalProp != ERectangleShapeProp::PROP_NONE && (LocalState == EEditState::END || LocalState == EEditState::ONE_GO))
		{
			pEditor->Map()->OnModify();
		}

		pEditor->Map()->m_SoundSourceRectShapePropTracker.Begin(pSource, LocalProp, LocalState);

		if(LocalProp == ERectangleShapeProp::PROP_RECTANGLE_WIDTH)
		{
			pSource->m_Shape.m_Rectangle.m_Width = NewVal * 1024;
		}
		else if(LocalProp == ERectangleShapeProp::PROP_RECTANGLE_HEIGHT)
		{
			pSource->m_Shape.m_Rectangle.m_Height = NewVal * 1024;
		}

		pEditor->Map()->m_SoundSourceRectShapePropTracker.End(LocalProp, LocalState);
		break;
	}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupPoint(void *pContext, CUIRect View, bool Active)
{
	CPointPopupContext *pPointPopupContext = static_cast<CPointPopupContext *>(pContext);
	CEditor *pEditor = pPointPopupContext->m_pEditor;
	std::vector<CQuad *> vpQuads = pEditor->Map()->SelectedQuads();
	if(!in_range<int>(pPointPopupContext->m_SelectedQuadIndex, 0, vpQuads.size() - 1))
		return CUi::POPUP_CLOSE_CURRENT;
	CQuad *pCurrentQuad = vpQuads[pPointPopupContext->m_SelectedQuadIndex];
	std::shared_ptr<CLayerQuads> pLayer = std::static_pointer_cast<CLayerQuads>(pEditor->Map()->SelectedLayerType(0, LAYERTYPE_QUADS));

	const int X = fx2i(pCurrentQuad->m_aPoints[pPointPopupContext->m_SelectedQuadPoint].x);
	const int Y = fx2i(pCurrentQuad->m_aPoints[pPointPopupContext->m_SelectedQuadPoint].y);
	const int TextureU = fx2f(pCurrentQuad->m_aTexcoords[pPointPopupContext->m_SelectedQuadPoint].x) * 1024;
	const int TextureV = fx2f(pCurrentQuad->m_aTexcoords[pPointPopupContext->m_SelectedQuadPoint].y) * 1024;

	CProperty aProps[] = {
		{Localize("Pos X", "Editor"), X, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Pos Y", "Editor"), Y, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Color", "Editor"), PackColor(pCurrentQuad->m_aColors[pPointPopupContext->m_SelectedQuadPoint]), PROPTYPE_COLOR, 0, 0},
		{Localize("Tex U", "Editor"), TextureU, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Tex V", "Editor"), TextureV, PROPTYPE_INT, -1000000, 1000000},
		{nullptr},
	};

	static int s_aIds[(int)EQuadPointProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<EQuadPointProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != EQuadPointProp::PROP_NONE && (State == EEditState::START || State == EEditState::ONE_GO))
	{
		pEditor->Map()->m_QuadTracker.BeginQuadPointPropTrack(pLayer, pEditor->Map()->m_vSelectedQuads, pEditor->Map()->m_SelectedQuadPoints);
		pEditor->Map()->m_QuadTracker.AddQuadPointPropTrack(Prop);
	}

	for(CQuad *pQuad : vpQuads)
	{
		if(Prop == EQuadPointProp::PROP_POS_X)
		{
			for(int v = 0; v < 4; v++)
				if(pEditor->Map()->IsQuadCornerSelected(v))
					pQuad->m_aPoints[v].x = i2fx(fx2i(pQuad->m_aPoints[v].x) + NewVal - X);
		}
		else if(Prop == EQuadPointProp::PROP_POS_Y)
		{
			for(int v = 0; v < 4; v++)
				if(pEditor->Map()->IsQuadCornerSelected(v))
					pQuad->m_aPoints[v].y = i2fx(fx2i(pQuad->m_aPoints[v].y) + NewVal - Y);
		}
		else if(Prop == EQuadPointProp::PROP_COLOR)
		{
			for(int v = 0; v < 4; v++)
			{
				if(pEditor->Map()->IsQuadCornerSelected(v))
				{
					pQuad->m_aColors[v] = UnpackColor(NewVal);
				}
			}
		}
		else if(Prop == EQuadPointProp::PROP_TEX_U)
		{
			for(int v = 0; v < 4; v++)
				if(pEditor->Map()->IsQuadCornerSelected(v))
					pQuad->m_aTexcoords[v].x = f2fx(fx2f(pQuad->m_aTexcoords[v].x) + (NewVal - TextureU) / 1024.0f);
		}
		else if(Prop == EQuadPointProp::PROP_TEX_V)
		{
			for(int v = 0; v < 4; v++)
				if(pEditor->Map()->IsQuadCornerSelected(v))
					pQuad->m_aTexcoords[v].y = f2fx(fx2f(pQuad->m_aTexcoords[v].y) + (NewVal - TextureV) / 1024.0f);
		}
	}

	if(Prop != EQuadPointProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->m_QuadTracker.EndQuadPointPropTrack(Prop);
		pEditor->Map()->OnModify();
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupEnvPoint(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	if(pEditor->Map()->m_SelectedEnvelope < 0 || pEditor->Map()->m_SelectedEnvelope >= (int)pEditor->Map()->m_vpEnvelopes.size())
		return CUi::POPUP_CLOSE_CURRENT;

	const float RowHeight = 12.0f;
	CUIRect Row, Label, EditBox;

	pEditor->m_ActiveEnvelopePreview = EEnvelopePreview::SELECTED;

	std::shared_ptr<CEnvelope> pEnvelope = pEditor->Map()->m_vpEnvelopes[pEditor->Map()->m_SelectedEnvelope];

	if(pEnvelope->GetChannels() == 4 && !pEditor->Map()->IsTangentSelected())
	{
		View.HSplitTop(RowHeight, &Row, &View);
		View.HSplitTop(4.0f, nullptr, &View);
		Row.VSplitLeft(60.0f, &Label, &Row);
		Row.VSplitLeft(10.0f, nullptr, &EditBox);
		pEditor->Ui()->DoLabel(&Label, Localize("Color:", "Editor"), RowHeight - 2.0f, TEXTALIGN_ML);

		const auto SelectedPoint = pEditor->Map()->m_vSelectedEnvelopePoints.front();
		const int SelectedIndex = SelectedPoint.first;
		auto *pValues = pEnvelope->m_vPoints[SelectedIndex].m_aValues;
		const ColorRGBA Color = pEnvelope->m_vPoints[SelectedIndex].ColorValue();
		const auto &&SetColor = [&](ColorRGBA NewColor) {
			if(Color == NewColor && pEditor->m_ColorPickerPopupContext.m_State == EEditState::EDITING)
				return;

			static int s_Values[4];

			if(pEditor->m_ColorPickerPopupContext.m_State == EEditState::START || pEditor->m_ColorPickerPopupContext.m_State == EEditState::ONE_GO)
			{
				for(int Channel = 0; Channel < 4; ++Channel)
					s_Values[Channel] = pValues[Channel];
			}

			pEnvelope->m_vPoints[SelectedIndex].SetColorValue(NewColor);

			if(pEditor->m_ColorPickerPopupContext.m_State == EEditState::END || pEditor->m_ColorPickerPopupContext.m_State == EEditState::ONE_GO)
			{
				std::vector<std::shared_ptr<IEditorAction>> vpActions(4);

				for(int Channel = 0; Channel < 4; ++Channel)
				{
					vpActions[Channel] = std::make_shared<CEditorActionEnvelopeEditPoint>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, Channel, CEditorActionEnvelopeEditPoint::EEditType::VALUE, s_Values[Channel], f2fx(NewColor[Channel]));
				}

				char aDisplay[256];
				str_format(aDisplay, sizeof(aDisplay), Localize("Edit envelope %d point %d color", "Editor"), pEditor->Map()->m_SelectedEnvelope, SelectedIndex);
				pEditor->Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(pEditor->Map(), vpActions, aDisplay));
			}

			pEditor->Map()->m_UpdateEnvPointInfo = true;
			pEditor->Map()->OnModify();
		};
		static char s_ColorPickerButton;
		pEditor->DoColorPickerButton(&s_ColorPickerButton, &EditBox, Color, SetColor);
	}

	static CLineInputNumber s_CurValueInput;
	static CLineInputNumber s_CurTimeInput;

	static float s_CurrentTime = 0;
	static float s_CurrentValue = 0;

	if(pEditor->Map()->m_UpdateEnvPointInfo)
	{
		pEditor->Map()->m_UpdateEnvPointInfo = false;

		const auto &[CurrentTime, CurrentValue] = pEditor->Map()->SelectedEnvelopeTimeAndValue();

		// update displayed text
		s_CurValueInput.SetFloat(fx2f(CurrentValue));
		s_CurTimeInput.SetFloat(CurrentTime.AsSeconds());

		s_CurrentTime = s_CurTimeInput.GetFloat();
		s_CurrentValue = s_CurValueInput.GetFloat();
	}

	View.HSplitTop(RowHeight, &Row, &View);
	Row.VSplitLeft(60.0f, &Label, &Row);
	Row.VSplitLeft(10.0f, nullptr, &EditBox);
	pEditor->Ui()->DoLabel(&Label, Localize("Value:", "Editor"), RowHeight - 2.0f, TEXTALIGN_ML);
	pEditor->DoEditBox(&s_CurValueInput, &EditBox, RowHeight - 2.0f, IGraphics::CORNER_ALL, Localize("The value of the selected envelope point.", "Editor"));

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	Row.VSplitLeft(60.0f, &Label, &Row);
	Row.VSplitLeft(10.0f, nullptr, &EditBox);
	pEditor->Ui()->DoLabel(&Label, Localize("Time (in s):", "Editor"), RowHeight - 2.0f, TEXTALIGN_ML);
	pEditor->DoEditBox(&s_CurTimeInput, &EditBox, RowHeight - 2.0f, IGraphics::CORNER_ALL, Localize("The time of the selected envelope point.", "Editor"));

	if(pEditor->Input()->KeyIsPressed(KEY_RETURN) || pEditor->Input()->KeyIsPressed(KEY_KP_ENTER))
	{
		float CurrentTime = s_CurTimeInput.GetFloat();
		float CurrentValue = s_CurValueInput.GetFloat();
		if(!(absolute(CurrentTime - s_CurrentTime) < 0.0001f && absolute(CurrentValue - s_CurrentValue) < 0.0001f))
		{
			const auto &[OldTime, OldValue] = pEditor->Map()->SelectedEnvelopeTimeAndValue();

			if(pEditor->Map()->IsTangentInSelected())
			{
				auto [SelectedIndex, SelectedChannel] = pEditor->Map()->m_SelectedTangentInPoint;

				pEditor->Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionEditEnvelopePointValue>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, SelectedChannel, CEditorActionEditEnvelopePointValue::EType::TANGENT_IN, OldTime, OldValue, CFixedTime::FromSeconds(CurrentTime), f2fx(CurrentValue)));
				CurrentTime = (pEnvelope->m_vPoints[SelectedIndex].m_Time + pEnvelope->m_vPoints[SelectedIndex].m_Bezier.m_aInTangentDeltaX[SelectedChannel]).AsSeconds();
			}
			else if(pEditor->Map()->IsTangentOutSelected())
			{
				auto [SelectedIndex, SelectedChannel] = pEditor->Map()->m_SelectedTangentOutPoint;

				pEditor->Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionEditEnvelopePointValue>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, SelectedChannel, CEditorActionEditEnvelopePointValue::EType::TANGENT_OUT, OldTime, OldValue, CFixedTime::FromSeconds(CurrentTime), f2fx(CurrentValue)));
				CurrentTime = (pEnvelope->m_vPoints[SelectedIndex].m_Time + pEnvelope->m_vPoints[SelectedIndex].m_Bezier.m_aOutTangentDeltaX[SelectedChannel]).AsSeconds();
			}
			else
			{
				auto [SelectedIndex, SelectedChannel] = pEditor->Map()->m_vSelectedEnvelopePoints.front();
				pEditor->Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionEditEnvelopePointValue>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, SelectedChannel, CEditorActionEditEnvelopePointValue::EType::POINT, OldTime, OldValue, CFixedTime::FromSeconds(CurrentTime), f2fx(CurrentValue)));

				if(SelectedIndex != 0)
				{
					CurrentTime = pEnvelope->m_vPoints[SelectedIndex].m_Time.AsSeconds();
				}
				else
				{
					CurrentTime = 0.0f;
					pEnvelope->m_vPoints[SelectedIndex].m_Time = CFixedTime(0);
				}
			}

			s_CurTimeInput.SetFloat(CFixedTime::FromSeconds(CurrentTime).AsSeconds());
			s_CurValueInput.SetFloat(fx2f(f2fx(CurrentValue)));

			s_CurrentTime = s_CurTimeInput.GetFloat();
			s_CurrentValue = s_CurValueInput.GetFloat();
		}
	}

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	static int s_DeleteButtonId = 0;
	const char *pButtonText = pEditor->Map()->IsTangentSelected() ? Localize("Reset", "Editor") : Localize("Delete", "Editor");
	const char *pTooltip = pEditor->Map()->IsTangentSelected() ? Localize("Reset tangent point to default value.", "Editor") : Localize("Delete current envelope point in all channels.", "Editor");
	if(pEditor->DoButton_Editor(&s_DeleteButtonId, pButtonText, 0, &Row, BUTTONFLAG_LEFT, pTooltip))
	{
		if(pEditor->Map()->IsTangentInSelected())
		{
			auto [SelectedIndex, SelectedChannel] = pEditor->Map()->m_SelectedTangentInPoint;
			const auto &[OldTime, OldValue] = pEditor->Map()->SelectedEnvelopeTimeAndValue();
			pEditor->Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionResetEnvelopePointTangent>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, SelectedChannel, true, OldTime, OldValue));
		}
		else if(pEditor->Map()->IsTangentOutSelected())
		{
			auto [SelectedIndex, SelectedChannel] = pEditor->Map()->m_SelectedTangentOutPoint;
			const auto &[OldTime, OldValue] = pEditor->Map()->SelectedEnvelopeTimeAndValue();
			pEditor->Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionResetEnvelopePointTangent>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, SelectedChannel, false, OldTime, OldValue));
		}
		else
		{
			auto [SelectedIndex, SelectedChannel] = pEditor->Map()->m_vSelectedEnvelopePoints.front();
			pEditor->Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionDeleteEnvelopePoint>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex));
		}

		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupEnvPointMulti(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	const float RowHeight = 12.0f;

	static int s_CurveButtonId = 0;
	CUIRect CurveButton;
	View.HSplitTop(RowHeight, &CurveButton, &View);
	if(pEditor->DoButton_MenuItem(&s_CurveButtonId, Localize("Project onto", "Editor"), 0, &CurveButton, BUTTONFLAG_LEFT, Localize("Project all selected envelopes onto the curve between the first and last selected envelope.", "Editor")))
	{
		static SPopupMenuId s_PopupCurveTypeId;
		pEditor->Ui()->DoPopupMenu(&s_PopupCurveTypeId, pEditor->Ui()->MouseX(), pEditor->Ui()->MouseY(), 80, 80, pEditor, PopupEnvPointCurveType);
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupEnvPointCurveType(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	const float RowHeight = 14.0f;

	int CurveType = -1;

	static int s_ButtonLinearId;
	CUIRect ButtonLinear;
	View.HSplitTop(RowHeight, &ButtonLinear, &View);
	if(pEditor->DoButton_MenuItem(&s_ButtonLinearId, Localize(CURVE_TYPE_NAMES[CURVETYPE_LINEAR], CURVE_TYPE_CONTEXTS[CURVETYPE_LINEAR]), 0, &ButtonLinear))
		CurveType = CURVETYPE_LINEAR;

	static int s_ButtonSlowId;
	CUIRect ButtonSlow;
	View.HSplitTop(RowHeight, &ButtonSlow, &View);
	if(pEditor->DoButton_MenuItem(&s_ButtonSlowId, Localize(CURVE_TYPE_NAMES[CURVETYPE_SLOW], CURVE_TYPE_CONTEXTS[CURVETYPE_SLOW]), 0, &ButtonSlow))
		CurveType = CURVETYPE_SLOW;

	static int s_ButtonFastId;
	CUIRect ButtonFast;
	View.HSplitTop(RowHeight, &ButtonFast, &View);
	if(pEditor->DoButton_MenuItem(&s_ButtonFastId, Localize(CURVE_TYPE_NAMES[CURVETYPE_FAST], CURVE_TYPE_CONTEXTS[CURVETYPE_FAST]), 0, &ButtonFast))
		CurveType = CURVETYPE_FAST;

	static int s_ButtonStepId;
	CUIRect ButtonStep;
	View.HSplitTop(RowHeight, &ButtonStep, &View);
	if(pEditor->DoButton_MenuItem(&s_ButtonStepId, Localize(CURVE_TYPE_NAMES[CURVETYPE_STEP], CURVE_TYPE_CONTEXTS[CURVETYPE_STEP]), 0, &ButtonStep))
		CurveType = CURVETYPE_STEP;

	static int s_ButtonSmoothId;
	CUIRect ButtonSmooth;
	View.HSplitTop(RowHeight, &ButtonSmooth, &View);
	if(pEditor->DoButton_MenuItem(&s_ButtonSmoothId, Localize(CURVE_TYPE_NAMES[CURVETYPE_SMOOTH], CURVE_TYPE_CONTEXTS[CURVETYPE_SMOOTH]), 0, &ButtonSmooth))
		CurveType = CURVETYPE_SMOOTH;

	std::vector<std::shared_ptr<IEditorAction>> vpActions;

	if(CurveType >= 0)
	{
		std::shared_ptr<CEnvelope> pEnvelope = pEditor->Map()->m_vpEnvelopes.at(pEditor->Map()->m_SelectedEnvelope);

		for(int c = 0; c < pEnvelope->GetChannels(); c++)
		{
			int FirstSelectedIndex = pEnvelope->m_vPoints.size();
			int LastSelectedIndex = -1;
			for(auto [SelectedIndex, SelectedChannel] : pEditor->Map()->m_vSelectedEnvelopePoints)
			{
				if(SelectedChannel == c)
				{
					FirstSelectedIndex = minimum(FirstSelectedIndex, SelectedIndex);
					LastSelectedIndex = maximum(LastSelectedIndex, SelectedIndex);
				}
			}

			if(FirstSelectedIndex < (int)pEnvelope->m_vPoints.size() && LastSelectedIndex >= 0 && FirstSelectedIndex != LastSelectedIndex)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-slicing)
				CEnvPoint FirstPoint = pEnvelope->m_vPoints[FirstSelectedIndex];
				// NOLINTNEXTLINE(cppcoreguidelines-slicing)
				CEnvPoint LastPoint = pEnvelope->m_vPoints[LastSelectedIndex];

				CEnvelope HelperEnvelope(1);
				HelperEnvelope.AddPoint(FirstPoint.m_Time, {FirstPoint.m_aValues[c], 0, 0, 0});
				HelperEnvelope.AddPoint(LastPoint.m_Time, {LastPoint.m_aValues[c], 0, 0, 0});
				HelperEnvelope.m_vPoints[0].m_Curvetype = CurveType;

				for(auto [SelectedIndex, SelectedChannel] : pEditor->Map()->m_vSelectedEnvelopePoints)
				{
					if(SelectedChannel == c)
					{
						if(SelectedIndex != FirstSelectedIndex && SelectedIndex != LastSelectedIndex)
						{
							CEnvPoint &CurrentPoint = pEnvelope->m_vPoints[SelectedIndex];
							ColorRGBA Channels = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
							HelperEnvelope.Eval(CurrentPoint.m_Time.AsSeconds(), Channels, 1);
							int PrevValue = CurrentPoint.m_aValues[c];
							CurrentPoint.m_aValues[c] = f2fx(Channels.r);
							vpActions.push_back(std::make_shared<CEditorActionEnvelopeEditPoint>(pEditor->Map(), pEditor->Map()->m_SelectedEnvelope, SelectedIndex, SelectedChannel, CEditorActionEnvelopeEditPoint::EEditType::VALUE, PrevValue, CurrentPoint.m_aValues[c]));
						}
					}
				}
			}
		}

		if(!vpActions.empty())
		{
			pEditor->Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(pEditor->Map(), vpActions, Localize("Project points", "Editor")));
		}

		pEditor->Map()->OnModify();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

static const auto &&gs_ModifyIndexDeleted = [](int DeletedIndex) {
	return [DeletedIndex](int *pIndex) {
		if(*pIndex == DeletedIndex)
			*pIndex = -1;
		else if(*pIndex > DeletedIndex)
			*pIndex = *pIndex - 1;
	};
};

CUi::EPopupMenuFunctionResult CEditor::PopupImage(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	static int s_ExternalButton = 0;
	static int s_ReimportButton = 0;
	static int s_ReplaceButton = 0;
	static int s_RemoveButton = 0;
	static int s_ExportButton = 0;

	const float RowHeight = 12.0f;

	CUIRect Slot;
	View.HSplitTop(RowHeight, &Slot, &View);
	std::shared_ptr<CEditorImage> pImg = pEditor->Map()->SelectedImage();

	if(!pImg->m_External)
	{
		CUIRect Label, EditBox;

		static CLineInput s_RenameInput;

		Slot.VMargin(5.0f, &Slot);
		Slot.VSplitLeft(35.0f, &Label, &Slot);
		Slot.VSplitLeft(RowHeight - 2.0f, nullptr, &EditBox);
		pEditor->Ui()->DoLabel(&Label, Localize("Name:", "Editor property label"), RowHeight - 2.0f, TEXTALIGN_ML);

		s_RenameInput.SetBuffer(pImg->m_aName, sizeof(pImg->m_aName));
		if(pEditor->DoEditBox(&s_RenameInput, &EditBox, RowHeight - 2.0f))
			pEditor->Map()->OnModify();

		View.HSplitTop(5.0f, nullptr, &View);
		View.HSplitTop(RowHeight, &Slot, &View);
	}

	if(pImg->m_External)
	{
		if(pEditor->DoButton_MenuItem(&s_ExternalButton, Localize("Embed", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Embed the image into the map file.", "Editor")))
		{
			if(pImg->m_pData == nullptr)
			{
				pEditor->ShowFileDialogError(Localize("Embedding is not possible because the image could not be loaded.", "Editor"));
				return CUi::POPUP_KEEP_OPEN;
			}
			pImg->m_External = 0;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		View.HSplitTop(5.0f, nullptr, &View);
		View.HSplitTop(RowHeight, &Slot, &View);
	}
	else if(CEditor::IsVanillaImage(pImg->m_aName))
	{
		if(pEditor->DoButton_MenuItem(&s_ExternalButton, Localize("Make external", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Remove the image from the map file.", "Editor")))
		{
			pImg->m_External = 1;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		View.HSplitTop(5.0f, nullptr, &View);
		View.HSplitTop(RowHeight, &Slot, &View);
	}

	static CUi::SSelectionPopupContext s_SelectionPopupContext;
	static CScrollRegion s_SelectionPopupScrollRegion;
	s_SelectionPopupContext.m_pScrollRegion = &s_SelectionPopupScrollRegion;
	if(pEditor->DoButton_MenuItem(&s_ReimportButton, Localize("Re-import", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Re-import the image from the mapres folder.", "Editor")))
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "%s.png", pImg->m_aName);
		s_SelectionPopupContext.Reset();
		std::set<std::string> EntriesSet;
		pEditor->Storage()->FindFiles(aFilename, "mapres", IStorage::TYPE_ALL, &EntriesSet);
		for(const auto &Entry : EntriesSet)
			s_SelectionPopupContext.m_vEntries.push_back(Entry);
		if(s_SelectionPopupContext.m_vEntries.empty())
		{
			pEditor->ShowFileDialogError(Localize("Error: could not find image '%s' in the mapres folder.", "Editor"), aFilename);
		}
		else if(s_SelectionPopupContext.m_vEntries.size() == 1)
		{
			s_SelectionPopupContext.m_pSelection = &s_SelectionPopupContext.m_vEntries.front();
		}
		else
		{
			str_copy(s_SelectionPopupContext.m_aMessage, Localize("Select the wanted image:", "Editor"));
			pEditor->Ui()->ShowPopupSelection(pEditor->Ui()->MouseX(), pEditor->Ui()->MouseY(), &s_SelectionPopupContext);
		}
	}
	if(s_SelectionPopupContext.m_pSelection != nullptr)
	{
		const bool WasExternal = pImg->m_External;
		const bool Result = pEditor->ReplaceImage(s_SelectionPopupContext.m_pSelection->c_str(), IStorage::TYPE_ALL, false);
		pImg->m_External = WasExternal;
		s_SelectionPopupContext.Reset();
		return Result ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
	}

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_ReplaceButton, Localize("Replace", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Replace the image with a new one.", "Editor")))
	{
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, Localize("Replace image", "Editor"), Localize("Replace", "Editor"), "mapres", "", ReplaceImageCallback, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_RemoveButton, Localize("Remove", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Remove the image from the map.", "Editor")))
	{
		if(pEditor->Map()->IsImageUsed(pEditor->Map()->m_SelectedImage))
		{
			pEditor->m_PopupEventType = POPEVENT_REMOVE_USED_IMAGE;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->Map()->m_vpImages.erase(pEditor->Map()->m_vpImages.begin() + pEditor->Map()->m_SelectedImage);
			pEditor->Map()->ModifyImageIndex(gs_ModifyIndexDeleted(pEditor->Map()->m_SelectedImage));
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(!pImg->m_External)
	{
		View.HSplitTop(5.0f, nullptr, &View);
		View.HSplitTop(RowHeight, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_ExportButton, Localize("Export", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Export the image to a separate file.", "Editor")))
		{
			if(pImg->m_pData == nullptr)
			{
				pEditor->ShowFileDialogError(Localize("Exporting is not possible because the image could not be loaded.", "Editor"));
				return CUi::POPUP_KEEP_OPEN;
			}
			pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::IMAGE, Localize("Save image", "Editor"), Localize("Save", "Editor"), "mapres", pImg->m_aName, CallbackSaveImage, pEditor);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupSound(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	static int s_ReimportButton = 0;
	static int s_ReplaceButton = 0;
	static int s_RemoveButton = 0;
	static int s_ExportButton = 0;

	const float RowHeight = 12.0f;

	CUIRect Slot;
	View.HSplitTop(RowHeight, &Slot, &View);
	std::shared_ptr<CEditorSound> pSound = pEditor->Map()->SelectedSound();

	static CUi::SSelectionPopupContext s_SelectionPopupContext;
	static CScrollRegion s_SelectionPopupScrollRegion;
	s_SelectionPopupContext.m_pScrollRegion = &s_SelectionPopupScrollRegion;

	CUIRect Label, EditBox;

	static CLineInput s_RenameInput;

	Slot.VMargin(5.0f, &Slot);
	Slot.VSplitLeft(35.0f, &Label, &Slot);
	Slot.VSplitLeft(RowHeight - 2.0f, nullptr, &EditBox);
	pEditor->Ui()->DoLabel(&Label, Localize("Name:", "Editor property label"), RowHeight - 2.0f, TEXTALIGN_ML);

	s_RenameInput.SetBuffer(pSound->m_aName, sizeof(pSound->m_aName));
	if(pEditor->DoEditBox(&s_RenameInput, &EditBox, RowHeight - 2.0f))
		pEditor->Map()->OnModify();

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Slot, &View);

	if(pEditor->DoButton_MenuItem(&s_ReimportButton, Localize("Re-import", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Re-import the sound from the mapres folder.", "Editor")))
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "%s.opus", pSound->m_aName);
		s_SelectionPopupContext.Reset();
		std::set<std::string> EntriesSet;
		pEditor->Storage()->FindFiles(aFilename, "mapres", IStorage::TYPE_ALL, &EntriesSet);
		for(const auto &Entry : EntriesSet)
			s_SelectionPopupContext.m_vEntries.push_back(Entry);
		if(s_SelectionPopupContext.m_vEntries.empty())
		{
			pEditor->ShowFileDialogError(Localize("Error: could not find sound '%s' in the mapres folder.", "Editor"), aFilename);
		}
		else if(s_SelectionPopupContext.m_vEntries.size() == 1)
		{
			s_SelectionPopupContext.m_pSelection = &s_SelectionPopupContext.m_vEntries.front();
		}
		else
		{
			str_copy(s_SelectionPopupContext.m_aMessage, Localize("Select the wanted sound:", "Editor"));
			pEditor->Ui()->ShowPopupSelection(pEditor->Ui()->MouseX(), pEditor->Ui()->MouseY(), &s_SelectionPopupContext);
		}
	}
	if(s_SelectionPopupContext.m_pSelection != nullptr)
	{
		const bool Result = pEditor->ReplaceSound(s_SelectionPopupContext.m_pSelection->c_str(), IStorage::TYPE_ALL, false);
		s_SelectionPopupContext.Reset();
		return Result ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
	}

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_ReplaceButton, Localize("Replace", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Replace the sound with a new one.", "Editor")))
	{
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::SOUND, Localize("Replace sound", "Editor"), Localize("Replace", "Editor"), "mapres", "", ReplaceSoundCallback, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_RemoveButton, Localize("Remove", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Remove the sound from the map.", "Editor")))
	{
		if(pEditor->Map()->IsSoundUsed(pEditor->Map()->m_SelectedSound))
		{
			pEditor->m_PopupEventType = POPEVENT_REMOVE_USED_SOUND;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->Map()->m_vpSounds.erase(pEditor->Map()->m_vpSounds.begin() + pEditor->Map()->m_SelectedSound);
			pEditor->Map()->ModifySoundIndex(gs_ModifyIndexDeleted(pEditor->Map()->m_SelectedSound));
			pEditor->m_ToolbarPreviewSound = -1;
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_ExportButton, Localize("Export", "Editor"), 0, &Slot, BUTTONFLAG_LEFT, Localize("Export the sound to a separate file.", "Editor")))
	{
		if(pSound->m_pData == nullptr)
		{
			pEditor->ShowFileDialogError(Localize("Exporting is not possible because the sound could not be loaded.", "Editor"));
			return CUi::POPUP_KEEP_OPEN;
		}
		pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::SOUND, Localize("Save sound", "Editor"), Localize("Save", "Editor"), "mapres", pSound->m_aName, CallbackSaveSound, pEditor);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupMapInfo(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	CUIRect Label, ButtonBar, Button;

	View.Margin(10.0f, &View);
	View.HSplitBottom(20.0f, &View, &ButtonBar);

	// title
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, Localize("Map details", "Editor"), 20.0f, TEXTALIGN_MC);
	View.HSplitTop(10.0f, nullptr, &View);

	// author box
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, Localize("Author:", "Editor"), 10.0f, TEXTALIGN_ML);
	Label.VSplitLeft(60.0f, nullptr, &Button);
	Button.HMargin(3.0f, &Button);
	static CLineInput s_AuthorInput;
	s_AuthorInput.SetBuffer(pEditor->Map()->m_MapInfoTmp.m_aAuthor, sizeof(pEditor->Map()->m_MapInfoTmp.m_aAuthor));
	pEditor->DoEditBox(&s_AuthorInput, &Button, 10.0f);

	// version box
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, Localize("Version:", "Editor"), 10.0f, TEXTALIGN_ML);
	Label.VSplitLeft(60.0f, nullptr, &Button);
	Button.HMargin(3.0f, &Button);
	static CLineInput s_VersionInput;
	s_VersionInput.SetBuffer(pEditor->Map()->m_MapInfoTmp.m_aVersion, sizeof(pEditor->Map()->m_MapInfoTmp.m_aVersion));
	pEditor->DoEditBox(&s_VersionInput, &Button, 10.0f);

	// credits box
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, Localize("Credits:", "Editor"), 10.0f, TEXTALIGN_ML);
	Label.VSplitLeft(60.0f, nullptr, &Button);
	Button.HMargin(3.0f, &Button);
	static CLineInput s_CreditsInput;
	s_CreditsInput.SetBuffer(pEditor->Map()->m_MapInfoTmp.m_aCredits, sizeof(pEditor->Map()->m_MapInfoTmp.m_aCredits));
	pEditor->DoEditBox(&s_CreditsInput, &Button, 10.0f);

	// license box
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, Localize("License:", "Editor"), 10.0f, TEXTALIGN_ML);
	Label.VSplitLeft(60.0f, nullptr, &Button);
	Button.HMargin(3.0f, &Button);
	static CLineInput s_LicenseInput;
	s_LicenseInput.SetBuffer(pEditor->Map()->m_MapInfoTmp.m_aLicense, sizeof(pEditor->Map()->m_MapInfoTmp.m_aLicense));
	pEditor->DoEditBox(&s_LicenseInput, &Button, 10.0f);

	// button bar
	ButtonBar.VSplitLeft(110.0f, &Label, &ButtonBar);
	static int s_CancelButton = 0;
	if(pEditor->DoButton_Editor(&s_CancelButton, Localize("Cancel", "Editor"), 0, &Label, BUTTONFLAG_LEFT, nullptr))
		return CUi::POPUP_CLOSE_CURRENT;

	ButtonBar.VSplitRight(110.0f, &ButtonBar, &Label);
	static int s_ConfirmButton = 0;
	if(pEditor->DoButton_Editor(&s_ConfirmButton, Localize("Confirm", "Editor"), 0, &Label, BUTTONFLAG_LEFT, nullptr) || (Active && pEditor->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)))
	{
		bool AuthorDifferent = str_comp(pEditor->Map()->m_MapInfoTmp.m_aAuthor, pEditor->Map()->m_MapInfo.m_aAuthor) != 0;
		bool VersionDifferent = str_comp(pEditor->Map()->m_MapInfoTmp.m_aVersion, pEditor->Map()->m_MapInfo.m_aVersion) != 0;
		bool CreditsDifferent = str_comp(pEditor->Map()->m_MapInfoTmp.m_aCredits, pEditor->Map()->m_MapInfo.m_aCredits) != 0;
		bool LicenseDifferent = str_comp(pEditor->Map()->m_MapInfoTmp.m_aLicense, pEditor->Map()->m_MapInfo.m_aLicense) != 0;

		if(AuthorDifferent || VersionDifferent || CreditsDifferent || LicenseDifferent)
			pEditor->Map()->OnModify();

		pEditor->Map()->m_MapInfo.Copy(pEditor->Map()->m_MapInfoTmp);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupEvent(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	const char *pTitle;
	const char *pMessage;
	char aMessageBuf[128];
	if(pEditor->m_PopupEventType == POPEVENT_EXIT)
	{
		pTitle = Localize("Exit the editor", "Editor");
		pMessage = Localize("The map contains unsaved data, you might want to save it before you exit the editor.\n\nContinue anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_LOAD || pEditor->m_PopupEventType == POPEVENT_LOADCURRENT || pEditor->m_PopupEventType == POPEVENT_LOADDROP)
	{
		pTitle = Localize("Load map", "Editor");
		pMessage = Localize("The map contains unsaved data, you might want to save it before you load a new map.\n\nContinue anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_NEW)
	{
		pTitle = Localize("New map", "Editor");
		pMessage = Localize("The map contains unsaved data, you might want to save it before you create a new map.\n\nContinue anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_CLOSE_MAP)
	{
		pTitle = Localize("Save changes?", "Editor");
		pMessage = Localize("Do you want to save your changes before closing this map?\n\nYour changes will be lost if you discard them.", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_LARGELAYER)
	{
		pTitle = Localize("Large layer", "Editor");
		pMessage = Localize("You are trying to set the height or width of a layer to more than 1000 tiles. This is actually possible, but only rarely necessary. It may cause the editor to work slower and will result in a larger file size as well as higher memory usage for client and server.", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_PREVENTUNUSEDTILES)
	{
		pTitle = Localize("Unused tiles disabled", "Editor");
		pMessage = Localize("Unused tiles can't be placed by default because they could get a use later and then destroy your map.\n\nActivate the 'Allow unused' setting to be able to place every tile.", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_IMAGEDIV16)
	{
		pTitle = Localize("Image width/height", "Editor");
		pMessage = Localize("The width or height of this image is not divisible by 16. This is required for images used in tile layers.", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_IMAGE_MAX)
	{
		pTitle = Localize("Max images", "Editor");
		str_format(aMessageBuf, sizeof(aMessageBuf), Localize("The client only allows a maximum of %zu images.", "Editor"), MAX_MAPIMAGES);
		pMessage = aMessageBuf;
	}
	else if(pEditor->m_PopupEventType == POPEVENT_SOUND_MAX)
	{
		pTitle = Localize("Max sounds", "Editor");
		str_format(aMessageBuf, sizeof(aMessageBuf), Localize("The client only allows a maximum of %zu sounds.", "Editor"), MAX_MAPSOUNDS);
		pMessage = aMessageBuf;
	}
	else if(pEditor->m_PopupEventType == POPEVENT_PLACE_BORDER_TILES)
	{
		pTitle = Localize("Place border tiles", "Editor");
		pMessage = Localize("This is going to overwrite any existing tiles around the edges of the layer.\n\nContinue?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_TILEART_BIG_IMAGE)
	{
		pTitle = Localize("Big image", "Editor");
		pMessage = Localize("The selected image is big. Converting it to tileart may take some time.\n\nContinue anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_TILEART_MANY_COLORS)
	{
		pTitle = Localize("Too many colors", "Editor");
		pMessage = Localize("The selected image contains many colors, which will lead to a big mapfile. You may want to consider reducing the number of colors.\n\nContinue anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_TILEART_TOO_MANY_COLORS)
	{
		pTitle = Localize("Too many colors", "Editor");
		pMessage = Localize("The client only supports 64 images but more would be needed to add the selected image as tileart.", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_QUADART_BIG_IMAGE)
	{
		pTitle = Localize("Big image", "Editor");
		pMessage = Localize("The selected image is really big. Expect performance issues!\n\nContinue anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_REMOVE_USED_IMAGE)
	{
		pTitle = Localize("Remove image", "Editor");
		pMessage = Localize("This image is used in the map. Removing it will reset all layers that use this image to their default.\n\nRemove anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_REMOVE_USED_SOUND)
	{
		pTitle = Localize("Remove sound", "Editor");
		pMessage = Localize("This sound is used in the map. Removing it will reset all layers that use this sound to their default.\n\nRemove anyway?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_RESTART_SERVER)
	{
		pTitle = Localize("Restart server", "Editor");
		pMessage = Localize("You have a local server running, but you are not authorized or connected.\n\nDo you want to restart the server and reconnect?", "Editor");
	}
	else if(pEditor->m_PopupEventType == POPEVENT_RESTARTING_SERVER)
	{
		pTitle = Localize("Restarting server", "Editor");
		pMessage = Localize("Local server is restarting. Please wait…", "Editor");

		CGameClient *pGameClient = (CGameClient *)pEditor->Kernel()->RequestInterface<IGameClient>();
		if(!pGameClient->m_LocalServer.IsServerRunning())
		{
			pEditor->TestMapLocally();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else
	{
		dbg_assert_failed("m_PopupEventType invalid");
	}

	CUIRect Label, ButtonBar, Button;

	View.Margin(10.0f, &View);
	View.HSplitBottom(20.0f, &View, &ButtonBar);

	// title
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, pTitle, 20.0f, TEXTALIGN_MC);

	// message
	SLabelProperties Props;
	Props.m_MaxWidth = View.w;
	pEditor->Ui()->DoLabel(&View, pMessage, 10.0f, TEXTALIGN_ML, Props);

	// button bar
	ButtonBar.VSplitLeft(110.0f, &Button, &ButtonBar);
	if(pEditor->m_PopupEventType != POPEVENT_LARGELAYER &&
		pEditor->m_PopupEventType != POPEVENT_PREVENTUNUSEDTILES &&
		pEditor->m_PopupEventType != POPEVENT_IMAGEDIV16 &&
		pEditor->m_PopupEventType != POPEVENT_IMAGE_MAX &&
		pEditor->m_PopupEventType != POPEVENT_SOUND_MAX &&
		pEditor->m_PopupEventType != POPEVENT_TILEART_TOO_MANY_COLORS)
	{
		static int s_CancelButton = 0;
		if(pEditor->DoButton_Editor(&s_CancelButton, Localize("Cancel", "Editor"), 0, &Button, BUTTONFLAG_LEFT, nullptr))
		{
			if(pEditor->m_PopupEventType == POPEVENT_LOADDROP)
				pEditor->m_aFilenamePendingLoad[0] = 0;

			else if(pEditor->m_PopupEventType == POPEVENT_TILEART_BIG_IMAGE || pEditor->m_PopupEventType == POPEVENT_TILEART_MANY_COLORS)
				pEditor->m_TileartImageInfo.Free();

			else if(pEditor->m_PopupEventType == POPEVENT_QUADART_BIG_IMAGE)
				pEditor->m_QuadArtImageInfo.Free();

			pEditor->m_PopupEventWasActivated = false;
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->m_PopupEventType == POPEVENT_RESTARTING_SERVER)
		return CUi::POPUP_KEEP_OPEN;

	ButtonBar.VSplitRight(110.0f, &ButtonBar, &Button);
	static int s_ConfirmButton = 0;
	if(pEditor->DoButton_Editor(&s_ConfirmButton, Localize("Confirm", "Editor"), 0, &Button, BUTTONFLAG_LEFT, nullptr) || (Active && pEditor->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)))
	{
		if(pEditor->m_PopupEventType == POPEVENT_EXIT)
		{
			pEditor->OnClose();
			g_Config.m_ClEditor = 0;
		}
		else if(pEditor->m_PopupEventType == POPEVENT_LOAD)
		{
			pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::MAP, Localize("Load map", "Editor"), Localize("Load", "Editor"), "maps", "", CallbackOpenMap, pEditor);
		}
		else if(pEditor->m_PopupEventType == POPEVENT_LOADCURRENT)
		{
			pEditor->LoadCurrentMap();
		}
		else if(pEditor->m_PopupEventType == POPEVENT_LOADDROP)
		{
			int Result = pEditor->Load(pEditor->m_aFilenamePendingLoad, IStorage::TYPE_ALL_OR_ABSOLUTE);
			if(!Result)
				dbg_msg("editor", Localize("editing passed map file '%s' failed", "Editor"), pEditor->m_aFilenamePendingLoad);
			pEditor->m_aFilenamePendingLoad[0] = 0;
		}
		else if(pEditor->m_PopupEventType == POPEVENT_NEW)
		{
			pEditor->AddDefaultMap();
			pEditor->Reset(false);
		}
		else if(pEditor->m_PopupEventType == POPEVENT_CLOSE_MAP)
		{
			pEditor->Map()->m_CloseOnSave = true;
			if(pEditor->Map()->m_aFilename[0] != '\0' && pEditor->Map()->m_ValidSaveFilename)
			{
				CallbackSaveMap(pEditor->Map()->m_aFilename, IStorage::TYPE_SAVE, pEditor);
			}
			else
			{
				char aDefaultName[IO_MAX_PATH_LENGTH];
				fs_split_file_extension(fs_filename(pEditor->Map()->m_aFilename), aDefaultName, sizeof(aDefaultName));
				pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, Localize("Save map", "Editor"), Localize("Save as", "Editor"), "maps", aDefaultName, CallbackSaveMap, pEditor);
			}
		}
		else if(pEditor->m_PopupEventType == POPEVENT_PLACE_BORDER_TILES)
		{
			pEditor->Map()->PlaceBorderTiles();
		}
		else if(pEditor->m_PopupEventType == POPEVENT_TILEART_BIG_IMAGE)
		{
			pEditor->TileartCheckColors();
		}
		else if(pEditor->m_PopupEventType == POPEVENT_TILEART_MANY_COLORS)
		{
			pEditor->Map()->AddTileArt(std::move(pEditor->m_TileartImageInfo), pEditor->m_aTileartFilename, false);
			pEditor->OnDialogClose();
		}
		else if(pEditor->m_PopupEventType == POPEVENT_QUADART_BIG_IMAGE)
		{
			pEditor->Map()->AddQuadArt(std::move(pEditor->m_QuadArtImageInfo), pEditor->m_QuadArtParameters, false);
			pEditor->OnDialogClose();
		}
		else if(pEditor->m_PopupEventType == POPEVENT_REMOVE_USED_IMAGE)
		{
			pEditor->Map()->m_vpImages.erase(pEditor->Map()->m_vpImages.begin() + pEditor->Map()->m_SelectedImage);
			pEditor->Map()->ModifyImageIndex(gs_ModifyIndexDeleted(pEditor->Map()->m_SelectedImage));
		}
		else if(pEditor->m_PopupEventType == POPEVENT_REMOVE_USED_SOUND)
		{
			pEditor->Map()->m_vpSounds.erase(pEditor->Map()->m_vpSounds.begin() + pEditor->Map()->m_SelectedSound);
			pEditor->Map()->ModifySoundIndex(gs_ModifyIndexDeleted(pEditor->Map()->m_SelectedSound));
			pEditor->m_ToolbarPreviewSound = -1;
		}
		else if(pEditor->m_PopupEventType == POPEVENT_RESTART_SERVER)
		{
			CGameClient *pGameClient = (CGameClient *)pEditor->Kernel()->RequestInterface<IGameClient>();
			pGameClient->m_LocalServer.KillServer();
			pEditor->m_PopupEventType = CEditor::POPEVENT_RESTARTING_SERVER;
			pEditor->m_PopupEventActivated = true;
		}
		pEditor->m_PopupEventWasActivated = false;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(pEditor->m_PopupEventType == POPEVENT_CLOSE_MAP)
	{
		static int s_DiscardButton = 0;
		ButtonBar.VMargin((ButtonBar.w - 110.0f) / 2.0f, &Button);
		if(pEditor->DoButton_Editor(&s_DiscardButton, Localize("Discard changes", "Editor"), EditorButtonChecked::DANGEROUS_ACTION, &Button, BUTTONFLAG_LEFT, nullptr))
		{
			pEditor->CloseMap(pEditor->m_PopupCloseMapIndex, false);
			pEditor->m_PopupEventWasActivated = false;
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

static int g_SelectImageSelected = -100;
static int g_SelectImageCurrent = -100;

CUi::EPopupMenuFunctionResult CEditor::PopupSelectImage(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	CUIRect ButtonBar, ImageView;
	View.VSplitLeft(150.0f, &ButtonBar, &View);
	View.Margin(10.0f, &ImageView);

	int ShowImage = g_SelectImageCurrent;

	const float ButtonHeight = 12.0f;
	const float ButtonMargin = 2.0f;

	static CListBox s_ListBox;
	s_ListBox.DoStart(ButtonHeight, pEditor->Map()->m_vpImages.size() + 1, 1, 4, g_SelectImageCurrent + 1, &ButtonBar, false);
	s_ListBox.DoAutoSpacing(ButtonMargin);

	for(int i = 0; i <= (int)pEditor->Map()->m_vpImages.size(); i++)
	{
		static int s_NoneButton = 0;
		CListboxItem Item = s_ListBox.DoNextItem(i == 0 ? (void *)&s_NoneButton : &pEditor->Map()->m_vpImages[i - 1], (i - 1) == g_SelectImageCurrent, 3.0f);
		if(!Item.m_Visible)
			continue;

		if(pEditor->Ui()->MouseInside(&Item.m_Rect))
			ShowImage = i - 1;

		CUIRect Label;
		Item.m_Rect.VMargin(5.0f, &Label);

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		pEditor->Ui()->DoLabel(&Label, i == 0 ? Localize("None", "Editor") : pEditor->Map()->m_vpImages[i - 1]->m_aName, EditorFontSizes::MENU, TEXTALIGN_ML, Props);
	}

	int NewSelected = s_ListBox.DoEnd() - 1;
	if(NewSelected != g_SelectImageCurrent)
		g_SelectImageSelected = NewSelected;

	if(ShowImage >= 0 && (size_t)ShowImage < pEditor->Map()->m_vpImages.size())
	{
		if(ImageView.h < ImageView.w)
			ImageView.w = ImageView.h;
		else
			ImageView.h = ImageView.w;
		float Max = (float)(maximum(pEditor->Map()->m_vpImages[ShowImage]->m_Width, pEditor->Map()->m_vpImages[ShowImage]->m_Height));
		ImageView.w *= pEditor->Map()->m_vpImages[ShowImage]->m_Width / Max;
		ImageView.h *= pEditor->Map()->m_vpImages[ShowImage]->m_Height / Max;
		pEditor->Graphics()->TextureSet(pEditor->Map()->m_vpImages[ShowImage]->m_Texture);
		pEditor->Graphics()->WrapClamp();
		pEditor->Graphics()->QuadsBegin();
		IGraphics::CQuadItem QuadItem(ImageView.x, ImageView.y, ImageView.w, ImageView.h);
		pEditor->Graphics()->QuadsDrawTL(&QuadItem, 1);
		pEditor->Graphics()->QuadsEnd();
		pEditor->Graphics()->WrapNormal();
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CEditor::PopupSelectImageInvoke(int Current, float x, float y)
{
	static SPopupMenuId s_PopupSelectImageId;
	g_SelectImageSelected = -100;
	g_SelectImageCurrent = Current;
	Ui()->DoPopupMenu(&s_PopupSelectImageId, x, y, 450, 300, this, PopupSelectImage);
}

int CEditor::PopupSelectImageResult()
{
	if(g_SelectImageSelected == -100)
		return -100;

	g_SelectImageCurrent = g_SelectImageSelected;
	g_SelectImageSelected = -100;
	return g_SelectImageCurrent;
}

static int g_SelectSoundSelected = -100;
static int g_SelectSoundCurrent = -100;

CUi::EPopupMenuFunctionResult CEditor::PopupSelectSound(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	const float ButtonHeight = 12.0f;
	const float ButtonMargin = 2.0f;

	static CListBox s_ListBox;
	s_ListBox.DoStart(ButtonHeight, pEditor->Map()->m_vpSounds.size() + 1, 1, 4, g_SelectSoundCurrent + 1, &View, false);
	s_ListBox.DoAutoSpacing(ButtonMargin);

	for(int i = 0; i <= (int)pEditor->Map()->m_vpSounds.size(); i++)
	{
		static int s_NoneButton = 0;
		CListboxItem Item = s_ListBox.DoNextItem(i == 0 ? (void *)&s_NoneButton : &pEditor->Map()->m_vpSounds[i - 1], (i - 1) == g_SelectSoundCurrent, 3.0f);
		if(!Item.m_Visible)
			continue;

		CUIRect Label;
		Item.m_Rect.VMargin(5.0f, &Label);

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		pEditor->Ui()->DoLabel(&Label, i == 0 ? Localize("None", "Editor") : pEditor->Map()->m_vpSounds[i - 1]->m_aName, EditorFontSizes::MENU, TEXTALIGN_ML, Props);
	}

	int NewSelected = s_ListBox.DoEnd() - 1;
	if(NewSelected != g_SelectSoundCurrent)
		g_SelectSoundSelected = NewSelected;

	return CUi::POPUP_KEEP_OPEN;
}

void CEditor::PopupSelectSoundInvoke(int Current, float x, float y)
{
	static SPopupMenuId s_PopupSelectSoundId;
	g_SelectSoundSelected = -100;
	g_SelectSoundCurrent = Current;
	Ui()->DoPopupMenu(&s_PopupSelectSoundId, x, y, 150, 300, this, PopupSelectSound);
}

int CEditor::PopupSelectSoundResult()
{
	if(g_SelectSoundSelected == -100)
		return -100;

	g_SelectSoundCurrent = g_SelectSoundSelected;
	g_SelectSoundSelected = -100;
	return g_SelectSoundCurrent;
}

static int s_GametileOpSelected = -1;

static const char *s_apGametileOpButtonNames[] = {
	Localizable("Air", "Editor"),
	Localizable("Hookable", "Editor"),
	Localizable("Death", "Editor"),
	Localizable("Unhookable", "Editor"),
	Localizable("Hookthrough", "Editor quick action"),
	Localizable("Freeze", "Editor"),
	Localizable("Unfreeze", "Editor"),
	Localizable("Deep Freeze", "Editor"),
	Localizable("Deep Unfreeze", "Editor"),
	Localizable("Blue Check-Tele", "Editor"),
	Localizable("Red Check-Tele", "Editor"),
	Localizable("Live Freeze", "Editor"),
	Localizable("Live Unfreeze", "Editor"),
};

CUi::EPopupMenuFunctionResult CEditor::PopupSelectGametileOp(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	const int PreviousSelected = s_GametileOpSelected;

	CUIRect Button;
	for(size_t i = 0; i < std::size(s_apGametileOpButtonNames); ++i)
	{
		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(12.0f, &Button, &View);
		const char *pLocalizationContext = i == static_cast<size_t>(EGameTileOp::HOOKTHROUGH) ? "Editor quick action" : "Editor";
		if(pEditor->DoButton_Editor(&s_apGametileOpButtonNames[i], Localize(s_apGametileOpButtonNames[i], pLocalizationContext), 0, &Button, BUTTONFLAG_LEFT, nullptr))
			s_GametileOpSelected = i;
	}

	return s_GametileOpSelected == PreviousSelected ? CUi::POPUP_KEEP_OPEN : CUi::POPUP_CLOSE_CURRENT;
}

void CEditor::PopupSelectGametileOpInvoke(float x, float y)
{
	static SPopupMenuId s_PopupSelectGametileOpId;
	s_GametileOpSelected = -1;
	Ui()->DoPopupMenu(&s_PopupSelectGametileOpId, x, y, 120.0f, std::size(s_apGametileOpButtonNames) * 14.0f + 10.0f, this, PopupSelectGametileOp);
}

int CEditor::PopupSelectGameTileOpResult()
{
	if(s_GametileOpSelected < 0)
		return -1;

	int Result = s_GametileOpSelected;
	s_GametileOpSelected = -1;
	return Result;
}

static int s_AutoMapConfigSelected = -100;
static int s_AutoMapConfigCurrent = -100;

CUi::EPopupMenuFunctionResult CEditor::PopupSelectConfigAutoMap(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pEditor->Map()->SelectedLayer(0));
	CAutoMapper *pAutoMapper = &pEditor->Map()->m_vpImages[pLayer->m_Image]->m_AutoMapper;

	const float ButtonHeight = 12.0f;
	const float ButtonMargin = 2.0f;

	CUIRect Button;
	View.HSplitBottom(ButtonHeight, &View, &Button);
	static char s_ShowDirectoryButton;
	if(pEditor->DoButton_MenuItem(&s_ShowDirectoryButton, Localize("Show directory", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Open the directory for automapper rules in the file browser.", "Editor")))
	{
		char aPath[IO_MAX_PATH_LENGTH];
		pEditor->Storage()->GetCompletePath(IStorage::TYPE_SAVE, "editor/automap", aPath, sizeof(aPath));
		pEditor->Storage()->CreateFolder("editor", IStorage::TYPE_SAVE);
		pEditor->Storage()->CreateFolder("editor/automap", IStorage::TYPE_SAVE);
		pEditor->Client()->ViewFile(aPath);
	}

	View.HSplitBottom(5.0f, &View, &Button);
	IGraphics::CLineItem LineItem(Button.x, Button.y + Button.h / 2, Button.x + Button.w, Button.y + Button.h / 2);
	pEditor->Graphics()->TextureClear();
	pEditor->Graphics()->LinesBegin();
	pEditor->Graphics()->LinesDraw(&LineItem, 1);
	pEditor->Graphics()->LinesEnd();

	static CListBox s_ListBox;
	s_ListBox.DoStart(ButtonHeight, pAutoMapper->ConfigNamesNum() + 1, 1, 4, s_AutoMapConfigCurrent + 1, &View, false);
	s_ListBox.SetScrollbarWidth(15.0f);
	s_ListBox.DoAutoSpacing(ButtonMargin);

	for(int i = 0; i < pAutoMapper->ConfigNamesNum() + 1; i++)
	{
		static int s_NoneButton = 0;
		CListboxItem Item = s_ListBox.DoNextItem(i == 0 ? (void *)&s_NoneButton : pAutoMapper->GetConfigName(i - 1), (i - 1) == s_AutoMapConfigCurrent, 3.0f);
		if(!Item.m_Visible)
			continue;

		CUIRect Label;
		Item.m_Rect.VMargin(5.0f, &Label);

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		pEditor->Ui()->DoLabel(&Label, i == 0 ? Localize("None", "Editor") : pAutoMapper->GetConfigName(i - 1), EditorFontSizes::MENU, TEXTALIGN_ML, Props);
	}

	int NewSelected = s_ListBox.DoEnd() - 1;
	if(NewSelected != s_AutoMapConfigCurrent)
		s_AutoMapConfigSelected = NewSelected;

	return CUi::POPUP_KEEP_OPEN;
}

void CEditor::PopupSelectConfigAutoMapInvoke(int Current, float x, float y)
{
	static SPopupMenuId s_PopupSelectConfigAutoMapId;
	s_AutoMapConfigSelected = -100;
	s_AutoMapConfigCurrent = Current;
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayer(0));
	const int ItemCount = minimum(Map()->m_vpImages[pLayer->m_Image]->m_AutoMapper.ConfigNamesNum() + 1, 10); // +1 for None-entry
	// Width for buttons is 120, 15 is the scrollbar width, 2 is the margin between both.
	Ui()->DoPopupMenu(&s_PopupSelectConfigAutoMapId, x, y, 120.0f + 15.0f + 2.0f, 10.0f + 12.0f * ItemCount + 2.0f * (ItemCount - 1) + 5.0f + 12.0f, this, PopupSelectConfigAutoMap);
}

int CEditor::PopupSelectConfigAutoMapResult()
{
	if(s_AutoMapConfigSelected == -100)
		return -100;

	s_AutoMapConfigCurrent = s_AutoMapConfigSelected;
	s_AutoMapConfigSelected = -100;
	return s_AutoMapConfigCurrent;
}

static int s_AutoMapReferenceSelected = -100;
static int s_AutoMapReferenceCurrent = -100;

CUi::EPopupMenuFunctionResult CEditor::PopupSelectAutoMapReference(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pEditor->Map()->SelectedLayer(0));

	const float ButtonHeight = 12.0f;
	const float ButtonMargin = 2.0f;

	static CListBox s_ListBox;
	s_ListBox.DoStart(ButtonHeight, std::size(AUTOMAP_REFERENCE_NAMES) + 1, 1, 4, s_AutoMapReferenceCurrent + 1, &View, false);
	s_ListBox.DoAutoSpacing(ButtonMargin);

	for(int i = 0; i < static_cast<int>(std::size(AUTOMAP_REFERENCE_NAMES)) + 1; i++)
	{
		static int s_NoneButton = 0;
		CListboxItem Item = s_ListBox.DoNextItem(i == 0 ? (void *)&s_NoneButton : AUTOMAP_REFERENCE_NAMES[i - 1], (i - 1) == s_AutoMapReferenceCurrent, 3.0f);
		if(!Item.m_Visible)
			continue;

		CUIRect Label;
		Item.m_Rect.VMargin(5.0f, &Label);

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		pEditor->Ui()->DoLabel(&Label, i == 0 ? Localize("None", "Editor") : Localize(AUTOMAP_REFERENCE_NAMES[i - 1], "Editor"), EditorFontSizes::MENU, TEXTALIGN_ML, Props);
	}

	int NewSelected = s_ListBox.DoEnd() - 1;
	if(NewSelected != s_AutoMapReferenceCurrent)
		s_AutoMapReferenceSelected = NewSelected;

	return CUi::POPUP_KEEP_OPEN;
}

void CEditor::PopupSelectAutoMapReferenceInvoke(int Current, float x, float y)
{
	static SPopupMenuId s_PopupSelectAutoMapReferenceId;
	s_AutoMapReferenceSelected = -100;
	s_AutoMapReferenceCurrent = Current;
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayer(0));
	// Width for buttons is 120, 15 is the scrollbar width, 2 is the margin between both.
	Ui()->DoPopupMenu(&s_PopupSelectAutoMapReferenceId, x, y, 120.0f + 15.0f + 2.0f, 26.0f + 14.0f * std::size(AUTOMAP_REFERENCE_NAMES) + 1, this, PopupSelectAutoMapReference);
}

int CEditor::PopupSelectAutoMapReferenceResult()
{
	if(s_AutoMapReferenceSelected == -100)
		return -100;

	s_AutoMapReferenceCurrent = s_AutoMapReferenceSelected;
	s_AutoMapReferenceSelected = -100;
	return s_AutoMapReferenceCurrent;
}

// DDRace

CUi::EPopupMenuFunctionResult CEditor::PopupTele(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	if(!pEditor->Map()->m_pTeleLayer)
		return CUi::POPUP_CLOSE_CURRENT;

	static int s_PreviousTeleNumber;
	static int s_PreviousCheckpointNumber;
	static int s_PreviousViewTeleNumber;

	CUIRect NumberPicker;
	CUIRect FindEmptySlot;
	CUIRect FindFreeTeleSlot, FindFreeCheckpointSlot, FindFreeViewSlot;

	View.VSplitRight(15.f, &NumberPicker, &FindEmptySlot);
	NumberPicker.VSplitRight(2.f, &NumberPicker, nullptr);

	FindEmptySlot.HSplitTop(13.0f, &FindFreeTeleSlot, &FindEmptySlot);
	FindEmptySlot.HSplitTop(13.0f, &FindFreeCheckpointSlot, &FindEmptySlot);
	FindEmptySlot.HSplitTop(13.0f, &FindFreeViewSlot, &FindEmptySlot);

	FindFreeTeleSlot.HMargin(1.0f, &FindFreeTeleSlot);
	FindFreeCheckpointSlot.HMargin(1.0f, &FindFreeCheckpointSlot);
	FindFreeViewSlot.HMargin(1.0f, &FindFreeViewSlot);

	auto ViewTele = [](CEditor *pEd) -> bool {
		if(!pEd->m_ViewTeleNumber)
			return false;
		int TeleX, TeleY;
		pEd->Map()->m_pTeleLayer->GetPos(pEd->m_ViewTeleNumber, -1, TeleX, TeleY);
		if(TeleX != -1 && TeleY != -1)
		{
			pEd->MapView()->SetWorldOffset({32.0f * (TeleX + 0.5f), 32.0f * (TeleY + 0.5f)});
			return true;
		}
		return false;
	};

	static std::vector<ColorRGBA> s_vColors = {
		ColorRGBA(0.5f, 1, 0.5f, 0.5f),
		ColorRGBA(0.5f, 1, 0.5f, 0.5f),
		ColorRGBA(1, 0.5f, 0.5f, 0.5f),
	};
	enum
	{
		PROP_TELE = 0,
		PROP_TELE_CP,
		PROP_TELE_VIEW,
		NUM_PROPS,
	};

	// find next free numbers buttons
	{
		// Pressing ctrl+f will find next free numbers for both tele and checkpoints

		static int s_NextFreeTelePid = 0;
		if(pEditor->DoButton_Editor(&s_NextFreeTelePid, "F", 0, &FindFreeTeleSlot, BUTTONFLAG_LEFT, Localize("[Ctrl+F] Find next free tele number.", "Editor")) ||
			(Active && pEditor->Input()->ModifierIsPressed() && pEditor->Input()->KeyPress(KEY_F)))
		{
			int TeleNumber = pEditor->Map()->m_pTeleLayer->FindNextFreeNumber(false);
			if(TeleNumber != -1)
			{
				pEditor->m_TeleNumber = TeleNumber;
				pEditor->AdjustBrushSpecialTiles(false, 0, 0);
			}
		}

		static int s_NextFreeCheckpointPid = 0;
		if(pEditor->DoButton_Editor(&s_NextFreeCheckpointPid, "F", 0, &FindFreeCheckpointSlot, BUTTONFLAG_LEFT, Localize("[Ctrl+F] Find next free checkpoint number.", "Editor")) ||
			(Active && pEditor->Input()->ModifierIsPressed() && pEditor->Input()->KeyPress(KEY_F)))
		{
			int CheckpointNumber = pEditor->Map()->m_pTeleLayer->FindNextFreeNumber(true);
			if(CheckpointNumber != -1)
			{
				pEditor->m_TeleCheckpointNumber = CheckpointNumber;
				pEditor->AdjustBrushSpecialTiles(false, 0, 0);
			}
		}

		static int s_NextFreeViewPid = 0;
		if(pEditor->DoButton_Editor(&s_NextFreeViewPid, "N", 0, &FindFreeViewSlot, BUTTONFLAG_LEFT, Localize("[N] Show next tele with this number.", "Editor")) ||
			(Active && pEditor->Input()->KeyPress(KEY_N)))
		{
			s_vColors[PROP_TELE_VIEW] = ViewTele(pEditor) ? ColorRGBA(0.5f, 1, 0.5f, 0.5f) : ColorRGBA(1, 0.5f, 0.5f, 0.5f);
		}
	}

	// number picker
	{
		CProperty aProps[] = {
			{Localize("Number", "Editor"), pEditor->m_TeleNumber, PROPTYPE_INT, 1, 255},
			{Localize("Checkpoint", "Editor"), pEditor->m_TeleCheckpointNumber, PROPTYPE_INT, 1, 255},
			{Localize("View", "Editor"), pEditor->m_ViewTeleNumber, PROPTYPE_INT, 1, 255},
			{nullptr},
		};

		static int s_aIds[NUM_PROPS] = {0};

		int NewVal = 0;
		int Prop = pEditor->DoProperties(&NumberPicker, aProps, s_aIds, &NewVal, s_vColors);
		if(Prop == PROP_TELE)
		{
			pEditor->m_TeleNumber = (NewVal - 1 + 255) % 255 + 1;
			pEditor->AdjustBrushSpecialTiles(false, 0, 0);
		}
		else if(Prop == PROP_TELE_CP)
		{
			pEditor->m_TeleCheckpointNumber = (NewVal - 1 + 255) % 255 + 1;
			pEditor->AdjustBrushSpecialTiles(false, 0, 0);
		}
		else if(Prop == PROP_TELE_VIEW)
		{
			pEditor->m_ViewTeleNumber = (NewVal - 1 + 255) % 255 + 1;
		}

		if(s_PreviousTeleNumber == 1 || s_PreviousTeleNumber != pEditor->m_TeleNumber)
			s_vColors[PROP_TELE] = pEditor->Map()->m_pTeleLayer->ContainsElementWithId(pEditor->m_TeleNumber, false) ? ColorRGBA(1, 0.5f, 0.5f, 0.5f) : ColorRGBA(0.5f, 1, 0.5f, 0.5f);

		if(s_PreviousCheckpointNumber == 1 || s_PreviousCheckpointNumber != pEditor->m_TeleCheckpointNumber)
			s_vColors[PROP_TELE_CP] = pEditor->Map()->m_pTeleLayer->ContainsElementWithId(pEditor->m_TeleCheckpointNumber, true) ? ColorRGBA(1, 0.5f, 0.5f, 0.5f) : ColorRGBA(0.5f, 1, 0.5f, 0.5f);

		if(s_PreviousViewTeleNumber != pEditor->m_ViewTeleNumber)
			s_vColors[PROP_TELE_VIEW] = ViewTele(pEditor) ? ColorRGBA(0.5f, 1, 0.5f, 0.5f) : ColorRGBA(1, 0.5f, 0.5f, 0.5f);
	}

	s_PreviousTeleNumber = pEditor->m_TeleNumber;
	s_PreviousCheckpointNumber = pEditor->m_TeleCheckpointNumber;
	s_PreviousViewTeleNumber = pEditor->m_ViewTeleNumber;

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupSpeedup(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	enum
	{
		PROP_FORCE = 0,
		PROP_MAXSPEED,
		PROP_ANGLE,
		NUM_PROPS
	};

	CProperty aProps[] = {
		{Localize("Force", "Editor"), pEditor->m_SpeedupForce, PROPTYPE_INT, 1, 255},
		{Localize("Max Speed", "Editor"), pEditor->m_SpeedupMaxSpeed, PROPTYPE_INT, 0, 255},
		{Localize("Angle", "Editor"), pEditor->m_SpeedupAngle, PROPTYPE_ANGLE_SCROLL, 0, 359},
		{nullptr},
	};

	static int s_aIds[NUM_PROPS] = {0};
	int NewVal = 0;
	int Prop = pEditor->DoProperties(&View, aProps, s_aIds, &NewVal);

	if(Prop == PROP_FORCE)
	{
		pEditor->m_SpeedupForce = std::clamp(NewVal, 1, 255);
	}
	else if(Prop == PROP_MAXSPEED)
	{
		pEditor->m_SpeedupMaxSpeed = std::clamp(NewVal, 0, 255);
	}
	else if(Prop == PROP_ANGLE)
	{
		pEditor->m_SpeedupAngle = std::clamp(NewVal, 0, 359);
		pEditor->AdjustBrushSpecialTiles(false, 0, 0);
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupSwitch(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	if(!pEditor->Map()->m_pSwitchLayer)
		return CUi::POPUP_CLOSE_CURRENT;

	CUIRect NumberPicker, FindEmptySlot, ViewEmptySlot;

	View.VSplitRight(15.0f, &NumberPicker, &FindEmptySlot);
	NumberPicker.VSplitRight(2.0f, &NumberPicker, nullptr);

	FindEmptySlot.HSplitTop(13.0f, &FindEmptySlot, &ViewEmptySlot);
	ViewEmptySlot.HSplitTop(13.0f, nullptr, &ViewEmptySlot);

	FindEmptySlot.HMargin(1.0f, &FindEmptySlot);
	ViewEmptySlot.HMargin(1.0f, &ViewEmptySlot);

	auto ViewSwitch = [pEditor]() -> bool {
		if(!pEditor->m_ViewSwitch)
			return false;
		ivec2 SwitchPos;
		pEditor->Map()->m_pSwitchLayer->GetPos(pEditor->m_ViewSwitch, -1, SwitchPos);
		if(SwitchPos != ivec2(-1, -1))
		{
			pEditor->MapView()->SetWorldOffset({32.0f * (SwitchPos.x + 0.5f), 32.0f * (SwitchPos.y + 0.5f)});
			return true;
		}
		return false;
	};

	static std::vector<ColorRGBA> s_vColors = {
		ColorRGBA(1, 1, 1, 0.5f),
		ColorRGBA(1, 1, 1, 0.5f),
		ColorRGBA(1, 1, 1, 0.5f),
	};

	enum
	{
		PROP_SWITCH_NUMBER = 0,
		PROP_SWITCH_DELAY,
		PROP_SWITCH_VIEW,
		NUM_PROPS,
	};

	// find empty number button
	{
		static int s_EmptySlotPid = 0;
		if(pEditor->DoButton_Editor(&s_EmptySlotPid, "F", 0, &FindEmptySlot, BUTTONFLAG_LEFT, Localize("[Ctrl+F] Find empty slot.", "Editor")) ||
			(Active && pEditor->Input()->ModifierIsPressed() && pEditor->Input()->KeyPress(KEY_F)))
		{
			int Number = pEditor->Map()->m_pSwitchLayer->FindNextFreeNumber();
			if(Number != -1)
				pEditor->m_SwitchNumber = Number;
		}

		static int s_NextViewPid = 0;
		if(pEditor->DoButton_Editor(&s_NextViewPid, "N", 0, &ViewEmptySlot, BUTTONFLAG_LEFT, Localize("[N] Show next switcher with this number.", "Editor")) ||
			(Active && pEditor->Input()->KeyPress(KEY_N)))
		{
			s_vColors[PROP_SWITCH_VIEW] = ViewSwitch() ? ColorRGBA(0.5f, 1, 0.5f, 0.5f) : ColorRGBA(1, 0.5f, 0.5f, 0.5f);
		}
	}

	// number picker
	static int s_PreviousNumber = -1;
	static int s_PreviousView = -1;
	{
		CProperty aProps[] = {
			{Localize("Number", "Editor"), pEditor->m_SwitchNumber, PROPTYPE_INT, 0, 255},
			{Localize("Delay", "Editor"), pEditor->m_SwitchDelay, PROPTYPE_INT, 0, 255},
			{Localize("View", "Editor"), pEditor->m_ViewSwitch, PROPTYPE_INT, 0, 255},
			{nullptr},
		};

		static int s_aIds[NUM_PROPS] = {0};
		int NewVal = 0;
		int Prop = pEditor->DoProperties(&NumberPicker, aProps, s_aIds, &NewVal, s_vColors);

		if(Prop == PROP_SWITCH_NUMBER)
		{
			pEditor->m_SwitchNumber = (NewVal + 256) % 256;
		}
		else if(Prop == PROP_SWITCH_DELAY)
		{
			pEditor->m_SwitchDelay = (NewVal + 256) % 256;
		}
		else if(Prop == PROP_SWITCH_VIEW)
		{
			pEditor->m_ViewSwitch = (NewVal + 256) % 256;
		}

		if(s_PreviousNumber == 1 || s_PreviousNumber != pEditor->m_SwitchNumber)
			s_vColors[PROP_SWITCH_NUMBER] = pEditor->Map()->m_pSwitchLayer->ContainsElementWithId(pEditor->m_SwitchNumber) ? ColorRGBA(1, 0.5f, 0.5f, 0.5f) : ColorRGBA(0.5f, 1, 0.5f, 0.5f);
		if(s_PreviousView != pEditor->m_ViewSwitch)
			s_vColors[PROP_SWITCH_VIEW] = ViewSwitch() ? ColorRGBA(0.5f, 1, 0.5f, 0.5f) : ColorRGBA(1, 0.5f, 0.5f, 0.5f);
	}

	s_PreviousNumber = pEditor->m_SwitchNumber;
	s_PreviousView = pEditor->m_ViewSwitch;
	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupTune(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	if(!pEditor->Map()->m_pTuneLayer)
		return CUi::POPUP_CLOSE_CURRENT;

	CUIRect NumberPicker, FindEmptySlot, ViewEmptySlot;

	View.VSplitRight(15.0f, &NumberPicker, &FindEmptySlot);
	NumberPicker.VSplitRight(2.0f, &NumberPicker, nullptr);

	FindEmptySlot.HSplitTop(13.0f, &FindEmptySlot, &ViewEmptySlot);
	FindEmptySlot.HMargin(1.0f, &FindEmptySlot);
	ViewEmptySlot.HMargin(1.0f, &ViewEmptySlot);

	auto ViewTune = [pEditor]() -> bool {
		if(!pEditor->m_ViewTuning)
			return false;
		ivec2 TunePos;
		pEditor->Map()->m_pTuneLayer->GetPos(pEditor->m_ViewTuning, -1, TunePos);

		if(TunePos != ivec2(-1, -1))
		{
			pEditor->MapView()->SetWorldOffset({32.0f * (TunePos.x + 0.5f), 32.0f * (TunePos.y + 0.5f)});
			return true;
		}
		return false;
	};

	static std::vector<ColorRGBA> s_vColors = {
		ColorRGBA(1, 1, 1, 0.5f),
		ColorRGBA(1, 0.5f, 0.5f, 0.5f),
	};

	enum
	{
		PROP_TUNE_NUMBER = 0,
		PROP_TUNE_VIEW,
		NUM_PROPS,
	};

	// find empty number button
	{
		static int s_EmptySlotPid = 0;
		if(pEditor->DoButton_Editor(&s_EmptySlotPid, "F", 0, &FindEmptySlot, BUTTONFLAG_LEFT, Localize("[Ctrl+F] Find unused zone.", "Editor")) ||
			(Active && pEditor->Input()->ModifierIsPressed() && pEditor->Input()->KeyPress(KEY_F)))
		{
			int Number = pEditor->Map()->m_pTuneLayer->FindNextFreeNumber();
			if(Number != -1)
				pEditor->m_TuningNumber = Number;
		}

		static int s_NextViewPid = 0;
		if(pEditor->DoButton_Editor(&s_NextViewPid, "N", 0, &ViewEmptySlot, BUTTONFLAG_LEFT, Localize("[N] Show next tune tile with this number.", "Editor")) ||
			(Active && pEditor->Input()->KeyPress(KEY_N)))
		{
			s_vColors[PROP_TUNE_VIEW] = ViewTune() ? ColorRGBA(0.5f, 1, 0.5f, 0.5f) : ColorRGBA(1, 0.5f, 0.5f, 0.5f);
		}
	}

	// number picker
	static int s_PreviousNumber = -1;
	static int s_PreviousView = -1;
	{
		CProperty aProps[] = {
			{Localize("Zone", "Editor"), pEditor->m_TuningNumber, PROPTYPE_INT, 1, 255},
			{Localize("View", "Editor"), pEditor->m_ViewTuning, PROPTYPE_INT, 1, 255},
			{nullptr},
		};

		static int s_aIds[NUM_PROPS] = {0};
		int NewVal = 0;
		int Prop = pEditor->DoProperties(&NumberPicker, aProps, s_aIds, &NewVal, s_vColors);

		if(Prop == PROP_TUNE_NUMBER)
		{
			pEditor->m_TuningNumber = (NewVal - 1 + 255) % 255 + 1;
		}
		else if(Prop == PROP_TUNE_VIEW)
		{
			pEditor->m_ViewTuning = (NewVal - 1 + 255) % 255 + 1;
		}

		if(s_PreviousNumber == 1 || s_PreviousNumber != pEditor->m_TuningNumber)
			s_vColors[PROP_TUNE_NUMBER] = pEditor->Map()->m_pTuneLayer->ContainsElementWithId(pEditor->m_TuningNumber) ? ColorRGBA(1, 0.5f, 0.5f, 0.5f) : ColorRGBA(0.5f, 1, 0.5f, 0.5f);
		if(s_PreviousView != pEditor->m_ViewTuning)
			s_vColors[PROP_TUNE_VIEW] = ViewTune() ? ColorRGBA(0.5f, 1, 0.5f, 0.5f) : ColorRGBA(1, 0.5f, 0.5f, 0.5f);
	}

	s_PreviousNumber = pEditor->m_TuningNumber;
	s_PreviousView = pEditor->m_ViewTuning;

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupGoto(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	enum
	{
		PROP_COORD_X = 0,
		PROP_COORD_Y,
		NUM_PROPS,
	};

	static ivec2 s_GotoPos(0, 0);

	CProperty aProps[] = {
		{Localize("X", "Editor coordinate label"), s_GotoPos.x, PROPTYPE_INT, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()},
		{Localize("Y", "Editor coordinate label"), s_GotoPos.y, PROPTYPE_INT, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()},
		{nullptr},
	};

	static int s_aIds[NUM_PROPS] = {0};
	int NewVal = 0;
	int Prop = pEditor->DoProperties(&View, aProps, s_aIds, &NewVal);

	if(Prop == PROP_COORD_X)
	{
		s_GotoPos.x = NewVal;
	}
	else if(Prop == PROP_COORD_Y)
	{
		s_GotoPos.y = NewVal;
	}

	CUIRect Button;
	View.HSplitBottom(12.0f, &View, &Button);

	static int s_Button;
	if(pEditor->DoButton_Editor(&s_Button, Localize("Go", "Editor"), 0, &Button, BUTTONFLAG_LEFT, nullptr))
	{
		pEditor->MapView()->SetWorldOffset({32.0f * (s_GotoPos.x + 0.5f), 32.0f * (s_GotoPos.y + 0.5f)});
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupEntities(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	for(size_t i = 0; i < pEditor->m_vSelectEntitiesFiles.size(); i++)
	{
		CUIRect Button;
		View.HSplitTop(14.0f, &Button, &View);

		const char *pName = pEditor->m_vSelectEntitiesFiles[i].c_str();
		const char *pDisplayName = EntitiesDisplayName(pName);
		if(pEditor->DoButton_MenuItem(pName, pDisplayName, pEditor->m_vSelectEntitiesFiles[i] == pEditor->m_SelectEntitiesImage, &Button))
		{
			if(pEditor->m_vSelectEntitiesFiles[i] != pEditor->m_SelectEntitiesImage)
			{
				if(i == pEditor->m_vSelectEntitiesFiles.size() - 1)
				{
					pEditor->m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, Localize("Load custom entities", "Editor"), Localize("Load", "Editor"), "assets/entities", "", CallbackCustomEntities, pEditor);
					return CUi::POPUP_CLOSE_CURRENT;
				}

				pEditor->m_SelectEntitiesImage = pEditor->m_vSelectEntitiesFiles[i];
				pEditor->m_AllowPlaceUnusedTiles = pEditor->m_SelectEntitiesImage == "DDNet" ? EUnusedEntities::NOT_ALLOWED : EUnusedEntities::ALLOWED_IMPLICIT;
				pEditor->m_PreventUnusedTilesWasWarned = false;

				pEditor->Graphics()->UnloadTexture(&pEditor->m_EntitiesTexture);

				char aBuf[IO_MAX_PATH_LENGTH];
				str_format(aBuf, sizeof(aBuf), "editor/entities/%s.png", pName);
				pEditor->m_EntitiesTexture = pEditor->Graphics()->LoadTexture(aBuf, IStorage::TYPE_ALL, pEditor->GetTextureUsageFlag());
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupProofMode(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	CUIRect Button;
	View.HSplitTop(12.0f, &Button, &View);
	static int s_ButtonIngame;
	if(pEditor->DoButton_MenuItem(&s_ButtonIngame, Localize("Ingame", "Editor"), pEditor->MapView()->ProofMode()->IsModeIngame(), &Button, BUTTONFLAG_LEFT, Localize("These borders represent what a player maximum can see.", "Editor")))
	{
		pEditor->MapView()->ProofMode()->SetModeIngame();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	View.HSplitTop(12.0f, &Button, &View);
	static int s_ButtonMenu;
	if(pEditor->DoButton_MenuItem(&s_ButtonMenu, Localize("Menu", "Editor"), pEditor->MapView()->ProofMode()->IsModeMenu(), &Button, BUTTONFLAG_LEFT, Localize("These borders represent what will be shown in the menu.", "Editor")))
	{
		pEditor->MapView()->ProofMode()->SetModeMenu();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupAnimateSettings(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CMapEnvelopeEvaluator &EnvelopeEvaluator = pEditor->Map()->m_EnvelopeEvaluator;

	static constexpr float MIN_ANIM_SPEED = 0.001f;
	static constexpr float MAX_ANIM_SPEED = 1000000.0f;

	CUIRect Row, Label, ButtonDecrease, EditBox, ButtonIncrease, ButtonReset;
	View.HSplitTop(13.0f, &Row, &View);
	Row.VSplitMid(&Label, &Row);
	Row.HMargin(1.0f, &Row);
	Row.VSplitLeft(10.0f, &ButtonDecrease, &Row);
	Row.VSplitRight(10.0f, &EditBox, &ButtonIncrease);
	View.HSplitBottom(12.0f, &View, &ButtonReset);
	pEditor->Ui()->DoLabel(&Label, Localize("Speed", "Editor"), 10.0f, TEXTALIGN_ML);

	const float OldAnimateSpeed = EnvelopeEvaluator.m_AnimateSpeed;

	static char s_DecreaseButton;
	if(pEditor->DoButton_QmIcon(&s_DecreaseButton, EQmIcon::MINUS, FONT_ICON_MINUS, 0, &ButtonDecrease, BUTTONFLAG_LEFT, Localize("Decrease animation speed.", "Editor"), IGraphics::CORNER_L, 7.0f))
	{
		EnvelopeEvaluator.m_AnimateSpeed -= EnvelopeEvaluator.m_AnimateSpeed <= 1.0f ? 0.1f : 0.5f;
		EnvelopeEvaluator.m_AnimateSpeed = maximum(EnvelopeEvaluator.m_AnimateSpeed, MIN_ANIM_SPEED);
		EnvelopeEvaluator.m_AnimateUpdatePopup = true;
	}

	static char s_IncreaseButton;
	if(pEditor->DoButton_QmIcon(&s_IncreaseButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &ButtonIncrease, BUTTONFLAG_LEFT, Localize("Increase animation speed.", "Editor"), IGraphics::CORNER_R, 7.0f))
	{
		if(EnvelopeEvaluator.m_AnimateSpeed < 0.1f)
			EnvelopeEvaluator.m_AnimateSpeed = 0.1f;
		else
			EnvelopeEvaluator.m_AnimateSpeed += EnvelopeEvaluator.m_AnimateSpeed < 1.0f ? 0.1f : 0.5f;
		EnvelopeEvaluator.m_AnimateSpeed = minimum(EnvelopeEvaluator.m_AnimateSpeed, MAX_ANIM_SPEED);
		EnvelopeEvaluator.m_AnimateUpdatePopup = true;
	}

	static char s_DefaultButton;
	if(pEditor->DoButton_Ex(&s_DefaultButton, Localize("Default", "Editor"), 0, &ButtonReset, BUTTONFLAG_LEFT, Localize("Reset to normal animation speed.", "Editor"), IGraphics::CORNER_ALL))
	{
		EnvelopeEvaluator.m_AnimateSpeed = 1.0f;
		EnvelopeEvaluator.m_AnimateUpdatePopup = true;
	}

	static CLineInputNumber s_SpeedInput;
	if(EnvelopeEvaluator.m_AnimateUpdatePopup)
	{
		s_SpeedInput.SetFloat(EnvelopeEvaluator.m_AnimateSpeed);
		EnvelopeEvaluator.m_AnimateUpdatePopup = false;
	}

	if(pEditor->DoEditBox(&s_SpeedInput, &EditBox, 10.0f, IGraphics::CORNER_NONE, Localize("The animation speed.", "Editor")))
	{
		EnvelopeEvaluator.m_AnimateSpeed = std::clamp(s_SpeedInput.GetFloat(), MIN_ANIM_SPEED, MAX_ANIM_SPEED);
	}

	// 调整起始时间，避免动画速度变化时跳变。
	const float AnimateSpeedRatio = OldAnimateSpeed / EnvelopeEvaluator.m_AnimateSpeed;
	const float Time = pEditor->Client()->GlobalTime();
	EnvelopeEvaluator.m_AnimateStart = Time + (EnvelopeEvaluator.m_AnimateStart - Time) * AnimateSpeedRatio;
	if(!EnvelopeEvaluator.m_Animate)
	{
		EnvelopeEvaluator.m_AnimateTime *= AnimateSpeedRatio;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupEnvelopeCurvetype(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	if(pEditor->Map()->m_SelectedEnvelope < 0 || pEditor->Map()->m_SelectedEnvelope >= (int)pEditor->Map()->m_vpEnvelopes.size())
	{
		return CUi::POPUP_CLOSE_CURRENT;
	}
	std::shared_ptr<CEnvelope> pEnvelope = pEditor->Map()->m_vpEnvelopes[pEditor->Map()->m_SelectedEnvelope];

	if(pEditor->m_PopupEnvelopeSelectedPoint < 0 || pEditor->m_PopupEnvelopeSelectedPoint >= (int)pEnvelope->m_vPoints.size())
	{
		return CUi::POPUP_CLOSE_CURRENT;
	}
	CEnvPoint_runtime &SelectedPoint = pEnvelope->m_vPoints[pEditor->m_PopupEnvelopeSelectedPoint];

	static char s_aButtonIds[NUM_CURVETYPES] = {0};

	for(int Type = 0; Type < NUM_CURVETYPES; Type++)
	{
		CUIRect Button;
		View.HSplitTop(14.0f, &Button, &View);

		if(pEditor->DoButton_MenuItem(&s_aButtonIds[Type], Localize(CURVE_TYPE_NAMES[Type], CURVE_TYPE_CONTEXTS[Type]), Type == SelectedPoint.m_Curvetype, &Button))
		{
			const int PrevCurve = SelectedPoint.m_Curvetype;
			if(PrevCurve != Type)
			{
				SelectedPoint.m_Curvetype = Type;
				pEditor->Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionEnvelopeEditPoint>(pEditor->Map(),
					pEditor->Map()->m_SelectedEnvelope, pEditor->m_PopupEnvelopeSelectedPoint, 0, CEditorActionEnvelopeEditPoint::EEditType::CURVE_TYPE, PrevCurve, SelectedPoint.m_Curvetype));
				pEditor->Map()->OnModify();
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::PopupQuadArt(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);

	enum
	{
		PROP_IMAGE_PIXELSIZE = 0,
		PROP_QUAD_PIXELSIZE,
		PROP_OPTIMIZE,
		PROP_CENTRALIZE,
		NUM_PROPS,
	};

	CProperty aProps[] = {
		{Localize("Image pixelsize", "Editor"), pEditor->m_QuadArtParameters.m_ImagePixelSize, PROPTYPE_INT, 1, 1024},
		{Localize("Quad pixelsize", "Editor"), pEditor->m_QuadArtParameters.m_QuadPixelSize, PROPTYPE_INT, 1, 1024},
		{Localize("Optimize", "Editor"), pEditor->m_QuadArtParameters.m_Optimize, PROPTYPE_BOOL, false, true},
		{Localize("Centralize", "Editor"), pEditor->m_QuadArtParameters.m_Centralize, PROPTYPE_BOOL, false, true},
		{nullptr},
	};

	static int s_aIds[NUM_PROPS] = {0};
	int NewVal = 0;

	// Title
	CUIRect Label;
	View.HSplitTop(20.0f, &Label, &View);
	pEditor->Ui()->DoLabel(&Label, Localize("Configure Quadart", "Editor"), 20.0f, TEXTALIGN_MC);
	View.HSplitTop(10.0f, nullptr, &View);

	// Properties
	int Prop = pEditor->DoProperties(&View, aProps, s_aIds, &NewVal);

	if(Prop == PROP_IMAGE_PIXELSIZE)
	{
		pEditor->m_QuadArtParameters.m_ImagePixelSize = NewVal;
	}
	else if(Prop == PROP_QUAD_PIXELSIZE)
	{
		pEditor->m_QuadArtParameters.m_QuadPixelSize = NewVal;
	}
	else if(Prop == PROP_OPTIMIZE)
	{
		pEditor->m_QuadArtParameters.m_Optimize = (bool)NewVal;
	}
	else if(Prop == PROP_CENTRALIZE)
	{
		pEditor->m_QuadArtParameters.m_Centralize = (bool)NewVal;
	}

	// Buttons
	CUIRect BottomBar, Left, Right;
	View.HSplitBottom(20.f, &View, &BottomBar);
	BottomBar.VSplitLeft(110.f, &Left, &BottomBar);

	static int s_Cancel;
	if(pEditor->DoButton_Editor(&s_Cancel, Localize("Cancel", "Editor"), 0, &Left, BUTTONFLAG_LEFT, nullptr))
	{
		pEditor->m_QuadArtImageInfo.Free();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	BottomBar.VSplitRight(110.f, &BottomBar, &Right);
	static int s_Confirm;
	constexpr int MaximumQuadThreshold = 100'000;
	if(pEditor->DoButton_Editor(&s_Confirm, Localize("Confirm", "Editor"), 0, &Right, BUTTONFLAG_LEFT, nullptr))
	{
		size_t MaximumQuadNumber = (pEditor->m_QuadArtImageInfo.m_Width / pEditor->m_QuadArtParameters.m_ImagePixelSize) *
					   (pEditor->m_QuadArtImageInfo.m_Height / pEditor->m_QuadArtParameters.m_ImagePixelSize);
		if(MaximumQuadNumber > MaximumQuadThreshold)
		{
			pEditor->m_PopupEventType = CEditor::POPEVENT_QUADART_BIG_IMAGE;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->Map()->AddQuadArt(std::move(pEditor->m_QuadArtImageInfo), pEditor->m_QuadArtParameters, false);
			pEditor->OnDialogClose();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}
