#ifndef ENGINE_SHARED_HTTP_RANGE_H
#define ENGINE_SHARED_HTTP_RANGE_H

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

struct CHttpByteRange
{
	int64_t m_First;
	int64_t m_Last;
	int64_t m_Total;
	int64_t Length() const { return m_Last - m_First + 1; }
};

// 仅接受有明确总大小的单段响应；未知大小和 multipart 不能用于安装包拼接。
inline std::optional<CHttpByteRange> ParseHttpContentRange(std::string_view Value)
{
	const auto Last = Value.find_last_not_of(" \t\r\n");
	if(Last == std::string_view::npos)
		return {};
	Value = Value.substr(0, Last + 1);
	const auto First = Value.find_first_not_of(" \t");
	Value.remove_prefix(First);
	if(!Value.starts_with("bytes "))
		return {};
	Value.remove_prefix(6);
	CHttpByteRange Range{};
	const auto Read = [&Value](int64_t &Number, char Separator) {
		if(Value.empty() || Value.front() < '0' || Value.front() > '9')
			return false;
		const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
		if(Parsed.ec != std::errc{})
			return false;
		Value.remove_prefix(Parsed.ptr - Value.data());
		if(Separator == '\0')
			return Value.empty();
		if(Value.empty() || Value.front() != Separator)
			return false;
		Value.remove_prefix(1);
		return true;
	};
	if(!Read(Range.m_First, '-') || !Read(Range.m_Last, '/') || !Read(Range.m_Total, '\0') ||
		Range.m_Last < Range.m_First || Range.m_Total <= Range.m_Last)
		return {};
	return Range;
}

#endif
