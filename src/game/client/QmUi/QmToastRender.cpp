#include "QmToast.h"
#include "UiSurface.h"
#include "UiTokens.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/ui.h>

#include <algorithm>

namespace qm_ui
{
	namespace
	{
		constexpr float TOAST_HEIGHT = 30.0f;
		constexpr float TOAST_PADDING_X = 12.0f;
		constexpr float TOAST_GAP = 6.0f;
		constexpr float TOAST_BOTTOM_MARGIN = 24.0f;
		constexpr float TOAST_MIN_WIDTH = 180.0f;
		constexpr float TOAST_MAX_WIDTH = 420.0f;
		constexpr float TOAST_ACTION_HEIGHT = 22.0f;
		constexpr float TOAST_LEADING_BAR = 3.0f;

		ColorRGBA ToastAccent(EQmToastKind Kind)
		{
			switch(Kind)
			{
			case EQmToastKind::SUCCESS: return ui_token::color::SUCCESS;
			case EQmToastKind::WARNING: return ui_token::color::WARNING;
			case EQmToastKind::FAILED: return ui_token::color::DANGER;
			case EQmToastKind::INFO:
			default: return ui_token::color::ACCENT_PRIMARY;
			}
		}
	} // namespace

	void RenderToasts(CUi *pUi)
	{
		if(pUi == nullptr)
			return;
		const CUIRect Screen = *pUi->Screen();
		CToastQueue &Queue = Toasts();
		const double Now = time_get();
		Queue.Expire(Now);
		if(Queue.Size() == 0)
			return;

		const int MotionLevel = g_Config.m_QmUiMotionLevel;
		// 底边留出安全距离：toast 自下而上堆叠，最新的在最下面。
		float Bottom = Screen.y + Screen.h - TOAST_BOTTOM_MARGIN;
		CUiScopedGaussianBlurSuppression BlurSuppression(pUi);
		ITextRender *pTextRender = pUi->TextRender();

		// 从新到旧绘制：最新一条贴底，较早的向上堆叠。
		for(size_t Index = Queue.Size(); Index-- > 0;)
		{
			CToastQueue::SEntry &Entry = Queue.At(Index);
			const float Alpha = Queue.EntryAlpha(Entry, Now, MotionLevel);
			if(Alpha <= 0.01f)
			{
				// 退场结束但还没被清理：跳过绘制，等下一帧 Expire。
				Bottom -= TOAST_HEIGHT + TOAST_GAP;
				continue;
			}

			const float TextWidth = pTextRender->TextWidth(ui_token::font::BODY, Entry.m_Text.c_str(), -1, -1.0f);
			const float ActionWidth = Entry.m_ActionLabel.empty() ? 0.0f : pTextRender->TextWidth(ui_token::font::BODY, Entry.m_ActionLabel.c_str(), -1, -1.0f) + 16.0f;
			const float Width = std::clamp(TextWidth + ActionWidth + 2.0f * TOAST_PADDING_X + TOAST_LEADING_BAR + 10.0f, TOAST_MIN_WIDTH, TOAST_MAX_WIDTH);

			CUIRect Rect;
			Rect.w = Width;
			Rect.h = TOAST_HEIGHT;
			Rect.x = Screen.x + (Screen.w - Width) * 0.5f;
			Rect.y = Bottom - TOAST_HEIGHT + Queue.EntryOffset(Entry, Now, MotionLevel);

			ColorRGBA Surface = ui_token::color::SURFACE_ELEVATED;
			Surface.a *= Alpha;
			ColorRGBA Border = ui_token::color::BORDER_SUBTLE;
			Border.a *= Alpha;
			DrawRoundedSurface(pUi, Rect, Surface, Border, ui_token::radius::BASE);

			CUIRect Inner = Rect;
			Inner.Margin(TOAST_PADDING_X, &Inner);

			// 左侧语义色条：颜色只是辅助，文案本身已经说明成功/失败。
			CUIRect Bar;
			Bar.x = Rect.x + 1.0f;
			Bar.y = Rect.y + 6.0f;
			Bar.w = TOAST_LEADING_BAR;
			Bar.h = Rect.h - 12.0f;
			ColorRGBA Accent = ToastAccent(Entry.m_Kind);
			Accent.a *= Alpha;
			DrawRoundedSurface(pUi, Bar, Accent, ColorRGBA(), 1.5f);

			CUIRect Label = Inner;
			Label.x += TOAST_LEADING_BAR + 6.0f;
			Label.w -= TOAST_LEADING_BAR + 6.0f;
			if(ActionWidth > 0.0f)
			{
				CUIRect ActionRect;
				Label.VSplitRight(ActionWidth, &Label, &ActionRect);
				ActionRect.y = Rect.y + (TOAST_HEIGHT - TOAST_ACTION_HEIGHT) * 0.5f;
				ActionRect.h = TOAST_ACTION_HEIGHT;

				const ColorRGBA PreviousColor = pTextRender->GetTextColor();
				pTextRender->TextColor(ui_token::color::ACCENT_PRIMARY.WithAlpha(Alpha));
				pUi->DoLabel(&ActionRect, Entry.m_ActionLabel.c_str(), ui_token::font::BODY, TEXTALIGN_MC);
				pTextRender->TextColor(PreviousColor);
				// 只有动作按钮参与命中测试，toast 本体永不接管输入。
				const bool Clicked = pUi->DoButtonLogic(&Entry.m_ButtonToken, 0, &ActionRect, BUTTONFLAG_LEFT) != 0;
				if(Clicked)
				{
					std::function<void()> Action = Entry.m_Action;
					Queue.Erase(Index);
					if(Action)
						Action();
					return;
				}
			}

			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, Label.w);
			Props.m_EllipsisAtEnd = true;
			const ColorRGBA PreviousColor = pTextRender->GetTextColor();
			pTextRender->TextColor(ui_token::color::TEXT_PRIMARY.WithAlpha(Alpha));
			pUi->DoLabel(&Label, Entry.m_Text.c_str(), ui_token::font::BODY, TEXTALIGN_ML, Props);
			pTextRender->TextColor(PreviousColor);

			Bottom -= TOAST_HEIGHT + TOAST_GAP;
		}
	}
} // namespace qm_ui
