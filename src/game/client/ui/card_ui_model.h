/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_UI_MODEL_H
#define GAME_CLIENT_UI_CARD_UI_MODEL_H

#include "card_registry.h"

#include <string>
#include <unordered_map>
#include <vector>

struct SCardUiPreferences
{
	bool m_Visible = true;
	bool m_Collapsed = false;
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

// 一个放置的完整持久化状态；ExportState 只导出与默认声明不同的覆盖。
struct SCardPlacementState
{
	std::string m_CardId;
	std::string m_PageId;
	ECardColumn m_Column = ECardColumn::FULL;
	int m_Order = 0;
	bool m_Present = true;
	bool m_Visible = true;
	bool m_Collapsed = false;
};

struct SCardUiState
{
	std::vector<SCardPlacementState> m_vPlacements;
	SCardViewPreferences m_View;
};

class CCardUiModel final
{
	const CCardRegistry &m_Registry;
	CCardOrderModel m_OrderModel;
	// 偏好按 (page, card) 保存：本页隐藏与折叠属于放置记录，不属于全局卡片。
	std::unordered_map<std::string, SCardUiPreferences> m_Preferences;
	bool m_Dirty = false;
	unsigned m_Revision = 0;
	SCardViewPreferences m_ViewPreferences;

	static std::string PlacementKey(const std::string &PageId, const std::string &CardId);

public:
	explicit CCardUiModel(const CCardRegistry &Registry);

	SCardUiPreferences Preferences(const std::string &PageId, const std::string &CardId) const;
	bool SetPreferences(const std::string &PageId, const std::string &CardId, SCardUiPreferences Preferences);
	SCardModelSnapshot Snapshot(const std::string &PageId, const std::string &CardId) const;
	const CCardRegistry &Registry() const { return m_Registry; }
	const CCardOrderModel &OrderModel() const { return m_OrderModel; }
	unsigned Revision() const { return m_Revision; }
	const SCardViewPreferences &ViewPreferences() const { return m_ViewPreferences; }
	bool SetViewPreferences(SCardViewPreferences Preferences);

	// 跨页移动：从源页面移除放置并插入/合并目标页面放置。
	bool MoveCard(const std::string &CardId, const std::string &FromPageId, const std::string &ToPageId, ECardColumn Column, int Order);
	bool MoveCardWithinPage(const std::string &PageId, const std::string &CardId, ECardColumn Column, int Order);
	bool MoveCardRelative(const std::string &PageId, const std::string &CardId, const std::string &TargetCardId, bool After);

	SCardUiState ExportState() const;
	bool ImportState(const SCardUiState &State, std::string &Error);

	bool ResetPreferences(const std::string &PageId, const std::string &CardId);
	bool ResetPagePreferences(const std::string &PageId);
	void ResetAllPreferences();
	bool IsDirty() const { return m_Dirty || m_OrderModel.IsDirty(); }
	void ClearDirty() { m_Dirty = false; m_OrderModel.ClearDirty(); }

	std::vector<const SCardDescriptor *> CardsForPage(const std::string &PageId) const;
};

#endif
