#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_BUBBLE_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_BUBBLE_GEOMETRY_H

#include <base/vmath.h>

#include <engine/gfx/viewport.h>

#include <array>
#include <cmath>
#include <limits>
#include <optional>

inline bool QmChatBubbleFinite(vec2 Value)
{
	return std::isfinite(Value.x) && std::isfinite(Value.y);
}

inline bool QmChatBubbleValidTextSize(vec2 Size)
{
	return QmChatBubbleFinite(Size) && Size.x >= 0.0f && Size.y >= 0.0f;
}

inline std::optional<vec2> QmChatBubbleProjectAnchor(const std::array<float, 4> &aWorld, const std::array<float, 4> &aInterface, vec2 WorldAnchor)
{
	if(!GraphicsViewport::IsValidScreen(aWorld) || !GraphicsViewport::IsValidScreen(aInterface) || !QmChatBubbleFinite(WorldAnchor))
		return std::nullopt;
	const float X = (WorldAnchor.x - aWorld[0]) / (aWorld[2] - aWorld[0]);
	const float Y = (WorldAnchor.y - aWorld[1]) / (aWorld[3] - aWorld[1]);
	const vec2 Result(aInterface[0] + X * (aInterface[2] - aInterface[0]), aInterface[1] + Y * (aInterface[3] - aInterface[1]));
	return QmChatBubbleFinite(Result) ? std::make_optional(Result) : std::nullopt;
}

// 先在可表示的字符域内截断，再转整数；保留原float加减0.5的舍入步骤。
inline std::optional<int> QmChatBubbleVisibleCharacters(float Animated, int Count)
{
	if(!std::isfinite(Animated) || Count < 0)
		return std::nullopt;
	const float Adjusted = Animated > 0.0f ? Animated + 0.5f : Animated - 0.5f;
	if(Adjusted <= 0.0f)
		return 0;
	if(static_cast<double>(Adjusted) >= Count)
		return Count;
	return static_cast<int>(Adjusted);
}

// 与文字引擎当前的像素比例和最小字号规则保持一致，只拒绝不可安全取整的输入。
inline bool QmChatBubbleTextSizeSafe(float FontSize, float ViewHeight, int PixelHeight)
{
	if(!std::isfinite(FontSize) || FontSize < 0.0f || !std::isfinite(ViewHeight) || ViewHeight <= 0.0f)
		return false;
	const float SafeHeight = PixelHeight > 0 ? static_cast<float>(PixelHeight) : 1.0f;
	const float Scale = ViewHeight >= 1.0f ? SafeHeight / ViewHeight : 1.0f;
	const float Pixels = FontSize * Scale;
	if(!std::isfinite(Pixels) || static_cast<double>(Pixels) >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0)
		return false;
	const int Actual = maximum(1, static_cast<int>(Pixels));
	return std::isfinite(Actual / Scale);
}

struct SQmChatBubbleGeometry
{
	vec2 m_Position;
	vec2 m_Size;
	vec2 m_ContentPosition;
	vec2 m_ContentSize;
};

inline std::optional<SQmChatBubbleGeometry> QmChatBubbleGeometry(vec2 Anchor, vec2 TextSize, float Scale, float Slide, float Gap, bool UnscaledContent)
{
	if(!QmChatBubbleFinite(Anchor) || !QmChatBubbleValidTextSize(TextSize) || !std::isfinite(Scale) || Scale <= 0.0f || !std::isfinite(Slide) || !std::isfinite(Gap))
		return std::nullopt;
	constexpr float Padding = 12.0f;
	SQmChatBubbleGeometry Result;
	Result.m_Size = vec2((TextSize.x + Padding * 2.0f) * Scale, (TextSize.y + Padding * 2.0f) * Scale);
	Result.m_Position = vec2(Anchor.x - Result.m_Size.x / 2.0f, Anchor.y - Gap - Result.m_Size.y + Slide);
	Result.m_ContentSize = UnscaledContent ? TextSize : TextSize * Scale;
	Result.m_ContentPosition = Result.m_Position + (Result.m_Size - Result.m_ContentSize) / 2.0f;
	if(!QmChatBubbleFinite(Result.m_Size) || !QmChatBubbleFinite(Result.m_Position) || !QmChatBubbleFinite(Result.m_ContentSize) ||
		!QmChatBubbleFinite(Result.m_ContentPosition) || !QmChatBubbleFinite(Result.m_Position + Result.m_Size) ||
		!QmChatBubbleFinite(Result.m_ContentPosition + Result.m_ContentSize))
		return std::nullopt;
	return Result;
}

#endif
