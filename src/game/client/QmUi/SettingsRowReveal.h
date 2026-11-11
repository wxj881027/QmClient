#ifndef GAME_CLIENT_QMUI_SETTINGSROWREVEAL_H
#define GAME_CLIENT_QMUI_SETTINGSROWREVEAL_H

#include "QmListEntryAnimation.h"
#include "SettingsContentLayout.h"

#include <game/client/ui.h>

#include <algorithm>
#include <optional>

// 开关驱动的条件行（打开开关后才出现的配置行）的显隐动效。
// 布局高度立刻给足，容器高度交给卡片自己的高度动画；这里只记“出现/消失”的
// 经过时间，淡入与上浮曲线直接复用列表行入口，不另造一套时长与曲线。
// 一帧里卡片内容会跑多趟（DECLARE 量高度 / INPUT 收命中 / DRAW 绘制），
// 进度只按帧推进一次，否则动画会按趟数成倍加速。没有布局作用域时（自绘卡片）
// 本来就一帧一趟，照常推进。
inline bool QmSettingsRevealShouldAdvance()
{
	const CSettingsContentLayoutScope *pScope = CSettingsContentLayoutScope::Current();
	// 用 DECLARE 趟推进：量高度在每帧最先跑，进度和高度同帧生效，点击后不会
	// “内容先动、行晚一帧让位”。DRAW 趟只读，避免一帧推进多次。
	return pScope == nullptr || pScope->m_Pass == ESettingsContentPass::DECLARE;
}

// 交互显隐的时长与曲线：时长读 ui_token::motion::INTERACTIVE_REVEAL_*，
// 曲线用 ease-out cubic，第一帧就能看见位移，不会「点了先愣一下」。
inline float QmInteractiveRevealSeconds(const int MotionLevel)
{
	if(MotionLevel <= 0)
		return 0.0f;
	return MotionLevel == 1 ? ui_token::motion::INTERACTIVE_REVEAL_SEC_REDUCED : ui_token::motion::INTERACTIVE_REVEAL_SEC;
}

inline float QmInteractiveRevealProgress(const float ElapsedSeconds, const int MotionLevel)
{
	const float Duration = QmInteractiveRevealSeconds(MotionLevel);
	if(Duration <= 0.0f)
		return 1.0f;
	const float T = std::clamp(ElapsedSeconds / Duration, 0.0f, 1.0f);
	const float Remaining = 1.0f - T;
	return 1.0f - Remaining * Remaining * Remaining;
}

class CSettingsRowReveal
{
public:
	// Visible = 本帧这一行该不该出现。返回本帧是否仍要声明/绘制：
	// 消失动画播完之前继续占位，否则会先塌掉高度、再看到内容凭空消失。
	bool Update(const bool Visible, const float DeltaTime, const int MotionLevel)
	{
		const float Dt = QmSettingsRevealShouldAdvance() ? std::max(0.0f, DeltaTime) : 0.0f;
		if(Visible)
		{
			m_Visible = true;
			m_Elapsed = std::min(m_Elapsed + Dt, QmInteractiveRevealSeconds(MotionLevel));
		}
		else if(m_Visible)
		{
			m_Elapsed -= Dt;
			if(m_Elapsed <= 0.0f)
			{
				m_Elapsed = 0.0f;
				m_Visible = false;
			}
		}
		return m_Visible;
	}

	SQmListEntryVisual Visual(const int MotionLevel) const
	{
		// 块显隐不叠上浮位移：高度本身在长/缩，再位移会看着发跳。
		return {QmInteractiveRevealProgress(m_Elapsed, MotionLevel), 0.0f};
	}

	// 直接就位：读档即展开的区块不该在打开设置页时补播一次入场。
	void SnapVisible(const float ElapsedSeconds)
	{
		m_Visible = true;
		m_Elapsed = std::max(0.0f, ElapsedSeconds);
	}

private:
	bool m_Visible = false;
	float m_Elapsed = 0.0f;
};

// 折叠区/条件区块的显隐作用域：构造时把块内声明的行按进度压高度、并整体套一层
// 透明度，析构自动收尾。调用方只需要「不可见就 return」+ 一行作用域声明。
// 进度由 CSettingsRowReveal 提供，区块自身的尺寸变化交给卡片高度动画。
class CSettingsContentRevealScope
{
public:
	CSettingsContentRevealScope(CUi *pUi, CSettingsContentLayoutFlow &Flow, const float Progress)
	{
		m_pFlow = &Flow;
		m_Progress = std::clamp(Progress, 0.0f, 1.0f);
		m_pFlow->BeginReveal(m_Progress);
		if(pUi != nullptr && m_Progress < 0.999f)
			m_Alpha.emplace(pUi, m_Progress);
	}

	~CSettingsContentRevealScope()
	{
		if(m_pFlow != nullptr)
			m_pFlow->EndReveal();
	}

	CSettingsContentRevealScope(const CSettingsContentRevealScope &) = delete;
	CSettingsContentRevealScope &operator=(const CSettingsContentRevealScope &) = delete;

private:
	CSettingsContentLayoutFlow *m_pFlow = nullptr;
	float m_Progress = 1.0f;
	std::optional<CUiScopedControlAlphaScale> m_Alpha;
};

// 开关/按钮驱动的一组条件行：第一次遇到「可见」时直接就位（打开设置页时不该
// 补播一次入场），之后显隐才走过渡。调用方三行接入：
//   static CSettingsConditionalRows s_Rows;
//   if(s_Rows.Update(Cond, FrameDt, MotionLevel)) {
//       const CSettingsContentRevealScope Scope(Ui(), Flow, s_Rows.Fade(MotionLevel));
//       ...声明行...
//   }
class CSettingsConditionalRows
{
public:
	bool Update(const bool Visible, const float DeltaTime, const int MotionLevel)
	{
		if(!m_Initialized)
		{
			m_Initialized = true;
			if(Visible)
				m_Reveal.SnapVisible(QmInteractiveRevealSeconds(MotionLevel));
		}
		return m_Reveal.Update(Visible, DeltaTime, MotionLevel);
	}

	float Fade(const int MotionLevel) const { return m_Reveal.Visual(MotionLevel).m_Fade; }

private:
	CSettingsRowReveal m_Reveal;
	bool m_Initialized = false;
};

// 这一段内容能否用 CSettingsContentLayoutFlow::AdvancedHeight() 量出占高：
// 声明趟的声明列表就是本帧布局的来源；没有布局作用域的自绘卡片直接推进内容游标。
// 输入/绘制趟没有新声明，区间差恒为 0，不能拿来量。
inline bool QmSettingsRevealCanMeasureContent(const CSettingsContentLayoutFlow &Flow)
{
	return Flow.Declaring() || CSettingsContentLayoutScope::Current() == nullptr;
}

// 互斥分支：同一个槽位里「同一时刻只该出现一个」的几块内容之间的交叉过渡。
// 和 CSettingsConditionalRows 的区别有两点：
//   * 收起与展开共用同一条进度（同时长、同一条 ease-out 曲线）。各块不再自己计时，
//     切换时不会两块各自按自己的节奏占位、挤出约 200ms 的叠合；
//   * 过渡期间槽高恒定。SlotPadding() 补一档 Flow.Space，让
//     「旧分支 × 收起系数 + 新分支 × 展开系数 + 补齐」= 两块自然高度的较大值，
//     槽下面的行在这 0.14s 里不会被反复推着走，卡片高度也不会先涨后缩。
// 接入范式（目标索引 -1 = 所有条件都不成立，按收起处理）：
//   static CSettingsExclusiveReveal s_BackendReveal;
//   s_BackendReveal.Update(IsTencent ? 0 : IsLibre ? 1 : -1, 2, FrameDt, MotionLevel);
//   {
//       const CSettingsExclusiveBranchScope Branch(Ui(), Flow, s_BackendReveal, 0);
//       if(Branch.Visible())
//       {
//           ...声明这一个分支的行（含它自留的行距）...
//       }
//   }
//   { ...另一个分支，同上... }
//   Flow.Space(s_BackendReveal.SlotPadding());
// 用 BranchScope 时不要自己再套一层 CSettingsContentRevealScope，系数已经进去了。
// 量占高走 Flow.AdvancedHeight()：分支的行高/行距按系数缩放后登记，所以含自动换行
// 文本的分支只能估到「声明高度」，槽高宁可略矮也不会把卡片撑歪（超额部分在收尾里放掉）。
class CSettingsExclusiveReveal
{
	friend class CSettingsExclusiveBranchScope;

public:
	// 互斥组按「同一槽位二选一 / 三选一」使用，4 档已经够用；再多说明该拆成两张卡，
	// 而不是继续往一个槽位里塞。
	static constexpr int MAX_BRANCHES = 4;
	// 反推分支自然高度时要求的最小系数：系数太小时「测量高度 / 系数」会把
	// 不随系数缩放的行（自动换行的文本行）放大成离谱的槽高，
	// 低于这个系数的分支只信上一回满系数时的记录，不再当场反推。
	static constexpr float MIN_NATURAL_FADE = 0.05f;

	// 每帧一次，在声明任何分支之前调用。Target = 本帧成立的分支索引，-1 = 都不成立；
	// BranchCount = 本组分支数。进度沿用 QmSettingsRevealShouldAdvance()：
	// 一帧只推一次，且推在量高那趟。
	void Update(const int Target, const int BranchCount, const float DeltaTime, const int MotionLevel)
	{
		m_BranchCount = std::clamp(BranchCount, 0, MAX_BRANCHES);
		m_MotionLevel = std::max(0, MotionLevel);
		m_FrameDt = QmSettingsRevealShouldAdvance() ? std::max(0.0f, DeltaTime) : 0.0f;
		for(int Branch = 0; Branch < MAX_BRANCHES; ++Branch)
			m_aMeasuredHeight[Branch] = 0.0f;
		m_Measured = false;

		const int ClampedTarget = Target >= 0 && Target < m_BranchCount ? Target : -1;
		const float Duration = QmInteractiveRevealSeconds(m_MotionLevel);
		if(!m_Initialized)
		{
			// 第一次渲染直接就位：打开设置页时不该补播一次交叉过渡。
			m_Initialized = true;
			m_Target = ClampedTarget;
			for(int Branch = 0; Branch < MAX_BRANCHES; ++Branch)
			{
				m_aFadeStart[Branch] = Branch == m_Target ? 1.0f : 0.0f;
				m_aFade[Branch] = m_aFadeStart[Branch];
			}
			m_Elapsed = Duration;
			m_SettleElapsed = Duration;
			m_Slot = 0.0f;
			m_SlotMode = ESlotMode::FOLLOW;
			return;
		}

		if(ClampedTarget != m_Target)
		{
			// 从当前系数接着走，不回到 0/1：来回快速切换时不会先弹满再收。
			m_Target = ClampedTarget;
			for(int Branch = 0; Branch < MAX_BRANCHES; ++Branch)
				m_aFadeStart[Branch] = m_aFade[Branch];
			m_Elapsed = 0.0f;
			m_SettleElapsed = 0.0f;
			// 新的一轮过渡接管槽高：有收起中的分支就走交叉，否则槽高跟着内容走。
			m_SlotMode = ESlotMode::FOLLOW;
		}

		if(m_Elapsed < Duration)
			m_Elapsed = std::min(m_Elapsed + m_FrameDt, Duration);
		const float Progress = QmInteractiveRevealProgress(m_Elapsed, m_MotionLevel);
		bool bOutgoing = false;
		for(int Branch = 0; Branch < MAX_BRANCHES; ++Branch)
		{
			const float Goal = Branch == m_Target ? 1.0f : 0.0f;
			m_aFade[Branch] = m_aFadeStart[Branch] + (Goal - m_aFadeStart[Branch]) * Progress;
			if(Branch != m_Target && m_aFade[Branch] > 0.0f)
				bOutgoing = true;
		}

		if(m_Target < 0)
		{
			// 一个都不成立：按收起处理，槽高跟着内容一起收，不补高度。
			m_SlotMode = ESlotMode::FOLLOW;
			m_Slot = 0.0f;
			m_SettleElapsed = 0.0f;
		}
		else if(bOutgoing && m_Elapsed < Duration)
		{
			// 交叉过渡进行中：槽高冻结在两块的较大高度上（具体数值在 SlotPadding 里按本帧测量确认）。
			m_SlotMode = ESlotMode::CROSSFADE;
		}
		else if(m_SlotMode == ESlotMode::CROSSFADE)
		{
			// 交叉刚结束：过渡期多出来的补齐不在这一帧放掉，否则槽下面的行会平移一下。
			m_SettleFrom = m_Slot;
			m_SettleElapsed = 0.0f;
			m_SlotMode = ESlotMode::SETTLE;
		}
		else if(m_SlotMode == ESlotMode::SETTLE)
		{
			m_SettleElapsed = std::min(m_SettleElapsed + m_FrameDt, Duration);
		}
	}

	// 分支本帧的显隐系数：目标分支展开、上一个分支收起、其余为 0。
	// 0 表示本帧不用声明该分支——过渡结束就真的不再占位。
	float Fade(const int Branch) const
	{
		if(Branch < 0 || Branch >= MAX_BRANCHES)
			return 0.0f;
		return std::clamp(m_aFade[Branch], 0.0f, 1.0f);
	}

	// 声明完本组所有分支后调用一次，把返回值交给 Flow.Space()。
	// 过渡中返回「两分支较大高度 − 两块当前占高」；收尾阶段按同一时长把剩余补齐放掉；
	// 单分支扩张/收起与目标为 -1 时返回 0（槽高就该跟着内容走）。
	float SlotPadding()
	{
		if(!m_Measured)
			return 0.0f; // 输入/绘制趟没有测量，也没有布局要补
		float Content = 0.0f;
		for(int Branch = 0; Branch < MAX_BRANCHES; ++Branch)
		{
			Content += m_aMeasuredHeight[Branch];
			// 系数满档时的测量就是真实占高，记下来：下一次过渡不用再靠系数反推。
			if(m_aFade[Branch] >= 0.999f)
				m_aNaturalHeight[Branch] = m_aMeasuredHeight[Branch];
		}

		if(m_SlotMode == ESlotMode::CROSSFADE)
		{
			// 参与交叉的分支取各自自然高度里的较大值（系数为 0 的分支没声明，不参与）。
			// 槽高只增不减：测量抖动不该让槽高在过渡里来回抖。
			for(int Branch = 0; Branch < MAX_BRANCHES; ++Branch)
			{
				if(m_aFade[Branch] <= 0.0f)
					continue;
				m_Slot = std::max(m_Slot, NaturalHeight(Branch));
			}
		}
		else if(m_SlotMode == ESlotMode::SETTLE)
		{
			const float Natural = TargetNaturalHeight();
			const float Progress = QmInteractiveRevealProgress(m_SettleElapsed, m_MotionLevel);
			m_Slot = m_SettleFrom + (Natural - m_SettleFrom) * Progress;
			if(Progress >= 1.0f)
			{
				m_Slot = Natural;
				m_SlotMode = ESlotMode::FOLLOW;
				m_SettleElapsed = 0.0f;
			}
		}
		else
		{
			// 稳态：槽高就是内容高度，不需要补齐。
			m_Slot = Content;
			return 0.0f;
		}
		return std::max(0.0f, m_Slot - Content);
	}

private:
	enum class ESlotMode
	{
		// 槽高跟随内容：单分支扩张/收起、目标为 -1，以及过渡收尾之后。
		FOLLOW,
		// 交叉过渡：槽高冻结在两块的较大高度上，补齐让槽下面的行不动。
		CROSSFADE,
		// 过渡结束后把多出来的补齐按同一时长放掉，避免最后一步瞬间塌高。
		SETTLE,
	};

	// 由 CSettingsExclusiveBranchScope 回调：登记该分支本趟按系数实际占用的高度（含它自留的行距）。
	void RegisterBranchHeight(const int Branch, const float Height)
	{
		if(Branch < 0 || Branch >= MAX_BRANCHES)
			return;
		m_aMeasuredHeight[Branch] = std::max(0.0f, Height);
		m_Measured = true;
	}

	// 分支的自然高度（本帧按系数缩放前的占高）。系数满档时的测量是准确的；
	// 过渡中按「测量高度 / 系数」反推，系数太小时只信上一回满档的记录——
	// 反推假设系数线性，而展开比例只乘在行高/行距上、不适用于自动换行文本的行，
	// 系数越小反推越不可靠。
	float NaturalHeight(const int Branch) const
	{
		if(Branch < 0 || Branch >= MAX_BRANCHES)
			return 0.0f;
		if(m_aFade[Branch] < MIN_NATURAL_FADE)
			return m_aNaturalHeight[Branch];
		return std::max(m_aNaturalHeight[Branch], m_aMeasuredHeight[Branch] / m_aFade[Branch]);
	}

	// 目标分支此刻的自然高度。收尾阶段目标分支一定是满系数（系数归零才进收尾），
	// 所以这里用当帧测量而不是历史记录，避免把「上次显示时的旧高度」带到这一轮。
	float TargetNaturalHeight() const
	{
		if(m_Target < 0 || m_Target >= m_BranchCount || m_aFade[m_Target] <= 0.0f)
			return 0.0f;
		return m_aMeasuredHeight[m_Target] / m_aFade[m_Target];
	}

	float m_aFade[MAX_BRANCHES] = {};
	// 本轮过渡的起点系数：目标变化时从当前系数接着走，而不是重置成 0/1。
	float m_aFadeStart[MAX_BRANCHES] = {};
	// 本趟各分支登记的占用高度（只在测量的趟里成立）。
	float m_aMeasuredHeight[MAX_BRANCHES] = {};
	// 各分支最近一次满系数显示时的真实占高（反推的兜底，避免过渡中估矮）。
	float m_aNaturalHeight[MAX_BRANCHES] = {};
	int m_Target = -1;
	int m_BranchCount = 0;
	int m_MotionLevel = 0;
	float m_FrameDt = 0.0f;
	float m_Elapsed = 0.0f;
	// 当前槽高与收尾阶段的起点，单位与分支测量高度一致（含外层折叠区的整体缩放）。
	float m_Slot = 0.0f;
	float m_SettleFrom = 0.0f;
	float m_SettleElapsed = 0.0f;
	ESlotMode m_SlotMode = ESlotMode::FOLLOW;
	bool m_Measured = false;
	bool m_Initialized = false;
};

// 互斥组里的一个分支：构造时按系数压入展开比例并记下 Flow 已推进的高度，
// 析构时把本分支实际占用的高度登记回组里。系数为 0 的分支整块不声明。
class CSettingsExclusiveBranchScope
{
public:
	CSettingsExclusiveBranchScope(CUi *pUi, CSettingsContentLayoutFlow &Flow, CSettingsExclusiveReveal &Group, const int Branch) :
		m_pFlow(&Flow), m_pGroup(&Group), m_Branch(Branch)
	{
		const float Fade = Group.Fade(Branch);
		if(Fade <= 0.0f)
			return;
		m_Visible = true;
		m_StartHeight = Flow.AdvancedHeight();
		m_Reveal.emplace(pUi, Flow, Fade);
	}

	~CSettingsExclusiveBranchScope()
	{
		if(!m_Visible)
			return;
		if(QmSettingsRevealCanMeasureContent(*m_pFlow))
			m_pGroup->RegisterBranchHeight(m_Branch, m_pFlow->AdvancedHeight() - m_StartHeight);
		m_Reveal.reset();
	}

	CSettingsExclusiveBranchScope(const CSettingsExclusiveBranchScope &) = delete;
	CSettingsExclusiveBranchScope &operator=(const CSettingsExclusiveBranchScope &) = delete;

	// 本帧是否需要声明这一分支的内容。
	bool Visible() const { return m_Visible; }

private:
	CSettingsContentLayoutFlow *m_pFlow = nullptr;
	CSettingsExclusiveReveal *m_pGroup = nullptr;
	int m_Branch = -1;
	bool m_Visible = false;
	float m_StartHeight = 0.0f;
	std::optional<CSettingsContentRevealScope> m_Reveal;
};

#endif
