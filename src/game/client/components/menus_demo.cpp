/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "maplayers.h"
#include "menus.h"
#include "qmclient/demo_ui.h"
#include "qmclient/perf_logging.h"
#include "qmclient/rank_demo_manifest.h"

#include <base/hash.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/demo.h>
#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/shared/snapshot.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/console.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	constexpr const char *RANK_DEMO_MANIFEST_URL = "https://ddnet.org/watch/watchable.jsonl";
	constexpr const char *RANK_DEMO_URL_PREFIX = "https://ddnet.org/watch/demos";
	constexpr size_t RANK_DEMO_MAX_MANIFEST_BYTES = 16 * 1024 * 1024;
	constexpr size_t RANK_DEMO_MAX_DOWNLOAD_BYTES = 128 * 1024 * 1024;
	constexpr size_t RANK_DEMO_MAX_UNPACKED_BYTES = 256 * 1024 * 1024;

	bool FindRankOneDemo(const unsigned char *pData, size_t DataSize, const char *pMapName, std::string &DemoName, int64_t &DemoTs)
	{
		std::vector<qmclient::rank_demo::SEntry> Entries;
		if(!qmclient::rank_demo::ParseManifest(pData, DataSize, Entries))
			return false;
		const qmclient::rank_demo::SEntry *pEntry = qmclient::rank_demo::FindLatest(Entries, pMapName, 1);
		if(pEntry == nullptr)
			return false;
		DemoName = pEntry->m_Demo;
		DemoTs = pEntry->m_Ts == std::numeric_limits<int64_t>::min() ? 0 : pEntry->m_Ts;
		return true;
	}

	bool IsGzipData(const unsigned char *pData, size_t DataSize)
	{
		return DataSize >= 2 && pData[0] == 0x1f && pData[1] == 0x8b;
	}

	bool HasDemoMagic(const unsigned char *pData, size_t DataSize)
	{
		return DataSize >= 7 && mem_comp(pData, "TWDEMO\0", 7) == 0;
	}

	bool IsStoredDemoValid(IStorage *pStorage, const char *pPath)
	{
		if(pStorage == nullptr || pPath == nullptr || pPath[0] == '\0')
			return false;
		rust::Box<CSnapshotDelta> pDelta = CSnapshotDelta::New();
		rust::Box<CSnapshotDelta> pDeltaSixup = CSnapshotDelta::New();
		CDemoPlayer DemoPlayer(&*pDelta, &*pDeltaSixup, false);
		const int Result = DemoPlayer.Load(pStorage, nullptr, pPath, IStorage::TYPE_SAVE);
		// CDemoPlayer 析构要求文件已关闭，否则 dbg_assert 在 Release 下也会 abort
		DemoPlayer.Stop();
		return Result == 0;
	}

	bool UnpackRankDemo(IStorage *pStorage, const char *pSourcePath, const char *pDestinationPath)
	{
		if(pStorage == nullptr || pSourcePath == nullptr || pSourcePath[0] == '\0' || pDestinationPath == nullptr || pDestinationPath[0] == '\0')
			return false;

		char aTempPath[IO_MAX_PATH_LENGTH];
		if(str_format(aTempPath, sizeof(aTempPath), "%s.tmp", pDestinationPath) >= (int)sizeof(aTempPath))
			return false;
		pStorage->RemoveFile(aTempPath, IStorage::TYPE_SAVE);
		const auto RemoveTemp = [&]() { pStorage->RemoveFile(aTempPath, IStorage::TYPE_SAVE); };

		void *pData = nullptr;
		unsigned DataSize = 0;
		if(!pStorage->ReadFile(pSourcePath, IStorage::TYPE_SAVE, &pData, &DataSize) || pData == nullptr || DataSize == 0)
		{
			free(pData);
			return false;
		}

		if(!IsGzipData(static_cast<const unsigned char *>(pData), DataSize))
		{
			const bool Valid = HasDemoMagic(static_cast<const unsigned char *>(pData), DataSize);
			if(Valid)
			{
				IOHANDLE File = pStorage->OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
				if(File == nullptr)
				{
					free(pData);
					return false;
				}
				const bool Written = io_write(File, pData, DataSize) == DataSize;
				io_close(File);
				free(pData);
				if(!Written || !pStorage->RenameFile(aTempPath, pDestinationPath, IStorage::TYPE_SAVE))
				{
					RemoveTemp();
					return false;
				}
				return true;
			}
			free(pData);
			return false;
		}

		z_stream Stream = {};
		Stream.next_in = static_cast<Bytef *>(pData);
		Stream.avail_in = DataSize;
		if(inflateInit2(&Stream, 15 + 32) != Z_OK)
		{
			free(pData);
			return false;
		}

		std::vector<unsigned char> Output;
		Output.resize(std::min<size_t>(std::max<size_t>(static_cast<size_t>(DataSize) * 4, 1024 * 1024), RANK_DEMO_MAX_UNPACKED_BYTES));
		size_t OutputLength = 0;
		int Result = Z_OK;
		while(Result == Z_OK)
		{
			if(OutputLength == Output.size())
			{
				if(Output.size() >= RANK_DEMO_MAX_UNPACKED_BYTES)
				{
					inflateEnd(&Stream);
					free(pData);
					return false;
				}
				Output.resize(std::min(Output.size() * 2, RANK_DEMO_MAX_UNPACKED_BYTES));
			}
			Stream.next_out = Output.data() + OutputLength;
			Stream.avail_out = static_cast<uInt>(std::min<size_t>(Output.size() - OutputLength, 0x40000000));
			Result = inflate(&Stream, Z_NO_FLUSH);
			OutputLength = Output.size() - Stream.avail_out;
		}
		inflateEnd(&Stream);
		free(pData);

		if(Result != Z_STREAM_END || OutputLength > RANK_DEMO_MAX_UNPACKED_BYTES || !HasDemoMagic(Output.data(), OutputLength))
			return false;

		IOHANDLE File = pStorage->OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File == nullptr)
			return false;
		const bool Written = io_write(File, Output.data(), OutputLength) == OutputLength;
		io_close(File);
		if(!Written || !pStorage->RenameFile(aTempPath, pDestinationPath, IStorage::TYPE_SAVE))
		{
			RemoveTemp();
			return false;
		}
		return true;
	}

	bool IsScreenshotBrowserFile(const char *pName)
	{
		return str_endswith_nocase(pName, ".png") != nullptr ||
		       str_endswith_nocase(pName, ".jpg") != nullptr ||
		       str_endswith_nocase(pName, ".jpeg") != nullptr ||
		       str_endswith_nocase(pName, ".webp") != nullptr;
	}

	const char *DemoBrowserListColumnLabel(bool BrowsingScreenshots)
	{
		return BrowsingScreenshots ? Localize("Screenshot") : Localize("Demo");
	}

	void FormatBrowserFileSize(int64_t SizeBytes, char *pBuf, size_t BufSize)
	{
		const float SizeKiB = SizeBytes / 1024.0f;
		if(SizeKiB > 1024.0f)
			str_format(pBuf, BufSize, Localize("%.2f MiB"), SizeKiB / 1024.0f);
		else
			str_format(pBuf, BufSize, Localize("%.2f KiB"), SizeKiB);
	}

}

void CMenus::RenderDemoExportDisplayToggle(const CUIRect &Rect)
{
	static CButtonContainer s_DisplayButton;
	const bool Expanded = m_DemoExportDisplayExpanded;
	if(DoButton_Menu(&s_DisplayButton, Localize("Demo display"), Ui()->IsPopupOpen() ? -1 : 0, &Rect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.0f, 0.0f, Expanded ? ui_token::color::ACCENT_PRIMARY_DIM.WithAlpha(0.18f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), nullptr, 11.0f) && !Ui()->IsPopupOpen())
		m_DemoExportDisplayExpanded = !Expanded;
	CUIRect Arrow;
	Rect.VSplitRight(22.0f, nullptr, &Arrow);
	Ui()->DoLabel(&Arrow, m_DemoExportDisplayExpanded ? "-" : "+", 12.0f, TEXTALIGN_MC);
}

void CMenus::RenderDemoDisplaySettings(CUIRect View, bool Enabled)
{
	CUiScopedGaussianBlurSuppression BlurSuppression(Ui());
	View.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f), IGraphics::CORNER_ALL, 8.0f);
	View.Margin(6.0f, &View);
	CUIRect Row;
	View.HSplitTop(16.0f, &Row, &View);
	Ui()->DoLabel(&Row, Localize("Demo display"), 11.0f, TEXTALIGN_ML);
	View.HSplitTop(2.0f, nullptr, &View);

	// 三组段选：值就是配置项本身的下标，点哪档写哪档。
	const auto Segments = [&](const char *pLabel, int *pValue, const char *const *ppLabels, int Count, CButtonContainer *pButtons) {
		CUIRect Label, Options;
		View.HSplitTop(20.0f, &Row, &View);
		View.HSplitTop(2.0f, nullptr, &View);
		Row.VSplitLeft(std::min(116.0f, Row.w * 0.27f), &Label, &Options);
		Ui()->DoLabel(&Label, pLabel, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = Label.w - 4.0f});
		Options.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f), IGraphics::CORNER_ALL, 6.0f);
		const float Width = Options.w / Count;
		for(int i = 0; i < Count; ++i)
		{
			CUIRect Option{Options.x + Width * i, Options.y, Width, Options.h};
			Option.Margin(2.0f, &Option);
			const ColorRGBA Fill = *pValue == i ? ui_token::color::ACCENT_PRIMARY_DIM : ColorRGBA(1.0f, 1.0f, 1.0f, 0.02f);
			if(DoButton_Menu(&pButtons[i], nullptr, Enabled ? 0 : -1, &Option, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, Fill) && Enabled)
				*pValue = i;
			Ui()->DoLabel(&Option, ppLabels[i], 10.0f, TEXTALIGN_MC, {.m_MaxWidth = Option.w - 4.0f, .m_EllipsisAtEnd = true});
			GameClient()->m_Tooltips.DoToolTip(&pButtons[i], &Option, ppLabels[i]);
		}
	};
	static CButtonContainer s_aDirectionButtons[4], s_aStrongWeakButtons[3], s_aScopeButtons[5];
	const char *apDirection[] = {Localize("None", "Show players' key presses"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses"), Localize("Own", "Show players' key presses")};
	Segments(Localize("Show key presses"), &g_Config.m_QmDemoShowDirection, apDirection, std::size(apDirection), s_aDirectionButtons);
	const char *apStrength[] = {Localize("Off"), Localize("Icons"), Localize("Icon and number")};
	Segments(Localize("Strong Weak Hook"), &g_Config.m_QmDemoShowStrongWeak, apStrength, std::size(apStrength), s_aStrongWeakButtons);
	const char *apScope[] = {Localize("Self"), Localize("Others"), Localize("Strong hook"), Localize("Weak hook"), Localize("All")};
	Segments(Localize("Hook strength scope"), &g_Config.m_QmDemoStrongWeakScope, apScope, std::size(apScope), s_aScopeButtons);

	// 两个开关并排：回放里是否画主 HUD 与聊天。
	IUiContext Context;
	Context.m_pUi = Ui();
	Context.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Context.m_ScopeHash = MakeUiScopeHash("demo_display");
	Context.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect Hud, Chat;
	Row.VSplitMid(&Hud, &Chat, 16.0f);
	const auto Toggle = [&](CUIRect Rect, const char *pLabel, int *pValue) {
		CUIRect Switch;
		Rect.VSplitRight(34.0f, &Rect, &Switch);
		Switch.HMargin(2.0f, &Switch);
		Ui()->DoLabel(&Rect, pLabel, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = Rect.w - 4.0f});
		bool Value = *pValue != 0;
		if(ui_widget::Toggle(Context, pValue, &Value, Switch, Enabled))
			*pValue = Value;
	};
	Toggle(Hud, Localize("Show ingame HUD"), &g_Config.m_QmDemoShowHud);
	Toggle(Chat, Localize("Show chat"), &g_Config.m_QmDemoShowChat);
}

bool CMenus::DemoFilterChat(const void *pData, int Size, void *pUser)
{
	bool DoFilterChat = *(bool *)pUser;
	if(!DoFilterChat)
	{
		return false;
	}

	CUnpacker Unpacker;
	Unpacker.Reset(pData, Size);

	int Msg = Unpacker.GetInt();
	int Sys = Msg & 1;
	Msg >>= 1;

	return !Unpacker.Error() && !Sys && Msg == NETMSGTYPE_SV_CHAT;
}

void CMenus::HandleDemoSeeking(float PositionToSeek, float TimeToSeek, int TickToSeek)
{
	if((PositionToSeek >= 0.0f && PositionToSeek <= 1.0f) || TimeToSeek != 0.0f || TickToSeek >= 0)
	{
		m_DemoCutPreview.Reset();
		GameClient()->m_Chat.Reset();
		GameClient()->m_DamageInd.OnReset();
		GameClient()->m_InfoMessages.OnReset();
		GameClient()->m_Particles.OnReset();
		GameClient()->m_Trails.OnReset();
		GameClient()->m_Sounds.OnReset();
		GameClient()->m_Scoreboard.OnReset();
		GameClient()->m_Statboard.OnReset();
		GameClient()->m_SuppressEvents = true;
		if(TickToSeek >= 0)
			DemoPlayer()->SetPos(TickToSeek + 1);
		else if(TimeToSeek != 0.0f)
			DemoPlayer()->SeekTime(TimeToSeek);
		else
			DemoPlayer()->SeekPercent(PositionToSeek);
		GameClient()->m_SuppressEvents = false;

		if(!DemoPlayer()->BaseInfo()->m_Paused &&
			!DemoPlayer()->BaseInfo()->m_LiveDemo &&
			PositionToSeek == 1.0f)
		{
			DemoPlayer()->Pause();
		}
	}
}

void CMenus::DemoSeekTick(IDemoPlayer::ETickOffset TickOffset)
{
	m_DemoCutPreview.Reset();
	GameClient()->m_Trails.OnReset();
	GameClient()->m_SuppressEvents = true;
	DemoPlayer()->SeekTick(TickOffset);
	GameClient()->m_SuppressEvents = false;
	DemoPlayer()->Pause();
}

const char *CMenus::DemoBrowserBaseFolder() const
{
	return DemoBrowserBrowsingScreenshots() ? "screenshots" : "demos";
}

bool CMenus::DemoBrowserBrowsingScreenshots() const
{
	return m_DemoBrowserSource == DEMO_BROWSER_SOURCE_SCREENSHOTS;
}

bool CMenus::DemoBrowserSupportedFile(const char *pName) const
{
	return DemoBrowserBrowsingScreenshots() ? IsScreenshotBrowserFile(pName) : str_endswith_nocase(pName, ".demo") != nullptr;
}

void CMenus::ResetDemoBrowserFolder()
{
	str_copy(m_aCurrentDemoFolder, DemoBrowserBaseFolder());
	m_DemolistStorageType = IStorage::TYPE_ALL;
}

void CMenus::RenderDemoPlayer(CUIRect MainView)
{
	const IDemoPlayer::CInfo *pInfo = DemoPlayer()->BaseInfo();
	if(m_DemoCutPreview.Update(pInfo->m_CurrentTick))
	{
		const int EndTick = m_DemoCutPreview.EndTick();
		if(pInfo->m_CurrentTick > EndTick)
		{
			const auto FinishedPreview = m_DemoCutPreview;
			HandleDemoSeeking(-1.0f, 0.0f, EndTick);
			m_DemoCutPreview = FinishedPreview;
		}
		DemoPlayer()->Pause();
	}
	const int CurrentTick = pInfo->m_CurrentTick - pInfo->m_FirstTick;
	const int TotalTicks = pInfo->m_LastTick - pInfo->m_FirstTick;

	// When rendering a demo and starting paused, render the pause indicator permanently.
#if defined(CONF_VIDEORECORDER)
	const bool VideoRendering = IVideo::Current() != nullptr;
	bool InitialVideoPause = VideoRendering && m_LastPauseChange < 0.0f && pInfo->m_Paused;
#else
	const bool VideoRendering = false;
	bool InitialVideoPause = false;
#endif

	const auto &&UpdateLastPauseChange = [&]() {
		// Immediately hide the pause indicator when unpausing the initial pause when rendering a demo.
		m_LastPauseChange = InitialVideoPause ? 0.0f : Client()->GlobalTime();
		InitialVideoPause = false;
	};
	const auto &&UpdateLastSpeedChange = [&]() {
		m_LastSpeedChange = Client()->GlobalTime();
	};

	// threshold value, accounts for slight inaccuracy when setting demo position
	constexpr int Threshold = 10;
	const auto &&FindPreviousMarkerPosition = [&]() {
		const int NumTimelineMarkers = std::clamp(pInfo->m_NumTimelineMarkers, 0, (int)std::size(pInfo->m_aTimelineMarkers));
		for(int i = NumTimelineMarkers - 1; i >= 0; i--)
		{
			if((pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) < CurrentTick && absolute(((pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) - CurrentTick)) > Threshold)
			{
				return (float)(pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) / TotalTicks;
			}
		}
		return 0.0f;
	};
	const auto &&FindNextMarkerPosition = [&]() {
		const int NumTimelineMarkers = std::clamp(pInfo->m_NumTimelineMarkers, 0, (int)std::size(pInfo->m_aTimelineMarkers));
		for(int i = 0; i < NumTimelineMarkers; i++)
		{
			if((pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) > CurrentTick && absolute(((pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) - CurrentTick)) > Threshold)
			{
				return (float)(pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) / TotalTicks;
			}
		}
		return 1.0f;
	};

	static constexpr float SKIP_DURATIONS_SECONDS[] = {0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 30.0f, 60.0f, 5.0f * 60.0f, 10.0f * 60.0f};
	static constexpr const char *SKIP_DURATIONS_STRINGS[] = {"0.1", "0.5", "1", "5", "10", "30", "1", "5", "10"};
	static_assert(SKIP_DURATIONS_SECONDS[DEFAULT_SKIP_DURATION_INDEX] == 5.0f);
	static_assert(std::size(SKIP_DURATIONS_SECONDS) == std::size(SKIP_DURATIONS_STRINGS));

	const float DemoLengthSeconds = TotalTicks / static_cast<float>(Client()->GameTickSpeed());
	int NumDurationLabels = 0;
	for(size_t i = 0; i < std::size(SKIP_DURATIONS_SECONDS); ++i)
	{
		if(SKIP_DURATIONS_SECONDS[i] >= DemoLengthSeconds)
			break;
		NumDurationLabels = i + 1;
	}
	if(NumDurationLabels < 2)
	{
		m_SkipDurationIndex = 0;
	}
	else if(m_SkipDurationIndex >= NumDurationLabels)
	{
		m_SkipDurationIndex = NumDurationLabels - 1;
	}

	const auto &&NormalizePendingSlice = [&]() {
		SDemoCutSegment Range;
		qm_demo_cut::ResolveRange(g_Config.m_ClDemoSliceBegin, g_Config.m_ClDemoSliceEnd, pInfo->m_FirstTick, pInfo->m_LastTick, Range);
		return Range;
	};
	const auto PreviewCut = [&]() {
		if(VideoRendering || m_DemoPlayerState != DEMOPLAYER_NONE || Ui()->IsPopupOpen())
			return;
		if(m_DemoCutPreview.IsActive())
		{
			m_DemoCutPreview.Reset();
			DemoPlayer()->Pause();
			return;
		}
		const SDemoCutSegment Range = NormalizePendingSlice();
		if(Range.m_StartTick < 0 || Range.m_StartTick >= Range.m_EndTick)
			return;
		HandleDemoSeeking(-1.0f, 0.0f, Range.m_StartTick);
		if(DemoPlayer()->IsPlaying() && m_DemoCutPreview.Start(Range))
			DemoPlayer()->Unpause();
	};
	const auto &&AddPendingSlice = [&]() {
		if(g_Config.m_ClDemoSliceBegin == -1 && g_Config.m_ClDemoSliceEnd == -1)
			return false;
		SDemoCutSegment Segment = NormalizePendingSlice();
		if(Segment.m_StartTick < 0 || Segment.m_StartTick >= Segment.m_EndTick)
			return false;
		for(const auto &ExistingSegment : m_vDemoCutSegments)
		{
			if(ExistingSegment.m_StartTick == Segment.m_StartTick && ExistingSegment.m_EndTick == Segment.m_EndTick)
				return false;
		}
		m_vDemoCutSegments.push_back(Segment);
		m_DemoCutPreview.Reset();
		g_Config.m_ClDemoSliceBegin = -1;
		g_Config.m_ClDemoSliceEnd = -1;
		return true;
	};

	// handle keyboard shortcuts independent of active menu
	float PositionToSeek = -1.0f;
	float TimeToSeek = 0.0f;
	if(!GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Spectator.IsEditingTeleNumber() && m_DemoPlayerState == DEMOPLAYER_NONE && g_Config.m_ClDemoKeyboardShortcuts && !Ui()->IsPopupOpen())
	{
		if(!VideoRendering && !Input()->ModifierIsPressed() && !Input()->ShiftIsPressed() && !Input()->AltIsPressed())
		{
			if(Input()->KeyPress(KEY_I))
				SetCutStart();
			if(Input()->KeyPress(KEY_O))
				SetCutEnd();
			if(Input()->KeyPress(KEY_P))
				PreviewCut();
		}
		// increase/decrease speed
		if(!Input()->ModifierIsPressed() && !Input()->ShiftIsPressed() && !Input()->AltIsPressed())
		{
			if(Input()->KeyPress(KEY_P))
				PreviewCut();
			if(Input()->KeyPress(KEY_UP) || (m_MenuActive && Input()->KeyPress(KEY_MOUSE_WHEEL_UP)))
			{
				DemoPlayer()->AdjustSpeedIndex(+1);
				UpdateLastSpeedChange();
			}
			else if(Input()->KeyPress(KEY_DOWN) || (m_MenuActive && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN)))
			{
				DemoPlayer()->AdjustSpeedIndex(-1);
				UpdateLastSpeedChange();
			}
		}

		// pause/unpause
		if(Input()->KeyPress(KEY_SPACE) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || Input()->KeyPress(KEY_K))
		{
			m_DemoCutPreview.Reset();
			if(pInfo->m_Paused)
			{
				DemoPlayer()->Unpause();
			}
			else
			{
				DemoPlayer()->Pause();
			}
			UpdateLastPauseChange();
		}

		// seek backward/forward configured time
		if(Input()->KeyPress(KEY_LEFT) || Input()->KeyPress(KEY_J))
		{
			if(Input()->ModifierIsPressed())
				PositionToSeek = FindPreviousMarkerPosition();
			else if(Input()->ShiftIsPressed())
			{
				if(m_SkipDurationIndex > 0)
				{
					--m_SkipDurationIndex;
				}
			}
			else
				TimeToSeek = -SKIP_DURATIONS_SECONDS[m_SkipDurationIndex];
		}
		else if(Input()->KeyPress(KEY_RIGHT) || Input()->KeyPress(KEY_L))
		{
			if(Input()->ModifierIsPressed())
				PositionToSeek = FindNextMarkerPosition();
			else if(Input()->ShiftIsPressed())
			{
				if(m_SkipDurationIndex < NumDurationLabels - 1)
				{
					++m_SkipDurationIndex;
				}
			}
			else
				TimeToSeek = SKIP_DURATIONS_SECONDS[m_SkipDurationIndex];
		}

		// seek to 0-90%
		const int aSeekPercentKeys[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
		for(unsigned i = 0; i < std::size(aSeekPercentKeys); i++)
		{
			if(Input()->KeyPress(aSeekPercentKeys[i]))
			{
				PositionToSeek = i * 0.1f;
				break;
			}
		}

		// seek to the beginning/end
		if(Input()->KeyPress(KEY_HOME))
		{
			PositionToSeek = 0.0f;
		}
		else if(Input()->KeyPress(KEY_END))
		{
			PositionToSeek = 1.0f;
		}

		// Advance single frame forward/backward with period/comma key
		if(Input()->KeyPress(KEY_PERIOD))
		{
			DemoSeekTick(IDemoPlayer::TICK_NEXT);
		}
		else if(Input()->KeyPress(KEY_COMMA))
		{
			DemoSeekTick(IDemoPlayer::TICK_PREVIOUS);
		}
	}

	const CUIRect PlayerRect = qm_demo_ui::PlayerRect(MainView, m_DemoDisplayExpanded);
	const float SeekBarHeight = 12.0f;
	const float ButtonbarHeight = qm_demo_ui::TransportButtonSize(PlayerRect.w);
	const float NameBarHeight = 20.0f;
	const float CutBarHeight = 24.0f;
	const float Margins = 3.0f;
	const float DisplayHeight = m_DemoDisplayExpanded ? qm_demo_ui::DISPLAY_HEIGHT : 0.0f;
	const float TotalHeight = PlayerRect.h;

	if(!m_MenuActive)
	{
		// Render pause indicator
		if(g_Config.m_ClDemoShowPause && (InitialVideoPause || (!VideoRendering && Client()->GlobalTime() - m_LastPauseChange < 0.5f)))
		{
			const float Time = InitialVideoPause ? 0.5f : ((Client()->GlobalTime() - m_LastPauseChange) / 0.5f);
			const float Alpha = (Time < 0.5f ? Time : (1.0f - Time)) * 2.0f;
			if(Alpha > 0.0f)
			{
				TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(Alpha));
				TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Alpha));
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
				Ui()->DoLabel_QmIcon(Ui()->Screen(), pInfo->m_Paused ? EQmIcon::PAUSE : EQmIcon::PLAY, pInfo->m_Paused ? FONT_ICON_PAUSE : FONT_ICON_PLAY, 36.0f + Time * 12.0f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				TextRender()->SetRenderFlags(0);
			}
		}

		// Render speed info
		if(g_Config.m_ClDemoShowSpeed && Client()->GlobalTime() - m_LastSpeedChange < 1.0f)
		{
			CUIRect Screen = *Ui()->Screen();

			char aSpeedBuf[16];
			str_format(aSpeedBuf, sizeof(aSpeedBuf), "×%.2f", pInfo->m_Speed);
			TextRender()->Text(120.0f, Screen.y + Screen.h - 120.0f - TotalHeight, 60.0f, aSpeedBuf, -1.0f);
		}
	}
	else
	{
		if(m_LastPauseChange > 0.0f)
			m_LastPauseChange = 0.0f;
		if(m_LastSpeedChange > 0.0f)
			m_LastSpeedChange = 0.0f;
	}

	if(CurrentTick == TotalTicks && !m_DemoCutPreview.IsFinished())
	{
		DemoPlayer()->Pause();
		PositionToSeek = 0.0f;
		UpdateLastPauseChange();
	}

	if(!m_MenuActive)
	{
		HandleDemoSeeking(PositionToSeek, TimeToSeek);
		return;
	}

	CUIRect DemoControls;
	const CUIRect DemoControlsOriginal = qm_demo_ui::PlayerRect(MainView, TotalHeight);
	DemoControls = qm_demo_ui::DraggedPlayerRect(MainView, DemoControlsOriginal, m_DemoControlsPositionOffset.x, m_DemoControlsPositionOffset.y);
	int Corners = IGraphics::CORNER_NONE;
	if(DemoControls.x > 0.0f && DemoControls.y > 0.0f)
		Corners |= IGraphics::CORNER_TL;
	if(DemoControls.x < MainView.w - DemoControls.w && DemoControls.y > 0.0f)
		Corners |= IGraphics::CORNER_TR;
	if(DemoControls.x > 0.0f && DemoControls.y < MainView.h - DemoControls.h)
		Corners |= IGraphics::CORNER_BL;
	if(DemoControls.x < MainView.w - DemoControls.w && DemoControls.y < MainView.h - DemoControls.h)
		Corners |= IGraphics::CORNER_BR;
	DemoControls.Draw(ui_token::color::SURFACE_ELEVATED, Corners, ui_token::radius::CARD);
	const CUIRect DemoControlsDragRect = DemoControls;

	CUIRect SeekBar, TimeBar, ButtonBar, NameBar, SpeedBar, CutBar, DisplayBar;
	DemoControls.Margin(8.0f, &DemoControls);
	DemoControls.HSplitTop(NameBarHeight, &NameBar, &DemoControls);
	DemoControls.HSplitTop(4.0f, nullptr, &DemoControls);
	DemoControls.HSplitTop(SeekBarHeight, &SeekBar, &DemoControls);
	DemoControls.HSplitTop(14.0f, &TimeBar, &DemoControls);
	DemoControls.HSplitTop(22.0f, &ButtonBar, &DemoControls);
	DemoControls.HSplitTop(8.0f, nullptr, &DemoControls);
	DemoControls.HSplitTop(CutBarHeight, &CutBar, &DemoControls);
	DemoControls.HSplitTop(6.0f, nullptr, &DisplayBar);
	ButtonBar.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.045f), IGraphics::CORNER_ALL, 6.0f);
	ButtonBar.VMargin(std::max(0.0f, (ButtonBar.w - qm_demo_ui::TransportWidth(ButtonbarHeight)) * 0.5f), &ButtonBar);
	ButtonBar.HMargin((ButtonBar.h - ButtonbarHeight) * 0.5f, &ButtonBar);

	// handle draggable demo controls
	{
		enum EDragOperation
		{
			OP_NONE,
			OP_DRAGGING,
			OP_CLICKED
		};
		static EDragOperation s_Operation = OP_NONE;
		static vec2 s_InitialMouse = vec2(0.0f, 0.0f);

		bool Clicked;
		bool Abrupted;
		if(int Result = Ui()->DoDraggableButtonLogic(&s_Operation, 8, &DemoControlsDragRect, &Clicked, &Abrupted))
		{
			if(s_Operation == OP_NONE && Result == 1)
			{
				s_InitialMouse = Ui()->MousePos();
				s_Operation = OP_CLICKED;
			}

			if(Clicked || Abrupted)
				s_Operation = OP_NONE;

			if(s_Operation == OP_CLICKED && length(Ui()->MousePos() - s_InitialMouse) > 5.0f)
			{
				s_Operation = OP_DRAGGING;
				s_InitialMouse -= m_DemoControlsPositionOffset;
			}

			if(s_Operation == OP_DRAGGING)
			{
				m_DemoControlsPositionOffset = Ui()->MousePos() - s_InitialMouse;
				const CUIRect Dragged = qm_demo_ui::DraggedPlayerRect(MainView, DemoControlsOriginal, m_DemoControlsPositionOffset.x, m_DemoControlsPositionOffset.y);
				m_DemoControlsPositionOffset = vec2(Dragged.x - DemoControlsOriginal.x, Dragged.y - DemoControlsOriginal.y);
			}
		}
	}

	// Live button
	if(pInfo->m_LiveDemo)
	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		CUIRect LiveButton;
		SeekBar.VSplitRight(SeekBar.h, &SeekBar, &LiveButton);
		SeekBar.VSplitRight(2.0f, &SeekBar, nullptr);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		TextRender()->TextColor(pInfo->m_LivePlayback ? ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f) : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		Ui()->DoLabel_QmIcon(&LiveButton, EQmIcon::CIRCLE, FONT_ICON_CIRCLE, 24.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->SetRenderFlags(0);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		static char s_LiveButtonId;
		if(Ui()->HotItem() == &s_LiveButtonId)
		{
			LiveButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);
		}
		if(Ui()->DoButtonLogic(&s_LiveButtonId, 0, &LiveButton, BUTTONFLAG_LEFT))
		{
			PositionToSeek = 1.0f;
			DemoPlayer()->SetSpeedIndex(DEMO_SPEED_INDEX_DEFAULT);
			UpdateLastSpeedChange();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_LiveButtonId, &LiveButton,
			pInfo->m_LivePlayback ? Localize("Live", "Demo playback") : Localize("Go to Live", "Demo playback"));
	}

	// do seekbar
	{
		// draw seek bar
		const float Rounding = 5.0f;
		SeekBar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, Rounding);

		// draw filled bar
		float Amount = CurrentTick / (float)TotalTicks;
		CUIRect FilledBar = SeekBar;
		FilledBar.w = 2 * Rounding + (FilledBar.w - 2 * Rounding) * Amount;
		FilledBar.Draw(DEMO_ACCENT.WithAlpha(0.40f), IGraphics::CORNER_ALL, Rounding);

		// draw highlighting
		for(const auto &Segment : m_vDemoCutSegments)
		{
			const float RatioBegin = (Segment.m_StartTick - pInfo->m_FirstTick) / (float)TotalTicks;
			const float RatioEnd = (Segment.m_EndTick - pInfo->m_FirstTick) / (float)TotalTicks;
			const float Span = ((SeekBar.w - 2 * Rounding) * RatioEnd) - ((SeekBar.w - 2 * Rounding) * RatioBegin);
			if(Span <= 0.0f)
				continue;
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(ui_token::color::ACCENT_PRIMARY_DIM);
			IGraphics::CQuadItem QuadItem(Rounding + SeekBar.x + (SeekBar.w - 2 * Rounding) * RatioBegin, SeekBar.y, Span, SeekBar.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		if(g_Config.m_ClDemoSliceBegin != -1 && g_Config.m_ClDemoSliceEnd != -1)
		{
			float RatioBegin = (g_Config.m_ClDemoSliceBegin - pInfo->m_FirstTick) / (float)TotalTicks;
			float RatioEnd = (g_Config.m_ClDemoSliceEnd - pInfo->m_FirstTick) / (float)TotalTicks;
			float Span = ((SeekBar.w - 2 * Rounding) * RatioEnd) - ((SeekBar.w - 2 * Rounding) * RatioBegin);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(DEMO_ACCENT.WithAlpha(0.36f));
			IGraphics::CQuadItem QuadItem(Rounding + SeekBar.x + (SeekBar.w - 2 * Rounding) * RatioBegin, SeekBar.y, Span, SeekBar.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// draw markers
		for(int i = 0; i < pInfo->m_NumTimelineMarkers; i++)
		{
			const float Ratio = (pInfo->m_aTimelineMarkers[i] - pInfo->m_FirstTick) / (float)TotalTicks;
			const float MarkerX = Rounding + SeekBar.x + (SeekBar.w - 2 * Rounding) * Ratio;
			const float MarkerWidth = maximum(2.0f, Ui()->PixelSize() * 2.0f);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.15f, 0.1f, 0.0f, 0.75f);
			IGraphics::CQuadItem MarkerShadow(MarkerX - MarkerWidth * 0.5f + 1.0f, SeekBar.y - 3.0f, MarkerWidth, SeekBar.h + 6.0f);
			Graphics()->QuadsDrawTL(&MarkerShadow, 1);
			Graphics()->SetColor(ui_token::color::WARNING);
			IGraphics::CQuadItem MarkerLine(MarkerX - MarkerWidth * 0.5f, SeekBar.y - 3.0f, MarkerWidth, SeekBar.h + 6.0f);
			Graphics()->QuadsDrawTL(&MarkerLine, 1);
			IGraphics::CQuadItem MarkerHead(MarkerX - 4.0f, SeekBar.y - 8.0f, 8.0f, 6.0f);
			Graphics()->QuadsDrawTL(&MarkerHead, 1);
			Graphics()->QuadsEnd();
		}

		// draw slice markers
		// begin
		if(g_Config.m_ClDemoSliceBegin != -1)
		{
			float Ratio = (g_Config.m_ClDemoSliceBegin - pInfo->m_FirstTick) / (float)TotalTicks;
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(DEMO_ACCENT);
			IGraphics::CQuadItem QuadItem(Rounding + SeekBar.x + (SeekBar.w - 2 * Rounding) * Ratio, SeekBar.y - 2.0f, 3.0f, SeekBar.h + 4.0f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// end
		if(g_Config.m_ClDemoSliceEnd != -1)
		{
			float Ratio = (g_Config.m_ClDemoSliceEnd - pInfo->m_FirstTick) / (float)TotalTicks;
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(DEMO_ACCENT);
			IGraphics::CQuadItem QuadItem(Rounding + SeekBar.x + (SeekBar.w - 2 * Rounding) * Ratio, SeekBar.y - 2.0f, 3.0f, SeekBar.h + 4.0f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// draw time
		char aCurrentTime[32];
		str_time(qm_demo_cut::ToCentiseconds(CurrentTick, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aCurrentTime, sizeof(aCurrentTime));
		char aTotalTime[32];
		str_time(qm_demo_cut::ToCentiseconds(TotalTicks, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aTotalTime, sizeof(aTotalTime));
		char aSeekBarLabel[128];
		str_format(aSeekBarLabel, sizeof(aSeekBarLabel), "%s / %s", aCurrentTime, aTotalTime);
		Ui()->DoLabel(&TimeBar, aSeekBarLabel, 10.0f, TEXTALIGN_MC);
		CUIRect Playhead{SeekBar.x + Rounding + (SeekBar.w - 2.0f * Rounding) * Amount - 2.0f, SeekBar.y - 2.0f, 4.0f, SeekBar.h + 4.0f};
		Playhead.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 2.0f);

		// do the logic
		const auto &&SnapToTimelineMarker = [&](float AmountSeek) {
			if(pInfo->m_NumTimelineMarkers <= 0)
				return AmountSeek;
			const float SnapPixels = 10.0f;
			float ClosestAmount = AmountSeek;
			float ClosestDistance = SnapPixels + 1.0f;
			for(int MarkerIndex = 0; MarkerIndex < pInfo->m_NumTimelineMarkers; ++MarkerIndex)
			{
				const float MarkerAmount = std::clamp((pInfo->m_aTimelineMarkers[MarkerIndex] - pInfo->m_FirstTick) / (float)TotalTicks, 0.0f, 1.0f);
				const float Distance = absolute((MarkerAmount - AmountSeek) * (SeekBar.w - 2 * Rounding));
				if(Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					ClosestAmount = MarkerAmount;
				}
			}
			return ClosestDistance <= SnapPixels ? ClosestAmount : AmountSeek;
		};

		static char s_SeekBarId;
		if(Ui()->CheckActiveItem(&s_SeekBarId))
		{
			if(!Ui()->MouseButton(0))
			{
				if(!m_PausedBeforeSeeking)
				{
					DemoPlayer()->Unpause();
				}
				Ui()->SetActiveItem(nullptr);
			}
			else
			{
				float SeekAmount = std::clamp((Ui()->MouseX() - SeekBar.x - Rounding) / (SeekBar.w - 2 * Rounding), 0.0f, 1.0f);
				if(!Input()->ShiftIsPressed())
					SeekAmount = SnapToTimelineMarker(SeekAmount);
				if(Input()->ShiftIsPressed())
				{
					Ui()->SetMouseSlow(true);
				}
				if(absolute(m_PrevSeekAmount - SeekAmount) >= 0.0001f)
				{
					PositionToSeek = m_PrevSeekAmount = SeekAmount;
				}
			}
		}
		else if(Ui()->HotItem() == &s_SeekBarId)
		{
			if(Ui()->MouseButton(0))
			{
				m_PrevSeekAmount = -1.0f;
				m_PausedBeforeSeeking = pInfo->m_Paused;
				if(!pInfo->m_Paused)
				{
					DemoPlayer()->Pause();
				}
				Ui()->SetActiveItem(&s_SeekBarId);
			}
		}

		if(Ui()->MouseInside(&SeekBar) && !Ui()->MouseButton(0))
			Ui()->SetHotItem(&s_SeekBarId);

		if(Ui()->HotItem() == &s_SeekBarId)
		{
			const float HoveredAmount = SnapToTimelineMarker(std::clamp((Ui()->MouseX() - SeekBar.x - Rounding) / (SeekBar.w - 2 * Rounding), 0.0f, 1.0f));
			const int HoveredTick = (int)(HoveredAmount * TotalTicks);
			static char s_aHoveredTime[32];
			str_time(qm_demo_cut::ToCentiseconds(HoveredTick, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, s_aHoveredTime, sizeof(s_aHoveredTime));
			GameClient()->m_Tooltips.DoToolTip(&s_SeekBarId, &SeekBar, s_aHoveredTime);
		}
	}

	bool IncreaseDemoSpeed = false, DecreaseDemoSpeed = false;

	// do buttons
	CUIRect Button;

	// combined play and pause button
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_PlayPauseButton;
	if(Ui()->DoButton_QmIcon(&s_PlayPauseButton, pInfo->m_Paused ? EQmIcon::PLAY : EQmIcon::PAUSE, pInfo->m_Paused ? FONT_ICON_PLAY : FONT_ICON_PAUSE, false, &Button, BUTTONFLAG_LEFT))
	{
		m_DemoCutPreview.Reset();
		if(pInfo->m_Paused)
		{
			DemoPlayer()->Unpause();
		}
		else
		{
			DemoPlayer()->Pause();
		}
		UpdateLastPauseChange();
	}
	GameClient()->m_Tooltips.DoToolTip(&s_PlayPauseButton, &Button, pInfo->m_Paused ? Localize("Play the current demo") : Localize("Pause the current demo"));

	// stop button
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_ResetButton;
	if(Ui()->DoButton_QmIcon(&s_ResetButton, EQmIcon::STOP, FONT_ICON_STOP, false, &Button, BUTTONFLAG_LEFT))
	{
		DemoPlayer()->Pause();
		PositionToSeek = 0.0f;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ResetButton, &Button, Localize("Stop the current demo"));

	// skip time back
	ButtonBar.VSplitLeft(Margins + 3.0f, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_TimeBackButton;
	if(Ui()->DoButton_QmIcon(&s_TimeBackButton, EQmIcon::BACKWARD, FONT_ICON_BACKWARD, 0, &Button, BUTTONFLAG_LEFT))
	{
		TimeToSeek = -SKIP_DURATIONS_SECONDS[m_SkipDurationIndex];
	}
	GameClient()->m_Tooltips.DoToolTip(&s_TimeBackButton, &Button, Localize("Go back the specified duration"));

	// skip time dropdown
	if(NumDurationLabels >= 2)
	{
		ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
		ButtonBar.VSplitLeft(56.0f, &Button, &ButtonBar);

		static std::vector<std::string> s_vDurationNames;
		static std::vector<const char *> s_vpDurationNames;
		s_vDurationNames.resize(NumDurationLabels);
		s_vpDurationNames.resize(NumDurationLabels);

		for(int i = 0; i < NumDurationLabels; ++i)
		{
			char aBuf[256];
			if(SKIP_DURATIONS_SECONDS[i] >= 60)
				str_format(aBuf, sizeof(aBuf), Localize("%s min.", "Demo player duration"), SKIP_DURATIONS_STRINGS[i]);
			else
				str_format(aBuf, sizeof(aBuf), Localize("%s sec.", "Demo player duration"), SKIP_DURATIONS_STRINGS[i]);
			s_vDurationNames[i] = aBuf;
			s_vpDurationNames[i] = s_vDurationNames[i].c_str();
		}

		static CUi::SDropDownState s_SkipDurationDropDownState;
		static CScrollRegion s_SkipDurationDropDownScrollRegion;
		s_SkipDurationDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_SkipDurationDropDownScrollRegion;
		m_SkipDurationIndex = Ui()->DoDropDown(&Button, m_SkipDurationIndex, s_vpDurationNames.data(), NumDurationLabels, s_SkipDurationDropDownState);
		GameClient()->m_Tooltips.DoToolTip(&s_SkipDurationDropDownState.m_ButtonContainer, &Button, Localize("Change the skip duration"));
	}

	// skip time forward
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_TimeForwardButton;
	if(Ui()->DoButton_QmIcon(&s_TimeForwardButton, EQmIcon::FORWARD, FONT_ICON_FORWARD, 0, &Button, BUTTONFLAG_LEFT))
	{
		TimeToSeek = SKIP_DURATIONS_SECONDS[m_SkipDurationIndex];
	}
	GameClient()->m_Tooltips.DoToolTip(&s_TimeForwardButton, &Button, Localize("Go forward the specified duration"));

	// one tick back
	ButtonBar.VSplitLeft(Margins + 3.0f, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_OneTickBackButton;
	if(Ui()->DoButton_QmIcon(&s_OneTickBackButton, EQmIcon::BACKWARD_STEP, FONT_ICON_BACKWARD_STEP, 0, &Button, BUTTONFLAG_LEFT))
	{
		DemoSeekTick(IDemoPlayer::TICK_PREVIOUS);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_OneTickBackButton, &Button, Localize("Go back one tick"));

	// one tick forward
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_OneTickForwardButton;
	if(Ui()->DoButton_QmIcon(&s_OneTickForwardButton, EQmIcon::FORWARD_STEP, FONT_ICON_FORWARD_STEP, 0, &Button, BUTTONFLAG_LEFT))
	{
		DemoSeekTick(IDemoPlayer::TICK_NEXT);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_OneTickForwardButton, &Button, Localize("Go forward one tick"));

	// one marker back
	ButtonBar.VSplitLeft(Margins + 3.0f, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_OneMarkerBackButton;
	if(Ui()->DoButton_QmIcon(&s_OneMarkerBackButton, EQmIcon::BACKWARD_FAST, FONT_ICON_BACKWARD_FAST, 0, &Button, BUTTONFLAG_LEFT))
	{
		PositionToSeek = FindPreviousMarkerPosition();
	}
	GameClient()->m_Tooltips.DoToolTip(&s_OneMarkerBackButton, &Button, Localize("Go back one marker"));

	// one marker forward
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_OneMarkerForwardButton;
	if(Ui()->DoButton_QmIcon(&s_OneMarkerForwardButton, EQmIcon::FORWARD_FAST, FONT_ICON_FORWARD_FAST, 0, &Button, BUTTONFLAG_LEFT))
	{
		PositionToSeek = FindNextMarkerPosition();
	}
	GameClient()->m_Tooltips.DoToolTip(&s_OneMarkerForwardButton, &Button, Localize("Go forward one marker"));

	// slowdown
	ButtonBar.VSplitLeft(Margins + 3.0f, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_SlowDownButton;
	if(Ui()->DoButton_QmIcon(&s_SlowDownButton, EQmIcon::CHEVRON_DOWN, FONT_ICON_CHEVRON_DOWN, 0, &Button, BUTTONFLAG_LEFT))
		DecreaseDemoSpeed = true;
	GameClient()->m_Tooltips.DoToolTip(&s_SlowDownButton, &Button, Localize("Slow down the demo"));

	// fastforward
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_SpeedUpButton;
	if(Ui()->DoButton_QmIcon(&s_SpeedUpButton, EQmIcon::CHEVRON_UP, FONT_ICON_CHEVRON_UP, 0, &Button, BUTTONFLAG_LEFT))
		IncreaseDemoSpeed = true;
	GameClient()->m_Tooltips.DoToolTip(&s_SpeedUpButton, &Button, Localize("Speed up the demo"));

	// speed meter
	ButtonBar.VSplitLeft(36.0f, &SpeedBar, &ButtonBar);
	char aBuffer[64];
	str_format(aBuffer, sizeof(aBuffer), "×%g", pInfo->m_Speed);
	Ui()->DoLabel(&SpeedBar, aBuffer, Button.h * 0.7f, TEXTALIGN_MC);

	// slice begin button
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_SliceBeginButton;
	const int SliceBeginButtonResult = Ui()->DoButton_QmIcon(&s_SliceBeginButton, EQmIcon::RIGHT_FROM_BRACKET, FONT_ICON_RIGHT_FROM_BRACKET, 0, &Button, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	if(SliceBeginButtonResult == 1)
	{
		m_DemoCutPreview.Reset();
		Client()->DemoSliceBegin();
		if(CurrentTick > (g_Config.m_ClDemoSliceEnd - pInfo->m_FirstTick))
			g_Config.m_ClDemoSliceEnd = -1;
	}
	else if(SliceBeginButtonResult == 2)
	{
		m_DemoCutPreview.Reset();
		g_Config.m_ClDemoSliceBegin = -1;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SliceBeginButton, &Button, Localize("Mark the beginning of a cut (right click to reset)"));

	// slice end button
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_SliceEndButton;
	const int SliceEndButtonResult = Ui()->DoButton_QmIcon(&s_SliceEndButton, EQmIcon::RIGHT_TO_BRACKET, FONT_ICON_RIGHT_TO_BRACKET, 0, &Button, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	if(SliceEndButtonResult == 1)
	{
		m_DemoCutPreview.Reset();
		Client()->DemoSliceEnd();
		if(CurrentTick < (g_Config.m_ClDemoSliceBegin - pInfo->m_FirstTick))
			g_Config.m_ClDemoSliceBegin = -1;
	}
	else if(SliceEndButtonResult == 2)
	{
		m_DemoCutPreview.Reset();
		g_Config.m_ClDemoSliceEnd = -1;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SliceEndButton, &Button, Localize("Mark the end of a cut (right click to reset)"));

	// add slice button
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_SliceAddButton;
	static CUi::SMessagePopupContext s_SliceAddMessagePopupContext;
	if(Ui()->DoButton_QmIcon(&s_SliceAddButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Button, BUTTONFLAG_LEFT))
	{
		if(!AddPendingSlice())
		{
			s_SliceAddMessagePopupContext.ErrorColor();
			str_copy(s_SliceAddMessagePopupContext.m_aMessage, Localize("Current cut is invalid or already added"));
			Ui()->ShowPopupMessage(Button.x, Button.y + Button.h + 5.0f, &s_SliceAddMessagePopupContext);
		}
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SliceAddButton, &Button, Localize("Add current cut to the export list"));

	// 清除选区与列表。
	Button = CutClearButton;
	static CButtonContainer s_SliceClearButton;
	const int SliceClearButtonResult = Ui()->DoButton_QmIcon(&s_SliceClearButton, EQmIcon::TRASH, FONT_ICON_TRASH, 0, &Button, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	if(SliceClearButtonResult == 1)
	{
		m_DemoCutPreview.Reset();
		if(g_Config.m_ClDemoSliceBegin == -1 && g_Config.m_ClDemoSliceEnd == -1)
		{
			m_vDemoCutSegments.clear();
		}
		else
		{
			g_Config.m_ClDemoSliceBegin = -1;
			g_Config.m_ClDemoSliceEnd = -1;
		}
	}
	else if(SliceClearButtonResult == 2)
	{
		m_DemoCutPreview.Reset();
		m_vDemoCutSegments.clear();
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SliceClearButton, &Button, Localize("Clear current cut (right click to clear all cuts)"));

	// slice save button
#if defined(CONF_VIDEORECORDER)
	const bool SliceEnabled = IVideo::Current() == nullptr;
#else
	const bool SliceEnabled = true;
#endif
	ButtonBar.VSplitLeft(Margins, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(ButtonbarHeight, &Button, &ButtonBar);
	static CButtonContainer s_SliceSaveButton;
	if(Ui()->DoButton_QmIcon(&s_SliceSaveButton, EQmIcon::ARROW_UP_RIGHT_FROM_SQUARE, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, SliceEnabled) && SliceEnabled)
	{
		m_DemoCutPreview.Reset();
		char aDemoName[IO_MAX_PATH_LENGTH];
		DemoPlayer()->GetDemoName(aDemoName, sizeof(aDemoName));
		str_format(aCutName, sizeof(aCutName), "%s_cut", aDemoName);
		m_DemoSliceInput.Set(aCutName);
		Ui()->SetActiveItem(&m_DemoSliceInput);
		m_DemoPlayerState = DEMOPLAYER_SLICE_SAVE;
		m_DemoExportDisplayExpanded = false;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_CutExportButton, &CutExportButton, Localize("Export cut as a separate demo"));

	// close button
	NameBar.VSplitRight(ButtonbarHeight, &NameBar, &Button);
	static CButtonContainer s_ExitButton;
	if(Ui()->DoButton_QmIcon(&s_ExitButton, EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &Button, BUTTONFLAG_LEFT) || (Input()->KeyPress(KEY_C) && !GameClient()->m_GameConsole.IsActive() && m_DemoPlayerState == DEMOPLAYER_NONE))
	{
		Client()->Disconnect();
		SetMenuPage(PAGE_DEMOS);
		DemolistOnUpdate(false);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ExitButton, &Button, Localize("Close the demo player"));

	// toggle keyboard shortcuts button
	NameBar.VSplitRight(Margins, &NameBar, nullptr);
	NameBar.VSplitRight(ButtonbarHeight, &NameBar, &Button);
	static CButtonContainer s_KeyboardShortcutsButton;
	if(Ui()->DoButton_QmIcon(&s_KeyboardShortcutsButton, EQmIcon::KEYBOARD, FONT_ICON_KEYBOARD, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, g_Config.m_ClDemoKeyboardShortcuts != 0))
	{
		g_Config.m_ClDemoKeyboardShortcuts ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_KeyboardShortcutsButton, &Button, Localize("Toggle keyboard shortcuts"));

	// auto camera button (only available when it is possible to use)
	if(GameClient()->m_Camera.CanUseAutoSpecCamera())
	{
		NameBar.VSplitRight(Margins, &NameBar, nullptr);
		NameBar.VSplitRight(ButtonbarHeight, &NameBar, &Button);
		static CButtonContainer s_AutoCameraButton;
		if(Ui()->DoButton_QmIcon(&s_AutoCameraButton, EQmIcon::CAMERA, FONT_ICON_CAMERA, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, GameClient()->m_Camera.m_AutoSpecCamera))
		{
			GameClient()->m_Camera.m_AutoSpecCamera = !GameClient()->m_Camera.m_AutoSpecCamera;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_AutoCameraButton, &Button, Localize("Toggle auto camera"));
	}

	NameBar.VSplitRight(Margins, &NameBar, nullptr);
	NameBar.VSplitRight(80.0f, &NameBar, &Button);
	static CButtonContainer s_DisplayButton;
	if(DoButton_Menu(&s_DisplayButton, Localize("Demo display"), CutControlsEnabled ? 0 : -1, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.0f, 0.0f, m_DemoDisplayExpanded ? DEMO_ACCENT : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), nullptr, 11.0f) && CutControlsEnabled)
		m_DemoDisplayExpanded = !m_DemoDisplayExpanded;
	NameBar.VSplitRight(8.0f, &NameBar, nullptr);
	if(DisplayHeight > 0.0f && m_DemoPlayerState == DEMOPLAYER_NONE && m_Popup == POPUP_NONE)
		RenderDemoDisplaySettings(DisplayBar, CutControlsEnabled);

	// demo name
	CUIRect PreviewButton;
	NameBar.VSplitRight(ButtonbarHeight, &NameBar, &PreviewButton);
	const SDemoCutSegment PendingCut = NormalizePendingSlice();
	const bool PreviewEnabled = !VideoRendering && m_DemoPlayerState == DEMOPLAYER_NONE && !Ui()->IsPopupOpen() &&
				    ((PendingCut.m_StartTick >= 0 && PendingCut.m_StartTick < PendingCut.m_EndTick) || m_DemoCutPreview.IsActive());
	static CButtonContainer s_CutPreviewButton;
	if(Ui()->DoButton_QmIcon(&s_CutPreviewButton, m_DemoCutPreview.IsActive() ? EQmIcon::STOP : EQmIcon::PLAY,
		   m_DemoCutPreview.IsActive() ? FONT_ICON_STOP : FONT_ICON_PLAY, 0, &PreviewButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, PreviewEnabled) &&
		PreviewEnabled)
		PreviewCut();
	GameClient()->m_Tooltips.DoToolTip(&s_CutPreviewButton, &PreviewButton, m_DemoCutPreview.IsActive() ? Localize("Stop preview (P)") : Localize("Preview cut (P)"));
	char aDemoName[IO_MAX_PATH_LENGTH];
	DemoPlayer()->GetDemoName(aDemoName, sizeof(aDemoName));
	char aBuf[IO_MAX_PATH_LENGTH + 128];
	str_format(aBuf, sizeof(aBuf), Localize("Demofile: %s"), aDemoName);
	SLabelProperties Props;
	Props.m_MaxWidth = NameBar.w;
	Props.m_EllipsisAtEnd = true;
	Props.m_EnableWidthCheck = false;
	Ui()->DoLabel(&NameBar, aBuf, 12.0f, TEXTALIGN_ML, Props);

	if(IncreaseDemoSpeed)
	{
		DemoPlayer()->AdjustSpeedIndex(+1);
		UpdateLastSpeedChange();
	}
	else if(DecreaseDemoSpeed)
	{
		DemoPlayer()->AdjustSpeedIndex(-1);
		UpdateLastSpeedChange();
	}

	HandleDemoSeeking(PositionToSeek, TimeToSeek);

	// render popups
	if(m_DemoPlayerState != DEMOPLAYER_NONE)
	{
		// prevent element under the active popup from being activated
		Ui()->SetHotItem(nullptr);
	}
}

void CMenus::RenderDemoPlayerSliceSavePopup(CUIRect MainView)
{
	const IDemoPlayer::CInfo *pInfo = DemoPlayer()->BaseInfo();

	const bool DisplayExpanded = m_DemoExportDisplayExpanded;
	const float DisplayPanelHeight = DisplayExpanded ? qm_demo_ui::DISPLAY_HEIGHT + 4.0f : 0.0f;
	const float ContentHeight = qm_demo_ui::SliceContentHeight((int)m_vDemoCutSegments.size(), DisplayExpanded);
	CUIRect Box = qm_demo_ui::PopupRect(MainView, ContentHeight + 116.0f);

	// background
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 15.0f);
	Box.Margin(24.0f, &Box);

	// title
	CUIRect Title;
	Box.HSplitTop(22.0f, &Title, &Box);
	Box.HSplitTop(6.0f, nullptr, &Box);
	Ui()->DoLabel(&Title, Localize("Export demo cut"), 16.0f, TEXTALIGN_ML);

	// 标题和确认按钮固定；缩放后的窗口通过正文滚动容纳完整选项。
	CUIRect ButtonBar, AbortButton, OkButton;
	Box.HSplitBottom(26.0f, &Box, &ButtonBar);
	Box.HSplitBottom(8.0f, &Box, nullptr);
	static CScrollRegion s_ContentScroll;
	vec2 ScrollOffset;
	s_ContentScroll.Begin(&Box, &ScrollOffset);
	Box.y += ScrollOffset.y;
	Box.h = ContentHeight;
	s_ContentScroll.AddRect(Box);

	CUIRect ButtonBar, AbortButton, OkButton;
	Box.HSplitBottom(24.0f, &Box, &ButtonBar);
	static CScrollRegion s_ContentScroll;
	vec2 ScrollOffset;
	s_ContentScroll.Begin(&Box, &ScrollOffset);
	Box.y += ScrollOffset.y;
	Box.h = ContentHeight;
	s_ContentScroll.AddRect(Box);

	// slice times
	CUIRect SliceTimesBar, SliceInterval, SliceLength;
	Box.HSplitTop(20.0f, &SliceTimesBar, &Box);
	SliceTimesBar.VSplitMid(&SliceInterval, &SliceLength, 12.0f);
	Box.HSplitTop(6.0f, nullptr, &Box);

	std::vector<SDemoCutSegment> vExportSegments = m_vDemoCutSegments;
	if(vExportSegments.empty() && (g_Config.m_ClDemoSliceBegin != -1 || g_Config.m_ClDemoSliceEnd != -1))
	{
		SDemoCutSegment Range;
		if(qm_demo_cut::ResolveRange(g_Config.m_ClDemoSliceBegin, g_Config.m_ClDemoSliceEnd, pInfo->m_FirstTick, pInfo->m_LastTick, Range))
			vExportSegments.push_back(Range);
	}
	std::sort(vExportSegments.begin(), vExportSegments.end(), [](const SDemoCutSegment &Left, const SDemoCutSegment &Right) {
		if(Left.m_StartTick != Right.m_StartTick)
			return Left.m_StartTick < Right.m_StartTick;
		return Left.m_EndTick < Right.m_EndTick;
	});
	int64_t TotalCutTicks = 0;
	for(const auto &Segment : vExportSegments)
	{
		TotalCutTicks += maximum<int64_t>(0, (int64_t)Segment.m_EndTick - Segment.m_StartTick + 1);
	}
	char aSliceLength[32];
	str_time(qm_demo_cut::ToCentiseconds(maximum<int64_t>(0, TotalCutTicks - 1), Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aSliceLength, sizeof(aSliceLength));
	char aBuf[256];
	if(vExportSegments.size() > 1)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Cut segments"), (int)vExportSegments.size());
	}
	else
	{
		const int64_t RealSliceBegin = vExportSegments.empty() ? 0 : vExportSegments[0].m_StartTick - pInfo->m_FirstTick;
		const int64_t RealSliceEnd = vExportSegments.empty() ? 0 : vExportSegments[0].m_EndTick - pInfo->m_FirstTick;
		char aSliceBegin[32];
		str_time(qm_demo_cut::ToCentiseconds(RealSliceBegin, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aSliceBegin, sizeof(aSliceBegin));
		char aSliceEnd[32];
		str_time(qm_demo_cut::ToCentiseconds(RealSliceEnd, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aSliceEnd, sizeof(aSliceEnd));
		str_format(aBuf, sizeof(aBuf), "%s: %s – %s", Localize("Cut interval"), aSliceBegin, aSliceEnd);
	}
	Ui()->DoLabel(&SliceInterval, aBuf, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = SliceInterval.w});
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Cut length"), aSliceLength);
	Ui()->DoLabel(&SliceLength, aBuf, 11.0f, TEXTALIGN_MR, {.m_MaxWidth = SliceLength.w});

	if(!m_vDemoCutSegments.empty())
	{
		CUIRect SegmentsHeader, SegmentsList;
		const float RemainingControlsHeight = 110.0f + DisplayPanelHeight;
		constexpr float SegmentsHeaderHeight = 20.0f;
		constexpr float SegmentRowHeight = 22.0f;
		constexpr float SegmentSpacing = 6.0f;
		const int DesiredVisibleSegments = minimum<int>((int)m_vDemoCutSegments.size(), 4);
		const float DesiredSegmentsHeight = SegmentsHeaderHeight + DesiredVisibleSegments * SegmentRowHeight + SegmentSpacing;
		const float SegmentsAreaHeight = std::clamp(Box.h - RemainingControlsHeight, 0.0f, DesiredSegmentsHeight);
		CUIRect SegmentsArea;
		Box.HSplitTop(SegmentsAreaHeight, &SegmentsArea, &Box);
		SegmentsArea.HSplitTop(std::min(SegmentsHeaderHeight, SegmentsArea.h), &SegmentsHeader, &SegmentsArea);
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Cut segments"), (int)m_vDemoCutSegments.size());
		Ui()->DoLabel(&SegmentsHeader, aBuf, 14.0f, TEXTALIGN_ML);

		const int NumVisibleSegments = QmUiVisibleRows(SegmentsArea.h, 0.0f, SegmentRowHeight, (int)m_vDemoCutSegments.size(), 4);
		SegmentsArea.HSplitTop(NumVisibleSegments * SegmentRowHeight, &SegmentsList, &SegmentsArea);
		if(NumVisibleSegments > 0)
		{
			static CListBox s_CutSegmentsList;
			static std::vector<CButtonContainer> s_vDeleteButtons;
			static std::vector<CButtonContainer> s_vPreviewButtons;
			s_vDeleteButtons.resize(m_vDemoCutSegments.size());
			s_vPreviewButtons.resize(m_vDemoCutSegments.size());
			int DeleteIndex = -1;
			int PreviewIndex = -1;
			s_CutSegmentsList.SetActive(!Ui()->IsPopupOpen() && !m_DemoSliceInput.IsActive());
			s_CutSegmentsList.DoStart(SegmentRowHeight, (int)m_vDemoCutSegments.size(), 1, 3, -1, &SegmentsList, false);
			for(size_t SegmentIndex = 0; SegmentIndex < m_vDemoCutSegments.size(); ++SegmentIndex)
			{
				const SDemoCutSegment &Segment = m_vDemoCutSegments[SegmentIndex];
				const CListboxItem Item = s_CutSegmentsList.DoNextItem(&Segment);
				if(!Item.m_Visible)
					continue;
				CUIRect SegmentLabel, DeleteButton, PreviewButton;
				Item.m_Rect.VSplitRight(SegmentRowHeight, &SegmentLabel, &DeleteButton);
				SegmentLabel.VSplitRight(SegmentRowHeight, &SegmentLabel, &PreviewButton);
				char aSliceBegin[32], aSliceEnd[32];
				str_time(qm_demo_cut::ToCentiseconds((int64_t)Segment.m_StartTick - pInfo->m_FirstTick, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aSliceBegin, sizeof(aSliceBegin));
				str_time(qm_demo_cut::ToCentiseconds((int64_t)Segment.m_EndTick - pInfo->m_FirstTick, Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aSliceEnd, sizeof(aSliceEnd));
				str_format(aBuf, sizeof(aBuf), "#%d  %s – %s", (int)SegmentIndex + 1, aSliceBegin, aSliceEnd);
				Ui()->DoLabel(&SegmentLabel, aBuf, 12.0f, TEXTALIGN_ML, {.m_MaxWidth = SegmentLabel.w});
				if(Ui()->DoButton_QmIcon(&s_vDeleteButtons[SegmentIndex], EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &DeleteButton, BUTTONFLAG_LEFT))
					DeleteIndex = (int)SegmentIndex;
				if(Ui()->DoButton_QmIcon(&s_vPreviewButtons[SegmentIndex], EQmIcon::PLAY, FONT_ICON_PLAY, 0, &PreviewButton, BUTTONFLAG_LEFT))
					PreviewIndex = (int)SegmentIndex;
				GameClient()->m_Tooltips.DoToolTip(&s_vPreviewButtons[SegmentIndex], &PreviewButton, Localize("Preview"));
			}
			s_CutSegmentsList.DoEnd();
			if(DeleteIndex >= 0)
			{
				m_vDemoCutSegments.erase(m_vDemoCutSegments.begin() + DeleteIndex);
				Ui()->SetActiveItem(nullptr);
				s_ContentScroll.End();
				return;
			}
			if(PreviewIndex >= 0)
			{
				const SDemoCutSegment Segment = m_vDemoCutSegments[PreviewIndex];
				g_Config.m_ClDemoSliceBegin = Segment.m_StartTick;
				g_Config.m_ClDemoSliceEnd = Segment.m_EndTick;
				HandleDemoSeeking(-1.0f, 0.0f, Segment.m_StartTick);
				if(DemoPlayer()->IsPlaying() && m_DemoCutPreview.Start(Segment))
					DemoPlayer()->Unpause();
				m_DemoPlayerState = DEMOPLAYER_NONE;
				Ui()->SetActiveItem(nullptr);
				s_ContentScroll.End();
				return;
			}
		}
	}

	IUiContext DemoSliceTextInputCtx;
	DemoSliceTextInputCtx.m_pUi = Ui();
	DemoSliceTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	DemoSliceTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	DemoSliceTextInputCtx.m_ScopeHash = MakeUiScopeHash("demo_slice_text_input");
	DemoSliceTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	// file name
	CUIRect NameLabel, NameBox;
	Box.HSplitTop(24.0f, &NameLabel, &Box);
	Box.HSplitTop(6.0f, nullptr, &Box);
	NameLabel.VSplitLeft(88.0f, &NameLabel, &NameBox);
	NameBox.VSplitLeft(8.0f, nullptr, &NameBox);
	Ui()->DoLabel(&NameLabel, Localize("New name:"), 12.0f, TEXTALIGN_ML, {.m_MaxWidth = NameLabel.w});
	ui_widget::SInputFieldOptions SliceNameInputOptions;
	SliceNameInputOptions.m_pPlaceholder = Localize("New name");
	SliceNameInputOptions.m_FontSize = 12.0f;
	ui_widget::InputField(DemoSliceTextInputCtx, &m_DemoSliceInput, NameBox, SliceNameInputOptions);

	// remove chat checkbox
	static int s_RemoveChat = 0;

	CUIRect CheckBoxBar, RemoveChatCheckBox, RenderCutCheckBox;
	Box.HSplitTop(22.0f, &CheckBoxBar, &Box);
	Box.HSplitTop(6.0f, nullptr, &Box);
	CheckBoxBar.VSplitMid(&RemoveChatCheckBox, &RenderCutCheckBox, 12.0f);
	if(DoButton_CheckBox(&s_RemoveChat, Localize("Remove chat"), s_RemoveChat, &RemoveChatCheckBox))
	{
		s_RemoveChat ^= 1;
	}
#if defined(CONF_VIDEORECORDER)
	static int s_RenderCut = 0;
	if(DoButton_CheckBox(&s_RenderCut, Localize("Render cut to video"), s_RenderCut, &RenderCutCheckBox))
	{
		s_RenderCut ^= 1;
	}
#endif

	// 回放/导出显示选项：折叠开关常驻，展开后显示三组段选与两个开关。
	CUIRect DisplayOptions;
	Box.HSplitTop(22.0f, &DisplayOptions, &Box);
	RenderDemoExportDisplayToggle(DisplayOptions);
	if(m_DemoExportDisplayExpanded)
	{
		Box.HSplitTop(4.0f, nullptr, &Box);
		Box.HSplitTop(qm_demo_ui::DISPLAY_HEIGHT, &DisplayOptions, &Box);
		RenderDemoDisplaySettings(DisplayOptions, !Ui()->IsPopupOpen());
	}
	s_ContentScroll.End();

	// buttons
	ButtonBar.VSplitMid(&AbortButton, &OkButton, 40.0f);

	// buttons
	ButtonBar.VSplitRight(96.0f, &ButtonBar, &OkButton);
	ButtonBar.VSplitRight(8.0f, &ButtonBar, nullptr);
	ButtonBar.VSplitRight(96.0f, &ButtonBar, &AbortButton);

	static CUi::SConfirmPopupContext s_ConfirmPopupContext;
	static CButtonContainer s_ButtonAbort;
	if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &AbortButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), nullptr, 12.0f) || (!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)))
	{
		if(str_endswith_nocase(m_DemoSliceInput.GetString(), ".demo"))
		{
			char aNameWithoutExt[IO_MAX_PATH_LENGTH];
			fs_split_file_extension(m_DemoSliceInput.GetString(), aNameWithoutExt, sizeof(aNameWithoutExt));
			m_DemoSliceInput.Set(aNameWithoutExt);
		}

		static CUi::SMessagePopupContext s_MessagePopupContext;
		char aDemoName[IO_MAX_PATH_LENGTH];
		DemoPlayer()->GetDemoName(aDemoName, sizeof(aDemoName));
		if(str_comp_nocase(aDemoName, m_DemoSliceInput.GetString()) == 0)
		{
			s_MessagePopupContext.ErrorColor();
			str_copy(s_MessagePopupContext.m_aMessage, Localize("Please use a different filename"));
			Ui()->ShowPopupMessage(Ui()->MouseX(), OkButton.y + OkButton.h + 5.0f, &s_MessagePopupContext);
		}
		else if(!str_valid_filename(m_DemoSliceInput.GetString()))
		{
			s_MessagePopupContext.ErrorColor();
			str_copy(s_MessagePopupContext.m_aMessage, Localize("This name cannot be used for files and folders"));
			Ui()->ShowPopupMessage(Ui()->MouseX(), OkButton.y + OkButton.h + 5.0f, &s_MessagePopupContext);
		}
		else if(vExportSegments.empty())
		{
			s_MessagePopupContext.ErrorColor();
			str_copy(s_MessagePopupContext.m_aMessage, Localize("Please add a valid cut first"));
			Ui()->ShowPopupMessage(Ui()->MouseX(), OkButton.y + OkButton.h + 5.0f, &s_MessagePopupContext);
		}
		else
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "%s/%s.demo", m_aCurrentDemoFolder, m_DemoSliceInput.GetString());
			if(Storage()->FileExists(aPath, IStorage::TYPE_SAVE))
			{
				s_ConfirmPopupContext.Reset();
				s_ConfirmPopupContext.YesNoButtons();
				str_copy(s_ConfirmPopupContext.m_aMessage, Localize("File already exists, do you want to overwrite it?"));
				Ui()->ShowPopupConfirm(Ui()->MouseX(), OkButton.y + OkButton.h + 5.0f, &s_ConfirmPopupContext);
			}
			else
			{
				s_ConfirmPopupContext.m_Result = CUi::SConfirmPopupContext::CONFIRMED;
			}
		}
	}

	if(s_ConfirmPopupContext.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
	{
		s_ConfirmPopupContext.Reset();
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.demo", m_aCurrentDemoFolder, m_DemoSliceInput.GetString());

		std::vector<SDemoSliceSegment> vDemoSliceSegments;
		vDemoSliceSegments.reserve(vExportSegments.size());
		for(const auto &Segment : vExportSegments)
			vDemoSliceSegments.push_back({Segment.m_StartTick, Segment.m_EndTick});
		static CUi::SMessagePopupContext s_MessagePopupContext;
		if(!Client()->DemoSlice(aPath, vDemoSliceSegments, CMenus::DemoFilterChat, &s_RemoveChat))
		{
			s_MessagePopupContext.ErrorColor();
			str_copy(s_MessagePopupContext.m_aMessage, Localize("Failed to export demo cut"));
			Ui()->ShowPopupMessage(Ui()->MouseX(), ButtonBar.y - 5.0f, &s_MessagePopupContext);
			return;
		}
		str_format(m_aCurrentDemoSelectionName, sizeof(m_aCurrentDemoSelectionName), "%s.demo", m_DemoSliceInput.GetString());
		DemolistPopulate();
		DemolistOnUpdate(false);
		m_vDemoCutSegments.clear();
		m_DemoCutPreview.Reset();
		m_DemoPlayerState = DEMOPLAYER_NONE;
#if defined(CONF_VIDEORECORDER)
		if(s_RenderCut)
		{
			m_HasPendingDemoRenderSource = true;
			str_copy(m_aPendingDemoRenderFolder, m_aCurrentDemoFolder, sizeof(m_aPendingDemoRenderFolder));
			str_format(m_aPendingDemoRenderSelectionName, sizeof(m_aPendingDemoRenderSelectionName), "%s.demo", m_DemoSliceInput.GetString());
			m_PendingDemoRenderStorageType = IStorage::TYPE_SAVE;
			m_DemoExportDisplayExpanded = false;
			m_Popup = POPUP_RENDER_DEMO;
			m_StartPaused = false;
			m_DemoRenderInput.Set(m_DemoSliceInput.GetString());
			Ui()->SetActiveItem(&m_DemoRenderInput);
			if(m_DemolistStorageType != IStorage::TYPE_ALL && m_DemolistStorageType != IStorage::TYPE_SAVE)
				m_DemolistStorageType = IStorage::TYPE_ALL; // Select a storage type containing the sliced demo
			DemolistOnUpdate(false);
		}
#endif
	}
	if(s_ConfirmPopupContext.m_Result != CUi::SConfirmPopupContext::UNSET)
	{
		s_ConfirmPopupContext.Reset();
	}
}

int CMenus::DemolistFetchCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CMenus *pSelf = (CMenus *)pUser;
	const char *pBaseFolder = pSelf->DemoBrowserBaseFolder();
	if(str_comp(pName, ".") == 0 ||
		(str_comp(pName, "..") == 0 && (pSelf->m_aCurrentDemoFolder[0] == '\0' || (!pSelf->m_DemolistMultipleStorages && str_comp(pSelf->m_aCurrentDemoFolder, pBaseFolder) == 0))) ||
		(!IsDir && !pSelf->DemoBrowserSupportedFile(pName)))
	{
		return 0;
	}

	CDemoItem Item;
	str_copy(Item.m_aFilename, pName);
	if(IsDir)
	{
		// QmClient：上级目录显示可读文案，代替技术风格的 "../"
		if(str_comp(pName, "..") == 0)
			str_copy(Item.m_aName, Localize("Parent Folder"));
		else
			str_format(Item.m_aName, sizeof(Item.m_aName), "%s/", pName);
		Item.m_Date = 0;
		Item.m_DateLoaded = true;
		Item.m_DateValid = false;
		Item.m_Size = 0;
		Item.m_SizeLoaded = true;
	}
	else
	{
		str_copy(Item.m_aName, pName);
		Item.m_Date = 0;
		Item.m_DateLoaded = false;
		Item.m_DateValid = false;
		Item.m_Size = 0;
		Item.m_SizeLoaded = false;
	}
	Item.m_InfosLoaded = false;
	Item.m_Valid = false;
	Item.m_IsDir = IsDir != 0;
	Item.m_IsLink = false;
	Item.m_StorageType = StorageType;
	pSelf->m_vDemos.push_back(Item);

	if(time_get_nanoseconds() - pSelf->m_DemoPopulateStartTime > 500ms)
	{
		pSelf->RenderLoading(pSelf->DemoBrowserBrowsingScreenshots() ? Localize("Loading screenshot files") : Localize("Loading demo files"), "", 0);
	}

	return 0;
}

bool CMenus::EnsureDemoDate(CDemoItem &Item)
{
	if(Item.m_IsDir)
		return false;
	if(Item.m_DateLoaded)
		return Item.m_DateValid;

	char aBuffer[IO_MAX_PATH_LENGTH];
	str_format(aBuffer, sizeof(aBuffer), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
	time_t Created;
	time_t Modified;
	Item.m_DateValid = Storage()->RetrieveTimes(aBuffer, Item.m_StorageType, &Created, &Modified);
	Item.m_Date = Item.m_DateValid ? Modified : 0;
	Item.m_DateLoaded = true;
	return Item.m_DateValid;
}

bool CMenus::EnsureDemoSize(CDemoItem &Item)
{
	if(Item.m_IsDir)
		return false;
	if(Item.m_SizeLoaded)
		return true;

	char aBuffer[IO_MAX_PATH_LENGTH];
	str_format(aBuffer, sizeof(aBuffer), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
	IOHANDLE File = Storage()->OpenFile(aBuffer, IOFLAG_READ, Item.m_StorageType);
	if(!File)
		return false;

	Item.m_Size = io_length(File);
	Item.m_SizeLoaded = true;
	io_close(File);
	return true;
}

void CMenus::EnsureAllDemoDates()
{
	m_DemoDateFetchCursor = 0;
	m_DemoDateFetchComplete = false;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameId = Client()->PerfFrame();
	str_copy(Input.m_aOperation, SettingsPerfActiveOperation(), sizeof(Input.m_aOperation));
	str_copy(Input.m_aPage, "demo_browser", sizeof(Input.m_aPage));
	str_copy(Input.m_aTab, "metadata", sizeof(Input.m_aTab));
	str_copy(Input.m_aContext, SettingsPerfContextName(), sizeof(Input.m_aContext));
	Input.m_FrameMsAverage = (float)GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_FrameTimeMs;
	Input.m_FrameMsP95 = Input.m_FrameMsAverage;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_BackgroundBacklog = (int)m_vDemos.size();
	Input.m_WindowActive = true;
	const SSettingsAdaptiveBudgetOutput AdaptiveBudget = BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::DemoBrowser, "demo_browser", Input);
	AdvanceDemoBrowserMetadata(0, maximum(1, AdaptiveBudget.m_DemoMetadataTokens), "ensure_dates");
}

void CMenus::ResetDemoBrowserMetadataProgress()
{
	m_DemoHeaderFetchCursor = 0;
	m_DemoDateFetchCursor = 0;
	m_DemoHeaderFetchComplete = DemoBrowserBrowsingScreenshots() || !g_Config.m_BrDemoFetchInfo;
	m_DemoDateFetchComplete = g_Config.m_BrDemoSort != SORT_DATE;
}

void CMenus::AdvanceDemoBrowserMetadata(int HeaderBudget, int DateBudget, const char *pTrigger, int VisibleFirst, int VisibleEnd)
{
	const char *pSource = DemoBrowserBrowsingScreenshots() ? "screenshots" : "demos";
	const auto ClampVisibleWindow = [&](int &First, int &End) {
		if(First < 0 || End <= First || m_vpFilteredDemos.empty())
		{
			First = -1;
			End = -1;
			return;
		}
		First = minimum(maximum(First, 0), (int)m_vpFilteredDemos.size());
		End = minimum(maximum(End, First), (int)m_vpFilteredDemos.size());
	};
	ClampVisibleWindow(VisibleFirst, VisibleEnd);
	const bool HasVisibleWindow = VisibleFirst >= 0 && VisibleEnd > VisibleFirst;
	if(!m_DemoHeaderFetchComplete && HeaderBudget > 0)
	{
		CPerfTimer Timer;
		int Scanned = 0;
		int Done = 0;
		int VisibleScanned = 0;
		int VisibleDone = 0;
		int BackgroundScanned = 0;
		int BackgroundDone = 0;
		int RemainingBudget = HeaderBudget;
		if(HasVisibleWindow)
		{
			for(int i = VisibleFirst; i < VisibleEnd && RemainingBudget > 0; ++i)
			{
				CDemoItem &Item = *m_vpFilteredDemos[i];
				if(!Item.IsDemoFile())
					continue;
				++Scanned;
				++VisibleScanned;
				if(!Item.m_InfosLoaded)
				{
					FetchHeader(Item);
					--RemainingBudget;
				}
				if(Item.m_InfosLoaded)
				{
					++Done;
					++VisibleDone;
				}
			}
		}
		while(m_DemoBrowserMetadataBackgroundAllowed && m_DemoHeaderFetchCursor < m_vDemos.size() && RemainingBudget > 0)
		{
			CDemoItem &Item = m_vDemos[m_DemoHeaderFetchCursor++];
			if(!Item.IsDemoFile())
				continue;
			++Scanned;
			++BackgroundScanned;
			if(!Item.m_InfosLoaded)
			{
				FetchHeader(Item);
				--RemainingBudget;
			}
			if(Item.m_InfosLoaded)
			{
				++Done;
				++BackgroundDone;
			}
		}
		m_DemoHeaderFetchComplete = m_DemoHeaderFetchCursor >= m_vDemos.size();
		if(m_DemoHeaderFetchComplete)
		{
			std::stable_sort(m_vDemos.begin(), m_vDemos.end());
			DemolistOnUpdate(false);
		}
		if(QmPerfEnabled())
		{
			char aPayload[384];
			str_format(aPayload, sizeof(aPayload), "event=demo_browser_header_fetch items_total=%d items_scanned=%d items_done=%d visible_first=%d visible_end=%d visible_scanned=%d visible_done=%d background_scanned=%d background_done=%d remaining=%d budget=%d dur_ms=%.3f trigger=%s source=%s sort=%d fetch_info=%d",
				(int)m_vDemos.size(), Scanned, Done, VisibleFirst, VisibleEnd, VisibleScanned, VisibleDone, BackgroundScanned, BackgroundDone, maximum(0, (int)m_vDemos.size() - (int)m_DemoHeaderFetchCursor), HeaderBudget, Timer.ElapsedMs(), pTrigger, pSource, g_Config.m_BrDemoSort, g_Config.m_BrDemoFetchInfo);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
		}
	}

	if(!m_DemoDateFetchComplete && DateBudget > 0)
	{
		CPerfTimer Timer;
		int Scanned = 0;
		int Done = 0;
		int VisibleScanned = 0;
		int VisibleDone = 0;
		int BackgroundScanned = 0;
		int BackgroundDone = 0;
		int RemainingBudget = DateBudget;
		if(HasVisibleWindow)
		{
			for(int i = VisibleFirst; i < VisibleEnd && RemainingBudget > 0; ++i)
			{
				CDemoItem &Item = *m_vpFilteredDemos[i];
				if(Item.m_IsDir)
					continue;
				++Scanned;
				++VisibleScanned;
				if(!Item.m_DateLoaded)
				{
					EnsureDemoDate(Item);
					--RemainingBudget;
				}
				if(Item.m_DateLoaded)
				{
					++Done;
					++VisibleDone;
				}
			}
		}
		while(m_DemoBrowserMetadataBackgroundAllowed && m_DemoDateFetchCursor < m_vDemos.size() && RemainingBudget > 0)
		{
			CDemoItem &Item = m_vDemos[m_DemoDateFetchCursor++];
			if(Item.m_IsDir)
				continue;
			++Scanned;
			++BackgroundScanned;
			if(!Item.m_DateLoaded)
			{
				EnsureDemoDate(Item);
				--RemainingBudget;
			}
			if(Item.m_DateLoaded)
			{
				++Done;
				++BackgroundDone;
			}
		}
		m_DemoDateFetchComplete = m_DemoDateFetchCursor >= m_vDemos.size();
		if(m_DemoDateFetchComplete)
		{
			std::stable_sort(m_vDemos.begin(), m_vDemos.end());
			DemolistOnUpdate(false);
		}
		if(QmPerfEnabled())
		{
			char aPayload[384];
			str_format(aPayload, sizeof(aPayload), "event=demo_browser_date_fetch items_total=%d items_scanned=%d items_done=%d visible_first=%d visible_end=%d visible_scanned=%d visible_done=%d background_scanned=%d background_done=%d remaining=%d budget=%d dur_ms=%.3f trigger=%s source=%s sort=%d fetch_info=%d",
				(int)m_vDemos.size(), Scanned, Done, VisibleFirst, VisibleEnd, VisibleScanned, VisibleDone, BackgroundScanned, BackgroundDone, maximum(0, (int)m_vDemos.size() - (int)m_DemoDateFetchCursor), DateBudget, Timer.ElapsedMs(), pTrigger, pSource, g_Config.m_BrDemoSort, g_Config.m_BrDemoFetchInfo);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
		}
	}
}

void CMenus::DemolistPopulate()
{
	CPerfTimer StartupTimer;
	m_vDemos.clear();

	int NumStoragesWithDemos = 0;
	const char *pBaseFolder = DemoBrowserBaseFolder();
	for(int StorageType = IStorage::TYPE_SAVE; StorageType < Storage()->NumPaths(); ++StorageType)
	{
		if(Storage()->FolderExists(pBaseFolder, StorageType))
		{
			NumStoragesWithDemos++;
		}
	}
	m_DemolistMultipleStorages = NumStoragesWithDemos > 1;

	if(m_aCurrentDemoFolder[0] == '\0')
	{
		{
			CDemoItem Item;
			str_copy(Item.m_aFilename, pBaseFolder);
			str_copy(Item.m_aName, Localize("All combined"));
			Item.m_InfosLoaded = false;
			Item.m_Valid = false;
			Item.m_Date = 0;
			Item.m_DateLoaded = true;
			Item.m_DateValid = false;
			Item.m_Size = 0;
			Item.m_SizeLoaded = true;
			Item.m_IsDir = true;
			Item.m_IsLink = true;
			Item.m_StorageType = IStorage::TYPE_ALL;
			m_vDemos.push_back(Item);
		}

		for(int StorageType = IStorage::TYPE_SAVE; StorageType < Storage()->NumPaths(); ++StorageType)
		{
			if(Storage()->FolderExists(pBaseFolder, StorageType))
			{
				CDemoItem Item;
				str_copy(Item.m_aFilename, pBaseFolder);
				Storage()->GetCompletePath(StorageType, pBaseFolder, Item.m_aName, sizeof(Item.m_aName));
				str_append(Item.m_aName, "/", sizeof(Item.m_aName));
				Item.m_InfosLoaded = false;
				Item.m_Valid = false;
				Item.m_Date = 0;
				Item.m_DateLoaded = true;
				Item.m_DateValid = false;
				Item.m_Size = 0;
				Item.m_SizeLoaded = true;
				Item.m_IsDir = true;
				Item.m_IsLink = true;
				Item.m_StorageType = StorageType;
				m_vDemos.push_back(Item);
			}
		}
	}
	else
	{
		m_DemoPopulateStartTime = time_get_nanoseconds();
		Storage()->ListDirectory(m_DemolistStorageType, m_aCurrentDemoFolder, DemolistFetchCallback, this);

		// QmClient: 顶层追加 Rank 1 回放缓存入口（文件存放在 qmclient/rank1/demos，不在 demos/ 下建目录）
		if(str_comp(m_aCurrentDemoFolder, pBaseFolder) == 0 && Storage()->FolderExists(CRankGhost::DEMO_CACHE_DIR, IStorage::TYPE_SAVE))
		{
			CDemoItem Item;
			str_copy(Item.m_aFilename, CRankGhost::DEMO_CACHE_DIR);
			str_copy(Item.m_aName, Localize("Rank 1 replays"));
			Item.m_Date = 0;
			Item.m_DateLoaded = true;
			Item.m_DateValid = false;
			Item.m_Size = 0;
			Item.m_SizeLoaded = true;
			Item.m_InfosLoaded = false;
			Item.m_Valid = false;
			Item.m_IsDir = true;
			Item.m_IsLink = true;
			Item.m_StorageType = IStorage::TYPE_SAVE;
			m_vDemos.push_back(Item);
		}

		// Make sure there is a demo item to navigate back to the parent folder, if the folder contents could not be enumerated.
		if(m_vDemos.empty())
		{
			CDemoItem Item;
			str_copy(Item.m_aFilename, "..");
			str_copy(Item.m_aName, Localize("Parent Folder"));
			Item.m_Date = 0;
			Item.m_DateLoaded = true;
			Item.m_DateValid = false;
			Item.m_Size = 0;
			Item.m_SizeLoaded = true;
			Item.m_InfosLoaded = false;
			Item.m_Valid = false;
			Item.m_IsDir = true;
			Item.m_IsLink = false;
			Item.m_StorageType = m_DemolistStorageType;
			m_vDemos.push_back(Item);
		}

		std::stable_sort(m_vDemos.begin(), m_vDemos.end());
	}
	ResetDemoBrowserMetadataProgress();
	RefreshFilteredDemos();
	if(QmPerfEnabled())
	{
		char aPayload[256];
		const int MetadataRemaining = (m_DemoHeaderFetchComplete ? 0 : (int)m_vDemos.size()) + (m_DemoDateFetchComplete ? 0 : (int)m_vDemos.size());
		str_format(aPayload, sizeof(aPayload), "event=demo_browser_startup items_total=%d items_scanned=%d items_done=%d remaining=%d metadata_remaining=%d budget=%d dur_ms=%.3f trigger=populate source=%s sort=%d fetch_info=%d",
			(int)m_vDemos.size(), (int)m_vDemos.size(), (int)m_vDemos.size(), 0, MetadataRemaining, 0, StartupTimer.ElapsedMs(), DemoBrowserBrowsingScreenshots() ? "screenshots" : "demos", g_Config.m_BrDemoSort, g_Config.m_BrDemoFetchInfo);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
	}
}

void CMenus::RefreshFilteredDemos()
{
	m_vpFilteredDemos.clear();
	for(auto &Demo : m_vDemos)
	{
		if(str_find_nocase(Demo.m_aFilename, m_DemoSearchInput.GetString()) || str_find_nocase(Demo.m_aName, m_DemoSearchInput.GetString()))
		{
			m_vpFilteredDemos.push_back(&Demo);
		}
	}
}

CMenus::SDemoSelectionEntry CMenus::DemoSelectionEntryFromItem(const CDemoItem &Item) const
{
	SDemoSelectionEntry Entry{};
	str_copy(Entry.m_aFilename, Item.m_aFilename);
	Entry.m_StorageType = Item.m_StorageType;
	return Entry;
}

bool CMenus::IsDemoItemSelected(const CDemoItem &Item) const
{
	const SDemoSelectionEntry Entry = DemoSelectionEntryFromItem(Item);
	return std::find(m_vDemoSelection.begin(), m_vDemoSelection.end(), Entry) != m_vDemoSelection.end();
}

bool CMenus::IsDemoItemDeletable(const CDemoItem &Item) const
{
	return m_aCurrentDemoFolder[0] != '\0' && Item.m_StorageType == IStorage::TYPE_SAVE && str_comp(Item.m_aFilename, "..") != 0;
}

void CMenus::SetDemoSelectionSingle(int Index)
{
	m_vDemoSelection.clear();

	if(Index < 0 || Index >= (int)m_vpFilteredDemos.size())
	{
		m_DemolistSelectedIndex = -1;
		m_DemoSelectionAnchorIndex = -1;
		m_aCurrentDemoSelectionName[0] = '\0';
		return;
	}

	m_DemolistSelectedIndex = Index;
	m_vDemoSelection.push_back(DemoSelectionEntryFromItem(*m_vpFilteredDemos[Index]));
	m_DemoSelectionAnchorIndex = Index;
	str_copy(m_aCurrentDemoSelectionName, m_vpFilteredDemos[Index]->m_aName);
}

void CMenus::ToggleDemoSelection(int Index)
{
	if(Index < 0 || Index >= (int)m_vpFilteredDemos.size())
		return;

	const SDemoSelectionEntry Entry = DemoSelectionEntryFromItem(*m_vpFilteredDemos[Index]);
	auto It = std::find(m_vDemoSelection.begin(), m_vDemoSelection.end(), Entry);
	if(It != m_vDemoSelection.end())
		m_vDemoSelection.erase(It);
	else
		m_vDemoSelection.push_back(Entry);

	m_DemolistSelectedIndex = Index;
	m_DemoSelectionAnchorIndex = Index;

	if(m_vDemoSelection.empty())
	{
		m_DemolistSelectedIndex = -1;
		m_aCurrentDemoSelectionName[0] = '\0';
		return;
	}

	if(IsValidDemoIndex(m_DemolistSelectedIndex) && !IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]))
	{
		for(int DemoIndex = 0; DemoIndex < (int)m_vpFilteredDemos.size(); ++DemoIndex)
		{
			if(IsDemoItemSelected(*m_vpFilteredDemos[DemoIndex]))
			{
				m_DemolistSelectedIndex = DemoIndex;
				break;
			}
		}
	}

	if(IsValidDemoIndex(m_DemolistSelectedIndex))
		str_copy(m_aCurrentDemoSelectionName, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aName);
}

void CMenus::SelectDemoRange(int StartIndex, int EndIndex, bool Additive)
{
	if(m_vpFilteredDemos.empty())
		return;

	StartIndex = std::clamp(StartIndex, 0, (int)m_vpFilteredDemos.size() - 1);
	EndIndex = std::clamp(EndIndex, 0, (int)m_vpFilteredDemos.size() - 1);

	if(!Additive)
		m_vDemoSelection.clear();

	const int RangeStart = minimum(StartIndex, EndIndex);
	const int RangeEnd = maximum(StartIndex, EndIndex);
	for(int DemoIndex = RangeStart; DemoIndex <= RangeEnd; ++DemoIndex)
	{
		const SDemoSelectionEntry Entry = DemoSelectionEntryFromItem(*m_vpFilteredDemos[DemoIndex]);
		if(std::find(m_vDemoSelection.begin(), m_vDemoSelection.end(), Entry) == m_vDemoSelection.end())
			m_vDemoSelection.push_back(Entry);
	}

	m_DemolistSelectedIndex = EndIndex;
	str_copy(m_aCurrentDemoSelectionName, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aName);
}

void CMenus::SelectAllDemos()
{
	m_vDemoSelection.clear();

	for(const auto *pDemo : m_vpFilteredDemos)
		m_vDemoSelection.push_back(DemoSelectionEntryFromItem(*pDemo));

	if(m_vDemoSelection.empty())
	{
		m_DemolistSelectedIndex = -1;
		m_DemoSelectionAnchorIndex = -1;
		m_aCurrentDemoSelectionName[0] = '\0';
		return;
	}

	if(m_DemolistSelectedIndex < 0 || m_DemolistSelectedIndex >= (int)m_vpFilteredDemos.size())
		m_DemolistSelectedIndex = 0;

	str_copy(m_aCurrentDemoSelectionName, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aName);
	m_DemoSelectionAnchorIndex = m_DemolistSelectedIndex;
}

void CMenus::SyncDemoSelection()
{
	const auto &&SelectionEntryExistsInList = [&](const SDemoSelectionEntry &SelectionEntry) {
		for(const auto *pDemo : m_vpFilteredDemos)
		{
			if(pDemo->m_StorageType == SelectionEntry.m_StorageType && str_comp(pDemo->m_aFilename, SelectionEntry.m_aFilename) == 0)
				return true;
		}
		return false;
	};

	m_vDemoSelection.erase(std::remove_if(m_vDemoSelection.begin(), m_vDemoSelection.end(), [&](const SDemoSelectionEntry &SelectionEntry) {
		return !SelectionEntryExistsInList(SelectionEntry);
	}),
		m_vDemoSelection.end());

	if(m_vpFilteredDemos.empty())
	{
		m_vDemoSelection.clear();
		m_DemolistSelectedIndex = -1;
		m_DemoSelectionAnchorIndex = -1;
		m_aCurrentDemoSelectionName[0] = '\0';
		return;
	}

	const auto &&IsValidIndex = [&](int Index) { return Index >= 0 && Index < (int)m_vpFilteredDemos.size(); };

	if(m_vDemoSelection.empty())
	{
		if(!IsValidIndex(m_DemolistSelectedIndex))
			m_DemolistSelectedIndex = 0;
		if(IsValidIndex(m_DemolistSelectedIndex))
			m_vDemoSelection.push_back(DemoSelectionEntryFromItem(*m_vpFilteredDemos[m_DemolistSelectedIndex]));
	}
	else if(!IsValidIndex(m_DemolistSelectedIndex) || !IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]))
	{
		m_DemolistSelectedIndex = -1;
		for(int DemoIndex = 0; DemoIndex < (int)m_vpFilteredDemos.size(); ++DemoIndex)
		{
			if(IsDemoItemSelected(*m_vpFilteredDemos[DemoIndex]))
			{
				m_DemolistSelectedIndex = DemoIndex;
				break;
			}
		}
	}

	if(IsValidIndex(m_DemolistSelectedIndex))
		str_copy(m_aCurrentDemoSelectionName, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aName);
	else
		m_aCurrentDemoSelectionName[0] = '\0';

	if(!IsValidIndex(m_DemoSelectionAnchorIndex) ||
		!IsDemoItemSelected(*m_vpFilteredDemos[m_DemoSelectionAnchorIndex]))
	{
		m_DemoSelectionAnchorIndex = m_DemolistSelectedIndex;
	}
}

int CMenus::NumSelectedDemos() const
{
	int NumSelected = 0;
	for(const auto *pDemo : m_vpFilteredDemos)
	{
		if(IsDemoItemSelected(*pDemo))
			++NumSelected;
	}
	return NumSelected;
}

int CMenus::NumSelectedDeletableDemos() const
{
	int NumSelectedDeletable = 0;
	for(const auto *pDemo : m_vpFilteredDemos)
	{
		if(IsDemoItemSelected(*pDemo) && IsDemoItemDeletable(*pDemo))
			++NumSelectedDeletable;
	}
	return NumSelectedDeletable;
}

void CMenus::PrepareDemoDeleteTargetsFromSelection()
{
	m_vDemoDeleteTargets.clear();
	for(const auto *pDemo : m_vpFilteredDemos)
	{
		if(!IsDemoItemSelected(*pDemo) || !IsDemoItemDeletable(*pDemo))
			continue;

		SDemoDeleteTarget Target{};
		Target.m_Selection = DemoSelectionEntryFromItem(*pDemo);
		Target.m_IsDir = pDemo->m_IsDir;
		m_vDemoDeleteTargets.push_back(Target);
	}
}

void CMenus::ResetDemoScreenshotPreview()
{
	if(m_DemoScreenshotPreviewTexture.IsValid())
		Graphics()->UnloadTexture(&m_DemoScreenshotPreviewTexture);
	m_DemoScreenshotPreviewOpen = false;
	m_DemoScreenshotPreviewLoadFailed = false;
	m_aDemoScreenshotPreviewFolder[0] = '\0';
	m_DemoScreenshotPreviewSelection = {};
	m_DemoScreenshotPreviewWidth = 0;
	m_DemoScreenshotPreviewHeight = 0;
}

bool CMenus::IsDemoScreenshotPreviewItem(const CDemoItem &Item) const
{
	return m_DemoScreenshotPreviewOpen &&
	       str_comp(m_aDemoScreenshotPreviewFolder, m_aCurrentDemoFolder) == 0 &&
	       m_DemoScreenshotPreviewSelection.m_StorageType == Item.m_StorageType &&
	       str_comp(m_DemoScreenshotPreviewSelection.m_aFilename, Item.m_aFilename) == 0;
}

void CMenus::ToggleDemoScreenshotPreview(const CDemoItem &Item)
{
	if(!DemoBrowserBrowsingScreenshots() || Item.m_IsDir)
	{
		ResetDemoScreenshotPreview();
		return;
	}

	if(IsDemoScreenshotPreviewItem(Item))
	{
		ResetDemoScreenshotPreview();
		return;
	}

	ResetDemoScreenshotPreview();
	m_DemoScreenshotPreviewOpen = true;
	str_copy(m_aDemoScreenshotPreviewFolder, m_aCurrentDemoFolder);
	m_DemoScreenshotPreviewSelection = DemoSelectionEntryFromItem(Item);
}

void CMenus::SyncDemoScreenshotPreview()
{
	if(!m_DemoScreenshotPreviewOpen)
		return;
	if(!DemoBrowserBrowsingScreenshots() || str_comp(m_aDemoScreenshotPreviewFolder, m_aCurrentDemoFolder) != 0)
	{
		ResetDemoScreenshotPreview();
		return;
	}

	for(const CDemoItem *pItem : m_vpFilteredDemos)
	{
		if(IsDemoScreenshotPreviewItem(*pItem))
			return;
	}
	ResetDemoScreenshotPreview();
}

bool CMenus::LoadDemoScreenshotPreviewTexture(const CDemoItem &Item)
{
	if(m_DemoScreenshotPreviewTexture.IsValid())
		return true;
	if(m_DemoScreenshotPreviewLoadFailed)
		return false;

	CPerfTimer Timer;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);

	CImageInfo Image;
	bool Loaded = false;
	if(str_endswith_nocase(Item.m_aFilename, ".png") != nullptr)
	{
		int PngliteIncompatible = 0;
		Loaded = CImageLoader::LoadPng(Storage()->OpenFile(aPath, IOFLAG_READ, Item.m_StorageType), aPath, Image, PngliteIncompatible);
	}
	else if(str_endswith_nocase(Item.m_aFilename, ".webp") != nullptr)
	{
		Loaded = CImageLoader::LoadWebP(Storage()->OpenFile(aPath, IOFLAG_READ, Item.m_StorageType), aPath, Image);
	}

	if(!Loaded)
	{
		m_DemoScreenshotPreviewLoadFailed = true;
		if(QmPerfEnabled())
		{
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload), "event=demo_browser_preview_load items_total=1 items_scanned=1 items_done=0 remaining=0 budget=1 dur_ms=%.3f trigger=preview source=%s sort=%d fetch_info=%d",
				Timer.ElapsedMs(), DemoBrowserBrowsingScreenshots() ? "screenshots" : "demos", g_Config.m_BrDemoSort, g_Config.m_BrDemoFetchInfo);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
		}
		return false;
	}

	m_DemoScreenshotPreviewWidth = (int)Image.m_Width;
	m_DemoScreenshotPreviewHeight = (int)Image.m_Height;
	m_DemoScreenshotPreviewTexture = Graphics()->LoadTextureRawMove(Image, 0, aPath);
	if(!m_DemoScreenshotPreviewTexture.IsValid())
	{
		m_DemoScreenshotPreviewLoadFailed = true;
		m_DemoScreenshotPreviewWidth = 0;
		m_DemoScreenshotPreviewHeight = 0;
		if(QmPerfEnabled())
		{
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload), "event=demo_browser_preview_load items_total=1 items_scanned=1 items_done=0 remaining=0 budget=1 dur_ms=%.3f trigger=preview source=%s sort=%d fetch_info=%d",
				Timer.ElapsedMs(), DemoBrowserBrowsingScreenshots() ? "screenshots" : "demos", g_Config.m_BrDemoSort, g_Config.m_BrDemoFetchInfo);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
		}
		return false;
	}
	if(QmPerfEnabled())
	{
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=demo_browser_preview_load items_total=1 items_scanned=1 items_done=1 remaining=0 budget=1 dur_ms=%.3f trigger=preview source=%s sort=%d fetch_info=%d",
			Timer.ElapsedMs(), DemoBrowserBrowsingScreenshots() ? "screenshots" : "demos", g_Config.m_BrDemoSort, g_Config.m_BrDemoFetchInfo);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
	}
	return true;
}

void CMenus::RenderDemoScreenshotPreview(CUIRect PreviewRect, const CDemoItem &Item)
{
	PreviewRect.Margin(3.0f, &PreviewRect);
	PreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	PreviewRect.Margin(5.0f, &PreviewRect);

	if(!LoadDemoScreenshotPreviewTexture(Item) || m_DemoScreenshotPreviewWidth <= 0 || m_DemoScreenshotPreviewHeight <= 0)
	{
		Ui()->DoLabel(&PreviewRect, Localize("Could not preview this image"), 12.0f, TEXTALIGN_MC);
		return;
	}

	const float Scale = minimum(PreviewRect.w / m_DemoScreenshotPreviewWidth, PreviewRect.h / m_DemoScreenshotPreviewHeight);
	CUIRect ImageRect;
	ImageRect.w = m_DemoScreenshotPreviewWidth * Scale;
	ImageRect.h = m_DemoScreenshotPreviewHeight * Scale;
	ImageRect.x = PreviewRect.x + (PreviewRect.w - ImageRect.w) * 0.5f;
	ImageRect.y = PreviewRect.y + (PreviewRect.h - ImageRect.h) * 0.5f;

	Graphics()->TextureSet(m_DemoScreenshotPreviewTexture);
	Graphics()->WrapClamp();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem QuadItem(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->WrapNormal();
}

void CMenus::DemolistOnUpdate(bool Reset)
{
	if(Reset)
	{
		if(m_vpFilteredDemos.empty())
		{
			m_DemolistSelectedIndex = -1;
			m_aCurrentDemoSelectionName[0] = '\0';
		}
		else
		{
			m_DemolistSelectedIndex = 0;
			str_copy(m_aCurrentDemoSelectionName, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aName);
		}
		m_vDemoSelection.clear();
		m_DemoSelectionAnchorIndex = -1;
	}
	else
	{
		RefreshFilteredDemos();

		// search for selected index
		m_DemolistSelectedIndex = -1;
		int SelectedIndex = -1;
		for(const auto &pItem : m_vpFilteredDemos)
		{
			SelectedIndex++;
			if(str_comp(m_aCurrentDemoSelectionName, pItem->m_aName) == 0)
			{
				m_DemolistSelectedIndex = SelectedIndex;
				break;
			}
		}
	}

	SyncDemoSelection();
	SyncDemoScreenshotPreview();

	if(m_DemolistSelectedIndex >= 0)
		m_DemolistSelectedReveal = true;
}

bool CMenus::FetchHeader(CDemoItem &Item)
{
	if(!Item.IsDemoFile())
		return false;

	if(!Item.m_InfosLoaded)
	{
		char aBuffer[IO_MAX_PATH_LENGTH];
		str_format(aBuffer, sizeof(aBuffer), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
		IOHANDLE File;
		Item.m_Valid = DemoPlayer()->GetDemoInfo(Storage(), nullptr, aBuffer, Item.m_StorageType, &Item.m_Info, &Item.m_TimelineMarkers, &Item.m_MapInfo, &File);
		Item.m_InfosLoaded = true;

		if(Item.m_Valid && File)
		{
			Item.m_Size = io_length(File);
			Item.m_SizeLoaded = true;
			io_close(File);
		}
	}
	return Item.m_Valid;
}

void CMenus::FetchAllHeaders()
{
	m_DemoHeaderFetchCursor = 0;
	m_DemoHeaderFetchComplete = DemoBrowserBrowsingScreenshots();
	if(g_Config.m_BrDemoSort == SORT_DATE)
	{
		m_DemoDateFetchCursor = 0;
		m_DemoDateFetchComplete = false;
	}
	AdvanceDemoBrowserMetadata(2, g_Config.m_BrDemoSort == SORT_DATE ? 4 : 0, "fetch_info");
}

void CMenus::FinishRankDemoDownload(bool Success, const char *pMessage)
{
	if(m_aRankDemoManifestPath[0] != '\0')
		Storage()->RemoveFile(m_aRankDemoManifestPath, IStorage::TYPE_SAVE);
	if(m_aRankDemoTempPath[0] != '\0')
		Storage()->RemoveFile(m_aRankDemoTempPath, IStorage::TYPE_SAVE);
	m_pRankDemoManifestRequest = nullptr;
	m_pRankDemoRequest = nullptr;
	m_RankDemoDownloadStage = ERankDemoDownloadStage::IDLE;

	if(Success)
	{
		DemolistPopulate();
		DemolistOnUpdate(false);
	}
	PopupMessage(Localize("Rank 1 demo"), pMessage, Localize("Ok"));
}

void CMenus::StartRankDemoDownload(const char *pMapName)
{
	if(m_RankDemoDownloadStage != ERankDemoDownloadStage::IDLE || pMapName == nullptr || pMapName[0] == '\0')
		return;
	if(Http() == nullptr)
	{
		PopupMessage(Localize("Rank 1 demo"), Localize("HTTP is not available"), Localize("Ok"));
		return;
	}

	// QmClient：Rank 1 页缓存已有该图回放时不再重复下载，直接提示缓存位置
	char aCachedRankDemo[IO_MAX_PATH_LENGTH];
	if(GameClient()->m_RankGhost.FindCachedRankDemo(pMapName, 1, aCachedRankDemo, sizeof(aCachedRankDemo)))
	{
		char aMessage[IO_MAX_PATH_LENGTH + 128];
		str_format(aMessage, sizeof(aMessage), Localize("Rank 1 demo already cached under 'Rank 1 replays': %s"), aCachedRankDemo);
		FinishRankDemoDownload(true, aMessage);
		return;
	}

	Storage()->CreateFolder("demos", IStorage::TYPE_SAVE);
	m_RankDemoMap = pMapName;
	str_copy(m_aRankDemoManifestPath, "demos/.qm_rank_watchable.jsonl");
	str_copy(m_aRankDemoTempPath, "demos/.qm_rank1_download.demo.gz");
	m_aRankDemoDestinationPath[0] = '\0';

	m_pRankDemoManifestRequest = HttpGetFile(RANK_DEMO_MANIFEST_URL, Storage(), m_aRankDemoManifestPath, IStorage::TYPE_SAVE);
	m_pRankDemoManifestRequest->MaxResponseSize(RANK_DEMO_MAX_MANIFEST_BYTES);
	m_pRankDemoManifestRequest->Timeout(CTimeout{5000, 120000, 200, 10});
	m_pRankDemoManifestRequest->LogProgress(HTTPLOG::FAILURE);
	m_pRankDemoManifestRequest->FailOnErrorStatus(false);
	m_pRankDemoManifestRequest->SkipByFileTime(false);
	m_pRankDemoManifestRequest->HeaderString("User-Agent", "QmClient demo browser");
	Http()->Run(m_pRankDemoManifestRequest);
	m_RankDemoDownloadStage = ERankDemoDownloadStage::FETCH_MANIFEST;
}

void CMenus::UpdateRankDemoDownload()
{
	if(m_RankDemoDownloadStage == ERankDemoDownloadStage::IDLE)
		return;

	if(m_RankDemoDownloadStage == ERankDemoDownloadStage::FETCH_MANIFEST)
	{
		if(!m_pRankDemoManifestRequest || !m_pRankDemoManifestRequest->Done())
			return;
		if(m_pRankDemoManifestRequest->State() != EHttpState::DONE || m_pRankDemoManifestRequest->StatusCode() != 200)
		{
			FinishRankDemoDownload(false, Localize("Failed to fetch the replay list"));
			return;
		}

		void *pData = nullptr;
		unsigned DataSize = 0;
		const bool ReadOk = Storage()->ReadFile(m_aRankDemoManifestPath, IStorage::TYPE_SAVE, &pData, &DataSize);
		m_pRankDemoManifestRequest = nullptr;
		if(!ReadOk || pData == nullptr || DataSize == 0)
		{
			free(pData);
			FinishRankDemoDownload(false, Localize("The replay list is empty or malformed"));
			return;
		}

		std::string DemoName;
		int64_t DemoTs = 0;
		const bool Found = FindRankOneDemo(static_cast<const unsigned char *>(pData), DataSize, m_RankDemoMap.c_str(), DemoName, DemoTs);
		free(pData);
		Storage()->RemoveFile(m_aRankDemoManifestPath, IStorage::TYPE_SAVE);
		if(!Found)
		{
			FinishRankDemoDownload(false, Localize("No rank 1 replay was found for this map"));
			return;
		}

		char aSafeMap[128];
		char aSafeDemo[256];
		str_copy(aSafeMap, m_RankDemoMap.c_str(), sizeof(aSafeMap));
		str_sanitize_filename(aSafeMap);
		str_copy(aSafeDemo, DemoName.c_str(), sizeof(aSafeDemo));
		if(str_endswith_nocase(aSafeDemo, ".gz") != nullptr)
			aSafeDemo[str_length(aSafeDemo) - 3] = '\0';
		if(str_endswith_nocase(aSafeDemo, ".demo") != nullptr)
			aSafeDemo[str_length(aSafeDemo) - 5] = '\0';
		str_sanitize_filename(aSafeDemo);
		str_format(m_aRankDemoDestinationPath, sizeof(m_aRankDemoDestinationPath), "demos/%s_rank1_%lld_%s.demo", aSafeMap, (long long)DemoTs, aSafeDemo);
		if(Storage()->FileExists(m_aRankDemoDestinationPath, IStorage::TYPE_SAVE))
		{
			if(IsStoredDemoValid(Storage(), m_aRankDemoDestinationPath))
			{
				FinishRankDemoDownload(true, Localize("The rank 1 demo is already downloaded"));
				return;
			}
			Storage()->RemoveFile(m_aRankDemoDestinationPath, IStorage::TYPE_SAVE);
		}

		char aUrl[1024];
		str_format(aUrl, sizeof(aUrl), "%s/%s", RANK_DEMO_URL_PREFIX, DemoName.c_str());
		m_pRankDemoRequest = HttpGetFile(aUrl, Storage(), m_aRankDemoTempPath, IStorage::TYPE_SAVE);
		m_pRankDemoRequest->MaxResponseSize(RANK_DEMO_MAX_DOWNLOAD_BYTES);
		m_pRankDemoRequest->Timeout(CTimeout{5000, 120000, 200, 10});
		m_pRankDemoRequest->LogProgress(HTTPLOG::FAILURE);
		m_pRankDemoRequest->FailOnErrorStatus(false);
		m_pRankDemoRequest->SkipByFileTime(false);
		m_pRankDemoRequest->HeaderString("User-Agent", "QmClient demo browser");
		Http()->Run(m_pRankDemoRequest);
		m_RankDemoDownloadStage = ERankDemoDownloadStage::FETCH_DEMO;
		return;
	}

	if(!m_pRankDemoRequest || !m_pRankDemoRequest->Done())
		return;
	if(m_pRankDemoRequest->State() != EHttpState::DONE || m_pRankDemoRequest->StatusCode() != 200)
	{
		FinishRankDemoDownload(false, Localize("Failed to download the rank 1 demo"));
		return;
	}
	if(!UnpackRankDemo(Storage(), m_aRankDemoTempPath, m_aRankDemoDestinationPath) || !IsStoredDemoValid(Storage(), m_aRankDemoDestinationPath))
	{
		Storage()->RemoveFile(m_aRankDemoDestinationPath, IStorage::TYPE_SAVE);
		FinishRankDemoDownload(false, Localize("The downloaded file is not a valid demo"));
		return;
	}

	char aMessage[IO_MAX_PATH_LENGTH + 64];
	str_format(aMessage, sizeof(aMessage), Localize("Rank 1 demo downloaded to '%s'"), m_aRankDemoDestinationPath);
	FinishRankDemoDownload(true, aMessage);
}

void CMenus::RenderDemoBrowser(CUIRect MainView)
{
	UpdateRankDemoDownload();
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_DEMOS);
	const bool UseNewUi = g_Config.m_QmNewUi != 0;

	CUIRect ListView, DetailsView, ButtonsView;
	if(UseNewUi)
	{
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
		MainView.HSplitBottom(44.0f, &ListView, &ButtonsView);
		ListView.HSplitBottom(10.0f, &ListView, nullptr);
		ListView.VSplitRight(205.0f, &ListView, &DetailsView);
		ListView.VSplitRight(10.0f, &ListView, nullptr);
		ListView.Draw(MenuPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		DetailsView.Draw(MenuPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		ButtonsView.Draw(MenuPanelElevatedColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		ListView.Margin(10.0f, &ListView);
		DetailsView.Margin(10.0f, &DetailsView);
		ButtonsView.Margin(10.0f, &ButtonsView);
	}
	else
	{
		MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
		MainView.Margin(10.0f, &MainView);
		MainView.HSplitBottom(22.0f * 2.0f + 5.0f, &ListView, &ButtonsView);
		ListView.VSplitRight(205.0f, &ListView, &DetailsView);
		ListView.VSplitRight(5.0f, &ListView, nullptr);
	}

	bool WasListboxItemActivated;
	RenderDemoBrowserList(ListView, WasListboxItemActivated);
	RenderDemoBrowserDetails(DetailsView);
	RenderDemoBrowserButtons(ButtonsView, WasListboxItemActivated);
}

void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)
{
	bool BrowsingScreenshots = DemoBrowserBrowsingScreenshots();
	const bool UseNewUi = g_Config.m_QmNewUi != 0;

	if(!m_DemoBrowserListInitialized)
	{
		DemolistPopulate();
		DemolistOnUpdate(true);
		m_DemoBrowserListInitialized = true;
	}

#if defined(CONF_VIDEORECORDER)
	if(!BrowsingScreenshots && !m_DemoRenderInput.IsEmpty())
	{
		if(DemoPlayer()->ErrorMessage()[0] == '\0')
		{
			m_Popup = POPUP_RENDER_DONE;
		}
		else
		{
			m_DemoRenderInput.Clear();
		}
	}
#endif

	class CColumn
	{
	public:
		int m_Id;
		int m_Sort;
		const char *m_pCaption;
		int m_Direction;
		bool m_FontIcon;
		float m_Width;
		CUIRect m_Rect;
		const char *m_pTooltip;
	};

	enum
	{
		COL_ICON = 0,
		COL_DEMONAME,
		COL_MARKERS,
		COL_LENGTH,
		COL_DATE,
	};

	static CListBox s_ListBox;
	const float HeaderHeight = UseNewUi ? ms_ListheaderHeight + 8.0f : ms_ListheaderHeight;
	const float HeaderOuterPadding = 2.0f;
	const float HeaderInnerPadding = 2.0f;
	const float HeaderGap = UseNewUi ? 4.0f : 2.0f;
	const float RowHeight = UseNewUi ? ms_ListheaderHeight + 1.0f : ms_ListheaderHeight;
	CColumn aCols[] = {
		{-1, -1, "", -1, false, HeaderGap, {0}, nullptr},
		{COL_ICON, -1, "", -1, false, HeaderHeight - HeaderOuterPadding * 2.0f, {0}, nullptr},
		{-1, -1, "", -1, false, HeaderGap, {0}, nullptr},
		{COL_DEMONAME, SORT_DEMONAME, Localizable("Demo"), 0, false, 0.0f, {0}, nullptr},
		{-1, -1, "", 1, false, HeaderGap, {0}, nullptr},
		{COL_MARKERS, SORT_MARKERS, FONT_ICON_BOOKMARK, 1, true, UseNewUi ? 34.0f : 30.0f, {0}, Localizable("Markers")},
		{-1, -1, "", 1, false, HeaderGap, {0}, nullptr},
		{COL_LENGTH, SORT_LENGTH, Localizable("Length"), 1, false, UseNewUi ? 84.0f : 75.0f, {0}, nullptr},
		{-1, -1, "", 1, false, HeaderGap, {0}, nullptr},
		{COL_DATE, SORT_DATE, Localizable("Date"), 1, false, UseNewUi ? 156.0f : 150.0f, {0}, nullptr},
		{-1, -1, "", 1, false, s_ListBox.ScrollbarWidthMax() + HeaderGap, {0}, nullptr},
	};
	aCols[3].m_pCaption = DemoBrowserListColumnLabel(BrowsingScreenshots);
	aCols[4].m_Width = BrowsingScreenshots ? 0.0f : HeaderGap;
	aCols[5].m_Width = BrowsingScreenshots ? 0.0f : (UseNewUi ? 34.0f : 30.0f);
	aCols[6].m_Width = BrowsingScreenshots ? 0.0f : HeaderGap;
	aCols[7].m_Width = BrowsingScreenshots ? 0.0f : (UseNewUi ? 84.0f : 75.0f);
	aCols[8].m_Width = BrowsingScreenshots ? 0.0f : HeaderGap;
	aCols[9].m_Width = BrowsingScreenshots ? (UseNewUi ? 176.0f : 170.0f) : (UseNewUi ? 156.0f : 150.0f);

	CUIRect HeaderArea, Headers, ListBox;
	if(UseNewUi)
	{
		ListView.HSplitTop(HeaderHeight, &HeaderArea, &ListBox);
		HeaderArea.Draw(MenuPanelElevatedColor(), IGraphics::CORNER_T, ui_token::radius::BASE);
		HeaderArea.Margin(HeaderOuterPadding, &Headers);
		Headers.VMargin(HeaderInnerPadding, &Headers);
		ListBox.Draw(MenuPanelColor(0.72f), IGraphics::CORNER_B, ui_token::radius::BASE);
	}
	else
	{
		ListView.HSplitTop(ms_ListheaderHeight, &Headers, &ListBox);
		Headers.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_T, 5.0f);
		ListBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);
	}

	for(auto &Col : aCols)
	{
		if(Col.m_Direction == -1)
		{
			Headers.VSplitLeft(Col.m_Width, &Col.m_Rect, &Headers);
		}
	}

	for(int i = std::size(aCols) - 1; i >= 0; i--)
	{
		if(aCols[i].m_Direction == 1)
		{
			Headers.VSplitRight(aCols[i].m_Width, &Headers, &aCols[i].m_Rect);
		}
	}

	for(auto &Col : aCols)
	{
		if(Col.m_Direction == 0)
			Col.m_Rect = Headers;
	}

	for(auto &Col : aCols)
	{
		if(Col.m_pCaption[0] != '\0' && Col.m_Sort != -1)
		{
			if(Col.m_Id == COL_DEMONAME)
			{
				static CUi::SDropDownState s_DemoSourceDropDownState;
				static CScrollRegion s_DemoSourceDropDownScrollRegion;
				s_DemoSourceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DemoSourceDropDownScrollRegion;
				const char *apBrowserSources[NUM_DEMO_BROWSER_SOURCES] = {
					Localize("Replay"),
					Localize("Screenshots"),
				};
				CUIRect DropDownRect = Col.m_Rect;
				DropDownRect.HMargin(1.0f, &DropDownRect);
				const int NewSource = Ui()->DoDropDown(&DropDownRect, m_DemoBrowserSource, apBrowserSources, NUM_DEMO_BROWSER_SOURCES, s_DemoSourceDropDownState);
				if(NewSource != m_DemoBrowserSource && NewSource >= 0 && NewSource < NUM_DEMO_BROWSER_SOURCES)
				{
					m_DemoBrowserSource = (EDemoBrowserSource)NewSource;
					if(DemoBrowserBrowsingScreenshots() && (g_Config.m_BrDemoSort == SORT_MARKERS || g_Config.m_BrDemoSort == SORT_LENGTH))
						g_Config.m_BrDemoSort = SORT_DATE;
					ResetDemoBrowserFolder();
					m_DemoSearchInput.Clear();
					DemolistPopulate();
					DemolistOnUpdate(true);
					WasListboxItemActivated = false;
					return;
				}
				GameClient()->m_Tooltips.DoToolTip(&s_DemoSourceDropDownState.m_ButtonContainer, &DropDownRect, Localize("Choose whether to browse replays or screenshots"));
				continue;
			}
			if(BrowsingScreenshots && (Col.m_Id == COL_MARKERS || Col.m_Id == COL_LENGTH))
				continue;
			if(Col.m_FontIcon)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			}
			CUIRect HeaderButton = Col.m_Rect;
			HeaderButton.HMargin(1.0f, &HeaderButton);
			const int HeaderAlign = (Col.m_FontIcon || Col.m_Id == COL_LENGTH || Col.m_Id == COL_DATE) ? TEXTALIGN_MC : TEXTALIGN_ML;
			const int ButtonPressed = DoButton_GridHeader(&Col.m_Id, Col.m_FontIcon ? Col.m_pCaption : Localize(Col.m_pCaption), g_Config.m_BrDemoSort == Col.m_Sort, &HeaderButton, HeaderAlign);
			if(Col.m_pTooltip != nullptr)
			{
				GameClient()->m_Tooltips.DoToolTip(&Col.m_Id, &HeaderButton, Localize(Col.m_pTooltip));
			}
			if(Col.m_FontIcon)
			{
				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
			if(ButtonPressed)
			{
				if(g_Config.m_BrDemoSort == Col.m_Sort)
					g_Config.m_BrDemoSortOrder ^= 1;
				else
					g_Config.m_BrDemoSortOrder = 0;
				g_Config.m_BrDemoSort = Col.m_Sort;
				// Don't rescan in order to keep fetched headers, just resort
				if(g_Config.m_BrDemoSort == SORT_DATE)
				{
					m_DemoDateFetchCursor = 0;
					m_DemoDateFetchComplete = false;
				}
				std::stable_sort(m_vDemos.begin(), m_vDemos.end());
				DemolistOnUpdate(false);
			}
		}
	}

	if(m_DemolistSelectedReveal)
	{
		s_ListBox.ScrollToSelected();
		m_DemolistSelectedReveal = false;
	}

	s_ListBox.DoAutoSpacing(1.0f);
	s_ListBox.DoStart(UseNewUi ? RowHeight : ms_ListheaderHeight, m_vpFilteredDemos.size(), 1, 3, m_DemolistSelectedIndex, &ListBox, false, IGraphics::CORNER_ALL);

	char aBuf[64];
	int ItemIndex = -1;
	int VisibleRows = 0;
	int FirstVisibleIndex = -1;
	int EndVisibleIndex = -1;
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto ListFrameStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	for(auto &pItem : m_vpFilteredDemos)
	{
		ItemIndex++;

		const bool Focused = ItemIndex == m_DemolistSelectedIndex;
		const bool Selected = IsDemoItemSelected(*pItem);
		const CListboxItem ListItem = s_ListBox.DoNextItem(pItem, Focused);

		if(ListItem.m_Visible)
		{
			auto GaussianBlurSuppression = ListItem.SuppressGaussianBlur();
			VisibleRows++;
			if(FirstVisibleIndex < 0)
				FirstVisibleIndex = ItemIndex;
			EndVisibleIndex = ItemIndex + 1;
			if(Selected && !Focused)
				ListItem.m_Rect.Draw(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(1.35f), IGraphics::CORNER_ALL, ui_token::radius::BASE);

			for(const auto &Col : aCols)
			{
				CUIRect Button;
				Button.x = Col.m_Rect.x;
				Button.y = ListItem.m_Rect.y;
				Button.h = ListItem.m_Rect.h;
				Button.w = Col.m_Rect.w;

				if(Col.m_Id == COL_ICON)
				{
					Button.Margin(1.0f, &Button);

					EQmIcon IconType;
					const char *pIconType;
					if(pItem->m_IsLink || str_comp(pItem->m_aFilename, "..") == 0)
					{
						IconType = EQmIcon::FOLDER_TREE;
						pIconType = FONT_ICON_FOLDER_TREE;
					}
					else if(pItem->m_IsDir)
					{
						IconType = EQmIcon::FOLDER;
						pIconType = FONT_ICON_FOLDER;
					}
					else if(BrowsingScreenshots)
					{
						IconType = EQmIcon::IMAGE;
						pIconType = FONT_ICON_IMAGE;
					}
					else
					{
						IconType = EQmIcon::FILM;
						pIconType = FONT_ICON_FILM;
					}

					ColorRGBA IconColor;
					if(!pItem->m_IsDir && pItem->IsDemoFile() && (!pItem->m_InfosLoaded || !pItem->m_Valid))
						IconColor = ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f); // not loaded
					else
						IconColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

					TextRender()->TextColor(IconColor);
					Ui()->DoLabel_QmIcon(&Button, IconType, pIconType, 12.0f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				else if(Col.m_Id == COL_DEMONAME)
				{
					SLabelProperties Props;
					Props.m_MaxWidth = Button.w;
					Props.m_EllipsisAtEnd = true;
					Props.m_EnableWidthCheck = false;
					Ui()->DoLabel(&Button, pItem->m_aName, 12.0f, TEXTALIGN_ML, Props);
				}
				else if(Col.m_Id == COL_MARKERS && pItem->IsDemoFile() && pItem->m_Valid)
				{
					str_format(aBuf, sizeof(aBuf), "%d", pItem->NumMarkers());
					Button.VMargin(4.0f, &Button);
					Ui()->DoLabel(&Button, aBuf, 12.0f, TEXTALIGN_MR);
				}
				else if(Col.m_Id == COL_LENGTH)
				{
					if(pItem->IsDemoFile() && pItem->m_Valid)
						str_time((int64_t)pItem->Length() * 100, TIME_HOURS, aBuf, sizeof(aBuf));
					else if(!pItem->m_IsDir)
						str_copy(aBuf, "-");
					else
						continue;
					Button.VMargin(4.0f, &Button);
					Ui()->DoLabel(&Button, aBuf, 12.0f, TEXTALIGN_MR);
				}
				else if(Col.m_Id == COL_DATE && !pItem->m_IsDir)
				{
					if(pItem->m_DateLoaded && pItem->m_DateValid)
						str_timestamp_ex(pItem->m_Date, aBuf, sizeof(aBuf), FORMAT_SPACE);
					else
						str_copy(aBuf, "-");
					Button.VMargin(4.0f, &Button);
					Ui()->DoLabel(&Button, aBuf, 12.0f, TEXTALIGN_MR);
				}
			}
		}

		if(BrowsingScreenshots && IsDemoScreenshotPreviewItem(*pItem))
		{
			const float PreviewHeight = minimum(240.0f, maximum(120.0f, ListBox.w * 0.36f));
			const CListboxItem PreviewItem = s_ListBox.DoCustomRow(PreviewHeight, Focused);
			if(PreviewItem.m_Visible)
				RenderDemoScreenshotPreview(PreviewItem.m_Rect, *pItem);
		}
	}

	// QmClient：Rank 1 缓存目录里没有任何回放时给出引导，避免空列表只有上级目录项让人困惑
	{
		const size_t RankCacheDirLen = str_length(CRankGhost::DEMO_CACHE_DIR);
		const bool InRankCacheFolder = str_startswith(m_aCurrentDemoFolder, CRankGhost::DEMO_CACHE_DIR) &&
					       (m_aCurrentDemoFolder[RankCacheDirLen] == '\0' || m_aCurrentDemoFolder[RankCacheDirLen] == '/');
		const bool AnyDemoFile = std::any_of(m_vpFilteredDemos.begin(), m_vpFilteredDemos.end(), [](const CDemoItem *pItem) { return !pItem->m_IsDir; });
		if(InRankCacheFolder && !AnyDemoFile)
		{
			const CListboxItem HintItem = s_ListBox.DoCustomRow(UseNewUi ? RowHeight : ms_ListheaderHeight, false);
			if(HintItem.m_Visible)
			{
				TextRender()->TextColor(0.55f, 0.55f, 0.55f, 1.0f);
				Ui()->DoLabel(&HintItem.m_Rect, Localize("No Rank 1 replays cached yet. Search and download from the Rank 1 page."), 12.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
	}

	const int OldSelected = m_DemolistSelectedIndex;
	const bool WasItemSelected = s_ListBox.WasItemSelected();
	const int NewSelected = s_ListBox.DoEnd();
	const bool ListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
	const bool PlainItemClick = WasItemSelected && !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed();
	if(WasItemSelected && NewSelected >= 0)
	{
		if(Input()->ShiftIsPressed())
		{
			const int Anchor = m_DemoSelectionAnchorIndex >= 0 ? m_DemoSelectionAnchorIndex : (OldSelected >= 0 ? OldSelected : NewSelected);
			SelectDemoRange(Anchor, NewSelected, Input()->ModifierIsPressed());
			if(m_DemoSelectionAnchorIndex < 0)
				m_DemoSelectionAnchorIndex = Anchor;
		}
		else if(Input()->ModifierIsPressed())
		{
			ToggleDemoSelection(NewSelected);
		}
		else
		{
			SetDemoSelectionSingle(NewSelected);
		}
		if(PlainItemClick && BrowsingScreenshots && IsValidDemoIndex(NewSelected))
			ToggleDemoScreenshotPreview(*m_vpFilteredDemos[NewSelected]);
	}
	else if(NewSelected != OldSelected)
	{
		SetDemoSelectionSingle(NewSelected);
	}

	WasListboxItemActivated = s_ListBox.WasItemActivated() && NumSelectedDemos() == 1;
	static int s_DemoLastFirstVisibleIndex = -1;
	static int s_DemoLastEndVisibleIndex = -1;
	const bool DemoListScrollActive = s_ListBox.ScrollbarActive() || s_ListBox.ScrollbarAnimating();
	const int DemoJumpThreshold = maximum(1, VisibleRows * 2);
	const bool DemoListJumpScrollActive =
		FirstVisibleIndex >= 0 && EndVisibleIndex > FirstVisibleIndex &&
		s_DemoLastFirstVisibleIndex >= 0 &&
		(abs(FirstVisibleIndex - s_DemoLastFirstVisibleIndex) >= DemoJumpThreshold ||
			abs((EndVisibleIndex - 1) - (s_DemoLastEndVisibleIndex - 1)) >= DemoJumpThreshold);
	SSettingsAdaptiveBudgetInput AdaptiveBudgetInput;
	AdaptiveBudgetInput.m_FrameId = Client()->PerfFrame();
	str_copy(AdaptiveBudgetInput.m_aOperation, SettingsPerfActiveOperation(), sizeof(AdaptiveBudgetInput.m_aOperation));
	str_copy(AdaptiveBudgetInput.m_aPage, "demo_browser", sizeof(AdaptiveBudgetInput.m_aPage));
	str_copy(AdaptiveBudgetInput.m_aTab, "list", sizeof(AdaptiveBudgetInput.m_aTab));
	str_copy(AdaptiveBudgetInput.m_aContext, SettingsPerfContextName(), sizeof(AdaptiveBudgetInput.m_aContext));
	AdaptiveBudgetInput.m_FrameMsAverage = (float)GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_FrameTimeMs;
	AdaptiveBudgetInput.m_FrameMsP95 = AdaptiveBudgetInput.m_FrameMsAverage;
	AdaptiveBudgetInput.m_TargetFrameMs = 8.333f;
	AdaptiveBudgetInput.m_ScrollActive = DemoListScrollActive;
	AdaptiveBudgetInput.m_JumpScrollActive = DemoListJumpScrollActive;
	AdaptiveBudgetInput.m_VisibleWaiting = maximum(0, VisibleRows);
	AdaptiveBudgetInput.m_BackgroundBacklog =
		(m_DemoHeaderFetchComplete ? 0 : maximum(0, (int)m_vDemos.size() - (int)m_DemoHeaderFetchCursor)) +
		(m_DemoDateFetchComplete ? 0 : maximum(0, (int)m_vDemos.size() - (int)m_DemoDateFetchCursor));
	AdaptiveBudgetInput.m_WindowActive = true;
	const SSettingsAdaptiveBudgetOutput AdaptiveBudget = BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::DemoBrowser, "demo_browser", AdaptiveBudgetInput);
	const bool MetadataBackgroundAllowed = AdaptiveBudget.m_BackgroundTokens > 0 && !AdaptiveBudgetInput.m_ScrollActive && !AdaptiveBudgetInput.m_JumpScrollActive;
	const int DemoHeaderBudget = g_Config.m_BrDemoFetchInfo && !BrowsingScreenshots ?
					     (MetadataBackgroundAllowed ? maximum(1, AdaptiveBudget.m_DemoMetadataTokens / 2) : minimum(AdaptiveBudget.m_DemoMetadataTokens, maximum(0, EndVisibleIndex - FirstVisibleIndex))) :
					     0;
	const int DemoDateBudget = g_Config.m_BrDemoSort == SORT_DATE ?
					   (MetadataBackgroundAllowed ? maximum(1, AdaptiveBudget.m_DemoMetadataTokens) : minimum(AdaptiveBudget.m_DemoMetadataTokens, maximum(0, EndVisibleIndex - FirstVisibleIndex))) :
					   0;
	m_DemoBrowserMetadataBackgroundAllowed = MetadataBackgroundAllowed;
	AdvanceDemoBrowserMetadata(
		DemoHeaderBudget,
		DemoDateBudget,
		"list_frame",
		FirstVisibleIndex,
		EndVisibleIndex);
	m_DemoBrowserMetadataBackgroundAllowed = true;
	s_DemoLastFirstVisibleIndex = FirstVisibleIndex;
	s_DemoLastEndVisibleIndex = EndVisibleIndex;
	const double ListFrameDurationMs = MenuUiPerfEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - ListFrameStartTime).count() : -1.0;
	if(QmPerfEnabled() && ListFrameDurationMs >= QmPerfThresholdMs())
	{
		char aPayload[160];
		str_format(aPayload, sizeof(aPayload), "event=list_frame page=demo_browser items_total=%d rows_visible=%d rows_processed=%d rows_skipped=%d dur_ms=%.3f",
			(int)m_vpFilteredDemos.size(), VisibleRows, VisibleRows, (int)m_vpFilteredDemos.size() - VisibleRows, ListFrameDurationMs);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "demo_browser");
	}
	if(ListScrollActive)
	{
		StartSettingsPerfScrollWindow("demo_browser_scroll", SettingsPerfContextName(), "demo_browser", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "demo_browser";
		MenuUiPerf.m_pOperation = "demo_browser_scroll";
		MenuUiPerf.m_ItemsTotal = (int)m_vpFilteredDemos.size();
		MenuUiPerf.m_ItemsVisible = VisibleRows;
		MenuUiPerf.m_ItemsProcessed = VisibleRows;
		MenuUiPerf.m_ItemsSkipped = maximum(0, (int)m_vpFilteredDemos.size() - VisibleRows);
		MenuUiPerf.m_UiMs = (float)ListFrameDurationMs;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}
}

void CMenus::RenderDemoBrowserDetails(CUIRect DetailsView)
{
	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	CUIRect Contents, Header;
	DetailsView.HSplitTop(ms_ListheaderHeight, &Header, &Contents);
	if(UseNewUi)
	{
		Contents.Draw(MenuPanelColor(0.72f), IGraphics::CORNER_B, ui_token::radius::BASE);
		Contents.Margin(10.0f, &Contents);
	}
	else
	{
		Contents.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);
		Contents.Margin(5.0f, &Contents);
	}

	const float FontSize = 12.0f;
	const int NumSelected = NumSelectedDemos();
	CDemoItem *pItem = nullptr;
	if(NumSelected == 1 &&
		m_DemolistSelectedIndex >= 0 &&
		m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size() &&
		IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]))
	{
		pItem = m_vpFilteredDemos[m_DemolistSelectedIndex];
	}

	if(UseNewUi)
		Header.Draw(MenuPanelElevatedColor(), IGraphics::CORNER_T, ui_token::radius::BASE);
	else
		Header.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_T, 5.0f);
	const char *pHeaderLabel;
	if(NumSelected == 0)
		pHeaderLabel = DemoBrowserBrowsingScreenshots() ? Localize("No screenshot selected") : Localize("No demo selected");
	else if(NumSelected > 1)
		pHeaderLabel = Localize("Selection");
	else if(pItem == nullptr)
		pHeaderLabel = DemoBrowserBrowsingScreenshots() ? Localize("No screenshot selected") : Localize("No demo selected");
	else if(str_comp(pItem->m_aFilename, "..") == 0)
		pHeaderLabel = Localize("Parent Folder");
	else if(pItem->m_IsLink)
		pHeaderLabel = Localize("Folder Link");
	else if(pItem->m_IsDir)
		pHeaderLabel = Localize("Folder");
	else if(pItem->IsDemoFile() && !pItem->m_InfosLoaded)
		pHeaderLabel = Localize("Loading Demo");
	else if(pItem->IsDemoFile() && !pItem->m_Valid)
		pHeaderLabel = Localize("Invalid Demo");
	else if(DemoBrowserBrowsingScreenshots())
		pHeaderLabel = Localize("Screenshot");
	else
		pHeaderLabel = Localize("Demo");
	Ui()->DoLabel(&Header, pHeaderLabel, FontSize + 2.0f, TEXTALIGN_MC);

	if(NumSelected > 1)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("%d items selected"), NumSelected);
		Ui()->DoLabel(&Contents, aBuf, FontSize + 1.0f, TEXTALIGN_ML);
		return;
	}

	if(pItem == nullptr || pItem->m_IsDir)
		return;

	char aBuf[256];
	CUIRect Left, Right;

	Contents.HSplitTop(18.0f, &Left, &Contents);
	Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
	Ui()->DoLabel(&Left, Localize("Created"), FontSize, TEXTALIGN_ML);
	Ui()->DoLabel(&Right, Localize("Size"), FontSize, TEXTALIGN_ML);
	if(pItem->m_DateLoaded && pItem->m_DateValid)
		str_timestamp_ex(pItem->m_Date, aBuf, sizeof(aBuf), FORMAT_SPACE);
	else
		str_copy(aBuf, "-");
	Contents.HSplitTop(18.0f, &Left, &Contents);
	Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
	Ui()->DoLabel(&Left, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	if(pItem->m_SizeLoaded)
		FormatBrowserFileSize(pItem->m_Size, aBuf, sizeof(aBuf));
	else
		str_copy(aBuf, "-");
	Ui()->DoLabel(&Right, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	Contents.HSplitTop(4.0f, nullptr, &Contents);

	if(!pItem->IsDemoFile())
		return;

	if(!pItem->m_InfosLoaded)
	{
		Contents.HSplitTop(18.0f, &Left, &Contents);
		Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
		Ui()->DoLabel(&Left, Localize("Loading demo info"), FontSize, TEXTALIGN_ML);
		Ui()->DoLabel(&Right, "-", FontSize, TEXTALIGN_ML);
		return;
	}

	if(!pItem->m_Valid)
		return;

	Contents.HSplitTop(18.0f, &Left, &Contents);
	Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
	Ui()->DoLabel(&Left, Localize("Type"), FontSize, TEXTALIGN_ML);
	Ui()->DoLabel(&Right, Localize("Version"), FontSize, TEXTALIGN_ML);
	Contents.HSplitTop(18.0f, &Left, &Contents);
	Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
	Ui()->DoLabel(&Left, pItem->m_Info.m_aType, FontSize - 1.0f, TEXTALIGN_ML);
	str_format(aBuf, sizeof(aBuf), "%d", pItem->m_Info.m_Version);
	Ui()->DoLabel(&Right, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	Contents.HSplitTop(4.0f, nullptr, &Contents);

	Contents.HSplitTop(18.0f, &Left, &Contents);
	Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
	Ui()->DoLabel(&Left, Localize("Length"), FontSize, TEXTALIGN_ML);
	Ui()->DoLabel(&Right, Localize("Markers"), FontSize, TEXTALIGN_ML);
	Contents.HSplitTop(18.0f, &Left, &Contents);
	Left.VSplitLeft(Contents.w / 2.f + 30.f, &Left, &Right);
	str_time((int64_t)pItem->Length() * 100, TIME_HOURS, aBuf, sizeof(aBuf));
	Ui()->DoLabel(&Left, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	str_format(aBuf, sizeof(aBuf), "%d", pItem->NumMarkers());
	Ui()->DoLabel(&Right, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	Contents.HSplitTop(4.0f, nullptr, &Contents);

	Contents.HSplitTop(18.0f, &Left, &Contents);
	Ui()->DoLabel(&Left, Localize("Netversion"), FontSize, TEXTALIGN_ML);
	Contents.HSplitTop(18.0f, &Left, &Contents);
	Ui()->DoLabel(&Left, pItem->m_Info.m_aNetversion, FontSize - 1.0f, TEXTALIGN_ML);
	Contents.HSplitTop(16.0f, nullptr, &Contents);

	Contents.HSplitTop(18.0f, &Left, &Contents);
	Ui()->DoLabel(&Left, Localize("Map"), FontSize, TEXTALIGN_ML);
	Contents.HSplitTop(18.0f, &Left, &Contents);
	Ui()->DoLabel(&Left, pItem->m_Info.m_aMapName, FontSize - 1.0f, TEXTALIGN_ML);
	Contents.HSplitTop(4.0f, nullptr, &Contents);

	Contents.HSplitTop(18.0f, &Left, &Contents);
	Ui()->DoLabel(&Left, Localize("Map size"), FontSize, TEXTALIGN_ML);
	Contents.HSplitTop(18.0f, &Left, &Contents);
	const float MapSize = pItem->MapSize() / 1024.0f;
	if(MapSize == 0.0f)
		str_copy(aBuf, Localize("map not included", "Demo details"));
	else if(MapSize > 1024)
		str_format(aBuf, sizeof(aBuf), Localize("%.2f MiB"), MapSize / 1024.0f);
	else
		str_format(aBuf, sizeof(aBuf), Localize("%.2f KiB"), MapSize);
	Ui()->DoLabel(&Left, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	Contents.HSplitTop(4.0f, nullptr, &Contents);

	Contents.HSplitTop(18.0f, &Left, &Contents);
	if(pItem->m_MapInfo.m_Sha256.has_value())
	{
		Ui()->DoLabel(&Left, "SHA256", FontSize, TEXTALIGN_ML);
		Contents.HSplitTop(18.0f, &Left, &Contents);
		char aSha[SHA256_MAXSTRSIZE];
		sha256_str(pItem->m_MapInfo.m_Sha256.value(), aSha, sizeof(aSha));
		SLabelProperties Props;
		Props.m_MaxWidth = Left.w;
		Props.m_EllipsisAtEnd = true;
		Props.m_EnableWidthCheck = false;
		Ui()->DoLabel(&Left, aSha, FontSize - 1.0f, TEXTALIGN_ML, Props);
	}
	else
	{
		Ui()->DoLabel(&Left, "CRC32", FontSize, TEXTALIGN_ML);
		Contents.HSplitTop(18.0f, &Left, &Contents);
		str_format(aBuf, sizeof(aBuf), "%08x", pItem->m_MapInfo.m_Crc);
		Ui()->DoLabel(&Left, aBuf, FontSize - 1.0f, TEXTALIGN_ML);
	}
	Contents.HSplitTop(4.0f, nullptr, &Contents);
}

void CMenus::RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated)
{
	const bool BrowsingScreenshots = DemoBrowserBrowsingScreenshots();
	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	const char *pBaseFolder = DemoBrowserBaseFolder();
	const IUiContext DemoBrowserSearchCtx = SettingsUiContext("demo_browser_search");

	const auto &&SetIconMode = [&](bool Enable) {
		if(Enable)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		}
		else
		{
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
	};

	if(UseNewUi)
	{
		ButtonsView.HSplitTop(3.0f, nullptr, &ButtonsView);
		ButtonsView.HSplitBottom(3.0f, &ButtonsView, nullptr);

		const float RowHeight = minimum(22.0f, ButtonsView.h);
		if(ButtonsView.h > RowHeight)
		{
			const float VerticalMargin = (ButtonsView.h - RowHeight) / 2.0f;
			ButtonsView.HMargin(VerticalMargin, &ButtonsView);
		}

		CUIRect MainRow = ButtonsView;
		const float ButtonWidth = MainRow.h * 1.55f;
		const float TightSpacing = 4.0f;
		const float GroupSpacing = 6.0f;

		bool HasSingleSelection =
			NumSelectedDemos() == 1 &&
			m_DemolistSelectedIndex >= 0 &&
			m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size() &&
			IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
		CDemoItem *pSelectedItem = HasSingleSelection ? m_vpFilteredDemos[m_DemolistSelectedIndex] : nullptr;
		if(!BrowsingScreenshots && pSelectedItem != nullptr && pSelectedItem->IsDemoFile() && !pSelectedItem->m_InfosLoaded)
			FetchHeader(*pSelectedItem);
		int NumSelectedDeletable = NumSelectedDeletableDemos();
		CUIRect LeftGroup = MainRow;
		CUIRect RightGroup;
		bool CanRenderDemo =
#if defined(CONF_VIDEORECORDER)
			!BrowsingScreenshots && HasSingleSelection && !pSelectedItem->m_IsDir && pSelectedItem->IsDemoFile();
#else
			false;
#endif
		const bool CanDownloadRankDemo = !BrowsingScreenshots && HasSingleSelection && pSelectedItem->IsDemoFile() && pSelectedItem->m_InfosLoaded && pSelectedItem->m_Valid && pSelectedItem->m_Info.m_aMapName[0] != '\0';
		int NumRightButtons = 0;
		if(NumSelectedDeletable > 0)
			NumRightButtons++;
		if(m_aCurrentDemoFolder[0] != '\0' && HasSingleSelection && IsDemoItemDeletable(*pSelectedItem))
			NumRightButtons++;
		if(HasSingleSelection)
			NumRightButtons++;
		if(CanRenderDemo)
			NumRightButtons++;
		if(CanDownloadRankDemo)
			NumRightButtons++;

		if(NumRightButtons > 0)
		{
			const float RightGroupWidth = NumRightButtons * ButtonWidth + (NumRightButtons - 1) * TightSpacing;
			MainRow.VSplitRight(RightGroupWidth, &LeftGroup, &RightGroup);
			if(LeftGroup.w > GroupSpacing)
				LeftGroup.VSplitRight(GroupSpacing, &LeftGroup, nullptr);
		}

		// quick search
		{
			CUIRect DemoSearch;
			const float SearchWidth = maximum(220.0f, minimum(LeftGroup.w * 0.48f, 360.0f));
			LeftGroup.VSplitLeft(minimum(SearchWidth, LeftGroup.w), &DemoSearch, &LeftGroup);
			if(LeftGroup.w > TightSpacing)
				LeftGroup.VSplitLeft(TightSpacing, nullptr, &LeftGroup);
			ui_widget::SInputFieldOptions NewUiSearchOptions;
			NewUiSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
			NewUiSearchOptions.m_Clearable = true;
			NewUiSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
			NewUiSearchOptions.m_FontSize = 13.0f;
			if(ui_widget::InputField(DemoBrowserSearchCtx, &m_DemoSearchInput, DemoSearch, NewUiSearchOptions).m_Changed)
			{
				RefreshFilteredDemos();
				DemolistOnUpdate(false);
			}
		}

		if(!Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive() && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_A))
		{
			SelectAllDemos();
		}

		// refresh button
		{
			CUIRect RefreshButton;
			LeftGroup.VSplitLeft(ButtonWidth, &RefreshButton, &LeftGroup);
			if(LeftGroup.w > TightSpacing)
				LeftGroup.VSplitLeft(TightSpacing, nullptr, &LeftGroup);
			SetIconMode(true);
			static CButtonContainer s_RefreshButton;
			if(DoButton_Menu_QmIcon(&s_RefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
			{
				SetIconMode(false);
				DemolistPopulate();
				DemolistOnUpdate(false);
			}
			SetIconMode(false);
		}

		// fetch info checkbox
		if(!BrowsingScreenshots)
		{
			CUIRect FetchInfo;
			const float FetchInfoWidth = minimum(maximum(LeftGroup.w * 0.26f, 104.0f), 140.0f);
			LeftGroup.VSplitLeft(minimum(FetchInfoWidth, LeftGroup.w), &FetchInfo, &LeftGroup);
			if(LeftGroup.w > TightSpacing)
				LeftGroup.VSplitLeft(TightSpacing, nullptr, &LeftGroup);
			if(DoButton_CheckBox(&g_Config.m_BrDemoFetchInfo, Localize("Fetch Info"), g_Config.m_BrDemoFetchInfo, &FetchInfo))
			{
				g_Config.m_BrDemoFetchInfo ^= 1;
				m_DemoHeaderFetchCursor = 0;
				m_DemoHeaderFetchComplete = DemoBrowserBrowsingScreenshots() || !g_Config.m_BrDemoFetchInfo;
			}
		}

		// demos directory button
		if(HasSingleSelection && pSelectedItem->m_StorageType != IStorage::TYPE_ALL)
		{
			CUIRect DemosDirectoryButton;
			const float DirectoryWidth = minimum(maximum(LeftGroup.w, 120.0f), 188.0f);
			LeftGroup.VSplitLeft(minimum(DirectoryWidth, LeftGroup.w), &DemosDirectoryButton, &LeftGroup);
			static CButtonContainer s_DemosDirectoryButton;
			if(DoButton_Menu(&s_DemosDirectoryButton, BrowsingScreenshots ? Localize("Screenshots directory") : Localize("Demos directory"), 0, &DemosDirectoryButton))
			{
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(pSelectedItem->m_StorageType, m_aCurrentDemoFolder[0] == '\0' ? pBaseFolder : m_aCurrentDemoFolder, aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_DemosDirectoryButton, &DemosDirectoryButton, BrowsingScreenshots ? Localize("Open the folder containing screenshots") : Localize("Open the folder containing demo files"));
		}

		// play/open button
		if(HasSingleSelection)
		{
			CUIRect PlayButton;
			RightGroup.VSplitRight(ButtonWidth, &RightGroup, &PlayButton);
			if(RightGroup.w > TightSpacing)
				RightGroup.VSplitRight(TightSpacing, &RightGroup, nullptr);
			SetIconMode(true);
			static CButtonContainer s_PlayButton;
			const EQmIcon OpenIcon = pSelectedItem->m_IsDir ? EQmIcon::FOLDER_OPEN : (BrowsingScreenshots ? EQmIcon::IMAGE : EQmIcon::PLAY);
			const char *pOpenIcon = pSelectedItem->m_IsDir ? FONT_ICON_FOLDER_OPEN : (BrowsingScreenshots ? FONT_ICON_IMAGE : FONT_ICON_PLAY);
			if(DoButton_Menu_QmIcon(&s_PlayButton, OpenIcon, pOpenIcon, 0, &PlayButton) || WasListboxItemActivated || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || (!BrowsingScreenshots && Input()->KeyPress(KEY_P) && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive()))
			{
				SetIconMode(false);
				if(pSelectedItem->m_IsDir) // folder
				{
					m_DemoSearchInput.Clear();
					const bool ParentFolder = str_comp(pSelectedItem->m_aFilename, "..") == 0;
					if(ParentFolder) // parent folder
					{
						// QmClient: Rank 1 缓存目录（含其子目录）是挂在顶层的链接入口，
						// .. 直接回到 demos 根目录，不逐级穿过 qmclient/ 等内部目录
						const size_t RankCacheDirLen = str_length(CRankGhost::DEMO_CACHE_DIR);
						const bool InRankCacheFolder = str_startswith(m_aCurrentDemoFolder, CRankGhost::DEMO_CACHE_DIR) &&
									       (m_aCurrentDemoFolder[RankCacheDirLen] == '\0' || m_aCurrentDemoFolder[RankCacheDirLen] == '/');
						if(InRankCacheFolder)
						{
							str_copy(m_aCurrentDemoFolder, pBaseFolder);
							m_DemolistStorageType = IStorage::TYPE_ALL;
							str_copy(m_aCurrentDemoSelectionName, Localize("Rank 1 replays"));
						}
						else
						{
							str_copy(m_aCurrentDemoSelectionName, fs_filename(m_aCurrentDemoFolder));
							str_append(m_aCurrentDemoSelectionName, "/");
							if(fs_parent_dir(m_aCurrentDemoFolder))
							{
								m_aCurrentDemoFolder[0] = '\0';
								if(m_DemolistStorageType == IStorage::TYPE_ALL)
								{
									m_aCurrentDemoSelectionName[0] = '\0'; // will select first list item
								}
								else
								{
									Storage()->GetCompletePath(m_DemolistStorageType, pBaseFolder, m_aCurrentDemoSelectionName, sizeof(m_aCurrentDemoSelectionName));
									str_append(m_aCurrentDemoSelectionName, "/");
								}
							}
						}
					}
					else // sub folder
					{
						// QmClient: 链接项携带完整存储路径（如 Rank 1 回放缓存目录），直接跳转
						if(str_find(pSelectedItem->m_aFilename, "/") != nullptr)
						{
							str_copy(m_aCurrentDemoFolder, pSelectedItem->m_aFilename);
							m_DemolistStorageType = pSelectedItem->m_StorageType;
						}
						else
						{
							if(m_aCurrentDemoFolder[0] != '\0')
								str_append(m_aCurrentDemoFolder, "/");
							else
								m_DemolistStorageType = pSelectedItem->m_StorageType;
							str_append(m_aCurrentDemoFolder, pSelectedItem->m_aFilename);
						}
					}
					DemolistPopulate();
					DemolistOnUpdate(!ParentFolder);
				}
				else // file
				{
					if(BrowsingScreenshots)
					{
						char aBuf[IO_MAX_PATH_LENGTH];
						str_format(aBuf, sizeof(aBuf), "%s/%s", m_aCurrentDemoFolder, pSelectedItem->m_aFilename);
						char aFullPath[IO_MAX_PATH_LENGTH];
						Storage()->GetCompletePath(pSelectedItem->m_StorageType, aBuf, aFullPath, sizeof(aFullPath));
						if(!Client()->ViewFile(aFullPath))
							PopupMessage(Localize("Error opening screenshot"), Localize("Unable to open the screenshot file"), Localize("Ok"));
					}
					else if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
					{
						PopupConfirm(Localize("Disconnect"), Localize("Are you sure that you want to disconnect and play this demo?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmPlayDemo);
					}
					else
					{
						CMenus::PopupConfirmPlayDemo();
					}
					return;
				}
			}
			SetIconMode(false);
		}

		HasSingleSelection =
			NumSelectedDemos() == 1 &&
			m_DemolistSelectedIndex >= 0 &&
			m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size() &&
			IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
		pSelectedItem = HasSingleSelection ? m_vpFilteredDemos[m_DemolistSelectedIndex] : nullptr;
		NumSelectedDeletable = NumSelectedDeletableDemos();
#if defined(CONF_VIDEORECORDER)
		CanRenderDemo = !BrowsingScreenshots && HasSingleSelection && pSelectedItem != nullptr && !pSelectedItem->m_IsDir && pSelectedItem->IsDemoFile();
#else
		CanRenderDemo = false;
#endif
		const bool CanDownloadRankDemoNow = !BrowsingScreenshots && HasSingleSelection && pSelectedItem != nullptr && pSelectedItem->IsDemoFile() && pSelectedItem->m_InfosLoaded && pSelectedItem->m_Valid && pSelectedItem->m_Info.m_aMapName[0] != '\0';
		if(CanDownloadRankDemoNow)
		{
			CUIRect DownloadButton;
			RightGroup.VSplitRight(ButtonWidth, &RightGroup, &DownloadButton);
			if(RightGroup.w > TightSpacing)
				RightGroup.VSplitRight(TightSpacing, &RightGroup, nullptr);
			SetIconMode(true);
			static CButtonContainer s_RankDemoDownloadButton;
			const bool DownloadActive = m_RankDemoDownloadStage != ERankDemoDownloadStage::IDLE;
			if(DoButton_Menu_QmIcon(&s_RankDemoDownloadButton, EQmIcon::FILE, FONT_ICON_FILE, DownloadActive ? -1 : 0, &DownloadButton))
				StartRankDemoDownload(pSelectedItem->m_Info.m_aMapName);
			GameClient()->m_Tooltips.DoToolTip(&s_RankDemoDownloadButton, &DownloadButton, DownloadActive ? Localize("Downloading rank 1 demo") : Localize("Download the rank 1 demo for this map"));
			SetIconMode(false);
		}

		if(m_aCurrentDemoFolder[0] != '\0')
		{
			if(HasSingleSelection && IsDemoItemDeletable(*pSelectedItem))
			{
				// rename button
				CUIRect RenameButton;
				RightGroup.VSplitRight(ButtonWidth, &RightGroup, &RenameButton);
				if(RightGroup.w > TightSpacing)
					RightGroup.VSplitRight(TightSpacing, &RightGroup, nullptr);
				SetIconMode(true);
				static CButtonContainer s_RenameButton;
				if(DoButton_Menu_QmIcon(&s_RenameButton, EQmIcon::PENCIL, FONT_ICON_PENCIL, 0, &RenameButton))
				{
					SetIconMode(false);
					m_Popup = POPUP_RENAME_DEMO;
					if(pSelectedItem->m_IsDir)
					{
						m_DemoRenameInput.Set(pSelectedItem->m_aFilename);
					}
					else
					{
						char aNameWithoutExt[IO_MAX_PATH_LENGTH];
						fs_split_file_extension(pSelectedItem->m_aFilename, aNameWithoutExt, sizeof(aNameWithoutExt));
						m_DemoRenameInput.Set(aNameWithoutExt);
					}
					Ui()->SetActiveItem(&m_DemoRenameInput);
					return;
				}
				SetIconMode(false);
			}

			if(NumSelectedDeletable > 0)
			{
				static CButtonContainer s_DeleteButton;
				CUIRect DeleteButton;
				RightGroup.VSplitRight(ButtonWidth, &RightGroup, &DeleteButton);
				if(RightGroup.w > TightSpacing)
					RightGroup.VSplitRight(TightSpacing, &RightGroup, nullptr);
				SetIconMode(true);
				if(DoButton_Menu_QmIcon(&s_DeleteButton, EQmIcon::TRASH, FONT_ICON_TRASH, 0, &DeleteButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_DELETE) || (Input()->KeyPress(KEY_D) && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive()))
				{
					SetIconMode(false);
					PrepareDemoDeleteTargetsFromSelection();
					if(m_vDemoDeleteTargets.empty())
						return;

					if(m_vDemoDeleteTargets.size() == 1)
					{
						char aBuf[128 + IO_MAX_PATH_LENGTH];
						str_format(aBuf, sizeof(aBuf), m_vDemoDeleteTargets[0].m_IsDir ? Localize("Are you sure that you want to delete the folder '%s'?") : (BrowsingScreenshots ? Localize("Are you sure that you want to delete the screenshot '%s'?") : Localize("Are you sure that you want to delete the demo '%s'?")), m_vDemoDeleteTargets[0].m_Selection.m_aFilename);
						PopupConfirm(m_vDemoDeleteTargets[0].m_IsDir ? Localize("Delete folder") : (BrowsingScreenshots ? Localize("Delete screenshot") : Localize("Delete demo")), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteSelectedDemos);
					}
					else
					{
						char aBuf[128];
						str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete %d selected items?"), (int)m_vDemoDeleteTargets.size());
						PopupConfirm(Localize("Delete selected items"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteSelectedDemos);
					}
					return;
				}
				SetIconMode(false);
			}

#if defined(CONF_VIDEORECORDER)
			// render demo button
			if(CanRenderDemo)
			{
				CUIRect RenderButton;
				RightGroup.VSplitRight(ButtonWidth, &RightGroup, &RenderButton);
				if(RightGroup.w > TightSpacing)
					RightGroup.VSplitRight(TightSpacing, &RightGroup, nullptr);
				SetIconMode(true);
				static CButtonContainer s_RenderButton;
				if(DoButton_Menu_QmIcon(&s_RenderButton, EQmIcon::VIDEO, FONT_ICON_VIDEO, 0, &RenderButton) || (Input()->KeyPress(KEY_R) && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive()))
				{
					SetIconMode(false);
					m_HasPendingDemoRenderSource = false;
					m_DemoExportDisplayExpanded = false;
					m_Popup = POPUP_RENDER_DEMO;
					m_StartPaused = false;
					char aNameWithoutExt[IO_MAX_PATH_LENGTH];
					fs_split_file_extension(pSelectedItem->m_aFilename, aNameWithoutExt, sizeof(aNameWithoutExt));
					m_DemoRenderInput.Set(aNameWithoutExt);
					Ui()->SetActiveItem(&m_DemoRenderInput);
					return;
				}
				SetIconMode(false);
			}
#endif
		}
		return;
	}

	CUIRect ButtonBarTop, ButtonBarBottom;
	ButtonsView.HSplitTop(5.0f, nullptr, &ButtonsView);
	ButtonsView.HSplitMid(&ButtonBarTop, &ButtonBarBottom, 5.0f);

	// quick search
	{
		CUIRect DemoSearch;
		ButtonBarTop.VSplitLeft(ButtonBarBottom.h * 21.0f, &DemoSearch, &ButtonBarTop);
		ButtonBarTop.VSplitLeft(ButtonBarTop.h / 2.0f, nullptr, &ButtonBarTop);
		ui_widget::SInputFieldOptions LegacySearchOptions;
		LegacySearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		LegacySearchOptions.m_Clearable = true;
		LegacySearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
		LegacySearchOptions.m_FontSize = 14.0f;
		if(ui_widget::InputField(DemoBrowserSearchCtx, &m_DemoSearchInput, DemoSearch, LegacySearchOptions).m_Changed)
		{
			RefreshFilteredDemos();
			DemolistOnUpdate(false);
		}
	}

	if(!Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive() && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_A))
	{
		SelectAllDemos();
	}

	bool HasSingleSelection =
		NumSelectedDemos() == 1 &&
		m_DemolistSelectedIndex >= 0 &&
		m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size() &&
		IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
	CDemoItem *pSelectedItem = HasSingleSelection ? m_vpFilteredDemos[m_DemolistSelectedIndex] : nullptr;
	if(!BrowsingScreenshots && pSelectedItem != nullptr && pSelectedItem->IsDemoFile() && !pSelectedItem->m_InfosLoaded)
		FetchHeader(*pSelectedItem);
	int NumSelectedDeletable = NumSelectedDeletableDemos();

	// refresh button
	{
		CUIRect RefreshButton;
		ButtonBarBottom.VSplitLeft(ButtonBarBottom.h * 3.0f, &RefreshButton, &ButtonBarBottom);
		ButtonBarBottom.VSplitLeft(ButtonBarBottom.h / 2.0f, nullptr, &ButtonBarBottom);
		SetIconMode(true);
		static CButtonContainer s_RefreshButton;
		if(DoButton_Menu_QmIcon(&s_RefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
		{
			SetIconMode(false);
			DemolistPopulate();
			DemolistOnUpdate(false);
		}
		SetIconMode(false);
		GameClient()->m_Tooltips.DoToolTip(&s_RefreshButton, &RefreshButton, Localize("Refresh the demo list"));
	}

	// fetch info checkbox
	if(!BrowsingScreenshots)
	{
		CUIRect FetchInfo;
		ButtonBarBottom.VSplitLeft(ButtonBarBottom.h * 7.0f, &FetchInfo, &ButtonBarBottom);
		ButtonBarBottom.VSplitLeft(ButtonBarBottom.h / 2.0f, nullptr, &ButtonBarBottom);
		if(DoButton_CheckBox(&g_Config.m_BrDemoFetchInfo, Localize("Fetch Info"), g_Config.m_BrDemoFetchInfo, &FetchInfo))
		{
			g_Config.m_BrDemoFetchInfo ^= 1;
			m_DemoHeaderFetchCursor = 0;
			m_DemoHeaderFetchComplete = DemoBrowserBrowsingScreenshots() || !g_Config.m_BrDemoFetchInfo;
		}
	}

	// demos directory button
	if(HasSingleSelection && pSelectedItem->m_StorageType != IStorage::TYPE_ALL)
	{
		CUIRect DemosDirectoryButton;
		ButtonBarBottom.VSplitLeft(ButtonBarBottom.h * 10.0f, &DemosDirectoryButton, &ButtonBarBottom);
		ButtonBarBottom.VSplitLeft(ButtonBarBottom.h / 2.0f, nullptr, &ButtonBarBottom);
		static CButtonContainer s_DemosDirectoryButton;
		if(DoButton_Menu(&s_DemosDirectoryButton, BrowsingScreenshots ? Localize("Screenshots directory") : Localize("Demos directory"), 0, &DemosDirectoryButton))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(pSelectedItem->m_StorageType, m_aCurrentDemoFolder[0] == '\0' ? pBaseFolder : m_aCurrentDemoFolder, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_DemosDirectoryButton, &DemosDirectoryButton, BrowsingScreenshots ? Localize("Open the folder containing screenshots") : Localize("Open the folder containing demo files"));
	}

	// play/open button
	if(HasSingleSelection)
	{
		CUIRect PlayButton;
		ButtonBarBottom.VSplitRight(ButtonBarBottom.h * 3.0f, &ButtonBarBottom, &PlayButton);
		ButtonBarBottom.VSplitRight(ButtonBarBottom.h, &ButtonBarBottom, nullptr);
		SetIconMode(true);
		static CButtonContainer s_PlayButton;
		const EQmIcon OpenIcon = pSelectedItem->m_IsDir ? EQmIcon::FOLDER_OPEN : (BrowsingScreenshots ? EQmIcon::IMAGE : EQmIcon::PLAY);
		const char *pOpenIcon = pSelectedItem->m_IsDir ? FONT_ICON_FOLDER_OPEN : (BrowsingScreenshots ? FONT_ICON_IMAGE : FONT_ICON_PLAY);
		const bool ActivateSelectedItem = DoButton_Menu_QmIcon(&s_PlayButton, OpenIcon, pOpenIcon, 0, &PlayButton) || WasListboxItemActivated ||
						  Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) ||
						  (!BrowsingScreenshots && Input()->KeyPress(KEY_P) && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive());
		SetIconMode(false);

		if(ActivateSelectedItem)
		{
			if(pSelectedItem->m_IsDir) // folder
			{
				m_DemoSearchInput.Clear();
				const bool ParentFolder = str_comp(pSelectedItem->m_aFilename, "..") == 0;
				if(ParentFolder) // parent folder
				{
					// QmClient: Rank 1 缓存目录（含其子目录）是挂在顶层的链接入口，
					// .. 直接回到 demos 根目录，不逐级穿过 qmclient/ 等内部目录
					const size_t RankCacheDirLen = str_length(CRankGhost::DEMO_CACHE_DIR);
					const bool InRankCacheFolder = str_startswith(m_aCurrentDemoFolder, CRankGhost::DEMO_CACHE_DIR) &&
								       (m_aCurrentDemoFolder[RankCacheDirLen] == '\0' || m_aCurrentDemoFolder[RankCacheDirLen] == '/');
					if(InRankCacheFolder)
					{
						str_copy(m_aCurrentDemoFolder, pBaseFolder);
						m_DemolistStorageType = IStorage::TYPE_ALL;
						str_copy(m_aCurrentDemoSelectionName, Localize("Rank 1 replays"));
					}
					else
					{
						str_copy(m_aCurrentDemoSelectionName, fs_filename(m_aCurrentDemoFolder));
						str_append(m_aCurrentDemoSelectionName, "/");
						if(fs_parent_dir(m_aCurrentDemoFolder))
						{
							m_aCurrentDemoFolder[0] = '\0';
							if(m_DemolistStorageType == IStorage::TYPE_ALL)
							{
								m_aCurrentDemoSelectionName[0] = '\0'; // will select first list item
							}
							else
							{
								Storage()->GetCompletePath(m_DemolistStorageType, pBaseFolder, m_aCurrentDemoSelectionName, sizeof(m_aCurrentDemoSelectionName));
								str_append(m_aCurrentDemoSelectionName, "/");
							}
						}
					}
				}
				else // sub folder
				{
					// QmClient: 链接项携带完整存储路径（如 Rank 1 回放缓存目录），直接跳转
					if(str_find(pSelectedItem->m_aFilename, "/") != nullptr)
					{
						str_copy(m_aCurrentDemoFolder, pSelectedItem->m_aFilename);
						m_DemolistStorageType = pSelectedItem->m_StorageType;
					}
					else
					{
						if(m_aCurrentDemoFolder[0] != '\0')
							str_append(m_aCurrentDemoFolder, "/");
						else
							m_DemolistStorageType = pSelectedItem->m_StorageType;
						str_append(m_aCurrentDemoFolder, pSelectedItem->m_aFilename);
					}
				}
				DemolistPopulate();
				DemolistOnUpdate(!ParentFolder);
			}
			else // file
			{
				if(BrowsingScreenshots)
				{
					char aBuf[IO_MAX_PATH_LENGTH];
					str_format(aBuf, sizeof(aBuf), "%s/%s", m_aCurrentDemoFolder, pSelectedItem->m_aFilename);
					char aFullPath[IO_MAX_PATH_LENGTH];
					Storage()->GetCompletePath(pSelectedItem->m_StorageType, aBuf, aFullPath, sizeof(aFullPath));
					if(!Client()->ViewFile(aFullPath))
						PopupMessage(Localize("Error opening screenshot"), Localize("Unable to open the screenshot file"), Localize("Ok"));
				}
				else if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
				{
					PopupConfirm(Localize("Disconnect"), Localize("Are you sure that you want to disconnect and play this demo?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmPlayDemo);
				}
				else
				{
					CMenus::PopupConfirmPlayDemo();
				}
				return;
			}
		}
		SetIconMode(false);
		const char *pPlayTooltip = pSelectedItem->m_IsDir ? Localize("Open the selected folder") : (BrowsingScreenshots ? Localize("Open the selected screenshot") : Localize("Play the selected demo"));
		GameClient()->m_Tooltips.DoToolTip(&s_PlayButton, &PlayButton, pPlayTooltip);
	}

	// The selected item can disappear when returning to the parent of a folder
	// that was deleted externally, so all later controls must re-check it.
	HasSingleSelection =
		NumSelectedDemos() == 1 &&
		m_DemolistSelectedIndex >= 0 &&
		m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size() &&
		IsDemoItemSelected(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
	pSelectedItem = HasSingleSelection ? m_vpFilteredDemos[m_DemolistSelectedIndex] : nullptr;
	if(!BrowsingScreenshots && pSelectedItem != nullptr && pSelectedItem->IsDemoFile() && !pSelectedItem->m_InfosLoaded)
		FetchHeader(*pSelectedItem);
	NumSelectedDeletable = NumSelectedDeletableDemos();
	const bool CanDownloadRankDemo = !BrowsingScreenshots && HasSingleSelection && pSelectedItem != nullptr && pSelectedItem->IsDemoFile() && pSelectedItem->m_InfosLoaded && pSelectedItem->m_Valid && pSelectedItem->m_Info.m_aMapName[0] != '\0';
	if(CanDownloadRankDemo)
	{
		CUIRect DownloadButton;
		ButtonBarBottom.VSplitRight(ButtonBarBottom.h * 3.0f, &ButtonBarBottom, &DownloadButton);
		ButtonBarBottom.VSplitRight(ButtonBarBottom.h / 2.0f, &ButtonBarBottom, nullptr);
		SetIconMode(true);
		static CButtonContainer s_RankDemoDownloadButton;
		const bool DownloadActive = m_RankDemoDownloadStage != ERankDemoDownloadStage::IDLE;
		if(DoButton_Menu_QmIcon(&s_RankDemoDownloadButton, EQmIcon::FILE, FONT_ICON_FILE, DownloadActive ? -1 : 0, &DownloadButton))
			StartRankDemoDownload(pSelectedItem->m_Info.m_aMapName);
		GameClient()->m_Tooltips.DoToolTip(&s_RankDemoDownloadButton, &DownloadButton, DownloadActive ? Localize("Downloading rank 1 demo") : Localize("Download the rank 1 demo for this map"));
		SetIconMode(false);
	}

	if(m_aCurrentDemoFolder[0] != '\0')
	{
		if(HasSingleSelection && IsDemoItemDeletable(*pSelectedItem))
		{
			// rename button
			CUIRect RenameButton;
			ButtonBarBottom.VSplitRight(ButtonBarBottom.h * 3.0f, &ButtonBarBottom, &RenameButton);
			ButtonBarBottom.VSplitRight(ButtonBarBottom.h / 2.0f, &ButtonBarBottom, nullptr);
			SetIconMode(true);
			static CButtonContainer s_RenameButton;
			if(DoButton_Menu_QmIcon(&s_RenameButton, EQmIcon::PENCIL, FONT_ICON_PENCIL, 0, &RenameButton))
			{
				SetIconMode(false);
				m_Popup = POPUP_RENAME_DEMO;
				if(pSelectedItem->m_IsDir)
				{
					m_DemoRenameInput.Set(pSelectedItem->m_aFilename);
				}
				else
				{
					char aNameWithoutExt[IO_MAX_PATH_LENGTH];
					fs_split_file_extension(pSelectedItem->m_aFilename, aNameWithoutExt, sizeof(aNameWithoutExt));
					m_DemoRenameInput.Set(aNameWithoutExt);
				}
				Ui()->SetActiveItem(&m_DemoRenameInput);
				return;
			}
			const char *pRenameTooltip = pSelectedItem->m_IsDir ? Localize("Rename folder") : (BrowsingScreenshots ? Localize("Rename screenshot") : Localize("Rename demo"));
			GameClient()->m_Tooltips.DoToolTip(&s_RenameButton, &RenameButton, pRenameTooltip);
			SetIconMode(false);
		}

		if(NumSelectedDeletable > 0)
		{
			static CButtonContainer s_DeleteButton;
			CUIRect DeleteButton;
			ButtonBarBottom.VSplitRight(ButtonBarBottom.h * 3.0f, &ButtonBarBottom, &DeleteButton);
			ButtonBarBottom.VSplitRight(ButtonBarBottom.h / 2.0f, &ButtonBarBottom, nullptr);
			SetIconMode(true);
			if(DoButton_Menu_QmIcon(&s_DeleteButton, EQmIcon::TRASH, FONT_ICON_TRASH, 0, &DeleteButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_DELETE) || (Input()->KeyPress(KEY_D) && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive()))
			{
				SetIconMode(false);
				PrepareDemoDeleteTargetsFromSelection();
				if(m_vDemoDeleteTargets.empty())
					return;

				if(m_vDemoDeleteTargets.size() == 1)
				{
					char aBuf[128 + IO_MAX_PATH_LENGTH];
					str_format(aBuf, sizeof(aBuf), m_vDemoDeleteTargets[0].m_IsDir ? Localize("Are you sure that you want to delete the folder '%s'?") : (BrowsingScreenshots ? Localize("Are you sure that you want to delete the screenshot '%s'?") : Localize("Are you sure that you want to delete the demo '%s'?")), m_vDemoDeleteTargets[0].m_Selection.m_aFilename);
					PopupConfirm(m_vDemoDeleteTargets[0].m_IsDir ? Localize("Delete folder") : (BrowsingScreenshots ? Localize("Delete screenshot") : Localize("Delete demo")), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteSelectedDemos);
				}
				else
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete %d selected items?"), (int)m_vDemoDeleteTargets.size());
					PopupConfirm(Localize("Delete selected items"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteSelectedDemos);
				}
				return;
			}
			const char *pDeleteTooltip = NumSelectedDeletable > 1 ? Localize("Delete selected items") : (pSelectedItem != nullptr && pSelectedItem->m_IsDir ? Localize("Delete folder") : (BrowsingScreenshots ? Localize("Delete screenshot") : Localize("Delete demo")));
			GameClient()->m_Tooltips.DoToolTip(&s_DeleteButton, &DeleteButton, pDeleteTooltip);
			SetIconMode(false);
		}

#if defined(CONF_VIDEORECORDER)
		// render demo button
		if(!BrowsingScreenshots && HasSingleSelection && !pSelectedItem->m_IsDir && pSelectedItem->IsDemoFile())
		{
			CUIRect RenderButton;
			ButtonBarTop.VSplitRight(ButtonBarBottom.h * 3.0f, &ButtonBarTop, &RenderButton);
			ButtonBarTop.VSplitRight(ButtonBarBottom.h, &ButtonBarTop, nullptr);
			SetIconMode(true);
			static CButtonContainer s_RenderButton;
			if(DoButton_Menu_QmIcon(&s_RenderButton, EQmIcon::VIDEO, FONT_ICON_VIDEO, 0, &RenderButton) || (Input()->KeyPress(KEY_R) && !GameClient()->m_GameConsole.IsActive() && !m_DemoSearchInput.IsActive()))
			{
				SetIconMode(false);
				m_HasPendingDemoRenderSource = false;
				m_DemoExportDisplayExpanded = false;
				m_Popup = POPUP_RENDER_DEMO;
				m_StartPaused = false;
				char aNameWithoutExt[IO_MAX_PATH_LENGTH];
				fs_split_file_extension(pSelectedItem->m_aFilename, aNameWithoutExt, sizeof(aNameWithoutExt));
				m_DemoRenderInput.Set(aNameWithoutExt);
				Ui()->SetActiveItem(&m_DemoRenderInput);
				return;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_RenderButton, &RenderButton, Localize("Render demo"));
			SetIconMode(false);
		}
#endif
	}
}

void CMenus::PopupConfirmPlayDemo()
{
	CDemoItem *pSelectedDemo = GetSelectedDemo();
	if(pSelectedDemo == nullptr)
	{
		PopupMessage(Localize("Error loading demo"), Localize("No demo selected"), Localize("Ok"));
		return;
	}
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s/%s", m_aCurrentDemoFolder, pSelectedDemo->m_aFilename);
	const char *pError = Client()->DemoPlayer_Play(aBuf, pSelectedDemo->m_StorageType);
	m_DemoCutPreview.Reset();
	m_vDemoCutSegments.clear();
	m_DemoCutPreview.Reset();
	g_Config.m_ClDemoSliceBegin = -1;
	g_Config.m_ClDemoSliceEnd = -1;
	m_LastPauseChange = -1.0f;
	m_LastSpeedChange = -1.0f;
	if(pError)
	{
		PopupMessage(Localize("Error loading demo"), pError, Localize("Ok"));
	}
	else
	{
		Ui()->SetActiveItem(nullptr);
		return;
	}
}

void CMenus::PopupConfirmDeleteSelectedDemos()
{
	if(m_vDemoDeleteTargets.empty())
		return;
	const bool SelectNeighbor = m_vDemoDeleteTargets.size() == 1 && m_DemolistSelectedIndex >= 0 && m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size();

	int NumDeleted = 0;
	int NumFailed = 0;
	bool FirstFailedWasDir = false;
	char aFirstFailedName[IO_MAX_PATH_LENGTH] = "";
	for(const auto &DeleteTarget : m_vDemoDeleteTargets)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s", m_aCurrentDemoFolder, DeleteTarget.m_Selection.m_aFilename);

		const bool Deleted = DeleteTarget.m_IsDir ? Storage()->RemoveFolder(aPath, DeleteTarget.m_Selection.m_StorageType) : Storage()->RemoveFile(aPath, DeleteTarget.m_Selection.m_StorageType);
		if(Deleted)
		{
			++NumDeleted;
		}
		else
		{
			if(NumFailed == 0)
			{
				FirstFailedWasDir = DeleteTarget.m_IsDir;
				str_copy(aFirstFailedName, DeleteTarget.m_Selection.m_aFilename);
			}
			++NumFailed;
		}
	}

	m_vDemoDeleteTargets.clear();

	if(NumDeleted > 0)
	{
		if(SelectNeighbor)
			DemolistSelectNeighbor();
		DemolistPopulate();
		DemolistOnUpdate(false);
	}

	if(NumFailed == 0)
		return;

	char aError[256 + IO_MAX_PATH_LENGTH];
	if(NumFailed == 1)
	{
		str_format(aError, sizeof(aError), FirstFailedWasDir ? Localize("Unable to delete the folder '%s'. Make sure it's empty first.") : (DemoBrowserBrowsingScreenshots() ? Localize("Unable to delete the screenshot '%s'") : Localize("Unable to delete the demo '%s'")), aFirstFailedName);
	}
	else if(NumDeleted == 0)
	{
		str_format(aError, sizeof(aError), Localize("Unable to delete %d selected items. The first failing item was '%s'."), NumFailed, aFirstFailedName);
	}
	else
	{
		str_format(aError, sizeof(aError), Localize("%d selected items were deleted, but %d items could not be deleted. The first failing item was '%s'."), NumDeleted, NumFailed, aFirstFailedName);
	}
	PopupMessage(Localize("Error"), aError, Localize("Ok"));
}

void CMenus::DemolistSelectNeighbor()
{
	const int NeighborIndex = m_DemolistSelectedIndex + 1 < (int)m_vpFilteredDemos.size() ? m_DemolistSelectedIndex + 1 : m_DemolistSelectedIndex - 1;
	str_copy(m_aCurrentDemoSelectionName, NeighborIndex >= 0 ? m_vpFilteredDemos[NeighborIndex]->m_aName : "");
}

void CMenus::PopupConfirmDeleteDemo()
{
	m_vDemoDeleteTargets.clear();
	if(m_DemolistSelectedIndex >= 0 && m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size())
	{
		SDemoDeleteTarget Target{};
		Target.m_Selection = DemoSelectionEntryFromItem(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
		Target.m_IsDir = false;
		m_vDemoDeleteTargets.push_back(Target);
	}
	PopupConfirmDeleteSelectedDemos();
}

void CMenus::PopupConfirmDeleteFolder()
{
	m_vDemoDeleteTargets.clear();
	if(m_DemolistSelectedIndex >= 0 && m_DemolistSelectedIndex < (int)m_vpFilteredDemos.size())
	{
		SDemoDeleteTarget Target{};
		Target.m_Selection = DemoSelectionEntryFromItem(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
		Target.m_IsDir = true;
		m_vDemoDeleteTargets.push_back(Target);
	}
	PopupConfirmDeleteSelectedDemos();
}

void CMenus::ConchainDemoPlay(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = static_cast<CMenus *>(pUserData);
	pThis->m_LastPauseChange = pThis->Client()->GlobalTime();
	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainDemoSpeed(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = static_cast<CMenus *>(pUserData);
	if(pResult->NumArguments() == 1)
	{
		pThis->m_LastSpeedChange = pThis->Client()->GlobalTime();
	}
	pfnCallback(pResult, pCallbackUserData);
}
