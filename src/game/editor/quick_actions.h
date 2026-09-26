// This file can be included several times.

#ifndef REGISTER_QUICK_ACTION
// This helps IDEs properly syntax highlight the uses of the macro below.
#define REGISTER_QUICK_ACTION(name, text, callback, disabled, active, button_color, description)
#endif

#define ALWAYS_FALSE []() -> bool { return false; }
#define DEFAULT_BTN []() -> int { return -1; }

REGISTER_QUICK_ACTION(
	ShowHelp,
	"Show help",
	[&]() { ShowHelp(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[F1] Open the DDNet Wiki page for the map editor in a web browser.")
REGISTER_QUICK_ACTION(
	GotoPosition,
	"Goto position",
	[&]() { GotoPosition(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"Go to a specified coordinate point on the map.")
REGISTER_QUICK_ACTION(
	ToggleGrid,
	Localizable("Toggle grid", "Editor"),
	[&]() { MapView()->MapGrid()->Toggle(); },
	ALWAYS_FALSE,
	[&]() -> bool { return MapView()->MapGrid()->IsEnabled(); },
	DEFAULT_BTN,
	Localizable("[Ctrl+G] Toggle grid.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesAir,
	Localizable("Game tiles: Air", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::AIR); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesHookable,
	Localizable("Game tiles: Hookable", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::HOOKABLE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesDeath,
	Localizable("Game tiles: Death", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::DEATH); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesUnhookable,
	Localizable("Game tiles: Unhookable", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::UNHOOKABLE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesHookthrough,
	Localizable("Game tiles: Hookthrough", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::HOOKTHROUGH); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesFreeze,
	Localizable("Game tiles: Freeze", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::FREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesUnfreeze,
	Localizable("Game tiles: Unfreeze", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::UNFREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesDeepFreeze,
	Localizable("Game tiles: Deep Freeze", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::DEEP_FREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesDeepUnfreeze,
	Localizable("Game tiles: Deep Unfreeze", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::DEEP_UNFREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesBlueCheckTele,
	Localizable("Game tiles: Blue Check Tele", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::BLUE_CHECK_TELE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesRedCheckTele,
	Localizable("Game tiles: Red Check Tele", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::RED_CHECK_TELE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesLiveFreeze,
	Localizable("Game tiles: Live Freeze", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::LIVE_FREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	GameTilesLiveUnfreeze,
	Localizable("Game tiles: Live Unfreeze", "Editor"),
	[&]() { FillGameTiles(EGameTileOp::LIVE_UNFREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Construct game tiles from this layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddGroup,
	Localizable("Add group", "Editor"),
	[&]() { AddGroup(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Add a new group.", "Editor"))
REGISTER_QUICK_ACTION(
	ResetZoom,
	Localizable("Reset zoom", "Editor"),
	[&]() { MapView()->ResetZoom(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Numpad*] Zoom to normal and remove editor offset.", "Editor"))
REGISTER_QUICK_ACTION(
	ZoomOut,
	Localizable("Zoom out", "Editor"),
	[&]() { MapView()->Zoom()->ChangeValue(50.0f); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Numpad-] Zoom out.", "Editor"))
REGISTER_QUICK_ACTION(
	ZoomIn,
	Localizable("Zoom in", "Editor"),
	[&]() { MapView()->Zoom()->ChangeValue(-50.0f); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Numpad+] Zoom in.", "Editor"))
REGISTER_QUICK_ACTION(
	Refocus,
	Localizable("Refocus", "Editor"),
	[&]() { MapView()->Focus(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Home] Restore map focus.", "Editor"))
REGISTER_QUICK_ACTION(
	Proof,
	Localizable("Proof", "Editor"),
	[&]() { MapView()->ProofMode()->Toggle(); },
	ALWAYS_FALSE,
	[&]() -> bool { return MapView()->ProofMode()->IsEnabled(); },
	DEFAULT_BTN,
	Localizable("Toggle proof borders. These borders represent the area that a player can see with default zoom.", "Editor"))
REGISTER_QUICK_ACTION(
	AddTileLayer, Localizable("Add tile layer", "Editor"), [&]() { AddTileLayer(); }, ALWAYS_FALSE, ALWAYS_FALSE, DEFAULT_BTN, Localizable("Create a new tile layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddSwitchLayer,
	Localizable("Add switch layer", "Editor"),
	[&]() { AddSwitchLayer(); },
	[&]() -> bool { return !Map()->SelectedGroup()->m_GameGroup || Map()->m_pSwitchLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Create a new switch layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddTuneLayer,
	Localizable("Add tune layer", "Editor"),
	[&]() { AddTuneLayer(); },
	[&]() -> bool { return !Map()->SelectedGroup()->m_GameGroup || Map()->m_pTuneLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Create a new tuning layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddSpeedupLayer,
	Localizable("Add speedup layer", "Editor"),
	[&]() { AddSpeedupLayer(); },
	[&]() -> bool { return !Map()->SelectedGroup()->m_GameGroup || Map()->m_pSpeedupLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Create a new speedup layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddTeleLayer,
	Localizable("Add tele layer", "Editor"),
	[&]() { AddTeleLayer(); },
	[&]() -> bool { return !Map()->SelectedGroup()->m_GameGroup || Map()->m_pTeleLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Create a new tele layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddFrontLayer,
	Localizable("Add front layer", "Editor"),
	[&]() { AddFrontLayer(); },
	[&]() -> bool { return !Map()->SelectedGroup()->m_GameGroup || Map()->m_pFrontLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Create a new item layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddQuadsLayer, Localizable("Add quad", "Editor"), [&]() { AddQuadsLayer(); }, ALWAYS_FALSE, ALWAYS_FALSE, DEFAULT_BTN, Localizable("Create a new quads layer.", "Editor"))
REGISTER_QUICK_ACTION(
	AddSoundLayer, Localizable("Add sound", "Editor"), [&]() { AddSoundLayer(); }, ALWAYS_FALSE, ALWAYS_FALSE, DEFAULT_BTN, Localizable("Create a new sound layer.", "Editor"))
REGISTER_QUICK_ACTION(
	NewMap,
	Localizable("New map", "Editor"),
	[&]() {
		AddDefaultMap();
		Reset(false);
	},
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Ctrl+N] Create a new map.", "Editor"))
REGISTER_QUICK_ACTION(
	SaveAs,
	Localizable("Save as", "Editor"),
	[&]() {
		char aDefaultName[IO_MAX_PATH_LENGTH];
		fs_split_file_extension(fs_filename(Map()->m_aFilename), aDefaultName, sizeof(aDefaultName));
		m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, Localize("Save map", "Editor"), Localize("Save as", "Editor"), "maps", aDefaultName, CallbackSaveMap, this);
	},
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Ctrl+Shift+S] Save the current map under a new name.", "Editor"))
REGISTER_QUICK_ACTION(
	LoadCurrentMap,
	Localizable("Load current map", "Editor"),
	[&]() {
		if(HasUnsavedData())
		{
			if(!m_PopupEventWasActivated)
			{
				m_PopupEventType = POPEVENT_LOADCURRENT;
				m_PopupEventActivated = true;
			}
		}
		else
		{
			LoadCurrentMap();
		}
	},
	[&]() -> bool { return Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Ctrl+Shift+L] Open the current ingame map for editing.", "Editor"))
REGISTER_QUICK_ACTION(
	CloseMap,
	Localizable("Close map", "Editor"),
	[&]() { CloseMap(m_SelectedMap, true); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Ctrl+F4] Close the current map.", "Editor"))
REGISTER_QUICK_ACTION(
	Envelopes,
	Localizable("Envelope", "Editor"),
	[&]() { m_ActiveExtraEditor = m_ActiveExtraEditor == EXTRAEDITOR_ENVELOPES ? EXTRAEDITOR_NONE : EXTRAEDITOR_ENVELOPES; },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	[&]() -> int { return m_ShowPicker ? -1 : m_ActiveExtraEditor == EXTRAEDITOR_ENVELOPES; },
	Localizable("Toggle the envelope editor.", "Editor"))
REGISTER_QUICK_ACTION(
	ServerSettings,
	Localizable("Server settings", "Editor"),
	[&]() { m_ActiveExtraEditor = m_ActiveExtraEditor == EXTRAEDITOR_SERVER_SETTINGS ? EXTRAEDITOR_NONE : EXTRAEDITOR_SERVER_SETTINGS; },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	[&]() -> int { return m_ShowPicker ? -1 : m_ActiveExtraEditor == EXTRAEDITOR_SERVER_SETTINGS; },
	Localizable("Toggle the server settings editor.", "Editor"))
REGISTER_QUICK_ACTION(
	History,
	Localizable("History", "Editor"),
	[&]() { m_ActiveExtraEditor = m_ActiveExtraEditor == EXTRAEDITOR_HISTORY ? EXTRAEDITOR_NONE : EXTRAEDITOR_HISTORY; },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	[&]() -> int { return m_ShowPicker ? -1 : m_ActiveExtraEditor == EXTRAEDITOR_HISTORY; },
	Localizable("Toggle the editor history view.", "Editor"))
REGISTER_QUICK_ACTION(
	AddImage,
	Localizable("Add image", "Editor"),
	[&]() { m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, Localize("Add image", "Editor"), Localize("Add", "Editor"), "mapres", "", AddImage, this); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Load a new image to use in the map.", "Editor"))
REGISTER_QUICK_ACTION(
	LayerPropAddImage,
	Localizable("Layer: add image", "Editor"),
	[&]() { LayerSelectImage(); },
	[&]() -> bool { return !IsNonGameTileLayerSelected(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Pick mapres image for currently selected layer.", "Editor"))
REGISTER_QUICK_ACTION(
	ShowInfoOff,
	Localizable("Show info: Off", "Editor"),
	[&]() {
		m_ShowTileInfo = SHOW_TILE_OFF;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowTileInfo == SHOW_TILE_OFF; },
	DEFAULT_BTN,
	Localizable("Do not show tile information.", "Editor"))
REGISTER_QUICK_ACTION(
	ShowInfoDec,
	Localizable("Show info: Dec", "Editor"),
	[&]() {
		m_ShowTileInfo = SHOW_TILE_DECIMAL;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowTileInfo == SHOW_TILE_DECIMAL; },
	DEFAULT_BTN,
	Localizable("[Ctrl+I] Show tile information.", "Editor"))
REGISTER_QUICK_ACTION(
	ShowInfoHex,
	Localizable("Show info: Hex", "Editor"),
	[&]() {
		m_ShowTileInfo = SHOW_TILE_HEXADECIMAL;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowTileInfo == SHOW_TILE_HEXADECIMAL; },
	DEFAULT_BTN,
	Localizable("[Ctrl+Shift+I] Show tile information in hexadecimal.", "Editor"))
REGISTER_QUICK_ACTION(
	PreviewQuadEnvelopes,
	Localizable("Preview quad envelopes", "Editor"),
	[&]() {
		m_ShowEnvelopePreview = !m_ShowEnvelopePreview;
		m_ActiveEnvelopePreview = EEnvelopePreview::NONE;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowEnvelopePreview; },
	DEFAULT_BTN,
	Localizable("Toggle previewing the paths of quads with a position envelope when a quad layer is selected.", "Editor"))
REGISTER_QUICK_ACTION(
	DeleteLayer,
	Localizable("Delete layer", "Editor"),
	[&]() { DeleteSelectedLayer(); },
	[&]() -> bool {
		std::shared_ptr<CLayer> pCurrentLayer = Map()->SelectedLayer(0);
		if(!pCurrentLayer)
			return true;
		return Map()->m_pGameLayer == pCurrentLayer;
	},
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Delete the layer.", "Editor"))
REGISTER_QUICK_ACTION(
	Pipette,
	Localizable("Pipette", "Editor"),
	[&]() { m_ColorPipetteActive = !m_ColorPipetteActive; },
	ALWAYS_FALSE,
	[&]() -> bool { return m_ColorPipetteActive; },
	DEFAULT_BTN,
	Localizable("[Ctrl+Shift+C] Color pipette. Pick a color from the screen by clicking on it.", "Editor"))
REGISTER_QUICK_ACTION(
	MapDetails,
	Localizable("Map details", "Editor"),
	[&]() { MapDetails(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Adjust the map details of the current map.", "Editor"))
REGISTER_QUICK_ACTION(
	AddQuad,
	Localizable("Add quad", "Editor"),
	[&]() { AddQuadOrSound(); },
	[&]() -> bool {
		std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(0);
		if(!pLayer)
			return false;
		return pLayer->m_Type != LAYERTYPE_QUADS;
	},
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Ctrl+Q] Add a new quad.", "Editor"))
REGISTER_QUICK_ACTION(
	AddSoundSource,
	Localizable("Add sound source", "Editor"),
	[&]() { AddQuadOrSound(); },
	[&]() -> bool {
		std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(0);
		if(!pLayer)
			return false;
		return pLayer->m_Type != LAYERTYPE_SOUNDS;
	},
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("[Ctrl+Q] Add a new sound source.", "Editor"))
REGISTER_QUICK_ACTION(
	TestMapLocally,
	Localizable("Test map locally", "Editor"),
	[&]() { TestMapLocally(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	Localizable("Run a local server with the current map and connect you to it.", "Editor"))

#undef ALWAYS_FALSE
#undef DEFAULT_BTN
