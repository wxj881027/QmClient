/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/log.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>

#include <unordered_map>

CConfig g_Config;

// ----------------------- Config Variables
namespace
{
	// QmClient v3：单一变量文件 qmclient/settings.cfg 存在即视为配置系统已收敛，
	// 不再需要额外的迁移标记文件。
	constexpr const char *QM_CONFIG_MAIN_PATH = "qmclient/settings.cfg";
	// v3 迁移需要删除的历史残留：
	// - v2 时代 qmclient/ 下的两个旧变量文件（已合并进 settings.cfg）
	// - v1 时代的大写 QmClient/ 目录（含 4 个旧文件）
	// - v0 时代散落在配置根目录的 qm 专属文件（settings_ddnet.cfg 保留给官方客户端）
	// - v2 迁移的备份目录与完成标记
	constexpr const char *QM_CONFIG_V2_QMCLIENT_PATH = "qmclient/settings_qmclient.cfg";
	constexpr const char *QM_CONFIG_V2_DDNET_PATH = "qmclient/settings_ddnet.cfg";
	constexpr const char *QM_CONFIG_V1_DIR = "QmClient";
	constexpr const char *QM_CONFIG_V2_BACKUP_DIR = "qmclient/migration_backup_v2";
	constexpr const char *QM_CONFIG_V1_BACKUP_DIR = "qmclient/migration_backup_v1";
	constexpr const char *QM_CONFIG_V2_MARKER = "qmclient/config_migration_v2.done";
	constexpr const char *QM_CONFIG_V0_QMCLIENT_PATH = "settings_qmclient.cfg";
	constexpr const char *QM_CONFIG_V0_PROFILES_PATH = "qmclient_profiles.cfg";
	constexpr const char *QM_CONFIG_V0_CHATBINDS_PATH = "qmclient_chatbinds.cfg";
	constexpr const char *QM_CONFIG_V0_WARLIST_PATH = "qmclient_warlist.cfg";

	std::unordered_map<const SIntConfigVariable *, int> *s_pToggleRestoreInts = nullptr;

	std::unordered_map<const SIntConfigVariable *, int> &ToggleRestoreInts()
	{
		if(s_pToggleRestoreInts == nullptr)
			s_pToggleRestoreInts = new std::unordered_map<const SIntConfigVariable *, int>();
		return *s_pToggleRestoreInts;
	}

	bool EnsureConfigPathFolder(IStorage *pStorage, const char *pConfigPath)
	{
		const char *pSlash = str_rchr(pConfigPath, '/');
		if(pSlash == nullptr)
			return true;

		const int FolderBufferSize = static_cast<int>(pSlash - pConfigPath) + 1;
		char aFolder[IO_MAX_PATH_LENGTH];
		if(FolderBufferSize <= 1 || FolderBufferSize > static_cast<int>(sizeof(aFolder)))
			return false;

		str_copy(aFolder, pConfigPath, FolderBufferSize);
		if(pStorage->FolderExists(aFolder, IStorage::TYPE_SAVE))
			return true;
		return pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE) || pStorage->FolderExists(aFolder, IStorage::TYPE_SAVE);
	}

	// 递归删除存储目录（先删内容，再删目录本身）。
	static void RemoveStorageDirRecursive(IStorage *pStorage, const char *pDir)
	{
		struct SRemoveContext
		{
			IStorage *m_pStorage;
			const char *m_pDir;
		};
		SRemoveContext Context{pStorage, pDir};
		const auto RemoveEntry = [](const CFsFileInfo *pInfo, int IsDir, int Type, void *pUser) -> int {
			(void)Type;
			SRemoveContext *pCtx = static_cast<SRemoveContext *>(pUser);
			if(pInfo == nullptr || pInfo->m_pName == nullptr)
				return 0;
			// Windows FindFirstFileW 会返回 "." 与 ".."，必须跳过，否则递归会删到父目录。
			if(str_comp(pInfo->m_pName, ".") == 0 || str_comp(pInfo->m_pName, "..") == 0)
				return 0;
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "%s/%s", pCtx->m_pDir, pInfo->m_pName);
			if(IsDir)
				RemoveStorageDirRecursive(pCtx->m_pStorage, aPath);
			else
				pCtx->m_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE);
			return 0;
		};
		pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, pDir, RemoveEntry, &Context);
		pStorage->RemoveFolder(pDir, IStorage::TYPE_SAVE);
	}

	EColorInputAlphaMode ColorInputAlphaMode(const char *pValue)
	{
		if(pValue == nullptr || pValue[0] == '\0')
			return EColorInputAlphaMode::PACKED;
		if(pValue[0] == '$')
		{
			const int HexLength = str_length(pValue + 1);
			if(HexLength == 3 || HexLength == 6)
				return EColorInputAlphaMode::OMITTED;
			if(HexLength == 4 || HexLength == 8)
				return EColorInputAlphaMode::EXPLICIT;
			return EColorInputAlphaMode::PACKED;
		}
		// 颜色名没有 alpha 分量。负数 packed 与旧版本序列化兼容，必须保留其来源信息；
		// 无符号与带正号 packed 则可用最高字节区分旧 RGB 和新的 ARGB。
		if(pValue[0] == '-' && str_isallnum(pValue + 1))
			return EColorInputAlphaMode::SIGNED_PACKED;
		if(str_isallnum(pValue) || (pValue[0] == '+' && str_isallnum(pValue + 1)))
			return EColorInputAlphaMode::PACKED;
		return EColorInputAlphaMode::OMITTED;
	}
}

bool QmConfigMigrationPending(IStorage *pStorage)
{
	// v3：qmclient/settings.cfg 存在即视为配置已收敛，无需额外标记文件。
	return pStorage && !pStorage->FileExists(QM_CONFIG_MAIN_PATH, IStorage::TYPE_SAVE);
}

// 读取变量配置的候选路径列表（v3 合并语义）：
// 1. 优先 qmclient/settings.cfg（v3 合并文件）；
// 2. 否则按 v2 → v1 → v0 顺序回退读取旧变量文件（存在才读），
//    根目录 settings_ddnet.cfg 仅作为全新用户的官方导入源，不会被删除。
void QmGetVariableConfigLoadPaths(IStorage *pStorage, std::vector<const char *> &vOut)
{
	vOut.clear();
	if(!pStorage)
		return;

	if(pStorage->FileExists(QM_CONFIG_MAIN_PATH, IStorage::TYPE_SAVE))
	{
		vOut.push_back(QM_CONFIG_MAIN_PATH);
		return;
	}

	// v2 时代：qmclient/ 下两个旧变量文件（官方 + qm/tc）合并读取。
	// 注意：Windows 文件系统大小写不敏感，QmClient/ 与 qmclient/ 是同一物理目录，
	// v1 用户的 qm 文件在此也会被命中；此时官方配置可能仍在根目录，需要补读。
	const bool V2DdnetExists = pStorage->FileExists(QM_CONFIG_V2_DDNET_PATH, IStorage::TYPE_SAVE);
	const bool V2QmclientExists = pStorage->FileExists(QM_CONFIG_V2_QMCLIENT_PATH, IStorage::TYPE_SAVE);
	if(V2DdnetExists || V2QmclientExists)
	{
		if(V2DdnetExists)
			vOut.push_back(QM_CONFIG_V2_DDNET_PATH);
		if(V2QmclientExists)
			vOut.push_back(QM_CONFIG_V2_QMCLIENT_PATH);
		if(!V2DdnetExists && pStorage->FileExists("settings_ddnet.cfg", IStorage::TYPE_SAVE))
			vOut.push_back("settings_ddnet.cfg");
		return;
	}

	// v1 时代：大写 QmClient/ 目录（qm 专属文件）+ 根目录官方共享配置
	if(pStorage->FileExists("QmClient/settings_qmclient.cfg", IStorage::TYPE_SAVE))
	{
		vOut.push_back("QmClient/settings_qmclient.cfg");
		if(pStorage->FileExists("settings_ddnet.cfg", IStorage::TYPE_SAVE))
			vOut.push_back("settings_ddnet.cfg");
		return;
	}

	// v0 时代 / 全新用户：根目录官方配置（保留）+ qm/tc 旧配置（将删除）
	if(pStorage->FileExists("settings_ddnet.cfg", IStorage::TYPE_SAVE))
		vOut.push_back("settings_ddnet.cfg");
	if(pStorage->FileExists(QM_CONFIG_V0_QMCLIENT_PATH, IStorage::TYPE_SAVE))
		vOut.push_back(QM_CONFIG_V0_QMCLIENT_PATH);
}

bool QmFinalizeConfigMigration(IStorage *pStorage)
{
	if(!pStorage)
		return false;

	// 安全边界：只有合并文件 qmclient/settings.cfg 已生成（调用方 Save 成功）才清理旧文件，
	// 防止保存失败时误删用户数据。调用时机在 Save(true) 之后，此时 settings.cfg 应已存在。
	if(!pStorage->FileExists(QM_CONFIG_MAIN_PATH, IStorage::TYPE_SAVE))
	{
		log_error("config", "Cannot finalize config migration because '%s' is missing", QM_CONFIG_MAIN_PATH);
		return false;
	}

	// 变量文件已合并进 qmclient/settings.cfg（由调用方 Save 完成），
	// 这里清理所有历史残留；根目录 settings_ddnet.cfg 保留给官方客户端。
	pStorage->RemoveFile(QM_CONFIG_V2_QMCLIENT_PATH, IStorage::TYPE_SAVE);
	pStorage->RemoveFile(QM_CONFIG_V2_DDNET_PATH, IStorage::TYPE_SAVE);

	// v1 时代目录。注意：Windows 文件系统大小写不敏感，QmClient/ 与 qmclient/ 是同一物理目录，
	// 其中的 settings_qmclient.cfg 等与当前 qmclient/ 文件是同一文件（已在 v2 清理中合并删除），
	// 而 qmclient_profiles.cfg 等无变量文件仍是当前生效配置，绝不能动。
	// 只有独立存在的 v1 目录（Linux/macOS）才删除其中的文件并移除目录。
	char aV1DirPath[IO_MAX_PATH_LENGTH];
	char aCurrentDirPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, QM_CONFIG_V1_DIR, aV1DirPath, sizeof(aV1DirPath));
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "qmclient", aCurrentDirPath, sizeof(aCurrentDirPath));
	if(str_comp_nocase(aV1DirPath, aCurrentDirPath) != 0)
	{
		pStorage->RemoveFile("QmClient/settings_ddnet.cfg", IStorage::TYPE_SAVE);
		pStorage->RemoveFile("QmClient/settings_qmclient.cfg", IStorage::TYPE_SAVE);
		pStorage->RemoveFile("QmClient/qmclient_profiles.cfg", IStorage::TYPE_SAVE);
		pStorage->RemoveFile("QmClient/qmclient_chatbinds.cfg", IStorage::TYPE_SAVE);
		pStorage->RemoveFile("QmClient/qmclient_warlist.cfg", IStorage::TYPE_SAVE);
		if(pStorage->FolderExists(QM_CONFIG_V1_DIR, IStorage::TYPE_SAVE))
			pStorage->RemoveFolder(QM_CONFIG_V1_DIR, IStorage::TYPE_SAVE);
	}

	// v0 时代散落在根目录的 qm 专属文件（settings_ddnet.cfg 不删）
	pStorage->RemoveFile(QM_CONFIG_V0_QMCLIENT_PATH, IStorage::TYPE_SAVE);
	pStorage->RemoveFile(QM_CONFIG_V0_PROFILES_PATH, IStorage::TYPE_SAVE);
	pStorage->RemoveFile(QM_CONFIG_V0_CHATBINDS_PATH, IStorage::TYPE_SAVE);
	pStorage->RemoveFile(QM_CONFIG_V0_WARLIST_PATH, IStorage::TYPE_SAVE);

	// v2 迁移残留：备份目录 + 完成标记
	if(pStorage->FolderExists(QM_CONFIG_V2_BACKUP_DIR, IStorage::TYPE_SAVE))
		RemoveStorageDirRecursive(pStorage, QM_CONFIG_V2_BACKUP_DIR);
	if(pStorage->FolderExists(QM_CONFIG_V1_BACKUP_DIR, IStorage::TYPE_SAVE))
		RemoveStorageDirRecursive(pStorage, QM_CONFIG_V1_BACKUP_DIR);
	pStorage->RemoveFile(QM_CONFIG_V2_MARKER, IStorage::TYPE_SAVE);

	log_info("config", "Merged managed client configs into qmclient/settings.cfg and cleaned up legacy files");
	return true;
}

static void EscapeParam(char *pDst, const char *pSrc, int Size)
{
	str_escape(&pDst, pSrc, pDst + Size);
}

void SConfigVariable::ExecuteLine(const char *pLine) const
{
	m_pConsole->ExecuteLine(pLine, (m_Flags & CFGFLAG_GAME) != 0 ? IConsole::CLIENT_ID_GAME : IConsole::CLIENT_ID_UNSPECIFIED);
}

bool SConfigVariable::CheckReadOnly() const
{
	if(!m_ReadOnly)
		return false;
	char aBuf[IConsole::CMDLINE_LENGTH + 64];
	str_format(aBuf, sizeof(aBuf), "The config variable '%s' cannot be changed right now.", m_pScriptName);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	return true;
}

// -----

void SIntConfigVariable::CommandCallback(IConsole::IResult *pResult, void *pUserData)
{
	SIntConfigVariable *pData = static_cast<SIntConfigVariable *>(pUserData);

	if(pResult->NumArguments())
	{
		if(pData->CheckReadOnly())
			return;

		int Value = pResult->GetInteger(0);

		// do clamping
		if(pData->m_Min != pData->m_Max)
		{
			if(Value < pData->m_Min)
				Value = pData->m_Min;
			if(pData->m_Max != 0 && Value > pData->m_Max)
				Value = pData->m_Max;
		}

		*pData->m_pVariable = Value;
		if(pResult->m_ClientId != IConsole::CLIENT_ID_GAME)
			pData->m_OldValue = Value;
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "Value: %d", *pData->m_pVariable);
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

void SIntConfigVariable::Register()
{
	m_pConsole->Register(m_pScriptName, "?i", m_Flags, CommandCallback, this, m_pHelp);
}

bool SIntConfigVariable::IsDefault() const
{
	return *m_pVariable == m_Default;
}

size_t SIntConfigVariable::MaxSerializedSize() const
{
	return str_length(m_pScriptName) + 32;
}

void SIntConfigVariable::Serialize(char *pOut, size_t Size, int Value) const
{
	str_format(pOut, Size, "%s %i", m_pScriptName, Value);
}

void SIntConfigVariable::Serialize(char *pOut, size_t Size) const
{
	Serialize(pOut, Size, *m_pVariable);
}

void SIntConfigVariable::SetValue(int Value)
{
	if(CheckReadOnly())
		return;
	char aBuf[IConsole::CMDLINE_LENGTH];
	Serialize(aBuf, sizeof(aBuf), Value);
	ExecuteLine(aBuf);
}

void SIntConfigVariable::ResetToDefault()
{
	SetValue(m_Default);
}

void SIntConfigVariable::ResetToOld()
{
	*m_pVariable = m_OldValue;
}

// -----

void SColorConfigVariable::CommandCallback(IConsole::IResult *pResult, void *pUserData)
{
	SColorConfigVariable *pData = static_cast<SColorConfigVariable *>(pUserData);
	char aBuf[IConsole::CMDLINE_LENGTH + 64];
	if(pResult->NumArguments())
	{
		if(pData->CheckReadOnly())
			return;

		const auto Color = pResult->GetColor(0, pData->m_DarkestLighting);
		const unsigned Value = Color.Pack(pData->m_DarkestLighting, pData->m_Alpha);

		*pData->m_pVariable = Value;
		pData->m_LastInputAlphaMode = pData->m_Alpha ? ColorInputAlphaMode(pResult->GetString(0)) : EColorInputAlphaMode::PACKED;
		if(pResult->m_ClientId != IConsole::CLIENT_ID_GAME)
			pData->m_OldValue = Value;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Value: %u", *pData->m_pVariable);
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);

		const ColorHSLA Hsla = ColorHSLA(*pData->m_pVariable, true).UnclampLighting(pData->m_DarkestLighting);
		str_format(aBuf, sizeof(aBuf), "H: %d°, S: %d%%, L: %d%%", round_to_int(Hsla.h * 360), round_to_int(Hsla.s * 100), round_to_int(Hsla.l * 100));
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);

		const ColorRGBA Rgba = color_cast<ColorRGBA>(Hsla);
		str_format(aBuf, sizeof(aBuf), "R: %d, G: %d, B: %d, #%06X", round_to_int(Rgba.r * 255), round_to_int(Rgba.g * 255), round_to_int(Rgba.b * 255), Rgba.Pack(false));
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);

		if(pData->m_Alpha)
		{
			str_format(aBuf, sizeof(aBuf), "A: %d%%", round_to_int(Hsla.a * 100));
			pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
		}
	}
}

void SColorConfigVariable::Register()
{
	m_pConsole->Register(m_pScriptName, "?c", m_Flags, CommandCallback, this, m_pHelp);
}

bool SColorConfigVariable::IsDefault() const
{
	return *m_pVariable == m_Default;
}

size_t SColorConfigVariable::MaxSerializedSize() const
{
	return str_length(m_pScriptName) + 32;
}

void SColorConfigVariable::Serialize(char *pOut, size_t Size, unsigned Value) const
{
	str_format(pOut, Size, "%s %u", m_pScriptName, Value);
}

void SColorConfigVariable::Serialize(char *pOut, size_t Size) const
{
	Serialize(pOut, Size, *m_pVariable);
}

void SColorConfigVariable::SetValue(unsigned Value)
{
	if(CheckReadOnly())
		return;
	char aBuf[IConsole::CMDLINE_LENGTH];
	Serialize(aBuf, sizeof(aBuf), Value);
	ExecuteLine(aBuf);
}

void SColorConfigVariable::ResetToDefault()
{
	SetValue(m_Default);
}

void SColorConfigVariable::ResetToOld()
{
	*m_pVariable = m_OldValue;
}

// -----

SStringConfigVariable::SStringConfigVariable(IConsole *pConsole, const char *pScriptName, EVariableType Type, int Flags, const char *pHelp, const char *pHelpLocalizeKey, char *pStr, const char *pDefault, size_t MaxSize, char *pOldValue) :
	SConfigVariable(pConsole, pScriptName, Type, Flags, pHelp, pHelpLocalizeKey),
	m_pStr(pStr),
	m_pDefault(pDefault),
	m_MaxSize(MaxSize),
	m_pOldValue(pOldValue)
{
	str_copy(m_pStr, m_pDefault, m_MaxSize);
	str_copy(m_pOldValue, m_pDefault, m_MaxSize);
}

void SStringConfigVariable::CommandCallback(IConsole::IResult *pResult, void *pUserData)
{
	SStringConfigVariable *pData = static_cast<SStringConfigVariable *>(pUserData);

	if(pResult->NumArguments())
	{
		if(pData->CheckReadOnly())
			return;

		const char *pString = pResult->GetString(0);
		str_copy(pData->m_pStr, pString, pData->m_MaxSize);

		if(pResult->m_ClientId != IConsole::CLIENT_ID_GAME)
			str_copy(pData->m_pOldValue, pData->m_pStr, pData->m_MaxSize);
	}
	else
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "Value: %s", pData->m_pStr);
		pData->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

void SStringConfigVariable::Register()
{
	m_pConsole->Register(m_pScriptName, "?r", m_Flags, CommandCallback, this, m_pHelp);
}

bool SStringConfigVariable::IsDefault() const
{
	return str_comp(m_pStr, m_pDefault) == 0;
}

size_t SStringConfigVariable::MaxSerializedSize() const
{
	return str_length(m_pScriptName) + 2 * m_MaxSize + 4;
}

void SStringConfigVariable::Serialize(char *pOut, size_t Size, const char *pValue) const
{
	str_copy(pOut, m_pScriptName, Size);
	str_append(pOut, " \"", Size);
	const int OutLen = str_length(pOut);
	EscapeParam(pOut + OutLen, pValue, Size - OutLen - 1); // -1 to ensure space for final quote
	str_append(pOut, "\"", Size);
}

void SStringConfigVariable::Serialize(char *pOut, size_t Size) const
{
	Serialize(pOut, Size, m_pStr);
}

void SStringConfigVariable::SetValue(const char *pValue)
{
	if(CheckReadOnly())
		return;
	std::vector<char> vBuf(MaxSerializedSize());
	Serialize(vBuf.data(), vBuf.size(), pValue);
	ExecuteLine(vBuf.data());
}

void SStringConfigVariable::ResetToDefault()
{
	SetValue(m_pDefault);
}

void SStringConfigVariable::ResetToOld()
{
	str_copy(m_pStr, m_pOldValue, m_MaxSize);
}

// ----------------------- Config Manager
CConfigManager::CConfigManager()
{
	m_pConsole = nullptr;
	m_pStorage = nullptr;
	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		m_aConfigFile[ConfigDomain] = nullptr;
		m_aFailed[ConfigDomain] = false;
	}
}

void CConfigManager::Init()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	ConfigDomain ConfigDomain;
	const auto &&AddVariable = [this, &ConfigDomain](SConfigVariable *pVariable) {
		pVariable->m_ConfigDomain = ConfigDomain;
		m_vpAllVariables.push_back(pVariable);
		if((pVariable->m_Flags & CFGFLAG_GAME) != 0)
			m_vpGameVariables.push_back(pVariable);
		pVariable->Register();
	};

#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	{ \
		const char *pHelp = Min == Max ? Desc " (default: " #Def ")" : (Max == 0 ? Desc " (default: " #Def ", min: " #Min ")" : Desc " (default: " #Def ", min: " #Min ", max: " #Max ")"); \
		AddVariable(m_ConfigHeap.Allocate<SIntConfigVariable>(m_pConsole, #ScriptName, SConfigVariable::VAR_INT, Flags, pHelp, Desc, &g_Config.m_##Name, Def, Min, Max)); \
	}

#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) \
	{ \
		const size_t HelpSize = (size_t)str_length(Desc) + 32; \
		char *pHelp = static_cast<char *>(m_ConfigHeap.Allocate(HelpSize)); \
		const bool Alpha = ((Flags) & CFGFLAG_COLALPHA) != 0; \
		str_format(pHelp, HelpSize, "%s (default: $%0*X)", Desc, Alpha ? 8 : 6, color_cast<ColorRGBA>(ColorHSLA(Def, Alpha)).Pack(Alpha)); \
		AddVariable(m_ConfigHeap.Allocate<SColorConfigVariable>(m_pConsole, #ScriptName, SConfigVariable::VAR_COLOR, Flags, pHelp, Desc, &g_Config.m_##Name, Def)); \
	}

#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	{ \
		const size_t HelpSize = (size_t)str_length(Desc) + str_length(Def) + 64; \
		char *pHelp = static_cast<char *>(m_ConfigHeap.Allocate(HelpSize)); \
		str_format(pHelp, HelpSize, "%s (default: \"%s\", max length: %d)", Desc, Def, Len - 1); \
		char *pOldValue = static_cast<char *>(m_ConfigHeap.Allocate(Len)); \
		AddVariable(m_ConfigHeap.Allocate<SStringConfigVariable>(m_pConsole, #ScriptName, SConfigVariable::VAR_STRING, Flags, pHelp, Desc, g_Config.m_##Name, Def, Len, pOldValue)); \
	}
#define SET_CONFIG_DOMAIN(_ConfigDomain) ConfigDomain = _ConfigDomain;
#include "config_includes.h"
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR

	m_pConsole->Register("reset", "s[config-name]", CFGFLAG_SERVER | CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Reset, this, "Reset a config to its default value");
	m_pConsole->Register("toggle", "s[config-option] s[value 1] s[value 2]", CFGFLAG_SERVER | CFGFLAG_CLIENT, Con_Toggle, this, "Toggle config value");
	m_pConsole->Register("+toggle", "s[config-option] s[value 1] s[value 2]", CFGFLAG_CLIENT, Con_ToggleStroke, this, "Toggle config value via keypress");
	m_pConsole->Register("+toggle_restore", "s[config-option] s[value]", CFGFLAG_CLIENT, Con_ToggleRestore, this, "Temporarily set a config value while pressed");
}

void CConfigManager::Reset(const char *pScriptName)
{
	for(SConfigVariable *pVariable : m_vpAllVariables)
	{
		if((pVariable->m_Flags & m_pConsole->FlagMask()) != 0 && str_comp(pScriptName, pVariable->m_pScriptName) == 0)
		{
			pVariable->ResetToDefault();
			return;
		}
	}

	char aBuf[IConsole::CMDLINE_LENGTH + 32];
	str_format(aBuf, sizeof(aBuf), "Invalid command: '%s'.", pScriptName);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
}

void CConfigManager::ResetGameSettings()
{
	for(SConfigVariable *pVariable : m_vpGameVariables)
	{
		pVariable->ResetToOld();
	}
}

void CConfigManager::SetReadOnly(const char *pScriptName, bool ReadOnly)
{
	for(SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(str_comp(pScriptName, pVariable->m_pScriptName) == 0)
		{
			pVariable->m_ReadOnly = ReadOnly;
			return;
		}
	}
	dbg_assert_failed("Invalid command for SetReadOnly: '%s'", pScriptName);
}

void CConfigManager::SetGameSettingsReadOnly(bool ReadOnly)
{
	for(SConfigVariable *pVariable : m_vpGameVariables)
	{
		pVariable->m_ReadOnly = ReadOnly;
	}
}

bool CConfigManager::Save(bool Force)
{
	if(!m_pStorage)
		return false;
	if(!Force && !g_Config.m_ClSaveSettings)
		return true;

	bool aFailedError[ConfigDomain::NUM] = {};
	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
		m_aFailed[ConfigDomain] = false;

	char aaConfigFileTmp[ConfigDomain::NUM][IO_MAX_PATH_LENGTH];
	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		if(s_aConfigDomains[ConfigDomain].m_aConfigPath == nullptr)
		{
			m_aConfigFile[ConfigDomain] = nullptr;
			continue;
		}
		if(!EnsureConfigPathFolder(m_pStorage, s_aConfigDomains[ConfigDomain].m_aConfigPath))
		{
			log_error("config", "ERROR: creating config folder for %s failed", s_aConfigDomains[ConfigDomain].m_aConfigPath);
			m_aConfigFile[ConfigDomain] = nullptr;
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
			continue;
		}
		m_aConfigFile[ConfigDomain] = m_pStorage->OpenFile(IStorage::FormatTmpPath(
									   aaConfigFileTmp[ConfigDomain], sizeof(aaConfigFileTmp[ConfigDomain]), s_aConfigDomains[ConfigDomain].m_aConfigPath),
			IOFLAG_WRITE, IStorage::TYPE_SAVE);

		if(!m_aConfigFile[ConfigDomain])
		{
			log_error("config", "ERROR: opening %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
	}

	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		if(!s_aConfigDomains[ConfigDomain].m_HasVars)
			continue;
		if(!m_aConfigFile[ConfigDomain])
			continue;
		for(const SConfigVariable *pVariable : m_vpAllVariables)
		{
			if(pVariable->m_ConfigDomain == ConfigDomain && (pVariable->m_Flags & CFGFLAG_SAVE) != 0 && !pVariable->IsDefault())
			{
				std::vector<char> vLineBuf(pVariable->MaxSerializedSize());
				pVariable->Serialize(vLineBuf.data(), vLineBuf.size());
				WriteLine(vLineBuf.data(), ConfigDomain);
			}
		}
	}

	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		if(m_aFailed[ConfigDomain])
			continue;
		if(!m_aConfigFile[ConfigDomain])
			continue;
		for(const auto &Callback : m_avCallbacks[ConfigDomain])
			Callback.m_pfnFunc(this, Callback.m_pUserData);
	}

	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		if(m_aFailed[ConfigDomain] || !m_aConfigFile[ConfigDomain])
			continue;
		for(const char *pCommand : m_avpUnknownCommands[ConfigDomain])
			WriteLine(pCommand, ConfigDomain);
	}

	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		if(!m_aConfigFile[ConfigDomain])
			continue;
		if(m_aFailed[ConfigDomain] && !aFailedError[ConfigDomain])
		{
			log_error("config", "ERROR: writing to %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = true;
		}
		if(!m_aFailed[ConfigDomain] && io_sync(m_aConfigFile[ConfigDomain]) != 0)
		{
			log_error("config", "ERROR: synchronizing %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
		if(io_close(m_aConfigFile[ConfigDomain]) != 0)
		{
			log_error("config", "ERROR: closing %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
		m_aConfigFile[ConfigDomain] = nullptr;
		if(!m_aFailed[ConfigDomain] && !m_pStorage->RenameFile(aaConfigFileTmp[ConfigDomain], s_aConfigDomains[ConfigDomain].m_aConfigPath, IStorage::TYPE_SAVE))
		{
			log_error("config", "ERROR: renaming %s to %s failed", aaConfigFileTmp[ConfigDomain], s_aConfigDomains[ConfigDomain].m_aConfigPath);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
		if(m_aFailed[ConfigDomain])
			m_pStorage->RemoveFile(aaConfigFileTmp[ConfigDomain], IStorage::TYPE_SAVE);
	}

	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
		m_aConfigFile[ConfigDomain] = nullptr;

	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
		if(m_aFailed[ConfigDomain])
			return false;

	return true;
}

void CConfigManager::RegisterCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData, ConfigDomain ConfigDomain)
{
	m_avCallbacks[ConfigDomain].emplace_back(pfnFunc, pUserData);
}

void CConfigManager::WriteLine(const char *pLine, ConfigDomain ConfigDomain)
{
	if(!m_aConfigFile[ConfigDomain] ||
		io_write(m_aConfigFile[ConfigDomain], pLine, str_length(pLine)) != static_cast<unsigned>(str_length(pLine)) ||
		!io_write_newline(m_aConfigFile[ConfigDomain]))
	{
		m_aFailed[ConfigDomain] = true;
	}
}

void CConfigManager::StoreUnknownCommand(const char *pCommand, ConfigDomain ConfigDomain)
{
	m_avpUnknownCommands[ConfigDomain].push_back(m_ConfigHeap.StoreString(pCommand));
}

void CConfigManager::PossibleConfigVariables(const char *pStr, int FlagMask, POSSIBLECFGFUNC pfnCallback, void *pUserData)
{
	for(const SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(pVariable->m_Flags & FlagMask)
		{
			if(str_find_nocase(pVariable->m_pScriptName, pStr))
			{
				pfnCallback(pVariable, pUserData);
			}
		}
	}
}

EColorInputAlphaMode CConfigManager::ColorValueInputAlphaMode(const char *pScriptName) const
{
	if(pScriptName == nullptr)
		return EColorInputAlphaMode::PACKED;
	for(const SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(pVariable->m_Type == SConfigVariable::VAR_COLOR && str_comp(pVariable->m_pScriptName, pScriptName) == 0)
			return static_cast<const SColorConfigVariable *>(pVariable)->m_LastInputAlphaMode;
	}
	return EColorInputAlphaMode::PACKED;
}

void CConfigManager::Con_Reset(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CConfigManager *>(pUserData)->Reset(pResult->GetString(0));
}

void CConfigManager::Con_Toggle(IConsole::IResult *pResult, void *pUserData)
{
	CConfigManager *pConfigManager = static_cast<CConfigManager *>(pUserData);
	IConsole *pConsole = pConfigManager->m_pConsole;

	const char *pScriptName = pResult->GetString(0);
	for(SConfigVariable *pVariable : pConfigManager->m_vpAllVariables)
	{
		if((pVariable->m_Flags & pConsole->FlagMask()) == 0 ||
			str_comp(pScriptName, pVariable->m_pScriptName) != 0)
		{
			continue;
		}

		if(pVariable->m_Type == SConfigVariable::VAR_INT)
		{
			SIntConfigVariable *pIntVariable = static_cast<SIntConfigVariable *>(pVariable);
			const bool EqualToFirst = *pIntVariable->m_pVariable == pResult->GetInteger(1);
			pIntVariable->SetValue(pResult->GetInteger(EqualToFirst ? 2 : 1));
		}
		else if(pVariable->m_Type == SConfigVariable::VAR_COLOR)
		{
			SColorConfigVariable *pColorVariable = static_cast<SColorConfigVariable *>(pVariable);
			const bool EqualToFirst = *pColorVariable->m_pVariable == pResult->GetColor(1, pColorVariable->m_DarkestLighting).Pack(pColorVariable->m_DarkestLighting, pColorVariable->m_Alpha);
			const std::optional<ColorHSLA> Value = pResult->GetColor(EqualToFirst ? 2 : 1, pColorVariable->m_DarkestLighting);
			pColorVariable->SetValue(Value.value_or(ColorHSLA(0, 0, 0)).Pack(pColorVariable->m_DarkestLighting, pColorVariable->m_Alpha));
		}
		else if(pVariable->m_Type == SConfigVariable::VAR_STRING)
		{
			SStringConfigVariable *pStringVariable = static_cast<SStringConfigVariable *>(pVariable);
			const bool EqualToFirst = str_comp(pStringVariable->m_pStr, pResult->GetString(1)) == 0;
			pStringVariable->SetValue(pResult->GetString(EqualToFirst ? 2 : 1));
		}
		return;
	}

	char aBuf[IConsole::CMDLINE_LENGTH + 32];
	str_format(aBuf, sizeof(aBuf), "Invalid command: '%s'.", pScriptName);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
}

void CConfigManager::Con_ToggleStroke(IConsole::IResult *pResult, void *pUserData)
{
	CConfigManager *pConfigManager = static_cast<CConfigManager *>(pUserData);
	IConsole *pConsole = pConfigManager->m_pConsole;

	const char *pScriptName = pResult->GetString(1);
	for(SConfigVariable *pVariable : pConfigManager->m_vpAllVariables)
	{
		if((pVariable->m_Flags & pConsole->FlagMask()) == 0 ||
			pVariable->m_Type != SConfigVariable::VAR_INT ||
			str_comp(pScriptName, pVariable->m_pScriptName) != 0)
		{
			continue;
		}

		SIntConfigVariable *pIntVariable = static_cast<SIntConfigVariable *>(pVariable);
		pIntVariable->SetValue(pResult->GetInteger(0) == 0 ? pResult->GetInteger(3) : pResult->GetInteger(2));
		return;
	}

	char aBuf[IConsole::CMDLINE_LENGTH + 32];
	str_format(aBuf, sizeof(aBuf), "Invalid command: '%s'.", pScriptName);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
}

void CConfigManager::Con_ToggleRestore(IConsole::IResult *pResult, void *pUserData)
{
	CConfigManager *pConfigManager = static_cast<CConfigManager *>(pUserData);
	IConsole *pConsole = pConfigManager->m_pConsole;

	const char *pScriptName = pResult->GetString(1);
	for(SConfigVariable *pVariable : pConfigManager->m_vpAllVariables)
	{
		if((pVariable->m_Flags & pConsole->FlagMask()) == 0 ||
			pVariable->m_Type != SConfigVariable::VAR_INT ||
			str_comp(pScriptName, pVariable->m_pScriptName) != 0)
		{
			continue;
		}

		SIntConfigVariable *pIntVariable = static_cast<SIntConfigVariable *>(pVariable);
		if(pResult->GetInteger(0) != 0)
		{
			// Key repeat sends extra "press" strokes while held. Preserve the original value
			// from the first press so release can restore correctly.
			std::unordered_map<const SIntConfigVariable *, int> &ToggleRestoreIntsRef = ToggleRestoreInts();
			if(!ToggleRestoreIntsRef.contains(pIntVariable))
			{
				ToggleRestoreIntsRef[pIntVariable] = *pIntVariable->m_pVariable;
			}
			pIntVariable->SetValue(pResult->GetInteger(2));
		}
		else
		{
			std::unordered_map<const SIntConfigVariable *, int> &ToggleRestoreIntsRef = ToggleRestoreInts();
			auto It = ToggleRestoreIntsRef.find(pIntVariable);
			if(It != ToggleRestoreIntsRef.end())
			{
				pIntVariable->SetValue(It->second);
				ToggleRestoreIntsRef.erase(It);
			}
		}
		return;
	}

	char aBuf[IConsole::CMDLINE_LENGTH + 32];
	str_format(aBuf, sizeof(aBuf), "Invalid command: '%s'.", pScriptName);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
}

IConfigManager *CreateConfigManager() { return new CConfigManager; }
