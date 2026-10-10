#ifndef GAME_CLIENT_QMUI_QMCARDLABELHINTS_H
#define GAME_CLIENT_QMUI_QMCARDLABELHINTS_H

#include <map>
#include <string>
#include <string_view>

struct SQmCardLabelText
{
	std::string m_Label;
	std::string m_Hint;
};

// 在本地化之后拆分，半角、全角和嵌套括号共用规则；未闭合的内容保持可见。
inline SQmCardLabelText QmSplitCardLabel(std::string_view Text)
{
	SQmCardLabelText Result;
	for(size_t Pos = 0; Pos < Text.size();)
	{
		const bool Wide = Text.substr(Pos, 3) == "（";
		if(Text[Pos] != '(' && !Wide)
		{
			Result.m_Label += Text[Pos++];
			continue;
		}
		const size_t Begin = Pos + (Wide ? 3 : 1);
		size_t End = Begin;
		std::string Closers(1, Wide ? 'w' : 'a');
		for(; End < Text.size() && !Closers.empty();)
		{
			const auto Tail = Text.substr(End);
			if(Tail.front() == '(' || Tail.substr(0, 3) == "（")
			{
				const bool NestedWide = Tail.front() != '(';
				Closers += NestedWide ? 'w' : 'a';
				End += NestedWide ? 3 : 1;
			}
			else if((Tail.front() == ')' && Closers.back() == 'a') || (Tail.substr(0, 3) == "）" && Closers.back() == 'w'))
			{
				Closers.pop_back();
				if(!Closers.empty())
					End += Tail.front() == ')' ? 1 : 3;
			}
			else
				++End;
		}
		if(!Closers.empty())
		{
			Result.m_Label += Text.substr(Pos);
			break;
		}
		const auto Hint = Text.substr(Begin, End - Begin);
		if(!Hint.empty())
		{
			if(!Result.m_Hint.empty())
				Result.m_Hint += '\n';
			Result.m_Hint += Hint;
		}
		Pos = End + (Wide ? 3 : 1);
		// 只清理移除括号留下的空格，不改写普通标签中的空白。
		while(!Result.m_Label.empty() && Result.m_Label.back() == ' ' && Pos < Text.size() && Text[Pos] == ' ')
			++Pos;
	}
	if(Result.m_Label != Text)
	{
		const auto Begin = Result.m_Label.find_first_not_of(" ");
		const auto End = Result.m_Label.find_last_not_of(" ");
		Result.m_Label = Begin == std::string::npos ? "…" : Result.m_Label.substr(Begin, End - Begin + 1);
	}
	return Result;
}

// 透明查找避免每帧为已有译文分配字符串；引用在新增标签后仍然有效。
class CQmCardLabelHintCache
{
	std::map<std::string, SQmCardLabelText, std::less<>> m_Labels;

public:
	const SQmCardLabelText &Get(std::string_view Text)
	{
		auto Iter = m_Labels.find(Text);
		if(Iter == m_Labels.end())
			Iter = m_Labels.emplace(std::string(Text), QmSplitCardLabel(Text)).first;
		return Iter->second;
	}
	void Clear() { m_Labels.clear(); }
};

// 只在共享卡片绘制期间启用，嵌套内容退出后恢复调用方状态。
template<typename TUi>
class CQmCardLabelHintScope
{
	TUi *m_pUi;
	bool m_Previous = false;

public:
	explicit CQmCardLabelHintScope(TUi *pUi, bool Enabled = true) : m_pUi(pUi)
	{
		if(m_pUi != nullptr)
		{
			m_Previous = m_pUi->CardLabelHintsEnabled();
			m_pUi->SetCardLabelHintsEnabled(Enabled);
		}
	}
	~CQmCardLabelHintScope()
	{
		if(m_pUi != nullptr)
			m_pUi->SetCardLabelHintsEnabled(m_Previous);
	}
	CQmCardLabelHintScope(const CQmCardLabelHintScope &) = delete;
	CQmCardLabelHintScope &operator=(const CQmCardLabelHintScope &) = delete;
};

#endif
