// 使用真实 provider、词库与 librime；只替换隔离文件路径，不启动客户端或访问真实用户目录。
#include <base/log.h>
#include <base/logger.h>
#include <base/windows.h>

#include <engine/shared/config.h>
#include <engine/shared/qm_ime_policy.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/ime/qm_ime_engine.h>
#include <game/client/components/qmclient/ime/qm_ime_lexicon.h>

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

CConfig g_Config{};

namespace
{
	void Require(bool Condition, const char *pMessage)
	{
		if(!Condition)
			throw std::runtime_error(pMessage);
	}

	class CProbeStorage final : public IStorage
	{
		std::string m_Root;
		std::string m_User;
		std::string m_Data;
		std::string Path(const char *pName, int Type) const
		{
			return (Type == TYPE_SAVE ? m_User : m_Data) + "/" + pName;
		}

	public:
		explicit CProbeStorage(const std::string &Root, bool Broken = false) :
			m_Root(Root), m_User(Root + (Broken ? "/tmp/ime/gpt-provider-failure-user" : "/tmp/ime/gpt-provider-user")), m_Data(Root + (Broken ? "/tmp/ime/gpt-provider-failure-data" : "/data"))
		{
			if(!Broken)
			{
				if(const char *pData = std::getenv("QM_IME_PROBE_DATA_DIR"))
					m_Data = pData;
				if(const char *pUser = std::getenv("QM_IME_PROBE_USER_DIR"))
					m_User = pUser;
			}
			std::filesystem::create_directories(m_User);
		}
		int NumPaths() const override { return 1; }
		bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override
		{
			str_copy(pBuffer, (m_Data + "/" + pName).c_str(), Size);
			return std::filesystem::exists(pBuffer);
		}
		bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override
		{
			str_copy(pBuffer, (m_Root + "/tmp/ime/rime-core/install/" + pName).c_str(), Size);
			return std::filesystem::exists(pBuffer);
		}
		void ListDirectory(int, const char *, FS_LISTDIR_CALLBACK, void *) override {}
		void ListDirectoryInfo(int, const char *, FS_LISTDIR_CALLBACK_FILEINFO, void *) override {}
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer, int Size) override
		{
			const auto Filename = Path(pName, Type);
			if(pBuffer != nullptr)
				str_copy(pBuffer, Filename.c_str(), Size);
			return io_open(Filename.c_str(), Flags);
		}
		bool FileExists(const char *pName, int Type) override { return std::filesystem::is_regular_file(Path(pName, Type)); }
		bool FolderExists(const char *pName, int Type) override { return std::filesystem::is_directory(Path(pName, Type)); }
		bool ReadFile(const char *, int, void **, unsigned *) override { return false; }
		char *ReadFileStr(const char *, int) override { return nullptr; }
		bool RetrieveTimes(const char *, int, time_t *, time_t *) override { return false; }
		bool CalculateHashes(const char *, int, SHA256_DIGEST *, unsigned *) override { return false; }
		bool FindFile(const char *, const char *, int, char *, int) override { return false; }
		size_t FindFiles(const char *, const char *, int, std::set<std::string> *) override { return 0; }
		bool RemoveFile(const char *pName, int Type) override { return std::filesystem::remove(Path(pName, Type)); }
		bool RemoveFolder(const char *pName, int Type) override { return std::filesystem::remove(Path(pName, Type)); }
		bool RenameFile(const char *pOld, const char *pNew, int Type, bool) override
		{
			return MoveFileExW(windows_utf8_to_wide(Path(pOld, Type).c_str()).c_str(), windows_utf8_to_wide(Path(pNew, Type).c_str()).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
		}
		bool CreateFolder(const char *pName, int Type) override
		{
			return std::filesystem::is_directory(Path(pName, Type)) || std::filesystem::create_directories(Path(pName, Type));
		}
		void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override { str_copy(pBuffer, Path(pName, Type).c_str(), Size); }
		bool RemoveBinaryFile(const char *) override { return false; }
		bool RenameBinaryFile(const char *, const char *) override { return false; }
		const char *GetBinaryPath(const char *pName, char *pBuffer, unsigned Size) override
		{
			GetExecutablePath(pName, pBuffer, Size);
			return pBuffer;
		}
		const char *GetBinaryPathAbsolute(const char *pName, char *pBuffer, unsigned Size) override { return GetBinaryPath(pName, pBuffer, Size); }
	};

	class CScenario
	{
	public:
		CProbeStorage m_Storage;
		CQmImeDomainLexicon m_Lexicon;
		CQmImeEngine m_Engine;
		std::vector<std::string> m_vCommits;
		std::string m_Text;

		explicit CScenario(const std::string &Root) :
			m_Storage(Root), m_Lexicon(&m_Storage), m_Engine(&m_Storage)
		{
			g_Config.m_QmImeDomainWords = 1;
			g_Config.m_QmImeDomainMax = 3;
			g_Config.m_QmImeEnglishCompletion = 1;
			g_Config.m_QmImeShiftSwitch = 1;
			g_Config.m_QmImeSwitchKey = KEY_SPACE;
			g_Config.m_QmImeSwitchModifiers = QM_IME_KEYMASK_CONTROL;
			m_Lexicon.Load();
			m_Engine.SetDomainLexicon(&m_Lexicon);
			m_Engine.SetCommitCallback([this](const char *pText) { m_vCommits.emplace_back(pText); m_Text += pText; });
			m_Engine.SetEnabled(true);
			Reset();
		}
		void Reset()
		{
			m_Engine.Deactivate();
			m_Engine.Activate();
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(65);
			while(!m_Engine.IsActive() && m_Engine.IsEnabled() && std::chrono::steady_clock::now() < Deadline)
			{
				m_Engine.OnFrame();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Require(m_Engine.IsActive(), "real Rime provider activation failed");
			m_vCommits.clear();
			m_Text.clear();
		}
		void Key(int Value, int Modifiers = 0)
		{
			m_Engine.OnKey(Value, Modifiers, true, false);
			m_Engine.OnKey(Value, Modifiers, false, false);
		}
		void Type(const std::string &Code)
		{
			for(char Value : Code)
			{
				if(Value >= 'a' && Value <= 'z')
					Key(KEY_A + Value - 'a');
				else if(Value >= 'A' && Value <= 'Z')
					Key(KEY_A + Value - 'A', QM_IME_KEYMASK_SHIFT);
				else if(Value >= '1' && Value <= '9')
					Key(KEY_1 + Value - '1');
				else if(Value == '0')
					Key(KEY_0);
				else if(Value == ' ')
					Key(KEY_SPACE);
				else
					throw std::runtime_error("unsupported probe input");
			}
		}
		void Pick(const std::string &Text)
		{
			for(int Page = 0; Page < 64; ++Page)
			{
				const auto State = m_Engine.State();
				for(size_t Index = 0; Index < State.m_vCandidates.size(); ++Index)
					if(State.m_vCandidates[Index] == Text)
					{
						Key(KEY_1 + static_cast<int>(Index));
						return;
					}
				Key(KEY_PAGEDOWN);
				if(m_Engine.State().m_PageStart == State.m_PageStart)
					break;
			}
			throw std::runtime_error("candidate not found: " + Text);
		}
		void Switch() { Key(KEY_SPACE, QM_IME_KEYMASK_CONTROL); }
	};

	void CheckEditingAndPaging(CScenario &Scenario)
	{
		Scenario.Reset();
		Scenario.Type("caishi");
		Scenario.Key(KEY_RETURN);
		Require(Scenario.m_Text == "caishi", "Enter did not preserve the typed original");
		Scenario.Reset();
		Scenario.Type("nihao");
		Scenario.Key(KEY_BACKSPACE);
		Scenario.Type("o");
		Scenario.Key(KEY_ESCAPE);
		Require(Scenario.m_Text.empty() && !Scenario.m_Engine.State().m_Composing, "Backspace/Escape composition handling regressed");
		Scenario.Type("nihao");
		Scenario.Key(KEY_1);
		Scenario.Key(KEY_COMMA);
		Require(Scenario.m_Text == "你好，", "Chinese punctuation regressed");
		Scenario.Reset();
		Scenario.Type("shi");
		const auto First = Scenario.m_Engine.State();
		Scenario.Key(KEY_PAGEDOWN);
		Require(Scenario.m_Engine.State().m_PageStart == 9, "PageDown did not expose the next native candidates");
		Scenario.Key(KEY_PAGEUP);
		Require(Scenario.m_Engine.State().m_PageStart == 0 && Scenario.m_Engine.State().m_vCandidates == First.m_vCandidates, "PageUp did not restore the original candidate page");
		Scenario.Key(KEY_MINUS);
		Require(Scenario.m_Engine.State().m_PageStart == 0, "minus wrapped before the first page");
		Scenario.Key(KEY_EQUALS);
		Require(Scenario.m_Engine.State().m_PageStart == 9 && Scenario.m_Engine.State().m_Preedit == First.m_Preedit && Scenario.m_Text.empty(), "equals did not page without committing composition");
		Scenario.Key(KEY_MINUS);
		Require(Scenario.m_Engine.State().m_PageStart == 0 && Scenario.m_Engine.State().m_vCandidates == First.m_vCandidates, "minus did not restore the original candidate page");
		for(int Page = 0; Page < 64; ++Page)
			Scenario.Key(KEY_EQUALS);
		const int LastPage = ((Scenario.m_Engine.State().m_TotalCount - 1) / 9) * 9;
		Require(Scenario.m_Engine.State().m_PageStart == LastPage, "equals wrapped past the last page");
		Scenario.Key(KEY_MINUS);
		Require(Scenario.m_Engine.State().m_PageStart == LastPage - 9, "minus failed to leave the final page");
		for(const auto &Pair : {std::pair{KEY_EQUALS, '+'}, {KEY_MINUS, '_'}})
		{
			Scenario.Reset();
			Scenario.Type("shi");
			Scenario.Key(Pair.first, QM_IME_KEYMASK_SHIFT);
			Require(Scenario.m_Engine.State().m_PageStart == 0 && Scenario.m_Text.find(Pair.second) != Scenario.m_Text.npos, "Shift punctuation was intercepted as a page key");
		}
		Scenario.Reset();
		Scenario.Key(KEY_MINUS);
		Scenario.Key(KEY_EQUALS);
		Require(Scenario.m_Text == "-=", "idle minus/equals were consumed as page keys");
		Scenario.Reset();
		Scenario.Type("shi");
		Scenario.Key(KEY_PAGEDOWN);
		Scenario.Key(KEY_DOWN);
		const auto Selected = Scenario.m_Engine.State();
		Require(Selected.m_Selected == 1 && Selected.m_vCandidates.size() > 1, "candidate highlight did not follow arrow key");
		Scenario.Key(KEY_SPACE);
		Require(Scenario.m_Text == Selected.m_vCandidates[1], "page/highlight selection used a wrong global native index");
		std::printf("PASS Enter, Backspace, Escape, punctuation, PageUp/PageDown, -/= boundaries and selected global index\n");
	}

	void CheckOriginalEnglishInChineseMode(CScenario &Scenario)
	{
		for(int Key : {KEY_RETURN, KEY_KP_ENTER})
		{
			Scenario.Reset();
			Scenario.Type("onlyfanx");
			Scenario.Key(KEY_BACKSPACE);
			Scenario.Type("s");
			Scenario.Key(KEY_DOWN);
			const bool Consumed = Scenario.m_Engine.OnKey(Key, 0, true, false);
			Scenario.m_Engine.OnKey(Key, 0, false, false);
			Require(Consumed && Scenario.m_Text == "onlyfans" && !Scenario.m_Engine.State().m_Composing, "Chinese-mode Enter selected a Chinese candidate or forwarded to the text field");
			Require(!Scenario.m_Engine.OnKey(Key, 0, true, false), "idle Enter did not return to the text field after raw commit");
			Scenario.m_Engine.OnKey(Key, 0, false, false);
		}
		Scenario.Reset();
		Scenario.Type("wo");
		Scenario.Pick("我");
		Scenario.Type("onlyfans");
		Scenario.Key(KEY_RETURN);
		Scenario.Type("nihao");
		Scenario.Key(KEY_SPACE);
		Require(Scenario.m_Text == "我onlyfans你好", "raw English commit switched modes or broke subsequent Chinese selection");
		Scenario.Reset();
		Scenario.Type("onlyfans");
		Scenario.Key(KEY_ESCAPE);
		Require(Scenario.m_Text.empty() && !Scenario.m_Engine.State().m_Composing, "cancelled English input was committed");
		std::printf("PASS Chinese-mode onlyfans+Enter/KPEnter commits raw English; first Enter consumed, idle Enter passed, Chinese Space selection retained\n");
	}

	void CheckNumberAndChinese(CScenario &Scenario)
	{
		for(const auto &Pair : {std::pair{"5ge", "5个"}, {"5tian", "5天"}, {"5wan", "5万"}, {"5ke", "5颗"}})
		{
			Scenario.Reset();
			Scenario.Type(Pair.first);
			Scenario.Pick(std::string(Pair.second).substr(1));
			Require(Scenario.m_Text == Pair.second, "number suffix swallowed a Chinese syllable");
		}
		Scenario.Reset();
		Scenario.Type("5k");
		Scenario.Key(KEY_BACKSPACE);
		Require(Scenario.m_Text == "5" && !Scenario.m_Engine.State().m_Composing, "pending unit Backspace regressed");
		Scenario.Type("k");
		Require(!Scenario.m_Engine.OnKey(KEY_RETURN, 0, true, false) && Scenario.m_Text == "5k", "trailing unit Enter did not preserve literal unit and pass through");
		std::printf("PASS 5ge/5tian/5wan/5ke Chinese quantities and trailing 5k editing\n");
	}
}

int main(int argc, char **argv)
{
	try
	{
		Require(argc >= 3, "usage: qm-ime-engine-probe <repo-root> <input|matrix|evaluate|restore|clear|after-clear|failure> [cases.tsv results.tsv]");
		log_set_global_logger_default();
		const std::string Mode = argv[2];
		if(Mode == "failure")
		{
			const auto Data = std::filesystem::path(argv[1]) / "tmp/ime/gpt-provider-failure-data/ime";
			std::filesystem::create_directories(Data);
			std::ofstream(Data / "default.yaml") << "schema_list:\n  - schema: qm_pinyin\n";
			std::ofstream(Data / "qm_pinyin.schema.yaml") << "schema: [ deliberately invalid yaml\n";
			std::ofstream(Data / "qm_pinyin.dict.yaml") << "deliberately invalid dictionary\n";
			CProbeStorage Storage(argv[1], true);
			CQmImeEngine Engine(&Storage);
			Engine.SetEnabled(true);
			Engine.Activate();
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
			while(Engine.IsEnabled() && !Engine.IsActive() && std::chrono::steady_clock::now() < Deadline)
			{
				Engine.OnFrame();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Require(!Engine.IsEnabled() && !Engine.IsActive(), "broken schema did not disable provider for system fallback");
			Require(!Engine.OnKey(KEY_A, 0, true, false), "failed provider swallowed a system-path key");
			std::printf("PASS actual Rime deployment failure releases input for system fallback\n");
			return 0;
		}
		CScenario Scenario(argv[1]);
		if(Mode == "chat-cycles")
		{
			Scenario.m_Lexicon.SetLearningEnabled(false);
			CQmImeProviderSession Session([&](const char *pText) { Scenario.m_vCommits.emplace_back(pText); Scenario.m_Text += pText; });
			Session.SetProvider(&Scenario.m_Engine);
			Session.SetEnabled(true);
			Session.Start();
			CQmImeCandidateState Candidates;
			const auto Key = [&](int Code) {
				Require(Session.OnKey(Code, 0, true, false), "session did not consume pinyin/selection press");
				Session.OnKey(Code, 0, false, false);
				Session.OnFrame();
				Candidates.UpdateFromEngine(Session.State());
				Require(Session.Enabled() && Session.TextInputRequested() && Session.IsActive(), "commit/next key dropped an active input request");
			};
			for(int Cycle = 0; Cycle < 3; ++Cycle)
			{
				g_Config.m_QmImeTrace = Cycle < 2;
				// 即使候选容器曾进入系统提交后的抑制状态，内置快照也须恢复它。
				Candidates.EndComposition();
				for(char Letter : std::string("ceshixiaoxi"))
				{
					Key(KEY_A + Letter - 'a');
					Require(Session.State().m_Composing && !Candidates.Candidates().m_vCandidates.empty(), "next composition lost exposed candidates");
					Require(Candidates.AcceptsSdlCandidates(), "built-in snapshot retained stale system suppression");
				}
				Require(Candidates.Candidates().m_vCandidates.front() == "测试消息", "chat phrase first candidate changed");
				Key(Cycle == 1 ? KEY_1 : KEY_SPACE);
				Require(!Session.State().m_Composing && Candidates.Candidates().m_vCandidates.empty(), "commit left composition or old candidates");
				Require(Scenario.m_vCommits.size() == static_cast<size_t>(Cycle + 1) && Scenario.m_vCommits.back() == "测试消息", "chat commit was missing/duplicated");
			}
			Require(Scenario.m_Text == "测试消息测试消息测试消息", "consecutive chat text differs");
			// 主动停用真实解码器，检查日志能区分“仍有请求但 provider 掉线”和主动停止。
			g_Config.m_QmImeTrace = 1;
			Scenario.m_Engine.Deactivate();
			Require(Session.TextInputRequested() && !Session.IsActive(), "injected provider loss changed the input request");
			Require(!Session.OnKey(KEY_A, 0, true, false), "inactive provider consumed a key");
			Session.Start();
			Require(Session.IsActive(), "explicit start did not recover injected provider loss");
			Session.Stop();
			Session.OnFrame();
			Require(!Session.IsActive() && !Session.TextInputRequested(), "intentional stop was undone");
			Session.Start();
			Key(KEY_N);
			Require(!Candidates.Candidates().m_vCandidates.empty(), "reopening did not restore candidates");
			Session.SetProvider(nullptr);
			g_Config.m_QmImeTrace = 0;
			std::printf("PASS controlled provider loss and explicit Stop emit distinct requested/active states; explicit Start restores real candidates\n");
			std::printf("PASS three consecutive real engine/session compositions, Space/digit commits, trace on/off, candidate sync and explicit reopen; no reset between commits\n");
			return 0;
		}
		if(Mode == "evaluate")
		{
			Require(argc == 5, "evaluate requires input and output TSV paths");
			// 只观察候选，不选词；各轮使用独立目录，避免个人学习混入语料对比。
			g_Config.m_QmImeLearning = 0;
			Scenario.m_Lexicon.SetLearningEnabled(false);
			std::ifstream Input(argv[3]);
			std::ofstream Output(argv[4]);
			Require(Input.is_open() && Output.is_open(), "cannot open corpus evaluation files");
			Output << "id\tkind\tcode\texpected\tfirst\trank\tmicroseconds\tcandidates\n";
			std::string Line;
			int Count = 0;
			while(std::getline(Input, Line))
			{
				if(!Line.empty() && Line.back() == '\r')
					Line.pop_back();
				if(Line.empty() || Line[0] == '#')
					continue;
				std::vector<std::string> vFields;
				for(size_t Begin = 0;;)
				{
					const auto End = Line.find('\t', Begin);
					vFields.emplace_back(Line.substr(Begin, End == Line.npos ? End : End - Begin));
					if(End == Line.npos)
						break;
					Begin = End + 1;
				}
				Require(vFields.size() == 4, "evaluation row must contain id, kind, code, expected");
				Scenario.Reset();
				const auto Start = std::chrono::steady_clock::now();
				Scenario.Type(vFields[2]);
				Require(Scenario.m_vCommits.empty(), "evaluation input committed text before explicit selection");
				const auto Elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - Start).count();
				const auto Candidates = Scenario.m_Engine.State().m_vCandidates;
				int Rank = 0;
				for(size_t Index = 0; Index < Candidates.size(); ++Index)
					if(Candidates[Index] == vFields[3])
						Rank = static_cast<int>(Index) + 1;
				Output << Line << '\t' << (Candidates.empty() ? "" : Candidates[0]) << '\t' << Rank << '\t' << Elapsed << '\t';
				for(size_t Index = 0; Index < Candidates.size(); ++Index)
					Output << (Index == 0 ? "" : "|") << Candidates[Index];
				Output << '\n';
				if(++Count % 500 == 0)
					std::printf("Evaluated %d rows\n", Count);
			}
			Require(Output.good() && Count > 0, "empty or failed corpus evaluation output");
			std::printf("PASS evaluated %d rows through real provider\n", Count);
			return 0;
		}
		if(Mode == "matrix")
		{
			std::ifstream Input(std::filesystem::path(argv[1]) / "qmclient_scripts/ime/engine_probe/pinyin_cases.tsv");
			Require(Input.is_open(), "pinyin cases missing");
			std::string Line;
			int Count = 0;
			int Failures = 0;
			while(std::getline(Input, Line))
			{
				if(!Line.empty() && Line.back() == '\r')
					Line.pop_back();
				if(Line.empty() || Line[0] == '#')
					continue;
				const auto Tab = Line.find('\t');
				const auto Next = Line.find('\t', Tab + 1);
				Require(Tab != Line.npos, "invalid pinyin case");
				const auto Code = Line.substr(0, Tab);
				const auto Expected = Line.substr(Tab + 1, Next == Line.npos ? Line.size() - Tab - 1 : Next - Tab - 1);
				Scenario.Reset();
				Scenario.Type(Code);
				const auto Candidates = Scenario.m_Engine.State().m_vCandidates;
				std::printf("%s -> %s; expected=%s\n", Code.c_str(), Candidates.empty() ? "<empty>" : Candidates[0].c_str(), Expected.c_str());
				++Count;
				if(Candidates.empty() || Candidates[0] != Expected)
				{
					++Failures;
					continue;
				}
				if(Next != Line.npos)
					if(Candidates.size() <= 1 || Candidates[1] != Line.substr(Next + 1))
						++Failures;
				Scenario.Key(KEY_1);
				if(Scenario.m_Text != Expected || Scenario.m_Engine.State().m_Composing)
				{
					std::printf("FAIL digit commit: %s; composition=%s\n", Scenario.m_Text.c_str(), Scenario.m_Engine.State().m_Preedit.c_str());
					++Failures;
				}
			}
			std::printf("Matrix: %d cases; %d mismatches\n", Count, Failures);
			Require(Failures == 0, "pinyin candidate order or digit commit mismatch");
			std::printf("PASS %d real-provider pinyin cases, candidate order and digit commits\n", Count);
			return 0;
		}
		if(Mode == "restore" || Mode == "after-clear")
		{
			Scenario.Type("caihong");
			const auto &Candidates = Scenario.m_Engine.State().m_vCandidates;
			if(Mode == "restore")
			{
				Require(Candidates.size() > 1 && Candidates[1] == "才红", "learned word did not persist in second position");
				std::printf("PASS restart: caihong -> %s / %s\n", Candidates[0].c_str(), Candidates[1].c_str());
			}
			else
			{
				Require(Scenario.m_Lexicon.LearnedSize() == 0, "clear left learned words on disk");
				Require(std::find(Candidates.begin(), Candidates.end(), "才红") == Candidates.end(), "clear left learned candidate");
				std::printf("PASS clear persisted across process restart\n");
			}
			return 0;
		}
		if(Mode == "clear")
		{
			Scenario.m_Lexicon.ClearLearned();
			Require(Scenario.m_Lexicon.LearnedSize() == 0 && !Scenario.m_Lexicon.Dirty(), "clear did not remove saved file");
			std::printf("PASS ClearLearned production boundary\n");
			return 0;
		}
		Require(Mode == "input", "unknown scenario");
		Scenario.Type("caishi");
		Scenario.Pick("才");
		Require(Scenario.m_Text == "才" && Scenario.m_Engine.State().m_Preedit == "shi", "partial selection lost remaining pinyin");
		Scenario.Pick("是");
		Require(Scenario.m_vCommits.size() == 2 && Scenario.m_Text == "才是", "partial commits are missing or duplicated");
		std::printf("PASS caishi -> commit=才; remaining=shi; commit=是\n");
		Scenario.Reset();
		Scenario.Type("caishi");
		Scenario.Key(KEY_SPACE);
		Require(Scenario.m_Text == "才是" && Scenario.m_vCommits.size() == 1, "whole sentence commit regressed");
		std::printf("PASS whole sentence and Space\n");
		CheckEditingAndPaging(Scenario);
		CheckOriginalEnglishInChineseMode(Scenario);
		Scenario.Reset();
		Scenario.Type("ta");
		Scenario.Pick("他");
		Scenario.Type("5kfenle");
		Scenario.Key(KEY_SPACE);
		Require(Scenario.m_Text == "他5k分了", "mixed number and Latin unit converted to Chinese");
		std::printf("PASS mixed input: %s\n", Scenario.m_Text.c_str());
		CheckNumberAndChinese(Scenario);
		Scenario.Reset();
		Scenario.Switch();
		Scenario.Type("hel");
		Require(!Scenario.m_Engine.State().m_vCandidates.empty(), "English suggestions unavailable");
		Scenario.Key(KEY_TAB);
		Scenario.Key(KEY_SPACE);
		Scenario.Type("hel");
		Scenario.Key(KEY_SPACE);
		Require(Scenario.m_Text == "hello hel ", "English Tab/Space semantics regressed");
		std::printf("PASS English: hel+Tab -> hello; hel+Space -> hel[space]\n");
		Scenario.Reset();
		g_Config.m_QmImeShiftSwitch = 0;
		Scenario.Key(KEY_LSHIFT, QM_IME_KEYMASK_SHIFT);
		Scenario.Type("nihao");
		Scenario.Key(KEY_SPACE);
		Require(Scenario.m_Text == "你好", "disabled Shift still toggles language");
		Scenario.Reset();
		g_Config.m_QmImeSwitchKey = KEY_LSHIFT;
		Scenario.m_Engine.OnKey(KEY_LCTRL, QM_IME_KEYMASK_CONTROL, true, false);
		Scenario.m_Engine.OnKey(KEY_RSHIFT, QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_SHIFT, true, false);
		Scenario.m_Engine.OnKey(KEY_RSHIFT, QM_IME_KEYMASK_CONTROL, false, false);
		Scenario.m_Engine.OnKey(KEY_LCTRL, 0, false, false);
		Scenario.Type("hel");
		Scenario.Key(KEY_TAB);
		Require(Scenario.m_Text == "hello", "custom Ctrl+Shift did not activate English completion");
		std::printf("PASS configurable shortcut and disabled Shift\n");
		Scenario.Reset();
		Scenario.m_Engine.SetCommandContext(true);
		Scenario.Key(KEY_SLASH);
		Scenario.Type("help");
		Require(Scenario.m_Text == "/help" && !Scenario.m_Engine.State().m_Composing, "chat command entered English completion");
		std::printf("PASS slash command bypasses English completion\n");
		Scenario.Reset();
		Scenario.Type("caihong");
		Scenario.Pick("才");
		Scenario.Pick("红");
		Scenario.m_Lexicon.Save();
		Require(Scenario.m_Text == "才红" && !Scenario.m_Lexicon.Dirty(), "sequential single-word learning did not persist");
		std::printf("PASS selected 才+红 learned and saved for restart test\n");
		return 0;
	}
	catch(const std::exception &Error)
	{
		std::fprintf(stderr, "FAIL: %s\n", Error.what());
		return 1;
	}
}
