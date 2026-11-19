#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MENU_BACKGROUND_PREPARE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MENU_BACKGROUND_PREPARE_H

#include <memory>
#include <new>
#include <stdexcept>

// 工厂返回 unique_ptr；候选只持有新资源，不接管当前显示状态。
template<typename TMap, typename TLayers, typename TImages, typename TRenderer>
struct SMenuBackgroundPrepared
{
	std::unique_ptr<TMap> m_pMap;
	std::unique_ptr<TLayers> m_pLayers;
	std::unique_ptr<TImages> m_pImages;
	std::unique_ptr<TRenderer> m_pRenderer;
};

// 工厂和初始化全部完成后才返回；任一步异常由已构造成员回收此前候选。
template<typename FMap, typename FLayers, typename FImages, typename FRenderer, typename FInit>
auto PrepareMenuBackgroundResources(FMap CreateMap, FLayers CreateLayers, FImages CreateImages, FRenderer CreateRenderer, FInit Initialise)
{
	using SPrepared = SMenuBackgroundPrepared<typename decltype(CreateMap())::element_type, typename decltype(CreateLayers())::element_type,
		typename decltype(CreateImages())::element_type, typename decltype(CreateRenderer())::element_type>;
	SPrepared Prepared{CreateMap(), CreateLayers(), CreateImages(), CreateRenderer()};
	Initialise(Prepared);
	return Prepared;
}

// 地图准备失败后只重试清理，不再恢复同一个失败协程。
class CMenuBackgroundMapPreparation
{
	enum class EState
	{
		ACTIVE,
		FAILED_PENDING_CLEANUP,
		FAILED,
	};
	EState m_State = EState::ACTIVE;

public:
	bool HasFailed() const { return m_State != EState::ACTIVE; }
	void Reset() { m_State = EState::ACTIVE; }

	template<typename TRenderer>
	bool RetryCleanup(TRenderer &Renderer)
	{
		if(m_State != EState::FAILED_PENDING_CLEANUP)
			return true;
		try
		{
			Renderer.UnloadMap();
		}
		catch(const std::bad_alloc &) { return false; }
		catch(const std::length_error &) { return false; }
		m_State = EState::FAILED;
		return true;
	}

	template<typename TRenderer>
	bool Cancel(TRenderer &Renderer)
	{
		m_State = EState::FAILED_PENDING_CLEANUP;
		return RetryCleanup(Renderer);
	}

	template<typename TRenderer>
	bool Advance(TRenderer &Renderer)
	{
		if(HasFailed())
		{
			RetryCleanup(Renderer);
			return false;
		}
		try
		{
			return Renderer.AdvanceMapLoad();
		}
		catch(const std::bad_alloc &) {}
		catch(const std::length_error &) {}
		Cancel(Renderer);
		return false;
	}
};

// 回收开始后不再绘制旧包；GPU 或图片删除失败时保留整包与身份。
template<typename TPrevious>
bool ReleaseMenuBackgroundResources(std::unique_ptr<TPrevious> &pPrevious, bool &Retiring)
{
	if(pPrevious == nullptr)
	{
		Retiring = false;
		return true;
	}
	Retiring = true;
	try
	{
		if(pPrevious->m_pRenderer)
			pPrevious->m_pRenderer->UnloadMap();
		if(pPrevious->m_pImages)
			pPrevious->m_pImages->Unload();
	}
	catch(const std::bad_alloc &) { return false; }
	catch(const std::length_error &) { return false; }
	pPrevious.reset();
	Retiring = false;
	return true;
}

template<typename TPrevious>
bool CanRenderMenuBackgroundPrevious(const std::unique_ptr<TPrevious> &pPrevious, bool Retiring)
{
	return pPrevious != nullptr && pPrevious->m_pRenderer != nullptr && !Retiring;
}

// 已准备的当前地图才可接替 previous；未完成候选只清理自身，保留可用旧包。
template<typename FReleasePrevious, typename FPreserveCurrent, typename FDiscardCurrent>
bool PrepareMenuBackgroundChange(bool CurrentReady, bool PreviousRetiring,
	FReleasePrevious ReleasePrevious, FPreserveCurrent PreserveCurrent, FDiscardCurrent DiscardCurrent)
{
	if(CurrentReady)
		return ReleasePrevious() && PreserveCurrent();
	if(PreviousRetiring && !ReleasePrevious())
		return false;
	return DiscardCurrent();
}

#endif
