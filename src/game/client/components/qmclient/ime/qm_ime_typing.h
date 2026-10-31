#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_TYPING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_TYPING_H

#include <base/system.h>

#include <engine/shared/qm_ime_provider.h>

#include <charconv>
#include <map>
#include <string_view>
#include <utility>

struct SQmImeTypingResult
{
	bool m_Handled = false;
	std::string m_Commit;
};

// 英文组合与补全独立于 Rime ascii_mode；空格提交原文，Tab 才接受建议。
class CQmImeEnglishInput
{
	std::vector<std::pair<std::string, int>> m_vWords;
	std::string m_Code;
	std::vector<std::string> m_vCandidates;
	int m_Selected = 0;

	void Refresh()
	{
		m_vCandidates.clear();
		m_Selected = 0;
		if(m_Code.size() < 2)
			return;
		std::string Lower = m_Code;
		for(char &Value : Lower)
			if(Value >= 'A' && Value <= 'Z')
				Value += 'a' - 'A';
		const auto First = std::lower_bound(m_vWords.begin(), m_vWords.end(), Lower,
			[](const auto &Word, const std::string &Prefix) { return Word.first < Prefix; });
		std::vector<const std::pair<std::string, int> *> vBest;
		const auto Better = [](const auto *pLeft, const auto *pRight) {
			if(pLeft->second != pRight->second)
				return pLeft->second > pRight->second;
			if(pLeft->first.size() != pRight->first.size())
				return pLeft->first.size() < pRight->first.size();
			return pLeft->first < pRight->first;
		};
		for(auto It = First; It != m_vWords.end() && It->first.starts_with(Lower); ++It)
		{
			const auto Position = std::lower_bound(vBest.begin(), vBest.end(), &*It, Better);
			vBest.insert(Position, &*It);
			if(vBest.size() > 9)
				vBest.pop_back();
		}
		const bool AllCaps = std::all_of(m_Code.begin(), m_Code.end(), [](char Value) { return Value >= 'A' && Value <= 'Z'; });
		for(const auto *pWord : vBest)
		{
			std::string Text = m_Code + pWord->first.substr(m_Code.size());
			if(AllCaps)
				for(char &Value : Text)
					if(Value >= 'a' && Value <= 'z')
						Value -= 'a' - 'A';
			m_vCandidates.push_back(std::move(Text));
		}
	}

public:
	bool Load(std::string_view Text)
	{
		if(Text.size() > 4 * 1024 * 1024)
			return false;
		std::map<std::string, int> Words;
		while(!Text.empty())
		{
			const size_t End = Text.find('\n');
			auto Line = Text.substr(0, End);
			Text = End == Text.npos ? std::string_view{} : Text.substr(End + 1);
			if(!Line.empty() && Line.back() == '\r')
				Line.remove_suffix(1);
			if(Line.empty() || Line.front() == '#')
				continue;
			const size_t Tab = Line.find('\t');
			const auto Word = Line.substr(0, Tab);
			if(Tab == Line.npos || Word.size() < 2 || Word.size() > 28 ||
				!std::all_of(Word.begin(), Word.end(), [](char Value) { return Value >= 'a' && Value <= 'z'; }))
				return false;
			int Weight = 0;
			const auto Value = Line.substr(Tab + 1);
			const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Weight);
			if(Parsed.ec != std::errc{} || Parsed.ptr != Value.data() + Value.size() || Weight < 1)
				return false;
			Words[std::string(Word)] = std::max(Words[std::string(Word)], Weight);
		}
		if(Words.empty())
			return false;
		m_vWords.assign(Words.begin(), Words.end());
		Refresh();
		return true;
	}
	const std::string &Code() const { return m_Code; }
	const std::vector<std::string> &Candidates() const { return m_vCandidates; }
	int Selected() const { return m_Selected; }
	void Cancel()
	{
		m_Code.clear();
		m_vCandidates.clear();
		m_Selected = 0;
	}
	std::string Finish()
	{
		std::string Text = std::move(m_Code);
		Cancel();
		return Text;
	}
	std::string Select(int Index)
	{
		if(Index < 0 || Index >= static_cast<int>(m_vCandidates.size()))
			return {};
		std::string Text = m_vCandidates[Index];
		Cancel();
		return Text;
	}
	SQmImeTypingResult OnKey(int Key, int Modifiers)
	{
		if(Modifiers & (QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_ALT | QM_IME_KEYMASK_GUI))
			return {false, Finish()};
		if(Key == KEY_TAB && !m_vCandidates.empty())
			return {true, Select(m_Selected)};
		if((Key == KEY_UP || Key == KEY_DOWN) && !m_vCandidates.empty())
		{
			m_Selected = std::clamp(m_Selected + (Key == KEY_UP ? -1 : 1), 0, static_cast<int>(m_vCandidates.size()) - 1);
			return {true, {}};
		}
		if(Key == KEY_ESCAPE && !m_Code.empty())
		{
			Cancel();
			return {true, {}};
		}
		if(Key == KEY_BACKSPACE && !m_Code.empty())
		{
			m_Code.pop_back();
			Refresh();
			return {true, {}};
		}
		const int Keysym = QmImeKeyToKeysym(Key, Modifiers);
		if((Keysym >= 'a' && Keysym <= 'z') || (Keysym >= 'A' && Keysym <= 'Z') ||
			(!m_Code.empty() && ((Keysym >= '0' && Keysym <= '9') || Keysym == '\'' || Keysym == '_')))
		{
			m_Code.push_back(static_cast<char>(Keysym));
			if(m_Code.size() >= 128)
				return {true, Finish()};
			Refresh();
			return {true, {}};
		}
		std::string Text = Finish();
		if(Keysym >= 0x20 && Keysym <= 0x7e)
		{
			Text.push_back(static_cast<char>(Keysym));
			return {true, std::move(Text)};
		}
		// Enter 和光标/编辑键先冲刷原文，再交给原输入框处理。
		return {false, std::move(Text)};
	}
};

class CQmImeNumberSuffix
{
	bool m_Number = false;

public:
	static bool ContinuesPinyin(int Unit, int Next)
	{
		return Unit >= 'a' && Unit <= 'z' && (Next == 'a' || Next == 'e' || Next == 'i' || Next == 'o' || Next == 'u' || Next == 'v');
	}
	void Reset() { m_Number = false; }
	void OnCommitted(std::string_view Text)
	{
		if(Text.empty())
			return;
		const char Last = Text.back();
		m_Number = (Last >= '0' && Last <= '9') || (Last == '.' && m_Number);
	}
	bool IsLiteral(int Keysym) const
	{
		return m_Number && (Keysym == '.' || Keysym == 'k' || Keysym == 'K' || Keysym == 'w' || Keysym == 'W' ||
					   Keysym == 'm' || Keysym == 'M' || Keysym == 'g' || Keysym == 'G' || Keysym == 't' || Keysym == 'T');
	}
};

// 快捷键状态归 provider 会话；组合快捷键在传给客户端之前识别，避免吞掉 Ctrl+C 等操作。
class CQmImeModeShortcut
{
	int m_ShiftTap = 0;
	int m_Chord = 0;
	int m_ChordKey = 0;

public:
	enum class EResult
	{
		PASS,
		CONSUME,
		TOGGLE
	};
	static int ModifierOf(int Key)
	{
		if(Key == KEY_LSHIFT || Key == KEY_RSHIFT)
			return QM_IME_KEYMASK_SHIFT;
		if(Key == KEY_LCTRL || Key == KEY_RCTRL)
			return QM_IME_KEYMASK_CONTROL;
		if(Key == KEY_LALT || Key == KEY_RALT)
			return QM_IME_KEYMASK_ALT;
		if(Key == KEY_LGUI || Key == KEY_RGUI)
			return QM_IME_KEYMASK_GUI;
		return 0;
	}
	void Reset() { m_ShiftTap = m_Chord = m_ChordKey = 0; }
	EResult OnKey(int Key, int Modifiers, bool Press, bool Repeat, bool Composing, bool ShiftTap, int BindKey, int BindModifiers)
	{
		const int Own = ModifierOf(Key);
		const int Mask = Modifiers & (QM_IME_KEYMASK_SHIFT | QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_ALT | QM_IME_KEYMASK_GUI);
		const int BindOwn = ModifierOf(BindKey);
		const int Wanted = BindModifiers | BindOwn;
		if(BindKey != KEY_UNKNOWN && BindOwn != 0)
		{
			if(Press && !Repeat && Own != 0 && (Mask | Own) == Wanted)
			{
				m_Chord = Wanted;
				m_ChordKey = Key;
				m_ShiftTap = 0;
				return EResult::CONSUME;
			}
			if(!Press && Key == m_ChordKey)
			{
				Reset();
				return EResult::TOGGLE;
			}
			if(!Press && (m_Chord & Own) != 0)
				return EResult::PASS;
		}
		else if(BindKey != KEY_UNKNOWN && Key == BindKey && Mask == BindModifiers)
		{
			m_ShiftTap = 0;
			return Press && !Repeat ? EResult::TOGGLE : EResult::CONSUME;
		}
		if(Own == 0 || (Mask & ~QM_IME_KEYMASK_SHIFT) != 0)
		{
			Reset();
			return EResult::PASS;
		}
		if(Own == QM_IME_KEYMASK_SHIFT && ShiftTap)
		{
			if(Press && !Repeat)
				m_ShiftTap = !Composing ? Key : 0;
			if(!Press)
			{
				const bool Toggle = m_ShiftTap == Key;
				Reset();
				return Toggle ? EResult::TOGGLE : EResult::CONSUME;
			}
			return EResult::CONSUME;
		}
		return EResult::PASS;
	}
};

// Rime C API 不公开候选的原始拼音跨度。按固定多音字表匹配前缀，
// 只在完整候选文字与输入前缀相符时分段；无法确定时保留原生选择路径。
class CQmImePinyinSlicer
{
	std::map<int, std::vector<std::string>> m_Readings;

public:
	bool Load(std::string_view Text)
	{
		std::map<int, std::vector<std::string>> Readings;
		while(!Text.empty())
		{
			const size_t End = Text.find('\n');
			auto Line = Text.substr(0, End);
			Text = End == Text.npos ? std::string_view{} : Text.substr(End + 1);
			if(!Line.empty() && Line.back() == '\r')
				Line.remove_suffix(1);
			if(Line.empty() || Line.front() == '#')
				continue;
			const size_t Tab = Line.find('\t');
			if(!Line.starts_with("U+") || Tab == Line.npos)
				return false;
			int Codepoint = 0;
			const auto Parsed = std::from_chars(Line.data() + 2, Line.data() + Tab, Codepoint, 16);
			if(Parsed.ec != std::errc{} || Parsed.ptr != Line.data() + Tab)
				return false;
			auto Values = Line.substr(Tab + 1);
			while(!Values.empty())
			{
				const size_t Space = Values.find(' ');
				const auto Value = Values.substr(0, Space);
				if(Value.empty() || Value.size() > 6 || !std::all_of(Value.begin(), Value.end(), [](char Ch) { return Ch >= 'a' && Ch <= 'z'; }))
					return false;
				Readings[Codepoint].emplace_back(Value);
				Values = Space == Values.npos ? std::string_view{} : Values.substr(Space + 1);
			}
		}
		if(Readings.empty())
			return false;
		m_Readings = std::move(Readings);
		return true;
	}
	size_t PrefixLength(const char *pText, std::string_view Code) const
	{
		if(pText == nullptr || *pText == '\0' || Code.empty() || Code.size() > 256)
			return 0;
		std::vector<size_t> vPositions{0};
		int Characters = 0;
		while(*pText != '\0')
		{
			const int Character = str_utf8_decode(&pText);
			const auto It = m_Readings.find(Character);
			if(Character < 0x3400 || It == m_Readings.end() || ++Characters > 12)
				return 0;
			std::vector<size_t> vNext;
			for(size_t Position : vPositions)
				for(const auto &Reading : It->second)
				{
					// 最后一个字也可能来自补全：ha 选“好”应吃掉 ha，不能误按 h 的简拼留下 a。
					if(*pText == '\0' && Position < Code.size() && std::string_view(Reading).starts_with(Code.substr(Position)))
						vNext.push_back(Code.size());
					for(size_t Length : {Reading.size(), size_t{1}, Reading.size() >= 2 && Reading[1] == 'h' ? size_t{2} : size_t{1}})
					{
						if(Code.substr(Position).starts_with(std::string_view(Reading).substr(0, Length)))
						{
							size_t Next = Position + Length;
							while(Next < Code.size() && (Code[Next] == '\'' || Code[Next] == ' '))
								++Next;
							vNext.push_back(Next);
						}
					}
				}
			std::sort(vNext.begin(), vNext.end());
			vNext.erase(std::unique(vNext.begin(), vNext.end()), vNext.end());
			if(vNext.empty())
				return 0;
			vPositions = std::move(vNext);
		}
		return vPositions.back();
	}
};

#endif
