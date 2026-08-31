#include "QmModuleLayoutAdapter.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <algorithm>

namespace qm_module
{
	int QmModuleColumnToInt(EQmModuleColumn Column)
	{
		switch(Column)
		{
		case EQmModuleColumn::Full: return 0;
		case EQmModuleColumn::Left: return 1;
		case EQmModuleColumn::Right: return 2;
		}
		return 0;
	}

	EQmModuleColumn QmModuleColumnFromInt(int Column)
	{
		switch(Column)
		{
		case 1: return EQmModuleColumn::Left;
		case 2: return EQmModuleColumn::Right;
		default: return EQmModuleColumn::Full;
		}
	}

	const char *QmModuleColumnToString(EQmModuleColumn Column)
	{
		switch(Column)
		{
		case EQmModuleColumn::Full: return "full";
		case EQmModuleColumn::Left: return "left";
		case EQmModuleColumn::Right: return "right";
		}
		return "left";
	}

	// 等价栖梦 ParseQmModuleColumn：str_comp_nocase 命中 full/left/right，否则 str_toint 解析整数 0/1/2。
	bool ParseQmModuleColumnString(const char *pStr, EQmModuleColumn *pOut)
	{
		if(pStr == nullptr || pOut == nullptr)
			return false;
		if(str_comp_nocase(pStr, "full") == 0)
		{
			*pOut = EQmModuleColumn::Full;
			return true;
		}
		if(str_comp_nocase(pStr, "left") == 0)
		{
			*pOut = EQmModuleColumn::Left;
			return true;
		}
		if(str_comp_nocase(pStr, "right") == 0)
		{
			*pOut = EQmModuleColumn::Right;
			return true;
		}
		int Num = 0;
		if(str_toint(pStr, &Num))
		{
			if(Num == 0)
			{
				*pOut = EQmModuleColumn::Full;
				return true;
			}
			if(Num == 1)
			{
				*pOut = EQmModuleColumn::Left;
				return true;
			}
			if(Num == 2)
			{
				*pOut = EQmModuleColumn::Right;
				return true;
			}
		}
		return false;
	}

	const char *QmModuleStableId(EQmModuleId Id)
	{
		switch(Id)
		{
		case EQmModuleId::Info: return "qm:info";
		case EQmModuleId::ChatBubble: return "qm:chat_bubble";
		case EQmModuleId::GoresActor: return "qm:gores_actor";
		case EQmModuleId::Gores: return "qm:gores";
		case EQmModuleId::FocusMode: return "qm:focus_mode";
		case EQmModuleId::KeyBinds: return "qm:key_binds";
		case EQmModuleId::MiniFeatures: return "qm:mini_features";
		case EQmModuleId::JumpHint: return "qm:jump_hint";
		case EQmModuleId::SkinTransition: return "qm:skin_transition";
		case EQmModuleId::CameraView: return "qm:camera_view";
		case EQmModuleId::DummyMiniView: return "qm:dummy_miniview";
		case EQmModuleId::Coords: return "qm:coords";
		case EQmModuleId::Streamer: return "qm:streamer";
		case EQmModuleId::FriendNotify: return "qm:friend_notify";
		case EQmModuleId::BlockWords: return "qm:block_words";
		case EQmModuleId::Translate: return "qm:translate";
		case EQmModuleId::TranslateUi: return "qm:translate_ui";
		case EQmModuleId::QiaFen: return "qm:qiafen"; // 持久化 key，非 UI 名 keyword_reply
		case EQmModuleId::PieMenu: return "qm:pie_menu";
		case EQmModuleId::EntityOverlay: return "qm:entity_overlay";
		case EQmModuleId::Laser: return "qm:laser";
		case EQmModuleId::PlayerStats: return "qm:player_stats";
		case EQmModuleId::CollisionHitbox: return "qm:collision_hitbox";
		case EQmModuleId::FavoriteMaps: return "qm:favorite_maps";
		case EQmModuleId::HJAssist: return "qm:hj_assist";
		case EQmModuleId::SpeedrunTimer: return "qm:speedrun_timer";
		case EQmModuleId::DebugGraph: return "qm:debug_graph";
		case EQmModuleId::InputOverlay: return "qm:input_overlay";
		case EQmModuleId::HudNotifications: return "qm:hud_notifications";
		case EQmModuleId::Voice: return "qm:voice";
		case EQmModuleId::DynamicIsland: return "qm:dynamic_island";
		case EQmModuleId::SystemMediaControls: return "qm:system_media_controls";
		case EQmModuleId::Lyrics: return "qm:lyrics";
		case EQmModuleId::Background3D: return "qm:background_3d";
		case EQmModuleId::WeaponTrajectory: return "qm:weapon_trajectory";
		case EQmModuleId::WeaponAnimation: return "qm:weapon_animation";
		case EQmModuleId::DebugMode: return "qm:debug_mode";
		case EQmModuleId::BindStatusHud: return "qm:bind_status_hud";
		}
		return nullptr;
	}

	bool QmModuleIdFromStableId(const char *pStableId, EQmModuleId *pOut)
	{
		if(pStableId == nullptr || pOut == nullptr)
			return false;
		for(size_t i = 0; i < QmModuleCount; ++i)
		{
			EQmModuleId Id = static_cast<EQmModuleId>(i);
			const char *pStable = QmModuleStableId(Id);
			if(pStable != nullptr && str_comp(pStable, pStableId) == 0)
			{
				*pOut = Id;
				return true;
			}
		}
		return false;
	}

	void NormalizeQmLayoutColumns(std::vector<SQmModuleEntry> &vEntries)
	{
		auto NormalizeColumn = [&](EQmModuleColumn Column) {
			std::vector<int> vIndices;
			vIndices.reserve(vEntries.size());
			for(size_t i = 0; i < vEntries.size(); ++i)
			{
				if(vEntries[i].m_Column == Column)
					vIndices.push_back(static_cast<int>(i));
			}
			std::stable_sort(vIndices.begin(), vIndices.end(), [&](int a, int b) {
				if(vEntries[a].m_OrderInColumn != vEntries[b].m_OrderInColumn)
					return vEntries[a].m_OrderInColumn < vEntries[b].m_OrderInColumn;
				return a < b;
			});
			for(size_t i = 0; i < vIndices.size(); ++i)
				vEntries[vIndices[i]].m_OrderInColumn = static_cast<int>(i);
		};
		NormalizeColumn(EQmModuleColumn::Left);
		NormalizeColumn(EQmModuleColumn::Right);
	}

	bool ParseLegacyQmLayout(const char *pConfig, const std::vector<SQmModuleEntry> &vDefaults, std::vector<SQmModuleEntry> &vOut)
	{
		vOut = vDefaults; // 基准（含全卡，缺失卡兜底）
		if(pConfig == nullptr || pConfig[0] == '\0')
		{
			NormalizeQmLayoutColumns(vOut);
			return false; // 空 config：调用方据此走 SmartDefaults 回退
		}

		bool AnyParsed = false;
		std::vector<bool> vSeen(vDefaults.size(), false);
		char aEntry[128];
		const char *pEntry = pConfig;
		while((pEntry = str_next_token(pEntry, ";", aEntry, sizeof(aEntry))) != nullptr)
		{
			if(aEntry[0] == '\0')
				continue;

			char aKey[64];
			char aColumn[16] = "";
			char aOrder[16] = "";
			const char *pField = str_next_token(aEntry, ":", aKey, sizeof(aKey));
			if(aKey[0] == '\0')
				continue;

			int Index = -1;
			for(size_t i = 0; i < vDefaults.size(); ++i)
			{
				if(vDefaults[i].m_pKey != nullptr && str_comp(vDefaults[i].m_pKey, aKey) == 0)
				{
					Index = static_cast<int>(i);
					break;
				}
			}
			if(Index < 0)
				continue; // 未知 key 跳过
			if(vSeen[Index])
				continue; // 重复 key 跳过

			const SQmModuleEntry &DefaultEntry = vDefaults[Index];
			EQmModuleColumn Column = DefaultEntry.m_Column;
			int Order = DefaultEntry.m_OrderInColumn;
			bool InvalidField = false;

			if(pField != nullptr)
			{
				pField = str_next_token(pField, ":", aColumn, sizeof(aColumn));
				if(aColumn[0] != '\0')
				{
					EQmModuleColumn ParsedColumn;
					if(ParseQmModuleColumnString(aColumn, &ParsedColumn))
						Column = ParsedColumn;
					else
						InvalidField = true;
				}
			}
			if(pField != nullptr)
			{
				str_next_token(pField, ":", aOrder, sizeof(aOrder));
				if(aOrder[0] != '\0')
				{
					int ParsedOrder = 0;
					if(str_toint(aOrder, &ParsedOrder) && ParsedOrder >= 0)
						Order = ParsedOrder;
					else
						InvalidField = true;
				}
			}
			if(InvalidField)
				continue; // 非法字段跳过整条

			// Full 列保护：默认 Full 的卡强制 Full；非 Full 卡解析成 Full 回退默认列
			if(DefaultEntry.m_Column == EQmModuleColumn::Full)
				Column = EQmModuleColumn::Full;
			else if(Column == EQmModuleColumn::Full)
				Column = DefaultEntry.m_Column;

			vOut[Index].m_Column = Column;
			vOut[Index].m_OrderInColumn = Order;
			vSeen[Index] = true;
			AnyParsed = true;
		}
		NormalizeQmLayoutColumns(vOut);
		return AnyParsed;
	}

	void SerializeLegacyQmLayout(const std::vector<SQmModuleEntry> &vEntries, char *pOut, int OutSize)
	{
		if(pOut == nullptr || OutSize <= 0)
			return;
		pOut[0] = '\0';
		bool First = true;
		for(const SQmModuleEntry &Entry : vEntries)
		{
			if(Entry.m_pKey == nullptr)
				continue;
			char aEntry[128];
			str_format(aEntry, sizeof(aEntry), "%s:%s:%d", Entry.m_pKey, QmModuleColumnToString(Entry.m_Column), Entry.m_OrderInColumn);
			if(!First)
				str_append(pOut, ";", OutSize);
			str_append(pOut, aEntry, OutSize);
			First = false;
		}
	}

	bool ParseLegacyQmCollapsed(const char *pConfig, std::span<const SQmModuleEntry> Entries, std::span<bool> Collapsed)
	{
		std::fill(Collapsed.begin(), Collapsed.end(), false);
		if(pConfig == nullptr || pConfig[0] == '\0')
			return false;

		bool AnyParsed = false;
		char aEntry[128];
		const char *pEntry = pConfig;
		while((pEntry = str_next_token(pEntry, ";", aEntry, sizeof(aEntry))) != nullptr)
		{
			if(aEntry[0] == '\0')
				continue;
			char aKey[64];
			str_next_token(aEntry, ":", aKey, sizeof(aKey));
			if(aKey[0] == '\0')
				continue;
			for(const SQmModuleEntry &Entry : Entries)
			{
				const size_t StateIndex = static_cast<size_t>(Entry.m_Id);
				if(Entry.m_pKey != nullptr && StateIndex < Collapsed.size() && str_comp(Entry.m_pKey, aKey) == 0)
				{
					Collapsed[StateIndex] = true;
					AnyParsed = true;
					break;
				}
			}
		}
		return AnyParsed;
	}

	void SerializeLegacyQmCollapsed(std::span<const SQmModuleEntry> Entries, std::span<const bool> Collapsed, char *pOut, int OutSize)
	{
		if(pOut == nullptr || OutSize <= 0)
			return;
		pOut[0] = '\0';
		bool First = true;
		for(const SQmModuleEntry &Entry : Entries)
		{
			const size_t StateIndex = static_cast<size_t>(Entry.m_Id);
			if(StateIndex >= Collapsed.size() || !Collapsed[StateIndex] || Entry.m_pKey == nullptr)
				continue;
			if(!First)
				str_append(pOut, ";", OutSize);
			str_append(pOut, Entry.m_pKey, OutSize);
			First = false;
		}
	}

	namespace
	{
		std::vector<SQmModuleEntry> BuildLegacyQmDefaultsFromRegistry()
		{
			std::vector<SQmModuleEntry> vDefaults;
			vDefaults.reserve(QmModuleCount);
			for(size_t Index = 0; Index < QmModuleCount; ++Index)
			{
				const EQmModuleId Id = static_cast<EQmModuleId>(Index);
				const char *pStableId = QmModuleStableId(Id);
				const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
				if(pStableId == nullptr || pDefault == nullptr)
					continue;
				EQmModuleColumn Column = EQmModuleColumn::Left;
				if(pDefault->m_DefaultColumn == qm_card_registry::ECardColumn::Full)
					Column = EQmModuleColumn::Full;
				else if(pDefault->m_DefaultColumn == qm_card_registry::ECardColumn::Right)
					Column = EQmModuleColumn::Right;
				vDefaults.push_back({Id, Column, pDefault->m_DefaultOrder, pStableId + 3});
			}
			return vDefaults;
		}

		bool ApplyLegacyPlacements(qm_card_order::CModel &Model, const std::vector<qm_card_order::SEntry> &vEntries)
		{
			bool Applied = false;
			for(const qm_card_order::SEntry &Entry : vEntries)
			{
				if(Entry.m_pStableId == nullptr || Model.FindByStableId(Entry.m_pStableId) < 0)
					continue;
				Model.Move(Entry.m_pStableId, Entry.m_Column, Entry.m_OrderInColumn);
				Applied = true;
			}
			return Applied;
		}
	}

	bool LoadLegacyQmLayoutIntoModel(qm_card_order::CModel &Model, const char *pConfig)
	{
		const std::vector<SQmModuleEntry> vDefaults = BuildLegacyQmDefaultsFromRegistry();
		std::vector<SQmModuleEntry> vParsed;
		if(!ParseLegacyQmLayout(pConfig, vDefaults, vParsed))
			return false;

		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vParsed.size());
		for(const SQmModuleEntry &Entry : vParsed)
		{
			const char *pStableId = QmModuleStableId(Entry.m_Id);
			if(pStableId == nullptr)
				continue;
			vEntries.push_back({pStableId, nullptr, QmModuleColumnToInt(Entry.m_Column), Entry.m_OrderInColumn});
		}
		return ApplyLegacyPlacements(Model, vEntries);
	}

	bool MoveQmModuleInModel(qm_card_order::CModel &Model, EQmModuleId Id, EQmModuleColumn TargetColumn, int TargetOrder)
	{
		const char *pStableId = QmModuleStableId(Id);
		const qm_card_registry::SCardDefault *pDefault = pStableId != nullptr ? qm_card_registry::FindByStableId(pStableId) : nullptr;
		if(pDefault == nullptr || Model.FindByStableId(pStableId) < 0 || pDefault->m_DefaultColumn == qm_card_registry::ECardColumn::Full || TargetColumn == EQmModuleColumn::Full)
			return false;
		Model.Move(pStableId, QmModuleColumnToInt(TargetColumn), TargetOrder);
		return true;
	}

	bool MoveQmModuleToTabInModel(qm_card_order::CModel &Model, EQmModuleId Id, const char *pTargetTab, EQmModuleColumn TargetColumn, int TargetOrder)
	{
		const char *pStableId = QmModuleStableId(Id);
		const qm_card_registry::SCardDefault *pDefault = pStableId != nullptr ? qm_card_registry::FindByStableId(pStableId) : nullptr;
		if(pDefault == nullptr || pTargetTab == nullptr || pTargetTab[0] == '\0' || Model.FindByStableId(pStableId) < 0 || pDefault->m_DefaultColumn == qm_card_registry::ECardColumn::Full || TargetColumn == EQmModuleColumn::Full)
			return false;
		Model.MoveToTab(pStableId, pTargetTab, QmModuleColumnToInt(TargetColumn), TargetOrder);
		return true;
	}

	bool SerializeLegacyQmLayoutFromModel(const qm_card_order::CModel &Model, char *pOut, int OutSize)
	{
		if(pOut == nullptr || OutSize <= 0)
			return false;
		pOut[0] = '\0';
		bool First = true;
		for(int Index = 0; Index < Model.Count(); ++Index)
		{
			const qm_card_order::SEntry &Entry = Model.Entry(Index);
			EQmModuleId Id;
			if(!QmModuleIdFromStableId(Entry.m_pStableId, &Id))
				continue;
			char aEntry[128];
			str_format(aEntry, sizeof(aEntry), "%s:%s:%d", Entry.m_pStableId + 3, QmModuleColumnToString(QmModuleColumnFromInt(Entry.m_Column)), Entry.m_OrderInColumn);
			const int Needed = str_length(pOut) + str_length(aEntry) + (First ? 0 : 1);
			if(Needed >= OutSize)
				return false;
			if(!First)
				str_append(pOut, ";", OutSize);
			str_append(pOut, aEntry, OutSize);
			First = false;
		}
		return true;
	}

	qm_card_order::CModel &QmModuleLayoutModel()
	{
		static qm_card_order::CModel s_QmLayoutModel;
		return s_QmLayoutModel;
	}

	void LoadQmLayoutIntoModel(const char *pConfig, const std::vector<SQmModuleEntry> &vDefaults)
	{
		qm_card_order::CModel &Model = QmModuleLayoutModel();
		std::vector<SQmModuleEntry> vParsed;
		ParseLegacyQmLayout(pConfig, vDefaults, vParsed);
		std::vector<qm_card_order::SEntry> vModelEntries;
		vModelEntries.reserve(vParsed.size());
		for(const SQmModuleEntry &E : vParsed)
		{
			const char *pStable = QmModuleStableId(E.m_Id);
			if(pStable == nullptr)
				continue;
			// tab 从注册表查（全局卡全集的默认 tab；栖梦模块在注册表都有对应 qm: 条目）
			const char *pTab = nullptr;
			const qm_card_registry::SCardDefault *pReg = qm_card_registry::FindByStableId(pStable);
			if(pReg != nullptr)
				pTab = pReg->m_pDefaultTab;
			vModelEntries.push_back({pStable, pTab, QmModuleColumnToInt(E.m_Column), E.m_OrderInColumn});
		}
		Model.SetEntries(vModelEntries);
		Model.ClearDirty(); // 加载完成，清除 dirty（后续 Move 才置 dirty 触发序列化）
	}

	bool LoadQmLayoutModelFromGlobalOrder(const char *pConfig, const std::vector<SQmModuleEntry> &vDefaults)
	{
		if(pConfig == nullptr || pConfig[0] == '\0')
			return false;

		std::vector<qm_card_order::SEntry> vDefaultEntries;
		vDefaultEntries.reserve(vDefaults.size());
		for(const SQmModuleEntry &E : vDefaults)
		{
			const char *pStable = QmModuleStableId(E.m_Id);
			if(pStable == nullptr)
				continue;
			const char *pTab = nullptr;
			const qm_card_registry::SCardDefault *pReg = qm_card_registry::FindByStableId(pStable);
			if(pReg != nullptr)
				pTab = pReg->m_pDefaultTab;
			vDefaultEntries.push_back({pStable, pTab, QmModuleColumnToInt(E.m_Column), E.m_OrderInColumn});
		}

		qm_card_order::CModel &Model = QmModuleLayoutModel();
		return Model.LoadMerged(pConfig, vDefaultEntries);
	}

	std::vector<SQmModuleEntry> SyncModelToLegacyLayout()
	{
		qm_card_order::CModel &Model = QmModuleLayoutModel();
		std::vector<SQmModuleEntry> vEntries;
		vEntries.reserve(Model.Count());
		for(int i = 0; i < Model.Count(); ++i)
		{
			const qm_card_order::SEntry &E = Model.Entry(i);
			EQmModuleId Id;
			if(!QmModuleIdFromStableId(E.m_pStableId, &Id))
				continue;
			// stableId "qm:<key>" 去 "qm:" 前缀 = 持久化 key（与 s_aQmModuleDefaults.m_pKey 一致）
			const char *pKey = E.m_pStableId + 3;
			vEntries.push_back({Id, QmModuleColumnFromInt(E.m_Column), E.m_OrderInColumn, pKey});
		}
		return vEntries;
	}

	void SerializeQmLayoutFromModel(char *pOut, int OutSize)
	{
		SerializeLegacyQmLayoutFromModel(QmModuleLayoutModel(), pOut, OutSize);
	}

	bool ParseLegacyTClientLayoutEntries(const char *pConfig, std::vector<qm_card_order::SEntry> &vOut)
	{
		vOut.clear();
		if(pConfig == nullptr || pConfig[0] == '\0')
			return false;

		const char *pEntry = pConfig;
		char aToken[128];
		while((pEntry = str_next_token(pEntry, ";", aToken, sizeof(aToken))) != nullptr)
		{
			if(aToken[0] == '\0')
				continue;

			char aId[80];
			char aColumn[16];
			char aOrder[16];
			const char *pLastColon = nullptr;
			for(const char *pIt = aToken; *pIt != '\0'; ++pIt)
			{
				if(*pIt == ':')
					pLastColon = pIt;
			}
			if(pLastColon == nullptr)
				continue;
			const char *pSecondLastColon = nullptr;
			for(const char *pIt = aToken; pIt < pLastColon; ++pIt)
			{
				if(*pIt == ':')
					pSecondLastColon = pIt;
			}
			if(pSecondLastColon == nullptr)
				continue;
			const int IdLen = (int)(pSecondLastColon - aToken);
			const int ColumnLen = (int)(pLastColon - pSecondLastColon - 1);
			if(IdLen <= 0 || IdLen >= (int)sizeof(aId) || ColumnLen <= 0 || ColumnLen >= (int)sizeof(aColumn))
				continue;
			str_copy(aId, aToken, IdLen + 1);
			str_copy(aColumn, pSecondLastColon + 1, ColumnLen + 1);
			str_copy(aOrder, pLastColon + 1, sizeof(aOrder));

			int LegacyColumn = 0;
			if(!str_toint(aColumn, &LegacyColumn))
				continue;
			int Order = 0;
			if(!str_toint(aOrder, &Order) || Order < 0)
				continue;
			int Column = -1;
			if(LegacyColumn == 0)
				Column = 1;
			else if(LegacyColumn == 1)
				Column = 2;
			else
				continue;

			const qm_card_registry::SCardDefault *pDefault = str_startswith(aId, "tclient:") != nullptr ? qm_card_registry::FindByStableId(aId) : nullptr;
			if(pDefault == nullptr)
				continue;
			vOut.push_back({pDefault->m_pStableId, pDefault->m_pDefaultTab, Column, Order});
		}
		return !vOut.empty();
	}

	bool LoadLegacyTClientLayoutIntoModel(qm_card_order::CModel &Model, const char *pConfig)
	{
		std::vector<qm_card_order::SEntry> vEntries;
		return ParseLegacyTClientLayoutEntries(pConfig, vEntries) && ApplyLegacyPlacements(Model, vEntries);
	}

	bool SerializeMergedGlobalCardOrderFromQmModel(const char *pExistingGlobalOrder, char *pOut, int OutSize)
	{
		if(pOut == nullptr || OutSize <= 0)
			return false;
		pOut[0] = '\0';

		auto AppendToken = [&](const char *pToken, bool *pFirst) {
			if(pToken == nullptr || pFirst == nullptr)
				return false;
			const int Needed = str_length(pOut) + str_length(pToken) + (*pFirst ? 0 : 1);
			if(Needed >= OutSize)
				return false;
			if(!*pFirst)
				str_append(pOut, ";", OutSize);
			str_append(pOut, pToken, OutSize);
			*pFirst = false;
			return true;
		};

		qm_card_order::CModel &Model = QmModuleLayoutModel();
		bool First = true;
		if(pExistingGlobalOrder != nullptr && pExistingGlobalOrder[0] != '\0')
		{
			char aToken[160];
			const char *pEntry = pExistingGlobalOrder;
			while((pEntry = str_next_token(pEntry, ";", aToken, sizeof(aToken))) != nullptr)
			{
				if(aToken[0] == '\0')
					continue;
				if(str_startswith(aToken, "qm:") != nullptr)
				{
					char aStableId[128];
					str_next_token(aToken, "|", aStableId, sizeof(aStableId));
					if(Model.FindByStableId(aStableId) >= 0)
						continue; // Qm 子模型里的卡由后续 Model.Serialize 全量重写
					if(qm_card_registry::FindByStableId(aStableId) == nullptr)
						continue; // 注册表外旧残留不再带回全局配置
				}
				if(!AppendToken(aToken, &First))
					return false;
			}
		}

		char aQmEntries[4096];
		if(!Model.Serialize(aQmEntries, sizeof(aQmEntries)))
			return false;
		char aToken[160];
		const char *pEntry = aQmEntries;
		while((pEntry = str_next_token(pEntry, ";", aToken, sizeof(aToken))) != nullptr)
		{
			if(aToken[0] == '\0')
				continue;
			if(!AppendToken(aToken, &First))
				return false;
		}
		if(pOut[0] != '\0')
		{
			if(str_length(pOut) + 1 >= OutSize)
				return false;
			str_append(pOut, ";", OutSize);
		}
		return true;
	}

	bool MigrateQmLayoutToGlobalCardOrder(const std::vector<SQmModuleEntry> &vDefaults)
	{
		if(g_Config.m_QmCardOrderMigrated != 0)
			return false;
		if(g_Config.m_QmGlobalCardOrder[0] != '\0')
		{
			g_Config.m_QmCardOrderMigrated = 1;
			return false;
		}

		LoadQmLayoutIntoModel(g_Config.m_QmSidebarCardOrder, vDefaults);
		qm_card_order::CModel DefaultGlobalModel;
		DefaultGlobalModel.SetEntries(qm_card_registry::BuildDefaultEntries());
		DefaultGlobalModel.ClearDirty();
		char aDefaultGlobalOrder[sizeof(g_Config.m_QmGlobalCardOrder)];
		if(!DefaultGlobalModel.Serialize(aDefaultGlobalOrder, sizeof(aDefaultGlobalOrder)))
			return false;
		char aMigratedGlobalOrder[sizeof(g_Config.m_QmGlobalCardOrder)];
		if(!SerializeMergedGlobalCardOrderFromQmModel(aDefaultGlobalOrder, aMigratedGlobalOrder, sizeof(aMigratedGlobalOrder)))
			return false;
		std::vector<qm_card_order::SEntry> vLegacyTClientEntries;
		if(ParseLegacyTClientLayoutEntries(g_Config.m_QmSettingsCardOrder, vLegacyTClientEntries))
		{
			char aTClientMigratedOrder[sizeof(g_Config.m_QmGlobalCardOrder)];
			if(!qm_card_order::SerializeMergedReplacingPrefix(aMigratedGlobalOrder, "tclient:", vLegacyTClientEntries, aTClientMigratedOrder, sizeof(aTClientMigratedOrder)))
				return false;
			str_copy(aMigratedGlobalOrder, aTClientMigratedOrder, sizeof(aMigratedGlobalOrder));
		}
		str_copy(g_Config.m_QmGlobalCardOrder, aMigratedGlobalOrder, sizeof(g_Config.m_QmGlobalCardOrder));
		g_Config.m_QmCardOrderMigrated = 1;
		return g_Config.m_QmGlobalCardOrder[0] != '\0';
	}

	bool MoveQmModuleInModel(EQmModuleId Id, EQmModuleColumn TargetColumn, int TargetOrder)
	{
		return MoveQmModuleInModel(QmModuleLayoutModel(), Id, TargetColumn, TargetOrder);
	}

	bool MoveQmModuleToTabInModel(EQmModuleId Id, const char *pTargetTab, EQmModuleColumn TargetColumn, int TargetOrder)
	{
		return MoveQmModuleToTabInModel(QmModuleLayoutModel(), Id, pTargetTab, TargetColumn, TargetOrder);
	}

	bool IsQmLayoutModelDirty() { return QmModuleLayoutModel().IsDirty(); }
	void ClearQmLayoutModelDirty() { QmModuleLayoutModel().ClearDirty(); }
} // namespace qm_module
