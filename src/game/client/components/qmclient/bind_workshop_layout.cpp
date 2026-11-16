#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/qmclient/bind_workshop.h>
#include <game/client/ui_rect.h>

#include <algorithm>

// 版式计算（卡片底栏与工坊框架）只依赖 CUIRect 几何，单独成一个编译单元：
// bind_workshop.cpp 因此保持「不依赖 UI 类型」的纯逻辑，qm-benchmarks 可以直接
// 链接它跑基准，不必把整套 client UI 拖进基准进程。
namespace qm_bind_workshop
{
	// ---- 底栏纵向度量：测量（瀑布流算卡高）与版式共用同一份算法 ----
	float SWorkshopFooterMetrics::Height() const
	{
		float Height = m_ModeHeight + m_RowGap + m_ActionHeight;
		if(m_ParamHeight > 0.0f)
			Height += m_ParamHeight + m_RowGap;
		if(m_KeyRowsHeight > 0.0f)
			Height += m_KeyRowsHeight + m_RowGap;
		return Height;
	}

	SWorkshopFooterMetrics ResolveWorkshopFooterMetrics(float InnerWidth, size_t NumSteppers, size_t NumKeyRows, float UiScale)
	{
		SWorkshopFooterMetrics Metrics;
		const float Scale = UiScale > 0.0f ? UiScale : 1.0f;
		Metrics.m_ActionHeight = WORKSHOP_ACTION_ROW_HEIGHT * Scale;
		Metrics.m_ModeHeight = WORKSHOP_MODE_ROW_HEIGHT * Scale;
		// 顶层分组之间（模式行 / 键槽块 / 应用行、参数行 / 模式行）用 SM 间距；
		// 同一个分组内部（多键槽行之间、堆叠的步进器之间）仍用行距，保持组内紧、组间松。
		Metrics.m_RowGap = ui_token::spacing::SM * Scale;
		const float InnerRowGap = WORKSHOP_ROW_GAP * Scale;
		const float ItemGap = WORKSHOP_ITEM_GAP * Scale;
		const size_t Steppers = std::min(NumSteppers, MAX_PRESET_PARAMS);
		// 步进器改成「左标签 + 右『- 值 +』」后，并排两份至少要这么多宽，
		// 放不下就一人一行：标签在左、按钮组贴右，不把标签挤没、也不让两条挤成一条。
		constexpr float STEPPER_STACK_MIN_WIDTH = 132.0f;
		if(Steppers > 0)
		{
			const float TotalGap = ItemGap * static_cast<float>(Steppers - 1);
			const float EachSide = std::max(0.0f, (InnerWidth - TotalGap) / static_cast<float>(Steppers));
			Metrics.m_SteppersStacked = Steppers > 1 && EachSide < STEPPER_STACK_MIN_WIDTH * Scale;
			Metrics.m_ParamHeight = Metrics.m_SteppersStacked ?
							Metrics.m_ActionHeight * static_cast<float>(Steppers) + InnerRowGap * static_cast<float>(Steppers - 1) :
							Metrics.m_ActionHeight;
		}
		// 一组动作：每个变体一行键槽；互斥方案只有一行，且这一行就是应用按钮那一行。
		Metrics.m_NumKeyRows = std::clamp<size_t>(NumKeyRows, 1, MAX_PRESET_KEY_ROWS);
		if(Metrics.m_NumKeyRows > 1)
			Metrics.m_KeyRowsHeight = Metrics.m_ActionHeight * static_cast<float>(Metrics.m_NumKeyRows) + InnerRowGap * static_cast<float>(Metrics.m_NumKeyRows - 1);
		return Metrics;
	}

	// ---- 卡片底栏版式：模式行 + 参数行 + 键槽行 + 应用行 ----
	SWorkshopCardFooter ResolveWorkshopCardFooter(const CUIRect &Inner, const SWorkshopCardFooterRequest &Request)
	{
		SWorkshopCardFooter Out;
		const float Scale = Request.m_UiScale > 0.0f ? Request.m_UiScale : 1.0f;
		const SWorkshopFooterMetrics Metrics = ResolveWorkshopFooterMetrics(Inner.w, Request.m_NumSteppers, Request.m_NumKeyRows, Scale);
		const float ActionHeight = Metrics.m_ActionHeight;
		const float ModeHeight = Metrics.m_ModeHeight;
		const float RowGap = Metrics.m_RowGap;
		const float InnerRowGap = WORKSHOP_ROW_GAP * Scale;
		const float ItemGap = WORKSHOP_ITEM_GAP * Scale;
		const size_t NumSteppers = std::min(Request.m_NumSteppers, MAX_PRESET_PARAMS);
		Out.m_NumKeyRows = Metrics.m_NumKeyRows;

		// 自下而上切：应用行 -> 一组动作的键槽行 -> 模式行 -> 可选参数区，
		// 剩下的上半部分都给命令框。互斥方案没有独立的键槽行区：键槽与应用同一行。
		Out.m_ActionRow = CUIRect{Inner.x, Inner.y + Inner.h - ActionHeight, Inner.w, ActionHeight};
		float BottomTop = Out.m_ActionRow.y;
		if(Metrics.m_KeyRowsHeight > 0.0f)
		{
			const float KeyBlockTop = Out.m_ActionRow.y - RowGap - Metrics.m_KeyRowsHeight;
			float RowY = KeyBlockTop;
			for(size_t Row = 0; Row < Metrics.m_NumKeyRows; ++Row)
			{
				const CUIRect RowRect{Inner.x, RowY, Inner.w, ActionHeight};
				// 变体名列只在给了列宽时占位；没有名字的卡片让键槽吃满整行。
				const float LabelWidth = std::clamp(Request.m_KeyLabelWidth, 0.0f, RowRect.w * 0.6f);
				Out.m_aKeyLabels[Row] = CUIRect{RowRect.x, RowRect.y, LabelWidth, RowRect.h};
				const float SlotLeft = LabelWidth > 0.0f ? RowRect.x + LabelWidth + ItemGap : RowRect.x;
				Out.m_aKeySlots[Row] = CUIRect{SlotLeft, RowRect.y, std::max(0.0f, RowRect.x + RowRect.w - SlotLeft), RowRect.h};
				RowY += ActionHeight + InnerRowGap;
			}
			BottomTop = KeyBlockTop;
		}
		Out.m_ModeRow = CUIRect{Inner.x, BottomTop - RowGap - ModeHeight, Inner.w, ModeHeight};
		float LowerTop = Out.m_ModeRow.y;
		if(NumSteppers > 0)
		{
			Out.m_ParamRow = CUIRect{Inner.x, Out.m_ModeRow.y - RowGap - Metrics.m_ParamHeight, Inner.w, Metrics.m_ParamHeight};
			LowerTop = Out.m_ParamRow.y;
		}
		// 命令框与底栏之间也按组间距走：组内紧、组间松。
		const float CodeBottom = LowerTop - RowGap;
		Out.m_CodeArea = CUIRect{Inner.x, Inner.y, Inner.w, std::max(0.0f, CodeBottom - Inner.y)};

		// 第一行：[模式徽标][变体][来源]。来源贴右，徽标宽度先夹住，绝不会压过去。
		// 变体是这一行唯一的可点控件：分段控件窄到放不下方案名时，段里就只剩看不懂的
		// 序号，点之前判断不出是哪一版。所以变体要的宽度不够时，先让来源标签退让
		// （三者里它信息最少），让出来的整行宽度都归分段控件。
		const auto LayOutModeRow = [&](float SourceWidth, CUIRect &SourceLabel, CUIRect &ModeBadge, CUIRect &VariantButton) {
			SourceLabel = CUIRect{Out.m_ModeRow.x + Out.m_ModeRow.w - SourceWidth, Out.m_ModeRow.y, SourceWidth, Out.m_ModeRow.h};
			const float ModeMax = std::max(0.0f, SourceLabel.x - ItemGap - Out.m_ModeRow.x);
			const float ModeWidth = std::clamp(Request.m_ModeBadgeWidth, 0.0f, ModeMax);
			ModeBadge = CUIRect{Out.m_ModeRow.x, Out.m_ModeRow.y, ModeWidth, Out.m_ModeRow.h};
			VariantButton = CUIRect{};
			if(Request.m_VariantWidth > 0.0f)
			{
				const float VariantLeft = ModeBadge.x + ModeBadge.w + ItemGap;
				const float VariantRight = SourceLabel.x - ItemGap;
				if(VariantRight > VariantLeft)
					VariantButton = CUIRect{VariantLeft, Out.m_ModeRow.y, std::min(Request.m_VariantWidth, VariantRight - VariantLeft), Out.m_ModeRow.h};
			}
		};
		const float SourceWidth = std::min(WORKSHOP_SOURCE_WIDTH * Scale, Out.m_ModeRow.w);
		LayOutModeRow(SourceWidth, Out.m_SourceLabel, Out.m_ModeBadge, Out.m_VariantButton);
		if(Request.m_VariantWidth > 0.0f && SourceWidth > 0.0f && Out.m_VariantButton.w + 0.5f < Request.m_VariantWidth)
		{
			// 来源让位：这一行只留徽标 + 变体。不禁用来源文字，只把它压成 0 宽。
			LayOutModeRow(0.0f, Out.m_SourceLabel, Out.m_ModeBadge, Out.m_VariantButton);
		}

		// 应用按钮：一组动作时独占应用行、贴右；互斥方案与键槽同一行。
		const float ApplyPreferred = Request.m_ApplyWidth > 0.0f ? Request.m_ApplyWidth : WORKSHOP_APPLY_WIDTH * Scale;
		if(Metrics.m_KeyRowsHeight > 0.0f)
		{
			const float ApplyLow = std::min(WORKSHOP_APPLY_MIN_WIDTH * Scale, Out.m_ActionRow.w);
			const float ApplyWidth = std::clamp(ApplyPreferred, ApplyLow, Out.m_ActionRow.w);
			Out.m_ApplyButton = CUIRect{Out.m_ActionRow.x + Out.m_ActionRow.w - ApplyWidth, Out.m_ActionRow.y, ApplyWidth, Out.m_ActionRow.h};
		}
		else
		{
			// 应用按钮最宽不超过「行宽 - 键槽最小宽 - 间距」，键槽因此不会被压成 0，
			// 两个矩形之间永远隔着 ItemGap：DoKeyReader 的矩形不会盖在任何按钮上。
			const float KeyMin = Request.m_ShowKeySlot ? std::min(WORKSHOP_KEY_SLOT_MIN_WIDTH * Scale, Out.m_ActionRow.w) : 0.0f;
			const float ApplyMax = std::max(WORKSHOP_APPLY_MIN_WIDTH * Scale, Out.m_ActionRow.w - (KeyMin > 0.0f ? KeyMin + ItemGap : 0.0f));
			const float ApplyLow = std::min(WORKSHOP_APPLY_MIN_WIDTH * Scale, ApplyMax);
			const float ApplyWidth = std::clamp(ApplyPreferred, ApplyLow, ApplyMax);
			Out.m_ApplyButton = CUIRect{Out.m_ActionRow.x + Out.m_ActionRow.w - ApplyWidth, Out.m_ActionRow.y, ApplyWidth, Out.m_ActionRow.h};
			if(Request.m_ShowKeySlot)
			{
				const float KeyRight = Out.m_ApplyButton.x - ItemGap;
				if(KeyRight > Out.m_ActionRow.x)
					Out.m_KeySlot = CUIRect{Out.m_ActionRow.x, Out.m_ActionRow.y, KeyRight - Out.m_ActionRow.x, Out.m_ActionRow.h};
			}
			// 单行键槽就是第 0 行：调用方只认 m_aKeySlots 也能画。
			Out.m_aKeySlots[0] = Out.m_KeySlot;
			Out.m_aKeyLabels[0] = CUIRect{};
		}

		// 参数区：放得下就并排平分，放不下就一人一行；两种排法的步进器都吃满自己那一格，
		// 「左标签 + 右按钮组」由 DoParamStepper 在格内再分。
		Out.m_NumSteppers = NumSteppers;
		if(NumSteppers > 0)
		{
			if(Metrics.m_SteppersStacked)
			{
				for(size_t Index = 0; Index < NumSteppers; ++Index)
					Out.m_aSteppers[Index] = CUIRect{Inner.x, Out.m_ParamRow.y + static_cast<float>(Index) * (ActionHeight + InnerRowGap), Inner.w, ActionHeight};
			}
			else
			{
				const float TotalGap = ItemGap * static_cast<float>(NumSteppers - 1);
				const float Each = std::max(0.0f, (Out.m_ParamRow.w - TotalGap) / static_cast<float>(NumSteppers));
				float X = Out.m_ParamRow.x;
				for(size_t Index = 0; Index < NumSteppers; ++Index)
				{
					Out.m_aSteppers[Index] = CUIRect{X, Out.m_ParamRow.y, Each, Out.m_ParamRow.h};
					X += Each + ItemGap;
				}
			}
		}
		return Out;
	}

	SWorkshopLayoutFrame ResolveWorkshopLayout(const CUIRect &MainView, float UiScale)
	{
		SWorkshopLayoutFrame Frame;
		CUIRect Rest = MainView;
		Rest.HSplitTop(WORKSHOP_TOOLBAR_HEIGHT * UiScale, &Frame.m_Toolbar, &Rest);
		Rest.HSplitTop(WORKSHOP_BODY_TOP_GAP * UiScale, nullptr, &Rest);
		Rest.HSplitBottom(WORKSHOP_FOOTER_HEIGHT * UiScale, &Rest, &Frame.m_Footer);
		// 正文与页脚之间的一档间距：这里要的是「剩余区域缩一缩」，
		// 所以剩余区域必须走 pTop；交给 pBottom 只会得到那条 8px 的带。
		Rest.HSplitBottom(WORKSHOP_BODY_BOTTOM_GAP * UiScale, &Rest, nullptr);
		Frame.m_Body = Rest;
		return Frame;
	}
} // namespace qm_bind_workshop
