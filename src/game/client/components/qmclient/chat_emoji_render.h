#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_EMOJI_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_EMOJI_RENDER_H

#include <engine/graphics.h>

#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <cmath>

// 检查完成后才开始批次；设备异常仍传播，不假定失败批次可以继续绘制。
template<typename TGraphics>
bool QmRenderChatEmoji(TGraphics &Graphics, IGraphics::CTextureHandle Texture, float X, float Y, float Width, float Height, float Alpha)
{
	if(!Texture.IsValid() || !std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Width) || !std::isfinite(Height) ||
		!std::isfinite(Alpha) || Width <= 0.0f || Height <= 0.0f || Alpha <= 0.0f)
		return false;
	IGraphics::CQuadItem Quad(X + Width * 0.5f, Y + Height * 0.5f, Width, Height);
	// QuadsDraw先从中心减半幅，再由左上角加整幅生成顶点；两步都必须有限。
	const float Left = Quad.m_X - Width / 2.0f;
	const float Top = Quad.m_Y - Height / 2.0f;
	if(!std::isfinite(Quad.m_X) || !std::isfinite(Quad.m_Y) || !std::isfinite(Left) || !std::isfinite(Top) ||
		!std::isfinite(Left + Width) || !std::isfinite(Top + Height))
		return false;

	Graphics.TextureSet(Texture);
	Graphics.QuadsBegin();
	Graphics.QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	Graphics.QuadsSetRotation(0.0f);
	Graphics.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, std::clamp(Alpha, 0.0f, 1.0f)));
	Graphics.QuadsDraw(&Quad, 1);
	Graphics.QuadsEnd();
	Graphics.TextureClear();
	Graphics.SetColor(ui_token::color::NEUTRAL_WHITE);
	return true;
}

#endif
