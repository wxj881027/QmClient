/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef GAME_EDITOR_ENVELOPE_EDITOR_H
#define GAME_EDITOR_ENVELOPE_EDITOR_H

#include <game/client/ui.h>
#include <game/editor/component.h>
#include <game/editor/editor_trackers.h>
#include <game/editor/smooth_value.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

class CEnvelope;

namespace EnvelopeEditor
{
inline int RoundFixedPoint(double Value)
{
	if(std::isnan(Value))
		return 0;
	return static_cast<int>(std::clamp(std::round(Value), (double)std::numeric_limits<int>::min(), (double)std::numeric_limits<int>::max()));
}
}

class CEnvelopeEditor : public CEditorComponent
{
public:
	class CState
	{
	public:
		CSmoothValue m_ZoomX;
		CSmoothValue m_ZoomY;
		bool m_ResetZoom;
		vec2 m_Offset;
		int m_ActiveChannels;

		void Reset(CEditor *pEditor);
	};

	void OnReset() override;
	void Render(CUIRect View);

private:
	std::shared_ptr<CEnvelope> RefreshSelectedEnvelope(bool FromToolbar = false);
	void ResetOperation();
	void RenderColorBar(CUIRect ColorBar, const std::shared_ptr<CEnvelope> &pEnvelope);

	void UpdateHotEnvelopeObject(const CUIRect &View, const CEnvelope *pEnvelope, int ActiveChannels);

	void ResetZoomEnvelope(const std::shared_ptr<CEnvelope> &pEnvelope, int ActiveChannels);

	void ZoomAdaptOffsetX(float ZoomFactor, const CUIRect &View);
	void UpdateZoomEnvelopeX(const CUIRect &View);

	void ZoomAdaptOffsetY(float ZoomFactor, const CUIRect &View);
	void UpdateZoomEnvelopeY(const CUIRect &View);

	float ScreenToEnvelopeX(const CUIRect &View, float x) const;
	float EnvelopeToScreenX(const CUIRect &View, float x) const;
	float ScreenToEnvelopeY(const CUIRect &View, float y) const;
	float EnvelopeToScreenY(const CUIRect &View, float y) const;
	float ScreenToEnvelopeDX(const CUIRect &View, float DeltaX);
	float ScreenToEnvelopeDY(const CUIRect &View, float DeltaY);

	const char m_RedoButtonId = 0;
	const char m_UndoButtonId = 0;
	const char m_NewSoundEnvelopeButtonId = 0;
	const char m_NewColorEnvelopeButtonId = 0;
	const char m_NewPositionEnvelopeButtonId = 0;
	const char m_DeleteButtonId = 0;
	const char m_MoveRightButtonId = 0;
	const char m_MoveLeftButtonId = 0;
	const char m_ZoomOutButtonId = 0;
	const char m_ResetZoomButtonId = 0;
	const char m_ZoomInButtonId = 0;
	int m_EnvelopeSelectorId = 0;
	const char m_PrevEnvelopeButtonId = 0;
	const char m_NextEnvelopeButtonId = 0;
	const char m_aChannelButtonIds[CEnvPoint::MAX_CHANNELS] = {0};
	const char m_EnvelopeEditorId = 0;
	const char m_ScaleId = 0;
	std::shared_ptr<CEnvelope> m_pDisplayedEnvelope;
	CLineInput m_NameInput;
	int m_EnvelopeEditorButtonUsed;
	EEnvelopeEditorOp m_Operation;
	std::vector<double> m_vAccurateDragValuesX;
	std::vector<double> m_vAccurateDragValuesY;
	vec2 m_MouseStart;
	vector2_base<double> m_ScaleFactor;
	vector2_base<double> m_Midpoint;
	std::vector<double> m_vInitialPositionsX;
	std::vector<double> m_vInitialPositionsY;

	// Popup 函数保留在 CEditor（popups.cpp），这里只提供稳定的菜单 id 成员
	SPopupMenuId m_PopupEnvPointId;
	SPopupMenuId m_PopupEnvPointMultiId;
	SPopupMenuId m_PopupCurvetypeId;
};

#endif
