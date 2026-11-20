#ifndef GAME_CLIENT_COMPONENTS_FREEZEBARS_H
#define GAME_CLIENT_COMPONENTS_FREEZEBARS_H
#include <base/vmath.h>

#include <game/client/component.h>

class CFreezeBars : public CComponent
{
	void RenderFreezeBar(int ClientId);
	void RenderFreezeBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);
	bool IsPlayerInfoAvailable(int ClientId) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

	// 在任意 Tee 位置画一根冻结进度条。查看模式的虚影回放没有服务器角色数据，
	// 但冻结段与进度是可从回放轨迹推出来的，复用同一套外观保持与实机一致。
	void RenderBarAt(vec2 TeePos, float Progress, float Alpha = 1.0f);
};

#endif
