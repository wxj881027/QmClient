#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_OUTLINES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_OUTLINES_H

#include <game/client/component.h>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

class CTile;
class CTeleTile;

// 新地图不能复用旧描边；只有可选缓存的资源不足在此回退，未知异常继续传播。
template<typename TBuild>
bool QmPrepareOutlineMapCache(ivec2 &Size, std::vector<int> &vData, TBuild &&Build)
{
	vData.clear();
	Size = {0, 0};
	const auto Disable = [&] {
		Size = {0, 0};
		std::vector<int>().swap(vData);
	};
	try
	{
		std::forward<TBuild>(Build)();
		return true;
	}
	catch(const std::bad_alloc &)
	{
	}
	catch(const std::length_error &)
	{
	}
	catch(...)
	{
		Disable();
		throw;
	}
	Disable();
	return false;
}

class COutlines : public CComponent
{
private:
	ivec2 m_MapDataSize;
	std::vector<int> m_vMapData;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnRender() override;
};

#endif
