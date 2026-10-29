// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMICONSWAP_H
#define GAME_CLIENT_QMUI_QMICONSWAP_H

#include "UiContext.h"

#include <base/color.h>

#include <game/client/qm_icon.h>

#include <cstdint>

// 图标交换对：同一位置在 A/B 两个图标之间切换时优先用离线烘焙的帧条做形变，
// 该字重没有资产时回落到「smoothstep alpha + 反向缩放」的交叉淡化。
// 帧条几何（画布边长、帧数、UV 内缩）与密码图标共用 QmPasswordIcon.h。
enum class EQmIconSwapPair
{
	EYE = 0,
	CHEVRON = 1,
	PLAY_PAUSE = 2,
	PLAY_STOP = 3,
	SQUARE_PLUS_MINUS = 4,
	STAR_FAVORITE = 5,
	COUNT,
};

namespace ui_widget
{
	// 归一化字重 -> 源资产名；该字重没有形变资产（Fill）时返回 nullptr。
	// 资产只有一份 64px 源帧条，运行时按目标绘制边长重采样成 1:1 纹理。
	inline const char *IconSwapAssetName(EQmIconSwapPair Pair, int NormalizedWeight)
	{
		switch(NormalizedWeight)
		{
		case 0:
		case 1:
		case 2:
		case 4:
		{
			const bool Bold = NormalizedWeight == 1;
			const bool Regular = NormalizedWeight == 0;
			switch(Pair)
			{
			case EQmIconSwapPair::CHEVRON:
				return Bold ? "qmclient/ui/chevron_swap_bold.png" : Regular ? "qmclient/ui/chevron_swap_regular.png" : "qmclient/ui/chevron_swap_light.png";
			case EQmIconSwapPair::PLAY_PAUSE:
				// play/pause 轮廓配对形变：三角形顶点分裂成双杠；Fill 是实心形，跳过。
				return Bold ? "qmclient/ui/play_pause_swap_bold.png" : Regular ? "qmclient/ui/play_pause_swap_regular.png" : "qmclient/ui/play_pause_swap_light.png";
			case EQmIconSwapPair::PLAY_STOP:
				// play/stop：三角形 ↔ 圆角方块，整字形配对（顶点展开成方块右缘）。
				return Bold ? "qmclient/ui/play_stop_swap_bold.png" : Regular ? "qmclient/ui/play_stop_swap_regular.png" : "qmclient/ui/play_stop_swap_light.png";
			case EQmIconSwapPair::SQUARE_PLUS_MINUS:
				// 加/减方框：外框常量，加号竖臂压平进横臂。
				return Bold ? "qmclient/ui/plus_minus_swap_bold.png" : Regular ? "qmclient/ui/plus_minus_swap_regular.png" : "qmclient/ui/plus_minus_swap_light.png";
			case EQmIconSwapPair::STAR_FAVORITE:
				// 收藏点亮：描边星 ↔ Fill 字面实心星（跨字面配对）；Fill 字重恒实心无切换。
				return Bold ? "qmclient/ui/star_favorite_swap_bold.png" : Regular ? "qmclient/ui/star_favorite_swap_regular.png" : "qmclient/ui/star_favorite_swap_light.png";
			case EQmIconSwapPair::EYE:
				return Bold ? "qmclient/ui/password_icon_bold.png" : Regular ? "qmclient/ui/password_icon_regular.png" : "qmclient/ui/password_icon_light.png";
			default:
				return nullptr;
			}
		}
		default:
			// Fill 的 eye 是实心眼眶加孔、chevron 是实心块，语义分解不成立，回落到交叉淡化。
			return nullptr;
		}
	}

	// Progress 0 = 第一个图标，1 = 第二个图标。调用方负责用 spring/tween 求解进度；
	// 本函数只决定「画帧条还是画交叉淡化」，不推进任何轨道。
	// FontSize 必须是静止态字形的字号（DoLabel / DrawQmIcon 实际用的那个值，即
	// QmIconFontSize(图标盒)），Rect 必须是静止态字形居中的区域。帧条按同一字号换算
	// 画布边长（FontSize * 1152 / 1024），字号或盒选错会让过渡期的图标比静止态大一圈。
	bool DrawIconSwap(const IUiContext &Ctx, const CUIRect &Rect, EQmIconSwapPair Pair, float FontSize, float Progress, const ColorRGBA &Color);

	// 推进一次图标交换的过渡轨道，返回 0..1 进度。
	// - 组件内部一律走「Ctx.m_pAnim 为空则回退 CUi 全局运行时」：设置卡片的内容趟是在
	//   BeginBackgroundRender(true) 里画的，SettingsUiContext 见到 RenderOnly() 就把
	//   m_pAnim 置空，照字面用 Ctx.m_pAnim 会让过渡在设置页永远不执行（点一下只看到瞬切）。
	// - 只读/测量趟（AnimationsBlocked）不推进轨道，只读当前值：一帧内被画多次时每次
	//   都必须拿到同一个进度，否则两层会停在相邻的两帧上。
	// - *pActiveOut 报告轨道是否仍在动。调用方必须据此在「帧条」和「静止态字形」之间
	//   二选一，两趟各画一种就会叠出重影。
	// - AutoSpring：有烘焙资产的 pair 用弹簧（帧条自带呼吸感）；没有资产时要走交叉淡化，
	//   弹簧的快起步会在头几帧把两个字形同时推上台，观感是突兀地糊一下，必须留补间。
	float ResolveIconSwapProgress(const IUiContext &Ctx, uint64_t NodeKey, EQmIconSwapPair Pair, float Target, bool *pActiveOut = nullptr, float Seconds = 0.26f, bool AutoSpring = false);

	// 一次交换的完整绘制参数：两端静止态字形 + 当前进度。由 ResolveIconSwapBinding 一次性
	// 算好，再交给 DrawIconSwapBinding（可直接当 ui_widget::DoIconButton 的图标回调用）。
	struct SIconSwapBinding
	{
		uint64_t m_NodeKey = 0;
		EQmIconSwapPair m_Pair = EQmIconSwapPair::CHEVRON;
		EQmIcon m_IconA = EQmIcon::COUNT;
		EQmIcon m_IconB = EQmIcon::COUNT;
		const char *m_pGlyphA = nullptr;
		const char *m_pGlyphB = nullptr;
		float m_Progress = 0.0f;
		bool m_Animate = false;
		// A 端静止态强制描边字面：收藏点亮的 A 端（未收藏）必须是描边星，而
		// DrawQmIcon 会按 qm_ui_favorite_icon_filled 把整个 STAR 画成实心——
		// 不强制的话 A 端静止字形与帧条第 0 帧不同形，轨道停下时会跳一下。
		bool m_ForceOutlineA = false;
	};

	// Salt 用来隔离同一页面里的多条轨道（不同图标共用一个 pId 指针时不至于撞车），
	// pId 必须与按钮/标签 id 同源，这样同一控件在重绘时始终落在同一条轨道上。
	SIconSwapBinding ResolveIconSwapBinding(const IUiContext &Ctx, EQmIconSwapPair Pair, EQmIcon IconA, const char *pGlyphA, EQmIcon IconB, const char *pGlyphB, float Target, uint64_t Salt, const void *pId, float Seconds = 0.26f, bool AutoSpring = false);

	// 轨道活跃时画帧条/交叉淡化，静止时回落到两端之一的字形。
	// 帧条与字形共用 QmIconFontSize(Rect)，所以过渡期不会比静止态大一圈。
	// 函数签名可直接用于 ui_widget::SIconButtonIconDraw（按钮托管图标）。
	void DrawIconSwapBinding(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Color, const void *pUser);
} // namespace ui_widget

#endif
