#ifndef GAME_EDITOR_QUAD_KNIFE_H
#define GAME_EDITOR_QUAD_KNIFE_H

#include "component.h"

#include <memory>

class CLayerQuads;

class CQuadKnife : public CEditorComponent
{
public:
	class CState
	{
	public:
		bool m_Active;
		int m_SelectedQuadIndex;
		int m_Count;
		vec2 m_aPoints[4];
		bool m_Rectangle;
		bool m_Dragging;
		vec2 m_DragStart;
		std::weak_ptr<CLayerQuads> m_pLayer;

		void Reset();
	};

	bool IsActive() const;
	void Activate(int SelectedQuad, bool Rectangle = false);
	void Deactivate();
	void DoSlice(bool MouseInside);
};

#endif
