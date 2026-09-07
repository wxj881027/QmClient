/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_UI_MODEL_H
#define GAME_CLIENT_UI_CARD_UI_MODEL_H

#include "card_registry.h"

#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

struct SCardUiPreferences
{
	bool m_Visible = true;
	bool m_Collapsed = false;
	int m_Order = 0;
};

struct SCardModelSnapshot
{
	const SCardDescriptor *m_pDescriptor = nullptr;
	SCardUiPreferences m_Preferences;
	bool m_Enabled = false;
	bool m_Available = false;
};

struct SCardViewPreferences
{
	int m_Mode = 0;
	bool m_LightTheme = false;
	bool m_Animations = true;
};

class CCardUiModel final
{
	struct SVisualPreferences
	{
		bool m_Visible;
		bool m_Collapsed;
	};
	const CCardRegistry &m_Registry;
	CCardOrderModel m_OrderModel;
	std::unordered_map<std::string, SVisualPreferences> m_Preferences;
	bool m_Dirty = false;
	unsigned m_Revision = 0;
	SCardViewPreferences m_ViewPreferences;

public:
	explicit CCardUiModel(const CCardRegistry &Registry) :
		m_Registry(Registry), m_OrderModel(Registry.BuildDefaultOrderModel()) {}

	bool SetPreferences(const std::string &CardId, SCardUiPreferences Preferences);
	SCardUiPreferences Preferences(const std::string &CardId) const;
	SCardModelSnapshot Snapshot(const std::string &CardId) const;
	const CCardRegistry &Registry() const { return m_Registry; }
	std::vector<std::pair<std::string, SCardUiPreferences>> ExportPreferences() const;
	bool ImportPreferences(const std::vector<std::pair<std::string, SCardUiPreferences>> &Preferences);
	bool ReplacePreferences(const std::vector<std::pair<std::string, SCardUiPreferences>> &Preferences);
	bool ReplaceState(const std::vector<std::pair<std::string, SCardUiPreferences>> &Preferences, const std::vector<SCardOrderEntry> &Placements);
	bool MoveCard(const std::string &CardId, const std::string &PageId, ECardColumn Column, int Order);
	bool MoveCardRelative(const std::string &CardId, const std::string &TargetId, bool After);
	const CCardOrderModel &OrderModel() const { return m_OrderModel; }
	unsigned Revision() const { return m_Revision; }
	const SCardViewPreferences &ViewPreferences() const { return m_ViewPreferences; }
	bool SetViewPreferences(SCardViewPreferences Preferences);
	bool ResetPreferences(const std::string &CardId);
	void ResetAllPreferences();
	bool IsDirty() const { return m_Dirty || m_OrderModel.IsDirty(); }
	void ClearDirty() { m_Dirty = false; m_OrderModel.ClearDirty(); }
	std::vector<const SCardDescriptor *> CardsForPage(const std::string &PageId) const;
	std::vector<const SCardDescriptor *> Search(const std::string &Query) const;
};

#endif
