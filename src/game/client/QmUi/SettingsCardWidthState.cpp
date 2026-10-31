#include "SettingsCardWidthState.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <utility>

namespace qm_card_width
{
	namespace
	{
		CState &GlobalState()
		{
			static CState s_State;
			return s_State;
		}

		char s_aSerializedCache[sizeof(g_Config.m_QmSettingsCardWidths)] = {};
		bool s_Initialized = false;
	}

	bool CState::Load(const char *pSerialized)
	{
		return m_States.Load(pSerialized);
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
		if(s_Initialized && str_comp(s_aSerializedCache, g_Config.m_QmSettingsCardWidths) == 0)
			return;
		GlobalState().Load(g_Config.m_QmSettingsCardWidths);
		str_copy(s_aSerializedCache, g_Config.m_QmSettingsCardWidths);
		s_Initialized = true;
	}

	const CState &CurrentState()
	{
		SyncFromConfig();
		return GlobalState();
	}

	bool IsFullWidth(const char *pStableId, const bool DefaultValue)
	{
		return CurrentState().IsFullWidth(pStableId, DefaultValue);
	}

	bool SetFullWidth(const char *pStableId, const bool FullWidth)
	{
		CState Candidate = CurrentState();
		if(!Candidate.SetFullWidth(pStableId, FullWidth))
			return false;
		char aSerialized[sizeof(g_Config.m_QmSettingsCardWidths)];
		if(!Candidate.Serialize(aSerialized, sizeof(aSerialized)))
			return false;
		// 配置完整写入后才发布状态，容量不足时保留原宽度。
		str_copy(g_Config.m_QmSettingsCardWidths, aSerialized);
		str_copy(s_aSerializedCache, aSerialized);
		GlobalState() = std::move(Candidate);
		return true;
	}
}
