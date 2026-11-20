#ifndef ENGINE_CLIENT_SERVER_INFO_CLIENT_H
#define ENGINE_CLIENT_SERVER_INFO_CLIENT_H

#include <base/math.h>

#include <engine/serverbrowser.h>
#include <engine/shared/packer.h>

namespace server_info
{
	enum class EClientReadResult
	{
		END,
		CLIENT,
		INVALID,
	};

	// 只有开始新记录前没有剩余字节才是包尾，任何字段读取失败都拒绝整包。
	inline EClientReadResult ReadClient(CUnpacker &Unpacker, bool Extended, CServerInfo::CClient &Result)
	{
		if(Unpacker.Error())
			return EClientReadResult::INVALID;
		if(Unpacker.RemainingSize() == 0)
			return EClientReadResult::END;
		CServerInfo::CClient Candidate{};
		constexpr int StringFlags = CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES;
		str_copy(Candidate.m_aName, Unpacker.GetString(StringFlags));
		str_copy(Candidate.m_aClan, Unpacker.GetString(StringFlags));
		Candidate.m_Country = str_toint(Unpacker.GetString());
		if(!in_range(Candidate.m_Country, CountryCode::MINIMUM, CountryCode::MAXIMUM))
			Candidate.m_Country = CountryCode::DEFAULT;
		Candidate.m_Score = str_toint(Unpacker.GetString());
		Candidate.m_Player = str_toint(Unpacker.GetString());
		if(Extended)
			Unpacker.GetString(); // 既有扩展保留字段。
		if(Unpacker.Error())
			return EClientReadResult::INVALID;
		Result = Candidate;
		return EClientReadResult::CLIENT;
	}
}

#endif
