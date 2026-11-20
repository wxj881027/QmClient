#include "SettingsCardWidthState.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <new>
#include <utility>

namespace qm_card_width
{
	namespace
	{
		// 启动时准备空状态，首次同步失败时仍可按卡片默认宽度布局。
		CState s_State;
		char s_aSerializedCache[sizeof(g_Config.m_QmSettingsCardWidths)] = {};
		bool s_Initialized = false;

		bool TrySyncFromConfig()
		{
			if(s_Initialized && str_comp(s_aSerializedCache, g_Config.m_QmSettingsCardWidths) == 0)
				return true;
			if(!s_State.Load(g_Config.m_QmSettingsCardWidths))
				return false;
			str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardWidths);
			s_Initialized = true;
			return true;
		}
	}

	bool CState::Load(const char *pSerialized)
	{
		if(pSerialized == nullptr || pSerialized[0] == '\0')
			return m_States.Load(pSerialized);
		try
		{
			qm_card_collapse::CState Candidate;
			if(!Candidate.Load(pSerialized))
				return false;
			m_States = std::move(Candidate);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
	}

	bool CState::IsFullWidth(const char *pStableId, const bool DefaultValue) const
	{
		return m_States.IsCollapsed(pStableId, DefaultValue);
	}

	bool CState::SetFullWidth(const char *pStableId, const bool FullWidth)
	{
		return m_States.SetCollapsed(pStableId, FullWidth);
	}

	bool CState::Serialize(char *pOut, const size_t OutSize) const
	{
		return m_States.Serialize(pOut, OutSize);
	}

	void SyncFromConfig()
	{
		TrySyncFromConfig();
	}

	const CState &CurrentState()
	{
		SyncFromConfig();
		return s_State;
	}

	bool IsFullWidth(const char *pStableId, const bool DefaultValue)
	{
		return CurrentState().IsFullWidth(pStableId, DefaultValue);
	}

	bool SetFullWidth(const char *pStableId, const bool FullWidth)
	{
		if(!TrySyncFromConfig())
			return false;
		try
		{
			CState Candidate = s_State;
			if(!Candidate.SetFullWidth(pStableId, FullWidth))
				return false;
			char aSerialized[sizeof(g_Config.m_QmSettingsCardWidths)];
			if(!Candidate.Serialize(aSerialized, sizeof(aSerialized)))
				return false;
			// 配置完整写入后才发布状态，容量不足时保留原宽度。
			str_copy(g_Config.m_QmSettingsCardWidths, aSerialized);
			str_copy(s_aSerializedCache, aSerialized);
			s_State = std::move(Candidate);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
	}
}
