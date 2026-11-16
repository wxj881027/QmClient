#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_TOUCH_RULES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_TOUCH_RULES_H

#include <algorithm>

// 手机端触摸判定的纯规则：抽出来单独测，避免把阈值散落在状态机里。
namespace qm_mobile_touch
{
	// 面板外「点一下关闭」：位移够小、按住够短才算点击，滑动与系统手势不触发。
	inline bool OutsideTapClosesPanel(float MovedNormalized, long long HeldMilliseconds)
	{
		return MovedNormalized < 0.02f && HeldMilliseconds >= 0 && HeldMilliseconds < 400;
	}

	// 只在「恰好一根手指、且起手点在面板外」时才记候选：多指手势（三指截图）不参与。
	inline bool ShouldArmOutsideTap(int FingerCount, bool InsidePanel)
	{
		return FingerCount == 1 && !InsidePanel;
	}

	// HUD 编辑器缩放：按指针到元素中心的距离比例缩放，并夹在允许区间内。
	inline int ScaledPercentByDistance(int StartPercent, float StartDistance, float Distance, int Minimum, int Maximum)
	{
		if(StartDistance <= 0.0f)
			return std::clamp(StartPercent, Minimum, Maximum);
		const float Ratio = Distance / StartDistance;
		const int Scaled = static_cast<int>(static_cast<float>(StartPercent) * Ratio + 0.5f);
		return std::clamp(Scaled, Minimum, Maximum);
	}
} // namespace qm_mobile_touch

#endif
