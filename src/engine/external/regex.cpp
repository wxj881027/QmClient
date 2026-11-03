#include "regex.h"

#include <regex>

class Regex::Data {
public:
	std::regex regex;
	std::string error;
};

Regex::Regex(const Regex& other) : data(new Data(*other.data)) {}

Regex::Regex(Regex&& Other) : data(Other.data) {
	Other.data = nullptr;
}

Regex::Regex() : data(new Data{{}, "Empty"}) {}

// 低成本结构检查：把明显非法的 pattern 挡在 std::regex 构造之前。
// 原因不是“不想 catch”，而是部分标准库（实测 macOS/libc++）在构造非法 pattern 时
// 直接终止进程，栈帧完好但没有 handler 被找到，调用方与包装器内部的 catch 都拿不到
// 控制权；libstdc++ 则正常抛出 regex_error。与其依赖标准库错误通道，不如自己先拦。
static bool PatternIsStructurallyValid(const std::string& Pattern) {
	bool Escaped = false;
	bool InClass = false;
	int ParenDepth = 0;
	for(size_t i = 0; i < Pattern.size(); ++i) {
		const char C = Pattern[i];
		if(Escaped) {
			Escaped = false;
			continue;
		}
		if(C == '\\') {
			Escaped = true;
			continue;
		}
		if(InClass) {
			if(C == ']')
				InClass = false;
			continue;
		}
		if(C == '[')
			InClass = true;
		else if(C == '(')
			++ParenDepth;
		else if(C == ')' && --ParenDepth < 0)
			return false;
		else if(C == '{') {
			// {n,m} 之外的花括号在 ECMAScript 里非法，部分标准库直接抛/终止。
			const size_t Close = Pattern.find('}', i + 1);
			if(Close == std::string::npos)
				return false;
			for(size_t k = i + 1; k < Close; ++k)
				if((Pattern[k] < '0' || Pattern[k] > '9') && Pattern[k] != ',')
					return false;
			i = Close;
		}
	}
	return !Escaped && !InClass && ParenDepth == 0;
}

Regex::Regex(const std::string& pattern, bool caseInsensitive) : data(new Data) {
	if(!PatternIsStructurallyValid(pattern)) {
		data->error = "Unsupported pattern";
		return;
	}
	try {
		std::regex_constants::syntax_option_type Flags = std::regex::ECMAScript;
		if(caseInsensitive)
			Flags |= std::regex::icase;
		data->regex = std::regex(pattern, Flags);
		data->error = "";
	} catch (const std::regex_error& e) {
		data->error = e.what();
	} catch (...) {
		// 合同是「包装器不向调用方抛」：不同标准库对非法 pattern 抛的类型不止
		// regex_error（libc++ 与 libstdc++ 的处理就有差异），这里统一兜住。
		data->error = "Unsupported pattern";
	}
}

Regex& Regex::operator=(const Regex& other) {
	if (this != &other) {
		delete data;
		data = new Data(*other.data);
	}
	return *this;
}

Regex& Regex::operator=(Regex&& other) {
	if (this != &other) {
		delete data;
		data = other.data;
		other.data = nullptr;
	}
	return *this;
}

Regex::~Regex() {
	delete data;
}

std::string Regex::error() const {
	return data->error;
}

bool Regex::test(const std::string &str) {
	if (!data || !data->error.empty()) return false;
	try {
		return std::regex_search(str, data->regex);
	} catch (...) {
		// 运行期异常同样降级为不匹配，调用方仍能继续处理聊天消息。
		return false;
	}
}

void Regex::match(const std::string &str, bool global, std::function<void(const std::string &str, int match, int group)> func) {
	if (!data->error.empty()) return;

	std::smatch sm;
	std::string::const_iterator searchStart(str.cbegin());
	int matchIndex = 0;

	while (std::regex_search(searchStart, str.cend(), sm, data->regex)) {
		for (size_t g = 0; g < sm.size(); ++g) {
			func(sm[g].str(), matchIndex, static_cast<int>(g));
		}

		++matchIndex;
		if (!global) break;  // only first match if global is false
		searchStart = sm.suffix().first;
	}
}

std::string Regex::replace(const std::string &str, bool global, std::function<std::string(const std::string &str, int match, int group)> func) {
	if (!data->error.empty()) return str;

	std::string result;
	std::string::const_iterator searchStart(str.cbegin());
	std::smatch sm;
	int matchIndex = 0;

	while (std::regex_search(searchStart, str.cend(), sm, data->regex)) {
		result.append(searchStart, sm[0].first); // append text before match

		std::string replacement;
		for (size_t g = 0; g < sm.size(); ++g) {
			replacement += func(sm[g].str(), matchIndex, static_cast<int>(g));
		}
		result += replacement;

		++matchIndex;
		if (!global) {
			result.append(sm.suffix().first, str.cend());
			return result;
		}
		searchStart = sm.suffix().first;
	}

	// append remaining text if any
	result.append(searchStart, str.cend());
	return result;
}
