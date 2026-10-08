#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTCLIENTINTERNAL_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTCLIENTINTERNAL_H
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/ui_scrollregion.h>

#include <cstddef>
#include <cstdint>
#include <vector>
namespace qm_tclient_cards
{
	inline bool PerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	inline void LogTClientPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/tclient", pStage, DurationMs, Force, nullptr, nullptr, nullptr, pExtra);
	}

	inline void LogTClientPerfStageEx(const char *pScope, const char *pSection, ETClientSettingsPerfStage Stage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		char aStage[128];
		if(pSection != nullptr && pSection[0] != '\0')
			str_format(aStage, sizeof(aStage), "%s_%s_%s", pScope, pSection, SettingsTClientPerfStageName(Stage));
		else
			str_format(aStage, sizeof(aStage), "%s_%s", pScope, SettingsTClientPerfStageName(Stage));
		LogTClientPerfStage(aStage, DurationMs, Force, pExtra);
	}

	class CUiRenderOnlyGuard
	{
	public:
		explicit CUiRenderOnlyGuard(CUi *pUi) :
			m_pUi(pUi)
		{
			m_pUi->BeginRenderOnly();
		}

		CUiRenderOnlyGuard(const CUiRenderOnlyGuard &) = delete;
		CUiRenderOnlyGuard &operator=(const CUiRenderOnlyGuard &) = delete;

		~CUiRenderOnlyGuard()
		{
			m_pUi->EndRenderOnly();
		}

	private:
		CUi *m_pUi;
	};

	inline int s_TClientWarListFilterRevision = 0;

	enum
	{
		TCLIENT_TAB_SETTINGS = 0,
		TCLIENT_TAB_BINDWHEEL,
		TCLIENT_TAB_WARLIST,
		TCLIENT_TAB_BINDCHAT,
		TCLIENT_TAB_STATUSBAR,
		NUMBER_OF_TCLIENT_TABS
	};

	constexpr float TCLIENT_BODY_FONT_SIZE = CMenus::TCLIENT_SETTINGS_BODY_FONT_SIZE;
	constexpr float TCLIENT_HEADLINE_FONT_SIZE = 20.0f;

	inline SLabelProperties TClientFixedLabelProperties(float FontSize, float MaxWidth = -1.0f)
	{
		SLabelProperties Props;
		Props.m_MaxWidth = MaxWidth;
		Props.m_MinimumFontSize = FontSize;
		Props.m_EllipsisAtEnd = true;
		return Props;
	}

	inline void DoTClientLabel(CUi *pUi, const CUIRect *pRect, const char *pText, float FontSize, int Align)
	{
		pUi->DoLabel(pRect, pText, FontSize, Align, TClientFixedLabelProperties(FontSize, pRect->w));
	}

	struct SSectionCullContext
	{
		float m_ViewportTop;
		float m_ViewportBottom;
		float m_PrefetchPadding;
	};
	inline bool IsSectionVisible(const CUIRect &SectionRect, const SSectionCullContext &Context)
	{
		return SectionRect.y + SectionRect.h >= Context.m_ViewportTop - Context.m_PrefetchPadding &&
		       SectionRect.y <= Context.m_ViewportBottom + Context.m_PrefetchPadding;
	}
	inline float FontSize = ui_token::font::BODY;
	inline float EditBoxFontSize = ui_token::font::BODY;
	inline float LineSize = ui_token::settings::ROW_HEIGHT;
	inline float ColorPickerLineSize = ui_token::settings::ROW_HEIGHT + ui_token::settings::ROW_GAP;
	inline float HeadlineFontSize = ui_token::font::HEADLINE;
	inline float StandardFontSize = ui_token::font::BODY;
	// 卡片角标一类的小字：与设置页小字号度量同源，避免在业务页散落裸字号。
	inline float SmallFontSize = ui_token::font::SMALL;

	inline float HeadlineHeight = ui_token::font::HEADLINE;
	const float Margin = 10.0f;
	inline float MarginSmall = ui_token::settings::ROW_GAP;
	inline float MarginExtraSmall = ui_token::settings::ROW_GAP;
	inline float MarginBetweenSections = ui_token::settings::ROW_GAP * 2.0f;

	inline float ColorPickerLabelSize = ui_token::font::BODY;
	inline float ColorPickerLineSpacing = ui_token::settings::ROW_GAP;
	inline std::vector<CButtonContainer> s_vTinyTeeModeButtons = {{}, {}, {}};
	inline int s_CountFrozenText = 0;
	inline CUi::SDropDownState s_TrailDropDownState;
	inline CScrollRegion s_TrailDropDownScrollRegion;
	inline CUi::SDropDownState s_TrailStyleDropDownState;
	inline CScrollRegion s_TrailStyleDropDownScrollRegion;

	inline float TClientSettingsRowsHeight(const int NumRows)
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

	inline void ApplyTClientContentMetrics(const SSettingsContentMetrics &Metrics)
	{
		FontSize = Metrics.m_BodySize;
		EditBoxFontSize = Metrics.m_BodySize;
		LineSize = Metrics.m_LineHeight;
		ColorPickerLineSize = Metrics.m_ButtonHeight;
		HeadlineFontSize = Metrics.m_HeadlineSize;
		StandardFontSize = Metrics.m_BodySize;
		SmallFontSize = Metrics.m_SmallSize;
		HeadlineHeight = Metrics.m_LineHeight;
		MarginSmall = Metrics.m_LineSpacing;
		MarginExtraSmall = Metrics.m_LineSpacing;
		MarginBetweenSections = Metrics.m_SectionGap;
		ColorPickerLabelSize = Metrics.m_BodySize;
		ColorPickerLineSpacing = Metrics.m_LineSpacing;
	}

	inline void ApplyTClientContentMetrics(float Width)
	{
		ApplyTClientContentMetrics(ResolveSettingsContentMetrics(Width));
	}

	inline uint64_t HashBytesFnv1a64(uint64_t Hash, const void *pData, size_t DataSize)
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
	inline uint64_t HashValueFnv1a64(uint64_t Hash, const T &Value)
	{
		return HashBytesFnv1a64(Hash, &Value, sizeof(Value));
	}
	[[maybe_unused]] inline uint64_t HashStringFnv1a64(uint64_t Hash, const char *pString)
	{
		return pString == nullptr ? Hash : HashBytesFnv1a64(Hash, pString, str_length(pString));
	}
	inline uint64_t HashTClientSettingsCardLayout(const char *pStableCardId)
	{
		uint64_t Hash = 1469598103934665603ull;
		if(str_comp(pStableCardId, "tclient:font") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmCustomFontWeight);
			// 样式行随字体族是否拥有多样式而出现/隐藏，卡片内容高度依赖字体族。
			Hash = HashStringFnv1a64(Hash, g_Config.m_QmCustomFont);
			Hash = HashStringFnv1a64(Hash, g_Config.m_QmCustomFontCjk);
			return HashValueFnv1a64(Hash, g_Config.m_QmCustomFontWeightCjk);
		}
		if(str_comp(pStableCardId, "tclient:input") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmFastInput);
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmFastInputOthers);
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmAutoMargin);
			return HashValueFnv1a64(Hash, g_Config.m_ClSubTickAiming);
		}
		if(str_comp(pStableCardId, "tclient:cursor") == 0)
			return Hash;
		if(str_comp(pStableCardId, "tclient:visual-nameplates") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_QmWhiteFeet);
		if(str_comp(pStableCardId, "tclient:visual-effects") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmTinyTees > 0);
			return HashValueFnv1a64(Hash, g_Config.m_QmJellyTee);
		}
		if(str_comp(pStableCardId, "tclient:anti-latency-tools") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmRemoveAnti);
			return HashValueFnv1a64(Hash, g_Config.m_QmPredMarginInFreeze);
		}
		if(str_comp(pStableCardId, "tclient:auto-reply") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmAutoReplyMuted);
			return HashValueFnv1a64(Hash, g_Config.m_QmAutoReplyMinimized);
		}
		if(str_comp(pStableCardId, "tclient:player-indicator") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmIndicatorVariableDistance);
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmWarListIndicator);
			return HashValueFnv1a64(Hash, g_Config.m_QmWarListIndicatorColors);
		}
		if(str_comp(pStableCardId, "tclient:hud") == 0)
		{
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmRenderCursorSpec);
			Hash = HashValueFnv1a64(Hash, g_Config.m_QmNotifyWhenLast);
			return HashValueFnv1a64(Hash, g_Config.m_QmShowCenter);
		}
		if(str_comp(pStableCardId, "tclient:tee-status-bar") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_QmShowFrozenText > 0);
		if(str_comp(pStableCardId, "tclient:finish-name") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_QmChangeNameNearFinish != 0);
		if(str_comp(pStableCardId, "tclient:tee-trails") == 0)
			return HashValueFnv1a64(Hash, g_Config.m_QmTeeTrailColorMode == CTrails::COLORMODE_SOLID);
		return Hash;
	}
}
#endif
