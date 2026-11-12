/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsIconOptions.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/message_gradient.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/tee_color_code.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/qmclient/tee_skin_apply.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	void LogPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

namespace
{
	struct SAudioPackEntry
	{
		char m_aName[64];
		int m_FileCount;
	};

	struct SAudioPackScanUser
	{
		IStorage *m_pStorage;
		std::vector<SAudioPackEntry> *m_pPacks;
	};
} // namespace

static int AudioPackFileScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	if(IsDir || pName[0] == '.')
		return 0;

	if(str_endswith(pName, ".wv") || str_endswith(pName, ".opus"))
	{
		int *pCount = static_cast<int *>(pUser);
		(*pCount)++;
	}

	return 0;
}

static int AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	if(!IsDir || pName[0] == '.' || str_comp(pName, "default") == 0)
		return 0;

	auto *pData = static_cast<SAudioPackScanUser *>(pUser);
	SAudioPackEntry Entry{};
	str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "audio/%s", pName);
	pData->m_pStorage->ListDirectory(IStorage::TYPE_ALL, aPath, AudioPackFileScan, &Entry.m_FileCount);

	if(Entry.m_FileCount == 0)
	{
		str_format(aPath, sizeof(aPath), "audio/%s/audio", pName);
		pData->m_pStorage->ListDirectory(IStorage::TYPE_ALL, aPath, AudioPackFileScan, &Entry.m_FileCount);
	}

	pData->m_pPacks->push_back(Entry);
	return 0;
}

static void RefreshAudioPacks(IStorage *pStorage, std::vector<SAudioPackEntry> &vPacks)
{
	vPacks.clear();

	SAudioPackEntry Default{};
	str_copy(Default.m_aName, "default", sizeof(Default.m_aName));
	pStorage->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackFileScan, &Default.m_FileCount);
	vPacks.push_back(Default);

	SAudioPackScanUser User{pStorage, &vPacks};
	pStorage->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackScan, &User);

	if(vPacks.size() > 1)
	{
		std::sort(vPacks.begin() + 1, vPacks.end(), [](const SAudioPackEntry &A, const SAudioPackEntry &B) {
			return str_comp(A.m_aName, B.m_aName) < 0;
		});
	}
}

static std::vector<SAudioPackEntry> gs_vAudioPacks;
static bool gs_AudioPacksInit = false;

static void RefreshSharedAudioPacks(IStorage *pStorage)
{
	RefreshAudioPacks(pStorage, gs_vAudioPacks);
	gs_AudioPacksInit = true;
}

static void EnsureSharedAudioPacks(IStorage *pStorage)
{
	if(!gs_AudioPacksInit)
		RefreshSharedAudioPacks(pStorage);
}

static int FindAudioPackIndexByName(const std::vector<SAudioPackEntry> &vPacks, const char *pPackName)
{
	for(size_t i = 0; i < vPacks.size(); ++i)
	{
		if(str_comp(vPacks[i].m_aName, pPackName) == 0)
			return (int)i;
	}
	return -1;
}

void CMenus::RefreshAudioPackList()
{
	RefreshSharedAudioPacks(Storage());
}

uint64_t CMenus::BuildSoundSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	static bool s_SndPackInit = false;
	static char s_aSndPack[sizeof(g_Config.m_SndPack)] = "";

	if(!s_SndPackInit)
	{
		str_copy(s_aSndPack, g_Config.m_SndPack, sizeof(s_aSndPack));
		s_SndPackInit = true;
	}

	const SSettingsContentMetrics SoundMetrics = Ctx.m_Metrics;
	const float BodySize = SoundMetrics.m_BodySize;
	const float LineHeight = SoundMetrics.m_LineHeight;
	const float LineSpacing = SoundMetrics.m_LineSpacing;
	const IUiContext SoundCardCtx = Ctx.m_UiContext;
	const auto DoSoundNumericField = [this, SoundCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = "%";
		Options.m_pScale = &CUi::ms_LogarithmicScrollbarScale;
		Options.m_FontSize = BodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_SOUND, -1, -1, pTextId, Rect, pLabel, 0u, Options))
			return false;
		return ui_widget::NumericField(SoundCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, 0, 100, Rect, Options);
	};
	const qm_card_registry::SCardDefault *pToggleDefault = qm_card_registry::FindByStableId("deck:sound-toggle");
	const qm_card_registry::SCardDefault *pVolumeDefault = qm_card_registry::FindByStableId("deck:sound-volume");
	const qm_card_registry::SCardDefault *pAudioPackDefault = qm_card_registry::FindByStableId("deck:sound-audio-pack");
	dbg_assert(pToggleDefault != nullptr && pVolumeDefault != nullptr && pAudioPackDefault != nullptr, "sound settings cards must be registered");
	if(pToggleDefault == nullptr || pVolumeDefault == nullptr || pAudioPackDefault == nullptr)
		return 0;

	const bool RenderOnly = Ctx.m_ReadOnly;
	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(3);
		const SSettingsCardSpec ToggleSpec{pToggleDefault->m_pStableId, Localize(pToggleDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pToggleDefault)};
		const SSettingsCardSpec VolumeSpec{pVolumeDefault->m_pStableId, Localize(pVolumeDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pVolumeDefault)};
		const SSettingsCardSpec AudioPackSpec{pAudioPackDefault->m_pStableId, Localize(pAudioPackDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pAudioPackDefault)};
		const auto AddCard = [this, &vCards, RenderOnly](const SSettingsCardSpec &Spec, FSettingsCardRenderMeasured Render, std::function<bool()> IsVisible = {}, bool VisibilityController = false) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_RenderMeasured = std::move(Render);
			Definition.m_IsVisible = std::move(IsVisible);
			Definition.m_VisibilityController = VisibilityController;
			qm_settings_content::UseDeclaredContent(Definition, Ui(), TextRender());
			if(RenderOnly)
				Definition.m_ContentLayoutInput = {};
			vCards.push_back(std::move(Definition));
		};
		AddCard(ToggleSpec, [this, SoundMetrics, LineHeight, LineSpacing, RenderOnly](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			CUIRect Button;
			if(Flow.Control("sound-enabled", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, -1, &g_Config.m_SndEnable, "Use sounds", Localize("Use sounds"), g_Config.m_SndEnable, &Button, SLabelProperties{}, CanCommit) && CanCommit)
				{
					g_Config.m_SndEnable ^= 1;
					UpdateMusicState();
				}
			}
			if(CanCommit)
				m_NeedRestartSound = (g_Config.m_SndEnable && !Sound()->IsSoundEnabled()) || str_comp(g_Config.m_SndPack, s_aSndPack) != 0;
			if(!g_Config.m_SndEnable)
				return;
			Flow.Space(LineSpacing);
			if(Flow.Control("SndMusic", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndMusic, "Play background music", Localize("Play background music"), g_Config.m_SndMusic, &Button) && CanCommit)
				{
					g_Config.m_SndMusic ^= 1;
					UpdateMusicState();
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndNonactiveMute", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndNonactiveMute, "Mute when not active", Localize("Mute when not active"), g_Config.m_SndNonactiveMute, &Button) && CanCommit)
				{
					g_Config.m_SndNonactiveMute ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndGame", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndGame, "Enable game sounds", Localize("Enable game sounds"), g_Config.m_SndGame, &Button) && CanCommit)
				{
					g_Config.m_SndGame ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(!qm_card_catalog::QmCardRenderHook::Personalization(this, ContentRect, "deck:sound-toggle", SoundMetrics, RenderOnly))
				return;

			if(Flow.Control("SndGun", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndGun, "Enable gun sound", Localize("Enable gun sound"), g_Config.m_SndGun, &Button) && CanCommit)
				{
					g_Config.m_SndGun ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndLongPain", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndLongPain, "Enable shout when holding fire while in water", Localize("Enable shout when holding fire while in water"), g_Config.m_SndLongPain, &Button) && CanCommit)
				{
					g_Config.m_SndLongPain ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndServerMessage", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndServerMessage, "Enable server message sound", Localize("Enable server message sound"), g_Config.m_SndServerMessage, &Button) && CanCommit)
				{
					g_Config.m_SndServerMessage ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndChat", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndChat, "Enable regular chat sound", Localize("Enable regular chat sound"), g_Config.m_SndChat, &Button) && CanCommit)
				{
					g_Config.m_SndChat ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndTeamChat", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndTeamChat, "Enable team chat sound", Localize("Enable team chat sound"), g_Config.m_SndTeamChat, &Button) && CanCommit)
				{
					g_Config.m_SndTeamChat ^= 1;
				}
			}
			Flow.Space(LineSpacing);
			if(Flow.Control("SndHighlight", LineHeight, Button))
			{
				if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndHighlight, "Enable highlighted chat sound", Localize("Enable highlighted chat sound"), g_Config.m_SndHighlight, &Button) && CanCommit)
				{
					g_Config.m_SndHighlight ^= 1;
				}
			} }, {}, true);
		AddCard(VolumeSpec, [this, SoundMetrics, DoSoundNumericField, LineHeight, LineSpacing, RenderOnly](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			CUIRect VolumeButton;
			if(Flow.Control("sound-volume", LineHeight, VolumeButton))
				DoSoundNumericField("sound-volume", &g_Config.m_SndVolume, &g_Config.m_SndVolume, VolumeButton, Localize("Sound volume"));
			Flow.Space(LineSpacing);
			if(!qm_card_catalog::QmCardRenderHook::Personalization(this, ContentRect, "deck:sound-volume", SoundMetrics, RenderOnly))
				return;

			if(Flow.Control("sound-game-volume", LineHeight, VolumeButton))
				DoSoundNumericField("sound-game-volume", &g_Config.m_SndGameVolume, &g_Config.m_SndGameVolume, VolumeButton, Localize("Game sound volume"));
			Flow.Space(LineSpacing);
			if(Flow.Control("sound-chat-volume", LineHeight, VolumeButton))
				DoSoundNumericField("sound-chat-volume", &g_Config.m_SndChatVolume, &g_Config.m_SndChatVolume, VolumeButton, Localize("Chat sound volume"));
			Flow.Space(LineSpacing);
			if(Flow.Control("sound-map-volume", LineHeight, VolumeButton))
				DoSoundNumericField("sound-map-volume", &g_Config.m_SndMapVolume, &g_Config.m_SndMapVolume, VolumeButton, Localize("Map sound volume"));
			Flow.Space(LineSpacing);
			if(Flow.Control("sound-background-music-volume", LineHeight, VolumeButton))
				DoSoundNumericField("sound-background-music-volume", &g_Config.m_SndBackgroundMusicVolume, &g_Config.m_SndBackgroundMusicVolume, VolumeButton, Localize("Background music volume")); }, [] { return g_Config.m_SndEnable != 0; });
		AddCard(AudioPackSpec, [this, SoundMetrics, LineHeight, LineSpacing, BodySize, RenderOnly](CUIRect &ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const bool CanCommit = Flow.Input() && !RenderOnly && !Ui()->RenderOnly();
			static CButtonContainer s_AudioPackRefreshButton;
			static CButtonContainer s_AudioPackEditorButton;
			static CButtonContainer s_AudioPackDirectoryButton;
			static CListBox s_AudioPackListBox;
			if(CanCommit)
				EnsureSharedAudioPacks(Storage());

			auto RefreshAudioPackState = [&]() {
				if(!CanCommit)
					return;
				RefreshSharedAudioPacks(Storage());
				if(g_Config.m_SndPack[0] == '\0')
					str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));
				if(m_AudioPackEditorState.m_PackNameInput.IsEmpty())
					m_AudioPackEditorState.m_PackNameInput.Set(g_Config.m_SndPack);
			};

			if(CanCommit && g_Config.m_SndPack[0] == '\0')
				str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));

			auto FindSelectedPackIndex = [&]() {
				int Result = FindAudioPackIndexByName(gs_vAudioPacks, g_Config.m_SndPack);
				if(CanCommit && Result < 0)
				{
					RefreshSharedAudioPacks(Storage());
					Result = FindAudioPackIndexByName(gs_vAudioPacks, g_Config.m_SndPack);
				}
				if(CanCommit && Result < 0)
				{
					str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));
					Result = 0;
				}
				return std::max(0, Result);
			};
			int SelectedPack = FindSelectedPackIndex();

			const auto Geometry = ResolveSettingsSoundAudioPackGeometry(gs_vAudioPacks.size(), SoundMetrics);
			CUIRect HeaderRow, ListRow;
			const bool HasHeader = Flow.Control("audio-pack-actions", LineHeight + 2.0f, HeaderRow);
			Flow.Space(LineSpacing);
			const bool HasList = Flow.Control("audio-pack-list", Geometry.m_ListViewportHeight + 14.0f, ListRow);
			if(HasHeader)
			{
				HeaderRow.HSplitTop(2.0f, nullptr, &HeaderRow);
				HeaderRow.VMargin(2.0f, &HeaderRow);

				const auto HeaderActions = ResolveSettingsInputActionsLayout(HeaderRow, 1, SoundMetrics);
				CUIRect RefreshButton = HeaderActions.m_Actions.m_aButtons[0];
				HeaderRow = HeaderActions.m_Input;
				static ui_widget::SSettingsActionGroupState s_AudioPackActions;
				const char *apActionLabels[] = {Localize("Edit audio pack"), Localize("Audio pack directory")};
				const auto Actions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_AudioPackActions, HeaderRow, apActionLabels, 2, SoundMetrics, ESettingsActionGroupWidth::FILL);
				CUIRect EditButton = Actions.m_aButtons[0];
				CUIRect DirectoryButton = Actions.m_aButtons[1];
				if(Ui()->DoButton_QmIcon(&s_AudioPackRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton, BUTTONFLAG_LEFT) && CanCommit)
				{
					RefreshAudioPackState();
					SelectedPack = FindSelectedPackIndex();
				}
				if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorButton, "sound-edit-audio-pack", apActionLabels[0], 0, &EditButton, SoundMetrics) && CanCommit)
					AudioPackEditorOpen(g_Config.m_SndPack);
				if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackDirectoryButton, "sound-audio-pack-directory", apActionLabels[1], 0, &DirectoryButton, SoundMetrics) && CanCommit)
				{
					char aBuf[IO_MAX_PATH_LENGTH];
					Storage()->GetCompletePath(IStorage::TYPE_SAVE, "audio", aBuf, sizeof(aBuf));
					Client()->ViewFile(aBuf);
				}
			}
			if(!HasList)
				return;
			ListRow.VMargin(2.0f, &ListRow);
			ListRow.h -= 2.0f;
			DrawRoundedSurface(Ui(), ListRow, ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f), ColorRGBA(), 6.0f);
			ListRow.Margin(6.0f, &ListRow);

			const int OldSelectedPack = SelectedPack;
			s_AudioPackListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			s_AudioPackListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_AudioPackListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);
			s_AudioPackListBox.DoStart(LineHeight + LineSpacing, gs_vAudioPacks.size(), 1, 4, SelectedPack, &ListRow, false);

			// 数量徽章在行内居中，使用紧凑尺寸并保留上下间隙。
			const float BadgeFontSize = SoundMetrics.m_SmallSize;
			const float BadgeWidth = 28.0f * SoundMetrics.m_UiScale;
			const float BadgeHeight = SoundMetrics.m_BadgeHeight;

			for(size_t i = 0; i < gs_vAudioPacks.size(); ++i)
			{
				const SAudioPackEntry &Entry = gs_vAudioPacks[i];
				const CListboxItem Item = s_AudioPackListBox.DoNextItem(&Entry, SelectedPack == (int)i);
				if(!Item.m_Visible)
					continue;

				char aLabel[128];
				if(str_comp(Entry.m_aName, "default") == 0)
				{
					str_copy(aLabel, Localize("Default"), sizeof(aLabel));
				}
				else
				{
					str_copy(aLabel, Entry.m_aName, sizeof(aLabel));
				}

				CUIRect NameRect, BadgeRect;
				Item.m_Rect.VSplitRight(BadgeWidth, &NameRect, &BadgeRect);
				NameRect.VMargin(6.0f, &NameRect);
				BadgeRect.HMargin((BadgeRect.h - BadgeHeight) * 0.5f, &BadgeRect);

				char aBadge[32];
				str_format(aBadge, sizeof(aBadge), "%d", Entry.m_FileCount);
				DrawRoundedSurface(Ui(), BadgeRect, SelectedPack == (int)i ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), ColorRGBA(), ui_token::radius::TIGHT);

				Ui()->DoLabel(&NameRect, aLabel, BodySize, TEXTALIGN_ML);
				Ui()->DoLabel(&BadgeRect, aBadge, BadgeFontSize, TEXTALIGN_MC);
			}

			SelectedPack = s_AudioPackListBox.DoEnd();
			if(CanCommit && SelectedPack != OldSelectedPack && SelectedPack >= 0 && SelectedPack < (int)gs_vAudioPacks.size())
			{
				str_copy(g_Config.m_SndPack, gs_vAudioPacks[SelectedPack].m_aName, sizeof(g_Config.m_SndPack));
				if(GameClient()->m_Sounds.Reload())
				{
					str_copy(s_aSndPack, g_Config.m_SndPack, sizeof(s_aSndPack));
					UpdateMusicState();
				}
				if(!m_AudioPackEditorState.m_PackNameInput.IsActive())
					m_AudioPackEditorState.m_PackNameInput.Set(g_Config.m_SndPack);
			} }, []() { return g_Config.m_SndEnable != 0; });
	};
	const uint64_t SoundLayoutRevision = RenderOnly;
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
	}
	if(!RenderOnly)
		m_NeedRestartSound = m_NeedRestartSound || str_comp(g_Config.m_SndPack, s_aSndPack) != 0;
	return SoundLayoutRevision;
}
