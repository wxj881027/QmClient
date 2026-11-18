#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTCLIENTINTERNAL_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTCLIENTINTERNAL_H
#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/warlist_model.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/lineinput.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qm_card_catalog
{
	struct SAudioPackEntry
	{
		char m_aName[sizeof(CConfig::m_SndPack)];
		int m_FileCount;
	};

	inline int FindAudioPackIndexByName(const std::vector<SAudioPackEntry> &vPacks, const char *pConfiguredName)
	{
		const char *pName = pConfiguredName[0] == '\0' ? "default" : pConfiguredName;
		const auto It = std::find_if(vPacks.begin(), vPacks.end(), [pName](const SAudioPackEntry &Pack) { return str_comp(Pack.m_aName, pName) == 0; });
		return It == vPacks.end() ? -1 : static_cast<int>(It - vPacks.begin());
	}
}

namespace qm_tclient_cards
{
	class CTClientBindWheelSelection
	{
		int m_Index = -1;
		CBindWheel::CBind m_Bind;

	public:
		void Select(int Index, const CBindWheel::CBind &Bind)
		{
			m_Index = Index;
			m_Bind = Bind;
		}

		void Clear() { m_Index = -1; }

		int Resolve(const std::vector<CBindWheel::CBind> &vBinds) const
		{
			if(m_Index < 0 || static_cast<size_t>(m_Index) >= vBinds.size() || !(vBinds[m_Index] == m_Bind))
				return -1;
			return m_Index;
		}
	};

	class CTClientConfigStringInput
	{
		std::vector<char> m_vBuffer;

	public:
		CTClientConfigStringInput(const SStringConfigVariable &Variable, const char *pValue) :
			m_vBuffer(Variable.m_MaxSize),
			m_Input(m_vBuffer.data(), m_vBuffer.size())
		{
			m_Input.Set(pValue);
		}

		CTClientConfigStringInput(const CTClientConfigStringInput &) = delete;
		CTClientConfigStringInput &operator=(const CTClientConfigStringInput &) = delete;

		CLineInput m_Input;
	};

	inline std::optional<std::string> PrepareTClientConfigStringCommand(IConsole &Console, const SStringConfigVariable &Variable, const char *pValue)
	{
		std::string Command(Variable.MaxSerializedSize(), '\0');
		Variable.Serialize(Command.data(), Command.size(), pValue);
		// 部分文本配置比控制台的单行解析容量更大。
		if(!Console.LineIsValid(Command.c_str()))
			return std::nullopt;
		Command.resize(str_length(Command.c_str()));
		return Command;
	}

	// 确认弹窗跨帧，期间条目和分组可能被控制台修改。
	struct SWarListRemoval
	{
		uint64_t m_Revision;
		size_t m_Index;

		void RemoveEntry(CQmWarListModel &Model) const
		{
			if(Model.Revision() == m_Revision && m_Index < Model.m_vWarEntries.size())
				Model.RemoveWarEntry(&Model.m_vWarEntries[m_Index]);
		}

		void RemoveType(CQmWarListModel &Model) const
		{
			if(Model.Revision() == m_Revision && m_Index < Model.m_WarTypes.size())
				Model.RemoveWarType(Model.m_WarTypes[m_Index]->m_aWarName);
		}
	};

	inline void RenameEditableWarType(CQmWarListModel &Model, const CWarType *pType, const char *pName, ColorRGBA Color)
	{
		const auto It = std::find(Model.m_WarTypes.begin(), Model.m_WarTypes.end(), pType);
		if(It != Model.m_WarTypes.end() && (*It)->m_Removable)
			Model.UpsertWarType(static_cast<int>(It - Model.m_WarTypes.begin()), pName, Color);
	}

	inline const CWarType *PlacePlayerIndicatorWarGroupRow(
		CSettingsContentLayoutFlow &Flow, CUIRect &Row, const CQmWarListModel &WarList,
		int BuiltinGroup, const char *pRowId, float RowHeight, float Spacing)
	{
		const CWarType *pType = WarList.FindActiveBuiltinWarType(BuiltinGroup);
		return pType != nullptr && Flow.Control(pRowId, RowHeight, Row, Spacing) ? pType : nullptr;
	}

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

	enum
	{
		TCLIENT_TAB_SETTINGS = 0,
		TCLIENT_TAB_BINDWHEEL,
		TCLIENT_TAB_WARLIST,
		TCLIENT_TAB_BINDCHAT,
		TCLIENT_TAB_STATUSBAR,
		NUMBER_OF_TCLIENT_TABS
	};

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
	// 卡片渲染期共用的字号与间距。卡片入口用 CTClientCardMetricsScope 绑定当前
	// SSettingsContentMetrics，退出时整份恢复：漏绑的卡只会拿到基础 token，不会
	// 沿用上一张卡的量；嵌套调用也安全。历史短名保留为引用，调用点无需改动，
	// 新增代码请直接用 Ctx.m_Metrics，不要再往这里加字段。
	struct STClientCardMetrics
	{
		float FontSize = ui_token::font::BODY;
		float EditBoxFontSize = ui_token::font::BODY;
		float LineSize = ui_token::settings::ROW_HEIGHT;
		float ColorPickerLineSize = ui_token::settings::ROW_HEIGHT + ui_token::settings::ROW_GAP;
		float HeadlineFontSize = ui_token::font::HEADLINE;
		float StandardFontSize = ui_token::font::BODY;
		// 卡片角标一类的小字：与设置页小字号度量同源，避免在业务页散落裸字号。
		float SmallFontSize = ui_token::font::SMALL;
		float HeadlineHeight = ui_token::font::HEADLINE;
		float RowSpacing = ui_token::settings::ROW_GAP;
		float MarginExtraSmall = ui_token::settings::ROW_GAP;
		float MarginBetweenSections = ui_token::settings::ROW_GAP * 2.0f;
		float ColorPickerLabelSize = ui_token::font::BODY;
		float ColorPickerLineSpacing = ui_token::settings::ROW_GAP;
	};

	inline STClientCardMetrics g_TClientCardMetrics{};
	inline int g_TClientCardMetricsDepth = 0;

	inline float &FontSize = g_TClientCardMetrics.FontSize;
	inline float &EditBoxFontSize = g_TClientCardMetrics.EditBoxFontSize;
	inline float &LineSize = g_TClientCardMetrics.LineSize;
	inline float &ColorPickerLineSize = g_TClientCardMetrics.ColorPickerLineSize;
	inline float &HeadlineFontSize = g_TClientCardMetrics.HeadlineFontSize;
	inline float &StandardFontSize = g_TClientCardMetrics.StandardFontSize;
	inline float &SmallFontSize = g_TClientCardMetrics.SmallFontSize;
	inline float &HeadlineHeight = g_TClientCardMetrics.HeadlineHeight;
	inline float &RowSpacing = g_TClientCardMetrics.RowSpacing;
	inline float &MarginExtraSmall = g_TClientCardMetrics.MarginExtraSmall;
	inline float &MarginBetweenSections = g_TClientCardMetrics.MarginBetweenSections;
	inline float &ColorPickerLabelSize = g_TClientCardMetrics.ColorPickerLabelSize;
	inline float &ColorPickerLineSpacing = g_TClientCardMetrics.ColorPickerLineSpacing;
	const float Margin = 10.0f;
	inline std::vector<CButtonContainer> s_vTinyTeeModeButtons = {{}, {}, {}};
	inline int s_CountFrozenText = 0;
	inline CUi::SDropDownState s_TrailDropDownState;
	inline CScrollRegion s_TrailDropDownScrollRegion;
	inline CUi::SDropDownState s_TrailStyleDropDownState;
	inline CScrollRegion s_TrailStyleDropDownScrollRegion;

	inline float TClientSettingsRowsHeight(const int NumRows)
	{
		return NumRows > 0 ? LineSize * NumRows + RowSpacing * (NumRows - 1) : 0.0f;
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
				m_Column.HSplitTop(RowSpacing, nullptr, &m_Column);
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

	inline STClientCardMetrics ResolveTClientCardMetrics(const SSettingsContentMetrics &Metrics)
	{
		STClientCardMetrics Result;
		Result.FontSize = Metrics.m_BodySize;
		Result.EditBoxFontSize = Metrics.m_BodySize;
		Result.LineSize = Metrics.m_LineHeight;
		Result.ColorPickerLineSize = Metrics.m_ButtonHeight;
		Result.HeadlineFontSize = Metrics.m_HeadlineSize;
		Result.StandardFontSize = Metrics.m_BodySize;
		Result.SmallFontSize = Metrics.m_SmallSize;
		Result.HeadlineHeight = Metrics.m_LineHeight;
		Result.RowSpacing = Metrics.m_LineSpacing;
		Result.MarginExtraSmall = Metrics.m_LineSpacing;
		Result.MarginBetweenSections = Metrics.m_SectionGap;
		Result.ColorPickerLabelSize = Metrics.m_BodySize;
		Result.ColorPickerLineSpacing = Metrics.m_LineSpacing;
		return Result;
	}

	// 卡片入口的生命周期绑定。整份保存/恢复，不逐个字段抄，避免漏一个字段就串味。
	class CTClientCardMetricsScope
	{
	public:
		explicit CTClientCardMetricsScope(const SSettingsContentMetrics &Metrics) :
			m_Previous(g_TClientCardMetrics),
			m_PreviousDepth(g_TClientCardMetricsDepth)
		{
			g_TClientCardMetrics = ResolveTClientCardMetrics(Metrics);
			g_TClientCardMetricsDepth = m_PreviousDepth + 1;
		}

		~CTClientCardMetricsScope()
		{
			g_TClientCardMetrics = m_Previous;
			g_TClientCardMetricsDepth = m_PreviousDepth;
		}

		CTClientCardMetricsScope(const CTClientCardMetricsScope &) = delete;
		CTClientCardMetricsScope &operator=(const CTClientCardMetricsScope &) = delete;

		// 排查用：当前是否有卡片在渲染（数量为 0 时读到的是基础 token）。
		static int Depth() { return g_TClientCardMetricsDepth; }

	private:
		STClientCardMetrics m_Previous;
		int m_PreviousDepth;
	};

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
