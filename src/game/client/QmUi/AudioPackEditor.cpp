/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/fs_path.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/AudioPackEditorLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiOverlays.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTag.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/audio_pack_candidates.h>
#include <game/client/components/qmclient/audio_pack_export.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/sound_pack.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace FontIcons;

static constexpr char AUDIO_PACK_EDITOR_INTERACTION_ID[] = "audio-pack-editor";

void CMenus::AudioPackEditorOpen(const char *pPackName)
{
	AudioPackEditorStopPreview();
	g_Config.m_UiSettingsPage = SETTINGS_SOUND;
	m_AudioPackEditorState.m_Open = true;
	m_AudioPackEditorState.m_Initialized = false;
	m_AudioPackEditorState.m_ListFocus = {};
	Ui()->DoubleClickState().Reset();
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
	Ui()->CloseInteractionSource(&m_AudioPackEditorState, AUDIO_PACK_EDITOR_INTERACTION_ID);
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

	static const char *ResolveAudioPackEditorPackName(const CLineInputBuffered<64> &PackNameInput, const char *pFallbackPackName)
	{
		if(PackNameInput.GetString()[0] != '\0')
			return PackNameInput.GetString();
		return pFallbackPackName != nullptr ? pFallbackPackName : "";
	}

	const auto &EditorSlots()
	{
		static const auto s_vSlots = CMenus::BuildAudioPackSlots();
		return s_vSlots;
	}

	const auto &EditorGroups()
	{
		static const auto s_vGroups = qm_audio_pack::BuildGroups(EditorSlots());
		return s_vGroups;
	}

}

void CMenus::AudioPackEditorRefreshCandidates()
{
	AudioPackEditorUpdateCandidates(true);
}

void CMenus::AudioPackEditorRebuildCandidates()
{
	AudioPackEditorUpdateCandidates(false);
}

void CMenus::AudioPackEditorUpdateCandidates(bool Rescan)
{
	try
	{
		auto &State = m_AudioPackEditorState;
		const auto &vSlots = EditorSlots();
		if(vSlots.empty())
			return;
		const int SelectedSlot = std::clamp(State.m_SelectedSlotIndex, 0, (int)vSlots.size() - 1);
		const char *pPackName = ResolveAudioPackEditorPackName(State.m_PackNameInput, g_Config.m_SndPack);
		const auto Builtin = BuildAudioPackBuiltinCandidatePath(vSlots[SelectedSlot].m_pRelativePath);
		char aResolved[IO_MAX_PATH_LENGTH];
		const char *pCurrent = CSoundPack(pPackName).Resolve(Builtin.c_str(), Storage(), aResolved, sizeof(aResolved));
		char aCurrentPath[IO_MAX_PATH_LENGTH];
		str_copy(aCurrentPath, Storage()->FileExists(pCurrent, IStorage::TYPE_ALL) ? pCurrent : "");
		const bool Published = State.Update(Rescan, pPackName, aCurrentPath, [&](auto &Paths) {
			qm_audio_pack::CCandidateScan Scan(*Storage());
			return Scan.Scan(qm_audio_pack::CandidateScanRoots(), Paths);
		});
		if(Published)
		{
			State.m_SelectedSlotIndex = SelectedSlot;
			Ui()->DoubleClickState().Reset();
		}
	}
	catch(...)
	{
		// 目录、路径或显示准备失败保留旧候选和选择，下次手动刷新再尝试。
	}
}

void CMenus::AudioPackEditorStopPreview()
{
	if(m_AudioPackEditorState.m_PreviewSampleId >= 0)
	{
		Sound()->UnloadSample(m_AudioPackEditorState.m_PreviewSampleId);
		m_AudioPackEditorState.m_PreviewSampleId = -1;
	}
}

bool CMenus::AudioPackEditorPlayPreview(const char *pFilename, int StorageType)
{
	if(pFilename == nullptr || pFilename[0] == '\0')
		return false;

	AudioPackEditorStopPreview();

	ISound *pSound = Sound();
	if(!pSound->IsSoundEnabled())
		return false;

	// 沿用原读取的 1 GiB 上限，同时在打开后确认是普通文件。
	constexpr unsigned MAX_PREVIEW_FILE_BYTES = 1024u * 1024 * 1024;
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	const auto ReadResult = Storage()->ReadFileCheckedBounded(pFilename, StorageType, &pFileData, &FileSize, MAX_PREVIEW_FILE_BYTES);
	const std::unique_ptr<void, decltype(&free)> pData(pFileData, free);
	if(ReadResult != IStorage::EReadFileResult::SUCCESS)
		return false;

	const int SampleId = pSound->LoadWVFromMem(pData.get(), FileSize, false, pFilename);
	if(SampleId < 0)
		return false;

	const auto Voice = GameClient()->m_Sounds.PlaySample(CSounds::CHN_GUI, SampleId, ISound::FLAG_PREVIEW, 1.0f);
	if(!Voice.IsValid())
	{
		pSound->UnloadSample(SampleId);
		return false;
	}
	m_AudioPackEditorState.m_PreviewSampleId = SampleId;
	return true;
}

bool CMenus::AudioPackEditorValidateSource(const char *pPath, bool Manual)
{
	if(!pPath[0])
		AudioPackEditorSetStatus(Manual ? Localize("Source file is empty.") : Localize("No candidate file selected."), true);
	else if(Manual && !str_endswith(pPath, ".wv"))
		AudioPackEditorSetStatus(Localize("Only .wv files are supported right now."), true);
	else if(Manual && !Storage()->FileExists(pPath, IStorage::TYPE_ABSOLUTE))
		AudioPackEditorSetStatus(Localize("Source file does not exist."), true);
	else
		return true;
	return false;
}

void CMenus::AudioPackEditorPreviewSource(const char *pPath, bool Manual)
{
	const auto Source = qm_audio_pack::ResolvePreviewSource(pPath, Manual);
	if(!AudioPackEditorValidateSource(Source.m_pPath, Manual))
		return;
	if(AudioPackEditorPlayPreview(Source.m_pPath, Source.m_StorageType))
		AudioPackEditorSetStatus("", false);
	else
		AudioPackEditorSetStatus(Manual ? Localize("Failed to preview import file.") : Localize("Failed to preview candidate file."), true);
}

void CMenus::AudioPackEditorReplaceVariant(const SAudioPackSlot &Slot, const char *pCandidate)
{
	auto &State = m_AudioPackEditorState;
	const char *pManual = State.m_SourcePathInput.GetString();
	const auto Source = ResolveAudioPackExportSourcePath(pCandidate, pManual);
	const bool Manual = pManual[0] != '\0';
	if(!AudioPackEditorValidateSource(Source.c_str(), Manual))
		return;

	const char *pPackName = ResolveAudioPackEditorPackName(State.m_PackNameInput, g_Config.m_SndPack);
	const auto Target = BuildAudioPackExportPath(pPackName, Slot.m_pRelativePath);
	const auto Builtin = fs_path_from_utf8(BuildAudioPackBuiltinCandidatePath(Slot.m_pRelativePath));
	if(fs_path_from_utf8(Target).lexically_normal() == Builtin ||
		!qm_audio_pack::ExportFile(Storage(), Source.c_str(), Manual ? IStorage::TYPE_ABSOLUTE : IStorage::TYPE_ALL, Target.c_str()))
	{
		AudioPackEditorSetStatus(Localize("Failed to export audio pack file."), true);
		return;
	}
	if(pPackName != g_Config.m_SndPack)
		str_copy(g_Config.m_SndPack, pPackName);
	RefreshAudioPackList();
	if(GameClient()->m_Sounds.Reload())
	{
		UpdateMusicState();
		AudioPackEditorSetStatus(Localize("Audio pack exported."), false);
	}
	else
		AudioPackEditorSetStatus(Localize("Audio file was exported, but reload failed. Restart sound to apply it."), true);
	AudioPackEditorRefreshCandidates();
}

namespace
{
	CUIRect TakeRow(CUIRect &Area, float Height)
	{
		CUIRect Row;
		Area.HSplitTop(std::clamp(Height, 0.0f, Area.h), &Row, &Area);
		return Row;
	}

	CUIRect Inset(CUIRect Rect, float Padding)
	{
		Rect.Margin(std::min({Padding, Rect.w * 0.5f, Rect.h * 0.5f}), &Rect);
		return Rect;
	}

	void Label(CUi *pUi, CUIRect Rect, const char *pText, float Size, ColorRGBA Color, int Align = TEXTALIGN_ML, bool Wrap = false)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		SLabelProperties Props;
		Props.SetColor(Color);
		Props.m_MaxWidth = Rect.w;
		Props.m_EnableWidthCheck = false;
		Props.m_DisallowNewline = !Wrap;
		Props.m_EllipsisAtEnd = !Wrap;
		pUi->DoLabel(&Rect, pText, Size, Align, Props);
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer &Id, const CUIRect &Rect, EQmIcon Icon, const char *pGlyph, const char *pHint, bool Enabled = true)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			return false;
		const auto Button = QmUiSquareIconButtonRect(Rect);
		const bool Clicked = ui_widget::DoIconButton(Ctx, &Id, Icon, pGlyph, 0, Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Enabled);
		Ctx.m_pTooltips->DoToolTipForRect(&Id, &Button, pHint);
		return Clicked;
	}

	bool SearchField(const IUiContext &Ctx, CLineInput &Input, CUIRect Row, float Size, bool Hotkey)
	{
		if(Row.w <= 0.0f || Row.h <= 0.0f)
			return false;
		ui_widget::SInputFieldOptions Options;
		Options.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		Options.m_Clearable = true;
		Options.m_SearchHotkeyEnabled = Hotkey;
		Options.m_pPlaceholder = Localize("Search");
		Options.m_FontSize = Size;
		return ui_widget::InputField(Ctx, &Input, Row, Options).m_Changed;
	}

	CUIRect LabelledInput(const IUiContext &Ctx, CUIRect Row, const char *pLabel, const SSettingsContentMetrics &Metrics)
	{
		CUIRect Caption;
		const float Width = Ctx.m_pUi->TextRender()->TextWidth(Metrics.m_BodySize, pLabel) + Metrics.m_LineSpacing;
		Row.VSplitLeft(std::min(Row.w * 0.45f, Width), &Caption, &Row);
		Label(Ctx.m_pUi, Caption, pLabel, Metrics.m_BodySize, ResolveConfiguredTextColor());
		return Row;
	}

	void StartList(CListBox &List, CUIRect Rect, int Count, int Selected, const SSettingsContentMetrics &Metrics, bool Active)
	{
		const float RowHeight = Metrics.m_LineHeight * 2.0f;
		List.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		List.SetCapsuleRows(Metrics.m_BodySize);
		List.SetActive(Active);
		List.SetFillViewportHeight(true);
		List.SetContainerPadding(std::min(ui_token::list::ContainerPadding(RowHeight), std::max(0.0f, (Rect.h - RowHeight) * 0.5f)));
		List.DoStart(RowHeight, Count, 1, 3, Selected, &Rect, false);
	}

	void TrailingTag(CUi *pUi, CUIRect &Name, const char *pTag, const SSettingsContentMetrics &Metrics)
	{
		const float Width = ui_tag::MeasureWidth(pUi->TextRender(), pTag, Metrics.m_UiScale);
		if(Width >= Name.w)
			return;
		CUIRect Tag;
		Name.VSplitRight(Width, &Name, &Tag);
		Name.w = std::max(0.0f, Name.w - Metrics.m_LineSpacing);
		ui_tag::Draw(pUi, Tag, pTag, ui_tag::EColorMode::DERIVED, pUi->QmControlTheme().m_Selected, nullptr, 1.0f, Metrics.m_UiScale);
	}

	const char *GroupName(const qm_audio_pack::SGroup &Group)
	{
		return Group.m_pInfo->m_pSetName[0] ? Localize(Group.m_pInfo->m_pName) : Group.m_pSetName;
	}

	// 正常高度直接绘制。局部内容溢出时独立滚动，搜索、候选列表与另一端的工具不会跟着移动。
	template<typename F>
	void DrawSection(CScrollRegion &Scroll, CUIRect View, float ContentHeight, float Scale, F Render)
	{
		if(View.w <= 0.0f || View.h <= 0.0f)
			return;
		const bool Scrollable = View.h + 0.01f < ContentHeight;
		if(Scrollable)
		{
			auto Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, Scale);
			Params.m_ScrollbarOverlay = true;
			Params.m_ScrollbarThickness = ui_token::list::SCROLLBAR_WIDTH;
			Params.m_ScrollbarMargin = ui_token::list::SCROLLBAR_MARGIN;
			Params.m_ScrollbarRevealSec = ui_token::list::SCROLLBAR_FADE.m_DurationSec;
			Params.m_ScrollbarSlide = ui_token::list::SCROLLBAR_SLIDE;
			vec2 Offset;
			Scroll.Begin(&View, &Offset, &Params);
			View.y += Offset.y;
			View.h = ContentHeight;
			Scroll.AddRect(View);
		}
		View.w = std::max(0.0f, View.w - ui_token::list::OverlayRailReserve());
		Render(View);
		if(Scrollable)
			Scroll.End();
	}
}

int CMenus::RenderAudioPackCatalog(CUIRect View, const IUiContext &Ctx, const SSettingsContentMetrics &Metrics, bool Initialize)
{
	using namespace qm_audio_pack;
	auto &State = m_AudioPackEditorState;
	static CListBox s_List;
	static CUi::SDropDownState s_Category;
	if(Initialize)
		s_List.Reset();
	if(SearchField(Ctx, State.m_FilterInput, TakeRow(View, Metrics.m_LineHeight), Metrics.m_BodySize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
		s_List.ResetScroll();
	TakeRow(View, Metrics.m_LineSpacing);
	auto CategoryRow = TakeRow(View, Metrics.m_LineHeight);
	const char *apCategories[EDITOR_CATEGORIES.size()];
	for(int i = 0; i < (int)EDITOR_CATEGORIES.size(); ++i)
		apCategories[i] = Localize(CategoryName(EDITOR_CATEGORIES[i]));
	const int Category = CategoryRow.h > 0.0f && CategoryRow.w > 0.0f ?
				     DoSettingsDropDown(&CategoryRow, State.m_Category, apCategories, (int)EDITOR_CATEGORIES.size(), s_Category) :
				     State.m_Category;
	if(Category != State.m_Category)
	{
		State.m_Category = Category;
		s_List.ResetScroll();
	}
	TakeRow(View, Metrics.m_LineSpacing);
	const auto &vGroups = EditorGroups();
	const auto vVisible = FilterGroups(vGroups, EditorSlots(), EDITOR_CATEGORIES[Category], State.m_FilterInput.GetString(), [](const char *pText) { return Localize(pText); });
	if(vVisible.empty())
	{
		Label(Ui(), View, Localize("No sounds match this search."), Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY, TEXTALIGN_MC);
		return -1;
	}
	const auto It = std::find(vVisible.begin(), vVisible.end(), SelectedGroup(vGroups, State.m_SelectedSlotIndex));
	int Row = It == vVisible.end() ? 0 : (int)(It - vVisible.begin());
	if(View.h > 0.0f)
	{
		StartList(s_List, View, (int)vVisible.size(), Row, Metrics, State.m_ListFocus.Active(EEditorList::CATALOG, Ui()->IsPopupOpen() || m_Popup != POPUP_NONE));
		for(int i = 0; i < (int)vVisible.size(); ++i)
		{
			const auto &Group = vGroups[vVisible[i]];
			const auto Item = s_List.DoNextItem(&Group, i == Row);
			if(!Item.m_Visible)
				continue;
			CUIRect Name, Origin;
			Item.m_ContentRect.HSplitMid(&Name, &Origin);
			char aCount[16], aOrigin[256];
			str_format(aCount, sizeof(aCount), "%d", (int)Group.m_vSlotIndices.size());
			TrailingTag(Ui(), Name, aCount, Metrics);
			Label(Ui(), Name, GroupName(Group), Metrics.m_BodySize, ResolveConfiguredTextColor());
			str_format(aOrigin, sizeof(aOrigin), "%s · %s", Localize(CategoryName(EditorCategory(Group.m_pInfo->m_Category))), Group.m_pSetName);
			Label(Ui(), Origin, aOrigin, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		}
		Row = std::clamp(s_List.DoEnd(), 0, (int)vVisible.size() - 1);
	}
	const int Slot = SelectGroupSlot(vGroups[vVisible[Row]], State.m_SelectedSlotIndex);
	if(Slot != State.m_SelectedSlotIndex && !Ui()->RenderOnly())
	{
		State.m_SelectedSlotIndex = Slot;
		AudioPackEditorRebuildCandidates();
	}
	return vVisible[Row];
}

bool CMenus::RenderAudioPackCandidates(CUIRect View, const IUiContext &Ctx, const SSettingsContentMetrics &Metrics, bool ResetScroll, bool Initialize)
{
	auto &State = m_AudioPackEditorState;
	static CListBox s_List;
	struct SRowIds
	{
		char m_Select;
		CButtonContainer m_Play;
	};
	static std::map<std::string, SRowIds> s_Ids;
	if(Initialize)
	{
		s_List.Reset();
		s_Ids.clear();
	}
	else if(ResetScroll)
		s_List.ResetScroll();
	const auto Filtered = qm_audio_pack::FilterCandidates(State.m_vCandidateEntries, State.m_CandidateFilterInput.GetString(), State.m_SelectedCandidateIndex);
	if(Filtered.m_vIndices.empty())
	{
		if(!Ui()->RenderOnly())
			State.m_SelectedCandidateIndex = -1;
		Label(Ui(), View, Localize("No candidate files match this search."), Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY, TEXTALIGN_MC);
		return false;
	}
	if(View.h <= 0.0f || View.w <= 0.0f)
		return false;
	const int Count = (int)Filtered.m_vIndices.size();
	StartList(s_List, View, Count, Filtered.m_SelectedRow, Metrics, State.m_ListFocus.Active(qm_audio_pack::EEditorList::CANDIDATES, Ui()->IsPopupOpen() || m_Popup != POPUP_NONE));
	int PreviewRow = -1;
	const auto Range = SettingsSkinListVisibleRangeForScroll(s_List.ScrollOffsetY(), s_List.ViewHeight(), s_List.RowHeight() + ui_token::list::ROW_SPACING, 1, Count, 1);
	s_List.SkipItems(Range.m_FirstItem);
	for(int i = Range.m_FirstItem; i < Range.m_EndItem; ++i)
	{
		const auto &Entry = State.m_vCandidateEntries[Filtered.m_vIndices[i]];
		auto &Ids = s_Ids[Entry.m_Path];
		const auto Item = s_List.DoNextItem(&Ids.m_Select, i == Filtered.m_SelectedRow, -1.0f, vec2(0, 0), Metrics.m_ButtonHeight);
		if(!Item.m_Visible)
			continue;
		const auto Play = Item.TrailingAction(Metrics.m_ButtonHeight);
		CUIRect Text = Item.m_ContentRect;
		Text.w = std::max(0.0f, Play.x - Text.x - Metrics.m_LineSpacing);
		CUIRect Name, Origin;
		Text.HSplitMid(&Name, &Origin);
		if(Entry.m_IsCurrentFile)
			TrailingTag(Ui(), Name, Localize("Current file"), Metrics);
		Label(Ui(), Name, fs_filename(Entry.m_Path.c_str()), Metrics.m_BodySize, ResolveConfiguredTextColor());
		Label(Ui(), Origin, Entry.m_DisplayName.c_str(), Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		const auto Hit = Item.SelectionHitRect(Metrics.m_ButtonHeight);
		GameClient()->m_Tooltips.DoToolTipForRect(&Ids.m_Select, &Hit, Entry.m_Path.c_str());
		if(IconButton(Ctx, Ids.m_Play, Play, EQmIcon::PLAY, FONT_ICON_PLAY, Localize("Preview")))
			PreviewRow = i;
	}
	s_List.SkipItems(Count - Range.m_EndItem);
	const int Selected = s_List.DoEnd();
	if(Ui()->RenderOnly())
		return false;
	const auto Action = qm_audio_pack::FinishCandidateList(Ui()->DoubleClickState(), State.m_SelectedCandidateIndex, Filtered, Selected, PreviewRow,
		s_List.WasItemSelected() && s_List.WasItemActivated());
	if(Action.m_PreviewIndex >= 0)
		AudioPackEditorPreviewSource(State.m_vCandidateEntries[Action.m_PreviewIndex].m_Path.c_str(), false);
	return Action.m_Replace;
}

void CMenus::RenderAudioPackEditorScreen(CUIRect MainView)
{
	using namespace qm_audio_pack;
	auto &State = m_AudioPackEditorState;
	if(!State.m_Open)
		return;
	if(m_MenuTextPlanCollecting)
	{
		RenderSettingsCatalogPage(MainView, "sound");
		return;
	}
	if(!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		AudioPackEditorClose();
		return;
	}
	if(MainView.w <= 0.0f || MainView.h <= 0.0f)
		return;
	const bool Initialize = !Ui()->RenderOnly() && !State.m_Initialized;
	if(Initialize)
	{
		AudioPackEditorRefreshCandidates();
		State.m_Initialized = true;
	}
	const CUiScopedInteractionOwner InteractionOwner(Ui(), &State, AUDIO_PACK_EDITOR_INTERACTION_ID);
	Ui()->RefreshInteractionSource(&State, AUDIO_PACK_EDITOR_INTERACTION_ID);
	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	const auto Ctx = SettingsUiContext("audio_pack_editor", Metrics.m_UiScale);
	DrawRoundedSurface(Ui(), MainView, ResolveConfiguredSecondaryPanelTheme().m_Surface, ui_token::color::BORDER_SUBTLE, ui_token::radius::CARD, 1.0f);
	static CScrollRegion s_EditorScroll;
	if(Initialize)
		s_EditorScroll.Reset();
	const auto MinimumHeight = [&](float Width) {
		return MinimumEditorHeight(Width, Metrics.m_UiScale, Metrics.m_LineHeight, Metrics.m_LineSpacing, State.m_aStatusMessage[0] != '\0');
	};
	if(MainView.h < MinimumHeight(MainView.w))
	{
		DrawSection(s_EditorScroll, MainView, MinimumHeight(MainView.w - ui_token::list::OverlayRailReserve()), Metrics.m_UiScale, [&](CUIRect Area) {
			RenderAudioPackEditorContent(Area, Ctx, Metrics, Initialize);
		});
	}
	else
		RenderAudioPackEditorContent(MainView, Ctx, Metrics, Initialize);
}

void CMenus::RenderAudioPackEditorContent(CUIRect MainView, const IUiContext &Ctx, const SSettingsContentMetrics &Metrics, bool Initialize)
{
	using namespace qm_audio_pack;
	auto &State = m_AudioPackEditorState;
	const float Line = Metrics.m_LineHeight;
	const float Gap = Metrics.m_LineSpacing;
	CUIRect Content = Inset(MainView, ui_token::panel::PADDING * Metrics.m_UiScale);
	const auto Header = ResolveSettingsInputActionsLayout(TakeRow(Content, ui_token::page_title::HEIGHT), 2, Metrics, Line);
	ui_widget::PageTitle(Ctx, Header.m_Input, Localize("Edit audio pack"), EQmIcon::MUSIC, FONT_ICON_MUSIC);
	static CButtonContainer s_Reload, s_Close;
	if(IconButton(Ctx, s_Reload, Header.m_Actions.m_aButtons[0], EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, Localize("Reload")))
		AudioPackEditorRefreshCandidates();
	if(IconButton(Ctx, s_Close, Header.m_Actions.m_aButtons[1], EQmIcon::CLOSE, FONT_ICON_XMARK, Localize("Close")))
	{
		AudioPackEditorClose();
		return;
	}
	const auto Pack = LabelledInput(Ctx, TakeRow(Content, Line), Localize("Pack name"), Metrics);
	if(ui_widget::InputField(Ctx, &State.m_PackNameInput, Pack, Localize("Pack name"), Metrics.m_BodySize))
		AudioPackEditorRebuildCandidates();
	TakeRow(Content, ui_token::panel::GAP * Metrics.m_UiScale);
	const auto Layout = ResolveEditorLayout(Content, Metrics.m_UiScale);
	if(Ui()->MouseButtonClicked(0))
	{
		const bool Blocked = Ui()->IsPopupOpen() || m_Popup != POPUP_NONE;
		if(Ui()->MouseHovered(&Layout.m_Catalog))
			State.m_ListFocus.Click(EEditorList::CATALOG, Blocked);
		else if(Ui()->MouseHovered(&Layout.m_Workspace))
			State.m_ListFocus.Click(EEditorList::CANDIDATES, Blocked);
	}
	for(const auto &Panel : {Layout.m_Catalog, Layout.m_Workspace})
		DrawRoundedSurface(Ui(), Panel, ui_token::color::PREVIEW_BACKDROP, ui_token::color::BORDER_SUBTLE, ui_token::radius::CARD, 1.0f);
	const float Padding = ui_token::settings::CARD_PADDING * Metrics.m_UiScale;
	const int PreviousSlot = State.m_SelectedSlotIndex;
	const int GroupIndex = RenderAudioPackCatalog(Inset(Layout.m_Catalog, Padding), Ctx, Metrics, Initialize);
	if(GroupIndex < 0)
		return;
	CUIRect Workspace = Inset(Layout.m_Workspace, Padding);
	if(Workspace.w <= 0.0f || Workspace.h <= 0.0f)
		return;

	const auto &Group = EditorGroups()[GroupIndex];
	const auto &vSlots = EditorSlots();
	const float SectionWidth = std::max(1.0f, Workspace.w - ui_token::list::OverlayRailReserve());
	const auto WrappedHeight = [&](const char *pText, float Size) {
		return std::max(Size, TextRender()->TextBoundingBox(Size, pText, -1, SectionWidth).m_H) + Gap;
	};
	const char *pUsage = Localize(Group.m_pInfo->m_pUsage);
	const char *pHelp = Localize("A manual source file takes priority over the selected candidate. Only this variant is replaced.");
	const float UsageHeight = WrappedHeight(pUsage, Metrics.m_BodySize);
	const float HelpHeight = WrappedHeight(pHelp, Metrics.m_SmallSize);
	const int VariantCount = (int)Group.m_vSlotIndices.size();
	const int Columns = std::clamp((int)((SectionWidth + Gap) / (ui_token::server_filter::CHIP_STAR_MIN_WIDTH * Metrics.m_UiScale + Gap)), 1, VariantCount);
	const int VariantRows = (VariantCount + Columns - 1) / Columns;
	const float DetailHeight = Line * 2 + Metrics.m_SmallSize + UsageHeight + Gap + Metrics.m_SmallSize + Gap + VariantRows * (Line + Gap);
	const float FooterHeight = Gap + Line + HelpHeight + Line;
	CUIRect Status;
	Workspace.HSplitBottom(State.m_aStatusMessage[0] ? std::min(Line, Workspace.h) : 0.0f, &Workspace, &Status);
	const auto Work = ResolveWorkspaceLayout(Workspace, DetailHeight, Line + Gap, Line * 2, FooterHeight);
	static CScrollRegion s_DetailsScroll, s_FooterScroll;
	if(Initialize || PreviousSlot != State.m_SelectedSlotIndex)
		s_DetailsScroll.Reset();
	if(Initialize)
		s_FooterScroll.Reset();

	DrawSection(s_DetailsScroll, Work.m_Details, DetailHeight, Metrics.m_UiScale, [&](CUIRect Area) {
		Label(Ui(), TakeRow(Area, Line), GroupName(Group), Metrics.m_HeadlineSize, ResolveConfiguredTextColor());
		char aContext[256];
		str_format(aContext, sizeof(aContext), "%s · %s · %s", Group.m_pSetName, Localize(CategoryName(EditorCategory(Group.m_pInfo->m_Category))), Localize(ContextName(Group.m_pInfo->m_Context)));
		Label(Ui(), TakeRow(Area, Metrics.m_SmallSize + Gap), aContext, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		Label(Ui(), TakeRow(Area, UsageHeight), pUsage, Metrics.m_BodySize, ResolveConfiguredTextColor(), TEXTALIGN_TL, true);
		Label(Ui(), TakeRow(Area, Metrics.m_SmallSize + Gap), Localize("Sound variants"), Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		CUIRect Row;
		const float ChipWidth = std::max(0.0f, (Area.w - (Columns - 1) * Gap) / Columns);
		for(int i = 0; i < VariantCount; ++i)
		{
			if(i % Columns == 0)
			{
				Row = TakeRow(Area, Line);
				TakeRow(Area, Gap);
			}
			const int SlotIndex = Group.m_vSlotIndices[i];
			const auto &Slot = vSlots[SlotIndex];
			const CUIRect Chip{Row.x + (i % Columns) * (ChipWidth + Gap), Row.y, ChipWidth, Row.h};
			char aNumber[16];
			str_format(aNumber, sizeof(aNumber), "#%d", Slot.m_VariantIndex + 1);
			if(ui_widget::FilterChip(Ctx, &Slot, Chip, VariantCount == 1 ? Slot.m_pRelativePath : aNumber, SlotIndex == State.m_SelectedSlotIndex, true, Metrics.m_SmallSize) && SlotIndex != State.m_SelectedSlotIndex)
			{
				State.m_SelectedSlotIndex = SlotIndex;
				AudioPackEditorRebuildCandidates();
			}
			GameClient()->m_Tooltips.DoToolTipForRect(&Slot, &Chip, Slot.m_pRelativePath);
		}
		const auto Current = ResolveSettingsInputActionsLayout(TakeRow(Area, Line), 1, Metrics);
		char aLabel[IO_MAX_PATH_LENGTH + 64];
		str_format(aLabel, sizeof(aLabel), "%s: %s", Localize("Current file"), State.m_aCurrentPath[0] ? State.m_aCurrentPath : Localize("Default"));
		Label(Ui(), Current.m_Input, aLabel, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		GameClient()->m_Tooltips.DoToolTipForRect(State.m_aCurrentPath, &Current.m_Input, aLabel);
		static CButtonContainer s_PlayCurrent;
		if(IconButton(Ctx, s_PlayCurrent, Current.m_Actions.m_aButtons[0], EQmIcon::PLAY, FONT_ICON_PLAY, Localize("Preview"), State.m_aCurrentPath[0] != '\0'))
			AudioPackEditorPreviewSource(State.m_aCurrentPath, false);
	});

	CUIRect Search = Work.m_Search;
	const auto SearchInput = LabelledInput(Ctx, TakeRow(Search, Line), Localize("Candidate files"), Metrics);
	const bool FilterChanged = SearchInput.h > 0.0f && SearchField(Ctx, State.m_CandidateFilterInput, SearchInput, Metrics.m_BodySize, false);
	const bool Activated = RenderAudioPackCandidates(Work.m_Candidates, Ctx, Metrics, FilterChanged || PreviousSlot != State.m_SelectedSlotIndex, Initialize);
	const auto &Slot = vSlots[State.m_SelectedSlotIndex];
	const char *pCandidate = State.m_SelectedCandidateIndex < 0 ? "" : State.m_vCandidateEntries[State.m_SelectedCandidateIndex].m_Path.c_str();
	bool Replace = false;
	DrawSection(s_FooterScroll, Work.m_Footer, FooterHeight, Metrics.m_UiScale, [&](CUIRect Area) {
		TakeRow(Area, Gap);
		const auto Manual = ResolveSettingsInputActionsLayout(TakeRow(Area, Line), 1, Metrics);
		ui_widget::SInputFieldOptions Options;
		Options.m_Clearable = true;
		Options.m_FontSize = Metrics.m_BodySize;
		Options.m_pPlaceholder = "…/file.wv";
		const auto Path = LabelledInput(Ctx, Manual.m_Input, Localize("Manual source file"), Metrics);
		ui_widget::InputField(Ctx, &State.m_SourcePathInput, Path, Options);
		static CButtonContainer s_PlayManual, s_PlaySelected, s_Replace;
		if(IconButton(Ctx, s_PlayManual, Manual.m_Actions.m_aButtons[0], EQmIcon::PLAY, FONT_ICON_PLAY, Localize("Preview import file")))
			AudioPackEditorPreviewSource(State.m_SourcePathInput.GetString(), true);
		Label(Ui(), TakeRow(Area, HelpHeight), pHelp, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY, TEXTALIGN_ML, true);
		static ui_widget::SSettingsActionGroupState s_ActionGroup;
		const char *apLabels[] = {Localize("Preview selected file"), Localize("Replace selected variant")};
		const auto Actions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_ActionGroup, TakeRow(Area, Line), apLabels, 2, Metrics);
		if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_PlaySelected, "sound-audio-pack-editor-preview-selected", apLabels[0], 0, &Actions.m_aButtons[0], Metrics))
			AudioPackEditorPreviewSource(pCandidate, false);
		Replace = DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_Replace, "sound-audio-pack-editor-export-selected", apLabels[1], 0, &Actions.m_aButtons[1], Metrics);
	});
	if(Activated || Replace)
		AudioPackEditorReplaceVariant(Slot, pCandidate);
	if(State.m_aStatusMessage[0] && Status.h > 0.0f)
	{
		Label(Ui(), Status, State.m_aStatusMessage, Metrics.m_SmallSize, State.m_StatusIsError ? ui_token::color::DANGER : ui_token::color::SUCCESS);
		GameClient()->m_Tooltips.DoToolTipForRect(State.m_aStatusMessage, &Status, State.m_aStatusMessage);
	}
}
