#ifndef ENGINE_SHARED_HTTP_RESPONSE_HEADERS_H
#define ENGINE_SHARED_HTTP_RESPONSE_HEADERS_H

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string_view>

// 自有缓冲内原地拆分，避免 SDK 解包器为每项头部再次分配。
// Reader 返回含 NUL 的实际字节数；兼容报告长度含或不含结束符。
// Visitor 的字符串仅在本次调用中有效，分配器的成功内存须兼容 free。
template<typename TReader, typename TVisitor, typename TAllocator = decltype(&std::malloc)>
bool ReadHttpResponseHeaders(size_t ReportedLength, TReader &&Reader, TVisitor &&Visitor,
	TAllocator Allocator = std::malloc) noexcept
try
{
	if(ReportedLength == 0)
		return true;
	if(ReportedLength == std::numeric_limits<size_t>::max())
		return false;
	const size_t Capacity = ReportedLength + 1;
	std::unique_ptr<char, decltype(&std::free)> pHeaders(static_cast<char *>(Allocator(Capacity)), std::free);
	if(!pHeaders)
		return false;
	const size_t Received = Reader(pHeaders.get(), Capacity);
	if((Received != ReportedLength && Received != Capacity) || pHeaders.get()[Received - 1] != '\0')
		return false;
	const std::string_view Text(pHeaders.get(), Received - 1);
	if(Text.find('\0') != std::string_view::npos)
		return false;
	size_t Start = 0;
	while(Start < Text.size())
	{
		const size_t Newline = Text.find('\n', Start);
		const size_t End = Newline == std::string_view::npos ? Text.size() : Newline;
		const size_t ValueEnd = End > Start && Text[End - 1] == '\r' ? End - 1 : End;
		if(ValueEnd > Start)
		{
			const auto Line = Text.substr(Start, ValueEnd - Start);
			const auto Colon = Line.find(':');
			if(Colon == std::string_view::npos || Colon == 0)
				return false;
			pHeaders.get()[Start + Colon] = '\0';
			pHeaders.get()[ValueEnd] = '\0';
			Visitor(pHeaders.get() + Start, pHeaders.get() + Start + Colon + 1);
		}
		Start = End + 1;
	}
	return true;
}
catch(...)
{
	// 头部局部处理失败仍交由调用者关闭请求并发布错误。
	return false;
}

#endif
