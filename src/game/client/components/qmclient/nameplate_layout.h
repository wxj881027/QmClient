#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_LAYOUT_H

#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

// 隐私屏蔽只停止绘制，布局继续使用当前内容的尺寸。
class CQmNameplatePartLayout
{
protected:
	vec2 m_Size = vec2(0.0f, 0.0f);
	vec2 m_Padding;
	bool m_Visible = true;
	bool m_HiddenForPrivacy = false;
	bool m_ShiftOnInvis = false;
	bool m_ReserveLineHeight = false;

public:
	explicit CQmNameplatePartLayout(vec2 Padding = vec2(0.0f, 0.0f)) :
		m_Padding(Padding) {}
	vec2 Size() const { return m_Size; }
	vec2 Padding() const { return m_Padding; }
	bool Visible() const { return m_Visible && !m_HiddenForPrivacy; }
	float VisibleTop(float LineBottomY, float LineHeight) const
	{
		// 与 RenderLine 的居中位置一致；隐藏的占位只参与排版，不抬高气泡。
		return Visible() ? LineBottomY - (std::max(LineHeight, m_Padding.y + m_Size.y) + m_Size.y) * 0.5f : std::numeric_limits<float>::infinity();
	}
	bool ShiftOnInvis() const { return m_ShiftOnInvis || (m_Visible && m_HiddenForPrivacy); }
	bool HasLayout() const { return m_Visible || m_ShiftOnInvis || m_ReserveLineHeight; }
	vec2 LayoutSize() const
	{
		if(m_Visible || m_ShiftOnInvis)
			return m_Size + m_Padding;
		if(m_ReserveLineHeight)
			return vec2(0.0f, m_Size.y + m_Padding.y);
		return vec2(0.0f, 0.0f);
	}
};

enum class ENameplateCoreRow
{
	NAME,
	CLAN,
	HOOK,
	COORDS,
	KEYS,
	NUM_ROWS
};

static constexpr size_t kNameplateCoreRowCount = static_cast<size_t>(ENameplateCoreRow::NUM_ROWS);

inline std::array<float, kNameplateCoreRowCount> QmNameplateCoreRowBaselines(const std::array<float, kNameplateCoreRowCount> &aHeights)
{
	std::array<float, kNameplateCoreRowCount> aBaselines{};
	float Position = 0.0f;
	for(size_t RowIndex = 0; RowIndex < aHeights.size(); ++RowIndex)
	{
		aBaselines[RowIndex] = Position;
		Position -= aHeights[RowIndex];
	}
	// 方向键只使用强弱钩默认大小的间距（24 + 5），不随其开关或大小变化。
	aBaselines[static_cast<size_t>(ENameplateCoreRow::KEYS)] = aBaselines[static_cast<size_t>(ENameplateCoreRow::HOOK)] - 29.0f - aHeights[static_cast<size_t>(ENameplateCoreRow::COORDS)];
	return aBaselines;
}

inline float QmNameplatePreviewContentSpan(std::array<float, kNameplateCoreRowCount> aHeights)
{
	// 为两项保留最大尺寸空间：强弱钩为 38 + 5，方向键为 38 * 1.5。
	// 开关和缩放只改变对应图标，不能带动预览框及另一项整体移动。
	aHeights[static_cast<size_t>(ENameplateCoreRow::HOOK)] = 43.0f;
	aHeights[static_cast<size_t>(ENameplateCoreRow::KEYS)] = 57.0f;
	const auto aBaselines = QmNameplateCoreRowBaselines(aHeights);
	float Span = 0.0f;
	for(size_t RowIndex = 0; RowIndex < aHeights.size(); ++RowIndex)
		Span = std::max(Span, -aBaselines[RowIndex] + aHeights[RowIndex]);
	return Span;
}

#endif
