#include "editor_trackers.h"

#include "editor.h"
#include "editor_actions.h"

#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_sounds.h>
#include <game/editor/mapitems/layer_tiles.h>

#include <algorithm>
#include <utility>

namespace
{
	const CLayer *FindTrackedLayer(const CEditorMap &Map, int GroupIndex, int LayerIndex)
	{
		if(GroupIndex < 0 || (size_t)GroupIndex >= Map.m_vpGroups.size())
			return nullptr;
		const auto &pGroup = Map.m_vpGroups[GroupIndex];
		if(!pGroup || LayerIndex < 0 || (size_t)LayerIndex >= pGroup->m_vpLayers.size())
			return nullptr;
		return pGroup->m_vpLayers[LayerIndex].get();
	}

	bool IsQuadTrackTargetValid(const CEditorMap &Map, const std::shared_ptr<CLayerQuads> &pLayer, int GroupIndex, int LayerIndex, const std::vector<int> &vQuads)
	{
		if(!pLayer || FindTrackedLayer(Map, GroupIndex, LayerIndex) != pLayer.get())
			return false;
		return std::all_of(vQuads.begin(), vQuads.end(), [NumQuads = pLayer->m_vQuads.size()](int QuadIndex) {
			return QuadIndex >= 0 && (size_t)QuadIndex < NumQuads;
		});
	}

	const CSoundSource *FindTrackedSoundSource(const CEditorMap &Map, int GroupIndex, int LayerIndex, const CSoundSource *pTrackedSource)
	{
		const CLayer *pLayer = FindTrackedLayer(Map, GroupIndex, LayerIndex);
		if(!pLayer || pLayer->m_Type != LAYERTYPE_SOUNDS)
			return nullptr;

		const auto &vSources = static_cast<const CLayerSounds *>(pLayer)->m_vSources;
		const int SourceIndex = Map.m_SelectedSoundSource;
		if(SourceIndex < 0 || (size_t)SourceIndex >= vSources.size())
			return nullptr;

		const CSoundSource *pSource = &vSources[SourceIndex];
		return pSource == pTrackedSource ? pSource : nullptr;
	}
}

CQuadEditTracker::CQuadEditTracker(CEditorMap *pMap) :
	CMapObject(pMap),
	m_TrackedProp(EQuadProp::PROP_NONE) {}

bool CQuadEditTracker::QuadPointChanged(const std::vector<CPoint> &vCurrentPoints, int QuadIndex)
{
	for(size_t i = 0; i < vCurrentPoints.size(); i++)
	{
		if(vCurrentPoints[i] != m_InitialPoints[QuadIndex][i])
			return true;
	}
	return false;
}

bool CQuadEditTracker::QuadColorChanged(const std::vector<CColor> &vCurrentColors, int QuadIndex)
{
	for(size_t i = 0; i < vCurrentColors.size(); i++)
	{
		if(vCurrentColors[i] != m_InitialColors[QuadIndex][i])
			return true;
	}
	return false;
}

void CQuadEditTracker::BeginQuadTrack(const std::shared_ptr<CLayerQuads> &pLayer, const std::vector<int> &vSelectedQuads, int GroupIndex, int LayerIndex)
{
	GroupIndex = GroupIndex < 0 ? Map()->m_SelectedGroup : GroupIndex;
	LayerIndex = LayerIndex < 0 ? CPropTrackerHelper::GetDefaultLayerIndex(Map()) : LayerIndex;
	if(m_Tracking && m_pLayer == pLayer && m_GroupIndex == GroupIndex && m_LayerIndex == LayerIndex && m_vSelectedQuads == vSelectedQuads)
		return;
	if(!IsQuadTrackTargetValid(*Map(), pLayer, GroupIndex, LayerIndex, vSelectedQuads))
		return;

	m_Tracking = true;
	m_TrackedProp = EQuadProp::PROP_NONE;
	m_vTrackedProps.clear();
	m_InitialPoints.clear();
	m_vSelectedQuads = vSelectedQuads;
	m_pLayer = pLayer;
	m_GroupIndex = GroupIndex;
	m_LayerIndex = LayerIndex;
	for(int QuadIndex : vSelectedQuads)
	{
		const auto &Quad = pLayer->m_vQuads[QuadIndex];
		m_InitialPoints[QuadIndex] = std::vector<CPoint>(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
	}
}

void CQuadEditTracker::EndQuadTrack()
{
	if(!m_Tracking)
		return;
	m_Tracking = false;

	std::vector<std::shared_ptr<IEditorAction>> vpActions;
	if(IsQuadTrackTargetValid(*Map(), m_pLayer, m_GroupIndex, m_LayerIndex, m_vSelectedQuads))
	{
		for(int QuadIndex : m_vSelectedQuads)
		{
			const auto &Quad = m_pLayer->m_vQuads[QuadIndex];
			const std::vector<CPoint> vCurrentPoints(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
			if(QuadPointChanged(vCurrentPoints, QuadIndex))
				vpActions.push_back(std::make_shared<CEditorActionEditQuadPoint>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, m_InitialPoints[QuadIndex], vCurrentPoints));
		}
	}
	m_pLayer.reset();
	m_vSelectedQuads.clear();
	m_InitialPoints.clear();

	if(!vpActions.empty())
	{
		Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Map(), vpActions));
		Map()->OnModify();
	}
}

void CQuadEditTracker::BeginQuadPropTrack(const std::shared_ptr<CLayerQuads> &pLayer, const std::vector<int> &vSelectedQuads, EQuadProp Prop, int GroupIndex, int LayerIndex)
{
	if(Prop == EQuadProp::PROP_NONE)
		return;
	GroupIndex = GroupIndex < 0 ? Map()->m_SelectedGroup : GroupIndex;
	LayerIndex = LayerIndex < 0 ? CPropTrackerHelper::GetDefaultLayerIndex(Map()) : LayerIndex;
	if(!IsQuadTrackTargetValid(*Map(), pLayer, GroupIndex, LayerIndex, vSelectedQuads))
		return;

	m_Tracking = false;
	m_vTrackedProps.clear();
	m_TrackedProp = Prop;
	m_pLayer = pLayer;
	m_GroupIndex = GroupIndex;
	m_LayerIndex = LayerIndex;
	m_vSelectedQuads = vSelectedQuads;
	m_PreviousValues.clear();

	for(auto QuadIndex : vSelectedQuads)
	{
		auto &Quad = pLayer->m_vQuads[QuadIndex];
		if(Prop == EQuadProp::PROP_POS_X || Prop == EQuadProp::PROP_POS_Y)
			m_InitialPoints[QuadIndex] = std::vector<CPoint>(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
		else if(Prop == EQuadProp::PROP_POS_ENV)
			m_PreviousValues[QuadIndex] = Quad.m_PosEnv;
		else if(Prop == EQuadProp::PROP_POS_ENV_OFFSET)
			m_PreviousValues[QuadIndex] = Quad.m_PosEnvOffset;
		else if(Prop == EQuadProp::PROP_COLOR)
			m_InitialColors[QuadIndex] = std::vector<CColor>(std::begin(Quad.m_aColors), std::end(Quad.m_aColors));
		else if(Prop == EQuadProp::PROP_COLOR_ENV)
			m_PreviousValues[QuadIndex] = Quad.m_ColorEnv;
		else if(Prop == EQuadProp::PROP_COLOR_ENV_OFFSET)
			m_PreviousValues[QuadIndex] = Quad.m_ColorEnvOffset;
	}
}
void CQuadEditTracker::EndQuadPropTrack(EQuadProp Prop)
{
	if(Prop == EQuadProp::PROP_NONE || m_TrackedProp != Prop)
		return;
	m_TrackedProp = EQuadProp::PROP_NONE;
	const auto pLayer = std::move(m_pLayer);
	if(!IsQuadTrackTargetValid(*Map(), pLayer, m_GroupIndex, m_LayerIndex, m_vSelectedQuads))
		return;

	std::vector<std::shared_ptr<IEditorAction>> vpActions;

	for(auto QuadIndex : m_vSelectedQuads)
	{
		const auto &Quad = pLayer->m_vQuads[QuadIndex];
		if(Prop == EQuadProp::PROP_POS_X || Prop == EQuadProp::PROP_POS_Y)
		{
			auto vCurrentPoints = std::vector<CPoint>(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
			if(QuadPointChanged(vCurrentPoints, QuadIndex))
				vpActions.push_back(std::make_shared<CEditorActionEditQuadPoint>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, m_InitialPoints[QuadIndex], vCurrentPoints));
		}
		else if(Prop == EQuadProp::PROP_COLOR)
		{
			auto vCurrentColors = std::vector<CColor>(std::begin(Quad.m_aColors), std::end(Quad.m_aColors));
			if(QuadColorChanged(vCurrentColors, QuadIndex))
				vpActions.push_back(std::make_shared<CEditorActionEditQuadColor>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, m_InitialColors[QuadIndex], vCurrentColors));
		}
		else
		{
			int Value = 0;
			if(Prop == EQuadProp::PROP_POS_ENV)
				Value = Quad.m_PosEnv;
			else if(Prop == EQuadProp::PROP_POS_ENV_OFFSET)
				Value = Quad.m_PosEnvOffset;
			else if(Prop == EQuadProp::PROP_COLOR_ENV)
				Value = Quad.m_ColorEnv;
			else if(Prop == EQuadProp::PROP_COLOR_ENV_OFFSET)
				Value = Quad.m_ColorEnvOffset;

			if(Value != m_PreviousValues[QuadIndex])
				vpActions.push_back(std::make_shared<CEditorActionEditQuadProp>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, Prop, m_PreviousValues[QuadIndex], Value));
		}
	}

	if(!vpActions.empty())
		Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Map(), vpActions));
}

void CQuadEditTracker::BeginQuadPointPropTrack(const std::shared_ptr<CLayerQuads> &pLayer, const std::vector<int> &vSelectedQuads, int SelectedQuadPoints, int GroupIndex, int LayerIndex)
{
	GroupIndex = GroupIndex < 0 ? Map()->m_SelectedGroup : GroupIndex;
	LayerIndex = LayerIndex < 0 ? CPropTrackerHelper::GetDefaultLayerIndex(Map()) : LayerIndex;
	if(!m_vTrackedProps.empty() && m_pLayer == pLayer && m_GroupIndex == GroupIndex && m_LayerIndex == LayerIndex &&
		m_SelectedQuadPoints == SelectedQuadPoints && m_vSelectedQuads == vSelectedQuads)
		return;
	if(!IsQuadTrackTargetValid(*Map(), pLayer, GroupIndex, LayerIndex, vSelectedQuads))
		return;

	m_Tracking = false;
	m_TrackedProp = EQuadProp::PROP_NONE;
	m_vTrackedProps.clear();
	m_pLayer = pLayer;
	m_GroupIndex = GroupIndex;
	m_LayerIndex = LayerIndex;
	m_SelectedQuadPoints = SelectedQuadPoints;
	m_vSelectedQuads = vSelectedQuads;
	m_PreviousValuesPoint.clear();

	for(auto QuadIndex : vSelectedQuads)
	{
		m_PreviousValuesPoint[QuadIndex] = std::vector<std::map<EQuadPointProp, int>>(4);
	}
}

void CQuadEditTracker::AddQuadPointPropTrack(EQuadPointProp Prop)
{
	if(!m_pLayer || m_Tracking || m_TrackedProp != EQuadProp::PROP_NONE ||
		std::find(m_vTrackedProps.begin(), m_vTrackedProps.end(), Prop) != m_vTrackedProps.end() ||
		!IsQuadTrackTargetValid(*Map(), m_pLayer, m_GroupIndex, m_LayerIndex, m_vSelectedQuads))
		return;

	m_vTrackedProps.push_back(Prop);

	for(auto QuadIndex : m_vSelectedQuads)
	{
		auto &Quad = m_pLayer->m_vQuads[QuadIndex];
		if(Prop == EQuadPointProp::PROP_POS_X || Prop == EQuadPointProp::PROP_POS_Y)
			m_InitialPoints[QuadIndex] = std::vector<CPoint>(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
		else if(Prop == EQuadPointProp::PROP_COLOR)
		{
			for(int v = 0; v < 4; v++)
			{
				if(m_SelectedQuadPoints & (1 << v))
				{
					m_PreviousValuesPoint[QuadIndex][v][Prop] = PackColor(Quad.m_aColors[v]);
				}
			}
		}
		else if(Prop == EQuadPointProp::PROP_TEX_U)
		{
			for(int v = 0; v < 4; v++)
			{
				if(m_SelectedQuadPoints & (1 << v))
				{
					m_PreviousValuesPoint[QuadIndex][v][Prop] = Quad.m_aTexcoords[v].x;
				}
			}
		}
		else if(Prop == EQuadPointProp::PROP_TEX_V)
		{
			for(int v = 0; v < 4; v++)
			{
				if(m_SelectedQuadPoints & (1 << v))
				{
					m_PreviousValuesPoint[QuadIndex][v][Prop] = Quad.m_aTexcoords[v].y;
				}
			}
		}
	}
}

void CQuadEditTracker::EndQuadPointPropTrack(EQuadPointProp Prop)
{
	auto It = std::find(m_vTrackedProps.begin(), m_vTrackedProps.end(), Prop);
	if(It == m_vTrackedProps.end())
		return;

	m_vTrackedProps.erase(It);
	if(!IsQuadTrackTargetValid(*Map(), m_pLayer, m_GroupIndex, m_LayerIndex, m_vSelectedQuads))
	{
		m_vTrackedProps.clear();
		m_pLayer.reset();
		return;
	}

	std::vector<std::shared_ptr<IEditorAction>> vpActions;

	for(auto QuadIndex : m_vSelectedQuads)
	{
		auto &Quad = m_pLayer->m_vQuads[QuadIndex];
		if(Prop == EQuadPointProp::PROP_POS_X || Prop == EQuadPointProp::PROP_POS_Y)
		{
			auto vCurrentPoints = std::vector<CPoint>(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
			if(QuadPointChanged(vCurrentPoints, QuadIndex))
				vpActions.push_back(std::make_shared<CEditorActionEditQuadPoint>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, m_InitialPoints[QuadIndex], vCurrentPoints));
		}
		else
		{
			int Value = 0;
			for(int v = 0; v < 4; v++)
			{
				if(m_SelectedQuadPoints & (1 << v))
				{
					if(Prop == EQuadPointProp::PROP_COLOR)
					{
						Value = PackColor(Quad.m_aColors[v]);
					}
					else if(Prop == EQuadPointProp::PROP_TEX_U)
					{
						Value = Quad.m_aTexcoords[v].x;
					}
					else if(Prop == EQuadPointProp::PROP_TEX_V)
					{
						Value = Quad.m_aTexcoords[v].y;
					}

					if(Value != m_PreviousValuesPoint[QuadIndex][v][Prop])
						vpActions.push_back(std::make_shared<CEditorActionEditQuadPointProp>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, v, Prop, m_PreviousValuesPoint[QuadIndex][v][Prop], Value));
				}
			}
		}
	}

	if(m_vTrackedProps.empty())
		m_pLayer.reset();
	if(!vpActions.empty())
		Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Map(), vpActions));
}

void CQuadEditTracker::EndQuadPointPropTrackAll()
{
	if(m_vTrackedProps.empty())
		return;
	if(!IsQuadTrackTargetValid(*Map(), m_pLayer, m_GroupIndex, m_LayerIndex, m_vSelectedQuads))
	{
		m_vTrackedProps.clear();
		m_pLayer.reset();
		return;
	}

	std::vector<std::shared_ptr<IEditorAction>> vpActions;
	for(auto &Prop : m_vTrackedProps)
	{
		for(auto QuadIndex : m_vSelectedQuads)
		{
			auto &Quad = m_pLayer->m_vQuads[QuadIndex];
			if(Prop == EQuadPointProp::PROP_POS_X || Prop == EQuadPointProp::PROP_POS_Y)
			{
				auto vCurrentPoints = std::vector<CPoint>(std::begin(Quad.m_aPoints), std::end(Quad.m_aPoints));
				if(QuadPointChanged(vCurrentPoints, QuadIndex))
					vpActions.push_back(std::make_shared<CEditorActionEditQuadPoint>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, m_InitialPoints[QuadIndex], vCurrentPoints));
			}
			else
			{
				int Value = 0;
				for(int v = 0; v < 4; v++)
				{
					if(m_SelectedQuadPoints & (1 << v))
					{
						if(Prop == EQuadPointProp::PROP_COLOR)
						{
							Value = PackColor(Quad.m_aColors[v]);
						}
						else if(Prop == EQuadPointProp::PROP_TEX_U)
						{
							Value = Quad.m_aTexcoords[v].x;
						}
						else if(Prop == EQuadPointProp::PROP_TEX_V)
						{
							Value = Quad.m_aTexcoords[v].y;
						}

						if(Value != m_PreviousValuesPoint[QuadIndex][v][Prop])
							vpActions.push_back(std::make_shared<CEditorActionEditQuadPointProp>(Map(), m_GroupIndex, m_LayerIndex, QuadIndex, v, Prop, m_PreviousValuesPoint[QuadIndex][v][Prop], Value));
					}
				}
			}
		}
	}

	m_vTrackedProps.clear();
	m_pLayer.reset();
	if(!vpActions.empty())
	{
		Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Map(), vpActions));
		Map()->OnModify();
	}
}

// -----------------------------

void CEnvelopeEditorOperationTracker::Begin(EEnvelopeEditorOp Operation)
{
	if(m_TrackedOp != EEnvelopeEditorOp::OP_NONE)
		return;

	const int Index = Map()->m_SelectedEnvelope;
	if(Index < 0 || Index >= (int)Map()->m_vpEnvelopes.size())
		return;

	const auto &pEnvelope = Map()->m_vpEnvelopes[Index];
	m_TrackedOp = Operation;
	m_pEnvelope = pEnvelope;
	m_pPointData = pEnvelope->m_vPoints.data();
	m_PointCount = pEnvelope->m_vPoints.size();
	m_vSelectedPoints = Map()->m_vSelectedEnvelopePoints;
	for(auto [PointIndex, Channel] : m_vSelectedPoints)
	{
		const auto &Point = pEnvelope->m_vPoints[PointIndex];
		auto &Data = m_SavedValues[PointIndex];
		Data.m_Time = Point.m_Time;
		Data.m_Values[Channel] = Point.m_aValues[Channel];
	}
}

CEnvelope *CEnvelopeEditorOperationTracker::CurrentTarget() const
{
	const int Index = Map()->m_SelectedEnvelope;
	if(Index < 0 || Index >= (int)Map()->m_vpEnvelopes.size())
		return nullptr;

	const auto &pEnvelope = Map()->m_vpEnvelopes[Index];
	if(m_pEnvelope.lock() != pEnvelope || !PointsMatch(*pEnvelope))
		return nullptr;
	return pEnvelope.get();
}

bool CEnvelopeEditorOperationTracker::PointsMatch(const CEnvelope &Envelope) const
{
	return m_PointCount == Envelope.m_vPoints.size() && m_pPointData == Envelope.m_vPoints.data();
}

bool CEnvelopeEditorOperationTracker::TargetChanged() const
{
	return m_TrackedOp != EEnvelopeEditorOp::OP_NONE &&
	       (CurrentTarget() == nullptr || m_vSelectedPoints != Map()->m_vSelectedEnvelopePoints);
}

void CEnvelopeEditorOperationTracker::Stop(bool Switch)
{
	if(m_TrackedOp == EEnvelopeEditorOp::OP_NONE)
		return;

	if(!Switch || m_TrackedOp == EEnvelopeEditorOp::OP_SCALE)
	{
		if(const CEnvelope *pEnvelope = CurrentTarget())
			RecordChanges(*pEnvelope);
	}
	Reset();
}

void CEnvelopeEditorOperationTracker::Cancel()
{
	if(auto pEnvelope = m_pEnvelope.lock(); pEnvelope && PointsMatch(*pEnvelope))
	{
		for(const auto &[PointIndex, Data] : m_SavedValues)
		{
			auto &Point = pEnvelope->m_vPoints[PointIndex];
			Point.m_Time = Data.m_Time;
			for(const auto &[Channel, Value] : Data.m_Values)
				Point.m_aValues[Channel] = Value;
		}
	}
	Reset();
}

void CEnvelopeEditorOperationTracker::Reset()
{
	m_TrackedOp = EEnvelopeEditorOp::OP_NONE;
	m_pEnvelope.reset();
	m_pPointData = nullptr;
	m_PointCount = 0;
	m_vSelectedPoints.clear();
	m_SavedValues.clear();
}

void CEnvelopeEditorOperationTracker::RecordChanges(const CEnvelope &Envelope)
{
	const int EnvelopeIndex = Map()->m_SelectedEnvelope;
	std::vector<std::shared_ptr<IEditorAction>> vpActions;

	for(const auto &[PointIndex, Data] : m_SavedValues)
	{
		const auto &Point = Envelope.m_vPoints[PointIndex];
		if(Data.m_Time != Point.m_Time)
			vpActions.push_back(std::make_shared<CEditorActionEnvelopeEditPointTime>(Map(), EnvelopeIndex, PointIndex, Data.m_Time, Point.m_Time));

		for(const auto &[Channel, Value] : Data.m_Values)
		{
			if(Value != Point.m_aValues[Channel])
				vpActions.push_back(std::make_shared<CEditorActionEnvelopeEditPoint>(Map(), EnvelopeIndex, PointIndex, Channel, CEditorActionEnvelopeEditPoint::EEditType::VALUE, Value, Point.m_aValues[Channel]));
		}
	}

	if(!vpActions.empty())
	{
		Map()->m_EnvelopeEditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Map(), vpActions, Localize("Envelope point drag", "Editor")));
		Map()->OnModify();
	}
}

// -----------------------------------------------------------------------
CSoundSourceOperationTracker::CSoundSourceOperationTracker(CEditorMap *pMap) :
	CMapObject(pMap),
	m_pSource(nullptr),
	m_TrackedOp(ESoundSourceOp::OP_NONE),
	m_LayerIndex(-1)
{
}

void CSoundSourceOperationTracker::Begin(const CSoundSource *pSource, ESoundSourceOp Operation, int LayerIndex)
{
	if(!pSource || Operation == ESoundSourceOp::OP_NONE ||
		(m_TrackedOp == Operation && m_pSource == pSource && m_LayerIndex == LayerIndex))
		return;

	m_TrackedOp = Operation;
	m_pSource = pSource;
	m_LayerIndex = LayerIndex;

	if(Operation == ESoundSourceOp::OP_MOVE)
		m_Data.m_OriginalPoint = pSource->m_Position;
}

void CSoundSourceOperationTracker::End()
{
	if(m_TrackedOp == ESoundSourceOp::OP_NONE)
		return;

	const bool WasMove = std::exchange(m_TrackedOp, ESoundSourceOp::OP_NONE) == ESoundSourceOp::OP_MOVE;
	const CSoundSource *pTrackedSource = std::exchange(m_pSource, nullptr);
	const int LayerIndex = std::exchange(m_LayerIndex, -1);
	if(!WasMove)
		return;

	const int GroupIndex = Map()->m_SelectedGroup;
	const CSoundSource *pSource = FindTrackedSoundSource(*Map(), GroupIndex, LayerIndex, pTrackedSource);
	if(!pSource || m_Data.m_OriginalPoint == pSource->m_Position)
		return;

	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionMoveSoundSource>(
		Map(), GroupIndex, LayerIndex, Map()->m_SelectedSoundSource, m_Data.m_OriginalPoint, pSource->m_Position));
	Map()->OnModify();
}

// -----------------------------------------------------------------------

int CPropTrackerHelper::GetDefaultGroupIndex(CEditorMap *pMap)
{
	return pMap->m_SelectedGroup;
}

int CPropTrackerHelper::GetDefaultLayerIndex(CEditorMap *pMap)
{
	return pMap->m_vSelectedLayers.empty() ? -1 : pMap->m_vSelectedLayers.front();
}

// -----------------------------------------------------------------------

void CLayerPropTracker::OnEnd(ELayerProp Prop, int Value)
{
	if(Prop == ELayerProp::PROP_GROUP)
	{
		Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditLayersGroupAndOrder>(Map(), m_OriginalGroupIndex, std::vector<int>{m_OriginalLayerIndex}, m_CurrentGroupIndex, std::vector<int>{m_CurrentLayerIndex}));
	}
	else
	{
		Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditLayerProp>(Map(), m_CurrentGroupIndex, m_CurrentLayerIndex, Prop, m_OriginalValue, Value));
	}
}

int CLayerPropTracker::PropToValue(ELayerProp Prop)
{
	switch(Prop)
	{
	case ELayerProp::PROP_GROUP: return m_CurrentGroupIndex;
	case ELayerProp::PROP_HQ: return m_pObject->m_Flags;
	case ELayerProp::PROP_ORDER: return m_CurrentLayerIndex;
	default: return 0;
	}
}

// -----------------------------------------------------------------------

bool CLayerTilesPropTracker::EndChecker(ETilesProp Prop, int Value)
{
	const bool Changed = Value != m_OriginalValue || Prop == ETilesProp::PROP_SHIFT;
	if(!Changed)
		m_SavedLayers.clear();
	return Changed;
}

void CLayerTilesPropTracker::OnStart(ETilesProp Prop)
{
	if(Prop == ETilesProp::PROP_WIDTH || Prop == ETilesProp::PROP_HEIGHT)
	{
		m_SavedLayers[LAYERTYPE_TILES] = m_pObject->Duplicate();
		if(m_pObject->m_HasGame || m_pObject->m_HasFront || m_pObject->m_HasSwitch || m_pObject->m_HasSpeedup || m_pObject->m_HasTune || m_pObject->m_HasTele)
		{ // Need to save all entities layers when any entity layer
			if(Map()->m_pFrontLayer && !m_pObject->m_HasFront)
				m_SavedLayers[LAYERTYPE_FRONT] = Map()->m_pFrontLayer->Duplicate();
			if(Map()->m_pTeleLayer && !m_pObject->m_HasTele)
				m_SavedLayers[LAYERTYPE_TELE] = Map()->m_pTeleLayer->Duplicate();
			if(Map()->m_pSwitchLayer && !m_pObject->m_HasSwitch)
				m_SavedLayers[LAYERTYPE_SWITCH] = Map()->m_pSwitchLayer->Duplicate();
			if(Map()->m_pSpeedupLayer && !m_pObject->m_HasSpeedup)
				m_SavedLayers[LAYERTYPE_SPEEDUP] = Map()->m_pSpeedupLayer->Duplicate();
			if(Map()->m_pTuneLayer && !m_pObject->m_HasTune)
				m_SavedLayers[LAYERTYPE_TUNE] = Map()->m_pTuneLayer->Duplicate();
			if(!m_pObject->m_HasGame)
				m_SavedLayers[LAYERTYPE_GAME] = Map()->m_pGameLayer->Duplicate();
		}
	}
	else if(Prop == ETilesProp::PROP_SHIFT)
	{
		m_SavedLayers[LAYERTYPE_TILES] = m_pObject->Duplicate();
	}
}

void CLayerTilesPropTracker::OnEnd(ETilesProp Prop, int Value)
{
	auto pAction = std::make_shared<CEditorActionEditLayerTilesProp>(Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, Prop, m_OriginalValue, Value);

	pAction->SetSavedLayers(m_SavedLayers);
	m_SavedLayers.clear();

	Map()->m_EditorHistory.RecordAction(pAction);
}

int CLayerTilesPropTracker::PropToValue(ETilesProp Prop)
{
	switch(Prop)
	{
	case ETilesProp::PROP_AUTOMAPPER: return m_pObject->m_AutomapperConfig;
	case ETilesProp::PROP_LIVE_GAMETILES: return m_pObject->m_LiveGameTiles;
	case ETilesProp::PROP_COLOR: return PackColor(m_pObject->m_Color);
	case ETilesProp::PROP_COLOR_ENV: return m_pObject->m_ColorEnv;
	case ETilesProp::PROP_COLOR_ENV_OFFSET: return m_pObject->m_ColorEnvOffset;
	case ETilesProp::PROP_HEIGHT: return m_pObject->m_Height;
	case ETilesProp::PROP_WIDTH: return m_pObject->m_Width;
	case ETilesProp::PROP_IMAGE: return m_pObject->m_Image;
	case ETilesProp::PROP_SEED: return m_pObject->m_Seed;
	case ETilesProp::PROP_SHIFT_BY: return Map()->m_ShiftBy;
	default: return 0;
	}
}

// ------------------------------

void CLayerTilesCommonPropTracker::OnStart(ETilesCommonProp Prop)
{
	if(Prop == ETilesCommonProp::PROP_SHIFT)
		for(const auto &pLayer : m_vpLayers)
			m_SavedLayers[pLayer][LAYERTYPE_TILES] = pLayer->Duplicate();
}

void CLayerTilesCommonPropTracker::OnEnd(ETilesCommonProp Prop, int Value)
{
	std::vector<std::shared_ptr<IEditorAction>> vpActions;

	static std::map<ETilesCommonProp, ETilesProp> s_PropMap{
		{ETilesCommonProp::PROP_COLOR, ETilesProp::PROP_COLOR},
		{ETilesCommonProp::PROP_SHIFT, ETilesProp::PROP_SHIFT},
		{ETilesCommonProp::PROP_SHIFT_BY, ETilesProp::PROP_SHIFT_BY}};

	int j = 0;
	for(auto &pLayer : m_vpLayers)
	{
		int LayerIndex = m_vLayerIndices[j++];
		auto pAction = std::make_shared<CEditorActionEditLayerTilesProp>(Map(), m_OriginalGroupIndex, LayerIndex, s_PropMap[Prop], m_OriginalValue, Value);
		const auto Saved = m_SavedLayers.find(pLayer);
		if(Saved != m_SavedLayers.end())
			pAction->SetSavedLayers(Saved->second);
		vpActions.push_back(pAction);
	}
	m_SavedLayers.clear();

	char aDisplay[256];
	const char *s_apNames[] = {
		Localize("Width", "Editor"),
		Localize("Height", "Editor"),
		Localize("shift", "Editor tile shift"),
		Localize("Shift by", "Editor tile shift amount"),
		Localize("Color", "Editor"),
	};

	str_format(aDisplay, sizeof(aDisplay), Localize("Edit %d layers common property: %s", "Editor"), (int)m_vpLayers.size(), s_apNames[(int)Prop]);
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionBulk>(Map(), vpActions, aDisplay));
}

bool CLayerTilesCommonPropTracker::EndChecker(ETilesCommonProp Prop, int Value)
{
	return Value != m_OriginalValue || Prop == ETilesCommonProp::PROP_SHIFT;
}

int CLayerTilesCommonPropTracker::PropToValue(ETilesCommonProp Prop)
{
	if(Prop == ETilesCommonProp::PROP_SHIFT_BY)
		return Map()->m_ShiftBy;
	return 0;
}

// ------------------------------

void CLayerGroupPropTracker::OnEnd(EGroupProp Prop, int Value)
{
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditGroupProp>(Map(), Map()->m_SelectedGroup, Prop, m_OriginalValue, Value));
}

int CLayerGroupPropTracker::PropToValue(EGroupProp Prop)
{
	switch(Prop)
	{
	case EGroupProp::PROP_ORDER: return Map()->m_SelectedGroup;
	case EGroupProp::PROP_POS_X: return m_pObject->m_OffsetX;
	case EGroupProp::PROP_POS_Y: return m_pObject->m_OffsetY;
	case EGroupProp::PROP_PARA_X: return m_pObject->m_ParallaxX;
	case EGroupProp::PROP_PARA_Y: return m_pObject->m_ParallaxY;
	case EGroupProp::PROP_USE_CLIPPING: return m_pObject->m_UseClipping;
	case EGroupProp::PROP_CLIP_X: return m_pObject->m_ClipX;
	case EGroupProp::PROP_CLIP_Y: return m_pObject->m_ClipY;
	case EGroupProp::PROP_CLIP_W: return m_pObject->m_ClipW;
	case EGroupProp::PROP_CLIP_H: return m_pObject->m_ClipH;
	default: return 0;
	}
}

// ------------------------------------------------------------------

void CLayerQuadsPropTracker::OnEnd(ELayerQuadsProp Prop, int Value)
{
	auto pAction = std::make_shared<CEditorActionEditLayerQuadsProp>(Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, Prop, m_OriginalValue, Value);
	Map()->m_EditorHistory.RecordAction(pAction);
}

int CLayerQuadsPropTracker::PropToValue(ELayerQuadsProp Prop)
{
	if(Prop == ELayerQuadsProp::PROP_IMAGE)
		return m_pObject->m_Image;
	return 0;
}

// -------------------------------------------------------------------

void CLayerSoundsPropTracker::OnEnd(ELayerSoundsProp Prop, int Value)
{
	auto pAction = std::make_shared<CEditorActionEditLayerSoundsProp>(Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, Prop, m_OriginalValue, Value);
	Map()->m_EditorHistory.RecordAction(pAction);
}

int CLayerSoundsPropTracker::PropToValue(ELayerSoundsProp Prop)
{
	if(Prop == ELayerSoundsProp::PROP_SOUND)
		return m_pObject->m_Sound;
	return 0;
}

// ----

void CSoundSourcePropTracker::OnEnd(ESoundProp Prop, int Value)
{
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditSoundSourceProp>(Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, Map()->m_SelectedSoundSource, Prop, m_OriginalValue, Value));
}

int CSoundSourcePropTracker::PropToValue(ESoundProp Prop)
{
	const CSoundSource *pSource = FindTrackedSoundSource(*Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, m_pObject);
	if(!pSource || m_CurrentGroupIndex != m_OriginalGroupIndex || m_CurrentLayerIndex != m_OriginalLayerIndex)
		return m_OriginalValue;

	switch(Prop)
	{
	case ESoundProp::PROP_POS_X: return pSource->m_Position.x;
	case ESoundProp::PROP_POS_Y: return pSource->m_Position.y;
	case ESoundProp::PROP_LOOP: return pSource->m_Loop;
	case ESoundProp::PROP_PAN: return pSource->m_Pan;
	case ESoundProp::PROP_TIME_DELAY: return pSource->m_TimeDelay;
	case ESoundProp::PROP_FALLOFF: return pSource->m_Falloff;
	case ESoundProp::PROP_POS_ENV: return pSource->m_PosEnv;
	case ESoundProp::PROP_POS_ENV_OFFSET: return pSource->m_PosEnvOffset;
	case ESoundProp::PROP_SOUND_ENV: return pSource->m_SoundEnv;
	case ESoundProp::PROP_SOUND_ENV_OFFSET: return pSource->m_SoundEnvOffset;
	default: return 0;
	}
}

// ----

void CSoundSourceRectShapePropTracker::OnEnd(ERectangleShapeProp Prop, int Value)
{
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditRectSoundSourceShapeProp>(Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, Map()->m_SelectedSoundSource, Prop, m_OriginalValue, Value));
}

int CSoundSourceRectShapePropTracker::PropToValue(ERectangleShapeProp Prop)
{
	const CSoundSource *pSource = FindTrackedSoundSource(*Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, m_pObject);
	if(!pSource || m_CurrentGroupIndex != m_OriginalGroupIndex || m_CurrentLayerIndex != m_OriginalLayerIndex ||
		pSource->m_Shape.m_Type != CSoundShape::SHAPE_RECTANGLE)
		return m_OriginalValue;

	switch(Prop)
	{
	case ERectangleShapeProp::PROP_RECTANGLE_WIDTH: return pSource->m_Shape.m_Rectangle.m_Width;
	case ERectangleShapeProp::PROP_RECTANGLE_HEIGHT: return pSource->m_Shape.m_Rectangle.m_Height;
	default: return 0;
	}
}

void CSoundSourceCircleShapePropTracker::OnEnd(ECircleShapeProp Prop, int Value)
{
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionEditCircleSoundSourceShapeProp>(Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, Map()->m_SelectedSoundSource, Prop, m_OriginalValue, Value));
}

int CSoundSourceCircleShapePropTracker::PropToValue(ECircleShapeProp Prop)
{
	const CSoundSource *pSource = FindTrackedSoundSource(*Map(), m_OriginalGroupIndex, m_OriginalLayerIndex, m_pObject);
	if(!pSource || m_CurrentGroupIndex != m_OriginalGroupIndex || m_CurrentLayerIndex != m_OriginalLayerIndex ||
		pSource->m_Shape.m_Type != CSoundShape::SHAPE_CIRCLE)
		return m_OriginalValue;

	switch(Prop)
	{
	case ECircleShapeProp::PROP_CIRCLE_RADIUS: return pSource->m_Shape.m_Circle.m_Radius;
	default: return 0;
	}
}
