#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STREAMER_WORD_FILTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STREAMER_WORD_FILTER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace QmStreamerWordFilter
{
	// 每个实例独占完整发布状态；默认构造不分配，首次准备失败可以重试。
	class CFilter
	{
		struct SState;
		std::unique_ptr<SState> m_pState;
		bool EnsureState() noexcept;

	public:
		CFilter() noexcept;
		~CFilter();
		CFilter(const CFilter &) = delete;
		CFilter &operator=(const CFilter &) = delete;

		bool SetAdditionalWords(const std::vector<std::string> &vWords) noexcept;
		// 首次准备失败时返回 0；后续调用重试，不冒充已发布的空词表。
		uint64_t Revision() noexcept;
		bool Filter(char *pText, bool Enabled) noexcept;
		bool Filter(std::string &Text, bool Enabled) noexcept;
	};

	// 只替换命中片段，不增长缓冲区；调用方决定是否启用，不修改网络输入。
	// 仅在主线程发布完整词表，所有显示入口共用同一版本；失败保留旧状态。
	bool SetAdditionalWords(const std::vector<std::string> &vWords) noexcept;
	uint64_t Revision() noexcept;
	bool Filter(char *pText, bool Enabled) noexcept;
	bool Filter(std::string &Text, bool Enabled) noexcept;
}

#endif
