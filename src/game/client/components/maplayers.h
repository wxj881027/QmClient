/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MAPLAYERS_H
#define GAME_CLIENT_COMPONENTS_MAPLAYERS_H

#include <game/client/component.h>
#include <game/client/components/envelope_state.h>
#include <game/map/map_renderer.h>

class CCamera;
class CLayers;
class CMapImages;
class ColorRGBA;

class CMapLayers : public CComponent
{
	// TClient
	friend class CScriptRunner;
	friend class COutlines;

	friend class CBackground;
	friend class CMenuBackground;

	// 图层来源与渲染模式。protected：派生类（如菜单背景、地图预览渲染器）
	// 需要注入独立的 CLayers/CMapImages 来源。
protected:
	CLayers *m_pLayers;
	CMapImages *m_pImages;
	ERenderType m_Type;
	bool m_OnlineOnly;

public:
	CMapLayers(ERenderType Type, bool OnlineOnly = true);
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnMapLoad() override;
	bool AdvanceMapLoad();
	void RenderCustom(const vec2 &Center, float Zoom);
	void RenderCustomWithCamera(const vec2 &Center, float Zoom);
	void SwapState(CMapLayers &Other);

	virtual CCamera *GetCurCamera();

	// QmClient: 供菜单背景等调用方判断图层是否已经可以渲染，避免加载期间误报渲染成功。
	bool IsMapLoaded() const { return m_MapLoaded; }

	// QmClient: 离线用途（地图预览等）覆盖实体叠加透明度；>=0 时生效，
	// 100 = 只渲染实体层（贴图层与四边形全部跳过，引擎既有语义）。
protected:
	int m_EntityOverlayValOverride = -1;

public:
	CEnvelopeState &EnvEvaluator() { return m_EnvEvaluator; }

private:
	CRenderLayerParams m_Params;
	CMapRenderer m_MapRenderer;
	CEnvelopeState m_EnvEvaluator;
	bool m_MapLoaded = false;
	bool m_MapLoadInProgress = false;
};

#endif
