// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_RENDER_H

#include <base/mem.h>
#include <base/str.h>
#include <base/vmath.h>

#include <game/client/components/qmclient/qm_title_style.h>

#include <array>
#include <string>

class CTextCursor;
class ITextRender;

// 只缓存字符前缀的布局宽度，颜色、掠光与浮动仍按当前帧计算。
class CQmTitleTextMetrics
{
public:
	struct SContext
	{
		float m_FontSize = 0.0f;
		vec2 m_ScreenScale = vec2(1.0f, 1.0f);
		unsigned m_RenderFlags = 0;
		int m_FontPreset = 0;

		bool operator==(const SContext &Other) const
		{
			return m_FontSize == Other.m_FontSize && m_ScreenScale == Other.m_ScreenScale &&
			       m_RenderFlags == Other.m_RenderFlags && m_FontPreset == Other.m_FontPreset;
		}
	};

	void Reset() { m_Valid = false; }

	// Measure 接收完整前缀，保留字体字距与首尾字形规则；字体资源失效时调用 Reset。
	template<typename F>
	void Update(const char *pText, const SContext &Context, F &&Measure)
	{
		if(m_Valid && m_Context == Context && m_Text == pText)
			return;
		m_Context = Context;
		m_Text = pText;
		m_aWidths.fill(0.0f);
		const char *pCurrent = pText;
		while(*pCurrent != '\0')
		{
			if(str_utf8_decode(&pCurrent) <= 0)
				break;
			const int PrefixBytes = (int)(pCurrent - pText);
			// 与既有 64 字节前缀缓冲的截断边界一致。
			if(PrefixBytes >= (int)m_aWidths.size())
				break;
			char aPrefix[64];
			mem_copy(aPrefix, pText, PrefixBytes);
			aPrefix[PrefixBytes] = '\0';
			m_aWidths[PrefixBytes] = Measure(aPrefix);
		}
		m_Valid = true;
	}

	float PrefixWidth(int PrefixBytes, float LeftX) const
	{
		return PrefixBytes < (int)m_aWidths.size() ? m_aWidths[PrefixBytes] : LeftX;
	}

private:
	bool m_Valid = false;
	SContext m_Context;
	std::string m_Text;
	std::array<float, 64> m_aWidths = {};
};

// 头衔动态渲染参数：颜色风格 + 可选的逐字符上下波动或放大凸起。
struct SQmTitleRenderStyle
{
	const SQmTitleStyle *m_pStyle = nullptr; // nullptr 表示不启用动态风格
	SQmTitleBobStyle m_Bob = {}; // 幅度为 0 表示不浮动
	EQmTitleInterpolation m_Interpolation = EQmTitleInterpolation::Smooth;
	// 逐字符相位系数覆盖（每像素）。0 表示沿用风格自带值（多数风格为 0，即整行同步变色）。
	float m_PhasePerPxOverride = 0.0f;
	// true 表示调用方的配色档（单色/彩虹）优先，风格只保留浮动与掠光等动效，不再提供颜色。
	// 配色优先级：本地配色档 > 服务端/本地风格的颜色；两者同时存在时以本字段为准。
	bool m_ColorOverride = false;
};

// 逐字符掠光（镜面扫过）。与逐字符浮动的区别在于：浮动改顶点位置，掠光只改顶点颜色，
// 因此不需要额外预留边界，也不会让字形离开基线。
//
// 相位 = 字符序号 / 字符总数 + 时间 / 周期，因此无论头衔是两个字符还是十二个字符，
// 都是一道从左掠到右的光带，且扫过整行的耗时固定（不随长度变慢）。
struct SQmTitleShimmer
{
	bool m_Enabled = false;
	float m_Speed = 100.0f; // 1/100 行每秒：100 表示每秒扫过一整行
	float m_DutyCycle = 0.35f; // 掠光高光占行宽的比例
	float m_Amount = 0.32f; // 高光峰值：向白色的插值比例
};

// 解析某个玩家头衔应当使用的动态风格。pServerStyleId 由服务端下发，优先于本地配置；
// 为空或未知 id 时回退到本地配置，本地也未启用则返回 m_pStyle == nullptr。
SQmTitleRenderStyle QmTitleResolveRenderStyle(const char *pServerStyleId);

// 同上，但由调用方显式给出本地兜底（设置页预览用）。预览必须与实际名牌走同一条解析路径，
// 否则会出现「预览一种颜色、游戏里另一种颜色」。
SQmTitleRenderStyle QmTitleResolveRenderStyle(const char *pServerStyleId, bool LocalStyleEnabled, const char *pLocalStyleId);

// 把动态风格写入文本光标：逐字符色段（含字符内渐变所需的右边缘色）与逐字符浮动偏移。
//
// pText 必须是头衔本体（不含外层方括号），FontSize 与该次绘制一致。
// 逐字符相位按「字符左边缘相对行首的累计像素宽度」计算，与 Calamity 的 pos.X 语义一致，
// 因此中英文混排时波纹与颜色带的间距仍然均匀。
//
// pMetrics 为可选的前缀宽度缓存，必须对应当前文本、字号与字体/屏幕映射。
//
// Shimmer 为可选参数：启用时把掠光的高光叠加到逐字符色段上（只改颜色，不改布局）。
//
// Style.m_ColorOverride 为 true 时本函数不采样风格颜色，改用 Color / ColorEnd 的线性渐变
// （彩虹档传逐字符彩虹色，单色档传同一个颜色）；浮动、掠光与插值方式仍然照常生效。
//
// 返回 true 表示内容随时间变化，调用方需要按帧重建文本容器；false 表示静态，只需建一次。
// 与 QmAddTitleRainbowSplits 一样，色段使用字节偏移作为字符序号（与引擎的 m_CharCount 语义一致）。
bool QmTitleRenderFillCursor(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, float FontSize, const SQmTitleRenderStyle &Style, float TimeSec, float Alpha, const SQmTitleShimmer &Shimmer = {}, const CQmTitleTextMetrics *pMetrics = nullptr, const ColorRGBA &Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), const ColorRGBA &ColorEnd = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

// 只写逐字符位移与缩放，不写任何色段：配色交给调用方自己的档位（单色走文本色，彩虹走 QmAddTitleRainbowSplits）。
// 风格颜色的优先级低于本地配色档，此时仍要保留浮动与掠光，所以不能整条跳过风格。
void QmTitleRenderFillMotionOffsets(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, float FontSize, const SQmTitleRenderStyle &Style, float TimeSec, const SQmTitleShimmer &Shimmer = {}, const CQmTitleTextMetrics *pMetrics = nullptr);

// 求某个字符在掠光周期里的高光强度，取值 [0, 1]。CharIndex 与 CharCount 为该次绘制内的字符序号与总数。
// 抽成独立函数是为了让「一个周期内恰好扫过一次、且整行亮度守恒」这类性质可以直接测试。
float QmTitleShimmerFactor(const SQmTitleShimmer &Shimmer, int CharIndex, int CharCount, float TimeSec);

// 掠光条带按 UTF-8 字符计数分配相位：长头衔不能因为字节数多就扫得更慢。
int QmTitleShimmerUtf8CharCount(const char *pText);

// 按当前配置构造掠光参数。名牌与设置页预览共用同一份规则，避免「预览有掠光、实际没有」。
SQmTitleShimmer QmTitleShimmerFromConfig();

#endif
