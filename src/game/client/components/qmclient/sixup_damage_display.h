#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SIXUP_DAMAGE_DISPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SIXUP_DAMAGE_DISPLAY_H

#include <base/math.h>

#include <cmath>
#include <limits>

// 只处理显示分组和角度，事件位置、身份与协议字段仍由翻译入口保留。
template<typename FEmit>
void QmForEachSixupDamageAngle(int Amount, int &DamageTaken, float &PreviousTime, float LocalTime, FEmit &&Emit)
{
	// 0.7 已校验的生命和护甲伤害各至多9点。
	if(Amount < 1 || Amount > 18)
		return;

	float Angle;
	if(LocalTime < PreviousTime + 0.5f)
	{
		// 计数到达类型上界后保持，间隔结束仍沿原规则重置分组。
		if(DamageTaken < std::numeric_limits<int>::max())
			++DamageTaken;
		Angle = DamageTaken * 0.25f;
	}
	else
	{
		DamageTaken = 0;
		Angle = 0;
	}
	PreviousTime = LocalTime;

	const float A = 3 * pi / 2 + Angle;
	const float Start = A - pi / 3;
	const float End = A + pi / 3;
	for(int Index = 0; Index < Amount; ++Index)
	{
		const float F = mix(Start, End, float(Index + 1) / float(Amount + 2));
		const float Encoded = F * 256.0f;
		// 比较时拓宽，避免INT_MAX舍入到2^31后漏过非法整数转换。
		if(!std::isfinite(Encoded) || static_cast<double>(Encoded) < std::numeric_limits<int>::min() || static_cast<double>(Encoded) > std::numeric_limits<int>::max())
			continue;
		Emit(static_cast<int>(Encoded));
	}
}

#endif
