// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_bind_status_hud.h"

EQmBindStatusTone QmResolveBuiltinBindStatusTone(EQmBindStatusLine Line, int Value)
{
	switch(Line)
	{
	case EQmBindStatusLine::KEY_STICKING:
		switch(Value)
		{
		case 0: return EQmBindStatusTone::OK; // Key Sticking: On
		case 1: return EQmBindStatusTone::DANGER; // Key Sticking: Off
		case 2: return EQmBindStatusTone::WARNING; // Key Sticking: Reset Self
		default: return EQmBindStatusTone::NONE; // 越界值显示 "Key Sticking: ?"，不配色
		}
	case EQmBindStatusLine::HAMMER:
		switch(Value)
		{
		case 0: return EQmBindStatusTone::OK; // Hammer: Normal
		case 1: return EQmBindStatusTone::DANGER; // Hammer: DF
		case 2: return EQmBindStatusTone::WARNING; // Hammer: HDF
		case 3: return EQmBindStatusTone::WARNING; // Hammer: Custom
		default: return EQmBindStatusTone::NONE;
		}
	case EQmBindStatusLine::DUMMY_CONTROL:
	case EQmBindStatusLine::DUMMY_COPY:
		return Value != 0 ? EQmBindStatusTone::OK : EQmBindStatusTone::DANGER;
	}
	return EQmBindStatusTone::NONE;
}

std::string QmSerializeBindStatusList(const std::vector<SQmBindStatusEntry> &vEntries)
{
	std::string Out;
	for(size_t i = 0; i < vEntries.size(); ++i)
	{
		if(i > 0)
			Out += "; ";
		const SQmBindStatusEntry &Entry = vEntries[i];
		Out += Entry.m_VarName;
		for(const auto &Pair : Entry.m_vValueTexts)
		{
			Out += "|";
			Out += std::to_string(Pair.first);
			Out += "=";
			Out += Pair.second;
		}
		if(Entry.m_HasNonZeroText)
		{
			Out += "|";
			Out += Entry.m_NonZeroText;
		}
	}
	return Out;
}

SQmBindStatusPanelSize QmComputeBindStatusPanelSize(int LineCount, float MaxLineWidth, float LineHeight, float PaddingX, float PaddingY)
{
	SQmBindStatusPanelSize Size{};
	if(LineCount <= 0)
		return Size; // 没有可见行时不绘制面板
	Size.m_W = MaxLineWidth + PaddingX * 2.0f;
	Size.m_H = LineHeight * (float)LineCount + PaddingY * 2.0f;
	return Size;
}

std::vector<SQmBindStatusRenderLine> QmBuildBindStatusRenderLines(bool CustomActive, const std::vector<std::string> &vCustomLines, const std::vector<SQmBindStatusBuiltinLine> &vBuiltinLines)
{
	std::vector<SQmBindStatusRenderLine> vResult;
	if(CustomActive)
	{
		// 自定义列表完全替换内置四项：这里只输出自定义行，内置行一条都不追加，
		// 否则内置行会画在自定义行下方、跑出按自定义行数算出的面板背景
		vResult.reserve(vCustomLines.size());
		for(const std::string &Line : vCustomLines)
			vResult.push_back({Line, EQmBindStatusTone::NONE});
		return vResult;
	}

	vResult.reserve(vBuiltinLines.size());
	for(const SQmBindStatusBuiltinLine &Line : vBuiltinLines)
	{
		if(!Line.m_Show)
			continue;
		vResult.push_back({Line.m_pText != nullptr ? Line.m_pText : "", Line.m_Tone});
	}
	return vResult;
}

void CQmBindStatusHud::OnConsoleInit()
{
	Console()->Register("qm_bind_status_reset", "", CFGFLAG_CLIENT, ConResetDefaults, this, "Clear qm_bind_status_items and use the built-in four entries (key stuck/hammer/dummy control/dummy copy)");
}

void CQmBindStatusHud::ConResetDefaults(IConsole::IResult *, void *pUserData)
{
	CQmBindStatusHud *pSelf = static_cast<CQmBindStatusHud *>(pUserData);
	// 默认态即空列表：清空后回落到内置四项（经 Localize 显示当前语言），
	// 不再把内置项的英文模板序列化进配置，否则自定义列表会以英文固化显示
	pSelf->Config()->m_QmBindStatusItems[0] = '\0';
	pSelf->m_LastConfig.clear();
	pSelf->m_ConfigValid = false;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "qm_bind_status", "qm_bind_status_items cleared, built-in four entries restored");
}

bool CQmBindStatusHud::IsCustomListActive() const
{
	return Config()->m_QmBindStatusItems[0] != '\0';
}

const std::vector<std::string> &CQmBindStatusHud::GetVisibleLines()
{
	const char *pConfig = Config()->m_QmBindStatusItems;
	if(!m_ConfigValid || str_comp(pConfig, m_LastConfig.c_str()) != 0)
	{
		m_LastConfig = pConfig;
		Rebuild();
		m_ConfigValid = true;
	}

	// 变量值变化时重建可见行（避免每帧字符串分配）
	bool ValuesChanged = m_vCachedValues.size() != m_vpVariables.size();
	if(!ValuesChanged)
	{
		for(size_t i = 0; i < m_vpVariables.size(); ++i)
		{
			const int Value = m_vpVariables[i] != nullptr ? *m_vpVariables[i]->m_pVariable : 0;
			if(Value != m_vCachedValues[i])
			{
				ValuesChanged = true;
				break;
			}
		}
	}
	if(ValuesChanged)
	{
		m_vVisibleLines.clear();
		m_vCachedValues.clear();
		for(size_t i = 0; i < m_vEntries.size(); ++i)
		{
			const bool Resolved = i < m_vpVariables.size() && m_vpVariables[i] != nullptr;
			const int Value = Resolved ? *m_vpVariables[i]->m_pVariable : 0;
			m_vCachedValues.push_back(Value);
			if(!Resolved)
				continue;
			std::string Text;
			if(QmResolveBindStatusEntry(m_vEntries[i], Value, Text))
				m_vVisibleLines.push_back(std::move(Text));
		}
	}
	return m_vVisibleLines;
}

void CQmBindStatusHud::Rebuild()
{
	m_vEntries.clear();
	m_vpVariables.clear();
	m_vVisibleLines.clear();
	m_vCachedValues.clear();
	if(!IsCustomListActive())
		return;
	QmParseBindStatusList(Config()->m_QmBindStatusItems, m_vEntries);
	ResolveVariables();
}

void CQmBindStatusHud::ResolveVariables()
{
	m_vpVariables.assign(m_vEntries.size(), nullptr);
	for(size_t i = 0; i < m_vEntries.size(); ++i)
	{
		struct SLookup
		{
			const char *m_pName;
			const SIntConfigVariable *m_pVariable = nullptr;
		} Lookup{m_vEntries[i].m_VarName.c_str()};
		ConfigManager()->PossibleConfigVariables(Lookup.m_pName, CFGFLAG_CLIENT, [](const SConfigVariable *pVariable, void *pUserData) {
			auto *pLookup = static_cast<SLookup *>(pUserData);
			if(pLookup->m_pVariable == nullptr && pVariable->m_Type == SConfigVariable::VAR_INT && str_comp(pVariable->m_pScriptName, pLookup->m_pName) == 0)
				pLookup->m_pVariable = static_cast<const SIntConfigVariable *>(pVariable); }, &Lookup);
		m_vpVariables[i] = Lookup.m_pVariable;
	}
}
