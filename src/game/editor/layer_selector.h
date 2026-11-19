#ifndef GAME_EDITOR_LAYER_SELECTOR_H
#define GAME_EDITOR_LAYER_SELECTOR_H

#include "component.h"

#include <cstddef>
#include <memory>
#include <vector>

class CLayer;

class CHoverTile
{
public:
	CHoverTile(size_t Group, size_t Layer, const std::shared_ptr<CLayer> &pLayer) :
		m_Group(Group),
		m_Layer(Layer),
		m_pLayer(pLayer)
	{
	}

	std::shared_ptr<CLayer> ResolveLayer(const std::vector<std::shared_ptr<CLayer>> &vpLayers) const
	{
		const auto pLayer = m_pLayer.lock();
		return m_Layer < vpLayers.size() && vpLayers[m_Layer] == pLayer ? pLayer : nullptr;
	}

	size_t m_Group;
	size_t m_Layer;

private:
	std::weak_ptr<CLayer> m_pLayer;
};

class CLayerSelector : public CEditorComponent
{
	int m_SelectionOffset;
	std::vector<CHoverTile> m_vHoverTiles;

public:
	void OnInit(CEditor *pEditor) override;
	bool SelectByTile();
	void UpdateHoveredTiles();
};

#endif
