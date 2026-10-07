#include "chat_emoji.h"

#include <base/log.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

void CQmChatEmoji::EnsureTextureLoaded(EQmChatEmoji Emoji) const
{
	if(!QmChatEmojiIsKnown(Emoji))
		return;
	const std::size_t TextureIndex = static_cast<std::size_t>(Emoji) - 1;
	if(!QmChatEmojiShouldLoadTexture(Emoji, m_aLoadAttempted[TextureIndex]))
		return;

	// 首次遇到时排队；同时只保留一张正在解码或等待上传的原图。
	m_aLoadAttempted[TextureIndex] = true;
	m_LoadQueue.push_back(Emoji);
	StartNextLoad();
}

void CQmChatEmoji::StartNextLoad() const
{
	if(m_pLoadJob || m_LoadQueue.empty())
		return;
	m_LoadingEmoji = m_LoadQueue.front();
	m_LoadQueue.pop_front();
	const std::string Path = QmChatEmojiTexturePath(m_LoadingEmoji);
	m_pLoadJob = std::make_shared<CQmChatEmojiLoadJob>([pStorage = Storage(), Path](CImageInfo &Image) {
		void *pData = nullptr;
		unsigned Size = 0;
		if(!pStorage->ReadFile(Path.c_str(), IStorage::TYPE_ALL, &pData, &Size))
		{
			log_error("chat_emoji", "Failed to read '%s'", Path.c_str());
			return;
		}
		// 随包表情统一为无损 WebP，避免先尝试 PNG 解码产生错误日志。
		const bool Loaded = CImageLoader::LoadWebP(pData, Size, Path.c_str(), Image);
		free(pData);
		if(Loaded)
			ConvertToRgba(Image);
		// ConvertToRgba 返回的是原格式是否已为 RGBA，而非转换成功标记。
		if(!Loaded || Image.m_Format != CImageInfo::FORMAT_RGBA)
			Image.Free();
	});
	Engine()->AddJob(m_pLoadJob);
}

void CQmChatEmoji::OnUpdate()
{
	if(!m_pLoadJob)
		return;
	CImageInfo *pImage = m_pLoadJob->Image();
	if(!pImage)
		return;
	if(pImage->m_pData)
	{
		auto *pLimiter = GameClient()->GpuUploadLimiter();
		if(!pLimiter->CanUpload())
			return;
		const size_t Index = static_cast<size_t>(m_LoadingEmoji) - 1;
		m_aTextures[Index] = Graphics()->LoadTextureRawMove(*pImage, 0, QmChatEmojiTexturePath(m_LoadingEmoji));
		pLimiter->OnUploaded();
	}
	m_pLoadJob.reset();
	m_LoadingEmoji = EQmChatEmoji::NONE;
	StartNextLoad();
}

void CQmChatEmoji::OnShutdown()
{
	m_pLoadJob.reset();
	m_LoadQueue.clear();
	m_LoadingEmoji = EQmChatEmoji::NONE;
	for(IGraphics::CTextureHandle &Texture : m_aTextures)
	{
		if(Texture.IsValid())
			Graphics()->UnloadTexture(&Texture);
	}
	m_aLoadAttempted.fill(false);
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
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}
