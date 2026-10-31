#include "chat_emoji.h"

#include <engine/engine.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/gameclient.h>

void CQmChatEmoji::EnsureTextureLoaded(EQmChatEmoji Emoji) const
{
	if(!QmChatEmojiIsKnown(Emoji))
		return;
	if(m_LoadState.Request(Emoji))
		StartNextLoad();
}

void CQmChatEmoji::StartNextLoad() const
{
	m_LoadState.TryStart([&](EQmChatEmoji Emoji) { return QmCreateChatEmojiLoadJob(Storage(), Emoji); },
		[&](const std::shared_ptr<CQmChatEmojiLoadJob> &pJob) { return Engine() && Engine()->TryAddJob(pJob); });
}

void CQmChatEmoji::OnUpdate()
{
	m_LoadState.Poll([&] { return GameClient()->GpuUploadLimiter()->CanUpload(); },
		[&](CImageInfo &Image, EQmChatEmoji Emoji) {
			const auto Texture = Graphics()->LoadTextureRawMove(Image, 0, QmChatEmojiTexturePath(Emoji));
			if(!Texture.IsValid())
				return false;
			m_aTextures[static_cast<size_t>(Emoji) - 1] = Texture;
			GameClient()->GpuUploadLimiter()->OnUploaded();
			return true;
		});
	StartNextLoad();
}

void CQmChatEmoji::OnShutdown()
{
	m_LoadState.Clear();
	for(IGraphics::CTextureHandle &Texture : m_aTextures)
	{
		if(Texture.IsValid())
			Graphics()->UnloadTexture(&Texture);
	}
}

bool CQmChatEmoji::CanRender(EQmChatEmoji Emoji) const
{
	if(!QmChatEmojiIsKnown(Emoji))
		return false;
	EnsureTextureLoaded(Emoji);
	const std::size_t TextureIndex = static_cast<std::size_t>(Emoji) - 1;
	return QmChatEmojiShouldRenderImage(Emoji, m_aTextures[TextureIndex].IsValid());
}

void CQmChatEmoji::Render(EQmChatEmoji Emoji, float X, float Y, float Width, float Height, float Alpha) const
{
	if(!CanRender(Emoji) || Width <= 0.0f || Height <= 0.0f || Alpha <= 0.0f)
		return;

	const std::size_t TextureIndex = static_cast<std::size_t>(Emoji) - 1;
	Graphics()->TextureSet(m_aTextures[TextureIndex]);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, std::clamp(Alpha, 0.0f, 1.0f)));
	IGraphics::CQuadItem Quad(X + Width * 0.5f, Y + Height * 0.5f, Width, Height);
	Graphics()->QuadsDraw(&Quad, 1);
	Graphics()->QuadsEnd();
	Graphics()->TextureClear();
	Graphics()->SetColor(ui_token::color::NEUTRAL_WHITE);
}
