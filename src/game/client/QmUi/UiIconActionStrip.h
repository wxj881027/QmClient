// 右对齐动作图标条：卡片/列表行里「并排的功能图标」只按**本帧真的会画**的动作从右到左
// 排列，隐藏的动作不占位；出现与消失走同一条缓动（宽度 + 透明度），因此右侧文字可用宽度
// 也连续变化。
//
// 公共约定（全仓并排功能图标一律走这里，不要再手写 VSplitRight 固定槽）：
//   1. **不存在 = 不占位**：不要为「暂时不显示的按钮」预留槽位，那会在图标之间留下
//      莫名空隙（实机反馈：加号与收藏星之间空一格，因为给减号留了位置）。
//   2. **出现/消失必须过渡**：宽度与透明度同一条缓动，不允许瞬切（图标会「跳」）。
//   3. **文本不许画进图标区**：消费方（名称/标题标签）必须用 ReserveWidth() 反算可用宽度，
//      而不是拿整行宽度去排版（实机反馈：皮肤名跑到收藏星下面）。
//   4. 只在交互趟推进状态机（与全仓本地时钟动画同一条规则）：出图/测量趟只读，
//      否则动画会在背景趟跑完、交互趟直接看到终点。
#ifndef GAME_CLIENT_QMUI_UIICONACTIONSTRIP_H
#define GAME_CLIENT_QMUI_UIICONACTIONSTRIP_H

#include <base/system.h>
#include <base/vmath.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace ui_widget
{
	// 单个动作的存在度动画：0 = 完全不存在（不占位、不接收点击），1 = 完全存在。
	struct SIconActionPresence
	{
		float m_From = 0.0f;
		float m_To = 0.0f;
		int64_t m_StartTick = 0;

		float Current(int64_t Now, int64_t Freq, float EaseSeconds) const
		{
			if(m_From == m_To || EaseSeconds <= 0.0f || Freq <= 0)
				return m_To;
			const float Progress = std::clamp((float)(Now - m_StartTick) / (float)Freq / EaseSeconds, 0.0f, 1.0f);
			const float Eased = Progress * Progress * (3.0f - 2.0f * Progress);
			return m_From + (m_To - m_From) * Eased;
		}
	};

	// 固定容量的图标条状态（容量在编译期定死，避免逐帧分配）。
	// 用法：每帧 Update 一次存在位掩码 → 用 Layout/ReserveWidth 排版 → 各自画自己的按钮。
	template<size_t MaxActions>
	class CIconActionStrip
	{
	public:
		// Present 的位 i 表示第 i 个动作本帧存在。interactive=false（出图/测量趟）只读，
		// 不推进状态机。MotionEnabled=false（动效等级 0）时直接落到目标值。
		void Update(const int64_t Now, const int64_t Freq, const bool MotionEnabled, const uint32_t Present, const float EaseSeconds, const bool Interactive)
		{
			for(size_t i = 0; i < MaxActions; ++i)
			{
				const float Target = (Present >> i) & 1u ? 1.0f : 0.0f;
				SIconActionPresence &Presence = m_aPresence[i];
				if(!Interactive)
					continue; // 非交互趟不写状态机，只读当前值
				if(!MotionEnabled)
				{
					Presence.m_From = Presence.m_To = Target;
					continue;
				}
				if(Presence.m_To == Target)
					continue;
				Presence.m_From = Presence.Current(Now, Freq, EaseSeconds);
				Presence.m_To = Target;
				Presence.m_StartTick = Now;
			}
		}

		// 第 Index 个动作当前的存在度（0..1）。宽度、透明度、命中都按它缩放。
		float Presence(const size_t Index, const int64_t Now, const int64_t Freq, const float EaseSeconds) const
		{
			if(Index >= MaxActions)
				return 0.0f;
			return std::clamp(m_aPresence[Index].Current(Now, Freq, EaseSeconds), 0.0f, 1.0f);
		}

		// 从右到左布局：Index 越大越靠右（最后的动作贴右缘）。
		// 每格宽 = IconSize × Presence，格间缝 = Gap × Presence，因此隐藏的动作宽度为 0。
		// pRects 按 Index 写入，未存在的动作得到 w=0 的退化矩形（调用点据 Presence 跳过绘制）。
		void Layout(const CUIRect &Row, const float IconSize, const float Gap, const int64_t Now, const int64_t Freq, const float EaseSeconds, CUIRect *pRects) const
		{
			float Cursor = Row.x + Row.w;
			const float CenterY = Row.y + Row.h * 0.5f;
			for(size_t Reverse = 0; Reverse < MaxActions; ++Reverse)
			{
				const size_t Index = MaxActions - 1 - Reverse;
				const float Alpha = Presence(Index, Now, Freq, EaseSeconds);
				const float Width = IconSize * Alpha;
				const float Height = std::min(Row.h, IconSize) * std::max(Alpha, 0.0f);
				Cursor -= Width;
				pRects[Index] = CUIRect{Cursor, CenterY - Height * 0.5f, Width, Height};
				Cursor -= Gap * Alpha;
			}
		}

		// 图标条当前占用的总宽度（含格间缝）。文字可用宽度 = 行宽 − ReserveWidth() − 文本缝。
		float ReserveWidth(const float IconSize, const float Gap, const int64_t Now, const int64_t Freq, const float EaseSeconds) const
		{
			float Total = 0.0f;
			for(size_t i = 0; i < MaxActions; ++i)
			{
				const float Alpha = Presence(i, Now, Freq, EaseSeconds);
				if(Alpha <= 0.0f)
					continue;
				Total += IconSize * Alpha + Gap * Alpha;
			}
			return Total;
		}

		void Reset()
		{
			for(auto &Presence : m_aPresence)
				Presence = SIconActionPresence{};
		}

	private:
		std::array<SIconActionPresence, MaxActions> m_aPresence{};
	};
} // namespace ui_widget

#endif
