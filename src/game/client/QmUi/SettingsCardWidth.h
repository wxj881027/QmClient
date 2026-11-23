#ifndef GAME_CLIENT_QMUI_SETTINGSCARDWIDTH_H
#define GAME_CLIENT_QMUI_SETTINGSCARDWIDTH_H

#include "QmAnimResolve.h"
#include "SettingsCardGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

inline int ResolveSettingsCardLayoutColumn(int Column, bool FullWidth, bool DefaultRightColumn)
{
	// 排序中的全宽槽位收窄后按实际左右列排布，不再占用另一列的高度。
	return Column == 0 && !FullWidth ? (DefaultRightColumn ? 2 : 1) : Column;
}

inline CUIRect SettingsCardWidthButtonRect(const SSettingsCardFrame &Frame)
{
	CUIRect Rect = Frame.m_HandleRect;
	const float Right = std::max(Frame.m_HeaderRect.x, Rect.x - 4.0f);
	Rect.w = std::min(Rect.w, std::max(0.0f, Right - Frame.m_HeaderRect.x));
	Rect.x = Right - Rect.w;
	if(Rect.w <= 0.0f)
		Rect.h = 0.0f;
	return Rect;
}

// 卡片宽度的横向过渡：只补间槽位的水平位置与宽度，纵向仍由列排布和
// 内容高度过渡负责。目标变化时从当前动画值续接，切换方向不会跳回起点。
class CSettingsCardWidthMotion
{
public:
	CUIRect Update(CUiV2AnimationRuntime *pAnim, uint64_t NodeKey, const CUIRect &Target, float DurationSec, bool Snap)
	{
		const bool InitializedThisFrame = !m_Initialized;
		const bool TargetChanged = m_Initialized &&
					   (std::abs(Target.x - m_LastTargetX) > 0.01f || std::abs(Target.w - m_LastTargetW) > 0.01f);
		CUIRect Out = Target;
		if(pAnim == nullptr)
		{
			m_Initialized = true;
			m_WasActive = false;
		}
		else if(InitializedThisFrame || !m_HasRuntime)
		{
			pAnim->SetValue(NodeKey, EUiAnimProperty::POS_X, Target.x);
			pAnim->SetValue(NodeKey, EUiAnimProperty::WIDTH, Target.w);
			m_Initialized = true;
			m_WasActive = false;
		}
		else if(Snap || DurationSec <= 0.0f)
		{
			if(TargetChanged || m_WasActive)
			{
				pAnim->SetValue(NodeKey, EUiAnimProperty::POS_X, Target.x);
				pAnim->SetValue(NodeKey, EUiAnimProperty::WIDTH, Target.w);
			}
			m_WasActive = false;
		}
		else if(TargetChanged || m_WasActive)
		{
			Out.x = ResolveUiAnimValue(*pAnim, NodeKey, EUiAnimProperty::POS_X, Target.x, DurationSec, EEasing::EASE_OUT_QUART);
			Out.w = ResolveUiAnimValue(*pAnim, NodeKey, EUiAnimProperty::WIDTH, Target.w, DurationSec, EEasing::EASE_OUT_QUART);
			m_WasActive = pAnim->HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X) ||
				      pAnim->HasActiveAnimation(NodeKey, EUiAnimProperty::WIDTH);
		}
		else
		{
			m_WasActive = false;
		}
		m_LastTargetX = Target.x;
		m_LastTargetW = Target.w;
		m_HasRuntime = pAnim != nullptr;
		return Out;
	}

	void Invalidate()
	{
		m_Initialized = false;
		m_WasActive = false;
		m_HasRuntime = false;
	}

	bool IsActive() const { return m_WasActive; }

private:
	float m_LastTargetX = 0.0f;
	float m_LastTargetW = 0.0f;
	bool m_Initialized = false;
	bool m_WasActive = false;
	bool m_HasRuntime = false;
};

struct SSettingsCardWidthPlacement
{
	int m_Column;
	bool m_FullWidth;
	CUIRect m_Rect;
	// 切换前实际布局用于保留跨列先后关系；首次布局使用当前矩形。
	CUIRect m_ReferenceRect{};
	bool m_HasReference = false;
	size_t m_Band = 0;
	const CUIRect &ReferenceRect() const { return m_HasReference ? m_ReferenceRect : m_Rect; }
};

// 宽度不改变列内顺序。跨列卡以原位置划分空位，放不下的邻卡向下让位。
class CSettingsCardWidthLayout
{
	std::vector<size_t> m_vFullWidth;

public:
	void Arrange(std::vector<SSettingsCardWidthPlacement> &vCards, float CardGap)
	{
		m_vFullWidth.clear();
		for(size_t i = 0; i < vCards.size(); ++i)
			if(vCards[i].m_FullWidth || vCards[i].m_Column == 0)
				m_vFullWidth.push_back(i);
		if(vCards.empty())
			return;
		std::sort(m_vFullWidth.begin(), m_vFullWidth.end(), [&](size_t Left, size_t Right) {
			return vCards[Left].ReferenceRect().y != vCards[Right].ReferenceRect().y ? vCards[Left].ReferenceRect().y < vCards[Right].ReferenceRect().y : Left < Right;
		});
		CardGap = std::max(0.0f, CardGap);
		// 先按原几何确定所属空位，再写回新坐标，避免重排影响后续判断。
		for(auto &Card : vCards)
		{
			Card.m_Band = 0;
			while(Card.m_Band < m_vFullWidth.size() && Card.ReferenceRect().y + Card.ReferenceRect().h + CardGap > vCards[m_vFullWidth[Card.m_Band]].ReferenceRect().y + 0.001f)
				++Card.m_Band;
		}
		float Top = vCards.front().m_Rect.y;
		for(const auto &Card : vCards)
			Top = std::min(Top, Card.m_Rect.y);
		std::array<float, 2> aCursor{Top, Top};
		for(size_t Band = 0; Band <= m_vFullWidth.size(); ++Band)
		{
			for(auto &Card : vCards)
			{
				if(Card.m_Column == 0 || Card.m_FullWidth || Card.m_Band != Band)
					continue;
				float &Cursor = aCursor[Card.m_Column - 1];
				Card.m_Rect.y = std::max(Card.m_Rect.y, Cursor);
				Cursor = Card.m_Rect.y + Card.m_Rect.h + CardGap;
			}
			if(Band < m_vFullWidth.size())
			{
				auto &Card = vCards[m_vFullWidth[Band]];
				Card.m_Rect.y = std::max({Card.m_Rect.y, aCursor[0], aCursor[1]});
				aCursor.fill(Card.m_Rect.y + Card.m_Rect.h + CardGap);
			}
		}
	}
};

#endif
