/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_ORDER_MODEL_H
#define GAME_CLIENT_UI_CARD_ORDER_MODEL_H

#include <string>
#include <vector>

enum class ECardColumn
{
	FULL = 0,
	LEFT = 1,
	RIGHT = 2,
};

// 一个放置记录是 (page, card) 的二元组：同一卡片可以被多个页面声明，
// 也可以被用户移动到声明之外的位置。m_Present = false 表示用户已把该卡片
// 从此页面移走；该记录必须保留，重启示 merge 时不得把默认声明补回原页。
struct SCardOrderEntry
{
	std::string m_Id;
	std::string m_PageId;
	ECardColumn m_Column = ECardColumn::FULL;
	int m_Order = 0;
	bool m_Present = true;
};

class CCardOrderModel final
{
	std::vector<SCardOrderEntry> m_vEntries;
	unsigned m_LayoutRevision = 0;
	unsigned m_StateRevision = 0;
	bool m_Dirty = false;

public:
	void SetDefaults(std::vector<SCardOrderEntry> Entries);
	bool ApplyOverrides(const std::vector<SCardOrderEntry> &Overrides);
	bool LoadMerged(const std::string &Serialized, const std::vector<SCardOrderEntry> &Defaults);
	std::string Serialize() const;
	// 同页内移动：把卡片移动到 (page, column) 的第 Order 个位置。
	bool Move(const std::string &PageId, const std::string &Id, ECardColumn Column, int Order);
	// 跨页移动：标记源页面放置为移出，并在目标页面插入或合并放置。
	bool MoveToPage(const std::string &Id, const std::string &FromPageId, const std::string &ToPageId, ECardColumn Column, int Order);
	// 移除一个放置（用户显式新增且要撤销时使用），不改其他页面的引用。
	bool RemovePlacement(const std::string &PageId, const std::string &Id);
	void Normalize();

	const SCardOrderEntry *Find(const std::string &PageId, const std::string &Id) const;
	std::vector<const SCardOrderEntry *> EntriesForPage(const std::string &PageId, ECardColumn Column) const;
	std::vector<const SCardOrderEntry *> EntriesForCard(const std::string &Id) const;
	const std::vector<SCardOrderEntry> &Entries() const { return m_vEntries; }
	unsigned LayoutRevision() const { return m_LayoutRevision; }
	unsigned StateRevision() const { return m_StateRevision; }
	bool IsDirty() const { return m_Dirty; }
	void ClearDirty() { m_Dirty = false; }
};

#endif
