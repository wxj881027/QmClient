// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PERF_DIAGNOSTICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PERF_DIAGNOSTICS_H

#include "perf_logging.h"

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

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

inline size_t QmPerfJsonBufferSize(size_t Length)
{
	// EscapeJson 接收 int 大小，乘法与窄化都必须在分配前检查。
	if(Length > (static_cast<size_t>(std::numeric_limits<int>::max()) - 1) / 6)
		throw std::length_error("performance JSON string is too long");
	return Length * 6 + 1;
}

inline std::string QmPerfJsonString(const std::string &Value)
{
	std::string Escaped(QmPerfJsonBufferSize(Value.size()), '\0');
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

// 格式化与输出使用同一生产边界；借用的实现必须比采集器存活更久。
class CQmPerfDiagnosticOperations
{
public:
	virtual ~CQmPerfDiagnosticOperations() = default;

	virtual SQmPerfConfigValue ReadValue(const SConfigVariable &Variable)
	{
		return QmPerfReadConfigValue(Variable);
	}

	virtual std::string ConfigFields(const SConfigVariable &Variable, const SQmPerfConfigValue &Value, const std::string &Chunk, bool Initial, uint64_t Revision, size_t Part, size_t Parts)
	{
		return "\"event\":\"config_value\",\"name\":" + QmPerfJsonString(Variable.m_pScriptName) +
		       ",\"owner\":" + QmPerfJsonString(QmPerfConfigOwner(Variable.m_pScriptName)) +
		       ",\"type\":" + QmPerfJsonString(Value.m_Type) +
		       ",\"initial\":" + (Initial ? "1" : "0") +
		       ",\"is_default\":" + (Value.m_IsDefault ? "1" : "0") +
		       ",\"redacted\":" + (Value.m_Redacted ? "1" : "0") +
		       ",\"revision\":" + std::to_string(Revision) +
		       ",\"part\":" + std::to_string(Part) + ",\"parts\":" + std::to_string(Parts) +
		       ",\"value\":" + QmPerfJsonString(Chunk);
	}

	virtual std::string FrameFields(const uint64_t *pFrames, const double *pDurations, size_t Count)
	{
		std::string Frames;
		std::string Durations;
		for(size_t i = 0; i < Count; ++i)
		{
			if(i != 0)
			{
				Frames += ",";
				Durations += ",";
			}
			Frames += std::to_string(pFrames[i]);
			char aDuration[64];
			str_format(aDuration, sizeof(aDuration), "%.3f", pDurations[i]);
			Durations += aDuration;
		}
		return "\"event\":\"frame_batch\",\"frames\":[" + Frames + "],\"durations_ms\":[" + Durations + "]";
	}

	virtual void Emit(const char *pSystem, const std::string &Fields, const IClient *pClient)
	{
		QmPerfLogFields(pSystem, Fields, pClient);
	}
};

class CQmPerfConfigSnapshot
{
	struct SRecordedValue
	{
		SQmPerfConfigValue m_Value;
		bool m_HasValue = false;
		bool m_Incomplete = false;
	};
	using TValueMap = std::map<std::string, SRecordedValue, std::less<>>;
	std::vector<const SConfigVariable *> m_vVariables;
	TValueMap m_Previous;
	uint64_t m_Revision = 0;
	IConfigManager *m_pManager = nullptr;
	CQmPerfDiagnosticOperations *m_pOperations;
	bool m_NeedsStart = false;

public:
	explicit CQmPerfConfigSnapshot(CQmPerfDiagnosticOperations *pOperations = nullptr) noexcept :
		m_pOperations(pOperations) {}

	bool Start(IConfigManager *pManager, const IClient *pClient) noexcept
	{
		m_pManager = pManager;
		m_NeedsStart = true;
		return Update(pClient);
	}

	bool Update(const IClient *pClient, bool Initial = false) noexcept
	{
		CQmPerfDiagnosticOperations DefaultOperations;
		auto &Operations = m_pOperations ? *m_pOperations : DefaultOperations;
		try
		{
			if(m_NeedsStart)
			{
				std::vector<const SConfigVariable *> vVariables;
				m_pManager->PossibleConfigVariables("", CFGFLAG_CLIENT, [](const SConfigVariable *pVariable, void *pUser) { static_cast<std::vector<const SConfigVariable *> *>(pUser)->push_back(pVariable); }, &vVariables);
				Operations.Emit("perf/config", "\"event\":\"config_snapshot\",\"expected_variables\":" + std::to_string(vVariables.size()), pClient);
				m_vVariables.swap(vVariables);
				m_Previous.clear();
				m_Revision = 0;
				m_NeedsStart = false;
				Initial = true;
			}
			++m_Revision;
			for(const SConfigVariable *pVariable : m_vVariables)
			{
				auto Value = Operations.ReadValue(*pVariable);
				auto Previous = m_Previous.find(pVariable->m_pScriptName);
				if(Previous != m_Previous.end() && Previous->second.m_HasValue && !Previous->second.m_Incomplete && Previous->second.m_Value.m_Value == Value.m_Value && Previous->second.m_Value.m_IsDefault == Value.m_IsDefault)
					continue;
				// 先准备节点；全部交付后用移动赋值提交基线，不再分配内存。
				auto &Recorded = m_Previous.try_emplace(pVariable->m_pScriptName).first->second;
				const auto vChunks = QmPerfConfigChunks(Value.m_Value);
				// 部分交付后即使用户改回旧值，也须用新修订号补齐当前值。
				Recorded.m_Incomplete = true;
				for(size_t Part = 0; Part < vChunks.size(); ++Part)
					Operations.Emit("perf/config", Operations.ConfigFields(*pVariable, Value, vChunks[Part], Initial || !Recorded.m_HasValue, m_Revision, Part, vChunks.size()), pClient);
				Recorded.m_Value = std::move(Value);
				Recorded.m_HasValue = true;
				Recorded.m_Incomplete = false;
			}
			return true;
		}
		catch(...)
		{
			// 已完成的变量仍有效；本项及后续变量在下次周期重试。
			return false;
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
	CQmPerfDiagnosticOperations *m_pOperations;

	void Clear() noexcept
	{
		m_Count = 0;
		m_ElapsedMs = 0.0;
	}

public:
	explicit CQmPerfFrameBatch(CQmPerfDiagnosticOperations *pOperations = nullptr) noexcept :
		m_pOperations(pOperations) {}

	bool Record(uint64_t Frame, double DurationMs) noexcept
	{
		if(!std::isfinite(DurationMs) || DurationMs <= 0.0)
			return false;
		if(m_Count == CAPACITY)
			return true;
		m_aFrames[m_Count] = Frame;
		m_aDurations[m_Count++] = DurationMs;
		m_ElapsedMs += DurationMs;
		return m_Count == CAPACITY || m_ElapsedMs >= 1000.0;
	}

	size_t Count() const { return m_Count; }

	std::string TakeFields()
	{
		CQmPerfDiagnosticOperations DefaultOperations;
		auto &Operations = m_pOperations ? *m_pOperations : DefaultOperations;
		auto Fields = Operations.FrameFields(m_aFrames.data(), m_aDurations.data(), m_Count);
		Clear();
		return Fields;
	}

	bool Flush(const IClient *pClient) noexcept
	{
		if(m_Count == 0)
			return true;
		CQmPerfDiagnosticOperations DefaultOperations;
		auto &Operations = m_pOperations ? *m_pOperations : DefaultOperations;
		try
		{
			Operations.Emit("perf/frame", Operations.FrameFields(m_aFrames.data(), m_aDurations.data(), m_Count), pClient);
			Clear();
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool RecordAndLog(uint64_t Frame, double DurationMs, const IClient *pClient) noexcept
	{
		// 满批次发送失败时跳过当前样本，不覆盖待发送数据，也不突破容量。
		if(m_Count == CAPACITY && !Flush(pClient))
			return false;
		return !Record(Frame, DurationMs) || Flush(pClient);
	}
};

#endif
