// 随包图标按实际像素字号逐项预热；调用者控制每步数量与时间预算。
#ifndef GAME_CLIENT_QM_ICON_PREWARM_H
#define GAME_CLIENT_QM_ICON_PREWARM_H

#include "qm_icon.h"

#include <array>
#include <cmath>

class CQmIconPrewarmPlan
{
	static constexpr std::array<int, 12> UI_SIZES = {6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36};
	std::array<int, UI_SIZES.size() * 3> m_aPixelSizes{};
	int m_SizeCount = 0;
	int m_SizeIndex = 0;
	int m_IconIndex = 0;
	int m_Weight = -1;

public:
	// 尺寸列表相同就不重启队列；微小 DPI 浮点变化不产生重复预热。
	bool Configure(float PixelsPerUiUnit, int Weight)
	{
		if(!std::isfinite(PixelsPerUiUnit) || PixelsPerUiUnit <= 0.0f)
			return false;
		std::array<int, UI_SIZES.size() * 3> Sizes{};
		int Count = 0;
		for(const int Size : UI_SIZES)
		{
			// 直接图标字号、DrawQmIcon 的方框 0.8、QmUi 按钮的 0.58*0.8。
			// 先按实际调用顺序计算逻辑字号，再向下取整成文字引擎使用的像素档。
			for(const float FontSize : {static_cast<float>(Size), Size * 0.8f, (Size * 0.58f) * 0.8f})
				Sizes[Count++] = QmIconRasterPixelSize(FontSize, PixelsPerUiUnit);
		}
		std::sort(Sizes.begin(), Sizes.begin() + Count);
		Count = static_cast<int>(std::unique(Sizes.begin(), Sizes.begin() + Count) - Sizes.begin());
		std::fill(Sizes.begin() + Count, Sizes.end(), 0);
		const int NormalizedWeight = NormalizeQmIconWeight(Weight);
		if(m_Weight == NormalizedWeight && m_SizeCount == Count && m_aPixelSizes == Sizes)
			return false;
		m_aPixelSizes = Sizes;
		m_SizeCount = Count;
		m_Weight = NormalizedWeight;
		m_SizeIndex = m_IconIndex = 0;
		return true;
	}

	bool Next(int &Codepoint, int &PixelSize)
	{
		if(m_SizeIndex >= m_SizeCount)
			return false;
		Codepoint = CQmIconRegistry::Codepoint(static_cast<EQmIcon>(m_IconIndex));
		PixelSize = m_aPixelSizes[m_SizeIndex];
		if(++m_IconIndex == static_cast<int>(EQmIcon::COUNT))
		{
			m_IconIndex = 0;
			++m_SizeIndex;
		}
		return true;
	}

	bool Complete() const { return m_SizeIndex >= m_SizeCount; }
	void Invalidate() { m_Weight = -1; }
};

#endif
