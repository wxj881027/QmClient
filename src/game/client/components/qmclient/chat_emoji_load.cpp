#include "chat_emoji.h"

#include <base/log.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/storage.h>

#include <cstdlib>

void QmLoadChatEmojiImage(IStorage *pStorage, const char *pPath, CImageInfo &Image)
{
	if(!pStorage || !pPath)
		return;
	// 在ReadFile写出指针前就持有释放职责，读取或解码异常都不会漏掉缓冲。
	struct SFileData
	{
		void *m_pData = nullptr;
		SFileData() = default;
		SFileData(const SFileData &) = delete;
		SFileData &operator=(const SFileData &) = delete;
		~SFileData() { free(m_pData); }
	} File;
	unsigned Size = 0;
	if(!pStorage->ReadFile(pPath, IStorage::TYPE_ALL, &File.m_pData, &Size))
	{
		log_error("chat_emoji", "Failed to read '%s'", pPath);
		return;
	}
	const bool Loaded = CImageLoader::LoadWebP(File.m_pData, Size, pPath, Image);
	if(Loaded)
		ConvertToRgba(Image);
	if(!Loaded || Image.m_Format != CImageInfo::FORMAT_RGBA)
		Image.Free();
}

std::shared_ptr<CQmChatEmojiLoadJob> QmCreateChatEmojiLoadJob(IStorage *pStorage, EQmChatEmoji Emoji)
{
	if(!pStorage || !QmChatEmojiIsKnown(Emoji))
		return nullptr;
	// 定义表中的路径具有静态生命周期，worker不借用组件或临时字符串。
	const char *pPath = QmChatEmojiTexturePath(Emoji);
	return std::make_shared<CQmChatEmojiLoadJob>([pStorage, pPath](CImageInfo &Image) {
		QmLoadChatEmojiImage(pStorage, pPath, Image);
	});
}
