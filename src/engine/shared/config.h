/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_CONFIG_H
#define ENGINE_SHARED_CONFIG_H

#include <base/detect.h>
#include <base/math.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/memheap.h>

#include <algorithm>
#include <vector>

// include protocol for MAX_CLIENT used in config_variables
#include <engine/shared/protocol.h>

static constexpr const char *DEFAULT_SAVED_RCON_USER = "local-server";

#define AUTOEXEC_FILE "autoexec.cfg"
#define AUTOEXEC_CLIENT_FILE "autoexec_client.cfg"
#define AUTOEXEC_SERVER_FILE "autoexec_server.cfg"
#define MAX_CALLBACKS 64;

class CConfig
{
public:
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	static constexpr int ms_##Name = Def; \
	int m_##Name;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) \
	static constexpr unsigned ms_##Name = Def; \
	unsigned m_##Name;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	static constexpr const char *ms_p##Name = Def; \
	char m_##Name[Len]; // Flawfinder: ignore
#define SET_CONFIG_DOMAIN(ConfigDomain) ;
#include "config_includes.h"
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
#undef SET_CONFIG_DOMAIN
};

extern CConfig g_Config;

static constexpr int QM_FAST_INPUT_GAMMA_UI_MAX = 600;
static constexpr int QM_FAST_INPUT_GAMMA_EFFECTIVE_MAX = 600;

constexpr int QmFastInputGammaClampUiAmount(int GammaUiAmount)
{
	if(GammaUiAmount <= 0)
		return 0;
	if(GammaUiAmount >= QM_FAST_INPUT_GAMMA_UI_MAX)
		return QM_FAST_INPUT_GAMMA_UI_MAX;
	return GammaUiAmount;
}

constexpr int QmFastInputGammaUiToEffectiveAmount(int GammaUiAmount)
{
	return QmFastInputGammaClampUiAmount(GammaUiAmount);
}

constexpr int QmFastInputGammaEffectiveToUiAmount(int EffectiveAmount)
{
	return QmFastInputGammaClampUiAmount(EffectiveAmount);
}

/**
 * The default values of all config variables in @link CConfig @endlink.
 */
namespace DefaultConfig
{
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	/** Default value of the integer config variable 'ScriptName' (see CConfig::m_##Name). */ \
	static constexpr int Name = Def;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) \
	/** Default value of the color config variable 'ScriptName' (see CConfig::m_##Name). */ \
	static constexpr unsigned Name = Def;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	/** Default value of the string config variable 'ScriptName' (see CConfig::m_##Name). */ \
	static constexpr const char *const Name = Def;
#define SET_CONFIG_DOMAIN(ConfigDomain) ;
#include "config_includes.h"
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
#undef SET_CONFIG_DOMAIN
}

struct SQmFastInputSettings
{
	bool m_Enabled = false;
	int m_Mode = 0;
	int m_FastAmountMs = 0;
	int m_BestOffset = 0;
	int m_BestSmoothing = 0;
	int m_BestLatencyComp = 0;
	int m_SaikoPlusAmount = 0;
	int m_BasePredictionMarginMs = 10;
};

constexpr int QmFastInputNormalizedMode(int Mode)
{
	if(Mode == 1 || Mode == 2)
		return 3;
	return Mode;
}

constexpr float QmEffectiveFastInputOffsetTicks(const SQmFastInputSettings &Settings)
{
	if(!Settings.m_Enabled)
		return 0.0f;

	const int Mode = QmFastInputNormalizedMode(Settings.m_Mode);
	if(Mode == 0)
		return Settings.m_FastAmountMs > 0 ? Settings.m_FastAmountMs / 20.0f : 0.0f;
	if(Mode == 4)
		return Settings.m_SaikoPlusAmount > 0 ? Settings.m_SaikoPlusAmount / 100.0f : 0.0f;

	if(Settings.m_BestOffset <= 0)
		return 0.0f;

	float Offset = Settings.m_BestOffset / 100.0f;
	if(Settings.m_BestSmoothing > 0)
		Offset *= 1.0f - (Settings.m_BestSmoothing / 200.0f);
	if(Settings.m_BestLatencyComp > 0)
		Offset *= 1.0f + (Settings.m_BestLatencyComp / 100.0f);
	return Offset;
}

constexpr int QmCeilPositiveFastInputTicks(float OffsetTicks)
{
	return OffsetTicks <= 0.0f ? 0 : (int)OffsetTicks + ((float)(int)OffsetTicks < OffsetTicks ? 1 : 0);
}

constexpr int QmFastInputPredictionTicks(float OffsetTicks, int Mode)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	if(QmFastInputNormalizedMode(Mode) == 4)
		return QmCeilPositiveFastInputTicks(OffsetTicks + 1.0f);
	return QmCeilPositiveFastInputTicks(OffsetTicks);
}

constexpr int QmFastInputPredictionTicksOthers(float OffsetTicks, int Mode)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	if(QmFastInputNormalizedMode(Mode) == 4)
		return QmCeilPositiveFastInputTicks(OffsetTicks);
	return QmFastInputPredictionTicks(OffsetTicks, Mode);
}

constexpr void QmApplyFastInputOffset(float OffsetTicks, int &Tick, float &Intra)
{
	if(OffsetTicks <= 0.0f)
		return;

	const int WholeTicks = (int)OffsetTicks;
	const float OffsetIntra = OffsetTicks - (float)WholeTicks;
	const float CombinedIntra = Intra + OffsetIntra;
	const int CarryOverTicks = (int)CombinedIntra;

	Tick += WholeTicks + CarryOverTicks;
	Intra = CombinedIntra - (float)CarryOverTicks;
}

constexpr bool QmEffectiveFastInputOthers(bool FastInputEnabled, int Mode, bool FastOthers, bool BestOthers, bool SaikoOthers)
{
	if(!FastInputEnabled)
		return false;
	const int NormalizedMode = QmFastInputNormalizedMode(Mode);
	if(NormalizedMode == 0)
		return FastOthers;
	if(NormalizedMode == 4)
		return SaikoOthers;
	return BestOthers;
}

constexpr int QmFastInputBasePredictionMarginMs(const SQmFastInputSettings &Settings)
{
	int FastInputMargin = 0;
	const int Mode = QmFastInputNormalizedMode(Settings.m_Mode);
	if(Settings.m_Enabled)
	{
		if(Mode == 0)
			FastInputMargin = Settings.m_FastAmountMs > 0 ? Settings.m_FastAmountMs : 0;
		else if(Mode == 4)
			FastInputMargin = Settings.m_SaikoPlusAmount > 0 ? (Settings.m_SaikoPlusAmount + 2) / 5 : 0;
		else
			FastInputMargin = Settings.m_BestOffset > 0 ? (Settings.m_BestOffset + 2) / 5 : 0;
	}
	return Settings.m_BasePredictionMarginMs > FastInputMargin ? Settings.m_BasePredictionMarginMs : FastInputMargin;
}

constexpr int QmComputeAutoPredictionMargin(int BaseMargin, float MeasuredPingMargin, float AverageLatencyMs, float LivePredictionMs, float JitterMs, bool ConnectionProblems)
{
	const float LiveConnectionMargin = std::max({MeasuredPingMargin, AverageLatencyMs, LivePredictionMs});
	const float ExcessLatencyMargin = std::max(0.0f, LiveConnectionMargin - BaseMargin) / 6.0f;
	const float JitterMargin = std::max(0.0f, JitterMs - 2.0f) * 0.75f;
	const float ConnectionMargin = BaseMargin + ExcessLatencyMargin + JitterMargin + (ConnectionProblems ? 10.0f : 0.0f);
	return std::clamp(round_to_int(ConnectionMargin), 1, 300);
}

enum
{
	CFGFLAG_SAVE = 1 << 0,
	CFGFLAG_CLIENT = 1 << 1,
	CFGFLAG_SERVER = 1 << 2,
	CFGFLAG_DEBUG_CLIENT =
#ifdef CONF_DEBUG
		CFGFLAG_CLIENT,
#else
		0,
#endif
	CFGFLAG_DEBUG_SERVER =
#ifdef CONF_DEBUG
		CFGFLAG_SERVER,
#else
		0,
#endif
	CFGFLAG_STORE = 1 << 3,
	CFGFLAG_MASTER = 1 << 4,
	CFGFLAG_ECON = 1 << 5,
	// DDRace

	CMDFLAG_TEST = 1 << 6,
	CFGFLAG_CHAT = 1 << 7,
	CFGFLAG_GAME = 1 << 8,
	CFGFLAG_NONTEEHISTORIC = 1 << 9,
	CFGFLAG_COLLIGHT = 1 << 10,
	CFGFLAG_COLLIGHT7 = 1 << 11,
	CFGFLAG_COLALPHA = 1 << 12,
	CFGFLAG_INSENSITIVE = 1 << 13,
	CMDFLAG_PRACTICE = 1 << 14,
};

struct SConfigVariable
{
	ConfigDomain m_ConfigDomain;
	enum EVariableType
	{
		VAR_INT,
		VAR_COLOR,
		VAR_STRING,
	};
	IConsole *m_pConsole;
	const char *m_pScriptName;
	EVariableType m_Type;
	int m_Flags;
	const char *m_pHelp;
	const char *m_pHelpLocalizeKey;
	// Note that this only applies to the console command and the SetValue function,
	// but the underlying config variable can still be modified programmatically.
	bool m_ReadOnly = false;

	SConfigVariable(IConsole *pConsole, const char *pScriptName, EVariableType Type, int Flags, const char *pHelp, const char *pHelpLocalizeKey) :
		m_pConsole(pConsole),
		m_pScriptName(pScriptName),
		m_Type(Type),
		m_Flags(Flags),
		m_pHelp(pHelp),
		m_pHelpLocalizeKey(pHelpLocalizeKey)
	{
	}

	virtual ~SConfigVariable() = default;

	virtual void Register() = 0;
	virtual bool IsDefault() const = 0;
	virtual size_t MaxSerializedSize() const = 0;
	virtual void Serialize(char *pOut, size_t Size) const = 0;
	virtual void ResetToDefault() = 0;
	virtual void ResetToOld() = 0;

protected:
	void ExecuteLine(const char *pLine) const;
	bool CheckReadOnly() const;
};

struct SIntConfigVariable : public SConfigVariable
{
	int *m_pVariable;
	int m_Default;
	int m_Min;
	int m_Max;
	int m_OldValue;

	SIntConfigVariable(IConsole *pConsole, const char *pScriptName, EVariableType Type, int Flags, const char *pHelp, const char *pHelpLocalizeKey, int *pVariable, int Default, int Min, int Max) :
		SConfigVariable(pConsole, pScriptName, Type, Flags, pHelp, pHelpLocalizeKey),
		m_pVariable(pVariable),
		m_Default(Default),
		m_Min(Min),
		m_Max(Max),
		m_OldValue(Default)
	{
		*m_pVariable = m_Default;
	}

	~SIntConfigVariable() override = default;

	static void CommandCallback(IConsole::IResult *pResult, void *pUserData);
	void Register() override;
	bool IsDefault() const override;
	size_t MaxSerializedSize() const override;
	void Serialize(char *pOut, size_t Size, int Value) const;
	void Serialize(char *pOut, size_t Size) const override;
	void SetValue(int Value);
	void ResetToDefault() override;
	void ResetToOld() override;
};

struct SColorConfigVariable : public SConfigVariable
{
	unsigned *m_pVariable;
	unsigned m_Default;
	float m_DarkestLighting;
	bool m_Alpha;
	EColorInputAlphaMode m_LastInputAlphaMode;
	unsigned m_OldValue;

	SColorConfigVariable(IConsole *pConsole, const char *pScriptName, EVariableType Type, int Flags, const char *pHelp, const char *pHelpLocalizeKey, unsigned *pVariable, unsigned Default) :
		SConfigVariable(pConsole, pScriptName, Type, Flags, pHelp, pHelpLocalizeKey),
		m_pVariable(pVariable),
		m_Default(Default),
		m_Alpha(Flags & CFGFLAG_COLALPHA),
		m_LastInputAlphaMode(EColorInputAlphaMode::PACKED),
		m_OldValue(Default)
	{
		*m_pVariable = m_Default;
		if(Flags & CFGFLAG_COLLIGHT)
		{
			m_DarkestLighting = ColorHSLA::DARKEST_LGT;
		}
		else if(Flags & CFGFLAG_COLLIGHT7)
		{
			m_DarkestLighting = ColorHSLA::DARKEST_LGT7;
		}
		else
		{
			m_DarkestLighting = 0.0f;
		}
	}

	~SColorConfigVariable() override = default;

	static void CommandCallback(IConsole::IResult *pResult, void *pUserData);
	void Register() override;
	bool IsDefault() const override;
	size_t MaxSerializedSize() const override;
	void Serialize(char *pOut, size_t Size, unsigned Value) const;
	void Serialize(char *pOut, size_t Size) const override;
	void SetValue(unsigned Value);
	void ResetToDefault() override;
	void ResetToOld() override;
};

struct SStringConfigVariable : public SConfigVariable
{
	char *m_pStr;
	const char *m_pDefault;
	size_t m_MaxSize;
	char *m_pOldValue;

	SStringConfigVariable(IConsole *pConsole, const char *pScriptName, EVariableType Type, int Flags, const char *pHelp, const char *pHelpLocalizeKey, char *pStr, const char *pDefault, size_t MaxSize, char *pOldValue);
	~SStringConfigVariable() override = default;

	static void CommandCallback(IConsole::IResult *pResult, void *pUserData);
	void Register() override;
	bool IsDefault() const override;
	size_t MaxSerializedSize() const override;
	void Serialize(char *pOut, size_t Size, const char *pValue) const;
	void Serialize(char *pOut, size_t Size) const override;
	void SetValue(const char *pValue);
	void ResetToDefault() override;
	void ResetToOld() override;
};

class CConfigManager : public IConfigManager
{
	IConsole *m_pConsole;
	class IStorage *m_pStorage;

	IOHANDLE m_aConfigFile[ConfigDomain::NUM];
	bool m_aFailed[ConfigDomain::NUM];

	struct SCallback
	{
		SAVECALLBACKFUNC m_pfnFunc;
		void *m_pUserData;

		SCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData) :
			m_pfnFunc(pfnFunc),
			m_pUserData(pUserData)
		{
		}
	};
	std::vector<SCallback> m_avCallbacks[ConfigDomain::NUM];

	std::vector<SConfigVariable *> m_vpAllVariables;
	std::vector<SConfigVariable *> m_vpGameVariables;
	std::vector<const char *> m_avpUnknownCommands[ConfigDomain::NUM];
	CHeap m_ConfigHeap;

	static void Con_Reset(IConsole::IResult *pResult, void *pUserData);
	static void Con_Toggle(IConsole::IResult *pResult, void *pUserData);
	static void Con_ToggleStroke(IConsole::IResult *pResult, void *pUserData);
	static void Con_ToggleRestore(IConsole::IResult *pResult, void *pUserData);

public:
	CConfigManager();

	void Init() override;
	void Reset(const char *pScriptName) override;
	void ResetGameSettings() override;
	void SetReadOnly(const char *pScriptName, bool ReadOnly) override;
	void SetGameSettingsReadOnly(bool ReadOnly) override;
	bool Save(bool Force = false) override;

	CConfig *Values() override { return &g_Config; }

	void RegisterCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData, ConfigDomain ConfigDomain = ConfigDomain::QMCLIENT) override;

	void WriteLine(const char *pLine, ConfigDomain ConfigDomain = ConfigDomain::QMCLIENT) override;

	void StoreUnknownCommand(const char *pCommand, ConfigDomain ConfigDomain = ConfigDomain::QMCLIENT) override;

	void PossibleConfigVariables(const char *pStr, int FlagMask, POSSIBLECFGFUNC pfnCallback, void *pUserData) override;
	EColorInputAlphaMode ColorValueInputAlphaMode(const char *pScriptName) const override;
};

bool QmConfigMigrationPending(class IStorage *pStorage);
bool QmFinalizeConfigMigration(class IStorage *pStorage);
// v3：收集变量配置的候选读取路径（qmclient/settings.cfg 优先，否则按 v2 → v1 → v0 回退）。
void QmGetVariableConfigLoadPaths(class IStorage *pStorage, std::vector<const char *> &vOut);

#endif
