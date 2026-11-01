#ifndef GAME_CLIENT_QMUI_SETTINGSCARDCONTENTBRIDGE_H
#define GAME_CLIENT_QMUI_SETTINGSCARDCONTENTBRIDGE_H

#include "SettingsCardDeck.h"

#include <engine/textrender.h>

#include <game/client/ui.h>

#include <optional>

namespace qm_settings_content
{
	// 只读绘制的状态随栈退出；仅在内容完整返回后发布消费高度。
	template<typename TDraw>
	inline void RenderLayoutContent(const CSettingsContentLayout &Layout, CUIRect &Content, CUi *pUi, const TDraw &Draw)
	{
		const CUIRect Origin = Content;
		{
			std::optional<CUiScopedBackgroundRender> Guard;
			if(pUi != nullptr)
				Guard.emplace(pUi, true);
			Draw(Layout, Origin);
		}
		Content.y = Origin.y + Layout.Height();
		Content.h = Origin.h - Layout.Height();
	}

	class CContentDeclarationUiGuard
	{
		CUi *m_pUi;

	public:
		explicit CContentDeclarationUiGuard(CUi *pUi) : m_pUi(pUi)
		{
			if(m_pUi)
				m_pUi->BeginRenderOnly();
		}
		~CContentDeclarationUiGuard()
		{
			if(m_pUi)
				m_pUi->EndRenderOnly();
		}
	};

	// 普通卡片与模块卡片共用显式声明桥，弹层结果的 pending 信号由来源保留。
	inline void UseDeclaredContent(SSettingsCardDefinition &Out, CUi *pUi, ITextRender *pText)
	{
		const auto Content = Out.m_RenderMeasured;
		Out.m_ResolveContentLayout = [Content, pText, pUi](CSettingsContentLayoutCache &Cache, float Width) {
			std::vector<SSettingsContentRowDeclaration> vRows;
			{
				CContentDeclarationUiGuard Guard(pUi);
				CSettingsContentLayoutScope Scope(vRows);
				CUIRect Rect{0, 0, Width, 0};
				Content(Rect);
			}
			return Cache.Resolve(vRows, Width, pText ? pText->GlyphAtlasRevision() : 0, [pText](const char *pValue, float TextWidth, float FontSize) { return pText ? pText->TextBoundingBox(FontSize, pValue, -1, TextWidth).m_H + FontSize * 0.25f : FontSize; }, [pText](const SSettingsContentRowDeclaration &Row, float TextWidth) {
			const char *pValue = Row.m_Text.empty() ? " " : Row.m_Text.c_str();
			return pText ? pText->TextBoundingBox(Row.m_FontSize, pValue, -1, std::max(1.0f, TextWidth - 2.0f * Row.m_TextPadding), Row.m_TextLineSpacing).m_H + 2.0f * Row.m_TextPadding : Row.m_MinimumHeight; });
		};
		Out.m_ContentLayoutInput = [Content](const CSettingsContentLayout &Layout, CUIRect Rect) {
			CSettingsContentLayoutScope Scope(Layout, ESettingsContentPass::INPUT);
			Content(Rect);
		};
		Out.m_ContentLayoutDraw = [Content, pUi](const CSettingsContentLayout &Layout, CUIRect Rect) {
			std::optional<CUiScopedBackgroundRender> Guard;
			if(pUi)
				Guard.emplace(pUi, true);
			CSettingsContentLayoutScope Scope(Layout, ESettingsContentPass::DRAW);
			Content(Rect);
		};
		Out.m_Measure = {};
		Out.m_MeasureRevision = 0;
		Out.m_MeasureEachFrame = false;
		// 全部控件在同一个 INPUT 回放中提交，禁止旧的手工行号回调再消费一次。
		Out.m_PreLayoutInput = {};
		// 保留弹层待提交信号；源卡片滚出视口后仍由同一个 INPUT 回放消费。
	}

	// 预览、网格与二维编辑器发布专用布局的真实块高度，保留其内部坐标与原尺寸。
	// MeasureBlock 必须只计算几何，不能通过执行控件回调探测高度。
	inline void UseDeclaredBlockContent(SSettingsCardDefinition &Out, CUi *pUi, ITextRender *pText, FSettingsCardMeasure MeasureBlock)
	{
		const auto Render = Out.m_Render;
		const auto Input = Out.m_PreLayoutInput;
		const std::string Id = Out.m_Spec.m_pStableId;
		Out.m_RenderMeasured = [Render, Input, MeasureBlock = std::move(MeasureBlock), Id](CUIRect &Content) {
			CSettingsContentLayoutFlow Flow(Content);
			CUIRect Block;
			if(!Flow.Control(Id.c_str(), MeasureBlock(Content.w), Block))
				return;
			const bool ProcessInput = Flow.Input();
			CSettingsContentLayoutScope InternalGeometry;
			if(ProcessInput && Input)
				Input(Block);
			if(Render)
				Render(Block);
		};
		UseDeclaredContent(Out, pUi, pText);
	}

	// Deck 的离屏延续入口：弹层提交即使没有鼠标输入也必须先消费，再发布最终布局。
	inline bool ExecuteInput(SSettingsCardDefinition const &Definition, std::shared_ptr<const CSettingsContentLayout> &pLayout, CSettingsContentLayoutCache &Cache, CUIRect Content, bool Visible, bool ActiveContinuation, bool Collapsed, CUi *pUi)
	{
		const bool Pending = (Definition.m_HasPendingPreLayoutInput && Definition.m_HasPendingPreLayoutInput()) || (pUi && pUi->HasPendingInteractionSelection());
		if(Collapsed || !pLayout || !Definition.m_ContentLayoutInput || !Definition.m_ResolveContentLayout ||
			(!Visible && !ActiveContinuation && !Pending) || (pUi && pUi->RenderOnly()))
			return false;
		const auto pPreviousLayout = pLayout;
		return ExecuteSettingsContentInputThenResolve(pPreviousLayout, Content, Definition.m_ContentLayoutInput,
			[&](float Width) {
				pLayout = Definition.m_ResolveContentLayout(Cache, Width);
				return pLayout;
			});
	}
}

#endif
