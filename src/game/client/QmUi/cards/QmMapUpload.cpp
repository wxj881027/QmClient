#include "QmCardCatalogFunctionMetrics.h"

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

const char *CMenus::QmMapUploadPlayerName() const
{
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		const int ClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		return ClientId >= 0 && ClientId < MAX_CLIENTS ? GameClient()->m_aClients[ClientId].m_aName : "";
	}
	return g_Config.m_PlayerName;
}

int CMenus::QmMapUploadScan(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	auto *pPicker = static_cast<CQmMapUploadPicker *>(pUser);
	if(str_comp(pInfo->m_pName, ".") == 0 || str_comp(pInfo->m_pName, "..") == 0 ||
		(!IsDir && !QmMapUpload::IsMapFilename(pInfo->m_pName)))
		return 0;
	SQmMapUploadFile File;
	if(str_length(pPicker->m_aFolder) + str_length(pInfo->m_pName) + 2 > (int)sizeof(File.m_aPath))
		return 0;
	str_copy(File.m_aFilename, pInfo->m_pName);
	str_format(File.m_aPath, sizeof(File.m_aPath), "%s/%s", pPicker->m_aFolder, pInfo->m_pName);
	File.m_IsDirectory = IsDir != 0;
	File.m_StorageType = StorageType;
	pPicker->m_vFiles.push_back(File);
	return 0;
}

void CMenus::PopulateQmMapUploadPicker()
{
	auto &Picker = m_QmMapUploadPicker;
	Picker.m_vFiles.clear();
	Picker.m_Selected = -1;
	Picker.m_ListBox.ResetScroll();
	if(!Picker.m_SearchInput.IsEmpty())
	{
		Picker.m_vFiles = Picker.m_SearchIndex.Find(Picker.m_SearchInput.GetString());
		return;
	}
	if(Picker.m_aFolder[0] == '\0')
	{
		for(const char *pFolder : {"maps", "downloadedmaps"})
		{
			SQmMapUploadFile Folder;
			str_copy(Folder.m_aFilename, pFolder);
			Folder.m_IsDirectory = true;
			Picker.m_vFiles.push_back(Folder);
		}
		return;
	}
	SQmMapUploadFile Parent;
	str_copy(Parent.m_aFilename, "..");
	Parent.m_IsDirectory = true;
	Picker.m_vFiles.push_back(Parent);
	Storage()->ListDirectoryInfo(Picker.m_StorageType, Picker.m_aFolder, QmMapUploadScan, &Picker);
	std::stable_sort(Picker.m_vFiles.begin() + 1, Picker.m_vFiles.end(), [](const SQmMapUploadFile &Left, const SQmMapUploadFile &Right) {
		if(Left.m_IsDirectory != Right.m_IsDirectory)
			return Left.m_IsDirectory;
		const int NameOrder = str_comp_filenames(Left.m_aFilename, Right.m_aFilename);
		return NameOrder != 0 ? NameOrder < 0 : Left.m_StorageType < Right.m_StorageType;
	});
}

CUi::EPopupMenuFunctionResult CMenus::PopupQmMapUploadPicker(void *pContext, CUIRect View, bool Active)
{
	auto *pPicker = static_cast<CQmMapUploadPicker *>(pContext);
	CMenus *pMenus = pPicker->m_pMenus;
	if(pMenus == nullptr || pMenus->m_QmMapUpload.Busy())
		return CUi::POPUP_CLOSE_CURRENT;

	const SSettingsContentMetrics Metrics = pMenus->CurrentSettingsContentMetrics();
	View.Margin(Metrics.m_LineSpacing, &View);
	CUIRect Path, Search, Scope, Footer;
	View.HSplitTop(Metrics.m_LineHeight, &Path, &View);
	View.HSplitTop(Metrics.m_LineHeight, &Search, &View);
	View.HSplitTop(Metrics.m_LineSpacing, nullptr, &View);
	View.HSplitTop(Metrics.m_LineHeight, &Scope, &View);
	View.HSplitBottom(Metrics.m_ButtonHeight, &View, &Footer);
	View.HSplitBottom(Metrics.m_LineSpacing, &View, nullptr);
	SLabelProperties SingleLine;
	SingleLine.m_MaxWidth = Path.w;
	SingleLine.m_DisallowNewline = true;
	SingleLine.m_EllipsisAtEnd = true;
	SingleLine.m_EnableWidthCheck = false;
	pMenus->Ui()->DoLabel(&Path, pPicker->m_aFolder[0] ? pPicker->m_aFolder : Localize("Map folders"), Metrics.m_BodySize, TEXTALIGN_ML, SingleLine);
	const bool QueryChanged = pMenus->Ui()->DoEditBox_Search(&pPicker->m_SearchInput, &Search, Metrics.m_BodySize, Active);
	const bool Searching = !pPicker->m_SearchInput.IsEmpty();
	// 按下到松开鼠标期间保持列表稳定，避免新增结果挤动正在点击的行。
	const bool IndexChanged = Active && Searching && !pMenus->Ui()->MouseButton(0) && !pMenus->Ui()->LastMouseButton(0) && pPicker->m_SearchIndex.ScanNext(pMenus->Storage());
	if(QueryChanged)
		pMenus->PopulateQmMapUploadPicker();
	else if(IndexChanged)
	{
		// 新结果到达时按路径和来源恢复选择，避免排序变化选中另一张地图。
		SQmMapUploadFile Previous;
		if(pPicker->m_Selected >= 0 && pPicker->m_Selected < (int)pPicker->m_vFiles.size())
			Previous = pPicker->m_vFiles[pPicker->m_Selected];
		pPicker->m_vFiles = pPicker->m_SearchIndex.Find(pPicker->m_SearchInput.GetString());
		pPicker->m_Selected = -1;
		for(size_t Index = 0; Index < pPicker->m_vFiles.size(); ++Index)
		{
			const auto &File = pPicker->m_vFiles[Index];
			if(File.m_StorageType == Previous.m_StorageType && str_comp(File.m_aPath, Previous.m_aPath) == 0)
				pPicker->m_Selected = (int)Index;
		}
	}
	pMenus->Ui()->DoLabel(&Scope, Searching && pPicker->m_SearchIndex.Busy() ? Localize("Searching all map folders...") : Localize("Search maps and downloadedmaps, including subfolders"), Metrics.m_SmallSize, TEXTALIGN_ML, SingleLine);
	if(pMenus->Ui()->DoButton_PopupMenu(&pPicker->m_CancelButton, Localize("Cancel"), &Footer, Metrics.m_BodySize, TEXTALIGN_MC) ||
		(Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)))
	{
		pMenus->Ui()->ReleaseActiveTextInput(&pPicker->m_SearchInput);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	CListBox &List = pPicker->m_ListBox;
	List.SetActive(Active);
	List.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	List.SetScrollProfile(EQmScrollProfile::POPUP_LIST);
	List.DoStart(Metrics.m_LineHeight + Metrics.m_SmallSize + Metrics.m_LineSpacing, (int)pPicker->m_vFiles.size(), 1, 3, pPicker->m_Selected, &View, false);
	for(size_t Index = 0; Index < pPicker->m_vFiles.size(); ++Index)
	{
		const SQmMapUploadFile &File = pPicker->m_vFiles[Index];
		const CListboxItem Item = List.DoNextItem(&File, (int)Index == pPicker->m_Selected);
		if(!Item.m_Visible)
			continue;
		CUIRect Name = Item.m_Rect, Source;
		Name.HSplitTop(Metrics.m_LineHeight, &Name, &Source);
		char aLabel[IO_MAX_PATH_LENGTH + 2];
		str_format(aLabel, sizeof(aLabel), "%s%s", File.m_aFilename, File.m_IsDirectory ? "/" : "");
		SingleLine.m_MaxWidth = Name.w;
		pMenus->Ui()->DoLabel(&Name, aLabel, Metrics.m_BodySize, TEXTALIGN_ML, SingleLine);
		// 同名地图可能来自不同存储目录，显示实际来源并保留上传时的 StorageType。
		if(File.m_StorageType >= 0)
		{
			char aSource[IO_MAX_PATH_LENGTH];
			pMenus->Storage()->GetCompletePath(File.m_StorageType, File.m_aPath, aSource, sizeof(aSource));
			pMenus->Ui()->DoLabel(&Source, aSource, Metrics.m_SmallSize, TEXTALIGN_ML, SingleLine);
		}
	}
	const int Selected = List.DoEnd();
	pPicker->m_Selected = Selected >= 0 && Selected < (int)pPicker->m_vFiles.size() ? Selected : -1;
	if((pPicker->m_vFiles.empty() && (!Searching || !pPicker->m_SearchIndex.Busy())) ||
		(pPicker->m_vFiles.size() == 1 && str_comp(pPicker->m_vFiles.front().m_aFilename, "..") == 0))
		pMenus->Ui()->DoLabel(&View, Localize("No saved maps found"), Metrics.m_BodySize, TEXTALIGN_MC);
	if(!Active || pPicker->m_Selected < 0 || (!List.WasItemSelected() && !List.WasItemActivated()))
		return CUi::POPUP_KEEP_OPEN;

	const SQmMapUploadFile SelectedFile = pPicker->m_vFiles[pPicker->m_Selected];
	if(SelectedFile.m_IsDirectory)
	{
		if(str_comp(SelectedFile.m_aFilename, "..") == 0)
		{
			if(str_comp(pPicker->m_aFolder, "maps") == 0 || str_comp(pPicker->m_aFolder, "downloadedmaps") == 0)
				pPicker->m_aFolder[0] = '\0';
			else
				fs_parent_dir(pPicker->m_aFolder);
			if(pPicker->m_aFolder[0] == '\0' || str_comp(pPicker->m_aFolder, "maps") == 0 || str_comp(pPicker->m_aFolder, "downloadedmaps") == 0)
				pPicker->m_StorageType = IStorage::TYPE_ALL;
		}
		else
		{
			const size_t Needed = str_length(pPicker->m_aFolder) + str_length(SelectedFile.m_aFilename) + 2;
			if(Needed > sizeof(pPicker->m_aFolder))
				return CUi::POPUP_KEEP_OPEN;
			if(pPicker->m_aFolder[0])
				str_append(pPicker->m_aFolder, "/");
			str_append(pPicker->m_aFolder, SelectedFile.m_aFilename);
			pPicker->m_StorageType = SelectedFile.m_StorageType;
		}
		pMenus->PopulateQmMapUploadPicker();
		return CUi::POPUP_KEEP_OPEN;
	}

	str_copy(pMenus->m_aQmMapUploadPath, SelectedFile.m_aPath);
	pMenus->m_QmMapUploadStorageType = SelectedFile.m_StorageType;
	pMenus->m_QmMapUpload.Reset();
	pMenus->Ui()->ReleaseActiveTextInput(&pPicker->m_SearchInput);
	pMenus->Ui()->SetActiveItem(nullptr);
	return CUi::POPUP_CLOSE_CURRENT;
}

void CMenus::RenderQmFunctionMapUploadContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const bool Busy = m_QmMapUpload.Busy();
	auto NextRow = [&](float Height) {
		CUIRect Row;
		Content.HSplitTop(Height, &Row, &Content);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		return Row;
	};
	SLabelProperties SingleLine;
	SingleLine.m_MaxWidth = Content.w;
	SingleLine.m_DisallowNewline = true;
	SingleLine.m_EllipsisAtEnd = true;
	SingleLine.m_EnableWidthCheck = false;
	char aText[IO_MAX_PATH_LENGTH + 128];
	CUIRect Row = NextRow(LineHeight);
	str_format(aText, sizeof(aText), Localize("Target server: %s"), g_Config.m_QmMapUploadEndpoint[0] ? g_Config.m_QmMapUploadEndpoint : Localize("None"));
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-map-upload-server", &Row, aText, BodySize, TEXTALIGN_ML, SingleLine);
	Row = NextRow(LineHeight);
	str_format(aText, sizeof(aText), Localize("File: %s"), m_aQmMapUploadPath[0] ? m_aQmMapUploadPath : Localize("No map selected"));
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-map-upload-file", &Row, aText, BodySize, TEXTALIGN_ML, SingleLine);
	Row = NextRow(LineHeight);
	str_format(aText, sizeof(aText), Localize("Player: %s"), Busy ? m_aQmMapUploadPlayer : QmMapUploadPlayerName());
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-map-upload-player", &Row, aText, BodySize, TEXTALIGN_ML, SingleLine);

	Row = NextRow(LineHeight);
	CUIRect SelectButton, ActionButton;
	Row.VSplitMid(&SelectButton, &ActionButton, LineSpacing);
	static CButtonContainer s_SelectButton, s_UploadButton, s_CancelButton;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, &s_SelectButton, "qmclient-map-upload-select", Localize("Select map"), Busy || ReadOnly ? -1 : 0, &SelectButton) && !Busy && !ReadOnly)
	{
		m_QmMapUploadPicker.m_pMenus = this;
		m_QmMapUploadPicker.m_SearchInput.Clear();
		m_QmMapUploadPicker.m_SearchIndex.Reset(Storage()->NumPaths());
		PopulateQmMapUploadPicker();
		const CUIRect Screen = *Ui()->Screen();
		const float Width = std::min(520.0f, Screen.w - 20.0f);
		const float Height = std::min(360.0f, Screen.h - 20.0f);
		SPopupMenuProperties Props;
		Props.m_BlockUnderlyingScroll = true;
		Props.m_BlockUnderlyingPointerInput = true;
		Ui()->DoPopupMenu(&m_QmMapUploadPicker, Screen.x + (Screen.w - Width) * 0.5f, Screen.y + (Screen.h - Height) * 0.5f, Width, Height, &m_QmMapUploadPicker, PopupQmMapUploadPicker, Props);
	}
	if(Busy)
	{
		const bool CanCancel = !ReadOnly && m_QmMapUpload.Status() == QmMapUpload::EStatus::UPLOADING;
		if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, &s_CancelButton, "qmclient-map-upload-cancel", Localize("Cancel"), CanCancel ? 0 : -1, &ActionButton) && CanCancel)
			m_QmMapUpload.Cancel();
	}
	else
	{
		const bool CanUpload = !ReadOnly && m_aQmMapUploadPath[0] != '\0';
		if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, &s_UploadButton, "qmclient-map-upload-submit", Localize("Upload map"), CanUpload ? 0 : -1, &ActionButton) && CanUpload)
		{
			// 提交时快照当前控制角色的服务端名字，随后切换分身不会改变本次上传。
			str_copy(m_aQmMapUploadPlayer, QmMapUploadPlayerName());
			// 本地差异：远程的 CUpload::Start 已去掉 endpoint 参数（内部固定），
			// 本地保留「endpoint 可由用户配置」（g_Config.m_QmMapUploadEndpoint，界面上可填），
			// 故此处按本地 7 参签名补上该参数，不牺牲本地能力。
			m_QmMapUpload.Start(Storage(), Http(), Engine(), g_Config.m_QmMapUploadEndpoint, m_aQmMapUploadPath, m_QmMapUploadStorageType, m_aQmMapUploadPlayer);
		}
	}

	const char *pStatus = Localize("Select a saved .map file (client limit: 64 MiB).");
	switch(m_QmMapUpload.Status())
	{
	case QmMapUpload::EStatus::IDLE: break;
	case QmMapUpload::EStatus::UPLOADING: pStatus = Localize("Map upload in progress..."); break;
	case QmMapUpload::EStatus::SUCCESS: pStatus = Localize("Map uploaded successfully."); break;
	case QmMapUpload::EStatus::CANCELLED: pStatus = Localize("Map upload cancelled."); break;
	case QmMapUpload::EStatus::INVALID_FILE: pStatus = Localize("Invalid map file or filename."); break;
	case QmMapUpload::EStatus::TOO_LARGE: pStatus = Localize("Map exceeds the client limit of 64 MiB."); break;
	case QmMapUpload::EStatus::READ_FAILED: pStatus = Localize("Could not read the map file."); break;
	case QmMapUpload::EStatus::NETWORK_ERROR: pStatus = Localize("Map upload network error."); break;
	case QmMapUpload::EStatus::SERVER_ERROR: pStatus = Localize("The server rejected the map upload."); break;
	case QmMapUpload::EStatus::INVALID_RESPONSE: pStatus = Localize("Unexpected map upload response."); break;
	case QmMapUpload::EStatus::MISSING_PLAYER: pStatus = Localize("A player name is required."); break;
	case QmMapUpload::EStatus::INVALID_ENDPOINT: pStatus = Localize("Map upload endpoint must use HTTP or HTTPS"); break;
	}
	char aStatus[256];
	if(m_QmMapUpload.Detail().empty() && m_QmMapUpload.StatusCode() > 0 &&
		(m_QmMapUpload.Status() == QmMapUpload::EStatus::SERVER_ERROR || m_QmMapUpload.Status() == QmMapUpload::EStatus::INVALID_RESPONSE))
	{
		str_format(aStatus, sizeof(aStatus), "%s (HTTP %d)", pStatus, m_QmMapUpload.StatusCode());
		pStatus = aStatus;
	}
	SLabelProperties Wrapped;
	Wrapped.m_MaxWidth = Content.w;
	Wrapped.m_EnableWidthCheck = false;
	Row = NextRow(LineHeight * 2.0f);
	Ui()->ClipEnable(&Row);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-map-upload-status", &Row, pStatus, BodySize, TEXTALIGN_TL, Wrapped);
	Ui()->ClipDisable();
	Row = NextRow(LineHeight * 2.0f);
	// 服务端消息只按普通文本显示，固定区域允许换行并裁剪过长内容。
	if(!m_QmMapUpload.Detail().empty())
	{
		char aDetail[768];
		str_copy(aDetail, m_QmMapUpload.Detail().c_str());
		Ui()->ClipEnable(&Row);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-map-upload-detail", &Row, aDetail, BodySize, TEXTALIGN_TL, Wrapped);
		Ui()->ClipDisable();
	}
	const auto Instructions = qm_card_catalog::QmMapUploadInstructions();
	for(size_t Index = 0; Index < Instructions.size(); ++Index)
	{
		Row = NextRow(qm_card_catalog::QmMapUploadHelpLineHeight(TextRender(), Instructions[Index], Content.w, BodySize, LineHeight));
		char aId[64];
		str_format(aId, sizeof(aId), "qmclient-map-upload-help-%d", (int)Index);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, aId, &Row, Instructions[Index], BodySize, TEXTALIGN_TL, Wrapped);
	}
}
