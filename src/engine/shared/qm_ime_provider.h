#ifndef ENGINE_SHARED_QM_IME_PROVIDER_H
#define ENGINE_SHARED_QM_IME_PROVIDER_H

#include <engine/keys.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// 与 X11 修饰键掩码一致；GUI 仅用于识别应交还客户端的快捷键。
enum
{
	QM_IME_KEYMASK_SHIFT = 1 << 0,
	QM_IME_KEYMASK_LOCK = 1 << 1,
	QM_IME_KEYMASK_CONTROL = 1 << 2,
	QM_IME_KEYMASK_ALT = 1 << 3,
	QM_IME_KEYMASK_GUI = 1 << 6,
};

struct SQmImeProviderState
{
	bool m_Active = false;
	bool m_Composing = false;
	std::string m_Preedit;
	int m_Cursor = 0; // UTF-8 字节偏移。
	std::vector<std::string> m_vCandidates;
	std::vector<std::string> m_vComments;
	int m_Selected = -1;
	int m_PageStart = 0;
	int m_PageSize = 0;
	int m_TotalCount = 0;
};

using FImeCommitCallback = std::function<void(const char *pText)>;

class IImeProvider
{
public:
	virtual ~IImeProvider() = default;
	virtual void SetCommitCallback(FImeCommitCallback Callback) = 0;
	virtual void Activate() = 0;
	virtual void Deactivate() = 0;
	virtual bool IsActive() const = 0;
	virtual bool OnKey(int Key, int Modifiers, bool Press, bool Repeat) = 0;
	virtual void OnFocusLost() = 0;
	virtual void OnFrame() = 0;
	virtual const SQmImeProviderState &State() const = 0;
	virtual void SetEnabled(bool Enabled) = 0;
	virtual bool IsEnabled() const = 0;
};

enum class EQmImeLexiconSource : unsigned char
{
	PLAYER = 0,
	MAP,
	COMMAND,
	LEARNED,
	NUM_SOURCES,
};

struct SQmImeLexiconEntry
{
	std::string m_Text;
	std::string m_Pinyin;
	std::string m_Abbrev;
	int m_Weight = 0;
	EQmImeLexiconSource m_Source = EQmImeLexiconSource::LEARNED;
};

class IImeDomainLexicon
{
public:
	virtual ~IImeDomainLexicon() = default;
	virtual void BeginSession() = 0;
	virtual void FeedPlayerNames(const std::vector<std::string> &vNames) = 0;
	virtual void FeedServerName(const char *pName) = 0;
	virtual void FeedMapName(const char *pMapName) = 0;
	virtual void FeedCommands(const std::vector<std::string> &vCommands) = 0;
	virtual size_t Match(const char *pCode, SQmImeLexiconEntry *pOut, size_t MaxOut) const = 0;
	virtual void OnCommitted(const char *pText) = 0;
	virtual void ClearLearned() = 0;
	virtual void Load() = 0;
	virtual void Save() = 0;
	virtual void SetEnabled(bool Enabled) = 0;
	virtual bool Enabled() const = 0;
};

// 返回 X11 keysym；不支持的键返回 0。只支持本期约定的 US 键盘布局。
int QmImeKeyToKeysym(int Key, int Modifiers);
bool QmImeKeyShouldPassThrough(int Key, int Modifiers, bool Composing);

// 自建分页：领域词只在第一页占位，其余 Rime 候选按 PageSize 连续分页，
// 保证混入领域词后原有候选仍可按页翻到，不丢失也不重复。
struct SQmImeCandidatePage
{
	int m_PageStart = 0;  // 页码 * PageSize，供现有候选弹窗计算页指示。
	int m_RimeStart = 0;  // 本页第一个 Rime 候选在完整候选列表中的全局下标。
	int m_ShownRime = 0;
	int m_ShownDomain = 0;
	int m_PageCount = 1;
};

inline SQmImeCandidatePage QmImeComputeCandidatePage(int DomainCount, int RimeCount, int PageIndex, int PageSize = 9)
{
	SQmImeCandidatePage Page;
	DomainCount = std::max(0, DomainCount);
	RimeCount = std::max(0, RimeCount);
	PageSize = std::max(1, PageSize);
	const int DomainRoom = std::min(DomainCount, PageSize);
	const int FirstRoom = PageSize - DomainRoom;
	Page.m_PageCount = 1;
	if(RimeCount > FirstRoom)
		Page.m_PageCount += (RimeCount - FirstRoom + PageSize - 1) / PageSize;
	PageIndex = std::clamp(PageIndex, 0, Page.m_PageCount - 1);
	Page.m_PageStart = PageIndex * PageSize;
	if(PageIndex == 0)
	{
		Page.m_ShownDomain = DomainRoom;
		Page.m_RimeStart = 0;
		Page.m_ShownRime = std::min(FirstRoom, RimeCount);
	}
	else
	{
		Page.m_RimeStart = FirstRoom + (PageIndex - 1) * PageSize;
		Page.m_ShownRime = std::max(0, std::min(PageSize, RimeCount - Page.m_RimeStart));
	}
	return Page;
}

// 输入设备与 provider 的生产边界，不依赖 SDL，可由 fake provider 验证。
// provider 为借用对象，销毁前必须 SetProvider(nullptr)。
class CQmImeProviderSession
{
public:
	explicit CQmImeProviderSession(FImeCommitCallback Commit);
	~CQmImeProviderSession();
	CQmImeProviderSession(const CQmImeProviderSession &) = delete;
	CQmImeProviderSession &operator=(const CQmImeProviderSession &) = delete;

	void SetProvider(IImeProvider *pProvider);
	void SetEnabled(bool Enabled);
	void Start();
	void Stop();
	void OnFrame();
	void OnFocusLost();
	bool OnKey(int Key, int Modifiers, bool Press, bool Repeat);
	bool IsActive() const;
	bool Enabled() const { return m_Enabled; }
	bool TextInputRequested() const { return m_TextInputRequested; }
	const SQmImeProviderState &State() const;

private:
	IImeProvider *m_pProvider = nullptr;
	FImeCommitCallback m_Commit;
	bool m_Enabled = false;
	bool m_TextInputRequested = false;
	std::array<bool, KEY_LAST> m_aConsumedKeys{};
};

#endif
