#include "config_package.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <memory>
#include <set>

namespace qm_config_package
{
	namespace
	{
		constexpr std::array<const char *, 4> MANAGED_PATHS = {
			"qmclient/settings.cfg", "qmclient/qmclient_profiles.cfg", "qmclient/qmclient_chatbinds.cfg", "qmclient/qmclient_warlist.cfg"};

		std::string Lower(std::string_view Text)
		{
			std::string Result(Text);
			for(char &Character : Result)
				if(Character >= 'A' && Character <= 'Z')
					Character += 'a' - 'A';
			return Result;
		}

		bool RelativePath(std::string_view Path)
		{
			if(Path.empty() || Path.size() > 200 || Path.front() == '/' || Path.back() == '/' || Path.find_first_of("\\:\r\n\t\"<>|?*") != std::string_view::npos || Path.find('\0') != std::string_view::npos || !str_utf8_check(std::string(Path).c_str()))
				return false;
			for(const unsigned char Character : Path)
				if(Character < 32 || Character == 127)
					return false;
			for(size_t Start = 0; Start < Path.size();)
			{
				const size_t End = Path.find('/', Start);
				const std::string Part = Lower(Path.substr(Start, End == std::string_view::npos ? Path.size() - Start : End - Start));
				const std::string Base = Part.substr(0, Part.find('.'));
				if(Part.empty() || Part == "." || Part == ".." || Part.back() == '.' || Part.back() == ' ' || Base == "con" || Base == "prn" || Base == "aux" || Base == "nul" || (Base.size() == 4 && (Base.starts_with("com") || Base.starts_with("lpt")) && Base[3] >= '0' && Base[3] <= '9'))
					return false;
				if(End == std::string_view::npos)
					break;
				Start = End + 1;
			}
			return true;
		}

		bool Fail(std::string &Error, const char *pMessage)
		{
			Error = pMessage;
			return false;
		}

		std::filesystem::path StorageRoot(IStorage &Storage)
		{
			char aRoot[IO_MAX_PATH_LENGTH];
			Storage.GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
			return std::filesystem::u8path(aRoot);
		}

		bool ResolvesInside(const std::filesystem::path &Root, const std::filesystem::path &Path)
		{
			std::error_code Ec;
			const auto ResolvedRoot = std::filesystem::weakly_canonical(Root, Ec);
			if(Ec)
				return false;
			const auto ResolvedPath = std::filesystem::weakly_canonical(Path, Ec);
			if(Ec)
				return false;
			auto RootPart = ResolvedRoot.begin();
			auto PathPart = ResolvedPath.begin();
			for(; RootPart != ResolvedRoot.end(); ++RootPart, ++PathPart)
				if(PathPart == ResolvedPath.end() || *RootPart != *PathPart)
					return false;
			return true;
		}

		bool ListFiles(IStorage &Storage, bool Packages, std::vector<std::string> &vPaths, std::string &Error)
		{
			vPaths.clear();
			Error.clear();
			std::error_code Ec;
			const auto Root = StorageRoot(Storage);
			const auto Search = Packages ? Root / PACKAGE_DIRECTORY : Root;
			if(!ResolvesInside(Root, Search) || (Packages && (std::filesystem::is_symlink(Root / "qmclient", Ec) || std::filesystem::is_symlink(Search, Ec))))
				return Fail(Error, "Could not list configuration files.");
			Ec.clear();
			if(!std::filesystem::exists(Search, Ec))
				return !Ec || Fail(Error, "Could not list configuration files.");
			std::filesystem::recursive_directory_iterator Iterator(Search, std::filesystem::directory_options::none, Ec), End;
			for(; !Ec && Iterator != End; Iterator.increment(Ec))
			{
				const auto &Entry = *Iterator;
				if(Entry.is_symlink(Ec))
				{
					Iterator.disable_recursion_pending();
					continue;
				}
				if(Entry.is_directory(Ec))
				{
					// 配置包与备份不参与自定义配置扫描，也不跟随目录链接。
					if(!ResolvesInside(Root, Entry.path()) || (!Packages && Entry.path() == Root / PACKAGE_DIRECTORY))
						Iterator.disable_recursion_pending();
					continue;
				}
				if(!Entry.is_regular_file(Ec))
					continue;
				const auto Relative = Entry.path().lexically_relative(Root).generic_u8string();
				std::string Path(reinterpret_cast<const char *>(Relative.data()), Relative.size());
				if((Packages && RelativePath(Path) && Lower(Path).ends_with(".qmconfig")) || (!Packages && IsConfigPath(Path) && !IsManagedPath(Path)))
				{
					if(vPaths.size() >= MAX_FILES)
						return Fail(Error, "Too many configuration files.");
					vPaths.push_back(std::move(Path));
				}
			}
			if(Ec)
				return Fail(Error, "Could not list configuration files.");
			std::sort(vPaths.begin(), vPaths.end());
			return true;
		}
	}

	bool IsManagedPath(std::string_view Path)
	{
		return std::any_of(MANAGED_PATHS.begin(), MANAGED_PATHS.end(), [Path](const char *pManaged) { return Path == pManaged; });
	}

	bool IsConfigPath(std::string_view Path)
	{
		if(!RelativePath(Path) || Path.size() > 160 || !Lower(Path).ends_with(".cfg"))
			return false;
		const std::string Normalized = Lower(Path);
		if(Normalized.starts_with("qmclient/config_packages/") || Normalized.starts_with("qmclient/builtinscripts/"))
			return false;
		// 旧变量文件和官方共享配置不属于自定义文件，避免恢复时触发旧迁移或覆盖其他客户端。
		if(Normalized == "settings_ddnet.cfg" || Normalized == "settings_qmclient.cfg" || Normalized == "qmclient/settings_ddnet.cfg" || Normalized == "qmclient/settings_qmclient.cfg" || Normalized == "qmclient_profiles.cfg" || Normalized == "qmclient_chatbinds.cfg" || Normalized == "qmclient_warlist.cfg")
			return false;
		for(const char *pManaged : MANAGED_PATHS)
			if(Normalized == pManaged && Path != pManaged)
				return false;
		return true;
	}

	bool Validate(const SPackage &Package, std::string &Error)
	{
		Error.clear();
		if(Package.m_ClientVersion.empty() || Package.m_ClientVersion.size() > 128 || Package.m_ExportedAt.empty() || Package.m_ExportedAt.size() > 128 || !str_utf8_check(Package.m_ClientVersion.c_str()) || !str_utf8_check(Package.m_ExportedAt.c_str()) || Package.m_ClientVersion.find('\0') != std::string::npos || Package.m_ExportedAt.find('\0') != std::string::npos)
			return Fail(Error, "Invalid configuration package metadata.");
		if(Package.m_vFiles.size() > MAX_FILES)
			return Fail(Error, "Too many configuration files.");
		std::set<std::string> Paths;
		size_t Bytes = 0;
		for(const SFile &File : Package.m_vFiles)
		{
			if(!IsConfigPath(File.m_Path) || !Paths.insert(Lower(File.m_Path)).second || (!File.m_Present && (!Package.m_Backup || !File.m_Content.empty())))
				return Fail(Error, "Invalid or duplicate configuration path.");
			if(File.m_Content.size() > MAX_FILE_BYTES || Bytes > MAX_CONTENT_BYTES - File.m_Content.size())
				return Fail(Error, "Configuration package is too large.");
			Bytes += File.m_Content.size();
		}
		for(const char *pManaged : MANAGED_PATHS)
			if(!Paths.contains(pManaged))
				return Fail(Error, "Configuration package is missing required files.");
		return true;
	}

	bool Encode(const SPackage &Package, std::string &Json, std::string &Error)
	{
		if(!Validate(Package, Error))
			return false;
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("format");
		Writer.WriteStrValue("qmconfig");
		Writer.WriteAttribute("schema_version");
		Writer.WriteIntValue(1);
		Writer.WriteAttribute("client_version");
		Writer.WriteStrValue(Package.m_ClientVersion.c_str());
		Writer.WriteAttribute("exported_at");
		Writer.WriteStrValue(Package.m_ExportedAt.c_str());
		Writer.WriteAttribute("backup");
		Writer.WriteBoolValue(Package.m_Backup);
		Writer.WriteAttribute("files");
		Writer.BeginArray();
		for(const SFile &File : Package.m_vFiles)
		{
			Writer.BeginObject();
			Writer.WriteAttribute("path");
			Writer.WriteStrValue(File.m_Path.c_str());
			Writer.WriteAttribute("present");
			Writer.WriteBoolValue(File.m_Present);
			Writer.WriteAttribute("content_base64");
			std::vector<char> vEncoded((File.m_Content.size() + 2) / 3 * 4 + 1);
			str_base64(vEncoded.data(), static_cast<int>(vEncoded.size()), File.m_Content.data(), static_cast<int>(File.m_Content.size()));
			Writer.WriteStrValue(vEncoded.data());
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
		std::string Result = Writer.GetOutputString();
		if(Result.size() > MAX_PACKAGE_BYTES)
			return Fail(Error, "Configuration package is too large.");
		Json = std::move(Result);
		return true;
	}

	bool Decode(std::string_view Json, SPackage &Package, std::string &Error)
	{
		if(Json.size() > MAX_PACKAGE_BYTES)
			return Fail(Error, "Configuration package is too large.");
		json_settings Settings{};
		Settings.max_memory = MAX_PACKAGE_BYTES * 4;
		std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParseEx(&Settings, Json.data(), Json.size(), nullptr), json_value_free);
		if(!pRoot || pRoot->type != json_object)
			return Fail(Error, "Invalid configuration package.");
		const auto *pFormat = json_object_get(pRoot.get(), "format");
		const auto *pSchema = json_object_get(pRoot.get(), "schema_version");
		const auto *pVersion = json_object_get(pRoot.get(), "client_version");
		const auto *pTime = json_object_get(pRoot.get(), "exported_at");
		const auto *pBackup = json_object_get(pRoot.get(), "backup");
		const auto *pFiles = json_object_get(pRoot.get(), "files");
		if(pRoot->u.object.length != 6 || pFormat->type != json_string || std::string_view(json_string_get(pFormat), pFormat->u.string.length) != "qmconfig" || pSchema->type != json_integer || pSchema->u.integer != 1 || pVersion->type != json_string || pTime->type != json_string || pBackup->type != json_boolean || pFiles->type != json_array)
			return Fail(Error, "Invalid configuration package metadata.");
		if(pFiles->u.array.length > MAX_FILES)
			return Fail(Error, "Too many configuration files.");
		SPackage Result;
		Result.m_ClientVersion.assign(pVersion->u.string.ptr, pVersion->u.string.length);
		Result.m_ExportedAt.assign(pTime->u.string.ptr, pTime->u.string.length);
		Result.m_Backup = pBackup->u.boolean != 0;
		size_t TotalBytes = 0;
		for(unsigned Index = 0; Index < pFiles->u.array.length; ++Index)
		{
			const auto *pFile = pFiles->u.array.values[Index];
			if(pFile->type != json_object || pFile->u.object.length != 3)
				return Fail(Error, "Invalid configuration package.");
			const auto *pPath = json_object_get(pFile, "path");
			const auto *pPresent = json_object_get(pFile, "present");
			const auto *pContent = json_object_get(pFile, "content_base64");
			if(pPath->type != json_string || pPresent->type != json_boolean || pContent->type != json_string || pContent->u.string.length > (MAX_FILE_BYTES + 2) / 3 * 4 || std::string_view(pContent->u.string.ptr, pContent->u.string.length).find('\0') != std::string_view::npos)
				return Fail(Error, "Invalid configuration package.");
			SFile File;
			File.m_Path.assign(pPath->u.string.ptr, pPath->u.string.length);
			File.m_Present = pPresent->u.boolean != 0;
			File.m_Content.resize(pContent->u.string.length / 4 * 3);
			const int Size = str_base64_decode(File.m_Content.data(), static_cast<int>(File.m_Content.size()), pContent->u.string.ptr);
			if(Size < 0 || static_cast<size_t>(Size) > MAX_FILE_BYTES || TotalBytes > MAX_CONTENT_BYTES - static_cast<size_t>(Size))
				return Fail(Error, "Invalid configuration package.");
			File.m_Content.resize(Size);
			TotalBytes += Size;
			Result.m_vFiles.push_back(std::move(File));
		}
		if(!Validate(Result, Error))
			return false;
		Package = std::move(Result);
		return true;
	}

	bool CStorageFiles::SafePath(const std::string &Path, bool CreateParents)
	{
		if(!RelativePath(Path))
			return false;
		std::error_code Ec;
		const auto Root = StorageRoot(m_Storage);
		std::filesystem::path Current = Root;
		if(!ResolvesInside(Root, Root / std::filesystem::u8path(Path)))
			return false;
		const auto Relative = std::filesystem::u8path(Path);
		for(auto Iterator = Relative.begin(); Iterator != Relative.end(); ++Iterator)
		{
			Current /= *Iterator;
			const auto Status = std::filesystem::symlink_status(Current, Ec);
			if(Ec && Ec != std::errc::no_such_file_or_directory)
				return false;
			Ec.clear();
			if(std::filesystem::is_symlink(Status))
				return false;
			if(std::next(Iterator) != Relative.end())
			{
				if(std::filesystem::exists(Status) && !std::filesystem::is_directory(Status))
					return false;
				if(CreateParents && !std::filesystem::exists(Status) && !std::filesystem::create_directory(Current, Ec))
					return false;
			}
		}
		return !std::filesystem::is_directory(Current, Ec) && (!Ec || Ec == std::errc::no_such_file_or_directory);
	}

	bool CStorageFiles::Read(const std::string &Path, std::string &Content, size_t MaxBytes)
	{
		if(!SafePath(Path))
			return false;
		IOHANDLE File = m_Storage.OpenFile(Path.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const int64_t Length = io_length(File);
		bool Success = Length >= 0 && static_cast<uint64_t>(Length) <= MaxBytes;
		std::string Result;
		if(Success)
		{
			Result.resize(static_cast<size_t>(Length));
			Success = io_read(File, Result.data(), Result.size()) == Result.size();
		}
		Success = io_close(File) == 0 && Success;
		if(Success)
			Content = std::move(Result);
		return Success;
	}

	bool CStorageFiles::Exists(const std::string &Path)
	{
		return m_Storage.FileExists(Path.c_str(), IStorage::TYPE_SAVE) || m_Storage.FolderExists(Path.c_str(), IStorage::TYPE_SAVE);
	}

	bool CStorageFiles::Write(const std::string &Path, std::string_view Content)
	{
		if(!SafePath(Path, true) || Exists(Path))
			return false;
		IOHANDLE File = m_Storage.OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		bool Success = io_write(File, Content.data(), Content.size()) == Content.size();
		Success = io_sync(File) == 0 && Success;
		return io_close(File) == 0 && Success;
	}

	bool CStorageFiles::Rename(const std::string &From, const std::string &To)
	{
		return SafePath(From) && SafePath(To, true) && !Exists(To) && m_Storage.RenameFile(From.c_str(), To.c_str(), IStorage::TYPE_SAVE);
	}

	bool CStorageFiles::Remove(const std::string &Path)
	{
		return SafePath(Path) && (!Exists(Path) || m_Storage.RemoveFile(Path.c_str(), IStorage::TYPE_SAVE));
	}

	bool Collect(IFileStore &Files, const std::vector<std::string> &vCustomPaths, const char *pVersion, const char *pExportedAt, SPackage &Package, std::string &Error)
	{
		if(pVersion == nullptr || pExportedAt == nullptr)
			return Fail(Error, "Invalid configuration package metadata.");
		SPackage Result;
		Result.m_ClientVersion = pVersion;
		Result.m_ExportedAt = pExportedAt;
		for(const char *pPath : MANAGED_PATHS)
			Result.m_vFiles.push_back({pPath, {}});
		for(const std::string &Path : vCustomPaths)
		{
			if(!IsConfigPath(Path) || IsManagedPath(Path))
				return Fail(Error, "Invalid or duplicate configuration path.");
			Result.m_vFiles.push_back({Path, {}});
		}
		if(!Validate(Result, Error))
			return false;
		for(SFile &File : Result.m_vFiles)
			if(!Files.Read(File.m_Path, File.m_Content, MAX_FILE_BYTES))
				return Fail(Error, "Could not read configuration files.");
		if(!Validate(Result, Error))
			return false;
		Package = std::move(Result);
		return true;
	}

	bool Save(IFileStore &Files, const std::string &Path, const SPackage &Package, std::string &Error)
	{
		if(!RelativePath(Path) || !Lower(Path).ends_with(".qmconfig"))
			return Fail(Error, "Invalid configuration package path.");
		std::string Json;
		if(!Encode(Package, Json, Error))
			return false;
		const std::string Temporary = Path + ".tmp";
		if(Files.Exists(Path) || Files.Exists(Temporary))
			return Fail(Error, "Configuration package already exists.");
		if(!Files.Write(Temporary, Json) || !Files.Rename(Temporary, Path))
			return Fail(Error, "Could not save configuration package.");
		return true;
	}

	bool Load(IFileStore &Files, const std::string &Path, SPackage &Package, std::string &Error)
	{
		std::string Json;
		if(!RelativePath(Path) || !Lower(Path).ends_with(".qmconfig") || !Files.Read(Path, Json, MAX_PACKAGE_BYTES))
			return Fail(Error, "Could not read configuration package.");
		return Decode(Json, Package, Error);
	}

	bool Restore(IFileStore &Files, const SPackage &Package, const std::string &BackupPath, std::string &Error, const char *pClientVersion)
	{
		if(!Validate(Package, Error))
			return false;
		SPackage Backup;
		Backup.m_Backup = true;
		Backup.m_ClientVersion = pClientVersion != nullptr ? pClientVersion : Package.m_ClientVersion;
		Backup.m_ExportedAt = ExportTimestamp();
		for(const SFile &File : Package.m_vFiles)
		{
			SFile Previous{File.m_Path, {}, Files.Exists(File.m_Path)};
			if(Previous.m_Present && !Files.Read(File.m_Path, Previous.m_Content, MAX_FILE_BYTES))
				return Fail(Error, "Could not back up configuration files.");
			Backup.m_vFiles.push_back(std::move(Previous));
		}
		// 必须先持久化可再次导入的备份，再开始任何目标文件替换。
		if(!Save(Files, BackupPath, Backup, Error))
			return false;
		const std::string Suffix = ".qmrestore-" + std::to_string(str_quickhash(BackupPath.c_str()));
		for(const SFile &File : Package.m_vFiles)
		{
			if(Files.Exists(File.m_Path + Suffix + ".old") || Files.Exists(File.m_Path + Suffix + ".new"))
				return Fail(Error, "Configuration restore temporary files already exist.");
		}
		std::vector<std::string> vStaged;
		for(const SFile &File : Package.m_vFiles)
		{
			if(!File.m_Present)
				continue;
			const std::string Stage = File.m_Path + Suffix + ".new";
			vStaged.push_back(Stage);
			if(!Files.Write(Stage, File.m_Content))
			{
				for(const std::string &Path : vStaged)
					Files.Remove(Path);
				return Fail(Error, "Could not stage configuration restore.");
			}
		}
		size_t Applied = 0;
		bool Failed = false;
		bool RollbackOk = true;
		for(size_t Index = 0; Index < Package.m_vFiles.size(); ++Index)
		{
			const SFile &File = Package.m_vFiles[Index];
			const std::string Old = File.m_Path + Suffix + ".old";
			if(Backup.m_vFiles[Index].m_Present && !Files.Rename(File.m_Path, Old))
			{
				Failed = true;
				break;
			}
			if(File.m_Present && !Files.Rename(File.m_Path + Suffix + ".new", File.m_Path))
			{
				if(Backup.m_vFiles[Index].m_Present)
					RollbackOk = Files.Rename(Old, File.m_Path);
				Failed = true;
				break;
			}
			++Applied;
		}
		if(Failed)
		{
			while(Applied > 0)
			{
				const size_t Index = --Applied;
				const SFile &File = Package.m_vFiles[Index];
				const bool Removed = !File.m_Present || Files.Remove(File.m_Path);
				const bool Restored = !Backup.m_vFiles[Index].m_Present || (Removed && Files.Rename(File.m_Path + Suffix + ".old", File.m_Path));
				RollbackOk = Removed && Restored && RollbackOk;
			}
			for(const std::string &Path : vStaged)
				Files.Remove(Path);
			return Fail(Error, RollbackOk ? "Configuration restore failed; previous files were restored." : "Configuration restore failed; recover from the saved backup.");
		}
		for(size_t Index = 0; Index < Package.m_vFiles.size(); ++Index)
			if(Backup.m_vFiles[Index].m_Present)
				Files.Remove(Package.m_vFiles[Index].m_Path + Suffix + ".old");
		return true;
	}

	bool ListCustomConfigs(IStorage &Storage, std::vector<std::string> &vPaths, std::string &Error) { return ListFiles(Storage, false, vPaths, Error); }
	bool ListPackages(IStorage &Storage, std::vector<std::string> &vPaths, std::string &Error) { return ListFiles(Storage, true, vPaths, Error); }

	std::string ExportTimestamp()
	{
		const std::time_t Now = std::time(nullptr);
		std::tm Utc{};
#if defined(CONF_FAMILY_WINDOWS)
		gmtime_s(&Utc, &Now);
#else
		gmtime_r(&Now, &Utc);
#endif
		char aTime[32];
		std::strftime(aTime, sizeof(aTime), "%Y-%m-%dT%H:%M:%SZ", &Utc);
		return aTime;
	}

	std::string NewPackagePath(IFileStore &Files, bool Backup)
	{
		std::string Timestamp = ExportTimestamp();
		Timestamp.erase(std::remove(Timestamp.begin(), Timestamp.end(), ':'), Timestamp.end());
		const std::string Prefix = std::string(PACKAGE_DIRECTORY) + (Backup ? "/backups/config-" : "/config-") + Timestamp;
		for(unsigned Index = 0; Index < 10000; ++Index)
		{
			const std::string Path = Prefix + "-" + std::to_string(Index) + ".qmconfig";
			if(!Files.Exists(Path) && !Files.Exists(Path + ".tmp"))
				return Path;
		}
		return {};
	}
}
