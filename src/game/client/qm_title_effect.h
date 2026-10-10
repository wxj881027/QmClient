// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_QM_TITLE_EFFECT_H
#define GAME_CLIENT_QM_TITLE_EFFECT_H

#include <base/color.h>
#include <base/vmath.h>

// 头衔的空间效果档位。0 也是默认档，因此「抛光」不需要任何迁移就能成为现状。
// 取值加前缀：本枚举在名牌、菜单、配置三处都可能被引用，裸名（SOLID/OFF 等）极易与其它枚举冲突。
enum EQmTitleEffect : int
{
	QM_TITLE_EFFECT_POLISHED = 0, // 投影 + 同色柔光 + 高光浮雕 + 逐字掠光
	QM_TITLE_EFFECT_SOLID = 1, // 只留一层低位投影，字形本身完全通透
	QM_TITLE_EFFECT_CLASSIC = 2, // 旧版 Calamity 语义：8 方向固定描边 + 圆周加法辉光
	QM_TITLE_EFFECT_OFF = 3, // 不加任何空间效果，颜色动画照常
};

// 头衔「抛光」档的绘制参数。
//
// 与旧档的区别在于没有任何固定的深色描边：柔光与浮雕都用文字自身的颜色提亮，投影只在文字够亮时才补上
// （强度按亮度缩放）。因此深色文字不会糊成一团黑，浅色文字也不会被灰边压脏。
struct SQmTitlePolishStyle
{
	ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	float m_TextAlpha = 1.0f; // 逐字符颜色已烘焙时用于统一施加名牌淡入淡出
	ColorRGBA m_ShadowColor = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	float m_ShadowAlpha = 0.5f; // 投影强度上限，实际强度再乘以文字亮度
	vec2 m_ShadowOffset = vec2(1.0f, 1.0f);
	float m_GlowAlpha = 0.16f;
	float m_GlowRadius = 2.0f;
	float m_HighlightAlpha = 0.30f; // 高光浮雕（本体色提亮后向上偏移一像素）
};

// 旧版 Calamity 语义的绘制参数（描边 + 圆周加法辉光）。
struct SQmTitleEffectStyle
{
	ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	ColorRGBA m_OutlineColor = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	ColorRGBA m_BloomColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	float m_OutlineRadius = 0.0f; // 0 表示不描边
	float m_BloomRadius = 0.0f; // 辉光基础半径（像素）
	float m_BloomPulse = 0.0f; // 呼吸附加半径（可正可负，与 Calamity 一致）
	float m_BloomRotation = 0.0f; // 整圈旋转角（弧度）
	float m_BloomAlpha = 0.0f; // 0 表示不发光
	int m_BloomDraws = 0; // 辉光副本份数
};

// 辉光与文字共用最终透明度，避免 spec 淡出后只剩固定亮度的模糊副本。
inline ColorRGBA QmTitleEffectBloomColor(const SQmTitleEffectStyle &Style)
{
	return Style.m_BloomColor.WithMultipliedAlpha(Style.m_BloomAlpha * Style.m_TextColor.a);
}

#endif
