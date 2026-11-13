#include "SettingsCardCollapseState.h"

#include "QmModuleLayoutAdapter.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <new>
#include <stdexcept>
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

		bool TrySyncFromConfig()
		{
			const bool ConfigChanged = !s_Initialized || str_comp(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed) != 0;
			if(!ConfigChanged && g_Config.m_QmSettingsCardCollapseMigrated)
				return true;

			try
			{
				// 配置解析与既有导入全部完成后再发布，失败不留下半份状态。
				CState Candidate;
				if(!Candidate.Load(g_Config.m_QmSettingsCardCollapsed))
					return false;
				const bool NeedsImport = !g_Config.m_QmSettingsCardCollapseMigrated;
				char aSerialized[sizeof(g_Config.m_QmSettingsCardCollapsed)];
				if(NeedsImport)
				{
					if(g_Config.m_QmSettingsCardCollapsed[0] == '\0')
						Candidate.ImportLegacyQm(g_Config.m_QmSidebarCardCollapsed);
					if(!Candidate.Serialize(aSerialized, sizeof(aSerialized)))
						return false;
				}
				GlobalState() = std::move(Candidate);
				if(NeedsImport)
				{
					str_copy(g_Config.m_QmSettingsCardCollapsed, aSerialized, sizeof(g_Config.m_QmSettingsCardCollapsed));
					g_Config.m_QmSettingsCardCollapseMigrated = 1;
				}
				str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed, sizeof(s_aSerializedCache));
				s_Initialized = true;
				return true;
			}
			catch(const std::bad_alloc &)
			{
			}
			catch(const std::length_error &)
			{
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
		size_t Remaining = OutSize - 1;
		bool First = true;
		for(const auto &[StableId, Collapsed] : m_States)
		{
			const size_t Prefix = (First ? 0 : 1) + (Collapsed ? 0 : 1);
			if(Prefix > Remaining)
				return false;
			Remaining -= Prefix;
			if(StableId.size() > Remaining)
				return false;
			Remaining -= StableId.size();
			First = false;
		}
		char *pWrite = pOut;
		for(const auto &[StableId, Collapsed] : m_States)
		{
			if(pWrite != pOut)
				*pWrite++ = ';';
			if(!Collapsed)
				*pWrite++ = '!';
			mem_copy(pWrite, StableId.data(), StableId.size());
			pWrite += StableId.size();
		}
		*pWrite = '\0';
		return true;
	}

	void SyncFromConfig()
	{
		TrySyncFromConfig();
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
		// 同步失败时拒绝本次点击，避免用旧缓存覆盖刚修改的配置。
		if(!TrySyncFromConfig())
			return false;
		try
		{
			CState Candidate = GlobalState();
			if(!Candidate.SetCollapsed(pStableId, Collapsed))
				return false;
			char aSerialized[sizeof(g_Config.m_QmSettingsCardCollapsed)];
			if(!Candidate.Serialize(aSerialized, sizeof(aSerialized)))
				return false;
			GlobalState() = std::move(Candidate);
			str_copy(g_Config.m_QmSettingsCardCollapsed, aSerialized, sizeof(g_Config.m_QmSettingsCardCollapsed));
			str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardCollapsed, sizeof(s_aSerializedCache));
			return true;
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
		return false;
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
