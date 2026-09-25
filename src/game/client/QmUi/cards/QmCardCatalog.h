#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOG_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOG_H

#include <game/client/QmUi/QmModuleTypes.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/ui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CMenus;

// 全局卡片目录：每张设置卡片的构造入口（标题/测量/重测版本/预布局输入/内容渲染）
// 都由卡片模块文件自己提供，页面只声明"这个页面有哪些卡片"。
// 这样卡片可以在任意页面（如搜索页）以完整、可交互的形态复用，
// 而不是在页面里复制一份渲染代码或退化成跳转链接。
namespace qm_card_catalog
{
	// 栖梦侧栏三个分类各自拥有的卡片（stableId 清单，与 QmCardRegistry 的 Placement 表同源）。
	const std::vector<const char *> &VisualCardStableIds();
	const std::vector<const char *> &FunctionCardStableIds();
	const std::vector<const char *> &HudCardStableIds();

	// 该 stableId 是否有可构造的卡片模块（用于搜索页过滤与页面自检）。
	bool HasCardModule(const char *pStableId);
	// 全目录卡片的重测版本聚合（任一卡的内容高度依赖项变化即失效），页面把它折进 DefinitionsRevision。
	uint64_t MeasureContentRevision();

	// 函数分类卡片的布局版本状态：由菜单层每帧刷新（它持有这些缓存与计数），
	// 卡片模块只读取，保证卡片的测量逻辑不再散落在页面函数里。
	struct SQmFunctionCardLayoutState
	{
		uint64_t m_BlockWordsRevision = 1;
		uint64_t m_KeywordRulesRevision = 1;
		size_t m_KeywordRulesCount = 0;
		bool m_KeywordRulesHalfFilled = false;
		uint64_t m_FavoriteMapsRevision = 1;
		size_t m_FavoriteMapSearchRows = 1;
	};

	// 卡片构造上下文：页面把自己的 UI 尺度、布局帧、折叠状态与持久化回调注入进来。
	// 卡片模块只依赖本结构，不依赖任何页面函数局部状态。
	struct SQmCardBuildContext
	{
		CMenus *m_pMenus = nullptr;
		bool m_ReadOnly = false;
		SSettingsPageLayoutFrame m_Page{};
		SSettingsContentMetrics m_Metrics{};
		float m_LabelWidth = 0.0f;
		IUiContext m_UiContext{};
		// 卡片内容基座（QmSettingsCardStyle）：卡片模块的渲染参数不依赖页面类型。
		float m_Padding = 14.0f;
		float m_CornerRadius = 10.0f;
		// 折叠状态按 EQmModuleId 索引，页面持有（同一份状态被所有页面与搜索页共享）。
		bool *m_pCollapsed = nullptr;
		CButtonContainer *m_pCollapseButtons = nullptr;
		void (*m_pToggleCollapsed)(void *pUser, qm_module::EQmModuleId Id) = nullptr;
		void *m_pToggleCollapsedUser = nullptr;
		// 卡片从折叠切回展开时通知页面让测量缓存失效（例如关键词回复需要重新同步编辑行）。
		void (*m_pOnCardExpanded)(void *pUser, qm_module::EQmModuleId Id) = nullptr;
		void *m_pOnCardExpandedUser = nullptr;
		const SQmFunctionCardLayoutState *m_pFunctionLayout = nullptr;
	};

	// 按 stableId 构造一张卡片 definition；未注册（无卡片模块）时返回 false。
	// 页面只做"取清单 → 逐张构造 → 交给 CSettingsCardDeck"。
	bool BuildCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out);
	void BuildCards(const SQmCardBuildContext &Ctx, const std::vector<const char *> &vStableIds, std::vector<SSettingsCardDefinition> &vOut);

	// 搜索页用的卡片目录条目：stableId + 该卡当前所在分类（导航目标），
	// 由 QmCardRegistry 的默认 Placement 表与当前布局模型共同解析。
	struct SQmSearchResultEntry
	{
		const char *m_pStableId = nullptr;
		const char *m_pTab = nullptr;
		std::string m_Title;
		std::string m_Description;
	};
	std::vector<SQmSearchResultEntry> SearchResultEntries(const char *pQuery, const qm_card_order::CModel &Model);
	// 搜索页卡片清单（含每张卡的当前稳定顺序与列），可直接喂给 qm_card_order::CModel::SetEntries。
	std::vector<qm_card_order::SEntry> BuildSearchModelEntries(const std::vector<SQmSearchResultEntry> &vResults);

	// 三个分类各自的卡片模块实现（供 BuildCard 分派，通常不需要直接调用）。
	bool BuildVisualCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);
	bool BuildFunctionCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);
	bool BuildHudCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);
	bool BuildSteamCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);

	// 卡片模块调用菜单内容渲染/输入助手的受控入口（CMenus 只对本结构开放友元）。
	// 卡片模块是独立文件，不能直接触达 CMenus 的私有内容函数；
	// 这里显式列出“卡片可以调用哪些渲染/输入助手”，避免把整类成员公开出去。
	struct QmCardRenderHook
	{
		static bool RenderQmFunctionCheckbox(CMenus *pMenus, const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, bool PrewarmOnly);
		static bool RenderQmVisualCheckbox(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue);
		static void RenderQmVisualTranslateUiContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing);
		static void RenderQmVisualStreamerContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing);
		static void RenderQmVisualEntityOverlayContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmVisualCollisionHitboxContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmVisualWeaponAnimationContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly);
		static void RenderQmVisualChatBubbleContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmVisualCameraViewContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmVisualSkinAppearanceContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmVisualSkinTransitionContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionGoresActorContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionGoresContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionKeyBindsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth);
		static void RenderQmFunctionEmoticonsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth);
		// 本地差异：远程的这两个入口已把 BodySize/LabelWidth 从参数表里去掉，本地既有的
		// CMenus::RenderQmFunctionMiniFeaturesContent / RenderQmHudBindStatusContent 仍需要
		// 它们来排版，故桥接按**本地签名**保留并透传这两个参数（以本地实现为准，不改本地行为）。
		static void RenderQmFunctionMiniFeaturesContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionJumpHintContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionWeaponTrajectoryContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionFriendNotifyContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionBlockWordsContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionTranslateContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionKeywordReplyContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmFunctionPieMenuContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CardCornerRadius, bool PrewarmOnly);
		static void RenderQmFunctionMapUploadContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly);
		static void RenderQmFunctionFavoriteMapsContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly);
		static void RenderQmFunctionHJAssistContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudDummyMiniViewContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool Expanded, bool PrewarmOnly);
		static void RenderQmHudCoordsContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudPlayerStatsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudDebugGraphContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudDebugModeContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudInputOverlayContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudNotificationsBasicContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudNotificationsAdvancedContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudVoiceContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudDynamicIslandContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle);
		static void RenderQmHudSystemMediaControlsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly);
		static void RenderQmHudLyricsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly);
		static void RenderQmHudBackground3DContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly);
		static void RenderQmHudBindStatusContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		static bool HandleQmHudCheckboxInput(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue);
		static bool ToggleQmHudCountdownLocation(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue);
		// 本地专属卡片的渲染入口（远程目录不含这两张：禅模式 / 单机分割）。
		// 这两张卡在本地是既有能力，吸收远程目录时必须一并模块化，否则切换 BuildCards 后会从 UI 消失。
		static void RenderQmVisualFocusModeContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth);
		static void RenderQmFunctionSoloSplitContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly);
		// CComponent 的保护成员（菜单本身是组件）：卡片模块经桥接取用，不直接调用。
		static bool DoButtonLogic(CMenus *pMenus, const void *pId, int Checked, const CUIRect *pRect, int Flags = 0);
		// 设置页统一文案渲染（streamed 托管，卡片目录与分类页标签同源）。
		static void DoSettingsLabel(CMenus *pMenus, const char *pTextId, const char *pText, const CUIRect *pRect, float Size, int Align);
		static ITextRender *TextRenderer(CMenus *pMenus);
		static size_t FavoriteMapCount(CMenus *pMenus);
		static bool GameConsoleActive(CMenus *pMenus);
	};
} // namespace qm_card_catalog

#endif // GAME_CLIENT_QMUI_CARDS_QMCARDCATALOG_H
