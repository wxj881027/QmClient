#ifndef GAME_MAP_MAP_RENDERER_H
#define GAME_MAP_MAP_RENDERER_H

#include <engine/map.h>

#include <game/layers.h>
#include <game/map/envelope_manager.h>
#include <game/map/render_component.h>
#include <game/map/render_layer.h>

#include <memory>

typedef std::function<void(int Completed, int Total)> FCallbackMapRendererInit;

class CMapRenderer : public CRenderComponent
{
public:
	CMapRenderer() = default;

	// 管理器由已加载图层共享，交换所属组件后只重绑求值器借用。
	void RebindEnvelopeEval(const IEnvelopeEval *pEnvelopeEval)
	{
		if(m_pLoadEnvelopeManager)
			m_pLoadEnvelopeManager->SetEnvelopeEval(pEnvelopeEval);
	}
	void Clear();
	void BeginLoad(ERenderType Type, CLayers *pLayers, IMapImages *pMapImages, const IEnvelopeEval *pEnvelopeEval, std::optional<FCallbackMapRendererInit> CallbackMapRendererInitOptional = std::nullopt);
	void LoadStep(int MaxLayers = 1);
	bool IsLoadFinished() const { return m_LoadFinished; }
	void Load(ERenderType Type, CLayers *pLayers, IMapImages *pMapImages, const IEnvelopeEval *pEnvelopeEval, std::optional<FCallbackMapRendererInit> CallbackMapRendererInitOptional);
	void Render(const CRenderLayerParams &Params);

private:
	int GetLayerType(const CMapItemLayer *pLayer) const;

	std::vector<std::unique_ptr<CRenderLayer>> m_vpRenderLayers;
	ERenderType m_LoadType = ERenderType::RENDERTYPE_FULL_DESIGN;
	CLayers *m_pLoadLayers = nullptr;
	IMapImages *m_pLoadImages = nullptr;
	std::shared_ptr<CEnvelopeManager> m_pLoadEnvelopeManager;
	std::optional<FCallbackMapRendererInit> m_LoadCallback;
	std::unique_ptr<CRenderLayer> m_pLoadGroup;
	int m_LoadGroupId = 0;
	int m_GameGroupId = -1;
	int m_GameLayerId = -1;
	int m_LoadLayerId = 0;
	int m_LoadTotalWork = 0;
	int m_LoadCompletedWork = 0;
	bool m_LoadPassedGameLayer = false;
	bool m_LoadFinished = true;
};

#endif
