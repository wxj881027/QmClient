#ifndef GAME_CLIENT_QMUI_SETTINGSCARDDECK_H
#define GAME_CLIENT_QMUI_SETTINGSCARDDECK_H

#include "SettingsCard.h"
#include "SettingsCardDeckLogic.h"
#include "SettingsPageLayout.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class CScrollRegion;
struct CScrollRegionParams;
struct IUiContext;

struct SSettingsCardDefinition
{
	SSettingsCardSpec m_Spec;
	FSettingsCardMeasure m_Measure;
	FSettingsCardRender m_Render;
	FSettingsCardRenderMeasured m_RenderMeasured;
	FSettingsCardPreLayoutInput m_PreLayoutInput;
	FSettingsCardHasPendingPreLayoutInput m_HasPendingPreLayoutInput;
	FSettingsCardPreLayoutHeaderInput m_PreLayoutHeaderInput;
	FSettingsCardHeaderAction m_HeaderAction;
	std::function<bool()> m_IsCollapsed;
	std::function<bool()> m_IsVisible;
	// 控制其他卡片可见性的输入在最终布局前处理。
	bool m_VisibilityController = false;
	// 内容高度依赖配置或运行时状态时，每帧重新测量。
	bool m_MeasureEachFrame = false;
	// 非零版本变化时重新测量，用于避免空闲帧重复执行昂贵布局。
	uint64_t m_MeasureRevision = 0;
	bool m_RenderWhenClipped = false;
	FSettingsCardHeaderAction m_LeadingHeaderAction;
	float m_LeadingHeaderActionWidth = 0.0f;
};

struct SSettingsCardDeckInput
{
	float m_MouseX = 0.0f;
	float m_MouseY = 0.0f;
	bool m_MousePressed = false;
	bool m_MouseDown = false;
	bool m_MouseReleased = false;
	bool m_CtrlPressed = false;
	bool m_AllowHeaderDrag = true;
	float m_FrameDt = 1.0f / 60.0f;
	const CScrollRegionParams *m_pScrollParams = nullptr;
	SSettingsCardDeckFrameDiagnostics *m_pDiagnostics = nullptr;
};

// 输入快照可能采集于本帧弹层打开之前，Deck 在预布局前后都需重新隔离。
inline SSettingsCardDeckInput ResolveSettingsCardDeckPointerInput(SSettingsCardDeckInput Input, bool Blocked)
{
	if(Blocked)
	{
		Input.m_MousePressed = false;
		Input.m_MouseDown = false;
		Input.m_MouseReleased = false;
		Input.m_CtrlPressed = false;
		Input.m_AllowHeaderDrag = false;
	}
	return Input;
}

struct SSettingsCardDeckResult
{
	const char *m_pRevealedStableId = nullptr;
	float m_AutoScrollDelta = 0.0f;
	bool m_DropFeedbackConsumed = false;
	bool m_ReflowCompleteFeedbackConsumed = false;
	bool m_OrderChanged = false;
};

// 设置卡片的 client-only 协调器：唯一负责声明式 definition 的排序、拖拽、滚动与 canonical shell 接线。
class CSettingsCardDeck
{
public:
	SSettingsCardDeckResult Render(const IUiContext &Ctx, const SSettingsPageLayoutFrame &Layout, const char *pTab, const std::vector<SSettingsCardDefinition> &vCards, qm_card_order::CModel &Model, CScrollRegion *pScrollRegion, const SSettingsCardDeckInput &Input, const SCardMotionSpec &Motion, const SSettingsCardDeckVisualOptions &VisualOptions);
	template<typename FBuildDefinitions>
	SSettingsCardDeckResult RenderCached(const IUiContext &Ctx, const SSettingsPageLayoutFrame &Layout, const char *pTab, uint64_t DefinitionsRevision, FBuildDefinitions &&BuildDefinitions, qm_card_order::CModel &Model, CScrollRegion *pScrollRegion, const SSettingsCardDeckInput &Input, const SCardMotionSpec &Motion, const SSettingsCardDeckVisualOptions &VisualOptions)
	{
		if(SettingsCardDeckDefinitionsCacheKeyChanged(m_CachedDefinitionsInitialized, m_CachedDefinitionsRevision, DefinitionsRevision, m_CachedDefinitionsTab.c_str(), pTab))
		{
			m_vCachedDefinitions.clear();
			BuildDefinitions(m_vCachedDefinitions);
			m_CachedDefinitionsRevision = DefinitionsRevision;
			m_CachedDefinitionsTab = pTab != nullptr ? pTab : "";
			m_CachedDefinitionsInitialized = true;
			m_CachedDefinitionsDirty = true;
			m_InvalidateMeasurementsOnDefinitionsPrepare = true;
		}
		return RenderInternal(Ctx, Layout, pTab, m_vCachedDefinitions, Model, pScrollRegion, Input, Motion, VisualOptions, true);
	}
	void RequestReveal(const char *pStableId);
	void BeginDisplayCycle(uint64_t DisplayCycle, bool AnimateEntry);

private:
	struct SPreparedCard
	{
		const SSettingsCardDefinition *m_pDefinition = nullptr;
		int m_StateIndex = -1;
		int m_Column = 0;
		SSettingsCardFrame m_Frame;
		float m_TargetContentHeight = 0.0f;
		bool m_FirstLayout = false;
		bool m_ContentHeightAnimationActive = false;
	};

	struct SRuntimeState
	{
		float m_DropFeedbackRemaining = 0.0f;
		float m_LastReflowTargetY = 0.0f;
		float m_AnimatedContentHeight = 0.0f;
		float m_LastContentHeightTarget = 0.0f;
		float m_LastDrawOffsetX = 0.0f;
		float m_LastDrawOffsetY = 0.0f;
		bool m_ReflowInitialized = false;
		bool m_ReflowWasActive = false;
		bool m_ContentHeightInitialized = false;
		bool m_ContentHeightWasActive = false;
		bool m_CollapsedInitialized = false;
		bool m_LastCollapsed = false;
		bool m_DefaultCollapsed = false;
		char m_DefaultCollapseButtonId = 0;
		char m_WidthButtonId = 0;
		int m_RestoreColumn = -1;
		int m_RestoreOrder = -1;
		bool m_PointerInsideLastFrame = false;
	};

	struct SDragState
	{
		int m_StateIndex = -1;
		int m_SourceColumn = 1;
		int m_TargetColumn = 1;
		int m_TargetOrder = 0;
		float m_GrabOffsetX = 0.0f;
		float m_GrabOffsetY = 0.0f;

		bool Active() const { return m_StateIndex >= 0; }
		void Reset() { *this = {}; }
	};

	void PrepareDefinitions(const std::vector<SSettingsCardDefinition> &vCards, const qm_card_order::CModel &Model);
	void ResetDefinitionViewState();
	SSettingsCardDeckResult RenderInternal(const IUiContext &Ctx, const SSettingsPageLayoutFrame &Layout, const char *pTab, const std::vector<SSettingsCardDefinition> &vCards, qm_card_order::CModel &Model, CScrollRegion *pScrollRegion, const SSettingsCardDeckInput &Input, const SCardMotionSpec &Motion, const SSettingsCardDeckVisualOptions &VisualOptions, bool PersistentDefinitions);

	CSettingsCardDeckFrameRuntime m_FrameRuntime;
	bool m_SuppressHoverFeedbackOnce = false;
	bool m_HasPointerPosition = false;
	bool m_HasScrollOffset = false;
	float m_LastPointerX = 0.0f;
	float m_LastPointerY = 0.0f;
	float m_LastScrollOffsetY = 0.0f;
	float m_LastViewportHeight = -1.0f;
	std::string m_LastRenderedTab;
	std::string m_PendingRevealStableId;
	SDragState m_Drag;
	std::vector<SRuntimeState> m_vRuntimeStates;
	std::vector<float> m_vContentHeights;
	std::vector<float> m_vContentWidths;
	std::vector<uint64_t> m_vMeasureRevisions;
	std::vector<SSettingsCardDefinition> m_vCachedDefinitions;
	std::string m_CachedDefinitionsTab;
	uint64_t m_CachedDefinitionsRevision = 0;
	bool m_CachedDefinitionsInitialized = false;
	bool m_CachedDefinitionsDirty = false;
	bool m_InvalidateMeasurementsOnDefinitionsPrepare = false;
	int m_PreparedDefinitionModelCount = -1;
	uint64_t m_PreparedDefinitionStateIndexRevision = UINT64_MAX;
	const SSettingsCardDefinition *m_pPreparedDefinitionData = nullptr;
	size_t m_PreparedDefinitionCount = 0;
	std::string m_PreparedDefinitionTab;
	std::vector<const char *> m_vPreparedStableIds;
	std::vector<const SSettingsCardDefinition *> m_vDefinitionsByState;
	std::vector<int> m_vBoundDefinitionStateIndices;
	std::vector<int> m_vActiveStateIndices;
	std::vector<int> m_vPreviousActiveStateIndices;
	std::vector<int> m_vLastRenderedActiveStateIndices;
	std::vector<SPreparedCard> m_vPreparedCards;
	std::vector<SSettingsCardDeckItemGeometry> m_vDragGeometry;
	std::array<std::vector<int>, 3> m_aDragColumns;
	settings_card_deck_logic::CProjectionCache m_ProjectionCache;
	CSettingsCardDeckPackingPlan m_PackingPlan;
};

#endif // GAME_CLIENT_QMUI_SETTINGSCARDDECK_H
