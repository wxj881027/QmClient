#ifndef TEST_SUPPORT_HTTP_RESPONSE_ALLOCATION_PROBE_H
#define TEST_SUPPORT_HTTP_RESPONSE_ALLOCATION_PROBE_H

#include <cstdlib>

// 按请求替换分配边界，不拦截全局分配；成功内存仍由生产析构 free。
class CHttpResponseAllocationProbe
{
public:
	bool m_Fail = false;
	int m_Requests = 0;
	size_t m_LastSize = 0;
	void *Reallocate(void *pBuffer, size_t Size) noexcept
	{
		++m_Requests;
		m_LastSize = Size;
		return m_Fail ? nullptr : std::realloc(pBuffer, Size);
	}
};

#endif
