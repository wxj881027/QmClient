#include "collision_hitbox.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/pickup_data.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

// 地图层类型
enum class HitboxLayer
{
	GAME,
	FRONT
};

class CHitboxLayer
{
private:
	CMapItemLayerTilemap *GetLayer(CGameClient *pThis) const
	{
		if(m_Type == HitboxLayer::GAME)
			return pThis->Layers()->GameLayer();
		if(m_Type == HitboxLayer::FRONT)
			return pThis->Layers()->FrontLayer();
		return nullptr;
	}
	int GetLayerData(CGameClient *pThis) const
	{
		if(m_Type == HitboxLayer::GAME)
			return pThis->Layers()->GameLayer()->m_Data;
		if(m_Type == HitboxLayer::FRONT)
			return pThis->Layers()->FrontLayer()->m_Front;
		return -1;
	}

public:
	const HitboxLayer m_Type;

	void GetMeta(CGameClient *pThis, ivec2 &Size) const
	{
		Size = {0, 0};
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const int DataSize = pThis->Layers()->Map()->GetDataSize(GetLayerData(pThis));
		if(DataSize <= 0 || (size_t)DataSize < (size_t)pLayer->m_Width * (size_t)pLayer->m_Height * sizeof(CTile))
			return;
		Size = {pLayer->m_Width, pLayer->m_Height};
	}

	void SetData(CGameClient *pThis, int *pData, const ivec2 &Size) const
	{
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const auto *pTiles = (CTile *)pThis->Layers()->Map()->GetData(GetLayerData(pThis));
		if(!pTiles)
			return;

		for(int y = 0; y < pLayer->m_Height; ++y)
		{
			for(int x = 0; x < pLayer->m_Width; ++x)
			{
				const int Index = y * pLayer->m_Width + x;
				const int IndexOut = y * Size.x + x;
				const auto Tile = pTiles[Index].m_Index;

				// 按优先级设置碰撞类型
				if(Tile == TILE_SOLID || Tile == TILE_NOHOOK)
				{
					if(pData[IndexOut] < HITBOX_SOLID)
						pData[IndexOut] = HITBOX_SOLID;
				}
				else if(Tile == TILE_FREEZE || Tile == TILE_LFREEZE)
				{
					if(pData[IndexOut] < HITBOX_FREEZE)
						pData[IndexOut] = HITBOX_FREEZE;
				}
				else if(Tile == TILE_DFREEZE)
				{
					if(pData[IndexOut] < HITBOX_DFREEZE)
						pData[IndexOut] = HITBOX_DFREEZE;
				}
				else if(Tile == TILE_DEATH)
				{
					if(pData[IndexOut] < HITBOX_DEATH)
						pData[IndexOut] = HITBOX_DEATH;
				}
			}
		}
	}
};

// 地图层顺序
static constexpr CHitboxLayer HITBOX_LAYERS[] = {{HitboxLayer::GAME}, {HitboxLayer::FRONT}};

void CCollisionHitbox::OnMapLoad()
{
	if(m_pMapData)
	{
		delete[] m_pMapData;
		m_pMapData = nullptr;
	}

	// 查找有效的图层并计算尺寸
	std::vector<const CHitboxLayer *> vValidLayers;
	m_MapDataSize = {0, 0};
	for(const auto &Layer : HITBOX_LAYERS)
	{
		ivec2 LayerSize;
		Layer.GetMeta(GameClient(), LayerSize);
		if(LayerSize.x <= 0 || LayerSize.y <= 0)
			continue;
		m_MapDataSize.x = std::max(m_MapDataSize.x, LayerSize.x);
		m_MapDataSize.y = std::max(m_MapDataSize.y, LayerSize.y);
		vValidLayers.push_back(&Layer);
	}

	if(m_MapDataSize.x <= 0 || m_MapDataSize.y <= 0)
		return;

	m_pMapData = new int[m_MapDataSize.x * m_MapDataSize.y]();
	for(int i = 0; i < m_MapDataSize.x * m_MapDataSize.y; ++i)
		m_pMapData[i] = HITBOX_NONE;

	// 填充碰撞数据
	for(const auto *pLayer : vValidLayers)
	{
		pLayer->SetData(GameClient(), m_pMapData, m_MapDataSize);
	}
}

void CCollisionHitbox::RenderTileHitboxes()
{
	if(!m_pMapData)
		return;

	const float Scale = 32.0f;
	const float Alpha = g_Config.m_QmCollisionHitboxAlpha / 100.0f;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	// 限制渲染范围避免性能问题
	int MaxScale = 12;
	if(EndX - StartX > Graphics()->ScreenWidth() / MaxScale || EndY - StartY > Graphics()->ScreenHeight() / MaxScale)
	{
		int EdgeX = (EndX - StartX) - (Graphics()->ScreenWidth() / MaxScale);
		StartX += EdgeX / 2;
		EndX -= EdgeX / 2;
		int EdgeY = (EndY - StartY) - (Graphics()->ScreenHeight() / MaxScale);
		StartY += EdgeY / 2;
		EndY -= EdgeY / 2;
	}

	auto GetTile = [&](int x, int y) -> int {
		if(x < 0 || x >= m_MapDataSize.x || y < 0 || y >= m_MapDataSize.y)
			return HITBOX_NONE;
		return m_pMapData[y * m_MapDataSize.x + x];
	};

	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			const int Type = GetTile(x, y);
			if(Type == HITBOX_NONE)
				continue;

			// 只检查freeze与death类型
			bool Enabled = false;
			ColorRGBA RgbaColor;

			if(Type == HITBOX_FREEZE)
			{
				Enabled = true;
				RgbaColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmCollisionHitboxColorFreeze));
				RgbaColor.a = Alpha;
			}
			else if(Type == HITBOX_DEATH)
			{
				Enabled = true;
				RgbaColor = ColorRGBA(0.0f, 0.0f, 0.0f, Alpha);
			}
			else
			{
				continue; // 跳过其他类型
			}

			// 设置颜色
			Graphics()->SetColor(RgbaColor);

			// 计算tile边界
			float TileX = x * Scale;
			float TileY = y * Scale;

			// 检查邻居 - 只有当邻居不是相同类型时才绘制边界
			// 这样就能绘制出最外层的线，表示碰撞发生的边界
			bool LeftNeighbor = (GetTile(x - 1, y) == Type);
			bool RightNeighbor = (GetTile(x + 1, y) == Type);
			bool TopNeighbor = (GetTile(x, y - 1) == Type);
			bool BottomNeighbor = (GetTile(x, y + 1) == Type);

			// 绘制边框线（只绘制最外层边界）
			// 当Tee的圆形碰撞体积与这些边界相交时，就会触发效果
			IGraphics::CLineItem aLines[4];
			int NumLines = 0;

			// 上边 - Tee从上方接触此边界时会被freeze
			if(!TopNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX, TileY, TileX + Scale, TileY);
			// 下边 - Tee从下方接触此边界时会被freeze
			if(!BottomNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX, TileY + Scale, TileX + Scale, TileY + Scale);
			// 左边 - Tee从左侧接触此边界时会被freeze
			if(!LeftNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX, TileY, TileX, TileY + Scale);
			// 右边 - Tee从右侧接触此边界时会被freeze
			if(!RightNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX + Scale, TileY, TileX + Scale, TileY + Scale);

			if(NumLines > 0)
				Graphics()->LinesDraw(aLines, NumLines);
		}
	}

	Graphics()->LinesEnd();
}

void CCollisionHitbox::RenderTeeHitboxes()
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float Alpha = g_Config.m_QmCollisionHitboxAlpha / 100.0f;
	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	const float PointSize = 3.0f;
	const float SampleOffset = CCharacterCore::PhysicalSize() / 3.0f;

	auto IsFreezeTile = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	};

	auto DrawCross = [&](const vec2 &Pos, const ColorRGBA &Color) {
		Graphics()->SetColor(Color);
		IGraphics::CLineItem CrossLines[2] = {
			{Pos.x - PointSize, Pos.y, Pos.x + PointSize, Pos.y},
			{Pos.x, Pos.y - PointSize, Pos.x, Pos.y + PointSize}};
		Graphics()->LinesDraw(CrossLines, 2);
	};

	for(const auto &Player : GameClient()->m_aClients)
	{
		const int ClientId = Player.ClientId();
		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active || !Player.m_Active)
			continue;
		if(Player.m_Team < 0)
			continue;

		// 检查是否在屏幕范围内
		if(!(Player.m_RenderPos.x >= ScreenX0 && Player.m_RenderPos.x <= ScreenX1 &&
			   Player.m_RenderPos.y >= ScreenY0 && Player.m_RenderPos.y <= ScreenY1))
			continue;

		float PlayerAlpha = Alpha;
		if(GameClient()->IsOtherTeam(ClientId))
			PlayerAlpha *= (float)g_Config.m_ClShowOthersAlpha / 100.0f;

		if(PlayerAlpha <= 0.0f)
			continue;

		vec2 Position = Player.m_RenderPos;

		// Freeze: center sample (tile-based)
		const int Index = Collision()->GetPureMapIndex(Position);
		const int Tile = Collision()->GetTileIndex(Index);
		const int FrontTile = Collision()->GetFrontTileIndex(Index);
		const int SwitchTile = Collision()->GetSwitchType(Index);
		const bool FreezeHit = IsFreezeTile(Tile) || IsFreezeTile(FrontTile) || IsFreezeTile(SwitchTile);
		const float FreezeAlpha = FreezeHit ? PlayerAlpha : PlayerAlpha * 0.35f;
		ColorRGBA FreezeColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmCollisionHitboxColorFreeze));
		FreezeColor.a = FreezeAlpha;
		DrawCross(Position, FreezeColor);

		// Death: 4-point samples (collision-based)
		const vec2 DeathOffsets[4] = {
			{SampleOffset, SampleOffset},
			{SampleOffset, -SampleOffset},
			{-SampleOffset, SampleOffset},
			{-SampleOffset, -SampleOffset}};
		for(const vec2 &Offset : DeathOffsets)
		{
			const vec2 SamplePos = Position + Offset;
			const bool DeathHit = Collision()->GetCollisionAt(SamplePos.x, SamplePos.y) == TILE_DEATH ||
					      Collision()->GetFrontCollisionAt(SamplePos.x, SamplePos.y) == TILE_DEATH;
			const float DeathAlpha = DeathHit ? PlayerAlpha : PlayerAlpha * 0.35f;
			DrawCross(SamplePos, ColorRGBA(0.0f, 0.0f, 0.0f, DeathAlpha));
		}
	}

	Graphics()->LinesEnd();
}

void CCollisionHitbox::RenderPickupHitboxes()
{
	const float Alpha = g_Config.m_QmCollisionHitboxAlpha / 100.0f;
	if(Alpha <= 0.0f)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float PickupRadius = 14.0f + 6.0f; // pickup phys size + extra collision size
	const ColorRGBA ShieldColor(1.0f, 1.0f, 0.0f, Alpha);

	const bool IsSuper = GameClient()->IsLocalCharSuper();
	const int SwitcherTeam = GameClient()->SwitchStateTeam();
	auto &aSwitchers = GameClient()->Switchers();
	const int Ticks = Client()->GameTick(g_Config.m_ClDummy) % Client()->GameTickSpeed();
	const bool BlinkingPickup = (Ticks % 22) < 4;

	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	for(const CSnapEntities &Ent : GameClient()->SnapEntities())
	{
		const IClient::CSnapItem Item = Ent.m_Item;
		if(Item.m_Type != NETOBJTYPE_PICKUP && Item.m_Type != NETOBJTYPE_DDNETPICKUP)
			continue;

		const CPickupData Data = ExtractPickupInfo(Item.m_Type, Item.m_pData, Ent.m_pDataEx);
		if(Data.m_Type != POWERUP_ARMOR)
			continue;

		const bool Inactive = !IsSuper && Data.m_SwitchNumber > 0 && Data.m_SwitchNumber < (int)aSwitchers.size() &&
				      !aSwitchers[Data.m_SwitchNumber].m_aStatus[SwitcherTeam];
		if(Inactive && BlinkingPickup)
			continue;

		if(Data.m_Pos.x + PickupRadius < ScreenX0 || Data.m_Pos.x - PickupRadius > ScreenX1 ||
		   Data.m_Pos.y + PickupRadius < ScreenY0 || Data.m_Pos.y - PickupRadius > ScreenY1)
			continue;

		Graphics()->SetColor(ShieldColor);
		const float Left = Data.m_Pos.x - PickupRadius;
		const float Right = Data.m_Pos.x + PickupRadius;
		const float Top = Data.m_Pos.y - PickupRadius;
		const float Bottom = Data.m_Pos.y + PickupRadius;
		IGraphics::CLineItem aLines[4] = {
			{Left, Top, Right, Top},
			{Left, Bottom, Right, Bottom},
			{Left, Top, Left, Bottom},
			{Right, Top, Right, Bottom}};
		Graphics()->LinesDraw(aLines, 4);
	}

	Graphics()->LinesEnd();
}

void CCollisionHitbox::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!g_Config.m_QmShowCollisionHitbox)
		return;

	// 绘制地图tile的碰撞体积
	RenderTileHitboxes();

	// 绘制Tee的碰撞体积
	RenderTeeHitboxes();

	// 绘制盾牌拾取的碰撞体积
	RenderPickupHitboxes();
}
