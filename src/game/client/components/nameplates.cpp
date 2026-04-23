#include "nameplates.h"

#include <base/str.h>

#include <algorithm>
#include <cmath>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol7.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/QmUi/QmAnim.h>

#include <array>
#include <limits>
#include <memory>
#include <vector>
//枚举
enum class EHookStrongWeakState
{
	WEAK,
	NEUTRAL,
	STRONG
};

enum class ENameplateCoreRow
{
	NAME,
	CLAN,
	HOOK,
	COORDS,
	KEYS,
	NUM_ROWS
};

static constexpr size_t kNameplateCoreRowCount = static_cast<size_t>(ENameplateCoreRow::NUM_ROWS);
static constexpr std::array<ENameplateCoreRow, kNameplateCoreRowCount> s_aDefaultNameplateCoreRowsTopToBottom = {
	ENameplateCoreRow::KEYS,
	ENameplateCoreRow::COORDS,
	ENameplateCoreRow::HOOK,
	ENameplateCoreRow::CLAN,
	ENameplateCoreRow::NAME};

struct SChatBubbleAnimState
{
	bool m_Initialized = false;
	bool m_LastVisible = false;
	bool m_LastTyping = false;
	int64_t m_LastChatBubbleStartTick = 0;
	float m_LastTargetAlpha = std::numeric_limits<float>::quiet_NaN();
	float m_LastTargetScale = std::numeric_limits<float>::quiet_NaN();
	float m_LastTargetSlide = std::numeric_limits<float>::quiet_NaN();
	float m_LastTargetFillChars = std::numeric_limits<float>::quiet_NaN();
	STextContainerIndex m_TextContainerIndex;
	float m_CachedTextWidth = 0.0f;
	float m_CachedTextHeight = 0.0f;
	float m_CachedFontSize = -INFINITY;
	float m_CachedLineWidth = -INFINITY;
	char m_aCachedText[256] = "";
	char m_aLayoutText[256] = "";
};

static uint64_t ChatBubbleAnimNodeKey(int ClientId)
{
	static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("chat_bubble_anim_v2"));
	return (s_BaseKey << 32) | static_cast<uint64_t>(ClientId & 0xffff);
}

static float ResolveChatBubbleAnimValue(
	CUiV2AnimationRuntime &AnimRuntime,
	uint64_t NodeKey,
	EUiAnimProperty Property,
	float Target,
	float DurationSec,
	EEasing Easing,
	float &LastTarget,
	bool ForceRequest = false)
{
	constexpr float Epsilon = 0.001f;
	const float Current = AnimRuntime.GetValue(NodeKey, Property, Target);
	const bool TargetChanged = !std::isfinite(LastTarget) || std::abs(Target - LastTarget) > Epsilon;
	const bool NeedsSync = !AnimRuntime.HasActiveAnimation(NodeKey, Property) && std::abs(Target - Current) > Epsilon;
	if(ForceRequest || TargetChanged || NeedsSync)
	{
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_DurationSec = DurationSec;
		Request.m_Transition.m_DelaySec = 0.0f;
		Request.m_Transition.m_Priority = 1;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Request.m_Transition.m_Easing = Easing;
		AnimRuntime.RequestAnimation(Request);
		LastTarget = Target;
	}
	return AnimRuntime.GetValue(NodeKey, Property, Target);
}

static bool ParseNameplateCoreRowKey(const char *pKey, ENameplateCoreRow &OutRow)
{
	if(str_comp_nocase(pKey, "name") == 0)
	{
		OutRow = ENameplateCoreRow::NAME;
		return true;
	}
	if(str_comp_nocase(pKey, "clan") == 0)
	{
		OutRow = ENameplateCoreRow::CLAN;
		return true;
	}
	if(str_comp_nocase(pKey, "hook") == 0)
	{
		OutRow = ENameplateCoreRow::HOOK;
		return true;
	}
	if(str_comp_nocase(pKey, "coords") == 0)
	{
		OutRow = ENameplateCoreRow::COORDS;
		return true;
	}
	if(str_comp_nocase(pKey, "keys") == 0)
	{
		OutRow = ENameplateCoreRow::KEYS;
		return true;
	}
	return false;
}

static std::array<ENameplateCoreRow, kNameplateCoreRowCount> ParseNameplateCoreRowOrderTopToBottom(const char *pConfigValue)
{
	std::array<ENameplateCoreRow, kNameplateCoreRowCount> Result = s_aDefaultNameplateCoreRowsTopToBottom;
	if(!pConfigValue || pConfigValue[0] == '\0')
		return Result;

	std::array<ENameplateCoreRow, kNameplateCoreRowCount> ParsedRows = {};
	bool aUsedRows[kNameplateCoreRowCount] = {};
	int ParsedCount = 0;

	char aToken[32];
	const char *pToken = pConfigValue;
	while((pToken = str_next_token(pToken, ",", aToken, sizeof(aToken))))
	{
		if(aToken[0] == '\0')
			continue;

		ENameplateCoreRow Row;
		if(!ParseNameplateCoreRowKey(aToken, Row))
			continue;

		const int RowIndex = static_cast<int>(Row);
		if(aUsedRows[RowIndex])
			continue;

		ParsedRows[ParsedCount++] = Row;
		aUsedRows[RowIndex] = true;
		if(ParsedCount == static_cast<int>(kNameplateCoreRowCount))
			break;
	}

	int OutIndex = 0;
	for(int i = 0; i < ParsedCount; ++i)
		Result[OutIndex++] = ParsedRows[i];
	for(const ENameplateCoreRow Row : s_aDefaultNameplateCoreRowsTopToBottom)
	{
		const int RowIndex = static_cast<int>(Row);
		if(aUsedRows[RowIndex])
			continue;
		Result[OutIndex++] = Row;
	}

	return Result;
}

class CNamePlateData
{
public:
	bool m_Local; // TClient
	bool m_InGame;
	ColorRGBA m_Color;
	bool m_ShowName;
	char m_aName[std::max<size_t>(MAX_NAME_LENGTH, protocol7::MAX_NAME_ARRAY_SIZE)];
	bool m_ShowFriendMark;
	bool m_ShowClientId;
	int m_ClientId;
	float m_FontSizeClientId;
	bool m_ClientIdSeparateLine;
	float m_FontSize;
	bool m_ShowClan;
	char m_aClan[std::max<size_t>(MAX_CLAN_LENGTH, protocol7::MAX_CLAN_ARRAY_SIZE)];
	float m_FontSizeClan;
	bool m_ShowCoordX;
	bool m_ShowCoordY;
	bool m_ShowCoords;
	bool m_CoordXAlignHint;
	bool m_CoordXAlignHintStrict;
	bool m_CoordXAligned;
	float m_CoordXAlignBaseX;
	int m_CoordXAlignDiff;
	vec2 m_Coords;
	float m_FontSizeCoords;
	bool m_ShowDirection;
	bool m_DirLeft;
	bool m_DirJump;
	bool m_DirRight;
	float m_FontSizeDirection;
	bool m_ShowHookStrongWeak;
	EHookStrongWeakState m_HookStrongWeakState;
	bool m_ShowHookStrongWeakId;
	int m_HookStrongWeakId;
	float m_FontSizeHookStrongWeak;
};

// Part Types

static constexpr float DEFAULT_PADDING = 5.0f;

class CNamePlatePart
{
protected:
	vec2 m_Size = vec2(0.0f, 0.0f);
	vec2 m_Padding = vec2(DEFAULT_PADDING, DEFAULT_PADDING);
	bool m_NewLine = false; // Whether this part is a new line (doesn't do anything else)
	bool m_Visible = true; // Whether this part is visible
	bool m_ShiftOnInvis = false; // Whether when not visible will still take up space
	CNamePlatePart(CGameClient &This) {}

public:
	virtual void Update(CGameClient &This, const CNamePlateData &Data) {}
	virtual void Reset(CGameClient &This) {}
	virtual void Render(CGameClient &This, vec2 Pos) const {}
	vec2 Size() const { return m_Size; }
	vec2 Padding() const { return m_Padding; }
	bool NewLine() const { return m_NewLine; }
	bool Visible() const { return m_Visible; }
	bool ShiftOnInvis() const { return m_ShiftOnInvis; }
	CNamePlatePart() = delete;
	virtual ~CNamePlatePart() = default;
};

using PartsVector = std::vector<std::unique_ptr<CNamePlatePart>>;

static constexpr ColorRGBA s_OutlineColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);

class CNamePlatePartText : public CNamePlatePart
{
protected:
	STextContainerIndex m_TextContainerIndex;
	virtual bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) = 0;
	virtual void UpdateText(CGameClient &This, const CNamePlateData &Data) = 0;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	CNamePlatePartText(CGameClient &This) :
		CNamePlatePart(This)
	{
		Reset(This);
	}

public:
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		if(!UpdateNeeded(This, Data) && m_TextContainerIndex.Valid())
			return;

		// Set flags
		unsigned int Flags = ETextRenderFlags::TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE;
		if(Data.m_InGame)
			Flags |= ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT; // Prevent jittering from rounding
		This.TextRender()->SetRenderFlags(Flags);

		if(Data.m_InGame)
		{
			// Create text at standard zoom
			float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
			This.Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
			This.Graphics()->MapScreenToInterface(This.m_Camera.m_Center.x, This.m_Camera.m_Center.y);
			This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
			UpdateText(This, Data);
			This.Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
		}
		else
		{
			UpdateText(This, Data);
		}

		This.TextRender()->SetRenderFlags(0);

		if(!m_TextContainerIndex.Valid())
		{
			m_Visible = false;
			return;
		}

		const STextBoundingBox Container = This.TextRender()->GetBoundingBoxTextContainer(m_TextContainerIndex);
		m_Size = vec2(Container.m_W, Container.m_H);
	}
	void Reset(CGameClient &This) override
	{
		This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	}
	void Render(CGameClient &This, vec2 Pos) const override
	{
		if(!m_TextContainerIndex.Valid())
			return;

		ColorRGBA OutlineColor, Color;
		Color = m_Color;
		OutlineColor = s_OutlineColor.WithMultipliedAlpha(m_Color.a);
		This.TextRender()->RenderTextContainer(m_TextContainerIndex,
			Color, OutlineColor,
			Pos.x - Size().x / 2.0f, Pos.y - Size().y / 2.0f);
	}
};

class CNamePlatePartIcon : public CNamePlatePart
{
protected:
	IGraphics::CTextureHandle m_Texture;
	float m_Rotation = 0.0f;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	CNamePlatePartIcon(CGameClient &This) :
		CNamePlatePart(This) {}

public:
	void Render(CGameClient &This, vec2 Pos) const override
	{
		IGraphics::CQuadItem QuadItem(Pos.x - Size().x / 2.0f, Pos.y - Size().y / 2.0f, Size().x, Size().y);
		This.Graphics()->TextureSet(m_Texture);
		This.Graphics()->QuadsBegin();
		This.Graphics()->SetColor(m_Color);
		This.Graphics()->QuadsSetRotation(m_Rotation);
		This.Graphics()->QuadsDrawTL(&QuadItem, 1);
		This.Graphics()->QuadsEnd();
		This.Graphics()->QuadsSetRotation(0.0f);
	}
};

class CNamePlatePartSprite : public CNamePlatePart
{
protected:
	IGraphics::CTextureHandle m_Texture;
	int m_Sprite = -1;
	int m_SpriteFlags = 0;
	float m_Rotation = 0.0f;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	CNamePlatePartSprite(CGameClient &This) :
		CNamePlatePart(This) {}

public:
	void Render(CGameClient &This, vec2 Pos) const override
	{
		This.Graphics()->TextureSet(m_Texture);
		This.Graphics()->QuadsSetRotation(m_Rotation);
		This.Graphics()->QuadsBegin();
		This.Graphics()->SetColor(m_Color);
		This.Graphics()->SelectSprite(m_Sprite, m_SpriteFlags);
		This.Graphics()->DrawSprite(Pos.x, Pos.y, Size().x, Size().y);
		This.Graphics()->QuadsEnd();
		This.Graphics()->QuadsSetRotation(0.0f);
	}
};

// Part Definitions

class CNamePlatePartNewLine : public CNamePlatePart
{
public:
	CNamePlatePartNewLine(CGameClient &This) :
		CNamePlatePart(This)
	{
		m_NewLine = true;
	}
};

enum Direction
{
	DIRECTION_LEFT,
	DIRECTION_UP,
	DIRECTION_RIGHT
};

class CNamePlatePartDirection : public CNamePlatePartIcon
{
private:
	int m_Direction;

public:
	CNamePlatePartDirection(CGameClient &This, Direction Dir) :
		CNamePlatePartIcon(This)
	{
		m_Texture = g_pData->m_aImages[IMAGE_ARROW].m_Id;
		m_Direction = Dir;
		switch(m_Direction)
		{
		case DIRECTION_LEFT:
			m_Rotation = pi;
			break;
		case DIRECTION_UP:
			m_Rotation = pi / -2.0f;
			break;
		case DIRECTION_RIGHT:
			m_Rotation = 0.0f;
			break;
		}
	}
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		if(!Data.m_ShowDirection)
		{
			m_ShiftOnInvis = false;
			m_Visible = false;
			return;
		}
		m_ShiftOnInvis = true; // Only shift (horizontally) the other parts if directions as a whole is visible
		m_Size = vec2(Data.m_FontSizeDirection, Data.m_FontSizeDirection);
		m_Padding.y = m_Size.y / 2.0f;
		switch(m_Direction)
		{
		case DIRECTION_LEFT:
			m_Visible = Data.m_DirLeft;
			break;
		case DIRECTION_UP:
			m_Visible = Data.m_DirJump;
			break;
		case DIRECTION_RIGHT:
			m_Visible = Data.m_DirRight;
			break;
		}
		m_Color.a = Data.m_Color.a;
	}
};

class CNamePlatePartClientId : public CNamePlatePartText
{
private:
	int m_ClientId = -1;
	static_assert(MAX_CLIENTS <= 999, "Make this buffer bigger");
	char m_aText[5] = "";
	float m_FontSize = -INFINITY;
	bool m_ClientIdSeparateLine = false;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowClientId && (Data.m_ClientIdSeparateLine == m_ClientIdSeparateLine);
		if(!m_Visible)
			return false;
		m_Color = Data.m_Color;
		return m_FontSize != Data.m_FontSizeClientId || m_ClientId != Data.m_ClientId;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClientId;
		m_ClientId = Data.m_ClientId;
		if(m_ClientIdSeparateLine)
			str_format(m_aText, sizeof(m_aText), "%d", m_ClientId);
		else
			str_format(m_aText, sizeof(m_aText), "%d:", m_ClientId);
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartClientId(CGameClient &This, bool ClientIdSeparateLine) :
		CNamePlatePartText(This)
	{
		m_ClientIdSeparateLine = ClientIdSeparateLine;
	}
};

class CNamePlatePartFriendMark : public CNamePlatePartText
{
private:
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowFriendMark;
		if(!m_Visible)
			return false;
		m_Color.a = Data.m_Color.a;
		return m_FontSize != Data.m_FontSize;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		CTextCursor Cursor;
		This.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, FontIcons::FONT_ICON_HEART);
		This.TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

public:
	CNamePlatePartFriendMark(CGameClient &This) :
		CNamePlatePartText(This)
	{
		m_Color = ColorRGBA(1.0f, 0.0f, 0.0f);
	}
};

class CNamePlatePartName : public CNamePlatePartText
{
private:
	char m_aText[std::max<size_t>(MAX_NAME_LENGTH, protocol7::MAX_NAME_ARRAY_SIZE)] = "";
	float m_FontSize = -INFINITY;
	bool m_IsLocal = false;
	float m_Alpha = 1.0f;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowName;
		if(!m_Visible)
			return false;
		m_Color = Data.m_Color;
		m_IsLocal = Data.m_Local;
		if(!m_IsLocal)
		{
			if(Data.m_InGame)
			{
				for(const auto Id : This.m_aLocalIds)
				{
					if(Id == Data.m_ClientId)
					{
						m_IsLocal = true;
						break;
					}
				}
			}
			else
			{
				m_IsLocal = Data.m_ClientId == 0 || Data.m_ClientId == 1;
			}
		}
		m_Alpha = Data.m_Color.a;
		// TClient
		if(g_Config.m_TcWarList)
		{
			if(This.m_WarList.GetWarData(Data.m_ClientId).m_WarName)
				m_Color = This.m_WarList.GetNameplateColor(Data.m_ClientId).WithAlpha(Data.m_Color.a);
			else if(This.m_WarList.GetWarData(Data.m_ClientId).m_WarClan)
				m_Color = This.m_WarList.GetClanColor(Data.m_ClientId).WithAlpha(Data.m_Color.a);
		}
		return m_FontSize != Data.m_FontSize || str_comp(m_aText, Data.m_aName) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		str_copy(m_aText, Data.m_aName, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

	void Render(CGameClient &This, vec2 Pos) const override
	{
		if(!m_TextContainerIndex.Valid())
			return;

		ColorRGBA OutlineColor, Color;
		Color = m_Color;

		// Rainbow name for local player (same style as QmClient sidebar)
		if(m_IsLocal && g_Config.m_QmRainbowName)
		{
			const float Time = This.Client()->GlobalTime();
			const float Hue = std::fmod(Time * 0.15f, 1.0f);
			ColorHSLA Hsla(Hue, 0.7f, 0.65f, m_Alpha);
			Color = color_cast<ColorRGBA>(Hsla);
		}

		OutlineColor = s_OutlineColor.WithMultipliedAlpha(Color.a);
		This.TextRender()->RenderTextContainer(m_TextContainerIndex,
			Color, OutlineColor,
			Pos.x - Size().x / 2.0f, Pos.y - Size().y / 2.0f);
	}

public:
	CNamePlatePartName(CGameClient &This) :
		CNamePlatePartText(This) {}
};

class CNamePlatePartClan : public CNamePlatePartText
{
private:
	char m_aText[std::max<size_t>(MAX_CLAN_LENGTH, protocol7::MAX_CLAN_ARRAY_SIZE)] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowClan;
		if(!m_Visible && Data.m_aClan[0] != '\0')
			return false;
		m_Color = Data.m_Color;
		// TClient
		if(This.m_WarList.GetWarData(Data.m_ClientId).m_WarClan)
			m_Color = This.m_WarList.GetClanColor(Data.m_ClientId).WithAlpha(Data.m_Color.a);
		return m_FontSize != Data.m_FontSizeClan || str_comp(m_aText, Data.m_aClan) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClan;
		str_copy(m_aText, Data.m_aClan, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartClan(CGameClient &This) :
		CNamePlatePartText(This) {}
};

class CNamePlatePartHookStrongWeak : public CNamePlatePartSprite
{
protected:
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowHookStrongWeak;
		if(!m_Visible)
			return;
		m_Size = vec2(Data.m_FontSizeHookStrongWeak + DEFAULT_PADDING, Data.m_FontSizeHookStrongWeak + DEFAULT_PADDING);
		switch(Data.m_HookStrongWeakState)
		{
		case EHookStrongWeakState::STRONG:
			m_Sprite = SPRITE_HOOK_STRONG;
			m_Color = color_cast<ColorRGBA>(ColorHSLA(6401973));
			break;
		case EHookStrongWeakState::NEUTRAL:
			m_Sprite = SPRITE_HOOK_ICON;
			m_Color = ColorRGBA(1.0f, 1.0f, 1.0f);
			break;
		case EHookStrongWeakState::WEAK:
			m_Sprite = SPRITE_HOOK_WEAK;
			m_Color = color_cast<ColorRGBA>(ColorHSLA(41131));
			break;
		}
		m_Color.a = Data.m_Color.a;
	}

public:
	CNamePlatePartHookStrongWeak(CGameClient &This) :
		CNamePlatePartSprite(This)
	{
		m_Texture = g_pData->m_aImages[IMAGE_STRONGWEAK].m_Id;
		m_Padding = vec2(0.0f, 0.0f);
	}
};

class CNamePlatePartHookStrongWeakId : public CNamePlatePartText
{
private:
	int m_StrongWeakId = -1;
	static_assert(MAX_CLIENTS <= 999, "Make this buffer bigger");
	char m_aText[4] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowHookStrongWeakId;
		if(!m_Visible)
			return false;
		switch(Data.m_HookStrongWeakState)
		{
		case EHookStrongWeakState::STRONG:
			m_Color = color_cast<ColorRGBA>(ColorHSLA(6401973));
			break;
		case EHookStrongWeakState::NEUTRAL:
			m_Color = ColorRGBA(1.0f, 1.0f, 1.0f);
			break;
		case EHookStrongWeakState::WEAK:
			m_Color = color_cast<ColorRGBA>(ColorHSLA(41131));
			break;
		}
		m_Color.a = Data.m_Color.a;
		return m_FontSize != Data.m_FontSizeHookStrongWeak || m_StrongWeakId != Data.m_HookStrongWeakId;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeHookStrongWeak;
		m_StrongWeakId = Data.m_HookStrongWeakId;
		str_format(m_aText, sizeof(m_aText), "%d", m_StrongWeakId);
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartHookStrongWeakId(CGameClient &This) :
		CNamePlatePartText(This) {}
};

// ***** TClient Parts *****

class CNamePlatePartCountry : public CNamePlatePart
{
protected:
	static constexpr float FLAG_WIDTH = 128.0f;
	static constexpr float FLAG_HEIGHT = 64.0f;
	static constexpr float FLAG_RATIO = FLAG_HEIGHT / FLAG_WIDTH;
	const CCountryFlags::CCountryFlag *m_pCountryFlag = nullptr;
	float m_Alpha = 1.0f;

public:
	friend class CGameClient;
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = true;
		if(g_Config.m_TcNameplateCountry == 0)
		{
			m_Visible = false;
			return;
		}
		if(Data.m_InGame)
		{
			// Check for us and dummy, Data.m_Local only does current char
			for(const auto Id : This.m_aLocalIds)
			{
				if(Id == Data.m_ClientId)
				{
					m_Visible = false;
					return;
				}
			}
			m_pCountryFlag = &This.m_CountryFlags.GetByCountryCode(This.m_aClients[Data.m_ClientId].m_Country);
		}
		else
		{
			if(Data.m_ClientId == 0)
				m_pCountryFlag = &This.m_CountryFlags.GetByCountryCode(g_Config.m_PlayerCountry);
			else
				m_pCountryFlag = &This.m_CountryFlags.GetByCountryCode(g_Config.m_ClDummyCountry);
		}
		// Do not show default flags
		if(m_pCountryFlag == &This.m_CountryFlags.GetByCountryCode(0))
		{
			m_Visible = false;
			return;
		}
		m_Alpha = Data.m_Color.a;
		m_Size = vec2(Data.m_FontSize / FLAG_RATIO, Data.m_FontSize);
	}
	void Render(CGameClient &This, vec2 Pos) const override
	{
		if(!m_pCountryFlag)
			return;
		This.m_CountryFlags.Render(*m_pCountryFlag, ColorRGBA(1.0f, 1.0f, 1.0f, m_Alpha),
			Pos.x - m_Size.x / 2.0f, Pos.y - m_Size.y / 2.0f,
			m_Size.x, m_Size.y);
	}
	CNamePlatePartCountry(CGameClient &This) :
		CNamePlatePart(This) {}
};

class CNamePlatePartPing : public CNamePlatePart
{
protected:
	float m_Radius = 7.0f;
	ColorRGBA m_Color;

public:
	friend class CGameClient;
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		const CNetObj_PlayerInfo *pInfo = Data.m_InGame ? This.m_Snap.m_apPlayerInfos[Data.m_ClientId] : nullptr;
		/*
			If in a real game,
				Show other people's pings if in scoreboard
				Or if ping circle and name enabled
			If in preview
				Show ping if ping circle and name enabled
		*/
		m_Radius = Data.m_FontSize / 3.0f;
		m_Size = vec2(m_Radius, m_Radius) * 1.5f;
		m_Visible = Data.m_InGame ? (
					    ((Data.m_ShowName && g_Config.m_TcNameplatePingCircle > 0) ||
						    (This.m_Scoreboard.IsActive() && pInfo && !pInfo->m_Local))) :
				    (
					    (Data.m_ShowName && g_Config.m_TcNameplatePingCircle > 0));
		if(!m_Visible)
			return;
		int Ping = Data.m_InGame && pInfo ? pInfo->m_Latency : (1 + Data.m_ClientId) * 25;
		m_Color = color_cast<ColorRGBA>(ColorHSLA((float)(300 - std::clamp(Ping, 0, 300)) / 1000.0f, 1.0f, 0.5f, Data.m_Color.a));
	}
	void Render(CGameClient &This, vec2 Pos) const override
	{
		This.Graphics()->TextureClear();
		This.Graphics()->QuadsBegin();
		This.Graphics()->SetColor(m_Color);
		This.Graphics()->DrawCircle(Pos.x, Pos.y, m_Radius, 24);
		This.Graphics()->QuadsEnd();
	}
	CNamePlatePartPing(CGameClient &This) :
		CNamePlatePart(This) {}
};

class CNamePlatePartSkin : public CNamePlatePartText
{
private:
	char m_aText[MAX_SKIN_LENGTH] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		const CNetObj_PlayerInfo *pInfo = Data.m_InGame ? This.m_Snap.m_apPlayerInfos[Data.m_ClientId] : nullptr;
		m_Visible = Data.m_InGame ? (pInfo && g_Config.m_TcNameplateSkins > (pInfo->m_Local ? 1 : 0)) : g_Config.m_TcNameplateSkins > 0;
		if(Data.m_InGame && This.ShouldHideStreamerSkin(Data.m_ClientId))
			m_Visible = false;
		if(!m_Visible)
			return false;
		m_Color = Data.m_Color;
		const char *pSkin = Data.m_InGame ? This.m_aClients[Data.m_ClientId].m_aSkinName : (Data.m_ClientId == 0 ? g_Config.m_ClPlayerSkin : g_Config.m_ClDummySkin);
		return m_FontSize != Data.m_FontSizeClan || str_comp(m_aText, pSkin) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClan;
		const char *pSkin = Data.m_InGame ? This.m_aClients[Data.m_ClientId].m_aSkinName : (Data.m_ClientId == 0 ? g_Config.m_ClPlayerSkin : g_Config.m_ClDummySkin);
		str_copy(m_aText, pSkin, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartSkin(CGameClient &This) :
		CNamePlatePartText(This) {}
};

class CNamePlatePartCoordinates : public CNamePlatePartText
{
private:
	vec2 m_Coords = vec2(INFINITY, INFINITY);
	float m_FontSize = -INFINITY;
	bool m_ShowX = false;
	bool m_ShowY = false;
	char m_aText[64] = "";

	static float RoundCoord(float Value)
	{
		return static_cast<float>(std::round(Value * 100.0f) / 100.0f);
	}

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowCoords && (Data.m_ShowCoordX || Data.m_ShowCoordY);
		if(!m_Visible)
			return false;
		m_Color = Data.m_Color;
		const float RoundedX = RoundCoord(Data.m_Coords.x);
		const float RoundedY = RoundCoord(Data.m_Coords.y);
		const bool ShowX = Data.m_ShowCoordX;
		const bool ShowY = Data.m_ShowCoordY;
		return m_FontSize != Data.m_FontSizeCoords || m_ShowX != ShowX || m_ShowY != ShowY ||
		       m_Coords.x != RoundedX || m_Coords.y != RoundedY;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeCoords;
		m_ShowX = Data.m_ShowCoordX;
		m_ShowY = Data.m_ShowCoordY;
		m_Coords = vec2(RoundCoord(Data.m_Coords.x), RoundCoord(Data.m_Coords.y));

		if(m_ShowX && m_ShowY)
			str_format(m_aText, sizeof(m_aText), "X:%.2f Y:%.2f", m_Coords.x, m_Coords.y);
		else if(m_ShowX)
			str_format(m_aText, sizeof(m_aText), "X:%.2f", m_Coords.x);
		else
			str_format(m_aText, sizeof(m_aText), "Y:%.2f", m_Coords.y);

		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartCoordinates(CGameClient &This) :
		CNamePlatePartText(This) {}
};

class CNamePlatePartCoordXAlignPopup : public CNamePlatePart
{
private:
	STextContainerIndex m_TextContainerIndex;
	char m_aText[32] = "";
	float m_StartTime = -INFINITY;
	float m_NextAllowedTriggerTime = -INFINITY;
	bool m_AlignHintEnabled = false;
	bool m_PrevAligned = false;
	vec2 m_TextSize = vec2(0.0f, 0.0f);
	ColorRGBA m_BaseColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

	static float RoundCoord(float Value)
	{
		return static_cast<float>(std::round(Value * 100.0f) / 100.0f);
	}

	static float CalcAlignPopupAlpha(float TimeSeconds)
	{
		const float FadeInDuration = 0.12f;
		const float HoldDuration = 0.56f;
		const float FadeOutDuration = 0.22f;
		const float TotalDuration = FadeInDuration + HoldDuration + FadeOutDuration;
		if(TimeSeconds < 0.0f || TimeSeconds > TotalDuration)
			return 0.0f;
		if(TimeSeconds < FadeInDuration)
			return TimeSeconds / FadeInDuration;
		if(TimeSeconds < FadeInDuration + HoldDuration)
			return 1.0f;
		const float FadeOutTime = TimeSeconds - FadeInDuration - HoldDuration;
		return 1.0f - FadeOutTime / FadeOutDuration;
	}

public:
	CNamePlatePartCoordXAlignPopup(CGameClient &This) :
		CNamePlatePart(This)
	{
		m_Size = vec2(0.0f, 0.0f);
		m_Padding = vec2(0.0f, 0.0f);
	}

	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		constexpr float AlignHintCooldown = 3.0f;
		const float Now = This.Client()->LocalTime();

		m_AlignHintEnabled = Data.m_CoordXAlignHint;
		m_Visible = m_AlignHintEnabled;
		m_BaseColor = Data.m_Color;
		m_Size = vec2(0.0f, 0.0f);
		m_Padding = vec2(0.0f, 0.0f);

		if(!m_AlignHintEnabled)
		{
			m_PrevAligned = false;
			return;
		}

		const bool Trigger = Data.m_CoordXAligned && !m_PrevAligned && Now >= m_NextAllowedTriggerTime;
		m_PrevAligned = Data.m_CoordXAligned;
		if(!Trigger)
			return;

		m_StartTime = Now;
		m_NextAllowedTriggerTime = Now + AlignHintCooldown;
		This.TextRender()->DeleteTextContainer(m_TextContainerIndex);

		const float BaseX = RoundCoord(Data.m_CoordXAlignBaseX);
		if(Data.m_CoordXAlignDiff >= 0)
			str_format(m_aText, sizeof(m_aText), "%.2f+%d", BaseX, Data.m_CoordXAlignDiff);
		else
			str_format(m_aText, sizeof(m_aText), "%.2f%d", BaseX, Data.m_CoordXAlignDiff);

		CTextCursor Cursor;
		Cursor.m_FontSize = Data.m_FontSizeCoords;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
		if(m_TextContainerIndex.Valid())
		{
			const STextBoundingBox PopupBounds = This.TextRender()->GetBoundingBoxTextContainer(m_TextContainerIndex);
			m_TextSize = vec2(PopupBounds.m_W, PopupBounds.m_H);
		}
		else
		{
			m_TextSize = vec2(0.0f, 0.0f);
		}
	}

	void Reset(CGameClient &This) override
	{
		This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
		m_StartTime = -INFINITY;
		m_NextAllowedTriggerTime = -INFINITY;
		m_PrevAligned = false;
		m_TextSize = vec2(0.0f, 0.0f);
	}

	void Render(CGameClient &This, vec2 Pos) const override
	{
		if(!m_AlignHintEnabled || !m_TextContainerIndex.Valid())
			return;

		const float TimeSinceStart = This.Client()->LocalTime() - m_StartTime;
		const float PopupAlpha = CalcAlignPopupAlpha(TimeSinceStart);
		if(PopupAlpha <= 0.0f)
			return;

		const float PopupDuration = 0.9f;
		const float Rise = std::clamp(TimeSinceStart / PopupDuration, 0.0f, 1.0f) * 12.0f;
		const ColorRGBA HintColor = ColorRGBA(0.2f, 1.0f, 0.2f, m_BaseColor.a * PopupAlpha);
		const ColorRGBA OutlineColor = s_OutlineColor.WithMultipliedAlpha(m_BaseColor.a * PopupAlpha);
		const float PopupX = Pos.x - m_TextSize.x / 2.0f;
		const float PopupY = Pos.y - m_TextSize.y - 36.0f - Rise;
		This.TextRender()->RenderTextContainer(m_TextContainerIndex, HintColor, OutlineColor, PopupX, PopupY);
	}
};

class CNamePlatePartReason : public CNamePlatePartText
{
private:
	char m_aText[MAX_WARLIST_REASON_LENGTH] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_InGame;
		if(!m_Visible)
			return false;
		const CNetObj_PlayerInfo *pInfo = This.m_Snap.m_apPlayerInfos[Data.m_ClientId];
		const char *pReason = This.m_WarList.GetWarData(Data.m_ClientId).m_aReason;
		m_Visible = pInfo && pReason[0] != '\0' && !pInfo->m_Local;
		if(!m_Visible)
			return false;
		m_Color = ColorRGBA(0.7f, 0.7f, 0.7f, Data.m_Color.a);
		return m_FontSize != Data.m_FontSizeClan || str_comp(m_aText, pReason) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClan;
		const char *pReason = This.m_WarList.GetWarData(Data.m_ClientId).m_aReason;
		str_copy(m_aText, pReason, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartReason(CGameClient &This) :
		CNamePlatePartText(This) {}
};

class CNamePlatePartIgnoreMark : public CNamePlatePartText
{
private:
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = (Data.m_InGame && Data.m_ShowName && This.Client()->State() != IClient::STATE_DEMOPLAYBACK && (This.m_aClients[Data.m_ClientId].m_Foe || This.m_aClients[Data.m_ClientId].m_ChatIgnore));
		if(!m_Visible)
			return false;
		m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, Data.m_Color.a);
		return m_FontSize != Data.m_FontSize;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		CTextCursor Cursor;
		This.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, FontIcons::FONT_ICON_COMMENT_SLASH);
		This.TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

public:
	CNamePlatePartIgnoreMark(CGameClient &This) :
		CNamePlatePartText(This) {}
};

class CNamePlatePartQmClientMark : public CNamePlatePartText
{
private:
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = g_Config.m_QmClientShowBadge && Data.m_InGame && Data.m_ShowName && This.GetQ1menGClientQid(Data.m_ClientId)[0] != '\0';
		if(!m_Visible)
			return false;
		m_Color = ColorRGBA(0.38f, 0.89f, 1.0f, Data.m_Color.a);
		return m_FontSize != Data.m_FontSize;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, "Qm");
	}

public:
	CNamePlatePartQmClientMark(CGameClient &This) :
		CNamePlatePartText(This) {}
};

// ***** Name Plates *****

class CNamePlate
{
private:
	bool m_Inited = false;
	bool m_InGame = false;
	char m_aCoreRowOrderConfigCache[sizeof(g_Config.m_QmNameplateRowOrder)] = {};
	PartsVector m_vpParts;
	void RenderLine(CGameClient &This,
		vec2 Pos, vec2 Size,
		PartsVector::iterator Start, PartsVector::iterator End)
	{
		Pos.x -= Size.x / 2.0f;
		for(auto PartIt = Start; PartIt != End; ++PartIt)
		{
			const CNamePlatePart &Part = **PartIt;
			if(Part.Visible())
			{
				Part.Render(This, vec2(
							  Pos.x + (Part.Padding().x + Part.Size().x) / 2.0f,
							  Pos.y - std::max(Size.y, Part.Padding().y + Part.Size().y) / 2.0f));
			}
			if(Part.Visible() || Part.ShiftOnInvis())
				Pos.x += Part.Size().x + Part.Padding().x;
		}
	}
	template<typename PartType, typename... ArgsType>
	void AddPart(CGameClient &This, ArgsType &&...Args)
	{
		m_vpParts.push_back(std::make_unique<PartType>(This, std::forward<ArgsType>(Args)...));
	}

	void AddNameRow(CGameClient &This)
	{
		AddPart<CNamePlatePartCountry>(This); // TClient
		AddPart<CNamePlatePartPing>(This); // TClient
		AddPart<CNamePlatePartQmClientMark>(This);
		AddPart<CNamePlatePartIgnoreMark>(This); // TClient
		AddPart<CNamePlatePartFriendMark>(This);
		AddPart<CNamePlatePartClientId>(This, false);
		AddPart<CNamePlatePartName>(This);
		AddPart<CNamePlatePartNewLine>(This);
	}

	void AddClanModule(CGameClient &This)
	{
		AddPart<CNamePlatePartClan>(This);
		AddPart<CNamePlatePartNewLine>(This);

		// Keep legacy optional rows attached to the clan module.
		AddPart<CNamePlatePartReason>(This); // TClient
		AddPart<CNamePlatePartNewLine>(This); // TClient
		AddPart<CNamePlatePartSkin>(This); // TClient
		AddPart<CNamePlatePartNewLine>(This); // TClient
		AddPart<CNamePlatePartClientId>(This, true);
		AddPart<CNamePlatePartNewLine>(This);
	}

	void AddHookRow(CGameClient &This)
	{
		AddPart<CNamePlatePartHookStrongWeak>(This);
		AddPart<CNamePlatePartHookStrongWeakId>(This);
		AddPart<CNamePlatePartNewLine>(This);
	}

	void AddCoordsRow(CGameClient &This)
	{
		AddPart<CNamePlatePartCoordinates>(This); // TClient
		AddPart<CNamePlatePartCoordXAlignPopup>(This); // TClient
		AddPart<CNamePlatePartNewLine>(This);
	}

	void AddKeysRow(CGameClient &This)
	{
		AddPart<CNamePlatePartDirection>(This, DIRECTION_LEFT);
		AddPart<CNamePlatePartDirection>(This, DIRECTION_UP);
		AddPart<CNamePlatePartDirection>(This, DIRECTION_RIGHT);
		AddPart<CNamePlatePartNewLine>(This);
	}

	void AddCoreRowModule(CGameClient &This, ENameplateCoreRow Row)
	{
		switch(Row)
		{
		case ENameplateCoreRow::NAME:
			AddNameRow(This);
			break;
		case ENameplateCoreRow::CLAN:
			AddClanModule(This);
			break;
		case ENameplateCoreRow::HOOK:
			AddHookRow(This);
			break;
		case ENameplateCoreRow::COORDS:
			AddCoordsRow(This);
			break;
		case ENameplateCoreRow::KEYS:
			AddKeysRow(This);
			break;
		case ENameplateCoreRow::NUM_ROWS:
			break;
		}
	}

	void RebuildPartLayout(CGameClient &This)
	{
		for(auto &Part : m_vpParts)
			Part->Reset(This);
		m_vpParts.clear();

		const auto CoreRowsTopToBottom = ParseNameplateCoreRowOrderTopToBottom(g_Config.m_QmNameplateRowOrder);
		for(auto It = CoreRowsTopToBottom.rbegin(); It != CoreRowsTopToBottom.rend(); ++It)
			AddCoreRowModule(This, *It);
	}

	void Init(CGameClient &This)
	{
		const bool NeedLayoutSync = !m_Inited || str_comp(m_aCoreRowOrderConfigCache, g_Config.m_QmNameplateRowOrder) != 0;
		if(!NeedLayoutSync)
			return;

		RebuildPartLayout(This);
		str_copy(m_aCoreRowOrderConfigCache, g_Config.m_QmNameplateRowOrder, sizeof(m_aCoreRowOrderConfigCache));
		m_Inited = true;
	}

public:
	CNamePlate() = default;
	CNamePlate(CGameClient &This, const CNamePlateData &Data)
	{
		// Convenience constructor
		Update(This, Data);
	}
	void Reset(CGameClient &This)
	{
		for(auto &Part : m_vpParts)
			Part->Reset(This);
	}
	void Update(CGameClient &This, const CNamePlateData &Data)
	{
		Init(This);
		m_InGame = Data.m_InGame;
		for(auto &Part : m_vpParts)
			Part->Update(This, Data);
	}
	void Render(CGameClient &This, const vec2 &PositionBottomMiddle)
	{
		dbg_assert(m_Inited, "Tried to render uninited nameplate");
		vec2 Position = PositionBottomMiddle;
		// X: Total width including padding of line, Y: Max height of line parts
		vec2 LineSize = vec2(0.0f, 0.0f);
		bool Empty = true;
		auto Start = m_vpParts.begin();
		for(auto PartIt = m_vpParts.begin(); PartIt != m_vpParts.end(); ++PartIt)
		{
			CNamePlatePart &Part = **PartIt;
			if(Part.NewLine())
			{
				if(!Empty)
				{
					RenderLine(This, Position, LineSize, Start, std::next(PartIt));
					Position.y -= LineSize.y;
				}
				Start = std::next(PartIt);
				LineSize = vec2(0.0f, 0.0f);
			}
			else if(Part.Visible() || Part.ShiftOnInvis())
			{
				Empty = false;
				LineSize.x += Part.Size().x + Part.Padding().x;
				LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);
			}
		}
		RenderLine(This, Position, LineSize, Start, m_vpParts.end());
		This.Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	bool IsInitialized() const
	{
		return m_Inited;
	}
	float TopY(const vec2 &PositionBottomMiddle) const
	{
		dbg_assert(m_Inited, "Tried to get top of uninited nameplate");

		vec2 Position = PositionBottomMiddle;
		vec2 LineSize = vec2(0.0f, 0.0f);
		bool Empty = true;
		float Top = Position.y;
		for(auto PartIt = m_vpParts.begin(); PartIt != m_vpParts.end(); ++PartIt) // NOLINT(modernize-loop-convert) For consistency with Render
		{
			CNamePlatePart &Part = **PartIt;
			if(Part.NewLine())
			{
				if(!Empty)
				{
					Top = std::min(Top, Position.y - LineSize.y / 2.0f);
					Position.y -= LineSize.y;
				}
				LineSize = vec2(0.0f, 0.0f);
			}
			else if(Part.Visible() || Part.ShiftOnInvis())
			{
				Empty = false;
				LineSize.x += Part.Size().x + Part.Padding().x;
				LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);
			}
		}
		Top = std::min(Top, Position.y - LineSize.y / 2.0f);
		return Top;
	}
	vec2 Size() const
	{
		dbg_assert(m_Inited, "Tried to get size of uninited nameplate");
		// X: Total width including padding of line, Y: Max height of line parts
		vec2 LineSize = vec2(0.0f, 0.0f);
		float WMax = 0.0f;
		float HTotal = 0.0f;
		bool Empty = true;
		for(auto PartIt = m_vpParts.begin(); PartIt != m_vpParts.end(); ++PartIt) // NOLINT(modernize-loop-convert) For consistency with Render
		{
			CNamePlatePart &Part = **PartIt;
			if(Part.NewLine())
			{
				if(!Empty)
				{
					if(LineSize.x > WMax)
						WMax = LineSize.x;
					HTotal += LineSize.y;
				}
				LineSize = vec2(0.0f, 0.0f);
			}
			else if(Part.Visible() || Part.ShiftOnInvis())
			{
				Empty = false;
				LineSize.x += Part.Size().x + Part.Padding().x;
				LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);
			}
		}
		if(LineSize.x > WMax)
			WMax = LineSize.x;
		HTotal += LineSize.y;
		return vec2(WMax, HTotal);
	}
};

class CNamePlates::CNamePlatesData
{
public:
	CNamePlate m_aNamePlates[MAX_CLIENTS];
	SChatBubbleAnimState m_aChatBubbleAnim[MAX_CLIENTS];
};

void CNamePlates::RenderNamePlateGame(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha)
{
	if(!pPlayerInfo)
		return;

	const int ClientId = pPlayerInfo->m_ClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	// Get screen edges to avoid rendering offscreen
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// Assume that the name plate fits into a 800x800 box placed directly above the tee
	ScreenX0 -= 400;
	ScreenX1 += 400;
	ScreenY1 += 800;
	if(!(in_range(Position.x, ScreenX0, ScreenX1) && in_range(Position.y, ScreenY0, ScreenY1)))
		return;

	CNamePlateData Data;

	const auto &ClientData = GameClient()->m_aClients[ClientId];
	const bool OtherTeam = GameClient()->IsOtherTeam(ClientId);

	Data.m_InGame = true;

	const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);

	const bool HideFocusNames = g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideNames;
	Data.m_ShowName = !HideFocusNames && (pPlayerInfo->m_Local ? g_Config.m_ClNamePlatesOwn : g_Config.m_ClNamePlates);
	GameClient()->FormatStreamerName(ClientId, Data.m_aName, sizeof(Data.m_aName));
	Data.m_ShowFriendMark = Data.m_ShowName && g_Config.m_ClNamePlatesFriendMark && GameClient()->m_aClients[ClientId].m_Friend;
	Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds) && !HideIdentity;
	Data.m_FontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;

	Data.m_ClientId = ClientId;
	Data.m_ClientIdSeparateLine = g_Config.m_ClNamePlatesIdsSeparateLine;
	Data.m_FontSizeClientId = Data.m_ClientIdSeparateLine ? (18.0f + 20.0f * g_Config.m_ClNamePlatesIdsSize / 100.0f) : Data.m_FontSize;

	Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan && !HideIdentity;
	GameClient()->FormatStreamerClan(ClientId, Data.m_aClan, sizeof(Data.m_aClan));
	Data.m_FontSizeClan = 18.0f + 20.0f * g_Config.m_ClNamePlatesClanSize / 100.0f;

	Data.m_ShowCoordX = g_Config.m_QmNameplateCoordX != 0;
	Data.m_ShowCoordY = g_Config.m_QmNameplateCoordY != 0;
	Data.m_ShowCoords = pPlayerInfo->m_Local ? g_Config.m_QmNameplateCoordsOwn : g_Config.m_QmNameplateCoords;
	Data.m_Coords = Position / 32.0f;
	Data.m_FontSizeCoords = 18.0f + 20.0f * g_Config.m_ClNamePlatesCoordsSize / 100.0f;
	Data.m_CoordXAlignHint = false;
	Data.m_CoordXAlignHintStrict = false;
	Data.m_CoordXAligned = false;
	Data.m_CoordXAlignBaseX = Data.m_Coords.x;
	Data.m_CoordXAlignDiff = 0;

	Data.m_FontSizeHookStrongWeak = 18.0f + 20.0f * g_Config.m_ClNamePlatesStrongSize / 100.0f;
	Data.m_FontSizeDirection = 18.0f + 20.0f * g_Config.m_ClDirectionSize / 100.0f;

	if(g_Config.m_ClNamePlatesAlways == 0)
		Alpha *= std::clamp(1.0f - std::pow(distance(GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy], Position) / 200.0f, 16.0f), 0.0f, 1.0f);
	if(OtherTeam)
		Alpha *= (float)g_Config.m_ClShowOthersAlpha / 100.0f;
	if(GameClient()->m_FastPractice.Enabled() && !GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_FastPractice.IsPracticeParticipant(pPlayerInfo->m_ClientId))
		Alpha = std::min(Alpha, 0.5f);

	Data.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f);
	if(g_Config.m_ClNamePlatesTeamcolors)
	{
		if(GameClient()->IsTeamPlay())
		{
			if(ClientData.m_Team == TEAM_RED)
				Data.m_Color = ColorRGBA(1.0f, 0.5f, 0.5f);
			else if(ClientData.m_Team == TEAM_BLUE)
				Data.m_Color = ColorRGBA(0.7f, 0.7f, 1.0f);
		}
		else
		{
			const int Team = GameClient()->m_Teams.Team(ClientId);
			if(Team)
				Data.m_Color = GameClient()->GetDDTeamColor(Team, 0.75f);
		}
	}
	Data.m_Color.a = Alpha;

	int ShowDirectionConfig = g_Config.m_ClShowDirection;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		ShowDirectionConfig = g_Config.m_ClVideoShowDirection;
#endif
	Data.m_DirLeft = Data.m_DirJump = Data.m_DirRight = false;
	const bool HideOverheadIndicators = g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideOverheadIndicators;
	switch(ShowDirectionConfig)
	{
	case 0: // Off
		Data.m_ShowDirection = false;
		break;
	case 1: // Others
		Data.m_ShowDirection = !pPlayerInfo->m_Local;
		break;
	case 2: // Everyone
		Data.m_ShowDirection = true;
		break;
	case 3: // Only self
		Data.m_ShowDirection = pPlayerInfo->m_Local;
		break;
	default:
		dbg_assert_failed("ShowDirectionConfig invalid");
	}
	if(HideOverheadIndicators)
		Data.m_ShowDirection = false;
	if(Data.m_ShowDirection)
	{
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			ClientId == GameClient()->m_aLocalIds[!g_Config.m_ClDummy])
		{
			const auto &InputData = GameClient()->m_Controls.m_aInputData[!g_Config.m_ClDummy];
			Data.m_DirLeft = InputData.m_Direction == -1;
			Data.m_DirJump = InputData.m_Jump == 1;
			Data.m_DirRight = InputData.m_Direction == 1;
		}
		else if(Client()->State() != IClient::STATE_DEMOPLAYBACK && pPlayerInfo->m_Local) // Always render local input when not in demo playback
		{
			const auto &InputData = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
			Data.m_DirLeft = InputData.m_Direction == -1;
			Data.m_DirJump = InputData.m_Jump == 1;
			Data.m_DirRight = InputData.m_Direction == 1;
		}
		else
		{
			const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
			Data.m_DirLeft = Character.m_Cur.m_Direction == -1;
			Data.m_DirJump = Character.m_Cur.m_Jumped & 1;
			Data.m_DirRight = Character.m_Cur.m_Direction == 1;
		}
	}

	Data.m_ShowHookStrongWeak = false;
	Data.m_HookStrongWeakState = EHookStrongWeakState::NEUTRAL;
	Data.m_ShowHookStrongWeakId = false;
	Data.m_HookStrongWeakId = 0;

	const bool Following = (GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_MultiViewActivated && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW);
	if(!HideOverheadIndicators && (GameClient()->m_Snap.m_LocalClientId != -1 || Following))
	{
		const int SelectedId = Following ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
		if(SelectedId >= 0 && SelectedId < MAX_CLIENTS)
		{
			const CGameClient::CSnapState::CCharacterInfo &Selected = GameClient()->m_Snap.m_aCharacters[SelectedId];
			const CGameClient::CSnapState::CCharacterInfo &Other = GameClient()->m_Snap.m_aCharacters[ClientId];

			if((Selected.m_HasExtendedData || GameClient()->m_aClients[SelectedId].m_SpecCharPresent) && Other.m_HasExtendedData)
			{
				int SelectedStrongWeakId = Selected.m_HasExtendedData ? Selected.m_ExtendedData.m_StrongWeakId : 0;
				Data.m_HookStrongWeakId = Other.m_ExtendedData.m_StrongWeakId;
				Data.m_ShowHookStrongWeakId = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong == 2;
				if(SelectedId == ClientId)
					Data.m_ShowHookStrongWeak = Data.m_ShowHookStrongWeakId;
				else
				{
					Data.m_HookStrongWeakState = SelectedStrongWeakId > Other.m_ExtendedData.m_StrongWeakId ? EHookStrongWeakState::STRONG : EHookStrongWeakState::WEAK;
					Data.m_ShowHookStrongWeak = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong > 0;
				}
			}
		}
	}

	// TClient
	if(!HideIdentity && g_Config.m_TcWarList && g_Config.m_TcWarListShowClan && GameClient()->m_WarList.GetWarData(ClientId).m_WarClan)
		Data.m_ShowClan = true;
	Data.m_Local = pPlayerInfo->m_Local;

	// Check if the nameplate is actually on screen
	CNamePlate &NamePlate = m_pData->m_aNamePlates[ClientId];
	NamePlate.Update(*GameClient(), Data);
	if(Alpha > 0.0f)
		NamePlate.Render(*GameClient(), Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset));
}

void CNamePlates::RenderNamePlatePreview(vec2 Position, int Dummy)
{
	const float FontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;
	const float FontSizeClan = 18.0f + 20.0f * g_Config.m_ClNamePlatesClanSize / 100.0f;
	const float FontSizeCoords = 18.0f + 20.0f * g_Config.m_ClNamePlatesCoordsSize / 100.0f;

	const float FontSizeDirection = 18.0f + 20.0f * g_Config.m_ClDirectionSize / 100.0f;
	const float FontSizeHookStrongWeak = 18.0f + 20.0f * g_Config.m_ClNamePlatesStrongSize / 100.0f;

	CNamePlateData Data;

	Data.m_InGame = false;
	Data.m_Color = g_Config.m_ClNamePlatesTeamcolors ? GameClient()->GetDDTeamColor(13, 0.75f) : TextRender()->DefaultTextColor();
	Data.m_Color.a = 1.0f;

	Data.m_ShowName = g_Config.m_ClNamePlates || g_Config.m_ClNamePlatesOwn;
	const char *pName = Dummy == 0 ? Client()->PlayerName() : Client()->DummyName();
	str_copy(Data.m_aName, str_utf8_skip_whitespaces(pName));
	str_utf8_trim_right(Data.m_aName);
	Data.m_FontSize = FontSize;

	Data.m_ShowFriendMark = Data.m_ShowName && g_Config.m_ClNamePlatesFriendMark;

	Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds);
	Data.m_ClientId = Dummy;
	Data.m_ClientIdSeparateLine = g_Config.m_ClNamePlatesIdsSeparateLine;
	Data.m_FontSizeClientId = Data.m_ClientIdSeparateLine ? (18.0f + 20.0f * g_Config.m_ClNamePlatesIdsSize / 100.0f) : Data.m_FontSize;

	Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan;
	const char *pClan = Dummy == 0 ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan;
	str_copy(Data.m_aClan, str_utf8_skip_whitespaces(pClan));
	str_utf8_trim_right(Data.m_aClan);
	if(Data.m_aClan[0] == '\0')
		str_copy(Data.m_aClan, "Clan Name");
	Data.m_FontSizeClan = FontSizeClan;

	Data.m_ShowCoordX = g_Config.m_QmNameplateCoordX != 0;
	Data.m_ShowCoordY = g_Config.m_QmNameplateCoordY != 0;
	Data.m_ShowCoords = g_Config.m_QmNameplateCoords || g_Config.m_QmNameplateCoordsOwn;
	Data.m_Coords = vec2(12.34f + Dummy, 56.78f + Dummy);
	Data.m_FontSizeCoords = FontSizeCoords;
	Data.m_CoordXAlignHint = false;
	Data.m_CoordXAlignHintStrict = false;
	Data.m_CoordXAligned = false;
	Data.m_CoordXAlignBaseX = Data.m_Coords.x;
	Data.m_CoordXAlignDiff = 0;

	Data.m_ShowDirection = g_Config.m_ClShowDirection != 0 ? true : false;
	Data.m_DirLeft = Data.m_DirJump = Data.m_DirRight = true;
	Data.m_FontSizeDirection = FontSizeDirection;

	Data.m_FontSizeHookStrongWeak = FontSizeHookStrongWeak;
	Data.m_HookStrongWeakId = Data.m_ClientId;
	Data.m_ShowHookStrongWeakId = g_Config.m_ClNamePlatesStrong == 2;
	if(Dummy == g_Config.m_ClDummy)
	{
		Data.m_HookStrongWeakState = EHookStrongWeakState::NEUTRAL;
		Data.m_ShowHookStrongWeak = Data.m_ShowHookStrongWeakId;
	}
	else
	{
		Data.m_HookStrongWeakState = Data.m_HookStrongWeakId == 2 ? EHookStrongWeakState::STRONG : EHookStrongWeakState::WEAK;
		Data.m_ShowHookStrongWeak = g_Config.m_ClNamePlatesStrong > 0;
	}

	// TClient
	Data.m_Local = false;

	CTeeRenderInfo TeeRenderInfo;
	if(Dummy == 0)
	{
		TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		TeeRenderInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
	}
	else
	{
		TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
		TeeRenderInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
	}
	TeeRenderInfo.m_Size = 64.0f;

	CNamePlate NamePlate(*GameClient(), Data);
	Position.y += NamePlate.Size().y / 2.0f;
	Position.y += (float)g_Config.m_ClNamePlatesOffset / 2.0f;
	// tee looking towards cursor, and it is happy when you touch it
	const vec2 DeltaPosition = Ui()->MousePos() - Position;
	const float Distance = length(DeltaPosition);
	const float InteractionDistance = 20.0f;
	const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, maximum(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
	const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : (Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes);
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, TeeEmote, TeeDirection, Position);
	Position.y -= (float)g_Config.m_ClNamePlatesOffset;
	NamePlate.Render(*GameClient(), Position);
	NamePlate.Reset(*GameClient());
}

void CNamePlates::ResetNamePlates()
{
	for(CNamePlate &NamePlate : m_pData->m_aNamePlates)
		NamePlate.Reset(*GameClient());
}

void CNamePlates::ResetChatBubbleAnimState(int ClientId, bool IsDestructing)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	SChatBubbleAnimState &AnimState = m_pData->m_aChatBubbleAnim[ClientId];
	if(IsDestructing)
	{
		TextRender()->DeleteTextContainer(AnimState.m_TextContainerIndex);
		AnimState = SChatBubbleAnimState();
		return;
	}
	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t NodeKey = ChatBubbleAnimNodeKey(ClientId);
	const bool RuntimeInitialized = AnimRuntime.GetValue(NodeKey, EUiAnimProperty::ALPHA, -1.0f) >= -0.5f;
	if(!AnimState.m_Initialized && AnimState.m_aCachedText[0] == '\0' && !RuntimeInitialized)
		return;

	TextRender()->DeleteTextContainer(AnimState.m_TextContainerIndex);
	AnimState = SChatBubbleAnimState();
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::WIDTH, 0.0f);
}

void CNamePlates::RenderChatBubble(vec2 Position, int ClientId, float Alpha)
{
	// Check if chat bubbles are enabled
	if(!g_Config.m_QmChatBubble)
		return;
	if(g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideChat)
		return;

	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	auto &ClientData = GameClient()->m_aClients[ClientId];
	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	SChatBubbleAnimState &AnimState = m_pData->m_aChatBubbleAnim[ClientId];
	const uint64_t NodeKey = ChatBubbleAnimNodeKey(ClientId);
	const bool RuntimeNeedsInit = AnimRuntime.GetValue(NodeKey, EUiAnimProperty::ALPHA, -1.0f) < -0.5f;
	if(!AnimState.m_Initialized || RuntimeNeedsInit)
	{
		TextRender()->DeleteTextContainer(AnimState.m_TextContainerIndex);
		AnimState = SChatBubbleAnimState();
		AnimState.m_Initialized = true;
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::WIDTH, 0.0f);
	}

	// Determine what text to show
	const char *pBubbleText = nullptr;
	bool IsTyping = false;
	int64_t Now = time();

	// Check if this is local player who is currently typing
	const bool IsLocalPlayer = (GameClient()->m_Snap.m_LocalClientId == ClientId);
	if(IsLocalPlayer && GameClient()->m_Chat.IsActive())
	{
		const char *pInputText = GameClient()->m_Chat.GetInputText();
		if(pInputText && pInputText[0] != '\0')
		{
			pBubbleText = pInputText;
			IsTyping = true;
		}
	}

	// If not typing, check for existing chat bubble
	if(!pBubbleText)
	{
		if(ClientData.m_aChatBubbleText[0] != '\0' && Now < ClientData.m_ChatBubbleExpireTick)
			pBubbleText = ClientData.m_aChatBubbleText;
	}

	const bool HasSourceText = pBubbleText && pBubbleText[0] != '\0';
	if(HasSourceText)
		str_copy(AnimState.m_aCachedText, pBubbleText, sizeof(AnimState.m_aCachedText));

	if(!HasSourceText && AnimState.m_aCachedText[0] == '\0')
	{
		AnimState.m_LastVisible = false;
		AnimState.m_LastTyping = false;
		return;
	}

	const bool TimedDisappearWindow = HasSourceText && !IsTyping && (ClientData.m_ChatBubbleExpireTick - Now <= time_freq());
	const bool NewTimedBubble = HasSourceText && !IsTyping && ClientData.m_ChatBubbleStartTick != AnimState.m_LastChatBubbleStartTick;
	if(NewTimedBubble)
		AnimState.m_LastChatBubbleStartTick = ClientData.m_ChatBubbleStartTick;

	const bool BecameVisible = HasSourceText && !AnimState.m_LastVisible;
	if(BecameVisible || NewTimedBubble)
	{
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::SCALE, 0.92f);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 12.0f);
		if(IsTyping)
			AnimRuntime.SetValue(NodeKey, EUiAnimProperty::WIDTH, 0.0f);
		AnimState.m_LastTargetAlpha = std::numeric_limits<float>::quiet_NaN();
		AnimState.m_LastTargetScale = std::numeric_limits<float>::quiet_NaN();
		AnimState.m_LastTargetSlide = std::numeric_limits<float>::quiet_NaN();
		AnimState.m_LastTargetFillChars = std::numeric_limits<float>::quiet_NaN();
	}

	float TargetAlpha = HasSourceText ? Alpha : 0.0f;
	float TargetScale = 1.0f;
	float TargetSlideOffset = 0.0f;
	float AlphaDuration = HasSourceText ? 0.20f : 0.16f;
	float TransformDuration = HasSourceText ? 0.20f : 0.16f;

	if(TimedDisappearWindow)
	{
		TargetAlpha = 0.0f;
		AlphaDuration = 1.0f;
		TransformDuration = 1.0f;
		int AnimationType = g_Config.m_QmChatBubbleAnimation;
		switch(AnimationType)
		{
		case 1:
			TargetScale = 0.2f;
			break;
		case 2:
			TargetSlideOffset = 50.0f;
			break;
		case 0:
		default:
			break;
		}
	}

	const float BubbleAlpha = std::clamp(ResolveChatBubbleAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::ALPHA, TargetAlpha, AlphaDuration, EEasing::EASE_OUT, AnimState.m_LastTargetAlpha), 0.0f, Alpha);
	const float AnimScale = std::max(0.0f, ResolveChatBubbleAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::SCALE, TargetScale, TransformDuration, EEasing::EASE_OUT, AnimState.m_LastTargetScale));
	const float AnimSlideOffset = ResolveChatBubbleAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::POS_Y, TargetSlideOffset, TransformDuration, EEasing::EASE_OUT, AnimState.m_LastTargetSlide);

	AnimState.m_LastVisible = HasSourceText;
	AnimState.m_LastTyping = IsTyping;

	const char *pRenderText = HasSourceText ? pBubbleText : AnimState.m_aCachedText;
	char aAnimatedText[sizeof(AnimState.m_aCachedText)];
	aAnimatedText[0] = '\0';
	const char *pDisplayText = pRenderText;

	size_t RenderBytes = 0;
	size_t RenderCharCountSize = 0;
	str_utf8_stats(pRenderText, std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max(), &RenderBytes, &RenderCharCountSize);
	(void)RenderBytes;
	const int RenderCharCount = static_cast<int>(RenderCharCountSize);

	if(IsTyping)
	{
		const float TargetFillChars = static_cast<float>(RenderCharCount);
		const float CurrentFillChars = AnimRuntime.GetValue(NodeKey, EUiAnimProperty::WIDTH, TargetFillChars);
		const float DeltaChars = std::abs(TargetFillChars - CurrentFillChars);
		float FillDuration = std::clamp(DeltaChars * 0.035f, 0.05f, 0.25f);
		if(TargetFillChars < CurrentFillChars)
			FillDuration = 0.08f;
		const float AnimatedFillChars = ResolveChatBubbleAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::WIDTH, TargetFillChars, FillDuration, EEasing::EASE_OUT, AnimState.m_LastTargetFillChars);
		const int VisibleChars = std::clamp(round_to_int(AnimatedFillChars), 0, RenderCharCount);
		str_utf8_copy_num(aAnimatedText, pRenderText, sizeof(aAnimatedText), VisibleChars);
		pDisplayText = aAnimatedText;
	}
	else
	{
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::WIDTH, static_cast<float>(RenderCharCount));
		AnimState.m_LastTargetFillChars = std::numeric_limits<float>::quiet_NaN();
	}

	if(BubbleAlpha <= 0.001f)
	{
		if(!HasSourceText)
			AnimState.m_aCachedText[0] = '\0';
		return;
	}

	// Get current screen bounds (world coordinates with current zoom)
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// Check if position is on screen
	if(!(in_range(Position.x, ScreenX0 - 400, ScreenX1 + 400) && in_range(Position.y, ScreenY0 - 400, ScreenY1 + 400)))
		return;

	// Calculate screen-space position (0-1 range) for zoom independence
	float ScreenWidth = ScreenX1 - ScreenX0;
	float ScreenHeight = ScreenY1 - ScreenY0;
	float ScreenPosX = (Position.x - ScreenX0) / ScreenWidth;

	// Map to interface coordinates for text rendering (fixed zoom)
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

	// Get interface screen bounds (fixed regardless of zoom)
	float InterfaceX0, InterfaceY0, InterfaceX1, InterfaceY1;
	Graphics()->GetScreen(&InterfaceX0, &InterfaceY0, &InterfaceX1, &InterfaceY1);
	float InterfaceWidth = InterfaceX1 - InterfaceX0;
	float InterfaceHeight = InterfaceY1 - InterfaceY0;

	// Convert screen-space position to interface coordinates
	float InterfaceX = InterfaceX0 + ScreenPosX * InterfaceWidth;

	// Keep bubble sizing in screen space so rapid camera zoom does not force
	// a full text relayout every frame.
	constexpr float kBubblePadding = 12.0f;
	constexpr float kBubbleRounding = 10.0f;
	constexpr float kBubbleMaxWidth = 230.0f;
	const float BaseFontSize = (float)g_Config.m_QmChatBubbleFontSize;

	const float FontSize = BaseFontSize;
	const float Padding = kBubblePadding;
	const float Rounding = kBubbleRounding;
	const float MaxWidth = kBubbleMaxWidth;

	// Anchor bubble to the top of the nameplate plus default nameplate spacing.
	float NameplateTopWorldY = Position.y - (float)g_Config.m_ClNamePlatesOffset;
	const vec2 NameplateBottomMiddleWorld = Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset);
	const CNamePlate &NamePlate = m_pData->m_aNamePlates[ClientId];
	if(NamePlate.IsInitialized())
		NameplateTopWorldY = NamePlate.TopY(NameplateBottomMiddleWorld);
	const float ScreenPosNameplateTopY = (NameplateTopWorldY - ScreenY0) / ScreenHeight;
	const float InterfaceNameplateTopY = InterfaceY0 + ScreenPosNameplateTopY * InterfaceHeight;

	unsigned int PrevFlags = TextRender()->GetRenderFlags();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);

	const bool UseTextContainer = !IsTyping && std::abs(AnimScale - 1.0f) <= 0.001f;
	const bool LayoutDirty = !AnimState.m_TextContainerIndex.Valid() ||
		str_comp(AnimState.m_aLayoutText, pDisplayText) != 0 ||
		AnimState.m_CachedFontSize != FontSize ||
		AnimState.m_CachedLineWidth != MaxWidth;

	if(!IsTyping && LayoutDirty)
	{
		TextRender()->DeleteTextContainer(AnimState.m_TextContainerIndex);

		CTextCursor LayoutCursor;
		LayoutCursor.m_FontSize = FontSize;
		LayoutCursor.m_LineWidth = MaxWidth;
		if(TextRender()->CreateTextContainer(AnimState.m_TextContainerIndex, &LayoutCursor, pDisplayText))
		{
			const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(AnimState.m_TextContainerIndex);
			AnimState.m_CachedTextWidth = BoundingBox.m_W;
			AnimState.m_CachedTextHeight = BoundingBox.m_H;
			AnimState.m_CachedFontSize = FontSize;
			AnimState.m_CachedLineWidth = MaxWidth;
			str_copy(AnimState.m_aLayoutText, pDisplayText, sizeof(AnimState.m_aLayoutText));
		}
		else
		{
			AnimState.m_CachedTextWidth = 0.0f;
			AnimState.m_CachedTextHeight = 0.0f;
			AnimState.m_CachedFontSize = FontSize;
			AnimState.m_CachedLineWidth = MaxWidth;
			AnimState.m_aLayoutText[0] = '\0';
		}
	}

	float TextHeight = 0.0f;
	float TextWidth = 0.0f;
	if(IsTyping)
	{
		CTextCursor MeasureCursor;
		MeasureCursor.m_FontSize = FontSize;
		MeasureCursor.m_LineWidth = MaxWidth;
		MeasureCursor.m_Flags = 0;
		TextRender()->TextEx(&MeasureCursor, pDisplayText);
		TextHeight = MeasureCursor.Height();
		TextWidth = MeasureCursor.m_LongestLineWidth;
	}
	else
	{
		TextHeight = AnimState.m_CachedTextHeight;
		TextWidth = AnimState.m_CachedTextWidth;
	}

	// Calculate bubble dimensions and position (with scale animation)
	float BubbleWidth = (TextWidth + Padding * 2.0f) * AnimScale;
	float BubbleHeight = (TextHeight + Padding * 2.0f) * AnimScale;

	// Position bubble above the nameplate with built-in nameplate spacing.
	float AnimOffset = AnimSlideOffset;
	const float BubbleGap = DEFAULT_PADDING;
	float BubbleX = InterfaceX - BubbleWidth / 2.0f;
	float BubbleY = InterfaceNameplateTopY - BubbleGap - BubbleHeight + AnimOffset;

	float ConfigAlpha = g_Config.m_QmChatBubbleAlpha / 100.0f;

	ColorHSLA BgHSLA(g_Config.m_QmChatBubbleBgColor, true);
	ColorRGBA BgColor = color_cast<ColorRGBA>(BgHSLA);
	BgColor.a *= BubbleAlpha * ConfigAlpha;

	Graphics()->TextureClear();
	Graphics()->DrawRect(
		BubbleX, BubbleY,
		BubbleWidth, BubbleHeight,
		BgColor,
		IGraphics::CORNER_ALL,
		Rounding);

	ColorHSLA TextHSLA(g_Config.m_QmChatBubbleTextColor, false);
	ColorRGBA TextColor = color_cast<ColorRGBA>(TextHSLA);
	TextColor.a *= BubbleAlpha * ConfigAlpha;

	ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
	OutlineColor.a *= TextColor.a;

	TextRender()->TextColor(TextColor);
	TextRender()->TextOutlineColor(OutlineColor);

	if(UseTextContainer && AnimState.m_TextContainerIndex.Valid())
	{
		const float TextX = BubbleX + (BubbleWidth - TextWidth) / 2.0f;
		const float TextY = BubbleY + (BubbleHeight - TextHeight) / 2.0f;
		TextRender()->RenderTextContainer(AnimState.m_TextContainerIndex, TextColor, OutlineColor, TextX, TextY);
	}
	else
	{
		const float ScaledTextWidth = TextWidth * AnimScale;
		const float ScaledTextHeight = TextHeight * AnimScale;
		const float TextX = BubbleX + (BubbleWidth - ScaledTextWidth) / 2.0f;
		const float TextY = BubbleY + (BubbleHeight - ScaledTextHeight) / 2.0f;

		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize * AnimScale;
		Cursor.m_LineWidth = MaxWidth * AnimScale;
		Cursor.m_Flags = TEXTFLAG_RENDER;
		Cursor.SetPosition(vec2(TextX, TextY));
		TextRender()->TextEx(&Cursor, pDisplayText);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->SetRenderFlags(PrevFlags);

	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}

void CNamePlates::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	int ShowDirection = g_Config.m_ClShowDirection;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		ShowDirection = g_Config.m_ClVideoShowDirection;
#endif
	const bool ShowCoords = (g_Config.m_QmNameplateCoords || g_Config.m_QmNameplateCoordsOwn) &&
				(g_Config.m_QmNameplateCoordX || g_Config.m_QmNameplateCoordY);
	const bool HideFocusNames = g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideNames;
	const bool RenderNames = !HideFocusNames && (g_Config.m_ClNamePlates || g_Config.m_ClNamePlatesOwn);
	const bool RenderNameplates = RenderNames || ShowDirection != 0 || ShowCoords;
	const bool RenderChatBubbles = g_Config.m_QmChatBubble != 0;
	const bool RenderFreezeWakeupPopups = GameClient()->HasFreezeWakeupPopups();
	if(!RenderNameplates && !RenderChatBubbles && !RenderFreezeWakeupPopups)
		return;

	if(RenderNameplates || RenderChatBubbles)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[i];
			if(!pInfo)
			{
				ResetChatBubbleAnimState(i);
				continue;
			}

			// Each player can also have a spectator char whose name plate is displayed independently
			if(GameClient()->m_aClients[i].m_SpecCharPresent && RenderNameplates)
			{
				const vec2 RenderPos = GameClient()->m_aClients[i].m_SpecChar;
				RenderNamePlateGame(RenderPos, pInfo, 0.4f);
			}
			// Only render name plates for active characters
			if(GameClient()->m_Snap.m_aCharacters[i].m_Active)
			{
				// TClient
				if(GameClient()->m_aClients[i].m_IsVolleyBall)
					continue;
				// if(g_Config.m_TcRenderNameplateSpec > 0)
				//	continue;
				const vec2 RenderPos = GameClient()->m_aClients[i].m_RenderPos;
				if(RenderNameplates || RenderChatBubbles)
					RenderNamePlateGame(RenderPos, pInfo, RenderNameplates ? 1.0f : 0.0f);

				// Render chat bubble above player
				if(RenderChatBubbles)
					RenderChatBubble(RenderPos, i, 1.0f);
			}
		}
	}

	if(RenderFreezeWakeupPopups)
		GameClient()->RenderFreezeWakeupPopups();
}

void CNamePlates::OnWindowResize()
{
	ResetNamePlates();
	for(int i = 0; i < MAX_CLIENTS; ++i)
		ResetChatBubbleAnimState(i);
}

CNamePlates::CNamePlates() :
	m_pData(new CNamePlates::CNamePlatesData()) {}

CNamePlates::~CNamePlates()
{
	delete m_pData;
}

