/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/log.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/linereader.h>
#include <engine/shared/protocol.h>
#include <engine/shared/qm_legacy_config.h>
#include <engine/shared/qm_removed_config.h>
#include <engine/shared/qm_storage_layout.h>
#include <engine/storage.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

CConfig g_Config;

// ----------------------- Config Variables
namespace
{
#if !defined(CONF_FAMILY_WINDOWS)
	// Android 的模拟存储（/sdcard，含应用外部 files 目录）不支持 flock，调用返回 ENOSYS；
	// 同一路径上 POSIX 记录锁可用，所以 flock 不可用时退回 fcntl，语义同样是排他锁。
	// 只对"不支持"错误退回：真被别的写入者持有时仍按失败处理，不绕过互斥。
	bool LockConfigFileExclusive(int File)
	{
		if(flock(File, LOCK_EX | LOCK_NB) == 0)
			return true;
		const int Error = errno;
		if(Error != ENOSYS && Error != ENOTSUP
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
			&& Error != EOPNOTSUPP
#endif
		)
			return false;
		struct flock Lock = {};
		Lock.l_type = F_WRLCK;
		Lock.l_whence = SEEK_SET;
		return fcntl(File, F_SETLK, &Lock) == 0;
	}
#endif

	// 锁文件长期保留，退出只释放句柄，避免删除并重建锁文件造成两个写入者。
	class CConfigSaveLock
	{
#if defined(CONF_FAMILY_WINDOWS)
		HANDLE m_File = INVALID_HANDLE_VALUE;
#else
		int m_File = -1;
#endif

		// 同一线程的重入（保存回调里再保存）不能等自己放锁，必须立即失败。
		static bool &HeldByCallingThread()
		{
			static thread_local bool s_Held = false;
			return s_Held;
		}

		bool TryAcquire(const char *pPath)
		{
#if defined(CONF_FAMILY_WINDOWS)
			const std::wstring Path = windows_utf8_to_wide(pPath);
			m_File = CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			return m_File != INVALID_HANDLE_VALUE;
#else
			m_File = open(pPath, O_CREAT | O_RDWR, 0600);
			if(m_File < 0)
				return false;
			if(LockConfigFileExclusive(m_File))
				return true;
			close(m_File);
			m_File = -1;
			return false;
#endif
		}

	public:
		explicit CConfigSaveLock(const char *pPath)
		{
			if(HeldByCallingThread())
				return;
			// 多开时另一个实例的一次保存通常只需几十毫秒：短暂等待让它们依次落盘，
			// 而不是把正常的并发退出变成一次「保存设置错误」。
			constexpr int WAIT_STEP_MS = 10;
			constexpr int WAIT_TIMEOUT_MS = 1000;
			for(int Waited = 0; Waited <= WAIT_TIMEOUT_MS; Waited += WAIT_STEP_MS)
			{
				if(TryAcquire(pPath))
				{
					HeldByCallingThread() = true;
					return;
				}
				if(Waited == WAIT_TIMEOUT_MS)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
			}
		}

		~CConfigSaveLock()
		{
			if(!IsLocked())
				return;
			HeldByCallingThread() = false;
#if defined(CONF_FAMILY_WINDOWS)
			CloseHandle(m_File);
#else
			close(m_File);
#endif
		}

		CConfigSaveLock(const CConfigSaveLock &) = delete;
		CConfigSaveLock &operator=(const CConfigSaveLock &) = delete;

		bool IsLocked() const
		{
#if defined(CONF_FAMILY_WINDOWS)
			return m_File != INVALID_HANDLE_VALUE;
#else
			return m_File >= 0;
#endif
		}
	};
	// 此守卫在磁盘锁之后创建，先关闭句柄，再在锁内清理本次未发布的候选。
	class CConfigSaveFiles
	{
		IStorage &m_Storage;
		IOHANDLE(&m_aFiles)
		[ConfigDomain::NUM];
		const char (&m_aaPaths)[ConfigDomain::NUM][IO_MAX_PATH_LENGTH];
		bool m_aOpened[ConfigDomain::NUM] = {};
		bool m_KeepCandidates = false;

	public:
		CConfigSaveFiles(IStorage &Storage, IOHANDLE (&aFiles)[ConfigDomain::NUM], const char (&aaPaths)[ConfigDomain::NUM][IO_MAX_PATH_LENGTH]) :
			m_Storage(Storage),
			m_aFiles(aFiles),
			m_aaPaths(aaPaths)
		{
		}

		~CConfigSaveFiles()
		{
			for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
				if(m_aFiles[Domain])
					io_close(std::exchange(m_aFiles[Domain], nullptr));
			if(m_KeepCandidates)
				return;

			for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
			{
				if(!m_aOpened[Domain])
					continue;
				try
				{
					m_Storage.RemoveFile(m_aaPaths[Domain], IStorage::TYPE_SAVE);
				}
				catch(...)
				{
					// 单个候选清理异常不能中断剩余清理。
				}
			}
		}

		CConfigSaveFiles(const CConfigSaveFiles &) = delete;
		CConfigSaveFiles &operator=(const CConfigSaveFiles &) = delete;

		void Open(ConfigDomain Domain)
		{
			m_aFiles[Domain] = m_Storage.OpenFile(m_aaPaths[Domain], IOFLAG_WRITE, IStorage::TYPE_SAVE);
			m_aOpened[Domain] = m_aFiles[Domain] != nullptr;
		}

		void KeepCandidates() { m_KeepCandidates = true; }
	};

	class CConfigSaveFlag
	{
		bool &m_InProgress;

	public:
		explicit CConfigSaveFlag(bool &InProgress) :
			m_InProgress(InProgress) { m_InProgress = true; }
		~CConfigSaveFlag() { m_InProgress = false; }
	};
	// v3 迁移需要删除的历史残留：
	// - v2 时代 qmclient/ 下的两个旧变量文件（已合并进 settings.cfg）
	// - v1 时代的大写 QmClient/ 目录（含 4 个旧文件）
	// - v0 时代散落在配置根目录的 qm 专属文件（settings_ddnet.cfg 保留给官方客户端）
	// - v2 迁移的备份目录与完成标记
	constexpr const char *QM_CONFIG_V2_QMCLIENT_PATH = "qmclient/settings_qmclient.cfg";
	constexpr const char *QM_CONFIG_V2_DDNET_PATH = "qmclient/settings_ddnet.cfg";
	constexpr const char *QM_CONFIG_V1_DIR = "QmClient";
	constexpr const char *QM_CONFIG_V1_QMCLIENT_PATH = "QmClient/settings_qmclient.cfg";
	constexpr const char *QM_CONFIG_V2_MARKER = "qmclient/config_migration_v2.done";
	constexpr const char *QM_CONFIG_V0_QMCLIENT_PATH = "settings_qmclient.cfg";
	constexpr const char *QM_CONFIG_V0_PROFILES_PATH = "qmclient_profiles.cfg";
	constexpr const char *QM_CONFIG_V0_CHATBINDS_PATH = "qmclient_chatbinds.cfg";
	constexpr const char *QM_CONFIG_V0_WARLIST_PATH = "qmclient_warlist.cfg";

	struct SRegisteredIntConfigVariable : public SIntConfigVariable
	{
		using SIntConfigVariable::SIntConfigVariable;
		std::optional<int> m_ToggleRestoreValue;
	};

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
	return pStorage && !pStorage->FileExists(qm_storage::SETTINGS_FILE, IStorage::TYPE_SAVE);
}

// 读取变量配置的候选路径列表（v3 合并语义）：
// 1. 优先 qmclient/settings.cfg（v3 合并文件）；
// 2. 否则按 v2 → v1 → v0 顺序回退读取旧变量文件（存在才读），
//    根目录 settings_ddnet.cfg 仍可作为官方配置来源，迁移后保留。
void QmGetVariableConfigLoadPaths(IStorage *pStorage, std::vector<const char *> &vOut)
{
	vOut.clear();
	if(!pStorage)
		return;

	if(pStorage->FileExists(qm_storage::SETTINGS_FILE, IStorage::TYPE_SAVE))
	{
		vOut.push_back(qm_storage::SETTINGS_FILE);
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
		// v2 可能只写成了官方配置，仍需读取上一代的 Qm 设置。
		if(V2QmclientExists)
			vOut.push_back(QM_CONFIG_V2_QMCLIENT_PATH);
		else if(pStorage->FileExists(QM_CONFIG_V1_QMCLIENT_PATH, IStorage::TYPE_SAVE))
			vOut.push_back(QM_CONFIG_V1_QMCLIENT_PATH);
		else if(pStorage->FileExists(QM_CONFIG_V0_QMCLIENT_PATH, IStorage::TYPE_SAVE))
			vOut.push_back(QM_CONFIG_V0_QMCLIENT_PATH);
		if(!V2DdnetExists && pStorage->FileExists("settings_ddnet.cfg", IStorage::TYPE_SAVE))
			vOut.push_back("settings_ddnet.cfg");
		return;
	}

	// v1 时代：大写 QmClient/ 目录（qm 专属文件）+ 根目录官方共享配置
	if(pStorage->FileExists(QM_CONFIG_V1_QMCLIENT_PATH, IStorage::TYPE_SAVE))
	{
		vOut.push_back(QM_CONFIG_V1_QMCLIENT_PATH);
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
	if(!pStorage->FileExists(qm_storage::SETTINGS_FILE, IStorage::TYPE_SAVE))
	{
		log_error("config", "Cannot finalize config migration because '%s' is missing", qm_storage::SETTINGS_FILE);
		return false;
	}

	// 变量文件已合并进 qmclient/settings.cfg（由调用方 Save 完成），
	// 这里清理已知历史配置文件；备份与官方共享的 settings_ddnet.cfg 保留。
	pStorage->RemoveFile(QM_CONFIG_V2_QMCLIENT_PATH, IStorage::TYPE_SAVE);
	pStorage->RemoveFile(QM_CONFIG_V2_DDNET_PATH, IStorage::TYPE_SAVE);

	// v1 时代目录。注意：Windows 文件系统大小写不敏感，QmClient/ 与 qmclient/ 是同一物理目录，
	// 其中的 settings_qmclient.cfg 等与当前 qmclient/ 文件是同一文件（已在 v2 清理中合并删除），
	// 而 qmclient_profiles.cfg 等无变量文件仍是当前生效配置，绝不能动。
	// 只有确认 v1 目录独立存在，才删除其中的旧文件并移除目录。
	char aV1DirPath[IO_MAX_PATH_LENGTH];
	char aCurrentDirPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, QM_CONFIG_V1_DIR, aV1DirPath, sizeof(aV1DirPath));
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "qmclient", aCurrentDirPath, sizeof(aCurrentDirPath));
#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_PLATFORM_MACOS)
	const bool SeparateV1Directory = str_comp_nocase(aV1DirPath, aCurrentDirPath) != 0;
#else
	// 大小写不同的路径仍可能指向同一目录。旧目录是符号链接时也保留该目录。
	struct stat V1Dir;
	struct stat CurrentDir;
	const bool SeparateV1Directory =
		lstat(aV1DirPath, &V1Dir) == 0 && S_ISDIR(V1Dir.st_mode) &&
		stat(aCurrentDirPath, &CurrentDir) == 0 && S_ISDIR(CurrentDir.st_mode) &&
		(V1Dir.st_dev != CurrentDir.st_dev || V1Dir.st_ino != CurrentDir.st_ino);
#endif
	if(SeparateV1Directory)
	{
		pStorage->RemoveFile("QmClient/settings_ddnet.cfg", IStorage::TYPE_SAVE);
		pStorage->RemoveFile(QM_CONFIG_V1_QMCLIENT_PATH, IStorage::TYPE_SAVE);
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

	// 历史备份保留供人工恢复，绝不递归清理可能包含目录链接的用户数据。
	pStorage->RemoveFile(QM_CONFIG_V2_MARKER, IStorage::TYPE_SAVE);

	log_info("config", "Merged managed client configs into qmclient/settings.cfg and cleaned up legacy files");
	return true;
}

void SConfigVariable::ExecuteLine(const char *pLine) const
{
	m_pConsole->ExecuteLine(pLine, (m_Flags & CFGFLAG_GAME) != 0 ? IConsole::CLIENT_ID_GAME : IConsole::CLIENT_ID_UNSPECIFIED);
}

bool SConfigVariable::CheckReadOnly() const
{
	if(!m_ReadOnly)
		return false;
	log_error("config", "The config variable '%s' cannot be changed right now.", m_pScriptName);
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
		pData->m_HasExplicitValue = true;
		if(pResult->m_ClientId != IConsole::CLIENT_ID_GAME)
			pData->m_OldValue = Value;
	}
	else
	{
		log_info("config", "Value: %d", *pData->m_pVariable);
	}
}

void SIntConfigVariable::Register()
{
	m_pConsole->Register(m_pScriptName, "?i", m_Flags, CommandCallback, this, m_pHelp);
}

bool SIntConfigVariable::IsDefault() const
{
	// 写盘覆盖生效时必须按用户真实值判断，否则会把临时状态当成用户设置写进配置文件。
	return (m_HasSaveValueOverride ? m_SaveValueOverride : *m_pVariable) == m_Default;
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
	Serialize(pOut, Size, m_HasSaveValueOverride ? m_SaveValueOverride : *m_pVariable);
}

void SIntConfigVariable::SerializeHumanReadable(char *pOut, size_t Size) const
{
	Serialize(pOut, Size);
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
	if(pResult->NumArguments())
	{
		if(pData->CheckReadOnly())
			return;

		const auto Color = pResult->GetColor(0, pData->m_DarkestLighting);
		const unsigned Value = Color.Pack(pData->m_DarkestLighting, pData->m_Alpha);

		*pData->m_pVariable = Value;
		pData->m_HasExplicitValue = true;
		pData->m_LastInputAlphaMode = pData->m_Alpha ? ColorInputAlphaMode(pResult->GetString(0)) : EColorInputAlphaMode::PACKED;
		if(pResult->m_ClientId != IConsole::CLIENT_ID_GAME)
			pData->m_OldValue = Value;
	}
	else
	{
		log_info("config", "Value: %u", *pData->m_pVariable);

		const ColorHSLA Hsla = ColorHSLA(*pData->m_pVariable, true).UnclampLighting(pData->m_DarkestLighting);
		log_info("config", "H: %d°, S: %d%%, L: %d%%", round_to_int(Hsla.h * 360), round_to_int(Hsla.s * 100), round_to_int(Hsla.l * 100));

		const ColorRGBA Rgba = color_cast<ColorRGBA>(Hsla);
		log_info("config", "R: %d, G: %d, B: %d, #%06X", round_to_int(Rgba.r * 255), round_to_int(Rgba.g * 255), round_to_int(Rgba.b * 255), Rgba.Pack(false));

		if(pData->m_Alpha)
		{
			log_info("config", "A: %d%%", round_to_int(Hsla.a * 100));
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

void SColorConfigVariable::SerializeHumanReadable(char *pOut, size_t Size) const
{
	const ColorRGBA Rgba = color_cast<ColorRGBA>(ColorHSLA(*m_pVariable, m_Alpha).UnclampLighting(m_DarkestLighting));
	str_format(pOut, Size, "%s $%0*X", m_pScriptName, m_Alpha ? 8 : 6, Rgba.PackAlphaLast(m_Alpha));
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
		pData->m_HasExplicitValue = true;

		if(pResult->m_ClientId != IConsole::CLIENT_ID_GAME)
			str_copy(pData->m_pOldValue, pData->m_pStr, pData->m_MaxSize);
	}
	else
	{
		log_info("config", "Value: %s", pData->m_pStr);
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
	if(Size == 0)
		return;

	str_copy(pOut, m_pScriptName, Size);
	str_append(pOut, " \"", Size);
	char *const pValueStart = pOut + str_length(pOut);
	char *pEscaped = pValueStart;
	str_escape(&pEscaped, pValue, pOut + Size - 1);
	// 在补上闭合引号前清掉末尾被截断的 UTF-8 字符。
	str_utf8_fix_truncation(pValueStart);
	str_append(pOut, "\"", Size);
}

void SStringConfigVariable::Serialize(char *pOut, size_t Size) const
{
	Serialize(pOut, Size, m_pStr);
}

void SStringConfigVariable::SerializeHumanReadable(char *pOut, size_t Size) const
{
	Serialize(pOut, Size);
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
CConfigManager::CConfigManager(CHeap::FAllocate Allocate) :
	m_ConfigHeap(Allocate)
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
	m_Initialized = false;
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	// 在加载前记录可读性，避免加载失败后用默认设置覆盖损坏的配置。
	// 先重试一次瞬时的占用，避免把另一个实例的替换窗口误判成永久不可读。
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
		ReadConfigFileStateWithRetry(s_aConfigDomains[Domain].m_aConfigPath, m_aSaveState[Domain]);

	ConfigDomain ConfigDomain;
	const auto &&AddVariable = [this, &ConfigDomain](SConfigVariable *pVariable) {
		pVariable->m_ConfigDomain = ConfigDomain;
		m_vpAllVariables.push_back(pVariable);
		if((pVariable->m_Flags & CFGFLAG_GAME) != 0)
			m_vpGameVariables.push_back(pVariable);
		pVariable->Register();
	};

	const auto &&AddIntVariable = [this, AddVariable](const char *pScriptName, int Flags, const char *pDesc, int *pVariable, int Default, int Min, int Max) {
		dbg_assert(Min == 0 || Max == 0 || Min < Max, "MACRO_CONFIG_INT(%s): minimum (%d) must be less than maximum (%d)", pScriptName, Min, Max);
		dbg_assert((Min == 0 || Default >= Min) && (Max == 0 || Default <= Max), "MACRO_CONFIG_INT(%s): default (%d) must be in range of minimum (%d) and maximum (%d)", pScriptName, Default, Min, Max);
		char aHelp[512];
		size_t HelpSize;
		if(Min == 0 && Max == 0)
			HelpSize = str_format(aHelp, sizeof(aHelp), "%s (default: %d)", pDesc, Default);
		else if(Max == 0)
			HelpSize = str_format(aHelp, sizeof(aHelp), "%s (default: %d, min: %d)", pDesc, Default, Min);
		else
			HelpSize = str_format(aHelp, sizeof(aHelp), "%s (default: %d, min: %d, max: %d)", pDesc, Default, Min, Max);
		dbg_assert(HelpSize < sizeof(aHelp) - UTF8_BYTE_LENGTH - 1, "MACRO_CONFIG_INT(%s): help text possibly truncated. Increase size of aHelp.", pScriptName);

		AddVariable(m_ConfigHeap.Allocate<SRegisteredIntConfigVariable>(
			m_pConsole, pScriptName, SConfigVariable::VAR_INT, Flags, m_ConfigHeap.StoreString(aHelp), pDesc, pVariable, Default, Min, Max));
	};

	const auto &&AddColorVariable = [this, AddVariable](const char *pScriptName, int Flags, const char *pDesc, unsigned *pVariable, unsigned Default) {
		const bool Alpha = (Flags & CFGFLAG_COLALPHA) != 0;

		char aHelp[512];
		const size_t HelpSize = str_format(aHelp, sizeof(aHelp), "%s (default: $%0*X)", pDesc, Alpha ? 8 : 6, color_cast<ColorRGBA>(ColorHSLA(Default, Alpha)).Pack(Alpha));
		dbg_assert(HelpSize < sizeof(aHelp) - UTF8_BYTE_LENGTH - 1, "MACRO_CONFIG_COL(%s): help text possibly truncated. Increase size of aHelp.", pScriptName);

		AddVariable(m_ConfigHeap.Allocate<SColorConfigVariable>(
			m_pConsole, pScriptName, SConfigVariable::VAR_COLOR, Flags, m_ConfigHeap.StoreString(aHelp), pDesc, pVariable, Default));
	};

	const auto &&AddStringVariable = [this, AddVariable](const char *pScriptName, int Flags, const char *pDesc, char *pVariable, size_t Len, const char *pDefault) {
		dbg_assert(Len > 0 && Len <= 1024 * 1024, "MACRO_CONFIG_STR(%s): length (%" PRIzu ") is invalid.", pScriptName, Len);

		char aHelp[512];
		const size_t HelpSize = str_format(aHelp, sizeof(aHelp), "%s (default: \"%s\", max length: %" PRIzu ")", pDesc, pDefault, Len - 1);
		dbg_assert(HelpSize < sizeof(aHelp) - UTF8_BYTE_LENGTH - 1, "MACRO_CONFIG_STR(%s): help text possibly truncated. Increase size of aHelp.", pScriptName);

		char *pOldValue = static_cast<char *>(m_ConfigHeap.Allocate(Len));
		if(!pOldValue)
			throw std::bad_alloc();

		AddVariable(m_ConfigHeap.Allocate<SStringConfigVariable>(
			m_pConsole, pScriptName, SConfigVariable::VAR_STRING, Flags, m_ConfigHeap.StoreString(aHelp), pDesc, pVariable, pDefault, Len, pOldValue));
	};

#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) AddIntVariable(#ScriptName, Flags, Desc, &g_Config.m_##Name, Def, Min, Max);
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) AddColorVariable(#ScriptName, Flags, Desc, &g_Config.m_##Name, Def);
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) AddStringVariable(#ScriptName, Flags, Desc, g_Config.m_##Name, Len, Def);
#define SET_CONFIG_DOMAIN(_ConfigDomain) ConfigDomain = _ConfigDomain;
#include "config_includes.h"
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR

	m_pConsole->Register("reset", "s[config]", CFGFLAG_SERVER | CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Reset, this, "Reset a config to its default value");
	m_pConsole->Register("toggle", "s[config] s[value 1] s[value 2]", CFGFLAG_SERVER | CFGFLAG_CLIENT, Con_Toggle, this, "Toggle config value");
	m_pConsole->Register("+toggle", "s[config] s[value 1] s[value 2]", CFGFLAG_CLIENT, Con_ToggleStroke, this, "Toggle config value via keypress");
	m_pConsole->Register("+toggle_restore", "s[config] s[value]", CFGFLAG_CLIENT, Con_ToggleRestore, this, "Temporarily set a config value while pressed");
	m_Initialized = true;
}

void CConfigManager::Reset(const char *pScriptName)
{
	pScriptName = QmLegacyConfig::CanonicalName(pScriptName);
	for(SConfigVariable *pVariable : m_vpAllVariables)
	{
		if((pVariable->m_Flags & m_pConsole->FlagMask()) != 0 && str_comp(pScriptName, pVariable->m_pScriptName) == 0)
		{
			pVariable->ResetToDefault();
			return;
		}
	}

	log_error("config", "Invalid config variable: '%s'", pScriptName);
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
	dbg_assert_failed("Invalid config variable for SetReadOnly: '%s'", pScriptName);
}

void CConfigManager::SetSaveValueOverride(const char *pScriptName, bool Active, int Value, const char *pOwnerId)
{
	for(SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(str_comp(pScriptName, pVariable->m_pScriptName) == 0)
		{
			// 每个配置项只允许一个临时接管来源：两个接管者会互相清除对方的写盘保护，
			// 静默地把接管值写进配置文件。
			const bool Conflicting = Active && pVariable->m_HasSaveValueOverride && pVariable->m_SaveValueOverride != Value;
			if(Conflicting)
				log_error("config", "Conflicting save value override for '%s'", pScriptName);
			dbg_assert(!Conflicting, "config variable '%s' already has a different save value override", pScriptName);
			pVariable->SetSaveValueOverride(Active, Value, pOwnerId);
			return;
		}
	}
	// Debug 构建断言，release 构建留下日志：名字写错会让写盘保护静默失效。
	log_error("config", "Invalid config variable for SetSaveValueOverride: '%s'", pScriptName);
	dbg_assert_failed("Invalid config variable for SetSaveValueOverride: '%s'", pScriptName);
}

const char *CConfigManager::SaveValueOverrideOwner(const int *pValue) const
{
	if(pValue == nullptr)
		return nullptr;
	for(const SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(pVariable->m_Type != SConfigVariable::VAR_INT)
			continue;
		const SIntConfigVariable *pIntVariable = static_cast<const SIntConfigVariable *>(pVariable);
		if(pIntVariable->m_pVariable != pValue)
			continue;
		return pVariable->m_HasSaveValueOverride ? pVariable->m_pSaveValueOverrideOwner : nullptr;
	}
	return nullptr;
}

int CConfigManager::RealValue(const int *pValue) const
{
	if(pValue == nullptr)
		return 0;
	for(const SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(pVariable->m_Type != SConfigVariable::VAR_INT)
			continue;
		const SIntConfigVariable *pIntVariable = static_cast<const SIntConfigVariable *>(pVariable);
		if(pIntVariable->m_pVariable != pValue)
			continue;
		return pVariable->m_HasSaveValueOverride ? pVariable->m_SaveValueOverride : *pIntVariable->m_pVariable;
	}
	return *pValue;
}

void CConfigManager::SetGameSettingsReadOnly(bool ReadOnly)
{
	for(SConfigVariable *pVariable : m_vpGameVariables)
	{
		pVariable->m_ReadOnly = ReadOnly;
	}
}

bool CConfigManager::ReadConfigFileState(const char *pPath, SConfigFileState &State) const
{
	State = {};
	if(pPath == nullptr)
	{
		State.m_Readable = true;
		return true;
	}
	if(!m_pStorage)
		return false;
	// 沿用整文件读取的大小上限，并在读取前拒绝非普通文件。
	constexpr unsigned MAX_CONFIG_FILE_BYTES = 1024u * 1024 * 1024;
	void *pData = nullptr;
	unsigned Length = 0;
	const auto Result = m_pStorage->ReadFileCheckedBounded(pPath, IStorage::TYPE_SAVE, &pData, &Length, MAX_CONFIG_FILE_BYTES);
	std::unique_ptr<void, decltype(&free)> pContent(pData, free);
	if(Result == IStorage::EReadFileResult::NOT_FOUND)
	{
		State.m_Readable = true;
		return true;
	}
	if(Result != IStorage::EReadFileResult::SUCCESS)
		return false;
	State.m_Exists = true;
	// 解码只用于校验，保存前后仍比较同一次读取的原始字节。
	State.m_Content.assign(static_cast<const char *>(pContent.get()), Length);
	CLineReader Reader;
	State.m_Readable = Reader.OpenTextBuffer(pContent.release(), Length, true);
	return State.m_Readable;
}

bool CConfigManager::ReadConfigFileStateWithRetry(const char *pPath, SConfigFileState &State) const
{
	// 另一个实例替换配置文件的瞬间会让目标短暂不可读；只对读取失败做短促重试，
	// 内容和权限问题重试后仍然失败，原有的保护策略不变。
	constexpr int ATTEMPTS = 5;
	constexpr int RETRY_STEP_MS = 20;
	for(int Attempt = 0; Attempt < ATTEMPTS; ++Attempt)
	{
		if(ReadConfigFileState(pPath, State))
			return true;
		if(Attempt + 1 < ATTEMPTS)
			std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_STEP_MS));
	}
	return false;
}

bool CConfigManager::CheckConfigFiles(bool RefreshBaseline)
{
	SConfigFileState aCurrent[ConfigDomain::NUM];
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
	{
		SConfigFileState &Current = aCurrent[Domain];
		const SConfigFileState &Expected = m_aSaveState[Domain];
		if(!Expected.m_Readable || !ReadConfigFileStateWithRetry(s_aConfigDomains[Domain].m_aConfigPath, Current) ||
			(Expected.m_Exists && !Current.m_Exists) ||
			(!RefreshBaseline && (Current.m_Exists != Expected.m_Exists || Current.m_Content != Expected.m_Content)))
		{
			SetSaveError("read", s_aConfigDomains[Domain].m_aConfigPath);
			log_error("config", "ERROR: %s is unreadable or changed since loading/last saving; refusing to overwrite it", s_aConfigDomains[Domain].m_aConfigPath);
			return false;
		}
	}
	if(RefreshBaseline)
		for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
			m_aSaveState[Domain] = std::move(aCurrent[Domain]);
	return true;
}

void CConfigManager::SetSaveError(const char *pStage, const char *pPath)
{
	m_LastSaveError = "stage=";
	m_LastSaveError += pStage;
	if(pPath != nullptr && pPath[0] != '\0')
	{
		m_LastSaveError += " path=";
		m_LastSaveError += pPath;
	}
}

const char *CConfigManager::LastSaveError() const
{
	return m_LastSaveError.c_str();
}

bool CConfigManager::Save(bool Force)
try
{
	if(m_SaveInProgress)
		return false;
	m_LastSaveError.clear();
	if(!m_pStorage)
	{
		SetSaveError("storage", nullptr);
		return false;
	}
	if(!m_Initialized || m_UnknownCommandsIncomplete)
	{
		SetSaveError("incomplete", nullptr);
		return false;
	}
	if(!Force && !g_Config.m_ClSaveSettings)
		return true;
	const CConfigSaveFlag SaveFlag(m_SaveInProgress);
	char aaConfigFileTmp[ConfigDomain::NUM][IO_MAX_PATH_LENGTH] = {};
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
	{
		const char *pConfigPath = s_aConfigDomains[Domain].m_aConfigPath;
		if(!pConfigPath)
			continue;
		IStorage::FormatTmpPath(aaConfigFileTmp[Domain], sizeof(aaConfigFileTmp[Domain]), pConfigPath);

		// 用更大的缓冲检查拼接后的路径，避免候选后缀被截掉后直接打开或清理正式文件。
		char aCompletePath[2 * IO_MAX_PATH_LENGTH] = {};
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, aaConfigFileTmp[Domain], aCompletePath, sizeof(aCompletePath));
		if(str_length(aCompletePath) >= IO_MAX_PATH_LENGTH)
		{
			SetSaveError("path", pConfigPath);
			log_error("config", "ERROR: temporary config path for %s is too long", pConfigPath);
			return false;
		}
	}
	if(!EnsureConfigPathFolder(m_pStorage, qm_storage::SETTINGS_FILE))
	{
		SetSaveError("create_folder", qm_storage::SETTINGS_FILE);
		log_error("config", "ERROR: creating config folder for %s failed", qm_storage::SETTINGS_FILE);
		return false;
	}
	if(!qm_storage::EnsureFolder(m_pStorage, qm_storage::RUNTIME_DIR))
	{
		SetSaveError("create_folder", qm_storage::RUNTIME_DIR);
		log_error("config", "ERROR: creating runtime folder for %s failed", qm_storage::CONFIG_SAVE_LOCK_FILE);
		return false;
	}
	char aLockPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, qm_storage::CONFIG_SAVE_LOCK_FILE, aLockPath, sizeof(aLockPath));
	const CConfigSaveLock SaveLock(aLockPath);
	if(!SaveLock.IsLocked())
	{
		SetSaveError("lock", aLockPath);
		log_error("config", "ERROR: could not lock client configs for saving");
		return false;
	}
	CConfigSaveFiles SaveFiles(*m_pStorage, m_aConfigFile, aaConfigFileTmp);
	// 锁内以最新磁盘内容为本次保存基线，允许多个实例依次保存，最后成功保存者生效。
	// 序列化期间仍检查外部修改；不可读文件和实际写盘错误继续返回失败。
	if(!CheckConfigFiles(true))
		return false;

	bool aFailedError[ConfigDomain::NUM] = {};
	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
		m_aFailed[ConfigDomain] = false;

	SConfigFileState aWrittenState[ConfigDomain::NUM];
	for(ConfigDomain ConfigDomain = ConfigDomain::START; ConfigDomain < ConfigDomain::NUM; ++ConfigDomain)
	{
		if(s_aConfigDomains[ConfigDomain].m_aConfigPath == nullptr)
		{
			m_aConfigFile[ConfigDomain] = nullptr;
			continue;
		}
		if(!EnsureConfigPathFolder(m_pStorage, s_aConfigDomains[ConfigDomain].m_aConfigPath))
		{
			SetSaveError("create_folder", s_aConfigDomains[ConfigDomain].m_aConfigPath);
			log_error("config", "ERROR: creating config folder for %s failed", s_aConfigDomains[ConfigDomain].m_aConfigPath);
			m_aConfigFile[ConfigDomain] = nullptr;
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
			continue;
		}
		SaveFiles.Open(ConfigDomain);

		if(!m_aConfigFile[ConfigDomain])
		{
			SetSaveError("open_tmp", aaConfigFileTmp[ConfigDomain]);
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
			SetSaveError("write", aaConfigFileTmp[ConfigDomain]);
			log_error("config", "ERROR: writing to %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = true;
		}
		if(!m_aFailed[ConfigDomain] && io_sync(m_aConfigFile[ConfigDomain]) != 0)
		{
			SetSaveError("sync", aaConfigFileTmp[ConfigDomain]);
			log_error("config", "ERROR: synchronizing %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
		if(io_close(std::exchange(m_aConfigFile[ConfigDomain], nullptr)) != 0)
		{
			SetSaveError("close", aaConfigFileTmp[ConfigDomain]);
			log_error("config", "ERROR: closing %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
		if(!m_aFailed[ConfigDomain] &&
			(!ReadConfigFileState(aaConfigFileTmp[ConfigDomain], aWrittenState[ConfigDomain]) || !aWrittenState[ConfigDomain].m_Exists))
		{
			SetSaveError("read_tmp", aaConfigFileTmp[ConfigDomain]);
			log_error("config", "ERROR: reading completed temporary config %s failed", aaConfigFileTmp[ConfigDomain]);
			aFailedError[ConfigDomain] = m_aFailed[ConfigDomain] = true;
		}
	}

	bool Failed = false;
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
		Failed |= m_aFailed[Domain];
	// 所有文件完成写入后才替换，写入失败或写入期间发生外部修改都保留原配置。
	if(Failed || !CheckConfigFiles())
		return false;

	SaveFiles.KeepCandidates();
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
	{
		if(s_aConfigDomains[Domain].m_aConfigPath == nullptr)
			continue;
		// 禁止底层删除旧目标再重试。替换失败保留完整临时文件供恢复，下次保存可重试。
		if(!m_pStorage->RenameFile(aaConfigFileTmp[Domain], s_aConfigDomains[Domain].m_aConfigPath, IStorage::TYPE_SAVE, false))
		{
			SetSaveError("replace", s_aConfigDomains[Domain].m_aConfigPath);
			log_error("config", "ERROR: replacing %s failed; completed config remains at %s", s_aConfigDomains[Domain].m_aConfigPath, aaConfigFileTmp[Domain]);
			Failed = true;
			continue;
		}
		m_aSaveState[Domain] = std::move(aWrittenState[Domain]);
	}

	return !Failed;
}
catch(...)
{
	// 候选句柄已在锁内关闭；异常出口不再分配诊断文本。
	return false;
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
try
{
	// 已移除的界面开关不再作为未知命令写回旧配置。
	std::string_view Command(pCommand);
	const std::string Token = QmRemovedConfig::ReadToken(Command);
	if(str_comp_nocase(Token.c_str(), "qm_new_ui") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_enhanced_rendering") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_enhanced_sdf") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_enhanced_blur") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_ui_popup_blur") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_report_endpoint") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_report_app_id") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_report_secret") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_foot_particles") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_client_mark_trail") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_title_advanced") == 0 ||
		str_comp_nocase(Token.c_str(), "qm_title_bloom") == 0)
		return;
	m_avpUnknownCommands[ConfigDomain].push_back(m_ConfigHeap.StoreString(pCommand));
}
catch(...)
{
	// 当前输入无法保留，禁止用缺少未知命令的候选覆盖原配置。
	m_UnknownCommandsIncomplete = true;
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

const SConfigVariable *CConfigManager::FindVariable(const char *pScriptName) const
{
	if(pScriptName == nullptr || pScriptName[0] == '\0')
		return nullptr;
	for(const SConfigVariable *pVariable : m_vpAllVariables)
	{
		if(str_comp(pVariable->m_pScriptName, pScriptName) == 0)
			return pVariable;
	}
	return nullptr;
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

	const char *pScriptName = QmLegacyConfig::CanonicalName(pResult->GetString(0));
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

	log_error("config", "Invalid config variable: '%s'", pScriptName);
}

void CConfigManager::Con_ToggleStroke(IConsole::IResult *pResult, void *pUserData)
{
	CConfigManager *pConfigManager = static_cast<CConfigManager *>(pUserData);
	IConsole *pConsole = pConfigManager->m_pConsole;

	const char *pScriptName = QmLegacyConfig::CanonicalName(pResult->GetString(1));
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

	log_error("config", "Invalid config variable: '%s'", pScriptName);
}

void CConfigManager::Con_ToggleRestore(IConsole::IResult *pResult, void *pUserData)
{
	CConfigManager *pConfigManager = static_cast<CConfigManager *>(pUserData);
	IConsole *pConsole = pConfigManager->m_pConsole;

	const char *pScriptName = QmLegacyConfig::CanonicalName(pResult->GetString(1));
	for(SConfigVariable *pVariable : pConfigManager->m_vpAllVariables)
	{
		if((pVariable->m_Flags & pConsole->FlagMask()) == 0 ||
			pVariable->m_Type != SConfigVariable::VAR_INT ||
			str_comp(pScriptName, pVariable->m_pScriptName) != 0)
		{
			continue;
		}

		auto *pIntVariable = static_cast<SRegisteredIntConfigVariable *>(pVariable);
		if(pResult->GetInteger(0) != 0)
		{
			// 重复按下事件保留第一次按下前的值。
			if(!pIntVariable->m_ToggleRestoreValue)
				pIntVariable->m_ToggleRestoreValue = *pIntVariable->m_pVariable;
			pIntVariable->SetValue(pResult->GetInteger(2));
		}
		else
		{
			// 先消费旧值，控制台回调中开始的新按键周期可再次登记。
			const auto RestoreValue = std::exchange(pIntVariable->m_ToggleRestoreValue, std::nullopt);
			if(RestoreValue)
				pIntVariable->SetValue(*RestoreValue);
		}
		return;
	}

	log_error("config", "Invalid config variable: '%s'", pScriptName);
}

IConfigManager *CreateConfigManager() { return new CConfigManager; }
