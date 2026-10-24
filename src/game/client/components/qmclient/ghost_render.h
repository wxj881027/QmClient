#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_RENDER_H

#include <base/vmath.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace qm_ghost_render
{
	inline bool CanRoundToInt(float Value) noexcept
	{
		if(!std::isfinite(Value))
			return false;
		if(static_cast<double>(Value) < static_cast<double>(std::numeric_limits<int>::min()) - 1 ||
			static_cast<double>(Value) > static_cast<double>(std::numeric_limits<int>::max()) + 1)
			return false;
		// 与round_to_int的浮点偏移一致，先验证截断值，避免保护检查本身缩窄。
		const double Rounded = std::trunc(static_cast<double>(Value > 0 ? Value + 0.5f : Value - 0.5f));
		return Rounded >= std::numeric_limits<int>::min() && Rounded <= std::numeric_limits<int>::max();
	}

	template<typename T>
	bool Position(const T &Previous, const T &Current, float Intra, vec2 &Result) noexcept
	{
		static_assert(std::is_same_v<decltype(Current.m_X), int> && std::is_same_v<decltype(Current.m_Y), int>);
		const int64_t FootY = static_cast<int64_t>(Current.m_Y) + 16;
		if(FootY > std::numeric_limits<int>::max() || FootY < std::numeric_limits<int>::min() || !std::isfinite(Intra))
			return false;
		if(!CanRoundToInt(static_cast<float>(Current.m_X)) || !CanRoundToInt(static_cast<float>(FootY)))
			return false;
		const vec2 Before(Previous.m_X, Previous.m_Y), Now(Current.m_X, Current.m_Y);
		const vec2 Delta = Now - Before;
		// 这种量级的插值不可能得到原生坐标；先排除，避免float乘法溢出。
		const double Limit = static_cast<double>(std::numeric_limits<float>::max()) / 2;
		if(std::abs(static_cast<double>(Delta.x) * Intra) > Limit || std::abs(static_cast<double>(Delta.y) * Intra) > Limit)
			return false;
		const vec2 Candidate = mix(Before, Now, Intra);
		if(!CanRoundToInt(Candidate.x) || !CanRoundToInt(Candidate.y + 16.0f))
			return false;
		Result = Candidate;
		return true;
	}

	// 槽位数量已固定；帧条目移动不抛异常，缓存本身不做动态分配。
	template<typename T, size_t Capacity>
	class CFrameCache
	{
		static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>);
		std::array<std::optional<T>, Capacity> m_aValues{};
		size_t m_Size = 0;

	public:
		CFrameCache() = default;
		CFrameCache(const CFrameCache &) = delete;
		CFrameCache &operator=(const CFrameCache &) = delete;
		void Clear() noexcept
		{
			for(size_t i = 0; i < m_Size; ++i)
				m_aValues[i].reset();
			m_Size = 0;
		}
		bool Add(T &&Value) noexcept
		{
			if(m_Size == Capacity)
				return false;
			m_aValues[m_Size].emplace(std::move(Value));
			++m_Size;
			return true;
		}
		size_t Size() const noexcept { return m_Size; }
		const T *Get(size_t Index) const noexcept { return Index < m_Size ? &*m_aValues[Index] : nullptr; }
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_RENDER_H
