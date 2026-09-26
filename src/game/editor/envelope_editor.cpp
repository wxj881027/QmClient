/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "envelope_editor.h"

#include <base/color.h>
#include <base/math.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/qm_icon_manager.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/envelope.h>
#include <game/localization.h>

#include <limits>

static const char *const CURVE_TYPE_NAMES_SHORT[] = {
	Localize("N", "Editor curve type abbreviation"),
	Localize("L", "Editor curve type abbreviation"),
	Localize("S", "Editor curve type abbreviation"),
	Localize("F", "Editor curve type abbreviation"),
	Localize("M", "Editor curve type abbreviation"),
	Localize("B", "Editor curve type abbreviation"),
};
static_assert(std::size(CURVE_TYPE_NAMES_SHORT) == NUM_CURVETYPES);

static const char *CurveTypeNameShort(int CurveType)
{
	if(0 <= CurveType && CurveType < (int)std::size(CURVE_TYPE_NAMES_SHORT))
		return Localize(CURVE_TYPE_NAMES_SHORT[CurveType], CurveType == CURVETYPE_BEZIER ? "Editor bezier curve abbreviation" : "Editor");
	return "!?";
}

void CEnvelopeEditor::CState::Reset(CEditor *pEditor)
{
	m_ZoomX = CSmoothValue(1.0f, 0.1f, 600.0f);
	m_ZoomY = CSmoothValue(640.0f, 0.1f, 32000.0f);
	m_ZoomX.OnInit(pEditor);
	m_ZoomY.OnInit(pEditor);
	m_ResetZoom = true;
	m_Offset = vec2(0.1f, 0.5f);
	m_ActiveChannels = 0xf;
}

void CEnvelopeEditor::OnReset()
{
	m_NameInput.SetBuffer(nullptr, 0, 0);
	m_EnvelopeEditorButtonUsed = -1;
	m_Operation = EEnvelopeEditorOp::OP_NONE;
	m_vAccurateDragValuesX.clear();
	m_vAccurateDragValuesY.clear();
	m_MouseStart = vec2(0.0f, 0.0f);
	m_ScaleFactor = vec2(1.0f, 1.0f);
	m_Midpoint = vec2(0.0f, 0.0f);
	m_vInitialPositionsX.clear();
	m_vInitialPositionsY.clear();
}

void CEnvelopeEditor::ZoomAdaptOffsetX(float ZoomFactor, const CUIRect &View)
{
	CState &State = Map()->m_EnvelopeEditorState;
	float PosX = g_Config.m_EdZoomTarget ? (Ui()->MouseX() - View.x) / View.w : 0.5f;
	State.m_Offset.x = PosX - (PosX - State.m_Offset.x) * ZoomFactor;
}

void CEnvelopeEditor::UpdateZoomEnvelopeX(const CUIRect &View)
{
	CState &State = Map()->m_EnvelopeEditorState;
	float OldZoom = State.m_ZoomX.GetValue();
	if(State.m_ZoomX.UpdateValue())
		ZoomAdaptOffsetX(OldZoom / State.m_ZoomX.GetValue(), View);
}

void CEnvelopeEditor::ZoomAdaptOffsetY(float ZoomFactor, const CUIRect &View)
{
	CState &State = Map()->m_EnvelopeEditorState;
	float PosY = g_Config.m_EdZoomTarget ? 1.0f - (Ui()->MouseY() - View.y) / View.h : 0.5f;
	State.m_Offset.y = PosY - (PosY - State.m_Offset.y) * ZoomFactor;
}

void CEnvelopeEditor::UpdateZoomEnvelopeY(const CUIRect &View)
{
	CState &State = Map()->m_EnvelopeEditorState;
	float OldZoom = State.m_ZoomY.GetValue();
	if(State.m_ZoomY.UpdateValue())
		ZoomAdaptOffsetY(OldZoom / State.m_ZoomY.GetValue(), View);
}

void CEnvelopeEditor::ResetZoomEnvelope(const std::shared_ptr<CEnvelope> &pEnvelope, int ActiveChannels)
{
	CState &State = Map()->m_EnvelopeEditorState;
	auto [Bottom, Top] = pEnvelope->GetValueRange(ActiveChannels);
	float EndTime = pEnvelope->EndTime();
	float ValueRange = absolute(Top - Bottom);

	if(ValueRange < State.m_ZoomY.GetMinValue())
	{
		// Set view to some sane default if range is too small
		State.m_Offset.y = 0.5f - ValueRange / State.m_ZoomY.GetMinValue() / 2.0f - Bottom / State.m_ZoomY.GetMinValue();
		State.m_ZoomY.SetValueInstant(State.m_ZoomY.GetMinValue());
	}
	else if(ValueRange > State.m_ZoomY.GetMaxValue())
	{
		State.m_Offset.y = -Bottom / State.m_ZoomY.GetMaxValue();
		State.m_ZoomY.SetValueInstant(State.m_ZoomY.GetMaxValue());
	}
	else
	{
		// calculate biggest possible spacing
		float SpacingFactor = minimum(1.25f, State.m_ZoomY.GetMaxValue() / ValueRange);
		State.m_ZoomY.SetValueInstant(SpacingFactor * ValueRange);
		float Space = 1.0f / SpacingFactor;
		float Spacing = (1.0f - Space) / 2.0f;

		if(Top >= 0 && Bottom >= 0)
			State.m_Offset.y = Spacing - Bottom / State.m_ZoomY.GetValue();
		else if(Top <= 0 && Bottom <= 0)
			State.m_Offset.y = Spacing - Bottom / State.m_ZoomY.GetValue();
		else
			State.m_Offset.y = Spacing + Space * absolute(Bottom) / ValueRange;
	}

	if(EndTime < State.m_ZoomX.GetMinValue())
	{
		State.m_Offset.x = 0.5f - EndTime / State.m_ZoomX.GetMinValue();
		State.m_ZoomX.SetValueInstant(State.m_ZoomX.GetMinValue());
	}
	else if(EndTime > State.m_ZoomX.GetMaxValue())
	{
		State.m_Offset.x = 0.0f;
		State.m_ZoomX.SetValueInstant(State.m_ZoomX.GetMaxValue());
	}
	else
	{
		float SpacingFactor = minimum(1.25f, State.m_ZoomX.GetMaxValue() / EndTime);
		State.m_ZoomX.SetValueInstant(SpacingFactor * EndTime);
		float Space = 1.0f / SpacingFactor;
		float Spacing = (1.0f - Space) / 2.0f;

		State.m_Offset.x = Spacing;
	}
}

float CEnvelopeEditor::ScreenToEnvelopeX(const CUIRect &View, float x) const
{
	const CState &State = Map()->m_EnvelopeEditorState;
	return (x - View.x - View.w * State.m_Offset.x) / View.w * State.m_ZoomX.GetValue();
}

float CEnvelopeEditor::EnvelopeToScreenX(const CUIRect &View, float x) const
{
	const CState &State = Map()->m_EnvelopeEditorState;
	return View.x + View.w * State.m_Offset.x + x / State.m_ZoomX.GetValue() * View.w;
}

float CEnvelopeEditor::ScreenToEnvelopeY(const CUIRect &View, float y) const
{
	const CState &State = Map()->m_EnvelopeEditorState;
	return (View.h - y + View.y) / View.h * State.m_ZoomY.GetValue() - State.m_Offset.y * State.m_ZoomY.GetValue();
}

float CEnvelopeEditor::EnvelopeToScreenY(const CUIRect &View, float y) const
{
	const CState &State = Map()->m_EnvelopeEditorState;
	return View.y + View.h - y / State.m_ZoomY.GetValue() * View.h - State.m_Offset.y * View.h;
}

float CEnvelopeEditor::ScreenToEnvelopeDX(const CUIRect &View, float DeltaX)
{
	CState &State = Map()->m_EnvelopeEditorState;
	return DeltaX / Graphics()->ScreenWidth() * Ui()->Screen()->w / View.w * State.m_ZoomX.GetValue();
}

float CEnvelopeEditor::ScreenToEnvelopeDY(const CUIRect &View, float DeltaY)
{
	CState &State = Map()->m_EnvelopeEditorState;
	return DeltaY / Graphics()->ScreenHeight() * Ui()->Screen()->h / View.h * State.m_ZoomY.GetValue();
}

void CEnvelopeEditor::RemoveTimeOffsetEnvelope(const std::shared_ptr<CEnvelope> &pEnvelope)
{
	CState &State = Map()->m_EnvelopeEditorState;
	CFixedTime TimeOffset = pEnvelope->m_vPoints[0].m_Time;
	for(auto &Point : pEnvelope->m_vPoints)
		Point.m_Time -= TimeOffset;

	State.m_Offset.x += TimeOffset.AsSeconds() / State.m_ZoomX.GetValue();
}

static float ClampDelta(float Val, float Delta, float Min, float Max)
{
	if(Val + Delta <= Min)
		return Min - Val;
	if(Val + Delta >= Max)
		return Max - Val;
	return Delta;
}

namespace
{

	class CTimeStep
	{
	public:
		template<class T>
		CTimeStep(T t)
		{
			if constexpr(std::is_same_v<T, std::chrono::milliseconds>)
				m_Unit = ETimeUnit::MILLISECONDS;
			else if constexpr(std::is_same_v<T, std::chrono::seconds>)
				m_Unit = ETimeUnit::SECONDS;
			else
				m_Unit = ETimeUnit::MINUTES;

			m_Value = t;
		}

		CTimeStep operator*(int k) const
		{
			return CTimeStep(m_Value * k, m_Unit);
		}

		CTimeStep operator-(const CTimeStep &Other)
		{
			return CTimeStep(m_Value - Other.m_Value, m_Unit);
		}

		void Format(char *pBuffer, size_t BufferSize)
		{
			int Milliseconds = m_Value.count() % 1000;
			int Seconds = std::chrono::duration_cast<std::chrono::seconds>(m_Value).count() % 60;
			int Minutes = std::chrono::duration_cast<std::chrono::minutes>(m_Value).count();

			switch(m_Unit)
			{
			case ETimeUnit::MILLISECONDS:
				if(Minutes != 0)
					str_format(pBuffer, BufferSize, Localize("%d:%02d.%03dmin", "Editor"), Minutes, Seconds, Milliseconds);
				else if(Seconds != 0)
					str_format(pBuffer, BufferSize, Localize("%d.%03ds", "Editor"), Seconds, Milliseconds);
				else
					str_format(pBuffer, BufferSize, Localize("%dms", "Editor"), Milliseconds);
				break;
			case ETimeUnit::SECONDS:
				if(Minutes != 0)
					str_format(pBuffer, BufferSize, Localize("%d:%02dmin", "Editor"), Minutes, Seconds);
				else
					str_format(pBuffer, BufferSize, Localize("%ds", "Editor"), Seconds);
				break;
			case ETimeUnit::MINUTES:
				str_format(pBuffer, BufferSize, Localize("%dmin", "Editor"), Minutes);
				break;
			}
		}

		float AsSeconds() const
		{
			return std::chrono::duration_cast<std::chrono::duration<float>>(m_Value).count();
		}

	private:
		enum class ETimeUnit
		{
			MILLISECONDS,
			SECONDS,
			MINUTES
		} m_Unit;
		std::chrono::milliseconds m_Value;

		CTimeStep(std::chrono::milliseconds Value, ETimeUnit Unit)
		{
			m_Value = Value;
			m_Unit = Unit;
		}
	};

}

void CEnvelopeEditor::UpdateHotEnvelopeObject(const CUIRect &View, const CEnvelope *pEnvelope, int ActiveChannels)
{
	if(!Ui()->MouseInside(&View))
		return;

	const vec2 MousePos = Ui()->MousePos();

	float MinDist = 200.0f;
	const void *pMinPointId = nullptr;

	const auto UpdateMinimum = [&](vec2 Position, const void *pId) {
		const float CurrDist = length_squared(Position - MousePos);
		if(CurrDist < MinDist)
		{
			MinDist = CurrDist;
			pMinPointId = pId;
		}
	};

	for(size_t i = 0; i < pEnvelope->m_vPoints.size(); i++)
	{
		for(int c = pEnvelope->GetChannels() - 1; c >= 0; c--)
		{
			if(!(ActiveChannels & (1 << c)))
				continue;

			if(i > 0 && pEnvelope->m_vPoints[i - 1].m_Curvetype == CURVETYPE_BEZIER)
			{
				vec2 Position;
				Position.x = EnvelopeToScreenX(View, (pEnvelope->m_vPoints[i].m_Time + pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c]).AsSeconds());
				Position.y = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c] + pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaY[c]));
				UpdateMinimum(Position, &pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c]);
			}

			if(i < pEnvelope->m_vPoints.size() - 1 && pEnvelope->m_vPoints[i].m_Curvetype == CURVETYPE_BEZIER)
			{
				vec2 Position;
				Position.x = EnvelopeToScreenX(View, (pEnvelope->m_vPoints[i].m_Time + pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c]).AsSeconds());
				Position.y = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c] + pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaY[c]));
				UpdateMinimum(Position, &pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c]);
			}

			vec2 Position;
			Position.x = EnvelopeToScreenX(View, pEnvelope->m_vPoints[i].m_Time.AsSeconds());
			Position.y = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c]));
			UpdateMinimum(Position, &pEnvelope->m_vPoints[i].m_aValues[c]);
		}
	}

	if(pMinPointId != nullptr)
	{
		Ui()->SetHotItem(pMinPointId);
	}
	else if(!Map()->m_EnvelopeEvaluator.m_Animate && pEnvelope->EndTime() > 0.0f)
	{
		const float Time = Map()->m_EnvelopeEvaluator.m_AnimateTime * Map()->m_EnvelopeEvaluator.m_AnimateSpeed;
		const float LoopedTime = std::fmod(Time, pEnvelope->EndTime());
		if(absolute(EnvelopeToScreenX(View, Time) - MousePos.x) < 20.0f || absolute(EnvelopeToScreenX(View, LoopedTime) - MousePos.x) < 20.0f)
		{
			Ui()->SetHotItem(&Map()->m_EnvelopeEvaluator.m_AnimateTime);
		}
	}
}

void CEnvelopeEditor::Render(CUIRect View)
{
	CState &State = Map()->m_EnvelopeEditorState;
	Map()->m_SelectedEnvelope = Map()->m_vpEnvelopes.empty() ? -1 : std::clamp(Map()->m_SelectedEnvelope, 0, (int)Map()->m_vpEnvelopes.size() - 1);
	std::shared_ptr<CEnvelope> pEnvelope = Map()->m_vpEnvelopes.empty() ? nullptr : Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];

	CUIRect ToolBar, CurveBar, ColorBar, DragBar;
	View.HSplitTop(30.0f, &DragBar, nullptr);
	DragBar.y -= 2.0f;
	DragBar.w += 2.0f;
	DragBar.h += 4.0f;
	Editor()->DoEditorDragBar(View, &DragBar, CEditor::EDragSide::SIDE_TOP, &Editor()->m_aExtraEditorSplits[CEditor::EXTRAEDITOR_ENVELOPES]);
	View.HSplitTop(15.0f, &ToolBar, &View);
	View.HSplitTop(15.0f, &CurveBar, &View);
	ToolBar.Margin(2.0f, &ToolBar);
	CurveBar.Margin(2.0f, &CurveBar);

	bool CurrentEnvelopeSwitched = false;

	// do the toolbar
	{
		CUIRect Button;

		// redo button
		ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
		if(Editor()->DoButton_QmIcon(&m_RedoButtonId, EQmIcon::REDO, FontIcons::FONT_ICON_REDO, Map()->m_EnvelopeEditorHistory.CanRedo() ? 0 : -1, &Button, BUTTONFLAG_LEFT, Localize("[Ctrl+Y] Redo the last action.", "Editor"), IGraphics::CORNER_R, 11.0f) == 1)
		{
			Map()->m_EnvelopeEditorHistory.Redo();
		}

		// undo button
		ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
		ToolBar.VSplitRight(10.0f, &ToolBar, nullptr);
		if(Editor()->DoButton_QmIcon(&m_UndoButtonId, EQmIcon::UNDO, FontIcons::FONT_ICON_UNDO, Map()->m_EnvelopeEditorHistory.CanUndo() ? 0 : -1, &Button, BUTTONFLAG_LEFT, Localize("[Ctrl+Z] Undo the last action.", "Editor"), IGraphics::CORNER_L, 11.0f) == 1)
		{
			Map()->m_EnvelopeEditorHistory.Undo();
		}

		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		if(Editor()->DoButton_Editor(&m_NewSoundEnvelopeButtonId, Localize("Sound+", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Create a new sound envelope.", "Editor")))
		{
			Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionEnvelopeAdd>(Map(), CEnvelope::EType::SOUND));
			pEnvelope = Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];
			CurrentEnvelopeSwitched = true;
		}

		ToolBar.VSplitRight(5.0f, &ToolBar, nullptr);
		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		if(Editor()->DoButton_Editor(&m_NewColorEnvelopeButtonId, Localize("Color+", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Create a new color envelope.", "Editor")))
		{
			Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionEnvelopeAdd>(Map(), CEnvelope::EType::COLOR));
			pEnvelope = Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];
			CurrentEnvelopeSwitched = true;
		}

		ToolBar.VSplitRight(5.0f, &ToolBar, nullptr);
		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		if(Editor()->DoButton_Editor(&m_NewPositionEnvelopeButtonId, Localize("Pos.+", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Create a new position envelope.", "Editor")))
		{
			Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionEnvelopeAdd>(Map(), CEnvelope::EType::POSITION));
			pEnvelope = Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];
			CurrentEnvelopeSwitched = true;
		}

		if(Map()->m_SelectedEnvelope >= 0)
		{
			// Delete button
			ToolBar.VSplitRight(10.0f, &ToolBar, nullptr);
			ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
			if(Editor()->DoButton_Editor(&m_DeleteButtonId, "✗", 0, &Button, BUTTONFLAG_LEFT, Localize("Delete this envelope.", "Editor")))
			{
				auto vpObjectReferences = Map()->DeleteEnvelope(Map()->m_SelectedEnvelope);
				Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionEnvelopeDelete>(Map(), Map()->m_SelectedEnvelope, vpObjectReferences, pEnvelope));

				Map()->m_SelectedEnvelope = Map()->m_vpEnvelopes.empty() ? -1 : std::clamp(Map()->m_SelectedEnvelope, 0, (int)Map()->m_vpEnvelopes.size() - 1);
				pEnvelope = Map()->m_vpEnvelopes.empty() ? nullptr : Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];
				Map()->OnModify();
			}
		}

		// check again, because the last envelope might has been deleted
		if(Map()->m_SelectedEnvelope >= 0)
		{
			// Move right button
			ToolBar.VSplitRight(5.0f, &ToolBar, nullptr);
			ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
			if(Editor()->DoButton_Ex(&m_MoveRightButtonId, "→", (Map()->m_SelectedEnvelope >= (int)Map()->m_vpEnvelopes.size() - 1 ? -1 : 0), &Button, BUTTONFLAG_LEFT, Localize("Move this envelope to the right.", "Editor"), IGraphics::CORNER_R))
			{
				int MoveTo = Map()->m_SelectedEnvelope + 1;
				int MoveFrom = Map()->m_SelectedEnvelope;
				Map()->m_SelectedEnvelope = Map()->MoveEnvelope(MoveFrom, MoveTo);
				if(Map()->m_SelectedEnvelope != MoveFrom)
				{
					Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionEnvelopeEdit>(Map(), Map()->m_SelectedEnvelope, CEditorActionEnvelopeEdit::EEditType::ORDER, MoveFrom, Map()->m_SelectedEnvelope));
					pEnvelope = Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];
					Map()->OnModify();
				}
			}

			// Move left button
			ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
			if(Editor()->DoButton_Ex(&m_MoveLeftButtonId, "←", (Map()->m_SelectedEnvelope <= 0 ? -1 : 0), &Button, BUTTONFLAG_LEFT, Localize("Move this envelope to the left.", "Editor"), IGraphics::CORNER_L))
			{
				int MoveTo = Map()->m_SelectedEnvelope - 1;
				int MoveFrom = Map()->m_SelectedEnvelope;
				Map()->m_SelectedEnvelope = Map()->MoveEnvelope(MoveFrom, MoveTo);
				if(Map()->m_SelectedEnvelope != MoveFrom)
				{
					Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionEnvelopeEdit>(Map(), Map()->m_SelectedEnvelope, CEditorActionEnvelopeEdit::EEditType::ORDER, MoveFrom, Map()->m_SelectedEnvelope));
					pEnvelope = Map()->m_vpEnvelopes[Map()->m_SelectedEnvelope];
					Map()->OnModify();
				}
			}

			if(pEnvelope)
			{
				ToolBar.VSplitRight(5.0f, &ToolBar, nullptr);
				ToolBar.VSplitRight(20.0f, &ToolBar, &Button);
				if(Editor()->DoButton_QmIcon(&m_ZoomOutButtonId, EQmIcon::MINUS, FontIcons::FONT_ICON_MINUS, 0, &Button, BUTTONFLAG_LEFT, Localize("[NumPad-] Zoom out horizontally, hold shift to zoom vertically.", "Editor"), IGraphics::CORNER_R, 9.0f))
				{
					if(Input()->ShiftIsPressed())
						State.m_ZoomY.ChangeValue(0.1f * State.m_ZoomY.GetValue());
					else
						State.m_ZoomX.ChangeValue(0.1f * State.m_ZoomX.GetValue());
				}

				ToolBar.VSplitRight(20.0f, &ToolBar, &Button);
				if(Editor()->DoButton_QmIcon(&m_ResetZoomButtonId, EQmIcon::SEARCH, FontIcons::FONT_ICON_MAGNIFYING_GLASS, 0, &Button, BUTTONFLAG_LEFT, Localize("[NumPad*] Reset zoom to default value.", "Editor"), IGraphics::CORNER_NONE, 9.0f))
					ResetZoomEnvelope(pEnvelope, State.m_ActiveChannels);

				ToolBar.VSplitRight(20.0f, &ToolBar, &Button);
				if(Editor()->DoButton_QmIcon(&m_ZoomInButtonId, EQmIcon::PLUS, FontIcons::FONT_ICON_PLUS, 0, &Button, BUTTONFLAG_LEFT, Localize("[NumPad+] Zoom in horizontally, hold shift to zoom vertically.", "Editor"), IGraphics::CORNER_L, 9.0f))
				{
					if(Input()->ShiftIsPressed())
						State.m_ZoomY.ChangeValue(-0.1f * State.m_ZoomY.GetValue());
					else
						State.m_ZoomX.ChangeValue(-0.1f * State.m_ZoomX.GetValue());
				}
			}

			// Margin on the right side
			ToolBar.VSplitRight(7.0f, &ToolBar, nullptr);
		}

		CUIRect Shifter, Inc, Dec;
		ToolBar.VSplitLeft(60.0f, &Shifter, &ToolBar);
		Shifter.VSplitRight(15.0f, &Shifter, &Inc);
		Shifter.VSplitLeft(15.0f, &Dec, &Shifter);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%d/%d", Map()->m_SelectedEnvelope + 1, (int)Map()->m_vpEnvelopes.size());

		ColorRGBA EnvColor = ColorRGBA(1, 1, 1, 0.5f);
		if(!Map()->m_vpEnvelopes.empty())
		{
			EnvColor = Map()->IsEnvelopeUsed(Map()->m_SelectedEnvelope) ? ColorRGBA(1, 0.7f, 0.7f, 0.5f) : ColorRGBA(0.7f, 1, 0.7f, 0.5f);
		}

		auto NewValueRes = Editor()->UiDoValueSelector(&m_EnvelopeSelectorId, &Shifter, aBuf, Map()->m_SelectedEnvelope + 1, 1, Map()->m_vpEnvelopes.size(), 1, 1.0f, Localize("Select the envelope.", "Editor"), false, false, IGraphics::CORNER_NONE, &EnvColor, false);
		int NewValue = NewValueRes.m_Value;
		if(NewValue - 1 != Map()->m_SelectedEnvelope)
		{
			Map()->m_SelectedEnvelope = NewValue - 1;
			CurrentEnvelopeSwitched = true;
		}

		if(Editor()->DoButton_QmIcon(&m_PrevEnvelopeButtonId, EQmIcon::MINUS, FontIcons::FONT_ICON_MINUS, 0, &Dec, BUTTONFLAG_LEFT, Localize("Select previous envelope.", "Editor envelope selector punctuation"), IGraphics::CORNER_L, 7.0f))
		{
			Map()->m_SelectedEnvelope--;
			if(Map()->m_SelectedEnvelope < 0)
				Map()->m_SelectedEnvelope = Map()->m_vpEnvelopes.size() - 1;
			CurrentEnvelopeSwitched = true;
		}

		if(Editor()->DoButton_QmIcon(&m_NextEnvelopeButtonId, EQmIcon::PLUS, FontIcons::FONT_ICON_PLUS, 0, &Inc, BUTTONFLAG_LEFT, Localize("Select next envelope.", "Editor envelope selector punctuation"), IGraphics::CORNER_R, 7.0f))
		{
			Map()->m_SelectedEnvelope++;
			if(Map()->m_SelectedEnvelope >= (int)Map()->m_vpEnvelopes.size())
				Map()->m_SelectedEnvelope = 0;
			CurrentEnvelopeSwitched = true;
		}

		if(pEnvelope)
		{
			ToolBar.VSplitLeft(15.0f, nullptr, &ToolBar);
			ToolBar.VSplitLeft(40.0f, &Button, &ToolBar);
			Ui()->DoLabel(&Button, Localize("Name:", "Editor file label"), 10.0f, TEXTALIGN_MR);

			ToolBar.VSplitLeft(3.0f, nullptr, &ToolBar);
			ToolBar.VSplitLeft(ToolBar.w > ToolBar.h * 40 ? 80.0f : 60.0f, &Button, &ToolBar);

			m_NameInput.SetBuffer(pEnvelope->m_aName, sizeof(pEnvelope->m_aName));
			if(Editor()->DoEditBox(&m_NameInput, &Button, 10.0f, IGraphics::CORNER_ALL, Localize("The name of the selected envelope.", "Editor")))
			{
				Map()->OnModify();
			}
		}
	}

	const bool ShowColorBar = pEnvelope && pEnvelope->GetChannels() == 4;
	if(ShowColorBar)
	{
		View.HSplitTop(20.0f, &ColorBar, &View);
		ColorBar.HMargin(2.0f, &ColorBar);
	}

	Editor()->RenderBackground(View, Editor()->m_CheckerTexture, 32.0f, 0.1f);

	if(pEnvelope)
	{
		if(State.m_ResetZoom)
		{
			State.m_ResetZoom = false;
			ResetZoomEnvelope(pEnvelope, State.m_ActiveChannels);
		}

		ColorRGBA aColors[] = {ColorRGBA(1, 0.2f, 0.2f), ColorRGBA(0.2f, 1, 0.2f), ColorRGBA(0.2f, 0.2f, 1), ColorRGBA(1, 1, 0.2f)};

		CUIRect Button;

		ToolBar.VSplitLeft(15.0f, &Button, &ToolBar);

		const char *aapNames[4][CEnvPoint::MAX_CHANNELS] = {
			{Localize("value", "Editor property value"), "", "", ""},
			{"", "", "", ""},
			{Localize("X", "Editor envelope channel"), Localize("Y", "Editor envelope channel"), Localize("R", "Editor envelope rotation channel"), ""},
			{Localize("R", "Editor envelope red channel"), Localize("G", "Editor"), Localize("B", "Editor envelope blue channel"), Localize("A", "Editor envelope alpha channel")},
		};

		const char *aapDescriptions[4][CEnvPoint::MAX_CHANNELS] = {
			{Localize("Volume of the envelope.", "Editor"), "", "", ""},
			{"", "", "", ""},
			{Localize("X-axis of the envelope.", "Editor"), Localize("Y-axis of the envelope.", "Editor"), Localize("Rotation of the envelope.", "Editor"), ""},
			{Localize("Red value of the envelope.", "Editor"), Localize("Green value of the envelope.", "Editor"), Localize("Blue value of the envelope.", "Editor"), Localize("Alpha value of the envelope.", "Editor")},
		};

		int Bit = 1;

		for(int i = 0; i < CEnvPoint::MAX_CHANNELS; i++, Bit <<= 1)
		{
			ToolBar.VSplitLeft(15.0f, &Button, &ToolBar);
			if(i < pEnvelope->GetChannels())
			{
				int Corners = IGraphics::CORNER_NONE;
				if(pEnvelope->GetChannels() == 1)
					Corners = IGraphics::CORNER_ALL;
				else if(i == 0)
					Corners = IGraphics::CORNER_L;
				else if(i == pEnvelope->GetChannels() - 1)
					Corners = IGraphics::CORNER_R;

				if(Editor()->DoButton_Env(&m_aChannelButtonIds[i], aapNames[pEnvelope->GetChannels() - 1][i], State.m_ActiveChannels & Bit, &Button, aapDescriptions[pEnvelope->GetChannels() - 1][i], aColors[i], Corners))
					State.m_ActiveChannels ^= Bit;
			}
		}

		ToolBar.VSplitLeft(15.0f, nullptr, &ToolBar);
		ToolBar.VSplitLeft(40.0f, &Button, &ToolBar);

		const bool ShouldPan = m_Operation == EEnvelopeEditorOp::OP_NONE && (Ui()->MouseButton(2) || (Ui()->MouseButton(0) && Input()->ModifierIsPressed()));
		if(Editor()->m_pContainerPanned == &m_EnvelopeEditorId)
		{
			if(!ShouldPan)
			{
				Editor()->m_pContainerPanned = nullptr;
			}
			else
			{
				State.m_Offset.x += Ui()->MouseDeltaX() / Graphics()->ScreenWidth() * Ui()->Screen()->w / View.w;
				State.m_Offset.y -= Ui()->MouseDeltaY() / Graphics()->ScreenHeight() * Ui()->Screen()->h / View.h;
			}
		}

		if(Ui()->MouseInside(&View) && Editor()->m_Dialog == DIALOG_NONE)
		{
			Ui()->SetHotItem(&m_EnvelopeEditorId);

			if(ShouldPan && Editor()->m_pContainerPanned == nullptr)
				Editor()->m_pContainerPanned = &m_EnvelopeEditorId;

			if(Input()->KeyPress(KEY_KP_MULTIPLY) && CLineInput::GetActiveInput() == nullptr)
				ResetZoomEnvelope(pEnvelope, State.m_ActiveChannels);
			if(Input()->ShiftIsPressed())
			{
				if(Input()->KeyPress(KEY_KP_MINUS) && CLineInput::GetActiveInput() == nullptr)
					State.m_ZoomY.ChangeValue(0.1f * State.m_ZoomY.GetValue());
				if(Input()->KeyPress(KEY_KP_PLUS) && CLineInput::GetActiveInput() == nullptr)
					State.m_ZoomY.ChangeValue(-0.1f * State.m_ZoomY.GetValue());
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					State.m_ZoomY.ChangeValue(0.1f * State.m_ZoomY.GetValue());
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					State.m_ZoomY.ChangeValue(-0.1f * State.m_ZoomY.GetValue());
			}
			else
			{
				if(Input()->KeyPress(KEY_KP_MINUS) && CLineInput::GetActiveInput() == nullptr)
					State.m_ZoomX.ChangeValue(0.1f * State.m_ZoomX.GetValue());
				if(Input()->KeyPress(KEY_KP_PLUS) && CLineInput::GetActiveInput() == nullptr)
					State.m_ZoomX.ChangeValue(-0.1f * State.m_ZoomX.GetValue());
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					State.m_ZoomX.ChangeValue(0.1f * State.m_ZoomX.GetValue());
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					State.m_ZoomX.ChangeValue(-0.1f * State.m_ZoomX.GetValue());
			}
		}

		if(Ui()->HotItem() == &m_EnvelopeEditorId)
		{
			// do stuff
			if(Ui()->MouseButton(0))
			{
				m_EnvelopeEditorButtonUsed = 0;
				if(m_Operation != EEnvelopeEditorOp::OP_BOX_SELECT && !Input()->ModifierIsPressed())
				{
					m_Operation = EEnvelopeEditorOp::OP_BOX_SELECT;
					m_MouseStart = Ui()->MousePos();
				}
			}
			else if(m_EnvelopeEditorButtonUsed == 0)
			{
				if(Ui()->DoDoubleClickLogic(&m_EnvelopeEditorId) && !Input()->ModifierIsPressed())
				{
					// add point
					float Time = ScreenToEnvelopeX(View, Ui()->MouseX());
					ColorRGBA Channels = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
					pEnvelope->Eval(std::clamp(Time, 0.0f, pEnvelope->EndTime()), Channels, 4);

					const CFixedTime FixedTime = CFixedTime::FromSeconds(Time);
					bool TimeFound = false;
					for(CEnvPoint &Point : pEnvelope->m_vPoints)
					{
						if(Point.m_Time == FixedTime)
							TimeFound = true;
					}

					if(!TimeFound)
						Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionAddEnvelopePoint>(Map(), Map()->m_SelectedEnvelope, FixedTime, Channels));

					if(FixedTime < CFixedTime(0))
						RemoveTimeOffsetEnvelope(pEnvelope);
					Map()->OnModify();
				}
				m_EnvelopeEditorButtonUsed = -1;
			}

			Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;
			str_copy(Editor()->m_aTooltip, Localize("Double click to create a new point. Use shift to change the zoom axis. Press S to scale selected envelope points.", "Editor"));
		}

		UpdateZoomEnvelopeX(View);
		UpdateZoomEnvelopeY(View);

		{
			float UnitsPerLineY = 0.001f;
			static const float s_aUnitPerLineOptionsY[] = {0.005f, 0.01f, 0.025f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 2 * 32.0f, 5 * 32.0f, 10 * 32.0f, 20 * 32.0f, 50 * 32.0f, 100 * 32.0f};
			for(float Value : s_aUnitPerLineOptionsY)
			{
				if(Value / State.m_ZoomY.GetValue() * View.h < 40.0f)
					UnitsPerLineY = Value;
			}
			int NumLinesY = State.m_ZoomY.GetValue() / UnitsPerLineY + 1;

			Ui()->ClipEnable(&View);
			Graphics()->TextureClear();
			Graphics()->LinesBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.2f);

			float BaseValue = static_cast<int>(State.m_Offset.y * State.m_ZoomY.GetValue() / UnitsPerLineY) * UnitsPerLineY;
			for(int i = 0; i <= NumLinesY; i++)
			{
				float Value = UnitsPerLineY * i - BaseValue;
				IGraphics::CLineItem LineItem(View.x, EnvelopeToScreenY(View, Value), View.x + View.w, EnvelopeToScreenY(View, Value));
				Graphics()->LinesDraw(&LineItem, 1);
			}

			Graphics()->LinesEnd();

			Ui()->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.4f);
			for(int i = 0; i <= NumLinesY; i++)
			{
				float Value = UnitsPerLineY * i - BaseValue;
				char aValueBuffer[16];
				if(UnitsPerLineY >= 1.0f)
				{
					str_format(aValueBuffer, sizeof(aValueBuffer), "%d", static_cast<int>(Value));
				}
				else
				{
					str_format(aValueBuffer, sizeof(aValueBuffer), "%.3f", Value);
				}
				Ui()->TextRender()->Text(View.x, EnvelopeToScreenY(View, Value) + 4.0f, 8.0f, aValueBuffer);
			}
			Ui()->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			Ui()->ClipDisable();
		}

		{
			using namespace std::chrono_literals;
			CTimeStep UnitsPerLineX = 1ms;
			static const CTimeStep s_aUnitPerLineOptionsX[] = {5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 2s, 5s, 10s, 15s, 30s, 1min};
			for(CTimeStep Value : s_aUnitPerLineOptionsX)
			{
				if(Value.AsSeconds() / State.m_ZoomX.GetValue() * View.w < 160.0f)
					UnitsPerLineX = Value;
			}
			int NumLinesX = State.m_ZoomX.GetValue() / UnitsPerLineX.AsSeconds() + 1;

			Ui()->ClipEnable(&View);
			Graphics()->TextureClear();
			Graphics()->LinesBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.2f);

			CTimeStep BaseValue = UnitsPerLineX * static_cast<int>(State.m_Offset.x * State.m_ZoomX.GetValue() / UnitsPerLineX.AsSeconds());
			for(int i = 0; i <= NumLinesX; i++)
			{
				float Value = UnitsPerLineX.AsSeconds() * i - BaseValue.AsSeconds();
				IGraphics::CLineItem LineItem(EnvelopeToScreenX(View, Value), View.y, EnvelopeToScreenX(View, Value), View.y + View.h);
				Graphics()->LinesDraw(&LineItem, 1);
			}

			Graphics()->LinesEnd();

			Ui()->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.4f);
			for(int i = 0; i <= NumLinesX; i++)
			{
				CTimeStep Value = UnitsPerLineX * i - BaseValue;
				if(Value.AsSeconds() >= 0)
				{
					char aValueBuffer[16];
					Value.Format(aValueBuffer, sizeof(aValueBuffer));

					Ui()->TextRender()->Text(EnvelopeToScreenX(View, Value.AsSeconds()) + 1.0f, View.y + View.h - 8.0f, 8.0f, aValueBuffer);
				}
			}
			Ui()->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			Ui()->ClipDisable();
		}

		// render lines
		{
			float EndX = View.x + View.w;
			float StartX = std::clamp(View.x + View.w * State.m_Offset.x, View.x, View.x + View.w);

			float EndTime = ScreenToEnvelopeX(View, EndX);
			float StartTime = ScreenToEnvelopeX(View, StartX);

			Ui()->ClipEnable(&View);
			Graphics()->TextureClear();
			IGraphics::CLineItemBatch LineItemBatch;
			for(int c = 0; c < pEnvelope->GetChannels(); c++)
			{
				Graphics()->LinesBatchBegin(&LineItemBatch);
				if(State.m_ActiveChannels & (1 << c))
					Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 1);
				else
					Graphics()->SetColor(aColors[c].r * 0.5f, aColors[c].g * 0.5f, aColors[c].b * 0.5f, 1);

				const int Steps = static_cast<int>(((EndX - StartX) / Ui()->Screen()->w) * Graphics()->ScreenWidth());
				const float StepTime = (EndTime - StartTime) / static_cast<float>(Steps);
				const float StepSize = (EndX - StartX) / static_cast<float>(Steps);

				ColorRGBA Channels = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
				pEnvelope->Eval(StartTime, Channels, c + 1);
				float PrevTime = StartTime;
				float PrevX = StartX;
				float PrevY = EnvelopeToScreenY(View, Channels[c]);
				for(int Step = 1; Step <= Steps; Step++)
				{
					float CurrentTime = StartTime + Step * StepTime;
					if(CurrentTime >= EndTime)
					{
						CurrentTime = EndTime - 0.001f;
						if(CurrentTime <= PrevTime)
							break;
					}

					Channels = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
					pEnvelope->Eval(CurrentTime, Channels, c + 1);
					const float CurrentX = StartX + Step * StepSize;
					const float CurrentY = EnvelopeToScreenY(View, Channels[c]);

					const IGraphics::CLineItem Item = IGraphics::CLineItem(PrevX, PrevY, CurrentX, CurrentY);
					Graphics()->LinesBatchDraw(&LineItemBatch, &Item, 1);

					PrevTime = CurrentTime;
					PrevX = CurrentX;
					PrevY = CurrentY;
				}
				Graphics()->LinesBatchEnd(&LineItemBatch);
			}
			Ui()->ClipDisable();
		}

		CUIRect InactiveRegionLeft{
			View.x,
			View.y,
			std::clamp(EnvelopeToScreenX(View, 0.0f) - View.x, 0.0f, View.w),
			View.h,
		};
		const float EndX = EnvelopeToScreenX(View, pEnvelope->EndTime());
		CUIRect InactiveRegionRight{
			std::max(View.x, EndX),
			View.y,
			std::clamp(View.x + View.w - EndX, 0.0f, View.w),
			View.h,
		};
		InactiveRegionLeft.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_NONE, 0.0f);
		InactiveRegionRight.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_NONE, 0.0f);

		// render tangents for bezier curves
		{
			Ui()->ClipEnable(&View);
			Graphics()->TextureClear();
			Graphics()->LinesBegin();
			for(int c = 0; c < pEnvelope->GetChannels(); c++)
			{
				if(!(State.m_ActiveChannels & (1 << c)))
					continue;

				for(int i = 0; i < (int)pEnvelope->m_vPoints.size(); i++)
				{
					float PosX = EnvelopeToScreenX(View, pEnvelope->m_vPoints[i].m_Time.AsSeconds());
					float PosY = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c]));

					// Out-Tangent
					if(i < (int)pEnvelope->m_vPoints.size() - 1 && pEnvelope->m_vPoints[i].m_Curvetype == CURVETYPE_BEZIER)
					{
						float TangentX = EnvelopeToScreenX(View, (pEnvelope->m_vPoints[i].m_Time + pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c]).AsSeconds());
						float TangentY = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c] + pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaY[c]));

						if(Map()->IsTangentOutPointSelected(i, c))
							Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
						else
							Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 0.4f);

						IGraphics::CLineItem LineItem(TangentX, TangentY, PosX, PosY);
						Graphics()->LinesDraw(&LineItem, 1);
					}

					// In-Tangent
					if(i > 0 && pEnvelope->m_vPoints[i - 1].m_Curvetype == CURVETYPE_BEZIER)
					{
						float TangentX = EnvelopeToScreenX(View, (pEnvelope->m_vPoints[i].m_Time + pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c]).AsSeconds());
						float TangentY = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c] + pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaY[c]));

						if(Map()->IsTangentInPointSelected(i, c))
							Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
						else
							Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 0.4f);

						IGraphics::CLineItem LineItem(TangentX, TangentY, PosX, PosY);
						Graphics()->LinesDraw(&LineItem, 1);
					}
				}
			}
			Graphics()->LinesEnd();
			Ui()->ClipDisable();
		}

		// render curve options
		{
			for(int i = 0; i < (int)pEnvelope->m_vPoints.size() - 1; i++)
			{
				float t0 = pEnvelope->m_vPoints[i].m_Time.AsSeconds();
				float t1 = pEnvelope->m_vPoints[i + 1].m_Time.AsSeconds();

				CUIRect CurveButton;
				CurveButton.x = EnvelopeToScreenX(View, t0 + (t1 - t0) * 0.5f);
				CurveButton.y = CurveBar.y;
				CurveButton.h = CurveBar.h;
				CurveButton.w = CurveBar.h;
				CurveButton.x -= CurveButton.w / 2.0f;
				const void *pId = &pEnvelope->m_vPoints[i].m_Curvetype;

				if(CurveButton.x >= View.x)
				{
					const int ButtonResult = Editor()->DoButton_Editor(pId, CurveTypeNameShort(pEnvelope->m_vPoints[i].m_Curvetype), 0, &CurveButton, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT, Localize("Switch curve type (N = step, L = linear, S = slow, F = fast, M = smooth, B = bezier).", "Editor"));
					if(ButtonResult == 1)
					{
						const int PrevCurve = pEnvelope->m_vPoints[i].m_Curvetype;
						const int Direction = Input()->ShiftIsPressed() ? -1 : 1;
						pEnvelope->m_vPoints[i].m_Curvetype = (pEnvelope->m_vPoints[i].m_Curvetype + Direction + NUM_CURVETYPES) % NUM_CURVETYPES;

						Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionEnvelopeEditPoint>(Map(),
							Map()->m_SelectedEnvelope, i, 0, CEditorActionEnvelopeEditPoint::EEditType::CURVE_TYPE, PrevCurve, pEnvelope->m_vPoints[i].m_Curvetype));
						Map()->OnModify();
					}
					else if(ButtonResult == 2)
					{
						Editor()->m_PopupEnvelopeSelectedPoint = i;
						Ui()->DoPopupMenu(&m_PopupCurvetypeId, Ui()->MouseX(), Ui()->MouseY(), 80, (float)NUM_CURVETYPES * 14.0f + 10.0f, Editor(), CEditor::PopupEnvelopeCurvetype);
					}
				}
			}
		}

		// render colorbar
		if(ShowColorBar)
		{
			RenderColorBar(ColorBar, pEnvelope);
		}

		// 处理时间条拖动。
		{
			if(m_Operation == EEnvelopeEditorOp::OP_NONE)
			{
				UpdateHotEnvelopeObject(View, pEnvelope.get(), State.m_ActiveChannels);
			}

			ColorRGBA BarColor;
			if(Ui()->CheckActiveItem(&Map()->m_EnvelopeEvaluator.m_AnimateTime))
			{
				if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
				{
					if(length_squared(m_MouseStart - Ui()->MousePos()) > 20.0f)
						m_Operation = EEnvelopeEditorOp::OP_DRAG_TIME_BAR;
				}

				if(m_Operation == EEnvelopeEditorOp::OP_DRAG_TIME_BAR)
				{
					if(Input()->ModifierIsPressed())
					{
						Ui()->SetMouseSlow(true);
					}

					const float DeltaX = ScreenToEnvelopeDX(View, Ui()->MouseDeltaX());
					Map()->m_EnvelopeEvaluator.m_AnimateTime += DeltaX / Map()->m_EnvelopeEvaluator.m_AnimateSpeed;
					Map()->m_EnvelopeEvaluator.m_AnimateTime = std::max(Map()->m_EnvelopeEvaluator.m_AnimateTime, 0.0f);
				}

				if(!Ui()->MouseButton(0))
				{
					Ui()->SetActiveItem(nullptr);
					m_Operation = EEnvelopeEditorOp::OP_NONE;
				}

				Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;
				BarColor = ColorRGBA(1.0f, 1.0f, 0.0f, 0.8f);
				str_copy(Editor()->m_aTooltip, Localize("Timebar. Press left-click to drag. Hold ctrl to be more precise.", "Editor"));
			}
			else if(Ui()->HotItem() == &Map()->m_EnvelopeEvaluator.m_AnimateTime)
			{
				if(Ui()->MouseButton(0))
				{
					Ui()->SetActiveItem(&Map()->m_EnvelopeEvaluator.m_AnimateTime);
					m_Operation = EEnvelopeEditorOp::OP_SELECT;

					m_MouseStart = Ui()->MousePos();
				}

				Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;
				BarColor = ColorRGBA(1.0f, 1.0f, 0.0f, 0.8f);
				str_copy(Editor()->m_aTooltip, Localize("Timebar. Press left-click to drag. Hold ctrl to be more precise.", "Editor"));
			}
			else
			{
				BarColor = ColorRGBA(1.0f, 1.0f, 0.0f, 0.5f);
			}

			Ui()->ClipEnable(&View);
			const float Time = Map()->m_EnvelopeEvaluator.m_AnimateTime * Map()->m_EnvelopeEvaluator.m_AnimateSpeed;
			const float BarWidth = 1.5f;
			CUIRect TimeBar{
				EnvelopeToScreenX(View, Time) - BarWidth / 2.0f,
				View.y,
				BarWidth,
				View.h,
			};
			TimeBar.Draw(BarColor, IGraphics::CORNER_NONE, 0.0f);

			const float EndTime = pEnvelope->EndTime();
			if(EndTime > 0.0f && Time > EndTime)
			{
				const float LoopedTime = std::fmod(Time, EndTime);
				TimeBar.x = EnvelopeToScreenX(View, LoopedTime) - BarWidth / 2.0f;
				TimeBar.Draw(BarColor, IGraphics::CORNER_NONE, 0.0f);
			}
			Ui()->ClipDisable();
		}

		// render handles
		if(CurrentEnvelopeSwitched)
		{
			Map()->DeselectEnvPoints();
			State.m_ResetZoom = true;
		}

		{
			const auto &&ShowPopupEnvPoint = [&]() {
				Ui()->DoPopupMenu(&m_PopupEnvPointId, Ui()->MouseX(), Ui()->MouseY(), 150, 56 + (pEnvelope->GetChannels() == 4 && !Map()->IsTangentSelected() ? 16.0f : 0.0f), Editor(), CEditor::PopupEnvPoint);
			};

			if(m_Operation == EEnvelopeEditorOp::OP_NONE)
			{
				UpdateHotEnvelopeObject(View, pEnvelope.get(), State.m_ActiveChannels);
				if(!Ui()->MouseButton(0))
					Map()->m_EnvOpTracker.Stop(false);
			}
			else
			{
				Map()->m_EnvOpTracker.Begin(m_Operation);
			}

			Ui()->ClipEnable(&View);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			for(int c = 0; c < pEnvelope->GetChannels(); c++)
			{
				if(!(State.m_ActiveChannels & (1 << c)))
					continue;

				for(int i = 0; i < (int)pEnvelope->m_vPoints.size(); i++)
				{
					// point handle
					{
						CUIRect Final;
						Final.x = EnvelopeToScreenX(View, pEnvelope->m_vPoints[i].m_Time.AsSeconds());
						Final.y = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c]));
						Final.x -= 2.0f;
						Final.y -= 2.0f;
						Final.w = 4.0f;
						Final.h = 4.0f;

						const void *pId = &pEnvelope->m_vPoints[i].m_aValues[c];

						if(Map()->IsEnvPointSelected(i, c))
						{
							Graphics()->SetColor(1, 1, 1, 1);
							CUIRect Background = {
								Final.x - 0.2f * Final.w,
								Final.y - 0.2f * Final.h,
								Final.w * 1.4f,
								Final.h * 1.4f};
							IGraphics::CQuadItem QuadItem(Background.x, Background.y, Background.w, Background.h);
							Graphics()->QuadsDrawTL(&QuadItem, 1);
						}

						if(Ui()->CheckActiveItem(pId))
						{
							Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;

							if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
							{
								if(length_squared(m_MouseStart - Ui()->MousePos()) > 20.0f)
								{
									m_Operation = EEnvelopeEditorOp::OP_DRAG_POINT;

									if(!Map()->IsEnvPointSelected(i, c))
										Map()->SelectEnvPoint(i, c);
								}
							}

							if(m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT || m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT_X || m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT_Y)
							{
								if(Input()->ModifierIsPressed())
								{
									Ui()->SetMouseSlow(true);
								}

								if(Input()->ShiftIsPressed())
								{
									if(m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT || m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT_Y)
									{
										m_Operation = EEnvelopeEditorOp::OP_DRAG_POINT_X;
										m_vAccurateDragValuesX.clear();
										for(auto [SelectedIndex, _] : Map()->m_vSelectedEnvelopePoints)
											m_vAccurateDragValuesX.push_back(pEnvelope->m_vPoints[SelectedIndex].m_Time.GetInternal());
									}
									else
									{
										float DeltaX = ScreenToEnvelopeDX(View, Ui()->MouseDeltaX()) * 1000.0f;

										for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
										{
											int SelectedIndex = Map()->m_vSelectedEnvelopePoints[k].first;
											CFixedTime BoundLow = CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x));
											CFixedTime BoundHigh = CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x + View.w));
											for(int j = 0; j < SelectedIndex; j++)
											{
												if(!Map()->IsEnvPointSelected(j))
													BoundLow = std::max(pEnvelope->m_vPoints[j].m_Time + CFixedTime(1), BoundLow);
											}
											for(int j = SelectedIndex + 1; j < (int)pEnvelope->m_vPoints.size(); j++)
											{
												if(!Map()->IsEnvPointSelected(j))
													BoundHigh = std::min(pEnvelope->m_vPoints[j].m_Time - CFixedTime(1), BoundHigh);
											}

											DeltaX = ClampDelta(m_vAccurateDragValuesX[k], DeltaX, BoundLow.GetInternal(), BoundHigh.GetInternal());
										}
										for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
										{
											int SelectedIndex = Map()->m_vSelectedEnvelopePoints[k].first;
											m_vAccurateDragValuesX[k] += DeltaX;
											pEnvelope->m_vPoints[SelectedIndex].m_Time = CFixedTime(std::round(m_vAccurateDragValuesX[k]));
										}
										for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
										{
											int SelectedIndex = Map()->m_vSelectedEnvelopePoints[k].first;
											if(SelectedIndex == 0 && pEnvelope->m_vPoints[SelectedIndex].m_Time != CFixedTime(0))
											{
												RemoveTimeOffsetEnvelope(pEnvelope);
												float Offset = m_vAccurateDragValuesX[k];
												for(auto &Value : m_vAccurateDragValuesX)
													Value -= Offset;
												break;
											}
										}
									}
								}
								else
								{
									if(m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT || m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT_X)
									{
										m_Operation = EEnvelopeEditorOp::OP_DRAG_POINT_Y;
										m_vAccurateDragValuesY.clear();
										for(auto [SelectedIndex, SelectedChannel] : Map()->m_vSelectedEnvelopePoints)
											m_vAccurateDragValuesY.push_back(pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel]);
									}
									else
									{
										float DeltaY = ScreenToEnvelopeDY(View, Ui()->MouseDeltaY()) * 1024.0f;
										for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
										{
											auto [SelectedIndex, SelectedChannel] = Map()->m_vSelectedEnvelopePoints[k];
											m_vAccurateDragValuesY[k] -= DeltaY;
											pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel] = std::round(m_vAccurateDragValuesY[k]);

											if(pEnvelope->GetChannels() == 1 || pEnvelope->GetChannels() == 4)
											{
												pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel] = std::clamp(pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel], 0, 1024);
												m_vAccurateDragValuesY[k] = std::clamp<float>(m_vAccurateDragValuesY[k], 0, 1024);
											}
										}
									}
								}
							}

							if(m_Operation == EEnvelopeEditorOp::OP_CONTEXT_MENU)
							{
								if(!Ui()->MouseButton(1))
								{
									if(Map()->m_vSelectedEnvelopePoints.size() == 1)
									{
										Map()->m_UpdateEnvPointInfo = true;
										ShowPopupEnvPoint();
									}
									else if(Map()->m_vSelectedEnvelopePoints.size() > 1)
									{
										Ui()->DoPopupMenu(&m_PopupEnvPointMultiId, Ui()->MouseX(), Ui()->MouseY(), 100, 22, Editor(), CEditor::PopupEnvPointMulti);
									}
									Ui()->SetActiveItem(nullptr);
									m_Operation = EEnvelopeEditorOp::OP_NONE;
								}
							}
							else if(!Ui()->MouseButton(0))
							{
								Ui()->SetActiveItem(nullptr);
								Map()->m_SelectedQuadEnvelope = -1;

								if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
								{
									if(Input()->ShiftIsPressed())
										Map()->ToggleEnvPoint(i, c);
									else
										Map()->SelectEnvPoint(i, c);
								}

								m_Operation = EEnvelopeEditorOp::OP_NONE;
								Map()->OnModify();
							}

							Graphics()->SetColor(1, 1, 1, 1);
						}
						else if(Ui()->HotItem() == pId)
						{
							if(Ui()->MouseButton(0))
							{
								Ui()->SetActiveItem(pId);
								m_Operation = EEnvelopeEditorOp::OP_SELECT;
								Map()->m_SelectedQuadEnvelope = Map()->m_SelectedEnvelope;

								m_MouseStart = Ui()->MousePos();
							}
							else if(Ui()->MouseButtonClicked(1))
							{
								if(Input()->ShiftIsPressed())
								{
									Map()->m_EnvelopeEditorHistory.Execute(std::make_shared<CEditorActionDeleteEnvelopePoint>(Map(), Map()->m_SelectedEnvelope, i));
								}
								else
								{
									m_Operation = EEnvelopeEditorOp::OP_CONTEXT_MENU;
									if(!Map()->IsEnvPointSelected(i, c))
										Map()->SelectEnvPoint(i, c);
									Ui()->SetActiveItem(pId);
								}
							}

							Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;
							Graphics()->SetColor(1, 1, 1, 1);
							str_copy(Editor()->m_aTooltip, Localize("Envelope point. Left mouse to drag. Hold ctrl to be more precise. Hold shift to alter time. Shift+right click to delete.", "Editor"));
							Editor()->m_pUiGotContext = pId;
						}
						else
						{
							Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 1.0f);
						}

						IGraphics::CQuadItem QuadItem(Final.x, Final.y, Final.w, Final.h);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
					}

					// tangent handles for bezier curves
					if(i >= 0 && i < (int)pEnvelope->m_vPoints.size())
					{
						// Out-Tangent handle
						if(i < (int)pEnvelope->m_vPoints.size() - 1 && pEnvelope->m_vPoints[i].m_Curvetype == CURVETYPE_BEZIER)
						{
							CUIRect Final;
							Final.x = EnvelopeToScreenX(View, (pEnvelope->m_vPoints[i].m_Time + pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c]).AsSeconds());
							Final.y = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c] + pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaY[c]));
							Final.x -= 2.0f;
							Final.y -= 2.0f;
							Final.w = 4.0f;
							Final.h = 4.0f;

							// handle logic
							const void *pId = &pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c];

							if(Map()->IsTangentOutPointSelected(i, c))
							{
								Graphics()->SetColor(1, 1, 1, 1);
								IGraphics::CFreeformItem FreeformItem(
									Final.x + Final.w / 2.0f,
									Final.y - 1,
									Final.x + Final.w / 2.0f,
									Final.y - 1,
									Final.x + Final.w + 1,
									Final.y + Final.h + 1,
									Final.x - 1,
									Final.y + Final.h + 1);
								Graphics()->QuadsDrawFreeform(&FreeformItem, 1);
							}

							if(Ui()->CheckActiveItem(pId))
							{
								Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;

								if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
								{
									if(length_squared(m_MouseStart - Ui()->MousePos()) > 20.0f)
									{
										m_Operation = EEnvelopeEditorOp::OP_DRAG_POINT;

										m_vAccurateDragValuesX = {static_cast<float>(pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c].GetInternal())};
										m_vAccurateDragValuesY = {static_cast<float>(pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaY[c])};

										if(!Map()->IsTangentOutPointSelected(i, c))
											Map()->SelectTangentOutPoint(i, c);
									}
								}

								if(m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT)
								{
									if(Input()->ModifierIsPressed())
									{
										Ui()->SetMouseSlow(true);
									}

									float DeltaX = ScreenToEnvelopeDX(View, Ui()->MouseDeltaX()) * 1000.0f;
									float DeltaY = ScreenToEnvelopeDY(View, Ui()->MouseDeltaY()) * 1024.0f;
									m_vAccurateDragValuesX[0] += DeltaX;
									m_vAccurateDragValuesY[0] -= DeltaY;

									pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c] = CFixedTime(std::round(m_vAccurateDragValuesX[0]));
									pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaY[c] = std::round(m_vAccurateDragValuesY[0]);

									// clamp time value
									pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c] = std::clamp(pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c], CFixedTime(0), CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x + View.w)) - pEnvelope->m_vPoints[i].m_Time);
									m_vAccurateDragValuesX[0] = std::clamp<float>(m_vAccurateDragValuesX[0], 0, (CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x + View.w)) - pEnvelope->m_vPoints[i].m_Time).GetInternal());
								}

								if(m_Operation == EEnvelopeEditorOp::OP_CONTEXT_MENU)
								{
									if(!Ui()->MouseButton(1))
									{
										if(Map()->IsTangentOutPointSelected(i, c))
										{
											Map()->m_UpdateEnvPointInfo = true;
											ShowPopupEnvPoint();
										}
										Ui()->SetActiveItem(nullptr);
										m_Operation = EEnvelopeEditorOp::OP_NONE;
									}
								}
								else if(!Ui()->MouseButton(0))
								{
									Ui()->SetActiveItem(nullptr);
									Map()->m_SelectedQuadEnvelope = -1;

									if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
										Map()->SelectTangentOutPoint(i, c);

									m_Operation = EEnvelopeEditorOp::OP_NONE;
									Map()->OnModify();
								}

								Graphics()->SetColor(1, 1, 1, 1);
							}
							else if(Ui()->HotItem() == pId)
							{
								if(Ui()->MouseButton(0))
								{
									Ui()->SetActiveItem(pId);
									m_Operation = EEnvelopeEditorOp::OP_SELECT;
									Map()->m_SelectedQuadEnvelope = Map()->m_SelectedEnvelope;

									m_MouseStart = Ui()->MousePos();
								}
								else if(Ui()->MouseButtonClicked(1))
								{
									if(Input()->ShiftIsPressed())
									{
										Map()->SelectTangentOutPoint(i, c);
										pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaX[c] = CFixedTime(0);
										pEnvelope->m_vPoints[i].m_Bezier.m_aOutTangentDeltaY[c] = 0.0f;
										Map()->OnModify();
									}
									else
									{
										m_Operation = EEnvelopeEditorOp::OP_CONTEXT_MENU;
										Map()->SelectTangentOutPoint(i, c);
										Ui()->SetActiveItem(pId);
									}
								}

								Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;
								Graphics()->SetColor(1, 1, 1, 1);
								str_copy(Editor()->m_aTooltip, Localize("Bezier out-tangent. Left mouse to drag. Hold ctrl to be more precise. Shift+right click to reset.", "Editor"));
								Editor()->m_pUiGotContext = pId;
							}
							else
							{
								Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 1.0f);
							}

							// draw triangle
							IGraphics::CFreeformItem FreeformItem(Final.x + Final.w / 2.0f, Final.y, Final.x + Final.w / 2.0f, Final.y, Final.x + Final.w, Final.y + Final.h, Final.x, Final.y + Final.h);
							Graphics()->QuadsDrawFreeform(&FreeformItem, 1);
						}

						// In-Tangent handle
						if(i > 0 && pEnvelope->m_vPoints[i - 1].m_Curvetype == CURVETYPE_BEZIER)
						{
							CUIRect Final;
							Final.x = EnvelopeToScreenX(View, (pEnvelope->m_vPoints[i].m_Time + pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c]).AsSeconds());
							Final.y = EnvelopeToScreenY(View, fx2f(pEnvelope->m_vPoints[i].m_aValues[c] + pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaY[c]));
							Final.x -= 2.0f;
							Final.y -= 2.0f;
							Final.w = 4.0f;
							Final.h = 4.0f;

							// handle logic
							const void *pId = &pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c];

							if(Map()->IsTangentInPointSelected(i, c))
							{
								Graphics()->SetColor(1, 1, 1, 1);
								IGraphics::CFreeformItem FreeformItem(
									Final.x + Final.w / 2.0f,
									Final.y - 1,
									Final.x + Final.w / 2.0f,
									Final.y - 1,
									Final.x + Final.w + 1,
									Final.y + Final.h + 1,
									Final.x - 1,
									Final.y + Final.h + 1);
								Graphics()->QuadsDrawFreeform(&FreeformItem, 1);
							}

							if(Ui()->CheckActiveItem(pId))
							{
								Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;

								if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
								{
									if(length_squared(m_MouseStart - Ui()->MousePos()) > 20.0f)
									{
										m_Operation = EEnvelopeEditorOp::OP_DRAG_POINT;

										m_vAccurateDragValuesX = {static_cast<float>(pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c].GetInternal())};
										m_vAccurateDragValuesY = {static_cast<float>(pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaY[c])};

										if(!Map()->IsTangentInPointSelected(i, c))
											Map()->SelectTangentInPoint(i, c);
									}
								}

								if(m_Operation == EEnvelopeEditorOp::OP_DRAG_POINT)
								{
									if(Input()->ModifierIsPressed())
									{
										Ui()->SetMouseSlow(true);
									}

									float DeltaX = ScreenToEnvelopeDX(View, Ui()->MouseDeltaX()) * 1000.0f;
									float DeltaY = ScreenToEnvelopeDY(View, Ui()->MouseDeltaY()) * 1024.0f;
									m_vAccurateDragValuesX[0] += DeltaX;
									m_vAccurateDragValuesY[0] -= DeltaY;

									pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c] = CFixedTime(std::round(m_vAccurateDragValuesX[0]));
									pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaY[c] = std::round(m_vAccurateDragValuesY[0]);

									// clamp time value
									pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c] = std::clamp(pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c], CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x)) - pEnvelope->m_vPoints[i].m_Time, CFixedTime(0));
									m_vAccurateDragValuesX[0] = std::clamp<float>(m_vAccurateDragValuesX[0], (CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x)) - pEnvelope->m_vPoints[i].m_Time).GetInternal(), 0);
								}

								if(m_Operation == EEnvelopeEditorOp::OP_CONTEXT_MENU)
								{
									if(!Ui()->MouseButton(1))
									{
										if(Map()->IsTangentInPointSelected(i, c))
										{
											Map()->m_UpdateEnvPointInfo = true;
											ShowPopupEnvPoint();
										}
										Ui()->SetActiveItem(nullptr);
										m_Operation = EEnvelopeEditorOp::OP_NONE;
									}
								}
								else if(!Ui()->MouseButton(0))
								{
									Ui()->SetActiveItem(nullptr);
									Map()->m_SelectedQuadEnvelope = -1;

									if(m_Operation == EEnvelopeEditorOp::OP_SELECT)
										Map()->SelectTangentInPoint(i, c);

									m_Operation = EEnvelopeEditorOp::OP_NONE;
									Map()->OnModify();
								}

								Graphics()->SetColor(1, 1, 1, 1);
							}
							else if(Ui()->HotItem() == pId)
							{
								if(Ui()->MouseButton(0))
								{
									Ui()->SetActiveItem(pId);
									m_Operation = EEnvelopeEditorOp::OP_SELECT;
									Map()->m_SelectedQuadEnvelope = Map()->m_SelectedEnvelope;

									m_MouseStart = Ui()->MousePos();
								}
								else if(Ui()->MouseButtonClicked(1))
								{
									if(Input()->ShiftIsPressed())
									{
										Map()->SelectTangentInPoint(i, c);
										pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaX[c] = CFixedTime(0);
										pEnvelope->m_vPoints[i].m_Bezier.m_aInTangentDeltaY[c] = 0.0f;
										Map()->OnModify();
									}
									else
									{
										m_Operation = EEnvelopeEditorOp::OP_CONTEXT_MENU;
										Map()->SelectTangentInPoint(i, c);
										Ui()->SetActiveItem(pId);
									}
								}

								Editor()->m_ActiveEnvelopePreview = CEditor::EEnvelopePreview::SELECTED;
								Graphics()->SetColor(1, 1, 1, 1);
								str_copy(Editor()->m_aTooltip, Localize("Bezier in-tangent. Left mouse to drag. Hold ctrl to be more precise. Shift+right click to reset.", "Editor"));
								Editor()->m_pUiGotContext = pId;
							}
							else
							{
								Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 1.0f);
							}

							// draw triangle
							IGraphics::CFreeformItem FreeformItem(Final.x + Final.w / 2.0f, Final.y, Final.x + Final.w / 2.0f, Final.y, Final.x + Final.w, Final.y + Final.h, Final.x, Final.y + Final.h);
							Graphics()->QuadsDrawFreeform(&FreeformItem, 1);
						}
					}
				}
			}
			Graphics()->QuadsEnd();
			Ui()->ClipDisable();
		}

		// handle scaling
		if(m_Operation == EEnvelopeEditorOp::OP_NONE &&
			Editor()->m_Dialog == DIALOG_NONE &&
			!Ui()->IsPopupOpen() &&
			CLineInput::GetActiveInput() == nullptr &&
			Input()->KeyIsPressed(KEY_S) &&
			!Input()->ModifierIsPressed() &&
			!Map()->m_vSelectedEnvelopePoints.empty())
		{
			m_Operation = EEnvelopeEditorOp::OP_SCALE;
			m_ScaleFactor.x = 1.0f;
			m_ScaleFactor.y = 1.0f;
			auto [FirstPointIndex, FirstPointChannel] = Map()->m_vSelectedEnvelopePoints.front();

			float MaximumX = pEnvelope->m_vPoints[FirstPointIndex].m_Time.GetInternal();
			float MinimumX = MaximumX;
			m_vInitialPositionsX.clear();
			for(auto [SelectedIndex, _] : Map()->m_vSelectedEnvelopePoints)
			{
				float Value = pEnvelope->m_vPoints[SelectedIndex].m_Time.GetInternal();
				m_vInitialPositionsX.push_back(Value);
				MaximumX = maximum(MaximumX, Value);
				MinimumX = minimum(MinimumX, Value);
			}
			m_Midpoint.x = (MaximumX - MinimumX) / 2.0f + MinimumX;

			float MaximumY = pEnvelope->m_vPoints[FirstPointIndex].m_aValues[FirstPointChannel];
			float MinimumY = MaximumY;
			m_vInitialPositionsY.clear();
			for(auto [SelectedIndex, SelectedChannel] : Map()->m_vSelectedEnvelopePoints)
			{
				float Value = pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel];
				m_vInitialPositionsY.push_back(Value);
				MaximumY = maximum(MaximumY, Value);
				MinimumY = minimum(MinimumY, Value);
			}
			m_Midpoint.y = (MaximumY - MinimumY) / 2.0f + MinimumY;
		}

		if(m_Operation == EEnvelopeEditorOp::OP_SCALE)
		{
			str_copy(Editor()->m_aTooltip, Localize("Press shift to scale the time. Press alt to scale along midpoint. Press ctrl to be more precise.", "Editor"));

			if(Input()->ModifierIsPressed())
			{
				Ui()->SetMouseSlow(true);
			}

			if(Input()->ShiftIsPressed())
			{
				m_ScaleFactor.x += Ui()->MouseDeltaX() / Graphics()->ScreenWidth() * 10.0f;
				float Midpoint = Input()->AltIsPressed() ? m_Midpoint.x : 0.0f;
				for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
				{
					int SelectedIndex = Map()->m_vSelectedEnvelopePoints[k].first;
					CFixedTime BoundLow = CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x));
					CFixedTime BoundHigh = CFixedTime::FromSeconds(ScreenToEnvelopeX(View, View.x + View.w));
					for(int j = 0; j < SelectedIndex; j++)
					{
						if(!Map()->IsEnvPointSelected(j))
							BoundLow = std::max(pEnvelope->m_vPoints[j].m_Time + CFixedTime(1), BoundLow);
					}
					for(int j = SelectedIndex + 1; j < (int)pEnvelope->m_vPoints.size(); j++)
					{
						if(!Map()->IsEnvPointSelected(j))
							BoundHigh = std::min(pEnvelope->m_vPoints[j].m_Time - CFixedTime(1), BoundHigh);
					}

					float Value = m_vInitialPositionsX[k];
					float ScaleBoundLow = (BoundLow.GetInternal() - Midpoint) / (Value - Midpoint);
					float ScaleBoundHigh = (BoundHigh.GetInternal() - Midpoint) / (Value - Midpoint);
					float ScaleBoundMin = minimum(ScaleBoundLow, ScaleBoundHigh);
					float ScaleBoundMax = maximum(ScaleBoundLow, ScaleBoundHigh);
					m_ScaleFactor.x = std::clamp(m_ScaleFactor.x, ScaleBoundMin, ScaleBoundMax);
				}

				for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
				{
					int SelectedIndex = Map()->m_vSelectedEnvelopePoints[k].first;
					float ScaleMinimum = m_vInitialPositionsX[k] - Midpoint > CFixedTime(1).AsSeconds() ? CFixedTime(1).AsSeconds() / (m_vInitialPositionsX[k] - Midpoint) : 0.0f;
					float ScaleFactor = maximum(ScaleMinimum, m_ScaleFactor.x);
					pEnvelope->m_vPoints[SelectedIndex].m_Time = CFixedTime(std::round((m_vInitialPositionsX[k] - Midpoint) * ScaleFactor + Midpoint));
				}
				for(size_t k = 1; k < pEnvelope->m_vPoints.size(); k++)
				{
					if(pEnvelope->m_vPoints[k].m_Time <= pEnvelope->m_vPoints[k - 1].m_Time)
						pEnvelope->m_vPoints[k].m_Time = pEnvelope->m_vPoints[k - 1].m_Time + CFixedTime(1);
				}
				for(auto [SelectedIndex, _] : Map()->m_vSelectedEnvelopePoints)
				{
					if(SelectedIndex == 0 && pEnvelope->m_vPoints[SelectedIndex].m_Time != CFixedTime(0))
					{
						float Offset = pEnvelope->m_vPoints[0].m_Time.GetInternal();
						RemoveTimeOffsetEnvelope(pEnvelope);
						m_Midpoint.x -= Offset;
						for(auto &Value : m_vInitialPositionsX)
							Value -= Offset;
						break;
					}
				}
			}
			else
			{
				m_ScaleFactor.y -= Ui()->MouseDeltaY() / Graphics()->ScreenHeight() * 10.0f;
				for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
				{
					auto [SelectedIndex, SelectedChannel] = Map()->m_vSelectedEnvelopePoints[k];
					if(Input()->AltIsPressed())
						pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel] = std::round((m_vInitialPositionsY[k] - m_Midpoint.y) * m_ScaleFactor.y + m_Midpoint.y);
					else
						pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel] = std::round(m_vInitialPositionsY[k] * m_ScaleFactor.y);

					if(pEnvelope->GetChannels() == 1 || pEnvelope->GetChannels() == 4)
						pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel] = std::clamp(pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel], 0, 1024);
				}
			}

			if(Ui()->MouseButton(0))
			{
				m_Operation = EEnvelopeEditorOp::OP_NONE;
				Map()->m_EnvOpTracker.Stop(false);
			}
			else if(Ui()->MouseButton(1) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
				{
					int SelectedIndex = Map()->m_vSelectedEnvelopePoints[k].first;
					pEnvelope->m_vPoints[SelectedIndex].m_Time = CFixedTime(std::round(m_vInitialPositionsX[k]));
				}
				for(size_t k = 0; k < Map()->m_vSelectedEnvelopePoints.size(); k++)
				{
					auto [SelectedIndex, SelectedChannel] = Map()->m_vSelectedEnvelopePoints[k];
					pEnvelope->m_vPoints[SelectedIndex].m_aValues[SelectedChannel] = std::round(m_vInitialPositionsY[k]);
				}
				RemoveTimeOffsetEnvelope(pEnvelope);
				m_Operation = EEnvelopeEditorOp::OP_NONE;
			}
		}

		// handle box selection
		if(m_Operation == EEnvelopeEditorOp::OP_BOX_SELECT)
		{
			Ui()->ClipEnable(&View);
			CUIRect SelectionRect;
			SelectionRect.x = m_MouseStart.x;
			SelectionRect.y = m_MouseStart.y;
			SelectionRect.w = Ui()->MouseX() - m_MouseStart.x;
			SelectionRect.h = Ui()->MouseY() - m_MouseStart.y;
			SelectionRect.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			Ui()->ClipDisable();

			if(!Ui()->MouseButton(0))
			{
				m_Operation = EEnvelopeEditorOp::OP_NONE;
				Ui()->SetActiveItem(nullptr);

				float TimeStart = ScreenToEnvelopeX(View, m_MouseStart.x);
				float TimeEnd = ScreenToEnvelopeX(View, Ui()->MouseX());
				float ValueStart = ScreenToEnvelopeY(View, m_MouseStart.y);
				float ValueEnd = ScreenToEnvelopeY(View, Ui()->MouseY());

				float TimeMin = minimum(TimeStart, TimeEnd);
				float TimeMax = maximum(TimeStart, TimeEnd);
				float ValueMin = minimum(ValueStart, ValueEnd);
				float ValueMax = maximum(ValueStart, ValueEnd);

				if(!Input()->ShiftIsPressed())
					Map()->DeselectEnvPoints();

				for(int i = 0; i < (int)pEnvelope->m_vPoints.size(); i++)
				{
					for(int c = 0; c < pEnvelope->GetChannels(); c++)
					{
						if(!(State.m_ActiveChannels & (1 << c)))
							continue;

						float Time = pEnvelope->m_vPoints[i].m_Time.AsSeconds();
						float Value = fx2f(pEnvelope->m_vPoints[i].m_aValues[c]);

						if(in_range(Time, TimeMin, TimeMax) && in_range(Value, ValueMin, ValueMax))
							Map()->ToggleEnvPoint(i, c);
					}
				}
			}
		}
	}
}

void CEnvelopeEditor::RenderColorBar(CUIRect ColorBar, const std::shared_ptr<CEnvelope> &pEnvelope)
{
	if(pEnvelope->m_vPoints.size() < 2)
	{
		return;
	}
	const float ViewStartTime = ScreenToEnvelopeX(ColorBar, ColorBar.x);
	const float ViewEndTime = ScreenToEnvelopeX(ColorBar, ColorBar.x + ColorBar.w);
	if(ViewEndTime < 0.0f || ViewStartTime > pEnvelope->EndTime())
	{
		return;
	}
	const float StartX = maximum(EnvelopeToScreenX(ColorBar, 0.0f), ColorBar.x);
	const float TotalWidth = minimum(EnvelopeToScreenX(ColorBar, pEnvelope->EndTime()) - StartX, ColorBar.x + ColorBar.w - StartX);

	Ui()->ClipEnable(&ColorBar);
	CUIRect ColorBarBackground = CUIRect{StartX, ColorBar.y, TotalWidth, ColorBar.h};
	Editor()->RenderBackground(ColorBarBackground, Editor()->m_CheckerTexture, ColorBarBackground.h, 1.0f);
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	int PointBeginIndex = pEnvelope->FindPointIndex(CFixedTime::FromSeconds(ViewStartTime));
	if(PointBeginIndex == -1)
	{
		PointBeginIndex = 0;
	}
	int PointEndIndex = pEnvelope->FindPointIndex(CFixedTime::FromSeconds(ViewEndTime));
	if(PointEndIndex == -1)
	{
		PointEndIndex = (int)pEnvelope->m_vPoints.size() - 2;
	}
	for(int PointIndex = PointBeginIndex; PointIndex <= PointEndIndex; PointIndex++)
	{
		const auto &PointStart = pEnvelope->m_vPoints[PointIndex];
		const auto &PointEnd = pEnvelope->m_vPoints[PointIndex + 1];
		const float PointStartTime = PointStart.m_Time.AsSeconds();
		const float PointEndTime = PointEnd.m_Time.AsSeconds();

		int Steps;
		if(PointStart.m_Curvetype == CURVETYPE_LINEAR || PointStart.m_Curvetype == CURVETYPE_STEP)
		{
			Steps = 1; // let the GPU do the work
		}
		else
		{
			const float ClampedPointStartX = maximum(EnvelopeToScreenX(ColorBar, PointStartTime), ColorBar.x);
			const float ClampedPointEndX = minimum(EnvelopeToScreenX(ColorBar, PointEndTime), ColorBar.x + ColorBar.w);
			Steps = std::clamp((int)std::sqrt(5.0f * (ClampedPointEndX - ClampedPointStartX)), 1, 250);
		}
		const float OverallSectionStartTime = Steps == 1 ? PointStartTime : maximum(PointStartTime, ViewStartTime);
		const float OverallSectionEndTime = Steps == 1 ? PointEndTime : minimum(PointEndTime, ViewEndTime);
		float SectionStartTime = OverallSectionStartTime;
		float SectionStartX = EnvelopeToScreenX(ColorBar, SectionStartTime);
		for(int Step = 1; Step <= Steps; Step++)
		{
			const float SectionEndTime = OverallSectionStartTime + (OverallSectionEndTime - OverallSectionStartTime) * (Step / (float)Steps);
			const float SectionEndX = EnvelopeToScreenX(ColorBar, SectionEndTime);

			ColorRGBA StartColor;
			if(Step == 1 && OverallSectionStartTime == PointStartTime)
			{
				StartColor = PointStart.ColorValue();
			}
			else
			{
				StartColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
				pEnvelope->Eval(SectionStartTime, StartColor, 4);
			}

			ColorRGBA EndColor;
			if(PointStart.m_Curvetype == CURVETYPE_STEP)
			{
				EndColor = StartColor;
			}
			else if(Step == Steps && OverallSectionEndTime == PointEndTime)
			{
				EndColor = PointEnd.ColorValue();
			}
			else
			{
				EndColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
				pEnvelope->Eval(SectionEndTime, EndColor, 4);
			}

			Graphics()->SetColor4(StartColor, EndColor, StartColor, EndColor);
			const IGraphics::CQuadItem QuadItem(SectionStartX, ColorBar.y, SectionEndX - SectionStartX, ColorBar.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);

			SectionStartTime = SectionEndTime;
			SectionStartX = SectionEndX;
		}
	}
	Graphics()->QuadsEnd();
	Ui()->ClipDisable();
	ColorBarBackground.h -= Ui()->Screen()->h / Graphics()->ScreenHeight(); // hack to fix alignment of bottom border
	ColorBarBackground.DrawOutline(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
}
