#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_RENDER_H

#include <base/color.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/textrender.h>

#include <game/client/QmUi/UiTextRenderState.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/localization.h>

#include <new>
#include <stdexcept>

namespace QmCheckpointGuide
{
	// 标签直接使用真实文字接口；资源不足只跳过本帧，调用方颜色始终随作用域恢复。
	inline bool RenderLabels(ITextRender &TextRender, vec2 Position, vec2 Target, int Checkpoint, bool Warning, ColorRGBA Color)
	{
		CUiScopedTextRenderState TextState(TextRender);
		try
		{
			TextRender.TextColor(Color);
			TextRender.TextOutlineColor(ui_token::color::NEUTRAL_BLACK.WithAlpha(0.8f));
			char aText[256];
			str_format(aText, sizeof(aText), Localize("CP %d"), Checkpoint);
			TextRender.Text(Target.x - TextRender.TextWidth(14.0f, aText) / 2.0f, Target.y - 38.0f, 14.0f, aText);
			if(Warning)
			{
				str_format(aText, sizeof(aText), Localize("Collect CP %d before teleporting"), Checkpoint);
				TextRender.Text(Position.x - TextRender.TextWidth(16.0f, aText) / 2.0f, Position.y - 66.0f, 16.0f, aText);
			}
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_RENDER_H
