// This file can be included several times.

#ifndef REGISTER_QUICK_ACTION
// This helps IDEs properly syntax highlight the uses of the macro below.
#define REGISTER_QUICK_ACTION(name, text, callback, disabled, active, button_color, description)
#endif

#define ALWAYS_FALSE []() -> bool { return false; }
#define DEFAULT_BTN []() -> int { return -1; }

REGISTER_QUICK_ACTION(
	ToggleGrid,
	"切换网格",
	[&]() { MapView()->MapGrid()->Toggle(); },
	ALWAYS_FALSE,
	[&]() -> bool { return MapView()->MapGrid()->IsEnabled(); },
	DEFAULT_BTN,
	"[Ctrl+G] 切换网格.")
REGISTER_QUICK_ACTION(
	GameTilesAir,
	"游戏图块: 空气",
	[&]() { FillGameTiles(EGameTileOp::AIR); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesHookable,
	"游戏图块: 可钩",
	[&]() { FillGameTiles(EGameTileOp::HOOKABLE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesDeath,
	"游戏图块: 死亡",
	[&]() { FillGameTiles(EGameTileOp::DEATH); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesUnhookable,
	"游戏图块: 不可钩",
	[&]() { FillGameTiles(EGameTileOp::UNHOOKABLE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesHookthrough,
	"游戏图块: 可穿透钩",
	[&]() { FillGameTiles(EGameTileOp::HOOKTHROUGH); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesFreeze,
	"游戏图块: 冻结",
	[&]() { FillGameTiles(EGameTileOp::FREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesUnfreeze,
	"游戏图块: 解冻",
	[&]() { FillGameTiles(EGameTileOp::UNFREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesDeepFreeze,
	"游戏图块: 深度冻结",
	[&]() { FillGameTiles(EGameTileOp::DEEP_FREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesDeepUnfreeze,
	"游戏图块: 深度解冻",
	[&]() { FillGameTiles(EGameTileOp::DEEP_UNFREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesBlueCheckTele,
	"游戏图块: 蓝色检查点传送",
	[&]() { FillGameTiles(EGameTileOp::BLUE_CHECK_TELE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesRedCheckTele,
	"游戏图块: 红色检查点传送",
	[&]() { FillGameTiles(EGameTileOp::RED_CHECK_TELE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesLiveFreeze,
	"游戏图块: 实时冻结",
	[&]() { FillGameTiles(EGameTileOp::LIVE_FREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	GameTilesLiveUnfreeze,
	"游戏图块: 实时解冻",
	[&]() { FillGameTiles(EGameTileOp::LIVE_UNFREEZE); },
	[&]() -> bool { return !CanFillGameTiles(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"从此层构建游戏图块.")
REGISTER_QUICK_ACTION(
	AddGroup,
	"添加组",
	[&]() { AddGroup(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"添加一个新组.")
REGISTER_QUICK_ACTION(
	ResetZoom,
	"重置缩放",
	[&]() { MapView()->ResetZoom(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Numpad*] 缩放到正常并移除编辑器偏移.")
REGISTER_QUICK_ACTION(
	ZoomOut,
	"缩小",
	[&]() { MapView()->Zoom()->ChangeValue(50.0f); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Numpad-] 缩小.")
REGISTER_QUICK_ACTION(
	ZoomIn,
	"放大",
	[&]() { MapView()->Zoom()->ChangeValue(-50.0f); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Numpad+] 放大.")
REGISTER_QUICK_ACTION(
	Refocus,
	"重新聚焦",
	[&]() { MapView()->Focus(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Home] 恢复地图焦点.")
REGISTER_QUICK_ACTION(
	Proof,
	"验证模式",
	[&]() { MapView()->ProofMode()->Toggle(); },
	ALWAYS_FALSE,
	[&]() -> bool { return MapView()->ProofMode()->IsEnabled(); },
	DEFAULT_BTN,
	"切换验证边框. 这些边框代表玩家在默认缩放下可以看到的区域.")
REGISTER_QUICK_ACTION(
	AddTileLayer, "添加图块层", [&]() { AddTileLayer(); }, ALWAYS_FALSE, ALWAYS_FALSE, DEFAULT_BTN, "创建一个新的图块层.")
REGISTER_QUICK_ACTION(
	AddSwitchLayer,
	"添加开关层",
	[&]() { AddSwitchLayer(); },
	[&]() -> bool { return !GetSelectedGroup()->m_GameGroup || m_Map.m_pSwitchLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"创建一个新的开关层.")
REGISTER_QUICK_ACTION(
	AddTuneLayer,
	"添加调整层",
	[&]() { AddTuneLayer(); },
	[&]() -> bool { return !GetSelectedGroup()->m_GameGroup || m_Map.m_pTuneLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"创建一个新的调整层.")
REGISTER_QUICK_ACTION(
	AddSpeedupLayer,
	"添加加速层",
	[&]() { AddSpeedupLayer(); },
	[&]() -> bool { return !GetSelectedGroup()->m_GameGroup || m_Map.m_pSpeedupLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"创建一个新的加速层.")
REGISTER_QUICK_ACTION(
	AddTeleLayer,
	"添加传送层",
	[&]() { AddTeleLayer(); },
	[&]() -> bool { return !GetSelectedGroup()->m_GameGroup || m_Map.m_pTeleLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"创建一个新的传送层.")
REGISTER_QUICK_ACTION(
	AddFrontLayer,
	"添加前景层",
	[&]() { AddFrontLayer(); },
	[&]() -> bool { return !GetSelectedGroup()->m_GameGroup || m_Map.m_pFrontLayer; },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"创建一个新的项目层.")
REGISTER_QUICK_ACTION(
	AddQuadsLayer, "添加四边形层", [&]() { AddQuadsLayer(); }, ALWAYS_FALSE, ALWAYS_FALSE, DEFAULT_BTN, "创建一个新的四边形层.")
REGISTER_QUICK_ACTION(
	AddSoundLayer, "添加声音层", [&]() { AddSoundLayer(); }, ALWAYS_FALSE, ALWAYS_FALSE, DEFAULT_BTN, "创建一个新的声音层.")
REGISTER_QUICK_ACTION(
	SaveAs,
	"另存为",
	[&]() {
		char aDefaultName[IO_MAX_PATH_LENGTH];
		fs_split_file_extension(fs_filename(m_aFilename), aDefaultName, sizeof(aDefaultName));
		m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, "保存地图", "另存为", "maps", aDefaultName, CallbackSaveMap, this);
	},
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Ctrl+Shift+S] 以新名称保存当前地图.")
REGISTER_QUICK_ACTION(
	LoadCurrentMap,
	"加载当前地图",
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
	"[Ctrl+Shift+L] 打开当前游戏中的地图进行编辑.")
REGISTER_QUICK_ACTION(
	Envelopes,
	"包络线",
	[&]() { m_ActiveExtraEditor = m_ActiveExtraEditor == EXTRAEDITOR_ENVELOPES ? EXTRAEDITOR_NONE : EXTRAEDITOR_ENVELOPES; },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	[&]() -> int { return m_ShowPicker ? -1 : m_ActiveExtraEditor == EXTRAEDITOR_ENVELOPES; },
	"切换包络线编辑器.")
REGISTER_QUICK_ACTION(
	ServerSettings,
	"服务器设置",
	[&]() { m_ActiveExtraEditor = m_ActiveExtraEditor == EXTRAEDITOR_SERVER_SETTINGS ? EXTRAEDITOR_NONE : EXTRAEDITOR_SERVER_SETTINGS; },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	[&]() -> int { return m_ShowPicker ? -1 : m_ActiveExtraEditor == EXTRAEDITOR_SERVER_SETTINGS; },
	"切换服务器设置编辑器.")
REGISTER_QUICK_ACTION(
	History,
	"历史记录",
	[&]() { m_ActiveExtraEditor = m_ActiveExtraEditor == EXTRAEDITOR_HISTORY ? EXTRAEDITOR_NONE : EXTRAEDITOR_HISTORY; },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	[&]() -> int { return m_ShowPicker ? -1 : m_ActiveExtraEditor == EXTRAEDITOR_HISTORY; },
	"切换编辑器历史记录视图.")
REGISTER_QUICK_ACTION(
	AddImage,
	"添加图像",
	[&]() { m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::IMAGE, "添加图像", "添加", "mapres", "", AddImage, this); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"加载一个新图像在地图中使用.")
REGISTER_QUICK_ACTION(
	LayerPropAddImage,
	"层: 添加图像",
	[&]() { LayerSelectImage(); },
	[&]() -> bool { return !IsNonGameTileLayerSelected(); },
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"为当前选中的层选择mapres图像.")
REGISTER_QUICK_ACTION(
	ShowInfoOff,
	"显示信息: 关闭",
	[&]() {
		m_ShowTileInfo = SHOW_TILE_OFF;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowTileInfo == SHOW_TILE_OFF; },
	DEFAULT_BTN,
	"不显示图块信息.")
REGISTER_QUICK_ACTION(
	ShowInfoDec,
	"显示信息: 十进制",
	[&]() {
		m_ShowTileInfo = SHOW_TILE_DECIMAL;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowTileInfo == SHOW_TILE_DECIMAL; },
	DEFAULT_BTN,
	"[Ctrl+I] 显示图块信息.")
REGISTER_QUICK_ACTION(
	ShowInfoHex,
	"显示信息: 十六进制",
	[&]() {
		m_ShowTileInfo = SHOW_TILE_HEXADECIMAL;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowTileInfo == SHOW_TILE_HEXADECIMAL; },
	DEFAULT_BTN,
	"[Ctrl+Shift+I] 以十六进制显示图块信息.")
REGISTER_QUICK_ACTION(
	PreviewQuadEnvelopes,
	"预览四边形包络线",
	[&]() {
		m_ShowEnvelopePreview = !m_ShowEnvelopePreview;
		m_ActiveEnvelopePreview = EEnvelopePreview::NONE;
	},
	ALWAYS_FALSE,
	[&]() -> bool { return m_ShowEnvelopePreview; },
	DEFAULT_BTN,
	"当选中四边形层时,切换预览带有位置包络线的四边形路径.")
REGISTER_QUICK_ACTION(
	DeleteLayer,
	"删除层",
	[&]() { DeleteSelectedLayer(); },
	[&]() -> bool {
		std::shared_ptr<CLayer> pCurrentLayer = GetSelectedLayer(0);
		if(!pCurrentLayer)
			return true;
		return m_Map.m_pGameLayer == pCurrentLayer;
	},
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"删除该层.")
REGISTER_QUICK_ACTION(
	Pipette,
	"吸管",
	[&]() { m_ColorPipetteActive = !m_ColorPipetteActive; },
	ALWAYS_FALSE,
	[&]() -> bool { return m_ColorPipetteActive; },
	DEFAULT_BTN,
	"[Ctrl+Shift+C] 颜色吸管. 通过点击屏幕从中选取颜色.")
REGISTER_QUICK_ACTION(
	MapDetails,
	"地图详情",
	[&]() { MapDetails(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"调整当前地图的地图详情.")
REGISTER_QUICK_ACTION(
	AddQuad,
	"添加四边形",
	[&]() { AddQuadOrSound(); },
	[&]() -> bool {
		std::shared_ptr<CLayer> pLayer = GetSelectedLayer(0);
		if(!pLayer)
			return false;
		return pLayer->m_Type != LAYERTYPE_QUADS;
	},
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Ctrl+Q] 添加一个新的四边形.")
REGISTER_QUICK_ACTION(
	AddSoundSource,
	"添加声音源",
	[&]() { AddQuadOrSound(); },
	[&]() -> bool {
		std::shared_ptr<CLayer> pLayer = GetSelectedLayer(0);
		if(!pLayer)
			return false;
		return pLayer->m_Type != LAYERTYPE_SOUNDS;
	},
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"[Ctrl+Q] 添加一个新的声音源.")
REGISTER_QUICK_ACTION(
	TestMapLocally,
	"本地测试地图",
	[&]() { TestMapLocally(); },
	ALWAYS_FALSE,
	ALWAYS_FALSE,
	DEFAULT_BTN,
	"运行一个使用当前地图的本地服务器并连接到它.")

#undef ALWAYS_FALSE
#undef DEFAULT_BTN
