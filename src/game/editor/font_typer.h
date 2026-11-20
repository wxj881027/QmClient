#ifndef GAME_EDITOR_FONT_TYPER_H
#define GAME_EDITOR_FONT_TYPER_H

#include "component.h"

#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/ui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>

class CLayer;
class CLayerTiles;

class CFontTyper : public CEditorComponent
{
public:
	class CState
	{
	public:
		bool m_Active;
		ivec2 m_TextIndex;
		// 换行返回到的列；未确定行首时为空。
		std::optional<int> m_LineStart;
		std::shared_ptr<CLayer> m_pLastLayer;
		int m_TilesPlacedSinceActivate;

		void Reset();

		void SetCursorFromWorld(vec2 Position, int Width, int Height)
		{
			const auto TileCoordinate = [](float World, int Last) {
				return std::isfinite(World) ? static_cast<int>(std::clamp(World / 32.0, 0.0, static_cast<double>(Last))) : 0;
			};
			m_TextIndex = ivec2(TileCoordinate(Position.x, Width), TileCoordinate(Position.y, Height - 1));
		}

		void ClampCursor(int Width, int Height)
		{
			if(m_LineStart)
				m_LineStart = std::clamp(*m_LineStart, 0, Width - 1);
			m_TextIndex.x = std::clamp(m_TextIndex.x, 0, Width);
			m_TextIndex.y = std::clamp(m_TextIndex.y, 0, Height - 1);
		}
	};

	void OnInit(CEditor *pEditor) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void Render();

	bool IsActive() const;

private:
	enum
	{
		LETTER_OFFSET = 1,
		NUMBER_OFFSET = 54,
	};
	std::chrono::nanoseconds m_CursorRenderTime;
	IGraphics::CTextureHandle m_CursorTextTexture;
	CUi::SConfirmPopupContext m_ConfirmActivatePopupContext;

	std::shared_ptr<CLayerTiles> SelectedTextLayer() const;
	void TextModeOff();
	void TextModeOn();
	void SetTile(ivec2 Pos, unsigned char Index, const std::shared_ptr<CLayerTiles> &pLayer);
	void PlaceTile(unsigned char Index, const std::shared_ptr<CLayerTiles> &pLayer);
};

#endif
