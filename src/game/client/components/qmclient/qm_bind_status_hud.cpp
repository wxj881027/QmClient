// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_bind_status_hud.h"

#include <base/system.h>

#include <engine/console.h>

#include <string>
#include <vector>

namespace
{
	bool IsSpace(char c)
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	std::string TrimCopy(const std::string &Str)
	{
		size_t Begin = 0;
		while(Begin < Str.size() && IsSpace(Str[Begin]))
			++Begin;
		size_t End = Str.size();
		while(End > Begin && IsSpace(Str[End - 1]))
			--End;
		return Str.substr(Begin, End - Begin);
	}

	void SplitTokens(const char *pText, char Separator, std::vector<std::string> &vOut)
	{
		vOut.clear();
		if(pText == nullptr)
			return;
		const char *pStart = pText;
		for(const char *p = pText;; ++p)
		{
			if(*p == Separator || *p == '\0')
			{
				vOut.emplace_back(pStart, p - pStart);
				if(*p == '\0')
					break;
				pStart = p + 1;
			}
		}
	}

	// 严格整数解析：只接受可选负号 + 十进制数字，非法返回 false
	bool ParseIntStrict(const char *pStr, int &Out)
	{
		if(pStr == nullptr || *pStr == '\0')
			return false;
		const char *p = pStr;
		bool Negative = false;
		if(*p == '-')
		{
			Negative = true;
			++p;
		}
		if(*p == '\0')
			return false;
		long long Value = 0;
		for(; *p != '\0'; ++p)
		{
			if(*p < '0' || *p > '9')
				return false;
			Value = Value * 10 + (*p - '0');
			if(Value > 1000000LL)
				return false; // 防御溢出
		}
		Out = (int)(Negative ? -Value : Value);
		return true;
	}

	// 配置变量脚本名合法性：首字符为字母或下划线，其余为字母/数字/下划线
	bool IsValidVariableName(const std::string &Name)
	{
		if(Name.empty())
			return false;
		const auto IsNameChar = [](char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		};
		const char First = Name[0];
		if(!((First >= 'a' && First <= 'z') || (First >= 'A' && First <= 'Z') || First == '_'))
			return false;
		for(char c : Name)
		{
			if(!IsNameChar(c))
				return false;
		}
		return true;
	}
} // namespace

bool QmParseBindStatusList(const char *pList, std::vector<SQmBindStatusEntry> &vOut)
{
	vOut.clear();
	if(pList == nullptr)
		return false;

	std::vector<std::string> vEntryTokens;
	SplitTokens(pList, ';', vEntryTokens);
	for(const std::string &EntryToken : vEntryTokens)
	{
		std::vector<std::string> vFields;
		SplitTokens(EntryToken.c_str(), '|', vFields);
		if(vFields.empty())
			continue;
		const std::string VarName = TrimCopy(vFields[0]);
		if(!IsValidVariableName(VarName))
			continue;

		SQmBindStatusEntry Entry;
		Entry.m_VarName = VarName;
		bool HasField = false;
		for(size_t i = 1; i < vFields.size(); ++i)
		{
			const std::string Field = TrimCopy(vFields[i]);
			if(Field.empty())
				continue;
			HasField = true;
			const size_t EqPos = Field.find('=');
			if(EqPos == std::string::npos)
			{
				// 非零开关文本（仅取第一个）
				if(!Entry.m_HasNonZeroText)
				{
					Entry.m_NonZeroText = Field;
					Entry.m_HasNonZeroText = true;
				}
			}
			else
			{
				const std::string ValuePart = TrimCopy(Field.substr(0, EqPos));
				const std::string TextPart = TrimCopy(Field.substr(EqPos + 1));
				if(TextPart.empty())
					continue;
				int MatchValue = 0;
				if(!ParseIntStrict(ValuePart.c_str(), MatchValue))
					continue;
				// 重复状态值后者覆盖
				bool Found = false;
				for(auto &Pair : Entry.m_vValueTexts)
				{
					if(Pair.first == MatchValue)
					{
						Pair.second = TextPart;
						Found = true;
						break;
					}
				}
				if(!Found)
					Entry.m_vValueTexts.emplace_back(MatchValue, TextPart);
			}
		}
		if(!HasField)
		{
			// 仅填变量名：非零时显示变量名本身
			Entry.m_NonZeroText = VarName;
			Entry.m_HasNonZeroText = true;
		}
		vOut.push_back(std::move(Entry));
	}
	return true;
}

bool QmResolveBindStatusEntry(const SQmBindStatusEntry &Entry, int Value, std::string &OutText)
{
	for(const auto &Pair : Entry.m_vValueTexts)
	{
		if(Pair.first == Value)
		{
			OutText = Pair.second;
			return true;
		}
	}
	if(Entry.m_HasNonZeroText && Value != 0)
	{
		OutText = Entry.m_NonZeroText;
		return true;
	}
	return false;
}

const std::vector<SQmBindStatusEntry> &QmDefaultBindStatusEntries()
{
	static const std::vector<SQmBindStatusEntry> s_vDefaults = []() {
		std::vector<SQmBindStatusEntry> v;
		// 卡键状态（对应 cl_dummy_resetonswitch）
		SQmBindStatusEntry Key;
		Key.m_VarName = "cl_dummy_resetonswitch";
		Key.m_vValueTexts = {{0, "Key Sticking: On"}, {1, "Key Sticking: Off"}, {2, "Key Sticking: Reset Self"}};
		v.push_back(std::move(Key));
		// 锤子状态（对应 qm_deepfly_mode）
		SQmBindStatusEntry Hammer;
		Hammer.m_VarName = "qm_deepfly_mode";
		Hammer.m_vValueTexts = {{0, "Hammer: Normal"}, {1, "Hammer: DF"}, {2, "Hammer: HDF"}, {3, "Hammer: Custom"}};
		v.push_back(std::move(Hammer));
		// 分控状态（对应 cl_dummy_control）
		SQmBindStatusEntry Control;
		Control.m_VarName = "cl_dummy_control";
		Control.m_vValueTexts = {{0, "Dummy Control: Off"}, {1, "Dummy Control: On"}};
		v.push_back(std::move(Control));
		// 同步状态（对应 cl_dummy_copy_moves）
		SQmBindStatusEntry Sync;
		Sync.m_VarName = "cl_dummy_copy_moves";
		Sync.m_vValueTexts = {{0, "Dummy copy: Off"}, {1, "Dummy copy: On"}};
		v.push_back(std::move(Sync));
		return v;
	}();
	return s_vDefaults;
}

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

void CQmBindStatusHud::OnConsoleInit()
{
	Console()->Register("qm_bind_status_reset", "", CFGFLAG_CLIENT, ConResetDefaults, this, "Reset qm_bind_status_items to the built-in four entries (key stuck/hammer/dummy control/dummy copy)");
}

void CQmBindStatusHud::ConResetDefaults(IConsole::IResult *, void *pUserData)
{
	CQmBindStatusHud *pSelf = static_cast<CQmBindStatusHud *>(pUserData);
	const std::string Serialized = QmSerializeBindStatusList(QmDefaultBindStatusEntries());
	str_copy(pSelf->Config()->m_QmBindStatusItems, Serialized.c_str(), sizeof(pSelf->Config()->m_QmBindStatusItems));
	pSelf->m_LastConfig.clear();
	pSelf->m_ConfigValid = false;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "qm_bind_status", "qm_bind_status_items reset to the built-in four entries");
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
