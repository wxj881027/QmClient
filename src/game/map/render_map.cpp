/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "render_map.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/map.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/mapitems.h>
#include <game/mapitems_ex.h>

#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

void CRenderMap::Init(IGraphics *pGraphics, ITextRender *pTextRender)
{
	m_pGraphics = pGraphics;
	m_pTextRender = pTextRender;
}

static void Rotate(const CPoint *pCenter, CPoint *pPoint, float Rotation)
{
	int x = pPoint->x - pCenter->x;
	int y = pPoint->y - pCenter->y;
	pPoint->x = (int)(x * std::cos(Rotation) - y * std::sin(Rotation) + pCenter->x);
	pPoint->y = (int)(x * std::sin(Rotation) + y * std::cos(Rotation) + pCenter->y);
}

void CRenderMap::ForceRenderQuads(CQuad *pQuads, int NumQuads, int RenderFlags, const IEnvelopeEval *pEnvEval, float Alpha)
{
	Graphics()->TrianglesBegin();
	float Conv = 1 / 255.0f;
	for(int i = 0; i < NumQuads; i++)
	{
		CQuad *pQuad = &pQuads[i];

		ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		pEnvEval->EnvelopeEval(pQuad->m_ColorEnvOffset, pQuad->m_ColorEnv, Color, 4);

		if(Color.a <= 0.0f)
			continue;

		bool Opaque = false;
		/* TODO: Analyze quadtexture
		if(a < 0.01f || (q->m_aColors[0].a < 0.01f && q->m_aColors[1].a < 0.01f && q->m_aColors[2].a < 0.01f && q->m_aColors[3].a < 0.01f))
			Opaque = true;
		*/
		if(Opaque && !(RenderFlags & LAYERRENDERFLAG_OPAQUE))
			continue;
		if(!Opaque && !(RenderFlags & LAYERRENDERFLAG_TRANSPARENT))
			continue;

		Graphics()->QuadsSetSubsetFree(
			fx2f(pQuad->m_aTexcoords[0].x), fx2f(pQuad->m_aTexcoords[0].y),
			fx2f(pQuad->m_aTexcoords[1].x), fx2f(pQuad->m_aTexcoords[1].y),
			fx2f(pQuad->m_aTexcoords[2].x), fx2f(pQuad->m_aTexcoords[2].y),
			fx2f(pQuad->m_aTexcoords[3].x), fx2f(pQuad->m_aTexcoords[3].y));

		ColorRGBA Position = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
		pEnvEval->EnvelopeEval(pQuad->m_PosEnvOffset, pQuad->m_PosEnv, Position, 3);
		const vec2 Offset = vec2(Position.r, Position.g);
		const float Rotation = Position.b / 180.0f * pi;

		IGraphics::CColorVertex aColors[4] = {
			IGraphics::CColorVertex(0, ColorRGBA(pQuad->m_aColors[0].r, pQuad->m_aColors[0].g, pQuad->m_aColors[0].b, pQuad->m_aColors[0].a * Alpha).Multiply(Color).Multiply(Conv)),
			IGraphics::CColorVertex(1, ColorRGBA(pQuad->m_aColors[1].r, pQuad->m_aColors[1].g, pQuad->m_aColors[1].b, pQuad->m_aColors[1].a * Alpha).Multiply(Color).Multiply(Conv)),
			IGraphics::CColorVertex(2, ColorRGBA(pQuad->m_aColors[2].r, pQuad->m_aColors[2].g, pQuad->m_aColors[2].b, pQuad->m_aColors[2].a * Alpha).Multiply(Color).Multiply(Conv)),
			IGraphics::CColorVertex(3, ColorRGBA(pQuad->m_aColors[3].r, pQuad->m_aColors[3].g, pQuad->m_aColors[3].b, pQuad->m_aColors[3].a * Alpha).Multiply(Color).Multiply(Conv)),
		};
		Graphics()->SetColorVertex(aColors, std::size(aColors));

		CPoint *pPoints = pQuad->m_aPoints;

		CPoint aRotated[4];
		if(Rotation != 0.0f)
		{
			for(size_t p = 0; p < std::size(aRotated); ++p)
			{
				aRotated[p] = pQuad->m_aPoints[p];
				Rotate(&pQuad->m_aPoints[4], &aRotated[p], Rotation);
			}
			pPoints = aRotated;
		}

		IGraphics::CFreeformItem Freeform(
			fx2f(pPoints[0].x) + Offset.x, fx2f(pPoints[0].y) + Offset.y,
			fx2f(pPoints[1].x) + Offset.x, fx2f(pPoints[1].y) + Offset.y,
			fx2f(pPoints[2].x) + Offset.x, fx2f(pPoints[2].y) + Offset.y,
			fx2f(pPoints[3].x) + Offset.x, fx2f(pPoints[3].y) + Offset.y);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	}
	Graphics()->TrianglesEnd();
}

void CRenderMap::RenderTileRectangle(int RectX, int RectY, int RectW, int RectH,
	unsigned char IndexIn, unsigned char IndexOut,
	float Scale, ColorRGBA Color, int RenderFlags)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// calculate the final pixelsize for the tiles
	float TilePixelSize = 1024 / 32.0f;
	float FinalTileSize = Scale / (ScreenX1 - ScreenX0) * Graphics()->ScreenWidth();
	float FinalTilesetScale = FinalTileSize / TilePixelSize;

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DBegin();
	else
		Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	// adjust the texture shift according to mipmap level
	float TexSize = 1024.0f;
	float Frac = (1.25f / TexSize) * (1 / FinalTilesetScale);
	float Nudge = (0.5f / TexSize) * (1 / FinalTilesetScale);

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			unsigned char Index = (x >= RectX && x < RectX + RectW && y >= RectY && y < RectY + RectH) ? IndexIn : IndexOut;
			if(Index)
			{
				bool Render = false;
				if(RenderFlags & LAYERRENDERFLAG_TRANSPARENT)
					Render = true;

				if(Render)
				{
					int tx = Index % 16;
					int ty = Index / 16;
					int Px0 = tx * (1024 / 16);
					int Py0 = ty * (1024 / 16);
					int Px1 = Px0 + (1024 / 16) - 1;
					int Py1 = Py0 + (1024 / 16) - 1;

					float x0 = Nudge + Px0 / TexSize + Frac;
					float y0 = Nudge + Py0 / TexSize + Frac;
					float x1 = Nudge + Px1 / TexSize - Frac;
					float y1 = Nudge + Py0 / TexSize + Frac;
					float x2 = Nudge + Px1 / TexSize - Frac;
					float y2 = Nudge + Py1 / TexSize - Frac;
					float x3 = Nudge + Px0 / TexSize + Frac;
					float y3 = Nudge + Py1 / TexSize - Frac;

					if(Graphics()->HasTextureArraysSupport())
					{
						x0 = 0;
						y0 = 0;
						x1 = x0 + 1;
						y1 = y0;
						x2 = x0 + 1;
						y2 = y0 + 1;
						x3 = x0;
						y3 = y0 + 1;
					}

					if(Graphics()->HasTextureArraysSupport())
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3, Index);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsTex3DDrawTL(&QuadItem, 1);
					}
					else
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
					}
				}
			}
		}
	}

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DEnd();
	else
		Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderTile(int x, int y, unsigned char Index, float Scale, ColorRGBA Color)
{
	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DBegin();
	else
		Graphics()->QuadsBegin();

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// calculate the final pixelsize for the tiles
	float TilePixelSize = 1024 / Scale;
	float FinalTileSize = Scale / (ScreenX1 - ScreenX0) * Graphics()->ScreenWidth();
	float FinalTilesetScale = FinalTileSize / TilePixelSize;

	float TexSize = 1024.0f;
	float Frac = (1.25f / TexSize) * (1 / FinalTilesetScale);
	float Nudge = (0.5f / TexSize) * (1 / FinalTilesetScale);

	int tx = Index % 16;
	int ty = Index / 16;
	int Px0 = tx * (1024 / 16);
	int Py0 = ty * (1024 / 16);
	int Px1 = Px0 + (1024 / 16) - 1;
	int Py1 = Py0 + (1024 / 16) - 1;

	float x0 = Nudge + Px0 / TexSize + Frac;
	float y0 = Nudge + Py0 / TexSize + Frac;
	float x1 = Nudge + Px1 / TexSize - Frac;
	float y1 = Nudge + Py0 / TexSize + Frac;
	float x2 = Nudge + Px1 / TexSize - Frac;
	float y2 = Nudge + Py1 / TexSize - Frac;
	float x3 = Nudge + Px0 / TexSize + Frac;
	float y3 = Nudge + Py1 / TexSize - Frac;

	if(Graphics()->HasTextureArraysSupport())
	{
		x0 = 0;
		y0 = 0;
		x1 = x0 + 1;
		y1 = y0;
		x2 = x0 + 1;
		y2 = y0 + 1;
		x3 = x0;
		y3 = y0 + 1;
	}

	if(Graphics()->HasTextureArraysSupport())
	{
		Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3, Index);
		IGraphics::CQuadItem QuadItem(x, y, Scale, Scale);
		Graphics()->QuadsTex3DDrawTL(&QuadItem, 1);
	}
	else
	{
		Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3);
		IGraphics::CQuadItem QuadItem(x, y, Scale, Scale);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
	}

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DEnd();
	else
		Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderTilemap(CTile *pTiles, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, FTileRenderFilter pFilter, void *pFilterUser)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// calculate the final pixelsize for the tiles
	float TilePixelSize = 1024 / 32.0f;
	float FinalTileSize = Scale / (ScreenX1 - ScreenX0) * Graphics()->ScreenWidth();
	float FinalTilesetScale = FinalTileSize / TilePixelSize;

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DBegin();
	else
		Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);
	const bool ColorOpaque = Color.a > 254.0f / 255.0f;

	bool ExtendTiles = (RenderFlags & TILERENDERFLAG_EXTEND) != 0;

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;
	if(!ExtendTiles)
	{
		StartY = std::max(0, StartY);
		StartX = std::max(0, StartX);
		EndY = std::min(h, EndY);
		EndX = std::min(w, EndX);
	}

	// adjust the texture shift according to mipmap level
	float TexSize = 1024.0f;
	float Frac = (1.25f / TexSize) * (1 / FinalTilesetScale);
	float Nudge = (0.5f / TexSize) * (1 / FinalTilesetScale);

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int mx = x;
			int my = y;

			if(ExtendTiles)
			{
				if(mx < 0)
					mx = 0;
				if(mx >= w)
					mx = w - 1;
				if(my < 0)
					my = 0;
				if(my >= h)
					my = h - 1;
			}

			int c = mx + my * w;

			unsigned char Index = pTiles[c].m_Index;
			if(Index)
			{
				if(pFilter && !pFilter(Index, pFilterUser))
					continue;
				unsigned char Flags = pTiles[c].m_Flags;

				bool Render = false;
				if(ColorOpaque && Flags & TILEFLAG_OPAQUE)
				{
					if(RenderFlags & LAYERRENDERFLAG_OPAQUE)
						Render = true;
				}
				else
				{
					if(RenderFlags & LAYERRENDERFLAG_TRANSPARENT)
						Render = true;
				}

				if(Render)
				{
					int tx = Index % 16;
					int ty = Index / 16;
					int Px0 = tx * (1024 / 16);
					int Py0 = ty * (1024 / 16);
					int Px1 = Px0 + (1024 / 16) - 1;
					int Py1 = Py0 + (1024 / 16) - 1;

					float x0 = Nudge + Px0 / TexSize + Frac;
					float y0 = Nudge + Py0 / TexSize + Frac;
					float x1 = Nudge + Px1 / TexSize - Frac;
					float y1 = Nudge + Py0 / TexSize + Frac;
					float x2 = Nudge + Px1 / TexSize - Frac;
					float y2 = Nudge + Py1 / TexSize - Frac;
					float x3 = Nudge + Px0 / TexSize + Frac;
					float y3 = Nudge + Py1 / TexSize - Frac;

					if(Graphics()->HasTextureArraysSupport())
					{
						x0 = 0;
						y0 = 0;
						x1 = x0 + 1;
						y1 = y0;
						x2 = x0 + 1;
						y2 = y0 + 1;
						x3 = x0;
						y3 = y0 + 1;
					}

					if(Flags & TILEFLAG_XFLIP)
					{
						x0 = x2;
						x1 = x3;
						x2 = x3;
						x3 = x0;
					}

					if(Flags & TILEFLAG_YFLIP)
					{
						y0 = y3;
						y2 = y1;
						y3 = y1;
						y1 = y0;
					}

					if(Flags & TILEFLAG_ROTATE)
					{
						float Tmp = x0;
						x0 = x3;
						x3 = x2;
						x2 = x1;
						x1 = Tmp;
						Tmp = y0;
						y0 = y3;
						y3 = y2;
						y2 = y1;
						y1 = Tmp;
					}

					if(Graphics()->HasTextureArraysSupport())
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3, Index);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsTex3DDrawTL(&QuadItem, 1);
					}
					else
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
					}
				}
			}
			x += pTiles[c].m_Skip;
		}
	}

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DEnd();
	else
		Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderTeleOverlay(CTeleTile *pTele, int w, int h, float Scale, int OverlayRenderFlag, float Alpha, FTileRenderFilter pFilter, void *pFilterUser)
{
	if(!(OverlayRenderFlag & OVERLAYRENDERFLAG_TEXT))
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	if(EndX - StartX > Graphics()->ScreenWidth() / g_Config.m_GfxTextOverlay || EndY - StartY > Graphics()->ScreenHeight() / g_Config.m_GfxTextOverlay)
		return; // its useless to render text at this distance

	StartY = std::max(0, StartY);
	StartX = std::max(0, StartX);
	EndY = std::min(h, EndY);
	EndX = std::min(w, EndX);

	float Size = g_Config.m_ClTextEntitiesSize / 100.f;
	char aBuf[16];

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int c = x + y * w;
			if(pFilter && !pFilter(pTele[c].m_Type, pFilterUser))
				continue;

			unsigned char Index = pTele[c].m_Number;
			if(Index && IsTeleTileNumberUsedAny(pTele[c].m_Type))
			{
				str_format(aBuf, sizeof(aBuf), "%d", Index);
				// Auto-resize text to fit inside the tile
				float ScaledWidth = TextRender()->TextWidth(Size * Scale, aBuf, -1);
				float Factor = std::clamp(Scale / ScaledWidth, 0.0f, 1.0f);
				float LocalSize = Size * Factor;
				float ToCenterOffset = (1 - LocalSize) / 2.f;
				TextRender()->Text((x + 0.5f) * Scale - (ScaledWidth * Factor) / 2.0f, (y + ToCenterOffset) * Scale, LocalSize * Scale, aBuf);
			}
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderSpeedupOverlay(CSpeedupTile *pSpeedup, int w, int h, float Scale, int OverlayRenderFlag, float Alpha)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	if(EndX - StartX > Graphics()->ScreenWidth() / g_Config.m_GfxTextOverlay || EndY - StartY > Graphics()->ScreenHeight() / g_Config.m_GfxTextOverlay)
		return; // its useless to render text at this distance

	StartY = std::max(0, StartY);
	StartX = std::max(0, StartX);
	EndY = std::min(h, EndY);
	EndX = std::min(w, EndX);

	float Size = g_Config.m_ClTextEntitiesSize / 100.f;
	float ToCenterOffset = (1 - Size) / 2.f;
	char aBuf[16];

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int c = x + y * w;

			int Force = (int)pSpeedup[c].m_Force;
			int MaxSpeed = (int)pSpeedup[c].m_MaxSpeed;
			int Type = (int)pSpeedup[c].m_Type;
			int Angle = (int)pSpeedup[c].m_Angle;
			if((Force && Type == TILE_SPEED_BOOST_OLD) || ((Force || MaxSpeed) && Type == TILE_SPEED_BOOST) || (OverlayRenderFlag & OVERLAYRENDERFLAG_EDITOR && (Type || Force || MaxSpeed || Angle)))
			{
				if(IsValidSpeedupTile(Type))
				{
					// draw arrow
					Graphics()->TextureSet(g_pData->m_aImages[IMAGE_SPEEDUP_ARROW].m_Id);
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
					Graphics()->SelectSprite(SPRITE_SPEEDUP_ARROW);
					Graphics()->QuadsSetRotation(pSpeedup[c].m_Angle * (pi / 180.0f));
					Graphics()->DrawSprite(x * Scale + 16, y * Scale + 16, 35.0f);
					Graphics()->QuadsEnd();

					// draw force and max speed
					if(OverlayRenderFlag & OVERLAYRENDERFLAG_TEXT)
					{
						str_format(aBuf, sizeof(aBuf), "%d", Force);
						TextRender()->Text(x * Scale, (y + 0.5f + ToCenterOffset / 2) * Scale, Size * Scale / 2.f, aBuf);
						if(MaxSpeed)
						{
							str_format(aBuf, sizeof(aBuf), "%d", MaxSpeed);
							TextRender()->Text(x * Scale, (y + ToCenterOffset / 2) * Scale, Size * Scale / 2.f, aBuf);
						}
					}
				}
				else
				{
					// draw all three values
					if(OverlayRenderFlag & OVERLAYRENDERFLAG_TEXT)
					{
						float LineSpacing = Size * Scale / 3.f;
						float BaseY = (y + ToCenterOffset) * Scale;
						str_format(aBuf, sizeof(aBuf), "%d", Force);
						TextRender()->Text(x * Scale, BaseY, LineSpacing, aBuf);
						str_format(aBuf, sizeof(aBuf), "%d", MaxSpeed);
						TextRender()->Text(x * Scale, BaseY + LineSpacing, LineSpacing, aBuf);
						str_format(aBuf, sizeof(aBuf), "%d", Angle);
						TextRender()->Text(x * Scale, BaseY + 2 * LineSpacing, LineSpacing, aBuf);
					}
				}
			}
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderSwitchOverlay(CSwitchTile *pSwitch, int w, int h, float Scale, int OverlayRenderFlag, float Alpha)
{
	if(!(OverlayRenderFlag & OVERLAYRENDERFLAG_TEXT))
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	if(EndX - StartX > Graphics()->ScreenWidth() / g_Config.m_GfxTextOverlay || EndY - StartY > Graphics()->ScreenHeight() / g_Config.m_GfxTextOverlay)
		return; // its useless to render text at this distance

	StartY = std::max(0, StartY);
	StartX = std::max(0, StartX);
	EndY = std::min(h, EndY);
	EndX = std::min(w, EndX);

	float Size = g_Config.m_ClTextEntitiesSize / 100.f;
	float ToCenterOffset = (1 - Size) / 2.f;
	char aBuf[16];

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int c = x + y * w;

			unsigned char Index = pSwitch[c].m_Number;
			if(Index && IsSwitchTileNumberUsed(pSwitch[c].m_Type))
			{
				str_format(aBuf, sizeof(aBuf), "%d", Index);
				TextRender()->Text(x * Scale, (y + ToCenterOffset / 2) * Scale, Size * Scale / 2.f, aBuf);
			}

			unsigned char Delay = pSwitch[c].m_Delay;
			if(Delay && IsSwitchTileDelayUsed(pSwitch[c].m_Type))
			{
				str_format(aBuf, sizeof(aBuf), "%d", Delay);
				TextRender()->Text(x * Scale, (y + 0.5f + ToCenterOffset / 2) * Scale, Size * Scale / 2.f, aBuf);
			}
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderTuneOverlay(CTuneTile *pTune, int w, int h, float Scale, int OverlayRenderFlag, float Alpha)
{
	if(!(OverlayRenderFlag & OVERLAYRENDERFLAG_TEXT))
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	if(EndX - StartX > Graphics()->ScreenWidth() / g_Config.m_GfxTextOverlay || EndY - StartY > Graphics()->ScreenHeight() / g_Config.m_GfxTextOverlay)
		return; // its useless to render text at this distance

	StartY = std::max(0, StartY);
	StartX = std::max(0, StartX);
	EndY = std::min(h, EndY);
	EndX = std::min(w, EndX);

	float Size = g_Config.m_ClTextEntitiesSize / 200.f;
	char aBuf[16];

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			int c = x + y * w;

			unsigned char Index = pTune[c].m_Number;
			if(Index)
			{
				str_format(aBuf, sizeof(aBuf), "%d", Index);
				// Auto-resize text to fit inside the tile
				float ScaledWidth = TextRender()->TextWidth(Size * Scale, aBuf, -1);
				float Factor = std::clamp(Scale / ScaledWidth, 0.0f, 1.0f);
				float LocalSize = Size * Factor;
				float ToCenterOffset = (1 - LocalSize) / 2.f;
				TextRender()->Text((x + 0.5f) * Scale - (ScaledWidth * Factor) / 2.0f, (y + ToCenterOffset) * Scale, LocalSize * Scale, aBuf);
			}
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderTelemap(CTeleTile *pTele, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, FTileRenderFilter pFilter, void *pFilterUser)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// calculate the final pixelsize for the tiles
	float TilePixelSize = 1024 / 32.0f;
	float FinalTileSize = Scale / (ScreenX1 - ScreenX0) * Graphics()->ScreenWidth();
	float FinalTilesetScale = FinalTileSize / TilePixelSize;

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DBegin();
	else
		Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);

	bool ExtendTiles = (RenderFlags & TILERENDERFLAG_EXTEND) != 0;

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;
	if(!ExtendTiles)
	{
		StartY = std::max(0, StartY);
		StartX = std::max(0, StartX);
		EndY = std::min(h, EndY);
		EndX = std::min(w, EndX);
	}

	// adjust the texture shift according to mipmap level
	float TexSize = 1024.0f;
	float Frac = (1.25f / TexSize) * (1 / FinalTilesetScale);
	float Nudge = (0.5f / TexSize) * (1 / FinalTilesetScale);

	for(int y = StartY; y < EndY; y++)
		for(int x = StartX; x < EndX; x++)
		{
			int mx = x;
			int my = y;

			if(ExtendTiles)
			{
				if(mx < 0)
					mx = 0;
				if(mx >= w)
					mx = w - 1;
				if(my < 0)
					my = 0;
				if(my >= h)
					my = h - 1;
			}

			int c = mx + my * w;

			unsigned char Index = pTele[c].m_Type;
			if(Index)
			{
				if(pFilter && !pFilter(Index, pFilterUser))
					continue;
				bool Render = false;
				if(RenderFlags & LAYERRENDERFLAG_TRANSPARENT)
					Render = true;

				if(Render)
				{
					int tx = Index % 16;
					int ty = Index / 16;
					int Px0 = tx * (1024 / 16);
					int Py0 = ty * (1024 / 16);
					int Px1 = Px0 + (1024 / 16) - 1;
					int Py1 = Py0 + (1024 / 16) - 1;

					float x0 = Nudge + Px0 / TexSize + Frac;
					float y0 = Nudge + Py0 / TexSize + Frac;
					float x1 = Nudge + Px1 / TexSize - Frac;
					float y1 = Nudge + Py0 / TexSize + Frac;
					float x2 = Nudge + Px1 / TexSize - Frac;
					float y2 = Nudge + Py1 / TexSize - Frac;
					float x3 = Nudge + Px0 / TexSize + Frac;
					float y3 = Nudge + Py1 / TexSize - Frac;

					if(Graphics()->HasTextureArraysSupport())
					{
						x0 = 0;
						y0 = 0;
						x1 = x0 + 1;
						y1 = y0;
						x2 = x0 + 1;
						y2 = y0 + 1;
						x3 = x0;
						y3 = y0 + 1;
					}

					if(Graphics()->HasTextureArraysSupport())
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3, Index);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsTex3DDrawTL(&QuadItem, 1);
					}
					else
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
					}
				}
			}
		}

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DEnd();
	else
		Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderSwitchmap(CSwitchTile *pSwitchTile, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, FTileRenderFilter pFilter, void *pFilterUser)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// calculate the final pixelsize for the tiles
	float TilePixelSize = 1024 / 32.0f;
	float FinalTileSize = Scale / (ScreenX1 - ScreenX0) * Graphics()->ScreenWidth();
	float FinalTilesetScale = FinalTileSize / TilePixelSize;

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DBegin();
	else
		Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);

	bool ExtendTiles = (RenderFlags & TILERENDERFLAG_EXTEND) != 0;

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;
	if(!ExtendTiles)
	{
		StartY = std::max(0, StartY);
		StartX = std::max(0, StartX);
		EndY = std::min(h, EndY);
		EndX = std::min(w, EndX);
	}

	// adjust the texture shift according to mipmap level
	float TexSize = 1024.0f;
	float Frac = (1.25f / TexSize) * (1 / FinalTilesetScale);
	float Nudge = (0.5f / TexSize) * (1 / FinalTilesetScale);

	for(int y = StartY; y < EndY; y++)
		for(int x = StartX; x < EndX; x++)
		{
			int mx = x;
			int my = y;

			if(ExtendTiles)
			{
				if(mx < 0)
					mx = 0;
				if(mx >= w)
					mx = w - 1;
				if(my < 0)
					my = 0;
				if(my >= h)
					my = h - 1;
			}

			int c = mx + my * w;

			unsigned char Index = pSwitchTile[c].m_Type;
			if(Index)
			{
				if(pFilter && !pFilter(Index, pFilterUser))
					continue;
				if(Index == TILE_SWITCHTIMEDOPEN)
					Index = 8;

				unsigned char Flags = pSwitchTile[c].m_Flags;

				bool Render = false;
				if(Flags & TILEFLAG_OPAQUE)
				{
					if(RenderFlags & LAYERRENDERFLAG_OPAQUE)
						Render = true;
				}
				else
				{
					if(RenderFlags & LAYERRENDERFLAG_TRANSPARENT)
						Render = true;
				}

				if(Render)
				{
					int tx = Index % 16;
					int ty = Index / 16;
					int Px0 = tx * (1024 / 16);
					int Py0 = ty * (1024 / 16);
					int Px1 = Px0 + (1024 / 16) - 1;
					int Py1 = Py0 + (1024 / 16) - 1;

					float x0 = Nudge + Px0 / TexSize + Frac;
					float y0 = Nudge + Py0 / TexSize + Frac;
					float x1 = Nudge + Px1 / TexSize - Frac;
					float y1 = Nudge + Py0 / TexSize + Frac;
					float x2 = Nudge + Px1 / TexSize - Frac;
					float y2 = Nudge + Py1 / TexSize - Frac;
					float x3 = Nudge + Px0 / TexSize + Frac;
					float y3 = Nudge + Py1 / TexSize - Frac;

					if(Graphics()->HasTextureArraysSupport())
					{
						x0 = 0;
						y0 = 0;
						x1 = x0 + 1;
						y1 = y0;
						x2 = x0 + 1;
						y2 = y0 + 1;
						x3 = x0;
						y3 = y0 + 1;
					}

					if(Flags & TILEFLAG_XFLIP)
					{
						x0 = x2;
						x1 = x3;
						x2 = x3;
						x3 = x0;
					}

					if(Flags & TILEFLAG_YFLIP)
					{
						y0 = y3;
						y2 = y1;
						y3 = y1;
						y1 = y0;
					}

					if(Flags & TILEFLAG_ROTATE)
					{
						float Tmp = x0;
						x0 = x3;
						x3 = x2;
						x2 = x1;
						x1 = Tmp;
						Tmp = y0;
						y0 = y3;
						y3 = y2;
						y2 = y1;
						y1 = Tmp;
					}

					if(Graphics()->HasTextureArraysSupport())
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3, Index);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsTex3DDrawTL(&QuadItem, 1);
					}
					else
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
					}
				}
			}
		}

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DEnd();
	else
		Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderTunemap(CTuneTile *pTune, int w, int h, float Scale, ColorRGBA Color, int RenderFlags, CTuneColorMapper *pTuneColorMapper)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// calculate the final pixelsize for the tiles
	float TilePixelSize = 1024 / 32.0f;
	float FinalTileSize = Scale / (ScreenX1 - ScreenX0) * Graphics()->ScreenWidth();
	float FinalTilesetScale = FinalTileSize / TilePixelSize;

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DBegin();
	else
		Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);

	bool ExtendTiles = (RenderFlags & TILERENDERFLAG_EXTEND) != 0;

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;
	if(!ExtendTiles)
	{
		StartY = std::max(0, StartY);
		StartX = std::max(0, StartX);
		EndY = std::min(h, EndY);
		EndX = std::min(w, EndX);
	}

	// adjust the texture shift according to mipmap level
	float TexSize = 1024.0f;
	float Frac = (1.25f / TexSize) * (1 / FinalTilesetScale);
	float Nudge = (0.5f / TexSize) * (1 / FinalTilesetScale);

	for(int y = StartY; y < EndY; y++)
		for(int x = StartX; x < EndX; x++)
		{
			int mx = x;
			int my = y;

			if(ExtendTiles)
			{
				if(mx < 0)
					mx = 0;
				if(mx >= w)
					mx = w - 1;
				if(my < 0)
					my = 0;
				if(my >= h)
					my = h - 1;
			}

			int c = mx + my * w;

			const unsigned char Index = pTune[c].m_Type;
			if(Index)
			{
				bool Render = false;
				if(RenderFlags & LAYERRENDERFLAG_TRANSPARENT)
					Render = true;

				if(Render)
				{
					const unsigned char Number = pTune[c].m_Number;
					if(Number == 0 || pTuneColorMapper == nullptr)
						Graphics()->SetColor(Color);
					else
					{
						const uint8_t ColorIndex = pTuneColorMapper->TuneNumberToColorIndex(Number);
						Graphics()->SetColor(pTuneColorMapper->TuneColorIndexToColor(ColorIndex).Multiply(Color));
					}

					int tx = Index % 16;
					int ty = Index / 16;
					int Px0 = tx * (1024 / 16);
					int Py0 = ty * (1024 / 16);
					int Px1 = Px0 + (1024 / 16) - 1;
					int Py1 = Py0 + (1024 / 16) - 1;

					float x0 = Nudge + Px0 / TexSize + Frac;
					float y0 = Nudge + Py0 / TexSize + Frac;
					float x1 = Nudge + Px1 / TexSize - Frac;
					float y1 = Nudge + Py0 / TexSize + Frac;
					float x2 = Nudge + Px1 / TexSize - Frac;
					float y2 = Nudge + Py1 / TexSize - Frac;
					float x3 = Nudge + Px0 / TexSize + Frac;
					float y3 = Nudge + Py1 / TexSize - Frac;

					if(Graphics()->HasTextureArraysSupport())
					{
						x0 = 0;
						y0 = 0;
						x1 = x0 + 1;
						y1 = y0;
						x2 = x0 + 1;
						y2 = y0 + 1;
						x3 = x0;
						y3 = y0 + 1;
					}

					if(Graphics()->HasTextureArraysSupport())
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3, Index);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsTex3DDrawTL(&QuadItem, 1);
					}
					else
					{
						Graphics()->QuadsSetSubsetFree(x0, y0, x1, y1, x2, y2, x3, y3);
						IGraphics::CQuadItem QuadItem(x * Scale, y * Scale, Scale, Scale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
					}
				}
			}
		}

	if(Graphics()->HasTextureArraysSupport())
		Graphics()->QuadsTex3DEnd();
	else
		Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CRenderMap::RenderDebugClip(float ClipX, float ClipY, float ClipW, float ClipH, ColorRGBA Color, float Zoom, const char *pLabel)
{
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Color);
	IGraphics::CLineItem aLineItems[] = {
		IGraphics::CLineItem(ClipX, ClipY, ClipX, ClipY + ClipH),
		IGraphics::CLineItem(ClipX + ClipW, ClipY, ClipX + ClipW, ClipY + ClipH),
		IGraphics::CLineItem(ClipX, ClipY, ClipX + ClipW, ClipY),
		IGraphics::CLineItem(ClipX, ClipY + ClipH, ClipX + ClipW, ClipY + ClipH),
	};
	Graphics()->LinesDraw(aLineItems, std::size(aLineItems));
	Graphics()->LinesEnd();

	TextRender()->TextColor(Color);

	// clamp zoom and set line width, because otherwise the text can be partially clipped out
	TextRender()->Text(ClipX, ClipY, std::min(12.0f * Zoom, 20.0f), pLabel, ClipW);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
