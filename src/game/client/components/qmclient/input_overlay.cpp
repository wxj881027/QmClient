#include "input_overlay.h"

#include <base/color.h>
#include <base/log.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/storage.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <ctime>
#include <cctype>
#include <cmath>

namespace
{
static constexpr const char *CONFIGURATION_FILENAME = "input_overlay.json";

static ColorRGBA ApplyOpacity(ColorRGBA Color, float Opacity)
{
	Color.a *= Opacity;
	return Color;
}

static ColorRGBA RainbowColor(float Hue, float Alpha)
{
	ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(std::fmod(Hue, 1.0f), 1.0f, 0.5f));
	Col.a = Alpha;
	return Col;
}

} // namespace

void CInputOverlay::OnInit()
{
	LoadConfiguration(IStorage::TYPE_ALL);
	const vec2 MousePos = Input()->NativeMousePos();
	m_LastMouseX = MousePos.x;
	m_LastMouseY = MousePos.y;

	time_t Modified;
	if(GetConfigModifiedTime(Modified))
	{
		m_ConfigModifiedTime = Modified;
		m_HasConfigModifiedTime = true;
	}
}

void CInputOverlay::OnRender()
{
	if(!g_Config.m_QmInputOverlay)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_Menus.IsActive())
		return;
	if(GameClient()->m_Scoreboard.IsActive())
		return;

	if(!m_ConfigLoaded)
		LoadConfiguration(IStorage::TYPE_ALL);

	m_ConfigCheckTimer += Client()->RenderFrameTime();
	if(m_ConfigCheckTimer >= 0.5f)
	{
		m_ConfigCheckTimer = 0.0f;
		time_t Modified;
		if(GetConfigModifiedTime(Modified))
		{
			if(!m_HasConfigModifiedTime || Modified != m_ConfigModifiedTime)
			{
				LoadConfiguration(IStorage::TYPE_ALL);
				m_ConfigModifiedTime = Modified;
				m_HasConfigModifiedTime = true;
			}
		}
	}

	if(!m_ConfigValid)
		return;
	if(m_ConfigMode == EConfigMode::VECTOR && m_vElements.empty())
		return;
	if(m_ConfigMode == EConfigMode::OBS)
	{
		bool HasLayout = false;
		for(const SObsLayout &Layout : m_vObsLayouts)
		{
			if(!Layout.m_vElements.empty() && Layout.m_Texture.IsValid())
			{
				HasLayout = true;
				break;
			}
		}
		if(!HasLayout)
			return;
	}

	const CUIRect *pScreen = Ui()->Screen();
	Graphics()->MapScreen(pScreen->x, pScreen->y, pScreen->w, pScreen->h);

	const float Scale = g_Config.m_QmInputOverlayScale / 100.0f;
	const float Opacity = g_Config.m_QmInputOverlayOpacity / 100.0f;
	const float PosXPercent = g_Config.m_QmInputOverlayPosX;
	const float PosYPercent = g_Config.m_QmInputOverlayPosY;
	float ObsMinX = 0.0f;
	float ObsMinY = 0.0f;
	float CanvasW = m_CanvasWidth * Scale;
	float CanvasH = m_CanvasHeight * Scale;
	if(m_ConfigMode == EConfigMode::OBS)
	{
		bool HasBounds = false;
		float MinX = 0.0f;
		float MinY = 0.0f;
		float MaxX = 0.0f;
		float MaxY = 0.0f;
		for(const SObsLayout &Layout : m_vObsLayouts)
		{
			if(Layout.m_vElements.empty() || !Layout.m_Texture.IsValid())
				continue;
			const float LMinX = Layout.m_OffsetX;
			const float LMinY = Layout.m_OffsetY;
			const float LMaxX = Layout.m_OffsetX + Layout.m_OverlayWidth;
			const float LMaxY = Layout.m_OffsetY + Layout.m_OverlayHeight;
			if(!HasBounds)
			{
				MinX = LMinX;
				MinY = LMinY;
				MaxX = LMaxX;
				MaxY = LMaxY;
				HasBounds = true;
			}
			else
			{
				MinX = std::min(MinX, LMinX);
				MinY = std::min(MinY, LMinY);
				MaxX = std::max(MaxX, LMaxX);
				MaxY = std::max(MaxY, LMaxY);
			}
		}
		if(HasBounds)
		{
			ObsMinX = MinX;
			ObsMinY = MinY;
			CanvasW = (MaxX - MinX) * Scale;
			CanvasH = (MaxY - MinY) * Scale;
		}
	}
	float OriginX = pScreen->w * PosXPercent / 100.0f;
	float OriginY = pScreen->h * PosYPercent / 100.0f;
	if(CanvasW > 0.0f && CanvasH > 0.0f)
	{
		if(CanvasW <= pScreen->w)
			OriginX = std::clamp(OriginX, 0.0f, pScreen->w - CanvasW);
		if(CanvasH <= pScreen->h)
			OriginY = std::clamp(OriginY, 0.0f, pScreen->h - CanvasH);
	}

	m_Time += Client()->RenderFrameTime();

	if(m_ConfigMode == EConfigMode::OBS)
	{
		const vec2 MousePos = Input()->NativeMousePos();
		m_MouseDeltaX = MousePos.x - m_LastMouseX;
		m_MouseDeltaY = MousePos.y - m_LastMouseY;
		m_LastMouseX = MousePos.x;
		m_LastMouseY = MousePos.y;

		const float WheelHoldTime = 0.5f;
		auto UpdateWheel = [&](int Index, int Key) {
			if(Input()->KeyPress(Key))
				m_aWheelLastTime[Index] = m_Time;
			if(m_aWheelLastTime[Index] < 0.0f)
			{
				m_aWheelAlpha[Index] = 0.0f;
				return;
			}
			const float Age = m_Time - m_aWheelLastTime[Index];
			if(Age <= WheelHoldTime)
				m_aWheelAlpha[Index] = 1.0f;
			else
			{
				m_aWheelAlpha[Index] = 0.0f;
				m_aWheelLastTime[Index] = -1.0f;
			}
		};

		UpdateWheel(0, KEY_MOUSE_WHEEL_UP);
		UpdateWheel(1, KEY_MOUSE_WHEEL_DOWN);
		UpdateWheel(2, KEY_MOUSE_WHEEL_LEFT);
		UpdateWheel(3, KEY_MOUSE_WHEEL_RIGHT);

		const vec2 AimPos = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
		float MouseMoveAngle = std::atan2(AimPos.y, AimPos.x);
		MouseMoveAngle += (float)(pi / 2.0);

		for(const SObsLayout &Layout : m_vObsLayouts)
		{
			if(Layout.m_vElements.empty() || !Layout.m_Texture.IsValid())
				continue;

			const float LayoutOriginX = OriginX + (Layout.m_OffsetX - ObsMinX) * Scale;
			const float LayoutOriginY = OriginY + (Layout.m_OffsetY - ObsMinY) * Scale;

			Graphics()->TextureSet(Layout.m_Texture);
			Graphics()->QuadsBegin();

			for(const SObsElement &Element : Layout.m_vElements)
			{
				float MapW = Element.m_MapW;
				float MapH = Element.m_MapH;
				if(MapW <= 0.0f)
					MapW = Layout.m_DefaultWidth;
				if(MapH <= 0.0f)
					MapH = Layout.m_DefaultHeight;
				if(MapW <= 0.0f || MapH <= 0.0f)
					continue;

				const bool IsPressable = Element.m_InputKind == EObsInputKind::KEY ||
					Element.m_InputKind == EObsInputKind::MOUSE ||
					Element.m_InputKind == EObsInputKind::WHEEL;
				float WheelAlpha = 1.0f;
				if(Element.m_InputKind == EObsInputKind::WHEEL)
				{
					if(Element.m_WheelDir >= 1 && Element.m_WheelDir <= 4)
						WheelAlpha = m_aWheelAlpha[Element.m_WheelDir - 1];
					else
						WheelAlpha = std::max(std::max(m_aWheelAlpha[0], m_aWheelAlpha[1]), std::max(m_aWheelAlpha[2], m_aWheelAlpha[3]));
				}
				const bool Active = IsObsActive(Element);
				bool UsePressedAtlas = Active && IsPressable && Layout.m_HasPressedOffset;
				float MapX = Element.m_MapX;
				float MapY = Element.m_MapY;
				if(UsePressedAtlas)
				{
					const float Candidate = MapY + (float)Layout.m_PressedOffsetY;
					if(Layout.m_TextureHeight > 0 && Candidate + MapH <= (float)Layout.m_TextureHeight)
						MapY = Candidate;
					else
						UsePressedAtlas = false;
				}

				const float X = LayoutOriginX + Element.m_PosX * Scale;
				const float Y = LayoutOriginY + Element.m_PosY * Scale;
				const float W = MapW * Scale;
				const float H = MapH * Scale;

				const float U0 = Layout.m_TextureWidth > 0 ? MapX / (float)Layout.m_TextureWidth : 0.0f;
				const float V0 = Layout.m_TextureHeight > 0 ? MapY / (float)Layout.m_TextureHeight : 0.0f;
				const float U1 = Layout.m_TextureWidth > 0 ? (MapX + MapW) / (float)Layout.m_TextureWidth : 1.0f;
				const float V1 = Layout.m_TextureHeight > 0 ? (MapY + MapH) / (float)Layout.m_TextureHeight : 1.0f;
				Graphics()->QuadsSetSubset(U0, V0, U1, V1);

				const bool Highlight = Active && IsPressable;
				ColorRGBA DrawColor = m_ObsColor;
				if(Highlight && !UsePressedAtlas)
					DrawColor = m_ObsActiveColor;
				float Alpha = Opacity;
				if(IsPressable || Element.m_InputKind == EObsInputKind::MOUSE_MOVE)
					Alpha *= Active ? 1.0f : m_ObsInactiveAlpha;
				if(Element.m_InputKind == EObsInputKind::WHEEL)
					Alpha *= WheelAlpha;
				if(Element.m_ActiveOnly && !Active)
					Alpha = 0.0f;
				if(Alpha <= 0.0f)
					continue;
				DrawColor.a *= Alpha;
				Graphics()->SetColor(DrawColor);

				if(Element.m_InputKind == EObsInputKind::MOUSE_MOVE && Element.m_MouseType == 1)
					Graphics()->QuadsSetRotation(MouseMoveAngle);
				else
					Graphics()->QuadsSetRotation(0.0f);

				IGraphics::CQuadItem Quad(X + W * 0.5f, Y + H * 0.5f, W, H);
				Graphics()->QuadsDraw(&Quad, 1);
			}

			Graphics()->QuadsSetRotation(0.0f);
			Graphics()->QuadsEnd();
		}

		Graphics()->TextureClear();
		return;
	}

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	for(const SElement &Element : m_vElements)
	{
		const bool Active = IsActiveInput(Element);
		const SStyle &Style = Element.m_Style;

		float X = OriginX + Element.m_X * Scale;
		float Y = OriginY + Element.m_Y * Scale;
		float W = Element.m_W * Scale;
		float H = Element.m_H * Scale;
		const float Skew = Element.m_Shape == EShape::PARALLELOGRAM ? Element.m_Skew * Scale : 0.0f;

		if(W <= 0.0f || H <= 0.0f)
			continue;

		auto MakeQuad = [&](float Qx, float Qy, float Qw, float Qh, float Qskew) {
			const vec2 TL(Qx + Qskew, Qy);
			const vec2 TR(Qx + Qw + Qskew, Qy);
			const vec2 BR(Qx + Qw, Qy + Qh);
			const vec2 BL(Qx, Qy + Qh);
			return IGraphics::CFreeformItem(TL, TR, BR, BL);
		};

		const bool BorderRainbow = Active ? Style.m_BorderRainbowActive : Style.m_BorderRainbow;
		const ColorRGBA BorderBaseColor = Active ? Style.m_BorderColorActive : Style.m_BorderColor;
		float BorderWidth = Style.m_BorderWidth * Scale;
		if(BorderWidth > 0.0f)
		{
			BorderWidth = std::min(BorderWidth, std::min(W, H) * 0.49f);
			const float InnerW = W - BorderWidth * 2.0f;
			const float InnerH = H - BorderWidth * 2.0f;
			const float InnerX = X + BorderWidth;
			const float InnerY = Y + BorderWidth;
			const float InnerSkew = Skew;

			const IGraphics::CFreeformItem Outer = MakeQuad(X, Y, W, H, Skew);
			const IGraphics::CFreeformItem Inner = MakeQuad(InnerX, InnerY, InnerW, InnerH, InnerSkew);

			ColorRGBA Ctl;
			ColorRGBA Ctr;
			ColorRGBA Cbr;
			ColorRGBA Cbl;
			const float BorderAlpha = BorderBaseColor.a * Opacity;
			if(BorderRainbow)
			{
				const float BaseHue = m_Time * Style.m_BorderRainbowSpeed + Style.m_BorderRainbowOffset;
				Ctl = RainbowColor(BaseHue + 0.0f, BorderAlpha);
				Ctr = RainbowColor(BaseHue + 0.25f, BorderAlpha);
				Cbr = RainbowColor(BaseHue + 0.5f, BorderAlpha);
				Cbl = RainbowColor(BaseHue + 0.75f, BorderAlpha);
			}
			else
			{
				Ctl = ApplyOpacity(BorderBaseColor, Opacity);
				Ctr = Ctl;
				Cbr = Ctl;
				Cbl = Ctl;
			}

			const IGraphics::CFreeformItem aEdges[4] = {
				IGraphics::CFreeformItem(Outer.m_X0, Outer.m_Y0, Outer.m_X1, Outer.m_Y1, Inner.m_X1, Inner.m_Y1, Inner.m_X0, Inner.m_Y0),
				IGraphics::CFreeformItem(Outer.m_X1, Outer.m_Y1, Outer.m_X2, Outer.m_Y2, Inner.m_X2, Inner.m_Y2, Inner.m_X1, Inner.m_Y1),
				IGraphics::CFreeformItem(Outer.m_X2, Outer.m_Y2, Outer.m_X3, Outer.m_Y3, Inner.m_X3, Inner.m_Y3, Inner.m_X2, Inner.m_Y2),
				IGraphics::CFreeformItem(Outer.m_X3, Outer.m_Y3, Outer.m_X0, Outer.m_Y0, Inner.m_X0, Inner.m_Y0, Inner.m_X3, Inner.m_Y3)};

			Graphics()->SetColor4(Ctl, Ctr, Ctl, Ctr);
			Graphics()->QuadsDrawFreeform(&aEdges[0], 1);
			Graphics()->SetColor4(Ctr, Cbr, Ctr, Cbr);
			Graphics()->QuadsDrawFreeform(&aEdges[1], 1);
			Graphics()->SetColor4(Cbr, Cbl, Cbr, Cbl);
			Graphics()->QuadsDrawFreeform(&aEdges[2], 1);
			Graphics()->SetColor4(Cbl, Ctl, Cbl, Ctl);
			Graphics()->QuadsDrawFreeform(&aEdges[3], 1);

			if(InnerW > 0.0f && InnerH > 0.0f)
			{
				const ColorRGBA FillColor = ApplyOpacity(Active ? Style.m_FillColorActive : Style.m_FillColor, Opacity);
				if(FillColor.a > 0.0f)
				{
					Graphics()->SetColor(FillColor);
					Graphics()->QuadsDrawFreeform(&Inner, 1);
				}
			}
			continue;
		}

		const ColorRGBA FillColor = ApplyOpacity(Active ? Style.m_FillColorActive : Style.m_FillColor, Opacity);
		if(FillColor.a > 0.0f)
		{
			const IGraphics::CFreeformItem Quad = MakeQuad(X, Y, W, H, Skew);
			Graphics()->SetColor(FillColor);
			Graphics()->QuadsDrawFreeform(&Quad, 1);
		}
	}

	Graphics()->QuadsEnd();

	for(const SElement &Element : m_vElements)
	{
		if(Element.m_Label.empty() || !Element.m_Style.m_TextEnabled)
			continue;

		const bool Active = IsActiveInput(Element);
		const SStyle &Style = Element.m_Style;
		const ColorRGBA TextColor = ApplyOpacity(Active ? Style.m_TextColorActive : Style.m_TextColor, Opacity);
		if(TextColor.a <= 0.0f)
			continue;

		const float X = OriginX + Element.m_X * Scale;
		const float Y = OriginY + Element.m_Y * Scale;
		const float W = Element.m_W * Scale;
		const float H = Element.m_H * Scale;
		const float Skew = Element.m_Shape == EShape::PARALLELOGRAM ? Element.m_Skew * Scale : 0.0f;
		const float CenterX = X + W * 0.5f + Skew * 0.5f + Style.m_TextOffsetX * Scale;
		const float CenterY = Y + H * 0.5f + Style.m_TextOffsetY * Scale;

		const float TextSize = Style.m_TextSize * Scale;
		const float TextWidth = TextRender()->TextWidth(TextSize, Element.m_Label.c_str());
		const float TextX = CenterX - TextWidth * 0.5f;
		const float TextY = CenterY - TextSize * 0.5f;

		TextRender()->TextColor(TextColor);
		TextRender()->Text(TextX, TextY, TextSize, Element.m_Label.c_str());
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CInputOverlay::LoadConfiguration(int StorageType)
{
	void *pFileData;
	unsigned FileLength;
	if(!Storage()->ReadFile(CONFIGURATION_FILENAME, StorageType, &pFileData, &FileLength))
	{
		log_error("input_overlay", "Failed to read configuration from '%s'", CONFIGURATION_FILENAME);
		m_ConfigLoaded = true;
		m_ConfigValid = false;
		return false;
	}

	const bool HadValid = m_ConfigValid;
	const bool Result = ParseConfiguration(pFileData, FileLength);

	free(pFileData);

	m_ConfigLoaded = true;
	if(Result)
		m_ConfigValid = true;
	else if(!HadValid)
		m_ConfigValid = false;
	return Result;
}

bool CInputOverlay::ParseConfiguration(const void *pFileData, unsigned FileLength)
{
	json_settings JsonSettings{};
	JsonSettings.settings = json_enable_comments;
	char aError[256];
	json_value *pJson = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileLength, aError);
	if(pJson == nullptr)
	{
		log_error("input_overlay", "Failed to parse configuration (invalid json): '%s'", aError);
		return false;
	}
	if(pJson->type != json_object)
	{
		log_error("input_overlay", "Failed to parse configuration: root must be an object");
		json_value_free(pJson);
		return false;
	}

	const json_value &Root = *pJson;
	ColorRGBA ObsColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	ColorRGBA ObsActiveColor = ColorRGBA(1.0f, 0.9f, 0.2f, 1.0f);
	float ObsInactiveAlpha = 0.65f;
	if(auto Parsed = ParseColor(Root["obs_color"]); Parsed.has_value())
		ObsColor = Parsed.value();
	if(auto Parsed = ParseColor(Root["obs_active_color"]); Parsed.has_value())
		ObsActiveColor = Parsed.value();
	ParseFloat(Root["obs_inactive_alpha"], ObsInactiveAlpha);
	ObsInactiveAlpha = std::clamp(ObsInactiveAlpha, 0.0f, 1.0f);

	float PosX = m_PosXPercent;
	float PosY = m_PosYPercent;
	bool HasPosX = false;
	bool HasPosY = false;
	ParsePosition(Root, PosX, PosY, HasPosX, HasPosY);

	float RootOffsetX = 0.0f;
	float RootOffsetY = 0.0f;
	ParseOffset(Root, RootOffsetX, RootOffsetY);
	int RootPressedOffset = 0;
	bool HasRootPressedOffset = false;
	float RootPressedOffsetValue = 0.0f;
	if(ParseFloat(Root["pressed_offset_y"], RootPressedOffsetValue))
	{
		RootPressedOffset = std::max(0, (int)std::round(RootPressedOffsetValue));
		HasRootPressedOffset = true;
	}

	auto ClearLayouts = [&](std::vector<SObsLayout> &Layouts) {
		for(SObsLayout &Layout : Layouts)
		{
			if(Layout.m_Texture.IsValid())
				Graphics()->UnloadTexture(&Layout.m_Texture);
		}
		Layouts.clear();
	};

	const json_value &LayoutsValue = Root["layouts"];
	if(LayoutsValue.type == json_array)
	{
		std::vector<SObsLayout> ParsedLayouts;
		ParsedLayouts.reserve(LayoutsValue.u.array.length);
		for(unsigned i = 0; i < LayoutsValue.u.array.length; ++i)
		{
			const json_value &Entry = LayoutsValue[i];
			if(Entry.type != json_object)
			{
				log_error("input_overlay", "Failed to parse configuration: layouts entry '%u' must be an object", i);
				ClearLayouts(ParsedLayouts);
				json_value_free(pJson);
				return false;
			}

			const json_value &LayoutValue = Entry["layout"];
			if(LayoutValue.type != json_string)
			{
				log_error("input_overlay", "Failed to parse configuration: layouts entry '%u' missing 'layout'", i);
				ClearLayouts(ParsedLayouts);
				json_value_free(pJson);
				return false;
			}

			const json_value &ImageValue = Entry["image"];
			const char *pImageOverride = ImageValue.type == json_string ? ImageValue.u.string.ptr : nullptr;
			float OffsetX = RootOffsetX;
			float OffsetY = RootOffsetY;
			ParseOffset(Entry, OffsetX, OffsetY);
			int EntryPressedOffset = 0;
			bool HasEntryPressedOffset = false;
			float EntryPressedOffsetValue = 0.0f;
			if(ParseFloat(Entry["pressed_offset_y"], EntryPressedOffsetValue))
			{
				EntryPressedOffset = std::max(0, (int)std::round(EntryPressedOffsetValue));
				HasEntryPressedOffset = true;
			}

			void *pLayoutData;
			unsigned LayoutLength;
			const char *pLayoutPath = LayoutValue.u.string.ptr;
			if(!Storage()->ReadFile(pLayoutPath, IStorage::TYPE_ALL, &pLayoutData, &LayoutLength))
			{
				log_error("input_overlay", "Failed to read layout file '%s'", pLayoutPath);
				ClearLayouts(ParsedLayouts);
				json_value_free(pJson);
				return false;
			}

			json_settings LayoutSettings{};
			LayoutSettings.settings = json_enable_comments;
			char aLayoutError[256];
			json_value *pLayoutJson = json_parse_ex(&LayoutSettings, static_cast<const json_char *>(pLayoutData), LayoutLength, aLayoutError);
			free(pLayoutData);
			if(pLayoutJson == nullptr)
			{
				log_error("input_overlay", "Failed to parse layout file '%s': '%s'", pLayoutPath, aLayoutError);
				ClearLayouts(ParsedLayouts);
				json_value_free(pJson);
				return false;
			}
			if(pLayoutJson->type != json_object)
			{
				log_error("input_overlay", "Layout file '%s' must be a JSON object", pLayoutPath);
				json_value_free(pLayoutJson);
				ClearLayouts(ParsedLayouts);
				json_value_free(pJson);
				return false;
			}

			SObsLayout Layout;
			const json_value &LayoutRoot = *pLayoutJson;
			const bool Result = ParseObsConfiguration(LayoutRoot, pLayoutPath, pImageOverride, OffsetX, OffsetY, Layout);
			json_value_free(pLayoutJson);
			if(!Result)
			{
				ClearLayouts(ParsedLayouts);
				json_value_free(pJson);
				return false;
			}
			if(HasEntryPressedOffset)
			{
				Layout.m_PressedOffsetY = EntryPressedOffset;
				Layout.m_HasPressedOffset = EntryPressedOffset > 0;
			}
			else if(HasRootPressedOffset)
			{
				Layout.m_PressedOffsetY = RootPressedOffset;
				Layout.m_HasPressedOffset = RootPressedOffset > 0;
			}

			ParsedLayouts.push_back(std::move(Layout));
		}

		ClearObsLayouts();
		m_ConfigMode = EConfigMode::OBS;
		m_vObsLayouts = std::move(ParsedLayouts);
		m_ObsColor = ObsColor;
		m_ObsActiveColor = ObsActiveColor;
		m_ObsInactiveAlpha = ObsInactiveAlpha;
		m_PosXPercent = std::clamp(PosX, 0.0f, 100.0f);
		m_PosYPercent = std::clamp(PosY, 0.0f, 100.0f);

		json_value_free(pJson);
		return true;
	}

	const json_value &LayoutValue = Root["layout"];
	const json_value &ImageValue = Root["image"];
	const char *pImageOverride = ImageValue.type == json_string ? ImageValue.u.string.ptr : nullptr;
	if(LayoutValue.type == json_string)
	{
		void *pLayoutData;
		unsigned LayoutLength;
		const char *pLayoutPath = LayoutValue.u.string.ptr;
		if(!Storage()->ReadFile(pLayoutPath, IStorage::TYPE_ALL, &pLayoutData, &LayoutLength))
		{
			log_error("input_overlay", "Failed to read layout file '%s'", pLayoutPath);
			json_value_free(pJson);
			return false;
		}

		json_settings LayoutSettings{};
		LayoutSettings.settings = json_enable_comments;
		char aLayoutError[256];
		json_value *pLayoutJson = json_parse_ex(&LayoutSettings, static_cast<const json_char *>(pLayoutData), LayoutLength, aLayoutError);
		free(pLayoutData);
		if(pLayoutJson == nullptr)
		{
			log_error("input_overlay", "Failed to parse layout file '%s': '%s'", pLayoutPath, aLayoutError);
			json_value_free(pJson);
			return false;
		}
		if(pLayoutJson->type != json_object)
		{
			log_error("input_overlay", "Layout file '%s' must be a JSON object", pLayoutPath);
			json_value_free(pLayoutJson);
			json_value_free(pJson);
			return false;
		}

		SObsLayout Layout;
		const json_value &LayoutRoot = *pLayoutJson;
		const bool Result = ParseObsConfiguration(LayoutRoot, pLayoutPath, pImageOverride, RootOffsetX, RootOffsetY, Layout);
		json_value_free(pLayoutJson);
		if(!Result)
		{
			json_value_free(pJson);
			return false;
		}
		if(HasRootPressedOffset)
		{
			Layout.m_PressedOffsetY = RootPressedOffset;
			Layout.m_HasPressedOffset = RootPressedOffset > 0;
		}

		ClearObsLayouts();
		m_ConfigMode = EConfigMode::OBS;
		m_vObsLayouts.clear();
		m_vObsLayouts.push_back(std::move(Layout));
		m_ObsColor = ObsColor;
		m_ObsActiveColor = ObsActiveColor;
		m_ObsInactiveAlpha = ObsInactiveAlpha;
		m_PosXPercent = std::clamp(PosX, 0.0f, 100.0f);
		m_PosYPercent = std::clamp(PosY, 0.0f, 100.0f);

		json_value_free(pJson);
		return true;
	}

	const json_value &ElementsProbe = Root["elements"];
	const json_value &OverlayWidthProbe = Root["overlay_width"];
	const json_value &DefaultWidthProbe = Root["default_width"];
	const bool LooksLikeObs = OverlayWidthProbe.type == json_integer || OverlayWidthProbe.type == json_double ||
		DefaultWidthProbe.type == json_integer || DefaultWidthProbe.type == json_double ||
		(ElementsProbe.type == json_array && ElementsProbe.u.array.length > 0 &&
			ElementsProbe[0].type == json_object && ElementsProbe[0]["mapping"].type == json_array && ElementsProbe[0]["pos"].type == json_array);
	if(LooksLikeObs)
	{
		SObsLayout Layout;
		const bool Result = ParseObsConfiguration(Root, CONFIGURATION_FILENAME, pImageOverride, RootOffsetX, RootOffsetY, Layout);
		if(!Result)
		{
			json_value_free(pJson);
			return false;
		}

		ClearObsLayouts();
		m_ConfigMode = EConfigMode::OBS;
		m_vObsLayouts.clear();
		m_vObsLayouts.push_back(std::move(Layout));
		m_ObsColor = ObsColor;
		m_ObsActiveColor = ObsActiveColor;
		m_ObsInactiveAlpha = ObsInactiveAlpha;
		m_PosXPercent = std::clamp(PosX, 0.0f, 100.0f);
		m_PosYPercent = std::clamp(PosY, 0.0f, 100.0f);

		json_value_free(pJson);
		return true;
	}

	SElementDefaults Defaults;
	if(const json_value &DefaultsObj = Root["defaults"]; DefaultsObj.type == json_object)
	{
		ParseStyleObject(DefaultsObj, Defaults.m_Style);
		if(const json_value &ShapeValue = DefaultsObj["shape"]; ShapeValue.type == json_string)
			Defaults.m_Shape = ParseShape(ShapeValue.u.string.ptr);
		if(const json_value &SkewValue = DefaultsObj["skew"]; ParseFloat(SkewValue, Defaults.m_Skew))
		{
		}
	}

	float CanvasW = m_CanvasWidth;
	float CanvasH = m_CanvasHeight;
	const json_value &Canvas = Root["canvas"];
	if(Canvas.type == json_object)
	{
		if(const json_value &WidthValue = Canvas["width"]; ParseFloat(WidthValue, CanvasW))
		{
		}
		if(const json_value &HeightValue = Canvas["height"]; ParseFloat(HeightValue, CanvasH))
		{
		}
	}

	const json_value &Elements = Root["elements"];
	if(Elements.type != json_array)
	{
		log_error("input_overlay", "Failed to parse configuration: attribute 'elements' must specify an array");
		json_value_free(pJson);
		return false;
	}

	std::vector<SElement> ParsedElements;
	ParsedElements.reserve(Elements.u.array.length);
	for(unsigned i = 0; i < Elements.u.array.length; ++i)
	{
		SElement Element;
		if(!ParseElement(Elements[i], Defaults, Element))
		{
			log_error("input_overlay", "Failed to parse configuration: invalid element at index '%u'", i);
			json_value_free(pJson);
			return false;
		}
		ParsedElements.push_back(std::move(Element));
	}

	m_vElements = std::move(ParsedElements);
	m_CanvasWidth = CanvasW;
	m_CanvasHeight = CanvasH;
	if(m_ConfigMode != EConfigMode::VECTOR)
		ClearObsLayouts();
	m_ConfigMode = EConfigMode::VECTOR;
	m_PosXPercent = std::clamp(PosX, 0.0f, 100.0f);
	m_PosYPercent = std::clamp(PosY, 0.0f, 100.0f);

	json_value_free(pJson);
	return true;
}

void CInputOverlay::ParseStyleObject(const json_value &Object, SStyle &Style) const
{
	const json_value &Fill = Object["fill"];
	if(Fill.type == json_string)
	{
		if(auto Parsed = ParseColor(Fill); Parsed.has_value())
			Style.m_FillColor = Parsed.value();
	}
	else if(Fill.type == json_object)
	{
		if(auto Parsed = ParseColor(Fill["color"]); Parsed.has_value())
			Style.m_FillColor = Parsed.value();
		if(auto Parsed = ParseColor(Fill["active_color"]); Parsed.has_value())
			Style.m_FillColorActive = Parsed.value();
		if(auto Parsed = ParseColor(Fill["active"]); Parsed.has_value())
			Style.m_FillColorActive = Parsed.value();
	}

	const json_value &Border = Object["border"];
	if(Border.type == json_object)
	{
		if(auto Parsed = ParseColor(Border["color"]); Parsed.has_value())
			Style.m_BorderColor = Parsed.value();
		if(auto Parsed = ParseColor(Border["active_color"]); Parsed.has_value())
			Style.m_BorderColorActive = Parsed.value();
		if(auto Parsed = ParseColor(Border["active"]); Parsed.has_value())
			Style.m_BorderColorActive = Parsed.value();

		if(const json_value &WidthValue = Border["width"]; WidthValue.type == json_double || WidthValue.type == json_integer)
			ParseFloat(WidthValue, Style.m_BorderWidth);

		if(const json_value &GradientValue = Border["gradient"]; GradientValue.type == json_string)
			Style.m_BorderRainbow = str_comp_nocase(GradientValue.u.string.ptr, "rainbow") == 0;
		if(const json_value &GradientActiveValue = Border["gradient_active"]; GradientActiveValue.type == json_string)
			Style.m_BorderRainbowActive = str_comp_nocase(GradientActiveValue.u.string.ptr, "rainbow") == 0;

		if(const json_value &SpeedValue = Border["rainbow_speed"]; SpeedValue.type == json_double || SpeedValue.type == json_integer)
			ParseFloat(SpeedValue, Style.m_BorderRainbowSpeed);
		if(const json_value &OffsetValue = Border["rainbow_offset"]; OffsetValue.type == json_double || OffsetValue.type == json_integer)
			ParseFloat(OffsetValue, Style.m_BorderRainbowOffset);
	}

	const json_value &Text = Object["text"];
	if(Text.type == json_object)
	{
		if(auto Parsed = ParseColor(Text["color"]); Parsed.has_value())
			Style.m_TextColor = Parsed.value();
		if(auto Parsed = ParseColor(Text["active_color"]); Parsed.has_value())
			Style.m_TextColorActive = Parsed.value();
		if(auto Parsed = ParseColor(Text["active"]); Parsed.has_value())
			Style.m_TextColorActive = Parsed.value();
		if(const json_value &SizeValue = Text["size"]; SizeValue.type == json_double || SizeValue.type == json_integer)
			ParseFloat(SizeValue, Style.m_TextSize);
		if(const json_value &OffsetX = Text["offset_x"]; OffsetX.type == json_double || OffsetX.type == json_integer)
			ParseFloat(OffsetX, Style.m_TextOffsetX);
		if(const json_value &OffsetY = Text["offset_y"]; OffsetY.type == json_double || OffsetY.type == json_integer)
			ParseFloat(OffsetY, Style.m_TextOffsetY);
		if(const json_value &Enabled = Text["enabled"]; Enabled.type == json_boolean)
			Style.m_TextEnabled = Enabled.u.boolean != 0;
	}
}

bool CInputOverlay::ParseElement(const json_value &Object, const SElementDefaults &Defaults, SElement &Out) const
{
	if(Object.type != json_object)
		return false;

	Out.m_Style = Defaults.m_Style;
	Out.m_Shape = Defaults.m_Shape;
	Out.m_Skew = Defaults.m_Skew;
	Out.m_InputKind = EInputKind::NONE;
	Out.m_Key = 0;
	Out.m_MouseButton = 0;

	if(const json_value &IdValue = Object["id"]; IdValue.type == json_string)
		Out.m_Id = IdValue.u.string.ptr;
	if(const json_value &LabelValue = Object["label"]; LabelValue.type == json_string)
		Out.m_Label = LabelValue.u.string.ptr;

	const json_value &InputValue = Object["input"];
	const json_value &KeyValue = Object["key"];
	const json_value &MouseValue = Object["mouse"];
	if(InputValue.type == json_string)
	{
		if(!ParseInputBinding(InputValue, Out))
			return false;
	}
	else if(KeyValue.type == json_string)
	{
		if(!ParseInputBinding(KeyValue, Out))
			return false;
	}
	else if(MouseValue.type == json_string)
	{
		if(!ParseInputBinding(MouseValue, Out))
			return false;
	}

	if(const json_value &ShapeValue = Object["shape"]; ShapeValue.type == json_string)
		Out.m_Shape = ParseShape(ShapeValue.u.string.ptr);

	if(!ParseFloat(Object["x"], Out.m_X))
		return false;
	if(!ParseFloat(Object["y"], Out.m_Y))
		return false;
	if(!ParseFloat(Object["w"], Out.m_W))
		return false;
	if(!ParseFloat(Object["h"], Out.m_H))
		return false;

	if(const json_value &SkewValue = Object["skew"]; SkewValue.type == json_double || SkewValue.type == json_integer)
		ParseFloat(SkewValue, Out.m_Skew);

	ParseStyleObject(Object, Out.m_Style);
	if(const json_value &StyleObject = Object["style"]; StyleObject.type == json_object)
		ParseStyleObject(StyleObject, Out.m_Style);

	return true;
}

bool CInputOverlay::ParseFloat(const json_value &Value, float &Out) const
{
	if(Value.type == json_integer)
	{
		Out = (float)Value.u.integer;
		return true;
	}
	if(Value.type == json_double)
	{
		Out = (float)Value.u.dbl;
		return true;
	}
	return false;
}

bool CInputOverlay::ParseFloatArray(const json_value &Value, float *pOut, int Count) const
{
	if(Value.type != json_array || (int)Value.u.array.length < Count)
		return false;
	for(int i = 0; i < Count; ++i)
	{
		if(!ParseFloat(Value[i], pOut[i]))
			return false;
	}
	return true;
}

void CInputOverlay::ParsePosition(const json_value &Root, float &PosX, float &PosY, bool &HasPosX, bool &HasPosY) const
{
	const json_value &Position = Root["position"];
	if(Position.type == json_object)
	{
		if(const json_value &PosXValue = Position["x"]; ParseFloat(PosXValue, PosX))
			HasPosX = true;
		if(const json_value &PosYValue = Position["y"]; ParseFloat(PosYValue, PosY))
			HasPosY = true;
	}

	if(const json_value &PosXValue = Root["x"]; ParseFloat(PosXValue, PosX))
		HasPosX = true;
	if(const json_value &PosYValue = Root["y"]; ParseFloat(PosYValue, PosY))
		HasPosY = true;
}

void CInputOverlay::ParseOffset(const json_value &Root, float &OffsetX, float &OffsetY) const
{
	const json_value &Offset = Root["offset"];
	if(Offset.type == json_object)
	{
		ParseFloat(Offset["x"], OffsetX);
		ParseFloat(Offset["y"], OffsetY);
	}
	else if(Offset.type == json_array)
	{
		float Values[2];
		if(ParseFloatArray(Offset, Values, 2))
		{
			OffsetX = Values[0];
			OffsetY = Values[1];
		}
	}

	ParseFloat(Root["offset_x"], OffsetX);
	ParseFloat(Root["offset_y"], OffsetY);
}

std::optional<ColorRGBA> CInputOverlay::ParseColor(const json_value &Value) const
{
	if(Value.type != json_string)
		return {};

	const char *pStr = Value.u.string.ptr;
	if(!pStr || pStr[0] == '\0')
		return {};

	if(pStr[0] == '#')
		++pStr;
	if(pStr[0] == '0' && (pStr[1] == 'x' || pStr[1] == 'X'))
		pStr += 2;

	return color_parse<ColorRGBA>(pStr);
}

CInputOverlay::EShape CInputOverlay::ParseShape(const char *pName) const
{
	if(pName == nullptr)
		return EShape::RECT;
	if(str_comp_nocase(pName, "parallelogram") == 0 || str_comp_nocase(pName, "para") == 0 || str_comp_nocase(pName, "slant") == 0)
		return EShape::PARALLELOGRAM;
	return EShape::RECT;
}

bool CInputOverlay::ParseInputBinding(const json_value &Value, SElement &Out) const
{
	if(Value.type != json_string)
		return false;
	const char *pName = Value.u.string.ptr;
	if(pName == nullptr || pName[0] == '\0')
		return false;

	if(str_comp_nocase(pName, "always") == 0)
	{
		Out.m_InputKind = EInputKind::ALWAYS;
		return true;
	}

	const int MouseButton = ParseMouseButton(pName);
	if(MouseButton > 0)
	{
		Out.m_InputKind = EInputKind::MOUSE;
		Out.m_MouseButton = MouseButton;
		return true;
	}

	const int Key = Input()->FindKeyByName(pName);
	if(Key != KEY_UNKNOWN)
	{
		Out.m_InputKind = EInputKind::KEY;
		Out.m_Key = Key;
		return true;
	}

	return false;
}

int CInputOverlay::ParseMouseButton(const char *pName) const
{
	if(pName == nullptr)
		return 0;
	if(str_comp_nocase(pName, "mouse1") == 0 || str_comp_nocase(pName, "lmb") == 0 || str_comp_nocase(pName, "mouse_left") == 0)
		return 1;
	if(str_comp_nocase(pName, "mouse2") == 0 || str_comp_nocase(pName, "rmb") == 0 || str_comp_nocase(pName, "mouse_right") == 0)
		return 3;
	if(str_comp_nocase(pName, "mouse3") == 0 || str_comp_nocase(pName, "mmb") == 0 || str_comp_nocase(pName, "mouse_middle") == 0)
		return 2;
	if(str_comp_nocase(pName, "mouse4") == 0 || str_comp_nocase(pName, "x1") == 0 || str_comp_nocase(pName, "mb4") == 0)
		return 4;
	if(str_comp_nocase(pName, "mouse5") == 0 || str_comp_nocase(pName, "x2") == 0 || str_comp_nocase(pName, "mb5") == 0)
		return 5;
	if(str_comp_nocase(pName, "mouse") == 0)
		return 0;
	if(str_startswith_nocase(pName, "mouse"))
	{
		const char *pNum = pName + 5;
		if(pNum[0] == '_')
			++pNum;
		const int Parsed = str_toint(pNum);
		if(Parsed >= 1 && Parsed <= 8)
			return Parsed;
	}
	return 0;
}

bool CInputOverlay::ParseObsConfiguration(const json_value &Root, const char *pLayoutPath, const char *pImageOverride, float OffsetX, float OffsetY, SObsLayout &Out)
{
	float DefaultW = 0.0f;
	float DefaultH = 0.0f;
	float OverlayW = 0.0f;
	float OverlayH = 0.0f;

	if(const json_value &Value = Root["default_width"]; ParseFloat(Value, DefaultW))
	{
	}
	if(const json_value &Value = Root["default_height"]; ParseFloat(Value, DefaultH))
	{
	}
	if(const json_value &Value = Root["overlay_width"]; ParseFloat(Value, OverlayW))
	{
	}
	if(const json_value &Value = Root["overlay_height"]; ParseFloat(Value, OverlayH))
	{
	}

	const json_value &Elements = Root["elements"];
	if(Elements.type != json_array)
	{
		log_error("input_overlay", "Failed to parse OBS layout: attribute 'elements' must specify an array");
		return false;
	}

	std::vector<SObsElement> ParsedElements;
	ParsedElements.reserve(Elements.u.array.length);
	for(unsigned i = 0; i < Elements.u.array.length; ++i)
	{
		SObsElement Element;
		if(!ParseObsElement(Elements[i], Element))
		{
			log_error("input_overlay", "Failed to parse OBS layout: invalid element at index '%u'", i);
			return false;
		}
		ParsedElements.push_back(std::move(Element));
	}

	std::string ImagePath;
	if(pImageOverride != nullptr && pImageOverride[0] != '\0')
	{
		ImagePath = pImageOverride;
	}
	else if(const json_value &ImageValue = Root["image"]; ImageValue.type == json_string)
	{
		ImagePath = ImageValue.u.string.ptr;
	}
	else if(pLayoutPath != nullptr && pLayoutPath[0] != '\0')
	{
		ImagePath = pLayoutPath;
		const size_t DotPos = ImagePath.find_last_of('.');
		if(DotPos == std::string::npos)
			ImagePath.append(".png");
		else
			ImagePath.replace(DotPos, std::string::npos, ".png");
	}

	if(ImagePath.empty())
	{
		log_error("input_overlay", "Failed to parse OBS layout: missing image path");
		return false;
	}

	CImageInfo ImageInfo;
	if(!Graphics()->LoadPng(ImageInfo, ImagePath.c_str(), IStorage::TYPE_ALL))
	{
		log_error("input_overlay", "Failed to load OBS overlay image '%s'", ImagePath.c_str());
		return false;
	}
	const int TextureWidth = ImageInfo.m_Width;
	const int TextureHeight = ImageInfo.m_Height;

	if(OverlayW <= 0.0f || OverlayH <= 0.0f)
	{
		float MaxX = 0.0f;
		float MaxY = 0.0f;
		for(const SObsElement &Element : ParsedElements)
		{
			float MapW = Element.m_MapW > 0.0f ? Element.m_MapW : DefaultW;
			float MapH = Element.m_MapH > 0.0f ? Element.m_MapH : DefaultH;
			MaxX = std::max(MaxX, Element.m_PosX + MapW);
			MaxY = std::max(MaxY, Element.m_PosY + MapH);
		}
		if(OverlayW <= 0.0f)
			OverlayW = MaxX;
		if(OverlayH <= 0.0f)
			OverlayH = MaxY;
	}

	std::stable_sort(ParsedElements.begin(), ParsedElements.end(), [](const SObsElement &A, const SObsElement &B) {
		return A.m_ZLevel < B.m_ZLevel;
	});

	int PressedOffsetY = 0;
	bool HasPressedOffset = false;
	float PressedOffsetValue = 0.0f;
	if(const json_value &PressedValue = Root["pressed_offset_y"]; ParseFloat(PressedValue, PressedOffsetValue))
	{
		PressedOffsetY = (int)PressedOffsetValue;
		PressedOffsetY = std::max(0, PressedOffsetY);
		HasPressedOffset = PressedOffsetY > 0;
	}
	if(!HasPressedOffset)
	{
		const int Detected = DetectObsPressedOffset(ImageInfo, ParsedElements);
		if(Detected > 0)
		{
			PressedOffsetY = Detected;
			HasPressedOffset = true;
		}
	}

	IGraphics::CTextureHandle Texture = Graphics()->LoadTextureRawMove(ImageInfo, 0, ImagePath.c_str());
	if(!Texture.IsValid())
	{
		log_error("input_overlay", "Failed to create texture for '%s'", ImagePath.c_str());
		return false;
	}

	Out.m_vElements = std::move(ParsedElements);
	Out.m_Texture = Texture;
	Out.m_TextureWidth = TextureWidth;
	Out.m_TextureHeight = TextureHeight;
	Out.m_OverlayWidth = OverlayW;
	Out.m_OverlayHeight = OverlayH;
	Out.m_DefaultWidth = DefaultW;
	Out.m_DefaultHeight = DefaultH;
	Out.m_OffsetX = OffsetX;
	Out.m_OffsetY = OffsetY;
	Out.m_PressedOffsetY = PressedOffsetY;
	Out.m_HasPressedOffset = HasPressedOffset;
	Out.m_LayoutPath = pLayoutPath != nullptr ? pLayoutPath : CONFIGURATION_FILENAME;
	Out.m_ImagePath = ImagePath;

	return true;
}

bool CInputOverlay::ParseObsElement(const json_value &Object, SObsElement &Out) const
{
	if(Object.type != json_object)
		return false;

	auto ParseIntValue = [&](const json_value &Value, int &OutValue) {
		if(Value.type == json_integer)
		{
			OutValue = (int)Value.u.integer;
			return true;
		}
		if(Value.type == json_double)
		{
			OutValue = (int)Value.u.dbl;
			return true;
		}
		return false;
	};

	if(const json_value &IdValue = Object["id"]; IdValue.type == json_string)
		Out.m_Id = IdValue.u.string.ptr;
	if(const json_value &CodeValue = Object["code"]; ParseIntValue(CodeValue, Out.m_Code))
	{
	}
	if(const json_value &TypeValue = Object["type"]; ParseIntValue(TypeValue, Out.m_Type))
	{
	}
	if(const json_value &ZValue = Object["z_level"]; ParseIntValue(ZValue, Out.m_ZLevel))
	{
	}
	if(const json_value &MouseTypeValue = Object["mouse_type"]; ParseIntValue(MouseTypeValue, Out.m_MouseType))
	{
	}
	bool HasActiveOnly = false;
	if(const json_value &ActiveOnlyValue = Object["active_only"]; ActiveOnlyValue.type == json_boolean)
	{
		Out.m_ActiveOnly = ActiveOnlyValue.u.boolean != 0;
		HasActiveOnly = true;
	}

	float Mapping[4];
	float Position[2];
	if(!ParseFloatArray(Object["mapping"], Mapping, 4))
		return false;
	if(!ParseFloatArray(Object["pos"], Position, 2))
		return false;

	Out.m_MapX = Mapping[0];
	Out.m_MapY = Mapping[1];
	Out.m_MapW = Mapping[2];
	Out.m_MapH = Mapping[3];
	Out.m_PosX = Position[0];
	Out.m_PosY = Position[1];

	switch(Out.m_Type)
	{
	case 1:
		Out.m_Key = KeyFromObsId(Out.m_Id.c_str(), Out.m_Code);
		if(Out.m_Key != KEY_UNKNOWN)
			Out.m_InputKind = EObsInputKind::KEY;
		break;
	case 3:
		Out.m_MouseButton = MouseButtonFromObsId(Out.m_Id.c_str(), Out.m_Code);
		if(Out.m_MouseButton > 0)
			Out.m_InputKind = EObsInputKind::MOUSE;
		break;
	case 4:
		Out.m_InputKind = EObsInputKind::WHEEL;
		Out.m_WheelDir = WheelDirFromObsId(Out.m_Id.c_str());
		if(Out.m_WheelDir != 0 && !HasActiveOnly)
			Out.m_ActiveOnly = true;
		break;
	case 9:
		Out.m_InputKind = EObsInputKind::MOUSE_MOVE;
		break;
	default:
		Out.m_InputKind = EObsInputKind::NONE;
		break;
	}

	return true;
}

int CInputOverlay::KeyFromObsId(const char *pId, int Code) const
{
	if(pId != nullptr && pId[0] != '\0')
	{
		std::string Name = pId;
		for(char &Ch : Name)
			Ch = (char)std::tolower((unsigned char)Ch);

		if(Name == "shift")
			Name = "lshift";
		else if(Name == "ctrl")
			Name = "lctrl";
		else if(Name == "alt")
			Name = "lalt";

		const int Key = Input()->FindKeyByName(Name.c_str());
		if(Key != KEY_UNKNOWN)
			return Key;
	}

	if(Code > 0)
	{
		static const struct
		{
			int m_Code;
			const char *m_Name;
		} s_aCodeMap[] = {
			{15, "tab"},
			{16, "q"},
			{17, "w"},
			{18, "e"},
			{19, "r"},
			{30, "a"},
			{31, "s"},
			{32, "d"},
			{33, "f"},
			{42, "lshift"},
			{29, "lctrl"},
			{56, "lalt"},
			{44, "z"},
			{45, "x"},
			{46, "c"},
			{47, "v"},
			{57, "space"},
		};
		for(const auto &Entry : s_aCodeMap)
		{
			if(Entry.m_Code == Code)
			{
				const int Key = Input()->FindKeyByName(Entry.m_Name);
				if(Key != KEY_UNKNOWN)
					return Key;
				break;
			}
		}
	}

	return KEY_UNKNOWN;
}

int CInputOverlay::MouseButtonFromObsId(const char *pId, int Code) const
{
	if(pId != nullptr && pId[0] != '\0')
	{
		if(str_comp_nocase(pId, "lmb") == 0 || str_comp_nocase(pId, "mouse1") == 0 || str_comp_nocase(pId, "mouse_left") == 0)
			return 1;
		if(str_comp_nocase(pId, "rmb") == 0 || str_comp_nocase(pId, "mouse2") == 0 || str_comp_nocase(pId, "mouse_right") == 0)
			return 3;
		if(str_comp_nocase(pId, "mmb") == 0 || str_comp_nocase(pId, "mouse3") == 0 || str_comp_nocase(pId, "mouse_middle") == 0)
			return 2;
		if(str_comp_nocase(pId, "smb1") == 0 || str_comp_nocase(pId, "mb4") == 0 || str_comp_nocase(pId, "mouse4") == 0)
			return 4;
		if(str_comp_nocase(pId, "smb2") == 0 || str_comp_nocase(pId, "mb5") == 0 || str_comp_nocase(pId, "mouse5") == 0)
			return 5;
	}

	if(Code >= 1 && Code <= 5)
	{
		switch(Code)
		{
		case 1:
			return 1;
		case 2:
			return 3;
		case 3:
			return 2;
		case 4:
			return 4;
		case 5:
			return 5;
		default:
			break;
		}
	}

	return 0;
}

int CInputOverlay::WheelDirFromObsId(const char *pId) const
{
	if(pId == nullptr || pId[0] == '\0')
		return 0;
	if(str_comp_nocase(pId, "wheel_up") == 0 || str_comp_nocase(pId, "wheelup") == 0 ||
		str_comp_nocase(pId, "mousewheelup") == 0 || str_comp_nocase(pId, "mwheelup") == 0 ||
		str_comp_nocase(pId, "scroll_up") == 0 || str_comp_nocase(pId, "scrollup") == 0)
		return 1;
	if(str_comp_nocase(pId, "wheel_down") == 0 || str_comp_nocase(pId, "wheeldown") == 0 ||
		str_comp_nocase(pId, "mousewheeldown") == 0 || str_comp_nocase(pId, "mwheeldown") == 0 ||
		str_comp_nocase(pId, "scroll_down") == 0 || str_comp_nocase(pId, "scrolldown") == 0)
		return 2;
	if(str_comp_nocase(pId, "wheel_left") == 0 || str_comp_nocase(pId, "wheelleft") == 0 ||
		str_comp_nocase(pId, "mousewheelleft") == 0 || str_comp_nocase(pId, "mwheelleft") == 0 ||
		str_comp_nocase(pId, "scroll_left") == 0 || str_comp_nocase(pId, "scrollleft") == 0)
		return 3;
	if(str_comp_nocase(pId, "wheel_right") == 0 || str_comp_nocase(pId, "wheelright") == 0 ||
		str_comp_nocase(pId, "mousewheelright") == 0 || str_comp_nocase(pId, "mwheelright") == 0 ||
		str_comp_nocase(pId, "scroll_right") == 0 || str_comp_nocase(pId, "scrollright") == 0)
		return 4;
	return 0;
}

bool CInputOverlay::IsObsActive(const SObsElement &Element) const
{
	switch(Element.m_InputKind)
	{
	case EObsInputKind::NONE:
		return true;
	case EObsInputKind::KEY:
		return Element.m_Key > KEY_UNKNOWN && Input()->KeyIsPressed(Element.m_Key);
	case EObsInputKind::MOUSE:
		return Element.m_MouseButton > 0 && Input()->NativeMousePressed(Element.m_MouseButton);
	case EObsInputKind::WHEEL:
		switch(Element.m_WheelDir)
		{
		case 1:
			return m_aWheelAlpha[0] > 0.0f;
		case 2:
			return m_aWheelAlpha[1] > 0.0f;
		case 3:
			return m_aWheelAlpha[2] > 0.0f;
		case 4:
			return m_aWheelAlpha[3] > 0.0f;
		default:
			return std::max(std::max(m_aWheelAlpha[0], m_aWheelAlpha[1]), std::max(m_aWheelAlpha[2], m_aWheelAlpha[3])) > 0.0f;
		}
	case EObsInputKind::MOUSE_MOVE:
		return std::fabs(m_MouseDeltaX) > 0.01f || std::fabs(m_MouseDeltaY) > 0.01f;
	default:
		return false;
	}
}

bool CInputOverlay::GetConfigModifiedTime(time_t &OutModified) const
{
	bool Found = false;
	time_t Latest = 0;
	time_t Created;
	time_t Modified;

	auto CheckFile = [&](const std::string &Path) {
		if(Path.empty())
			return;
		for(int Type = IStorage::TYPE_SAVE; Type < Storage()->NumPaths(); ++Type)
		{
			if(!Storage()->FileExists(Path.c_str(), Type))
				continue;
			if(Storage()->RetrieveTimes(Path.c_str(), Type, &Created, &Modified))
			{
				if(!Found || Modified > Latest)
					Latest = Modified;
				Found = true;
				break;
			}
		}
	};

	CheckFile(CONFIGURATION_FILENAME);
	for(const SObsLayout &Layout : m_vObsLayouts)
	{
		CheckFile(Layout.m_LayoutPath);
		CheckFile(Layout.m_ImagePath);
	}

	if(Found)
		OutModified = Latest;
	return Found;
}

int CInputOverlay::DetectObsPressedOffset(const CImageInfo &Image, const std::vector<SObsElement> &Elements) const
{
	if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
		return 0;

	constexpr int SampleCols = 6;
	constexpr int SampleRows = 6;
	constexpr float AlphaThreshold = 0.1f;
	constexpr float MinColorDiff = 12.0f / 255.0f;

	auto BuildSamples = [&](const SObsElement &Element, std::vector<vec2> &OutPositions, std::vector<ColorRGBA> &OutColors) {
		const int BaseX = (int)std::round(Element.m_MapX);
		const int BaseY = (int)std::round(Element.m_MapY);
		const int MapW = (int)std::round(Element.m_MapW);
		const int MapH = (int)std::round(Element.m_MapH);
		if(MapW <= 0 || MapH <= 0)
			return;
		if(BaseX < 0 || BaseY < 0)
			return;
		if(BaseX + MapW > (int)Image.m_Width || BaseY + MapH > (int)Image.m_Height)
			return;

		OutPositions.clear();
		OutColors.clear();
		OutPositions.reserve(SampleCols * SampleRows);
		OutColors.reserve(SampleCols * SampleRows);
		for(int Sy = 0; Sy < SampleRows; ++Sy)
		{
			const int Y = BaseY + (int)((Sy + 0.5f) * MapH / SampleRows);
			for(int Sx = 0; Sx < SampleCols; ++Sx)
			{
				const int X = BaseX + (int)((Sx + 0.5f) * MapW / SampleCols);
				if(X < 0 || Y < 0 || X >= (int)Image.m_Width || Y >= (int)Image.m_Height)
					continue;
				const ColorRGBA Base = Image.PixelColor(X, Y);
				if(Base.a <= AlphaThreshold)
					continue;
				OutPositions.push_back(vec2(X, Y));
				OutColors.push_back(Base);
			}
		}
	};

	const SObsElement *pSample = nullptr;
	std::vector<vec2> SamplePositions;
	std::vector<ColorRGBA> SampleColors;
	for(const SObsElement &Element : Elements)
	{
		if(Element.m_InputKind != EObsInputKind::KEY &&
			Element.m_InputKind != EObsInputKind::MOUSE &&
			Element.m_InputKind != EObsInputKind::WHEEL)
			continue;
		if(Element.m_MapW <= 1.0f || Element.m_MapH <= 1.0f)
			continue;

		BuildSamples(Element, SamplePositions, SampleColors);
		if(SamplePositions.size() >= 4)
		{
			pSample = &Element;
			break;
		}
	}

	if(pSample == nullptr)
		return 0;

	const int BaseY = (int)std::round(pSample->m_MapY);
	const int MapH = (int)std::round(pSample->m_MapH);
	if(MapH <= 0)
		return 0;

	const int MaxOffset = std::min((int)Image.m_Height - (BaseY + MapH), MapH * 3);
	const int MinOffset = std::max(1, MapH / 2);
	if(MaxOffset < MinOffset)
		return 0;

	int BestOffset = 0;
	float BestScore = 0.0f;
	float BestMatch = 0.0f;
	for(int Offset = MinOffset; Offset <= MaxOffset; ++Offset)
	{
		int OpaqueCount = 0;
		float ColorDiffSum = 0.0f;
		int ColorSamples = 0;
		for(size_t i = 0; i < SamplePositions.size(); ++i)
		{
			const int X = (int)SamplePositions[i].x;
			const int Y = (int)SamplePositions[i].y + Offset;
			if(Y < 0 || Y >= (int)Image.m_Height)
				continue;
			const ColorRGBA Other = Image.PixelColor(X, Y);
			if(Other.a <= AlphaThreshold)
				continue;
			++OpaqueCount;
			const ColorRGBA Base = SampleColors[i];
			const float Diff = (std::fabs(Base.r - Other.r) +
						std::fabs(Base.g - Other.g) +
						std::fabs(Base.b - Other.b)) /
					       3.0f;
			ColorDiffSum += Diff;
			++ColorSamples;
		}

		const float MatchRatio = SamplePositions.empty() ? 0.0f : (float)OpaqueCount / SamplePositions.size();
		const float ColorDiff = ColorSamples > 0 ? ColorDiffSum / ColorSamples : 0.0f;
		if(ColorDiff < MinColorDiff)
			continue;

		const float Score = MatchRatio * (0.5f + ColorDiff);
		if(Score > BestScore)
		{
			BestScore = Score;
			BestOffset = Offset;
			BestMatch = MatchRatio;
		}
	}

	if(BestOffset <= 0 || BestMatch < 0.6f)
		return 0;

	return BestOffset;
}

void CInputOverlay::ClearObsLayouts()
{
	for(SObsLayout &Layout : m_vObsLayouts)
	{
		if(Layout.m_Texture.IsValid())
			Graphics()->UnloadTexture(&Layout.m_Texture);
	}
	m_vObsLayouts.clear();
}

bool CInputOverlay::IsActiveInput(const SElement &Element) const
{
	switch(Element.m_InputKind)
	{
	case EInputKind::ALWAYS:
		return true;
	case EInputKind::KEY:
		return Element.m_Key > KEY_UNKNOWN && Input()->KeyIsPressed(Element.m_Key);
	case EInputKind::MOUSE:
		return Element.m_MouseButton > 0 && Input()->NativeMousePressed(Element.m_MouseButton);
	default:
		return false;
	}
}
