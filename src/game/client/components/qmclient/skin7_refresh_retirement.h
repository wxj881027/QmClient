#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_REFRESH_RETIREMENT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_REFRESH_RETIREMENT_H

#include "qm_skin_outline.h"

#include <generated/protocol7.h>

#include <array>
#include <vector>

// 仅主线程持有一代待卸载部件；析构不调用图形接口，失败句柄仍由记录持有。
template<typename TPart>
class CQmSkin7RefreshRetirement
{
	std::array<std::vector<TPart>, protocol7::NUM_SKINPARTS> m_aParts;
	size_t m_Part = 0;

	static bool Unload(IGraphics &Graphics, IGraphics::CTextureHandle &Texture)
	{
		if(Texture.IsValid() && !Texture.IsNullTexture())
			Graphics.UnloadTexture(&Texture);
		if(Texture.IsNullTexture())
			Texture.Invalidate();
		return !Texture.IsValid();
	}

public:
	CQmSkin7RefreshRetirement() = default;
	CQmSkin7RefreshRetirement(const CQmSkin7RefreshRetirement &) = delete;
	CQmSkin7RefreshRetirement &operator=(const CQmSkin7RefreshRetirement &) = delete;

	bool IsEmpty() const noexcept
	{
		for(const auto &Parts : m_aParts)
			if(!Parts.empty())
				return false;
		return true;
	}

	void Adopt(std::vector<TPart> (&Active)[protocol7::NUM_SKINPARTS],
		std::array<std::vector<TPart>, protocol7::NUM_SKINPARTS> &Prepared) noexcept
	{
		dbg_assert(IsEmpty(), "Previous skin part retirement is incomplete");
		for(size_t Part = 0; Part < m_aParts.size(); ++Part)
		{
			Active[Part].swap(m_aParts[Part]);
			Active[Part].swap(Prepared[Part]);
		}
		m_Part = 0;
	}

	template<typename TContinue>
	bool Drain(IGraphics &Graphics, int MaxParts, TContinue &&Continue) noexcept
	{
		int Processed = 0;
		try
		{
			while(m_Part < m_aParts.size())
			{
				auto &Parts = m_aParts[m_Part];
				if(Parts.empty())
				{
					++m_Part;
					continue;
				}
				if(Processed >= MaxParts || !Continue())
					return false;
				auto &Part = Parts.back();
				if(!Unload(Graphics, Part.m_OriginalTexture) || !Unload(Graphics, Part.m_ColorableTexture))
					return false;
				if(Part.m_QmSkinOutline)
					Part.m_QmSkinOutline->Unload(&Graphics);
				Parts.pop_back();
				++Processed;
			}
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool Drain(IGraphics &Graphics, int MaxParts) noexcept
	{
		return Drain(Graphics, MaxParts, [] { return true; });
	}
};

#endif
