#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MENU_BACKGROUND_PREPARE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MENU_BACKGROUND_PREPARE_H

#include <memory>

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

#endif
