#ifndef GAME_CLIENT_COMPONENTS_NAMEPLATE_TEXT_EFFECTS_H
#define GAME_CLIENT_COMPONENTS_NAMEPLATE_TEXT_EFFECTS_H

#include <game/client/render.h>

#include <algorithm>

// QmClient：名牌文字特效的每帧绘制预算与自适应档位（全部为纯函数，便于单测）。
// 货币单位是「一次文字绘制」，即一次 TextRender()->RenderTextContainer 调用：
// 辉光每一圈画 4 个方向，描边每一圈画 8 个方向，本体固定 1 次且永远保留。
inline constexpr int QM_NAMEPLATE_EFFECT_GLOW_DRAWS_PER_PASS = 4;
inline constexpr int QM_NAMEPLATE_EFFECT_BORDER_DRAWS_PER_PASS = 8;
inline constexpr int QM_NAMEPLATE_EFFECT_GLOW_MAX_PASSES = 6;
inline constexpr int QM_NAMEPLATE_EFFECT_BORDER_MAX_PASSES = 4;
// 同屏名牌数的抖动死区与每帧跟随步长：死区内不动，上升快（尽快保护帧率），下降慢（避免特效闪烁）。
inline constexpr int QM_NAMEPLATE_EFFECT_LOD_DEAD_ZONE_NAMEPLATES = 2;
inline constexpr int QM_NAMEPLATE_EFFECT_LOD_STEP_UP_NAMEPLATES = 8;
inline constexpr int QM_NAMEPLATE_EFFECT_LOD_STEP_DOWN_NAMEPLATES = 2;

inline float QmNameplateTextEffectPadding(int Effects, int BorderRange, int GlowRange)
{
	float Padding = 0.0f;
	if((Effects & QM_TEXT_EFFECT_BORDER) != 0)
		Padding = (float)std::clamp(BorderRange, 1, 4);
	if((Effects & QM_TEXT_EFFECT_GLOW) != 0)
		Padding = std::max(Padding, (float)std::clamp(GlowRange, 1, 12));
	return Padding;
}

// 满档特效绘制次数（不含本体），必须与 RenderTextContainerWithEffects 里的圈数上限一致。
inline int QmNameplateEffectFullDraws(int Effects, int BorderRange, int GlowRange)
{
	int Draws = 0;
	if((Effects & QM_TEXT_EFFECT_GLOW) != 0)
		Draws += QM_NAMEPLATE_EFFECT_GLOW_DRAWS_PER_PASS * std::clamp(GlowRange, 1, QM_NAMEPLATE_EFFECT_GLOW_MAX_PASSES);
	if((Effects & QM_TEXT_EFFECT_BORDER) != 0 && BorderRange > 1)
		Draws += QM_NAMEPLATE_EFFECT_BORDER_DRAWS_PER_PASS * std::clamp(BorderRange, 1, QM_NAMEPLATE_EFFECT_BORDER_MAX_PASSES);
	return Draws;
}

// 平滑同屏名牌数：屏幕边缘单个玩家进出造成的 1~2 个名牌抖动不该改变档位，
// 真实变化（缩放镜头）按每帧步长跟上。
inline int QmNameplateEffectLodSmoothCount(int PreviousCount, int CurrentCount, int DeadZone, int MaxStepUp, int MaxStepDown)
{
	const int Delta = CurrentCount - PreviousCount;
	if(Delta <= DeadZone && Delta >= -DeadZone)
		return PreviousCount;
	if(Delta > 0)
		return std::min(CurrentCount, PreviousCount + MaxStepUp);
	return std::max(CurrentCount, PreviousCount - MaxStepDown);
}

// 理想档位：名牌数不超过满档阈值时返回满档绘制次数（一个绘制都不少），
// 超过后把「满档阈值对应的特效绘制预算」按人数比例摊到每个名牌文本行上。
// 由于按人均摊，每帧特效绘制总量不会超过「满档时 FullQualityNameplates 个名牌」的开销。
// 向上取整：人数再多也至少留下最内圈一层特效。
inline int QmNameplateEffectLodIdealDraws(int SmoothedCount, int FullDraws, int FullQualityNameplates)
{
	if(FullDraws <= 0 || FullQualityNameplates <= 0)
		return FullDraws;
	if(SmoothedCount <= FullQualityNameplates)
		return FullDraws;
	const int Scaled = (FullDraws * FullQualityNameplates + SmoothedCount - 1) / SmoothedCount;
	return std::max(1, std::min(FullDraws, Scaled));
}

// 档位换算成实际圈数：本体永远画；预算不足时先砍外层辉光（外层 alpha 最低、视觉贡献最小），
// 再砍外层描边，留下的都是最内圈。
struct SQmNameplateEffectPasses
{
	int m_BorderPasses = 0;
	int m_GlowPasses = 0;
};

inline SQmNameplateEffectPasses QmNameplateEffectResolvePasses(int MaxEffectDraws, int BorderPasses, int GlowPasses)
{
	SQmNameplateEffectPasses Result;
	if(MaxEffectDraws < 0)
	{
		Result.m_BorderPasses = BorderPasses;
		Result.m_GlowPasses = GlowPasses;
		return Result;
	}
	int Remaining = MaxEffectDraws;
	Result.m_BorderPasses = std::min(BorderPasses, Remaining / QM_NAMEPLATE_EFFECT_BORDER_DRAWS_PER_PASS);
	Remaining -= Result.m_BorderPasses * QM_NAMEPLATE_EFFECT_BORDER_DRAWS_PER_PASS;
	Result.m_GlowPasses = std::min(GlowPasses, Remaining / QM_NAMEPLATE_EFFECT_GLOW_DRAWS_PER_PASS);
	return Result;
}

#endif
