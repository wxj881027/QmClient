#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MOVING_TILES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MOVING_TILES_H

#include <base/vmath.h>

#include <game/client/component.h>
#include <game/client/components/envelope_state.h>
#include <game/mapitems.h>

#include <vector>

class CQuad;
class CMapItemGroup;
class CMapItemLayerQuads;

enum class EQType
{
	NONE = -1,
	FREEZE,
	UNFREEZE,
	DEATH,
	STOPA,
	CFRM,
	HOOKABLE,
	UNHOOKABLE,
	NUM
};

static constexpr char gs_aValidMovingTileQuadNames[][30] = {
	"QFr",
	"QUnFr",
	"QDeath",
	"QStopa",
	"QCfrm",
	"QHook",
	"QUnHook"};

class CQuadData : CMapItemLayerQuads
{
public:
	CQuad *m_pQuad = nullptr;
	CMapItemGroup *m_pGroup = nullptr;
	CMapItemLayerQuads *m_pLayer = nullptr;
	EQType m_Type = EQType::NONE;
	bool m_RequiresAxiomOrGores = false;
	vec2 m_Pos[5] = {vec2(0, 0)};
	float m_Angle = 0.0f;
};

class CMovingTiles : public CComponent
{
	void Reset();
	bool ShouldRenderMovingWater() const;
	std::vector<CQuadData> m_vQuads;
	bool m_HasAxiomOrGoresOnlyQuads = false;
	bool m_RenderAbove;
	CEnvelopeState m_EnvEvaluator;
	// 影子回放时由 CRankGhost 写入播放头；-1 表示继续使用主世界时钟。
	int m_ViewTick = -1;
	float m_ViewIntraTick = 0.0f;

public:
	explicit CMovingTiles(bool Above) :
		m_RenderAbove(Above)
	{
	}

	void OnStateChange(int NewState, int OldState) override;
	void OnMapLoad() override;
	void OnRender() override;
	void SetViewPlaybackTime(int Tick, float IntraTick)
	{
		m_ViewTick = Tick;
		m_ViewIntraTick = IntraTick;
	}
	void ClearViewPlaybackTime() { m_ViewTick = -1; }

	int Sizeof() const override { return sizeof(*this); }
};

#endif
