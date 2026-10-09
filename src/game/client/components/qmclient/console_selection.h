// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SELECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SELECTION_H

#include <base/str.h>

#include <game/client/QmUi/QmLineInputMotion.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

// 选区使用日志的稳定编号和已解码字符位置，不随视口滚动、换行或新日志移动。
class CQmConsoleSelection
{
public:
	struct CPosition
	{
		int m_EntryId = -1;
		int m_Character = 0;
	};

	struct CRange
	{
		int m_Start;
		int m_End;
	};

private:
	CPosition m_Anchor;
	CPosition m_Cursor;
	bool m_Dragging = false;
	float m_ScrollRemainder = 0.0f;

	std::pair<CPosition, CPosition> OrderedPositions() const
	{
		if(m_Anchor.m_EntryId < m_Cursor.m_EntryId || (m_Anchor.m_EntryId == m_Cursor.m_EntryId && m_Anchor.m_Character <= m_Cursor.m_Character))
			return {m_Anchor, m_Cursor};
		return {m_Cursor, m_Anchor};
	}

public:
	void Clear()
	{
		m_Anchor = {};
		m_Cursor = {};
		Finish();
	}

	void Begin(CPosition Position)
	{
		Clear();
		if(Position.m_EntryId < 0 || Position.m_Character < 0)
			return;
		m_Anchor = Position;
		m_Cursor = Position;
		m_Dragging = true;
	}

	void Extend(CPosition Position)
	{
		if(m_Dragging && Position.m_EntryId >= 0 && Position.m_Character >= 0)
			m_Cursor = Position;
	}

	void Finish()
	{
		m_Dragging = false;
		m_ScrollRemainder = 0.0f;
	}

	CPosition CursorPosition() const { return m_Cursor; }
	bool IsDragging() const { return m_Dragging; }
	bool HasSelection() const
	{
		return m_Anchor.m_EntryId >= 0 && (m_Anchor.m_EntryId != m_Cursor.m_EntryId || m_Anchor.m_Character != m_Cursor.m_Character);
	}

	// 点击产生的零长度选区也保留光标；编号回收或清空时一并失效。
	bool HasCursorForEntry(int EntryId) const
	{
		return m_Cursor.m_EntryId >= 0 && m_Cursor.m_EntryId == EntryId;
	}

	std::optional<int> CursorForEntry(int EntryId, int CharacterCount) const
	{
		if(!HasCursorForEntry(EntryId))
			return std::nullopt;
		return std::clamp(m_Cursor.m_Character, 0, std::max(0, CharacterCount));
	}

	bool ContainsEntry(int EntryId) const
	{
		const auto [Start, End] = OrderedPositions();
		return HasSelection() && EntryId >= Start.m_EntryId && EntryId <= End.m_EntryId;
	}

	std::optional<CRange> RangeForEntry(int EntryId, int CharacterCount) const
	{
		if(!ContainsEntry(EntryId))
			return std::nullopt;
		const auto [Start, End] = OrderedPositions();
		const int Length = std::max(0, CharacterCount);
		return CRange{
			EntryId == Start.m_EntryId ? std::clamp(Start.m_Character, 0, Length) : 0,
			EntryId == End.m_EntryId ? std::clamp(End.m_Character, 0, Length) : Length};
	}

	// 调用方按日志顺序传入当前筛选下的原始可见文本，复制不依赖绘制范围。
	void AppendText(int EntryId, const char *pText, std::string &Result) const
	{
		if(!ContainsEntry(EntryId))
			return;
		const int Characters = static_cast<int>(str_utf8_offset_bytes_to_chars(pText, str_length(pText)));
		const auto Range = RangeForEntry(EntryId, Characters);
		const size_t Start = str_utf8_offset_chars_to_bytes(pText, Range->m_Start);
		const size_t End = str_utf8_offset_chars_to_bytes(pText, Range->m_End);
		if(EntryId != OrderedPositions().first.m_EntryId)
			Result.push_back('\n');
		Result.append(pText + Start, End - Start);
	}

	void OnEntryRemoved(int EntryId)
	{
		// 环形缓冲区回收端点时取消选区，避免复用的内存指向另一段日志。
		if(EntryId == m_Anchor.m_EntryId || EntryId == m_Cursor.m_EntryId)
			Clear();
	}

	int AutoScroll(float MouseY, float Top, float Bottom, float LineHeight, float FrameTime)
	{
		if(!m_Dragging || Bottom <= Top || LineHeight <= 0.0f)
		{
			m_ScrollRemainder = 0.0f;
			return 0;
		}
		const float Distance = MouseY < Top ? Top - MouseY : (MouseY > Bottom ? Bottom - MouseY : 0.0f);
		if(Distance == 0.0f)
		{
			m_ScrollRemainder = 0.0f;
			return 0;
		}
		if(Distance * m_ScrollRemainder < 0.0f)
			m_ScrollRemainder = 0.0f;
		const float Speed = 10.0f + std::min(std::abs(Distance) / LineHeight, 5.0f) * 10.0f;
		m_ScrollRemainder += (Distance > 0.0f ? Speed : -Speed) * std::clamp(FrameTime, 0.0f, 0.1f);
		const int Lines = static_cast<int>(m_ScrollRemainder);
		m_ScrollRemainder -= Lines;
		return Lines;
	}
};

// 稳定字符身份与视口坐标分开：只让重新定位字符平滑移动，滚动/拖选精确对齐。
class CQmConsoleCaretMotion
{
	CQmLineInputMotion m_Motion;
	CQmConsoleSelection::CPosition m_Position;
	vec2 m_Target{};
	bool m_Initialized = false;

public:
	void Reset()
	{
		m_Motion.Reset();
		m_Initialized = false;
	}
	vec2 Resolve(CQmConsoleSelection::CPosition Position, vec2 Target, float Height,
		std::chrono::nanoseconds Now, int Level, bool HasSelection)
	{
		const bool SameCharacter = m_Initialized && Position.m_EntryId == m_Position.m_EntryId && Position.m_Character == m_Position.m_Character;
		const bool LayoutMoved = SameCharacter && Target != m_Target;
		m_Motion.Update("", Now, Level, false);
		const vec2 Result = m_Motion.ResolveCaret(Target, Height, HasSelection || LayoutMoved, true);
		m_Position = Position;
		m_Target = Target;
		m_Initialized = true;
		return Result;
	}
};

#endif
