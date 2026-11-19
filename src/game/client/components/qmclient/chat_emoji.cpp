#include "chat_emoji.h"

#include "chat_emoji_render.h"

#include <engine/engine.h>

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
	if(!CanRender(Emoji))
		return;

	const std::size_t TextureIndex = static_cast<std::size_t>(Emoji) - 1;
	QmRenderChatEmoji(*Graphics(), m_aTextures[TextureIndex], X, Y, Width, Height, Alpha);
}
