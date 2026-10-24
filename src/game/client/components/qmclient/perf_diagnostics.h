// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PERF_DIAGNOSTICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PERF_DIAGNOSTICS_H

#include "perf_logging.h"

#include <engine/shared/packer.h>

#include <array>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct SQmPerfWireMessage
{
	int m_Id = -1;
	int m_System = -1;
};

// 只读取线上的消息编号，不保留包体；未知或损坏的包头保持 unavailable。
inline SQmPerfWireMessage QmPerfWireMessage(const void *pData, int Size)
{
	if(pData == nullptr || Size <= 0)
		return {};
	CUnpacker Unpacker;
	Unpacker.Reset(pData, Size);
	const int Header = Unpacker.GetInt();
	if(Unpacker.Error() || Header < 0)
		return {};
	return {Header >> 1, Header & 1};
}

inline bool QmPerfSensitiveConfig(const char *pName)
{
	for(const char *pPart : {"password", "secret", "token", "api_key", "apikey", "llm_key_", "libre_key", "cookie", "sp_dc", "authorization"})
	{
		if(str_find_nocase(pName, pPart) != nullptr)
			return true;
	}
	return false;
}

inline const char *QmPerfConfigOwner(const char *pName)
{
	// 按声明来源分类，兼容 TClient 中仍使用 cl_ 前缀的变量。
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) #ScriptName,
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) #ScriptName,
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) #ScriptName,
	static const std::unordered_set<std::string_view> s_QmNames = {
#include <engine/shared/config_variables_qmclient.h>
	};
	static const std::unordered_set<std::string_view> s_TcNames = {
#include <engine/shared/config_variables_tclient.h>
	};
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
	if(s_QmNames.count(pName) != 0)
		return "qmclient";
	if(s_TcNames.count(pName) != 0)
		return "tclient";
	return "ddnet";
}

struct SQmPerfConfigValue
{
	std::string m_Value;
	std::string m_Type;
	bool m_IsDefault = false;
	bool m_Redacted = false;
};

inline SQmPerfConfigValue QmPerfReadConfigValue(const SConfigVariable &Variable)
{
	SQmPerfConfigValue Value;
	Value.m_IsDefault = Variable.IsDefault();
	switch(Variable.m_Type)
	{
	case SConfigVariable::VAR_INT:
		Value.m_Type = "int";
		Value.m_Value = std::to_string(*static_cast<const SIntConfigVariable &>(Variable).m_pVariable);
		break;
	case SConfigVariable::VAR_COLOR:
		Value.m_Type = "color";
		Value.m_Value = std::to_string(*static_cast<const SColorConfigVariable &>(Variable).m_pVariable);
		break;
	case SConfigVariable::VAR_STRING:
		Value.m_Type = "string";
		Value.m_Redacted = QmPerfSensitiveConfig(Variable.m_pScriptName);
		Value.m_Value = Value.m_Redacted ? "<redacted>" : static_cast<const SStringConfigVariable &>(Variable).m_pStr;
		break;
	}
	return Value;
}

inline std::vector<std::string> QmPerfConfigChunks(const std::string &Value)
{
	std::vector<std::string> vChunks;
	size_t Start = 0;
	do
	{
		size_t End = std::min(Start + 128, Value.size());
		// 在 UTF-8 字符边界切分，避免长字符串超过单条日志的缓冲区。
		while(End < Value.size() && End > Start && ((unsigned char)Value[End] & 0xc0) == 0x80)
			--End;
		if(End == Start && End < Value.size())
			End = std::min(Start + 128, Value.size());
		vChunks.emplace_back(Value.substr(Start, End - Start));
		Start = End;
	} while(Start < Value.size());
	return vChunks;
}

inline std::string QmPerfJsonString(const std::string &Value)
{
	std::string Escaped(Value.size() * 6 + 1, '\0');
	EscapeJson(Escaped.data(), (int)Escaped.size(), Value.c_str());
	Escaped.resize(std::strlen(Escaped.c_str()));
	return "\"" + Escaped + "\"";
}

inline void QmPerfLogFields(const char *pSystem, const std::string &Fields, const IClient *pClient)
{
	const std::string Json = "{\"system\":" + QmPerfJsonString(pSystem) +
				 ",\"session\":" + std::to_string(QmPerfSessionId()) +
				 ",\"frame\":" + std::to_string(QmPerfFrameId(pClient)) + "," + Fields + "}";
	dbg_msg(pSystem, "%s", Json.c_str());
}

class CQmPerfConfigSnapshot
{
	std::vector<const SConfigVariable *> m_vVariables;
	std::map<std::string, SQmPerfConfigValue> m_Previous;
	uint64_t m_Revision = 0;

public:
	void Start(IConfigManager *pManager, const IClient *pClient)
	{
		m_vVariables.clear();
		m_Previous.clear();
		m_Revision = 0;
		pManager->PossibleConfigVariables("", CFGFLAG_CLIENT, [](const SConfigVariable *pVariable, void *pUser) { static_cast<CQmPerfConfigSnapshot *>(pUser)->m_vVariables.push_back(pVariable); }, this);
		QmPerfLogFields("perf/config", "\"event\":\"config_snapshot\",\"expected_variables\":" + std::to_string(m_vVariables.size()), pClient);
		Update(pClient, true);
	}

	void Update(const IClient *pClient, bool Initial = false)
	{
		++m_Revision;
		for(const SConfigVariable *pVariable : m_vVariables)
		{
			const SQmPerfConfigValue Value = QmPerfReadConfigValue(*pVariable);
			auto Previous = m_Previous.find(pVariable->m_pScriptName);
			if(Previous != m_Previous.end() && Previous->second.m_Value == Value.m_Value && Previous->second.m_IsDefault == Value.m_IsDefault)
				continue;
			m_Previous[pVariable->m_pScriptName] = Value;
			const auto vChunks = QmPerfConfigChunks(Value.m_Value);
			for(size_t Part = 0; Part < vChunks.size(); ++Part)
			{
				QmPerfLogFields("perf/config",
					"\"event\":\"config_value\",\"name\":" + QmPerfJsonString(pVariable->m_pScriptName) +
						",\"owner\":" + QmPerfJsonString(QmPerfConfigOwner(pVariable->m_pScriptName)) +
						",\"type\":" + QmPerfJsonString(Value.m_Type) +
						",\"initial\":" + (Initial ? "1" : "0") +
						",\"is_default\":" + (Value.m_IsDefault ? "1" : "0") +
						",\"redacted\":" + (Value.m_Redacted ? "1" : "0") +
						",\"revision\":" + std::to_string(m_Revision) +
						",\"part\":" + std::to_string(Part) + ",\"parts\":" + std::to_string(vChunks.size()) +
						",\"value\":" + QmPerfJsonString(vChunks[Part]),
					pClient);
			}
		}
	}
};

class CQmPerfFrameBatch
{
	static constexpr size_t CAPACITY = 64;
	std::array<uint64_t, CAPACITY> m_aFrames{};
	std::array<double, CAPACITY> m_aDurations{};
	size_t m_Count = 0;
	double m_ElapsedMs = 0.0;

public:
	bool Record(uint64_t Frame, double DurationMs)
	{
		if(!std::isfinite(DurationMs) || DurationMs <= 0.0)
			return false;
		dbg_assert(m_Count < CAPACITY, "full performance frame batch must be flushed");
		m_aFrames[m_Count] = Frame;
		m_aDurations[m_Count++] = DurationMs;
		m_ElapsedMs += DurationMs;
		return m_Count == CAPACITY || m_ElapsedMs >= 1000.0;
	}

	size_t Count() const { return m_Count; }

	std::string TakeFields()
	{
		std::string Frames;
		std::string Durations;
		for(size_t i = 0; i < m_Count; ++i)
		{
			if(i != 0)
			{
				Frames += ",";
				Durations += ",";
			}
			Frames += std::to_string(m_aFrames[i]);
			char aDuration[64];
			str_format(aDuration, sizeof(aDuration), "%.3f", m_aDurations[i]);
			Durations += aDuration;
		}
		m_Count = 0;
		m_ElapsedMs = 0.0;
		return "\"event\":\"frame_batch\",\"frames\":[" + Frames + "],\"durations_ms\":[" + Durations + "]";
	}
};

#endif
