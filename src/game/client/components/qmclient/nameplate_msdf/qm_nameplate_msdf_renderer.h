// QmClient: 名牌专用 MSDF 文本渲染器（预烤字形图集）。
//
// 与全局文本渲染完全独立：不动 ITextRender / FreeType 位图字，HUD、菜单、聊天仍走原路径。
// 字形在离线阶段用 msdfgen 烤进图集（qmclient_scripts/qm_nameplate_msdf_build.py），
// 运行时只做「查表 + 一次性画四边形」，因此缩放与 HiDPI 下都不再有位图重采样发虚的问题。
//
// 覆盖策略：整名回退。名字里只要有一个字符不在图集内，SupportsText() 返回 false，
// 调用方应把整条名字交回原 FreeType 路径，避免同一名字混用两种清晰度。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_MSDF_QM_NAMEPLATE_MSDF_RENDERER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_MSDF_QM_NAMEPLATE_MSDF_RENDERER_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class IStorage;

struct SQmNameplateMsdfTextStyle
{
	ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	// 描边宽度（屏幕像素）
	float m_OutlineWidth = 1.0f;
	ColorRGBA m_OutlineColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	bool m_RainbowEnabled = false;
	float m_RainbowTime = 0.0f;
};

struct SQmNameplateMsdfPageInfo
{
	std::string m_ImageName;
	int m_Width = 0;
	int m_Height = 0;
};

class CQmNameplateMsdfRenderer
{
	struct SGlyph
	{
		int m_X = 0;
		int m_Y = 0;
		int m_W = 0;
		int m_H = 0;
		float m_Advance = 0.0f;
		float m_BearingX = 0.0f;
		float m_BearingY = 0.0f;
		int m_Page = -1;
		bool m_HasOutline = false;
		// 字形的水平推进（空白字形也有效），用于测量
		bool m_Valid = false;
	};

	struct SPage
	{
		IGraphics::CTextureHandle m_Texture;
		SQmNameplateMsdfPageInfo m_Info;
		float m_PxRange = 0.0f;
	};

	IStorage *m_pStorage = nullptr;
	IGraphics *m_pGraphics = nullptr;
	bool m_InitAttempted = false;
	bool m_Ready = false;
	// 图集缺失/损坏时的下次重试时间，避免每帧重复读盘
	int64_t m_NextInitAttempt = 0;
	// 后端不支持等硬性失败：不再重试（重试也不会成功）
	bool m_FatalError = false;
	std::string m_Error;

	std::vector<SPage> m_vPages;
	std::unordered_map<uint32_t, SGlyph> m_Glyphs;
	float m_RefEmPixels = 0.0f;

	bool LoadPage(const char *pManifestPath);
	bool ParseManifest(const char *pText, const std::string &ManifestPath);
	void UnloadPages();
	void EmitGlyphQuad(const SGlyph &Glyph, float X, float Y, float W, float H, const ColorRGBA &Color);

public:
	~CQmNameplateMsdfRenderer();

	bool Init(IStorage *pStorage, IGraphics *pGraphics);
	void Shutdown();
	// 供每帧调用：必要时初始化，失败则按退避重试（不阻塞渲染）
	void EnsureInitialized(IStorage *pStorage, IGraphics *pGraphics);
	bool IsReady() const { return m_Ready; }
	const char *Error() const { return m_Error.c_str(); }

	// 文本是否全部可由图集渲染（整名回退判定）
	bool SupportsText(const char *pText) const;
	// 图集内的字形数量与页数，供日志/自检
	size_t GlyphCount() const { return m_Glyphs.size(); }
	size_t PageCount() const { return m_vPages.size(); }

	// 按 FontSize 测量文本（宽, 高）；无法整体渲染时返回 (-1,-1)
	vec2 Measure(const char *pText, float FontSize) const;
	// 在 (X,Y) 左上角绘制，返回实际绘制尺寸
	vec2 Draw(const char *pText, float X, float Y, float FontSize, const SQmNameplateMsdfTextStyle &Style);
	// 以 (CenterX,CenterY) 为中心绘制
	vec2 DrawCentered(const char *pText, float CenterX, float CenterY, float FontSize, const SQmNameplateMsdfTextStyle &Style);
};

// 单例：由 nameplates 组件持有，避免每帧重建图集
CQmNameplateMsdfRenderer &QmNameplateMsdf();

#endif
