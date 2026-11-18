#include "envelope_extrema.h"

#include <game/map/map_render_items.h>

#include <cstdint>
#include <limits>

CEnvelopeExtrema::CEnvelopeExtrema(IMap *pMap) :
	m_EnvelopePoints(pMap)
{
	m_pMap = pMap;

	m_EnvelopeExtremaItemNone.m_Available = true;
	m_EnvelopeExtremaItemNone.m_Rotating = false;
	m_EnvelopeExtremaItemNone.m_Minima = ivec2(0, 0);
	m_EnvelopeExtremaItemNone.m_Maxima = ivec2(0, 0);

	m_EnvelopeExtremaItemInvalid.m_Available = false;
	m_EnvelopeExtremaItemInvalid.m_Rotating = false;
	m_EnvelopeExtremaItemInvalid.m_Minima = ivec2(0, 0);
	m_EnvelopeExtremaItemInvalid.m_Maxima = ivec2(0, 0);

	CalculateExtrema();
}

void CEnvelopeExtrema::CalculateEnvelope(const CMapItemEnvelope *pEnvelopeItem, int EnvelopeIndex)
{
	CEnvelopeExtremaItem &EnvExt = m_vEnvelopeExtrema[EnvelopeIndex];

	// setup default values
	for(int Channel = 0; Channel < 2; ++Channel)
	{
		EnvExt.m_Minima[Channel] = std::numeric_limits<int>::max(); // minimum of channel
		EnvExt.m_Maxima[Channel] = std::numeric_limits<int>::min(); // maximum of channel
	}
	EnvExt.m_Available = false;
	EnvExt.m_Rotating = false;

	// check if the envelope is a valid position envelope
	if(!pEnvelopeItem || pEnvelopeItem->m_Channels != 3)
		return;
	const int Start = pEnvelopeItem->m_StartPoint;
	const int Count = pEnvelopeItem->m_NumPoints;
	if(Count == 0)
	{
		EnvExt.m_Minima = EnvExt.m_Maxima = ivec2(0, 0);
		EnvExt.m_Available = true;
		return;
	}
	if(Start < 0 || Count < 0 || Start > m_EnvelopePoints.NumPointsMax() || Count > m_EnvelopePoints.NumPointsMax() - Start)
		return;
	const auto ExpandByTangent = [&EnvExt](int Channel, int Value, int Tangent) {
		const int64_t Position = static_cast<int64_t>(Value) + Tangent;
		// 范围无法表示时禁用提前裁剪，实际包络求值仍走原路径。
		if(Position < std::numeric_limits<int>::min() || Position > std::numeric_limits<int>::max())
			return false;
		EnvExt.m_Minima[Channel] = std::min(static_cast<int>(Position), EnvExt.m_Minima[Channel]);
		EnvExt.m_Maxima[Channel] = std::max(static_cast<int>(Position), EnvExt.m_Maxima[Channel]);
		return true;
	};

	const int End = Start + Count;
	for(int PointId = Start; PointId < End; ++PointId)
	{
		const CEnvPoint *pEnvPoint = m_EnvelopePoints.GetPoint(PointId);
		if(!pEnvPoint)
			return;

		// check if quad is rotating
		if(pEnvPoint->m_aValues[2] != 0)
			EnvExt.m_Rotating = true;

		for(int Channel = 0; Channel < 2; ++Channel)
		{
			EnvExt.m_Minima[Channel] = std::min(pEnvPoint->m_aValues[Channel], EnvExt.m_Minima[Channel]);
			EnvExt.m_Maxima[Channel] = std::max(pEnvPoint->m_aValues[Channel], EnvExt.m_Maxima[Channel]);
		}

		const bool HasOutgoing = PointId < End - 1 && pEnvPoint->m_Curvetype == CURVETYPE_BEZIER;
		const CEnvPoint *pPreviousPoint = PointId > Start ? m_EnvelopePoints.GetPoint(PointId - 1) : nullptr;
		const bool HasIncoming = pPreviousPoint != nullptr && pPreviousPoint->m_Curvetype == CURVETYPE_BEZIER;
		if(!HasOutgoing && !HasIncoming)
			continue;
		const CEnvPointBezier *pBezier = m_EnvelopePoints.GetBezier(PointId);
		if(!pBezier)
			return;

		const auto IncludeTangent = [&](const int *pDeltaY) {
			if(pDeltaY[2] != 0)
				EnvExt.m_Rotating = true;
			for(int Channel = 0; Channel < 2; ++Channel)
			{
				if(!ExpandByTangent(Channel, pEnvPoint->m_aValues[Channel], pDeltaY[Channel]))
					return false;
			}
			return true;
		};
		if((HasOutgoing && !IncludeTangent(pBezier->m_aOutTangentDeltaY)) ||
			(HasIncoming && !IncludeTangent(pBezier->m_aInTangentDeltaY)))
			return;
	}

	EnvExt.m_Available = true;
}

void CEnvelopeExtrema::CalculateExtrema()
{
	int EnvelopeStart, EnvelopeNum;
	m_pMap->GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeNum);
	m_vEnvelopeExtrema.resize(EnvelopeNum);
	for(int EnvelopeIndex = 0; EnvelopeIndex < EnvelopeNum; ++EnvelopeIndex)
	{
		const auto Item = MapRenderItems::ReadEnvelope(*m_pMap, EnvelopeStart + EnvelopeIndex);
		CalculateEnvelope(Item ? &*Item : nullptr, EnvelopeIndex);
	}
}

const CEnvelopeExtrema::CEnvelopeExtremaItem &CEnvelopeExtrema::GetExtrema(int EnvelopeIndex) const
{
	if(EnvelopeIndex == -1)
		return m_EnvelopeExtremaItemNone; // No envelope just means no movement
	else if(EnvelopeIndex < -1 || EnvelopeIndex >= (int)m_vEnvelopeExtrema.size())
		return m_EnvelopeExtremaItemInvalid;
	return m_vEnvelopeExtrema[EnvelopeIndex];
}
