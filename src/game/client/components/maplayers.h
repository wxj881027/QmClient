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

	CEnvelopeState &EnvEvaluator() { return m_EnvEvaluator; }

private:
	CRenderLayerParams m_Params;
	CMapRenderer m_MapRenderer;
	CEnvelopeState m_EnvEvaluator;
	bool m_MapLoaded = false;
	bool m_MapLoadInProgress = false;
};

#endif
