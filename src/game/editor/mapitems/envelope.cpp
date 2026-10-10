#include "envelope.h"

#include <base/system.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace std::chrono_literals;

namespace
{
	class CSegmentAccess : public IEnvelopePointAccess
	{
		const CEnvPoint_runtime *m_apPoints[2];

	public:
		CSegmentAccess(const CEnvPoint_runtime *pFirst, const CEnvPoint_runtime *pSecond) :
			m_apPoints{pFirst, pSecond} {}
		int NumPoints() const override { return 2; }
		const CEnvPoint *GetPoint(int Index) const override { return m_apPoints[Index]; }
		const CEnvPointBezier *GetBezier(int Index) const override { return &m_apPoints[Index]->m_Bezier; }
	};

	struct CCurvePoint
	{
		double m_X;
		double m_Y;
		CCurvePoint Mix(const CCurvePoint &Other, double Amount) const
		{
			return {m_X + (Other.m_X - m_X) * Amount, m_Y + (Other.m_Y - m_Y) * Amount};
		}
	};
	using TCurve = std::array<CCurvePoint, 4>;

	// 在时间轴上分割原曲线，避免给其他轴补点时改变原有插值。
	std::pair<TCurve, TCurve> SplitCurve(const TCurve &Curve, double Amount)
	{
		const auto A = Curve[0].Mix(Curve[1], Amount);
		const auto B = Curve[1].Mix(Curve[2], Amount);
		const auto C = Curve[2].Mix(Curve[3], Amount);
		const auto D = A.Mix(B, Amount);
		const auto E = B.Mix(C, Amount);
		const auto F = D.Mix(E, Amount);
		return {{Curve[0], A, D, F}, {F, E, C, Curve[3]}};
	}

	double CurveParameter(const TCurve &Curve, double Time)
	{
		if(Time <= Curve[0].m_X)
			return 0.0;
		if(Time >= Curve[3].m_X)
			return 1.0;
		double Low = 0.0, High = 1.0;
		for(int i = 0; i < 48; ++i)
		{
			const double Mid = (Low + High) * 0.5;
			if(SplitCurve(Curve, Mid).first[3].m_X < Time)
				Low = Mid;
			else
				High = Mid;
		}
		return (Low + High) * 0.5;
	}

	TCurve ChannelCurve(const CEnvelope &Envelope, int Channel, CFixedTime Start, CFixedTime End)
	{
		const CEnvelope::CPoint *pPrevious = nullptr, *pNext = nullptr, *pLast = nullptr;
		for(const auto &Point : Envelope.m_vPoints)
		{
			if(!Point.HasChannel(Channel))
				continue;
			if(pLast == nullptr || Point.m_Time >= pLast->m_Time)
				pLast = &Point;
			if(Point.m_Time <= Start && (pPrevious == nullptr || Point.m_Time >= pPrevious->m_Time))
				pPrevious = &Point;
			if(Point.m_Time > Start && (pNext == nullptr || Point.m_Time < pNext->m_Time))
				pNext = &Point;
		}
		const double X0 = Start.GetInternal(), X3 = End.GetInternal();
		if(pPrevious == nullptr || pNext == nullptr || pPrevious->m_Curvetype == CURVETYPE_STEP)
		{
			const double Value = pPrevious ? pPrevious->m_aValues[Channel] : (pLast ? pLast->m_aValues[Channel] : (Envelope.GetChannels() == 4 ? 1024 : 0));
			return {{{X0, Value}, {X0 + (X3 - X0) / 3, Value}, {X3 - (X3 - X0) / 3, Value}, {X3, Value}}};
		}
		const double A = pPrevious->m_Time.GetInternal(), B = pNext->m_Time.GetInternal();
		const double Y0 = pPrevious->m_aValues[Channel], Y3 = pNext->m_aValues[Channel];
		TCurve Curve{{{A, Y0}, {A + (B - A) / 3, Y0 + (Y3 - Y0) / 3}, {B - (B - A) / 3, Y3 - (Y3 - Y0) / 3}, {B, Y3}}};
		switch(pPrevious->m_Curvetype)
		{
		case CURVETYPE_SLOW:
			Curve[1].m_Y = Curve[2].m_Y = Y0;
			break;
		case CURVETYPE_FAST:
			Curve[1].m_Y = Curve[2].m_Y = Y3;
			break;
		case CURVETYPE_SMOOTH:
			Curve[1].m_Y = Y0;
			Curve[2].m_Y = Y3;
			break;
		case CURVETYPE_BEZIER:
			Curve[1] = {std::clamp(A + pPrevious->m_Bezier.m_aOutTangentDeltaX[Channel].GetInternal(), A, B), Y0 + pPrevious->m_Bezier.m_aOutTangentDeltaY[Channel]};
			Curve[2] = {std::clamp(B + pNext->m_Bezier.m_aInTangentDeltaX[Channel].GetInternal(), A, B), Y3 + pNext->m_Bezier.m_aInTangentDeltaY[Channel]};
			break;
		default:
			break;
		}
		const double Begin = CurveParameter(Curve, X0), Finish = CurveParameter(Curve, X3);
		Curve = SplitCurve(Curve, Finish).first;
		if(Begin > 0.0 && Finish > 0.0)
			Curve = SplitCurve(Curve, Begin / Finish).second;
		Curve[0].m_X = X0;
		Curve[3].m_X = X3;
		return Curve;
	}

	// 校验兼容播放数据，防止其他编辑器修改动画后仍载入过期的独立轨道。
	int RuntimeChecksum(const std::vector<CEnvPoint_runtime> &vPoints)
	{
		uint32_t Hash = 2166136261u;
		const auto Mix = [&](int Value) { Hash = (Hash ^ static_cast<uint32_t>(Value)) * 16777619u; };
		bool PreviousBezier = false;
		for(const auto &Point : vPoints)
		{
			Mix(Point.m_Time.GetInternal());
			Mix(Point.m_Curvetype);
			for(int c = 0; c < CEnvPoint::MAX_CHANNELS; ++c)
			{
				Mix(Point.m_aValues[c]);
				Mix(PreviousBezier ? Point.m_Bezier.m_aInTangentDeltaX[c].GetInternal() : 0);
				Mix(PreviousBezier ? Point.m_Bezier.m_aInTangentDeltaY[c] : 0);
				Mix(Point.m_Curvetype == CURVETYPE_BEZIER ? Point.m_Bezier.m_aOutTangentDeltaX[c].GetInternal() : 0);
				Mix(Point.m_Curvetype == CURVETYPE_BEZIER ? Point.m_Bezier.m_aOutTangentDeltaY[c] : 0);
			}
			PreviousBezier = Point.m_Curvetype == CURVETYPE_BEZIER;
		}
		return static_cast<int>(Hash);
	}
}

void CEnvelope::Eval(float Time, ColorRGBA &Result, size_t Channels, bool Loop) const
{
	Channels = minimum<size_t>(Channels, GetChannels(), CEnvPoint::MAX_CHANNELS);
	int64_t EndMillis = 0;
	for(const auto &Point : m_vPoints)
		EndMillis = std::max<int64_t>(EndMillis, Point.m_Time.GetInternal());
	auto TimeNanos = std::chrono::nanoseconds(static_cast<int64_t>(static_cast<double>(Time) * 1000000000.0));
	if(Loop && EndMillis > 0)
		TimeNanos = std::chrono::nanoseconds(TimeNanos.count() % (EndMillis * 1000000));
	const double LoopedTime = TimeNanos.count() / 1000000000.0;
	for(size_t c = 0; c < Channels; ++c)
	{
		const CPoint *pPrevious = nullptr, *pNext = nullptr, *pLast = nullptr;
		for(const auto &Point : m_vPoints)
		{
			if(!Point.HasChannel(c))
				continue;
			if(pLast == nullptr || Point.m_Time >= pLast->m_Time)
				pLast = &Point;
			const double PointTime = static_cast<double>(Point.m_Time.GetInternal()) / 1000.0;
			if(PointTime <= LoopedTime && (pPrevious == nullptr || Point.m_Time >= pPrevious->m_Time))
				pPrevious = &Point;
			if(PointTime > LoopedTime && (pNext == nullptr || Point.m_Time < pNext->m_Time))
				pNext = &Point;
		}
		if(pPrevious && pNext)
		{
			CSegmentAccess Access(pPrevious, pNext);
			ColorRGBA Value(0, 0, 0, 0);
			CRenderMap::RenderEvalEnvelope(&Access, TimeNanos, Value, c + 1);
			Result[c] = Value[c];
		}
		else if(pPrevious || pLast)
			Result[c] = fx2f((pPrevious ? pPrevious : pLast)->m_aValues[c]);
		else if(!m_vPoints.empty())
			Result[c] = GetChannels() == 4 ? 1.0f : 0.0f;
	}
}

void CEnvelope::AddPoint(CFixedTime Time, std::array<int, CEnvPoint::MAX_CHANNELS> aValues, int ChannelMask)
{
	Time = std::max(Time, CFixedTime(0));
	for(int c = 0; c < GetChannels(); ++c)
	{
		if(!(ChannelMask & (1 << c)))
			continue;
		const auto It = std::find_if(m_vPoints.begin(), m_vPoints.end(), [&](const CPoint &Point) { return Point.HasChannel(c) && Point.m_Time == Time; });
		if(It != m_vPoints.end())
			continue;
		CPoint Point{};
		Point.m_Channel = c;
		Point.m_Time = Time;
		Point.m_Curvetype = CURVETYPE_LINEAR;
		std::copy(aValues.begin(), aValues.end(), Point.m_aValues);
		m_vPoints.push_back(Point);
	}
	Resort();
}

void CEnvelope::ImportPoints(const std::vector<CEnvPoint_runtime> &vPoints)
{
	m_vPoints.clear();
	for(const auto &Source : vPoints)
		for(int c = 0; c < GetChannels(); ++c)
		{
			CPoint Point{};
			static_cast<CEnvPoint_runtime &>(Point) = Source;
			Point.m_Channel = c;
			m_vPoints.push_back(Point);
		}
	Resort();
}

float CEnvelope::EndTime() const
{
	CFixedTime End(0);
	for(const auto &Point : m_vPoints)
		End = std::max(End, Point.m_Time);
	return End.AsSeconds();
}

int CEnvelope::PreviousPoint(int Index, int Channel) const
{
	for(int i = Index - 1; i >= 0; --i)
		if(m_vPoints[i].HasChannel(Channel))
			return i;
	return -1;
}

int CEnvelope::NextPoint(int Index, int Channel) const
{
	for(int i = Index + 1; i < static_cast<int>(m_vPoints.size()); ++i)
		if(m_vPoints[i].HasChannel(Channel))
			return i;
	return -1;
}

CFixedTime CEnvelope::ClampPointTime(int Index, int Channel, CFixedTime Time) const
{
	const int Previous = PreviousPoint(Index, Channel), Next = NextPoint(Index, Channel);
	const CFixedTime Low = Previous < 0 ? CFixedTime(0) : m_vPoints[Previous].m_Time + CFixedTime(1);
	const CFixedTime High = Next < 0 ? CFixedTime(std::numeric_limits<int>::max()) : m_vPoints[Next].m_Time - CFixedTime(1);
	return std::clamp(Time, Low, std::max(Low, High));
}

std::vector<CEnvPoint_runtime> CEnvelope::ExportPoints() const
{
	std::vector<CFixedTime> vTimes;
	for(const auto &Point : m_vPoints)
		vTimes.push_back(Point.m_Time);
	std::sort(vTimes.begin(), vTimes.end());
	vTimes.erase(std::unique(vTimes.begin(), vTimes.end()), vTimes.end());
	if(vTimes.empty())
		return {};

	// 对齐的旧轨道直接合并，保留原始曲线类型和定点数据。
	std::vector<CEnvPoint_runtime> vAligned;
	for(CFixedTime Time : vTimes)
	{
		CEnvPoint_runtime Combined{};
		Combined.m_Time = Time;
		int CurveType = -1;
		bool Complete = true;
		for(int c = 0; c < GetChannels(); ++c)
		{
			const auto It = std::find_if(m_vPoints.begin(), m_vPoints.end(), [&](const CPoint &Point) { return Point.m_Time == Time && Point.HasChannel(c); });
			if(It == m_vPoints.end() || std::count_if(m_vPoints.begin(), m_vPoints.end(), [&](const CPoint &Point) { return Point.m_Time == Time && Point.HasChannel(c); }) != 1 || (CurveType != -1 && It->m_Curvetype != CurveType))
			{
				Complete = false;
				break;
			}
			CurveType = It->m_Curvetype;
			Combined.m_aValues[c] = It->m_aValues[c];
			Combined.m_Bezier.m_aInTangentDeltaX[c] = It->m_Bezier.m_aInTangentDeltaX[c];
			Combined.m_Bezier.m_aInTangentDeltaY[c] = It->m_Bezier.m_aInTangentDeltaY[c];
			Combined.m_Bezier.m_aOutTangentDeltaX[c] = It->m_Bezier.m_aOutTangentDeltaX[c];
			Combined.m_Bezier.m_aOutTangentDeltaY[c] = It->m_Bezier.m_aOutTangentDeltaY[c];
		}
		if(!Complete)
		{
			vAligned.clear();
			break;
		}
		Combined.m_Curvetype = CurveType;
		vAligned.push_back(Combined);
	}
	if(!vAligned.empty())
		return vAligned;
	if(vTimes.front() > CFixedTime(0))
		vTimes.insert(vTimes.begin(), CFixedTime(0));
	if(vTimes.size() == 1)
		vTimes.push_back(vTimes.front() + CFixedTime(1));

	std::vector<CEnvPoint_runtime> vResult;
	for(size_t i = 0; i + 1 < vTimes.size(); ++i)
	{
		CEnvPoint_runtime Left{}, Right{};
		Left.m_Time = vTimes[i];
		Right.m_Time = vTimes[i + 1];
		Left.m_Curvetype = CURVETYPE_BEZIER;
		Right.m_Curvetype = CURVETYPE_LINEAR;
		for(int c = 0; c < GetChannels(); ++c)
		{
			const auto Curve = ChannelCurve(*this, c, Left.m_Time, Right.m_Time);
			Left.m_aValues[c] = static_cast<int>(std::round(Curve[0].m_Y));
			Right.m_aValues[c] = static_cast<int>(std::round(Curve[3].m_Y));
			Left.m_Bezier.m_aOutTangentDeltaX[c] = CFixedTime(std::round(Curve[1].m_X - Curve[0].m_X));
			Left.m_Bezier.m_aOutTangentDeltaY[c] = static_cast<int>(std::round(Curve[1].m_Y)) - Left.m_aValues[c];
			Right.m_Bezier.m_aInTangentDeltaX[c] = CFixedTime(std::round(Curve[2].m_X - Curve[3].m_X));
			Right.m_Bezier.m_aInTangentDeltaY[c] = static_cast<int>(std::round(Curve[2].m_Y)) - Right.m_aValues[c];
		}
		if(!vResult.empty() && std::equal(std::begin(Left.m_aValues), std::end(Left.m_aValues), std::begin(vResult.back().m_aValues)))
		{
			vResult.back().m_Curvetype = Left.m_Curvetype;
			std::copy(std::begin(Left.m_Bezier.m_aOutTangentDeltaX), std::end(Left.m_Bezier.m_aOutTangentDeltaX), std::begin(vResult.back().m_Bezier.m_aOutTangentDeltaX));
			std::copy(std::begin(Left.m_Bezier.m_aOutTangentDeltaY), std::end(Left.m_Bezier.m_aOutTangentDeltaY), std::begin(vResult.back().m_Bezier.m_aOutTangentDeltaY));
		}
		else
			vResult.push_back(Left);
		// 阶梯跳变保留同一时刻的左右值，DDNet 会选择跳变后的区间。
		vResult.push_back(Right);
	}
	// 最后一轴结束时的终值也用于地图检查和后续编辑。
	auto Final = vResult.back();
	for(int c = 0; c < GetChannels(); ++c)
		for(const auto &Point : m_vPoints)
			if(Point.HasChannel(c) && Point.m_Time == vTimes.back())
				Final.m_aValues[c] = Point.m_aValues[c];
	if(!std::equal(std::begin(Final.m_aValues), std::end(Final.m_aValues), std::begin(vResult.back().m_aValues)))
		vResult.push_back(Final);
	return vResult;
}

std::vector<int> CEnvelope::SerializeChannels(const std::vector<CEnvPoint_runtime> &vRuntimePoints) const
{
	std::vector<int> vData{1, GetChannels(), static_cast<int>(m_vPoints.size()), RuntimeChecksum(vRuntimePoints)};
	for(const auto &Point : m_vPoints)
	{
		const int c = Point.m_Channel;
		dbg_assert(c >= 0 && c < GetChannels(), "independent envelope channel");
		vData.insert(vData.end(), {c, Point.m_Time.GetInternal(), Point.m_Curvetype, Point.m_aValues[c],
						  Point.m_Bezier.m_aInTangentDeltaX[c].GetInternal(), Point.m_Bezier.m_aInTangentDeltaY[c],
						  Point.m_Bezier.m_aOutTangentDeltaX[c].GetInternal(), Point.m_Bezier.m_aOutTangentDeltaY[c]});
	}
	return vData;
}

bool CEnvelope::DeserializeChannels(const int *pData, size_t NumInts, const std::vector<CEnvPoint_runtime> &vRuntimePoints)
{
	if(pData == nullptr || NumInts < 4 || pData[0] != 1 || pData[1] != GetChannels() || pData[2] < 0 ||
		static_cast<size_t>(pData[2]) != (NumInts - 4) / 8 || (NumInts - 4) % 8 != 0 || pData[3] != RuntimeChecksum(vRuntimePoints))
		return false;
	std::vector<CPoint> vPoints;
	std::array<int, CEnvPoint::MAX_CHANNELS> aLastTime{-1, -1, -1, -1};
	for(size_t i = 4; i < NumInts; i += 8)
	{
		const int c = pData[i];
		if(c < 0 || c >= GetChannels() || pData[i + 1] < 0 || pData[i + 1] < aLastTime[c] || pData[i + 2] < 0 || pData[i + 2] >= NUM_CURVETYPES)
			return false;
		aLastTime[c] = pData[i + 1];
		CPoint Point{};
		Point.m_Channel = c;
		Point.m_Time = CFixedTime(pData[i + 1]);
		Point.m_Curvetype = pData[i + 2];
		Point.m_aValues[c] = pData[i + 3];
		Point.m_Bezier.m_aInTangentDeltaX[c] = CFixedTime(pData[i + 4]);
		Point.m_Bezier.m_aInTangentDeltaY[c] = pData[i + 5];
		Point.m_Bezier.m_aOutTangentDeltaX[c] = CFixedTime(pData[i + 6]);
		Point.m_Bezier.m_aOutTangentDeltaY[c] = pData[i + 7];
		vPoints.push_back(Point);
	}
	m_vPoints = std::move(vPoints);
	return true;
}

CEnvelope::CEnvelope(EType Type) :
	m_Type(Type) {}

CEnvelope::CEnvelope(int NumChannels)
{
	switch(NumChannels)
	{
	case 1:
		m_Type = EType::SOUND;
		break;
	case 3:
		m_Type = EType::POSITION;
		break;
	case 4:
		m_Type = EType::COLOR;
		break;
	default:
		dbg_assert_failed("invalid number of channels for envelope");
	}
}

void CEnvelope::Resort()
{
	std::stable_sort(m_vPoints.begin(), m_vPoints.end());
}

std::pair<float, float> CEnvelope::GetValueRange(int ChannelMask)
{
	float Top = -std::numeric_limits<float>::infinity();
	float Bottom = std::numeric_limits<float>::infinity();
	for(size_t PointIndex = 0; PointIndex < m_vPoints.size(); ++PointIndex)
	{
		const auto &Point = m_vPoints[PointIndex];
		for(int c = 0; c < GetChannels(); c++)
		{
			if((ChannelMask & (1 << c)) && Point.HasChannel(c))
			{
				{
					// value handle
					const float v = fx2f(Point.m_aValues[c]);
					Top = maximum(Top, v);
					Bottom = minimum(Bottom, v);
				}

				if(NextPoint(PointIndex, c) >= 0 && Point.m_Curvetype == CURVETYPE_BEZIER)
				{
					// out-tangent handle
					const float v = fx2f(Point.m_aValues[c] + Point.m_Bezier.m_aOutTangentDeltaY[c]);
					Top = maximum(Top, v);
					Bottom = minimum(Bottom, v);
				}

				if(PreviousPoint(PointIndex, c) >= 0 && m_vPoints[PreviousPoint(PointIndex, c)].m_Curvetype == CURVETYPE_BEZIER)
				{
					// in-tangent handle
					const float v = fx2f(Point.m_aValues[c] + Point.m_Bezier.m_aInTangentDeltaY[c]);
					Top = maximum(Top, v);
					Bottom = minimum(Bottom, v);
				}
			}
		}
	}
	return std::isfinite(Bottom) ? std::pair{Bottom, Top} : std::pair{0.0f, 0.0f};
}

int CEnvelope::GetChannels() const
{
	switch(m_Type)
	{
	case EType::POSITION:
		return 3;
	case EType::COLOR:
		return 4;
	case EType::SOUND:
		return 1;
	default:
		dbg_assert_failed("unknown envelope type");
	}
}
