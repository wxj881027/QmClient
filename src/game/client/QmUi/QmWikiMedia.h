// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMWIKIMEDIA_H
#define GAME_CLIENT_QMUI_QMWIKIMEDIA_H

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

#include <string>
#include <unordered_map>

class IStorage;

// Wiki 图片与动图。静态图直接加载 png/webp；动图由构建脚本把
// mp4/avif/webp/gif 转成“雪碧图 + 元数据”，客户端只播放雪碧图，
// 因此不需要在运行时引入视频解码器。
struct SWikiMedia
{
	IGraphics::CTextureHandle m_Texture;
	int m_Width = 0;
	int m_Height = 0;
	int m_Cols = 1;
	int m_Rows = 1;
	int m_Frames = 1;
	float m_Fps = 0.0f;
	bool m_Valid = false;
};

namespace qm_wiki_media
{
	void Init(IStorage *pStorage, IGraphics *pGraphics);
	// 丢弃已加载的图片缓存（不主动卸载贴图句柄：图形设备重建后仍需要重启客户端）。
	// 目前没有调用点——面板只在打开时 Init 一次；将来接语言切换时在这里补卸载。
	void Clear();
	// pPath 相对 wiki/media/；未找到或解码失败返回 nullptr。
	const SWikiMedia *Get(const char *pPath);
	bool Exists(const char *pPath);
	// 动图按全局时间循环播放，静态图固定在首帧。
	void Draw(const SWikiMedia &Media, const CUIRect &Rect, ColorRGBA Color);
	float Aspect(const SWikiMedia &Media);
} // namespace qm_wiki_media

#endif
