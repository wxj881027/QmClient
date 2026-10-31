#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_H

#include <base/str.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 命令候选只产生待编辑文本，发送仍由聊天输入框负责。
class CQmChatCommandHud
{
public:
	static constexpr float UI_SCALE = 0.6f;
	static constexpr float ROW_PADDING_Y = 3.0f * UI_SCALE;
	static constexpr float ROW_LINE_GAP = 0.25f * UI_SCALE;

	enum class ESource
	{
		NONE,
		SERVER,
		EMOJI,
	};

	struct SCandidate
	{
		std::string m_Name;
		std::string m_Params;
		std::string m_Detail;
		std::string m_Label;
		bool m_HasParams;
	};

	struct SLayout
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
		float m_HeaderHeight = 0.0f;
		float m_RowHeight = 0.0f;
		float m_FontSize = 0.0f;
		float m_HeaderFontSize = 0.0f;
		float m_DetailFontSize = 0.0f;
		int m_VisibleRows = 0;
	};

private:
	std::vector<SCandidate> m_vCandidates;
	std::string m_Input;
	size_t m_Cursor = 0;
	size_t m_TokenEnd = 0;
	size_t m_TokenStart = 0;
	size_t m_EmojiColonLength = 0;
	uint64_t m_CommandRevision = 0;
	bool m_CommandsEnabled = false;
	bool m_EmojisEnabled = false;
	bool m_Valid = false;
	ESource m_Source = ESource::NONE;
	SLayout m_Layout;
	int m_Offset = 0;
	int m_PressedRow = -1;
	int m_Selected = -1;
	float m_InputOffsetX = 0.0f;
	float m_InputOffsetY = 0.0f;
	float m_InputScale = 1.0f;

	bool MatchesInput(const char *pInput, size_t Cursor) const
	{
		return m_Valid && pInput != nullptr && m_Input == pInput && m_Cursor == Cursor;
	}

	void KeepSelectionVisible()
	{
		if(m_Selected < 0 || m_Layout.m_VisibleRows <= 0)
			return;
		if(m_Selected < m_Offset)
			m_Offset = m_Selected;
		else if(m_Selected >= m_Offset + m_Layout.m_VisibleRows)
			m_Offset = m_Selected - m_Layout.m_VisibleRows + 1;
	}

	bool Complete(int Index, const char *pInput, size_t Cursor, std::string &Completion, size_t &NewCursor)
	{
		Completion.clear();
		if(!MatchesInput(pInput, Cursor) || Index < 0 || Index >= Count())
			return false;
		const SCandidate &Candidate = m_vCandidates[Index];
		Completion.assign(m_Input, 0, m_TokenStart);
		if(m_Source == ESource::EMOJI)
		{
			Completion.append(m_Input, m_TokenStart, m_EmojiColonLength);
			Completion += Candidate.m_Name.substr(1);
		}
		else
		{
			Completion += Candidate.m_Name;
			if(m_TokenEnd == m_Input.size() && Candidate.m_HasParams)
				Completion += ' ';
		}
		NewCursor = Completion.size();
		Completion.append(m_Input, m_TokenEnd, std::string::npos);
		// 补全只编辑一次；再次 Enter 交还聊天发送路径。
		Hide();
		return true;
	}

public:
	void Hide()
	{
		m_Valid = false;
		m_Source = ESource::NONE;
		m_Layout = {};
		m_PressedRow = -1;
		m_Selected = -1;
	}

	bool BeginUpdate(const char *pInput, size_t Cursor, bool CommandsEnabled, uint64_t CommandRevision, bool EmojisEnabled = false)
	{
		if(MatchesInput(pInput, Cursor) && m_CommandsEnabled == CommandsEnabled && m_EmojisEnabled == EmojisEnabled && m_CommandRevision == CommandRevision)
			return false;

		Hide();
		m_Valid = true;
		m_Input = pInput != nullptr ? pInput : "";
		m_Cursor = Cursor;
		m_CommandsEnabled = CommandsEnabled;
		m_EmojisEnabled = EmojisEnabled;
		m_CommandRevision = CommandRevision;
		m_Offset = 0;
		m_vCandidates.clear();
		m_TokenEnd = 0;
		m_TokenStart = 0;
		m_EmojiColonLength = 0;
		if(Cursor > m_Input.size())
			return true;
		while(m_TokenEnd < m_Input.size() && !str_isspace(m_Input[m_TokenEnd]))
			++m_TokenEnd;

		// 命令候选仅处理首个 token，参数区仍允许既有玩家名补全。
		if(CommandsEnabled && Cursor > 0 && Cursor <= m_TokenEnd && m_Input[0] == '/')
		{
			m_Source = ESource::SERVER;
			return true;
		}
		if(!EmojisEnabled)
			return true;
		m_TokenStart = Cursor;
		while(m_TokenStart > 0 && !str_isspace(m_Input[m_TokenStart - 1]))
			--m_TokenStart;
		m_TokenEnd = Cursor;
		while(m_TokenEnd < m_Input.size() && !str_isspace(m_Input[m_TokenEnd]))
			++m_TokenEnd;
		if(m_TokenEnd > m_TokenStart && m_Input[m_TokenStart] == ':')
			m_EmojiColonLength = 1;
		else if(m_TokenEnd - m_TokenStart >= 3 && m_Input.compare(m_TokenStart, 3, "：") == 0)
			m_EmojiColonLength = 3;
		if(m_EmojiColonLength > 0 && Cursor >= m_TokenStart + m_EmojiColonLength)
			m_Source = ESource::EMOJI;
		return true;
	}

	void AddServerCommand(const char *pName, const char *pParams, const char *pHelp)
	{
		if(m_Source != ESource::SERVER || pName == nullptr || pName[0] == '\0')
			return;
		const std::string Prefix = m_Input.substr(1, m_TokenEnd - 1);
		if(!str_startswith_nocase(pName, Prefix.c_str()))
			return;
		SCandidate Candidate{std::string("/") + pName, pParams != nullptr ? pParams : "", pHelp != nullptr ? pHelp : "", "", pParams != nullptr && pParams[0] != '\0'};
		Candidate.m_Label = Candidate.m_Name;
		if(Candidate.m_HasParams)
		{
			Candidate.m_Label += ' ';
			Candidate.m_Label += Candidate.m_Params;
		}
		m_vCandidates.push_back(std::move(Candidate));
	}

	void AddEmoji(const char *pCode)
	{
		if(m_Source != ESource::EMOJI || pCode == nullptr || pCode[0] != ':')
			return;
		const size_t PrefixStart = m_TokenStart + m_EmojiColonLength;
		const std::string Prefix = m_Input.substr(PrefixStart, m_Cursor - PrefixStart);
		if(str_startswith_nocase(pCode + 1, Prefix.c_str()))
			m_vCandidates.push_back({pCode, "", "", pCode, false});
	}

	void EndUpdate()
	{
		if(m_Source != ESource::SERVER)
			return;
		std::stable_sort(m_vCandidates.begin(), m_vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
			return str_comp_nocase(Left.m_Name.c_str(), Right.m_Name.c_str()) < 0;
		});
	}

	void SetLayout(float X, float Bottom, float Width, float AvailableHeight, float FontSize, float ScreenLeft, float ScreenRight)
	{
		m_Layout = {};
		if(!m_Valid || m_vCandidates.empty() || Width < 60.0f || ScreenRight <= ScreenLeft)
			return;
		const float NameFontSize = FontSize * UI_SCALE;
		const float DetailFontSize = m_Source == ESource::SERVER ? NameFontSize * 0.72f : 0.0f;
		const float RowHeight = ROW_PADDING_Y * 2.0f + NameFontSize + (DetailFontSize > 0.0f ? DetailFontSize + ROW_LINE_GAP + 2.0f * UI_SCALE : 2.0f * UI_SCALE);
		const float HeaderFontSize = NameFontSize * 1.15f;
		const float HeaderHeight = HeaderFontSize + 10.0f * UI_SCALE;
		const int AvailableRows = std::max(0, (int)std::floor((AvailableHeight - HeaderHeight) / RowHeight));
		const int VisibleRows = std::min({6, (int)m_vCandidates.size(), AvailableRows});
		if(VisibleRows == 0)
			return;
		const float Height = HeaderHeight + VisibleRows * RowHeight;
		const float PanelWidth = std::min(Width * UI_SCALE, ScreenRight - ScreenLeft);
		m_Layout = {std::clamp(X, ScreenLeft, ScreenRight - PanelWidth), Bottom - Height, PanelWidth, Height, HeaderHeight, RowHeight, NameFontSize, HeaderFontSize, DetailFontSize, VisibleRows};
		m_Offset = std::clamp(m_Offset, 0, (int)m_vCandidates.size() - VisibleRows);
		KeepSelectionVisible();
	}

	void SetInputTransform(float OffsetX, float OffsetY, float Scale)
	{
		m_InputOffsetX = OffsetX;
		m_InputOffsetY = OffsetY;
		m_InputScale = Scale > 0.0f ? Scale : 1.0f;
	}

	bool Contains(float X, float Y) const
	{
		X = (X - m_InputOffsetX) / m_InputScale;
		Y = (Y - m_InputOffsetY) / m_InputScale;
		return m_Layout.m_VisibleRows > 0 && X >= m_Layout.m_X && X < m_Layout.m_X + m_Layout.m_W && Y >= m_Layout.m_Y && Y < m_Layout.m_Y + m_Layout.m_H;
	}

	int HoveredRow(float X, float Y) const
	{
		if(!Contains(X, Y))
			return -1;
		Y = (Y - m_InputOffsetY) / m_InputScale - m_Layout.m_Y - m_Layout.m_HeaderHeight;
		return Y < 0.0f ? -1 : m_Offset + std::min(m_Layout.m_VisibleRows - 1, (int)(Y / m_Layout.m_RowHeight));
	}

	bool Scroll(float X, float Y, int Direction, const char *pInput, size_t Cursor)
	{
		if(!MatchesInput(pInput, Cursor) || !Contains(X, Y))
			return false;
		m_Offset = std::clamp(m_Offset + Direction, 0, std::max(0, (int)m_vCandidates.size() - m_Layout.m_VisibleRows));
		m_PressedRow = -1;
		m_Selected = -1;
		return true;
	}

	bool MoveSelection(int Direction, const char *pInput, size_t Cursor, bool HistoryNavigationActive = false)
	{
		// 从历史回填的命令或表情仍可显示候选，上下键继续交给历史浏览。
		if(HistoryNavigationActive || !MatchesInput(pInput, Cursor) || m_vCandidates.empty() || Direction == 0)
			return false;
		if(m_Selected < 0)
			m_Selected = Direction > 0 ? 0 : Count() - 1;
		else
			m_Selected = (m_Selected + (Direction > 0 ? 1 : -1) + Count()) % Count();
		m_PressedRow = -1;
		KeepSelectionVisible();
		return true;
	}

	bool CompleteSelected(const char *pInput, size_t Cursor, std::string &Completion, size_t &NewCursor)
	{
		return Complete(m_Selected, pInput, Cursor, Completion, NewCursor);
	}

	bool Press(float X, float Y, const char *pInput, size_t Cursor)
	{
		m_PressedRow = -1;
		m_Selected = -1;
		if(!MatchesInput(pInput, Cursor) || !Contains(X, Y))
			return false;
		m_PressedRow = HoveredRow(X, Y);
		return true;
	}

	bool Release(float X, float Y, const char *pInput, size_t Cursor, std::string &Completion, size_t &NewCursor)
	{
		Completion.clear();
		const int PressedRow = m_PressedRow;
		m_PressedRow = -1;
		if(!MatchesInput(pInput, Cursor) || PressedRow < 0 || PressedRow != HoveredRow(X, Y))
			return false;
		return Complete(PressedRow, pInput, Cursor, Completion, NewCursor);
	}

	bool IsPressed() const { return m_PressedRow >= 0; }
	bool MatchesSource(uint64_t CommandRevision) const { return m_CommandRevision == CommandRevision; }
	const SLayout &Layout() const { return m_Layout; }
	ESource Source() const { return m_Source; }
	int Offset() const { return m_Offset; }
	int Selected() const { return m_Selected; }
	int Count() const { return (int)m_vCandidates.size(); }
	const SCandidate &Candidate(int Index) const { return m_vCandidates[Index]; }
};

#endif
