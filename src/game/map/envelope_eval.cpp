/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "render_map.h"

#include <base/math.h>

#include <engine/map.h>
#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/mapitems_ex.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>

using namespace std::chrono_literals;

int IEnvelopePointAccess::FindPointIndex(CFixedTime Time) const
{
	// binary search for the interval around Time
	int Low = 0;
	int High = NumPoints() - 2;
	int FoundIndex = -1;

	while(Low <= High)
	{
		int Mid = Low + (High - Low) / 2;
		const CEnvPoint *pMid = GetPoint(Mid);
		const CEnvPoint *pNext = GetPoint(Mid + 1);
		if(Time >= pMid->m_Time && Time < pNext->m_Time)
		{
			FoundIndex = Mid;
			break;
		}
		else if(Time < pMid->m_Time)
		{
			High = Mid - 1;
		}
		else
		{
			Low = Mid + 1;
		}
	}
	return FoundIndex;
}

CMapBasedEnvelopePointAccess::CMapBasedEnvelopePointAccess(CDataFileReader *pReader)
{
	m_pPoints = nullptr;
	m_pPointsBezier = nullptr;
	m_pPointsBezierUpstream = nullptr;
	m_NumPointsMax = 0;
	bool FoundBezierEnvelope = false;
	int EnvelopeStart, EnvelopeNum;
	pReader->GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeNum);
	for(int EnvelopeIndex = 0; EnvelopeIndex < EnvelopeNum; EnvelopeIndex++)
	{
		const int ItemIndex = EnvelopeStart + EnvelopeIndex;
		if(pReader->GetItemSize(ItemIndex) < static_cast<int>(sizeof(int)))
			continue;
		const auto *pVersion = static_cast<const int *>(pReader->GetItem(ItemIndex));
		if(pVersion != nullptr && *pVersion >= CMapItemEnvelope::VERSION_TEEWORLDS_BEZIER)
		{
			FoundBezierEnvelope = true;
			break;
		}
	}

	if(FoundBezierEnvelope)
	{
		int EnvPointStart, FakeEnvPointNum;
		pReader->GetType(MAPITEMTYPE_ENVPOINTS, &EnvPointStart, &FakeEnvPointNum);
		if(FakeEnvPointNum > 0)
		{
			m_pPointsBezierUpstream = static_cast<CEnvPointBezier_upstream *>(pReader->GetItem(EnvPointStart));
			const int Size = pReader->GetItemSize(EnvPointStart);
			if(m_pPointsBezierUpstream != nullptr && Size >= 0)
				m_NumPointsMax = Size / static_cast<int>(sizeof(CEnvPointBezier_upstream));
		}
	}
	else
	{
		int EnvPointStart, FakeEnvPointNum;
		pReader->GetType(MAPITEMTYPE_ENVPOINTS, &EnvPointStart, &FakeEnvPointNum);
		if(FakeEnvPointNum > 0)
		{
			m_pPoints = static_cast<CEnvPoint *>(pReader->GetItem(EnvPointStart));
			const int Size = pReader->GetItemSize(EnvPointStart);
			if(m_pPoints != nullptr && Size >= 0)
				m_NumPointsMax = Size / static_cast<int>(sizeof(CEnvPoint));
		}

		int EnvPointBezierStart, FakeEnvPointBezierNum;
		pReader->GetType(MAPITEMTYPE_ENVPOINTS_BEZIER, &EnvPointBezierStart, &FakeEnvPointBezierNum);
		if(FakeEnvPointBezierNum > 0)
		{
			const int Size = pReader->GetItemSize(EnvPointBezierStart);
			if(Size >= 0 && m_NumPointsMax == Size / static_cast<int>(sizeof(CEnvPointBezier)))
				m_pPointsBezier = static_cast<CEnvPointBezier *>(pReader->GetItem(EnvPointBezierStart));
		}
	}

	SetPointsRange(0, m_NumPointsMax);
}

CMapBasedEnvelopePointAccess::CMapBasedEnvelopePointAccess(IMap *pMap) :
	CMapBasedEnvelopePointAccess(static_cast<CMap *>(pMap)->GetReader())
{
}

void CMapBasedEnvelopePointAccess::SetPointsRange(int StartPoint, int NumPoints)
{
	m_StartPoint = std::clamp(StartPoint, 0, m_NumPointsMax);
	m_NumPoints = std::clamp(NumPoints, 0, m_NumPointsMax - m_StartPoint);
}

int CMapBasedEnvelopePointAccess::StartPoint() const
{
	return m_StartPoint;
}

int CMapBasedEnvelopePointAccess::NumPoints() const
{
	return m_NumPoints;
}

int CMapBasedEnvelopePointAccess::NumPointsMax() const
{
	return m_NumPointsMax;
}

const CEnvPoint *CMapBasedEnvelopePointAccess::GetPoint(int Index) const
{
	if(Index < 0 || Index >= m_NumPoints)
		return nullptr;
	if(m_pPoints != nullptr)
		return &m_pPoints[Index + m_StartPoint];
	if(m_pPointsBezierUpstream != nullptr)
		return &m_pPointsBezierUpstream[Index + m_StartPoint];
	return nullptr;
}

const CEnvPointBezier *CMapBasedEnvelopePointAccess::GetBezier(int Index) const
{
	if(Index < 0 || Index >= m_NumPoints)
		return nullptr;
	if(m_pPointsBezier != nullptr)
		return &m_pPointsBezier[Index + m_StartPoint];
	if(m_pPointsBezierUpstream != nullptr)
		return &m_pPointsBezierUpstream[Index + m_StartPoint].m_Bezier;
	return nullptr;
}

static double SolveBezierPolynomial(double Time, double Control1, double Control2, double Duration)
{
	const double x3 = 3.0 * (Control1 - Control2) + Duration;
	const double x2 = 3.0 * Control2 - 6.0 * Control1;
	const double x1 = 3.0 * Control1;
	const double x0 = -Time;

	if(x3 == 0.0 && x2 == 0.0)
	{
		// linear
		// a * t + b = 0
		const double a = x1;
		const double b = x0;

		if(a == 0.0)
			return 0.0f;
		return -b / a;
	}
	else if(x3 == 0.0)
	{
		// quadratic
		// t * t + b * t + c = 0
		const double b = x1 / x2;
		const double c = x0 / x2;

		if(c == 0.0)
			return 0.0f;

		const double D = b * b - 4.0 * c;
		const double SqrtD = std::sqrt(D);

		const double t = (-b + SqrtD) / 2.0;

		if(0.0 <= t && t <= 1.0001)
			return t;
		return (-b - SqrtD) / 2.0;
	}
	else
	{
		// cubic
		// t * t * t + a * t * t + b * t * t + c = 0
		const double a = x2 / x3;
		const double b = x1 / x3;
		const double c = x0 / x3;

		// substitute t = y - a / 3
		const double Substitute = a / 3.0;

		// depressed form x^3 + px + q = 0
		// cardano's method
		const double p = b / 3.0 - a * a / 9.0;
		const double q = (2.0 * a * a * a / 27.0 - a * b / 3.0 + c) / 2.0;

		const double D = q * q + p * p * p;

		if(D > 0.0)
		{
			// only one 'real' solution
			const double s = std::sqrt(D);
			return std::cbrt(s - q) - std::cbrt(s + q) - Substitute;
		}
		else if(D == 0.0)
		{
			// one single, one double solution or triple solution
			const double s = std::cbrt(-q);
			const double t = 2.0 * s - Substitute;

			if(0.0 <= t && t <= 1.0001)
				return t;
			return (-s - Substitute);
		}
		else
		{
			// Casus irreducibilis ... ,_,
			const double Phi = std::acos(-q / std::sqrt(-(p * p * p))) / 3.0;
			const double s = 2.0 * std::sqrt(-p);

			const double t1 = s * std::cos(Phi) - Substitute;

			if(0.0 <= t1 && t1 <= 1.0001)
				return t1;

			const double t2 = -s * std::cos(Phi + std::numbers::pi / 3.0) - Substitute;

			if(0.0 <= t2 && t2 <= 1.0001)
				return t2;
			return -s * std::cos(Phi - std::numbers::pi / 3.0) - Substitute;
		}
	}
}

static float SolveBezier(double Time, double Control1, double Control2, double Duration)
{
	if(Time == 0.0)
		return 0.0f;

	const double Parameter = SolveBezierPolynomial(Time, Control1, Control2, Duration);
	constexpr double SamplePrecisionMillis = 1.0 / std::chrono::nanoseconds(1ms).count();
	if(Parameter >= 0.0 && Parameter <= 1.0 &&
		std::abs(bezier(0.0, Control1, Control2, Duration, Parameter) - Time) <= SamplePrecisionMillis)
		return static_cast<float>(Parameter);

	// 闭式解消减误差过大时，在单调的时间轴上二分。
	double Low = 0.0, High = 1.0;
	for(int i = 0; i < std::numeric_limits<double>::digits; ++i)
	{
		const double Mid = (Low + High) * 0.5;
		if(bezier(0.0, Control1, Control2, Duration, Mid) < Time)
			Low = Mid;
		else
			High = Mid;
	}
	return static_cast<float>((Low + High) * 0.5);
}

void CRenderMap::RenderEvalEnvelope(const IEnvelopePointAccess *pPoints, std::chrono::nanoseconds TimeNanos, ColorRGBA &Result, size_t Channels)
{
	const int NumPoints = pPoints->NumPoints();
	if(NumPoints == 0)
	{
		return;
	}

	if(NumPoints == 1)
	{
		const CEnvPoint *pFirstPoint = pPoints->GetPoint(0);
		for(size_t c = 0; c < Channels; c++)
		{
			Result[c] = fx2f(pFirstPoint->m_aValues[c]);
		}
		return;
	}

	const CEnvPoint *pLastPoint = pPoints->GetPoint(NumPoints - 1);
	const int64_t MaxPointTime = (int64_t)pLastPoint->m_Time.GetInternal() * std::chrono::nanoseconds(1ms).count();
	if(MaxPointTime > 0) // TODO: remove this check when implementing a IO check for maps(in this case broken envelopes)
		TimeNanos = std::chrono::nanoseconds(TimeNanos.count() % MaxPointTime);
	else
		TimeNanos = decltype(TimeNanos)::zero();

	const double TimeMillis = TimeNanos.count() / (double)std::chrono::nanoseconds(1ms).count();

	int FoundIndex = pPoints->FindPointIndex(CFixedTime(TimeMillis));
	if(FoundIndex == -1)
	{
		for(size_t c = 0; c < Channels; c++)
		{
			Result[c] = fx2f(pLastPoint->m_aValues[c]);
		}
		return;
	}

	const CEnvPoint *pCurrentPoint = pPoints->GetPoint(FoundIndex);
	const CEnvPoint *pNextPoint = pPoints->GetPoint(FoundIndex + 1);

	const int StartMillis = pCurrentPoint->m_Time.GetInternal();
	const int64_t Delta = static_cast<int64_t>(pNextPoint->m_Time.GetInternal()) - StartMillis;
	if(Delta <= 0)
	{
		for(size_t c = 0; c < Channels; c++)
		{
			Result[c] = fx2f(pCurrentPoint->m_aValues[c]);
		}
		return;
	}

	// 按整毫秒查找会向零截断，负的亚毫秒采样可能早于所选区间。
	const double SampleOffsetMillis = std::max(TimeMillis - StartMillis, 0.0);
	float a = (float)SampleOffsetMillis / Delta;

	switch(pCurrentPoint->m_Curvetype)
	{
	case CURVETYPE_STEP:
		a = 0.0f;
		break;

	case CURVETYPE_SLOW:
		a = a * a * a;
		break;

	case CURVETYPE_FAST:
		a = 1.0f - a;
		a = 1.0f - a * a * a;
		break;

	case CURVETYPE_SMOOTH:
		a = -2.0f * a * a * a + 3.0f * a * a; // second hermite basis
		break;

	case CURVETYPE_BEZIER:
	{
		const CEnvPointBezier *pCurrentPointBezier = pPoints->GetBezier(FoundIndex);
		const CEnvPointBezier *pNextPointBezier = pPoints->GetBezier(FoundIndex + 1);
		if(pCurrentPointBezier == nullptr || pNextPointBezier == nullptr)
			break; // fallback to linear
		// 段内时间用双精度，避免相近的关键帧在较大的绝对毫秒值下重合。
		const double Duration = static_cast<double>(Delta);
		for(size_t c = 0; c < Channels; c++)
		{
			const double Control1 = std::clamp(static_cast<double>(pCurrentPointBezier->m_aOutTangentDeltaX[c].GetInternal()), 0.0, Duration);
			const double Control2 = std::clamp(Duration + pNextPointBezier->m_aInTangentDeltaX[c].GetInternal(), 0.0, Duration);
			const float Parameter = SolveBezier(SampleOffsetMillis, Control1, Control2, Duration);

			const float StartValue = fx2f(pCurrentPoint->m_aValues[c]);
			const float EndValue = fx2f(pNextPoint->m_aValues[c]);
			Result[c] = bezier(StartValue, StartValue + fx2f(pCurrentPointBezier->m_aOutTangentDeltaY[c]),
				EndValue + fx2f(pNextPointBezier->m_aInTangentDeltaY[c]), EndValue, Parameter);
		}
		return;
	}

	case CURVETYPE_LINEAR: [[fallthrough]];
	default:
		break;
	}

	for(size_t c = 0; c < Channels; c++)
	{
		const float v0 = fx2f(pCurrentPoint->m_aValues[c]);
		const float v1 = fx2f(pNextPoint->m_aValues[c]);
		Result[c] = v0 + (v1 - v0) * a;
	}
}
