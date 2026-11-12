#ifndef GAME_CLIENT_QMUI_QMSEARCHHEADER_H
#define GAME_CLIENT_QMUI_QMSEARCHHEADER_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>

class CUi;
class IGraphics;
class ITextRender;

namespace qm_search_header
{

	// 中文彩蛋是作者文案，保留 Tee 谐音与客户端名称，不作为界面标签翻译。
	inline constexpr const char *SPLASHES[] = {
		"今天打算跑什么图呢?",
		"栖梦客户端,努力打造最适合中国Tee质的客户端",
		"再试一次，这次一定能过！",
		"终点不急，先把队友接上。",
		"钩住的是队友，也是默契。",
		"今天的第一锤，留给最好的搭档。",
		"跑累了就歇一会儿，地图又不会跑。",
		"别急，好钩不怕晚。",
		"这一跳，献给还没放弃的你。",
		"过图记得合影！",
		"有时候，等等队友也是一种进步。",
		"小小的 Tee，大大的梦想。",
		"栖一会儿，梦里也能过图。",
		"前方有冻，身后有朋友。",
		"今天也要对新手温柔一点。",
		"向 DDNet 和 Teeworlds 的开拓者致敬。",
		"谢谢 TaterClient，土豆前辈！",
		"向仙人掌客户端致敬，带刺也温柔。",
		"也试试 BestClient！",
		"也试试 NaiMeiClient！",
		"也试试 TaterClient！",
		"也试试仙人掌客户端！",
		"Also try Terraria!",
		"Also try Minecraft!",
	};
	inline constexpr int NUM_SPLASHES = sizeof(SPLASHES) / sizeof(SPLASHES[0]);

	struct SLogo
	{
		int m_Left;
		int m_Top;
		int m_Right;
		int m_Bottom;
		float Aspect() const { return (float)(m_Right - m_Left) / (m_Bottom - m_Top); }
	};

	// 三张原图均为 2048×800；固定有效区域与渲染侧的资源列表一一对应。
	inline constexpr SLogo s_aLogos[] = {
		{204, 161, 1709, 658},
		{204, 84, 1709, 718},
		{392, 227, 1677, 658},
	};
	inline constexpr int NUM_LOGOS = sizeof(s_aLogos) / sizeof(s_aLogos[0]);

	class CSession
	{
		uint64_t m_DisplayCycle = 0;
		int m_Index = -1;
		int m_LogoIndex = 0;
		bool m_Entered = false;

	public:
		bool NeedsEntry(uint64_t DisplayCycle) const { return !m_Entered || m_DisplayCycle != DisplayCycle; }
		int ChoiceCount() const { return m_Index < 0 ? NUM_SPLASHES : NUM_SPLASHES - 1; }
		void Enter(uint64_t DisplayCycle, int Choice, int LogoChoice)
		{
			if(!NeedsEntry(DisplayCycle))
				return;
			// 在其余文案中等概率选择，避免连续两次进入仍显示同一句。
			int Index = std::clamp(Choice, 0, ChoiceCount() - 1);
			if(m_Index >= 0 && Index >= m_Index)
				++Index;
			m_Index = Index;
			m_LogoIndex = std::clamp(LogoChoice, 0, NUM_LOGOS - 1);
			m_DisplayCycle = DisplayCycle;
			m_Entered = true;
		}
		void Leave() { m_Entered = false; }
		const char *Text() const { return SPLASHES[std::max(0, m_Index)]; }
		int LogoIndex() const { return m_LogoIndex; }
	};

	struct SLayout
	{
		CUIRect m_Logo{};
		CUIRect m_Subtitle{};
		float m_InputOffsetY = 0.0f;
		int m_LogoIndex = 0;
	};

	inline float SubtitleWidth(const CUIRect &Viewport)
	{
		return std::max(0.0f, Viewport.w * 0.7f);
	}

	inline SLayout Layout(const CUIRect &Viewport, float InputHeight, float UiScale, bool HasQuery, int LogoIndex, float TextHeight = 0.0f)
	{
		SLayout Result;
		Result.m_LogoIndex = std::clamp(LogoIndex, 0, NUM_LOGOS - 1);
		if(HasQuery)
			return Result;
		const float Available = std::max(0.0f, Viewport.h - InputHeight);
		Result.m_InputOffsetY = Available * 0.5f;
		const float Gap = 12.0f * UiScale;
		const float SubtitleHeight = std::max(22.0f * UiScale, TextHeight);
		const float LogoBudget = std::max(0.0f, Available - SubtitleHeight - 2.0f * Gap);
		// 极矮视口优先保留输入框。
		if(LogoBudget < 20.0f * UiScale || Viewport.w <= 0.0f)
			return Result;
		// 图片中心位于视口的 1/4，输入卡片中心位于 1/2；短句与间距预留在两者之间。
		const float LogoCenterY = Viewport.h * 0.25f;
		const float CenteredLogoBudget = 2.0f * std::min(LogoCenterY, Result.m_InputOffsetY - LogoCenterY - SubtitleHeight - 2.0f * Gap);
		const bool UseAnchors = CenteredLogoBudget >= 20.0f * UiScale;
		const float LogoHeightBudget = UseAnchors ? std::min(LogoBudget, CenteredLogoBudget) : LogoBudget;
		const float LogoAspect = s_aLogos[Result.m_LogoIndex].Aspect();
		const float LogoWidth = std::min({340.0f * UiScale, Viewport.w * 0.8f, LogoHeightBudget * LogoAspect});
		const float LogoHeight = LogoWidth / LogoAspect;
		const float HeaderHeight = LogoHeight + SubtitleHeight + 2.0f * Gap;
		// 高度不足时沿用整组居中的紧凑布局，避免图片、短句和输入卡片重叠。
		const float Top = UseAnchors ? LogoCenterY - LogoHeight * 0.5f : (Available - HeaderHeight) * 0.5f;
		Result.m_Logo = {Viewport.x + (Viewport.w - LogoWidth) * 0.5f, Viewport.y + Top, LogoWidth, LogoHeight};
		const float TextWidth = SubtitleWidth(Viewport);
		Result.m_Subtitle = {Viewport.x + (Viewport.w - TextWidth) * 0.5f, Result.m_Logo.y + LogoHeight + Gap, TextWidth, SubtitleHeight};
		if(!UseAnchors)
			Result.m_InputOffsetY = Top + HeaderHeight;
		return Result;
	}

	void Render(CUi *pUi, IGraphics *pGraphics, ITextRender *pTextRender, const SLayout &Layout, const char *pText, float UiScale, float OffsetY);

}

#endif
