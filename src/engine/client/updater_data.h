#ifndef ENGINE_CLIENT_UPDATER_DATA_H
#define ENGINE_CLIENT_UPDATER_DATA_H

#include <base/types.h>

#include <cstddef>
#include <forward_list>
#include <string>
#include <string_view>
#include <utility>

class IStorage;

namespace QmLegacyUpdater
{
	// URL 容量与 IHttpRequest 的实际存储一致；其它上限仅约束旧更新元数据。
	constexpr size_t UPDATE_URL_CAPACITY = 2048;
	constexpr size_t MAX_MANIFEST_BYTES = 1024 * 1024;
	constexpr unsigned long MAX_MANIFEST_JSON_MEMORY = 8 * 1024 * 1024;
	constexpr size_t MAX_MANIFEST_OPERATIONS = 8192;

	struct SRequestPaths
	{
		char m_aUrl[UPDATE_URL_CAPACITY]{};
		char m_aDestination[IO_MAX_PATH_LENGTH]{};
	};

	struct SUpdatePlan
	{
		bool m_ClientUpdate = false;
		bool m_ServerUpdate = false;
		std::forward_list<std::pair<std::string, bool>> m_FileJobs;
	};

	bool IsAllowedPath(std::string_view Path) noexcept;
	bool ResolveBinaryPath(std::string_view Root, std::string_view Relative, char *pOut, size_t OutSize) noexcept;
	// 输出仅在完整 URL、相对目标和原生临时文件路径均可表示时发布。
	bool PrepareRequestPaths(std::string_view File, std::string_view Destination, std::string_view BinaryRoot, size_t TempSuffixLength, SRequestPaths &Out) noexcept;
	bool ParseManifest(std::string_view Content, std::string_view CurrentVersion, SUpdatePlan &Out) noexcept;
	bool LoadManifest(IStorage *pStorage, const char *pAbsolutePath, std::string_view CurrentVersion, SUpdatePlan &Out) noexcept;
}

#endif
