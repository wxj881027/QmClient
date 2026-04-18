#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_INPUT_OVERLAY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_INPUT_OVERLAY_H

#include <base/color.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <optional>
#include <string>
#include <vector>
#include <ctime>

typedef struct _json_value json_value;

class CInputOverlay : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;

private:
	enum class EConfigMode
	{
		VECTOR,
		OBS
	};

	enum class EShape
	{
		RECT,
		PARALLELOGRAM
	};

	enum class EInputKind
	{
		NONE,
		ALWAYS,
		KEY,
		MOUSE
	};

	enum class EObsInputKind
	{
		NONE,
		KEY,
		MOUSE,
		WHEEL,
		MOUSE_MOVE
	};

	struct SStyle
	{
		ColorRGBA m_FillColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f);
		ColorRGBA m_FillColorActive = ColorRGBA(0.18f, 0.6f, 1.0f, 0.85f);
		ColorRGBA m_BorderColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f);
		ColorRGBA m_BorderColorActive = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		float m_BorderWidth = 2.0f;
		bool m_BorderRainbow = false;
		bool m_BorderRainbowActive = false;
		float m_BorderRainbowSpeed = 0.6f;
		float m_BorderRainbowOffset = 0.0f;
		float m_TextSize = 16.0f;
		ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f);
		ColorRGBA m_TextColorActive = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		float m_TextOffsetX = 0.0f;
		float m_TextOffsetY = 0.0f;
		bool m_TextEnabled = true;
	};

	struct SElementDefaults
	{
		SStyle m_Style;
		EShape m_Shape = EShape::RECT;
		float m_Skew = 0.0f;
	};

	struct SElement
	{
		std::string m_Id;
		std::string m_Label;
		EInputKind m_InputKind = EInputKind::NONE;
		int m_Key = 0;
		int m_MouseButton = 0;
		EShape m_Shape = EShape::RECT;
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
		float m_Skew = 0.0f;
		SStyle m_Style;
	};

	struct SObsElement
	{
		std::string m_Id;
		int m_Code = 0;
		int m_Type = 0;
		int m_ZLevel = 0;
		int m_MouseType = 0;
		int m_WheelDir = 0;
		bool m_ActiveOnly = false;
		EObsInputKind m_InputKind = EObsInputKind::NONE;
		int m_Key = 0;
		int m_MouseButton = 0;
		float m_MapX = 0.0f;
		float m_MapY = 0.0f;
		float m_MapW = 0.0f;
		float m_MapH = 0.0f;
		float m_PosX = 0.0f;
		float m_PosY = 0.0f;
	};

	struct SObsLayout
	{
		std::vector<SObsElement> m_vElements;
		IGraphics::CTextureHandle m_Texture;
		int m_TextureWidth = 0;
		int m_TextureHeight = 0;
		float m_OverlayWidth = 0.0f;
		float m_OverlayHeight = 0.0f;
		float m_DefaultWidth = 0.0f;
		float m_DefaultHeight = 0.0f;
		float m_OffsetX = 0.0f;
		float m_OffsetY = 0.0f;
		int m_PressedOffsetY = 0;
		bool m_HasPressedOffset = false;
		std::string m_LayoutPath;
		std::string m_ImagePath;
	};

	bool LoadConfiguration(int StorageType);
	bool ParseConfiguration(const void *pFileData, unsigned FileLength);
	void ParseStyleObject(const json_value &Object, SStyle &Style) const;
	bool ParseElement(const json_value &Object, const SElementDefaults &Defaults, SElement &Out) const;
	bool ParseFloat(const json_value &Value, float &Out) const;
	bool ParseFloatArray(const json_value &Value, float *pOut, int Count) const;
	std::optional<ColorRGBA> ParseColor(const json_value &Value) const;
	EShape ParseShape(const char *pName) const;
	bool ParseInputBinding(const json_value &Value, SElement &Out) const;
	int ParseMouseButton(const char *pName) const;
	void ParsePosition(const json_value &Root, float &PosX, float &PosY, bool &HasPosX, bool &HasPosY) const;
	void ParseOffset(const json_value &Root, float &OffsetX, float &OffsetY) const;

	bool ParseObsConfiguration(const json_value &Root, const char *pLayoutPath, const char *pImageOverride, float OffsetX, float OffsetY, SObsLayout &Out);
	bool ParseObsElement(const json_value &Object, SObsElement &Out) const;
	int KeyFromObsId(const char *pId, int Code) const;
	int MouseButtonFromObsId(const char *pId, int Code) const;
	int WheelDirFromObsId(const char *pId) const;
	bool IsObsActive(const SObsElement &Element) const;
	bool GetConfigModifiedTime(time_t &OutModified) const;
	int DetectObsPressedOffset(const CImageInfo &Image, const std::vector<SObsElement> &Elements) const;
	void ClearObsLayouts();

	bool IsActiveInput(const SElement &Element) const;

	EConfigMode m_ConfigMode = EConfigMode::VECTOR;
	std::vector<SElement> m_vElements;
	std::vector<SObsLayout> m_vObsLayouts;
	ColorRGBA m_ObsColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	ColorRGBA m_ObsActiveColor = ColorRGBA(1.0f, 0.9f, 0.2f, 1.0f);
	float m_ObsInactiveAlpha = 0.65f;
	float m_CanvasWidth = 320.0f;
	float m_CanvasHeight = 120.0f;
	bool m_ConfigLoaded = false;
	bool m_ConfigValid = false;
	float m_Time = 0.0f;
	float m_ConfigCheckTimer = 0.0f;
	time_t m_ConfigModifiedTime = 0;
	bool m_HasConfigModifiedTime = false;
	float m_PosXPercent = 71.0f;
	float m_PosYPercent = 80.0f;
	float m_LastMouseX = 0.0f;
	float m_LastMouseY = 0.0f;
	float m_MouseDeltaX = 0.0f;
	float m_MouseDeltaY = 0.0f;
	float m_aWheelLastTime[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
	float m_aWheelAlpha[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	bool m_UsingDemoInputState = false;
	uint64_t m_LastDemoWheelSequence = 0;
};

#endif
