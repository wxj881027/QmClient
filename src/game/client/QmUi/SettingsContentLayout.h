#ifndef GAME_CLIENT_QMUI_SETTINGSCONTENTLAYOUT_H
#define GAME_CLIENT_QMUI_SETTINGSCONTENTLAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline std::array<CUIRect, 2> ResolveSettingsTextColumns(const CUIRect &Row, float PrimaryFraction, float Gap)
{
	const float Width = std::max(0.0f, Row.w);
	Gap = std::clamp(Gap, 0.0f, Width);
	const float PrimaryWidth = std::clamp(Width * std::clamp(PrimaryFraction, 0.0f, 1.0f) - Gap, 0.0f, Width - Gap);
	return {{{Row.x, Row.y, PrimaryWidth, Row.h}, {Row.x + PrimaryWidth + Gap, Row.y, Width - PrimaryWidth - Gap, Row.h}}};
}

// 行声明只包含尺寸事实；绑定值与控件输入不进入测量缓存。
struct SSettingsContentRowDeclaration
{
	std::string m_Id;
	std::string m_Text;
	float m_MinimumHeight = 0.0f;
	float m_FontSize = 0.0f;
	float m_Indent = 0.0f;
	float m_Spacing = 0.0f;
	bool m_WrappedText = false;
	bool m_ControlText = false;
	float m_TextLineSpacing = 0.0f;
	float m_TextPadding = 0.0f;
	std::string m_SecondaryText;
	float m_PrimaryFraction = 0.0f;
	float m_ColumnGap = 0.0f;
	bool m_TextColumns = false;

	bool operator==(const SSettingsContentRowDeclaration &Other) const
	{
		return m_Id == Other.m_Id && m_Text == Other.m_Text && m_MinimumHeight == Other.m_MinimumHeight && m_FontSize == Other.m_FontSize && m_Indent == Other.m_Indent && m_Spacing == Other.m_Spacing && m_WrappedText == Other.m_WrappedText && m_ControlText == Other.m_ControlText && m_TextLineSpacing == Other.m_TextLineSpacing && m_TextPadding == Other.m_TextPadding && m_SecondaryText == Other.m_SecondaryText && m_PrimaryFraction == Other.m_PrimaryFraction && m_ColumnGap == Other.m_ColumnGap && m_TextColumns == Other.m_TextColumns;
	}
};

struct SSettingsContentLayoutRow
{
	SSettingsContentRowDeclaration m_Declaration;
	CUIRect m_Rect;
};

// 解析后只通过 const 所有权发布。测量、输入与绘制共用相对坐标，动画只平移原点。
class CSettingsContentLayout
{
	friend class CSettingsContentLayoutCache;
	std::vector<SSettingsContentLayoutRow> m_vRows;
	float m_Width = 0.0f;
	float m_Height = 0.0f;

public:
	float Width() const { return m_Width; }
	float Height() const { return m_Height; }
	const std::vector<SSettingsContentLayoutRow> &Rows() const { return m_vRows; }
	const SSettingsContentLayoutRow *Find(const char *pId) const
	{
		const auto It = std::find_if(m_vRows.begin(), m_vRows.end(), [pId](const auto &Row) { return Row.m_Declaration.m_Id == pId; });
		return It != m_vRows.end() ? &*It : nullptr;
	}
};

class CSettingsContentLayoutCache
{
	std::vector<SSettingsContentRowDeclaration> m_vDeclarations;
	std::shared_ptr<const CSettingsContentLayout> m_pLayout;
	uint64_t m_FontRevision = UINT64_MAX;
	// 声明缓冲由缓存持有并复用：每帧每张卡都会重新声明内容，不能每次新建 vector。
	std::vector<SSettingsContentRowDeclaration> m_vScratchDeclarations;

public:
	using FMeasureText = std::function<float(const char *, float, float)>;
	using FMeasureControlText = std::function<float(const SSettingsContentRowDeclaration &, float)>;
	std::vector<SSettingsContentRowDeclaration> &ScratchDeclarations()
	{
		m_vScratchDeclarations.clear();
		return m_vScratchDeclarations;
	}
	std::shared_ptr<const CSettingsContentLayout> Resolve(const std::vector<SSettingsContentRowDeclaration> &vRows, float Width, uint64_t FontRevision, const FMeasureText &MeasureText, const FMeasureControlText &MeasureControlText = {})
	{
		Width = std::isfinite(Width) ? std::max(0.0f, Width) : 0.0f;
		if(m_pLayout && m_pLayout->Width() == Width && m_FontRevision == FontRevision && m_vDeclarations == vRows)
			return m_pLayout;
		// 声明键与几何共用同一候选快照，准备失败不修改已发布缓存。
		std::vector<SSettingsContentRowDeclaration> Declarations(vRows);
		auto pLayout = std::make_shared<CSettingsContentLayout>();
		pLayout->m_Width = Width;
		pLayout->m_vRows.reserve(Declarations.size());
		for(const auto &Declaration : Declarations)
		{
			const float Indent = std::clamp(Declaration.m_Indent, 0.0f, Width);
			const float RowWidth = std::max(0.0f, Width - Indent);
			float Height = std::max(0.0f, Declaration.m_MinimumHeight);
			if(Declaration.m_TextColumns && MeasureText)
			{
				const auto Columns = ResolveSettingsTextColumns({0, 0, RowWidth, Height}, Declaration.m_PrimaryFraction, Declaration.m_ColumnGap);
				const char *apText[] = {Declaration.m_Text.c_str(), Declaration.m_SecondaryText.c_str()};
				for(int Index = 0; Index < 2; ++Index)
				{
					if(Columns[Index].w <= 0.0f)
						continue;
					const float TextHeight = MeasureText(apText[Index], Columns[Index].w, Declaration.m_FontSize);
					if(std::isfinite(TextHeight))
						Height = std::max(Height, TextHeight);
				}
			}
			else if(Declaration.m_WrappedText && RowWidth > 0.0f && (MeasureText || (Declaration.m_ControlText && MeasureControlText)))
			{
				const float TextHeight = Declaration.m_ControlText && MeasureControlText ? MeasureControlText(Declaration, RowWidth) : MeasureText(Declaration.m_Text.c_str(), RowWidth, Declaration.m_FontSize);
				if(std::isfinite(TextHeight))
					Height = std::max(Height, TextHeight);
			}
			pLayout->m_vRows.push_back({Declaration, {Declaration.m_ControlText ? 0.0f : Indent, pLayout->m_Height, Declaration.m_ControlText ? Width : RowWidth, Height}});
			pLayout->m_Height += Height + std::max(0.0f, Declaration.m_Spacing);
		}
		m_vDeclarations.swap(Declarations);
		m_FontRevision = FontRevision;
		m_pLayout = std::move(pLayout);
		return m_pLayout;
	}
};

// Deck 与行为测试共用同帧边界：先消费既有布局上的输入，再发布本帧最终布局。
template<typename TInput, typename TResolve>
inline bool ExecuteSettingsContentInputThenResolve(const std::shared_ptr<const CSettingsContentLayout> &pPreviousLayout, const CUIRect &Content, TInput &&Input, TResolve &&Resolve)
{
	if(!pPreviousLayout)
		return false;
	Input(*pPreviousLayout, Content);
	return Resolve(pPreviousLayout->Width()) != pPreviousLayout;
}

enum class ESettingsContentPass
{
	DECLARE,
	INPUT,
	DRAW,
};

// 旧菜单签名的迁移桥：作用域明确携带声明或不可变布局，不修改全局配置快照。
// 新模块可直接使用声明和 Layout，迁移模块在同一内容函数中登记稳定行 ID。
class CSettingsContentLayoutScope
{
	inline static thread_local CSettingsContentLayoutScope *s_pCurrent = nullptr;
	CSettingsContentLayoutScope *m_pPrevious;

public:
	ESettingsContentPass m_Pass;
	std::vector<SSettingsContentRowDeclaration> *m_pDeclarations = nullptr;
	const CSettingsContentLayout *m_pLayout = nullptr;
	CUIRect m_Origin{};
	bool m_HasOrigin = false;
	// 已由声明方自行预留行距的行：后续对该行的间距只按较大者合并，避免重复叠加成双倍空隙。
	size_t m_MinimumSpacingRow = SIZE_MAX;
	CSettingsContentLayoutScope(std::vector<SSettingsContentRowDeclaration> &vRows) : m_pPrevious(s_pCurrent), m_Pass(ESettingsContentPass::DECLARE), m_pDeclarations(&vRows) { s_pCurrent = this; }
	CSettingsContentLayoutScope(const CSettingsContentLayout &Layout, ESettingsContentPass Pass) : m_pPrevious(s_pCurrent), m_Pass(Pass), m_pLayout(&Layout) { s_pCurrent = this; }
	// 专用二维编辑器自行解析内部几何；外层只登记真实块尺寸，内部不复用卡片行坐标。
	CSettingsContentLayoutScope() : m_pPrevious(s_pCurrent), m_Pass(ESettingsContentPass::DRAW) { s_pCurrent = nullptr; }
	~CSettingsContentLayoutScope() { s_pCurrent = m_pPrevious; }
	CSettingsContentLayoutScope(const CSettingsContentLayoutScope &) = delete;
	CSettingsContentLayoutScope &operator=(const CSettingsContentLayoutScope &) = delete;
	static CSettingsContentLayoutScope *Current() { return s_pCurrent; }
};

class CSettingsContentLayoutFlow
{
	CUIRect &m_Content;
	CSettingsContentLayoutScope *m_pScope = CSettingsContentLayoutScope::Current();
	const CUIRect m_Origin;
	// 展开/收起过渡的缩放系数：区间内声明的行按它缩放高度与行距，布局随之重算，
	// 下面的行因此会平滑让位，而不是瞬间跳一格。默认 1（无过渡）。
	float m_RevealScale = 1.0f;
	float m_RevealStack[8] = {};
	int m_RevealDepth = 0;

	const SSettingsContentLayoutRow *Place(const char *pId, CUIRect &Row)
	{
		const auto *pRow = m_pScope->m_pLayout->Find(pId);
		if(pRow == nullptr)
			return nullptr; // 输入提交新增分支时，本帧不能命中上一布局不存在的控件。
		Row = pRow->m_Rect;
		Row.x += m_Origin.x;
		Row.y += m_Origin.y;
		m_Content.y = m_Origin.y + m_pScope->m_pLayout->Height();
		m_Content.h = m_Origin.h - m_pScope->m_pLayout->Height();
		return pRow;
	}

public:
	explicit CSettingsContentLayoutFlow(CUIRect &Content) : m_Content(Content), m_Origin(ResolveOrigin(Content)) {}

private:
	CUIRect ResolveOrigin(const CUIRect &Content)
	{
		if(m_pScope == nullptr || Declaring())
			return Content;
		// 同一卡片可由多个内容 helper 声明；回放必须始终使用卡片原点，
		// 不能把前一个 helper 已消费的光标当成下一组行的原点。
		if(!m_pScope->m_HasOrigin)
		{
			m_pScope->m_Origin = Content;
			m_pScope->m_HasOrigin = true;
		}
		return m_pScope->m_Origin;
	}

public:
	bool Input() const { return m_pScope == nullptr || m_pScope->m_Pass == ESettingsContentPass::INPUT; }
	bool Drawing() const { return m_pScope == nullptr || m_pScope->m_Pass == ESettingsContentPass::DRAW; }
	bool Declaring() const { return m_pScope && m_pScope->m_Pass == ESettingsContentPass::DECLARE; }
	// 只读：Flow 到此刻已经推进的高度。用法是「声明一段内容前后各取一次，差值是它的占高」。
	// 声明趟按已登记的「行高 + 行距」累计，与布局高度同口径；自动换行的文本要等布局解析
	// 才有真实行高，所以含换行文本的行会比最终布局高度略小。输入/绘制趟不再产生声明，
	// 这里返回本帧布局总高度（区间差恒为 0），只适合观察总量。没有布局作用域的自绘卡片
	// 直接按内容游标位移给出真实推进高度。
	float AdvancedHeight() const
	{
		if(Declaring())
		{
			if(m_pScope->m_pDeclarations == nullptr)
				return 0.0f;
			float Height = 0.0f;
			for(const auto &Declaration : *m_pScope->m_pDeclarations)
				Height += std::max(0.0f, Declaration.m_MinimumHeight) + std::max(0.0f, Declaration.m_Spacing);
			return Height;
		}
		if(m_pScope != nullptr)
			return m_pScope->m_pLayout != nullptr ? m_pScope->m_pLayout->Height() : 0.0f;
		return std::max(0.0f, m_Content.y - m_Origin.y);
	}
	const char *PublishedText(const char *pId, const char *pFallback) const
	{
		if(m_pScope && m_pScope->m_pLayout)
		{
			const auto *pRow = m_pScope->m_pLayout->Find(pId);
			if(pRow)
				return pRow->m_Declaration.m_Text.c_str();
		}
		return pFallback;
	}
	bool Control(const char *pId, float Height, CUIRect &Row, float SpacingBefore = 0.0f)
	{
		const float ScaledHeight = std::max(0.0f, Height * m_RevealScale);
		// 行距不要在这里预先乘系数：Space() 内部会按展开进度缩放一次，先乘一遍会变成
		// 系数平方（过渡中行距被压得过死，展开结束才「弹」回正常值）。
		const float UnscaledSpacing = std::max(0.0f, SpacingBefore);
		if(Declaring())
		{
			Space(UnscaledSpacing);
			m_pScope->m_pDeclarations->push_back({pId, {}, ScaledHeight});
			return false;
		}
		if(m_pScope)
			return Place(pId, Row) != nullptr && Row.w > 0.0f && Row.h > 0.0f;
		if(m_Content.y != m_Origin.y)
			Space(UnscaledSpacing);
		m_Content.HSplitTop(ScaledHeight, &Row, &m_Content);
		return true;
	}

	// 展开/收起过渡：Progress 0..1，区间内新声明的行按进度缩放，区间外不受影响；
	// 支持嵌套（例如折叠区里再套一层开关驱动的参数行），系数相乘。
	void BeginReveal(float Progress)
	{
		if(m_RevealDepth < (int)std::size(m_RevealStack))
			m_RevealStack[m_RevealDepth] = m_RevealScale;
		++m_RevealDepth;
		m_RevealScale *= std::clamp(Progress, 0.0f, 1.0f);
	}

	void EndReveal()
	{
		if(m_RevealDepth <= 0)
			return;
		--m_RevealDepth;
		if(m_RevealDepth < (int)std::size(m_RevealStack))
			m_RevealScale = m_RevealStack[m_RevealDepth];
	}
	void Space(float Height)
	{
		const float ScaledHeight = std::max(0.0f, Height * m_RevealScale);
		if(Declaring())
		{
			if(!m_pScope->m_pDeclarations->empty())
			{
				auto &Back = m_pScope->m_pDeclarations->back();
				// 自留行距的行只取较大者：调用方再声明同一处间距时不会叠成两倍。
				if(m_pScope->m_pDeclarations->size() - 1 == m_pScope->m_MinimumSpacingRow)
					Back.m_Spacing = std::max(Back.m_Spacing, ScaledHeight);
				else
					Back.m_Spacing += ScaledHeight;
			}
		}
		else if(m_pScope == nullptr)
			m_Content.HSplitTop(ScaledHeight, nullptr, &m_Content);
	}
	// 多行输入测量 control 列的文本，回放仍发布包含 label 的完整行。
	bool MeasuredControl(const char *pId, const char *pText, float MinimumHeight, float FontSize, float ControlIndent, float TextLineSpacing, float TextPadding, CUIRect &Row, const CSettingsContentLayoutCache::FMeasureControlText &Measure)
	{
		if(Declaring())
		{
			m_pScope->m_pDeclarations->push_back({pId, pText, MinimumHeight, FontSize, ControlIndent, 0.0f, true, true, TextLineSpacing, TextPadding});
			return false;
		}
		if(m_pScope)
			return Place(pId, Row) != nullptr && Row.w > 0.0f && Row.h > 0.0f;
		const SSettingsContentRowDeclaration Declaration{pId, pText, MinimumHeight, FontSize, ControlIndent, 0.0f, true, true, TextLineSpacing, TextPadding};
		const float Height = Measure ? std::max(MinimumHeight, Measure(Declaration, std::max(0.0f, m_Content.w - ControlIndent))) : MinimumHeight;
		m_Content.HSplitTop(Height, &Row, &m_Content);
		return true;
	}
	void Text(const char *pId, const char *pText, float Indent, float FontSize, float Spacing, const CSettingsContentLayoutCache::FMeasureText &Measure, const std::function<void(const CUIRect &, const char *)> &Draw, float MinimumHeight = 0.0f)
	{
		if(Declaring())
		{
			// 文本行的行后间距也要跟着展开进度缩放，否则过渡中它和 Control 行的节奏不一致
			// （Control 由 Space() 缩放一次，这里手动补同一次缩放）。
			m_pScope->m_pDeclarations->push_back({pId, pText, std::max(FontSize, MinimumHeight), FontSize, Indent, std::max(0.0f, Spacing) * m_RevealScale, true});
			return;
		}
		CUIRect Row;
		if(m_pScope)
		{
			const auto *pRow = Place(pId, Row);
			if(pRow && Row.w > 0.0f && Row.h > 0.0f && m_pScope->m_Pass == ESettingsContentPass::DRAW)
				Draw(Row, pRow->m_Declaration.m_Text.c_str());
			return;
		}
		const float Left = std::clamp(Indent, 0.0f, std::max(0.0f, m_Content.w));
		const float Width = std::max(1.0f, m_Content.w - Left);
		m_Content.HSplitTop(std::max({FontSize, MinimumHeight, Measure ? Measure(pText, Width, FontSize) : FontSize}), &Row, &m_Content);
		Row.x += Left;
		Row.w = Width;
		Space(Spacing);
		Draw(Row, pText);
	}
	void TextColumns(const char *pId, const char *pPrimary, const char *pSecondary, float FontSize, float MinimumHeight, float PrimaryFraction, float Gap, float Spacing, const CSettingsContentLayoutCache::FMeasureText &Measure, const std::function<void(const CUIRect &, const char *)> &Draw)
	{
		SSettingsContentRowDeclaration Declaration{pId, pPrimary, MinimumHeight, FontSize, 0.0f, Spacing};
		Declaration.m_SecondaryText = pSecondary;
		Declaration.m_PrimaryFraction = PrimaryFraction;
		Declaration.m_ColumnGap = Gap;
		Declaration.m_TextColumns = true;
		if(Declaring())
		{
			m_pScope->m_pDeclarations->push_back(std::move(Declaration));
			return;
		}
		CUIRect Row;
		if(m_pScope)
		{
			const auto *pRow = Place(pId, Row);
			if(!pRow || m_pScope->m_Pass != ESettingsContentPass::DRAW)
				return;
			Declaration = pRow->m_Declaration;
		}
		else
		{
			CSettingsContentLayoutCache Cache;
			const auto pLayout = Cache.Resolve({Declaration}, m_Content.w, 0, Measure);
			m_Content.HSplitTop(pLayout->Height() - std::max(0.0f, Spacing), &Row, &m_Content);
			Space(Spacing);
		}
		const auto Columns = ResolveSettingsTextColumns(Row, Declaration.m_PrimaryFraction, Declaration.m_ColumnGap);
		if(Columns[0].w > 0.0f)
			Draw(Columns[0], Declaration.m_Text.c_str());
		if(Columns[1].w > 0.0f)
			Draw(Columns[1], Declaration.m_SecondaryText.c_str());
	}
};

#endif
