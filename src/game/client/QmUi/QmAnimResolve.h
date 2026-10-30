// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMANIMRESOLVE_H
#define GAME_CLIENT_QMUI_QMANIMRESOLVE_H

#include "QmAnim.h"

#include <base/color.h>

#include <cstdint>

class CUIRect;

// Stable hash combiner for animation NodeKeys. Builds a 64-bit identity from a
// caller-chosen scope (typically str_quickhash("page_name")) and a per-element
// pointer.
uint64_t BuildUiAnimNodeKey(uint64_t ScopeHash, uint64_t Id);

// Drives a single float property toward Target through the v2 animation
// runtime using MERGE_TARGET interrupt policy. The runtime owns the per-node
// last-target cache, so new requests are only issued when Target changes or the
// value is out of sync. Returns the current frame value (already advanced).
float ResolveUiAnimValue(
	CUiV2AnimationRuntime &AnimRuntime,
	uint64_t NodeKey,
	EUiAnimProperty Property,
	float Target,
	float DurationSec,
	EEasing Easing);
// 带 start-delay 的变体：同一帧批量启动多条轨道时按序错开（stagger 入场）。
// 延迟期间值保持在起点，轨道仍视为活跃。
float ResolveUiAnimSpringValue(
	CUiV2AnimationRuntime &AnimRuntime,
	uint64_t NodeKey,
	EUiAnimProperty Property,
	float Target,
	const SUiSpringConfig &Spring,
	int Priority = 1);

// Convenience wrappers: drive four channels of a Rect or Color in lockstep.
// Each component is an independent track keyed by (NodeKey, EUiAnimProperty),
// so MERGE_TARGET continuity behaviour holds per-component.
CUIRect ResolveUiAnimValueRect(
	CUiV2AnimationRuntime &AnimRuntime,
	uint64_t NodeKey,
	const CUIRect &Target,
	float DurationSec,
	EEasing Easing);
CUIRect ResolveUiAnimSpringRectXY(
	CUiV2AnimationRuntime &AnimRuntime,
	uint64_t NodeKey,
	const CUIRect &Target,
	const SUiSpringConfig &Spring,
	int Priority = 1);
ColorRGBA ResolveUiAnimValueColor(
	CUiV2AnimationRuntime &AnimRuntime,
	uint64_t NodeKey,
	const ColorRGBA &Target,
	float DurationSec,
	EEasing Easing);
ColorRGBA ResolveUiAnimInterpolatedColor(
	const ColorRGBA &From,
	const ColorRGBA &To,
	float Amount);

// Presentation State resolver for interruptible, redirectable UI surfaces.
// The caller updates Target from business state; this helper keeps a single
// spring track per property and lets MERGE_TARGET preserve velocity continuity.
// 目标缓存由传入运行时持有，随 Reset 清理；Epsilon 控制目标变更阈值。
float ResolveUiPresentationStateValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, const SUiSpringConfig &Spring, int Priority = 2, float Epsilon = 0.01f);
void SetUiPresentationStateValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Value);

#endif
