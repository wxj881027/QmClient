#ifndef GAME_CLIENT_COMPONENTS_ENVELOPE_STATE_H
#define GAME_CLIENT_COMPONENTS_ENVELOPE_STATE_H

#include <game/client/component.h>
#include <game/map/render_interfaces.h>
#include <game/map/render_map.h>

#include <chrono>
#include <memory>

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
	void EvalAtTime(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels,
		std::chrono::nanoseconds Time) const;
	std::shared_ptr<CMapBasedEnvelopePointAccess> m_pEnvelopePoints;
	IMap *m_pMap;
	bool m_OnlineOnly;
};

#endif
