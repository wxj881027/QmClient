#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WORD_FILTER_LIST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WORD_FILTER_LIST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class IStorage;

namespace QmWordFilterList
{

	constexpr size_t INLINE_CAPACITY = 65536;
	constexpr size_t MAX_FILE_BYTES = 2 * 1024 * 1024;
	constexpr size_t MAX_FILE_WORDS = 100000;

	// 文件按行读取，兼容纯文本和 "词语", 格式；解析失败不发布残缺词表。
	bool ParseFile(std::string_view Content, std::vector<std::string> &vWords, std::string &Error) noexcept;
	bool AppendInlineChunk(char *pList, size_t Capacity, const char *pChunk);
	bool AppendInlineHexChunk(char *pList, size_t Capacity, const char *pChunk) noexcept;
	// 使用现有 cfg 命令格式分段保存，避免超过控制台单行解析容量。
	std::vector<std::string> SerializeInline(const char *pList);

	class CFileList
	{
		std::string m_AttemptedPath;
		std::vector<std::string> m_vWords;
		std::string m_Error;
		uint64_t m_Revision = 1;

	public:
		void Refresh(IStorage *pStorage, const char *pPath, bool Force = false) noexcept;
		bool UpdateFromText(std::string_view Content) noexcept;
		const std::vector<std::string> &Words() const { return m_vWords; }
		const std::string &Error() const { return m_Error; }
		uint64_t Revision() const { return m_Revision; }
	};

}

#endif
