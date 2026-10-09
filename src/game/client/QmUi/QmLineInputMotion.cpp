#include "QmLineInputMotion.h"

#include "QmInputMotion.h"

#include <base/system.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
	constexpr uint64_t CARET_NODE = CQmLineInputMotion::MAX_ANIMATED_GLYPHS + 1;
}

void CQmLineInputMotion::Reset()
{
	m_Runtime.Reset();
	m_Text.clear();
	m_vCharacters.clear();
	m_vNextCharacters.clear();
	m_aUsedSlots.fill(false);
	m_LastUpdate = {};
	m_CaretTarget = vec2(0.0f, 0.0f);
	m_CaretHeight = 0.0f;
	m_MotionLevel = 0;
	m_TextInitialized = false;
	m_CaretInitialized = false;
}

SUiAnimTransition CQmLineInputMotion::Transition(const SUiSpringConfig &Spring) const
{
	SUiAnimTransition Result;
	Result.m_Driver = EUiAnimDriver::SPRING;
	Result.m_Spring = Spring;
	Result.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	// 输入框和编辑器使用同一显式等级，测试也无需改写全局配置。
	Result.m_RespectMotionLevel = false;
	return qm_animation::ApplyMotionLevel(Result, m_MotionLevel);
}

SUiAnimTransition CQmLineInputMotion::CaretTransition() const
{
	SUiAnimTransition Result;
	Result.m_DurationSec = 0.08f;
	Result.m_Easing = EEasing::CUBIC_BEZIER;
	Result.m_Bezier = {0.25f, 0.1f, 0.25f, 1.0f};
	Result.m_RespectMotionLevel = false;
	return qm_animation::ApplyMotionLevel(Result, m_MotionLevel);
}

void CQmLineInputMotion::ClearGlyph(SCharacter &Character)
{
	if(Character.m_NodeKey == 0)
		return;
	m_Runtime.SetValue(Character.m_NodeKey, EUiAnimProperty::SCALE, 0.0f);
	m_aUsedSlots[static_cast<size_t>(Character.m_NodeKey - 1)] = false;
	Character.m_NodeKey = 0;
}

void CQmLineInputMotion::Update(const char *pText, std::chrono::nanoseconds Now, int MotionLevel, bool AnimateGlyphs)
{
	if(pText == nullptr)
		pText = "";
	if(m_TextInitialized)
	{
		const auto Elapsed = Now - m_LastUpdate;
		// 离开渲染或时钟回退后从当前画面恢复，不续播过期动效。
		if(Elapsed < std::chrono::nanoseconds::zero() || Elapsed > std::chrono::milliseconds(250))
			Reset();
		else
			m_Runtime.Advance(std::chrono::duration<float>(Elapsed).count());
	}
	m_LastUpdate = Now;
	const int NewMotionLevel = qm_animation::NormalizeMotionLevel(MotionLevel);
	const bool MotionChanged = NewMotionLevel != m_MotionLevel;
	m_MotionLevel = NewMotionLevel;
	for(auto &Character : m_vCharacters)
	{
		if(Character.m_NodeKey == 0)
			continue;
		if(!AnimateGlyphs || m_MotionLevel == 0 || !m_Runtime.HasActiveAnimation(Character.m_NodeKey, EUiAnimProperty::SCALE))
			ClearGlyph(Character);
		else if(MotionChanged)
			m_Runtime.RequestAnimation({Character.m_NodeKey, EUiAnimProperty::SCALE, 0.0f, Transition(qm_input_motion::GLYPH)});
	}
	if(MotionChanged && m_CaretInitialized)
	{
		for(const auto Property : {EUiAnimProperty::POS_X, EUiAnimProperty::POS_Y})
		{
			const float Target = Property == EUiAnimProperty::POS_X ? m_CaretTarget.x : m_CaretTarget.y;
			const float Current = m_Runtime.GetValue(CARET_NODE, Property, Target);
			m_Runtime.SetValue(CARET_NODE, Property, Current);
			m_Runtime.RequestAnimation({CARET_NODE, Property, Target, CaretTransition()});
		}
	}
	SUiAnimCompleteEvent Completed;
	// 输入框没有完成回调，及时消费事件，避免长时间打字积累。
	while(m_Runtime.PollCompletedEvent(Completed))
	{
	}
	if(m_TextInitialized && m_Text == pText)
		return;

	m_vNextCharacters.clear();
	for(const char *pCurrent = pText; *pCurrent != '\0';)
	{
		const int ByteOffset = static_cast<int>(pCurrent - pText);
		const int Codepoint = str_utf8_decode(&pCurrent);
		m_vNextCharacters.push_back({Codepoint, ByteOffset});
	}
	const int OldCount = static_cast<int>(m_vCharacters.size());
	const int NewCount = static_cast<int>(m_vNextCharacters.size());
	int Prefix = 0;
	while(Prefix < std::min(OldCount, NewCount) && m_vCharacters[Prefix].m_Codepoint == m_vNextCharacters[Prefix].m_Codepoint)
	{
		m_vNextCharacters[Prefix].m_NodeKey = m_vCharacters[Prefix].m_NodeKey;
		++Prefix;
	}
	int Suffix = 0;
	while(Suffix < std::min(OldCount, NewCount) - Prefix &&
		m_vCharacters[OldCount - Suffix - 1].m_Codepoint == m_vNextCharacters[NewCount - Suffix - 1].m_Codepoint)
	{
		m_vNextCharacters[NewCount - Suffix - 1].m_NodeKey = m_vCharacters[OldCount - Suffix - 1].m_NodeKey;
		++Suffix;
	}
	for(int i = Prefix; i < OldCount - Suffix; ++i)
		ClearGlyph(m_vCharacters[i]);
	if(m_TextInitialized && AnimateGlyphs && m_MotionLevel > 0)
	{
		// 固定槽位循环复用，快速输入和长粘贴都不会无限增加动画节点。
		for(int i = Prefix; i < NewCount - Suffix; ++i)
		{
			if(m_vNextCharacters[i].m_Codepoint == '\n')
				continue;
			const auto Free = std::find(m_aUsedSlots.begin(), m_aUsedSlots.end(), false);
			if(Free == m_aUsedSlots.end())
				break;
			const uint64_t NodeKey = static_cast<uint64_t>(std::distance(m_aUsedSlots.begin(), Free)) + 1;
			*Free = true;
			m_vNextCharacters[i].m_NodeKey = NodeKey;
			m_Runtime.SetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
			m_Runtime.RequestAnimation({NodeKey, EUiAnimProperty::SCALE, 0.0f, Transition(qm_input_motion::GLYPH)});
		}
	}
	m_vCharacters.swap(m_vNextCharacters);
	m_Text = pText;
	m_TextInitialized = true;
}

void CQmLineInputMotion::FillCharOffsets(std::vector<STextCharOffset> &vOffsets) const
{
	vOffsets.clear();
	if(m_MotionLevel == 0 || std::none_of(m_aUsedSlots.begin(), m_aUsedSlots.end(), [](bool Used) { return Used; }))
		return;
	vOffsets.reserve(m_vCharacters.size());
	// 原幅度系数增加 18%，由归一化弹簧驱动从小到大的一次缩放回弹。
	const float Amplitude = (m_MotionLevel == 1 ? 0.06f : 0.18f) * 1.18f;
	for(const auto &Character : m_vCharacters)
	{
		const float Shrink = Character.m_NodeKey == 0 ? 0.0f :
			std::clamp(m_Runtime.GetValue(Character.m_NodeKey, EUiAnimProperty::SCALE), -0.2f, 1.0f) * Amplitude;
		vOffsets.emplace_back(Character.m_ByteOffset, 0.0f, 0.0f, 1.0f - Shrink);
	}
}

vec2 CQmLineInputMotion::ResolveCaret(vec2 Target, float FontHeight, bool Snap)
{
	const vec2 PreviousTarget = m_CaretTarget;
	// 跨行、布局缩放和鼠标选区立即对齐，避免光标扫过无关文字。
	Snap |= !m_CaretInitialized || m_MotionLevel == 0 ||
		std::abs(FontHeight - m_CaretHeight) > 0.1f ||
		std::abs(Target.y - m_CaretTarget.y) > FontHeight * 0.5f ||
		std::abs(Target.x - m_CaretTarget.x) > FontHeight * 4.0f;
	m_CaretTarget = Target;
	m_CaretHeight = FontHeight;
	m_CaretInitialized = true;
	if(Snap)
	{
		m_Runtime.SetValue(CARET_NODE, EUiAnimProperty::POS_X, Target.x);
		m_Runtime.SetValue(CARET_NODE, EUiAnimProperty::POS_Y, Target.y);
		return Target;
	}
	const auto Transition = CaretTransition();
	// 输入框已经保存目标，无需为两个光标属性再分配通用目标缓存。
	const auto ResolveAxis = [&](EUiAnimProperty Property, float Value, float Previous) {
		const float Current = m_Runtime.GetValue(CARET_NODE, Property, Value);
		if(Value != Previous || (!m_Runtime.HasActiveAnimation(CARET_NODE, Property) && std::abs(Current - Value) > 0.01f))
		{
			// 光标从当前画面重放短补间，不继承通用打断弹簧的旧速度。
			m_Runtime.SetValue(CARET_NODE, Property, Current);
			m_Runtime.RequestAnimation({CARET_NODE, Property, Value, Transition});
		}
		return m_Runtime.GetValue(CARET_NODE, Property, Value);
	};
	return vec2(ResolveAxis(EUiAnimProperty::POS_X, Target.x, PreviousTarget.x),
		ResolveAxis(EUiAnimProperty::POS_Y, Target.y, PreviousTarget.y));
}
