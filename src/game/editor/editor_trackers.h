#ifndef GAME_EDITOR_EDITOR_TRACKERS_H
#define GAME_EDITOR_EDITOR_TRACKERS_H

#include <game/client/ui.h>
#include <game/editor/map_object.h>
#include <game/editor/mapitems.h>
#include <game/mapitems.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class CEnvelope;
class CLayer;
class CLayerGroup;
class CLayerQuads;
class CLayerSounds;
class CLayerTiles;
class CSoundSource;

class CQuadEditTracker : public CMapObject
{
public:
	explicit CQuadEditTracker(CEditorMap *pMap);

	bool QuadPointChanged(const std::vector<CPoint> &vCurrentPoints, int QuadIndex);
	bool QuadColorChanged(const std::vector<CColor> &vCurrentColors, int QuadIndex);

	void BeginQuadTrack(const std::shared_ptr<CLayerQuads> &pLayer, const std::vector<int> &vSelectedQuads, int GroupIndex = -1, int LayerIndex = -1);
	void EndQuadTrack();

	void BeginQuadPropTrack(const std::shared_ptr<CLayerQuads> &pLayer, const std::vector<int> &vSelectedQuads, EQuadProp Prop, int GroupIndex = -1, int LayerIndex = -1);
	void EndQuadPropTrack(EQuadProp Prop);

	void BeginQuadPointPropTrack(const std::shared_ptr<CLayerQuads> &pLayer, const std::vector<int> &vSelectedQuads, int SelectedQuadPoints, int GroupIndex = -1, int LayerIndex = -1);
	void AddQuadPointPropTrack(EQuadPointProp Prop);
	void EndQuadPointPropTrack(EQuadPointProp Prop);
	void EndQuadPointPropTrackAll();

private:
	std::vector<int> m_vSelectedQuads;
	int m_SelectedQuadPoints;
	std::map<int, std::vector<CPoint>> m_InitialPoints;
	std::map<int, std::vector<CColor>> m_InitialColors;

	bool m_Tracking = false;
	std::shared_ptr<CLayerQuads> m_pLayer;

	EQuadProp m_TrackedProp;
	std::vector<EQuadPointProp> m_vTrackedProps;
	std::map<int, int> m_PreviousValues;
	std::map<int, std::vector<std::map<EQuadPointProp, int>>> m_PreviousValuesPoint;
	int m_LayerIndex;
	int m_GroupIndex;
};

enum class EEnvelopeEditorOp
{
	OP_NONE = 0,
	OP_SELECT,
	OP_DRAG_POINT,
	OP_DRAG_POINT_X,
	OP_DRAG_POINT_Y,
	OP_CONTEXT_MENU,
	OP_BOX_SELECT,
	OP_DRAG_TIME_BAR,
	OP_SCALE
};

enum class ESoundSourceOp
{
	OP_NONE = 0,
	OP_MOVE,
	OP_CONTEXT_MENU,
};

class CEnvelopeEditorOperationTracker : public CMapObject
{
public:
	explicit CEnvelopeEditorOperationTracker(CEditorMap *pMap) :
		CMapObject(pMap) {}

	void Begin(EEnvelopeEditorOp Operation);
	void Stop(bool Switch = true);
	void Cancel();
	void Reset();
	bool TargetChanged() const;

private:
	EEnvelopeEditorOp m_TrackedOp = EEnvelopeEditorOp::OP_NONE;

	class CPointData
	{
	public:
		CFixedTime m_Time;
		std::map<int, int> m_Values;
	};

	std::weak_ptr<CEnvelope> m_pEnvelope;
	const void *m_pPointData = nullptr;
	size_t m_PointCount = 0;
	std::vector<std::pair<int, int>> m_vSelectedPoints;
	std::map<int, CPointData> m_SavedValues;

	bool PointsMatch(const CEnvelope &Envelope) const;
	CEnvelope *CurrentTarget() const;
	void RecordChanges(const CEnvelope &Envelope);
};

class CSoundSourceOperationTracker : public CMapObject
{
public:
	explicit CSoundSourceOperationTracker(CEditorMap *pMap);

	void Begin(const CSoundSource *pSource, ESoundSourceOp Operation, int LayerIndex);
	void End();

private:
	const CSoundSource *m_pSource;
	ESoundSourceOp m_TrackedOp;
	int m_LayerIndex;

	class CData
	{
	public:
		CPoint m_OriginalPoint;
	};
	CData m_Data;
};

class CPropTrackerHelper
{
public:
	static int GetDefaultGroupIndex(CEditorMap *pMap);
	static int GetDefaultLayerIndex(CEditorMap *pMap);
};

template<typename T, typename E>
class CPropTracker : public CMapObject
{
public:
	explicit CPropTracker(CEditorMap *pMap) :
		CMapObject(pMap),
		m_OriginalValue(0),
		m_pObject(nullptr),
		m_OriginalLayerIndex(-1),
		m_OriginalGroupIndex(-1),
		m_CurrentLayerIndex(-1),
		m_CurrentGroupIndex(-1) {}

	void Begin(const T *pObject, E Prop, EEditState State, int GroupIndex = -1, int LayerIndex = -1)
	{
		m_EndReady = false;
		m_pObject = nullptr;

		const bool Starting = State == EEditState::START || State == EEditState::ONE_GO;
		if(Prop == NO_PROP || (!Starting && State != EEditState::END))
			return;

		const int SelectedGroup = GroupIndex < 0 ? CPropTrackerHelper::GetDefaultGroupIndex(Map()) : GroupIndex;
		const auto ObjectAddress = reinterpret_cast<std::uintptr_t>(pObject);
		if(Starting)
		{
			DiscardEdit();
			m_TrackedProp = Prop;
			m_TrackedObjectAddress = ObjectAddress;
			m_OriginalGroupIndex = SelectedGroup;
			m_OriginalLayerIndex = LayerIndex < 0 ? CPropTrackerHelper::GetDefaultLayerIndex(Map()) : LayerIndex;
			m_CurrentGroupIndex = m_OriginalGroupIndex;
			m_CurrentLayerIndex = m_OriginalLayerIndex;

			m_pObject = pObject;
			m_OriginalValue = PropToValue(Prop);
			OnStart(Prop);
		}
		else if(Prop != m_TrackedProp || ObjectAddress != m_TrackedObjectAddress ||
			(pObject == nullptr && SelectedGroup != m_OriginalGroupIndex))
		{
			DiscardEdit();
			return;
		}

		m_EndReady = State != EEditState::START;
		m_pObject = m_EndReady ? pObject : nullptr;
	}

	void End(E Prop, EEditState State, int GroupIndex = -1, int LayerIndex = -1)
	{
		if(!m_EndReady || Prop != m_TrackedProp || (State != EEditState::END && State != EEditState::ONE_GO))
			return;

		m_CurrentGroupIndex = GroupIndex < 0 ? CPropTrackerHelper::GetDefaultGroupIndex(Map()) : GroupIndex;
		m_CurrentLayerIndex = LayerIndex < 0 ? CPropTrackerHelper::GetDefaultLayerIndex(Map()) : LayerIndex;

		m_EndReady = false;
		m_TrackedProp = NO_PROP;
		const int Value = PropToValue(Prop);
		if(EndChecker(Prop, Value))
			OnEnd(Prop, Value);
		m_pObject = nullptr;
	}

protected:
	virtual void OnStart(E Prop) {}
	virtual void OnEnd(E Prop, int Value) {}
	virtual void OnDiscard() {}
	virtual int PropToValue(E Prop) { return 0; }
	virtual bool EndChecker(E Prop, int Value)
	{
		return Value != m_OriginalValue;
	}

	int m_OriginalValue;
	const T *m_pObject;
	int m_OriginalLayerIndex;
	int m_OriginalGroupIndex;
	int m_CurrentLayerIndex;
	int m_CurrentGroupIndex;

private:
	static constexpr E NO_PROP = static_cast<E>(-1);
	E m_TrackedProp = NO_PROP;
	// 跨帧仅比较地址，读取属性时使用本帧 Begin 传入的对象。
	std::uintptr_t m_TrackedObjectAddress = 0;
	bool m_EndReady = false;

	void DiscardEdit()
	{
		if(m_TrackedProp != NO_PROP)
			OnDiscard();
		m_TrackedProp = NO_PROP;
	}
};

class CLayerPropTracker : public CPropTracker<CLayer, ELayerProp>
{
public:
	explicit CLayerPropTracker(CEditorMap *pMap) :
		CPropTracker<CLayer, ELayerProp>(pMap) {}

protected:
	void OnEnd(ELayerProp Prop, int Value) override;
	int PropToValue(ELayerProp Prop) override;
};

class CLayerTilesPropTracker : public CPropTracker<CLayerTiles, ETilesProp>
{
public:
	explicit CLayerTilesPropTracker(CEditorMap *pMap) :
		CPropTracker<CLayerTiles, ETilesProp>(pMap) {}

protected:
	void OnStart(ETilesProp Prop) override;
	void OnEnd(ETilesProp Prop, int Value) override;
	void OnDiscard() override { m_SavedLayers.clear(); }
	bool EndChecker(ETilesProp Prop, int Value) override;

	int PropToValue(ETilesProp Prop) override;

private:
	std::map<int, std::shared_ptr<CLayer>> m_SavedLayers;
};

class CLayerTilesCommonPropTracker : public CPropTracker<CLayerTiles, ETilesCommonProp>
{
public:
	explicit CLayerTilesCommonPropTracker(CEditorMap *pMap) :
		CPropTracker<CLayerTiles, ETilesCommonProp>(pMap) {}

protected:
	void OnStart(ETilesCommonProp Prop) override;
	void OnEnd(ETilesCommonProp Prop, int Value) override;
	bool EndChecker(ETilesCommonProp Prop, int Value) override;

	int PropToValue(ETilesCommonProp Prop) override;

private:
	std::map<std::shared_ptr<CLayerTiles>, std::map<int, std::shared_ptr<CLayer>>> m_SavedLayers;

public:
	std::vector<std::shared_ptr<CLayerTiles>> m_vpLayers;
	std::vector<int> m_vLayerIndices;
};

class CLayerGroupPropTracker : public CPropTracker<CLayerGroup, EGroupProp>
{
public:
	explicit CLayerGroupPropTracker(CEditorMap *pMap) :
		CPropTracker<CLayerGroup, EGroupProp>(pMap) {}

protected:
	void OnEnd(EGroupProp Prop, int Value) override;
	int PropToValue(EGroupProp Prop) override;
};

class CLayerQuadsPropTracker : public CPropTracker<CLayerQuads, ELayerQuadsProp>
{
public:
	explicit CLayerQuadsPropTracker(CEditorMap *pMap) :
		CPropTracker<CLayerQuads, ELayerQuadsProp>(pMap) {}

protected:
	void OnEnd(ELayerQuadsProp Prop, int Value) override;
	int PropToValue(ELayerQuadsProp Prop) override;
};

class CLayerSoundsPropTracker : public CPropTracker<CLayerSounds, ELayerSoundsProp>
{
public:
	explicit CLayerSoundsPropTracker(CEditorMap *pMap) :
		CPropTracker<CLayerSounds, ELayerSoundsProp>(pMap) {}

protected:
	void OnEnd(ELayerSoundsProp Prop, int Value) override;
	int PropToValue(ELayerSoundsProp Prop) override;
};

class CSoundSourcePropTracker : public CPropTracker<CSoundSource, ESoundProp>
{
public:
	explicit CSoundSourcePropTracker(CEditorMap *pMap) :
		CPropTracker<CSoundSource, ESoundProp>(pMap) {}

protected:
	void OnEnd(ESoundProp Prop, int Value) override;
	int PropToValue(ESoundProp Prop) override;
};

class CSoundSourceRectShapePropTracker : public CPropTracker<CSoundSource, ERectangleShapeProp>
{
public:
	explicit CSoundSourceRectShapePropTracker(CEditorMap *pMap) :
		CPropTracker<CSoundSource, ERectangleShapeProp>(pMap) {}

protected:
	void OnEnd(ERectangleShapeProp Prop, int Value) override;
	int PropToValue(ERectangleShapeProp Prop) override;
};

class CSoundSourceCircleShapePropTracker : public CPropTracker<CSoundSource, ECircleShapeProp>
{
public:
	explicit CSoundSourceCircleShapePropTracker(CEditorMap *pMap) :
		CPropTracker<CSoundSource, ECircleShapeProp>(pMap) {}

protected:
	void OnEnd(ECircleShapeProp Prop, int Value) override;
	int PropToValue(ECircleShapeProp Prop) override;
};

#endif
