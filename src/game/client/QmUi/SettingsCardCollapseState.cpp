#include "SettingsCardCollapseState.h"

#include "QmModuleLayoutAdapter.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <utility>

namespace qm_card_collapse
{
	namespace
	{
		CState &GlobalState()
		{
			static CState s_State;
			return s_State;
		}

		std::string NextToken(const char *&pCursor)
		{
			if(pCursor == nullptr || *pCursor == '\0')
				return {};
			const char *pStart = pCursor;
			while(*pCursor != '\0' && *pCursor != ';')
				++pCursor;
			std::string Token(pStart, pCursor - pStart);
			if(*pCursor == ';')
				++pCursor;
			return Token;
		}

		char s_aSerializedCache[sizeof(g_Config.m_QmSettingsCardCollapsed)] = {};
		bool s_Initialized = false;

		bool ValidStableId(const char *pStableId)
		{
			if(pStableId == nullptr || pStableId[0] == '\0' || pStableId[0] == '!')
				return false;
			for(const char *pChar = pStableId; *pChar != '\0'; ++pChar)
			{
				if(*pChar == ';' || (unsigned char)*pChar < 32 || *pChar == 127)
					return false;
			}
			return true;
		}

		bool PersistGlobalState()
		{
			char aSerialized[sizeof(g_Config.m_QmSettingsCardCollapsed)];
			if(GlobalState().Serialize(aSerialized, sizeof(aSerialized)))
			{
				str_copy(g_Config.m_QmSettingsCardCollapsed, aSerialized, sizeof(g_Config.m_QmSettingsCardCollapsed));
				str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed, sizeof(s_aSerializedCache));
				return true;
			}
			return false;
		}
	}

	bool CState::Load(const char *pSerialized)
	{
		m_States.clear();
		const char *pCursor = pSerialized != nullptr ? pSerialized : "";
		while(*pCursor != '\0')
		{
			const std::string Token = NextToken(pCursor);
			if(!Token.empty())
				SetCollapsed(Token.c_str() + (Token[0] == '!' ? 1 : 0), Token[0] != '!');
		}
		return true;
	}

	bool CState::ImportLegacyQm(const char *pSerialized)
	{
		bool Imported = false;
		const char *pCursor = pSerialized != nullptr ? pSerialized : "";
		while(*pCursor != '\0')
		{
			const std::string Token = NextToken(pCursor);
			const size_t Suffix = Token.find(':');
			const std::string Key = Token.substr(0, Suffix);
			if(Key.empty())
				continue;

			char aStableId[128];
			str_format(aStableId, sizeof(aStableId), "qm:%s", Key.c_str());
			qm_module::EQmModuleId Id;
			if(qm_module::QmModuleIdFromStableId(aStableId, &Id) && m_States.emplace(qm_module::QmModuleStableId(Id), true).second)
				Imported = true;
		}
		return Imported;
	}

	bool CState::IsCollapsed(const char *pStableId, const bool DefaultValue) const
	{
		if(pStableId == nullptr || pStableId[0] == '\0')
			return DefaultValue;
		const auto It = m_States.find(pStableId);
		return It != m_States.end() ? It->second : DefaultValue;
	}

	bool CState::SetCollapsed(const char *pStableId, const bool Collapsed)
	{
		if(!ValidStableId(pStableId))
			return false;
		const auto It = m_States.find(pStableId);
		if(It != m_States.end() && It->second == Collapsed)
			return false;
		m_States[pStableId] = Collapsed;
		return true;
	}

	bool CState::Serialize(char *pOut, const size_t OutSize) const
	{
		if(pOut == nullptr || OutSize == 0)
			return false;
		pOut[0] = '\0';
		std::string Serialized;
		for(const auto &[StableId, Collapsed] : m_States)
		{
			if(!Serialized.empty())
				Serialized += ';';
			if(!Collapsed)
				Serialized += '!';
			Serialized += StableId;
		}
		if(Serialized.size() >= OutSize)
			return false;
		str_copy(pOut, Serialized.c_str(), OutSize);
		return true;
	}

	void SyncFromConfig()
	{
		const bool ConfigChanged = !s_Initialized || str_comp(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed) != 0;
		if(!ConfigChanged && g_Config.m_QmSettingsCardCollapseMigrated)
			return;

		GlobalState().Load(g_Config.m_QmSettingsCardCollapsed);
		if(!g_Config.m_QmSettingsCardCollapseMigrated)
		{
			const CState Previous = GlobalState();
			if(g_Config.m_QmSettingsCardCollapsed[0] == '\0')
				GlobalState().ImportLegacyQm(g_Config.m_QmSidebarCardCollapsed);
			if(!PersistGlobalState())
			{
				GlobalState() = Previous;
				return;
			}
			g_Config.m_QmSettingsCardCollapseMigrated = 1;
		}
		str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed, sizeof(s_aSerializedCache));
		s_Initialized = true;
	}

	const CState &CurrentState()
	{
		SyncFromConfig();
		return GlobalState();
	}

	bool IsCollapsed(const char *pStableId, const bool DefaultValue)
	{
		return CurrentState().IsCollapsed(pStableId, DefaultValue);
	}

	bool SetCollapsed(const char *pStableId, const bool Collapsed)
	{
		SyncFromConfig();
		CState &State = GlobalState();
		CState Previous = State;
		if(!State.SetCollapsed(pStableId, Collapsed))
			return false;
		char aSerialized[sizeof(g_Config.m_QmSettingsCardCollapsed)];
		if(!State.Serialize(aSerialized, sizeof(aSerialized)))
		{
			State = std::move(Previous);
			return false;
		}
		str_copy(g_Config.m_QmSettingsCardCollapsed, aSerialized, sizeof(g_Config.m_QmSettingsCardCollapsed));
		str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed, sizeof(s_aSerializedCache));
		return true;
	}

	void SyncQmModules(std::array<bool, qm_module::QmModuleCount> &Collapsed)
	{
		const CState &State = CurrentState();
		for(size_t Index = 0; Index < Collapsed.size(); ++Index)
			Collapsed[Index] = State.IsCollapsed(qm_module::QmModuleStableId(static_cast<qm_module::EQmModuleId>(Index)), false);
	}

	bool SetQmModuleCollapsed(const qm_module::EQmModuleId Id, const bool Collapsed)
	{
		return SetCollapsed(qm_module::QmModuleStableId(Id), Collapsed);
	}
}
