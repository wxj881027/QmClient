#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_LEXICON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_LEXICON_H

#include <engine/shared/qm_ime_provider.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IStorage;

class CQmImeDomainLexicon final : public IImeDomainLexicon
{
public:
	static constexpr size_t MAX_SESSION_WORDS = 512;
	static constexpr size_t MAX_LEARNED_WORDS = 5000;
	static constexpr int MAX_NAME_LENGTH = 12;
	static constexpr size_t MAX_CODE_LENGTH = 24;
	static constexpr int64_t SAVE_INTERVAL_SECONDS = 5 * 60;
	static constexpr const char *LEXICON_FILE = "qm_ime_lexicon.tsv";

	explicit CQmImeDomainLexicon(IStorage *pStorage = nullptr, std::function<int64_t()> Clock = {});
	CQmImeDomainLexicon(const CQmImeDomainLexicon &) = delete;
	CQmImeDomainLexicon &operator=(const CQmImeDomainLexicon &) = delete;

	void BeginSession() override;
	void FeedPlayerNames(const std::vector<std::string> &vNames) override;
	void FeedServerName(const char *pName) override;
	void FeedMapName(const char *pMapName) override;
	void FeedCommands(const std::vector<std::string> &vCommands) override;
	size_t Match(const char *pCode, SQmImeLexiconEntry *pOut, size_t MaxOut) const override;
	void OnCommitted(const char *pText) override;
	void ClearLearned() override;
	void Load() override;
	void Save() override;
	void SetEnabled(bool Enabled) override { m_Enabled = Enabled; }
	bool Enabled() const override { return m_Enabled; }
	void SetLearningEnabled(bool Enabled) { m_LearningEnabled = Enabled; }
	bool LearningEnabled() const { return m_LearningEnabled; }
	void Tick();

	// 生产读写共用同一解析器；先校验完整文件，再发布新状态。
	bool LoadReadingTable(std::string_view Text);
	bool LoadText(std::string_view Text);
	std::string Serialize() const;
	size_t SessionSize() const { return m_Session.size(); }
	size_t LearnedSize() const { return m_Learned.size(); }
	bool Dirty() const { return m_Dirty || m_ClearPending; }
	uint64_t Revision() const { return m_Revision; }

private:
	struct SWord
	{
		SQmImeLexiconEntry m_Entry;
		std::vector<std::vector<std::string>> m_vReadings;
		int m_Count = 0;
		int64_t m_LastUsed = 0;
	};
	struct SIndexEntry
	{
		std::string m_Key;
		const SWord *m_pWord;
	};

	bool MakeWord(std::string_view Text, EQmImeLexiconSource Source, int Weight, SWord &Word) const;
	bool Feed(std::string_view Text, EQmImeLexiconSource Source, int Weight);
	void RebuildIndex();
	void TrimLearned();
	static bool Matches(const SWord &Word, std::string_view Code, bool Abbreviation);
	static bool BetterWord(const SWord *pLeft, const SWord *pRight);

	IStorage *m_pStorage;
	std::function<int64_t()> m_Clock;
	std::unordered_map<int, std::vector<std::string>> m_CharReadings;
	std::map<std::string, SWord> m_Session;
	std::map<std::string, SWord> m_Learned;
	std::vector<SIndexEntry> m_vFullIndex;
	std::vector<SIndexEntry> m_vAbbrevIndex;
	bool m_Enabled = true;
	bool m_LearningEnabled = true;
	bool m_Dirty = false;
	bool m_ClearPending = false;
	int64_t m_LastSaveAttempt;
	uint64_t m_Revision = 0;
};

#endif
