/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_TEXTRENDER_H
#define ENGINE_TEXTRENDER_H
#include "kernel.h"

#include <base/color.h>

#include <engine/graphics.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum
{
	TEXTFLAG_RENDER = 1 << 0,
	TEXTFLAG_DISALLOW_NEWLINE = 1 << 1,
	TEXTFLAG_STOP_AT_END = 1 << 2,
	TEXTFLAG_ELLIPSIS_AT_END = 1 << 3,
};

enum ETextAlignment
{
	TEXTALIGN_LEFT = 0,
	TEXTALIGN_CENTER = 1 << 1,
	TEXTALIGN_RIGHT = 1 << 2,
	TEXTALIGN_TOP = 0, // this is also 0, so the default alignment is top-left
	TEXTALIGN_MIDDLE = 1 << 3,
	TEXTALIGN_BOTTOM = 1 << 4,

	TEXTALIGN_TL = TEXTALIGN_TOP | TEXTALIGN_LEFT,
	TEXTALIGN_TC = TEXTALIGN_TOP | TEXTALIGN_CENTER,
	TEXTALIGN_TR = TEXTALIGN_TOP | TEXTALIGN_RIGHT,
	TEXTALIGN_ML = TEXTALIGN_MIDDLE | TEXTALIGN_LEFT,
	TEXTALIGN_MC = TEXTALIGN_MIDDLE | TEXTALIGN_CENTER,
	TEXTALIGN_MR = TEXTALIGN_MIDDLE | TEXTALIGN_RIGHT,
	TEXTALIGN_BL = TEXTALIGN_BOTTOM | TEXTALIGN_LEFT,
	TEXTALIGN_BC = TEXTALIGN_BOTTOM | TEXTALIGN_CENTER,
	TEXTALIGN_BR = TEXTALIGN_BOTTOM | TEXTALIGN_RIGHT,

	TEXTALIGN_MASK_HORIZONTAL = TEXTALIGN_LEFT | TEXTALIGN_CENTER | TEXTALIGN_RIGHT,
	TEXTALIGN_MASK_VERTICAL = TEXTALIGN_TOP | TEXTALIGN_MIDDLE | TEXTALIGN_BOTTOM,
};

enum ETextRenderFlags
{
	TEXT_RENDER_FLAG_NO_X_BEARING = 1 << 0,
	TEXT_RENDER_FLAG_NO_Y_BEARING = 1 << 1,
	TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH = 1 << 2,
	TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT = 1 << 3,
	TEXT_RENDER_FLAG_KERNING = 1 << 4,
	TEXT_RENDER_FLAG_NO_OVERSIZE = 1 << 5,
	TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING = 1 << 6,
	TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE = 1 << 7,
	TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD = 1 << 8,
	// text is only rendered once and then discarded (a hint for buffer creation)
	TEXT_RENDER_FLAG_ONE_TIME_USE = 1 << 9,
};

enum class EFontPreset
{
	DEFAULT_FONT,
	ICON_FONT,
	ICON_FONT_BOLD,
};

namespace FontIcons
{
	// Stable names keep the call sites independent from the icon font's codepoints.
	[[maybe_unused]] static const char *FONT_ICON_PLUS = "\xEE\x8F\x94";
	[[maybe_unused]] static const char *FONT_ICON_MINUS = "\xEE\x8C\xAA";
	[[maybe_unused]] static const char *FONT_ICON_LOCK = "\xEE\x8B\xBA";
	[[maybe_unused]] static const char *FONT_ICON_MAGNIFYING_GLASS = "\xEE\x8C\x8C";
	[[maybe_unused]] static const char *FONT_ICON_HEART = "\xEE\x8A\xA8";
	[[maybe_unused]] static const char *FONT_ICON_HEART_CRACK = "\xEE\xAF\xA8";
	[[maybe_unused]] static const char *FONT_ICON_STAR = "\xEE\x91\xAA";
	[[maybe_unused]] static const char *FONT_ICON_XMARK = "\xEE\x93\xB6";
	[[maybe_unused]] static const char *FONT_ICON_CIRCLE = "\xEE\x86\x8A";
	[[maybe_unused]] static const char *FONT_ICON_ARROW_ROTATE_LEFT = "\xEE\x80\xB8";
	[[maybe_unused]] static const char *FONT_ICON_ARROW_ROTATE_RIGHT = "\xEE\x80\xB6";
	[[maybe_unused]] static const char *FONT_ICON_FLAG_CHECKERED = "\xEE\xA8\xB8";
	[[maybe_unused]] static const char *FONT_ICON_BAN = "\xEE\x8F\x9E";
	[[maybe_unused]] static const char *FONT_ICON_CIRCLE_CHEVRON_DOWN = "\xEE\x84\x9E";
	[[maybe_unused]] static const char *FONT_ICON_KEY = "\xEE\x8B\x96";
	[[maybe_unused]] static const char *FONT_ICON_LANGUAGE = "\xEE\x92\xA2";
	[[maybe_unused]] static const char *FONT_ICON_SQUARE_MINUS = "\xEE\xB5\x8C";
	[[maybe_unused]] static const char *FONT_ICON_SQUARE_PLUS = "\xEE\xB5\x8A";
	[[maybe_unused]] static const char *FONT_ICON_SORT_UP = "\xEE\x91\x84";
	[[maybe_unused]] static const char *FONT_ICON_SORT_DOWN = "\xEE\x91\x86";
	[[maybe_unused]] static const char *FONT_ICON_TRIANGLE_EXCLAMATION = "\xEE\x93\xA0";

	[[maybe_unused]] static const char *FONT_ICON_HOUSE = "\xEE\x8B\x82";
	[[maybe_unused]] static const char *FONT_ICON_BOOKMARK = "\xEE\x83\xA8";
	[[maybe_unused]] static const char *FONT_ICON_NEWSPAPER = "\xEE\x8D\x84";
	[[maybe_unused]] static const char *FONT_ICON_POWER_OFF = "\xEE\x8F\x9A";
	[[maybe_unused]] static const char *FONT_ICON_GEAR = "\xEE\x89\xB0";
	[[maybe_unused]] static const char *FONT_ICON_PEN_TO_SQUARE = "\xEE\x8E\xAA";
	[[maybe_unused]] static const char *FONT_ICON_CLAPPERBOARD = "\xEE\xA3\x82";
	[[maybe_unused]] static const char *FONT_ICON_EARTH_AMERICAS = "\xEE\x8A\x8C";
	[[maybe_unused]] static const char *FONT_ICON_NETWORK_WIRED = "\xEE\xB7\x9E";
	[[maybe_unused]] static const char *FONT_ICON_LIST_UL = "\xEE\x8B\xB2";
	[[maybe_unused]] static const char *FONT_ICON_INFO = "\xEE\x8B\x8E";
	[[maybe_unused]] static const char *FONT_ICON_TERMINAL = "\xEE\x91\xBE";
	[[maybe_unused]] static const char *FONT_ICON_USER = "\xEE\x93\x82";

	[[maybe_unused]] static const char *FONT_ICON_SLASH = "\xEE\x8F\xA0";
	[[maybe_unused]] static const char *FONT_ICON_PLAY = "\xEE\x8F\x90";
	[[maybe_unused]] static const char *FONT_ICON_PAUSE = "\xEE\x8E\x9E";
	[[maybe_unused]] static const char *FONT_ICON_STOP = "\xEE\x91\xAC";
	[[maybe_unused]] static const char *FONT_ICON_CHEVRON_LEFT = "\xEE\x84\xB8";
	[[maybe_unused]] static const char *FONT_ICON_CHEVRON_RIGHT = "\xEE\x84\xBA";
	[[maybe_unused]] static const char *FONT_ICON_CHEVRON_UP = "\xEE\x84\xBC";
	[[maybe_unused]] static const char *FONT_ICON_CHEVRON_DOWN = "\xEE\x84\xB6";
	[[maybe_unused]] static const char *FONT_ICON_BACKWARD = "\xEE\x9A\xA8";
	[[maybe_unused]] static const char *FONT_ICON_FORWARD = "\xEE\x9A\xA6";
	[[maybe_unused]] static const char *FONT_ICON_RIGHT_FROM_BRACKET = "\xEE\x90\xAA";
	[[maybe_unused]] static const char *FONT_ICON_RIGHT_TO_BRACKET = "\xEE\x90\xA8";
	[[maybe_unused]] static const char *FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE = "\xEE\x97\x9E";
	[[maybe_unused]] static const char *FONT_ICON_BACKWARD_STEP = "\xEE\x96\xA4";
	[[maybe_unused]] static const char *FONT_ICON_FORWARD_STEP = "\xEE\x96\xA6";
	[[maybe_unused]] static const char *FONT_ICON_BACKWARD_FAST = "\xEE\x84\xA8";
	[[maybe_unused]] static const char *FONT_ICON_FORWARD_FAST = "\xEE\x84\xAA";
	[[maybe_unused]] static const char *FONT_ICON_KEYBOARD = "\xEE\x8B\x98";
	[[maybe_unused]] static const char *FONT_ICON_ELLIPSIS = "\xEE\x87\xBE";

	[[maybe_unused]] static const char *FONT_ICON_FOLDER = "\xEE\x89\x8A";
	[[maybe_unused]] static const char *FONT_ICON_FOLDER_OPEN = "\xEE\x89\x96";
	[[maybe_unused]] static const char *FONT_ICON_FOLDER_TREE = "\xEE\x99\xBC";
	[[maybe_unused]] static const char *FONT_ICON_FILM = "\xEE\x9E\x92";
	[[maybe_unused]] static const char *FONT_ICON_VIDEO = "\xEE\x9D\x80";
	[[maybe_unused]] static const char *FONT_ICON_MAP = "\xEE\x8C\x9A";
	[[maybe_unused]] static const char *FONT_ICON_IMAGE = "\xEE\x8B\x8A";
	[[maybe_unused]] static const char *FONT_ICON_MUSIC = "\xEE\x8C\xBC";
	[[maybe_unused]] static const char *FONT_ICON_FILE = "\xEE\x88\xB0";

	[[maybe_unused]] static const char *FONT_ICON_PENCIL = "\xEE\x8E\xAE";
	[[maybe_unused]] static const char *FONT_ICON_COPY = "\xEE\x87\x8A";
	[[maybe_unused]] static const char *FONT_ICON_TRASH = "\xEE\x92\xA6";

	[[maybe_unused]] static const char *FONT_ICON_ARROWS_LEFT_RIGHT = "\xEE\x82\xA0";
	[[maybe_unused]] static const char *FONT_ICON_ARROWS_UP_DOWN = "\xEE\x82\x98";
	[[maybe_unused]] static const char *FONT_ICON_CIRCLE_PLAY = "\xEE\x8F\x92";
	[[maybe_unused]] static const char *FONT_ICON_BORDER_ALL = "\xEE\x8A\x96";
	[[maybe_unused]] static const char *FONT_ICON_EYE = "\xEE\x88\xA0";
	[[maybe_unused]] static const char *FONT_ICON_EYE_SLASH = "\xEE\x88\xA4";
	[[maybe_unused]] static const char *FONT_ICON_EYE_DROPPER = "\xEE\x95\xA8";
	[[maybe_unused]] static const char *FONT_ICON_COMMENT = "\xEE\x85\x9C";
	[[maybe_unused]] static const char *FONT_ICON_COMMENT_SLASH = "\xEE\x85\x9E";

	[[maybe_unused]] static const char *FONT_ICON_DICE_ONE = "\xEE\x87\xB2";
	[[maybe_unused]] static const char *FONT_ICON_DICE_TWO = "\xEE\x87\xB8";
	[[maybe_unused]] static const char *FONT_ICON_DICE_THREE = "\xEE\x87\xB6";
	[[maybe_unused]] static const char *FONT_ICON_DICE_FOUR = "\xEE\x87\xB0";
	[[maybe_unused]] static const char *FONT_ICON_DICE_FIVE = "\xEE\x87\xAE";
	[[maybe_unused]] static const char *FONT_ICON_DICE_SIX = "\xEE\x87\xB4";

	[[maybe_unused]] static const char *FONT_ICON_LAYER_GROUP = "\xEE\x91\xA6";
	[[maybe_unused]] static const char *FONT_ICON_UNDO = "\xEE\x80\xA4";
	[[maybe_unused]] static const char *FONT_ICON_REDO = "\xEE\x80\xA6";

	[[maybe_unused]] static const char *FONT_ICON_ARROWS_ROTATE = "\xEE\x82\x94";
	[[maybe_unused]] static const char *FONT_ICON_QUESTION = "\xEE\x8F\xA8";

	[[maybe_unused]] static const char *FONT_ICON_CAMERA = "\xEE\x84\x8E";

	// TClient
	[[maybe_unused]] static const char *FONT_ICON_USERS = "\xEE\x93\x96";
}

enum ETextCursorSelectionMode
{
	// ignore any kind of selection
	TEXT_CURSOR_SELECTION_MODE_NONE = 0,
	// calculates the selection based on the mouse press and release cursor position
	TEXT_CURSOR_SELECTION_MODE_CALCULATE,
	// sets the selection based on the character start and end count(these values have to be decoded character offsets)
	TEXT_CURSOR_SELECTION_MODE_SET,
};

enum ETextCursorCursorMode
{
	// ignore any kind of cursor
	TEXT_CURSOR_CURSOR_MODE_NONE = 0,
	// calculates the cursor based on the mouse release cursor position
	TEXT_CURSOR_CURSOR_MODE_CALCULATE,
	// sets the cursor based on the current character (this value has to be decoded character offset)
	TEXT_CURSOR_CURSOR_MODE_SET,
};

struct STextBoundingBox
{
	float m_X;
	float m_Y;
	float m_W;
	float m_H;

	float Right() const { return m_X + m_W; }
	float Bottom() const { return m_Y + m_H; }
	vec2 Size() const { return vec2(m_W, m_H); }
	void MoveBy(vec2 Offset)
	{
		m_X += Offset.x;
		m_Y += Offset.y;
	}
};

// Allow to render multi colored text in one go without having to call TextEx() multiple times.
// Needed to allow multi colored multi line texts
struct STextColorSplit
{
	int m_CharIndex; // Which index within the text should the split occur
	int m_Length; // How long is the split
	ColorRGBA m_Color; // The color the text should be starting from m_CharIndex

	STextColorSplit(int CharIndex, int Length, const ColorRGBA &Color) :
		m_CharIndex(CharIndex), m_Length(Length), m_Color(Color) {}
};

class CTextCursor
{
public:
	int m_Flags = TEXTFLAG_RENDER;
	int m_LineCount = 1;
	int m_GlyphCount = 0;
	int m_CharCount = 0;
	int m_MaxLines = 0;

	float m_StartX = 0.0f;
	float m_StartY = 0.0f;
	float m_LineWidth = -1.0f;
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_MaxCharacterHeight = 0.0f;
	float m_LongestLineWidth = 0.0f;
	bool m_CalculateVisualBoundingBox = false;
	bool m_HasVisualBoundingBox = false;
	float m_VisualTop = 0.0f;
	float m_VisualBottom = 0.0f;

	float m_FontSize = 0.0f;
	float m_AlignedFontSize = 0.0f;
	float m_LineSpacing = 0.0f;
	float m_AlignedLineSpacing = 0.0f;

	ETextCursorSelectionMode m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_NONE;
	float m_SelectionHeightFactor = 1.0f;

	// these coordinates are respected if selection mode is set to calculate @see ETextCursorSelectionMode
	vec2 m_PressMouse = vec2(0.0f, 0.0f);
	// these coordinates are respected if selection/cursor mode is set to calculate @see ETextCursorSelectionMode / @see ETextCursorCursorMode
	vec2 m_ReleaseMouse = vec2(0.0f, 0.0f);

	// note m_SelectionStart can be bigger than m_SelectionEnd, depending on how the mouse cursor was dragged
	// also note, that these are the character offsets decoded
	int m_SelectionStart = 0;
	int m_SelectionEnd = 0;

	ETextCursorCursorMode m_CursorMode = TEXT_CURSOR_CURSOR_MODE_NONE;
	bool m_ForceCursorRendering = false;
	// 输入框独立绘制光标，避免闪烁命令影响后续提交的文本命令。
	bool m_RenderCursor = true;
	// 输入框独立绘制选区，避免临时选择 quad 容器改变后续文本命令状态。
	bool m_RenderSelection = true;
	std::vector<IGraphics::CQuadItem> m_vSelectionQuads;
	// note this is the decoded character offset
	int m_CursorCharacter = -1;
	vec2 m_CursorRenderedPosition = vec2(-1.0f, -1.0f);
	bool m_HasCursorRenderedPosition = false;

	/**
	 * Whether the text was truncated with @link TEXTFLAG_STOP_AT_END @endlink or @link TEXTFLAG_ELLIPSIS_AT_END @endlink being set.
	 */
	bool m_Truncated = false;

	// Color splits of the cursor to allow multicolored text
	std::vector<STextColorSplit> m_vColorSplits;

	float Height() const;
	STextBoundingBox BoundingBox() const;
	void SetPosition(vec2 Position);
};

struct STextContainerUsages
{
	int m_Dummy = 0;
};

struct STextContainerIndex
{
	int m_Index;
	std::shared_ptr<STextContainerUsages> m_UseCount =
		std::make_shared<STextContainerUsages>(STextContainerUsages());

	STextContainerIndex() { Reset(); }
	bool Valid() const { return m_Index >= 0; }
	void Reset() { m_Index = -1; }
};

struct STextSizeProperties
{
	float *m_pHeight = nullptr;
	float *m_pAlignedFontSize = nullptr;
	float *m_pMaxCharacterHeightInLine = nullptr;
	// Optional glyph fill bounds relative to the cursor start Y, excluding outline atlas padding.
	float *m_pVisualTop = nullptr;
	float *m_pVisualBottom = nullptr;
	int *m_pLineCount = nullptr;
};

struct SQmTextRuntimeBudgetSnapshot
{
	int m_GlyphNew = 0;
	int m_GlyphUploads = 0;
	double m_GlyphRasterizeMs = 0.0;
	double m_GlyphUploadMs = 0.0;
	int m_TextContainerNew = 0;
	int m_TextContainerUploads = 0;
	double m_TextContainerCreateMs = 0.0;
	double m_TextContainerUploadMs = 0.0;
	uint64_t m_Frame = 0;
};

class ITextRender : public IInterface
{
	MACRO_INTERFACE("textrender")
public:
	virtual std::vector<std::string> *GetCustomFaces() = 0; // TClient
	virtual void SetCustomFace(const char *pFace) = 0; // TClient

	virtual bool LoadFonts() = 0;
	virtual void SetFontPreset(EFontPreset FontPreset) = 0;
	virtual EFontPreset GetFontPreset() const = 0;
	virtual void SetIconFontWeight(bool Bold) = 0;
	virtual void SetFontLanguageVariant(const char *pLanguageFile) = 0;

	virtual void SetRenderFlags(unsigned Flags) = 0;
	virtual unsigned GetRenderFlags() const = 0;

	ColorRGBA DefaultTextColor() const { return ColorRGBA(1, 1, 1, 1); }
	ColorRGBA DefaultTextOutlineColor() const { return ColorRGBA(0, 0, 0, 0.3f); }
	ColorRGBA DefaultTextSelectionColor() const { return ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f); }

	//
	virtual void TextEx(CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual bool CreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual void AppendTextContainer(STextContainerIndex TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	// either creates a new text container or appends to a existing one
	virtual bool CreateOrAppendTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	// just deletes and creates text container
	virtual void RecreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual void RecreateTextContainerSoft(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) = 0;
	virtual void DeleteTextContainer(STextContainerIndex &TextContainerIndex) = 0;

	virtual void UploadTextContainer(STextContainerIndex TextContainerIndex) = 0;
	virtual void FlushQmTextRuntimeBudgetLog() {}
	virtual SQmTextRuntimeBudgetSnapshot QmTextRuntimeBudgetSnapshot() const { return {}; }

	virtual void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) = 0;
	virtual void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor, float X, float Y) = 0;

	virtual STextBoundingBox GetBoundingBoxTextContainer(STextContainerIndex TextContainerIndex) = 0;

	virtual void UploadEntityLayerText(const CImageInfo &TextImage, int TexSubWidth, int TexSubHeight, const char *pText, int Length, float x, float y, int FontSize) = 0;
	virtual int AdjustFontSize(const char *pText, int TextLength, int MaxSize, int MaxWidth) const = 0;
	virtual float GetGlyphOffsetX(int FontSize, char TextCharacter) const = 0;
	virtual int CalculateTextWidth(const char *pText, int TextLength, int FontWidth, int FontSize) const = 0;

	// old foolish interface
	virtual void TextColor(float r, float g, float b, float a) = 0;
	virtual void TextColor(ColorRGBA Color) = 0;
	virtual void TextOutlineColor(float r, float g, float b, float a) = 0;
	virtual void TextOutlineColor(ColorRGBA Color) = 0;
	virtual void TextSelectionColor(float r, float g, float b, float a) = 0;
	virtual void TextSelectionColor(ColorRGBA Color) = 0;
	virtual void Text(float x, float y, float Size, const char *pText, float LineWidth = -1.0f) = 0;
	virtual float TextWidth(float Size, const char *pText, int StrLength = -1, float LineWidth = -1.0f, int Flags = 0, const STextSizeProperties &TextSizeProps = {}) = 0;
	virtual STextBoundingBox TextBoundingBox(float Size, const char *pText, int StrLength = -1, float LineWidth = -1.0f, float LineSpacing = 0.0f, int Flags = 0) = 0;

	virtual ColorRGBA GetTextColor() const = 0;
	virtual ColorRGBA GetTextOutlineColor() const = 0;
	virtual ColorRGBA GetTextSelectionColor() const = 0;

	virtual void OnPreWindowResize() = 0;
	virtual void OnWindowResize() = 0;
	/**
	 * 图形设备重建后调用：字体纹理已随设备消失，需要重新上传。
	 * 由 IGraphics 的「图形资源重置」广播驱动。
	 */
	virtual void OnGraphicsResourcesReset() = 0;
};

class IEngineTextRender : public ITextRender
{
	MACRO_INTERFACE("enginetextrender")
public:
	virtual void Init() = 0;
	void Shutdown() override = 0;
};

extern IEngineTextRender *CreateEngineTextRender();

#endif
