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
	static constexpr float UI_SCALE = 0.4f;

	enum class ESource
	{
		NONE,
		SERVER,
	};

	struct SCandidate
	{
		std::string m_Name;
		std::string m_Detail;
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
		int m_VisibleRows = 0;
	};

private:
	std::vector<SCandidate> m_vCandidates;
	std::string m_Input;
	size_t m_Cursor = 0;
	size_t m_TokenEnd = 0;
	uint64_t m_CommandRevision = 0;
	bool m_CommandsEnabled = false;
	bool m_Valid = false;
	ESource m_Source = ESource::NONE;
	SLayout m_Layout;
	int m_Offset = 0;
	int m_PressedRow = -1;
	float m_InputOffsetX = 0.0f;
	float m_InputOffsetY = 0.0f;
	float m_InputScale = 1.0f;

	bool MatchesInput(const char *pInput, size_t Cursor) const
	{
		return m_Valid && pInput != nullptr && m_Input == pInput && m_Cursor == Cursor;
	}

public:
	void Hide()
	{
		m_Valid = false;
		m_Source = ESource::NONE;
		m_Layout = {};
		m_PressedRow = -1;
	}

	bool BeginUpdate(const char *pInput, size_t Cursor, bool CommandsEnabled, uint64_t CommandRevision)
	{
		if(MatchesInput(pInput, Cursor) && m_CommandsEnabled == CommandsEnabled && m_CommandRevision == CommandRevision)
			return false;

		Hide();
		m_Valid = true;
		m_Input = pInput != nullptr ? pInput : "";
		m_Cursor = Cursor;
		m_CommandsEnabled = CommandsEnabled;
		m_CommandRevision = CommandRevision;
		m_Offset = 0;
		m_vCandidates.clear();
		m_TokenEnd = 0;
		while(m_TokenEnd < m_Input.size() && !str_isspace(m_Input[m_TokenEnd]))
			++m_TokenEnd;

		// 参数中的光标继续使用既有玩家名和表情补全，不抢占输入。
		if(Cursor > m_TokenEnd || (!m_Input.empty() && m_TokenEnd == 0))
			return true;
		if(CommandsEnabled && !m_Input.empty() && m_Input[0] == '/')
			m_Source = ESource::SERVER;
		return true;
	}

	void AddServerCommand(const char *pName, const char *pParams, const char *pHelp)
	{
		if(m_Source != ESource::SERVER || pName == nullptr || pName[0] == '\0')
			return;
		const std::string Prefix = m_Input.substr(1, m_TokenEnd - 1);
		if(!str_startswith_nocase(pName, Prefix.c_str()))
			return;
		std::string Detail = pParams != nullptr ? pParams : "";
		if(pHelp != nullptr && pHelp[0] != '\0')
		{
			if(!Detail.empty())
				Detail += "  ";
			Detail += pHelp;
		}
		m_vCandidates.push_back({std::string("/") + pName, std::move(Detail), pParams != nullptr && pParams[0] != '\0'});
	}

	void EndUpdate()
	{
		std::stable_sort(m_vCandidates.begin(), m_vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
			return str_comp_nocase(Left.m_Name.c_str(), Right.m_Name.c_str()) < 0;
		});
	}

	void SetLayout(float X, float Bottom, float Width, float AvailableHeight, float FontSize)
	{
		m_Layout = {};
		if(!m_Valid || m_vCandidates.empty() || Width < 60.0f)
			return;
		const float RowHeight = std::max(14.0f, FontSize * 2.2f) * UI_SCALE;
		const float HeaderHeight = (FontSize + 6.0f) * UI_SCALE;
		const int AvailableRows = std::max(0, (int)std::floor((AvailableHeight - HeaderHeight) / RowHeight));
		const int VisibleRows = std::min({6, (int)m_vCandidates.size(), AvailableRows});
		if(VisibleRows == 0)
			return;
		const float Height = HeaderHeight + VisibleRows * RowHeight;
		m_Layout = {X, Bottom - Height, Width * UI_SCALE, Height, HeaderHeight, RowHeight, VisibleRows};
		m_Offset = std::clamp(m_Offset, 0, (int)m_vCandidates.size() - VisibleRows);
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
		return true;
	}

	bool Press(float X, float Y, const char *pInput, size_t Cursor)
	{
		m_PressedRow = -1;
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
		const SCandidate &Candidate = m_vCandidates[PressedRow];
		Completion = Candidate.m_Name;
		if(m_TokenEnd == m_Input.size() && Candidate.m_HasParams)
			Completion += ' ';
		NewCursor = Completion.size();
		Completion.append(m_Input, m_TokenEnd, std::string::npos);
		return true;
	}

	bool IsPressed() const { return m_PressedRow >= 0; }
	bool MatchesSource(uint64_t CommandRevision) const { return m_CommandRevision == CommandRevision; }
	const SLayout &Layout() const { return m_Layout; }
	ESource Source() const { return m_Source; }
	int Offset() const { return m_Offset; }
	int Count() const { return (int)m_vCandidates.size(); }
	const SCandidate &Candidate(int Index) const { return m_vCandidates[Index]; }
};

#endif
