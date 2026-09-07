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

struct SCardOrderEntry
{
	std::string m_Id;
	std::string m_PageId;
	ECardColumn m_Column = ECardColumn::FULL;
	int m_Order = 0;
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
	bool Move(const std::string &Id, ECardColumn Column, int Order);
	bool MoveToPage(const std::string &Id, const std::string &PageId, ECardColumn Column, int Order);
	void Normalize();

	const SCardOrderEntry *Find(const std::string &Id) const;
	std::vector<const SCardOrderEntry *> EntriesForPage(const std::string &PageId, ECardColumn Column) const;
	const std::vector<SCardOrderEntry> &Entries() const { return m_vEntries; }
	unsigned LayoutRevision() const { return m_LayoutRevision; }
	unsigned StateRevision() const { return m_StateRevision; }
	bool IsDirty() const { return m_Dirty; }
	void ClearDirty() { m_Dirty = false; }
};

#endif
