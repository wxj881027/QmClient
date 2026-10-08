#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONFIG_PACKAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONFIG_PACKAGE_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class IStorage;

namespace qm_config_package
{
	inline constexpr size_t MAX_FILES = 512;
	inline constexpr size_t MAX_FILE_BYTES = 1024 * 1024;
	inline constexpr size_t MAX_CONTENT_BYTES = 16 * 1024 * 1024;
	inline constexpr size_t MAX_PACKAGE_BYTES = 24 * 1024 * 1024;
	inline constexpr const char *PACKAGE_DIRECTORY = "qmclient/config_packages";

	struct SFile
	{
		std::string m_Path;
		std::string m_Content;
		bool m_Present = true;
	};

	struct SPackage
	{
		std::string m_ClientVersion;
		std::string m_ExportedAt;
		bool m_Backup = false;
		std::vector<SFile> m_vFiles;
	};

	// 文件接口只承载存储边界；恢复顺序、备份和回滚由生产实现统一处理。
	class IFileStore
	{
	public:
		virtual ~IFileStore() = default;
		virtual bool Read(const std::string &Path, std::string &Content, size_t MaxBytes) = 0;
		virtual bool Exists(const std::string &Path) = 0;
		virtual bool Write(const std::string &Path, std::string_view Content) = 0;
		virtual bool Rename(const std::string &From, const std::string &To) = 0;
		virtual bool Remove(const std::string &Path) = 0;
	};

	class CStorageFiles final : public IFileStore
	{
		IStorage &m_Storage;
		bool SafePath(const std::string &Path, bool CreateParents = false);

	public:
		explicit CStorageFiles(IStorage &Storage) :
			m_Storage(Storage) {}
		bool Read(const std::string &Path, std::string &Content, size_t MaxBytes) override;
		bool Exists(const std::string &Path) override;
		bool Write(const std::string &Path, std::string_view Content) override;
		bool Rename(const std::string &From, const std::string &To) override;
		bool Remove(const std::string &Path) override;
	};

	bool IsManagedPath(std::string_view Path);
	bool IsConfigPath(std::string_view Path);
	bool Validate(const SPackage &Package, std::string &Error);
	bool Encode(const SPackage &Package, std::string &Json, std::string &Error);
	bool Decode(std::string_view Json, SPackage &Package, std::string &Error);
	bool Collect(IFileStore &Files, const std::vector<std::string> &vCustomPaths, const char *pVersion, const char *pExportedAt, SPackage &Package, std::string &Error);
	bool Save(IFileStore &Files, const std::string &Path, const SPackage &Package, std::string &Error);
	bool Load(IFileStore &Files, const std::string &Path, SPackage &Package, std::string &Error);
	bool Restore(IFileStore &Files, const SPackage &Package, const std::string &BackupPath, std::string &Error, const char *pClientVersion = nullptr);
	bool ListCustomConfigs(IStorage &Storage, std::vector<std::string> &vPaths, std::string &Error);
	bool ListPackages(IStorage &Storage, std::vector<std::string> &vPaths, std::string &Error);
	std::string NewPackagePath(IFileStore &Files, bool Backup = false);
	std::string ExportTimestamp();
}

#endif
