#include "QmCardCatalog.h"

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

// 卡片目录与菜单内容渲染之间的受控桥接：卡片模块是独立文件，
// 这里把"卡片可以调用哪些渲染/输入助手"显式列出来（CMenus 只对本结构开放友元）。
bool qm_card_catalog::QmCardRenderHook::RenderQmFunctionCheckbox(CMenus *pMenus, const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, bool PrewarmOnly)
{
	return pMenus->RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, pRect, PrewarmOnly);
}

bool qm_card_catalog::QmCardRenderHook::RenderQmVisualCheckbox(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue)
{
	return pMenus->RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, pId, pTextId, pText, pValue);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualTranslateUiContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing)
{
	pMenus->RenderQmVisualTranslateUiContent(Content, LineHeight, BodySize, LineSpacing);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualStreamerContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing)
{
	pMenus->RenderQmVisualStreamerContent(Content, LineHeight, LineSpacing);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualEntityOverlayContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmVisualEntityOverlayContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualCollisionHitboxContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmVisualCollisionHitboxContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualWeaponAnimationContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly)
{
	pMenus->RenderQmVisualWeaponAnimationContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ContentGap, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualChatBubbleContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmVisualChatBubbleContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualCameraViewContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmVisualCameraViewContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualSkinAppearanceContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmVisualSkinAppearanceContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualSkinTransitionContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmVisualSkinTransitionContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionGoresActorContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionGoresActorContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionGoresContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionGoresContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmVisualFocusModeContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth)
{
	pMenus->RenderQmVisualFocusModeContent(Content, LineHeight, BodySize, LineSpacing, ColumnGap, LabelWidth);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionSoloSplitContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionSoloSplitContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionKeyBindsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	pMenus->RenderQmFunctionKeyBindsContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionEmoticonsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	pMenus->RenderQmFunctionEmoticonsContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionMiniFeaturesContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionMiniFeaturesContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionJumpHintContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionJumpHintContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionWeaponTrajectoryContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionWeaponTrajectoryContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionFriendNotifyContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionFriendNotifyContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionBlockWordsContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionBlockWordsContent(Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionTranslateContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionTranslateContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionKeywordReplyContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionKeywordReplyContent(Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionPieMenuContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CardCornerRadius, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionPieMenuContent(Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardPadding, CardCornerRadius, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionMapUploadContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionMapUploadContent(Content, LineHeight, BodySize, LineSpacing, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionFavoriteMapsContent(CMenus *pMenus, CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionFavoriteMapsContent(Content, UiScale, LineHeight, BodySize, LineSpacing, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmFunctionHJAssistContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmFunctionHJAssistContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudDummyMiniViewContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool Expanded, bool PrewarmOnly)
{
	pMenus->RenderQmHudDummyMiniViewContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, Expanded, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudCoordsContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudCoordsContent(Content, Metrics, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudPlayerStatsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudPlayerStatsContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudDebugGraphContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudDebugGraphContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudDebugModeContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudDebugModeContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudInputOverlayContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudInputOverlayContent(Content, Metrics, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudNotificationsBasicContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudNotificationsBasicContent(Content, Metrics, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudNotificationsAdvancedContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudNotificationsAdvancedContent(Content, Metrics, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudVoiceContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudVoiceContent(Content, Metrics, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudDynamicIslandContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle)
{
	pMenus->RenderQmHudDynamicIslandContent(Content, LineHeight, LineSpacing, OriginalStyle);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudSystemMediaControlsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	pMenus->RenderQmHudSystemMediaControlsContent(Content, LineHeight, BodySize, LineSpacing, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudLyricsContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly)
{
	pMenus->RenderQmHudLyricsContent(Content, LineHeight, LineSpacing, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudBackground3DContent(CMenus *pMenus, CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudBackground3DContent(Content, Metrics, LabelWidth, PrewarmOnly);
}

void qm_card_catalog::QmCardRenderHook::RenderQmHudBindStatusContent(CMenus *pMenus, CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	pMenus->RenderQmHudBindStatusContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
}

bool qm_card_catalog::QmCardRenderHook::HandleQmHudCheckboxInput(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue)
{
	return pMenus->HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, pId, pValue);
}

bool qm_card_catalog::QmCardRenderHook::ToggleQmHudCountdownLocation(CMenus *pMenus, CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue)
{
	return pMenus->ToggleQmHudCountdownLocation(Content, LineHeight, LineSpacing, pId, pValue);
}

bool qm_card_catalog::QmCardRenderHook::DoButtonLogic(CMenus *pMenus, const void *pId, int Checked, const CUIRect *pRect, int Flags)
{
	return pMenus->Ui()->DoButtonLogic(pId, Checked, pRect, Flags);
}

void qm_card_catalog::QmCardRenderHook::DoSettingsLabel(CMenus *pMenus, const char *pTextId, const char *pText, const CUIRect *pRect, float Size, int Align)
{
	pMenus->DoSettingsLabelStreamed(pMenus->SettingsTextElement(CMenus::SETTINGS_SEARCH, -1, pTextId), pRect, pText, Size, Align);
}

ITextRender *qm_card_catalog::QmCardRenderHook::TextRenderer(CMenus *pMenus)
{
	return pMenus->TextRender();
}

size_t qm_card_catalog::QmCardRenderHook::FavoriteMapCount(CMenus *pMenus)
{
	return pMenus != nullptr ? pMenus->GameClient()->TClientComponent().GetFavoriteMaps().size() : 0;
}

bool qm_card_catalog::QmCardRenderHook::GameConsoleActive(CMenus *pMenus)
{
	return pMenus != nullptr && pMenus->GameClient()->m_GameConsole.IsActive();
}
