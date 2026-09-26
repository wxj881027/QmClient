#include "moving_tiles.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/modes.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

static void RotatePoint(const vec2 &Center, vec2 &Point, float Rotation)
{
	const vec2 RelativePos = Point - Center;
	Point.x = RelativePos.x * std::cos(Rotation) - RelativePos.y * std::sin(Rotation) + Center.x;
	Point.y = RelativePos.x * std::sin(Rotation) + RelativePos.y * std::cos(Rotation) + Center.y;
}

static bool QuadName(const int *pInts, size_t NumInts, char *pStr, size_t StrSize)
{
	dbg_assert(NumInts > 0, "QuadName: invalid quad name size");
	dbg_assert(StrSize >= NumInts * sizeof(int), "QuadName: target buffer too small");

	size_t StrIndex = 0;
	for(size_t IntIndex = 0; IntIndex < NumInts; IntIndex++)
	{
		const int CurrentInt = pInts[IntIndex];
		pStr[StrIndex++] = ((CurrentInt >> 24) & 0xff) - 128;
		pStr[StrIndex++] = ((CurrentInt >> 16) & 0xff) - 128;
		pStr[StrIndex++] = ((CurrentInt >> 8) & 0xff) - 128;
		pStr[StrIndex++] = (CurrentInt & 0xff) - 128;
	}
	pStr[StrIndex - 1] = '\0';

	if(str_utf8_check(pStr))
		return true;
	pStr[0] = '\0';
	return false;
}

static bool IsMovingWaterImageName(const char *pImageName)
{
	if(pImageName == nullptr || pImageName[0] == '\0')
		return false;

	return str_find_nocase(pImageName, "blackwater") != nullptr ||
	       str_find_nocase(pImageName, "darkwater") != nullptr ||
	       str_find_nocase(pImageName, "black_water") != nullptr ||
	       str_find_nocase(pImageName, "dark_water") != nullptr ||
	       str_find_nocase(pImageName, "water") != nullptr;
}

static bool QuadLayerUsesMovingWaterImage(IMap *pMap, const CMapItemLayerQuads *pQuadLayer)
{
	if(!pMap || !pQuadLayer)
		return false;

	int ImagesStart = 0;
	int ImagesNum = 0;
	pMap->GetType(MAPITEMTYPE_IMAGE, &ImagesStart, &ImagesNum);
	if(pQuadLayer->m_Image < 0 || pQuadLayer->m_Image >= ImagesNum)
		return false;

	const auto *pImage = static_cast<const CMapItemImage_v2 *>(pMap->GetItem(ImagesStart + pQuadLayer->m_Image));
	if(!pImage)
		return false;

	const char *pImageName = pMap->GetDataString(pImage->m_ImageName);
	const bool IsMovingWater = IsMovingWaterImageName(pImageName);
	pMap->UnloadData(pImage->m_ImageName);
	return IsMovingWater;
}

void CMovingTiles::Reset()
{
	m_vQuads.clear();
	m_HasAxiomOrGoresOnlyQuads = false;
}

bool CMovingTiles::ShouldRenderMovingWater() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	const CServerInfo &ServerInfo = Client()->ServerInfo();

	const char *pServerInfoGameType = ServerInfo.m_aGameType;
	const char *pCommunityId = ServerInfo.m_aCommunityId;

	IServerBrowser *pServerBrowser = ServerBrowser();
	const NETADDR *pServerAddr = Client()->ServerAddress();
	const IServerBrowser::CServerEntry *pEntry = pServerBrowser && pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
	if(pEntry)
	{
		if(!pServerInfoGameType || pServerInfoGameType[0] == '\0')
			pServerInfoGameType = pEntry->m_Info.m_aGameType;
		pCommunityId = pEntry->m_Info.m_aCommunityId;
	}
	else if(GameClient()->m_ConnectServerInfo)
	{
		if(!pServerInfoGameType || pServerInfoGameType[0] == '\0')
			pServerInfoGameType = GameClient()->m_ConnectServerInfo->m_aGameType;
		pCommunityId = GameClient()->m_ConnectServerInfo->m_aCommunityId;
	}

	const CCommunity *pCommunity = pServerBrowser && pCommunityId && pCommunityId[0] != '\0' ? pServerBrowser->Community(pCommunityId) : nullptr;
	return ShouldEnableQmMovingWaterTiles(GameClient()->m_GameInfo.m_aGameType, pServerInfoGameType, pCommunityId, pCommunity ? pCommunity->Name() : nullptr);
}

void CMovingTiles::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState && NewState != IClient::STATE_ONLINE)
		Reset();
}

void CMovingTiles::OnMapLoad()
{
	Reset();

	IMap *pMap = Layers()->Map();
	if(!pMap)
		return;

	int GroupsStart;
	int LayersStart;
	int GroupsNum;
	int LayersNum;
	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(pMap->GetItem(GroupsStart + GroupIndex));
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));
			if(pLayer->m_Type != LAYERTYPE_QUADS)
				continue;

			char aLayerName[30];
			CMapItemLayerQuads *pQuadLayer = reinterpret_cast<CMapItemLayerQuads *>(pLayer);
			QuadName(pQuadLayer->m_aName, std::size(pQuadLayer->m_aName), aLayerName, sizeof(aLayerName));

			EQType Type = EQType::NONE;
			bool RequiresAxiomOrGores = false;
			for(size_t NameIndex = 0; NameIndex < std::size(gs_aValidMovingTileQuadNames); NameIndex++)
			{
				if(str_comp(gs_aValidMovingTileQuadNames[NameIndex], aLayerName) != 0)
					continue;

				Type = static_cast<EQType>(NameIndex);
				break;
			}
			if(Type == EQType::NONE && IsMovingWaterImageName(aLayerName))
			{
				Type = EQType::FREEZE;
				RequiresAxiomOrGores = true;
			}
			if(Type == EQType::NONE && QuadLayerUsesMovingWaterImage(pMap, pQuadLayer))
			{
				Type = EQType::FREEZE;
				RequiresAxiomOrGores = true;
			}
			if(Type == EQType::NONE)
				continue;

			if(!m_RenderAbove && (Type == EQType::HOOKABLE || Type == EQType::UNHOOKABLE))
				continue;
			if(m_RenderAbove && Type != EQType::HOOKABLE && Type != EQType::UNHOOKABLE)
				continue;

			CQuad *pQuads = static_cast<CQuad *>(pMap->GetDataSwapped(pQuadLayer->m_Data));
			for(int QuadIndex = 0; QuadIndex < pQuadLayer->m_NumQuads; QuadIndex++)
			{
				CQuadData QuadData;
				QuadData.m_pQuad = &pQuads[QuadIndex];
				QuadData.m_pGroup = pGroup;
				QuadData.m_pLayer = pQuadLayer;
				QuadData.m_Type = Type;
				QuadData.m_RequiresAxiomOrGores = RequiresAxiomOrGores;
				m_HasAxiomOrGoresOnlyQuads = m_HasAxiomOrGoresOnlyQuads || RequiresAxiomOrGores;
				for(int PointIndex = 0; PointIndex < 5; PointIndex++)
					QuadData.m_Pos[PointIndex] = vec2(fx2f(QuadData.m_pQuad->m_aPoints[PointIndex].x), fx2f(QuadData.m_pQuad->m_aPoints[PointIndex].y));
				m_vQuads.push_back(QuadData);
			}
		}
	}

	m_EnvEvaluator = CEnvelopeState(pMap, true);
	m_EnvEvaluator.OnInterfacesInit(GameClient());
}

void CMovingTiles::OnRender()
{
	if(g_Config.m_ClOverlayEntities != 100 || !g_Config.m_TcMovingTilesEntities || m_vQuads.empty())
		return;

	// 保存调用方坐标系，避免地图分组的偏移和视差影响后续名牌等组件。
	float SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1;
	Graphics()->GetScreen(&SavedScreenX0, &SavedScreenY0, &SavedScreenX1, &SavedScreenY1);

	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float Zoom = GameClient()->m_Camera.m_Zoom;
	const bool RenderMovingWater = !m_HasAxiomOrGoresOnlyQuads || ShouldRenderMovingWater();

	auto ApplyGroupState = [&](const CMapItemGroup *pGroup) -> bool {
		Graphics()->ClipDisable();
		if(!pGroup)
			return false;

		if(!g_Config.m_GfxNoclip && pGroup->m_Version >= 2 && pGroup->m_UseClipping)
		{
			Graphics()->MapScreenToGameInterface(Center.x, Center.y, Zoom);

			float ScreenX0;
			float ScreenY0;
			float ScreenX1;
			float ScreenY1;
			Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

			const float ScreenWidth = ScreenX1 - ScreenX0;
			const float ScreenHeight = ScreenY1 - ScreenY0;
			const float Left = pGroup->m_ClipX - ScreenX0;
			const float Top = pGroup->m_ClipY - ScreenY0;
			const float Right = pGroup->m_ClipX + pGroup->m_ClipW - ScreenX0;
			const float Bottom = pGroup->m_ClipY + pGroup->m_ClipH - ScreenY0;

			if(Right < 0.0f || Left > ScreenWidth || Bottom < 0.0f || Top > ScreenHeight)
				return false;

			const int ClipX = (int)std::round(Left * Graphics()->ScreenWidth() / ScreenWidth);
			const int ClipY = (int)std::round(Top * Graphics()->ScreenHeight() / ScreenHeight);
			Graphics()->ClipEnable(
				ClipX,
				ClipY,
				(int)std::round(Right * Graphics()->ScreenWidth() / ScreenWidth) - ClipX,
				(int)std::round(Bottom * Graphics()->ScreenHeight() / ScreenHeight) - ClipY);
		}

		const int ParallaxZoom = std::clamp(maximum(pGroup->m_ParallaxX, pGroup->m_ParallaxY), 0, 100);
		float aPoints[4];
		Graphics()->MapScreenToWorld(
			Center.x, Center.y,
			pGroup->m_ParallaxX, pGroup->m_ParallaxY, (float)ParallaxZoom,
			pGroup->m_OffsetX, pGroup->m_OffsetY,
			Graphics()->GameScreenAspect(), Zoom, aPoints);
		Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);
		return true;
	};

	auto RenderPass = [&]() {
		constexpr float ColorConv = 1.0f / 255.0f;

		size_t QuadStart = 0;
		while(QuadStart < m_vQuads.size())
		{
			const CMapItemGroup *pGroup = m_vQuads[QuadStart].m_pGroup;
			const CMapItemLayerQuads *pLayer = m_vQuads[QuadStart].m_pLayer;
			if(!pLayer)
			{
				QuadStart++;
				continue;
			}

			size_t QuadEnd = QuadStart + 1;
			while(QuadEnd < m_vQuads.size() &&
				m_vQuads[QuadEnd].m_pGroup == pGroup &&
				m_vQuads[QuadEnd].m_pLayer == pLayer)
			{
				QuadEnd++;
			}

			if(!ApplyGroupState(pGroup))
			{
				QuadStart = QuadEnd;
				continue;
			}

			if(pLayer->m_Image >= 0 && pLayer->m_Image < GameClient()->m_MapImages.Num())
				Graphics()->TextureSet(GameClient()->m_MapImages.Get(pLayer->m_Image));
			else
				Graphics()->TextureClear();

			Graphics()->TrianglesBegin();
			for(size_t QuadIndex = QuadStart; QuadIndex < QuadEnd; QuadIndex++)
			{
				const CQuadData &QuadData = m_vQuads[QuadIndex];
				if(QuadData.m_RequiresAxiomOrGores && !RenderMovingWater)
					continue;

				const CQuad *pQuad = QuadData.m_pQuad;
				if(!pQuad)
					continue;

				ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
				m_EnvEvaluator.EnvelopeEval(pQuad->m_ColorEnvOffset, pQuad->m_ColorEnv, Color, 4);
				if(Color.a <= 0.0f)
					continue;

				Graphics()->QuadsSetSubsetFree(
					fx2f(pQuad->m_aTexcoords[0].x), fx2f(pQuad->m_aTexcoords[0].y),
					fx2f(pQuad->m_aTexcoords[1].x), fx2f(pQuad->m_aTexcoords[1].y),
					fx2f(pQuad->m_aTexcoords[2].x), fx2f(pQuad->m_aTexcoords[2].y),
					fx2f(pQuad->m_aTexcoords[3].x), fx2f(pQuad->m_aTexcoords[3].y));

				ColorRGBA PositionEval(0.0f, 0.0f, 0.0f, 0.0f);
				m_EnvEvaluator.EnvelopeEval(pQuad->m_PosEnvOffset, pQuad->m_PosEnv, PositionEval, 3);

				const vec2 Offset(PositionEval.r, PositionEval.g);
				const float Rotation = PositionEval.b / 180.0f * pi + QuadData.m_Angle;

				IGraphics::CColorVertex aColors[4] = {
					IGraphics::CColorVertex(0, ColorRGBA(pQuad->m_aColors[0].r, pQuad->m_aColors[0].g, pQuad->m_aColors[0].b, pQuad->m_aColors[0].a).Multiply(Color).Multiply(ColorConv)),
					IGraphics::CColorVertex(1, ColorRGBA(pQuad->m_aColors[1].r, pQuad->m_aColors[1].g, pQuad->m_aColors[1].b, pQuad->m_aColors[1].a).Multiply(Color).Multiply(ColorConv)),
					IGraphics::CColorVertex(2, ColorRGBA(pQuad->m_aColors[2].r, pQuad->m_aColors[2].g, pQuad->m_aColors[2].b, pQuad->m_aColors[2].a).Multiply(Color).Multiply(ColorConv)),
					IGraphics::CColorVertex(3, ColorRGBA(pQuad->m_aColors[3].r, pQuad->m_aColors[3].g, pQuad->m_aColors[3].b, pQuad->m_aColors[3].a).Multiply(Color).Multiply(ColorConv)),
				};
				Graphics()->SetColorVertex(aColors, std::size(aColors));

				vec2 aPoints[4] = {
					QuadData.m_Pos[0],
					QuadData.m_Pos[1],
					QuadData.m_Pos[2],
					QuadData.m_Pos[3]};

				if(Rotation != 0.0f)
				{
					for(vec2 &Point : aPoints)
						RotatePoint(QuadData.m_Pos[4], Point, Rotation);
				}

				const IGraphics::CFreeformItem Freeform(
					aPoints[0] + Offset,
					aPoints[1] + Offset,
					aPoints[2] + Offset,
					aPoints[3] + Offset);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}
			Graphics()->TrianglesEnd();
			QuadStart = QuadEnd;
		}
	};

	// 移动方块统一走透明通道，避免空的不透明遍历。
	RenderPass();
	Graphics()->ClipDisable();
	Graphics()->MapScreen(SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1);
}
