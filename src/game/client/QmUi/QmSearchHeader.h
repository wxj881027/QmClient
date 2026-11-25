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

	inline constexpr const char *SPLASHES[] = {
		"今天打算恰多少分呢?",
		"栖梦客户端,努力打造最适合中国Tee质的客户端",
		"你的意思是你玩了这么久技术也学不好bind也学不会只会swap和恰吗",
		"如果你觉得好用,把QmClient也捎给你的朋友吧!",
		"BiliBili关注贝塔塔塔塔塔喵,关注贝塔塔塔塔塔谢谢喵",
		"I'm gonna pack my things and leave you behind This feeling's old, and I know that I've made up my mind I hope you feel what I felt when you shattered my soul 'Cause you were cruel, and I'm a fool, so please, let me go",
		"三万分大佬手动跑图,能量微小;一百分萌新自动上分,能量巨大",
		"今天也要对新手温柔一点.....你玩蛇图?!.....还泡在PVP区域?!.......滚出去!",
		"I Like This 👉 QmClient 👈",
		"栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️栖梦❤️",
		"Why not try BestClient😊",
		"你说得对,但是....\"说白了做个自瞄又怎么样收你们钱了么?散发铜臭味的客户端天天在串什么?不输个账号密码都进不去的客户端在叫什么?ddnet开发者都没赚到钱给他赚上了\"",
		"Why not try TClient? Actually...QmClient is based on TClient XD",
		"■■■■■■■■■■■■■■[数据删除]■■■■■■■■■■■■■■",
		"为啥要恰分，跑到大半死了想恰分可以理解，图都没跑甚至自己根本跑不过的，恰了有啥意义，分高了水平也就那样,别人一查分以为大佬，一起玩一看菜鸡，别人一看图过了时间很快，一起玩发现啥都不会，现在分高的没啥含金量了，碰到太多恰上去的",
		"这qm不用睡觉吗?怎么老是在我骂他的时候就在线",
		"你知道的，栖梦是我的兄弟，我们曾并肩作战击败过许多对手。在QmClientV2.97.33的至暗时刻，我也是栖梦的忠实用户，我从来没有说过优化的嘲讽以及群内开串的举动。QmClient简直强得不可思议，我认为我们能够产生很棒的化学反应。有时候作出决定很简单，经过许多个日夜的思考，我决定把天赋带给栖梦。我已经迫不及待使用QmClient了。至于NMClient、BestClient和TaterClient ，我祝他们一切顺利",
		"(请输入文本)",
		"(你倒是输啊)",
		"写不完的 Feature，修不完的 Bug，睡不够的觉",
		"QmClient：从入门到放弃，再从放弃到真香",
		"没有什么是重启客户端解决不了的，如果有，那就重装（划掉）",
		"虽然但是，我们的拖尾是真的帅吧？",
		"你好，我是秦始皇，其实我并没有死，现在在 DDNet 里打钱，V我50带你恰分",
		"我测你们码（指正在测试你的客户端代码）",
		"前面忘了，中间忘了，总之 QmClient 启动！",
		"这里本来应该有一句很帅的标语，但栖梦写到一半困了",
		"我承认阁下的实力确实很强，但如果你用的是 QmClient，阁下又该如何应对？",
		"你说的对，但是 QmClient 是由栖梦自主研发的一款全新开源 DDNet 客户端。游戏发生在一个被称作「Teeworlds」的幻想世界……",
		"₵Ⱨ₳₮! ₵Ⱨ₳₮! 刚才那下绝对是服务器丢包！DÜD！我明明按了空格的！",
		"犹豫就会败北，果断就会白给",
	};
	inline constexpr int NUM_SPLASHES = sizeof(SPLASHES) / sizeof(SPLASHES[0]);

	inline constexpr const char *SEARCH_PLACEHOLDERS[] = {
		"截图保存在哪里",
		"灵动岛歌词",
		"碰撞箱",
		"武器弹道辅助线",
		"八角定位",
		"计分板怎么呼出鼠标",
		"快速练习",
		"怎么更换客户端字体",
		"聊天翻译",
		"怎么给皮肤调色",
		"钩子倒计时",
		"绑定轮盘",
		"鼠标灵敏度",
		"旁观模式按键",
		"怎么防止误按自杀键",
		"录制回放",
		"聊天记录在哪里",
		"聊天气泡",
		"语音没有声音怎么办",
		"客户端ID",
	};
	inline constexpr int NUM_SEARCH_PLACEHOLDERS = sizeof(SEARCH_PLACEHOLDERS) / sizeof(SEARCH_PLACEHOLDERS[0]);

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
		int m_PlaceholderIndex = -1;
		bool m_Entered = false;

		static int PickDifferentIndex(int Choice, int ChoiceCount, int PreviousIndex)
		{
			const int Index = std::clamp(Choice, 0, ChoiceCount - 1);
			return PreviousIndex >= 0 && Index >= PreviousIndex ? Index + 1 : Index;
		}

	public:
		bool NeedsEntry(uint64_t DisplayCycle) const { return !m_Entered || m_DisplayCycle != DisplayCycle; }
		int ChoiceCount() const { return m_Index < 0 ? NUM_SPLASHES : NUM_SPLASHES - 1; }
		int PlaceholderChoiceCount() const { return m_PlaceholderIndex < 0 ? NUM_SEARCH_PLACEHOLDERS : NUM_SEARCH_PLACEHOLDERS - 1; }
		void Enter(uint64_t DisplayCycle, int Choice, int LogoChoice, int PlaceholderChoice = 0)
		{
			if(!NeedsEntry(DisplayCycle))
				return;
			// 略过上次的标语和搜索词，其余候选仍可等概率选中。
			m_Index = PickDifferentIndex(Choice, ChoiceCount(), m_Index);
			m_LogoIndex = std::clamp(LogoChoice, 0, NUM_LOGOS - 1);
			m_PlaceholderIndex = PickDifferentIndex(PlaceholderChoice, PlaceholderChoiceCount(), m_PlaceholderIndex);
			m_DisplayCycle = DisplayCycle;
			m_Entered = true;
		}
		void Leave() { m_Entered = false; }
		const char *Text() const { return SPLASHES[std::max(0, m_Index)]; }
		int LogoIndex() const { return m_LogoIndex; }
		const char *Placeholder() const { return SEARCH_PLACEHOLDERS[std::max(0, m_PlaceholderIndex)]; }
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
		const float SingleLineHeight = 22.0f * UiScale;
		const float SubtitleHeight = std::max(SingleLineHeight, TextHeight);
		const float LogoBudget = std::max(0.0f, Available - SubtitleHeight - 2.0f * Gap);
		// 极矮视口优先保留输入框。
		if(LogoBudget < 20.0f * UiScale || Viewport.w <= 0.0f)
			return Result;
		// 按单行文案确定常规图高，换行时让输入框为新增的行腾出空间。
		const float LogoCenterY = Viewport.h * 0.25f;
		const float CenteredLogoBudget = 2.0f * std::min(LogoCenterY, Result.m_InputOffsetY - LogoCenterY - SingleLineHeight - 2.0f * Gap);
		const bool UseAnchors = CenteredLogoBudget >= 20.0f * UiScale;
		const float LogoHeightBudget = UseAnchors ? std::min(LogoBudget, CenteredLogoBudget) : LogoBudget;
		const float LogoAspect = s_aLogos[Result.m_LogoIndex].Aspect();
		const float LogoWidth = std::min({340.0f * UiScale, Viewport.w * 0.8f, LogoHeightBudget * LogoAspect});
		const float LogoHeight = LogoWidth / LogoAspect;
		const float HeaderHeight = LogoHeight + SubtitleHeight + 2.0f * Gap;
		// 输入框到达底边后，标题组向上让位。
		const float FreeHeight = std::max(0.0f, Available - HeaderHeight);
		const float Top = UseAnchors ? std::min(LogoCenterY - LogoHeight * 0.5f, FreeHeight) : FreeHeight * 0.5f;
		Result.m_Logo = {Viewport.x + (Viewport.w - LogoWidth) * 0.5f, Viewport.y + Top, LogoWidth, LogoHeight};
		const float TextWidth = SubtitleWidth(Viewport);
		Result.m_Subtitle = {Viewport.x + (Viewport.w - TextWidth) * 0.5f, Result.m_Logo.y + LogoHeight + Gap, TextWidth, SubtitleHeight};
		Result.m_InputOffsetY = std::max(Result.m_InputOffsetY, Top + HeaderHeight);
		return Result;
	}

	void Render(CUi *pUi, IGraphics *pGraphics, ITextRender *pTextRender, const SLayout &Layout, const char *pText, float UiScale, float OffsetY);

}

#endif
