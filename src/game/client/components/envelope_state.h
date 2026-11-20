#ifndef GAME_CLIENT_COMPONENTS_ENVELOPE_STATE_H
#define GAME_CLIENT_COMPONENTS_ENVELOPE_STATE_H

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/map/render_interfaces.h>
#include <game/map/render_map.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace EnvelopeStateTime
{
	inline std::optional<std::chrono::nanoseconds> OnlineSampleTime(int RoundStartTick, int CurrentTick, int PrevTick, double IntraTick, bool Predicted, int TimeOffsetMillis)
	{
		using namespace std::chrono;
		using CLimits = std::numeric_limits<nanoseconds::rep>;
		constexpr auto NanosPerTick = nanoseconds(seconds(1)) / static_cast<int64_t>(SERVER_TICK_SPEED);
		const int64_t FirstTick = Predicted ? static_cast<int64_t>(CurrentTick) - 1 : PrevTick;
		const double TickRatio = (static_cast<int64_t>(CurrentTick) - FirstTick) * IntraTick;
		const double FractionalCount = (TickRatio * NanosPerTick).count();
		// 以右开区间同时排除 NaN、无穷和不能转换成整数纳秒的值。
		constexpr double MinCount = static_cast<double>(CLimits::min());
		if(!(FractionalCount >= MinCount && FractionalCount < -MinCount))
			return std::nullopt;

		const auto FractionalNanos = static_cast<nanoseconds::rep>(FractionalCount);
		const auto WholeNanos = ((FirstTick - RoundStartTick) * NanosPerTick + milliseconds(TimeOffsetMillis)).count();
		if((FractionalNanos > 0 && WholeNanos > CLimits::max() - FractionalNanos) ||
			(FractionalNanos < 0 && WholeNanos < CLimits::min() - FractionalNanos))
			return std::nullopt;
		return nanoseconds(WholeNanos + FractionalNanos);
	}
} // namespace EnvelopeStateTime

class CEnvelopeState : public CComponent, public IEnvelopeEval
{
public:
	CEnvelopeState() :
		m_pEnvelopePoints(nullptr), m_pMap(nullptr) {}
	CEnvelopeState(IMap *pMap, bool OnlineOnly);
	void EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const override;
	// 用调用方给出的 tick 代替客户端当前时钟。影子回放必须按播放头求值，
	// 不能继续读主世界的预测 tick。
	void EnvelopeEvalAtTick(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels,
		int Tick, float IntraTick) const;

	int Sizeof() const override { return sizeof(*this); }

private:
	void EvalAtTime(int EnvelopeIndex, ColorRGBA &Result, size_t Channels,
		std::chrono::nanoseconds Time) const;
	std::shared_ptr<CMapBasedEnvelopePointAccess> m_pEnvelopePoints;
	IMap *m_pMap = nullptr;
	bool m_OnlineOnly = false;
};

#endif
