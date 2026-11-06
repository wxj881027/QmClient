#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_TEXT_BUILD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_TEXT_BUILD_H

#include "nameplate_text_cache.h"

#include <optional>
#include <type_traits>
#include <utility>

// 快照和归还状态只使用值；真正后端的MapScreen/SetRenderFlags不分配。
template<typename TTextRender, typename TGraphics>
class CQmNameplateTextContext
{
	TTextRender &m_Text;
	TGraphics &m_Graphics;
	unsigned m_PreviousFlags;
	float m_aScreen[4]{};
	bool m_MapEnabled;
	bool m_FlagsTouched = false;
	bool m_ScreenTouched = false;

	void RestoreFlags()
	{
		if(std::exchange(m_FlagsTouched, false))
			m_Text.SetRenderFlags(m_PreviousFlags);
	}

public:
	CQmNameplateTextContext(TTextRender &Text, TGraphics &Graphics, bool MapEnabled) :
		m_Text(Text), m_Graphics(Graphics), m_PreviousFlags(Text.GetRenderFlags()), m_MapEnabled(MapEnabled)
	{
		if(m_MapEnabled)
			m_Graphics.GetScreen(&m_aScreen[0], &m_aScreen[1], &m_aScreen[2], &m_aScreen[3]);
	}
	CQmNameplateTextContext(const CQmNameplateTextContext &) = delete;
	CQmNameplateTextContext &operator=(const CQmNameplateTextContext &) = delete;
	~CQmNameplateTextContext()
	{
		try
		{
			Finish();
		}
		catch(...)
		{
		}
	}
	template<typename TMap>
	void Apply(unsigned Flags, TMap &&Map)
	{
		m_FlagsTouched = true;
		m_Text.SetRenderFlags(Flags);
		if(m_MapEnabled)
		{
			m_ScreenTouched = true;
			std::forward<TMap>(Map)();
		}
	}
	void Finish()
	{
		if(std::exchange(m_ScreenTouched, false))
		{
			try
			{
				m_Graphics.MapScreen(m_aScreen[0], m_aScreen[1], m_aScreen[2], m_aScreen[3]);
			}
			catch(...)
			{
				// 恢复映射自身失败也要尝试恢复flags，不替换原异常。
				try
				{
					RestoreFlags();
				}
				catch(...)
				{
				}
				throw;
			}
		}
		RestoreFlags();
	}
};

// 由名牌部件在主线程串行使用，构建回调不重入本状态。
// 每个部件最多有一次未收尾的创建；回收成功前不开始下一次，失败句柄不会失去owner。
class CQmNameplateTextBuild
{
	std::optional<STextContainerIndex> m_Pending;
	static_assert(std::is_nothrow_copy_constructible_v<STextContainerIndex> &&
		      std::is_nothrow_move_constructible_v<STextContainerIndex> &&
		      std::is_nothrow_swappable_v<STextContainerIndex>);

public:
	CQmNameplateTextBuild() = default;
	CQmNameplateTextBuild(const CQmNameplateTextBuild &) = delete;
	CQmNameplateTextBuild &operator=(const CQmNameplateTextBuild &) = delete;
	bool HasPendingCleanup() const { return m_Pending.has_value(); }
	template<typename TTextRender>
	void Cleanup(TTextRender &Text)
	{
		if(m_Pending)
		{
			// 图集失效的索引也可能仍持有槽位，不能只用Valid判断是否归还。
			if(m_Pending->m_Index >= 0)
				Text.DeleteTextContainer(*m_Pending);
			m_Pending.reset();
		}
	}
	template<typename TTextRender>
	void Reset(TTextRender &Text, STextContainerIndex &Index)
	{
		Cleanup(Text);
		Text.DeleteTextContainer(Index);
	}
	template<typename TTextRender, typename TBuild>
	bool Run(TTextRender &Text, STextContainerIndex &Index, bool &Reuse, bool KeepPrevious, CQmNameplateTextCache &Cache, TBuild &&Build)
	{
		const bool PreviousReuse = Reuse;
		bool Entered = false, RestorePrevious = false;
		try
		{
			Cleanup(Text);
			Entered = true;
			if(KeepPrevious)
			{
				// 复制已有索引不分配，避免默认构造一个临时usage对象。
				m_Pending.emplace(Index);
				RestorePrevious = true;
				Index.Reset();
				Reuse = false;
			}
			else if(!Reuse)
				Text.DeleteTextContainer(Index);
			std::forward<TBuild>(Build)();
			Reuse = PreviousReuse;
			if(KeepPrevious)
			{
				RestorePrevious = false;
				if(!Index.Valid())
				{
					std::swap(Index, *m_Pending);
					Cleanup(Text);
					return false;
				}
				// 新容器已完整生成；旧容器删除失败时两者继续有owner。
				Cleanup(Text);
			}
			return true;
		}
		catch(...)
		{
			Reuse = PreviousReuse;
			Cache.Reset();
			if(Entered)
			{
				if(RestorePrevious)
					std::swap(Index, *m_Pending);
				else if(!KeepPrevious)
				{
					m_Pending.emplace(std::move(Index));
					Index.Reset();
				}
			}
			throw;
		}
	}
};

#endif
