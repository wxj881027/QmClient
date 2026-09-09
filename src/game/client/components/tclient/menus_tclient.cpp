#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	TCLIENT_TAB_SETTINGS = 0,
	TCLIENT_TAB_BINDWHEEL,
	TCLIENT_TAB_WARLIST,
	TCLIENT_TAB_BINDCHAT,
	TCLIENT_TAB_STATUSBAR,
	TCLIENT_TAB_INFO,
	NUMBER_OF_TCLIENT_TABS
};

constexpr float TCLIENT_BODY_FONT_SIZE = CMenus::TCLIENT_SETTINGS_BODY_FONT_SIZE;
constexpr float TCLIENT_HEADLINE_FONT_SIZE = 20.0f;

static SLabelProperties TClientFixedLabelProperties(float FontSize, float MaxWidth = -1.0f)
{
	SLabelProperties Props;
	Props.m_MaxWidth = MaxWidth;
	Props.m_MinimumFontSize = FontSize;
	Props.m_EllipsisAtEnd = true;
	return Props;
}

static void DoTClientLabel(CUi *pUi, const CUIRect *pRect, const char *pText, float FontSize, int Align)
{
	pUi->DoLabel(pRect, pText, FontSize, Align, TClientFixedLabelProperties(FontSize, pRect->w));
}

int CMenus::DoTClientSettingsButton_CheckBox(const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect)
{
	return DoSettingsButton_CheckBox(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, pId, pTextId, pText, Checked, pRect, TClientFixedLabelProperties(TCLIENT_BODY_FONT_SIZE, pRect->w), true, TCLIENT_BODY_FONT_SIZE);
}

int CMenus::DoTClientSettingsButton_Menu(CButtonContainer *pButtonContainer, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding)
{
	return DoSettingsButton_Menu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, pButtonContainer, pTextId, pText, Checked, pRect, Flags, Corners, Rounding, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, TCLIENT_BODY_FONT_SIZE);
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

using namespace FontIcons;

namespace
{
	bool LoadTClientOrderFromGlobalCardModel(const char *pConfig, std::vector<std::string> &vLeftOrder, std::vector<std::string> &vRightOrder)
	{
		if(pConfig == nullptr || pConfig[0] == '\0')
			return false;

		qm_card_order::CModel Model;
		if(!Model.LoadExplicit(pConfig, qm_card_registry::BuildDefaultEntries()))
			return false;

		std::vector<std::string> vParsedLeftOrder = Model.StableIdOrder("tclient:", "tclient", 1);
		std::vector<std::string> vParsedRightOrder = Model.StableIdOrder("tclient:", "tclient", 2);
		if(vParsedLeftOrder.empty() && vParsedRightOrder.empty())
			return false;

		vLeftOrder = std::move(vParsedLeftOrder);
		vRightOrder = std::move(vParsedRightOrder);
		return true;
	}

	void LoadTClientOrderFromLegacyCardOrder(const char *pConfig, std::vector<std::string> &vLeftOrder, std::vector<std::string> &vRightOrder)
	{
		if(pConfig == nullptr || pConfig[0] == '\0')
			return;
		vLeftOrder.clear();
		vRightOrder.clear();
		std::vector<std::pair<int, std::string>> vLeftEntries;
		std::vector<std::pair<int, std::string>> vRightEntries;
		const char *p = pConfig;
		char aToken[128];
		while((p = str_next_token(p, ";", aToken, sizeof(aToken))) != nullptr)
		{
			if(aToken[0] == '\0')
				continue;
			char aId[80];
			char aCol[16];
			char aOrder[16];
			const char *pLastColon = nullptr;
			for(const char *pIt = aToken; *pIt != '\0'; ++pIt)
			{
				if(*pIt == ':')
					pLastColon = pIt;
			}
			if(pLastColon == nullptr)
				continue;
			const char *pSecondLastColon = nullptr;
			for(const char *pIt = aToken; pIt < pLastColon; ++pIt)
			{
				if(*pIt == ':')
					pSecondLastColon = pIt;
			}
			if(pSecondLastColon == nullptr)
				continue;
			const int IdLen = (int)(pSecondLastColon - aToken);
			const int ColumnLen = (int)(pLastColon - pSecondLastColon - 1);
			if(IdLen <= 0 || IdLen >= (int)sizeof(aId) || ColumnLen <= 0 || ColumnLen >= (int)sizeof(aCol))
				continue;
			str_copy(aId, aToken, IdLen + 1);
			str_copy(aCol, pSecondLastColon + 1, ColumnLen + 1);
			str_copy(aOrder, pLastColon + 1, sizeof(aOrder));
			int Col = 0;
			if(!str_toint(aCol, &Col))
				continue;
			int Order = 0;
			if(!str_toint(aOrder, &Order) || Order < 0)
				continue;
			if(Col == 0)
				vLeftEntries.emplace_back(Order, aId);
			else if(Col == 1)
				vRightEntries.emplace_back(Order, aId);
		}
		const auto OrderLess = [](const auto &a, const auto &b) {
			return a.first < b.first;
		};
		std::stable_sort(vLeftEntries.begin(), vLeftEntries.end(), OrderLess);
		std::stable_sort(vRightEntries.begin(), vRightEntries.end(), OrderLess);
		for(const auto &Entry : vLeftEntries)
			vLeftOrder.push_back(Entry.second);
		for(const auto &Entry : vRightEntries)
			vRightOrder.push_back(Entry.second);
	}

	bool SerializeMergedTClientGlobalCardOrder(const char *pExistingGlobalOrder, const std::vector<std::string> &vLeftOrder, const std::vector<std::string> &vRightOrder, char *pOut, int OutSize)
	{
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vLeftOrder.size() + vRightOrder.size());
		auto AppendOrder = [&](const std::vector<std::string> &vOrder, int Column) {
			for(size_t i = 0; i < vOrder.size(); ++i)
			{
				if(vOrder[i].empty())
					continue;
				vEntries.push_back({vOrder[i].c_str(), "tclient", Column, (int)i});
			}
		};
		AppendOrder(vLeftOrder, 1);
		AppendOrder(vRightOrder, 2);
		return qm_card_order::SerializeMergedReplacingPrefix(pExistingGlobalOrder, "tclient:", vEntries, pOut, OutSize);
	}
}

[[maybe_unused]] static float s_Time = 0.0f;
[[maybe_unused]] static bool s_StartedTime = false;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
extern bool g_CommandBindCacheInitialized;

namespace
{
	int CanonicalizePersistedTClientTab(int Tab)
	{
		if(Tab < 0 || Tab >= NUMBER_OF_TCLIENT_TABS)
			return TCLIENT_TAB_SETTINGS;
		return Tab;
	}

	int CanonicalizePersistedQmClientTab(int Tab)
	{
		if(Tab < 0 || Tab >= CMenus::NUMBER_OF_QMCLIENT_SETTINGS_TABS)
			return CMenus::QMCLIENT_SETTINGS_TAB_VISUAL;
		return Tab;
	}

	bool PerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	void LogTClientPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/tclient", pStage, DurationMs, Force, nullptr, nullptr, nullptr, pExtra);
	}

	void LogTClientPerfStageEx(const char *pScope, const char *pSection, ETClientSettingsPerfStage Stage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		char aStage[128];
		if(pSection != nullptr && pSection[0] != '\0')
			str_format(aStage, sizeof(aStage), "%s_%s_%s", pScope, pSection, SettingsTClientPerfStageName(Stage));
		else
			str_format(aStage, sizeof(aStage), "%s_%s", pScope, SettingsTClientPerfStageName(Stage));
		LogTClientPerfStage(aStage, DurationMs, Force, pExtra);
	}

	static CSectionLoader s_VisualFontLoader;
	static CSectionLoader s_RightSectionLoader;
	static int s_TClientWarListFilterRevision = 0;

	struct SSectionCullContext
	{
		float m_ViewportTop;
		float m_ViewportBottom;
		float m_PrefetchPadding;
	};

	bool IsSectionVisible(const CUIRect &SectionRect, const SSectionCullContext &Context)
	{
		return SectionRect.y + SectionRect.h >= Context.m_ViewportTop - Context.m_PrefetchPadding &&
		       SectionRect.y <= Context.m_ViewportBottom + Context.m_PrefetchPadding;
	}

	uint64_t HashBytesFnv1a64(uint64_t Hash, const void *pData, size_t DataSize)
	{
		const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
		for(size_t i = 0; i < DataSize; ++i)
		{
			Hash ^= pBytes[i];
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	template<typename T>
	uint64_t HashValueFnv1a64(uint64_t Hash, const T &Value)
	{
		return HashBytesFnv1a64(Hash, &Value, sizeof(Value));
	}

	[[maybe_unused]] uint64_t HashStringFnv1a64(uint64_t Hash, const char *pString)
	{
		return pString == nullptr ? Hash : HashBytesFnv1a64(Hash, pString, str_length(pString));
	}

	uint64_t HashTClientSettingsConfig()
	{
		uint64_t Hash = 1469598103934665603ull;
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) \
	if(str_startswith(#ScriptName, "tc_")) \
		Hash = HashValueFnv1a64(Hash, g_Config.m_##Name);
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) \
	if(str_startswith(#ScriptName, "tc_")) \
		Hash = HashValueFnv1a64(Hash, g_Config.m_##Name);
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) \
	if(str_startswith(#ScriptName, "tc_")) \
		Hash = HashStringFnv1a64(Hash, g_Config.m_##Name);
#define SET_CONFIG_DOMAIN(ConfigDomain) ;
#include <engine/shared/config_includes.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
#undef SET_CONFIG_DOMAIN
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmAutoMargin);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmFastInputMode);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmBestInputOffset);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmBestInputSmoothing);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmBestInputLatencyComp);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmBestInputInterpolation);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmBestInputOthers);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmSaikoPlusAmount);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmSaikoPlusOthers);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTee);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTeeDuration);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTeeOthers);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTeeStrength);
		return Hash;
	}

	uint64_t HashTClientSettingsCardLayout(const char *pStableCardId)
	{
		uint64_t Hash = 1469598103934665603ull;
		if(str_comp(pStableCardId, "tclient:visual-font-cursor") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_TcAnimateWheelTime > 0);
		if(str_comp(pStableCardId, "tclient:visual-nameplates") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_TcWhiteFeet);
		if(str_comp(pStableCardId, "tclient:visual-effects") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcTinyTees > 0);
			return HashValueFnv1a64(Hash, g_Config.m_QmJellyTee);
		}
		if(str_comp(pStableCardId, "tclient:input") == 0)
			return HashValueFnv1a64(Hash, QmFastInputNormalizedMode(g_Config.m_QmFastInputMode));
		if(str_comp(pStableCardId, "tclient:anti-latency-tools") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcRemoveAnti);
			return HashValueFnv1a64(Hash, g_Config.m_TcPredMarginInFreeze);
		}
		if(str_comp(pStableCardId, "tclient:auto-reply") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcAutoReplyMuted);
			return HashValueFnv1a64(Hash, g_Config.m_TcAutoReplyMinimized);
		}
		if(str_comp(pStableCardId, "tclient:player-indicator") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcIndicatorVariableDistance);
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcWarListIndicator);
			return HashValueFnv1a64(Hash, g_Config.m_TcWarListIndicatorColors);
		}
		if(str_comp(pStableCardId, "tclient:hud") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcRenderCursorSpec);
			Hash = HashValueFnv1a64(Hash, g_Config.m_TcNotifyWhenLast);
			return HashValueFnv1a64(Hash, g_Config.m_TcShowCenter);
		}
		if(str_comp(pStableCardId, "tclient:tee-status-bar") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_TcShowFrozenText > 0);
		if(str_comp(pStableCardId, "tclient:finish-name") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_TcChangeNameNearFinish != 0);
		if(str_comp(pStableCardId, "tclient:tee-trails") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID);
		return Hash;
	}

	SSettingsSectionCacheRuntimeKey MakeSettingsSectionRuntimeKey(CUIRect View, IGraphics *pGraphics, bool IncludeConfigHash = true)
	{
		SSettingsSectionCacheRuntimeKey RuntimeKey;
		RuntimeKey.m_ViewportWidth = SettingsRuntimeCacheDimensionKey(View.w);
		RuntimeKey.m_ViewportHeight = SettingsRuntimeCacheDimensionKey(View.h);
		RuntimeKey.m_ConfigHash = IncludeConfigHash ? HashTClientSettingsConfig() : 0;
		RuntimeKey.m_LanguageHash = str_quickhash(g_Config.m_ClLanguagefile);
		RuntimeKey.m_FontHash = HashStringFnv1a64(1469598103934665603ull, g_Config.m_TcCustomFont);
		RuntimeKey.m_BackendHash = str_quickhash(g_Config.m_GfxBackend);
		if(pGraphics)
		{
			RuntimeKey.m_UiScale = SettingsRuntimeCachePositiveRoundedKey(pGraphics->ScreenHiDPIScale() * std::clamp(g_Config.m_QmUiScale, 50, 200));
			RuntimeKey.m_WindowHash = HashValueFnv1a64(1469598103934665603ull, pGraphics->WindowWidth());
			RuntimeKey.m_WindowHash = HashValueFnv1a64(RuntimeKey.m_WindowHash, pGraphics->WindowHeight());
		}
		return RuntimeKey;
	}

	SSettingsRuntimeCacheKey ToSettingsRuntimeCacheKey(const SSettingsSectionCacheRuntimeKey &RuntimeKey)
	{
		SSettingsRuntimeCacheKey Key;
		Key.m_LanguageHash = RuntimeKey.m_LanguageHash;
		Key.m_FontGeneration = RuntimeKey.m_FontHash;
		Key.m_BackendGeneration = RuntimeKey.m_BackendHash;
		Key.m_WindowWidth = RuntimeKey.m_ViewportWidth;
		Key.m_WindowHeight = RuntimeKey.m_ViewportHeight;
		Key.m_UiScale = RuntimeKey.m_UiScale;
		Key.m_ConfigHash = RuntimeKey.m_ConfigHash;
		return Key;
	}

	class CUiRenderOnlyGuard
	{
	public:
		explicit CUiRenderOnlyGuard(CUi *pUi) :
			m_pUi(pUi)
		{
			m_pUi->BeginRenderOnly();
		}

		~CUiRenderOnlyGuard()
		{
			m_pUi->EndRenderOnly();
		}

	private:
		CUi *m_pUi;
	};

	// Deck definitions are cached across frames, while several TClient pages still
	// build frame-local layout callbacks. The cached callbacks only retain this
	// stable bridge and the bridge is rebound for the synchronous RenderCached call.
	class CTClientSettingsCardFrameBinding
	{
	public:
		template<typename TMeasure, typename TRender>
		void Bind(const TMeasure &Measure, const TRender &Render)
		{
			m_SectionMeasure = nullptr;
			m_SectionRender = nullptr;
			m_pIndexedMeasureObject = nullptr;
			m_pIndexedRenderObject = nullptr;
			m_Index = 0;
			m_pMeasureObject = &Measure;
			m_pMeasure = [](const void *pObject, float ContentWidth) {
				return (*static_cast<const TMeasure *>(pObject))(ContentWidth);
			};
			m_pRenderObject = &Render;
			m_pRender = [](const void *pObject, CUIRect &Content) {
				(*static_cast<const TRender *>(pObject))(Content);
			};
		}

		template<typename TMeasure, typename TRender>
		void BindIndexed(const TMeasure &Measure, const TRender &Render, size_t Index)
		{
			m_SectionMeasure = nullptr;
			m_SectionRender = nullptr;
			m_Index = Index;
			m_pMeasure = [](const void *pObject, float ContentWidth) {
				const auto *pSelf = static_cast<const CTClientSettingsCardFrameBinding *>(pObject);
				return (*static_cast<const TMeasure *>(pSelf->m_pIndexedMeasureObject))(pSelf->m_Index, ContentWidth);
			};
			m_pIndexedMeasureObject = &Measure;
			m_pMeasureObject = this;
			m_pRenderObject = this;
			m_pIndexedRenderObject = &Render;
			m_pRender = [](const void *pObject, CUIRect &Content) {
				const auto *pSelf = static_cast<const CTClientSettingsCardFrameBinding *>(pObject);
				(*static_cast<const TRender *>(pSelf->m_pIndexedRenderObject))(pSelf->m_Index, Content);
			};
		}

		void BindSection(SSettingsSection &Section)
		{
			// Deck 与 loader 在当前帧同步消费同一组回调。把较大的 frame-local
			// function 移入稳定 bridge，并给 loader 留下小型转发器。
			m_SectionMeasure = std::move(Section.m_MeasureFn);
			m_SectionRender = std::move(Section.m_RenderFullFn);
			Section.m_MeasureFn = [this](CUIRect &MeasureColumn) {
				return m_SectionMeasure ? m_SectionMeasure(MeasureColumn) : 0.0f;
			};
			m_pIndexedMeasureObject = nullptr;
			m_pIndexedRenderObject = nullptr;
			m_Index = 0;
			m_pMeasureObject = this;
			m_pMeasure = [](const void *pObject, float ContentWidth) {
				const auto *pSelf = static_cast<const CTClientSettingsCardFrameBinding *>(pObject);
				if(!pSelf->m_SectionMeasure)
					return 0.0f;
				CUIRect MeasureColumn{0.0f, 0.0f, ContentWidth, 0.0f};
				return pSelf->m_SectionMeasure(MeasureColumn);
			};
			m_pRenderObject = this;
			m_pRender = [](const void *pObject, CUIRect &Content) {
				const auto *pSelf = static_cast<const CTClientSettingsCardFrameBinding *>(pObject);
				if(pSelf->m_SectionRender)
					pSelf->m_SectionRender(Content);
			};
		}

		float Measure(float ContentWidth) const
		{
			return m_pMeasure != nullptr ? m_pMeasure(m_pMeasureObject, ContentWidth) : 0.0f;
		}

		void Render(CUIRect &Content) const
		{
			if(m_pRender != nullptr)
				m_pRender(m_pRenderObject, Content);
		}

		void Clear()
		{
			m_pMeasureObject = nullptr;
			m_pMeasure = nullptr;
			m_pRenderObject = nullptr;
			m_pRender = nullptr;
			m_pIndexedMeasureObject = nullptr;
			m_pIndexedRenderObject = nullptr;
			m_Index = 0;
			m_SectionMeasure = nullptr;
			m_SectionRender = nullptr;
		}

	private:
		const void *m_pMeasureObject = nullptr;
		float (*m_pMeasure)(const void *, float) = nullptr;
		const void *m_pRenderObject = nullptr;
		void (*m_pRender)(const void *, CUIRect &) = nullptr;
		const void *m_pIndexedMeasureObject = nullptr;
		const void *m_pIndexedRenderObject = nullptr;
		size_t m_Index = 0;
		std::function<float(CUIRect &)> m_SectionMeasure;
		FSettingsCardRenderMeasured m_SectionRender;
	};

	uint64_t TClientCardDefinitionsLayoutRevision(bool ReadOnly, int Tab, const char *pDeckTab, uint64_t DynamicRevision = 0)
	{
		uint64_t Revision = 1469598103934665603ull;
		Revision = HashValueFnv1a64(Revision, ReadOnly);
		Revision = HashValueFnv1a64(Revision, Tab);
		Revision = HashStringFnv1a64(Revision, pDeckTab);
		return HashValueFnv1a64(Revision, DynamicRevision);
	}

}

static float FontSize = ui_token::font::BODY;
static float EditBoxFontSize = ui_token::font::BODY;
static float LineSize = ui_token::settings::ROW_HEIGHT;
static float ColorPickerLineSize = ui_token::settings::ROW_HEIGHT + ui_token::settings::ROW_GAP;
static float HeadlineFontSize = ui_token::font::HEADLINE;
static float StandardFontSize = ui_token::font::BODY;

static float HeadlineHeight = ui_token::font::HEADLINE;
const float Margin = 10.0f;
static float MarginSmall = ui_token::settings::ROW_GAP;
static float MarginExtraSmall = ui_token::settings::ROW_GAP;
static float MarginBetweenSections = ui_token::settings::ROW_GAP * 2.0f;

static float ColorPickerLabelSize = ui_token::font::BODY;
static float ColorPickerLineSpacing = ui_token::settings::ROW_GAP;
static std::vector<CButtonContainer> s_vTinyTeeModeButtons = {{}, {}, {}};
static CButtonContainer s_FastInputModeFast;
static CButtonContainer s_FastInputModeBest;
static CButtonContainer s_FastInputModeSaikoPlus;
static int s_CountFrozenText = 0;
static CUi::SDropDownState s_TrailDropDownState;
static CScrollRegion s_TrailDropDownScrollRegion;

static float TClientSettingsRowsHeight(const int NumRows)
{
	return NumRows > 0 ? LineSize * NumRows + MarginSmall * (NumRows - 1) : 0.0f;
}

class CTClientSettingsRowAllocator
{
	CUIRect &m_Column;
	bool m_HasPreviousRow = false;

public:
	explicit CTClientSettingsRowAllocator(CUIRect &Column) :
		m_Column(Column)
	{
	}

	CUIRect Next(float Height)
	{
		if(m_HasPreviousRow)
			m_Column.HSplitTop(MarginSmall, nullptr, &m_Column);
		m_HasPreviousRow = true;
		CUIRect Row;
		m_Column.HSplitTop(Height, &Row, &m_Column);
		return Row;
	}

	CUIRect Next()
	{
		return Next(LineSize);
	}
};

static void ApplyTClientContentMetrics(const float ContentWidth)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(ContentWidth);
	FontSize = Metrics.m_BodySize;
	EditBoxFontSize = Metrics.m_BodySize;
	LineSize = Metrics.m_LineHeight;
	ColorPickerLineSize = Metrics.m_ButtonHeight;
	HeadlineFontSize = Metrics.m_HeadlineSize;
	StandardFontSize = Metrics.m_BodySize;
	HeadlineHeight = Metrics.m_LineHeight;
	MarginSmall = Metrics.m_LineSpacing;
	MarginExtraSmall = Metrics.m_LineSpacing;
	MarginBetweenSections = Metrics.m_SectionGap;
	ColorPickerLabelSize = Metrics.m_BodySize;
	ColorPickerLineSpacing = Metrics.m_LineSpacing;
}

int CMenus::DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float VMargin)
{
	return DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_TCLIENT, m_TClientSettingsTab, pId, pTextId, pText, pValue, pRect, VMargin, 0.0f, FontSize);
}

static constexpr const char *SETTINGS_RUNTIME_CACHE_METADATA_FILE = "qmclient/settings_section_cache_metadata.cfg";

CUIRect TClientSettingsContentView(CUIRect MainView, CUIRect *pTabBar = nullptr)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const SSettingsSubTabLayoutFrame SubTabs = ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale);
	if(pTabBar != nullptr)
		*pTabBar = SubTabs.m_TabBarRect;
	return SubTabs.m_ContentRect;
}

void CMenus::BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)
{
	Tab = CanonicalizePersistedTClientTab(Tab);
	const int PreviousTab = m_TClientSettingsTab;
	const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;

	g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
	m_TClientSettingsTab = Tab;
	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();
	RenderSettings(MainView);
	Ui()->EndRenderOnly();
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	m_TClientSettingsTab = PreviousTab;
	g_Config.m_UiSettingsPage = PreviousSettingsPage;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRulePlain
{
	std::string m_Keywords;
	std::string m_Reply;
	bool m_AutoRename = false;
	bool m_Regex = false;
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRuleInputRow
{
	char m_aTrigger[512] = "";
	char m_aReply[256] = "";
	int m_AutoRename = 0;
	int m_Regex = 0;
	CLineInput m_TriggerInput;
	CLineInput m_ReplyInput;

	SAutoReplyRuleInputRow()
	{
		m_TriggerInput.SetBuffer(m_aTrigger, sizeof(m_aTrigger));
		m_ReplyInput.SetBuffer(m_aReply, sizeof(m_aReply));
	}
};

static char *ParseAutoReplyRulePrefixes(char *pLine, bool &OutAutoRename, bool &OutRegex, bool &OutHasExplicitRenameFlag, bool &OutHasExplicitRegexFlag)
{
	OutAutoRename = false;
	OutRegex = false;
	OutHasExplicitRenameFlag = false;
	OutHasExplicitRegexFlag = false;

	char *pTrimmedLine = (char *)str_utf8_skip_whitespaces(pLine);
	while(true)
	{
		const char *pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[rename]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[r]");
		if(pAfterPrefix)
		{
			OutAutoRename = true;
			OutHasExplicitRenameFlag = true;
			pTrimmedLine = (char *)str_utf8_skip_whitespaces(pAfterPrefix);
			continue;
		}

		pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[regex]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[re]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[rx]");
		if(pAfterPrefix)
		{
			OutRegex = true;
			OutHasExplicitRegexFlag = true;
			pTrimmedLine = (char *)str_utf8_skip_whitespaces(pAfterPrefix);
			continue;
		}

		break;
	}

	return pTrimmedLine;
}

static bool CopyTrimmedString(const char *pSrc, char *pOut, size_t OutSize)
{
	pOut[0] = '\0';
	if(!pSrc)
		return false;

	char aBuf[1024];
	str_copy(aBuf, pSrc, sizeof(aBuf));
	char *pTrimmed = (char *)str_utf8_skip_whitespaces(aBuf);
	str_utf8_trim_right(pTrimmed);
	str_copy(pOut, pTrimmed, OutSize);
	return pOut[0] != '\0';
}

[[maybe_unused]] static std::unique_ptr<SAutoReplyRuleInputRow> CreateAutoReplyRuleInputRow(const char *pTrigger = "", const char *pReply = "", bool AutoRename = false, bool Regex = false)
{
	auto pRow = std::make_unique<SAutoReplyRuleInputRow>();
	pRow->m_TriggerInput.Set(pTrigger);
	pRow->m_ReplyInput.Set(pReply);
	pRow->m_AutoRename = AutoRename ? 1 : 0;
	pRow->m_Regex = Regex ? 1 : 0;
	return pRow;
}

[[maybe_unused]] static void ParseAutoReplyRules(const char *pRules, std::vector<SAutoReplyRulePlain> &vOutRules)
{
	vOutRules.clear();
	if(!pRules || pRules[0] == '\0')
		return;

	const char *pCursor = pRules;
	while(*pCursor)
	{
		char aLine[1024];
		int LineLen = 0;
		while(*pCursor && *pCursor != '\n' && *pCursor != '\r')
		{
			if(LineLen < (int)sizeof(aLine) - 1)
				aLine[LineLen++] = *pCursor;
			pCursor++;
		}
		aLine[LineLen] = '\0';

		while(*pCursor == '\n' || *pCursor == '\r')
			pCursor++;

		char *pLine = (char *)str_utf8_skip_whitespaces(aLine);
		str_utf8_trim_right(pLine);
		if(pLine[0] == '\0' || pLine[0] == '#')
			continue;

		bool AutoRename = false;
		bool RegexRule = false;
		bool HasExplicitRenameFlag = false;
		bool HasExplicitRegexFlag = false;
		char *pRuleText = ParseAutoReplyRulePrefixes(pLine, AutoRename, RegexRule, HasExplicitRenameFlag, HasExplicitRegexFlag);
		(void)HasExplicitRenameFlag;
		(void)HasExplicitRegexFlag;

		const char *pArrowConst = str_find(pRuleText, "=>");
		if(!pArrowConst)
			continue;

		char *pArrow = pRuleText + (pArrowConst - pRuleText);
		*pArrow = '\0';
		pArrow += 2;

		char *pKeywords = (char *)str_utf8_skip_whitespaces(pRuleText);
		str_utf8_trim_right(pKeywords);
		char *pReply = (char *)str_utf8_skip_whitespaces(pArrow);
		str_utf8_trim_right(pReply);
		if(pKeywords[0] == '\0' || pReply[0] == '\0')
			continue;

		vOutRules.push_back({pKeywords, pReply, AutoRename, RegexRule});
	}
}

[[maybe_unused]] static bool AutoReplyRowsMatchRules(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, const std::vector<SAutoReplyRulePlain> &vRules)
{
	std::vector<SAutoReplyRulePlain> vCompleteRows;
	vCompleteRows.reserve(vRows.size());
	for(const auto &pRow : vRows)
	{
		char aTrigger[512];
		char aReply[256];
		const bool HasTrigger = CopyTrimmedString(pRow->m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
		const bool HasReply = CopyTrimmedString(pRow->m_ReplyInput.GetString(), aReply, sizeof(aReply));
		if(!(HasTrigger && HasReply))
			continue;
		vCompleteRows.push_back({aTrigger, aReply, pRow->m_AutoRename != 0, pRow->m_Regex != 0});
	}

	if(vCompleteRows.size() != vRules.size())
		return false;

	for(size_t i = 0; i < vCompleteRows.size(); ++i)
	{
		if(str_comp(vCompleteRows[i].m_Keywords.c_str(), vRules[i].m_Keywords.c_str()) != 0 ||
			str_comp(vCompleteRows[i].m_Reply.c_str(), vRules[i].m_Reply.c_str()) != 0 ||
			vCompleteRows[i].m_AutoRename != vRules[i].m_AutoRename ||
			vCompleteRows[i].m_Regex != vRules[i].m_Regex)
			return false;
	}
	return true;
}

[[maybe_unused]] static bool IsAutoReplyRuleRowHalfFilled(const SAutoReplyRuleInputRow &Row)
{
	char aTrigger[512];
	char aReply[256];
	const bool HasTrigger = CopyTrimmedString(Row.m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
	const bool HasReply = CopyTrimmedString(Row.m_ReplyInput.GetString(), aReply, sizeof(aReply));
	return HasTrigger != HasReply;
}

[[maybe_unused]] static void BuildAutoReplyRulesFromRows(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, char *pOutRules, size_t OutRulesSize)
{
	pOutRules[0] = '\0';
	for(const auto &pRow : vRows)
	{
		char aTrigger[512];
		char aReply[256];
		const bool HasTrigger = CopyTrimmedString(pRow->m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
		const bool HasReply = CopyTrimmedString(pRow->m_ReplyInput.GetString(), aReply, sizeof(aReply));
		if(!(HasTrigger && HasReply))
			continue;

		if(pOutRules[0] != '\0')
			str_append(pOutRules, "\n", OutRulesSize);
		if(pRow->m_AutoRename != 0)
			str_append(pOutRules, "[rename] ", OutRulesSize);
		if(pRow->m_Regex != 0)
			str_append(pOutRules, "[regex] ", OutRulesSize);
		str_append(pOutRules, aTrigger, OutRulesSize);
		str_append(pOutRules, "=>", OutRulesSize);
		str_append(pOutRules, aReply, OutRulesSize);
	}
}

[[maybe_unused]] static float CalcQiaFenInputHeight(ITextRender *pTextRender, const char *pText, float Width, float TextFontSize, float LineSpacing, float MinHeight)
{
	const float VPadding = 2.0f;
	const float LineWidth = maximum(1.0f, Width - VPadding * 2.0f);
	const char *pMeasureText = (pText && pText[0] != '\0') ? pText : " ";
	const STextBoundingBox Box = pTextRender->TextBoundingBox(TextFontSize, pMeasureText, -1, LineWidth, LineSpacing);
	return maximum(MinHeight, Box.m_H + VPadding * 2.0f);
}

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

bool CMenus::DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand)
{
	CBindSlot Bind(0, 0);
	if(g_CommandBindCacheInitialized)
	{
		const auto It = g_CommandBindCache.find(pCommand);
		if(It != g_CommandBindCache.end())
			Bind = It->second;
	}
	else
	{
		for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
		{
			for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
			{
				const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
				if(!pBind[0])
					continue;

				if(str_comp(pBind, pCommand) == 0)
				{
					Bind.m_Key = KeyId;
					Bind.m_ModifierMask = Mod;
					break;
				}
			}
		}
	}

	CUIRect KeyButton, KeyLabel;
	View.HSplitTop(LineSize, &KeyButton, &View);
	KeyButton.VSplitMid(&KeyLabel, &KeyButton);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", pName);
	DoTClientLabel(Ui(), &KeyLabel, aBuf, FontSize, TEXTALIGN_ML);

	View.HSplitTop(MarginExtraSmall, nullptr, &View);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false, TCLIENT_BODY_FONT_SIZE);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		g_CommandBindCacheInitialized = false;
		return true;
	}
	return false;
}

bool CMenus::DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(pId);

	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = pStr;
	Options.m_pSuffix = pSuffix;
	Options.m_pScale = pScale;
	Options.m_Flags = Flags;
	Options.m_FontSize = CurrentSettingsContentMetrics().m_BodySize;
	Options.m_LabelAlign = TEXTALIGN_ML;
	Options.m_ValueMultiplier = Scale;

	IUiContext InputCtx = SettingsUiContext("tclient_slider_input", Options.m_FontSize / ui_token::font::BODY);

	return ui_widget::NumericField(InputCtx, pState, pId, pOption, Min, Max, *pRect, Options);
}

int CMenus::DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color, float FontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	if(Fake)
		Color.a *= 0.5f;

	pRect->Draw(Color, Corners, Rounding);

	Text.HMargin((Text.h - ButtonLineSize) / 2.0f, &Text);
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	const float EffectiveFontSize = FontSize > 0.0f ? FontSize : CurrentSettingsContentMetrics().m_BodySize;
	if(FontSize > 0.0f)
		DoTClientLabel(Ui(), &Text, pText, EffectiveFontSize, TEXTALIGN_MC);
	else
		Ui()->DoLabel(&Text, pText, EffectiveFontSize, TEXTALIGN_MC);

	if(Fake)
		return 0;

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody)
{
	bool WhiteFeetTemp = g_Config.m_TcWhiteFeet;
	g_Config.m_TcWhiteFeet = false;

	float DefTick = std::fmod(s_Time, 1.0f);

	CTeeRenderInfo SkinInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(pSkinName);
	if(str_comp(pSkin->GetName(), pSkinName) != 0)
		pSkin = GameClient()->m_Skins.Find(pBackupSkin);

	SkinInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	SkinInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	SkinInfo.m_SkinMetrics = pSkin->m_Metrics;
	SkinInfo.m_CustomColoredSkin = CustomColors;
	if(SkinInfo.m_CustomColoredSkin)
	{
		SkinInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		SkinInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		if(ColorFeet.a != 0.0f)
		{
			SkinInfo.m_ColorBody = ColorBody;
			SkinInfo.m_ColorFeet = ColorFeet;
		}
	}
	else
	{
		SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	if(Rainbow)
	{
		ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(DefTick, 1.0f, 0.5f));
		SkinInfo.m_ColorBody = Col;
		SkinInfo.m_ColorFeet = Col;
	}
	SkinInfo.m_Size = Size;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &SkinInfo, OffsetToMid);
	vec2 TeeRenderPos(RenderPos.x, RenderPos.y + OffsetToMid.y);
	if(Cute)
		RenderTeeCute(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos, true);
	else
		RenderTools()->RenderTee(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos);
	g_Config.m_TcWhiteFeet = WhiteFeetTemp;
}

void CMenus::RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha)
{
	Dir = Ui()->MousePos() - Pos;
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;
	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;
	RenderTools()->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}

void CMenus::RenderFontIcon(const CUIRect Rect, const char *pText, float Size, int Align)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	Ui()->DoLabel(&Rect, pText, Size, Align);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

int CMenus::DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(TextRender()->DefaultTextSelectionColor());
	if(Ui()->HotItem() == pButtonContainer)
	{
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect Temp;
	pRect->HMargin(0.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, CurrentSettingsContentMetrics().m_BodySize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::PopupConfirmRemoveWarType()
{
	GameClient()->m_WarList.RemoveWarType(m_pRemoveWarType->m_aWarName);
	++s_TClientWarListFilterRevision;
	m_pRemoveWarType = nullptr;
}

void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	if(!ReadOnly)
		EnsureSettingsBindCache();

	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	if(!ReadOnly)
	{
		s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
		if(!s_StartedTime)
		{
			s_StartedTime = true;
			s_Time = (float)rand() / (float)RAND_MAX;
		}
	}

	CUIRect TabBar, Button;
	int ActiveTab = m_TClientSettingsTab;
	int TabCount = NUMBER_OF_TCLIENT_TABS;
	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
		{
			TabCount--;
			if(ActiveTab == Tab)
				ActiveTab++;
		}
	}
	if(TabCount <= 0)
	{
		if(!ReadOnly)
			SetFlag(g_Config.m_TcTClientSettingsTabs, TCLIENT_TAB_INFO, false);
		TabCount = 1;
		ActiveTab = TCLIENT_TAB_INFO;
	}
	auto FirstVisibleTab = []() -> int {
		for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
			if(!IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
				return Tab;
		return TCLIENT_TAB_INFO;
	};
	if(ActiveTab < 0 || ActiveTab >= NUMBER_OF_TCLIENT_TABS || IsFlagSet(g_Config.m_TcTClientSettingsTabs, ActiveTab))
		ActiveTab = FirstVisibleTab();
	if(!ReadOnly)
		m_TClientSettingsTab = ActiveTab;

	MainView = TClientSettingsContentView(MainView, &TabBar);
	const float TabWidth = TabBar.w / TabCount;
	static CButtonContainer s_aPageTabs[NUMBER_OF_TCLIENT_TABS] = {};
	static const char *s_apTClientTabNames[NUMBER_OF_TCLIENT_TABS] = {};
	static char s_aTClientLanguageFile[IO_MAX_PATH_LENGTH] = {};
	static bool s_TClientTabNamesInitialized = false;
	if(!s_TClientTabNamesInitialized || str_comp(s_aTClientLanguageFile, g_Config.m_ClLanguagefile) != 0)
	{
		s_TClientTabNamesInitialized = true;
		str_copy(s_aTClientLanguageFile, g_Config.m_ClLanguagefile, sizeof(s_aTClientLanguageFile));
		if(!ReadOnly)
		{
			s_VisualFontLoader.InvalidateCache(ESettingsCacheDirtyReason::LANGUAGE);
			s_RightSectionLoader.InvalidateCache(ESettingsCacheDirtyReason::LANGUAGE);
		}
		s_apTClientTabNames[TCLIENT_TAB_SETTINGS] = Localize("Settings");
		s_apTClientTabNames[TCLIENT_TAB_BINDWHEEL] = Localize("Bind Wheel");
		s_apTClientTabNames[TCLIENT_TAB_WARLIST] = Localize("War List");
		s_apTClientTabNames[TCLIENT_TAB_BINDCHAT] = Localize("Chat Binds");
		s_apTClientTabNames[TCLIENT_TAB_STATUSBAR] = Localize("Status Bar");
		s_apTClientTabNames[TCLIENT_TAB_INFO] = Localize("Info");
	}

	int VisibleTabIndex = 0;
	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = VisibleTabIndex == 0 ? IGraphics::CORNER_L : VisibleTabIndex == TabCount - 1 ? IGraphics::CORNER_R :
														   IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f) && !ReadOnly)
		{
			m_TClientSettingsTab = Tab;
			ActiveTab = Tab;
		}
		++VisibleTabIndex;
	}

	CUIRect ContentView = MainView;
	// 子 Tab 的入场由设置 Card Deck 统一处理，避免和页面级位移动效叠加。
	const bool TransitionActive = false;

	{
		CPerfTimer StageTimer;
		if(ActiveTab == TCLIENT_TAB_SETTINGS)
		{
			RenderSettingsTClientSettings(ContentView, ReadOnly);
		}
		if(ActiveTab == TCLIENT_TAB_BINDCHAT)
			RenderSettingsTClientChatBinds(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_BINDWHEEL)
			RenderSettingsTClientBindWheel(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_WARLIST)
			RenderSettingsTClientWarList(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_STATUSBAR)
			RenderSettingsTClientStatusBar(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_INFO)
			RenderSettingsTClientInfo(ContentView, ReadOnly);
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", ActiveTab, TransitionActive ? 1 : 0);
		LogTClientPerfStageEx("tclient_tab", nullptr, ETClientSettingsPerfStage::TAB_SHELL, StageTimer.ElapsedMs(), TransitionActive, aExtra);
		const char *pTabShellStage = nullptr;
		switch(ActiveTab)
		{
		case TCLIENT_TAB_BINDCHAT: pTabShellStage = "tclient_tab_3_shell"; break;
		case TCLIENT_TAB_STATUSBAR: pTabShellStage = "tclient_tab_4_shell"; break;
		case TCLIENT_TAB_INFO: pTabShellStage = "tclient_tab_5_shell"; break;
		default: break;
		}
		if(pTabShellStage != nullptr)
			LogTClientPerfStage(pTabShellStage, StageTimer.ElapsedMs(), TransitionActive, aExtra);
		LogTClientPerfStage("tclient_tab_content", StageTimer.ElapsedMs(), TransitionActive, aExtra);
	}

	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", ActiveTab, TransitionActive ? 1 : 0);
	LogTClientPerfStage("tclient_page_total", RenderTimer.ElapsedMs(), false, aExtra);
	if(!ReadOnly)
	{
		m_SettingsRuntimeMetadata.m_LastTClientTab = ActiveTab;
		m_SettingsRuntimeMetadata.m_Valid = true;
	}
}

void CMenus::ConfigureSettingsCardSection(SSettingsSection &Section, const char *pTitle, const char *pStableCardId, std::function<float(CUIRect &, bool)> LayoutSection, float TopMargin)
{
	Section.m_pStableCardId = pStableCardId;
	const float LegacyHeaderHeight = TopMargin + HeadlineHeight + MarginSmall;
	Section.m_MeasureFn = [LayoutSection, LegacyHeaderHeight](CUIRect &Col) -> float {
		const float SavedY = Col.y;
		CUIRect LegacyContent = Col;
		LayoutSection(LegacyContent, false);
		Col.y = LegacyContent.y - LegacyHeaderHeight;
		return Col.y - SavedY;
	};
	Section.m_RenderCompactFn = [this, LayoutSection, LegacyHeaderHeight](CUIRect &Col) -> float {
		const float SavedY = Col.y;
		CUIRect LegacyContent = Col;
		LegacyContent.y -= LegacyHeaderHeight;
		LegacyContent.h += LegacyHeaderHeight;
		const float UiScale = CurrentSettingsContentMetrics().m_UiScale;
		const CUIRect SectionClip = ResolveSettingsFocusSafeClipRect(Col, UiScale);
		Ui()->ClipEnable(&SectionClip);
		LayoutSection(LegacyContent, true);
		Ui()->ClipDisable();
		Col.y = LegacyContent.y;
		return Col.y - SavedY;
	};
	Section.m_RenderFullFn = Section.m_RenderCompactFn;
}

float CMenus::LayoutTClientThemeCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, Button, TmpLabel;
	const float SavedY = CurrentColumn.y;
	CUIRect BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
	if(Render)
	{
		CUIElement &TitleElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-visual-font-cursor-title");
		DoSettingsLabelStreamed(TitleElement, &Label, Localize("Visual: Font & Cursor"), HeadlineFontSize, TEXTALIGN_ML, TClientFixedLabelProperties(HeadlineFontSize, Label.w));
	}
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);

	Button = Rows.Next();
	if(Render)
	{
		Button.VSplitLeft(100.0f, &Label, &Button);
		CUIElement &CustomFontElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-custom-font-label");
		DoSettingsLabelStreamed(CustomFontElement, &Label, Localize("Custom Font:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
		static std::vector<std::string> s_FontDropDownNamesOwned;
		static std::vector<const char *> s_FontDropDownNames;
		static CUi::SDropDownState s_FontDropDownState;
		static CScrollRegion s_FontDropDownScrollRegion;
		s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
		s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
		const auto &CustomFaces = *TextRender()->GetCustomFaces();
		if(s_FontDropDownNamesOwned != CustomFaces)
		{
			s_FontDropDownNamesOwned = CustomFaces;
			s_FontDropDownNames.clear();
			s_FontDropDownNames.reserve(s_FontDropDownNamesOwned.size());
			for(const auto &FaceName : s_FontDropDownNamesOwned)
				s_FontDropDownNames.push_back(FaceName.c_str());
		}
		int FontSelectedOld = -1;
		for(size_t i = 0; i < CustomFaces.size(); ++i)
		{
			if(str_find_nocase(g_Config.m_TcCustomFont, CustomFaces[i].c_str()))
				FontSelectedOld = (int)i;
		}
		CUIRect FontDirectory;
		Button.VSplitRight(20.0f, &Button, &FontDirectory);
		Button.VSplitRight(MarginSmall, &Button, nullptr);
		const int FontSelectedNew = DoSettingsDropDown(&Button, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
		if(FontSelectedOld != FontSelectedNew && FontSelectedNew >= 0 && (size_t)FontSelectedNew < s_FontDropDownNames.size())
		{
			str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
			s_VisualFontLoader.InvalidateCache(ESettingsCacheDirtyReason::FONT);
			s_RightSectionLoader.InvalidateCache(ESettingsCacheDirtyReason::FONT);
			TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
			InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
			TextRender()->OnPreWindowResize();
			GameClient()->OnWindowResize();
			GameClient()->Editor()->OnWindowResize();
			TextRender()->OnWindowResize();
			GameClient()->m_MapImages.SetTextureScale(101);
			GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
		}
		static CButtonContainer s_FontDirectoryId;
		if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FONT_ICON_FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
		{
			Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE);
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/fonts", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
	}
	Button = Rows.Next();
	if(Render)
	{
		Button.VSplitLeft(120.0f, &Label, &Button);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Hammer Mode:"), FontSize, TEXTALIGN_ML);
		static std::vector<const char *> s_DropDownNames;
		s_DropDownNames = {Localize("Normal", "Hammer Mode"), Localize("Rotate with cursor", "Hammer Mode"), Localize("Rotate with cursor like gun", "Hammer Mode")};
		static CUi::SDropDownState s_DropDownState;
		static CScrollRegion s_DropDownScrollRegion;
		s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
		g_Config.m_TcHammerRotatesWithCursor = DoSettingsDropDown(&Button, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
	}
	Button = Rows.Next();
	if(Render)
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-cursor-scale", &g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, Localize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Button = Rows.Next();
	if(Render)
	{
		if(g_Config.m_TcAnimateWheelTime > 0)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-wheel-animate-ms", &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
		else
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-wheel-animate-ms", &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");
	}
	BoxRect.h = CurrentColumn.y - BoxRect.y;
	return CurrentColumn.y - SavedY;
}

float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, ReplyRect, TmpRect;
	CUIRect BoxRect;
	IUiContext TClientAutoReplyTextInputCtx;
	TClientAutoReplyTextInputCtx.m_pUi = Ui();
	TClientAutoReplyTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientAutoReplyTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientAutoReplyTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_reply_text_inputs");
	TClientAutoReplyTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
	if(Render)
	{
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-auto-reply-title", &Label, Localize("Auto reply"), HeadlineFontSize, TEXTALIGN_ML);
	}
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);

	CUIRect MutedToggle = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, "tclient-auto-reply-muted", Localize("Automatically reply to muted players"), &g_Config.m_TcAutoReplyMuted, &MutedToggle, LineSize);
	if(g_Config.m_TcAutoReplyMuted)
	{
		ReplyRect = Rows.Next();
		if(Render)
		{
			static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
			s_MutedReply.SetEmptyText(Localize("I muted you"));
			ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);
		}
	}
	CUIRect MinimizedToggle = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, "tclient-auto-reply-minimized", Localize("Automatically reply while the window is unfocused"), &g_Config.m_TcAutoReplyMinimized, &MinimizedToggle, LineSize);
	if(g_Config.m_TcAutoReplyMinimized)
	{
		ReplyRect = Rows.Next();
		if(Render)
		{
			static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
			s_MinimizedReply.SetEmptyText(Localize("I am away from the game window"));
			ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);
		}
	}
	return CurrentColumn.y - SavedY;
}

float CMenus::LayoutTClientPetCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, Button, TmpRect, PetSkinBox;
	CUIRect BoxRect;
	IUiContext TClientPetTextInputCtx;
	TClientPetTextInputCtx.m_pUi = Ui();
	TClientPetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientPetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientPetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_pet_text_inputs");
	TClientPetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
	if(Render)
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);
	CUIRect ShowPetRow = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, "tclient-show-pet", Localize("Show the pet"), &g_Config.m_TcPetShow, &ShowPetRow, LineSize);
	Button = Rows.Next();
	if(Render)
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-pet-size", &g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, Localize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Button = Rows.Next();
	if(Render)
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-pet-alpha", &g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, Localize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	PetSkinBox = Rows.Next();
	if(Render)
	{
		PetSkinBox.VSplitMid(&Label, &Button);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet Skin:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
		ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);
	}
	return CurrentColumn.y - SavedY;
}

float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, Button, NotificationConfig, TmpRect;
	CUIRect BoxRect;
	IUiContext TClientHudTextInputCtx;
	TClientHudTextInputCtx.m_pUi = Ui();
	TClientHudTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TClientHudTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TClientHudTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_hud_text_inputs");
	TClientHudTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const float SavedY = CurrentColumn.y;
	CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
	if(Render)
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);
	CUIRect Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, "tclient-mini-vote-hud", Localize("Show compact vote HUD"), &g_Config.m_TcMiniVoteHud, &Row, LineSize);
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, "tclient-mini-debug", Localize("Show position and angle (mini debug)"), &g_Config.m_TcMiniDebug, &Row, LineSize);
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, "tclient-render-cursor-spec", Localize("Show the cursor while free spectating"), &g_Config.m_TcRenderCursorSpec, &Row, LineSize);
	if(g_Config.m_TcRenderCursorSpec)
	{
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-freeview-cursor-opacity", &g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, Localize("Freeview cursor opacity"), 0, 100);
	}
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, "tclient-notify-when-last", Localize("Notify when only one tee is still alive:"), &g_Config.m_TcNotifyWhenLast, &Row, LineSize);
	if(g_Config.m_TcNotifyWhenLast)
	{
		NotificationConfig = Rows.Next();
		const CUIRect NotificationX = Rows.Next();
		const CUIRect NotificationY = Rows.Next();
		const CUIRect NotificationSize = Rows.Next();
		if(Render)
		{
			NotificationConfig.VSplitMid(&Button, &NotificationConfig);
			static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
			s_LastInput.SetEmptyText(Localize("You're the last one!"));
			ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);
			static CButtonContainer s_ClientNotifyWhenLastColor;
			DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, CurrentSettingsContentMetrics(), &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-x", &g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &NotificationX, Localize("Horizontal position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-y", &g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &NotificationY, Localize("Vertical position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-size", &g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &NotificationSize, Localize("Font size"), 1, 50);
		}
	}
	Row = Rows.Next();
	if(Render)
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, "tclient-show-center-line", Localize("Show the screen center line"), &g_Config.m_TcShowCenter, &Row, LineSize);
	if(g_Config.m_TcShowCenter)
	{
		CUIRect CenterColor = Rows.Next();
		const CUIRect CenterWidth = Rows.Next();
		if(Render)
		{
			static CButtonContainer s_ShowCenterLineColor;
			DoLine_ColorPicker(&s_ShowCenterLineColor, CurrentSettingsContentMetrics(), &CenterColor, Localize("Screen center line color"), &g_Config.m_TcShowCenterColor, DefaultConfig::TcShowCenterColor, false, nullptr, true);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-center-line-width", &g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &CenterWidth, Localize("Screen center line width"), 0, 20);
		}
	}
	return CurrentColumn.y - SavedY;
}

SSettingsSection CMenus::BuildTClientThemeCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Visual: Font & Cursor";
	ConfigureSettingsCardSection(S, Localizable("Visual: Font & Cursor"), "tclient:visual-font-cursor", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientThemeCacheSection(Col, Render); }, Margin);
	S.m_DependencyConfigInts = {&g_Config.m_TcCursorScale, &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcHammerRotatesWithCursor};
	return S;
}

SSettingsSection CMenus::BuildTClientAutoReplyCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Auto reply";
	ConfigureSettingsCardSection(S, Localizable("Auto reply"), "tclient:auto-reply", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientAutoReplyCacheSection(Col, Render); }, MarginBetweenSections);
	S.m_DependencyConfigInts = {&g_Config.m_TcAutoReplyMuted, &g_Config.m_TcAutoReplyMinimized};
	return S;
}

SSettingsSection CMenus::BuildTClientPetCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Pet";
	ConfigureSettingsCardSection(S, "Pet", "tclient:pet", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientPetCacheSection(Col, Render); }, MarginBetweenSections);
	S.m_DependencyConfigInts = {&g_Config.m_TcPetShow, &g_Config.m_TcPetSize, &g_Config.m_TcPetAlpha};
	return S;
}

SSettingsSection CMenus::BuildTClientHudCacheSection()
{
	SSettingsSection S;
	S.m_pName = "HUD";
	ConfigureSettingsCardSection(S, "HUD", "tclient:hud", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientHudCacheSection(Col, Render); }, Margin);
	S.m_DependencyConfigInts = {
		&g_Config.m_TcMiniVoteHud,
		&g_Config.m_TcMiniDebug,
		&g_Config.m_TcRenderCursorSpec,
		&g_Config.m_TcNotifyWhenLast,
		&g_Config.m_TcNotifyWhenLastX,
		&g_Config.m_TcNotifyWhenLastY,
		&g_Config.m_TcNotifyWhenLastSize,
		&g_Config.m_TcShowCenter,
		&g_Config.m_TcShowCenterWidth,
	};
	S.m_DependencyConfigCols = {&g_Config.m_TcNotifyWhenLastColor, &g_Config.m_TcShowCenterColor};
	return S;
}

std::vector<SSettingsSection> CMenus::BuildTClientLeftCacheSections()
{
	std::vector<SSettingsSection> vSections;
	vSections.push_back(BuildTClientThemeCacheSection());
	vSections.push_back(BuildTClientAutoReplyCacheSection());
	vSections.push_back(BuildTClientPetCacheSection());
	return vSections;
}

std::vector<SSettingsSection> CMenus::BuildTClientRightCacheSections()
{
	std::vector<SSettingsSection> vSections;
	vSections.push_back(BuildTClientHudCacheSection());
	return vSections;
}

void CMenus::InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason Reason)
{
	s_VisualFontLoader.InvalidateCache(Reason);
	s_RightSectionLoader.InvalidateCache(Reason);
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	CPerfTimer LayoutBudgetTimer;
	CUIRect Column, LeftView, RightView, Button, Label;
	const CUIRect Viewport = MainView;
	const bool TClientVisibleTargetFrame = !ReadOnly;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	static CScrollRegion s_TClientSettingsScrollRegion;
	static CSectionLoader s_VisualFontReadOnlyLoader;
	static CSectionLoader s_RightSectionReadOnlyLoader;
	CSectionLoader &VisualFontLoader = ReadOnly ? s_VisualFontReadOnlyLoader : s_VisualFontLoader;
	CSectionLoader &RightSectionLoader = ReadOnly ? s_RightSectionReadOnlyLoader : s_RightSectionLoader;
	vec2 ScrollOffset(0.0f, 0.0f);
	auto LogSettingsStage = [&](const char *pStage, const CPerfTimer &Timer) {
		char aExtra[192];
		str_format(aExtra, sizeof(aExtra), "frame=%" PRIu64 " operation=%s page=settings:tclient tab=%d subtab=%d scroll_y=%.1f",
			(uint64_t)Client()->PerfFrame(), SettingsPerfActiveOperation(), m_TClientSettingsTab, m_TClientSettingsTab, ScrollOffset.y);
		LogTClientPerfStage(pStage, Timer.ElapsedMs(), false, aExtra);
	};
	auto LogTClientSectionHeightConsistency = [&](const char *pSection, float MeasuredHeight, float RenderedHeight) {
		const float HeightDelta = RenderedHeight - MeasuredHeight;
		const bool HeightStable = absolute(HeightDelta) <= 0.01f;
		char aExtra[256];
		str_format(aExtra, sizeof(aExtra), "frame=%" PRIu64 " operation=%s page=settings:tclient tab=%d subtab=%d section=%s section_height_measured=%.3f section_height_rendered=%.3f height_delta=%.3f stable=%d",
			(uint64_t)Client()->PerfFrame(), SettingsPerfActiveOperation(), m_TClientSettingsTab, m_TClientSettingsTab,
			pSection != nullptr ? pSection : "unknown", MeasuredHeight, RenderedHeight, HeightDelta, HeightStable ? 1 : 0);
		LogTClientPerfStage("tclient_settings_section_height", 0.0, !HeightStable, aExtra);
	};
	{
		CPerfTimer StageTimer;
		if(!ReadOnly)
		{
			if(m_SettingsTClientScrollRestorePending)
			{
				s_TClientSettingsScrollRegion.SetScrollOffsetY(m_SettingsRuntimeMetadata.m_LastScrollY);
				m_SettingsTClientScrollRestorePending = false;
			}
			ScrollOffset.y = s_TClientSettingsScrollRegion.ContentScrollOffsetY();
			m_SettingsTClientCurrentScrollY = ScrollOffset.y;
		}
		else if(m_SettingsRuntimeMetadata.m_Valid)
		{
			ScrollOffset.y = m_SettingsRuntimeMetadata.m_LastScrollY;
		}
		LogSettingsStage("tclient_settings_scroll_begin", StageTimer);
	}

	const SSectionCullContext CullContext{
		Viewport.y,
		Viewport.y + Viewport.h,
		720.0f,
	};
	auto ShouldRenderSection = [&](const CUIRect &CurrentColumn, float TopPadding, float EstimatedHeight) {
		CUIRect SectionRect = CurrentColumn;
		if(TopPadding > 0.0f)
			SectionRect.HSplitTop(TopPadding, nullptr, &SectionRect);
		SectionRect.HSplitTop(EstimatedHeight, &SectionRect, nullptr);
		return IsSectionVisible(SectionRect, CullContext);
	};
	auto SkipSection = [&](CUIRect &CurrentColumn, float TopPadding, float EstimatedHeight) {
		CurrentColumn.HSplitTop(TopPadding + EstimatedHeight, nullptr, &CurrentColumn);
	};
	auto RenderBoxedFullSection = [&](const char *pSectionName, auto &LayoutSection, CUIRect &Col) -> float {
		const float SavedY = Col.y;
		CUIRect MeasuredContent = Col;
		const CUIRect BoxRect = LayoutSection(MeasuredContent, false);
		const float LegacyHeaderHeight = BoxRect.y - SavedY + HeadlineHeight + MarginSmall;
		const float MeasuredHeight = MeasuredContent.y - SavedY - LegacyHeaderHeight;
		CUIRect LegacyContent = Col;
		LegacyContent.y -= LegacyHeaderHeight;
		LegacyContent.h += LegacyHeaderHeight;
		const CUIRect SectionClip = ResolveSettingsFocusSafeClipRect(Col, UiScale);
		Ui()->ClipEnable(&SectionClip);
		LayoutSection(LegacyContent, true);
		Ui()->ClipDisable();
		Col.y = LegacyContent.y;
		const float RenderedHeight = Col.y - SavedY;
		LogTClientSectionHeightConsistency(pSectionName, MeasuredHeight, RenderedHeight);
		return RenderedHeight;
	};
	auto FillCachedStaticLayer = [&](SSettingsSection &Section, auto &LayoutSection) {
		Section.m_MeasureFn = [LayoutSection](CUIRect &Col) -> float {
			const float SavedY = Col.y;
			CUIRect LegacyContent = Col;
			const CUIRect BoxRect = LayoutSection(LegacyContent, false);
			const float LegacyHeaderHeight = BoxRect.y - SavedY + HeadlineHeight + MarginSmall;
			Col.y = LegacyContent.y - LegacyHeaderHeight;
			return Col.y - SavedY;
		};
		Section.m_RenderCompactFn = [this, LayoutSection, &LogTClientSectionHeightConsistency, SectionName = Section.m_pName, UiScale](CUIRect &Col) -> float {
			const float SavedY = Col.y;
			CUIRect MeasuredContent = Col;
			const CUIRect BoxRect = LayoutSection(MeasuredContent, false);
			const float LegacyHeaderHeight = BoxRect.y - SavedY + HeadlineHeight + MarginSmall;
			const float MeasuredHeight = MeasuredContent.y - SavedY - LegacyHeaderHeight;
			CUIRect LegacyContent = Col;
			LegacyContent.y -= LegacyHeaderHeight;
			LegacyContent.h += LegacyHeaderHeight;
			const CUIRect SectionClip = ResolveSettingsFocusSafeClipRect(Col, UiScale);
			Ui()->ClipEnable(&SectionClip);
			LayoutSection(LegacyContent, true);
			Ui()->ClipDisable();
			Col.y = LegacyContent.y;
			const float RenderedHeight = Col.y - SavedY;
			LogTClientSectionHeightConsistency(SectionName, MeasuredHeight, RenderedHeight);
			return RenderedHeight;
		};
		Section.m_RenderFullFn = Section.m_RenderCompactFn;
	};
	static std::array<CTClientSettingsCardFrameBinding, 19> s_aDeckCardBindings;
	size_t DeckCardBindingIndex = 0;
	auto AppendDeckCards = [&](std::vector<SSettingsSection> &vSections) {
		if(ReadOnly)
			return;
		for(SSettingsSection &Section : vSections)
		{
			if(Section.m_pStableCardId == nullptr || Section.m_pStableCardId[0] == '\0')
				continue;
			dbg_assert(DeckCardBindingIndex < s_aDeckCardBindings.size(), "too many TClient settings cards");
			if(DeckCardBindingIndex < s_aDeckCardBindings.size())
				s_aDeckCardBindings[DeckCardBindingIndex++].BindSection(Section);
		}
	};
	[[maybe_unused]] auto CalcHudSectionHeight = [&]() {
		float Height = 0.0f;
		Height += HeadlineHeight + MarginSmall;
		Height += LineSize * 5.0f;
		Height += LineSize + MarginSmall;
		Height += LineSize * 3.0f;
		Height += LineSize;
		Height += LineSize + MarginSmall;
		Height += LineSize;
		Height += MarginExtraSmall;
		return Height;
	};
	[[maybe_unused]] auto CalcTeeStatusBarSectionHeight = [&]() {
		return HeadlineHeight + MarginSmall + LineSize * 7.0f;
	};
	[[maybe_unused]] static float s_InputSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AntiLatencyToolsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AntiPingSmoothingSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AutoExecuteSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VotingSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AutoReplySectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_PlayerIndicatorSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_PetSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VisualFontSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VisualNameplateSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VisualEffectsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_HudSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_TeeStatusBarSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_GhostToolsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_RainbowSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_TeeTrailsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_BackgroundDrawSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_FinishNameSectionCachedHeight = 0.0f;

	LeftView = Page.m_aColumns[0];
	RightView = Page.m_aColumns[1];
	const SSettingsSectionCacheRuntimeKey LiveRuntimeKey = MakeSettingsSectionRuntimeKey(LeftView, Graphics(), false);

	// Initialize VisualFont section loader for this frame
	VisualFontLoader.SetRuntimeKey(LiveRuntimeKey);
	VisualFontLoader.SetProgressiveEnabled(TClientVisibleTargetFrame);
	VisualFontLoader.SetMaxSectionsPerFrame(TClientVisibleTargetFrame ? 1 : 2);
	VisualFontLoader.SetDeferredFarMeasurementEnabled(true);
	CUIRect LeftLoaderViewport = LeftView;
	LeftLoaderViewport.y -= ScrollOffset.y;
	VisualFontLoader.Begin(LeftView, LeftLoaderViewport, 5.0f);

	// ***** LeftView ***** //
	{
		CPerfTimer LeftColumnTimer;
		CPerfTimer VisualSectionsTotalTimer;
		Column = LeftView;
		auto LayoutVisualFontSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect = CurrentColumn;
			CUIRect TmpLabel;
			auto ShouldRenderVisualBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			[[maybe_unused]] auto SkipVisualBlock = [&](float Height) {
				SkipSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-visual-font-cursor-title", &Label, Localize("Visual: Font & Cursor"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			const bool RenderFontDropdown = Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize);
			if(RenderFontDropdown)
			{
				CUIRect FontDropDownRect;
				CurrentColumn.HSplitTop(LineSize, &FontDropDownRect, &CurrentColumn);
				FontDropDownRect.VSplitLeft(100.0f, &Label, &FontDropDownRect);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Custom Font:"), FontSize, TEXTALIGN_ML);
				static std::vector<std::string> s_FontDropDownNamesOwned;
				static std::vector<const char *> s_FontDropDownNames;
				static CUi::SDropDownState s_FontDropDownState;
				static CScrollRegion s_FontDropDownScrollRegion;
				s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
				s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
				const auto &CustomFaces = *TextRender()->GetCustomFaces();
				{
					CPerfTimer StageTimer;
					const bool FacesChanged = s_FontDropDownNamesOwned != CustomFaces;
					if(FacesChanged)
					{
						s_FontDropDownNamesOwned = CustomFaces;
						s_FontDropDownNames.clear();
						s_FontDropDownNames.reserve(s_FontDropDownNamesOwned.size());
						for(const auto &FaceName : s_FontDropDownNamesOwned)
							s_FontDropDownNames.push_back(FaceName.c_str());
					}
					char aExtra[96];
					str_format(aExtra, sizeof(aExtra), "faces=%d changed=%d", (int)CustomFaces.size(), FacesChanged ? 1 : 0);
					LogTClientPerfStage("tclient_font_faces_sync", StageTimer.ElapsedMs(), FacesChanged, aExtra);
				}
				{
					CPerfTimer FontDropDownTimer;
					int FontSelectedOld = -1;
					for(size_t i = 0; i < CustomFaces.size(); ++i)
					{
						if(str_find_nocase(g_Config.m_TcCustomFont, CustomFaces[i].c_str()))
							FontSelectedOld = i;
					}
					CUIRect FontDirectory;
					FontDropDownRect.VSplitRight(20.0f, &FontDropDownRect, &FontDirectory);
					FontDropDownRect.VSplitRight(MarginSmall, &FontDropDownRect, nullptr);

					const int FontSelectedNew = DoSettingsDropDown(&FontDropDownRect, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
					if(FontSelectedOld != FontSelectedNew && FontSelectedNew >= 0 && (size_t)FontSelectedNew < s_FontDropDownNames.size())
					{
						str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
						VisualFontLoader.InvalidateCache(ESettingsCacheDirtyReason::FONT);
						RightSectionLoader.InvalidateCache(ESettingsCacheDirtyReason::FONT);
						TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
						InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
						TextRender()->OnPreWindowResize();
						GameClient()->OnWindowResize();
						GameClient()->Editor()->OnWindowResize();
						TextRender()->OnWindowResize();
						GameClient()->m_MapImages.SetTextureScale(101);
						GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
					}

					static CButtonContainer s_FontDirectoryId;
					if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FONT_ICON_FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
					{
						Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
						Storage()->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE);
						char aBuf[IO_MAX_PATH_LENGTH];
						Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/fonts", aBuf, sizeof(aBuf));
						Client()->ViewFile(aBuf);
					}
					LogSettingsStage("tclient_settings_left_visual_font_dropdown", FontDropDownTimer);
				}
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, LineSize);
			}

			if(ShouldRenderVisualBlock(MarginExtraSmall * 2.0f + LineSize))
			{
				CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
				CUIRect DropDownRect;
				CurrentColumn.HSplitTop(LineSize, &DropDownRect, &CurrentColumn);
				DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-hammer-mode", &Label, Localize("Hammer Mode:"), FontSize, TEXTALIGN_ML);
				CPerfTimer HammerModeTimer;
				static std::vector<const char *> s_DropDownNames;
				s_DropDownNames = {Localize("Normal", "Hammer Mode"), Localize("Rotate with cursor", "Hammer Mode"), Localize("Rotate with cursor like gun", "Hammer Mode")};
				static CUi::SDropDownState s_DropDownState;
				static CScrollRegion s_DropDownScrollRegion;
				s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
				g_Config.m_TcHammerRotatesWithCursor = DoSettingsDropDown(&DropDownRect, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
				LogSettingsStage("tclient_settings_left_visual_hammer_mode", HammerModeTimer);
				CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			}
			else
			{
				SkipVisualBlock(MarginExtraSmall * 2.0f + LineSize);
			}

			if(ShouldRenderVisualBlock(LineSize * 2.0f))
			{
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-cursor-scale", &g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, Localize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				if(g_Config.m_TcAnimateWheelTime > 0)
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-wheel-animate-ms", &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
				else
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-wheel-animate-off", &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");
			}
			else
			{
				SkipVisualBlock(LineSize * 2.0f);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		[[maybe_unused]] auto MeasureVisualFontSection = [&](CUIRect &CurrentColumn) -> float {
			const float SavedY = CurrentColumn.y;
			LayoutVisualFontSection(CurrentColumn, false);
			return CurrentColumn.y - SavedY;
		};
		[[maybe_unused]] auto RenderVisualFontInteractiveSection = [&](CUIRect &CurrentColumn) {
			const bool RenderFontDropdown = ShouldRenderSection(CurrentColumn, 0.0f, LineSize);
			if(RenderFontDropdown)
			{
				CUIRect FontDropDownRect;
				CurrentColumn.HSplitTop(LineSize, &FontDropDownRect, &CurrentColumn);
				FontDropDownRect.VSplitLeft(100.0f, &Label, &FontDropDownRect);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Custom Font:"), FontSize, TEXTALIGN_ML);
				static std::vector<std::string> s_FontDropDownNamesOwned;
				static std::vector<const char *> s_FontDropDownNames;
				static CUi::SDropDownState s_FontDropDownState;
				static CScrollRegion s_FontDropDownScrollRegion;
				s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
				s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
				const auto &CustomFaces = *TextRender()->GetCustomFaces();
				const bool FacesChanged = s_FontDropDownNamesOwned != CustomFaces;
				if(FacesChanged)
				{
					s_FontDropDownNamesOwned = CustomFaces;
					s_FontDropDownNames.clear();
					s_FontDropDownNames.reserve(s_FontDropDownNamesOwned.size());
					for(const auto &FaceName : s_FontDropDownNamesOwned)
						s_FontDropDownNames.push_back(FaceName.c_str());
				}
				int FontSelectedOld = -1;
				for(size_t i = 0; i < CustomFaces.size(); ++i)
				{
					if(str_find_nocase(g_Config.m_TcCustomFont, CustomFaces[i].c_str()))
						FontSelectedOld = i;
				}
				CUIRect FontDirectory;
				FontDropDownRect.VSplitRight(20.0f, &FontDropDownRect, &FontDirectory);
				FontDropDownRect.VSplitRight(MarginSmall, &FontDropDownRect, nullptr);
				const int FontSelectedNew = DoSettingsDropDown(&FontDropDownRect, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
				if(FontSelectedOld != FontSelectedNew && FontSelectedNew >= 0 && (size_t)FontSelectedNew < s_FontDropDownNames.size())
				{
					str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
					VisualFontLoader.InvalidateCache(ESettingsCacheDirtyReason::FONT);
					RightSectionLoader.InvalidateCache(ESettingsCacheDirtyReason::FONT);
					TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
					TextRender()->OnPreWindowResize();
					GameClient()->OnWindowResize();
					GameClient()->Editor()->OnWindowResize();
					TextRender()->OnWindowResize();
					GameClient()->m_MapImages.SetTextureScale(101);
					GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
				}
				static CButtonContainer s_FontDirectoryId;
				if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FONT_ICON_FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
				{
					Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
					Storage()->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE);
					char aBuf[IO_MAX_PATH_LENGTH];
					Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/fonts", aBuf, sizeof(aBuf));
					Client()->ViewFile(aBuf);
				}
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, LineSize);
			}

			const bool RenderHammerBlock = ShouldRenderSection(CurrentColumn, 0.0f, MarginExtraSmall * 2.0f + LineSize);
			if(RenderHammerBlock)
			{
				CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
				CUIRect DropDownRect;
				CurrentColumn.HSplitTop(LineSize, &DropDownRect, &CurrentColumn);
				DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Hammer Mode:"), FontSize, TEXTALIGN_ML);
				static std::vector<const char *> s_DropDownNames;
				s_DropDownNames = {Localize("Normal", "Hammer Mode"), Localize("Rotate with cursor", "Hammer Mode"), Localize("Rotate with cursor like gun", "Hammer Mode")};
				static CUi::SDropDownState s_DropDownState;
				static CScrollRegion s_DropDownScrollRegion;
				s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
				g_Config.m_TcHammerRotatesWithCursor = DoSettingsDropDown(&DropDownRect, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
				CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, MarginExtraSmall * 2.0f + LineSize);
			}

			if(ShouldRenderSection(CurrentColumn, 0.0f, LineSize * 2.0f))
			{
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-cursor-scale", &g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, Localize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				if(g_Config.m_TcAnimateWheelTime > 0)
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-wheel-animate-ms", &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
				else
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-wheel-animate-ms", &g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, LineSize * 2.0f);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
		};

		auto LayoutVisualNameplateSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect = CurrentColumn;
			CUIRect TmpLabel;
			IUiContext TClientWhiteFeetTextInputCtx;
			TClientWhiteFeetTextInputCtx.m_pUi = Ui();
			TClientWhiteFeetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientWhiteFeetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientWhiteFeetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_white_feet_text_inputs");
			TClientWhiteFeetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			auto ShouldRenderVisualBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			[[maybe_unused]] auto SkipVisualBlock = [&](float Height) {
				SkipSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Visual: Nameplates"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			const int NameplateRowCount = 7 + (g_Config.m_TcWhiteFeet ? 1 : 0);
			const bool RenderNameplateRows = ShouldRenderVisualBlock(TClientSettingsRowsHeight(NameplateRowCount));
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect PingCircleRow = Rows.Next();
			CUIRect CountryRow = Rows.Next();
			CUIRect SkinsRow = Rows.Next();
			CUIRect FreezeStarsRow = Rows.Next();
			CUIRect ColorFreezeRow = Rows.Next();
			CUIRect FreezeKatanaRow = Rows.Next();
			CUIRect WhiteFeetRow = Rows.Next();
			CUIRect FeetBox;
			if(g_Config.m_TcWhiteFeet)
				FeetBox = Rows.Next();
			if(RenderNameplateRows)
			{
				CPerfTimer NameplateTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplatePingCircle, "tclient-nameplate-ping-circle", Localize("Show ping colored circle in nameplates"), &g_Config.m_TcNameplatePingCircle, &PingCircleRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateCountry, "tclient-nameplate-country", Localize("Show country flags in nameplates"), &g_Config.m_TcNameplateCountry, &CountryRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateSkins, "tclient-nameplate-skins", Localize("Show skin names in nameplate"), &g_Config.m_TcNameplateSkins, &SkinsRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, "tclient-freeze-stars", Localize("Freeze stars"), &g_Config.m_ClFreezeStars, &FreezeStarsRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, "tclient-color-freeze", Localize("Use colored skins for frozen tees"), &g_Config.m_TcColorFreeze, &ColorFreezeRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeKatana, "tclient-freeze-katana", Localize("Show katan on frozen players"), &g_Config.m_TcFreezeKatana, &FreezeKatanaRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWhiteFeet, "tclient-white-feet", Localize("Render all custom colored feet as white feet skin"), &g_Config.m_TcWhiteFeet, &WhiteFeetRow, LineSize);
				LogSettingsStage("tclient_settings_left_visual_nameplates", NameplateTimer);
			}
			if(RenderNameplateRows && g_Config.m_TcWhiteFeet)
			{
				FeetBox.VSplitMid(&FeetBox, nullptr);
				static CLineInput s_WhiteFeet(g_Config.m_TcWhiteFeetSkin, sizeof(g_Config.m_TcWhiteFeetSkin));
				s_WhiteFeet.SetEmptyText("x_ninja");
				ui_widget::InputField(TClientWhiteFeetTextInputCtx, &s_WhiteFeet, FeetBox, nullptr, EditBoxFontSize);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

		auto LayoutVisualEffectsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect = CurrentColumn;
			CUIRect TmpLabel;
			CUIRect TinyTeeConfig;
			auto ShouldRenderVisualBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Visual: Effects"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(CurrentColumn.w);
			const float TinyTeeModeHeight = ResolveSettingsRadioRowLayout(CurrentColumn, 3, ContentMetrics).m_Height;
			const bool RenderTinyTeeMode = ShouldRenderVisualBlock(TinyTeeModeHeight);
			CUIRect Row = Rows.Next(TinyTeeModeHeight);
			if(RenderTinyTeeMode)
			{
				int Value = g_Config.m_TcTinyTees ? (g_Config.m_TcTinyTeesOthers ? 2 : 1) : 0;
				CPerfTimer TinyTeeModeTimer;
				if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-smaller-tees-label", Localize("Smaller tees"), s_vTinyTeeModeButtons, {"tclient-smaller-tees-none", "tclient-smaller-tees-self", "tclient-smaller-tees-all"}, {Localize("None"), Localize("Self"), Localize("All")}, {0, 1, 2}, Value, ContentMetrics))
				{
					g_Config.m_TcTinyTees = Value > 0 ? 1 : 0;
					g_Config.m_TcTinyTeesOthers = Value > 1 ? 1 : 0;
				}
				LogSettingsStage("tclient_settings_left_visual_tiny_tee_mode", TinyTeeModeTimer);
			}
			if(g_Config.m_TcTinyTees > 0)
			{
				const bool RenderTinyTeeSize = ShouldRenderVisualBlock(LineSize);
				TinyTeeConfig = Rows.Next();
				if(RenderTinyTeeSize)
				{
					CPerfTimer TinyTeeSizeTimer;
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tiny-tee-size", &g_Config.m_TcTinyTeeSize, &g_Config.m_TcTinyTeeSize, &TinyTeeConfig, Localize("Tiny Tee Size"), 85, 115);
					LogSettingsStage("tclient_settings_left_visual_tiny_tee_size", TinyTeeSizeTimer);
				}
			}

			const bool RenderJellyToggle = ShouldRenderVisualBlock(LineSize);
			Row = Rows.Next();
			if(RenderJellyToggle)
			{
				CPerfTimer MainControlsTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, "tclient-enable-jelly-tee", Localize("Enable Jelly Tee"), &g_Config.m_QmJellyTee, &Row, LineSize);
				LogSettingsStage("tclient_settings_left_visual_main_controls", MainControlsTimer);
			}
			if(g_Config.m_QmJellyTee)
			{
				const bool RenderJellyRows = ShouldRenderVisualBlock(TClientSettingsRowsHeight(3));
				CUIRect JellyOthersRow = Rows.Next();
				CUIRect JellyStrengthRow = Rows.Next();
				CUIRect JellyDurationRow = Rows.Next();
				if(RenderJellyRows)
				{
					CPerfTimer JellyTimer;
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, "tclient-jelly-others", Localize("Jelly others"), &g_Config.m_QmJellyTeeOthers, &JellyOthersRow, LineSize);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-strength", &g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &JellyStrengthRow, Localize("Jelly strength"), 0, 1000);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-duration", &g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &JellyDurationRow, Localize("Jelly duration"), 1, 500);
					LogSettingsStage("tclient_settings_left_visual_jelly", JellyTimer);
				}
			}
			const float FakeFlagsHeight = ResolveSettingsRadioRowLayout(CurrentColumn, 3, ContentMetrics).m_Height;
			const bool RenderFakeFlags = ShouldRenderVisualBlock(FakeFlagsHeight + MarginSmall + LineSize);
			CUIRect FakeFlagsRow = Rows.Next(FakeFlagsHeight);
			CUIRect MovingTilesRow = Rows.Next();
			if(RenderFakeFlags)
			{
				static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
				int Value = g_Config.m_TcFakeCtfFlags;
				CPerfTimer FakeFlagsTimer;
				if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, FakeFlagsRow, "tclient-fake-ctf-flags-label", Localize("Fake CTF flags"), s_vButtonContainers, {"tclient-fake-ctf-flags-none", "tclient-fake-ctf-flags-red", "tclient-fake-ctf-flags-blue"}, {Localize("None"), Localize("Red"), Localize("Blue")}, {0, 1, 2}, Value, ContentMetrics))
					g_Config.m_TcFakeCtfFlags = Value;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMovingTilesEntities, "tclient-moving-tiles-entities", Localize("Show moving tiles in entities"), &g_Config.m_TcMovingTilesEntities, &MovingTilesRow, LineSize);
				LogSettingsStage("tclient_settings_left_visual_fake_flags", FakeFlagsTimer);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutInputSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpButton;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Input"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, "tclient-fast-input", Localize("Fast input (reduce visual latency)"), &g_Config.m_TcFastInput, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoMargin, "qm-auto-margin", Localize("Auto margin"), &g_Config.m_QmAutoMargin, &Row, LineSize);
			Button = Rows.Next();
			if(Render)
			{
				CUIRect FastButton, BestButton, SaikoButton, ButtonsRest;
				const float Spacing = MarginSmall;
				const float ButtonWidth = (Button.w - Spacing * 2.0f) / 3.0f;
				Button.VSplitLeft(ButtonWidth, &FastButton, &ButtonsRest);
				ButtonsRest.VSplitLeft(Spacing, nullptr, &ButtonsRest);
				ButtonsRest.VSplitLeft(ButtonWidth, &BestButton, &ButtonsRest);
				ButtonsRest.VSplitLeft(Spacing, nullptr, &ButtonsRest);
				SaikoButton = ButtonsRest;
				FastButton.HMargin(2.0f, &FastButton);
				BestButton.HMargin(2.0f, &BestButton);
				SaikoButton.HMargin(2.0f, &SaikoButton);
				const int UiMode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
				if(DoButton_Menu(&s_FastInputModeFast, Localize("Fast input"), UiMode == 0, &FastButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_QmFastInputMode = 0;
				if(DoButton_Menu(&s_FastInputModeBest, Localize("Best input"), UiMode == 3, &BestButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
					g_Config.m_QmFastInputMode = 3;
				if(DoButton_Menu(&s_FastInputModeSaikoPlus, "Saiko+", UiMode == 4, &SaikoButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_QmFastInputMode = 4;
			}
			if(Render)
			{
				const int UiMode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
				if(UiMode == 0)
				{
					Button = Rows.Next();
					DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, Localize("Amount"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				}
				else if(UiMode == 3)
				{
					Button = Rows.Next();
					DoSliderWithScaledValue(&g_Config.m_QmBestInputOffset, &g_Config.m_QmBestInputOffset, &Button, Localize("Prediction offset"), 0, 1000, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ticks");
					Button = Rows.Next();
					DoSliderWithScaledValue(&g_Config.m_QmBestInputSmoothing, &g_Config.m_QmBestInputSmoothing, &Button, Localize("Input smoothing"), 0, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
					Button = Rows.Next();
					DoSliderWithScaledValue(&g_Config.m_QmBestInputLatencyComp, &g_Config.m_QmBestInputLatencyComp, &Button, Localize("Latency compensation"), 0, 50, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
					Button = Rows.Next();
					DoSliderWithScaledValue(&g_Config.m_QmBestInputInterpolation, &g_Config.m_QmBestInputInterpolation, &Button, Localize("Interpolation"), 1, 3, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");
				}
				else
				{
					Button = Rows.Next();
					DoSliderWithScaledValue(&g_Config.m_QmSaikoPlusAmount, &g_Config.m_QmSaikoPlusAmount, &Button, "Saiko+", 0, 500, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ticks");
				}
			}
			else
			{
				const int UiMode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
				for(int RowIndex = 0; RowIndex < (UiMode == 3 ? 4 : 1); ++RowIndex)
					Rows.Next();
			}
			Row = Rows.Next();
			if(Render)
			{
				const int UiMode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
				if(UiMode == 0)
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, "tclient-fast-input-others", Localize("Fast input others"), &g_Config.m_TcFastInputOthers, &Row, LineSize);
				else if(UiMode == 3)
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBestInputOthers, "qm-best-input-others", Localize("Best input others"), &g_Config.m_QmBestInputOthers, &Row, LineSize);
				else
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmSaikoPlusOthers, "qm-saiko-plus-others", Localize("Saiko+ others"), &g_Config.m_QmSaikoPlusOthers, &Row, LineSize);
			}
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, "tclient-sub-tick-aiming", Localize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Row, LineSize);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAntiLatencyToolsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpButton;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Anti Latency Tools"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-prediction-margin", &g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Base prediction margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			CUIRect Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRemoveAnti, "tclient-remove-anti-freeze", Localize("Reduce prediction while frozen"), &g_Config.m_TcRemoveAnti, &Row, LineSize);
			if(g_Config.m_TcRemoveAnti)
			{
				CUIRect AmountButton = Rows.Next();
				CUIRect DelayButton = Rows.Next();
				if(Render)
				{
					if(g_Config.m_TcUnfreezeLagDelayTicks < g_Config.m_TcUnfreezeLagTicks)
						g_Config.m_TcUnfreezeLagDelayTicks = g_Config.m_TcUnfreezeLagTicks;
					DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagTicks, &g_Config.m_TcUnfreezeLagTicks, &AmountButton, Localize("Maximum reduction"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
					DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagDelayTicks, &g_Config.m_TcUnfreezeLagDelayTicks, &DelayButton, Localize("Delay before reduction"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				}
			}
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcUnpredOthersInFreeze, "tclient-unpred-others-in-freeze", Localize("Dont predict other players if you are frozen"), &g_Config.m_TcUnpredOthersInFreeze, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPredMarginInFreeze, "tclient-pred-margin-in-freeze", Localize("Use a fixed prediction margin while frozen"), &g_Config.m_TcPredMarginInFreeze, &Row, LineSize);
			if(g_Config.m_TcPredMarginInFreeze)
			{
				Button = Rows.Next();
				if(Render)
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-margin", &g_Config.m_TcPredMarginInFreezeAmount, &g_Config.m_TcPredMarginInFreezeAmount, &Button, Localize("Frozen prediction margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAntiPingSmoothingSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpButton;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Anti Ping Smoothing"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingImproved, "tclient-antiping-improved", Localize("Use new smoothing algorithm"), &g_Config.m_TcAntiPingImproved, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingStableDirection, "tclient-antiping-stable-direction", Localize("Optimistic prediction along stable direction"), &g_Config.m_TcAntiPingStableDirection, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingNegativeBuffer, "tclient-antiping-negative-buffer", Localize("Negative stability buffer (for Gores)"), &g_Config.m_TcAntiPingNegativeBuffer, &Row, LineSize);
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-antiping-uncertainty-scale", &g_Config.m_TcAntiPingUncertaintyScale, &g_Config.m_TcAntiPingUncertaintyScale, &Button, Localize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAutoExecuteSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect Box;
			IUiContext TClientAutoExecuteTextInputCtx;
			TClientAutoExecuteTextInputCtx.m_pUi = Ui();
			TClientAutoExecuteTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientAutoExecuteTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientAutoExecuteTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_execute_text_inputs");
			TClientAutoExecuteTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Auto execute"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);

			const bool RenderBeforeConnectInput = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(3));
			Box = Rows.Next();
			if(RenderBeforeConnectInput)
			{
				Box.VSplitMid(&Label, &Button);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Execute before connecting"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
				ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);
			}

			const bool RenderOnConnectInput = RenderBeforeConnectInput;
			Box = Rows.Next();
			if(RenderOnConnectInput)
			{
				Box.VSplitMid(&Label, &Button);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Execute on connect"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
				ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);
			}

			const bool RenderDelaySlider = RenderBeforeConnectInput;
			Button = Rows.Next();
			if(RenderDelaySlider)
				DoSliderWithScaledValue(&g_Config.m_TcExecuteOnJoinDelay, &g_Config.m_TcExecuteOnJoinDelay, &Button, Localize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutVotingSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect VoteMessage;
			IUiContext TClientVotingTextInputCtx;
			TClientVotingTextInputCtx.m_pUi = Ui();
			TClientVotingTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientVotingTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientVotingTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_voting_text_inputs");
			TClientVotingTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Voting"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(MainView.w);
			const float AutoVoteHeight = ResolveSettingsRadioRowLayout(CurrentColumn, 3, ContentMetrics).m_Height;
			CUIRect Row = Rows.Next(AutoVoteHeight);

			if(Render)
			{
				static std::vector<CButtonContainer> s_vAutoMapVoteButtons = {{}, {}, {}};
				int AutoMapVote = std::clamp(g_Config.m_TcAutoVoteWhenFar, 0, 2);
				if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-auto-map-vote-label", Localize("Auto map vote"), s_vAutoMapVoteButtons, {"tclient-auto-map-vote-off", "tclient-auto-map-vote-agree", "tclient-auto-map-vote-reject"}, {Localize("Off"), Localize("Auto agree vote"), Localize("Auto reject vote")}, {0, 2, 1}, AutoMapVote, ContentMetrics))
					g_Config.m_TcAutoVoteWhenFar = AutoMapVote;
			}
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-auto-vote-minimum-time", &g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, Localize("Minimum time"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, Localize(" min"));

			VoteMessage = Rows.Next();
			if(Render)
			{
				VoteMessage.VSplitMid(&Label, &VoteMessage);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Message to send in chat:"), FontSize, TEXTALIGN_ML);
				static CLineInput s_VoteMessage(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
				s_VoteMessage.SetEmptyText(Localize("Leave empty to disable"));
				ui_widget::InputField(TClientVotingTextInputCtx, &s_VoteMessage, VoteMessage, nullptr, EditBoxFontSize);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutPetSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect PetSkinBox;
			IUiContext TClientPetTextInputCtx;
			TClientPetTextInputCtx.m_pUi = Ui();
			TClientPetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientPetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientPetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_pet_text_inputs");
			TClientPetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, "tclient-show-pet", Localize("Show the pet"), &g_Config.m_TcPetShow, &CurrentColumn, LineSize);
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-pet-size", &g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, Localize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-pet-alpha", &g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, Localize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, Render ? &PetSkinBox : &TmpRect, &CurrentColumn);
			if(Render)
			{
				PetSkinBox.VSplitMid(&Label, &Button);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet Skin:"), FontSize, TEXTALIGN_ML);
				static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
				ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		[[maybe_unused]] auto MeasurePetSection = [&](CUIRect &CurrentColumn) -> float {
			const float SavedY = CurrentColumn.y;
			LayoutPetSection(CurrentColumn, false);
			return CurrentColumn.y - SavedY;
		};
		[[maybe_unused]] auto RenderPetInteractiveSection = [&](CUIRect &CurrentColumn) {
			CUIRect PetSkinBox;
			IUiContext TClientPetTextInputCtx;
			TClientPetTextInputCtx.m_pUi = Ui();
			TClientPetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientPetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientPetTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_pet_text_inputs");
			TClientPetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, "tclient-show-pet", Localize("Show the pet"), &g_Config.m_TcPetShow, &CurrentColumn, LineSize);
			CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-pet-size", &g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, Localize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-pet-alpha", &g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, Localize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &PetSkinBox, &CurrentColumn);
			PetSkinBox.VSplitMid(&Label, &Button);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Pet Skin:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
			ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);
		};
		auto LayoutAutoReplySection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect ReplyRect;
			IUiContext TClientAutoReplyTextInputCtx;
			TClientAutoReplyTextInputCtx.m_pUi = Ui();
			TClientAutoReplyTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientAutoReplyTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientAutoReplyTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_reply_text_inputs");
			TClientAutoReplyTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
			{
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-auto-reply-title", &Label, Localize("Auto reply"), HeadlineFontSize, TEXTALIGN_ML);
			}
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				CPerfTimer MutedTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, "tclient-auto-reply-muted", Localize("Automatically reply to muted players"), &g_Config.m_TcAutoReplyMuted, &CurrentColumn, LineSize);
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &ReplyRect, &CurrentColumn);
				if(g_Config.m_TcAutoReplyMuted)
				{
					ReplyRect.HSplitTop(MarginExtraSmall, nullptr, &ReplyRect);
					static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
					s_MutedReply.SetEmptyText(Localize("I muted you"));
					ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);
				}
				LogSettingsStage("tclient_settings_left_auto_reply_muted", MutedTimer);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				CPerfTimer MinimizedTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, "tclient-auto-reply-minimized", Localize("Automatically reply while the window is unfocused"), &g_Config.m_TcAutoReplyMinimized, &CurrentColumn, LineSize);
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &ReplyRect, &CurrentColumn);
				if(g_Config.m_TcAutoReplyMinimized)
				{
					ReplyRect.HSplitTop(MarginExtraSmall, nullptr, &ReplyRect);
					static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
					s_MinimizedReply.SetEmptyText(Localize("I am away from the game window"));
					ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);
				}
				LogSettingsStage("tclient_settings_left_auto_reply_minimized", MinimizedTimer);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);

			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		[[maybe_unused]] auto MeasureAutoReplySection = [&](CUIRect &CurrentColumn) -> float {
			const float SavedY = CurrentColumn.y;
			LayoutAutoReplySection(CurrentColumn, false);
			return CurrentColumn.y - SavedY;
		};
		[[maybe_unused]] auto RenderAutoReplyInteractiveSection = [&](CUIRect &CurrentColumn) {
			CUIRect ReplyRect;
			IUiContext TClientAutoReplyTextInputCtx;
			TClientAutoReplyTextInputCtx.m_pUi = Ui();
			TClientAutoReplyTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientAutoReplyTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientAutoReplyTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_auto_reply_text_inputs");
			TClientAutoReplyTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, "tclient-auto-reply-muted", Localize("Automatically reply to muted players"), &g_Config.m_TcAutoReplyMuted, &CurrentColumn, LineSize);
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &ReplyRect, &CurrentColumn);
			if(g_Config.m_TcAutoReplyMuted)
			{
				ReplyRect.HSplitTop(MarginExtraSmall, nullptr, &ReplyRect);
				static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
				s_MutedReply.SetEmptyText(Localize("I muted you"));
				ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);

			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, "tclient-auto-reply-minimized", Localize("Automatically reply while the window is unfocused"), &g_Config.m_TcAutoReplyMinimized, &CurrentColumn, LineSize);
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &ReplyRect, &CurrentColumn);
			if(g_Config.m_TcAutoReplyMinimized)
			{
				ReplyRect.HSplitTop(MarginExtraSmall, nullptr, &ReplyRect);
				static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
				s_MinimizedReply.SetEmptyText(Localize("I am away from the game window"));
				ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
		};
		auto LayoutPlayerIndicatorSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
			{
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-player-indicator-title", &Label, Localize("Player indicator"), HeadlineFontSize, TEXTALIGN_ML);
			}
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			const bool RenderBaseRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(6));
			CUIRect EnabledRow = Rows.Next();
			CUIRect HideVisibleRow = Rows.Next();
			CUIRect FreezeOnlyRow = Rows.Next();
			CUIRect TeamOnlyRow = Rows.Next();
			CUIRect TeesRow = Rows.Next();
			CUIRect WarListRow = Rows.Next();
			if(RenderBaseRows)
			{
				CPerfTimer BaseTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicator, "tclient-player-indicator-enabled", Localize("Show any enabled Indicators"), &g_Config.m_TcPlayerIndicator, &EnabledRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorHideVisible, "tclient-indicator-hide-visible", Localize("Hide indicator for tees on your screen"), &g_Config.m_TcIndicatorHideVisible, &HideVisibleRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicatorFreeze, "tclient-player-indicator-freeze-only", Localize("Show only freeze Players"), &g_Config.m_TcPlayerIndicatorFreeze, &FreezeOnlyRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTeamOnly, "tclient-indicator-team-only", Localize("Only show after joining a team"), &g_Config.m_TcIndicatorTeamOnly, &TeamOnlyRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTees, "tclient-indicator-tees", Localize("Render tiny tees instead of circles"), &g_Config.m_TcIndicatorTees, &TeesRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicator, "tclient-warlist-indicator", Localize("Use warlist groups for indicator"), &g_Config.m_TcWarListIndicator, &WarListRow, LineSize);
				LogSettingsStage("tclient_settings_left_player_indicator_base", BaseTimer);
			}

			const int DistanceRowCount = 3 + (g_Config.m_TcIndicatorVariableDistance ? 3 : 1);
			const bool RenderDistanceRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(DistanceRowCount));
			CUIRect RadiusRow = Rows.Next();
			CUIRect OpacityRow = Rows.Next();
			CUIRect VariableDistanceRow = Rows.Next();
			CUIRect OffsetRow = Rows.Next();
			CUIRect MaxOffsetRow;
			CUIRect MaxDistanceRow;
			if(g_Config.m_TcIndicatorVariableDistance)
			{
				MaxOffsetRow = Rows.Next();
				MaxDistanceRow = Rows.Next();
			}
			if(RenderDistanceRows)
			{
				CPerfTimer DistanceTimer;
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-radius", &g_Config.m_TcIndicatorRadius, &g_Config.m_TcIndicatorRadius, &RadiusRow, Localize("Indicator size"), 1, 16);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-opacity", &g_Config.m_TcIndicatorOpacity, &g_Config.m_TcIndicatorOpacity, &OpacityRow, Localize("Indicator opacity"), 0, 100);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorVariableDistance, "tclient-indicator-variable-distance", Localize("Change indicator offset based on distance to other tees"), &g_Config.m_TcIndicatorVariableDistance, &VariableDistanceRow, LineSize);
				if(g_Config.m_TcIndicatorVariableDistance)
				{
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &OffsetRow, Localize("Indicator min offset"), 16, 200);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset-max", &g_Config.m_TcIndicatorOffsetMax, &g_Config.m_TcIndicatorOffsetMax, &MaxOffsetRow, Localize("Indicator max offset"), 16, 200);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-max-distance", &g_Config.m_TcIndicatorMaxDistance, &g_Config.m_TcIndicatorMaxDistance, &MaxDistanceRow, Localize("Indicator max distance"), 500, 7000);
				}
				else
				{
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &OffsetRow, Localize("Indicator offset"), 16, 200);
				}
				LogSettingsStage("tclient_settings_left_player_indicator_distance", DistanceTimer);
			}

			const bool ShowWarListIndicatorOptions = g_Config.m_TcWarListIndicator;
			if(ShowWarListIndicatorOptions)
			{
				const bool RenderWarListRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(4));
				CUIRect ColorsRow = Rows.Next();
				CUIRect AllRow = Rows.Next();
				CUIRect EnemyRow = Rows.Next();
				CUIRect TeamRow = Rows.Next();
				if(RenderWarListRows)
				{
					CPerfTimer WarListTimer;
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorColors, "tclient-warlist-indicator-colors", Localize("Use warlist colors instead of regular colors"), &g_Config.m_TcWarListIndicatorColors, &ColorsRow, LineSize);
					char aBuf[128];
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorAll, "tclient-warlist-indicator-all", Localize("Show all warlist groups"), &g_Config.m_TcWarListIndicatorAll, &AllRow, LineSize);
					str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorEnemy, "tclient-warlist-indicator-enemy", aBuf, &g_Config.m_TcWarListIndicatorEnemy, &EnemyRow, LineSize);
					str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
					DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorTeam, "tclient-warlist-indicator-team", aBuf, &g_Config.m_TcWarListIndicatorTeam, &TeamRow, LineSize);
					LogSettingsStage("tclient_settings_left_player_indicator_warlist", WarListTimer);
				}
			}

			const bool ShowIndicatorColorOptions = !g_Config.m_TcWarListIndicatorColors || !g_Config.m_TcWarListIndicator;
			if(ShowIndicatorColorOptions)
			{
				const bool RenderColorRows = Render && ShouldRenderSection(CurrentColumn, 0.0f, TClientSettingsRowsHeight(3));
				CUIRect AliveColorRow = Rows.Next();
				CUIRect FreezeColorRow = Rows.Next();
				CUIRect SavedColorRow = Rows.Next();
				if(RenderColorRows)
				{
					CPerfTimer ColorsTimer;
					static CButtonContainer s_IndicatorAliveColorId, s_IndicatorDeadColorId, s_IndicatorSavedColorId;
					DoLine_ColorPicker(&s_IndicatorAliveColorId, CurrentSettingsContentMetrics(), &AliveColorRow, Localize("Indicator alive color"), &g_Config.m_TcIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
					DoLine_ColorPicker(&s_IndicatorDeadColorId, CurrentSettingsContentMetrics(), &FreezeColorRow, Localize("Indicator in freeze color"), &g_Config.m_TcIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
					DoLine_ColorPicker(&s_IndicatorSavedColorId, CurrentSettingsContentMetrics(), &SavedColorRow, Localize("Indicator safe color"), &g_Config.m_TcIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
					LogSettingsStage("tclient_settings_left_player_indicator_colors", ColorsTimer);
				}
			}

			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

		// ---- CSectionLoader: register LeftView sections ----
		{
			std::vector<SSettingsSection> vLeftSections;
			SSettingsSection S;

			// -- Visual: Font & Cursor --
			vLeftSections.push_back(BuildTClientThemeCacheSection());

			// -- Visual: Nameplates --
			S = SSettingsSection{};
			S.m_pName = "Visual: Nameplates";
			S.m_pStableCardId = "tclient:visual-nameplates";
			S.m_MeasureFn = [&LayoutVisualNameplateSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutVisualNameplateSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutVisualNameplateSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Visual: Nameplates", LayoutVisualNameplateSection, Col);
			};
			S.m_RenderFullFn = [&LayoutVisualNameplateSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Visual: Nameplates", LayoutVisualNameplateSection, Col);
			};
			FillCachedStaticLayer(S, LayoutVisualNameplateSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcNameplatePingCircle, &g_Config.m_TcNameplateCountry, &g_Config.m_TcNameplateSkins, &g_Config.m_TcWhiteFeet};
			vLeftSections.push_back(S);

			// -- Visual: Effects --
			S = SSettingsSection{};
			S.m_pName = "Visual: Effects";
			S.m_pStableCardId = "tclient:visual-effects";
			S.m_MeasureFn = [&LayoutVisualEffectsSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutVisualEffectsSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutVisualEffectsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Visual: Effects", LayoutVisualEffectsSection, Col);
			};
			S.m_RenderFullFn = [&LayoutVisualEffectsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Visual: Effects", LayoutVisualEffectsSection, Col);
			};
			FillCachedStaticLayer(S, LayoutVisualEffectsSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcTinyTees, &g_Config.m_TcTinyTeesOthers, &g_Config.m_QmJellyTee};
			vLeftSections.push_back(S);
			// -- Input --
			S = SSettingsSection{};
			S.m_pName = "Input";
			S.m_pStableCardId = "tclient:input";
			S.m_MeasureFn = [&LayoutInputSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutInputSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutInputSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Input", LayoutInputSection, Col);
			};
			S.m_RenderFullFn = [&LayoutInputSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Input", LayoutInputSection, Col);
			};
			FillCachedStaticLayer(S, LayoutInputSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcFastInput, &g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputOthers, &g_Config.m_ClSubTickAiming};
			vLeftSections.push_back(S);

			// -- Anti Latency Tools --
			S = SSettingsSection{};
			S.m_pName = "Anti Latency Tools";
			S.m_pStableCardId = "tclient:anti-latency-tools";
			S.m_MeasureFn = [&LayoutAntiLatencyToolsSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutAntiLatencyToolsSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutAntiLatencyToolsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Anti Latency Tools", LayoutAntiLatencyToolsSection, Col);
			};
			S.m_RenderFullFn = [&LayoutAntiLatencyToolsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Anti Latency Tools", LayoutAntiLatencyToolsSection, Col);
			};
			FillCachedStaticLayer(S, LayoutAntiLatencyToolsSection);
			S.m_DependencyConfigInts = {&g_Config.m_ClPredictionMargin, &g_Config.m_TcRemoveAnti, &g_Config.m_TcUnfreezeLagTicks, &g_Config.m_TcUnfreezeLagDelayTicks, &g_Config.m_TcUnpredOthersInFreeze, &g_Config.m_TcPredMarginInFreeze, &g_Config.m_TcPredMarginInFreezeAmount};
			vLeftSections.push_back(S);

			// -- Improved Anti Ping --
			S = SSettingsSection{};
			S.m_pName = "Improved Anti Ping";
			S.m_pStableCardId = "tclient:improved-anti-ping";
			S.m_MeasureFn = [&LayoutAntiPingSmoothingSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutAntiPingSmoothingSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutAntiPingSmoothingSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Improved Anti Ping", LayoutAntiPingSmoothingSection, Col);
			};
			S.m_RenderFullFn = [&LayoutAntiPingSmoothingSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Improved Anti Ping", LayoutAntiPingSmoothingSection, Col);
			};
			FillCachedStaticLayer(S, LayoutAntiPingSmoothingSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcAntiPingImproved, &g_Config.m_TcAntiPingStableDirection, &g_Config.m_TcAntiPingNegativeBuffer, &g_Config.m_TcAntiPingUncertaintyScale};
			vLeftSections.push_back(S);

			// -- Execute on join --
			S = SSettingsSection{};
			S.m_pName = "Execute on join";
			S.m_pStableCardId = "tclient:execute-on-join";
			S.m_MeasureFn = [&LayoutAutoExecuteSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutAutoExecuteSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutAutoExecuteSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Execute on join", LayoutAutoExecuteSection, Col);
			};
			S.m_RenderFullFn = [&LayoutAutoExecuteSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Execute on join", LayoutAutoExecuteSection, Col);
			};
			FillCachedStaticLayer(S, LayoutAutoExecuteSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcExecuteOnJoinDelay};
			vLeftSections.push_back(S);

			// -- Voting --
			S = SSettingsSection{};
			S.m_pName = "Voting";
			S.m_pStableCardId = "tclient:voting";
			S.m_MeasureFn = [&LayoutVotingSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutVotingSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutVotingSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Voting", LayoutVotingSection, Col);
			};
			S.m_RenderFullFn = [&LayoutVotingSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Voting", LayoutVotingSection, Col);
			};
			FillCachedStaticLayer(S, LayoutVotingSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcAutoVoteWhenFar, &g_Config.m_TcAutoVoteWhenFarTime};
			vLeftSections.push_back(S);

			// -- 自动回复 --
			vLeftSections.push_back(BuildTClientAutoReplyCacheSection());

			// -- Player Indicator --
			S = SSettingsSection{};
			S.m_pName = "Player Indicator";
			S.m_pStableCardId = "tclient:player-indicator";
			S.m_MeasureFn = [&LayoutPlayerIndicatorSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutPlayerIndicatorSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutPlayerIndicatorSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Player Indicator", LayoutPlayerIndicatorSection, Col);
			};
			S.m_RenderFullFn = [&LayoutPlayerIndicatorSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Player Indicator", LayoutPlayerIndicatorSection, Col);
			};
			FillCachedStaticLayer(S, LayoutPlayerIndicatorSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcPlayerIndicator, &g_Config.m_TcIndicatorHideVisible, &g_Config.m_TcPlayerIndicatorFreeze, &g_Config.m_TcIndicatorTeamOnly, &g_Config.m_TcIndicatorTees, &g_Config.m_TcWarListIndicator, &g_Config.m_TcIndicatorRadius, &g_Config.m_TcIndicatorOpacity, &g_Config.m_TcIndicatorVariableDistance, &g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffsetMax, &g_Config.m_TcIndicatorMaxDistance};
			vLeftSections.push_back(S);

			// -- 宠物 --
			vLeftSections.push_back(BuildTClientPetCacheSection());
			AppendDeckCards(vLeftSections);
			VisualFontLoader.Register(std::move(vLeftSections));
		}

		if(ReadOnly)
		{
			VisualFontLoader.Process();
			Column = VisualFontLoader.GetRunningColumn();
			LeftView = Column;
			LogSettingsStage("tclient_settings_left_prewarm_budgeted", VisualSectionsTotalTimer);
		}
		else
		{
			VisualFontLoader.Process(false);
			Column = VisualFontLoader.GetRunningColumn();

			LogSettingsStage("tclient_settings_left_visual_total", VisualSectionsTotalTimer);
			LeftView = Column;
			LogSettingsStage("tclient_settings_left_column", LeftColumnTimer);
		}
	}

	// ***** RightView ***** //
	{
		CPerfTimer RightColumnTimer;
		Column = RightView;
		SSettingsSectionCacheRuntimeKey RightRuntimeKey = LiveRuntimeKey;
		RightRuntimeKey.m_ViewportWidth = SettingsRuntimeCacheDimensionKey(RightView.w);
		RightRuntimeKey.m_ViewportHeight = SettingsRuntimeCacheDimensionKey(RightView.h);
		RightSectionLoader.SetRuntimeKey(RightRuntimeKey);
		RightSectionLoader.SetProgressiveEnabled(TClientVisibleTargetFrame);
		RightSectionLoader.SetMaxSectionsPerFrame(TClientVisibleTargetFrame ? 1 : 2);
		RightSectionLoader.SetDeferredFarMeasurementEnabled(true);
		CUIRect RightLoaderViewport = RightView;
		RightLoaderViewport.y -= ScrollOffset.y;
		RightSectionLoader.Begin(RightView, RightLoaderViewport, 5.0f);

		auto LayoutHudSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			IUiContext TClientHudTextInputCtx;
			TClientHudTextInputCtx.m_pUi = Ui();
			TClientHudTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientHudTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientHudTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_hud_text_inputs");
			TClientHudTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(Render)
			{
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, "tclient-mini-vote-hud", Localize("Show compact vote HUD"), &g_Config.m_TcMiniVoteHud, &CurrentColumn, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, "tclient-mini-debug", Localize("Show position and angle (mini debug)"), &g_Config.m_TcMiniDebug, &CurrentColumn, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, "tclient-render-cursor-spec", Localize("Show the cursor while free spectating"), &g_Config.m_TcRenderCursorSpec, &CurrentColumn, LineSize);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render && g_Config.m_TcRenderCursorSpec)
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-freeview-cursor-opacity", &g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, Localize("Freeview cursor opacity"), 0, 100);
			}

			if(Render)
			{
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, "tclient-notify-when-last", Localize("Notify when only one tee is still alive:"), &g_Config.m_TcNotifyWhenLast, &CurrentColumn, LineSize);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			}
			CUIRect NotificationConfig;
			CurrentColumn.HSplitTop(LineSize + MarginSmall, Render ? &NotificationConfig : &TmpRect, &CurrentColumn);
			if(Render)
			{
				CPerfTimer NotifyWhenLastTimer;
				if(g_Config.m_TcNotifyWhenLast)
				{
					NotificationConfig.VSplitMid(&Button, &NotificationConfig);
					static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
					s_LastInput.SetEmptyText(Localize("You're the last one!"));
					Button.HSplitTop(MarginSmall, nullptr, &Button);
					ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);
					static CButtonContainer s_ClientNotifyWhenLastColor;
					DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, CurrentSettingsContentMetrics(), &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-x", &g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, Localize("Horizontal position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-y", &g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, Localize("Vertical position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-size", &g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, Localize("Font size"), 1, 50);
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
				}
				LogSettingsStage("tclient_settings_right_hud_notify_when_last", NotifyWhenLastTimer);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}

			if(Render)
			{
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, "tclient-show-center-line", Localize("Show the screen center line"), &g_Config.m_TcShowCenter, &CurrentColumn, LineSize);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(LineSize + MarginSmall, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
			{
				CPerfTimer ShowCenterTimer;
				if(g_Config.m_TcShowCenter)
				{
					static CButtonContainer s_ShowCenterLineColor;
					DoLine_ColorPicker(&s_ShowCenterLineColor, CurrentSettingsContentMetrics(), &Button, Localize("Screen center line color"), &g_Config.m_TcShowCenterColor, DefaultConfig::TcShowCenterColor, false, nullptr, true);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-center-line-width", &g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, Localize("Screen center line width"), 0, 20);
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
				}
				LogSettingsStage("tclient_settings_right_hud_show_center", ShowCenterTimer);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		[[maybe_unused]] auto MeasureHudSection = [&](CUIRect &CurrentColumn) -> float {
			const float SavedY = CurrentColumn.y;
			LayoutHudSection(CurrentColumn, false);
			return CurrentColumn.y - SavedY;
		};
		[[maybe_unused]] auto RenderHudInteractiveSection = [&](CUIRect &CurrentColumn) {
			IUiContext TClientHudTextInputCtx;
			TClientHudTextInputCtx.m_pUi = Ui();
			TClientHudTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientHudTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientHudTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_hud_text_inputs");
			TClientHudTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, "tclient-mini-vote-hud", Localize("Show compact vote HUD"), &g_Config.m_TcMiniVoteHud, &CurrentColumn, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, "tclient-mini-debug", Localize("Show position and angle (mini debug)"), &g_Config.m_TcMiniDebug, &CurrentColumn, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, "tclient-render-cursor-spec", Localize("Show the cursor while free spectating"), &g_Config.m_TcRenderCursorSpec, &CurrentColumn, LineSize);
			CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
			if(g_Config.m_TcRenderCursorSpec)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-freeview-cursor-opacity", &g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, Localize("Freeview cursor opacity"), 0, 100);

			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, "tclient-notify-when-last", Localize("Notify when only one tee is still alive:"), &g_Config.m_TcNotifyWhenLast, &CurrentColumn, LineSize);
			CUIRect NotificationConfig;
			CurrentColumn.HSplitTop(LineSize + MarginSmall, &NotificationConfig, &CurrentColumn);
			if(g_Config.m_TcNotifyWhenLast)
			{
				NotificationConfig.VSplitMid(&Button, &NotificationConfig);
				static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
				s_LastInput.SetEmptyText(Localize("You're the last one!"));
				Button.HSplitTop(MarginSmall, nullptr, &Button);
				ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);
				static CButtonContainer s_ClientNotifyWhenLastColor;
				DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, CurrentSettingsContentMetrics(), &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-x", &g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, Localize("Horizontal position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-y", &g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, Localize("Vertical position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-notify-last-size", &g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, Localize("Font size"), 1, 50);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}

			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, "tclient-show-center-line", Localize("Show the screen center line"), &g_Config.m_TcShowCenter, &CurrentColumn, LineSize);
			CurrentColumn.HSplitTop(LineSize + MarginSmall, &Button, &CurrentColumn);
			if(g_Config.m_TcShowCenter)
			{
				static CButtonContainer s_ShowCenterLineColor;
				DoLine_ColorPicker(&s_ShowCenterLineColor, CurrentSettingsContentMetrics(), &Button, Localize("Screen center line color"), &g_Config.m_TcShowCenterColor, DefaultConfig::TcShowCenterColor, false, nullptr, true);
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-center-line-width", &g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, Localize("Screen center line width"), 0, 20);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
		};
		auto LayoutTeeStatusBarSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
			{
				CUIElement &TeeStatusBarTitle = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-tee-status-bar-title");
				DoSettingsLabelStreamed(TeeStatusBarTitle, &Label, Localize("Tee status bar"), HeadlineFontSize, TEXTALIGN_ML, TClientFixedLabelProperties(HeadlineFontSize, Label.w));
			}
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, "tclient-show-frozen-hud", Localize("Show tee status bar"), &g_Config.m_TcShowFrozenHud, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, "tclient-frozen-hud-skins", Localize("Use custom skins instead of the ninja tee"), &g_Config.m_TcShowFrozenHudSkins, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenHudTeamOnly, "tclient-frozen-hud-team-only", Localize("Only show after joining a team"), &g_Config.m_TcFrozenHudTeamOnly, &Row, LineSize);
			Button = Rows.Next();
			if(Render)
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-max-rows", &g_Config.m_TcFrozenMaxRows, &g_Config.m_TcFrozenMaxRows, &Button, Localize("Maximum rows"), 1, 6);
			}
			Button = Rows.Next();
			if(Render)
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-hud-tee-size", &g_Config.m_TcFrozenHudTeeSize, &g_Config.m_TcFrozenHudTeeSize, &Button, Localize("Tee size"), 8, 27);
			}
			CUIRect CheckBoxRect = Rows.Next();
			if(Render)
			{
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcShowFrozenText, "tclient-show-frozen-text", Localize("Show the number of tees still alive"), g_Config.m_TcShowFrozenText >= 1, &CheckBoxRect))
					g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;
			}
			if(g_Config.m_TcShowFrozenText)
			{
				CUIRect CheckBoxRect2 = Rows.Next();
				if(Render)
				{
					if(DoTClientSettingsButton_CheckBox(&s_CountFrozenText, "tclient-show-frozen-count-text", Localize("Show the number of frozen tees"), g_Config.m_TcShowFrozenText == 2, &CheckBoxRect2))
						g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
				}
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutTileOutlinesSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			auto ShouldRenderTileOutlineBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Tile outlines"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			const bool RenderBaseRows = ShouldRenderTileOutlineBlock(TClientSettingsRowsHeight(4));
			CUIRect EnabledRow = Rows.Next();
			CUIRect EntitiesRow = Rows.Next();
			CUIRect OpacityRow = Rows.Next();
			CUIRect SolidOpacityRow = Rows.Next();
			if(RenderBaseRows)
			{
				CPerfTimer TileOutlinesBaseTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, "tclient-outline-enabled", Localize("Show all enabled outlines"), &g_Config.m_TcOutline, &EnabledRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineEntities, "tclient-outline-entities", Localize("Only show outlines in the entities layer"), &g_Config.m_TcOutlineEntities, &EntitiesRow, LineSize);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-opacity", &g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineAlpha, &OpacityRow, Localize("Outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-solid-opacity", &g_Config.m_TcOutlineSolidAlpha, &g_Config.m_TcOutlineSolidAlpha, &SolidOpacityRow, Localize("Solid tile outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				LogSettingsStage("tclient_settings_right_tile_outlines_base", TileOutlinesBaseTimer);
			}

			auto DoOutlineType = [&](const char *pStage, CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
				const bool RenderRows = ShouldRenderTileOutlineBlock(TClientSettingsRowsHeight(2));
				CUIRect ColorRow = Rows.Next();
				CUIRect WidthRow = Rows.Next();
				if(RenderRows)
				{
					CPerfTimer OutlineTypeTimer;
					DoLine_ColorPicker(&ButtonContainer, CurrentSettingsContentMetrics(), &ColorRow, pName, &Color, ColorDefault, true, &Enable, true);
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-width", &Width, &Width, &WidthRow, Localize("Width", "Outlines"), 1, 16);
					LogSettingsStage(pStage, OutlineTypeTimer);
				}
			};

			static CButtonContainer s_aOutlineButtonContainers[5];
			static CButtonContainer s_OutlineDeepFreezeColorId;
			static CButtonContainer s_OutlineDeepUnfreezeColorId;
			DoOutlineType("tclient_settings_right_tile_outlines_solid", s_aOutlineButtonContainers[0], Localize("Solid"), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid, DefaultConfig::TcOutlineColorSolid);
			DoOutlineType("tclient_settings_right_tile_outlines_freeze", s_aOutlineButtonContainers[1], Localize("Freeze"), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze, DefaultConfig::TcOutlineColorFreeze);
			{
				const bool RenderColorRow = ShouldRenderTileOutlineBlock(LineSize);
				CUIRect ColorRow = Rows.Next();
				if(RenderColorRow)
				{
					CPerfTimer DeepFreezeTimer;
					DoLine_ColorPicker(&s_OutlineDeepFreezeColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Deep freeze color"), &g_Config.m_TcOutlineColorDeepFreeze, DefaultConfig::TcOutlineColorDeepFreeze, false, nullptr, true);
					LogSettingsStage("tclient_settings_right_tile_outlines_deepfreeze_color", DeepFreezeTimer);
				}
			}
			DoOutlineType("tclient_settings_right_tile_outlines_unfreeze", s_aOutlineButtonContainers[2], Localize("Unfreeze"), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze, DefaultConfig::TcOutlineColorUnfreeze);
			{
				const bool RenderColorRow = ShouldRenderTileOutlineBlock(LineSize);
				CUIRect ColorRow = Rows.Next();
				if(RenderColorRow)
				{
					CPerfTimer DeepUnfreezeTimer;
					DoLine_ColorPicker(&s_OutlineDeepUnfreezeColorId, CurrentSettingsContentMetrics(), &ColorRow, Localize("Deep unfreeze color"), &g_Config.m_TcOutlineColorDeepUnfreeze, DefaultConfig::TcOutlineColorDeepUnfreeze, false, nullptr, true);
					LogSettingsStage("tclient_settings_right_tile_outlines_deepunfreeze_color", DeepUnfreezeTimer);
				}
			}
			DoOutlineType("tclient_settings_right_tile_outlines_kill", s_aOutlineButtonContainers[3], Localize("Kill"), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill, DefaultConfig::TcOutlineColorKill);
			DoOutlineType("tclient_settings_right_tile_outlines_tele", s_aOutlineButtonContainers[4], Localize("Tele"), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele, DefaultConfig::TcOutlineColorTele);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutGhostToolsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Ghost tools"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowOthersGhosts, "tclient-show-others-ghosts", Localize("Show unpredicted ghosts for other players"), &g_Config.m_TcShowOthersGhosts, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcSwapGhosts, "tclient-swap-ghosts", Localize("Swap ghosts with regular players"), &g_Config.m_TcSwapGhosts, &Row, LineSize);
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-predicted-ghost-opacity", &g_Config.m_TcPredGhostsAlpha, &g_Config.m_TcPredGhostsAlpha, &Button, Localize("Predicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-unpredicted-ghost-opacity", &g_Config.m_TcUnpredGhostsAlpha, &g_Config.m_TcUnpredGhostsAlpha, &Button, Localize("Unpredicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcHideFrozenGhosts, "tclient-hide-frozen-ghosts", Localize("Hide ghosts of frozen players"), &g_Config.m_TcHideFrozenGhosts, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderGhostAsCircle, "tclient-render-ghost-as-circle", Localize("Render ghosts as circles"), &g_Config.m_TcRenderGhostAsCircle, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
			{
				static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
				DoLine_KeyReader(Row, s_ReaderButtonGhost, s_ClearButtonGhost, Localize("Toggle ghost key"), "toggle tc_show_others_ghosts 0 1");
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutRainbowSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect RainbowDropDownRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Rainbow"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowTees, "tclient-rainbow-tees", Localize("Rainbow Tees"), &g_Config.m_TcRainbowTees, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowWeapon, "tclient-rainbow-weapons", Localize("Rainbow weapons"), &g_Config.m_TcRainbowWeapon, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowHook, "tclient-rainbow-hook", Localize("Rainbow hook"), &g_Config.m_TcRainbowHook, &Row, LineSize);
			Row = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowOthers, "tclient-rainbow-others", Localize("Rainbow others"), &g_Config.m_TcRainbowOthers, &Row, LineSize);
			static std::vector<const char *> s_RainbowDropDownNames;
			s_RainbowDropDownNames = {Localize("Rainbow"), Localize("Pulse"), Localize("Black"), Localize("Random")};
			static CUi::SDropDownState s_RainbowDropDownState;
			static CScrollRegion s_RainbowDropDownScrollRegion;
			s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
			int RainbowSelectedOld = g_Config.m_TcRainbowMode - 1;
			RainbowDropDownRect = Rows.Next();
			if(Render)
			{
				const int RainbowSelectedNew = DoSettingsDropDown(&RainbowDropDownRect, RainbowSelectedOld, s_RainbowDropDownNames.data(), s_RainbowDropDownNames.size(), s_RainbowDropDownState);
				if(RainbowSelectedOld != RainbowSelectedNew)
					g_Config.m_TcRainbowMode = RainbowSelectedNew + 1;
			}
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-rainbow-speed", &g_Config.m_TcRainbowSpeed, &g_Config.m_TcRainbowSpeed, &Button, Localize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutTeeTrailsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect TrailDropDownRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Tee Trails"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect TrailEnabledRow = Rows.Next();
			CUIRect TrailOthersRow = Rows.Next();
			CUIRect TrailFadeRow = Rows.Next();
			CUIRect TrailTaperRow = Rows.Next();
			if(Render)
			{
				CPerfTimer BaseTimer;
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrail, "tclient-tee-trail-enabled", Localize("Enable tee trails"), &g_Config.m_TcTeeTrail, &TrailEnabledRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailOthers, "tclient-tee-trail-others", Localize("Show other tees' trails"), &g_Config.m_TcTeeTrailOthers, &TrailOthersRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailFade, "tclient-tee-trail-fade", Localize("Fade trail alpha"), &g_Config.m_TcTeeTrailFade, &TrailFadeRow, LineSize);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailTaper, "tclient-tee-trail-taper", Localize("Taper trail width"), &g_Config.m_TcTeeTrailTaper, &TrailTaperRow, LineSize);
				LogSettingsStage("tclient_settings_right_tee_trails_base", BaseTimer);
			}
			static std::vector<const char *> s_TrailDropDownNames;
			s_TrailDropDownNames = {Localize("Solid"), Localize("Tee"), Localize("Rainbow"), Localize("Speed")};
			s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
			int TrailSelectedOld = g_Config.m_TcTeeTrailColorMode - 1;
			TrailDropDownRect = Rows.Next();
			if(Render)
			{
				CPerfTimer DropDownTimer;
				const int TrailSelectedNew = DoSettingsDropDown(&TrailDropDownRect, TrailSelectedOld, s_TrailDropDownNames.data(), s_TrailDropDownNames.size(), s_TrailDropDownState);
				if(TrailSelectedOld != TrailSelectedNew)
					g_Config.m_TcTeeTrailColorMode = TrailSelectedNew + 1;
				LogSettingsStage("tclient_settings_right_tee_trails_dropdown", DropDownTimer);
			}
			if(g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID)
			{
				CUIRect ColorRow = Rows.Next();
				if(Render)
				{
					CPerfTimer ColorTimer;
					static CButtonContainer s_TeeTrailColor;
					DoLine_ColorPicker(&s_TeeTrailColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Tee trail color"), &g_Config.m_TcTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
					LogSettingsStage("tclient_settings_right_tee_trails_color", ColorTimer);
				}
			}
			CUIRect WidthRow = Rows.Next();
			CUIRect LengthRow = Rows.Next();
			CUIRect AlphaRow = Rows.Next();
			if(Render)
			{
				CPerfTimer SlidersTimer;
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-width", &g_Config.m_TcTeeTrailWidth, &g_Config.m_TcTeeTrailWidth, &WidthRow, Localize("Trail width"), 0, 20);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-length", &g_Config.m_TcTeeTrailLength, &g_Config.m_TcTeeTrailLength, &LengthRow, Localize("Trail length"), 0, 200);
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-alpha", &g_Config.m_TcTeeTrailAlpha, &g_Config.m_TcTeeTrailAlpha, &AlphaRow, Localize("Trail alpha"), 0, 100);
				LogSettingsStage("tclient_settings_right_tee_trails_sliders", SlidersTimer);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutBackgroundDrawSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Background Draw"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect ColorRow = Rows.Next();
			static CButtonContainer s_BgDrawColor;
			if(Render)
				DoLine_ColorPicker(&s_BgDrawColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Color"), &g_Config.m_TcBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

			Button = Rows.Next();
			if(Render)
			{
				if(g_Config.m_TcBgDrawFadeTime == 0)
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds (never)"));
				else
					DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds"));
			}
			Button = Rows.Next();
			if(Render)
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-width", &g_Config.m_TcBgDrawWidth, &g_Config.m_TcBgDrawWidth, &Button, Localize("Width"), 1, 50);
			CUIRect KeyRow = Rows.Next();
			if(Render)
			{
				static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
				DoLine_KeyReader(KeyRow, s_ReaderButtonDraw, s_ClearButtonDraw, Localize("Draw where mouse is"), "+bg_draw");
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutFinishNameSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect FinishNameBox;
			IUiContext TClientFinishNameTextInputCtx;
			TClientFinishNameTextInputCtx.m_pUi = Ui();
			TClientFinishNameTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientFinishNameTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientFinishNameTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_finish_name_text_inputs");
			TClientFinishNameTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Finish Name"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CTClientSettingsRowAllocator Rows(CurrentColumn);
			CUIRect ToggleRow = Rows.Next();
			if(Render)
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcChangeNameNearFinish, "tclient-change-name-near-finish", Localize("Attempt to change your name when near finish"), &g_Config.m_TcChangeNameNearFinish, &ToggleRow, LineSize);
			if(g_Config.m_TcChangeNameNearFinish)
			{
				FinishNameBox = Rows.Next();
				if(Render)
				{
					FinishNameBox.VSplitMid(&Label, &Button);
					DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Finish Name:"), FontSize, TEXTALIGN_ML);
					static CLineInput s_FinishName(g_Config.m_TcFinishName, sizeof(g_Config.m_TcFinishName));
					ui_widget::InputField(TClientFinishNameTextInputCtx, &s_FinishName, Button, nullptr, EditBoxFontSize);
				}
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

		{
			std::vector<SSettingsSection> vRightSections;
			SSettingsSection S;

			// -- HUD --
			vRightSections.push_back(BuildTClientHudCacheSection());

			// -- Tee status bar --
			S = SSettingsSection{};
			S.m_pName = "Tee status bar";
			S.m_pStableCardId = "tclient:tee-status-bar";
			S.m_MeasureFn = [&LayoutTeeStatusBarSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutTeeStatusBarSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutTeeStatusBarSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Tee status bar", LayoutTeeStatusBarSection, Col);
			};
			S.m_RenderFullFn = [&LayoutTeeStatusBarSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Tee status bar", LayoutTeeStatusBarSection, Col);
			};
			FillCachedStaticLayer(S, LayoutTeeStatusBarSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcShowFrozenHud, &g_Config.m_TcFrozenMaxRows, &g_Config.m_TcShowFrozenHudSkins};
			vRightSections.push_back(S);

			// -- Tile outlines --
			S = SSettingsSection{};
			S.m_pName = "Tile outlines";
			S.m_pStableCardId = "tclient:tile-outlines";
			S.m_MeasureFn = [&LayoutTileOutlinesSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutTileOutlinesSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutTileOutlinesSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Tile outlines", LayoutTileOutlinesSection, Col);
			};
			S.m_RenderFullFn = [&LayoutTileOutlinesSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Tile outlines", LayoutTileOutlinesSection, Col);
			};
			FillCachedStaticLayer(S, LayoutTileOutlinesSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcOutline, &g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineEntities, &g_Config.m_TcOutlineSolidAlpha, &g_Config.m_TcOutlineSolid, &g_Config.m_TcOutlineWidthSolid, &g_Config.m_TcOutlineFreeze, &g_Config.m_TcOutlineWidthFreeze, &g_Config.m_TcOutlineUnfreeze, &g_Config.m_TcOutlineWidthUnfreeze, &g_Config.m_TcOutlineKill, &g_Config.m_TcOutlineWidthKill, &g_Config.m_TcOutlineTele, &g_Config.m_TcOutlineWidthTele};
			S.m_DependencyConfigCols = {&g_Config.m_TcOutlineColorSolid, &g_Config.m_TcOutlineColorFreeze, &g_Config.m_TcOutlineColorDeepFreeze, &g_Config.m_TcOutlineColorDeepUnfreeze, &g_Config.m_TcOutlineColorUnfreeze, &g_Config.m_TcOutlineColorKill, &g_Config.m_TcOutlineColorTele};
			vRightSections.push_back(S);

			// -- Ghost tools --
			S = SSettingsSection{};
			S.m_pName = "Ghost tools";
			S.m_pStableCardId = "tclient:ghost-tools";
			S.m_MeasureFn = [&LayoutGhostToolsSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutGhostToolsSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutGhostToolsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Ghost tools", LayoutGhostToolsSection, Col);
			};
			S.m_RenderFullFn = [&LayoutGhostToolsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Ghost tools", LayoutGhostToolsSection, Col);
			};
			FillCachedStaticLayer(S, LayoutGhostToolsSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcShowOthersGhosts, &g_Config.m_TcSwapGhosts};
			vRightSections.push_back(S);

			// -- Rainbow --
			S = SSettingsSection{};
			S.m_pName = "Rainbow";
			S.m_pStableCardId = "tclient:rainbow";
			S.m_MeasureFn = [&LayoutRainbowSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutRainbowSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutRainbowSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Rainbow", LayoutRainbowSection, Col);
			};
			S.m_RenderFullFn = [&LayoutRainbowSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Rainbow", LayoutRainbowSection, Col);
			};
			FillCachedStaticLayer(S, LayoutRainbowSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcRainbowTees, &g_Config.m_TcRainbowWeapon, &g_Config.m_TcRainbowHook};
			vRightSections.push_back(S);

			// -- Tee Trails --
			S = SSettingsSection{};
			S.m_pName = "Tee Trails";
			S.m_pStableCardId = "tclient:tee-trails";
			S.m_MeasureFn = [&LayoutTeeTrailsSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutTeeTrailsSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutTeeTrailsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Tee Trails", LayoutTeeTrailsSection, Col);
			};
			S.m_RenderFullFn = [&LayoutTeeTrailsSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Tee Trails", LayoutTeeTrailsSection, Col);
			};
			FillCachedStaticLayer(S, LayoutTeeTrailsSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcTeeTrail, &g_Config.m_TcTeeTrailOthers, &g_Config.m_TcTeeTrailWidth, &g_Config.m_TcTeeTrailLength, &g_Config.m_TcTeeTrailAlpha};
			vRightSections.push_back(S);

			// -- Background Draw --
			S = SSettingsSection{};
			S.m_pName = "Background Draw";
			S.m_pStableCardId = "tclient:background-draw";
			S.m_MeasureFn = [&LayoutBackgroundDrawSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutBackgroundDrawSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutBackgroundDrawSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Background Draw", LayoutBackgroundDrawSection, Col);
			};
			S.m_RenderFullFn = [&LayoutBackgroundDrawSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Background Draw", LayoutBackgroundDrawSection, Col);
			};
			FillCachedStaticLayer(S, LayoutBackgroundDrawSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcBgDrawWidth, &g_Config.m_TcBgDrawFadeTime};
			vRightSections.push_back(S);

			// -- Finish Name --
			S = SSettingsSection{};
			S.m_pName = "Finish Name";
			S.m_pStableCardId = "tclient:finish-name";
			S.m_MeasureFn = [&LayoutFinishNameSection](CUIRect &Col) -> float {
				float SavedY = Col.y;
				LayoutFinishNameSection(Col, false);
				return Col.y - SavedY;
			};
			S.m_RenderCompactFn = [&LayoutFinishNameSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Finish Name", LayoutFinishNameSection, Col);
			};
			S.m_RenderFullFn = [&LayoutFinishNameSection, &RenderBoxedFullSection](CUIRect &Col) -> float {
				return RenderBoxedFullSection("Finish Name", LayoutFinishNameSection, Col);
			};
			FillCachedStaticLayer(S, LayoutFinishNameSection);
			S.m_DependencyConfigInts = {&g_Config.m_TcChangeNameNearFinish};
			vRightSections.push_back(S);
			AppendDeckCards(vRightSections);
			RightSectionLoader.Register(std::move(vRightSections));
		}

		if(ReadOnly)
		{
			RightSectionLoader.Process();
			Column = RightSectionLoader.GetRunningColumn();
			RightView = Column;
			LogSettingsStage("tclient_settings_right_prewarm_budgeted", RightColumnTimer);
		}
		else
		{
			RightSectionLoader.Process(false);
			Column = RightSectionLoader.GetRunningColumn();

			// ***** END OF PAGE 1 SETTINGS ***** //
			RightView = Column;
			LogSettingsStage("tclient_settings_right_column", RightColumnTimer);
		}
	}
	if(!ReadOnly)
	{
		static constexpr std::array<std::pair<const char *, const char *>, 19> s_aDeckCardSpecs = {{
			{"tclient:visual-font-cursor", "Visual: Font & Cursor"},
			{"tclient:visual-nameplates", "Visual: Nameplates"},
			{"tclient:visual-effects", "Visual: Effects"},
			{"tclient:input", "Input"},
			{"tclient:anti-latency-tools", "Anti Latency Tools"},
			{"tclient:improved-anti-ping", "Improved Anti Ping"},
			{"tclient:execute-on-join", "Execute on join"},
			{"tclient:voting", "Voting"},
			{"tclient:auto-reply", "Auto reply"},
			{"tclient:player-indicator", "Player Indicator"},
			{"tclient:pet", "Pet"},
			{"tclient:hud", "HUD"},
			{"tclient:tee-status-bar", "Tee status bar"},
			{"tclient:tile-outlines", "Tile outlines"},
			{"tclient:ghost-tools", "Ghost tools"},
			{"tclient:rainbow", "Rainbow"},
			{"tclient:tee-trails", "Tee Trails"},
			{"tclient:background-draw", "Background Draw"},
			{"tclient:finish-name", "Finish Name"},
		}};
		dbg_assert(DeckCardBindingIndex == s_aDeckCardBindings.size(), "missing TClient settings card binding");
		// 高度由开关控制的行必须先提交点击，再重新测量卡片，避免首帧溢出旧内容矩形。
		const auto BuildTClientConditionalRowsPreLayoutInput = [this](const char *pStableCardId) -> FSettingsCardPreLayoutInput {
			const auto ProcessToggle = [this](const CUIRect &Row, int *pValue) {
				if(!Ui()->DoButtonLogic(pValue, 0, &Row, BUTTONFLAG_LEFT))
					return false;
				*pValue ^= 1;
				return true;
			};
			if(str_comp(pStableCardId, "tclient:visual-nameplates") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					Rows.Next();
					Rows.Next();
					Rows.Next();
					Rows.Next();
					Rows.Next();
					Rows.Next();
					return ProcessToggle(Rows.Next(), &g_Config.m_TcWhiteFeet);
				};
			}
			if(str_comp(pStableCardId, "tclient:anti-latency-tools") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					Rows.Next();
					bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcRemoveAnti);
					if(g_Config.m_TcRemoveAnti)
					{
						Rows.Next();
						Rows.Next();
					}
					Rows.Next();
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcPredMarginInFreeze) || Changed;
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:auto-reply") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcAutoReplyMuted);
					if(g_Config.m_TcAutoReplyMuted)
						Rows.Next();
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcAutoReplyMinimized) || Changed;
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:player-indicator") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					for(int RowIndex = 0; RowIndex < 5; ++RowIndex)
						Rows.Next();
					bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcWarListIndicator);

					Rows.Next();
					Rows.Next();
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcIndicatorVariableDistance) || Changed;
					Rows.Next();
					if(g_Config.m_TcIndicatorVariableDistance)
					{
						Rows.Next();
						Rows.Next();
					}

					if(g_Config.m_TcWarListIndicator)
					{
						Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcWarListIndicatorColors) || Changed;
						Rows.Next();
						Rows.Next();
						Rows.Next();
					}
					if(!g_Config.m_TcWarListIndicator || !g_Config.m_TcWarListIndicatorColors)
					{
						Rows.Next();
						Rows.Next();
						Rows.Next();
					}
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:tee-status-bar") == 0)
			{
				return [this](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					for(int RowIndex = 0; RowIndex < 5; ++RowIndex)
						Rows.Next();

					CUIRect ShowFrozenTextRow = Rows.Next();
					bool Changed = false;
					if(Ui()->DoButtonLogic(&g_Config.m_TcShowFrozenText, g_Config.m_TcShowFrozenText >= 1, &ShowFrozenTextRow, BUTTONFLAG_LEFT))
					{
						g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;
						Changed = true;
					}
					if(g_Config.m_TcShowFrozenText)
					{
						CUIRect CountFrozenTextRow = Rows.Next();
						if(Ui()->DoButtonLogic(&s_CountFrozenText, g_Config.m_TcShowFrozenText == 2, &CountFrozenTextRow, BUTTONFLAG_LEFT))
						{
							g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
							Changed = true;
						}
					}
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:finish-name") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					const CUIRect ToggleRow = Rows.Next();
					const bool Changed = ProcessToggle(ToggleRow, &g_Config.m_TcChangeNameNearFinish);
					if(g_Config.m_TcChangeNameNearFinish)
						Rows.Next();
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:hud") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					Rows.Next();
					Rows.Next();
					bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcRenderCursorSpec);
					if(g_Config.m_TcRenderCursorSpec)
						Rows.Next();
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcNotifyWhenLast) || Changed;
					if(g_Config.m_TcNotifyWhenLast)
					{
						Rows.Next();
						Rows.Next();
						Rows.Next();
						Rows.Next();
					}
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcShowCenter) || Changed;
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:visual-effects") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(Content.w);
					const CUIRect TinyTeeModeRow = Rows.Next(ResolveSettingsRadioRowLayout(Content, 3, ContentMetrics).m_Height);
					const SSettingsRadioRowLayout TinyTeeModeLayout = ResolveSettingsRadioRowLayout(TinyTeeModeRow, 3, ContentMetrics);
					CUIRect TinyTeeModeButtons = TinyTeeModeLayout.m_ButtonsRect;
					int TinyTeeMode = g_Config.m_TcTinyTees ? (g_Config.m_TcTinyTeesOthers ? 2 : 1) : 0;
					bool Changed = false;
					const float ButtonWidth = TinyTeeModeButtons.w / s_vTinyTeeModeButtons.size();
					for(int Index = 0; Index < (int)s_vTinyTeeModeButtons.size(); ++Index)
					{
						CUIRect RadioButton;
						TinyTeeModeButtons.VSplitLeft(ButtonWidth, &RadioButton, &TinyTeeModeButtons);
						if(Ui()->DoButtonLogic(&s_vTinyTeeModeButtons[Index], Index == TinyTeeMode, &RadioButton, BUTTONFLAG_LEFT))
						{
							g_Config.m_TcTinyTees = Index > 0 ? 1 : 0;
							g_Config.m_TcTinyTeesOthers = Index > 1 ? 1 : 0;
							Changed = true;
						}
					}
					if(g_Config.m_TcTinyTees > 0)
						Rows.Next();
					return ProcessToggle(Rows.Next(), &g_Config.m_QmJellyTee) || Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:input") == 0)
			{
				return [this, ProcessToggle](CUIRect Content) {
					if(m_MenuTextPlanCollecting)
						return false;
					CTClientSettingsRowAllocator Rows(Content);
					bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_TcFastInput);
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmAutoMargin) || Changed;

					CUIRect Button = Rows.Next();
					CUIRect FastButton, BestButton, SaikoButton, ButtonsRest;
					const float Spacing = MarginSmall;
					const float ButtonWidth = (Button.w - Spacing * 2.0f) / 3.0f;
					Button.VSplitLeft(ButtonWidth, &FastButton, &ButtonsRest);
					ButtonsRest.VSplitLeft(Spacing, nullptr, &ButtonsRest);
					ButtonsRest.VSplitLeft(ButtonWidth, &BestButton, &ButtonsRest);
					ButtonsRest.VSplitLeft(Spacing, nullptr, &ButtonsRest);
					SaikoButton = ButtonsRest;
					FastButton.HMargin(2.0f, &FastButton);
					BestButton.HMargin(2.0f, &BestButton);
					SaikoButton.HMargin(2.0f, &SaikoButton);
					const int UiMode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
					if(Ui()->DoButtonLogic(&s_FastInputModeFast, UiMode == 0, &FastButton, BUTTONFLAG_LEFT))
					{
						g_Config.m_QmFastInputMode = 0;
						Changed = true;
					}
					if(Ui()->DoButtonLogic(&s_FastInputModeBest, UiMode == 3, &BestButton, BUTTONFLAG_LEFT))
					{
						g_Config.m_QmFastInputMode = 3;
						Changed = true;
					}
					if(Ui()->DoButtonLogic(&s_FastInputModeSaikoPlus, UiMode == 4, &SaikoButton, BUTTONFLAG_LEFT))
					{
						g_Config.m_QmFastInputMode = 4;
						Changed = true;
					}

					const int ActiveMode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
					for(int RowIndex = 0; RowIndex < (ActiveMode == 3 ? 4 : 1); ++RowIndex)
						Rows.Next();
					Changed = ProcessToggle(Rows.Next(), ActiveMode == 0 ? &g_Config.m_TcFastInputOthers : (ActiveMode == 3 ? &g_Config.m_QmBestInputOthers : &g_Config.m_QmSaikoPlusOthers)) || Changed;
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_ClSubTickAiming) || Changed;
					return Changed;
				};
			}
			if(str_comp(pStableCardId, "tclient:tee-trails") == 0)
			{
				return [](CUIRect) {
					// 下拉弹层先于卡片内容绘制写入选择项；这里仅提前提交选择，
					// 正式 DoSettingsDropDown 仍负责清理状态和绘制弹层。
					const int Selected = s_TrailDropDownState.m_SelectionPopupContext.m_SelectionIndex;
					if(Selected < 0 || Selected >= 4)
						return false;
					const int NewColorMode = Selected + 1;
					if(g_Config.m_TcTeeTrailColorMode == NewColorMode)
						return false;
					g_Config.m_TcTeeTrailColorMode = NewColorMode;
					return true;
				};
			}
			return {};
		};
		auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
			vCards.reserve(s_aDeckCardSpecs.size());
			for(size_t Index = 0; Index < s_aDeckCardSpecs.size(); ++Index)
			{
				CTClientSettingsCardFrameBinding *pBinding = &s_aDeckCardBindings[Index];
				SSettingsCardDefinition Definition;
				Definition.m_Spec = {s_aDeckCardSpecs[Index].first, Localize(s_aDeckCardSpecs[Index].second), qm_card_registry::ResolveLocalizedDescription(s_aDeckCardSpecs[Index].first)};
				Definition.m_Measure = [pBinding](float ContentWidth) { return pBinding->Measure(ContentWidth); };
				Definition.m_RenderMeasured = [pBinding](CUIRect &Content) { pBinding->Render(Content); };
				Definition.m_MeasureRevision = HashTClientSettingsCardLayout(s_aDeckCardSpecs[Index].first);
				Definition.m_PreLayoutInput = BuildTClientConditionalRowsPreLayoutInput(s_aDeckCardSpecs[Index].first);
				if(str_comp(s_aDeckCardSpecs[Index].first, "tclient:tee-trails") == 0)
				{
					Definition.m_HasPendingPreLayoutInput = [] {
						const int Selected = s_TrailDropDownState.m_SelectionPopupContext.m_SelectionIndex;
						return Selected >= 0 && Selected < 4;
					};
				}
				vCards.push_back(std::move(Definition));
			}
		};
		uint64_t CardLayoutRevision = TClientCardDefinitionsLayoutRevision(Ui()->RenderOnly(), m_TClientSettingsTab, "tclient");
		for(const auto &Spec : s_aDeckCardSpecs)
			CardLayoutRevision = HashValueFnv1a64(CardLayoutRevision, HashTClientSettingsCardLayout(Spec.first));
		const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
		const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
		const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
		SSettingsCardDeckInput InputState;
		InputState.m_MouseX = Ui()->MouseX();
		InputState.m_MouseY = Ui()->MouseY();
		InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
		InputState.m_MouseDown = Ui()->MouseButton(0);
		InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
		InputState.m_CtrlPressed = Input()->ModifierIsPressed();
		InputState.m_AllowHeaderDrag = true;
		InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		InputState.m_pScrollParams = &ScrollParams;
		if(str_startswith(m_SettingsCardFocusStableId.c_str(), "tclient:") != nullptr)
		{
			m_SettingsCardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
			m_SettingsCardFocusStableId.clear();
		}
		const SSettingsCardDeckResult DeckResult = m_SettingsCardDeck.RenderCached(SettingsUiContext("settings_tclient_main", UiScale), Page, "tclient", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModel(), &s_TClientSettingsScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
		for(CTClientSettingsCardFrameBinding &Binding : s_aDeckCardBindings)
			Binding.Clear();
		if(DeckResult.m_OrderChanged)
			SaveSettingsCardOrderModel();
		m_SettingsTClientCurrentScrollY = s_TClientSettingsScrollRegion.ContentScrollOffsetY();
		m_SettingsRuntimeMetadata.m_LastScrollPage = SETTINGS_TCLIENT;
		m_SettingsRuntimeMetadata.m_LastScrollY = m_SettingsTClientCurrentScrollY;
		m_SettingsRuntimeMetadata.m_Valid = true;
	}
	if(!ReadOnly)
	{
		SSettingsUiBudgetFrame UiBudget;
		UiBudget.m_LayoutMs = LayoutBudgetTimer.ElapsedMs();
		UiBudget.m_TextMs = 0.0;
		UiBudget.m_TextNew = 0;
		UiBudget.m_TextReused = 0;
		UiBudget.m_DrawCalls = 0;
		UiBudget.m_Vertices = 0;
		UiBudget.m_Indices = 0;
		UiBudget.m_HeapAllocs = 0;
		UiBudget.m_VisibleWidgets = VisualFontLoader.LastFrameStats().m_SectionsVisible + RightSectionLoader.LastFrameStats().m_SectionsVisible;
		UiBudget.m_Tab = m_TClientSettingsTab;
		UiBudget.m_Subtab = m_TClientSettingsTab;
		LogSettingsUiBudget("settings:tclient", UiBudget);
	}
}

void CMenus::LoadSettingsRuntimeCacheMetadata()
{
	SSessionUiCache SessionCache;
	CSectionLoader::LoadSessionCache(SessionCache, SETTINGS_RUNTIME_CACHE_METADATA_FILE, Storage());
	m_SettingsRuntimeMetadata = {};
	const CUIRect CacheView = Ui()->Screen() != nullptr ? TClientSettingsContentView(*Ui()->Screen()) : CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	const SSettingsRuntimeCacheKey CurrentRuntimeKey = ToSettingsRuntimeCacheKey(MakeSettingsSectionRuntimeKey(CacheView, Graphics()));
	const SSettingsRuntimeCacheKey PersistedRuntimeKey = ToSettingsRuntimeCacheKey(SessionCache.m_RuntimeKey);
	const bool RuntimeKeyMatches = SettingsRuntimeCacheKeyMatches(CurrentRuntimeKey, PersistedRuntimeKey);
	m_SettingsRuntimeMetadata.m_LastPage = SessionCache.m_LastSettingsPage;
	m_SettingsRuntimeMetadata.m_LastTClientTab = CanonicalizePersistedTClientTab(SessionCache.m_LastTClientTab >= 0 ? SessionCache.m_LastTClientTab : 0);
	m_SettingsRuntimeMetadata.m_LastQmTab = CanonicalizePersistedQmClientTab(SessionCache.m_LastQmTab >= 0 ? SessionCache.m_LastQmTab : 0);
	m_SettingsRuntimeMetadata.m_LastScrollPage = SessionCache.m_Valid && RuntimeKeyMatches ? SETTINGS_TCLIENT : -1;
	m_SettingsRuntimeMetadata.m_LastScrollY = RuntimeKeyMatches ? SessionCache.m_LastScrollY : 0.0f;
	m_SettingsRuntimeMetadata.m_RuntimeKey = CurrentRuntimeKey;
	m_SettingsRuntimeMetadata.m_Valid = SessionCache.m_Valid && RuntimeKeyMatches;
	if(m_SettingsRuntimeMetadata.m_LastPage == SETTINGS_CONFIGS)
	{
		m_SettingsRuntimeMetadata.m_LastPage = SETTINGS_QMCLIENT;
		m_SettingsRuntimeMetadata.m_LastQmTab = QMCLIENT_SETTINGS_TAB_CONFIG;
	}
	else if(m_SettingsRuntimeMetadata.m_LastPage == SETTINGS_CONTRIBUTORS)
	{
		m_SettingsRuntimeMetadata.m_LastPage = SETTINGS_QMCLIENT;
		m_SettingsRuntimeMetadata.m_LastQmTab = QMCLIENT_SETTINGS_TAB_CONTRIBUTORS;
	}
	if(SessionCache.m_LastTClientTab >= 0)
		m_TClientSettingsTab = CanonicalizePersistedTClientTab(SessionCache.m_LastTClientTab);
	m_SettingsTClientCurrentScrollY = RuntimeKeyMatches ? SessionCache.m_LastScrollY : 0.0f;
	m_SettingsTClientScrollRestorePending = SessionCache.m_Valid && RuntimeKeyMatches;
	if(SessionCache.m_LastQmTab >= 0)
		m_QmClientSettingsTab = CanonicalizePersistedQmClientTab(SessionCache.m_LastQmTab);
}

void CMenus::SaveSettingsRuntimeCacheMetadata()
{
	if(m_SettingsRuntimeMetadata.m_LastPage < 0 && g_Config.m_UiSettingsPage >= 0)
		m_SettingsRuntimeMetadata.m_LastPage = g_Config.m_UiSettingsPage;
	m_SettingsRuntimeMetadata.m_LastQmTab = CanonicalizePersistedQmClientTab(m_QmClientSettingsTab);
	m_SettingsRuntimeMetadata.m_LastTClientTab = CanonicalizePersistedTClientTab(m_TClientSettingsTab);
	if(m_SettingsRuntimeMetadata.m_LastPage >= 0)
		m_SettingsRuntimeMetadata.m_Valid = true;
	SSessionUiCache SessionCache;
	CUIRect CacheView = Ui()->Screen() != nullptr ? TClientSettingsContentView(*Ui()->Screen()) : CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	SessionCache.m_RuntimeKey = MakeSettingsSectionRuntimeKey(CacheView, Graphics());
	m_SettingsRuntimeMetadata.m_RuntimeKey = ToSettingsRuntimeCacheKey(SessionCache.m_RuntimeKey);
	SessionCache.m_LastSettingsPage = m_SettingsRuntimeMetadata.m_LastPage;
	SessionCache.m_LastTClientTab = m_SettingsRuntimeMetadata.m_LastTClientTab;
	SessionCache.m_LastQmTab = m_SettingsRuntimeMetadata.m_LastQmTab;
	SessionCache.m_LastScrollY = m_SettingsRuntimeMetadata.m_LastScrollPage == SETTINGS_TCLIENT ? m_SettingsRuntimeMetadata.m_LastScrollY : 0.0f;
	SessionCache.m_Valid = m_SettingsRuntimeMetadata.m_Valid;
	CSectionLoader::SaveSessionCache(SessionCache, SETTINGS_RUNTIME_CACHE_METADATA_FILE, Storage());
}

void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const float SmallSize = ResolveSettingsContentMetrics(MainView.w).m_SmallSize;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	IUiContext TClientBindWheelTextInputCtx = SettingsUiContext("settings_tclient_bindwheel_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientBindWheelTextInputCtx.m_pAnim = nullptr;
		TClientBindWheelTextInputCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_BindWheelSettingsScrollRegion;
	static char s_aBindName[BINDWHEEL_MAX_NAME];
	static char s_aBindCommand[BINDWHEEL_MAX_CMD];
	static int s_SelectedBindIndex = -1;
	const float EditorContentHeight = LineSize * 7.0f + SmallSize + MarginSmall * 4.0f;
	const float PreviewContentHeight = 280.0f;
	const auto MeasureEditor = [EditorContentHeight](float) { return EditorContentHeight; };
	const auto RenderEditor = [this, &TClientBindWheelTextInputCtx, ReadOnly, SmallSize](CUIRect &Content) {
		CPerfTimer EditorTimer;
		CUIRect LeftView = Content, Label, Button;
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Button.VSplitLeft(100.0f, &Label, &Button);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-name-label", &Label, Localize("Name:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_NameInput;
		s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
		s_NameInput.SetEmptyText(Localize("Name"));
		if(!ReadOnly)
			ui_widget::InputField(TClientBindWheelTextInputCtx, &s_NameInput, Button, Localize("Name"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-name-value", &Button, s_aBindName, FontSize, TEXTALIGN_ML);

		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Button.VSplitLeft(100.0f, &Label, &Button);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-command-label", &Label, Localize("Command:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindInput;
		s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
		s_BindInput.SetEmptyText(Localize("Command"));
		if(!ReadOnly)
			ui_widget::InputField(TClientBindWheelTextInputCtx, &s_BindInput, Button, Localize("Command"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-command-value", &Button, s_aBindCommand, FontSize, TEXTALIGN_ML);

		static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_OverrideButton, "tclient-bindwheel-override-selected", Localize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
		{
			CBindWheel::CBind TempBind;
			str_copy(TempBind.m_aName, str_length(s_aBindName) == 0 ? "*" : s_aBindName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-override-selected", &Button, Localize("Override Selected"), FontSize, TEXTALIGN_MC);

		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		CUIRect ButtonAdd, ButtonRemove;
		Button.VSplitMid(&ButtonRemove, &ButtonAdd, MarginSmall);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton, "tclient-bindwheel-add-bind", Localize("Add Bind"), 0, &ButtonAdd))
		{
			CBindWheel::CBind TempBind;
			str_copy(TempBind.m_aName, str_length(s_aBindName) == 0 ? "*" : s_aBindName);
			GameClient()->m_BindWheel.AddBind(TempBind.m_aName, s_aBindCommand);
			s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()) - 1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-add-bind", &ButtonAdd, Localize("Add Bind"), FontSize, TEXTALIGN_MC);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_RemoveButton, "tclient-bindwheel-remove-bind", Localize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
		{
			GameClient()->m_BindWheel.RemoveBind(s_SelectedBindIndex);
			s_SelectedBindIndex = -1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-remove-bind", &ButtonRemove, Localize("Remove Bind"), FontSize, TEXTALIGN_MC);

		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Label, &LeftView);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-footer-console", &Label, Localize("Commands run in the console"), FontSize, TEXTALIGN_ML);
		LeftView.HSplitTop(SmallSize, &Label, &LeftView);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-footer-mouse", &Label, Localize("L select  R swap  M select only"), SmallSize, TEXTALIGN_ML);

		LeftView.HSplitBottom(LineSize, &LeftView, &Label);
		static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
		if(!ReadOnly)
			DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, Localize("Bind Wheel Key"), "+bindwheel");
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-key", &Label, Localize("Bind Wheel Key"), FontSize, TEXTALIGN_ML);
		LeftView.HSplitBottom(LineSize, &LeftView, &Label);
		CUIRect CheckBoxRect;
		Label.HSplitTop(LineSize, &CheckBoxRect, &Label);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &g_Config.m_TcResetBindWheelMouse, "tclient-bindwheel-reset-mouse", Localize("Reset position of mouse when opening bindwheel"), g_Config.m_TcResetBindWheelMouse, &CheckBoxRect))
			g_Config.m_TcResetBindWheelMouse ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-reset-mouse", &CheckBoxRect, Localize("Reset position of mouse when opening bindwheel"), FontSize, TEXTALIGN_ML);
		LogTClientPerfStage("tclient_bindwheel_editor", EditorTimer.ElapsedMs(), false);
	};
	const auto MeasurePreview = [PreviewContentHeight](float) { return PreviewContentHeight; };
	const auto RenderPreview = [this, ReadOnly](CUIRect RightView) {
		if(ReadOnly)
			return;
		CPerfTimer WheelTimer;
		const float Radius = minimum(RightView.w, RightView.h) / 2.0f;
		const vec2 Center = RightView.Center();
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
		Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
		Graphics()->QuadsEnd();

		int HoveringIndex = -1;
		const int SegmentCount = GameClient()->m_BindWheel.m_vBinds.size();
		const float MouseDist = distance(Center, Ui()->MousePos());
		if(MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
		{
			float HoveringAngle = angle(Ui()->MousePos() - Center) + pi / SegmentCount;
			if(HoveringAngle < 0.0f)
				HoveringAngle += 2.0f * pi;
			HoveringIndex = std::clamp((int)(HoveringAngle / (2.0f * pi) * SegmentCount), 0, SegmentCount - 1);
			if(!ReadOnly && Ui()->MouseButtonClicked(0))
			{
				s_SelectedBindIndex = HoveringIndex;
				str_copy(s_aBindName, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName);
				str_copy(s_aBindCommand, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand);
			}
			else if(!ReadOnly && Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < SegmentCount && HoveringIndex != s_SelectedBindIndex)
			{
				std::swap(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex], GameClient()->m_BindWheel.m_vBinds[HoveringIndex]);
			}
			else if(!ReadOnly && Ui()->MouseButtonClicked(2))
				s_SelectedBindIndex = HoveringIndex;
		}
		else if(!ReadOnly && MouseDist < Radius && Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = -1;
			str_copy(s_aBindName, "");
			str_copy(s_aBindCommand, "");
		}

		const float Theta = pi * 2.0f / std::max<float>(1.0f, SegmentCount);
		for(int Index = 0; Index < SegmentCount; ++Index)
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			float SegmentFontSize = FontSize * 1.1f;
			if(Index == s_SelectedBindIndex)
			{
				SegmentFontSize = FontSize * 1.7f;
				TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
			}
			else if(Index == HoveringIndex)
				SegmentFontSize = FontSize * 1.35f;
			const vec2 Pos = direction(Theta * Index) * (Radius * 0.75f) + Center;
			const CUIRect Rect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
			Ui()->DoLabel(&Rect, GameClient()->m_BindWheel.m_vBinds[Index].m_aName, SegmentFontSize, TEXTALIGN_MC);
		}
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "count=%d", SegmentCount);
		LogTClientPerfStage("tclient_bindwheel_wheel", WheelTimer.ElapsedMs(), false, aExtra);
	};
	static std::array<CTClientSettingsCardFrameBinding, 2> s_aCardBindings;
	s_aCardBindings[0].Bind(MeasureEditor, RenderEditor);
	s_aCardBindings[1].Bind(MeasurePreview, RenderPreview);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(s_aCardBindings.size());
		constexpr std::array<std::pair<const char *, const char *>, 2> aSpecs = {{
			{"deck:tclient-bind-wheel-editor", "Bind Wheel"},
			{"deck:tclient-bind-wheel-preview", "Preview"},
		}};
		for(size_t Index = 0; Index < aSpecs.size(); ++Index)
		{
			CTClientSettingsCardFrameBinding *pBinding = &s_aCardBindings[Index];
			SSettingsCardDefinition Definition;
			Definition.m_Spec = {aSpecs[Index].first, Localize(aSpecs[Index].second), qm_card_registry::ResolveLocalizedDescription(aSpecs[Index].first)};
			Definition.m_Measure = [pBinding](float ContentWidth) { return pBinding->Measure(ContentWidth); };
			Definition.m_RenderMeasured = [pBinding](CUIRect &Content) { pBinding->Render(Content); };
			vCards.push_back(std::move(Definition));
		}
	};
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-bind-wheel"));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_BindWheelPrewarmOrderModel;
	static bool s_BindWheelPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_BindWheelPrewarmDeck;
	if(ReadOnly && !s_BindWheelPrewarmOrderModelInitialized)
	{
		s_BindWheelPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_BindWheelPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_BindWheelPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_BindWheelPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-bind-wheel-") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(TClientBindWheelTextInputCtx, Page, "tclient-bind-wheel", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_BindWheelSettingsScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	for(CTClientSettingsCardFrameBinding &Binding : s_aCardBindings)
		Binding.Clear();
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	IUiContext TClientChatBindsTextInputCtx = SettingsUiContext("settings_tclient_chatbinds_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientChatBindsTextInputCtx.m_pAnim = nullptr;
		TClientChatBindsTextInputCtx.m_pTree = nullptr;
	}

	static CScrollRegion s_ChatBindsScrollRegion;
	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindDefault &BindDefault) {
		CUIRect Button, Input, Title;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName = pOldBind == nullptr ? s_aTempName : pOldBind->m_aName;
		Button.VSplitLeft(210.0f, &Title, &Input);
		CUIElement &TitleElement = SettingsTextElement(SETTINGS_TCLIENT, TCLIENT_TAB_BINDCHAT, BindDefault.m_pTitle);
		DoSettingsLabelStreamed(TitleElement, &Title, Localize(BindDefault.m_pTitle), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Title.w));
		BindDefault.m_LineInput.SetBuffer(pName, BINDCHAT_MAX_NAME);
		BindDefault.m_LineInput.SetEmptyText(BindDefault.m_Bind.m_aName);
		if(!ReadOnly && ui_widget::InputField(TClientChatBindsTextInputCtx, &BindDefault.m_LineInput, Input, BindDefault.m_Bind.m_aName, EditBoxFontSize) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName);
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
				GameClient()->m_BindChat.RemoveBind(pName);
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDCHAT, BindDefault.m_Bind.m_aName, &Input, pName, FontSize, TEXTALIGN_ML);
	};

	constexpr const char *apStableIds[] = {
		"deck:tclient-chat-binds-kaomoji",
		"deck:tclient-chat-binds-warlist",
		"deck:tclient-chat-binds-other",
	};
	static std::array<CTClientSettingsCardFrameBinding, std::size(apStableIds)> s_aCardBindings;
	const auto MeasureCard = [](size_t Index, float) {
		return CBindChat::BIND_DEFAULTS[Index].second.size() * (MarginSmall + LineSize);
	};
	const auto RenderCard = [&](size_t Index, CUIRect &Content) {
		for(CBindChat::CBindDefault &BindDefault : CBindChat::BIND_DEFAULTS[Index].second)
			DoBindchatDefault(Content, BindDefault);
	};
	const size_t CardCount = minimum(CBindChat::BIND_DEFAULTS.size(), std::size(apStableIds));
	for(size_t Index = 0; Index < CardCount; ++Index)
		s_aCardBindings[Index].BindIndexed(MeasureCard, RenderCard, Index);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(CardCount);
		for(size_t Index = 0; Index < CardCount; ++Index)
		{
			CTClientSettingsCardFrameBinding *pBinding = &s_aCardBindings[Index];
			SSettingsCardDefinition Definition;
			Definition.m_Spec = {apStableIds[Index], Localize(CBindChat::BIND_DEFAULTS[Index].first), qm_card_registry::ResolveLocalizedDescription(apStableIds[Index])};
			Definition.m_Measure = [pBinding](float ContentWidth) { return pBinding->Measure(ContentWidth); };
			Definition.m_RenderMeasured = [pBinding](CUIRect &Content) { pBinding->Render(Content); };
			vCards.push_back(std::move(Definition));
		}
	};
	const uint64_t DynamicRevision = HashValueFnv1a64(1469598103934665603ull, CardCount);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-chat-binds", DynamicRevision));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_ChatBindsPrewarmOrderModel;
	static bool s_ChatBindsPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_ChatBindsPrewarmDeck;
	if(ReadOnly && !s_ChatBindsPrewarmOrderModelInitialized)
	{
		s_ChatBindsPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_ChatBindsPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_ChatBindsPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_ChatBindsPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-chat-binds-") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(TClientChatBindsTextInputCtx, Page, "tclient-chat-binds", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ChatBindsScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	for(size_t Index = 0; Index < CardCount; ++Index)
		s_aCardBindings[Index].Clear();
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogTClientPerfStage("tclient_chatbinds_total", RenderTimer.ElapsedMs(), false);
}

void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const SSettingsContentMetrics WarListMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float ListRowHeight = WarListMetrics.m_ListRowHeight;
	constexpr int WarListViewportRows = 8;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());

	static char s_aEntryName[MAX_NAME_LENGTH];
	static char s_aEntryClan[MAX_CLAN_LENGTH];
	static char s_aEntryReason[MAX_WARLIST_REASON_LENGTH];
	static bool s_IsClan = false;
	static bool s_IsName = true;
	static CWarEntry *s_pSelectedEntry = nullptr;
	static CWarType *s_pSelectedType = nullptr;
	static char s_aTypeName[MAX_WARLIST_TYPE_LENGTH];
	static ColorRGBA s_GroupColor = ColorRGBA(1, 1, 1, 1);
	static CLineInputBuffered<128> s_EntriesFilterInput;
	static CLineInputBuffered<128> s_PlayerSearchInput;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	static CLineInput s_ReasonInput;
	static CLineInput s_TypeNameInput;
	static CScrollRegion s_WarListScrollRegion;
	CWarEntry *pSelectedEntry = s_pSelectedEntry;
	CWarType *pSelectedType = s_pSelectedType;

	auto WarTypeExists = [&](const CWarType *pType) {
		return std::find(GameClient()->m_WarList.m_WarTypes.begin(), GameClient()->m_WarList.m_WarTypes.end(), pType) != GameClient()->m_WarList.m_WarTypes.end();
	};
	auto DefaultWarType = [&]() -> CWarType * {
		return GameClient()->m_WarList.m_WarTypes.empty() ? nullptr : GameClient()->m_WarList.m_WarTypes[0];
	};
	if(pSelectedType == nullptr || !WarTypeExists(pSelectedType))
		pSelectedType = DefaultWarType();
	auto WarEntryExists = [&](const CWarEntry *pEntry) {
		return pEntry != nullptr && std::find_if(GameClient()->m_WarList.m_vWarEntries.begin(), GameClient()->m_WarList.m_vWarEntries.end(),
						    [pEntry](const CWarEntry &Entry) { return &Entry == pEntry; }) != GameClient()->m_WarList.m_vWarEntries.end();
	};
	if(!WarEntryExists(pSelectedEntry))
		pSelectedEntry = nullptr;

	IUiContext TClientWarListTextInputCtx = SettingsUiContext("settings_tclient_warlist_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientWarListTextInputCtx.m_pAnim = nullptr;
		TClientWarListTextInputCtx.m_pTree = nullptr;
	}
	IUiContext TClientWarListEntriesSearchCtx = TClientWarListTextInputCtx;
	TClientWarListEntriesSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_warlist_entries_search");
	IUiContext TClientWarListPlayerSearchCtx = TClientWarListTextInputCtx;
	TClientWarListPlayerSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_warlist_player_search");

	auto RenderEntries = [&](CUIRect &Column) {
		CPerfTimer ListTimer;
		CUIRect Button, EntriesSearch;
		Column.HSplitTop(LineSize, &Button, &Column);
		Button.VSplitRight(25.0f, nullptr, &Button);

		static CButtonContainer s_ReverseEntries;
		static bool s_Reversed = true;
		if(!ReadOnly && Ui()->DoButton_FontIcon(&s_ReverseEntries, s_Reversed ? FONT_ICON_CHEVRON_UP : FONT_ICON_CHEVRON_DOWN, 0, &Button, IGraphics::CORNER_ALL))
			s_Reversed = !s_Reversed;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &EntriesSearch, &Column);
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListEntriesSearchCtx, &s_EntriesFilterInput, EntriesSearch, FontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-entries-search", &EntriesSearch, s_EntriesFilterInput.GetString(), FontSize, TEXTALIGN_ML);
		static std::vector<CWarEntry *> s_vFilteredEntries;
		static char s_aCachedEntriesFilter[128] = "";
		static bool s_CachedReversed = false;
		static int s_CachedWarEntriesRevision = -1;
		if(str_comp(s_aCachedEntriesFilter, s_EntriesFilterInput.GetString()) != 0 ||
			s_CachedReversed != s_Reversed ||
			s_CachedWarEntriesRevision != s_TClientWarListFilterRevision)
		{
			s_vFilteredEntries.clear();
			for(CWarEntry &Entry : GameClient()->m_WarList.m_vWarEntries)
			{
				if(str_find_nocase(Entry.m_aName, s_EntriesFilterInput.GetString()))
					s_vFilteredEntries.push_back(&Entry);
				else if(str_find_nocase(Entry.m_aClan, s_EntriesFilterInput.GetString()))
					s_vFilteredEntries.push_back(&Entry);
				else if(str_find_nocase(Entry.m_pWarType->m_aWarName, s_EntriesFilterInput.GetString()))
					s_vFilteredEntries.push_back(&Entry);
			}
			if(s_Reversed)
				std::reverse(s_vFilteredEntries.begin(), s_vFilteredEntries.end());
			str_copy(s_aCachedEntriesFilter, s_EntriesFilterInput.GetString(), sizeof(s_aCachedEntriesFilter));
			s_CachedReversed = s_Reversed;
			s_CachedWarEntriesRevision = s_TClientWarListFilterRevision;
		}

		int SelectedOldEntry = -1;
		static CListBox s_EntriesListBox;
		static CListBox s_EntriesReadOnlyListBox;
		CListBox &EntriesListBox = ReadOnly ? s_EntriesReadOnlyListBox : s_EntriesListBox;
		EntriesListBox.SetActive(!ReadOnly);
		EntriesListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		EntriesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		EntriesListBox.DoStart(ListRowHeight, s_vFilteredEntries.size(), 1, 2, SelectedOldEntry, &Column);

		static std::vector<unsigned char> s_vItemIds;
		static std::vector<CButtonContainer> s_vDeleteButtons;
		const int MaxEntries = GameClient()->m_WarList.m_vWarEntries.size();
		s_vItemIds.resize(MaxEntries);
		s_vDeleteButtons.resize(MaxEntries);

		CWarEntry *pEntryToRemove = nullptr;
		for(size_t i = 0; i < s_vFilteredEntries.size(); i++)
		{
			CWarEntry *pEntry = s_vFilteredEntries[i];
			if(pSelectedEntry && pEntry == pSelectedEntry)
				SelectedOldEntry = (int)i;

			const CListboxItem Item = EntriesListBox.DoNextItem(&s_vItemIds[i], SelectedOldEntry >= 0 && (size_t)SelectedOldEntry == i);
			if(!Item.m_Visible)
				continue;

			CUIRect EntryRect, DeleteButton, EntryTypeRect, WarType, ToolTip;
			Item.m_Rect.Margin(0.0f, &EntryRect);
			EntryRect.VSplitLeft(26.0f, &DeleteButton, &EntryRect);
			DeleteButton.HMargin(7.5f, &DeleteButton);
			DeleteButton.VSplitLeft(MarginSmall, nullptr, &DeleteButton);
			DeleteButton.VSplitRight(MarginExtraSmall, &DeleteButton, nullptr);
			if(!ReadOnly && Ui()->DoButton_FontIcon(&s_vDeleteButtons[i], FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
			{
				pEntryToRemove = pEntry;
			}

			bool IsClan = false;
			char aBuf[32];
			if(str_comp(pEntry->m_aClan, "") != 0)
			{
				str_copy(aBuf, pEntry->m_aClan);
				IsClan = true;
			}
			else
			{
				str_copy(aBuf, pEntry->m_aName);
			}
			EntryRect.VSplitLeft(35.0f, &EntryTypeRect, &EntryRect);
			if(!ReadOnly)
			{
				if(IsClan)
					RenderFontIcon(EntryTypeRect, FONT_ICON_USERS, 18.0f, TEXTALIGN_MC);
				else
					RenderDevSkin(EntryTypeRect.Center(), ListRowHeight, "default", "default", false, 0, 0, 0, false, false);
			}

			if(str_comp(pEntry->m_aReason, "") != 0)
			{
				EntryRect.VSplitRight(20.0f, &EntryRect, &ToolTip);
				if(!ReadOnly)
					RenderFontIcon(ToolTip, FONT_ICON_COMMENT, 18.0f, TEXTALIGN_MC);
				GameClient()->m_Tooltips.DoToolTip(&s_vItemIds[i], &ToolTip, pEntry->m_aReason);
				GameClient()->m_Tooltips.SetFadeTime(&s_vItemIds[i], 0.0f);
			}

			EntryRect.HMargin(MarginExtraSmall, &EntryRect);
			EntryRect.HSplitMid(&EntryRect, &WarType, MarginSmall);
			DoTClientLabel(Ui(), &EntryRect, aBuf, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(pEntry->m_pWarType->m_Color);
			DoTClientLabel(Ui(), &WarType, pEntry->m_pWarType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		const int NewSelectedEntry = EntriesListBox.DoEnd();
		if(!ReadOnly && pEntryToRemove != nullptr)
		{
			GameClient()->m_WarList.RemoveWarEntry(pEntryToRemove);
			pSelectedEntry = nullptr;
			++s_TClientWarListFilterRevision;
		}
		else if(!ReadOnly && NewSelectedEntry >= 0 && NewSelectedEntry < (int)s_vFilteredEntries.size() &&
			(SelectedOldEntry != NewSelectedEntry || (Ui()->HotItem() == &s_vItemIds[NewSelectedEntry] && Ui()->MouseButtonClicked(0))))
		{
			pSelectedEntry = s_vFilteredEntries[NewSelectedEntry];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aEntryName, pSelectedEntry->m_aName);
				str_copy(s_aEntryClan, pSelectedEntry->m_aClan);
				str_copy(s_aEntryReason, pSelectedEntry->m_aReason);
				if(str_comp(pSelectedEntry->m_aClan, "") != 0)
				{
					s_IsName = false;
					s_IsClan = true;
				}
				else
				{
					s_IsName = true;
					s_IsClan = false;
				}
				pSelectedType = pSelectedEntry->m_pWarType;
			}
		}

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "entries=%d filtered=%d", (int)GameClient()->m_WarList.m_vWarEntries.size(), (int)s_vFilteredEntries.size());
		LogTClientPerfStageEx("tclient_warlist", "list", ETClientSettingsPerfStage::TEXT_CACHE, ListTimer.ElapsedMs(), false, aExtra);
	};

	auto RenderEditor = [&](CUIRect &Column) {
		CPerfTimer FilterTimer;
		CUIRect Button, ButtonL, ButtonR;
		Column.HSplitTop(LineSize, &Button, &Column);

		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
		s_NameInput.SetEmptyText(Localize("Name"));
		if(!ReadOnly && s_IsName)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_NameInput, ButtonL, Localize("Name"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-name", &ButtonL, s_aEntryName, FontSize, TEXTALIGN_ML);

		s_ClanInput.SetBuffer(s_aEntryClan, sizeof(s_aEntryClan));
		s_ClanInput.SetEmptyText(Localize("Clan"));
		if(!ReadOnly && s_IsClan)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_ClanInput, ButtonR, Localize("Clan"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-clan", &ButtonR, s_aEntryClan, FontSize, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		static unsigned char s_NameRadio, s_ClanRadio;
		if(!ReadOnly && DoButton_CheckBox_Common(&s_NameRadio, Localize("Name"), s_IsName ? "X" : "", &ButtonL, BUTTONFLAG_LEFT))
		{
			s_IsName = true;
			s_IsClan = false;
		}
		if(!ReadOnly && DoButton_CheckBox_Common(&s_ClanRadio, Localize("Clan"), s_IsClan ? "X" : "", &ButtonR, BUTTONFLAG_LEFT))
		{
			s_IsName = false;
			s_IsClan = true;
		}
		if(!s_IsName)
			str_copy(s_aEntryName, "");
		if(!s_IsClan)
			str_copy(s_aEntryClan, "");

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		s_ReasonInput.SetBuffer(s_aEntryReason, sizeof(s_aEntryReason));
		s_ReasonInput.SetEmptyText(Localize("Reason"));
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_ReasonInput, Button, Localize("Reason"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-reason", &Button, s_aEntryReason, FontSize, TEXTALIGN_ML);

		static CButtonContainer s_AddButton, s_OverrideButton;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_OverrideButton, Localize("Override Entry"), 0, &ButtonL, LineSize) && pSelectedEntry)
		{
			if(pSelectedEntry && pSelectedType && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
			{
				str_copy(pSelectedEntry->m_aName, s_aEntryName);
				str_copy(pSelectedEntry->m_aClan, s_aEntryClan);
				str_copy(pSelectedEntry->m_aReason, s_aEntryReason);
				pSelectedEntry->m_pWarType = pSelectedType;
				++s_TClientWarListFilterRevision;
			}
		}
		if(!ReadOnly && DoButtonLineSize_Menu(&s_AddButton, Localize("Add Entry"), 0, &ButtonR, LineSize))
		{
			if(pSelectedType)
			{
				GameClient()->m_WarList.AddWarEntry(s_aEntryName, s_aEntryClan, s_aEntryReason, pSelectedType->m_aWarName);
				pSelectedEntry = nullptr;
				++s_TClientWarListFilterRevision;
			}
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column);
		if(pSelectedType)
		{
			float Shade = 0.0f;
			if(!ReadOnly)
				Button.Draw(ColorRGBA(Shade, Shade, Shade, 0.25f), 15, 3.0f);
			TextRender()->TextColor(pSelectedType->m_Color);
			Ui()->DoLabel(&Button, pSelectedType->m_aWarName, HeadlineFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		LogTClientPerfStageEx("tclient_warlist", "filter", ETClientSettingsPerfStage::INTERACTIVE_LAYER, FilterTimer.ElapsedMs());
	};

	auto RenderSettings = [&](CUIRect &Column) {
		CUIRect CheckBoxRect;
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_TcWarListAllowDuplicates, "tclient-warlist-allow-duplicates", Localize("Allow Duplicate Entries"), g_Config.m_TcWarListAllowDuplicates, &CheckBoxRect))
			g_Config.m_TcWarListAllowDuplicates ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_TcWarList, "tclient-warlist-enable", Localize("Enable warlist"), g_Config.m_TcWarList, &CheckBoxRect))
			g_Config.m_TcWarList ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListBlockEnemyChat, "tclient-warlist-block-enemy-chat", Localize("Block enemy chat"), g_Config.m_QmWarListBlockEnemyChat, &CheckBoxRect))
			g_Config.m_QmWarListBlockEnemyChat ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_TcWarListChat, "tclient-warlist-colors-chat", Localize("Colors in chat"), g_Config.m_TcWarListChat, &CheckBoxRect))
			g_Config.m_TcWarListChat ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_TcWarListScoreboard, "tclient-warlist-colors-scoreboard", Localize("Colors in scoreboard"), g_Config.m_TcWarListScoreboard, &CheckBoxRect))
			g_Config.m_TcWarListScoreboard ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_TcWarListSpectate, "tclient-warlist-colors-spectate", Localize("Show colors in spectator selection"), g_Config.m_TcWarListSpectate, &CheckBoxRect))
			g_Config.m_TcWarListSpectate ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_TcWarListShowClan, "tclient-warlist-show-clan", Localize("Show clan if war"), g_Config.m_TcWarListShowClan, &CheckBoxRect))
			g_Config.m_TcWarListShowClan ^= 1;
	};

	auto RenderGroups = [&](CUIRect &Column) {
		CPerfTimer ActionsTimer;
		CUIRect WarTypeList, Button, ButtonL, ButtonR;
		Column.HSplitTop(WarListViewportRows * ListRowHeight, &WarTypeList, &Column);
		m_pRemoveWarType = nullptr;
		int SelectedOldType = -1;
		static CListBox s_WarTypeListBox;
		static CListBox s_WarTypeReadOnlyListBox;
		CListBox &WarTypeListBox = ReadOnly ? s_WarTypeReadOnlyListBox : s_WarTypeListBox;
		WarTypeListBox.SetActive(!ReadOnly);
		WarTypeListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		WarTypeListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		WarTypeListBox.DoStart(ListRowHeight, GameClient()->m_WarList.m_WarTypes.size(), 1, 2, SelectedOldType, &WarTypeList, true, IGraphics::CORNER_ALL);

		static std::vector<unsigned char> s_vTypeItemIds;
		static std::vector<CButtonContainer> s_vTypeDeleteButtons;
		const int MaxTypes = GameClient()->m_WarList.m_WarTypes.size();
		s_vTypeItemIds.resize(MaxTypes);
		s_vTypeDeleteButtons.resize(MaxTypes);

		for(int i = 0; i < (int)GameClient()->m_WarList.m_WarTypes.size(); i++)
		{
			CWarType *pType = GameClient()->m_WarList.m_WarTypes[i];
			if(!pType)
				continue;
			if(pSelectedType && pType == pSelectedType)
				SelectedOldType = i;

			const CListboxItem Item = WarTypeListBox.DoNextItem(&s_vTypeItemIds[i], SelectedOldType >= 0 && SelectedOldType == i);
			if(!Item.m_Visible)
				continue;

			CUIRect TypeRect, DeleteButton;
			Item.m_Rect.Margin(0.0f, &TypeRect);
			if(pType->m_Removable)
			{
				TypeRect.VSplitRight(20.0f, &TypeRect, &DeleteButton);
				DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
				DeleteButton.Margin(2.0f, &DeleteButton);
				if(!ReadOnly && DoButtonNoRect_FontIcon(&s_vTypeDeleteButtons[i], FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
					m_pRemoveWarType = pType;
			}
			TextRender()->TextColor(pType->m_Color);
			DoTClientLabel(Ui(), &TypeRect, pType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		const int NewSelectedType = WarTypeListBox.DoEnd();
		const bool NewSelectedTypeValid = NewSelectedType >= 0 && NewSelectedType < (int)GameClient()->m_WarList.m_WarTypes.size();
		if(!ReadOnly && ((SelectedOldType != NewSelectedType && NewSelectedTypeValid) || (NewSelectedTypeValid && Ui()->HotItem() == &s_vTypeItemIds[NewSelectedType] && Ui()->MouseButtonClicked(0))))
		{
			pSelectedType = GameClient()->m_WarList.m_WarTypes[NewSelectedType];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aTypeName, pSelectedType->m_aWarName);
				s_GroupColor = pSelectedType->m_Color;
			}
		}
		if(!ReadOnly && m_pRemoveWarType != nullptr)
		{
			if(m_pRemoveWarType == pSelectedType)
				pSelectedType = DefaultWarType();
			char aMessage[256];
			str_format(aMessage, sizeof(aMessage),
				Localize("Are you sure that you want to remove '%s' from your war groups?"),
				m_pRemoveWarType->m_aWarName);
			PopupConfirm(Localize("Remove War Group"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmRemoveWarType);
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
		s_TypeNameInput.SetEmptyText(Localize("Group name"));
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_TypeNameInput, Button, Localize("Group name"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-group-name", &Button, s_aTypeName, FontSize, TEXTALIGN_ML);
		static CButtonContainer s_AddGroupButton, s_OverrideGroupButton, s_GroupColorPicker;

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static unsigned int s_ColorValue = 0;
		s_ColorValue = color_cast<ColorHSLA>(s_GroupColor).Pack(false);
		if(!ReadOnly)
			s_GroupColor = color_cast<ColorRGBA>(DoLine_ColorPicker(&s_GroupColorPicker, CurrentSettingsContentMetrics(), &Column, Localize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), true));
		else
			Column.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, nullptr, &Column);

		Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		bool OverrideDisabled = NewSelectedType == 0;
		if(!ReadOnly && DoButtonLineSize_Menu(&s_OverrideGroupButton, Localize("Override Group"), 0, &ButtonL, LineSize, OverrideDisabled) && pSelectedType)
		{
			if(pSelectedType && str_comp(s_aTypeName, "") != 0)
			{
				str_copy(pSelectedType->m_aWarName, s_aTypeName);
				pSelectedType->m_Color = s_GroupColor;
				++s_TClientWarListFilterRevision;
			}
		}
		bool AddDisabled = str_comp(GameClient()->m_WarList.FindWarType(s_aTypeName)->m_aWarName, "none") != 0 || str_comp(s_aTypeName, "none") == 0;
		if(!ReadOnly && DoButtonLineSize_Menu(&s_AddGroupButton, Localize("Add Group"), 0, &ButtonR, LineSize, AddDisabled))
		{
			GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
			++s_TClientWarListFilterRevision;
		}

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "groups=%d", (int)GameClient()->m_WarList.m_WarTypes.size());
		LogTClientPerfStageEx("tclient_warlist", "actions", ETClientSettingsPerfStage::RESOURCE_PRETRIGGER, ActionsTimer.ElapsedMs(), false, aExtra);
	};

	auto RenderPlayers = [&](CUIRect &Column) {
		CPerfTimer PlayersTimer;
		CUIRect PlayerSearch, PlayerList;
		Column.HSplitTop(LineSize, &PlayerSearch, &Column);
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListPlayerSearchCtx, &s_PlayerSearchInput, PlayerSearch, FontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-player-search", &PlayerSearch, s_PlayerSearchInput.GetString(), FontSize, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		PlayerList = Column;
		static CListBox s_PlayerListBox;
		static CListBox s_PlayerReadOnlyListBox;
		CListBox &PlayerListBox = ReadOnly ? s_PlayerReadOnlyListBox : s_PlayerListBox;
		PlayerListBox.SetActive(!ReadOnly);
		PlayerListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		PlayerListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		static std::vector<int> s_vFilteredPlayerIds;
		s_vFilteredPlayerIds.clear();
		s_vFilteredPlayerIds.reserve(MAX_CLIENTS);
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId])
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) ||
				str_find_nocase(Client.m_aClan, s_PlayerSearchInput.GetString()))
				s_vFilteredPlayerIds.push_back(ClientId);
		}
		PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size(), 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL);

		static std::vector<unsigned char> s_vPlayerItemIds;
		static std::vector<CButtonContainer> s_vNameButtons;
		static std::vector<CButtonContainer> s_vClanButtons;
		s_vPlayerItemIds.resize(MAX_CLIENTS);
		s_vNameButtons.resize(MAX_CLIENTS);
		s_vClanButtons.resize(MAX_CLIENTS);

		for(const int ClientId : s_vFilteredPlayerIds)
		{
			const auto &Client = GameClient()->m_aClients[ClientId];

			const CListboxItem Item = PlayerListBox.DoNextItem(&s_vPlayerItemIds[ClientId], false);
			if(!Item.m_Visible)
				continue;

			CUIRect PlayerRect, TeeRect, NameRect, ClanRect;
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			Item.m_Rect.Margin(0.0f, &PlayerRect);
			PlayerRect.VSplitLeft(25.0f, &TeeRect, &PlayerRect);
			PlayerRect.VSplitMid(&NameRect, &ClanRect);
			PlayerRect = NameRect;
			PlayerRect.x = TeeRect.x;
			PlayerRect.w += TeeRect.w;
			TextRender()->TextColor(GameClient()->m_WarList.GetWarData(ClientId).m_NameColor);
			ColorRGBA NameButtonColor = Ui()->CheckActiveItem(&s_vNameButtons[ClientId]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
												       (Ui()->HotItem() == &s_vNameButtons[ClientId] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			if(!ReadOnly && NameButtonColor.a > 0.0f)
				DrawRoundedSurface(Ui(), PlayerRect, NameButtonColor, ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_L);
			Ui()->DoLabel(&NameRect, Client.m_aName, StandardFontSize, TEXTALIGN_ML);
			if(!ReadOnly && Ui()->DoButtonLogic(&s_vNameButtons[ClientId], false, &PlayerRect, BUTTONFLAG_LEFT))
			{
				s_IsName = true;
				s_IsClan = false;
				str_copy(s_aEntryName, Client.m_aName);
			}

			TextRender()->TextColor(GameClient()->m_WarList.GetWarData(ClientId).m_ClanColor);
			ColorRGBA ClanButtonColor = Ui()->CheckActiveItem(&s_vClanButtons[ClientId]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
												       (Ui()->HotItem() == &s_vClanButtons[ClientId] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			if(!ReadOnly && ClanButtonColor.a > 0.0f)
				DrawRoundedSurface(Ui(), ClanRect, ClanButtonColor, ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_R);
			Ui()->DoLabel(&ClanRect, Client.m_aClan, StandardFontSize, TEXTALIGN_ML);
			if(!ReadOnly && Ui()->DoButtonLogic(&s_vClanButtons[ClientId], false, &ClanRect, BUTTONFLAG_LEFT))
			{
				s_IsName = false;
				s_IsClan = true;
				str_copy(s_aEntryClan, Client.m_aClan);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
			TeeInfo.m_Size = ListRowHeight;
			if(!ReadOnly)
				RenderTeeCute(CAnimState::GetIdle(), &TeeInfo, 0, vec2(1.0f, 0.0f), TeeRect.Center() + vec2(-1.0f, 2.5f), true);
		}
		PlayerListBox.DoEnd();

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "players=%d filtered=%d", MAX_CLIENTS, (int)s_vFilteredPlayerIds.size());
		LogTClientPerfStageEx("tclient_warlist", "players", ETClientSettingsPerfStage::STATIC_LAYER, PlayersTimer.ElapsedMs(), false, aExtra);
	};

	const float WarListColumnMinimum = ResolveSettingsInlineRowMinimumWidth(WarListMetrics.m_LabelWidth + 2.0f * WarListMetrics.m_ButtonHeight, WarListMetrics.m_SectionGap, 1);
	const float FourColumnMinWidth = 4.0f * WarListColumnMinimum + 3.0f * WarListMetrics.m_SectionGap;
	const float TwoColumnMinWidth = 2.0f * WarListColumnMinimum + WarListMetrics.m_SectionGap;
	const float EntriesHeight = LineSize * 2.0f + MarginSmall + WarListViewportRows * ListRowHeight;
	const float GroupsHeight = WarListViewportRows * ListRowHeight + MarginSmall * 2.0f + LineSize * 3.0f + ColorPickerLineSize + ColorPickerLineSpacing;
	const float PlayersHeight = LineSize + MarginSmall + WarListViewportRows * ListRowHeight;
	const float EditorHeight = LineSize * 5.0f + MarginSmall * 5.0f + HeadlineFontSize;
	const float SettingsHeight = LineSize * 7.0f + MarginSmall * 6.0f;
	const float SectionHeaderHeight = LineSize + MarginSmall;
	const float SectionGap = WarListMetrics.m_SectionGap;
	const char *pWarEntriesTitle = Localizable("War Entries");
	const char *pSettingsTitle = Localizable("Settings");
	const char *pEditEntryTitle = Localizable("Edit Entry");
	const char *pWarGroupsTitle = Localizable("War Groups");
	const char *pOnlinePlayersTitle = Localizable("Online Players");
	const auto SectionHeight = [SectionHeaderHeight](float ContentHeight) {
		return SectionHeaderHeight + ContentHeight;
	};
	const auto WarListContentHeight = [&](float ContentWidth) {
		const float EntriesSectionHeight = SectionHeight(EntriesHeight);
		const float EditorSectionHeight = SectionHeight(EditorHeight);
		const float SettingsSectionHeight = SectionHeight(SettingsHeight);
		const float GroupsSectionHeight = SectionHeight(GroupsHeight);
		const float PlayersSectionHeight = SectionHeight(PlayersHeight);
		if(ContentWidth >= FourColumnMinWidth)
			return maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight,
				maximum(EditorSectionHeight, maximum(GroupsSectionHeight, PlayersSectionHeight)));
		if(ContentWidth >= TwoColumnMinWidth)
			return maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight, EditorSectionHeight) + SectionGap +
			       maximum(GroupsSectionHeight, PlayersSectionHeight);
		return EntriesSectionHeight + SectionGap + SettingsSectionHeight + SectionGap + EditorSectionHeight +
		       SectionGap + GroupsSectionHeight + SectionGap + PlayersSectionHeight;
	};
	const auto RenderWarListLayout = [&](CUIRect ContentRect, bool Render) {
		const auto RenderSection = [&](CUIRect &Column, const char *pTextId, const char *pTitle, float ContentHeight, const auto &RenderContent) {
			CUIRect Section, Header, Body;
			Column.HSplitTop(SectionHeight(ContentHeight), &Section, &Column);
			Section.HSplitTop(LineSize, &Header, &Body);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, pTextId, &Header, Localize(pTitle), HeadlineFontSize, TEXTALIGN_ML);
			Body.HSplitTop(MarginSmall, nullptr, &Body);
			if(Render)
				RenderContent(Body);
		};
		const auto AddSectionGap = [&](CUIRect &Column) {
			Column.HSplitTop(SectionGap, nullptr, &Column);
		};

		if(ContentRect.w >= FourColumnMinWidth)
		{
			CUIRect EntriesColumn, EditorColumn, GroupsColumn, PlayersColumn;
			const float ColumnGap = SectionGap;
			const float ColumnWidth = maximum(0.0f, (ContentRect.w - ColumnGap * 3.0f) / 4.0f);
			ContentRect.VSplitLeft(ColumnWidth, &EntriesColumn, &ContentRect);
			ContentRect.VSplitLeft(ColumnGap, nullptr, &ContentRect);
			ContentRect.VSplitLeft(ColumnWidth, &EditorColumn, &ContentRect);
			ContentRect.VSplitLeft(ColumnGap, nullptr, &ContentRect);
			ContentRect.VSplitLeft(ColumnWidth, &GroupsColumn, &ContentRect);
			ContentRect.VSplitLeft(ColumnGap, nullptr, &ContentRect);
			PlayersColumn = ContentRect;
			RenderSection(EntriesColumn, "tclient-warlist-section-entries", pWarEntriesTitle, EntriesHeight, RenderEntries);
			AddSectionGap(EntriesColumn);
			RenderSection(EntriesColumn, "tclient-warlist-section-settings", pSettingsTitle, SettingsHeight, RenderSettings);
			RenderSection(EditorColumn, "tclient-warlist-section-editor", pEditEntryTitle, EditorHeight, RenderEditor);
			RenderSection(GroupsColumn, "tclient-warlist-section-groups", pWarGroupsTitle, GroupsHeight, RenderGroups);
			RenderSection(PlayersColumn, "tclient-warlist-section-players", pOnlinePlayersTitle, PlayersHeight, RenderPlayers);
		}
		else if(ContentRect.w >= TwoColumnMinWidth)
		{
			const float FirstRowHeight = maximum(SectionHeight(EntriesHeight) + SectionGap + SectionHeight(SettingsHeight), SectionHeight(EditorHeight));
			CUIRect FirstRow, SecondRow, EntriesColumn, EditorColumn, GroupsColumn, PlayersColumn;
			ContentRect.HSplitTop(FirstRowHeight, &FirstRow, &SecondRow);
			SecondRow.HSplitTop(SectionGap, nullptr, &SecondRow);
			FirstRow.VSplitMid(&EntriesColumn, &EditorColumn, SectionGap);
			SecondRow.VSplitMid(&GroupsColumn, &PlayersColumn, SectionGap);
			RenderSection(EntriesColumn, "tclient-warlist-section-entries", pWarEntriesTitle, EntriesHeight, RenderEntries);
			AddSectionGap(EntriesColumn);
			RenderSection(EntriesColumn, "tclient-warlist-section-settings", pSettingsTitle, SettingsHeight, RenderSettings);
			RenderSection(EditorColumn, "tclient-warlist-section-editor", pEditEntryTitle, EditorHeight, RenderEditor);
			RenderSection(GroupsColumn, "tclient-warlist-section-groups", pWarGroupsTitle, GroupsHeight, RenderGroups);
			RenderSection(PlayersColumn, "tclient-warlist-section-players", pOnlinePlayersTitle, PlayersHeight, RenderPlayers);
		}
		else
		{
			RenderSection(ContentRect, "tclient-warlist-section-entries", pWarEntriesTitle, EntriesHeight, RenderEntries);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-settings", pSettingsTitle, SettingsHeight, RenderSettings);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-editor", pEditEntryTitle, EditorHeight, RenderEditor);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-groups", pWarGroupsTitle, GroupsHeight, RenderGroups);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-players", pOnlinePlayersTitle, PlayersHeight, RenderPlayers);
		}
	};

	const auto RenderWarListCard = [&](CUIRect &ContentRect) { RenderWarListLayout(ContentRect, true); };
	static CTClientSettingsCardFrameBinding s_CardBinding;
	s_CardBinding.Bind(WarListContentHeight, RenderWarListCard);
	const uint64_t MeasureRevision = ((uint64_t)GameClient()->m_WarList.m_vWarEntries.size() << 32) ^ (uint64_t)GameClient()->m_WarList.m_WarTypes.size();
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		SSettingsCardDefinition Definition;
		Definition.m_Spec = {"deck:tclient-warlist", Localize("War List"), qm_card_registry::ResolveLocalizedDescription("deck:tclient-warlist")};
		Definition.m_Measure = [](float ContentWidth) { return s_CardBinding.Measure(ContentWidth); };
		Definition.m_RenderMeasured = [](CUIRect &Content) { s_CardBinding.Render(Content); };
		Definition.m_MeasureRevision = MeasureRevision;
		vCards.push_back(std::move(Definition));
	};
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-warlist", MeasureRevision));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_WarListPrewarmOrderModel;
	static bool s_WarListPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_WarListPrewarmDeck;
	if(ReadOnly && !s_WarListPrewarmOrderModelInitialized)
	{
		s_WarListPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_WarListPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_WarListPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_WarListPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-warlist") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(TClientWarListTextInputCtx, Page, "tclient-warlist", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_WarListScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	s_CardBinding.Clear();
	if(!ReadOnly)
	{
		s_pSelectedEntry = pSelectedEntry;
		s_pSelectedType = pSelectedType;
	}
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();

	LogTClientPerfStage("tclient_warlist_total", RenderTimer.ElapsedMs(), false);
}

void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	IUiContext TClientStatusSchemeTextInputCtx = SettingsUiContext("settings_tclient_status_scheme_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientStatusSchemeTextInputCtx.m_pAnim = nullptr;
		TClientStatusSchemeTextInputCtx.m_pTree = nullptr;
	}

	static CScrollRegion s_StatusBarSettingsScrollRegion;
	static int s_SelectedItem = -1;
	static int s_TypeSelectedOld = -1;
	static CLineInput s_StatusScheme(g_Config.m_TcStatusBarScheme, sizeof(g_Config.m_TcStatusBarScheme));
	const int StatusBarCodeCount = (int)GameClient()->m_StatusBar.m_vStatusItemTypes.size();
	const int StatusBarItemCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
	const float SettingsContentHeight = LineSize * 7.0f + HeadlineHeight * 2.0f + ColorPickerLineSize * 2.0f + MarginSmall * 10.0f;
	const float StatusBarPreviewHeight = LineSize + MarginSmall * 2.0f;

	auto GetStatusBarEditorLabel = [](const CStatusItem *pItem) {
		return str_comp(pItem->m_aName, "Space") == 0 ? pItem->m_aName : pItem->m_aDisplayName;
	};
	auto RenderStatusBarPreview = [&](CUIRect PreviewRect, int MaxItems = -1) {
		DrawRoundedSurface(Ui(), PreviewRect, ColorRGBA(0, 0, 0, 0.5f), ColorRGBA(), 5.0f);
		PreviewRect.VSplitLeft(MarginExtraSmall, nullptr, &PreviewRect);
		const int TotalCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
		const int PreviewCount = MaxItems > 0 ? minimum(TotalCount, MaxItems) : TotalCount;
		if(TotalCount <= 0 || PreviewCount <= 0)
		{
			PreviewRect.Margin(10.0f, &PreviewRect);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-empty-preview", &PreviewRect, Localize("No status bar items"), FontSize, TEXTALIGN_ML);
			return;
		}

		const float ItemWidth = (PreviewRect.w - MarginSmall) / (float)PreviewCount;
		CUIRect PreviewItem;
		for(int i = 0; i < PreviewCount; ++i)
		{
			PreviewRect.VSplitLeft(ItemWidth, &PreviewItem, &PreviewRect);
			PreviewItem.HMargin(MarginSmall, &PreviewItem);
			PreviewItem.VMargin(MarginExtraSmall, &PreviewItem);
			DrawRoundedSurface(Ui(), PreviewItem, ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), ColorRGBA(), 5.0f);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &PreviewItem, Localize(GetStatusBarEditorLabel(GameClient()->m_StatusBar.m_StatusBarItems[i])), FontSize, TEXTALIGN_MC);
		}
	};
	auto RenderStatusBarCodes = [&](CUIRect View) {
		static std::vector<std::string> s_vCodeStorage;
		static std::vector<const char *> s_vCodes;
		static char s_aCodeLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
		if(s_vCodeStorage.size() != GameClient()->m_StatusBar.m_vStatusItemTypes.size() || str_comp(s_aCodeLanguage, g_Config.m_ClLanguagefile) != 0)
		{
			s_vCodeStorage.clear();
			s_vCodes.clear();
			s_vCodeStorage.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			s_vCodes.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			for(const CStatusItem &Item : GameClient()->m_StatusBar.m_vStatusItemTypes)
			{
				char aCode[256];
				const char *pLetters = str_comp(Item.m_aName, "Space") == 0 ? "_ or ' '" : Item.m_aLetters;
				str_format(aCode, sizeof(aCode), "%s = %s", pLetters, Localize(GetStatusBarEditorLabel(&Item)));
				s_vCodeStorage.emplace_back(aCode);
			}
			for(const std::string &Code : s_vCodeStorage)
				s_vCodes.push_back(Code.c_str());
			str_copy(s_aCodeLanguage, g_Config.m_ClLanguagefile);
		}
		const char *const *apCodes = s_vCodes.data();
		CUIRect Label;
		if(View.w > 360.0f)
		{
			CUIRect LeftCodes, RightCodes;
			View.VSplitMid(&LeftCodes, &RightCodes, MarginSmall);
			const int LeftCount = (StatusBarCodeCount + 1) / 2;
			for(int i = 0; i < StatusBarCodeCount; ++i)
			{
				CUIRect &Column = i < LeftCount ? LeftCodes : RightCodes;
				Column.HSplitTop(LineSize, &Label, &Column);
				Ui()->DoLabel(&Label, apCodes[i], FontSize, TEXTALIGN_ML);
				if(i + 1 < (i < LeftCount ? LeftCount : StatusBarCodeCount))
					Column.HSplitTop(MarginSmall, nullptr, &Column);
			}
		}
		else
		{
			for(int i = 0; i < StatusBarCodeCount; ++i)
			{
				View.HSplitTop(LineSize, &Label, &View);
				Ui()->DoLabel(&Label, apCodes[i], FontSize, TEXTALIGN_ML);
				if(i + 1 < StatusBarCodeCount)
					View.HSplitTop(MarginSmall, nullptr, &View);
			}
		}
	};
	const auto MeasureItems = [StatusBarCodeCount](const float ContentWidth) {
		const int Rows = ResolveSettingsStatusCodeRows(StatusBarCodeCount, ContentWidth);
		return Rows * LineSize + maximum(0, Rows - 1) * MarginSmall;
	};

	const auto MeasureSettings = [SettingsContentHeight](float) { return SettingsContentHeight; };
	const auto RenderSettings = [this, ReadOnly](CUIRect &View) {
		CPerfTimer SectionsTimer;
		CUIRect CheckBoxRect, Button, Label;
		CTClientSettingsRowAllocator Rows(View);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_TcStatusBar, "tclient-statusbar-show", Localize("Show status bar"), g_Config.m_TcStatusBar, &CheckBoxRect))
			g_Config.m_TcStatusBar ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-show", &CheckBoxRect, Localize("Show status bar"), FontSize, TEXTALIGN_ML);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_TcStatusBarLabels, "tclient-statusbar-show-labels", Localize("Show labels on status bar items"), g_Config.m_TcStatusBarLabels, &CheckBoxRect))
			g_Config.m_TcStatusBarLabels ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-show-labels", &CheckBoxRect, Localize("Show labels on status bar items"), FontSize, TEXTALIGN_ML);
		Button = Rows.Next();
		if(!ReadOnly)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-height", &g_Config.m_TcStatusBarHeight, &g_Config.m_TcStatusBarHeight, &Button, Localize("Status bar height"), 1, 16);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-height", &Button, Localize("Status bar height"), FontSize, TEXTALIGN_ML);
		Label = Rows.Next(HeadlineHeight);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-local-time-title", &Label, Localize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_TcStatusBar12HourClock, "tclient-statusbar-12-hour-clock", Localize("Use 12 hour clock"), g_Config.m_TcStatusBar12HourClock, &CheckBoxRect))
			g_Config.m_TcStatusBar12HourClock ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-12-hour-clock", &CheckBoxRect, Localize("Use 12 hour clock"), FontSize, TEXTALIGN_ML);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_TcStatusBarLocalTimeSeconds, "tclient-statusbar-seconds", Localize("Show seconds on clock"), g_Config.m_TcStatusBarLocalTimeSeconds, &CheckBoxRect))
			g_Config.m_TcStatusBarLocalTimeSeconds ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-seconds", &CheckBoxRect, Localize("Show seconds on clock"), FontSize, TEXTALIGN_ML);
		Label = Rows.Next(HeadlineHeight);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-colors-title", &Label, Localize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
		if(!ReadOnly)
		{
			static CButtonContainer s_StatusbarColor, s_StatusbarTextColor;
			CUIRect ColorRow = Rows.Next(ColorPickerLineSize);
			DoLine_ColorPicker(&s_StatusbarColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Status bar color"), &g_Config.m_TcStatusBarColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
			ColorRow = Rows.Next(ColorPickerLineSize);
			DoLine_ColorPicker(&s_StatusbarTextColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Text color"), &g_Config.m_TcStatusBarTextColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		}
		else
		{
			Label = Rows.Next(ColorPickerLineSize);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-color", &Label, Localize("Status bar color"), FontSize, TEXTALIGN_ML);
			Label = Rows.Next(ColorPickerLineSize);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-text-color", &Label, Localize("Text color"), FontSize, TEXTALIGN_ML);
		}
		Button = Rows.Next();
		if(!ReadOnly)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-alpha", &g_Config.m_TcStatusBarAlpha, &g_Config.m_TcStatusBarAlpha, &Button, Localize("Status bar alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-alpha", &Button, Localize("Status bar alpha"), FontSize, TEXTALIGN_ML);
		Button = Rows.Next();
		if(!ReadOnly)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-text-alpha", &g_Config.m_TcStatusBarTextAlpha, &g_Config.m_TcStatusBarTextAlpha, &Button, Localize("Text alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-text-alpha", &Button, Localize("Text alpha"), FontSize, TEXTALIGN_ML);
		LogTClientPerfStageEx("tclient_statusbar", "sections", ETClientSettingsPerfStage::INTERACTIVE_LAYER, SectionsTimer.ElapsedMs());
	};
	const auto MeasurePreview = [&MeasureItems, StatusBarCodeCount, StatusBarItemCount, StatusBarPreviewHeight](const float ContentWidth) {
		float Height = 0.0f;
		if(s_SelectedItem >= 0 && s_SelectedItem < StatusBarItemCount && s_TypeSelectedOld >= 0 && s_TypeSelectedOld < StatusBarCodeCount)
			Height += LineSize + MarginSmall;
		Height += StatusBarPreviewHeight + MarginSmall;
		Height += LineSize + MarginSmall;
		Height += LineSize + MarginSmall;
		Height += HeadlineHeight + MarginSmall;
		return Height + MeasureItems(ContentWidth);
	};
	const auto RenderPreview = [this, &TClientStatusSchemeTextInputCtx, &GetStatusBarEditorLabel, &RenderStatusBarCodes, &RenderStatusBarPreview, Page, ReadOnly, StatusBarPreviewHeight](CUIRect &Content) {
		CPerfTimer EditorTimer;
		const int StatusItemTypeCount = (int)GameClient()->m_StatusBar.m_vStatusItemTypes.size();
		if(s_TypeSelectedOld >= StatusItemTypeCount)
			s_TypeSelectedOld = -1;
		if(s_SelectedItem >= (int)GameClient()->m_StatusBar.m_StatusBarItems.size())
			s_SelectedItem = -1;
		CUIRect StatusScheme, StatusButtons, ItemLabel, CodesTitle, StatusBar;
		if(s_SelectedItem >= 0 && s_TypeSelectedOld >= 0)
		{
			Content.HSplitTop(LineSize, &ItemLabel, &Content);
			Ui()->DoLabel(&ItemLabel, Localize(GameClient()->m_StatusBar.m_vStatusItemTypes[s_TypeSelectedOld].m_aDesc), FontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
		}
		Content.HSplitTop(StatusBarPreviewHeight, &StatusBar, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(LineSize, &StatusButtons, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(LineSize, &StatusScheme, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(HeadlineHeight, &CodesTitle, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);

		CUIRect DropDownRect, AddButton, RemoveButton;
		StatusButtons.VSplitMid(&DropDownRect, &StatusButtons, MarginSmall);
		StatusButtons.VSplitMid(&AddButton, &RemoveButton, MarginSmall);
		static CButtonContainer s_ApplyButton, s_AddButton, s_RemoveButton;
		CUIRect SchemeLabel, SchemeInput, ApplyButton;
		const float SchemeLabelWidth = std::min(StatusScheme.w * 0.28f, LineSize * 5.0f);
		StatusScheme.VSplitLeft(SchemeLabelWidth, &SchemeLabel, &StatusScheme);
		StatusScheme.VSplitLeft(MarginSmall, nullptr, &StatusScheme);
		const float ApplyWidth = std::min(StatusScheme.w * 0.30f, std::max(LineSize * 2.5f, StatusScheme.w * 0.18f));
		StatusScheme.VSplitRight(ApplyWidth, &SchemeInput, &ApplyButton);
		SchemeInput.VSplitRight(MarginSmall, &SchemeInput, nullptr);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &s_ApplyButton, "tclient-statusbar-apply-scheme", Localize("Apply"), 0, &ApplyButton))
		{
			GameClient()->m_StatusBar.ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
			s_SelectedItem = -1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-apply-scheme", &ApplyButton, Localize("Apply"), FontSize, TEXTALIGN_MC);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-scheme-label", &SchemeLabel, Localize("Status Scheme:"), FontSize, TEXTALIGN_MR);
		s_StatusScheme.SetEmptyText("");
		if(!ReadOnly)
			ui_widget::InputField(TClientStatusSchemeTextInputCtx, &s_StatusScheme, SchemeInput, nullptr, EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-scheme-value", &SchemeInput, g_Config.m_TcStatusBarScheme, FontSize, TEXTALIGN_ML);

		static std::vector<std::string> s_DropDownNameStorage;
		static std::vector<const char *> s_DropDownNames;
		static char s_aDropDownLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
		if(s_DropDownNameStorage.size() != GameClient()->m_StatusBar.m_vStatusItemTypes.size() || str_comp(s_aDropDownLanguage, g_Config.m_ClLanguagefile) != 0)
		{
			s_DropDownNameStorage.clear();
			s_DropDownNames.clear();
			s_DropDownNameStorage.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			s_DropDownNames.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			for(const CStatusItem &StatusItemType : GameClient()->m_StatusBar.m_vStatusItemTypes)
			{
				s_DropDownNameStorage.emplace_back(Localize(GetStatusBarEditorLabel(&StatusItemType)));
			}
			for(const std::string &Name : s_DropDownNameStorage)
				s_DropDownNames.push_back(Name.c_str());
			str_copy(s_aDropDownLanguage, g_Config.m_ClLanguagefile);
		}
		if(!ReadOnly)
		{
			static CUi::SDropDownState s_DropDownState;
			static CScrollRegion s_DropDownScrollRegion;
			s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
			CUi::SDropDownProperties DropDownProps;
			DropDownProps.m_pPopupViewport = &Page.m_ScrollViewport;
			const int TypeSelectedNew = DoSettingsDropDown(&DropDownRect, s_TypeSelectedOld, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState, DropDownProps);
			if(s_TypeSelectedOld != TypeSelectedNew)
			{
				s_TypeSelectedOld = TypeSelectedNew;
				if(s_SelectedItem >= 0 && s_TypeSelectedOld >= 0 && s_TypeSelectedOld < StatusItemTypeCount)
				{
					GameClient()->m_StatusBar.m_StatusBarItems[s_SelectedItem] = &GameClient()->m_StatusBar.m_vStatusItemTypes[s_TypeSelectedOld];
					GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
				}
			}
		}
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-item-type", &DropDownRect, Localize("Item type"), FontSize, TEXTALIGN_MC);
		const size_t NumItems = GameClient()->m_StatusBar.m_StatusBarItems.size();
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &s_AddButton, "tclient-statusbar-add-item", Localize("Add Item"), 0, &AddButton) && s_TypeSelectedOld >= 0 && s_TypeSelectedOld < StatusItemTypeCount && NumItems < 128)
		{
			GameClient()->m_StatusBar.m_StatusBarItems.push_back(&GameClient()->m_StatusBar.m_vStatusItemTypes[s_TypeSelectedOld]);
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
			s_SelectedItem = (int)GameClient()->m_StatusBar.m_StatusBarItems.size() - 1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-add-item", &AddButton, Localize("Add Item"), FontSize, TEXTALIGN_MC);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &s_RemoveButton, "tclient-statusbar-remove-item", Localize("Remove Item"), 0, &RemoveButton) && s_SelectedItem >= 0)
		{
			if(s_SelectedItem < (int)GameClient()->m_StatusBar.m_StatusBarItems.size())
			{
				GameClient()->m_StatusBar.m_StatusBarItems.erase(GameClient()->m_StatusBar.m_StatusBarItems.begin() + s_SelectedItem);
				GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
			}
			s_SelectedItem = -1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-remove-item", &RemoveButton, Localize("Remove Item"), FontSize, TEXTALIGN_MC);

		const int ItemCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
		if(ItemCount <= 0)
		{
			RenderStatusBarPreview(StatusBar);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-codes-title", &CodesTitle, Localize("Status Bar Codes"), HeadlineFontSize, TEXTALIGN_ML);
			RenderStatusBarCodes(Content);
			LogTClientPerfStageEx("tclient_statusbar", "editor", ETClientSettingsPerfStage::STATIC_LAYER, EditorTimer.ElapsedMs());
			return;
		}
		DrawRoundedSurface(Ui(), StatusBar, ColorRGBA(0, 0, 0, 0.5f), ColorRGBA(), 5.0f);
		const float ItemWidth = (StatusBar.w - MarginSmall) / (float)ItemCount;
		StatusBar.VSplitLeft(MarginExtraSmall, nullptr, &StatusBar);
		static std::vector<CButtonContainer *> s_pItemButtons;
		static std::vector<CButtonContainer> s_ItemButtons;
		static vec2 s_ActivePos = vec2(0.0f, 0.0f);
		class CSwapItem
		{
		public:
			vec2 m_InitialPosition = vec2(0.0f, 0.0f);
			float m_Duration = 0.0f;
		};
		static std::vector<CSwapItem> s_ItemSwaps;
		if((int)s_ItemButtons.size() != ItemCount)
		{
			s_ItemSwaps.resize(ItemCount);
			s_pItemButtons.resize(ItemCount);
			s_ItemButtons.resize(ItemCount);
			for(int i = 0; i < ItemCount; ++i)
				s_pItemButtons[i] = &s_ItemButtons[i];
		}
		bool StatusItemActive = false;
		int HotStatusIndex = 0;
		if(!ReadOnly)
		{
			for(int i = 0; i < ItemCount; ++i)
			{
				if(Ui()->ActiveItem() == s_pItemButtons[i])
				{
					StatusItemActive = true;
					HotStatusIndex = i;
				}
			}
		}
		CUIRect StatusItemButton;
		for(int i = 0; i < ItemCount; ++i)
		{
			StatusBar.VSplitLeft(ItemWidth, &StatusItemButton, &StatusBar);
			StatusItemButton.HMargin(MarginSmall, &StatusItemButton);
			StatusItemButton.VMargin(MarginExtraSmall, &StatusItemButton);
			CStatusItem *pStatusItem = GameClient()->m_StatusBar.m_StatusBarItems[i];
			const ColorRGBA Color = s_SelectedItem == i ? ColorRGBA(1.0f, 0.35f, 0.35f, 0.75f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
			CUIRect TempItemButton = StatusItemButton;
			if(!ReadOnly && StatusItemActive && Ui()->ActiveItem() != s_pItemButtons[i])
			{
				CUIRect FullHeightItemButton = StatusItemButton;
				FullHeightItemButton.y = 0.0f;
				FullHeightItemButton.h = 10000.0f;
				if(Ui()->MouseInside(&FullHeightItemButton))
				{
					std::swap(s_pItemButtons[i], s_pItemButtons[HotStatusIndex]);
					std::swap(GameClient()->m_StatusBar.m_StatusBarItems[i], GameClient()->m_StatusBar.m_StatusBarItems[HotStatusIndex]);
					s_SelectedItem = -2;
					s_ItemSwaps[HotStatusIndex].m_InitialPosition = vec2(StatusItemButton.x, StatusItemButton.y);
					s_ItemSwaps[HotStatusIndex].m_Duration = 0.15f;
					s_ItemSwaps[i].m_InitialPosition = vec2(s_ActivePos.x, s_ActivePos.y);
					s_ItemSwaps[i].m_Duration = 0.15f;
					GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
				}
			}
			if(!ReadOnly)
			{
				s_ItemSwaps[i].m_Duration = std::max(0.0f, s_ItemSwaps[i].m_Duration - Client()->RenderFrameTime());
				if(s_ItemSwaps[i].m_Duration > 0.0f)
				{
					const float Progress = std::pow(2.0, -5.0 * (1.0 - s_ItemSwaps[i].m_Duration / 0.15f));
					TempItemButton.x = mix(TempItemButton.x, s_ItemSwaps[i].m_InitialPosition.x, Progress);
				}
			}
			if(!ReadOnly && DoButtonLineSize_Menu(s_pItemButtons[i], Localize(GetStatusBarEditorLabel(pStatusItem)), 0, &TempItemButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, Color))
			{
				if(s_SelectedItem == -2)
				{
					s_SelectedItem++;
				}
				else if(s_SelectedItem != i)
				{
					s_SelectedItem = i;
					for(int TypeIndex = 0; TypeIndex < StatusItemTypeCount; ++TypeIndex)
						if(str_comp(GameClient()->m_StatusBar.m_vStatusItemTypes[TypeIndex].m_aName, pStatusItem->m_aName) == 0)
							s_TypeSelectedOld = TypeIndex;
				}
				else
				{
					s_SelectedItem = -1;
					s_TypeSelectedOld = -1;
				}
			}
			else if(ReadOnly)
				DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-preview-item", &StatusItemButton, Localize(GetStatusBarEditorLabel(pStatusItem)), FontSize, TEXTALIGN_MC);
			if(!ReadOnly && Ui()->ActiveItem() == s_pItemButtons[i])
				s_ActivePos = vec2(StatusItemButton.x, StatusItemButton.y);
		}
		if(!ReadOnly && !StatusItemActive)
			s_SelectedItem = std::max(-1, s_SelectedItem);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-codes-title", &CodesTitle, Localize("Status Bar Codes"), HeadlineFontSize, TEXTALIGN_ML);
		RenderStatusBarCodes(Content);
		LogTClientPerfStageEx("tclient_statusbar", "editor", ETClientSettingsPerfStage::STATIC_LAYER, EditorTimer.ElapsedMs());
	};
	const uint64_t StatusLayoutRevision = ((uint64_t)GameClient()->m_StatusBar.m_vStatusItemTypes.size() << 32) ^
					      (uint64_t)GameClient()->m_StatusBar.m_StatusBarItems.size() ^
					      ((uint64_t)(s_SelectedItem + 2) << 16) ^ (uint64_t)(s_TypeSelectedOld + 2);
	static std::array<CTClientSettingsCardFrameBinding, 2> s_aCardBindings;
	s_aCardBindings[0].Bind(MeasureSettings, RenderSettings);
	s_aCardBindings[1].Bind(MeasurePreview, RenderPreview);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		constexpr std::array<std::pair<const char *, const char *>, 2> aSpecs = {{
			{"deck:tclient-status-bar-settings", "Status Bar"},
			{"deck:tclient-status-bar-preview", "Preview"},
		}};
		vCards.reserve(aSpecs.size());
		for(size_t Index = 0; Index < aSpecs.size(); ++Index)
		{
			CTClientSettingsCardFrameBinding *pBinding = &s_aCardBindings[Index];
			SSettingsCardDefinition Definition;
			Definition.m_Spec = {aSpecs[Index].first, Localize(aSpecs[Index].second), qm_card_registry::ResolveLocalizedDescription(aSpecs[Index].first)};
			Definition.m_Measure = [pBinding](float ContentWidth) { return pBinding->Measure(ContentWidth); };
			Definition.m_RenderMeasured = [pBinding](CUIRect &Content) { pBinding->Render(Content); };
			if(Index == 1)
				Definition.m_MeasureRevision = StatusLayoutRevision;
			vCards.push_back(std::move(Definition));
		}
	};
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-status-bar", StatusLayoutRevision));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_StatusBarPrewarmOrderModel;
	static bool s_StatusBarPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_StatusBarPrewarmDeck;
	if(ReadOnly && !s_StatusBarPrewarmOrderModelInitialized)
	{
		s_StatusBarPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_StatusBarPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_StatusBarPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_StatusBarPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-status-bar-") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(TClientStatusSchemeTextInputCtx, Page, "tclient-status-bar", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_StatusBarSettingsScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	for(CTClientSettingsCardFrameBinding &Binding : s_aCardBindings)
		Binding.Clear();
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());
	IUiContext InfoCtx = SettingsUiContext("settings_tclient_info", UiScale);
	if(ReadOnly)
	{
		InfoCtx.m_pAnim = nullptr;
		InfoCtx.m_pTree = nullptr;
	}

	auto RenderLinks = [this, ReadOnly](CUIRect Content) {
		CPerfTimer LinksTimer;
		static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton, s_SupportButton;
		CUIRect Row, LeftButton, RightButton;
		Content.HSplitTop(LineSize * 2.0f, &Row, &Content);
		Row.VSplitMid(&LeftButton, &RightButton, MarginSmall);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_DiscordButton, Localize("Discord"), 0, &LeftButton, LineSize))
			Client()->ViewLink("https://discord.gg/fBvhH93Bt6");
		if(!ReadOnly && DoButtonLineSize_Menu(&s_WebsiteButton, Localize("Website"), 0, &RightButton, LineSize))
			Client()->ViewLink("https://tclient.app/");
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(LineSize * 2.0f, &Row, &Content);
		Row.VSplitMid(&LeftButton, &RightButton, MarginSmall);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_GithubButton, Localize("Github"), 0, &LeftButton, LineSize))
			Client()->ViewLink("https://github.com/sjrc6/TaterClient-ddnet");
		if(!ReadOnly && DoButtonLineSize_Menu(&s_SupportButton, Localize("Support ♥"), 0, &RightButton, LineSize))
			Client()->ViewLink("https://ko-fi.com/Totar");
		LogTClientPerfStageEx("tclient_info", "links", ETClientSettingsPerfStage::INTERACTIVE_LAYER, LinksTimer.ElapsedMs());
	};

	auto RenderFiles = [this, ReadOnly](CUIRect Content) {
		CPerfTimer FilesTimer;
		static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
		auto OpenFile = [this, ReadOnly](ConfigDomain Domain) {
			if(ReadOnly)
				return;
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[Domain].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		};
		CUIRect Row, LeftButton, RightButton;
		Content.HSplitTop(LineSize * 2.0f, &Row, &Content);
		Row.VSplitMid(&LeftButton, &RightButton, MarginSmall);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_Config, Localize("QmClient Settings"), 0, &LeftButton, LineSize))
			OpenFile(ConfigDomain::QMCLIENT);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_Profiles, Localize("Profiles"), 0, &RightButton, LineSize))
			OpenFile(ConfigDomain::TCLIENTPROFILES);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(LineSize * 2.0f, &Row, &Content);
		Row.VSplitMid(&LeftButton, &RightButton, MarginSmall);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_Warlist, Localize("War List"), 0, &LeftButton, LineSize))
			OpenFile(ConfigDomain::TCLIENTWARLIST);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_Chatbinds, Localize("Chat Binds"), 0, &RightButton, LineSize))
			OpenFile(ConfigDomain::TCLIENTCHATBINDS);
		LogTClientPerfStageEx("tclient_info", "files", ETClientSettingsPerfStage::RESOURCE_PRETRIGGER, FilesTimer.ElapsedMs());
	};

	auto RenderDevelopers = [this, ReadOnly](CUIRect Content) {
		struct SDeveloper
		{
			const char *m_pName;
			const char *m_pUrl;
			const char *m_pSkin;
			const char *m_pUseCustomColors;
			bool m_CustomColors;
			ColorRGBA m_BodyColor;
			ColorRGBA m_FeetColor;
		};
		static const SDeveloper s_aDevelopers[] = {
			{"Tater", "https://github.com/sjrc6", "glow_mermyfox", "mermyfox", true, ColorRGBA(0.92f, 0.29f, 0.48f, 1.0f), ColorRGBA(0.55f, 0.64f, 0.76f, 1.0f)},
			{"SollyBunny / bun bun", "https://github.com/SollyBunny", "tuzi", "tuzi", false, ColorRGBA(), ColorRGBA()},
			{"PeBox", "https://github.com/danielkempf", "greyfox", "greyfox", true, ColorRGBA(0.0f, 0.09f, 1.0f, 1.0f), ColorRGBA(1.0f, 0.92f, 0.0f, 1.0f)},
			{"Teero", "https://github.com/Teero888", "glow_mermyfox", "mermyfox", true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 0.02f, 0.13f, 1.0f)},
			{"ChillerDragon", "https://github.com/ChillerDragon", "glow_greensward", "greensward", true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 0.02f, 0.13f, 1.0f)},
		};
		static std::array<CButtonContainer, std::size(s_aDevelopers)> s_aLinkButtons;
		constexpr float TeeSize = 50.0f;
		for(size_t Index = 0; Index < std::size(s_aDevelopers); ++Index)
		{
			CUIRect Row, TeeRect, Label, Button;
			Content.HSplitTop(TeeSize + MarginSmall, &Row, &Content);
			Row.VSplitLeft(TeeSize + MarginSmall, &TeeRect, &Label);
			TeeRect.w = TeeSize;
			const float DeveloperFontSize = CurrentSettingsContentMetrics().m_BodySize;
			Label.VSplitLeft(TextRender()->TextWidth(DeveloperFontSize, s_aDevelopers[Index].m_pName), &Label, &Button);
			Button.VSplitLeft(MarginSmall, nullptr, &Button);
			Button.w = LineSize;
			Button.h = LineSize;
			Button.y = Label.y + (Label.h - Button.h) * 0.5f;
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_INFO, s_aDevelopers[Index].m_pName, &Label, s_aDevelopers[Index].m_pName, DeveloperFontSize, TEXTALIGN_ML);
			if(!ReadOnly && Ui()->DoButton_FontIcon(&s_aLinkButtons[Index], FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
				Client()->ViewLink(s_aDevelopers[Index].m_pUrl);
			RenderDevSkin(TeeRect.Center(), TeeSize, s_aDevelopers[Index].m_pSkin, s_aDevelopers[Index].m_pUseCustomColors, s_aDevelopers[Index].m_CustomColors, 0, 0, 0, false, true, s_aDevelopers[Index].m_BodyColor, s_aDevelopers[Index].m_FeetColor);
		}
	};

	auto RenderTabs = [this, ReadOnly](CUIRect Content) {
		CPerfTimer TabsTimer;
		const char *apTabNames[] = {
			Localize("Settings"), Localize("Bind Wheel"), Localize("War List"), Localize("Chat Binds"), Localize("Status Bar"), Localize("Info")};
		static int s_aShowTabs[NUMBER_OF_TCLIENT_TABS] = {};
		CUIRect LeftColumn, RightColumn;
		Content.VSplitMid(&LeftColumn, &RightColumn, MarginSmall);
		for(int i = 0; i < NUMBER_OF_TCLIENT_TABS - 1; ++i)
		{
			s_aShowTabs[i] = IsFlagSet(g_Config.m_TcTClientSettingsTabs, i);
			CUIRect &Column = i % 2 == 0 ? LeftColumn : RightColumn;
			CUIRect CheckBoxRect;
			Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
			char aTextId[64];
			str_format(aTextId, sizeof(aTextId), "tclient-info-hide-tab-%d", i);
			if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_INFO, TCLIENT_TAB_INFO, &s_aShowTabs[i], aTextId, apTabNames[i], s_aShowTabs[i], &CheckBoxRect))
				s_aShowTabs[i] ^= 1;
			if(!ReadOnly)
				SetFlag(g_Config.m_TcTClientSettingsTabs, i, s_aShowTabs[i]);
		}
		LogTClientPerfStageEx("tclient_info", "settings_tabs", ETClientSettingsPerfStage::INTERACTIVE_LAYER, TabsTimer.ElapsedMs());
	};

	const std::array<float, 4> aCardHeights = {
		LineSize * 4.0f + MarginSmall,
		LineSize * 4.0f + MarginSmall,
		(50.0f + MarginSmall) * 5.0f,
		LineSize * 3.0f,
	};
	const auto MeasureCard = [&](size_t Index, float) { return aCardHeights[Index]; };
	const auto RenderCard = [&](size_t Index, CUIRect &Content) {
		switch(Index)
		{
		case 0: RenderLinks(Content); break;
		case 1: RenderFiles(Content); break;
		case 2: RenderDevelopers(Content); break;
		case 3: RenderTabs(Content); break;
		default: break;
		}
	};
	static std::array<CTClientSettingsCardFrameBinding, 4> s_aCardBindings;
	for(size_t Index = 0; Index < s_aCardBindings.size(); ++Index)
		s_aCardBindings[Index].BindIndexed(MeasureCard, RenderCard, Index);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		constexpr std::array<std::pair<const char *, const char *>, 4> aSpecs = {{
			{"deck:tclient-info-links", "TClient Links"},
			{"deck:tclient-info-files", "Config Files"},
			{"deck:tclient-info-developers", "TClient Developers"},
			{"deck:tclient-info-tabs", "Hide Settings Tabs"},
		}};
		vCards.reserve(aSpecs.size());
		for(size_t Index = 0; Index < aSpecs.size(); ++Index)
		{
			CTClientSettingsCardFrameBinding *pBinding = &s_aCardBindings[Index];
			SSettingsCardDefinition Definition;
			Definition.m_Spec = {aSpecs[Index].first, Localize(aSpecs[Index].second), qm_card_registry::ResolveLocalizedDescription(aSpecs[Index].first)};
			Definition.m_Measure = [pBinding](float ContentWidth) { return pBinding->Measure(ContentWidth); };
			Definition.m_Render = [pBinding](CUIRect Content) { pBinding->Render(Content); };
			vCards.push_back(std::move(Definition));
		}
	};
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-info"));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static CScrollRegion s_InfoScrollRegion;
	static qm_card_order::CModel s_InfoPrewarmOrderModel;
	static bool s_InfoPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_InfoPrewarmDeck;
	if(ReadOnly && !s_InfoPrewarmOrderModelInitialized)
	{
		s_InfoPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_InfoPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_InfoPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_InfoPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-info-") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(InfoCtx, Page, "tclient-info", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_InfoScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	for(CTClientSettingsCardFrameBinding &Binding : s_aCardBindings)
		Binding.Clear();
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogTClientPerfStage("tclient_info_total", RenderTimer.ElapsedMs(), false);
}

void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsContentMetrics ProfileMetrics = ResolveSettingsContentMetrics(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());
	IUiContext ProfilesCtx = SettingsUiContext("settings_tclient_profiles", UiScale);
	if(ReadOnly)
	{
		ProfilesCtx.m_pAnim = nullptr;
		ProfilesCtx.m_pTree = nullptr;
	}
	int *pCurrentUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;

	const char *pCurrentSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const int CurrentColorBody = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : -1;
	const int CurrentColorFeet = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : -1;
	const int CurrentFlag = m_Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	const int Emote = m_Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;
	const char *pCurrentName = m_Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const char *pCurrentClan = m_Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;

	const CProfile CurrentProfile(
		CurrentColorBody,
		CurrentColorFeet,
		CurrentFlag,
		Emote,
		pCurrentSkinName,
		pCurrentName,
		pCurrentClan);

	static int s_SelectedProfile = -1;
	static int s_AllowDelete = 0;
	auto &vProfiles = GameClient()->m_SkinProfiles.m_Profiles;
	int SelectedProfile = s_SelectedProfile;
	if(SelectedProfile >= (int)vProfiles.size())
		SelectedProfile = vProfiles.empty() ? -1 : (int)vProfiles.size() - 1;

	CUIRect Label, Button;

	auto RenderProfile = [&](CUIRect Rect, const CProfile &Profile, bool Main) {
		const float PreviewTeeSize = ProfileMetrics.m_ButtonHeight * 2.0f;
		const float PreviewFlagHeight = ProfileMetrics.m_ButtonHeight;
		const float PreviewColorSize = ProfileMetrics.m_LineSpacing * 2.0f;
		auto RenderCross = [&](CUIRect Cross, float MaxSize = 0.0f) {
			// 未覆盖字段使用中性短横线，避免与删除操作的 destructive 图标混淆。
			const float Extent = std::min(MaxSize > 0.0f ? MaxSize : Cross.h * 0.4f, Cross.w * 0.5f);
			CUIRect Placeholder{Cross.Center().x - Extent * 0.5f, Cross.Center().y - 1.0f, Extent, 2.0f};
			Placeholder.Draw(ColorRGBA(0.65f, 0.65f, 0.65f, 0.8f), IGraphics::CORNER_ALL, 1.0f);
		};
		{
			CUIRect Skin;
			Rect.VSplitLeft(PreviewTeeSize, &Skin, &Rect);
			if(!Main && Profile.m_SkinName[0] == '\0')
			{
				RenderCross(Skin, 20.0f);
			}
			else
			{
				CTeeRenderInfo TeeRenderInfo;
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(Profile.m_SkinName));
				TeeRenderInfo.ApplyColors(Profile.m_BodyColor >= 0 && Profile.m_FeetColor >= 0, Profile.m_BodyColor, Profile.m_FeetColor);
				TeeRenderInfo.m_Size = PreviewTeeSize;
				const vec2 Pos = Skin.Center() + vec2(0.0f, TeeRenderInfo.m_Size / 10.0f); // Prevent overflow from hats
				vec2 Dir = vec2(1.0f, 0.0f);
				if(Main)
					RenderTeeCute(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos, false);
				else
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos);
			}
		}
		Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);
		{
			CUIRect Colors;
			Rect.VSplitLeft(PreviewColorSize, &Colors, &Rect);
			CUIRect BodyColor{Colors.Center().x - PreviewColorSize * 0.5f, Colors.Center().y - PreviewColorSize * 0.5f - ProfileMetrics.m_LineSpacing, PreviewColorSize, PreviewColorSize};
			CUIRect FeetColor{Colors.Center().x - PreviewColorSize * 0.5f, Colors.Center().y + ProfileMetrics.m_LineSpacing, PreviewColorSize, PreviewColorSize};
			if(Profile.m_BodyColor >= 0 && Profile.m_FeetColor >= 0)
			{
				// Body Color
				DrawRoundedSurface(Ui(), BodyColor,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f), ColorRGBA(), 2.0f);
				// Feet Color;
				DrawRoundedSurface(Ui(), FeetColor,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f), ColorRGBA(), 2.0f);
			}
			else
			{
				RenderCross(BodyColor);
				RenderCross(FeetColor);
			}
		}
		Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);
		{
			CUIRect Flag;
			Rect.VSplitRight(PreviewFlagHeight * 2.0f, &Rect, &Flag);
			Flag = {Flag.x, Flag.y + (Flag.h - PreviewFlagHeight) / 2.0f, Flag.w, PreviewFlagHeight};
			if(Profile.m_CountryFlag == -2)
				RenderCross(Flag, 20.0f);
			else
				GameClient()->m_CountryFlags.Render(Profile.m_CountryFlag, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
		}
		Rect.VSplitRight(ProfileMetrics.m_LineSpacing, &Rect, nullptr);
		{
			const float Height = Rect.h / 3.0f;
			if(Main)
			{
				char aBuf[256];
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Skin: %s"), Profile.m_SkinName);
				Ui()->DoLabel(&Label, aBuf, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
			}
			else
			{
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Name, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Clan, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
			}
		}
	};

	auto IsSelectedProfileValid = [&]() {
		return SelectedProfile >= 0 && SelectedProfile < (int)vProfiles.size();
	};

	auto pSelectedProfile = [&]() -> CProfile * {
		if(!IsSelectedProfileValid())
			return nullptr;
		return &vProfiles[SelectedProfile];
	};

	auto pConstSelectedProfile = [&]() -> const CProfile * {
		if(!IsSelectedProfileValid())
			return nullptr;
		return &vProfiles[SelectedProfile];
	};

	auto BuildProfileFromCurrentSettings = [&]() {
		return CProfile(
			g_Config.m_TcProfileColors ? CurrentColorBody : -1,
			g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
			g_Config.m_TcProfileFlag ? CurrentFlag : -2,
			g_Config.m_TcProfileEmote ? Emote : -1,
			g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
			g_Config.m_TcProfileName ? pCurrentName : "",
			g_Config.m_TcProfileClan ? pCurrentClan : "");
	};

	auto BuildPreviewProfile = [&]() {
		CProfile PreviewProfile = CurrentProfile;
		const CProfile *pProfile = pConstSelectedProfile();
		if(!pProfile)
			return PreviewProfile;

		if(g_Config.m_TcProfileSkin && pProfile->m_SkinName[0] != '\0')
			str_copy(PreviewProfile.m_SkinName, pProfile->m_SkinName);
		if(g_Config.m_TcProfileColors && pProfile->m_BodyColor != -1 && pProfile->m_FeetColor != -1)
		{
			PreviewProfile.m_BodyColor = pProfile->m_BodyColor;
			PreviewProfile.m_FeetColor = pProfile->m_FeetColor;
		}
		if(g_Config.m_TcProfileEmote && pProfile->m_Emote != -1)
			PreviewProfile.m_Emote = pProfile->m_Emote;
		if(g_Config.m_TcProfileName && pProfile->m_Name[0] != '\0')
			str_copy(PreviewProfile.m_Name, pProfile->m_Name);
		if(g_Config.m_TcProfileClan && (pProfile->m_Clan[0] != '\0' || g_Config.m_TcProfileOverwriteClanWithEmpty))
			str_copy(PreviewProfile.m_Clan, pProfile->m_Clan);
		if(g_Config.m_TcProfileFlag && pProfile->m_CountryFlag != -2)
			PreviewProfile.m_CountryFlag = pProfile->m_CountryFlag;

		return PreviewProfile;
	};

	auto ApplySelectedProfile = [&]() {
		const CProfile *pProfile = pConstSelectedProfile();
		if(!pProfile)
			return;
		GameClient()->m_SkinProfiles.ApplyProfile(m_Dummy, *pProfile);
	};

	auto DeleteSelectedProfile = [&]() {
		if(!IsSelectedProfileValid())
			return;
		vProfiles.erase(vProfiles.begin() + SelectedProfile);
		if(vProfiles.empty())
			SelectedProfile = -1;
		else if(SelectedProfile >= (int)vProfiles.size())
			SelectedProfile = (int)vProfiles.size() - 1;
	};

	auto RenderProfilePreview = [&](CUIRect Profiles) {
		CUIRect Skin;
		Profiles.HSplitTop(LineSize, &Label, &Profiles);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Your profile"), FontSize, TEXTALIGN_ML);
		Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
		Profiles.HSplitTop(LineSize * 3.0f, &Skin, &Profiles);
		DrawRoundedSurface(Ui(), Skin, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f), ColorRGBA(), 4.0f);
		Skin.Margin(ProfileMetrics.m_LineSpacing, &Skin);
		const float PreviewRowWidth = std::min(Skin.w, ProfileMetrics.m_LabelWidth * 2.5f);
		Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);
		RenderProfile(Skin, CurrentProfile, true);
		if(pConstSelectedProfile())
		{
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(LineSize, &Label, &Profiles);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("After Load"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(LineSize * 3.0f, &Skin, &Profiles);
			DrawRoundedSurface(Ui(), Skin, ColorRGBA(0.25f, 0.55f, 0.85f, 0.08f), ColorRGBA(), 4.0f);
			Skin.Margin(ProfileMetrics.m_LineSpacing, &Skin);
			const float PreviewRowWidth = std::min(Skin.w, ProfileMetrics.m_LabelWidth * 2.5f);
			Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);
			RenderProfile(Skin, BuildPreviewProfile(), true);
		}
	};

	auto RenderActionButtons = [&](CUIRect Actions) {
		Actions.HSplitTop(ProfileMetrics.m_ButtonHeight, &Button, &Actions);
		static CButtonContainer s_LoadButton;
		if(!ReadOnly && DoTClientSettingsButton_Menu(&s_LoadButton, "tclient-profile-load", Localize("Load"), 0, &Button))
			ApplySelectedProfile();
		Actions.HSplitTop(MarginSmall, nullptr, &Actions);
		Actions.HSplitTop(ProfileMetrics.m_ButtonHeight, &Button, &Actions);
		static CButtonContainer s_SaveButton;
		if(!ReadOnly && DoTClientSettingsButton_Menu(&s_SaveButton, "tclient-profile-save", Localize("Save"), 0, &Button))
		{
			const CProfile ProfileToSave = BuildProfileFromCurrentSettings();
			GameClient()->m_SkinProfiles.AddProfile(ProfileToSave.m_BodyColor, ProfileToSave.m_FeetColor, ProfileToSave.m_CountryFlag, ProfileToSave.m_Emote, ProfileToSave.m_SkinName, ProfileToSave.m_Name, ProfileToSave.m_Clan);
		}
		Actions.HSplitTop(MarginSmall, nullptr, &Actions);
		if(!ReadOnly)
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&s_AllowDelete, "tclient-profile-enable-deleting", Localizable("Enable Deleting"), &s_AllowDelete, &Actions, LineSize);
		if(s_AllowDelete)
		{
			Actions.HSplitTop(MarginSmall, nullptr, &Actions);
			Actions.HSplitTop(ProfileMetrics.m_ButtonHeight, &Button, &Actions);
			static CButtonContainer s_DeleteButton;
			if(!ReadOnly && DoTClientSettingsButton_Menu(&s_DeleteButton, "tclient-profile-delete", Localize("Delete"), 0, &Button))
				DeleteSelectedProfile();
			Actions.HSplitTop(MarginSmall, nullptr, &Actions);
			Actions.HSplitTop(ProfileMetrics.m_ButtonHeight, &Button, &Actions);
			static CButtonContainer s_OverrideButton;
			if(!ReadOnly && DoTClientSettingsButton_Menu(&s_OverrideButton, "tclient-profile-override", Localize("Override"), 0, &Button))
			{
				if(CProfile *pProfile = pSelectedProfile())
					*pProfile = BuildProfileFromCurrentSettings();
			}
		}
	};

	auto RenderActions = [&](CUIRect MainView) {
		CPerfTimer ActionsTimer;
		const float PreviewHeight = pConstSelectedProfile() ? LineSize * 8.0f + MarginSmall * 3.0f : LineSize * 4.0f + MarginSmall;
		CUIRect Preview, Actions;
		MainView.HSplitTop(PreviewHeight, &Preview, &MainView);
		const float ActionsInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth + 2.0f * ProfileMetrics.m_ButtonHeight, ProfileMetrics.m_SectionGap, 1);
		if(MainView.w >= ActionsInlineMinWidth)
		{
			const float PreviewWidth = std::min(ProfileMetrics.m_LabelWidth * 2.5f, Preview.w * 0.55f);
			Preview.VSplitLeft(PreviewWidth, &Preview, &Actions);
			RenderProfilePreview(Preview);
			Actions.VMargin(MarginSmall, &Actions);
			RenderActionButtons(Actions);
		}
		else
		{
			RenderProfilePreview(Preview);
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			RenderActionButtons(MainView);
		}
		LogTClientPerfStageEx("tclient_profiles", "actions", ETClientSettingsPerfStage::INTERACTIVE_LAYER, ActionsTimer.ElapsedMs(), false);
	};

	auto RenderOptions = [&](CUIRect MainView) {
		CUIRect Left, Right;
		const float OptionsInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth + 2.0f * ProfileMetrics.m_ButtonHeight, ProfileMetrics.m_SectionGap, 1);
		if(MainView.w >= OptionsInlineMinWidth)
			MainView.VSplitMid(&Left, &Right, MarginSmall);
		else
			Left = MainView, Right = {};
		const auto RenderSaveLoad = [&](CUIRect &View) {
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileSkin, "tclient-profile-save-load-skin", Localize("Save/Load Skin"), &g_Config.m_TcProfileSkin, &View, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileColors, "tclient-profile-save-load-colors", Localize("Save/Load Colors"), &g_Config.m_TcProfileColors, &View, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileEmote, "tclient-profile-save-load-emote", Localize("Save/Load Emote"), &g_Config.m_TcProfileEmote, &View, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileName, "tclient-profile-save-load-name", Localize("Save/Load Name"), &g_Config.m_TcProfileName, &View, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileClan, "tclient-profile-save-load-clan", Localize("Save/Load Clan"), &g_Config.m_TcProfileClan, &View, LineSize);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileFlag, "tclient-profile-save-load-flag", Localize("Save/Load Flag"), &g_Config.m_TcProfileFlag, &View, LineSize);
		};
		RenderSaveLoad(Left);
		const auto RenderIdentityOptions = [&](CUIRect &View) {
			CTClientSettingsRowAllocator IdentityRows(View);
			CUIRect Row = IdentityRows.Next();
			if(!ReadOnly && DoTClientSettingsButton_CheckBox(&m_Dummy, "tclient-profile-dummy", Localize("Dummy"), m_Dummy, &Row))
				m_Dummy = 1 - m_Dummy;
			static int s_CustomColorId = 0;
			Row = IdentityRows.Next();
			if(!ReadOnly && DoTClientSettingsButton_CheckBox(&s_CustomColorId, "tclient-profile-custom-colors", Localize("Custom colors"), *pCurrentUseCustomColor, &Row))
			{
				*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
				SetNeedSendInfo();
			}
			Row = IdentityRows.Next();
			if(!ReadOnly && DoTClientSettingsButton_CheckBox(&g_Config.m_TcProfileOverwriteClanWithEmpty, "tclient-profile-overwrite-empty-clan", Localize("Overwrite clan even if empty"), g_Config.m_TcProfileOverwriteClanWithEmpty, &Row))
				g_Config.m_TcProfileOverwriteClanWithEmpty = 1 - g_Config.m_TcProfileOverwriteClanWithEmpty;
		};
		if(Right.w > 0.0f)
			RenderIdentityOptions(Right);
		else
		{
			Left.HSplitTop(MarginSmall, nullptr, &Left);
			RenderIdentityOptions(Left);
		}
	};

	auto RenderSavedProfiles = [&](CUIRect MainView) {
		CUIRect SelectorRect;
		MainView.HSplitTop(LineSize, &SelectorRect, &MainView);
		MainView.HSplitTop(ProfileMetrics.m_LineSpacing, nullptr, &MainView);

		static CButtonContainer s_ProfilesFile;
		const float ProfilesButtonWidth = std::clamp(ProfileMetrics.m_LabelWidth, ProfileMetrics.m_ButtonHeight * 4.0f, ProfileMetrics.m_LabelWidth * 1.25f);
		SelectorRect.VSplitLeft(ProfilesButtonWidth, &Button, &SelectorRect);
		if(!ReadOnly && DoTClientSettingsButton_Menu(&s_ProfilesFile, "tclient-profiles-file", Localize("Profiles file"), 0, &Button))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}

		static CListBox s_ProfilesListBox;
		static CListBox s_ProfilesReadOnlyListBox;
		CListBox &ProfilesListBox = ReadOnly ? s_ProfilesReadOnlyListBox : s_ProfilesListBox;
		ProfilesListBox.SetActive(!ReadOnly);
		ProfilesListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		ProfilesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		CPerfTimer ListTimer;
		const float ProfileItemWidth = std::max(ProfileMetrics.m_ListRowHeight * 6.0f, ProfileMetrics.m_LabelWidth + ProfileMetrics.m_ButtonHeight * 2.0f);
		const int ProfilesPerRow = maximum(1, (int)(MainView.w / ProfileItemWidth));
		const float ProfileListRowHeight = std::max(ProfileMetrics.m_ListRowHeight * 3.0f, ProfileMetrics.m_ButtonHeight * 3.0f + ProfileMetrics.m_LineSpacing * 2.0f);
		ProfilesListBox.DoStart(ProfileListRowHeight, vProfiles.size(), ProfilesPerRow, 3, SelectedProfile, &MainView, true, IGraphics::CORNER_ALL);

		static std::vector<int> s_vProfileItemIds;
		if(s_vProfileItemIds.size() != vProfiles.size())
		{
			s_vProfileItemIds.resize(vProfiles.size());
			for(size_t i = 0; i < s_vProfileItemIds.size(); ++i)
				s_vProfileItemIds[i] = (int)i;
		}

		for(size_t i = 0; i < vProfiles.size(); ++i)
		{
			CListboxItem Item = ProfilesListBox.DoNextItem(&s_vProfileItemIds[i], SelectedProfile >= 0 && (size_t)SelectedProfile == i);
			if(!Item.m_Visible)
				continue;

			RenderProfile(Item.m_Rect, vProfiles[i], false);
		}

		const int NewSelectedProfile = ProfilesListBox.DoEnd();
		if(!ReadOnly)
			SelectedProfile = NewSelectedProfile;
		if(!ReadOnly && ProfilesListBox.WasItemActivated())
			ApplySelectedProfile();
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "profiles=%d", (int)vProfiles.size());
		LogTClientPerfStageEx("tclient_profiles", "list", ETClientSettingsPerfStage::STATIC_LAYER, ListTimer.ElapsedMs(), false, aExtra);
	};

	const bool HasSelectedProfile = pConstSelectedProfile() != nullptr;
	const float ProfilePreviewHeight = HasSelectedProfile ? ProfileMetrics.m_ButtonHeight * 8.0f + MarginSmall * 3.0f : ProfileMetrics.m_ButtonHeight * 4.0f + MarginSmall;
	const float ProfileActionsHeight = s_AllowDelete ? ProfileMetrics.m_ButtonHeight * 5.0f + ProfileMetrics.m_LineSpacing * 6.0f : ProfileMetrics.m_ButtonHeight * 3.0f + ProfileMetrics.m_LineSpacing * 3.0f;
	const float ActionsInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth + 2.0f * ProfileMetrics.m_ButtonHeight, ProfileMetrics.m_SectionGap, 1);
	const float OptionsInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth + 2.0f * ProfileMetrics.m_ButtonHeight, ProfileMetrics.m_SectionGap, 1);
	const auto MeasureActions = [ProfilePreviewHeight, ProfileActionsHeight, ActionsInlineMinWidth](float ContentWidth) { return ContentWidth >= ActionsInlineMinWidth ? std::max(ProfilePreviewHeight, ProfileActionsHeight) : ProfilePreviewHeight + MarginSmall + ProfileActionsHeight; };
	const auto MeasureOptions = [ProfileMetrics, OptionsInlineMinWidth](float ContentWidth) {
		const float Rows = ContentWidth >= OptionsInlineMinWidth ? 6.0f : 9.0f;
		return Rows * ProfileMetrics.m_ButtonHeight + (Rows - 1.0f) * ProfileMetrics.m_LineSpacing;
	};
	const int ProfileCount = (int)vProfiles.size();
	const auto MeasureSavedProfiles = [ProfileMetrics, ProfileCount](float ContentWidth) { return ResolveSettingsProfilesListHeight(ProfileMetrics, ContentWidth, ProfileCount); };
	static std::array<CTClientSettingsCardFrameBinding, 3> s_aCardBindings;
	s_aCardBindings[0].Bind(MeasureActions, RenderActions);
	s_aCardBindings[1].Bind(MeasureOptions, RenderOptions);
	s_aCardBindings[2].Bind(MeasureSavedProfiles, RenderSavedProfiles);
	const uint64_t ProfilesLayoutRevision = (static_cast<uint64_t>(HasSelectedProfile) << 0) | (static_cast<uint64_t>(s_AllowDelete != 0) << 1) | (static_cast<uint64_t>(vProfiles.size()) << 2);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		constexpr std::array<std::pair<const char *, const char *>, 3> aSpecs = {{
			{"deck:tclient-profiles-actions", "Profiles"},
			{"deck:tclient-profiles-options", "Profile Options"},
			{"deck:tclient-profiles-list", "Saved Profiles"},
		}};
		vCards.reserve(aSpecs.size());
		for(size_t Index = 0; Index < aSpecs.size(); ++Index)
		{
			CTClientSettingsCardFrameBinding *pBinding = &s_aCardBindings[Index];
			SSettingsCardDefinition Definition;
			Definition.m_Spec = {aSpecs[Index].first, Localize(aSpecs[Index].second), qm_card_registry::ResolveLocalizedDescription(aSpecs[Index].first)};
			Definition.m_Measure = [pBinding](float ContentWidth) { return pBinding->Measure(ContentWidth); };
			Definition.m_Render = [pBinding](CUIRect Content) { pBinding->Render(Content); };
			Definition.m_MeasureRevision = Index == 0 || Index == 2 ? ProfilesLayoutRevision : 0;
			vCards.push_back(std::move(Definition));
		}
	};
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-profiles", ProfilesLayoutRevision));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static CScrollRegion s_ProfilesScrollRegion;
	static qm_card_order::CModel s_ProfilesPrewarmOrderModel;
	static bool s_ProfilesPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_ProfilesPrewarmDeck;
	if(ReadOnly && !s_ProfilesPrewarmOrderModelInitialized)
	{
		s_ProfilesPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_ProfilesPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_ProfilesPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_ProfilesPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-profiles-") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(ProfilesCtx, Page, "tclient-profiles", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ProfilesScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	for(CTClientSettingsCardFrameBinding &Binding : s_aCardBindings)
		Binding.Clear();
	if(!ReadOnly)
		s_SelectedProfile = SelectedProfile;
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogTClientPerfStage("tclient_profiles_total", RenderTimer.ElapsedMs(), false);
}

void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)
{
	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsContentMetrics ConfigMetrics = ResolveSettingsContentMetrics(MainView.w);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());
	IUiContext ConfigsCtx = SettingsUiContext("settings_tclient_configs", UiScale);
	if(ReadOnly)
	{
		ConfigsCtx.m_pAnim = nullptr;
		ConfigsCtx.m_pTree = nullptr;
	}
	// hi hello, this is a relatively self-contained mess, sorry if you're forking or need to modify this -Tater
	// 你好, 这是一个相对独立的混乱，如果你要分叉或需要修改它，抱歉 -Tater
	struct SIntStage
	{
		int m_Value;
	};
	struct SStrStage
	{
		std::string m_Value;
	};
	struct SColStage
	{
		unsigned m_Value;
	};
	enum class EConfigSource
	{
		DDNET,
		TCLIENT,
		QM,
	};
	static std::unordered_map<const SConfigVariable *, SIntStage> s_StagedInts;
	static std::unordered_map<const SConfigVariable *, SStrStage> s_StagedStrs;
	static std::unordered_map<const SConfigVariable *, SColStage> s_StagedCols;

	struct SIntState
	{
		CLineInputNumber m_Input;
		int m_LastValue = 0;
		bool m_Inited = false;
	};
	struct SStrState
	{
		CLineInputBuffered<512> m_Input;
		bool m_Inited = false;
	};
	struct SColState
	{
		unsigned m_LastValue = 0;
		unsigned m_Working = 0;
		bool m_Inited = false;
	};
	static std::unordered_map<const SConfigVariable *, SIntState> s_IntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_StrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ColInputs;
	static std::unordered_map<const SConfigVariable *, SIntState> s_ReadOnlyIntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_ReadOnlyStrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ReadOnlyColInputs;
	auto &IntInputs = ReadOnly ? s_ReadOnlyIntInputs : s_IntInputs;
	auto &StrInputs = ReadOnly ? s_ReadOnlyStrInputs : s_StrInputs;
	auto &ColInputs = ReadOnly ? s_ReadOnlyColInputs : s_ColInputs;

	auto ClearStagedAndCaches = [&]() {
		s_StagedInts.clear();
		s_StagedStrs.clear();
		s_StagedCols.clear();
		s_IntInputs.clear();
		s_StrInputs.clear();
		s_ColInputs.clear();
	};
	auto SortStagedKeys = [](auto &Staged) {
		std::vector<const SConfigVariable *> vKeys;
		vKeys.reserve(Staged.size());
		for(const auto &Entry : Staged)
			vKeys.push_back(Entry.first);
		std::sort(vKeys.begin(), vKeys.end(), [](const SConfigVariable *pLeft, const SConfigVariable *pRight) {
			return str_comp(pLeft->m_pScriptName, pRight->m_pScriptName) < 0;
		});
		return vKeys;
	};

	size_t ChangesCount = 0;

	static CLineInputBuffered<128> s_SearchInput;
	static int s_TcUiTagVisual = 0;
	static int s_TcUiTagHud = 0;
	static int s_TcUiTagInput = 0;
	static int s_TcUiTagChat = 0;
	static int s_TcUiTagAudio = 0;
	static int s_TcUiTagAutomation = 0;
	static int s_TcUiTagSocial = 0;
	static int s_TcUiTagCamera = 0;
	static int s_TcUiTagGameplay = 0;
	static int s_TcUiTagMisc = 0;

	ChangesCount = s_StagedInts.size() + s_StagedStrs.size() + s_StagedCols.size();
	constexpr float ConfigSearchLabelWidth = 50.0f;
	constexpr float ConfigSearchEditWidth = 250.0f;
	constexpr float ConfigDomainWidth = 85.0f;
	constexpr float ConfigFilterWidth = 90.0f;
	const float WideFiltersMinimumWidth = ResolveSettingsInlineRowMinimumWidth(
		ConfigSearchLabelWidth + ConfigSearchEditWidth + ConfigDomainWidth * 3.0f + ConfigFilterWidth * 2.0f + Margin,
		MarginSmall, 7);
	const auto UseNarrowConfigFilters = [WideFiltersMinimumWidth](float ContentWidth) {
		return ContentWidth < WideFiltersMinimumWidth;
	};
	const auto FiltersHeightForWidth = [UseNarrowConfigFilters](float ContentWidth) {
		return (LineSize + MarginSmall) * (UseNarrowConfigFilters(ContentWidth) ? 5.0f : 3.0f);
	};
	auto RenderActions = [&](CUIRect ApplyBar) {
		CPerfTimer ActionsTimer;
		CUIRect Row = ApplyBar;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;

		const float BtnWidth = 120.0f;
		CUIRect ApplyBtn, ClearBtn, Counter;
		Row.VSplitLeft(BtnWidth, &ApplyBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(BtnWidth, &ClearBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Counter);

		static CButtonContainer s_ApplyBtn, s_ClearBtn;
		int DisabledStyle = ChangesCount > 0 ? 0 : -1;
		const bool ApplyClicked = DoTClientSettingsButton_Menu(&s_ApplyBtn, "tclient-config-apply-changes", Localize("Apply Changes"), DisabledStyle, &ApplyBtn);
		if(ChangesCount > 0 && ApplyClicked)
		{
			for(const SConfigVariable *pVar : SortStagedKeys(s_StagedInts))
			{
				const auto Entry = s_StagedInts.find(pVar);
				dbg_assert(Entry != s_StagedInts.end(), "missing staged int");
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, Entry->second.m_Value);
				Console()->ExecuteLine(aCmd);
			}
			for(const SConfigVariable *pVar : SortStagedKeys(s_StagedStrs))
			{
				const auto Entry = s_StagedStrs.find(pVar);
				dbg_assert(Entry != s_StagedStrs.end(), "missing staged string");
				char aEsc[1024];
				aEsc[0] = '\0';
				char *pDst = aEsc;
				str_escape(&pDst, Entry->second.m_Value.c_str(), aEsc + sizeof(aEsc));
				char aCmd[1200];
				str_format(aCmd, sizeof(aCmd), "%s \"%s\"", pVar->m_pScriptName, aEsc);
				Console()->ExecuteLine(aCmd);
			}
			for(const SConfigVariable *pVar : SortStagedKeys(s_StagedCols))
			{
				const auto Entry = s_StagedCols.find(pVar);
				dbg_assert(Entry != s_StagedCols.end(), "missing staged color");
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %u", pVar->m_pScriptName, Entry->second.m_Value);
				Console()->ExecuteLine(aCmd);
			}
			ClearStagedAndCaches();
		}
		const bool ClearClicked = DoTClientSettingsButton_Menu(&s_ClearBtn, "tclient-config-clear-changes", Localize("Clear Changes"), DisabledStyle, &ClearBtn);
		if(ChangesCount > 0 && ClearClicked)
		{
			ClearStagedAndCaches();
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		DoTClientLabel(Ui(), &Counter, aBuf, TCLIENT_BODY_FONT_SIZE, TEXTALIGN_ML);
		LogTClientPerfStageEx("tclient_configs", "actions", ETClientSettingsPerfStage::INTERACTIVE_LAYER, ActionsTimer.ElapsedMs(), false, aBuf);
	};

	auto RenderFilters = [&](CUIRect Content) {
		const float SearchLabelW = ConfigSearchLabelWidth;
		if(UseNarrowConfigFilters(Content.w))
		{
			auto NextRow = [&]() {
				CUIRect Row;
				Content.HSplitTop(LineSize, &Row, &Content);
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				return Row;
			};
			auto RenderSearch = [&](CUIRect Row) {
				CUIRect SearchLabel, SearchEdit;
				Row.VSplitLeft(SearchLabelW, &SearchLabel, &SearchEdit);
				DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
				IUiContext TClientConfigSearchCtx;
				TClientConfigSearchCtx.m_pUi = Ui();
				TClientConfigSearchCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
				TClientConfigSearchCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
				TClientConfigSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_config_search");
				TClientConfigSearchCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
				ui_widget::InputField(TClientConfigSearchCtx, &s_SearchInput, SearchEdit, EditBoxFontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
			};
			auto RenderTags = [&](CUIRect Row, const char *pTitle, const std::array<const char *, 5> &aLabels, const std::array<int *, 5> &aValues, const std::array<const char *, 5> &aIds) {
				CUIRect Title, Area;
				Row.VSplitLeft(40.0f, &Title, &Area);
				if(pTitle != nullptr)
					DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Title, pTitle, FontSize, TEXTALIGN_ML);
				const float Gap = 5.0f;
				const float ButtonWidth = std::max(0.0f, (Area.w - Gap * 4.0f) / 5.0f);
				for(size_t Index = 0; Index < aLabels.size(); ++Index)
				{
					CUIRect Button;
					Area.VSplitLeft(ButtonWidth, &Button, &Area);
					if(DoTClientSettingsButton_CheckBox(aValues[Index], aIds[Index], Localize(aLabels[Index]), *aValues[Index], &Button))
						*aValues[Index] ^= 1;
					if(Index + 1 < aLabels.size())
						Area.VSplitLeft(Gap, nullptr, &Area);
				}
			};
			RenderSearch(NextRow());
			{
				CUIRect Row = NextRow();
				const float Gap = MarginSmall;
				const float ButtonWidth = std::max(0.0f, (Row.w - Gap * 2.0f) / 3.0f);
				CUIRect DomainDDNet, DomainTClient, DomainQm;
				Row.VSplitLeft(ButtonWidth, &DomainDDNet, &Row);
				Row.VSplitLeft(Gap, nullptr, &Row);
				Row.VSplitLeft(ButtonWidth, &DomainTClient, &Row);
				Row.VSplitLeft(Gap, nullptr, &Row);
				DomainQm = Row;
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiShowDDNet, "tclient-ui-show-ddnet", Localize("DDNet"), g_Config.m_TcUiShowDDNet, &DomainDDNet))
					g_Config.m_TcUiShowDDNet ^= 1;
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiShowTClient, "tclient-ui-show-tclient", Localize("TClient"), g_Config.m_TcUiShowTClient, &DomainTClient))
					g_Config.m_TcUiShowTClient ^= 1;
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiShowQm, "tclient-ui-show-qmclient", Localize("QmClient"), g_Config.m_TcUiShowQm, &DomainQm))
					g_Config.m_TcUiShowQm ^= 1;
			}
			{
				CUIRect Row = NextRow();
				const float Gap = MarginSmall;
				const float ButtonWidth = std::max(0.0f, (Row.w - Gap) / 2.0f);
				CUIRect Compact, Modified;
				Row.VSplitLeft(ButtonWidth, &Compact, &Row);
				Row.VSplitLeft(Gap, nullptr, &Row);
				Modified = Row;
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiCompactList, "tclient-ui-compact-list", Localize("Compact"), g_Config.m_TcUiCompactList, &Compact))
					g_Config.m_TcUiCompactList ^= 1;
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiOnlyModified, "tclient-ui-only-modified", Localize("Modified"), g_Config.m_TcUiOnlyModified, &Modified))
					g_Config.m_TcUiOnlyModified ^= 1;
			}
			RenderTags(NextRow(), Localize("Tags"), {"Visual", "HUD", "Input", "Chat", "Audio"}, {&s_TcUiTagVisual, &s_TcUiTagHud, &s_TcUiTagInput, &s_TcUiTagChat, &s_TcUiTagAudio}, {"tclient-ui-tag-visual", "tclient-ui-tag-hud", "tclient-ui-tag-input", "tclient-ui-tag-chat", "tclient-ui-tag-audio"});
			RenderTags(NextRow(), nullptr, {"Auto", "Social", "Camera", "Gameplay", "Misc"}, {&s_TcUiTagAutomation, &s_TcUiTagSocial, &s_TcUiTagCamera, &s_TcUiTagGameplay, &s_TcUiTagMisc}, {"tclient-ui-tag-auto", "tclient-ui-tag-social", "tclient-ui-tag-camera", "tclient-ui-tag-gameplay", "tclient-ui-tag-misc"});
			return;
		}
		CUIRect FilterBar, TagsBar;
		Content.HSplitTop(LineSize + MarginSmall, &FilterBar, &Content);
		Content.HSplitTop((LineSize + MarginSmall) * 2.0f, &TagsBar, &Content);
		CPerfTimer FilterTimer;
		CUIRect Row = FilterBar;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = FilterBar.y + (FilterBar.h - LineSize) / 2.0f;

		// 搜索框
		CUIRect SearchLabel, SearchEdit;
		Row.VSplitLeft(SearchLabelW, &SearchLabel, &Row);
		Row.VSplitLeft(ConfigSearchEditWidth, &SearchEdit, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		IUiContext TClientConfigSearchCtx;
		TClientConfigSearchCtx.m_pUi = Ui();
		TClientConfigSearchCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		TClientConfigSearchCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		TClientConfigSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_config_search");
		TClientConfigSearchCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		ui_widget::InputField(TClientConfigSearchCtx, &s_SearchInput, SearchEdit, EditBoxFontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

		// 分隔
		Row.VSplitLeft(MarginSmall, nullptr, &Row);

		// Domain 筛选 - DDNet / TClient / 栖梦
		const float DomainWidth = ConfigDomainWidth;
		CUIRect DomainDDNet, DomainTClient, DomainQm;
		Row.VSplitLeft(DomainWidth, &DomainDDNet, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(DomainWidth, &DomainTClient, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(DomainWidth, &DomainQm, &Row);
		Row.VSplitLeft(Margin, nullptr, &Row);

		if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiShowDDNet, "tclient-ui-show-ddnet", Localize("DDNet"), g_Config.m_TcUiShowDDNet, &DomainDDNet))
			g_Config.m_TcUiShowDDNet ^= 1;
		if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiShowTClient, "tclient-ui-show-tclient", Localize("TClient"), g_Config.m_TcUiShowTClient, &DomainTClient))
			g_Config.m_TcUiShowTClient ^= 1;
		if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiShowQm, "tclient-ui-show-qmclient", Localize("QmClient"), g_Config.m_TcUiShowQm, &DomainQm))
			g_Config.m_TcUiShowQm ^= 1;

		// 其他筛选 - 紧凑列表 / 仅显示已修改
		const float FilterWidth = ConfigFilterWidth;
		CUIRect FilterCompact, FilterModified;
		Row.VSplitLeft(FilterWidth, &FilterCompact, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(FilterWidth, &FilterModified, &Row);

		if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiCompactList, "tclient-ui-compact-list", Localize("Compact"), g_Config.m_TcUiCompactList, &FilterCompact))
			g_Config.m_TcUiCompactList ^= 1;
		if(DoTClientSettingsButton_CheckBox(&g_Config.m_TcUiOnlyModified, "tclient-ui-only-modified", Localize("Modified"), g_Config.m_TcUiOnlyModified, &FilterModified))
			g_Config.m_TcUiOnlyModified ^= 1;
		LogTClientPerfStageEx("tclient_configs", "filter", ETClientSettingsPerfStage::TEXT_CACHE, FilterTimer.ElapsedMs());

		// Tags Filter Bar - Row 1
		{
			CUIRect TagsRow = TagsBar;
			TagsRow.h = LineSize;
			TagsRow.y = TagsBar.y;

			const float TagLabelWidth = 40.0f;
			CUIRect TagsLabel, TagsArea;
			TagsRow.VSplitLeft(TagLabelWidth, &TagsLabel, &TagsArea);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &TagsLabel, Localize("Tags"), FontSize, TEXTALIGN_ML);

			// Calculate tag button width - fit 5 tags per row
			const float TagMargin = 5.0f;
			const int TagsPerRow = 5;
			float TagBtnWidth = (TagsArea.w - TagMargin * (TagsPerRow - 1)) / TagsPerRow;

			CUIRect TagBtn;
			TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagVisual, "tclient-ui-tag-visual", Localize("Visual"), s_TcUiTagVisual, &TagBtn))
				s_TcUiTagVisual ^= 1;

			TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
			TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagHud, "tclient-ui-tag-hud", Localize("HUD"), s_TcUiTagHud, &TagBtn))
				s_TcUiTagHud ^= 1;

			TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
			TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagInput, "tclient-ui-tag-input", Localize("Input"), s_TcUiTagInput, &TagBtn))
				s_TcUiTagInput ^= 1;

			TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
			TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagChat, "tclient-ui-tag-chat", Localize("Chat"), s_TcUiTagChat, &TagBtn))
				s_TcUiTagChat ^= 1;

			TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
			TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagAudio, "tclient-ui-tag-audio", Localize("Audio"), s_TcUiTagAudio, &TagBtn))
				s_TcUiTagAudio ^= 1;
		}

		// Tags Filter Bar - Row 2 (Automation, Social, Camera, Gameplay, Misc)
		{
			CUIRect TagsRow2 = TagsBar;
			TagsRow2.h = LineSize;
			TagsRow2.y = TagsBar.y + LineSize + 2.0f;

			const float TagLabelWidth = 40.0f;
			CUIRect TagsLabel2, TagsArea2;
			TagsRow2.VSplitLeft(TagLabelWidth, &TagsLabel2, &TagsArea2);
			// Leave label empty for second row alignment

			const float TagMargin = 5.0f;
			const int TagsPerRow = 5;
			float TagBtnWidth = (TagsArea2.w - TagMargin * (TagsPerRow - 1)) / TagsPerRow;

			CUIRect TagBtn;
			TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagAutomation, "tclient-ui-tag-auto", Localize("Auto"), s_TcUiTagAutomation, &TagBtn))
				s_TcUiTagAutomation ^= 1;

			TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
			TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagSocial, "tclient-ui-tag-social", Localize("Social"), s_TcUiTagSocial, &TagBtn))
				s_TcUiTagSocial ^= 1;

			TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
			TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagCamera, "tclient-ui-tag-camera", Localize("Camera"), s_TcUiTagCamera, &TagBtn))
				s_TcUiTagCamera ^= 1;

			TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
			TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagGameplay, "tclient-ui-tag-gameplay", Localize("Gameplay"), s_TcUiTagGameplay, &TagBtn))
				s_TcUiTagGameplay ^= 1;

			TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
			TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
			if(DoTClientSettingsButton_CheckBox(&s_TcUiTagMisc, "tclient-ui-tag-misc", Localize("Misc"), s_TcUiTagMisc, &TagBtn))
				s_TcUiTagMisc ^= 1;
		}
	};

	auto RenderList = [&](CUIRect ListArea) {
		const int FlagMask = CFGFLAG_CLIENT;
		auto BuildConfigTagMask = [](const char *pScriptName) {
			unsigned int Mask = 0;
			for(EConfigTag Tag : ConfigTagsManager()->GetTagsForVariable(pScriptName))
				Mask |= 1u << static_cast<unsigned int>(Tag);
			return Mask;
		};
		static std::vector<const SConfigVariable *> s_vAllClientVars;
		if(s_vAllClientVars.empty())
		{
			auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
				auto *pVec = static_cast<std::vector<const SConfigVariable *> *>(pUserData);
				pVec->push_back(pVar);
			};
			std::vector<const SConfigVariable *> vCollectedVars;
			ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &vCollectedVars);
			s_vAllClientVars = std::move(vCollectedVars);
			std::sort(s_vAllClientVars.begin(), s_vAllClientVars.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
				if(a->m_ConfigDomain != b->m_ConfigDomain)
					return a->m_ConfigDomain < b->m_ConfigDomain;
				return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
			});
		}
		static std::vector<unsigned int> s_vAllClientVarTagMasks;
		if(s_vAllClientVarTagMasks.size() != s_vAllClientVars.size())
		{
			s_vAllClientVarTagMasks.resize(s_vAllClientVars.size());
			for(size_t i = 0; i < s_vAllClientVars.size(); ++i)
				s_vAllClientVarTagMasks[i] = BuildConfigTagMask(s_vAllClientVars[i]->m_pScriptName);
		}

		auto GetConfigSource = [&](const SConfigVariable *pVar) {
			if(pVar->m_ConfigDomain == ConfigDomain::DDNET)
				return EConfigSource::DDNET;
			const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			if(str_startswith(pName, "qm_"))
				return EConfigSource::QM;
			return EConfigSource::TCLIENT;
		};

		auto SourceEnabled = [&](EConfigSource Source) {
			switch(Source)
			{
			case EConfigSource::DDNET: return g_Config.m_TcUiShowDDNet != 0;
			case EConfigSource::TCLIENT: return g_Config.m_TcUiShowTClient != 0;
			case EConfigSource::QM: return g_Config.m_TcUiShowQm != 0;
			default: return false;
			}
		};

		// Tags filter check
		auto TagEnabled = [&](EConfigTag Tag) -> bool {
			switch(Tag)
			{
			case EConfigTag::VISUAL: return s_TcUiTagVisual != 0;
			case EConfigTag::HUD: return s_TcUiTagHud != 0;
			case EConfigTag::INPUT: return s_TcUiTagInput != 0;
			case EConfigTag::CHAT: return s_TcUiTagChat != 0;
			case EConfigTag::AUDIO: return s_TcUiTagAudio != 0;
			case EConfigTag::AUTOMATION: return s_TcUiTagAutomation != 0;
			case EConfigTag::SOCIAL: return s_TcUiTagSocial != 0;
			case EConfigTag::CAMERA: return s_TcUiTagCamera != 0;
			case EConfigTag::GAMEPLAY: return s_TcUiTagGameplay != 0;
			case EConfigTag::MISC: return s_TcUiTagMisc != 0;
			default: return true;
			}
		};

		// Check if any tag filter is enabled
		bool AnyTagEnabled = s_TcUiTagVisual || s_TcUiTagHud || s_TcUiTagInput ||
				     s_TcUiTagChat || s_TcUiTagAudio || s_TcUiTagAutomation ||
				     s_TcUiTagSocial || s_TcUiTagCamera || s_TcUiTagGameplay ||
				     s_TcUiTagMisc;

		const char *pSearch = s_SearchInput.GetString();

		auto IsEffectiveDefaultVar = [&](const SConfigVariable *p) -> bool {
			if(p->m_Type == SConfigVariable::VAR_INT)
			{
				const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(p);
				auto Iter = s_StagedInts.find(p);
				int v = Iter != s_StagedInts.end() ? Iter->second.m_Value : *pInt->m_pVariable;
				return v == pInt->m_Default;
			}
			if(p->m_Type == SConfigVariable::VAR_STRING)
			{
				const SStringConfigVariable *pString = static_cast<const SStringConfigVariable *>(p);
				auto Iter = s_StagedStrs.find(p);
				const char *v = Iter != s_StagedStrs.end() ? Iter->second.m_Value.c_str() : pString->m_pStr;
				return str_comp(v, pString->m_pDefault) == 0;
			}
			if(p->m_Type == SConfigVariable::VAR_COLOR)
			{
				const SColorConfigVariable *pColor = static_cast<const SColorConfigVariable *>(p);
				auto Iter = s_StagedCols.find(p);
				unsigned v = Iter != s_StagedCols.end() ? Iter->second.m_Value : *pColor->m_pVariable;
				return v == pColor->m_Default;
			}
			return true;
		};

		auto BuildLocalizedConfigHelpText = [](const SConfigVariable *pVar) {
			const char *pHelpKey = pVar->m_pHelpLocalizeKey ? pVar->m_pHelpLocalizeKey : (pVar->m_pHelp ? pVar->m_pHelp : "");
			const char *pHelpText = pHelpKey[0] != '\0' ? Localize(pHelpKey) : "";
			char aHelp[512];
			if(pVar->m_Type == SConfigVariable::VAR_INT)
			{
				const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
				if(pInt->m_Min == pInt->m_Max)
					str_format(aHelp, sizeof(aHelp), "%s (%s: %d)", pHelpText, Localize("default"), pInt->m_Default);
				else if(pInt->m_Max == 0)
					str_format(aHelp, sizeof(aHelp), "%s (%s: %d, %s: %d)", pHelpText, Localize("default"), pInt->m_Default, Localize("minimum"), pInt->m_Min);
				else
					str_format(aHelp, sizeof(aHelp), "%s (%s: %d, %s: %d, %s: %d)", pHelpText, Localize("default"), pInt->m_Default, Localize("minimum"), pInt->m_Min, Localize("maximum"), pInt->m_Max);
			}
			else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
			{
				const SColorConfigVariable *pColor = static_cast<const SColorConfigVariable *>(pVar);
				str_format(aHelp, sizeof(aHelp), "%s (%s: $%0*X)", pHelpText, Localize("default"), pColor->m_Alpha ? 8 : 6, color_cast<ColorRGBA>(ColorHSLA(pColor->m_Default, pColor->m_Alpha)).Pack(pColor->m_Alpha));
			}
			else if(pVar->m_Type == SConfigVariable::VAR_STRING)
			{
				const SStringConfigVariable *pString = static_cast<const SStringConfigVariable *>(pVar);
				str_format(aHelp, sizeof(aHelp), "%s (%s: \"%s\", %s: %d)", pHelpText, Localize("default"), pString->m_pDefault, Localize("maximum"), (int)pString->m_MaxSize - 1);
			}
			else
				str_copy(aHelp, pHelpText);
			return std::string(aHelp);
		};

		unsigned int SelectedTagMask = 0;
		for(int Tag = 1; Tag < static_cast<int>(EConfigTag::NUM_TAGS); ++Tag)
		{
			const EConfigTag TagValue = static_cast<EConfigTag>(Tag);
			if(TagEnabled(TagValue))
				SelectedTagMask |= 1u << static_cast<unsigned int>(TagValue);
		}
		const unsigned int MiscTagMask = 1u << static_cast<unsigned int>(EConfigTag::MISC);
		const int DomainMask = (g_Config.m_TcUiShowDDNet != 0 ? 1 : 0) |
				       (g_Config.m_TcUiShowTClient != 0 ? 2 : 0) |
				       (g_Config.m_TcUiShowQm != 0 ? 4 : 0);
		static std::vector<const SConfigVariable *> s_vFilteredConfigs;
		static std::string s_CachedConfigSearch;
		static int s_CachedConfigDomainMask = -1;
		static int s_CachedConfigChangesCount = -1;
		static int s_CachedConfigOnlyModified = -1;
		static unsigned int s_CachedConfigTagMask = 0;
		static size_t s_CachedConfigVarCount = 0;
		static uint64_t s_CachedConfigLanguageHash = 0;
		const uint64_t ConfigLanguageHash = str_quickhash(g_Config.m_ClLanguagefile);
		if(s_CachedConfigSearch != (pSearch ? pSearch : "") ||
			s_CachedConfigDomainMask != DomainMask ||
			s_CachedConfigChangesCount != ChangesCount ||
			s_CachedConfigOnlyModified != g_Config.m_TcUiOnlyModified ||
			s_CachedConfigTagMask != SelectedTagMask ||
			s_CachedConfigVarCount != s_vAllClientVars.size() ||
			s_CachedConfigLanguageHash != ConfigLanguageHash)
		{
			s_vFilteredConfigs.clear();
			s_vFilteredConfigs.reserve(s_vAllClientVars.size());
			for(size_t i = 0; i < s_vAllClientVars.size(); ++i)
			{
				const SConfigVariable *pVar = s_vAllClientVars[i];
				if(!SourceEnabled(GetConfigSource(pVar)))
					continue;
				if(g_Config.m_TcUiOnlyModified && IsEffectiveDefaultVar(pVar))
					continue;
				if(pSearch && pSearch[0])
				{
					const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
					const char *pHelp = pVar->m_pHelp ? pVar->m_pHelp : "";
					std::string LocalizedHelp = BuildLocalizedConfigHelpText(pVar);
					if(!str_find_nocase(pName, pSearch) && !str_find_nocase(pHelp, pSearch) && !str_find_nocase(LocalizedHelp.c_str(), pSearch))
						continue;
				}
				if(AnyTagEnabled)
				{
					const unsigned int VarTagMask = s_vAllClientVarTagMasks[i];
					if((VarTagMask & SelectedTagMask) == 0 && !(VarTagMask == 0 && (SelectedTagMask & MiscTagMask) != 0))
						continue;
				}
				s_vFilteredConfigs.push_back(pVar);
			}
			s_CachedConfigSearch = pSearch ? pSearch : "";
			s_CachedConfigDomainMask = DomainMask;
			s_CachedConfigChangesCount = ChangesCount;
			s_CachedConfigOnlyModified = g_Config.m_TcUiOnlyModified;
			s_CachedConfigTagMask = SelectedTagMask;
			s_CachedConfigVarCount = s_vAllClientVars.size();
			s_CachedConfigLanguageHash = ConfigLanguageHash;
		}
		const std::vector<const SConfigVariable *> &vpFiltered = s_vFilteredConfigs;

		static CScrollRegion s_ConfigListScrollRegion;
		static CScrollRegion s_ConfigListReadOnlyScrollRegion;
		CScrollRegion &ConfigListScrollRegion = ReadOnly ? s_ConfigListReadOnlyScrollRegion : s_ConfigListScrollRegion;
		vec2 ScrollOffset(0.0f, 0.0f);
		SQmScrollRequest ConfigListScrollRequest;
		ConfigListScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
		const float ConfigInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ConfigMetrics.m_LabelWidth + 2.0f * ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_SectionGap, 1);
		const bool StackedConfigRows = ListArea.w < ConfigInlineMinWidth;
		const float ConfigHelpHeight = std::max(MarginSmall, FontSize - 2.0f);
		const SSettingsConfigRowMetrics ConfigRowMetrics = ResolveSettingsConfigRowMetrics(g_Config.m_TcUiCompactList != 0, StackedConfigRows, LineSize, MarginSmall, ConfigHelpHeight, ColorPickerLineSize, ConfigMetrics.m_LineSpacing);
		const float ConfigRowHeight = ConfigRowMetrics.m_RowHeight;
		ConfigListScrollRequest.m_RowExtent = ConfigRowHeight;
		CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ConfigListScrollRequest, UiScale, 0.0f));
		CPerfTimer ListTimer;
		static float s_PrevConfigsScrollY = 0.0f;
		static float s_PrevConfigsReadOnlyScrollY = 0.0f;
		float &PrevConfigsScrollY = ReadOnly ? s_PrevConfigsReadOnlyScrollY : s_PrevConfigsScrollY;
		SSettingsScrollRegionFrame ScrollFrame = BeginSettingsScrollRegion(ConfigListScrollRegion, &ListArea, ScrollParams, PrevConfigsScrollY);
		ScrollOffset = ScrollFrame.m_BeginOffset;

		ListArea.y += ScrollOffset.y;
		ListArea.VSplitRight(5.0f, &ListArea, nullptr);
		CUIRect Content = ListArea;

		auto SourceName = [](EConfigSource Source) {
			switch(Source)
			{
			case EConfigSource::DDNET: return "DDNet";
			case EConfigSource::TCLIENT: return "TClient";
			case EConfigSource::QM: return "QmClient";
			default: return "Other";
			}
		};

		bool HasCurrentSource = false;
		EConfigSource CurrentSource = EConfigSource::DDNET;
		for(const SConfigVariable *pVar : vpFiltered)
		{
			const EConfigSource Source = GetConfigSource(pVar);
			if(!HasCurrentSource || Source != CurrentSource)
			{
				HasCurrentSource = true;
				CurrentSource = Source;
				CUIRect Header;
				Content.HSplitTop(HeadlineHeight, &Header, &Content);
				if(ConfigListScrollRegion.AddRect(Header))
					Ui()->DoLabel(&Header, SourceName(CurrentSource), HeadlineFontSize, TEXTALIGN_ML);
				Content.HSplitTop(MarginSmall, nullptr, &Content);
			}

			CUIRect RowItem;
			const float RowHeight = ConfigRowHeight;
			Content.HSplitTop(RowHeight, &RowItem, &Content);
			Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
			const bool Visible = ConfigListScrollRegion.AddRect(RowItem);
			if(!Visible)
				continue;

			const bool Modified = !IsEffectiveDefaultVar(pVar);
			const ColorRGBA BgModified = ColorRGBA(1.0f, 0.8f, 0.0f, 0.15f);
			const ColorRGBA BgNormal = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
			RowItem.Draw(Modified ? BgModified : BgNormal, IGraphics::CORNER_ALL, 6.0f);

			CUIRect RowContent;
			RowItem.Margin(5.0f, &RowContent);

			CUIRect TopLine, Below;
			IUiContext TClientConfigTextInputCtx;
			TClientConfigTextInputCtx.m_pUi = Ui();
			TClientConfigTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientConfigTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientConfigTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_config_text_inputs");
			TClientConfigTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			if(g_Config.m_TcUiCompactList)
			{
				const float UsedHeight = ConfigRowMetrics.m_ControlBlockHeight;
				TopLine = RowContent;
				TopLine.h = UsedHeight;
				TopLine.y = round_to_int(RowContent.y + (RowContent.h - UsedHeight) / 2.0f);
				Below = RowContent;
			}
			else
			{
				RowContent.HSplitTop(ConfigRowMetrics.m_ControlBlockHeight, &TopLine, &Below);
			}
			CUIRect NameLine, Right;
			if(StackedConfigRows)
			{
				TopLine.HSplitTop(LineSize, &NameLine, &TopLine);
				TopLine.HSplitTop(MarginSmall, nullptr, &TopLine);
				TopLine.HSplitTop(ConfigRowMetrics.m_ControlLineHeight, &Right, nullptr);
			}
			else
				TopLine.VSplitRight(std::clamp(TopLine.w * 0.46f, 170.0f, 320.0f), &NameLine, &Right);
			NameLine.VSplitLeft(std::min(10.0f, NameLine.w), nullptr, &NameLine);

			Ui()->DoLabel(&NameLine, pVar->m_pScriptName, FontSize, TEXTALIGN_ML);

			CUIRect Controls, ResetRect;
			Right.VSplitRight(std::min(100.0f, std::max(0.0f, Right.w * 0.25f)), &Controls, &ResetRect);
			Controls.h = LineSize;
			Controls.y = Right.y + (Right.h - LineSize) / 2.0f;
			ResetRect.h = LineSize;
			ResetRect.y = Controls.y;
			Controls.VSplitRight(MarginSmall, &Controls, nullptr);

			if(!g_Config.m_TcUiCompactList)
			{
				CUIRect Help;
				Below.HSplitTop(ConfigRowMetrics.m_HelpGap, nullptr, &Below);
				Help = Below;
				Help.VSplitLeft(10.0f, nullptr, &Help);
				const std::string LocalizedHelp = BuildLocalizedConfigHelpText(pVar);
				Ui()->DoLabel(&Help, LocalizedHelp.c_str(), ConfigHelpHeight, TEXTALIGN_ML);
			}

			static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ResetBtns;
			if(Modified && pVar->m_Type != SConfigVariable::VAR_COLOR)
			{
				CButtonContainer &ResetBtn = s_ResetBtns[pVar];
				if(DoTClientSettingsButton_Menu(&ResetBtn, "tclient-config-reset", Localize("Reset"), 0, &ResetRect))
				{
					if(pVar->m_Type == SConfigVariable::VAR_INT)
					{
						const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
						s_StagedInts[pVar] = {pInt->m_Default};
					}
					else if(pVar->m_Type == SConfigVariable::VAR_STRING)
					{
						const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
						s_StagedStrs[pVar] = {std::string(pStr->m_pDefault)};
					}
				}
			}

			if(pVar->m_Type == SConfigVariable::VAR_INT)
			{
				const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
				// treat 0 1 ints as checkboxes
				if(pInt->m_Min == 0 && pInt->m_Max == 1)
				{
					const auto StagedInt = s_StagedInts.find(pVar);
					const int Effective = StagedInt != s_StagedInts.end() ? StagedInt->second.m_Value : *pInt->m_pVariable;
					if(DoButton_CheckBox(pVar, "", Effective, &Controls))
					{
						const int NewVal = Effective ? 0 : 1;
						if(NewVal == *pInt->m_pVariable)
							s_StagedInts.erase(pVar);
						else
							s_StagedInts[pVar] = {NewVal};
					}
				}
				else
				{
					SIntState &State = IntInputs[pVar];
					const auto StagedInt = s_StagedInts.find(pVar);
					const int Effective = StagedInt != s_StagedInts.end() ? StagedInt->second.m_Value : *pInt->m_pVariable;
					if(!State.m_Inited)
					{
						State.m_Input.SetInteger(Effective);
						State.m_LastValue = Effective;
						State.m_Inited = true;
					}
					else if(!State.m_Input.IsActive() && State.m_LastValue != Effective)
					{
						State.m_Input.SetInteger(Effective);
						State.m_LastValue = Effective;
					}

					CUIRect InputBox, Dummy;
					Controls.VSplitLeft(60.0f, &InputBox, &Dummy);

					if(ui_widget::InputField(TClientConfigTextInputCtx, &State.m_Input, InputBox, nullptr, EditBoxFontSize))
					{
						int NewVal = State.m_Input.GetInteger();
						bool InRange = true;
						if(pInt->m_Min != pInt->m_Max)
						{
							if(NewVal < pInt->m_Min)
								InRange = false;
							if(pInt->m_Max != 0 && NewVal > pInt->m_Max)
								InRange = false;
						}
						if(InRange && NewVal != State.m_LastValue)
						{
							if(NewVal == *pInt->m_pVariable)
								s_StagedInts.erase(pVar);
							else
								s_StagedInts[pVar] = {NewVal};
							State.m_LastValue = NewVal;
						}
					}
				}
			}
			else if(pVar->m_Type == SConfigVariable::VAR_STRING)
			{
				const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
				SStrState &State = StrInputs[pVar];
				const auto StagedStr = s_StagedStrs.find(pVar);
				const char *Effective = StagedStr != s_StagedStrs.end() ? StagedStr->second.m_Value.c_str() : pStr->m_pStr;
				if(!State.m_Inited)
				{
					State.m_Input.Set(Effective);
					State.m_Inited = true;
				}
				else if(!State.m_Input.IsActive())
				{
					if(str_comp(State.m_Input.GetString(), Effective) != 0)
						State.m_Input.Set(Effective);
				}

				if(ui_widget::InputField(TClientConfigTextInputCtx, &State.m_Input, Controls, nullptr, EditBoxFontSize))
				{
					const char *NewVal = State.m_Input.GetString();
					if(str_comp(NewVal, pStr->m_pStr) == 0)
						s_StagedStrs.erase(pVar);
					else
						s_StagedStrs[pVar] = {std::string(NewVal)};
				}
			}
			else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
			{
				const SColorConfigVariable *pCol = static_cast<const SColorConfigVariable *>(pVar);
				CUIRect ColorRect;
				ColorRect.x = Controls.x;
				ColorRect.h = ColorPickerLineSize;
				ColorRect.y = Right.y + (Right.h - ColorPickerLineSize) / 2.0f;
				ColorRect.w = ColorPickerLineSize + 8.0f + 60.0f;
				const ColorRGBA DefaultColor = color_cast<ColorRGBA>(ColorHSLA(pCol->m_Default, true).UnclampLighting(pCol->m_DarkestLighting));
				static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ColorResetIds;
				CButtonContainer &ResetId = s_ColorResetIds[pVar];

				SColState &ColState = ColInputs[pVar];
				const auto StagedCol = s_StagedCols.find(pVar);
				unsigned Effective = StagedCol != s_StagedCols.end() ? StagedCol->second.m_Value : *pCol->m_pVariable;
				if(!ColState.m_Inited)
				{
					ColState.m_Working = Effective;
					ColState.m_LastValue = Effective;
					ColState.m_Inited = true;
				}
				else
				{
					const bool EditingThis = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &ColState.m_Working;
					if(!EditingThis && ColState.m_Working != Effective)
					{
						ColState.m_Working = Effective;
						ColState.m_LastValue = Effective;
					}
				}

				DoLine_ColorPicker(&ResetId, CurrentSettingsContentMetrics(), &ColorRect, "", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);
				if(ColState.m_Working != Effective)
				{
					if(ColState.m_Working == *pCol->m_pVariable)
						s_StagedCols.erase(pVar);
					else
						s_StagedCols[pVar] = {ColState.m_Working};
					ColState.m_LastValue = ColState.m_Working;
				}
			}
		}

		CUIRect EndPad{Content.x, Content.y, Content.w, 5.0f};
		FinishSettingsScrollRegion(ConfigListScrollRegion, ScrollFrame, &EndPad);
		PrevConfigsScrollY = ScrollFrame.m_FinalOffsetY;
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "filtered=%d scroll_y=%.1f", (int)vpFiltered.size(), ScrollFrame.m_FinalOffsetY);
		LogTClientPerfStageEx("tclient_configs", "list", ETClientSettingsPerfStage::STATIC_LAYER, ListTimer.ElapsedMs(), false, aExtra);
	};

	const float SectionHeadingHeight = HeadlineHeight + MarginSmall;
	const float SectionGap = MarginSmall * 2.0f;
	const auto ConfigListViewportHeightForWidth = [ConfigMetrics](float ContentWidth) {
		const float ConfigInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ConfigMetrics.m_LabelWidth + 2.0f * ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_SectionGap, 1);
		const bool StackedRows = ContentWidth < ConfigInlineMinWidth;
		const float HelpHeight = std::max(MarginSmall, FontSize - 2.0f);
		const SSettingsConfigRowMetrics RowMetrics = ResolveSettingsConfigRowMetrics(g_Config.m_TcUiCompactList != 0, StackedRows, ConfigMetrics.m_LineHeight, ConfigMetrics.m_LineSpacing, HelpHeight, ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_LineSpacing);
		return std::max(RowMetrics.m_RowHeight * 2.0f, ContentWidth * 0.52f);
	};
	const auto ConfigsContentHeightForWidth = [ConfigListViewportHeightForWidth, SectionHeadingHeight, SectionGap, FiltersHeightForWidth](float ContentWidth) {
		return LineSize + FiltersHeightForWidth(ContentWidth) + ConfigListViewportHeightForWidth(ContentWidth) + SectionHeadingHeight * 2.0f + SectionGap * 2.0f;
	};
	const auto RenderConfigCard = [&](CUIRect Content) {
		CUIRect ChangesHeading, Actions, FiltersHeading, Filters, List;
		const float FiltersHeight = FiltersHeightForWidth(Content.w);
		const float ListViewportHeight = ConfigListViewportHeightForWidth(Content.w);
		Content.HSplitTop(SectionHeadingHeight, &ChangesHeading, &Content);
		Content.HSplitTop(LineSize, &Actions, &Content);
		Content.HSplitTop(SectionGap, nullptr, &Content);
		Content.HSplitTop(SectionHeadingHeight, &FiltersHeading, &Content);
		Content.HSplitTop(FiltersHeight, &Filters, &Content);
		Content.HSplitTop(SectionGap, nullptr, &Content);
		Content.HSplitTop(ListViewportHeight, &List, &Content);
		DoSettingsMenuLabel(g_Config.m_UiSettingsPage, m_TClientSettingsTab, -1, "tclient-config-changes-heading", &ChangesHeading, Localize("Config Changes"), HeadlineFontSize, TEXTALIGN_ML);
		DoSettingsMenuLabel(g_Config.m_UiSettingsPage, m_TClientSettingsTab, -1, "tclient-config-filters-heading", &FiltersHeading, Localize("Config Filters"), HeadlineFontSize, TEXTALIGN_ML);
		RenderActions(Actions);
		RenderFilters(Filters);
		RenderList(List);
	};
	static CTClientSettingsCardFrameBinding s_CardBinding;
	s_CardBinding.Bind(ConfigsContentHeightForWidth, RenderConfigCard);
	const uint64_t ConfigsLayoutRevision = static_cast<uint64_t>(g_Config.m_TcUiCompactList != 0);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		SSettingsCardDefinition Definition;
		Definition.m_Spec = {"deck:tclient-configs-actions", Localize("Configuration"), qm_card_registry::ResolveLocalizedDescription("deck:tclient-configs-actions")};
		Definition.m_Measure = [](float ContentWidth) { return s_CardBinding.Measure(ContentWidth); };
		Definition.m_Render = [](CUIRect Content) { s_CardBinding.Render(Content); };
		Definition.m_MeasureRevision = ConfigsLayoutRevision;
		vCards.push_back(std::move(Definition));
	};
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, TClientCardDefinitionsLayoutRevision(ReadOnly, m_TClientSettingsTab, "tclient-configs", ConfigsLayoutRevision));

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static CScrollRegion s_ConfigsScrollRegion;
	static qm_card_order::CModel s_ConfigsPrewarmOrderModel;
	static bool s_ConfigsPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_ConfigsPrewarmDeck;
	if(ReadOnly && !s_ConfigsPrewarmOrderModelInitialized)
	{
		s_ConfigsPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_ConfigsPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_ConfigsPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_ConfigsPrewarmDeck : m_SettingsCardDeck;
	if(!ReadOnly && str_startswith(m_SettingsCardFocusStableId.c_str(), "deck:tclient-configs-") != nullptr)
	{
		CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(ConfigsCtx, Page, "tclient-configs", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ConfigsScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	s_CardBinding.Clear();
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogTClientPerfStage("tclient_configs_total", RenderTimer.ElapsedMs(), false);
}
