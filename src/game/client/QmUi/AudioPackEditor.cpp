/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/AudioPackEditorLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace FontIcons;

void CMenus::AudioPackEditorOpen(const char *pPackName)
{
	AudioPackEditorStopPreview();
	g_Config.m_UiSettingsPage = SETTINGS_SOUND;
	m_AudioPackEditorState.m_Open = true;
	g_Config.m_UiSettingsPage = SETTINGS_SOUND;
	m_AudioPackEditorState.m_Initialized = false;
	m_AudioPackEditorState.m_SelectedSlotIndex = 0;
	m_AudioPackEditorState.m_Category = 0;
	m_AudioPackEditorState.m_SelectedCandidateIndex = -1;
	m_AudioPackEditorState.m_StatusIsError = false;
	m_AudioPackEditorState.m_aStatusMessage[0] = '\0';
	m_AudioPackEditorState.m_FilterInput.Clear();
	m_AudioPackEditorState.m_CandidateFilterInput.Clear();
	m_AudioPackEditorState.m_SourcePathInput.Clear();
	if(pPackName != nullptr && pPackName[0] != '\0')
		m_AudioPackEditorState.m_PackNameInput.Set(pPackName);
	else
		m_AudioPackEditorState.m_PackNameInput.Set("default");
}

void CMenus::AudioPackEditorClose()
{
	AudioPackEditorStopPreview();
	m_AudioPackEditorState.m_Open = false;
	m_AudioPackEditorState.m_Initialized = false;
	m_AudioPackEditorState.m_SelectedCandidateIndex = -1;
	m_AudioPackEditorState.m_vCandidateEntries.clear();
	m_AudioPackEditorState.m_vCandidatePaths.clear();
}

void CMenus::AudioPackEditorSetStatus(const char *pMessage, bool IsError)
{
	m_AudioPackEditorState.m_StatusIsError = IsError;
	str_copy(m_AudioPackEditorState.m_aStatusMessage, pMessage != nullptr ? pMessage : "", sizeof(m_AudioPackEditorState.m_aStatusMessage));
}

namespace
{
	struct SAudioPackCandidateScanContext
	{
		IStorage *m_pStorage = nullptr;
		std::set<std::string> *m_pEntries = nullptr;
		char m_aScanRoot[IO_MAX_PATH_LENGTH] = "";
		char m_aOutputPrefix[IO_MAX_PATH_LENGTH] = "";
		char m_aRelativePath[IO_MAX_PATH_LENGTH] = "";
	};

}

namespace
{

	static int AudioPackCandidateScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;

		auto *pContext = static_cast<SAudioPackCandidateScanContext *>(pUser);
		if(!str_comp(pInfo->m_pName, ".") || !str_comp(pInfo->m_pName, ".."))
			return 0;

		char aRelativePath[IO_MAX_PATH_LENGTH];
		if(pContext->m_aRelativePath[0] != '\0')
			str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pContext->m_aRelativePath, pInfo->m_pName);
		else
			str_copy(aRelativePath, pInfo->m_pName);

		char aScanPath[IO_MAX_PATH_LENGTH];
		str_format(aScanPath, sizeof(aScanPath), "%s/%s", pContext->m_aScanRoot, aRelativePath);

		if(IsDir)
		{
			if(pInfo->m_pName[0] == '.')
				return 0;

			SAudioPackCandidateScanContext NextContext = *pContext;
			str_copy(NextContext.m_aRelativePath, aRelativePath, sizeof(NextContext.m_aRelativePath));
			pContext->m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, aScanPath, AudioPackCandidateScanCallback, &NextContext);
			return 0;
		}

		std::string CandidatePath;
		if(CMenus::TryBuildAudioPackCandidatePathFromScan(pContext->m_aOutputPrefix, aRelativePath, CandidatePath))
			pContext->m_pEntries->insert(std::move(CandidatePath));

		return 0;
	}

	static const char *ResolveAudioPackEditorPackName(const CLineInputBuffered<64> &PackNameInput, const char *pFallbackPackName)
	{
		if(PackNameInput.GetString()[0] != '\0')
			return PackNameInput.GetString();
		return pFallbackPackName != nullptr ? pFallbackPackName : "";
	}

	static void ResolveAudioPackEditorCurrentFilePath(IStorage *pStorage, const char *pPackName, const CMenus::SAudioPackSlot &Slot, char *pOut, int OutSize)
	{
		pOut[0] = '\0';

		char aDirectPath[IO_MAX_PATH_LENGTH];
		char aLegacyPath[IO_MAX_PATH_LENGTH];
		char aBuiltinPath[IO_MAX_PATH_LENGTH];

		str_copy(aDirectPath, CMenus::BuildAudioPackExportPath(pPackName, Slot.m_pRelativePath).c_str(), sizeof(aDirectPath));
		str_format(aLegacyPath, sizeof(aLegacyPath), "audio/%s/audio/%s", pPackName, Slot.m_pRelativePath);
		str_copy(aBuiltinPath, CMenus::BuildAudioPackBuiltinCandidatePath(Slot.m_pRelativePath).c_str(), sizeof(aBuiltinPath));

		if(pStorage->FileExists(aDirectPath, IStorage::TYPE_ALL))
			str_copy(pOut, aDirectPath, OutSize);
		else if(pStorage->FileExists(aLegacyPath, IStorage::TYPE_ALL))
			str_copy(pOut, aLegacyPath, OutSize);
		else if(pStorage->FileExists(aBuiltinPath, IStorage::TYPE_ALL))
			str_copy(pOut, aBuiltinPath, OutSize);
	}

}

void CMenus::AudioPackEditorRefreshCandidates()
{
	std::set<std::string> vCandidatePaths;
	for(const auto &ScanRoot : BuildAudioPackCandidateScanRoots())
	{
		if(!Storage()->FolderExists(ScanRoot.m_pScanRoot, IStorage::TYPE_ALL))
			continue;

		SAudioPackCandidateScanContext Context;
		Context.m_pStorage = Storage();
		Context.m_pEntries = &vCandidatePaths;
		str_copy(Context.m_aScanRoot, ScanRoot.m_pScanRoot, sizeof(Context.m_aScanRoot));
		str_copy(Context.m_aOutputPrefix, ScanRoot.m_pOutputPrefix, sizeof(Context.m_aOutputPrefix));
		Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, ScanRoot.m_pScanRoot, AudioPackCandidateScanCallback, &Context);
	}

	m_AudioPackEditorState.m_vCandidatePaths.assign(vCandidatePaths.begin(), vCandidatePaths.end());
	AudioPackEditorRebuildCandidates();
}

void CMenus::AudioPackEditorRebuildCandidates()
{
	char aCurrentPath[IO_MAX_PATH_LENGTH] = "";
	const auto vSlots = BuildAudioPackSlots();
	if(!vSlots.empty())
	{
		m_AudioPackEditorState.m_SelectedSlotIndex = std::clamp(m_AudioPackEditorState.m_SelectedSlotIndex, 0, (int)vSlots.size() - 1);
		ResolveAudioPackEditorCurrentFilePath(Storage(), ResolveAudioPackEditorPackName(m_AudioPackEditorState.m_PackNameInput, g_Config.m_SndPack), vSlots[m_AudioPackEditorState.m_SelectedSlotIndex], aCurrentPath, sizeof(aCurrentPath));
	}

	std::string SelectedPath;
	if(m_AudioPackEditorState.m_SelectedCandidateIndex >= 0 && m_AudioPackEditorState.m_SelectedCandidateIndex < (int)m_AudioPackEditorState.m_vCandidateEntries.size())
		SelectedPath = m_AudioPackEditorState.m_vCandidateEntries[m_AudioPackEditorState.m_SelectedCandidateIndex].m_Path;

	m_AudioPackEditorState.m_vCandidateEntries = BuildAudioPackCandidateEntries(m_AudioPackEditorState.m_vCandidatePaths, ResolveAudioPackEditorPackName(m_AudioPackEditorState.m_PackNameInput, g_Config.m_SndPack), aCurrentPath);

	int SelectedIndex = FindAudioPackCandidateEntryIndex(m_AudioPackEditorState.m_vCandidateEntries, aCurrentPath);
	if(SelectedIndex < 0 && !SelectedPath.empty())
		SelectedIndex = FindAudioPackCandidateEntryIndex(m_AudioPackEditorState.m_vCandidateEntries, SelectedPath.c_str());
	if(SelectedIndex < 0 && !m_AudioPackEditorState.m_vCandidateEntries.empty())
		SelectedIndex = 0;
	m_AudioPackEditorState.m_SelectedCandidateIndex = SelectedIndex;
}

void CMenus::AudioPackEditorStopPreview()
{
	if(m_AudioPackEditorState.m_PreviewSampleId >= 0)
	{
		Sound()->Stop(m_AudioPackEditorState.m_PreviewSampleId);
		Sound()->UnloadSample(m_AudioPackEditorState.m_PreviewSampleId);
		m_AudioPackEditorState.m_PreviewSampleId = -1;
	}
}

bool CMenus::AudioPackEditorPlayPreview(const char *pFilename, int StorageType)
{
	if(pFilename == nullptr || pFilename[0] == '\0')
		return false;

	AudioPackEditorStopPreview();

	const int SampleId = Sound()->LoadWV(pFilename, StorageType);
	if(SampleId < 0)
		return false;

	m_AudioPackEditorState.m_PreviewSampleId = SampleId;
	GameClient()->m_Sounds.PlaySample(CSounds::CHN_GUI, SampleId, ISound::FLAG_PREVIEW, 1.0f);
	return true;
}

bool CMenus::AudioPackEditorEnsureStorageDirectories(const char *pStoragePath)
{
	if(pStoragePath == nullptr || pStoragePath[0] == '\0')
		return false;

	std::string CurrentDirectory;
	for(const char *pCursor = pStoragePath; *pCursor != '\0'; ++pCursor)
	{
		if(*pCursor == '/')
		{
			if(!CurrentDirectory.empty() && !Storage()->FolderExists(CurrentDirectory.c_str(), IStorage::TYPE_SAVE) &&
				!Storage()->CreateFolder(CurrentDirectory.c_str(), IStorage::TYPE_SAVE))
			{
				return false;
			}
			CurrentDirectory.push_back('/');
		}
		else
		{
			CurrentDirectory.push_back(*pCursor);
		}
	}

	return true;
}

bool CMenus::AudioPackEditorCopyFileToStorage(const char *pSourcePath, int SourceStorageType, const char *pStoragePath)
{
	if(pSourcePath == nullptr || pSourcePath[0] == '\0' || pStoragePath == nullptr || pStoragePath[0] == '\0')
		return false;
	if(!Storage()->FileExists(pSourcePath, SourceStorageType))
		return false;
	if(!AudioPackEditorEnsureStorageDirectories(pStoragePath))
		return false;

	IOHANDLE SourceFile = Storage()->OpenFile(pSourcePath, IOFLAG_READ, SourceStorageType);
	if(!SourceFile)
		return false;

	void *pData = nullptr;
	unsigned DataSize = 0;
	const bool ReadOk = io_read_all(SourceFile, &pData, &DataSize);
	io_close(SourceFile);
	if(!ReadOk || pData == nullptr)
	{
		if(pData != nullptr)
			free(pData);
		return false;
	}

	IOHANDLE DestFile = Storage()->OpenFile(pStoragePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!DestFile)
	{
		free(pData);
		return false;
	}

	const bool WriteOk = io_write(DestFile, pData, DataSize) == DataSize;
	io_close(DestFile);
	free(pData);
	return WriteOk;
}

bool CMenus::AudioPackEditorCopyAbsoluteFileToStorage(const char *pSourcePath, const char *pStoragePath)
{
	return AudioPackEditorCopyFileToStorage(pSourcePath, IStorage::TYPE_ABSOLUTE, pStoragePath);
}

void CMenus::RenderAudioPackEditorScreen(CUIRect MainView)
{
	using namespace qm_audio_pack;
	if(!m_AudioPackEditorState.m_Open)
		return;
	const bool Initialize = !m_AudioPackEditorState.m_Initialized;
	if(Initialize)
	{
		AudioPackEditorRefreshCandidates();
		m_AudioPackEditorState.m_Initialized = true;
	}
	if(!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		AudioPackEditorClose();
		return;
	}

	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float EditorFontSize = Metrics.m_BodySize;
	const float EditorSecondaryFontSize = maximum(9.0f, EditorFontSize - 2.0f);
	const float EditorLineSize = Metrics.m_LineHeight;
	const float Gap = maximum(4.0f, Metrics.m_LineSpacing);
	const auto Theme = ResolveConfiguredSecondaryPanelTheme();
	IUiContext Ctx;
	Ctx.m_pUi = Ui();
	Ctx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Ctx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Ctx.m_ScopeHash = MakeUiScopeHash("audio_pack_editor");
	Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	static const auto s_vSlots = BuildAudioPackSlots();
	static const auto s_vGroups = BuildGroups(s_vSlots);
	static CButtonContainer s_AudioPackEditorCloseButton;
	static CButtonContainer s_AudioPackEditorRefreshButton;
	static CButtonContainer s_AudioPackEditorPreviewButton;
	static CButtonContainer s_AudioPackEditorExportButton;
	static CButtonContainer s_AudioPackEditorImportPreviewButton;
	static CListBox s_GroupList;
	static CListBox s_CandidateList;
	static CUi::SDropDownState s_CategoryDropdown;
	static CScrollRegion s_WorkspaceScroll;
	static std::vector<CButtonContainer> s_vVariantButtons(s_vSlots.size());
	static int s_CurrentPathId;
	if(Initialize)
	{
		s_GroupList.Reset();
		s_CandidateList.Reset();
		s_WorkspaceScroll.Reset();
	}
	const int PreviousSlot = m_AudioPackEditorState.m_SelectedSlotIndex;

	auto Label = [&](const CUIRect &Rect, const char *pText, bool Secondary = false) {
		SLabelProperties Props;
		Props.m_MaxWidth = maximum(0.0f, Rect.w);
		Props.m_EllipsisAtEnd = true;
		Props.m_EnableWidthCheck = false;
		if(Secondary)
			Props.SetColor(ColorRGBA(0.70f, 0.73f, 0.79f, 1.0f));
		Ui()->DoLabel(&Rect, pText, Secondary ? EditorSecondaryFontSize : EditorFontSize, TEXTALIGN_ML, Props);
	};

	CUIRect WorkRect = MainView;
	const float Padding = minimum(10.0f * Metrics.m_UiScale, minimum(WorkRect.w, WorkRect.h) * 0.03f);
	WorkRect.Margin(Padding, &WorkRect);
	WorkRect.Draw(Theme.m_Surface, IGraphics::CORNER_ALL, ui_token::radius::CARD);
	WorkRect.Margin(Padding, &WorkRect);
	CUIRect Header, PackRow, StatusRow;
	WorkRect.HSplitTop(EditorLineSize + Gap, &Header, &WorkRect);
	CUIRect CloseButton, ReloadButton, Title;
	Header.VSplitRight(EditorLineSize, &Header, &CloseButton);
	Header.VSplitRight(Gap, &Header, nullptr);
	Header.VSplitRight(minimum(100.0f * Metrics.m_UiScale, Header.w * 0.25f), &Title, &ReloadButton);
	Label(Title, Localize("Edit audio pack"));
	if(Ui()->DoButton_QmIcon(&s_AudioPackEditorCloseButton, EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
	{
		AudioPackEditorClose();
		return;
	}
	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorRefreshButton, "audio-pack-reload", Localize("Reload"), 0, &ReloadButton))
		AudioPackEditorRefreshCandidates();
	WorkRect.HSplitTop(EditorLineSize, &PackRow, &WorkRect);
	CUIRect PackLabel, PackInput;
	PackRow.VSplitLeft(minimum(90.0f * Metrics.m_UiScale, PackRow.w * 0.25f), &PackLabel, &PackInput);
	Label(PackLabel, Localize("Pack name"));
	if(ui_widget::InputField(Ctx, &m_AudioPackEditorState.m_PackNameInput, PackInput, Localize("Pack name"), EditorFontSize))
		AudioPackEditorRebuildCandidates();
	WorkRect.HSplitTop(Gap * 2.0f, nullptr, &WorkRect);
	WorkRect.HSplitBottom(EditorLineSize + Gap, &WorkRect, &StatusRow);
	const auto Layout = ResolveEditorLayout(WorkRect, Metrics.m_UiScale);
	CUIRect Catalog = Layout.m_Catalog;
	CUIRect Workspace = Layout.m_Workspace;
	Catalog.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, ui_token::radius::CARD);
	Workspace.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.025f), IGraphics::CORNER_ALL, ui_token::radius::CARD);
	Catalog.Margin(Padding, &Catalog);
	Workspace.Margin(Padding, &Workspace);
	if(s_vSlots.empty() || Catalog.w <= 0.0f || Workspace.w <= 0.0f)
		return;

	CUIRect SearchRow, CategoryRow;
	Catalog.HSplitTop(EditorLineSize, &SearchRow, &Catalog);
	IUiContext SearchCtx = Ctx;
	SearchCtx.m_ScopeHash = MakeUiScopeHash("audio_pack_sound_search");
	if(ui_widget::InputField(SearchCtx, &m_AudioPackEditorState.m_FilterInput, SearchRow, EditorFontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
		s_GroupList.ResetScroll();
	Catalog.HSplitTop(Gap, nullptr, &Catalog);
	Catalog.HSplitTop(EditorLineSize, &CategoryRow, &Catalog);
	const char *apCategories[(int)ECategory::COUNT];
	for(int Index = 0; Index < (int)ECategory::COUNT; ++Index)
		apCategories[Index] = Localize(CategoryName((ECategory)Index));
	const int PreviousCategory = m_AudioPackEditorState.m_Category;
	m_AudioPackEditorState.m_Category = DoSettingsDropDown(&CategoryRow, PreviousCategory, apCategories, (int)ECategory::COUNT, s_CategoryDropdown);
	if(PreviousCategory != m_AudioPackEditorState.m_Category)
		s_GroupList.ResetScroll();
	Catalog.HSplitTop(Gap, nullptr, &Catalog);
	const auto vVisible = FilterGroups(s_vGroups, s_vSlots, (ECategory)m_AudioPackEditorState.m_Category, m_AudioPackEditorState.m_FilterInput.GetString(), [](const char *pText) { return Localize(pText); });
	int GroupIndex = SelectedGroup(s_vGroups, m_AudioPackEditorState.m_SelectedSlotIndex);
	if(!vVisible.empty() && std::find(vVisible.begin(), vVisible.end(), GroupIndex) == vVisible.end())
	{
		GroupIndex = vVisible.front();
		m_AudioPackEditorState.m_SelectedSlotIndex = SelectGroupSlot(s_vGroups[GroupIndex], -1);
		AudioPackEditorRebuildCandidates();
	}
	int VisibleSelection = -1;
	for(int Index = 0; Index < (int)vVisible.size(); ++Index)
		if(vVisible[Index] == GroupIndex)
			VisibleSelection = Index;
	// 组列表填满目录面板：行数取「面板高度装得下的整数行」与实际条目数的较小值，
	// 高度反推给 CListBox 的自适应上限（RowsHeight），末行才不会被面板底边裁一半。
	const float GroupRowHeight = EditorLineSize * 2.0f;
	const int GroupRowsFit = std::max(1, (int)((Catalog.h + ui_token::list::ROW_SPACING) / (GroupRowHeight + ui_token::list::ROW_SPACING)));
	const int GroupRows = std::min(GroupRowsFit, ui_token::list::VisibleRows((int)vVisible.size(), 1, 1, GroupRowsFit));
	// 容器内边距：行区在目录面板四边内缩，档位由行高派生。行数仍按面板高度反推，
	// 组件为保行数会把留白收敛到富余的那一段，所以可见行数不随留白变化；
	// 没有匹配分组时不声明留白，空面板保持原样。
	const float GroupListPadding = vVisible.empty() ? 0.0f : ui_token::list::ContainerPadding(GroupRowHeight);
	s_GroupList.SetCapsuleRows(EditorFontSize);
	s_GroupList.SetContainerPadding(GroupListPadding);
	s_GroupList.SetVisibleRowRange(GroupRows, GroupRows);
	s_GroupList.DoStart(GroupRowHeight, (int)vVisible.size(), 1, 3, VisibleSelection, &Catalog, false);
	for(int Index = 0; Index < (int)vVisible.size(); ++Index)
	{
		const auto &Group = s_vGroups[vVisible[Index]];
		const auto Item = s_GroupList.DoNextItem(&Group, Index == VisibleSelection);
		if(!Item.m_Visible)
			continue;
		CUIRect NameRow, IdRow;
		// 行内容排在内容区：胶囊行已经让出左右内退与滚动条那一列。
		Item.m_ContentRect.HSplitMid(&NameRow, &IdRow);
		char aName[256];
		str_format(aName, sizeof(aName), "%s [%d]", Group.m_pInfo->m_pSetName[0] != '\0' ? Localize(Group.m_pInfo->m_pName) : Group.m_pSetName, (int)Group.m_vSlotIndices.size());
		Label(NameRow, aName);
		char aSubtitle[256];
		str_format(aSubtitle, sizeof(aSubtitle), "%s · %s", Localize(CategoryName(Group.m_pInfo->m_Category)), Group.m_pSetName);
		Label(IdRow, aSubtitle, true);
	}
	const int NewSelection = s_GroupList.DoEnd();
	if(NewSelection != VisibleSelection && NewSelection >= 0 && NewSelection < (int)vVisible.size())
	{
		GroupIndex = vVisible[NewSelection];
		m_AudioPackEditorState.m_SelectedSlotIndex = SelectGroupSlot(s_vGroups[GroupIndex], m_AudioPackEditorState.m_SelectedSlotIndex);
		AudioPackEditorRebuildCandidates();
	}
	if(vVisible.empty())
	{
		Label(Workspace, Localize("No sounds match this search."));
		return;
	}
	if(PreviousSlot != m_AudioPackEditorState.m_SelectedSlotIndex)
	{
		s_WorkspaceScroll.Reset();
		s_CandidateList.ResetScroll();
	}

	const auto &Group = s_vGroups[GroupIndex];
	CScrollRegionParams ScrollParams = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, Metrics.m_UiScale);
	// 轨道粗细/边距统一取列表 token：整面板滚动面和 CListBox 共用同一条覆盖式滚动条，
	// 不再按面板档另取一套粗细。
	ScrollParams.m_ScrollbarThickness = ui_token::list::SCROLLBAR_WIDTH;
	ScrollParams.m_ScrollbarMargin = ui_token::list::SCROLLBAR_MARGIN;
	// 覆盖式轨道：轨道压在内容上，显隐走和列表同一档淡入 + 横向滑出。
	// 之前漏置这一位，Begin 按非覆盖式只扣了厚度 8，而轨道被左右外边距挤成 0 宽：
	// 内容白让 8，轨道根本画不出来。
	ScrollParams.m_ScrollbarOverlay = true;
	ScrollParams.m_ScrollbarRevealSec = ui_token::list::SCROLLBAR_FADE.m_DurationSec;
	ScrollParams.m_ScrollbarSlide = ui_token::list::SCROLLBAR_SLIDE;
	// 轨道沿长度方向按 Workspace 的卡片圆角内缩，滑块端头不会从圆角外侧探出来。
	ScrollParams.m_ScrollbarTrackInset = ui_token::radius::CARD;
	ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::PAGE;
	vec2 ScrollOffset;
	s_WorkspaceScroll.Begin(&Workspace, &ScrollOffset, &ScrollParams);
	// 覆盖式不占内容宽度，让位必须显式扣一次：厚度 + 外边距 + 缝。
	// 原来那 8 是非覆盖式自动扣的厚度，现在换成统一的 OverlayRailReserve()。
	Workspace.w = std::max(0.0f, Workspace.w - ui_token::list::OverlayRailReserve());
	Workspace.y += ScrollOffset.y;
	auto Take = [&](float Height) {
		CUIRect Row;
		Workspace.HSplitTop(Height, &Row, &Workspace);
		s_WorkspaceScroll.AddRect(Row);
		return Row;
	};
	auto Space = [&]() { Take(Gap); };
	auto WrappedLabel = [&](const char *pText, bool Secondary = false) {
		const float Size = Secondary ? EditorSecondaryFontSize : EditorFontSize;
		const float Width = maximum(1.0f, Workspace.w);
		const float Height = maximum(EditorLineSize, TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H + Gap);
		const CUIRect Row = Take(Height);
		SLabelProperties Props;
		Props.m_MaxWidth = Width;
		Props.m_EnableWidthCheck = false;
		if(Secondary)
			Props.SetColor(ColorRGBA(0.70f, 0.73f, 0.79f, 1.0f));
		Ui()->DoLabel(&Row, pText, Size, TEXTALIGN_TL, Props);
	};
	WrappedLabel(Group.m_pInfo->m_pSetName[0] != '\0' ? Localize(Group.m_pInfo->m_pName) : Group.m_pSetName);
	char aContext[256];
	str_format(aContext, sizeof(aContext), "%s · %s · %s", Group.m_pSetName, Localize(CategoryName(Group.m_pInfo->m_Category)), Localize(ContextName(Group.m_pInfo->m_Context)));
	WrappedLabel(aContext, true);
	Space();
	WrappedLabel(Localize(Group.m_pInfo->m_pUsage));
	Space();
	Label(Take(EditorLineSize), Localize("Sound variants"), true);
	const int Columns = maximum(1, minimum(6, (int)(Workspace.w / (EditorLineSize * 2.0f + Gap))));
	const int Rows = ((int)Group.m_vSlotIndices.size() + Columns - 1) / Columns;
	for(int RowIndex = 0; RowIndex < Rows; ++RowIndex)
	{
		CUIRect Row = Take(EditorLineSize);
		const float Width = maximum(1.0f, (Row.w - (Columns - 1) * Gap) / Columns);
		for(int Column = 0; Column < Columns; ++Column)
		{
			const int Variant = RowIndex * Columns + Column;
			if(Variant >= (int)Group.m_vSlotIndices.size())
				break;
			const int SlotIndex = Group.m_vSlotIndices[Variant];
			CUIRect Button;
			Row.VSplitLeft(Width, &Button, &Row);
			Row.VSplitLeft(minimum(Gap, maximum(0.0f, Row.w)), nullptr, &Row);
			char aVariant[16];
			str_format(aVariant, sizeof(aVariant), "%d", s_vSlots[SlotIndex].m_VariantIndex + 1);
			if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_vVariantButtons[SlotIndex], "audio-pack-variant", aVariant, SlotIndex == m_AudioPackEditorState.m_SelectedSlotIndex, &Button))
			{
				m_AudioPackEditorState.m_SelectedSlotIndex = SlotIndex;
				AudioPackEditorRebuildCandidates();
				s_CandidateList.ResetScroll();
			}
			GameClient()->m_Tooltips.DoToolTipForRect(&s_vVariantButtons[SlotIndex], &Button, s_vSlots[SlotIndex].m_pRelativePath);
		}
		Space();
	}
	const auto &SelectedSlot = s_vSlots[m_AudioPackEditorState.m_SelectedSlotIndex];
	WrappedLabel(SelectedSlot.m_pRelativePath, true);
	const char *pPackName = ResolveAudioPackEditorPackName(m_AudioPackEditorState.m_PackNameInput, g_Config.m_SndPack);
	char aCurrentPath[IO_MAX_PATH_LENGTH] = "";
	ResolveAudioPackEditorCurrentFilePath(Storage(), pPackName, SelectedSlot, aCurrentPath, sizeof(aCurrentPath));
	CUIRect CurrentRow = Take(EditorLineSize);
	char aCurrentLabel[IO_MAX_PATH_LENGTH + 64];
	str_format(aCurrentLabel, sizeof(aCurrentLabel), "%s: %s", Localize("Current file"), aCurrentPath[0] != '\0' ? aCurrentPath : Localize("Default"));
	Label(CurrentRow, aCurrentLabel, true);
	GameClient()->m_Tooltips.DoToolTipForRect(&s_CurrentPathId, &CurrentRow, aCurrentLabel);
	Space();
	Label(Take(EditorLineSize), Localize("Candidate files"));
	CUIRect CandidateSearch = Take(EditorLineSize);
	IUiContext CandidateCtx = Ctx;
	CandidateCtx.m_ScopeHash = MakeUiScopeHash("audio_pack_candidate_search");
	if(ui_widget::InputField(CandidateCtx, &m_AudioPackEditorState.m_CandidateFilterInput, CandidateSearch, EditorFontSize, false))
		s_CandidateList.ResetScroll();
	Space();
	std::vector<int> vCandidates;
	const char *pFilter = m_AudioPackEditorState.m_CandidateFilterInput.GetString();
	for(int Index = 0; Index < (int)m_AudioPackEditorState.m_vCandidateEntries.size(); ++Index)
		if(str_utf8_find_nocase(m_AudioPackEditorState.m_vCandidateEntries[Index].m_Path.c_str(), pFilter))
			vCandidates.push_back(Index);
	if(std::find(vCandidates.begin(), vCandidates.end(), m_AudioPackEditorState.m_SelectedCandidateIndex) == vCandidates.end())
		m_AudioPackEditorState.m_SelectedCandidateIndex = vCandidates.empty() ? -1 : vCandidates.front();
	int CandidateSelection = -1;
	for(int Index = 0; Index < (int)vCandidates.size(); ++Index)
		if(vCandidates[Index] == m_AudioPackEditorState.m_SelectedCandidateIndex)
			CandidateSelection = Index;
	// 候选列表最多 3 行（沿用原面板高度），行数按实际候选数收；
	// 高度用 PaddedRowsHeight 派生（行内容 + 上下两档容器内边距），和 CListBox 的
	// 自适应上限同源；没有候选文件时不声明留白——0 条目时留白只会多出一块空面板。
	const float CandidateRowHeight = EditorLineSize * 2.0f;
	const int CandidateRows = std::clamp((int)vCandidates.size(), 1, 3);
	const float CandidateListPadding = vCandidates.empty() ? 0.0f : ui_token::list::ContainerPadding(CandidateRowHeight);
	const CUIRect CandidateRect = Take(ui_token::list::PaddedRowsHeight(CandidateRows, CandidateRowHeight, ui_token::list::ROW_SPACING, CandidateListPadding));
	s_CandidateList.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
	s_CandidateList.SetCapsuleRows(EditorFontSize);
	s_CandidateList.SetContainerPadding(CandidateListPadding);
	s_CandidateList.SetVisibleRowRange(CandidateRows, CandidateRows);
	s_CandidateList.DoStart(CandidateRowHeight, (int)vCandidates.size(), 1, 3, CandidateSelection, &CandidateRect);
	for(int Index = 0; Index < (int)vCandidates.size(); ++Index)
	{
		const auto &Entry = m_AudioPackEditorState.m_vCandidateEntries[vCandidates[Index]];
		const auto Item = s_CandidateList.DoNextItem(&Entry, Index == CandidateSelection);
		if(!Item.m_Visible)
			continue;
		CUIRect FileRow, OriginRow;
		// 两行内容都排在内容区：胶囊行的左右内退与滚动条那一列的缝由组件让出。
		Item.m_ContentRect.HSplitMid(&FileRow, &OriginRow);
		const auto Slash = Entry.m_Path.find_last_of('/');
		const char *pFilename = Slash != std::string::npos ? Entry.m_Path.c_str() + Slash + 1 : Entry.m_Path.c_str();
		char aFilename[IO_MAX_PATH_LENGTH + 64];
		str_format(aFilename, sizeof(aFilename), "%s%s%s", pFilename, Entry.m_IsCurrentFile ? " · " : "", Entry.m_IsCurrentFile ? Localize("Current file") : "");
		Label(FileRow, aFilename);
		Label(OriginRow, Entry.m_DisplayName.c_str(), true);
		GameClient()->m_Tooltips.DoToolTipForRect(&Entry, &Item.m_Rect, Entry.m_Path.c_str());
	}
	const int NewCandidate = s_CandidateList.DoEnd();
	if(NewCandidate >= 0 && NewCandidate < (int)vCandidates.size())
		m_AudioPackEditorState.m_SelectedCandidateIndex = vCandidates[NewCandidate];
	if(vCandidates.empty())
		Label(CandidateRect, Localize("No candidate files match this search."), true);
	const char *pSelectedCandidatePath = "";
	if(m_AudioPackEditorState.m_SelectedCandidateIndex >= 0)
		pSelectedCandidatePath = m_AudioPackEditorState.m_vCandidateEntries[m_AudioPackEditorState.m_SelectedCandidateIndex].m_Path.c_str();
	Space();
	Label(Take(EditorLineSize), Localize("Manual source file"), true);
	CUIRect ManualInput = Take(EditorLineSize);
	ui_widget::InputField(Ctx, &m_AudioPackEditorState.m_SourcePathInput, ManualInput, Localize("Manual source file"), EditorFontSize);
	Space();
	WrappedLabel(Localize("A manual source file takes priority over the selected candidate. Only this variant is replaced."), true);
	Space();
	CUIRect PreviewButton, ImportPreviewButton, ExportButton;
	if(Workspace.w >= 400.0f * Metrics.m_UiScale)
	{
		CUIRect PreviewRow = Take(EditorLineSize);
		PreviewRow.VSplitMid(&PreviewButton, &ImportPreviewButton, Gap);
	}
	else
	{
		PreviewButton = Take(EditorLineSize);
		Space();
		ImportPreviewButton = Take(EditorLineSize);
	}
	Space();
	ExportButton = Take(EditorLineSize);
	Space();
	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorPreviewButton, "sound-audio-pack-editor-preview-selected", Localize("Preview selected file"), 0, &PreviewButton))
	{
		if(pSelectedCandidatePath[0] == '\0')
		{
			AudioPackEditorSetStatus(Localize("No candidate file selected."), true);
		}
		else if(!AudioPackEditorPlayPreview(pSelectedCandidatePath, IStorage::TYPE_ALL))
		{
			AudioPackEditorSetStatus(Localize("Failed to preview candidate file."), true);
		}
		else
		{
			AudioPackEditorSetStatus("", false);
		}
	}

	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorImportPreviewButton, "sound-audio-pack-editor-preview-import", Localize("Preview import file"), 0, &ImportPreviewButton))
	{
		const char *pManualPath = m_AudioPackEditorState.m_SourcePathInput.GetString();
		const std::string PreviewPath = ResolveAudioPackPreviewPath("", pManualPath);
		if(PreviewPath.empty())
		{
			AudioPackEditorSetStatus(Localize("Source file is empty."), true);
		}
		else if(!Storage()->FileExists(PreviewPath.c_str(), IStorage::TYPE_ABSOLUTE))
		{
			AudioPackEditorSetStatus(Localize("Source file does not exist."), true);
		}
		else if(!str_endswith(PreviewPath.c_str(), ".wv"))
		{
			AudioPackEditorSetStatus(Localize("Only .wv files are supported right now."), true);
		}
		else if(!AudioPackEditorPlayPreview(PreviewPath.c_str(), IStorage::TYPE_ABSOLUTE))
		{
			AudioPackEditorSetStatus(Localize("Failed to preview import file."), true);
		}
		else
		{
			AudioPackEditorSetStatus("", false);
		}
	}

	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorExportButton, "sound-audio-pack-editor-export-selected", Localize("Replace selected variant"), 0, &ExportButton))
	{
		const char *pManualPath = m_AudioPackEditorState.m_SourcePathInput.GetString();
		const std::string SourcePath = ResolveAudioPackExportSourcePath(pSelectedCandidatePath, pManualPath);
		const bool UseManualSource = pManualPath[0] != '\0';
		int SourceStorageType = IStorage::TYPE_ALL;

		if(SourcePath.empty())
		{
			AudioPackEditorSetStatus(UseManualSource ? Localize("Source file is empty.") : Localize("No candidate file selected."), true);
		}
		else
		{
			if(UseManualSource)
			{
				SourceStorageType = IStorage::TYPE_ABSOLUTE;
				if(!Storage()->FileExists(SourcePath.c_str(), SourceStorageType))
				{
					AudioPackEditorSetStatus(Localize("Source file does not exist."), true);
					goto AudioPackExportDone;
				}
				if(!str_endswith(SourcePath.c_str(), ".wv"))
				{
					AudioPackEditorSetStatus(Localize("Only .wv files are supported right now."), true);
					goto AudioPackExportDone;
				}
			}

			const std::string ExportPath = BuildAudioPackExportPath(pPackName, SelectedSlot.m_pRelativePath);
			if(AudioPackEditorCopyFileToStorage(SourcePath.c_str(), SourceStorageType, ExportPath.c_str()))
			{
				str_copy(g_Config.m_SndPack, pPackName, sizeof(g_Config.m_SndPack));
				RefreshAudioPackList();
				if(GameClient()->m_Sounds.Reload())
				{
					UpdateMusicState();
					AudioPackEditorSetStatus(Localize("Audio pack exported."), false);
				}
				else
				{
					AudioPackEditorSetStatus(Localize("Audio file was exported, but reload failed. Restart sound to apply it."), true);
				}
				AudioPackEditorRefreshCandidates();
			}
			else
			{
				AudioPackEditorSetStatus(Localize("Failed to export audio pack file."), true);
			}
		}
	}
AudioPackExportDone:
	s_WorkspaceScroll.End();

	if(m_AudioPackEditorState.m_aStatusMessage[0] != '\0')
	{
		TextRender()->TextColor(m_AudioPackEditorState.m_StatusIsError ? ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f) : ColorRGBA(0.45f, 1.0f, 0.55f, 1.0f));
		Ui()->DoLabel(&StatusRow, m_AudioPackEditorState.m_aStatusMessage, EditorSecondaryFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}
