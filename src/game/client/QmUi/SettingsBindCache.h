#ifndef GAME_CLIENT_QMUI_SETTINGSBINDCACHE_H
#define GAME_CLIENT_QMUI_SETTINGSBINDCACHE_H

#include <base/str.h>

#include <game/client/components/binds.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

class CSettingsBindCache
{
	struct SHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view Command) const noexcept { return std::hash<std::string_view>{}(Command); }
	};
	struct SEqual
	{
		using is_transparent = void;
		bool operator()(std::string_view Left, std::string_view Right) const noexcept { return Left == Right; }
	};
	using CMap = std::unordered_map<std::string, CBindSlot, SHash, SEqual>;
	std::unique_ptr<CMap> m_pBinds;
	const void *m_pOwner = nullptr;
	uint64_t m_Revision = 0;

public:
	enum class EFallback
	{
		FIRST_MATCH,
		LAST_MODIFIER_MATCH,
	};

	void Invalidate() noexcept
	{
		m_pOwner = nullptr;
		m_pBinds.reset();
	}

	template<typename TBinds>
	bool Ready(const TBinds &Binds) const
	{
		return m_pBinds != nullptr && m_pOwner == &Binds && m_Revision == Binds.Revision();
	}

	template<typename TBinds>
	bool Ensure(const TBinds &Binds)
	{
		if(Ready(Binds))
			return true;
		Invalidate();
		try
		{
			auto pCandidate = std::make_unique<CMap>();
			pCandidate->reserve(64);
			for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; ++Mod)
				for(int Key = 0; Key < KEY_LAST; ++Key)
				{
					const char *pCommand = Binds.Get(Key, Mod);
					if(pCommand[0])
						pCandidate->try_emplace(pCommand, Key, Mod);
				}
			m_pBinds.swap(pCandidate);
			m_pOwner = &Binds;
			m_Revision = Binds.Revision();
			return true;
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
		return false;
	}

	template<typename TBinds>
	CBindSlot Find(const TBinds &Binds, const char *pCommand, EFallback Fallback = EFallback::FIRST_MATCH) const
	{
		if(Ready(Binds))
		{
			const auto It = m_pBinds->find(std::string_view(pCommand));
			return It == m_pBinds->end() ? EMPTY_BIND_SLOT : It->second;
		}
		// 缓存失败或过期时直接查实际绑定，不构造临时命令字符串。
		CBindSlot Result = EMPTY_BIND_SLOT;
		for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; ++Mod)
			for(int Key = 0; Key < KEY_LAST; ++Key)
			{
				const char *pBind = Binds.Get(Key, Mod);
				if(pBind[0] && str_comp(pBind, pCommand) == 0)
				{
					Result = CBindSlot(Key, Mod);
					if(Fallback == EFallback::FIRST_MATCH)
						return Result;
					break;
				}
			}
		return Result;
	}
};

extern CSettingsBindCache g_CommandBindCache;

#endif
