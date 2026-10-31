#include "qm_ime_lexicon.h"

#include "../bounded_file_reader.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/storage.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <utility>

namespace
{
	constexpr size_t MAX_FILE_BYTES = 2 * 1024 * 1024;
	constexpr const char *TEMP_FILE = "qm_ime_lexicon.tsv.tmp";

	bool IsCjk(int Codepoint)
	{
		return Codepoint == 0x3007 || (Codepoint >= 0x3400 && Codepoint <= 0x4dbf) ||
			(Codepoint >= 0x4e00 && Codepoint <= 0x9fff) ||
			(Codepoint >= 0xf900 && Codepoint <= 0xfaff) ||
			(Codepoint >= 0x20000 && Codepoint <= 0x2ee5f) ||
			(Codepoint >= 0x2f800 && Codepoint <= 0x2fa1f) ||
			(Codepoint >= 0x30000 && Codepoint <= 0x323af);
	}

	bool IsAsciiLetter(char Value)
	{
		return (Value >= 'a' && Value <= 'z') || (Value >= 'A' && Value <= 'Z');
	}

	char Lower(char Value)
	{
		return Value >= 'A' && Value <= 'Z' ? Value - 'A' + 'a' : Value;
	}

	bool TakeLine(std::string_view &Remaining, std::string_view &Line)
	{
		if(Remaining.empty())
			return false;
		const size_t End = Remaining.find('\n');
		Line = Remaining.substr(0, End);
		Remaining = End == std::string_view::npos ? std::string_view{} : Remaining.substr(End + 1);
		if(!Line.empty() && Line.back() == '\r')
			Line.remove_suffix(1);
		return true;
	}

	template<typename T>
	bool ParseNumber(std::string_view Text, T &Value, int Base = 10)
	{
		const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value, Base);
		return Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size();
	}

	bool IsLearnable(const char *pText)
	{
		if(pText == nullptr || !str_utf8_check(pText))
			return false;
		int Count = 0;
		int CjkCount = 0;
		bool LastWasCjk = false;
		while(*pText != '\0')
		{
			const int Codepoint = str_utf8_decode(&pText);
			if(++Count > 8)
				return false;
			LastWasCjk = IsCjk(Codepoint);
			if(LastWasCjk)
				++CjkCount;
			else if(CjkCount == 0 || Codepoint > 127 || (!IsAsciiLetter(static_cast<char>(Codepoint)) && (Codepoint < '0' || Codepoint > '9')))
				return false;
		}
		return CjkCount >= 2 && LastWasCjk;
	}

	int LearnedWeight(int Count)
	{
		return 100 + 25 * std::min(Count, 12);
	}

	std::string Escape(std::string_view Text)
	{
		std::string Result;
		for(char Value : Text)
		{
			switch(Value)
			{
			case '\\': Result += "\\\\"; break;
			case '\t': Result += "\\t"; break;
			case '\n': Result += "\\n"; break;
			case '\r': Result += "\\r"; break;
			default: Result += Value; break;
			}
		}
		return Result;
	}

	bool Unescape(std::string_view Text, std::string &Result)
	{
		Result.clear();
		for(size_t Index = 0; Index < Text.size(); ++Index)
		{
			char Value = Text[Index];
			if(Value == '\\')
			{
				if(++Index == Text.size())
					return false;
				switch(Text[Index])
				{
				case '\\': Value = '\\'; break;
				case 't': Value = '\t'; break;
				case 'n': Value = '\n'; break;
				case 'r': Value = '\r'; break;
				default: return false;
				}
			}
			Result += Value;
		}
		return true;
	}
}

CQmImeDomainLexicon::CQmImeDomainLexicon(IStorage *pStorage, std::function<int64_t()> Clock) :
	m_pStorage(pStorage),
	m_Clock(Clock ? std::move(Clock) : std::function<int64_t()>([] { return time_timestamp(); })),
	m_LastSaveAttempt(m_Clock())
{
}

bool CQmImeDomainLexicon::MakeWord(std::string_view Text, EQmImeLexiconSource Source, int Weight, SWord &Word) const
{
	if(Text.empty() || Text.size() > MAX_NAME_LENGTH * 4 || Text.find('\0') != std::string_view::npos)
		return false;
	Word = {};
	Word.m_Entry.m_Text = Text;
	Word.m_Entry.m_Source = Source;
	Word.m_Entry.m_Weight = Weight;
	const char *pCursor = Word.m_Entry.m_Text.c_str();
	if(!str_utf8_check(pCursor))
		return false;
	const bool Command = Source == EQmImeLexiconSource::COMMAND;
	if(Command && *pCursor != '/')
		return false;
	if(Command && *pCursor == '/')
		++pCursor;
	int Characters = 0;
	bool HasCjk = false;
	while(*pCursor != '\0')
	{
		const int Codepoint = str_utf8_decode(&pCursor);
		if(++Characters > MAX_NAME_LENGTH)
			return false;
		std::vector<std::string> vReadings;
		if(IsCjk(Codepoint))
		{
			const auto It = m_CharReadings.find(Codepoint);
			if(It == m_CharReadings.end())
				return false;
			vReadings = It->second;
			HasCjk = true;
		}
		else if(Codepoint <= 127 && (IsAsciiLetter(static_cast<char>(Codepoint)) || (Codepoint >= '0' && Codepoint <= '9')))
			vReadings.emplace_back(1, Lower(static_cast<char>(Codepoint)));
		else if(Codepoint >= 32 && Codepoint != 127 && !Command)
			continue; // 昵称中的可见装饰不参与编码，提交仍保留原文。
		else
			return false;
		if(!Word.m_Entry.m_Pinyin.empty())
			Word.m_Entry.m_Pinyin += ' ';
		Word.m_Entry.m_Pinyin += vReadings.front();
		Word.m_Entry.m_Abbrev += vReadings.front().front();
		Word.m_vReadings.push_back(std::move(vReadings));
	}
	return !Word.m_vReadings.empty() && (HasCjk || Command);
}

bool CQmImeDomainLexicon::Feed(std::string_view Text, EQmImeLexiconSource Source, int Weight)
{
	const auto Existing = m_Session.find(std::string(Text));
	if(Existing != m_Session.end() && Existing->second.m_Entry.m_Weight >= Weight)
		return false;
	if(Existing == m_Session.end() && m_Session.size() >= MAX_SESSION_WORDS)
		return false;
	SWord Word;
	if(!MakeWord(Text, Source, Weight, Word))
		return false;
	m_Session.insert_or_assign(Word.m_Entry.m_Text, std::move(Word));
	return true;
}

void CQmImeDomainLexicon::BeginSession()
{
	if(m_Session.empty())
		return;
	m_Session.clear();
	RebuildIndex();
}

void CQmImeDomainLexicon::FeedPlayerNames(const std::vector<std::string> &vNames)
{
	bool Changed = false;
	for(const auto &Name : vNames)
		Changed = Feed(Name, EQmImeLexiconSource::PLAYER, 300) || Changed;
	if(Changed)
		RebuildIndex();
}

void CQmImeDomainLexicon::FeedServerName(const char *pName)
{
	// 冻结接口没有 SERVER 来源；与 A 约定按 MAP 分类，保持服务器独立权重。
	if(pName != nullptr && Feed(pName, EQmImeLexiconSource::MAP, 200))
		RebuildIndex();
}

void CQmImeDomainLexicon::FeedMapName(const char *pMapName)
{
	if(pMapName != nullptr && Feed(pMapName, EQmImeLexiconSource::MAP, 300))
		RebuildIndex();
}

void CQmImeDomainLexicon::FeedCommands(const std::vector<std::string> &vCommands)
{
	bool Changed = false;
	for(const auto &Command : vCommands)
		Changed = Feed(Command, EQmImeLexiconSource::COMMAND, 150) || Changed;
	if(Changed)
		RebuildIndex();
}

void CQmImeDomainLexicon::RebuildIndex()
{
	m_vFullIndex.clear();
	m_vAbbrevIndex.clear();
	const auto Add = [&](const auto &Words) {
		for(const auto &[Text, Word] : Words)
		{
			for(const auto &Reading : Word.m_vReadings.front())
			{
				m_vFullIndex.push_back({Reading, &Word});
				m_vAbbrevIndex.push_back({Reading.substr(0, 1), &Word});
			}
		}
	};
	Add(m_Session);
	Add(m_Learned);
	const auto Less = [](const auto &Left, const auto &Right) { return Left.m_Key < Right.m_Key; };
	std::sort(m_vFullIndex.begin(), m_vFullIndex.end(), Less);
	std::sort(m_vAbbrevIndex.begin(), m_vAbbrevIndex.end(), Less);
	++m_Revision;
}

bool CQmImeDomainLexicon::Matches(const SWord &Word, std::string_view Code, bool Abbreviation)
{
	// 有界状态集合逐字对齐，不展开多音字组合，也不会把“西安”误切成单音节 xian。
	std::array<bool, MAX_CODE_LENGTH + 1> aPositions{};
	aPositions[0] = true;
	for(const auto &vReadings : Word.m_vReadings)
	{
		std::array<bool, MAX_CODE_LENGTH + 1> aNext{};
		bool Advanced = false;
		for(size_t Position = 0; Position < Code.size(); ++Position)
		{
			if(!aPositions[Position])
				continue;
			for(const auto &Reading : vReadings)
			{
				const std::string_view Syllable(Reading.data(), Abbreviation ? 1 : Reading.size());
				const size_t Length = std::min(Syllable.size(), Code.size() - Position);
				if(Code.substr(Position, Length) != Syllable.substr(0, Length))
					continue;
				if(Position + Length == Code.size())
					return true;
				aNext[Position + Length] = true;
				Advanced = true;
				aNext[Position + Length] = true;
				Advanced = true;
			}
		}
		if(!Advanced)
			return false;
		aPositions = aNext;
	}
	return false;
}

bool CQmImeDomainLexicon::BetterWord(const SWord *pLeft, const SWord *pRight)
{
	if(pLeft->m_Entry.m_Weight != pRight->m_Entry.m_Weight)
		return pLeft->m_Entry.m_Weight > pRight->m_Entry.m_Weight;
	if(pLeft->m_LastUsed != pRight->m_LastUsed)
		return pLeft->m_LastUsed > pRight->m_LastUsed;
	return pLeft->m_Entry.m_Text < pRight->m_Entry.m_Text;
}

size_t CQmImeDomainLexicon::Match(const char *pCode, SQmImeLexiconEntry *pOut, size_t MaxOut) const
{
	if(!m_Enabled || pCode == nullptr || pOut == nullptr || MaxOut == 0)
		return 0;
	std::string Code;
	for(size_t Index = 0; pCode[Index] != '\0'; ++Index)
	{
		if(Index >= MAX_CODE_LENGTH || (!IsAsciiLetter(pCode[Index]) && (pCode[Index] < '0' || pCode[Index] > '9')))
			return 0;
		Code += Lower(pCode[Index]);
	}
	if(Code.empty())
		return 0;
	std::vector<const SWord *> vCandidates;
	const auto Find = [&](const auto &vIndex, std::string_view Key, bool Prefix) {
		auto It = std::lower_bound(vIndex.begin(), vIndex.end(), Key, [](const SIndexEntry &Entry, std::string_view Value) { return Entry.m_Key < Value; });
		for(; It != vIndex.end() && (Prefix ? std::string_view(It->m_Key).starts_with(Key) : It->m_Key == Key); ++It)
			vCandidates.push_back(It->m_pWord);
	};
	// 第一音节的前缀检索；后续读音在逐字匹配中验证。
	Find(m_vFullIndex, Code, true);
	for(size_t Length = 1; Length < Code.size() && Length <= 6; ++Length)
		Find(m_vFullIndex, std::string_view(Code).substr(0, Length), false);
	if(Code.size() <= MAX_NAME_LENGTH)
		Find(m_vAbbrevIndex, std::string_view(Code).substr(0, 1), false);
	std::sort(vCandidates.begin(), vCandidates.end(), std::less<const SWord *>{});
	vCandidates.erase(std::unique(vCandidates.begin(), vCandidates.end()), vCandidates.end());
	vCandidates.erase(std::remove_if(vCandidates.begin(), vCandidates.end(), [&](const SWord *pWord) {
		// 单个字母只匹配单字词条：长昵称/学习词凭首字读音挤进单字母候选没有意义，
		// 例如“咸”有 jiǎn 读音时，j 不应带出“咸的梅柿干”。
		if(Code.size() == 1 && pWord->m_vReadings.size() > 1)
			return true;
		return !Matches(*pWord, Code, false) && !Matches(*pWord, Code, true);
	}), vCandidates.end());
	// 每个文本最多有会话/学习两份；只排序足以填满输出的前部。
	const size_t Window = std::min(vCandidates.size(), std::min(MaxOut, vCandidates.size()) * 2);
	std::partial_sort(vCandidates.begin(), vCandidates.begin() + Window, vCandidates.end(), BetterWord);
	size_t Count = 0;
	for(size_t Index = 0; Index < Window && Count < MaxOut; ++Index)
	{
		const auto &Entry = vCandidates[Index]->m_Entry;
		bool Duplicate = false;
		for(size_t OutIndex = 0; OutIndex < Count; ++OutIndex)
			Duplicate = Duplicate || pOut[OutIndex].m_Text == Entry.m_Text;
		if(!Duplicate)
			pOut[Count++] = Entry;
	}
	return Count;
}

void CQmImeDomainLexicon::TrimLearned()
{
	if(m_Learned.size() <= MAX_LEARNED_WORDS)
		return;
	std::vector<const SWord *> vWords;
	for(const auto &[Text, Word] : m_Learned)
		vWords.push_back(&Word);
	std::sort(vWords.begin(), vWords.end(), BetterWord);
	for(size_t Index = MAX_LEARNED_WORDS; Index < vWords.size(); ++Index)
		m_Learned.erase(vWords[Index]->m_Entry.m_Text);
}

void CQmImeDomainLexicon::OnCommitted(const char *pText)
{
	if(!m_LearningEnabled || pText == nullptr)
		return;
	auto Learned = m_Learned.find(pText);
	if(Learned == m_Learned.end())
	{
		SWord Word;
		const auto Session = m_Session.find(pText);
		if(Session != m_Session.end())
			Word = Session->second;
		else if(!IsLearnable(pText) || !MakeWord(pText, EQmImeLexiconSource::LEARNED, 100, Word))
			return;
		Word.m_Entry.m_Source = EQmImeLexiconSource::LEARNED;
		Learned = m_Learned.emplace(pText, std::move(Word)).first;
	}
	SWord &Word = Learned->second;
	if(Word.m_Count < std::numeric_limits<int>::max())
		++Word.m_Count;
	Word.m_LastUsed = std::max<int64_t>(0, m_Clock());
	Word.m_Entry.m_Weight = LearnedWeight(Word.m_Count);
	m_Dirty = true;
	m_ClearPending = false;
	TrimLearned();
	RebuildIndex();
	Tick();
}

bool CQmImeDomainLexicon::LoadReadingTable(std::string_view Text)
{
	if(Text.size() > 16 * 1024 * 1024)
		return false;
	std::unordered_map<int, std::vector<std::string>> Readings;
	std::string_view Line;
	while(TakeLine(Text, Line))
	{
		if(Line.empty() || Line.front() == '#')
			continue;
		const size_t Tab = Line.find('\t');
		int Codepoint = 0;
		// 上游表还含私用区字形；接受有效 Unicode 码点，中文词条过滤仍由 MakeWord 负责。
		if(Tab == std::string_view::npos || !Line.starts_with("U+") || !ParseNumber(Line.substr(2, Tab - 2), Codepoint, 16) ||
			Codepoint <= 0 || Codepoint > 0x10ffff || (Codepoint >= 0xd800 && Codepoint <= 0xdfff))
			return false;
		std::string_view Values = Line.substr(Tab + 1);
		auto &vValues = Readings[Codepoint];
		while(!Values.empty())
		{
			const size_t End = Values.find(' ');
			const auto Reading = Values.substr(0, End);
			if(Reading.empty() || Reading.size() > 6 || !std::all_of(Reading.begin(), Reading.end(), [](char Value) { return Value >= 'a' && Value <= 'z'; }))
				return false;
			if(std::find(vValues.begin(), vValues.end(), Reading) == vValues.end())
				vValues.emplace_back(Reading);
			if(vValues.size() > 32)
				return false;
			Values = End == std::string_view::npos ? std::string_view{} : Values.substr(End + 1);
		}
		if(vValues.empty())
			return false;
	}
	if(Readings.empty())
		return false;
	m_CharReadings = std::move(Readings);
	// 读音表可重载；已有记录保留次数和时间，匹配索引使用新的读音。
	for(auto *pWords : {&m_Session, &m_Learned})
		for(auto It = pWords->begin(); It != pWords->end();)
		{
			SWord Word;
			const auto Source = It->first.starts_with('/') ? EQmImeLexiconSource::COMMAND : It->second.m_Entry.m_Source;
			if(!MakeWord(It->first, Source, It->second.m_Entry.m_Weight, Word))
				It = pWords->erase(It);
			else
			{
				Word.m_Entry.m_Source = It->second.m_Entry.m_Source;
				Word.m_Count = It->second.m_Count;
				Word.m_LastUsed = It->second.m_LastUsed;
				It->second = std::move(Word);
				++It;
			}
		}
	RebuildIndex();
	return true;
}

std::string CQmImeDomainLexicon::Serialize() const
{
	std::string Result = "qmimelex v1\n";
	for(const auto &[Text, Word] : m_Learned)
	{
		Result += std::to_string(static_cast<int>(EQmImeLexiconSource::LEARNED)) + '\t' + std::to_string(Word.m_Count) + '\t' + std::to_string(Word.m_LastUsed) + '\t';
		Result += Escape(Text) + '\t' + Escape(Word.m_Entry.m_Pinyin) + '\n';
	}
	return Result;
}

bool CQmImeDomainLexicon::LoadText(std::string_view Text)
{
	std::map<std::string, SWord> Learned;
	const auto Invalid = [&] {
		m_Learned.clear();
		m_Dirty = false;
		RebuildIndex();
		return false;
	};
	std::string_view Line;
	if(Text.size() > MAX_FILE_BYTES || !TakeLine(Text, Line) || Line != "qmimelex v1")
		return Invalid();
	while(TakeLine(Text, Line))
	{
		if(Line.empty())
			continue;
		std::array<std::string_view, 5> aFields;
		for(size_t Index = 0; Index < aFields.size(); ++Index)
		{
			const size_t Tab = Line.find('\t');
			if((Index + 1 < aFields.size()) != (Tab != std::string_view::npos))
				return Invalid();
			aFields[Index] = Line.substr(0, Tab);
			Line = Tab == std::string_view::npos ? std::string_view{} : Line.substr(Tab + 1);
		}
		int Source = 0;
		int Count = 0;
		int64_t LastUsed = 0;
		std::string WordText, Pinyin;
		if(!ParseNumber(aFields[0], Source) || Source != static_cast<int>(EQmImeLexiconSource::LEARNED) ||
			!ParseNumber(aFields[1], Count) || Count <= 0 || !ParseNumber(aFields[2], LastUsed) || LastUsed < 0 ||
			!Unescape(aFields[3], WordText) || !Unescape(aFields[4], Pinyin) || Pinyin.empty() || Pinyin.size() > MAX_NAME_LENGTH * 7 ||
			!std::all_of(Pinyin.begin(), Pinyin.end(), [](char Value) { return (Value >= 'a' && Value <= 'z') || (Value >= '0' && Value <= '9') || Value == ' '; }))
			return Invalid();
		SWord Word;
		const auto WordSource = WordText.starts_with('/') ? EQmImeLexiconSource::COMMAND : EQmImeLexiconSource::LEARNED;
		if(!MakeWord(WordText, WordSource, LearnedWeight(Count), Word))
			return Invalid();
		Word.m_Entry.m_Source = EQmImeLexiconSource::LEARNED;
		Word.m_Count = Count;
		Word.m_LastUsed = LastUsed;
		const auto It = Learned.find(WordText);
		if(It == Learned.end() || BetterWord(&Word, &It->second))
			Learned.insert_or_assign(WordText, std::move(Word));
	}
	m_Learned = std::move(Learned);
	TrimLearned();
	m_Dirty = false;
	m_ClearPending = false;
	m_LastSaveAttempt = m_Clock();
	RebuildIndex();
	return true;
}

void CQmImeDomainLexicon::Load()
{
	if(m_pStorage == nullptr)
		return;
	std::string Text;
	if(QmReadFileBounded(m_pStorage->OpenFile("ime/qm_pinyin_chars.txt", IOFLAG_READ, IStorage::TYPE_ALL), 16 * 1024 * 1024, Text))
		LoadReadingTable(Text);
	if(QmReadFileBounded(m_pStorage->OpenFile(LEXICON_FILE, IOFLAG_READ, IStorage::TYPE_SAVE), MAX_FILE_BYTES, Text))
		LoadText(Text);
}

void CQmImeDomainLexicon::Save()
{
	if(m_pStorage == nullptr)
		return;
	if(m_ClearPending)
	{
		const bool Removed = !m_pStorage->FileExists(LEXICON_FILE, IStorage::TYPE_SAVE) || m_pStorage->RemoveFile(LEXICON_FILE, IStorage::TYPE_SAVE);
		const bool RemovedTemp = !m_pStorage->FileExists(TEMP_FILE, IStorage::TYPE_SAVE) || m_pStorage->RemoveFile(TEMP_FILE, IStorage::TYPE_SAVE);
		if(Removed && RemovedTemp)
			m_ClearPending = false;
		else
			log_warn("qmime", "could not remove learned lexicon");
		return;
	}
	if(!m_Dirty || !m_LearningEnabled)
		return;
	const std::string Text = Serialize();
	IOHANDLE File = m_pStorage->OpenFile(TEMP_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
	{
		log_warn("qmime", "could not open temporary learned lexicon");
		return;
	}
	const bool Written = io_write(File, Text.data(), static_cast<unsigned>(Text.size())) == Text.size();
	const bool Synced = Written && io_sync(File) == 0;
	const bool Closed = io_close(File) == 0;
	// 禁止删除目标后的重试，替换失败时保留原始学习记录。
	if(Synced && Closed && m_pStorage->RenameFile(TEMP_FILE, LEXICON_FILE, IStorage::TYPE_SAVE, false))
		m_Dirty = false;
	else
		log_warn("qmime", "could not replace learned lexicon");
}

void CQmImeDomainLexicon::ClearLearned()
{
	m_Learned.clear();
	m_Dirty = false;
	m_ClearPending = true;
	RebuildIndex();
	Save(); // 用户显式清除不受自动学习开关限制。
	if(m_pStorage == nullptr)
		m_ClearPending = false;
}

void CQmImeDomainLexicon::Tick()
{
	const int64_t Now = m_Clock();
	if(Now < m_LastSaveAttempt)
		m_LastSaveAttempt = Now;
	if(Dirty() && Now - m_LastSaveAttempt >= SAVE_INTERVAL_SECONDS)
	{
		m_LastSaveAttempt = Now;
		Save();
	}
}
