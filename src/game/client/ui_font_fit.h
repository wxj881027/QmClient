// 标签「缩字号到放得下」的唯一算法：二分夹逼 + 档位量化。
// 从 ui.cpp 抽出来是为了让它能被单测直接覆盖（文本渲染器本身不便进单测）。
#ifndef GAME_CLIENT_UI_FONT_FIT_H
#define GAME_CLIENT_UI_FONT_FIT_H

#include <algorithm>
#include <cmath>

// 求 [Minimum, StartSize] 上满足 Measure(Size) <= MaxWidth 的最大字号，结果落在
// GridStep 网格上。前置条件由调用方保证：Minimum 可行、StartSize 不可行。
//
// 档位只是精度，不是循环步数：逐档递减要试 (StartSize-Minimum)/Step 次，二分只要
// log2 次，所以把档位从 1 细化到 0.5 反而更省测量（TextWidth 是唯一的开销）。
// 最多 16 轮：2^16 的细分远超任何实际字号区间，也防住异常字号把循环拖长。
template<typename TMeasure>
float ResolveFittedFontSize(const float StartSize, const float Minimum, const float MaxWidth, const float GridStep, TMeasure &&Measure)
{
	const float Step = GridStep > 0.0f ? GridStep : 1.0f;
	float Lo = Minimum; // 已知可行
	float Hi = StartSize; // 已知不可行
	for(int Iteration = 0; Iteration < 16 && Hi - Lo > Step + 0.0001f; ++Iteration)
	{
		// 候选必须先落到档位网格：任意浮点中间档会生成一批「一次性」物理字号
		// （ActualSize = round(字号 × 屏幕缩放)），每个新字号都要重新光栅化该标签的
		// 全部字形并永久留在字形缓存里。网格化后整条链只在固定的几档之间取值。
		const float Candidate = std::floor(0.5f * (Lo + Hi) / Step) * Step;
		if(Candidate <= Lo + 0.0001f)
			break; // 网格里已经没有新的可试探点
		if(Measure(Candidate) <= MaxWidth + 0.001f)
			Lo = Candidate;
		else
			Hi = Candidate;
	}
	// Lo 已经是网格值（或原样等于非网格的下限）。再复核一次，
	// 防止下限那档的 STOP/ELLIPSIS 判据与前面试探用的宽度口径不一致。
	float Result = Lo;
	if(Result > Minimum + 0.0001f && Measure(Result) > MaxWidth + 0.001f)
		Result = std::max(Minimum, Result - Step);
	return std::clamp(Result, Minimum, StartSize);
}

#endif
