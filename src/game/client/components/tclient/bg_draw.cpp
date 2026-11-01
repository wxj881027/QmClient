#include "bg_draw.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/external/spt.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/qmclient/bg_draw_persistence.h>
#include <game/client/components/qmclient/bg_draw_quad_containers.h>
#include <game/client/components/qmclient/bg_draw_state.h>
#include <game/client/components/tclient/bg_draw_file.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>
#include <vector>

constexpr float AUTO_SAVE_INTERVAL = 60.0f;

static float cross(vec2 A, vec2 B)
{
	return A.x * B.y - A.y * B.x;
}

static bool line_intersects(vec2 A, vec2 B, vec2 C, vec2 D)
{
	const vec2 R = B - A;
	const vec2 S = D - C;
	const vec2 Ac = C - A;
	const float Denom = cross(R, S);
	const float Num1 = cross(Ac, S);
	const float Num2 = cross(Ac, R);
	if(Denom == 0.0f)
		return false;
	float T = Num1 / Denom;
	float U = Num2 / Denom;
	return (T >= 0.0f && T <= 1.0f) && (U >= 0.0f && U <= 1.0f);
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CPathContainer
{
private:
	std::vector<int> m_vQuadContainerIndexes;
	int m_QuadCount = 0;
	IGraphics &m_Graphics;
	void Clear()
	{
		QmBgDrawClearQuadContainers(m_Graphics, m_vQuadContainerIndexes, m_QuadCount);
	}
	void Upload()
	{
		if(!m_vQuadContainerIndexes.empty())
			m_Graphics.QuadContainerUpload(m_vQuadContainerIndexes.back());
	}
	void AddQuads(IGraphics::CFreeformItem *pFreeformItem, int Count, ColorRGBA Color)
	{
		QmBgDrawAppendQuads(m_Graphics, m_vQuadContainerIndexes, m_QuadCount, pFreeformItem, Count, Color);
	}
	void AddCircle(vec2 Pos, float Angle1, float Angle2, float Width, ColorRGBA Color)
	{
		const float Radius = Width / 2.0f;
		const int Segments = std::clamp((int)(std::fabs(Angle2 - Angle1) / (pi / 8.0f)), 1, 20);
		const float SegmentAngle = (Angle2 - Angle1) / Segments;
		IGraphics::CFreeformItem FreeformItems[20];
		for(int i = 0; i < Segments; ++i)
		{
			const float A1 = Angle1 + (float)i * SegmentAngle;
			const float A2 = A1 + SegmentAngle;
			const vec2 P1 = Pos + direction(A1) * Radius;
			const vec2 P2 = Pos + direction(A2) * Radius;
			FreeformItems[i] = IGraphics::CFreeformItem(P1.x, P1.y, P2.x, P2.y, Pos.x, Pos.y, Pos.x, Pos.y);
		}
		AddQuads(FreeformItems, Segments, Color);
	}

public:
	CPathContainer(IGraphics &Graphics) :
		m_Graphics(Graphics) {}
	void Update(const CBgDrawItemData &Data)
	{
		Clear();
		if(Data.size() == 0)
		{
			return;
		}
		size_t i = 0;
		auto SPTGetPt = [&](SPT_pt *pPt) {
			if(i >= Data.size())
				return false;
			pPt->color = {Data[i].r, Data[i].g, Data[i].b, Data[i].a};
			pPt->pos = {Data[i].x, Data[i].y};
			pPt->w = Data[i].w;
			i += 1;
			return true;
		};
		auto SPTAddQuad = [&](SPT_quad Quad, SPT_color Color) {
			IGraphics::CFreeformItem FreeformItem(
				Quad.a.x, Quad.a.y,
				Quad.b.x, Quad.b.y,
				Quad.d.x, Quad.d.y,
				Quad.c.x, Quad.c.y);
			AddQuads(&FreeformItem, 1, {Color.r, Color.g, Color.b, Color.a});
			return true;
		};
		auto SPTAddArc = [&](SPT_vec2 Pos, float A1, float A2, float R, SPT_color Color) {
			AddCircle({Pos.x, Pos.y}, A1, A2, R * 2.0f, {Color.r, Color.g, Color.b, Color.a});
			return true;
		};
		SPT_prims(SPTGetPt, SPTAddQuad, SPTAddArc);
		Upload();
	}
	void Render()
	{
		for(int QuadContainerIndex : m_vQuadContainerIndexes)
			m_Graphics.RenderQuadContainer(QuadContainerIndex, -1);
	}
	~CPathContainer()
	{
		Clear();
	}
};

class CBgDrawItem
{
private:
	CGameClient &m_This;

	CPathContainer m_PathContainer;
	CQmBgDrawStroke m_Stroke;

	float CurrentWidth() const
	{
		return (float)g_Config.m_QmBgDrawWidth * m_This.m_Camera.m_Zoom;
	}
	ColorRGBA CurrentColor() const
	{
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmBgDrawColor));
	}

public:
	bool m_Killed = false;
	float m_SecondsAge = 0.0f;

	const CBgDrawItemData &Data() const { return m_Stroke.Data(); }
	vec2 Min() const { return m_Stroke.Min(); }
	vec2 Max() const { return m_Stroke.Max(); }

	void PenUp() { m_Stroke.PenUp(); }
	bool MoveTo(vec2 Pos)
	{
		if(!m_Stroke.MoveTo({Pos, CurrentWidth(), CurrentColor()}))
			return false;
		m_PathContainer.Update(Data());
		return true;
	}
	bool PointIntersect(const vec2 Pos, const float Radius) const
	{
		const auto &vData = Data();
		if(vData.empty())
			return true;
		if(vData.size() == 1)
			return distance(vData[0].Pos(), Pos) < vData[0].w + Radius;
		vec2 C, D = vData[0].Pos();
		for(auto It = std::next(vData.begin()); It != vData.end(); ++It)
		{
			const CBgDrawItemDataPoint &Point = *It;
			C = D;
			D = Point.Pos();
			vec2 Closest;
			if(!closest_point_on_line(C, D, Pos, Closest))
				continue;
			if(distance(Closest, Pos) < Radius + Point.w)
				return true;
		}
		return false;
	}
	bool LineIntersect(const vec2 A, const vec2 B) const
	{
		const auto &vData = Data();
		if(vData.empty())
			return true;
		if(vData.size() == 1)
		{
			vec2 P = vData[0].Pos();
			vec2 Closest;
			if(!closest_point_on_line(A, B, P, Closest))
				return false;
			return distance(Closest, P) < vData[0].w;
		}
		vec2 C, D = vData[0].Pos();
		for(auto It = std::next(vData.begin()); It != vData.end(); ++It)
		{
			const CBgDrawItemDataPoint &Point = *It;
			C = D;
			D = Point.Pos();
			if(line_intersects(A, B, C, D))
				return true;
		}
		return false;
	}
	void Render()
	{
		m_PathContainer.Render();
	}

	CBgDrawItem() = delete;
	CBgDrawItem(CGameClient &This, vec2 StartPos) :
		m_This(This), m_PathContainer(*This.Graphics()), m_Stroke(CBgDrawItemDataPoint(StartPos, CurrentWidth(), CurrentColor()))
	{
		m_PathContainer.Update(Data());
	}
	CBgDrawItem(CGameClient &This, const CBgDrawItemData &Data) :
		m_This(This), m_PathContainer(*This.Graphics()), m_Stroke(Data)
	{
		m_PathContainer.Update(this->Data());
	}
};

static void DoInput(CBgDraw &This, CBgDraw::InputMode Mode, int Value)
{
	CBgDraw::InputMode &Input = This.m_aInputData[g_Config.m_ClDummy];
	if(Value > 0)
		Input = Mode;
	else if(Input == Mode)
		Input = CBgDraw::InputMode::NONE;
}

void CBgDraw::ConBgDraw(IConsole::IResult *pResult, void *pUserData)
{
	DoInput(*(CBgDraw *)pUserData, InputMode::DRAW, pResult->GetInteger(0));
}

void CBgDraw::ConBgDrawErase(IConsole::IResult *pResult, void *pUserData)
{
	DoInput(*(CBgDraw *)pUserData, InputMode::ERASE, pResult->GetInteger(0));
}

void CBgDraw::ConBgDrawReset(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Reset();
}

void CBgDraw::ConBgDrawSave(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Save(pResult->GetString(0), true);
}

void CBgDraw::ConBgDrawLoad(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Load(pResult->GetString(0), true);
}

static void BgDrawFilename(CGameClient &This, const char *pFilename, char (&aFilename)[IO_MAX_PATH_LENGTH])
{
	if(pFilename && pFilename[0] != '\0')
	{
		if(str_endswith_nocase(pFilename, ".csv"))
			str_format(aFilename, sizeof(aFilename), "bgdraw/%s", pFilename);
		else
			str_format(aFilename, sizeof(aFilename), "bgdraw/%s.csv", pFilename);
	}
	else
	{
		SHA256_DIGEST Sha256 = This.Map()->Sha256();
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(Sha256, aSha256, sizeof(aSha256));
		str_format(aFilename, sizeof(aFilename), "bgdraw/%s_%s.csv", This.Client()->GetCurrentMap(), aSha256);
	}
}

bool CBgDraw::Save(const char *pFilename, bool Verbose)
{
	if(!m_MapActive)
		return false;

	if(!m_Dirty)
	{
		if(Verbose)
			GameClient()->Echo(Localize("No changes since last save"));
		return false;
	}
	char aFilename[IO_MAX_PATH_LENGTH];
	BgDrawFilename(*GameClient(), pFilename, aFilename);
	const auto Result = qm_bg_draw_persistence::Save(Storage(), aFilename, *m_pvItems);
	if(Result.m_Success)
		m_Dirty = false;
	char aMsg[256];
	if(Result.m_FolderFailed)
		GameClient()->Echo(Localize("Failed to create bgdraw folder"));
	else if(Result.m_WriteFailed)
	{
		str_format(aMsg, sizeof(aMsg), Localize("Writing item %d failed"), Result.m_Written);
		GameClient()->Echo(aMsg);
	}
	else if(!Result.m_Success)
	{
		str_format(aMsg, sizeof(aMsg), "%s: %s", Localize("Error"), aFilename);
		GameClient()->Echo(aMsg);
	}
	if((Result.m_Success && Verbose) || Result.m_WriteFailed)
	{
		str_format(aMsg, sizeof(aMsg), Localize("Written %d items"), Result.m_Written);
		GameClient()->Echo(aMsg);
	}
	return Result.m_Success;
}

bool CBgDraw::Load(const char *pFilename, bool Verbose)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	int ItemsLoaded = 0;
	int ItemsDiscarded = 0;
	try
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		BgDrawFilename(*GameClient(), pFilename, aFilename);
		qm_bg_draw_persistence::SLoadedData Loaded;
		if(!qm_bg_draw_persistence::Read(Storage(), aFilename, g_Config.m_QmBgDrawMaxItems, Loaded))
			return false;
		m_Dirty |= QmBgDrawAppendLoaded(*m_pvItems, m_apActiveItems, g_Config.m_QmBgDrawMaxItems, Loaded.m_Queue, [&](auto &Candidate, const auto &Data) {
			Candidate.emplace_back(*GameClient(), Data);
		});
		ItemsLoaded = Loaded.m_Loaded;
		ItemsDiscarded = Loaded.m_Discarded;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	if(Verbose)
	{
		char aInfo[256];
		if(ItemsDiscarded == 0)
			str_format(aInfo, sizeof(aInfo), Localize("Loaded %d items"), ItemsLoaded);
		else
			str_format(aInfo, sizeof(aInfo), Localize("Loaded %d items (discarded %d items)"), ItemsLoaded - ItemsDiscarded, ItemsDiscarded);
		GameClient()->Echo(aInfo);
	}

	return true;
}

template<typename... T>
CBgDrawItem *CBgDraw::AddItem(T &&...Aargs)
{
	return QmBgDrawAddItem(*m_pvItems, m_apActiveItems, g_Config.m_QmBgDrawMaxItems, m_Dirty, std::forward<T>(Aargs)...);
}

void CBgDraw::MakeSpaceFor(int Count)
{
	if(QmBgDrawMakeSpace(*m_pvItems, m_apActiveItems, g_Config.m_QmBgDrawMaxItems, Count))
		m_Dirty = true;
}

void CBgDraw::OnConsoleInit()
{
	Console()->Register("+bg_draw", "", CFGFLAG_CLIENT, ConBgDraw, this, "Draw on the in game background");
	Console()->Register("+bg_draw_erase", "", CFGFLAG_CLIENT, ConBgDrawErase, this, "Erase items on the in game background");
	Console()->Register("bg_draw_reset", "", CFGFLAG_CLIENT, ConBgDrawReset, this, "Reset all drawings on the background");
	Console()->Register("bg_draw_save", "?r[filename]", CFGFLAG_CLIENT, ConBgDrawSave, this, "Save drawings to a given csv file (defaults to map name)");
	Console()->Register("bg_draw_load", "?r[filename]", CFGFLAG_CLIENT, ConBgDrawLoad, this, "Load drawings from a given csv file (defaults to map name)");
}

void CBgDraw::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	Graphics()->TextureClear();

	float Delta = Client()->RenderFrameTime();

	m_NextAutoSave -= Delta;
	if(m_NextAutoSave < 0)
	{
		if(g_Config.m_QmBgDrawAutoSaveLoad)
			Save(nullptr, false);
		m_NextAutoSave = AUTO_SAVE_INTERVAL;
	}

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Handle updating active item
		const InputMode Input = m_aInputData[Dummy];
		CBgDrawItem *&ActiveItem = m_apActiveItems[Dummy];
		vec2 Pos;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && Dummy == g_Config.m_ClDummy)
			Pos = GameClient()->m_Camera.m_Center;
		else
			Pos = (GameClient()->m_Controls.m_aTargetPos[Dummy] - GameClient()->m_Camera.m_Center) * GameClient()->m_Camera.m_Zoom + GameClient()->m_Camera.m_Center;
		if(Input == InputMode::DRAW)
		{
			if(ActiveItem != nullptr)
				m_Dirty |= ActiveItem->MoveTo(Pos);
			else
				ActiveItem = AddItem(*GameClient(), Pos);
		}
		else if(ActiveItem != nullptr)
		{
			ActiveItem->PenUp();
			ActiveItem = nullptr;
		}
		std::optional<vec2> &LastPos = m_aLastPos[Dummy];
		if(Input == InputMode::ERASE)
		{
			if(LastPos.has_value())
			{
				for(CBgDrawItem &Item : *m_pvItems)
					if(Item.LineIntersect(*LastPos, Pos))
						Item.m_Killed = true;
			}
			else
			{
				for(CBgDrawItem &Item : *m_pvItems)
					if(Item.PointIntersect(Pos, 2.0f))
						Item.m_Killed = true;
			}
			LastPos = Pos;
		}
		else
		{
			LastPos = std::nullopt;
		}
	}
	// Remove extra items
	MakeSpaceFor(0);
	// Update age of items, delete old items, render items
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	for(CBgDrawItem &Item : *m_pvItems)
	{
		// If this item is currently active
		if(std::any_of(std::begin(m_apActiveItems), std::end(m_apActiveItems), [&](const CBgDrawItem *pActiveItem) {
			   return pActiveItem == &Item;
		   }))
		{
			Item.m_SecondsAge = 0.0f;
		}
		else
		{
			Item.m_SecondsAge += Delta;
			if(g_Config.m_QmBgDrawFadeTime > 0 && Item.m_SecondsAge > (float)g_Config.m_QmBgDrawFadeTime)
				Item.m_Killed = true;
		}
		const bool InRangeX = Item.Min().x < ScreenX1 && Item.Max().x > ScreenX0;
		const bool InRangeY = Item.Min().y < ScreenY1 && Item.Max().y > ScreenY0;
		if(InRangeX && InRangeY)
			Item.Render();
	}
	for(CBgDrawItem *&ActiveItem : m_apActiveItems)
	{
		if(ActiveItem != nullptr && ActiveItem->m_Killed)
			ActiveItem = nullptr;
	}
	// Remove killed items
	if(m_pvItems->remove_if([&](CBgDrawItem &Item) { return Item.m_Killed; }))
		m_Dirty = true;
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

void CBgDraw::Reset()
{
	m_aInputData.fill(InputMode::NONE);
	m_aLastPos.fill(std::nullopt);
	m_apActiveItems.fill(nullptr);
	m_Dirty |= !m_pvItems->empty();
	m_pvItems->clear();
}

void CBgDraw::OnMapLoad()
{
}

void CBgDraw::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_DEMOPLAYBACK)
	{
		if(g_Config.m_QmBgDrawAutoSaveLoad > 0)
			Save(nullptr, true);
	}
	Reset();
	m_Dirty = false;
	m_MapActive = NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK;
	m_NextAutoSave = AUTO_SAVE_INTERVAL;
	if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
	{
		if(g_Config.m_QmBgDrawAutoSaveLoad > 0)
			Load(nullptr, false);
	}
}

void CBgDraw::OnShutdown()
{
	Reset();
	m_MapActive = false;
}

CBgDraw::CBgDraw() :
	m_pvItems(new std::list<CBgDrawItem>())
{
}

CBgDraw::~CBgDraw()
{
	delete m_pvItems;
}
