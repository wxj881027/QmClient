#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

FSettingsCardPreLayoutInput CMenus::BuildTClientCardPreLayoutInput(const char *pStableId)
{
	return {};
}

qm_card_catalog::STClientCardResult CMenus::RunTClientMainCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	uint64_t Revision = HashTClientSettingsCardLayout(pStableId);
	if(str_comp(pStableId, "tclient:font") == 0)
		Revision = HashValueFnv1a64(Revision, TextRender()->GetCustomFaces()->size());
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, Revision};
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const bool Render = Pass == qm_card_catalog::ETClientCardPass::RENDER;
	if(str_comp(pStableId, "tclient:visual-nameplates") == 0)
		return {LayoutTClientVisualNameplateCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:visual-effects") == 0)
		return {LayoutTClientVisualEffectsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:input") == 0)
		return {LayoutTClientInputCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:anti-latency-tools") == 0)
		return {LayoutTClientAntiLatencyToolsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:improved-anti-ping") == 0)
		return {LayoutTClientAntiPingSmoothingCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:execute-on-join") == 0)
		return {LayoutTClientAutoExecuteCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:voting") == 0)
		return {LayoutTClientVotingCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:player-indicator") == 0)
		return {LayoutTClientPlayerIndicatorCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:tee-status-bar") == 0)
		return {LayoutTClientTeeStatusBarCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:tile-outlines") == 0)
		return {LayoutTClientTileOutlinesCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:ghost-tools") == 0)
		return {LayoutTClientGhostToolsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:rainbow") == 0)
		return {LayoutTClientRainbowCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:tee-trails") == 0)
		return {LayoutTClientTeeTrailsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:background-draw") == 0)
		return {LayoutTClientBackgroundDrawCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:finish-name") == 0)
		return {LayoutTClientFinishNameCard(Ctx, Content, Render), Revision};
	SSettingsSection Section;
	if(str_comp(pStableId, "tclient:font") == 0)
		Section = BuildTClientThemeCacheSection();
	else if(str_comp(pStableId, "tclient:cursor") == 0)
		Section = BuildTClientCursorCacheSection();
	else if(str_comp(pStableId, "tclient:auto-reply") == 0)
		Section = BuildTClientAutoReplyCacheSection();
	else if(str_comp(pStableId, "tclient:pet") == 0)
		Section = BuildTClientPetCacheSection();
	else if(str_comp(pStableId, "tclient:hud") == 0)
		Section = BuildTClientHudCacheSection();
	else
		return {0.0f, Revision};
	const float Height = Render ? (Section.m_RenderFullFn ? Section.m_RenderFullFn(Content) : 0.0f) : (Section.m_MeasureFn ? Section.m_MeasureFn(Content) : 0.0f);
	return {Height, Revision};
}
