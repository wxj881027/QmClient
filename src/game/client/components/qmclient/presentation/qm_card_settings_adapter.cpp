/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_card_settings_adapter.h"

#include <engine/shared/config.h>

namespace
{
bool IsHudCard(const SCardDescriptor &Card)
{
	return Card.m_Id == "ddnet.hud" && Card.m_Owner == ECardOwner::UPSTREAM && Card.m_PresentationId == "toggle";
}

bool IsDiagnosticsCard(const SCardDescriptor &Card)
{
	return Card.m_Id == "qm.diagnostics" && Card.m_Owner == ECardOwner::QM && Card.m_PresentationId == "toggle";
}
}

std::optional<SCardSettingSnapshot> CQmCardSettingsAdapter::Read(const SCardDescriptor &Card) const
{
	if(IsHudCard(Card))
		return SCardSettingSnapshot{m_Config.m_ClShowhud, DefaultConfig::ClShowhud, 0, 1, false};
	if(IsDiagnosticsCard(Card))
		return SCardSettingSnapshot{m_Config.m_QmDiagnostics, DefaultConfig::QmDiagnostics, 0, 1, true};
	return std::nullopt;
}

ECardSettingResult CQmCardSettingsAdapter::Apply(const SCardDescriptor &Card, int Value)
{
	const auto Snapshot = Read(Card);
	if(!Snapshot)
		return ECardSettingResult::UNSUPPORTED;
	if(Value < Snapshot->m_Min || Value > Snapshot->m_Max)
		return ECardSettingResult::INVALID_VALUE;
	// 与官方 checkbox 共用同一配置字段，不创建副本，不改变默认值或执行链。
	if(IsHudCard(Card))
		m_Config.m_ClShowhud = Value;
	else if(IsDiagnosticsCard(Card))
		m_Config.m_QmDiagnostics = Value;
	else
		return ECardSettingResult::UNSUPPORTED;
	return ECardSettingResult::APPLIED;
}

bool RegisterQmSettingsAdapterCards(CCardRegistry &Registry)
{
	if(!Registry.RegisterPage({"official", "Settings", 100, {"ddnet.hud"}}))
		return false;
	return Registry.RegisterCard({"ddnet.hud", "Show ingame HUD", {}, "activity", {}, {"hud", "interface"}, ECardOwner::UPSTREAM, 0, true, "toggle"});
}
