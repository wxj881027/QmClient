#ifndef BASE_OS_LOCALE_H
#define BASE_OS_LOCALE_H

#include "str.h"
#include "types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace os_locale
{
	// Query只负责平台读取；回退、输出边界和字符规则由同一入口执行。
	template<typename TQuery>
	void Read(char *pLocale, size_t Length, TQuery &&Query) noexcept
	{
		if(!pLocale || Length == 0)
			return;
		str_copy(pLocale, "en-US", Length);
		try
		{
			if(!std::forward<TQuery>(Query)(pLocale, Length) || !std::memchr(pLocale, '\0', Length))
			{
				str_copy(pLocale, "en-US", Length);
				return;
			}
			for(size_t Index = 0; pLocale[Index] != '\0'; ++Index)
			{
				const char Character = pLocale[Index];
				if(Character == '_')
					pLocale[Index] = '-';
				else if(Character != '-' && !(Character >= 'a' && Character <= 'z') &&
					!(Character >= 'A' && Character <= 'Z') && !(Character >= '0' && Character <= '9'))
				{
					pLocale[Index] = '\0';
					break;
				}
			}
			if(pLocale[0] == '\0' || str_comp(pLocale, "C") == 0 || str_comp(pLocale, "POSIX") == 0)
				str_copy(pLocale, "en-US", Length);
		}
		catch(...)
		{
			str_copy(pLocale, "en-US", Length);
		}
	}

	// 由Read验证输出后调用；Copy取得的locale归本调用，Get取得的identifier仅借用。
	template<typename TOperations>
	bool ReadOwnedIdentifier(char *pLocale, size_t Length, TOperations &Operations)
	{
		auto Locale = Operations.Acquire();
		if(!Locale)
			return false;
		static_assert(noexcept(Operations.Release(Locale)));
		struct CLocaleOwner
		{
			TOperations &m_Operations;
			decltype(Locale) m_Locale;
			~CLocaleOwner() { m_Operations.Release(m_Locale); }
		} LocaleOwner{Operations, Locale};
		auto Identifier = Operations.Identifier(Locale);
		if(!Identifier)
			return false;
		const auto Units = Operations.Length(Identifier);
		using TIndex = std::remove_cv_t<decltype(Units)>;
		static_assert(std::is_integral_v<TIndex> && std::is_signed_v<TIndex>);
		if(Units < 0 || Units > (std::numeric_limits<TIndex>::max() - 1) / UTF8_BYTE_LENGTH)
			return false;
		const TIndex Capacity = Units * UTF8_BYTE_LENGTH + 1;
		if constexpr(sizeof(size_t) < sizeof(TIndex))
		{
			if(static_cast<std::uintmax_t>(Capacity) > std::numeric_limits<size_t>::max())
				return false;
		}
		char *pBuffer = Operations.Allocate(static_cast<size_t>(Capacity));
		if(!pBuffer)
			return false;
		static_assert(noexcept(Operations.Free(pBuffer)));
		struct CBufferOwner
		{
			TOperations &m_Operations;
			char *m_pBuffer;
			~CBufferOwner() { m_Operations.Free(m_pBuffer); }
		} BufferOwner{Operations, pBuffer};
		if(!Operations.Convert(Identifier, pBuffer, Capacity) || !std::memchr(pBuffer, '\0', static_cast<size_t>(Capacity)))
			return false;
		str_copy(pLocale, pBuffer, Length);
		return true;
	}
}

#endif
