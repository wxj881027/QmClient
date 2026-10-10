#ifndef GAME_CLIENT_QMUI_QMLINEINPUTMOTION_H
#define GAME_CLIENT_QMUI_QMLINEINPUTMOTION_H

#include "QmAnimationBackend.h"

#include <base/vmath.h>

#include <engine/textrender.h>

#include <array>
#include <chrono>
#include <string>
#include <vector>

// 每个输入框独占绘制状态；动画不改变文本、排版或输入法锚点。
class CQmLineInputMotion
{
public:
	static constexpr int MAX_ANIMATED_GLYPHS = 32;

	void Reset();
	void Update(const char *pText, std::chrono::nanoseconds Now, int MotionLevel, bool AnimateGlyphs = true);
	void FillCharOffsets(std::vector<STextCharOffset> &vOffsets) const;
	vec2 ResolveCaret(vec2 Target, float FontHeight, bool Snap = false, bool AnimateAcrossLines = false);

private:
	struct SCharacter
	{
		int m_Codepoint;
		int m_ByteOffset;
		uint64_t m_NodeKey = 0;
	};

	CQmAnimationBackend m_Runtime;
	std::string m_Text;
	std::vector<SCharacter> m_vCharacters;
	std::vector<SCharacter> m_vNextCharacters;
	std::array<bool, MAX_ANIMATED_GLYPHS> m_aUsedSlots{};
	std::chrono::nanoseconds m_LastUpdate{};
	vec2 m_CaretTarget = vec2(0.0f, 0.0f);
	float m_CaretHeight = 0.0f;
	int m_MotionLevel = 0;
	bool m_TextInitialized = false;
	bool m_CaretInitialized = false;

	SUiAnimTransition Transition(const SUiSpringConfig &Spring) const;
	SUiAnimTransition CaretTransition() const;
	void ClearGlyph(SCharacter &Character);
};

#endif
