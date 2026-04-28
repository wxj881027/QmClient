/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "console.h"

#include <base/lock.h>
#include <base/logger.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/ringbuffer.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

static constexpr float FONT_SIZE = 10.0f;
static constexpr float LINE_SPACING = 1.0f;
static constexpr float LINK_CLICK_DRAG_THRESHOLD = 3.0f;
static constexpr float LINK_UNDERLINE_HEIGHT = 0.12f;
static constexpr ColorRGBA LINK_TEXT_COLOR = ColorRGBA(0.2f, 0.65f, 1.0f, 1.0f);
static constexpr ColorRGBA LINK_UNDERLINE_COLOR = ColorRGBA(0.2f, 0.65f, 1.0f, 0.9f);

struct SLinkRange
{
	int m_StartChar;
	int m_EndChar;
};

static bool IsLinkTrailingPunctuation(char c)
{
	switch(c)
	{
	case '.':
	case ',':
	case ';':
	case ':':
	case ')':
	case ']':
	case '}':
	case '>':
	case '"':
	case '\'':
		return true;
	default:
		return false;
	}
}

static const char *FindNextLinkStart(const char *pText)
{
	const char *pHttps = str_find_nocase(pText, "https://");
	const char *pHttp = str_find_nocase(pText, "http://");
	const char *pWww = str_find_nocase(pText, "www.");
	const char *pStart = nullptr;
	for(const char *pCandidate : {pHttps, pHttp, pWww})
	{
		if(!pCandidate)
			continue;
		if(!pStart || pCandidate < pStart)
			pStart = pCandidate;
	}
	return pStart;
}

static bool FindLinkAtChar(const char *pText, int CharIndex, char *pOut, size_t OutSize)
{
	if(!pText || CharIndex < 0 || OutSize == 0)
		return false;

	const char *pSearch = pText;
	while(*pSearch)
	{
		const char *pStart = FindNextLinkStart(pSearch);
		if(!pStart)
			break;

		const char *pEnd = pStart;
		while(*pEnd && !str_isspace(*pEnd))
			++pEnd;

		while(pEnd > pStart && IsLinkTrailingPunctuation(pEnd[-1]))
			--pEnd;

		if(pEnd > pStart)
		{
			const int StartChar = (int)str_utf8_offset_bytes_to_chars(pText, pStart - pText);
			const int EndChar = (int)str_utf8_offset_bytes_to_chars(pText, pEnd - pText);
			if(CharIndex >= StartChar && CharIndex < EndChar)
			{
				str_truncate(pOut, (int)OutSize, pStart, (int)(pEnd - pStart));
				return true;
			}
			pSearch = pEnd;
		}
		else
		{
			pSearch = pStart + 1;
		}
	}

	return false;
}

static bool HasPotentialLink(const char *pText)
{
	return pText && (str_find_nocase(pText, "https://") || str_find_nocase(pText, "http://") || str_find_nocase(pText, "www."));
}

static void CollectLinkRanges(const char *pText, std::vector<SLinkRange> &vRanges)
{
	vRanges.clear();
	if(!pText || pText[0] == '\0')
		return;

	const char *pSearch = pText;
	while(*pSearch)
	{
		const char *pStart = FindNextLinkStart(pSearch);
		if(!pStart)
			break;

		const char *pEnd = pStart;
		while(*pEnd && !str_isspace(*pEnd))
			++pEnd;

		while(pEnd > pStart && IsLinkTrailingPunctuation(pEnd[-1]))
			--pEnd;

		if(pEnd > pStart)
		{
			const int StartChar = (int)str_utf8_offset_bytes_to_chars(pText, pStart - pText);
			const int EndChar = (int)str_utf8_offset_bytes_to_chars(pText, pEnd - pText);
			if(EndChar > StartChar)
				vRanges.push_back({StartChar, EndChar});
			pSearch = pEnd;
		}
		else
		{
			pSearch = pStart + 1;
		}
	}
}

static void BuildLinkColorSplits(const std::vector<SLinkRange> &vRanges, std::vector<STextColorSplit> &vSplits)
{
	vSplits.clear();
	if(vRanges.empty())
		return;

	int Cursor = 0;
	for(const auto &Range : vRanges)
	{
		if(Range.m_StartChar > Cursor)
			vSplits.emplace_back(Cursor, Range.m_StartChar - Cursor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		if(Range.m_EndChar > Range.m_StartChar)
			vSplits.emplace_back(Range.m_StartChar, Range.m_EndChar - Range.m_StartChar, LINK_TEXT_COLOR);
		Cursor = Range.m_EndChar;
	}
	vSplits.emplace_back(Cursor, 9999, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
}

struct SChatExportLine
{
	std::string m_Raw;
	std::string m_Time;
	std::string m_Sender;
	std::string m_Message;
	bool m_Local;
};

struct SChatExportLayout
{
	SChatExportLine m_Line;
	std::vector<std::string> m_vMessageLines;
	int m_CardWidth;
	int m_CardHeight;
	int m_RecordHeight;
};

struct SChatExportPage
{
	int m_Start;
	int m_End;
	int m_Height;
};

static bool WriteAll(IOHANDLE File, const void *pData, size_t Size)
{
	return Size == 0 || io_write(File, pData, Size) == Size;
}

static bool WriteString(IOHANDLE File, const std::string &Text)
{
	return WriteAll(File, Text.data(), Text.size());
}

static void AppendHtmlEscaped(std::string &Output, const std::string &Text)
{
	for(const char Char : Text)
	{
		switch(Char)
		{
		case '&':
			Output.append("&amp;");
			break;
		case '<':
			Output.append("&lt;");
			break;
		case '>':
			Output.append("&gt;");
			break;
		case '"':
			Output.append("&quot;");
			break;
		case '\'':
			Output.append("&#39;");
			break;
		default:
			Output.push_back(Char);
			break;
		}
	}
}

static void TrimAsciiSpaces(std::string &Text)
{
	size_t Start = 0;
	while(Start < Text.size() && (Text[Start] == ' ' || Text[Start] == '\t' || Text[Start] == '\r' || Text[Start] == '\n'))
		++Start;
	size_t End = Text.size();
	while(End > Start && (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '\r' || Text[End - 1] == '\n'))
		--End;
	if(Start != 0 || End != Text.size())
		Text = Text.substr(Start, End - Start);
}

static std::string ChatExportDisplayText(const SChatExportLine &Line)
{
	if(Line.m_Sender.empty())
		return Line.m_Message;
	std::string Text = Line.m_Sender;
	Text.append(":");
	Text.append(Line.m_Message);
	return Text;
}

static bool TryParseChatExportLine(const char *pText, const char *pLocalName, SChatExportLine &Line)
{
	struct SChatPrefix
	{
		const char *m_pNeedle;
	};
	static const SChatPrefix s_aPrefixes[] = {
		{" chat/all: "},
		{" chat/team: "},
		{" chat/whisper: "},
	};

	const char *pMessage = nullptr;
	for(const auto &Prefix : s_aPrefixes)
	{
		const char *pFound = str_find(pText, Prefix.m_pNeedle);
		if(pFound)
		{
			pMessage = pFound + str_length(Prefix.m_pNeedle);
			break;
		}
	}
	if(!pMessage)
		return false;

	Line.m_Raw = pText;
	Line.m_Time.clear();
	if(str_length(pText) >= 19 && pText[4] == '-' && pText[7] == '-' && pText[10] == ' ' && pText[13] == ':' && pText[16] == ':')
		Line.m_Time.assign(pText, 19);

	const char *pNameEnd = str_find(pMessage, ": ");
	if(pNameEnd && pNameEnd > pMessage)
	{
		Line.m_Sender.assign(pMessage, pNameEnd - pMessage);
		Line.m_Message = pNameEnd + 2;
	}
	else
	{
		Line.m_Sender.clear();
		Line.m_Message = pMessage;
	}
	Line.m_Local = pLocalName && pLocalName[0] != '\0' && !Line.m_Sender.empty() && str_comp(Line.m_Sender.c_str(), pLocalName) == 0;
	return true;
}

static std::vector<std::string> WrapChatExportText(ITextRender *pTextRender, const std::string &Text, int FontSize, int MaxWidth)
{
	std::vector<std::string> vLines;
	std::string Remaining = Text;
	TrimAsciiSpaces(Remaining);
	if(Remaining.empty())
	{
		vLines.emplace_back("");
		return vLines;
	}

	while(!Remaining.empty())
	{
		if(pTextRender->TextWidth(FontSize, Remaining.c_str()) <= MaxWidth)
		{
			vLines.push_back(Remaining);
			break;
		}

		int BestEnd = 0;
		int LastSpaceEnd = -1;
		int Cursor = 0;
		while(Cursor < (int)Remaining.size())
		{
			const int NextCursor = str_utf8_forward(Remaining.c_str(), Cursor);
			const int SafeNextCursor = NextCursor > Cursor ? NextCursor : Cursor + 1;
			std::string Candidate = Remaining.substr(0, SafeNextCursor);
			if(pTextRender->TextWidth(FontSize, Candidate.c_str()) > MaxWidth && BestEnd > 0)
				break;
			BestEnd = SafeNextCursor;
			if(Remaining[SafeNextCursor - 1] == ' ' || Remaining[SafeNextCursor - 1] == '\t')
				LastSpaceEnd = SafeNextCursor;
			Cursor = SafeNextCursor;
		}

		int Cut = BestEnd;
		if(BestEnd < (int)Remaining.size() && LastSpaceEnd > 0)
			Cut = LastSpaceEnd;
		if(Cut <= 0)
			Cut = maximum(1, str_utf8_forward(Remaining.c_str(), 0));

		std::string Line = Remaining.substr(0, Cut);
		TrimAsciiSpaces(Line);
		if(!Line.empty())
			vLines.push_back(Line);
		Remaining.erase(0, Cut);
		TrimAsciiSpaces(Remaining);
	}

	return vLines;
}

static unsigned char ColorByte(float Value)
{
	return (unsigned char)std::clamp(round_to_int(Value * 255.0f), 0, 255);
}

static void BlendPixel(CImageInfo &Image, int PosX, int PosY, ColorRGBA Color, float AlphaMultiplier = 1.0f)
{
	if(PosX < 0 || PosY < 0 || PosX >= (int)Image.m_Width || PosY >= (int)Image.m_Height)
		return;

	const float Alpha = std::clamp(Color.a * AlphaMultiplier, 0.0f, 1.0f);
	const size_t Offset = ((size_t)PosY * Image.m_Width + (size_t)PosX) * Image.PixelSize();
	Image.m_pData[Offset + 0] = ColorByte(Color.r * Alpha + (Image.m_pData[Offset + 0] / 255.0f) * (1.0f - Alpha));
	Image.m_pData[Offset + 1] = ColorByte(Color.g * Alpha + (Image.m_pData[Offset + 1] / 255.0f) * (1.0f - Alpha));
	Image.m_pData[Offset + 2] = ColorByte(Color.b * Alpha + (Image.m_pData[Offset + 2] / 255.0f) * (1.0f - Alpha));
	Image.m_pData[Offset + 3] = 255;
}

static void FillRect(CImageInfo &Image, int PosX, int PosY, int Width, int Height, ColorRGBA Color)
{
	const int StartX = std::clamp(PosX, 0, (int)Image.m_Width);
	const int StartY = std::clamp(PosY, 0, (int)Image.m_Height);
	const int EndX = std::clamp(PosX + Width, 0, (int)Image.m_Width);
	const int EndY = std::clamp(PosY + Height, 0, (int)Image.m_Height);
	for(int Row = StartY; Row < EndY; ++Row)
	{
		for(int Column = StartX; Column < EndX; ++Column)
			BlendPixel(Image, Column, Row, Color);
	}
}

static void FillRoundedRect(CImageInfo &Image, int PosX, int PosY, int Width, int Height, int Radius, ColorRGBA Color)
{
	const int StartX = std::clamp(PosX, 0, (int)Image.m_Width);
	const int StartY = std::clamp(PosY, 0, (int)Image.m_Height);
	const int EndX = std::clamp(PosX + Width, 0, (int)Image.m_Width);
	const int EndY = std::clamp(PosY + Height, 0, (int)Image.m_Height);
	const int RadiusSq = Radius * Radius;
	for(int Row = StartY; Row < EndY; ++Row)
	{
		for(int Column = StartX; Column < EndX; ++Column)
		{
			int CornerX = 0;
			if(Column < PosX + Radius)
				CornerX = PosX + Radius - Column;
			else if(Column >= PosX + Width - Radius)
				CornerX = Column - (PosX + Width - Radius - 1);
			int CornerY = 0;
			if(Row < PosY + Radius)
				CornerY = PosY + Radius - Row;
			else if(Row >= PosY + Height - Radius)
				CornerY = Row - (PosY + Height - Radius - 1);
			if(CornerX > 0 && CornerY > 0 && CornerX * CornerX + CornerY * CornerY > RadiusSq)
				continue;
			BlendPixel(Image, Column, Row, Color);
		}
	}
}

static void DrawCircleOutline(CImageInfo &Image, int CenterX, int CenterY, int Radius, int Thickness, ColorRGBA Color)
{
	const int OuterSq = Radius * Radius;
	const int InnerRadius = maximum(0, Radius - Thickness);
	const int InnerSq = InnerRadius * InnerRadius;
	for(int Row = CenterY - Radius; Row <= CenterY + Radius; ++Row)
	{
		for(int Column = CenterX - Radius; Column <= CenterX + Radius; ++Column)
		{
			const int DistX = Column - CenterX;
			const int DistY = Row - CenterY;
			const int DistSq = DistX * DistX + DistY * DistY;
			if(DistSq <= OuterSq && DistSq >= InnerSq)
				BlendPixel(Image, Column, Row, Color);
		}
	}
}

static void ClearImageRect(CImageInfo &Image, int PosX, int PosY, int Width, int Height)
{
	const int StartX = std::clamp(PosX, 0, (int)Image.m_Width);
	const int StartY = std::clamp(PosY, 0, (int)Image.m_Height);
	const int EndX = std::clamp(PosX + Width, 0, (int)Image.m_Width);
	const int EndY = std::clamp(PosY + Height, 0, (int)Image.m_Height);
	for(int Row = StartY; Row < EndY; ++Row)
	{
		uint8_t *pRow = Image.m_pData + ((size_t)Row * Image.m_Width + (size_t)StartX) * Image.PixelSize();
		mem_zero(pRow, (EndX - StartX) * Image.PixelSize());
	}
}

static void DrawPngText(ITextRender *pTextRender, CImageInfo &Image, CImageInfo &Mask, int PosX, int PosY, int MaxWidth, int FontSize, const std::string &Text, ColorRGBA Color)
{
	if(Text.empty() || PosX >= (int)Image.m_Width || PosY >= (int)Image.m_Height)
		return;

	const int RectWidth = std::clamp(MaxWidth, 1, (int)Image.m_Width - PosX);
	const int RectHeight = std::clamp(FontSize + 14, 1, (int)Image.m_Height - PosY);
	ClearImageRect(Mask, PosX, PosY, RectWidth, RectHeight);

	int Cursor = 0;
	int DrawX = PosX;
	const int EndX = PosX + RectWidth;
	while(Cursor < (int)Text.size() && DrawX < EndX)
	{
		const int NextCursor = str_utf8_forward(Text.c_str(), Cursor);
		const int SafeNextCursor = NextCursor > Cursor ? NextCursor : Cursor + 1;
		const std::string Character = Text.substr(Cursor, SafeNextCursor - Cursor);
		int Advance = round_to_int(pTextRender->TextWidth(FontSize, Character.c_str()));
		if(Character == " " || Character == "\t")
			Advance = maximum(Advance, FontSize / 3);
		else
			Advance = maximum(Advance, 1);

		if(Character != " " && Character != "\t")
			pTextRender->UploadEntityLayerText(Mask, EndX - DrawX, RectHeight, Character.c_str(), str_length(Character.c_str()), DrawX, PosY, FontSize);

		DrawX += Advance;
		Cursor = SafeNextCursor;
	}

	for(int Row = PosY; Row < PosY + RectHeight && Row < (int)Image.m_Height; ++Row)
	{
		for(int Column = PosX; Column < PosX + RectWidth && Column < (int)Image.m_Width; ++Column)
		{
			const size_t Offset = ((size_t)Row * Mask.m_Width + (size_t)Column) * Mask.PixelSize();
			const float Alpha = Mask.m_pData[Offset + 3] / 255.0f;
			if(Alpha > 0.0f)
				BlendPixel(Image, Column, Row, Color, Alpha);
		}
	}
	ClearImageRect(Mask, PosX, PosY, RectWidth, RectHeight);
}

static void DrawPngTextCentered(ITextRender *pTextRender, CImageInfo &Image, CImageInfo &Mask, int CenterX, int PosY, int MaxWidth, int FontSize, const std::string &Text, ColorRGBA Color)
{
	if(Text.empty())
		return;
	const int TextWidth = round_to_int(pTextRender->TextWidth(FontSize, Text.c_str()));
	const int DrawWidth = minimum(TextWidth, MaxWidth);
	const int TextX = std::clamp(CenterX - TextWidth / 2, 0, maximum(0, (int)Image.m_Width - DrawWidth));
	DrawPngText(pTextRender, Image, Mask, TextX, PosY, MaxWidth, FontSize, Text, Color);
}

static bool SaveChatExportTxt(IStorage *pStorage, const char *pFilename, const std::vector<SChatExportLine> &vLines)
{
	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("console", "Failed to open '%s'", pFilename);
		return false;
	}

	bool Success = true;
	for(const SChatExportLine &Line : vLines)
	{
		Success &= WriteString(File, Line.m_Raw);
		Success &= io_write_newline(File);
	}
	io_close(File);
	if(!Success)
		log_error("console", "Failed to write '%s'", pFilename);
	return Success;
}

static bool SaveChatExportHtml(IStorage *pStorage, const char *pFilename, const std::vector<SChatExportLine> &vLines)
{
	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("console", "Failed to open '%s'", pFilename);
		return false;
	}

	std::string Html;
	Html.reserve(vLines.size() * 256 + 2048);
	Html.append("<!doctype html><html><head><meta charset=\"utf-8\"><title>");
	AppendHtmlEscaped(Html, Localize("QmClient 聊天记录"));
	Html.append("</title>");
	Html.append("<style>body{margin:0;background:#eef1f5;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;color:#202124}.wrap{max-width:920px;margin:0 auto;padding:28px}.title{font-size:26px;font-weight:700;margin-bottom:6px}.sub{color:#68707a;margin-bottom:24px}.msg{display:flex;margin:12px 0}.msg.local{justify-content:flex-end}.bubble{max-width:72%;border-radius:16px;padding:10px 14px;background:#fff;box-shadow:0 1px 3px #0002;white-space:pre-wrap;word-break:break-word}.local .bubble{background:#3d7eff;color:#fff}.time{font-size:12px;opacity:.68;margin-top:6px}</style>");
	Html.append("</head><body><div class=\"wrap\"><div class=\"title\">");
	AppendHtmlEscaped(Html, Localize("QmClient 聊天记录"));
	Html.append("</div><div class=\"sub\">");
	AppendHtmlEscaped(Html, Localize("共"));
	Html.append(" ");
	Html.append(std::to_string(vLines.size()));
	Html.append(" ");
	AppendHtmlEscaped(Html, Localize("条消息"));
	Html.append("</div>");
	for(const SChatExportLine &Line : vLines)
	{
		const std::string DisplayText = ChatExportDisplayText(Line);
		Html.append("<div class=\"msg");
		if(Line.m_Local)
			Html.append(" local");
		Html.append("\"><div class=\"bubble\">");
		AppendHtmlEscaped(Html, DisplayText);
		if(!Line.m_Time.empty())
		{
			Html.append("<div class=\"time\">");
			AppendHtmlEscaped(Html, Line.m_Time);
			Html.append("</div>");
		}
		Html.append("</div></div>");
	}
	Html.append("</div></body></html>\n");

	const bool Success = WriteString(File, Html);
	io_close(File);
	if(!Success)
		log_error("console", "Failed to write '%s'", pFilename);
	return Success;
}

static std::vector<SChatExportLayout> BuildChatExportLayouts(ITextRender *pTextRender, const std::vector<SChatExportLine> &vLines)
{
	static constexpr int FONT_SIZE_MESSAGE = 58;
	static constexpr int CARD_MAX_WIDTH = 620;
	static constexpr int CARD_MIN_WIDTH = 220;
	static constexpr int CARD_PADDING_X = 46;
	static constexpr int CARD_PADDING_Y = 26;
	static constexpr int MESSAGE_LINE_HEIGHT = 72;
	static constexpr int TIME_LINE_HEIGHT = 52;
	static constexpr int TIME_TOP_MARGIN = 18;
	static constexpr int RECORD_GAP = 54;

	std::vector<SChatExportLayout> vLayouts;
	vLayouts.reserve(vLines.size());
	for(const SChatExportLine &Line : vLines)
	{
		SChatExportLayout Layout;
		Layout.m_Line = Line;
		const int TextMaxWidth = CARD_MAX_WIDTH - CARD_PADDING_X * 2;
		Layout.m_vMessageLines = WrapChatExportText(pTextRender, ChatExportDisplayText(Line), FONT_SIZE_MESSAGE, TextMaxWidth);

		float MaxTextWidth = 0.0f;
		for(const std::string &MessageLine : Layout.m_vMessageLines)
			MaxTextWidth = maximum(MaxTextWidth, pTextRender->TextWidth(FONT_SIZE_MESSAGE, MessageLine.c_str()));
		Layout.m_CardWidth = std::clamp(round_to_int(MaxTextWidth) + CARD_PADDING_X * 2, CARD_MIN_WIDTH, CARD_MAX_WIDTH);
		Layout.m_CardHeight = CARD_PADDING_Y * 2 + (int)Layout.m_vMessageLines.size() * MESSAGE_LINE_HEIGHT;
		Layout.m_RecordHeight = Layout.m_CardHeight + (Line.m_Time.empty() ? 0 : TIME_TOP_MARGIN + TIME_LINE_HEIGHT) + RECORD_GAP;
		vLayouts.push_back(std::move(Layout));
	}
	return vLayouts;
}

static std::vector<SChatExportPage> BuildChatExportPages(const std::vector<SChatExportLayout> &vLayouts)
{
	static constexpr int TOP_MARGIN = 36;
	static constexpr int BOTTOM_MARGIN = 44;
	static constexpr int MIN_IMAGE_HEIGHT = 360;
	static constexpr int MAX_IMAGE_HEIGHT = 12000;

	std::vector<SChatExportPage> vPages;
	int PageStart = 0;
	int PageHeight = TOP_MARGIN;
	for(int LayoutIndex = 0; LayoutIndex < (int)vLayouts.size(); ++LayoutIndex)
	{
		if(LayoutIndex > PageStart && PageHeight + vLayouts[LayoutIndex].m_RecordHeight + BOTTOM_MARGIN > MAX_IMAGE_HEIGHT)
		{
			vPages.push_back({PageStart, LayoutIndex, std::clamp(PageHeight + BOTTOM_MARGIN, MIN_IMAGE_HEIGHT, MAX_IMAGE_HEIGHT)});
			PageStart = LayoutIndex;
			PageHeight = TOP_MARGIN;
		}
		PageHeight += vLayouts[LayoutIndex].m_RecordHeight;
	}
	if(PageStart < (int)vLayouts.size())
		vPages.push_back({PageStart, (int)vLayouts.size(), std::clamp(PageHeight + BOTTOM_MARGIN, MIN_IMAGE_HEIGHT, MAX_IMAGE_HEIGHT)});
	return vPages;
}

static bool AllocateImage(CImageInfo &Image, int Width, int Height, ColorRGBA FillColor)
{
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = static_cast<uint8_t *>(malloc(Image.DataSize()));
	if(!Image.m_pData)
	{
		Image.Free();
		return false;
	}
	mem_zero(Image.m_pData, Image.DataSize());
	FillRect(Image, 0, 0, Width, Height, FillColor);
	return true;
}

static bool AllocateClearImage(CImageInfo &Image, int Width, int Height)
{
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = static_cast<uint8_t *>(calloc(Image.DataSize(), sizeof(uint8_t)));
	if(!Image.m_pData)
	{
		Image.Free();
		return false;
	}
	return true;
}

static bool SaveChatExportPngPage(IStorage *pStorage, ITextRender *pTextRender, const char *pFilename, const std::vector<SChatExportLayout> &vLayouts, const SChatExportPage &Page, int, int, int)
{
	static constexpr int IMAGE_WIDTH = 1080;
	static constexpr int TOP_MARGIN = 36;
	static constexpr int CARD_CENTER_X = 720;
	static constexpr int CARD_PADDING_X = 46;
	static constexpr int CARD_PADDING_Y = 26;
	static constexpr int MESSAGE_LINE_HEIGHT = 72;
	static constexpr int TIME_TOP_MARGIN = 18;
	static constexpr int CIRCLE_OFFSET_X = 92;
	static constexpr int FONT_SIZE_MESSAGE = 58;
	static constexpr int FONT_SIZE_TIME = 34;
	static constexpr int FONT_SIZE_PLUS = 34;

	CImageInfo Image;
	CImageInfo Mask;
	if(!AllocateImage(Image, IMAGE_WIDTH, Page.m_Height, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f)) || !AllocateClearImage(Mask, IMAGE_WIDTH, Page.m_Height))
	{
		Image.Free();
		Mask.Free();
		log_error("console", "Failed to allocate chat export image");
		return false;
	}

	int PosY = TOP_MARGIN;
	for(int LayoutIndex = Page.m_Start; LayoutIndex < Page.m_End; ++LayoutIndex)
	{
		const SChatExportLayout &Layout = vLayouts[LayoutIndex];
		const int CardX = CARD_CENTER_X - Layout.m_CardWidth / 2;
		const int CardY = PosY;
		const int CardCenterY = CardY + Layout.m_CardHeight / 2;
		const int CircleCenterX = CardX - CIRCLE_OFFSET_X;

		for(int Glow = 18; Glow >= 4; Glow -= 4)
		{
			const float Alpha = 0.035f + (18 - Glow) * 0.003f;
			FillRoundedRect(Image, CardX - Glow, CardY - Glow, Layout.m_CardWidth + Glow * 2, Layout.m_CardHeight + Glow * 2, 24 + Glow, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
		}
		FillRoundedRect(Image, CardX, CardY, Layout.m_CardWidth, Layout.m_CardHeight, 20, ColorRGBA(1.0f, 1.0f, 1.0f, 0.98f));

		DrawCircleOutline(Image, CircleCenterX, CardCenterY, 34, 4, ColorRGBA(0.0f, 0.55f, 1.0f, 1.0f));
		DrawPngTextCentered(pTextRender, Image, Mask, CircleCenterX, CardCenterY - FONT_SIZE_PLUS / 2 - 4, 72, FONT_SIZE_PLUS, "+1", ColorRGBA(0.0f, 0.60f, 1.0f, 1.0f));

		int TextY = CardY + CARD_PADDING_Y;
		for(const std::string &MessageLine : Layout.m_vMessageLines)
		{
			DrawPngTextCentered(pTextRender, Image, Mask, CardX + Layout.m_CardWidth / 2, TextY, Layout.m_CardWidth - CARD_PADDING_X * 2, FONT_SIZE_MESSAGE, MessageLine, ColorRGBA(0.30f, 0.30f, 0.30f, 1.0f));
			TextY += MESSAGE_LINE_HEIGHT;
		}
		if(!Layout.m_Line.m_Time.empty())
			DrawPngTextCentered(pTextRender, Image, Mask, CardX + Layout.m_CardWidth / 2, CardY + Layout.m_CardHeight + TIME_TOP_MARGIN, Layout.m_CardWidth + 180, FONT_SIZE_TIME, Layout.m_Line.m_Time, ColorRGBA(0.94f, 0.94f, 0.94f, 1.0f));

		PosY += Layout.m_RecordHeight;
	}

	char aWholePath[IO_MAX_PATH_LENGTH];
	const bool Success = CImageLoader::SavePng(pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE, aWholePath, sizeof(aWholePath)), pFilename, Image);
	if(Success)
		log_info("console", "Saved chat export image to '%s'", aWholePath);
	Image.Free();
	Mask.Free();
	return Success;
}

static bool SaveChatExportPng(IStorage *pStorage, ITextRender *pTextRender, const char *pBaseFilename, const std::vector<SChatExportLine> &vLines)
{
	const std::vector<SChatExportLayout> vLayouts = BuildChatExportLayouts(pTextRender, vLines);
	const std::vector<SChatExportPage> vPages = BuildChatExportPages(vLayouts);
	bool Success = true;
	for(int PageIndex = 0; PageIndex < (int)vPages.size(); ++PageIndex)
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		if(vPages.size() == 1)
			str_format(aFilename, sizeof(aFilename), "%s.png", pBaseFilename);
		else
			str_format(aFilename, sizeof(aFilename), "%s_%03d.png", pBaseFilename, PageIndex + 1);
		Success &= SaveChatExportPngPage(pStorage, pTextRender, aFilename, vLayouts, vPages[PageIndex], PageIndex, (int)vPages.size(), (int)vLines.size());
	}
	return Success;
}

class CConsoleLogger : public ILogger
{
	CGameConsole *m_pConsole;
	CLock m_ConsoleMutex;

public:
	CConsoleLogger(CGameConsole *pConsole) :
		m_pConsole(pConsole)
	{
		dbg_assert(pConsole != nullptr, "console pointer must not be null");
	}

	void Log(const CLogMessage *pMessage) override REQUIRES(!m_ConsoleMutex);
	void OnConsoleDeletion() REQUIRES(!m_ConsoleMutex);
};

void CConsoleLogger::Log(const CLogMessage *pMessage)
{
	if(m_Filter.Filters(pMessage))
	{
		return;
	}
	ColorRGBA Color = gs_ConsoleDefaultColor;
	if(pMessage->m_HaveColor)
	{
		Color.r = pMessage->m_Color.r / 255.0;
		Color.g = pMessage->m_Color.g / 255.0;
		Color.b = pMessage->m_Color.b / 255.0;
	}
	const CLockScope LockScope(m_ConsoleMutex);
	if(m_pConsole)
	{
		m_pConsole->m_LocalConsole.PrintLine(pMessage->m_aLine, pMessage->m_LineLength, Color);
	}
}

void CConsoleLogger::OnConsoleDeletion()
{
	const CLockScope LockScope(m_ConsoleMutex);
	m_pConsole = nullptr;
}

enum class EArgumentCompletionType
{
	NONE,
	MAP,
	TUNE,
	SETTING,
	KEY,
};

class CArgumentCompletionEntry
{
public:
	EArgumentCompletionType m_Type;
	const char *m_pCommandName;
	int m_ArgumentIndex;
};

static const CArgumentCompletionEntry gs_aArgumentCompletionEntries[] = {
	{EArgumentCompletionType::MAP, "sv_map", 0},
	{EArgumentCompletionType::MAP, "change_map", 0},
	{EArgumentCompletionType::TUNE, "tune", 0},
	{EArgumentCompletionType::TUNE, "tune_reset", 0},
	{EArgumentCompletionType::TUNE, "toggle_tune", 0},
	{EArgumentCompletionType::TUNE, "tune_zone", 1},
	{EArgumentCompletionType::SETTING, "reset", 0},
	{EArgumentCompletionType::SETTING, "toggle", 0},
	{EArgumentCompletionType::SETTING, "access_level", 0},
	{EArgumentCompletionType::SETTING, "+toggle", 0},
	{EArgumentCompletionType::KEY, "bind", 0},
	{EArgumentCompletionType::KEY, "binds", 0},
	{EArgumentCompletionType::KEY, "unbind", 0},
};

static std::pair<EArgumentCompletionType, int> ArgumentCompletion(const char *pStr)
{
	const char *pCommandStart = pStr;
	const char *pIt = pStr;
	pIt = str_skip_to_whitespace_const(pIt);
	int CommandLength = pIt - pCommandStart;
	const char *pCommandEnd = pIt;

	if(!CommandLength)
		return {EArgumentCompletionType::NONE, -1};

	pIt = str_skip_whitespaces_const(pIt);
	if(pIt == pCommandEnd)
		return {EArgumentCompletionType::NONE, -1};

	for(const auto &Entry : gs_aArgumentCompletionEntries)
	{
		int Length = maximum(str_length(Entry.m_pCommandName), CommandLength);
		if(str_comp_nocase_num(Entry.m_pCommandName, pCommandStart, Length) == 0)
		{
			int CurrentArg = 0;
			const char *pArgStart = nullptr, *pArgEnd = nullptr;
			while(CurrentArg < Entry.m_ArgumentIndex)
			{
				pArgStart = pIt;
				pIt = str_skip_to_whitespace_const(pIt); // Skip argument value
				pArgEnd = pIt;

				if(!pIt[0] || pArgStart == pIt) // Check that argument is not empty
					return {EArgumentCompletionType::NONE, -1};

				pIt = str_skip_whitespaces_const(pIt); // Go to next argument position
				CurrentArg++;
			}
			if(pIt == pArgEnd)
				return {EArgumentCompletionType::NONE, -1}; // Check that there is at least one space after
			return {Entry.m_Type, pIt - pStr};
		}
	}
	return {EArgumentCompletionType::NONE, -1};
}

static int PossibleTunings(const char *pStr, IConsole::FPossibleCallback pfnCallback = IConsole::EmptyPossibleCommandCallback, void *pUser = nullptr)
{
	int Index = 0;
	for(int i = 0; i < CTuningParams::Num(); i++)
	{
		if(str_find_nocase(CTuningParams::Name(i), pStr))
		{
			pfnCallback(Index, CTuningParams::Name(i), pUser);
			Index++;
		}
	}
	return Index;
}

static int PossibleKeys(const char *pStr, IInput *pInput, IConsole::FPossibleCallback pfnCallback = IConsole::EmptyPossibleCommandCallback, void *pUser = nullptr)
{
	int Index = 0;
	for(int Key = KEY_A; Key < KEY_JOY_AXIS_11_RIGHT; Key++)
	{
		// Ignore unnamed keys starting with '&'
		const char *pKeyName = pInput->KeyName(Key);
		if(pKeyName[0] != '&' && str_find_nocase(pKeyName, pStr))
		{
			pfnCallback(Index, pKeyName, pUser);
			Index++;
		}
	}
	return Index;
}

static void CollectPossibleCommandsCallback(int Index, const char *pStr, void *pUser)
{
	((std::vector<const char *> *)pUser)->push_back(pStr);
}

static void SortCompletions(std::vector<const char *> &vCompletions, const char *pSearch)
{
	if(pSearch[0] == '\0')
		return;

	std::sort(vCompletions.begin(), vCompletions.end(), [pSearch](const char *pA, const char *pB) {
		const char *pMatchA = str_find_nocase(pA, pSearch);
		const char *pMatchB = str_find_nocase(pB, pSearch);
		int MatchPosA = pMatchA ? (pMatchA - pA) : -1;
		int MatchPosB = pMatchB ? (pMatchB - pB) : -1;

		if(MatchPosA != MatchPosB)
			return MatchPosA < MatchPosB;

		int LenA = str_length(pA);
		int LenB = str_length(pB);
		if(LenA != LenB)
			return LenA < LenB;

		return str_comp_nocase(pA, pB) < 0;
	});
}

CGameConsole::CInstance::CInstance(int Type)
{
	m_pHistoryEntry = nullptr;

	m_Type = Type;

	if(Type == CGameConsole::CONSOLETYPE_LOCAL)
	{
		m_pName = "local_console";
		m_CompletionFlagmask = CFGFLAG_CLIENT;
	}
	else
	{
		m_pName = "remote_console";
		m_CompletionFlagmask = CFGFLAG_SERVER;
	}

	m_aCompletionBuffer[0] = 0;
	m_CompletionChosen = -1;
	m_aCompletionBufferArgument[0] = 0;
	m_CompletionChosenArgument = -1;
	m_CompletionArgumentPosition = 0;
	m_CompletionDirty = true;
	m_QueueResetAnimation = false;
	Reset();

	m_aUser[0] = '\0';
	m_UserGot = false;
	m_UsernameReq = false;

	m_IsCommand = false;

	m_Backlog.SetPopCallback([this](CBacklogEntry *pEntry) {
		if(pEntry->m_LineCount != -1 && MatchesLogFilter(pEntry))
		{
			m_NewLineCounter -= pEntry->m_LineCount;
			for(auto &SearchMatch : m_vSearchMatches)
			{
				SearchMatch.m_StartLine += pEntry->m_LineCount;
				SearchMatch.m_EndLine += pEntry->m_LineCount;
				SearchMatch.m_EntryLine += pEntry->m_LineCount;
			}
		}
		if(pEntry->m_ExportId == m_ChatExportAnchorId)
			m_ChatExportAnchorId = -1;
	});

	m_Input.SetClipboardLineCallback([this](const char *pStr) { ExecuteLine(pStr); });

	m_CurrentMatchIndex = -1;
	m_aCurrentSearchString[0] = '\0';
}

void CGameConsole::CInstance::Init(CGameConsole *pGameConsole)
{
	m_pGameConsole = pGameConsole;
}

void CGameConsole::CInstance::ClearBacklog()
{
	{
		// We must ensure that no log messages are printed while owning
		// m_BacklogPendingLock or this will result in a dead lock.
		const CLockScope LockScope(m_BacklogPendingLock);
		m_BacklogPending.Init();
		m_NextExportId = 1;
	}

	m_Backlog.Init();
	m_BacklogCurLine = 0;
	m_ChatExportAnchorId = -1;
	m_ChatExportMode = false;
	ClearSearch();
}

void CGameConsole::CInstance::UpdateBacklogTextAttributes()
{
	// Pending backlog entries are not handled because they don't have text attributes yet.
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		UpdateEntryTextAttributes(pEntry);
	}
}

void CGameConsole::CInstance::PumpBacklogPending()
{
	{
		// We must ensure that no log messages are printed while owning
		// m_BacklogPendingLock or this will result in a dead lock.
		const CLockScope LockScopePending(m_BacklogPendingLock);
		for(CBacklogEntry *pPendingEntry = m_BacklogPending.First(); pPendingEntry; pPendingEntry = m_BacklogPending.Next(pPendingEntry))
		{
			const size_t EntrySize = sizeof(CBacklogEntry) + pPendingEntry->m_Length;
			CBacklogEntry *pEntry = m_Backlog.Allocate(EntrySize);
			mem_copy(pEntry, pPendingEntry, EntrySize);
		}

		m_BacklogPending.Init();
	}

	// Update text attributes and count number of added lines
	m_pGameConsole->Ui()->MapScreen();
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(pEntry->m_LineCount == -1)
		{
			UpdateEntryTextAttributes(pEntry);
			if(MatchesLogFilter(pEntry))
				m_NewLineCounter += pEntry->m_LineCount;
		}
	}
}

void CGameConsole::CInstance::ClearHistory()
{
	m_History.Init();
	m_pHistoryEntry = nullptr;
}

void CGameConsole::CInstance::Reset()
{
	m_CompletionRenderOffset = 0.0f;
	m_CompletionRenderOffsetChange = 0.0f;
	m_pCommandName = "";
	m_pCommandHelp = "";
	m_pCommandParams = "";
	m_CompletionArgumentPosition = 0;
	m_CompletionDirty = true;
}

void CGameConsole::ForceUpdateRemoteCompletionSuggestions()
{
	m_RemoteConsole.m_CompletionDirty = true;
	m_RemoteConsole.UpdateCompletionSuggestions();
}

void CGameConsole::CInstance::UpdateCompletionSuggestions()
{
	if(!m_CompletionDirty)
		return;

	// Store old selection
	char aOldCommand[IConsole::CMDLINE_LENGTH];
	aOldCommand[0] = '\0';
	if(m_CompletionChosen != -1 && (size_t)m_CompletionChosen < m_vpCommandSuggestions.size())
		str_copy(aOldCommand, m_vpCommandSuggestions[m_CompletionChosen], sizeof(aOldCommand));

	char aOldArgument[IConsole::CMDLINE_LENGTH];
	aOldArgument[0] = '\0';
	if(m_CompletionChosenArgument != -1 && (size_t)m_CompletionChosenArgument < m_vpArgumentSuggestions.size())
		str_copy(aOldArgument, m_vpArgumentSuggestions[m_CompletionChosenArgument], sizeof(aOldArgument));

	m_vpCommandSuggestions.clear();
	m_vpArgumentSuggestions.clear();

	// Command completion
	char aSearch[IConsole::CMDLINE_LENGTH];
	GetCommand(m_aCompletionBuffer, aSearch);
	const bool RemoteConsoleCompletion = m_Type == CGameConsole::CONSOLETYPE_REMOTE && m_pGameConsole->Client()->RconAuthed();
	const bool UseTempCommands = RemoteConsoleCompletion && m_pGameConsole->Client()->UseTempRconCommands();
	m_pGameConsole->m_pConsole->PossibleCommands(aSearch, m_CompletionFlagmask, UseTempCommands, CollectPossibleCommandsCallback, &m_vpCommandSuggestions);
	SortCompletions(m_vpCommandSuggestions, aSearch);

	// Argument completion
	const auto [CompletionType, CompletionPos] = ArgumentCompletion(GetString());
	if(CompletionType != EArgumentCompletionType::NONE)
	{
		if(CompletionType == EArgumentCompletionType::MAP)
			m_pGameConsole->PossibleMaps(m_aCompletionBufferArgument, CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		else if(CompletionType == EArgumentCompletionType::TUNE)
			PossibleTunings(m_aCompletionBufferArgument, CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		else if(CompletionType == EArgumentCompletionType::SETTING)
			m_pGameConsole->m_pConsole->PossibleCommands(m_aCompletionBufferArgument, m_CompletionFlagmask, UseTempCommands, CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		else if(CompletionType == EArgumentCompletionType::KEY)
			PossibleKeys(m_aCompletionBufferArgument, m_pGameConsole->Input(), CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		SortCompletions(m_vpArgumentSuggestions, m_aCompletionBufferArgument);
	}

	// Restore old selection if it changed
	if(m_CompletionChosen != -1 && (size_t)m_CompletionChosen < m_vpCommandSuggestions.size() &&
		aOldCommand[0] != '\0' && str_comp(m_vpCommandSuggestions[m_CompletionChosen], aOldCommand) != 0)
	{
		for(size_t SuggestedId = 0; SuggestedId < m_vpCommandSuggestions.size(); SuggestedId++)
		{
			if(str_comp(m_vpCommandSuggestions[SuggestedId], aOldCommand) == 0)
			{
				m_CompletionChosen = SuggestedId;
				m_QueueResetAnimation = true;
				break;
			}
		}
	}
	if(m_CompletionChosenArgument != -1 && (size_t)m_CompletionChosenArgument < m_vpArgumentSuggestions.size() &&
		aOldArgument[0] != '\0' && str_comp(m_vpArgumentSuggestions[m_CompletionChosenArgument], aOldArgument) != 0)
	{
		for(size_t SuggestedId = 0; SuggestedId < m_vpArgumentSuggestions.size(); SuggestedId++)
		{
			if(str_comp(m_vpArgumentSuggestions[SuggestedId], aOldArgument) == 0)
			{
				m_CompletionChosenArgument = SuggestedId;
				m_QueueResetAnimation = true;
				break;
			}
		}
	}

	m_CompletionDirty = false;
}

void CGameConsole::CInstance::ExecuteLine(const char *pLine)
{
	if(m_Type == CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
	{
		const char *pPrevEntry = m_History.Last();
		if(pPrevEntry == nullptr || str_comp(pPrevEntry, pLine) != 0)
		{
			const size_t Size = str_length(pLine) + 1;
			char *pEntry = m_History.Allocate(Size);
			str_copy(pEntry, pLine, Size);
		}
		// print out the user's commands before they get run
		char aBuf[IConsole::CMDLINE_LENGTH + 3];
		str_format(aBuf, sizeof(aBuf), "> %s", pLine);
		m_pGameConsole->PrintLine(m_Type, aBuf);
	}

	if(m_Type == CGameConsole::CONSOLETYPE_LOCAL)
	{
		m_pGameConsole->m_pConsole->ExecuteLine(pLine);
	}
	else
	{
		if(m_pGameConsole->Client()->RconAuthed())
		{
			m_pGameConsole->Client()->Rcon(pLine);
		}
		else
		{
			if(!m_UserGot && m_UsernameReq)
			{
				m_UserGot = true;
				str_copy(m_aUser, pLine);
			}
			else
			{
				m_pGameConsole->Client()->RconAuth(m_aUser, pLine, g_Config.m_ClDummy);
				m_UserGot = false;
			}
		}
	}
}

void CGameConsole::CInstance::PossibleCommandsCompleteCallback(int Index, const char *pStr, void *pUser)
{
	CGameConsole::CInstance *pInstance = (CGameConsole::CInstance *)pUser;
	if(pInstance->m_CompletionChosen == Index)
	{
		char aBefore[IConsole::CMDLINE_LENGTH];
		str_truncate(aBefore, sizeof(aBefore), pInstance->m_aCompletionBuffer, pInstance->m_CompletionCommandStart);
		char aBuf[IConsole::CMDLINE_LENGTH];
		str_format(aBuf, sizeof(aBuf), "%s%s%s", aBefore, pStr, pInstance->m_aCompletionBuffer + pInstance->m_CompletionCommandEnd);
		pInstance->m_Input.Set(aBuf);
		pInstance->m_Input.SetCursorOffset(str_length(pStr) + pInstance->m_CompletionCommandStart);
	}
}

void CGameConsole::CInstance::GetCommand(const char *pInput, char (&aCmd)[IConsole::CMDLINE_LENGTH])
{
	char aInput[IConsole::CMDLINE_LENGTH];
	str_copy(aInput, pInput);
	m_CompletionCommandStart = 0;
	m_CompletionCommandEnd = 0;

	char aaSeparators[][2] = {";", "\""};
	for(auto *pSeparator : aaSeparators)
	{
		int Start, End;
		str_delimiters_around_offset(aInput + m_CompletionCommandStart, pSeparator, m_Input.GetCursorOffset() - m_CompletionCommandStart, &Start, &End);
		m_CompletionCommandStart += Start;
		m_CompletionCommandEnd = m_CompletionCommandStart + (End - Start);
		aInput[m_CompletionCommandEnd] = '\0';
	}
	m_CompletionCommandStart = str_skip_whitespaces_const(aInput + m_CompletionCommandStart) - aInput;

	str_copy(aCmd, aInput + m_CompletionCommandStart, sizeof(aCmd));
}

static void StrCopyUntilSpace(char *pDest, size_t DestSize, const char *pSrc)
{
	const char *pSpace = str_find(pSrc, " ");
	str_copy(pDest, pSrc, minimum<size_t>(pSpace ? pSpace - pSrc + 1 : 1, DestSize));
}

void CGameConsole::CInstance::PossibleArgumentsCompleteCallback(int Index, const char *pStr, void *pUser)
{
	CGameConsole::CInstance *pInstance = (CGameConsole::CInstance *)pUser;
	if(pInstance->m_CompletionChosenArgument == Index)
	{
		// get command
		char aBuf[IConsole::CMDLINE_LENGTH];
		str_copy(aBuf, pInstance->GetString(), pInstance->m_CompletionArgumentPosition);
		str_append(aBuf, " ");

		// append argument
		str_append(aBuf, pStr);
		pInstance->m_Input.Set(aBuf);
	}
}

bool CGameConsole::CInstance::OnInput(const IInput::CEvent &Event)
{
	bool Handled = false;

	// Don't allow input while the console is opening/closing
	if(m_pGameConsole->m_ConsoleState == CONSOLE_OPENING || m_pGameConsole->m_ConsoleState == CONSOLE_CLOSING)
		return Handled;

	auto &&SelectNextSearchMatch = [&](int Direction) {
		if(!m_vSearchMatches.empty())
		{
			m_CurrentMatchIndex += Direction;
			if(m_CurrentMatchIndex >= (int)m_vSearchMatches.size())
				m_CurrentMatchIndex = 0;
			if(m_CurrentMatchIndex < 0)
				m_CurrentMatchIndex = (int)m_vSearchMatches.size() - 1;
			m_HasSelection = false;
			// Also scroll to the correct line
			ScrollToCenter(m_vSearchMatches[m_CurrentMatchIndex].m_StartLine, m_vSearchMatches[m_CurrentMatchIndex].m_EndLine);
		}
	};

	const int BacklogPrevLine = m_BacklogCurLine;
	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER)
		{
			if(!m_Searching)
			{
				if(!m_Input.IsEmpty() || (m_UsernameReq && !m_pGameConsole->Client()->RconAuthed() && !m_UserGot))
				{
					ExecuteLine(m_Input.GetString());
					m_Input.Clear();
					m_pHistoryEntry = nullptr;
				}
			}
			else
			{
				SelectNextSearchMatch(m_pGameConsole->GameClient()->Input()->ShiftIsPressed() ? -1 : 1);
			}

			Handled = true;
		}
		else if(Event.m_Key == KEY_UP)
		{
			if(m_Searching)
			{
				SelectNextSearchMatch(-1);
			}
			else if(m_Type == CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
			{
				if(m_pHistoryEntry)
				{
					char *pTest = m_History.Prev(m_pHistoryEntry);

					if(pTest)
						m_pHistoryEntry = pTest;
				}
				else
					m_pHistoryEntry = m_History.Last();

				if(m_pHistoryEntry)
					m_Input.Set(m_pHistoryEntry);
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_DOWN)
		{
			if(m_Searching)
			{
				SelectNextSearchMatch(1);
			}
			else if(m_Type == CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
			{
				if(m_pHistoryEntry)
					m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

				if(m_pHistoryEntry)
					m_Input.Set(m_pHistoryEntry);
				else
					m_Input.Clear();
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_TAB)
		{
			const int Direction = m_pGameConsole->GameClient()->Input()->ShiftIsPressed() ? -1 : 1;

			if(!m_Searching)
			{
				UpdateCompletionSuggestions();

				// Command completion
				int CompletionEnumerationCount = m_vpCommandSuggestions.size();

				if(m_Type == CGameConsole::CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
				{
					if(CompletionEnumerationCount)
					{
						if(m_CompletionChosen == -1 && Direction < 0)
							m_CompletionChosen = 0;
						m_CompletionChosen = (m_CompletionChosen + Direction + CompletionEnumerationCount) % CompletionEnumerationCount;
						m_CompletionArgumentPosition = 0;

						PossibleCommandsCompleteCallback(m_CompletionChosen, m_vpCommandSuggestions[m_CompletionChosen], this);
					}
					else if(m_CompletionChosen != -1)
					{
						m_CompletionChosen = -1;
						Reset();
					}
				}

				// Argument completion
				const auto [CompletionType, CompletionPos] = ArgumentCompletion(GetString());
				int CompletionEnumerationCountArgs = m_vpArgumentSuggestions.size();
				if(CompletionEnumerationCountArgs)
				{
					if(m_CompletionChosenArgument == -1 && Direction < 0)
						m_CompletionChosenArgument = 0;
					m_CompletionChosenArgument = (m_CompletionChosenArgument + Direction + CompletionEnumerationCountArgs) % CompletionEnumerationCountArgs;
					m_CompletionArgumentPosition = CompletionPos;

					PossibleArgumentsCompleteCallback(m_CompletionChosenArgument, m_vpArgumentSuggestions[m_CompletionChosenArgument], this);
				}
				else if(m_CompletionChosenArgument != -1)
				{
					m_CompletionChosenArgument = -1;
					Reset();
				}
			}
			else
			{
				// Use Tab / Shift-Tab to cycle through search matches
				SelectNextSearchMatch(Direction);
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_PAGEUP)
		{
			m_BacklogCurLine += GetLinesToScroll(-1, m_LinesRendered);
			Handled = true;
		}
		else if(Event.m_Key == KEY_PAGEDOWN)
		{
			m_BacklogCurLine -= GetLinesToScroll(1, m_LinesRendered);
			if(m_BacklogCurLine < 0)
			{
				m_BacklogCurLine = 0;
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			m_BacklogCurLine += GetLinesToScroll(-1, 1);
			Handled = true;
		}
		else if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			--m_BacklogCurLine;
			if(m_BacklogCurLine < 0)
			{
				m_BacklogCurLine = 0;
			}
			Handled = true;
		}
		// in order not to conflict with CLineInput's handling of Home/End only
		// react to it when the input is empty
		else if(Event.m_Key == KEY_HOME && m_Input.IsEmpty())
		{
			m_BacklogCurLine += GetLinesToScroll(-1, -1);
			m_BacklogLastActiveLine = m_BacklogCurLine;
			Handled = true;
		}
		else if(Event.m_Key == KEY_END && m_Input.IsEmpty())
		{
			m_BacklogCurLine = 0;
			Handled = true;
		}
		else if(Event.m_Key == KEY_ESCAPE && m_Searching)
		{
			SetSearching(false);
			Handled = true;
		}
		else if(Event.m_Key == KEY_F && m_pGameConsole->Input()->ModifierIsPressed())
		{
			SetSearching(true);
			Handled = true;
		}
	}

	if(m_BacklogCurLine != BacklogPrevLine)
	{
		m_HasSelection = false;
	}

	if(!Handled)
	{
		Handled = m_Input.ProcessInput(Event);
		if(Handled)
			UpdateSearch();
	}

	if(Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_TEXT))
	{
		if(Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT)
		{
			const char *pInputStr = m_Input.GetString();

			m_CompletionChosen = -1;
			str_copy(m_aCompletionBuffer, pInputStr);

			const auto [CompletionType, CompletionPos] = ArgumentCompletion(GetString());
			if(CompletionType != EArgumentCompletionType::NONE)
			{
				for(const auto &Entry : gs_aArgumentCompletionEntries)
				{
					if(Entry.m_Type != CompletionType)
						continue;
					const int Len = str_length(Entry.m_pCommandName);
					if(str_comp_nocase_num(pInputStr, Entry.m_pCommandName, Len) == 0 && str_isspace(pInputStr[Len]))
					{
						m_CompletionChosenArgument = -1;
						str_copy(m_aCompletionBufferArgument, &pInputStr[CompletionPos]);
					}
				}
			}

			Reset();
		}

		// find the current command
		{
			char aCmd[IConsole::CMDLINE_LENGTH];
			GetCommand(GetString(), aCmd);
			char aBuf[IConsole::CMDLINE_LENGTH];
			StrCopyUntilSpace(aBuf, sizeof(aBuf), aCmd);

			const IConsole::ICommandInfo *pCommand = m_pGameConsole->m_pConsole->GetCommandInfo(aBuf, m_CompletionFlagmask,
				m_Type != CGameConsole::CONSOLETYPE_LOCAL && m_pGameConsole->Client()->RconAuthed() && m_pGameConsole->Client()->UseTempRconCommands());
			if(pCommand)
			{
				m_IsCommand = true;
				m_pCommandName = pCommand->Name();
				m_pCommandHelp = pCommand->Help();
				m_pCommandParams = pCommand->Params();
			}
			else
				m_IsCommand = false;
		}
	}

	return Handled;
}

void CGameConsole::CInstance::PrintLine(const char *pLine, int Len, ColorRGBA PrintColor)
{
	// We must ensure that no log messages are printed while owning
	// m_BacklogPendingLock or this will result in a dead lock.
	const CLockScope LockScope(m_BacklogPendingLock);
	CBacklogEntry *pEntry = m_BacklogPending.Allocate(sizeof(CBacklogEntry) + Len);
	pEntry->m_YOffset = -1.0f;
	pEntry->m_PrintColor = PrintColor;
	pEntry->m_Length = Len;
	pEntry->m_LogCategory = ClassifyLogCategory(pLine, (size_t)Len);
	pEntry->m_ExportId = m_NextExportId++;
	pEntry->m_ExportSelected = false;
	pEntry->m_LineCount = -1;
	str_copy(pEntry->m_aText, pLine, Len + 1);
}

CGameConsole::CInstance::ELogCategory CGameConsole::CInstance::ClassifyLogCategory(const char *pLine, size_t Length)
{
	if(!pLine || Length == 0)
		return ELogCategory::SYSTEM;

	const char *pSearchEnd = pLine + Length;
	const char *pSystemStart = pLine;
	if(const char *pColon = str_find(pLine, ": "))
	{
		pSearchEnd = pColon;
		const char *pIt = pColon;
		while(pIt > pLine && pIt[-1] != ' ')
			--pIt;
		pSystemStart = pIt;
	}

	const char *pChat = str_find_nocase(pSystemStart, "chat/");
	if(pChat && pChat < pSearchEnd)
	{
		if(str_startswith_nocase(pChat, "chat/all") || str_startswith_nocase(pChat, "chat/team") || str_startswith_nocase(pChat, "chat/whisper"))
			return ELogCategory::PLAYER;
	}
	return ELogCategory::SYSTEM;
}

bool CGameConsole::CInstance::MatchesLogFilter(const CBacklogEntry *pEntry) const
{
	if(m_LogFilter == ELogFilter::ALL)
		return true;
	const ELogCategory Category = ClassifyLogCategory(pEntry->m_aText, pEntry->m_Length);
	if(Category == ELogCategory::PLAYER)
		return m_LogFilter == ELogFilter::PLAYER;
	return m_LogFilter == ELogFilter::SYSTEM;
}

void CGameConsole::CInstance::SetLogFilter(ELogFilter Filter)
{
	if(m_LogFilter == Filter)
		return;
	m_LogFilter = Filter;
	m_BacklogCurLine = 0;
	m_BacklogLastActiveLine = -1;
	m_NewLineCounter = 0;
	m_HasSelection = false;
	m_CurSelStart = 0;
	m_CurSelEnd = 0;
	m_MouseIsPress = false;
	if(m_Searching)
		UpdateSearch();
}

bool CGameConsole::CInstance::IsChatExportableEntry(const CBacklogEntry *pEntry) const
{
	return pEntry && pEntry->m_LogCategory == ELogCategory::PLAYER;
}

void CGameConsole::CInstance::ClearChatExportSelection()
{
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
		pEntry->m_ExportSelected = false;
	m_ChatExportAnchorId = -1;
}

void CGameConsole::CInstance::SelectAllChatExportable() REQUIRES(!m_BacklogPendingLock)
{
	PumpBacklogPending();
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(IsChatExportableEntry(pEntry))
			pEntry->m_ExportSelected = true;
	}
}

int CGameConsole::CInstance::SelectedChatExportCount()
{
	int Count = 0;
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(IsChatExportableEntry(pEntry) && pEntry->m_ExportSelected)
			++Count;
	}
	return Count;
}

void CGameConsole::CInstance::ToggleChatExportEntry(CBacklogEntry *pEntry, bool RangeSelect)
{
	if(!IsChatExportableEntry(pEntry))
		return;

	const bool Select = !pEntry->m_ExportSelected;
	if(RangeSelect && m_ChatExportAnchorId >= 0)
	{
		const int RangeStart = minimum(m_ChatExportAnchorId, pEntry->m_ExportId);
		const int RangeEnd = maximum(m_ChatExportAnchorId, pEntry->m_ExportId);
		for(CBacklogEntry *pRangeEntry = m_Backlog.First(); pRangeEntry; pRangeEntry = m_Backlog.Next(pRangeEntry))
		{
			if(IsChatExportableEntry(pRangeEntry) && pRangeEntry->m_ExportId >= RangeStart && pRangeEntry->m_ExportId <= RangeEnd)
				pRangeEntry->m_ExportSelected = Select;
		}
	}
	else
	{
		pEntry->m_ExportSelected = Select;
	}
	m_ChatExportAnchorId = pEntry->m_ExportId;
	m_HasSelection = false;
	m_CurSelStart = 0;
	m_CurSelEnd = 0;
}

void CGameConsole::CInstance::SetChatExportMode(bool Enable)
{
	if(m_ChatExportMode == Enable)
		return;

	if(Enable)
	{
		PumpBacklogPending();
		if(m_Searching)
			SetSearching(false);
		ClearChatExportSelection();
		m_ChatExportPreviousFilter = m_LogFilter;
		SetLogFilter(ELogFilter::PLAYER);
		m_ChatExportMode = true;
		m_HasSelection = false;
		m_MouseIsPress = false;
	}
	else
	{
		m_ChatExportMode = false;
		ClearChatExportSelection();
		SetLogFilter(m_ChatExportPreviousFilter);
		m_HasSelection = false;
		m_MouseIsPress = false;
	}
}

int CGameConsole::CInstance::GetLinesToScroll(int Direction, int LinesToScroll)
{
	auto *pEntry = m_Backlog.Last();
	int Line = 0;
	int LinesToSkip = (Direction == -1 ? m_BacklogCurLine + m_LinesRendered : m_BacklogCurLine - 1);
	while(Line < LinesToSkip && pEntry)
	{
		if(pEntry->m_LineCount == -1)
			UpdateEntryTextAttributes(pEntry);
		if(MatchesLogFilter(pEntry))
			Line += pEntry->m_LineCount;
		pEntry = m_Backlog.Prev(pEntry);
	}

	int Amount = maximum(0, Line - LinesToSkip);
	while(pEntry && (LinesToScroll > 0 ? Amount < LinesToScroll : true))
	{
		if(pEntry->m_LineCount == -1)
			UpdateEntryTextAttributes(pEntry);
		if(MatchesLogFilter(pEntry))
			Amount += pEntry->m_LineCount;
		pEntry = Direction == -1 ? m_Backlog.Prev(pEntry) : m_Backlog.Next(pEntry);
	}

	return LinesToScroll > 0 ? minimum(Amount, LinesToScroll) : Amount;
}

void CGameConsole::CInstance::ScrollToCenter(int StartLine, int EndLine)
{
	// This method is used to scroll lines from `StartLine` to `EndLine` to the center of the screen, if possible.

	// Find target line
	int Target = maximum(0, (int)ceil(StartLine - minimum(StartLine - EndLine, m_LinesRendered) / 2) - m_LinesRendered / 2);
	if(m_BacklogCurLine == Target)
		return;

	// Compute actual amount of lines to scroll to make sure lines fit in viewport and we don't have empty space
	int Direction = m_BacklogCurLine - Target < 0 ? -1 : 1;
	int LinesToScroll = absolute(Target - m_BacklogCurLine);
	int ComputedLines = GetLinesToScroll(Direction, LinesToScroll);

	if(Direction == -1)
		m_BacklogCurLine += ComputedLines;
	else
		m_BacklogCurLine -= ComputedLines;
}

void CGameConsole::CInstance::UpdateEntryTextAttributes(CBacklogEntry *pEntry) const
{
	CTextCursor Cursor;
	Cursor.m_FontSize = FONT_SIZE;
	Cursor.m_Flags = 0;
	Cursor.m_LineWidth = m_pGameConsole->Ui()->Screen()->w - 10;
	Cursor.m_MaxLines = 10;
	Cursor.m_LineSpacing = LINE_SPACING;
	m_pGameConsole->TextRender()->TextEx(&Cursor, pEntry->m_aText, -1);
	pEntry->m_YOffset = Cursor.Height();
	pEntry->m_LineCount = Cursor.m_LineCount;
}

bool CGameConsole::CInstance::IsInputHidden() const
{
	if(m_Type != CONSOLETYPE_REMOTE)
		return false;
	if(m_pGameConsole->Client()->State() != IClient::STATE_ONLINE || m_Searching)
		return false;
	if(m_pGameConsole->Client()->RconAuthed())
		return false;
	return m_UserGot || !m_UsernameReq;
}

void CGameConsole::CInstance::SetSearching(bool Searching)
{
	m_Searching = Searching;
	if(Searching)
	{
		m_Input.SetClipboardLineCallback(nullptr); // restore default behavior (replace newlines with spaces)
		m_Input.Set(m_aCurrentSearchString);
		m_Input.SelectAll();
		UpdateSearch();
	}
	else
	{
		m_Input.SetClipboardLineCallback([this](const char *pLine) { ExecuteLine(pLine); });
		m_Input.Clear();
	}
}

void CGameConsole::CInstance::ClearSearch()
{
	m_vSearchMatches.clear();
	m_CurrentMatchIndex = -1;
	m_Input.Clear();
	m_aCurrentSearchString[0] = '\0';
}

void CGameConsole::CInstance::UpdateSearch()
{
	if(!m_Searching)
		return;

	const char *pSearchText = m_Input.GetString();
	bool SearchChanged = str_utf8_comp_nocase(pSearchText, m_aCurrentSearchString) != 0;

	int SearchLength = m_Input.GetLength();
	str_copy(m_aCurrentSearchString, pSearchText);

	m_vSearchMatches.clear();
	if(pSearchText[0] == '\0')
	{
		m_CurrentMatchIndex = -1;
		return;
	}

	if(SearchChanged)
	{
		m_CurrentMatchIndex = -1;
		m_HasSelection = false;
	}

	ITextRender *pTextRender = m_pGameConsole->Ui()->TextRender();
	const int LineWidth = m_pGameConsole->Ui()->Screen()->w - 10.0f;

	CBacklogEntry *pEntry = m_Backlog.Last();
	int EntryLine = 0, LineToScrollStart = 0, LineToScrollEnd = 0;

	for(; pEntry; pEntry = m_Backlog.Prev(pEntry))
	{
		if(pEntry->m_LineCount == -1)
			UpdateEntryTextAttributes(pEntry);
		if(!MatchesLogFilter(pEntry))
			continue;

		const char *pSearchPos = str_utf8_find_nocase(pEntry->m_aText, pSearchText);
		if(!pSearchPos)
		{
			EntryLine += pEntry->m_LineCount;
			continue;
		}

		int EntryLineCount = pEntry->m_LineCount;

		// Find all occurrences of the search string and save their positions
		while(pSearchPos)
		{
			int Pos = pSearchPos - pEntry->m_aText;

			if(EntryLineCount == 1)
			{
				m_vSearchMatches.emplace_back(Pos, EntryLine, EntryLine, EntryLine);
				if(EntryLine > LineToScrollStart)
				{
					LineToScrollStart = EntryLine;
					LineToScrollEnd = EntryLine;
				}
			}
			else
			{
				// A match can span multiple lines in case of a multiline entry, so we need to know which line the match starts at
				// and which line it ends at in order to put it in viewport properly
				STextSizeProperties Props;
				int LineCount;
				Props.m_pLineCount = &LineCount;

				// Compute line of end match
				pTextRender->TextWidth(FONT_SIZE, pEntry->m_aText, Pos + SearchLength, LineWidth, 0, Props);
				int EndLine = (EntryLineCount - LineCount);
				int MatchEndLine = EntryLine + EndLine;

				// Compute line of start of match
				int MatchStartLine = MatchEndLine;
				if(LineCount > 1)
				{
					pTextRender->TextWidth(FONT_SIZE, pEntry->m_aText, Pos, LineWidth, 0, Props);
					int StartLine = (EntryLineCount - LineCount);
					MatchStartLine = EntryLine + StartLine;
				}

				if(MatchStartLine > LineToScrollStart)
				{
					LineToScrollStart = MatchStartLine;
					LineToScrollEnd = MatchEndLine;
				}

				m_vSearchMatches.emplace_back(Pos, MatchStartLine, MatchEndLine, EntryLine);
			}

			pSearchPos = str_utf8_find_nocase(pEntry->m_aText + Pos + SearchLength, pSearchText);
		}

		EntryLine += pEntry->m_LineCount;
	}

	if(!m_vSearchMatches.empty() && SearchChanged)
		m_CurrentMatchIndex = 0;
	else
		m_CurrentMatchIndex = std::clamp(m_CurrentMatchIndex, -1, (int)m_vSearchMatches.size() - 1);

	// Reverse order of lines by sorting so we have matches from top to bottom instead of bottom to top
	std::sort(m_vSearchMatches.begin(), m_vSearchMatches.end(), [](const SSearchMatch &MatchA, const SSearchMatch &MatchB) {
		if(MatchA.m_StartLine == MatchB.m_StartLine)
			return MatchA.m_Pos < MatchB.m_Pos; // Make sure to keep position order
		return MatchA.m_StartLine > MatchB.m_StartLine;
	});

	if(!m_vSearchMatches.empty() && SearchChanged)
	{
		ScrollToCenter(LineToScrollStart, LineToScrollEnd);
	}
}

void CGameConsole::CInstance::Dump()
{
	char aTimestamp[20];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "dumps/%s_dump_%s.txt", m_pName, aTimestamp);
	IOHANDLE File = m_pGameConsole->Storage()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File)
	{
		PumpBacklogPending();
		for(CInstance::CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
		{
			io_write(File, pEntry->m_aText, pEntry->m_Length);
			io_write_newline(File);
		}
		io_close(File);
		log_info("console", "%s contents were written to '%s'", m_pName, aFilename);
	}
	else
	{
		log_error("console", "Failed to open '%s'", aFilename);
	}
}

bool CGameConsole::CInstance::ExportSelectedChat()
{
	PumpBacklogPending();

	const char *pLocalName = "";
	const int LocalClientId = m_pGameConsole->GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
		pLocalName = m_pGameConsole->GameClient()->m_aClients[LocalClientId].m_aName;

	std::vector<SChatExportLine> vLines;
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(!IsChatExportableEntry(pEntry) || !pEntry->m_ExportSelected)
			continue;
		SChatExportLine Line;
		if(TryParseChatExportLine(pEntry->m_aText, pLocalName, Line))
			vLines.push_back(std::move(Line));
	}

	if(vLines.empty())
	{
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("未选择聊天记录"));
		return false;
	}

	if(!m_pGameConsole->Storage()->CreateFolder("dumps", IStorage::TYPE_SAVE) && !m_pGameConsole->Storage()->FolderExists("dumps", IStorage::TYPE_SAVE))
	{
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("聊天记录导出失败"));
		return false;
	}
	if(!m_pGameConsole->Storage()->CreateFolder("dumps/local_chat_export", IStorage::TYPE_SAVE) && !m_pGameConsole->Storage()->FolderExists("dumps/local_chat_export", IStorage::TYPE_SAVE))
	{
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("聊天记录导出失败"));
		return false;
	}

	char aTimestamp[20];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	char aBaseFilename[IO_MAX_PATH_LENGTH];
	str_format(aBaseFilename, sizeof(aBaseFilename), "dumps/local_chat_export/local_chat_export_%s", aTimestamp);

	char aTxtFilename[IO_MAX_PATH_LENGTH];
	char aHtmlFilename[IO_MAX_PATH_LENGTH];
	str_format(aTxtFilename, sizeof(aTxtFilename), "%s.txt", aBaseFilename);
	str_format(aHtmlFilename, sizeof(aHtmlFilename), "%s.html", aBaseFilename);

	bool Success = true;
	Success &= SaveChatExportTxt(m_pGameConsole->Storage(), aTxtFilename, vLines);
	Success &= SaveChatExportHtml(m_pGameConsole->Storage(), aHtmlFilename, vLines);
	Success &= SaveChatExportPng(m_pGameConsole->Storage(), m_pGameConsole->TextRender(), aBaseFilename, vLines);

	if(Success)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("已导出 %d 条聊天记录"), (int)vLines.size());
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
		SetChatExportMode(false);
	}
	else
	{
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("聊天记录导出失败"));
	}
	return Success;
}

CGameConsole::CGameConsole() :
	m_LocalConsole(CONSOLETYPE_LOCAL), m_RemoteConsole(CONSOLETYPE_REMOTE)
{
	m_ConsoleType = CONSOLETYPE_LOCAL;
	m_ConsoleState = CONSOLE_CLOSED;
	m_StateChangeEnd = 0.0f;
	m_StateChangeDuration = 0.1f;

	m_pConsoleLogger = new CConsoleLogger(this);
}

CGameConsole::~CGameConsole()
{
	if(m_pConsoleLogger)
		m_pConsoleLogger->OnConsoleDeletion();
}

CGameConsole::CInstance *CGameConsole::ConsoleForType(int ConsoleType)
{
	if(ConsoleType == CONSOLETYPE_REMOTE)
		return &m_RemoteConsole;
	return &m_LocalConsole;
}

CGameConsole::CInstance *CGameConsole::CurrentConsole()
{
	return ConsoleForType(m_ConsoleType);
}

void CGameConsole::OnReset()
{
	m_RemoteConsole.Reset();
}

int CGameConsole::PossibleMaps(const char *pStr, IConsole::FPossibleCallback pfnCallback, void *pUser)
{
	int Index = 0;
	for(const std::string &Entry : Client()->MaplistEntries())
	{
		if(str_find_nocase(Entry.c_str(), pStr))
		{
			pfnCallback(Index, Entry.c_str(), pUser);
			Index++;
		}
	}
	return Index;
}

// only defined for 0<=t<=1
static float ConsoleScaleFunc(float t)
{
	return std::sin(std::acos(1.0f - t));
}

struct CCompletionOptionRenderInfo
{
	CGameConsole *m_pSelf;
	CTextCursor m_Cursor;
	const char *m_pCurrentCmd;
	int m_WantedCompletion;
	float m_Offset;
	float *m_pOffsetChange;
	float m_Width;
	float m_TotalWidth;
};

void CGameConsole::PossibleCommandsRenderCallback(int Index, const char *pStr, void *pUser)
{
	CCompletionOptionRenderInfo *pInfo = static_cast<CCompletionOptionRenderInfo *>(pUser);

	ColorRGBA TextColor;
	if(Index == pInfo->m_WantedCompletion)
	{
		TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		const float TextWidth = pInfo->m_pSelf->TextRender()->TextWidth(pInfo->m_Cursor.m_FontSize, pStr);
		const CUIRect Rect = {pInfo->m_Cursor.m_X - 2.0f, pInfo->m_Cursor.m_Y - 2.0f, TextWidth + 4.0f, pInfo->m_Cursor.m_FontSize + 4.0f};
		Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.85f), IGraphics::CORNER_ALL, 2.0f);

		// scroll when out of sight
		const bool MoveLeft = Rect.x - *pInfo->m_pOffsetChange < 0.0f;
		const bool MoveRight = Rect.x + Rect.w - *pInfo->m_pOffsetChange > pInfo->m_Width;
		if(MoveLeft && !MoveRight)
		{
			*pInfo->m_pOffsetChange -= -Rect.x + pInfo->m_Width / 4.0f;
		}
		else if(!MoveLeft && MoveRight)
		{
			*pInfo->m_pOffsetChange += Rect.x + Rect.w - pInfo->m_Width + pInfo->m_Width / 4.0f;
		}
	}
	else
	{
		TextColor = ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f);
	}

	const char *pMatchStart = str_find_nocase(pStr, pInfo->m_pCurrentCmd);
	if(pMatchStart)
	{
		pInfo->m_pSelf->TextRender()->TextColor(TextColor);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pStr, pMatchStart - pStr);
		pInfo->m_pSelf->TextRender()->TextColor(1.0f, 0.75f, 0.0f, 1.0f);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pMatchStart, str_length(pInfo->m_pCurrentCmd));
		pInfo->m_pSelf->TextRender()->TextColor(TextColor);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pMatchStart + str_length(pInfo->m_pCurrentCmd));
	}
	else
	{
		pInfo->m_pSelf->TextRender()->TextColor(TextColor);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pStr);
	}

	pInfo->m_Cursor.m_X += 7.0f;
	pInfo->m_TotalWidth = pInfo->m_Cursor.m_X + pInfo->m_Offset;
}

void CGameConsole::Prompt(char (&aPrompt)[32])
{
	CInstance *pConsole = CurrentConsole();
	if(pConsole->m_Searching)
	{
		str_format(aPrompt, sizeof(aPrompt), "%s: ", Localize("Searching"));
	}
	else if(m_ConsoleType == CONSOLETYPE_REMOTE)
	{
		if(Client()->State() == IClient::STATE_LOADING || Client()->State() == IClient::STATE_ONLINE)
		{
			if(Client()->RconAuthed())
				str_copy(aPrompt, "rcon> ");
			else if(pConsole->m_UsernameReq && !pConsole->m_UserGot)
				str_format(aPrompt, sizeof(aPrompt), "%s> ", Localize("Enter Username"));
			else
				str_format(aPrompt, sizeof(aPrompt), "%s> ", Localize("Enter Password"));
		}
		else
			str_format(aPrompt, sizeof(aPrompt), "%s> ", Localize("NOT CONNECTED"));
	}
	else
	{
		str_copy(aPrompt, "> ");
	}
}

void CGameConsole::OnRender()
{
	CUIRect Screen = *Ui()->Screen();
	CInstance *pConsole = CurrentConsole();

	const float MaxConsoleHeight = Screen.h * 3 / 5.0f;
	float Progress = (Client()->GlobalTime() - (m_StateChangeEnd - m_StateChangeDuration)) / m_StateChangeDuration;

	if(Progress >= 1.0f)
	{
		if(m_ConsoleState == CONSOLE_CLOSING)
		{
			m_ConsoleState = CONSOLE_CLOSED;
			pConsole->m_BacklogLastActiveLine = -1;
		}
		else if(m_ConsoleState == CONSOLE_OPENING)
		{
			m_ConsoleState = CONSOLE_OPEN;
			pConsole->m_Input.Activate(EInputPriority::CONSOLE);
		}

		Progress = 1.0f;
	}

	if(m_ConsoleState == CONSOLE_OPEN && g_Config.m_ClEditor)
		Toggle(CONSOLETYPE_LOCAL);

	if(m_ConsoleState == CONSOLE_CLOSED)
	{
		m_TopbarMouseDown = false;
		return;
	}

	if(m_ConsoleState == CONSOLE_OPEN)
		Input()->MouseModeAbsolute();

	float ConsoleHeightScale;
	if(m_ConsoleState == CONSOLE_OPENING)
		ConsoleHeightScale = ConsoleScaleFunc(Progress);
	else if(m_ConsoleState == CONSOLE_CLOSING)
		ConsoleHeightScale = ConsoleScaleFunc(1.0f - Progress);
	else // CONSOLE_OPEN
		ConsoleHeightScale = ConsoleScaleFunc(1.0f);

	const float ConsoleHeight = ConsoleHeightScale * MaxConsoleHeight;

	const ColorRGBA ShadowColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f);
	const ColorRGBA TransparentColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	const ColorRGBA aBackgroundColors[NUM_CONSOLETYPES] = {ColorRGBA(0.2f, 0.2f, 0.2f, 0.9f), ColorRGBA(0.4f, 0.2f, 0.2f, 0.9f)};
	const ColorRGBA aBorderColors[NUM_CONSOLETYPES] = {ColorRGBA(0.1f, 0.1f, 0.1f, 0.9f), ColorRGBA(0.2f, 0.1f, 0.1f, 0.9f)};

	Ui()->MapScreen();

	const bool UpdateConsoleUi = !Ui()->Enabled();
	if(UpdateConsoleUi)
	{
		Ui()->SetEnabled(true);
		Ui()->StartCheck();
		const vec2 NativeMousePos = Input()->NativeMousePos();
		const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
		Ui()->OnCursorMove(NativeMousePos.x - UpdatedMousePos.x, NativeMousePos.y - UpdatedMousePos.y);
		if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
		{
			Ui()->SetActiveItem(pActiveInput);
			Ui()->SetActiveItem(nullptr);
		}
		Ui()->Update();
	}

	// background
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BACKGROUND_NOISE].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(aBackgroundColors[m_ConsoleType]);
	Graphics()->QuadsSetSubset(0, 0, Screen.w / 80.0f, ConsoleHeight / 80.0f);
	IGraphics::CQuadItem QuadItemBackground(0.0f, 0.0f, Screen.w, ConsoleHeight);
	Graphics()->QuadsDrawTL(&QuadItemBackground, 1);
	Graphics()->QuadsEnd();

	// bottom border
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(aBorderColors[m_ConsoleType]);
	IGraphics::CQuadItem QuadItemBorder(0.0f, ConsoleHeight, Screen.w, 1.0f);
	Graphics()->QuadsDrawTL(&QuadItemBorder, 1);
	Graphics()->QuadsEnd();

	// bottom shadow
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor4(ShadowColor, ShadowColor, TransparentColor, TransparentColor);
	IGraphics::CQuadItem QuadItemShadow(0.0f, ConsoleHeight + 1.0f, Screen.w, 10.0f);
	Graphics()->QuadsDrawTL(&QuadItemShadow, 1);
	Graphics()->QuadsEnd();

	{
		// Get height of 1 line
		const float LineHeight = TextRender()->TextBoundingBox(FONT_SIZE, " ", -1, -1.0f, LINE_SPACING).m_H;

		const float RowHeight = FONT_SIZE * 2.0f;

		float x = 3;
		float y = ConsoleHeight - RowHeight - 18.0f;

		const float InitialX = x;
		const float InitialY = y;

		// render prompt
		CTextCursor PromptCursor;
		PromptCursor.SetPosition(vec2(x, y + FONT_SIZE / 2.0f));
		PromptCursor.m_FontSize = FONT_SIZE;

		char aPrompt[32];
		Prompt(aPrompt);
		TextRender()->TextEx(&PromptCursor, aPrompt);

		// check if mouse is pressed
		const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
		const vec2 ScreenSize = vec2(Screen.w, Screen.h);
		bool LinkClickPending = false;
		vec2 LinkClickPos = vec2(0.0f, 0.0f);
		vec2 LinkClickPress = vec2(0.0f, 0.0f);
		bool ChatExportClickPending = false;
		vec2 ChatExportClickPos = vec2(0.0f, 0.0f);
		const bool CtrlPressed = Input()->ModifierIsPressed();
		const bool WasMousePressed = pConsole->m_MouseIsPress;
		Ui()->UpdateTouchState(m_TouchState);
		const auto &&GetMousePosition = [&]() -> vec2 {
			if(m_TouchState.m_PrimaryPressed)
			{
				return m_TouchState.m_PrimaryPosition * ScreenSize;
			}
			else
			{
				return Input()->NativeMousePos() / WindowSize * ScreenSize;
			}
		};
		if(!pConsole->m_MouseIsPress && (m_TouchState.m_PrimaryPressed || Input()->NativeMousePressed(1)))
		{
			pConsole->m_MouseIsPress = true;
			pConsole->m_MousePress = GetMousePosition();
		}
		if(pConsole->m_MouseIsPress && !m_TouchState.m_PrimaryPressed && !Input()->NativeMousePressed(1))
		{
			const vec2 ReleasePos = GetMousePosition();
			pConsole->m_MouseRelease = ReleasePos;
			pConsole->m_MouseIsPress = false;
			if(WasMousePressed && pConsole->m_ChatExportMode && pConsole->m_MousePress.y < pConsole->m_BoundingBox.m_Y && length(ReleasePos - pConsole->m_MousePress) <= LINK_CLICK_DRAG_THRESHOLD)
			{
				ChatExportClickPending = true;
				ChatExportClickPos = ReleasePos;
			}
			else if(WasMousePressed && CtrlPressed && !m_TouchState.m_PrimaryPressed)
			{
				LinkClickPending = true;
				LinkClickPos = ReleasePos;
				LinkClickPress = pConsole->m_MousePress;
			}
		}
		if(pConsole->m_MouseIsPress)
		{
			pConsole->m_MouseRelease = GetMousePosition();
		}
		const float ScaledLineHeight = LineHeight / ScreenSize.y;
		if(absolute(m_TouchState.m_ScrollAmount.y) >= ScaledLineHeight)
		{
			if(m_TouchState.m_ScrollAmount.y > 0.0f)
			{
				pConsole->m_BacklogCurLine += pConsole->GetLinesToScroll(-1, 1);
				m_TouchState.m_ScrollAmount.y -= ScaledLineHeight;
			}
			else
			{
				--pConsole->m_BacklogCurLine;
				if(pConsole->m_BacklogCurLine < 0)
					pConsole->m_BacklogCurLine = 0;
				m_TouchState.m_ScrollAmount.y += ScaledLineHeight;
			}
			pConsole->m_HasSelection = false;
		}

		x = PromptCursor.m_X;

		if(m_ConsoleState == CONSOLE_OPEN)
		{
			if(pConsole->m_MousePress.y >= pConsole->m_BoundingBox.m_Y && pConsole->m_MousePress.y < pConsole->m_BoundingBox.m_Y + pConsole->m_BoundingBox.m_H)
			{
				CLineInput::SMouseSelection *pMouseSelection = pConsole->m_Input.GetMouseSelection();
				if(pMouseSelection->m_Selecting && !pConsole->m_MouseIsPress && pConsole->m_Input.IsActive())
				{
					Input()->EnsureScreenKeyboardShown();
				}
				pMouseSelection->m_Selecting = pConsole->m_MouseIsPress;
				pMouseSelection->m_PressMouse = pConsole->m_MousePress;
				pMouseSelection->m_ReleaseMouse = pConsole->m_MouseRelease;
			}
			else if(pConsole->m_MouseIsPress)
			{
				pConsole->m_Input.SelectNothing();
			}
		}

		// render console input (wrap line)
		pConsole->m_Input.SetHidden(pConsole->IsInputHidden());
		if(m_ConsoleState == CONSOLE_OPEN)
		{
			pConsole->m_Input.Activate(EInputPriority::CONSOLE); // Ensure that the input is active
		}
		const CUIRect InputCursorRect = {x, y + FONT_SIZE * 1.5f, 0.0f, 0.0f};
		const bool WasChanged = pConsole->m_Input.WasChanged();
		const bool WasCursorChanged = pConsole->m_Input.WasCursorChanged();
		const bool Changed = WasChanged || WasCursorChanged;
		pConsole->m_BoundingBox = pConsole->m_Input.Render(&InputCursorRect, FONT_SIZE, TEXTALIGN_BL, Changed, Screen.w - 10.0f - x, LINE_SPACING);
		if(pConsole->m_LastInputHeight == 0.0f && pConsole->m_BoundingBox.m_H != 0.0f)
			pConsole->m_LastInputHeight = pConsole->m_BoundingBox.m_H;
		if(pConsole->m_Input.HasSelection())
			pConsole->m_HasSelection = false; // Clear console selection if we have a line input selection

		y -= pConsole->m_BoundingBox.m_H - FONT_SIZE;

		if(pConsole->m_LastInputHeight != pConsole->m_BoundingBox.m_H)
		{
			pConsole->m_HasSelection = false;
			pConsole->m_MouseIsPress = false;
			pConsole->m_LastInputHeight = pConsole->m_BoundingBox.m_H;
		}

		bool HandleLinkClick = false;
		if(LinkClickPending)
		{
			const bool InLogArea = LinkClickPress.y < pConsole->m_BoundingBox.m_Y;
			const float DragDistance = length(LinkClickPos - LinkClickPress);
			HandleLinkClick = InLogArea && DragDistance <= LINK_CLICK_DRAG_THRESHOLD;
		}

		// render possible commands
		if(!pConsole->m_Searching && (m_ConsoleType == CONSOLETYPE_LOCAL || Client()->RconAuthed()) && !pConsole->m_Input.IsEmpty())
		{
			pConsole->UpdateCompletionSuggestions();

			CCompletionOptionRenderInfo Info;
			Info.m_pSelf = this;
			Info.m_WantedCompletion = pConsole->m_CompletionChosen;
			Info.m_Offset = pConsole->m_CompletionRenderOffset;
			Info.m_pOffsetChange = &pConsole->m_CompletionRenderOffsetChange;
			Info.m_Width = Screen.w;
			Info.m_TotalWidth = 0.0f;
			char aCmd[IConsole::CMDLINE_LENGTH];
			pConsole->GetCommand(pConsole->m_aCompletionBuffer, aCmd);
			Info.m_pCurrentCmd = aCmd;

			Info.m_Cursor.SetPosition(vec2(InitialX - Info.m_Offset, InitialY + RowHeight + 2.0f));
			Info.m_Cursor.m_FontSize = FONT_SIZE;

			for(size_t SuggestionId = 0; SuggestionId < pConsole->m_vpCommandSuggestions.size(); ++SuggestionId)
			{
				PossibleCommandsRenderCallback(SuggestionId, pConsole->m_vpCommandSuggestions[SuggestionId], &Info);
			}
			const int NumCommands = pConsole->m_vpCommandSuggestions.size();
			Info.m_TotalWidth = Info.m_Cursor.m_X + Info.m_Offset;
			pConsole->m_CompletionRenderOffset = Info.m_Offset;

			if(NumCommands <= 0 && pConsole->m_IsCommand)
			{
				int NumArguments = 0;
				if(!pConsole->m_vpArgumentSuggestions.empty())
				{
					Info.m_WantedCompletion = pConsole->m_CompletionChosenArgument;
					Info.m_TotalWidth = 0.0f;
					Info.m_pCurrentCmd = pConsole->m_aCompletionBufferArgument;

					for(size_t SuggestionId = 0; SuggestionId < pConsole->m_vpArgumentSuggestions.size(); ++SuggestionId)
					{
						PossibleCommandsRenderCallback(SuggestionId, pConsole->m_vpArgumentSuggestions[SuggestionId], &Info);
					}
					NumArguments = pConsole->m_vpArgumentSuggestions.size();
					Info.m_TotalWidth = Info.m_Cursor.m_X + Info.m_Offset;
					pConsole->m_CompletionRenderOffset = Info.m_Offset;
				}

				if(NumArguments <= 0 && pConsole->m_IsCommand)
				{
					char aBuf[1024];
					const char *pCommandHelp = pConsole->m_pCommandHelp != nullptr ? pConsole->m_pCommandHelp : "";
					const char *pCommandParams = pConsole->m_pCommandParams != nullptr ? pConsole->m_pCommandParams : "";
					const char *pLocalizedHelp = Localize(pCommandHelp);
					const char *pLocalizedParams = Localize(pCommandParams);
					str_format(aBuf, sizeof(aBuf), Localize("Help: %s"), pLocalizedHelp);
					TextRender()->TextEx(&Info.m_Cursor, aBuf, -1);
					TextRender()->TextColor(0.75f, 0.75f, 0.75f, 1);
					str_format(aBuf, sizeof(aBuf), Localize("Usage: %s %s"), pConsole->m_pCommandName, pLocalizedParams);
					TextRender()->TextEx(&Info.m_Cursor, aBuf, -1);
				}
			}

			// Reset animation offset in case our chosen completion index changed due to new commands being added/removed
			if(pConsole->m_QueueResetAnimation)
			{
				pConsole->m_CompletionRenderOffset += pConsole->m_CompletionRenderOffsetChange;
				pConsole->m_CompletionRenderOffsetChange = 0.0f;
				pConsole->m_QueueResetAnimation = false;
			}
			Ui()->DoSmoothScrollLogic(&pConsole->m_CompletionRenderOffset, &pConsole->m_CompletionRenderOffsetChange, Info.m_Width, Info.m_TotalWidth);
		}
		else if(pConsole->m_Searching && !pConsole->m_Input.IsEmpty())
		{ // Render current match and match count
			CTextCursor MatchInfoCursor;
			MatchInfoCursor.SetPosition(vec2(InitialX, InitialY + RowHeight + 2.0f));
			MatchInfoCursor.m_FontSize = FONT_SIZE;
			TextRender()->TextColor(0.8f, 0.8f, 0.8f, 1.0f);
			if(!pConsole->m_vSearchMatches.empty())
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), Localize("Match %d of %d"), pConsole->m_CurrentMatchIndex + 1, (int)pConsole->m_vSearchMatches.size());
				TextRender()->TextEx(&MatchInfoCursor, aBuf, -1);
			}
			else
			{
				TextRender()->TextEx(&MatchInfoCursor, Localize("No results"), -1);
			}
		}

		pConsole->PumpBacklogPending();
		if(pConsole->m_NewLineCounter != 0)
		{
			pConsole->UpdateSearch();

			// keep scroll position when new entries are printed.
			if(pConsole->m_BacklogCurLine != 0 || pConsole->m_HasSelection)
			{
				pConsole->m_BacklogCurLine += pConsole->m_NewLineCounter;
				pConsole->m_BacklogLastActiveLine += pConsole->m_NewLineCounter;
			}
			if(pConsole->m_NewLineCounter < 0)
				pConsole->m_NewLineCounter = 0;
		}

		// render console log (current entry, status, wrap lines)
		CInstance::CBacklogEntry *pEntry = pConsole->m_Backlog.Last();
		float OffsetY = 0.0f;

		std::string SelectionString;
		std::vector<SLinkRange> vLinkRanges;
		std::vector<STextColorSplit> vLinkColorSplits;

		if(pConsole->m_BacklogLastActiveLine < 0)
			pConsole->m_BacklogLastActiveLine = pConsole->m_BacklogCurLine;

		int LineNum = -1;
		pConsole->m_LinesRendered = 0;

		int SkippedLines = 0;
		bool First = true;
		bool LinkClickHandled = !HandleLinkClick;

		const float XScale = Graphics()->ScreenWidth() / Screen.w;
		const float YScale = Graphics()->ScreenHeight() / Screen.h;
		const float CalcOffsetY = LineHeight * std::floor((y - RowHeight) / LineHeight);
		const float ClipStartY = (y - CalcOffsetY) * YScale;
		Graphics()->ClipEnable(0, ClipStartY, Screen.w * XScale, (y + 2.0f) * YScale - ClipStartY);

		while(pEntry)
		{
			if(pEntry->m_LineCount == -1)
				pConsole->UpdateEntryTextAttributes(pEntry);

			if(!pConsole->MatchesLogFilter(pEntry))
			{
				pEntry = pConsole->m_Backlog.Prev(pEntry);
				continue;
			}

			LineNum += pEntry->m_LineCount;
			if(LineNum < pConsole->m_BacklogLastActiveLine)
			{
				SkippedLines += pEntry->m_LineCount;
				pEntry = pConsole->m_Backlog.Prev(pEntry);
				continue;
			}
			TextRender()->TextColor(pEntry->m_PrintColor);

			if(First)
			{
				OffsetY -= (pConsole->m_BacklogLastActiveLine - SkippedLines) * LineHeight;
			}

			const float LocalOffsetY = OffsetY + pEntry->m_YOffset / (float)pEntry->m_LineCount;
			OffsetY += pEntry->m_YOffset;
			const float EntryTop = y - OffsetY;
			const float EntryBottom = EntryTop + pEntry->m_YOffset;

			// Only apply offset if we do not keep scroll position (m_BacklogCurLine == 0)
			if((pConsole->m_HasSelection || pConsole->m_MouseIsPress) && pConsole->m_NewLineCounter > 0 && pConsole->m_BacklogCurLine == 0)
			{
				pConsole->m_MousePress.y -= pEntry->m_YOffset;
				if(!pConsole->m_MouseIsPress)
					pConsole->m_MouseRelease.y -= pEntry->m_YOffset;
			}

			// stop rendering when lines reach the top
			const bool Outside = y - OffsetY <= RowHeight;
			const bool CanRenderOneLine = y - LocalOffsetY > RowHeight;
			if(Outside && !CanRenderOneLine)
				break;

			const int LinesNotRendered = pEntry->m_LineCount - minimum((int)std::floor((y - LocalOffsetY) / RowHeight), pEntry->m_LineCount);
			pConsole->m_LinesRendered -= LinesNotRendered;

			const bool ChatExportable = pConsole->IsChatExportableEntry(pEntry);
			if(pConsole->m_ChatExportMode && ChatExportable)
			{
				CUIRect EntryRect = {0.0f, EntryTop, Screen.w, EntryBottom - EntryTop};
				if(pEntry->m_ExportSelected)
					EntryRect.Draw(ColorRGBA(0.20f, 0.48f, 1.0f, 0.18f), IGraphics::CORNER_NONE, 0.0f);
				CUIRect CheckBox = {5.0f, EntryTop + maximum(2.0f, (EntryBottom - EntryTop - 11.0f) / 2.0f), 11.0f, 11.0f};
				CheckBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 2.0f);
				CheckBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f));
				if(pEntry->m_ExportSelected)
				{
					CUIRect Inner = {CheckBox.x + 3.0f, CheckBox.y + 3.0f, CheckBox.w - 6.0f, CheckBox.h - 6.0f};
					Inner.Draw(ColorRGBA(0.35f, 0.65f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 1.0f);
				}
				if(ChatExportClickPending && ChatExportClickPos.y >= EntryTop && ChatExportClickPos.y <= EntryBottom)
				{
					pConsole->ToggleChatExportEntry(pEntry, GameClient()->Input()->ShiftIsPressed());
					ChatExportClickPending = false;
				}
			}

			const float EntryTextX = pConsole->m_ChatExportMode ? 22.0f : 0.0f;
			const float EntryLineWidth = Screen.w - 10.0f - EntryTextX;
			CTextCursor EntryCursor;
			EntryCursor.SetPosition(vec2(EntryTextX, y - OffsetY));
			EntryCursor.m_FontSize = FONT_SIZE;
			EntryCursor.m_LineWidth = EntryLineWidth;
			EntryCursor.m_MaxLines = pEntry->m_LineCount;
			EntryCursor.m_LineSpacing = LINE_SPACING;
			EntryCursor.m_CalculateSelectionMode = (!pConsole->m_ChatExportMode && m_ConsoleState == CONSOLE_OPEN && pConsole->m_MousePress.y < pConsole->m_BoundingBox.m_Y && (pConsole->m_MouseIsPress || (pConsole->m_CurSelStart != pConsole->m_CurSelEnd) || pConsole->m_HasSelection)) ? TEXT_CURSOR_SELECTION_MODE_CALCULATE : TEXT_CURSOR_SELECTION_MODE_NONE;
			EntryCursor.m_PressMouse = pConsole->m_MousePress;
			EntryCursor.m_ReleaseMouse = pConsole->m_MouseRelease;

			bool ParseColors = false; // TClient

			if(pConsole->m_Searching && pConsole->m_CurrentMatchIndex != -1)
			{
				std::vector<CInstance::SSearchMatch> vMatches;
				std::copy_if(pConsole->m_vSearchMatches.begin(), pConsole->m_vSearchMatches.end(), std::back_inserter(vMatches), [&](const CInstance::SSearchMatch &Match) { return Match.m_EntryLine == LineNum + 1 - pEntry->m_LineCount; });

				auto CurrentSelectedOccurrence = pConsole->m_vSearchMatches[pConsole->m_CurrentMatchIndex];

				EntryCursor.m_vColorSplits.reserve(vMatches.size());
				for(const auto &Match : vMatches)
				{
					bool IsSelected = CurrentSelectedOccurrence.m_EntryLine == Match.m_EntryLine && CurrentSelectedOccurrence.m_Pos == Match.m_Pos;
					EntryCursor.m_vColorSplits.emplace_back(
						Match.m_Pos,
						pConsole->m_Input.GetLength(),
						IsSelected ? ms_SearchSelectedColor : ms_SearchHighlightColor);
				}
			}
			else if(pEntry->m_Length > (size_t)str_length("xxxx-xx-xx xx:xx:xx x ") && str_startswith(pEntry->m_aText + str_length("xxxx-xx-xx xx:xx:xx x "), "chat/client"))
			{
				ParseColors = true;
			}

			// TODO don't recalculate every frame
			// TODO less jank way of detecting echo
			CColoredParts ColoredParts(pEntry->m_aText, ParseColors);
			ColoredParts.AddSplitsToCursor(EntryCursor);
			if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == str_length("xxxx-xx-xx xx:xx:xx x chat/client: — "))
			{
				EntryCursor.m_vColorSplits[0].m_CharIndex -= str_length("— ");
				EntryCursor.m_vColorSplits[0].m_Length += str_length("— ");
			}
			const char *pText = ColoredParts.Text();
			vLinkRanges.clear();
			if(HasPotentialLink(pText))
				CollectLinkRanges(pText, vLinkRanges);

			if(HandleLinkClick && !LinkClickHandled && LinkClickPos.y >= EntryTop && LinkClickPos.y <= EntryBottom)
			{
				LinkClickHandled = true;
				CTextCursor LinkCursor;
				LinkCursor.SetPosition(vec2(EntryTextX, EntryTop));
				LinkCursor.m_FontSize = FONT_SIZE;
				LinkCursor.m_LineWidth = EntryLineWidth;
				LinkCursor.m_MaxLines = pEntry->m_LineCount;
				LinkCursor.m_LineSpacing = LINE_SPACING;
				LinkCursor.m_Flags = 0;
				LinkCursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_CALCULATE;
				LinkCursor.m_ReleaseMouse = LinkClickPos;
				TextRender()->TextEx(&LinkCursor, pText, -1);

				char aLink[512];
				if(FindLinkAtChar(pText, LinkCursor.m_CursorCharacter, aLink, sizeof(aLink)))
				{
					bool Opened = false;
					char aNormalized[512];
					if(const char *pAfterHttps = str_startswith_nocase(aLink, "https://"))
					{
						str_copy(aNormalized, "https://");
						str_append(aNormalized, pAfterHttps, sizeof(aNormalized));
						Opened = Client()->ViewLink(aNormalized);
					}
					else if(const char *pAfterHttp = str_startswith_nocase(aLink, "http://"))
					{
						str_copy(aNormalized, "http://");
						str_append(aNormalized, pAfterHttp, sizeof(aNormalized));
#if defined(CONF_PLATFORM_ANDROID)
						Opened = Client()->ViewLink(aNormalized);
#else
						Opened = open_link(aNormalized) != 0;
#endif
					}
					else if(str_startswith_nocase(aLink, "www."))
					{
						str_copy(aNormalized, "https://");
						str_append(aNormalized, aLink, sizeof(aNormalized));
						Opened = Client()->ViewLink(aNormalized);
					}
					if(Opened)
					{
						pConsole->m_HasSelection = false;
						pConsole->m_CurSelStart = 0;
						pConsole->m_CurSelEnd = 0;
					}
				}
			}

			if(!vLinkRanges.empty())
			{
				const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
				const ColorRGBA PrevSelectionColor = TextRender()->GetTextSelectionColor();
				TextRender()->TextColor(TransparentColor);
				TextRender()->TextSelectionColor(LINK_UNDERLINE_COLOR);
				for(const auto &Range : vLinkRanges)
				{
					CTextCursor UnderlineCursor;
					UnderlineCursor.SetPosition(vec2(EntryTextX, EntryTop));
					UnderlineCursor.m_FontSize = FONT_SIZE;
					UnderlineCursor.m_LineWidth = EntryLineWidth;
					UnderlineCursor.m_MaxLines = pEntry->m_LineCount;
					UnderlineCursor.m_LineSpacing = LINE_SPACING;
					UnderlineCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_SET;
					UnderlineCursor.m_SelectionHeightFactor = LINK_UNDERLINE_HEIGHT;
					UnderlineCursor.m_SelectionStart = Range.m_StartChar;
					UnderlineCursor.m_SelectionEnd = Range.m_EndChar;
					TextRender()->TextEx(&UnderlineCursor, pText, -1);
				}
				TextRender()->TextSelectionColor(PrevSelectionColor);
				TextRender()->TextColor(PrevTextColor);
			}

			TextRender()->TextEx(&EntryCursor, pText, -1); // TClient
			EntryCursor.m_vColorSplits = {};

			if(!vLinkRanges.empty())
			{
				BuildLinkColorSplits(vLinkRanges, vLinkColorSplits);
				if(!vLinkColorSplits.empty())
				{
					const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
					CTextCursor LinkCursor;
					LinkCursor.SetPosition(vec2(EntryTextX, EntryTop));
					LinkCursor.m_FontSize = FONT_SIZE;
					LinkCursor.m_LineWidth = EntryLineWidth;
					LinkCursor.m_MaxLines = pEntry->m_LineCount;
					LinkCursor.m_LineSpacing = LINE_SPACING;
					LinkCursor.m_vColorSplits = vLinkColorSplits;
					TextRender()->TextColor(TransparentColor);
					TextRender()->TextEx(&LinkCursor, pText, -1);
					TextRender()->TextColor(PrevTextColor);
				}
			}

			if(EntryCursor.m_CalculateSelectionMode == TEXT_CURSOR_SELECTION_MODE_CALCULATE)
			{
				pConsole->m_CurSelStart = minimum(EntryCursor.m_SelectionStart, EntryCursor.m_SelectionEnd);
				pConsole->m_CurSelEnd = maximum(EntryCursor.m_SelectionStart, EntryCursor.m_SelectionEnd);
			}
			pConsole->m_LinesRendered += First ? pEntry->m_LineCount - (pConsole->m_BacklogLastActiveLine - SkippedLines) : pEntry->m_LineCount;

			if(pConsole->m_CurSelStart != pConsole->m_CurSelEnd)
			{
				if(m_WantsSelectionCopy)
				{
					const bool HasNewLine = !SelectionString.empty();
					const size_t OffUTF8Start = str_utf8_offset_chars_to_bytes(pEntry->m_aText, pConsole->m_CurSelStart);
					const size_t OffUTF8End = str_utf8_offset_chars_to_bytes(pEntry->m_aText, pConsole->m_CurSelEnd);
					SelectionString.insert(0, (std::string(&pEntry->m_aText[OffUTF8Start], OffUTF8End - OffUTF8Start) + (HasNewLine ? "\n" : "")));
				}
				pConsole->m_HasSelection = true;
			}

			if(pConsole->m_NewLineCounter > 0) // Decrease by the entry line count since we can have multiline entries
				pConsole->m_NewLineCounter -= pEntry->m_LineCount;

			pEntry = pConsole->m_Backlog.Prev(pEntry);

			// reset color
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			First = false;

			if(!pEntry)
				break;
		}

		// Make sure to reset m_NewLineCounter when we are done drawing
		// This is because otherwise, if many entries are printed at once while console is
		// hidden, m_NewLineCounter will always be > 0 since the console won't be able to render
		// them all, thus wont be able to decrease m_NewLineCounter to 0.
		// This leads to an infinite increase of m_BacklogCurLine and m_BacklogLastActiveLine
		// when we want to keep scroll position.
		pConsole->m_NewLineCounter = 0;

		Graphics()->ClipDisable();

		pConsole->m_BacklogLastActiveLine = pConsole->m_BacklogCurLine;

		if(m_WantsSelectionCopy && !SelectionString.empty())
		{
			pConsole->m_HasSelection = false;
			pConsole->m_CurSelStart = -1;
			pConsole->m_CurSelEnd = -1;
			Input()->SetClipboardText(SelectionString.c_str());
			m_WantsSelectionCopy = false;
		}

		TextRender()->TextColor(TextRender()->DefaultTextColor());

		// render current lines and status (locked, following)
		char aLinesBuf[128];
		const int LineStart = pConsole->m_LinesRendered > 0 ? pConsole->m_BacklogCurLine + 1 : 0;
		const int LineEnd = pConsole->m_LinesRendered > 0 ? pConsole->m_BacklogCurLine + pConsole->m_LinesRendered : 0;
		str_format(aLinesBuf, sizeof(aLinesBuf), Localize("Lines %d - %d (%s)"), LineStart, LineEnd, pConsole->m_BacklogCurLine != 0 ? Localize("Locked") : Localize("Following"));
		const float LinesTextX = 10.0f;
		const float LinesTextY = FONT_SIZE / 2.f;

		const float FilterFontSize = FONT_SIZE;
		const float FilterHeight = RowHeight - 6.0f;
		const float FilterY = (RowHeight - FilterHeight) / 2.0f;
		const float FilterPadding = 6.0f;
		const float FilterSpacing = 4.0f;
		const float TopbarRightMargin = 10.0f;

		vec2 UiMousePos = Input()->NativeMousePos();
		if(WindowSize.x > 0.0f && WindowSize.y > 0.0f)
			UiMousePos = UiMousePos / WindowSize * ScreenSize;
		const bool MouseDown = Input()->NativeMousePressed(1);
		const bool MousePressed = MouseDown && !m_TopbarMouseDown;

		const float LinesWidth = TextRender()->TextWidth(FONT_SIZE, aLinesBuf);
		if(pConsole->m_ChatExportMode)
		{
			char aSelectedBuf[64];
			str_format(aSelectedBuf, sizeof(aSelectedBuf), Localize("已选 %d 条"), pConsole->SelectedChatExportCount());
			TextRender()->Text(LinesTextX + LinesWidth + 10.0f, LinesTextY, FONT_SIZE, aSelectedBuf);

			enum class EExportAction
			{
				CANCEL = 0,
				SAVE,
				CLEAR,
				SELECT_ALL,
			};
			struct SExportButton
			{
				CButtonContainer *m_pButton;
				const char *m_pLabel;
				EExportAction m_Action;
			};
			SExportButton aButtons[] = {
				{&m_ChatExportCancelButton, Localize("取消"), EExportAction::CANCEL},
				{&m_ChatExportSaveButton, Localize("导出所选"), EExportAction::SAVE},
				{&m_ChatExportClearButton, Localize("清空"), EExportAction::CLEAR},
				{&m_ChatExportSelectAllButton, Localize("全选聊天"), EExportAction::SELECT_ALL},
			};
			float ButtonRight = Screen.w - TopbarRightMargin;
			for(const SExportButton &ExportButton : aButtons)
			{
				const float ButtonWidth = TextRender()->TextWidth(FilterFontSize, ExportButton.m_pLabel) + FilterPadding * 2.0f;
				CUIRect Button = {ButtonRight - ButtonWidth, FilterY, ButtonWidth, FilterHeight};
				Ui()->DoButton_PopupMenu(ExportButton.m_pButton, ExportButton.m_pLabel, &Button, FilterFontSize, TEXTALIGN_MC);
				const bool ManualClicked = MousePressed && Button.Inside(UiMousePos);
				if(ManualClicked)
				{
					if(ExportButton.m_Action == EExportAction::CANCEL)
						pConsole->SetChatExportMode(false);
					else if(ExportButton.m_Action == EExportAction::SAVE)
						pConsole->ExportSelectedChat();
					else if(ExportButton.m_Action == EExportAction::CLEAR)
						pConsole->ClearChatExportSelection();
					else if(ExportButton.m_Action == EExportAction::SELECT_ALL)
						pConsole->SelectAllChatExportable();
				}
				ButtonRight -= ButtonWidth + FilterSpacing;
			}
		}
		else
		{
			char aVersionBuf[128];
			str_copy(aVersionBuf, "v" GAME_VERSION " on " CONF_PLATFORM_STRING " " CONF_ARCH_STRING);
			const char *pClientVersion = CLIENT_NAME " " CLIENT_RELEASE_VERSION;
			const char *apFilterLabels[] = {Localize("All"), Localize("Players"), Localize("System")};
			const CInstance::ELogFilter aFilters[] = {CInstance::ELogFilter::ALL, CInstance::ELogFilter::PLAYER, CInstance::ELogFilter::SYSTEM};
			const bool ShowExportButton = m_ConsoleType == CONSOLETYPE_LOCAL;
			const char *pExportLabel = Localize("选择导出");
			const float ExportButtonWidth = ShowExportButton ? TextRender()->TextWidth(FilterFontSize, pExportLabel) + FilterPadding * 2.0f : 0.0f;
			const float VersionRight = ShowExportButton ? Screen.w - TopbarRightMargin - ExportButtonWidth - FilterSpacing : Screen.w - TopbarRightMargin;
			float aFilterWidths[3];
			float TotalFilterWidth = 0.0f;
			for(int i = 0; i < 3; ++i)
			{
				aFilterWidths[i] = TextRender()->TextWidth(FilterFontSize, apFilterLabels[i]) + FilterPadding * 2.0f;
				TotalFilterWidth += aFilterWidths[i];
				if(i != 2)
					TotalFilterWidth += FilterSpacing;
			}

			float FilterX = LinesTextX + LinesWidth + 10.0f;
			const float VersionWidth = TextRender()->TextWidth(FONT_SIZE, aVersionBuf);
			const float FilterRightLimit = VersionRight - VersionWidth - 10.0f;
			if(FilterX + TotalFilterWidth > FilterRightLimit)
				FilterX = maximum(LinesTextX + LinesWidth + 10.0f, FilterRightLimit - TotalFilterWidth);

			CUIRect aFilterRects[3];
			float FilterLayoutX = FilterX;
			for(int i = 0; i < 3; ++i)
			{
				aFilterRects[i] = {FilterLayoutX, FilterY, aFilterWidths[i], FilterHeight};
				FilterLayoutX += aFilterWidths[i] + FilterSpacing;
			}

			for(int i = 0; i < 3; ++i)
			{
				CUIRect Button = aFilterRects[i];
				const bool Active = pConsole->m_LogFilter == aFilters[i];
				const bool UiClicked = Ui()->DoButton_PopupMenu(&m_aFilterButtons[i], apFilterLabels[i], &Button, FilterFontSize, TEXTALIGN_MC);
				const bool ManualClicked = MousePressed && Button.Inside(UiMousePos);
				if(UiClicked || ManualClicked)
					pConsole->SetLogFilter(aFilters[i]);
				if(Active)
					Button.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
			}
			if(ShowExportButton)
			{
				CUIRect Button = {Screen.w - TopbarRightMargin - ExportButtonWidth, FilterY, ExportButtonWidth, FilterHeight};
				Ui()->DoButton_PopupMenu(&m_ChatExportButton, pExportLabel, &Button, FilterFontSize, TEXTALIGN_MC);
				const bool ManualClicked = MousePressed && Button.Inside(UiMousePos);
				if(ManualClicked)
					m_LocalConsole.SetChatExportMode(true);
			}

			if(m_ConsoleType == CONSOLETYPE_REMOTE && (Client()->ReceivingRconCommands() || Client()->ReceivingMaplist()))
			{
				const float Percentage = Client()->ReceivingRconCommands() ? Client()->GotRconCommandsPercentage() : Client()->GotMaplistPercentage();
				SProgressSpinnerProperties ProgressProps;
				ProgressProps.m_Progress = Percentage;
				Ui()->RenderProgressSpinner(vec2(Screen.w / 4.0f + FONT_SIZE / 2.f, FONT_SIZE), FONT_SIZE / 2.f, ProgressProps);

				char aLoading[128];
				str_copy(aLoading, Client()->ReceivingRconCommands() ? Localize("Loading commands…") : Localize("Loading maps…"));
				if(Percentage > 0)
				{
					char aPercentage[8];
					str_format(aPercentage, sizeof(aPercentage), " %d%%", (int)(Percentage * 100));
					str_append(aLoading, aPercentage);
				}
				TextRender()->Text(Screen.w / 4.0f + FONT_SIZE + 2.0f, FONT_SIZE / 2.f, FONT_SIZE, aLoading);
			}

			TextRender()->Text(VersionRight - VersionWidth, FONT_SIZE / 2.f, FONT_SIZE, aVersionBuf);
			TextRender()->Text(VersionRight - TextRender()->TextWidth(FONT_SIZE, pClientVersion), FONT_SIZE / 2.0f + FONT_SIZE * 1.5f, FONT_SIZE, pClientVersion);
		}
		m_TopbarMouseDown = MouseDown;

		TextRender()->Text(LinesTextX, LinesTextY, FONT_SIZE, aLinesBuf);
	}

	if(UpdateConsoleUi)
	{
		Ui()->FinishCheck();
		Ui()->SetEnabled(false);
	}
}

void CGameConsole::OnMessage(int MsgType, void *pRawMsg)
{
}

bool CGameConsole::OnInput(const IInput::CEvent &Event)
{
	// accept input when opening, but not at first frame to discard the input that caused the console to open
	if(m_ConsoleState != CONSOLE_OPEN && (m_ConsoleState != CONSOLE_OPENING || m_StateChangeEnd == Client()->GlobalTime() + m_StateChangeDuration))
		return false;
	if((Event.m_Key >= KEY_F1 && Event.m_Key <= KEY_F12) || (Event.m_Key >= KEY_F13 && Event.m_Key <= KEY_F24))
		return false;

	if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS) && CurrentConsole()->m_ChatExportMode)
		CurrentConsole()->SetChatExportMode(false);
	else if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS) && !CurrentConsole()->m_Searching)
		Toggle(m_ConsoleType);
	else if(!CurrentConsole()->OnInput(Event))
	{
		if(GameClient()->Input()->ModifierIsPressed() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_C)
			m_WantsSelectionCopy = true;
	}

	return true;
}

void CGameConsole::Toggle(int Type)
{
	if(m_ConsoleType != Type && (m_ConsoleState == CONSOLE_OPEN || m_ConsoleState == CONSOLE_OPENING))
	{
		// don't toggle console, just switch what console to use
	}
	else
	{
		if(m_ConsoleState == CONSOLE_CLOSED || m_ConsoleState == CONSOLE_OPEN)
		{
			m_StateChangeEnd = Client()->GlobalTime() + m_StateChangeDuration;
		}
		else
		{
			float Progress = m_StateChangeEnd - Client()->GlobalTime();
			float ReversedProgress = m_StateChangeDuration - Progress;

			m_StateChangeEnd = Client()->GlobalTime() + ReversedProgress;
		}

		if(m_ConsoleState == CONSOLE_CLOSED || m_ConsoleState == CONSOLE_CLOSING)
		{
			Ui()->SetEnabled(false);
			m_ConsoleState = CONSOLE_OPENING;
		}
		else
		{
			ConsoleForType(Type)->m_Input.Deactivate();
			Input()->MouseModeRelative();
			Ui()->SetEnabled(true);
			GameClient()->OnRelease();
			m_ConsoleState = CONSOLE_CLOSING;
		}
	}
	m_ConsoleType = Type;
}

void CGameConsole::ConToggleLocalConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->Toggle(CONSOLETYPE_LOCAL);
}

void CGameConsole::ConToggleRemoteConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->Toggle(CONSOLETYPE_REMOTE);
}

void CGameConsole::ConClearLocalConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_LocalConsole.ClearBacklog();
}

void CGameConsole::ConClearRemoteConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_RemoteConsole.ClearBacklog();
}

void CGameConsole::ConDumpLocalConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_LocalConsole.Dump();
}

void CGameConsole::ConDumpRemoteConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_RemoteConsole.Dump();
}

void CGameConsole::ConConsolePageUp(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine += pConsole->GetLinesToScroll(-1, pConsole->m_LinesRendered);
	pConsole->m_HasSelection = false;
}

void CGameConsole::ConConsolePageDown(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine -= pConsole->GetLinesToScroll(1, pConsole->m_LinesRendered);
	pConsole->m_HasSelection = false;
	if(pConsole->m_BacklogCurLine < 0)
		pConsole->m_BacklogCurLine = 0;
}

void CGameConsole::ConConsolePageTop(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine += pConsole->GetLinesToScroll(-1, pConsole->m_LinesRendered);
	pConsole->m_HasSelection = false;
}

void CGameConsole::ConConsolePageBottom(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine = 0;
	pConsole->m_HasSelection = false;
}

void CGameConsole::ConchainConsoleOutputLevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameConsole *pSelf = (CGameConsole *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		pSelf->m_pConsoleLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_ConsoleOutputLevel)});
	}
}

void CGameConsole::RequireUsername(bool UsernameReq)
{
	if((m_RemoteConsole.m_UsernameReq = UsernameReq))
	{
		m_RemoteConsole.m_aUser[0] = '\0';
		m_RemoteConsole.m_UserGot = false;
	}
}

void CGameConsole::PrintLine(int Type, const char *pLine)
{
	if(Type == CONSOLETYPE_LOCAL)
		m_LocalConsole.PrintLine(pLine, str_length(pLine), TextRender()->DefaultTextColor());
	else if(Type == CONSOLETYPE_REMOTE)
		m_RemoteConsole.PrintLine(pLine, str_length(pLine), TextRender()->DefaultTextColor());
}

void CGameConsole::OnConsoleInit()
{
	// init console instances
	m_LocalConsole.Init(this);
	m_RemoteConsole.Init(this);

	m_pConsole = Kernel()->RequestInterface<IConsole>();

	// TClient
	Console()->Register("clear", "", CFGFLAG_CLIENT, ConClearLocalConsole, this, "Clear local console");

	Console()->Register("toggle_local_console", "", CFGFLAG_CLIENT, ConToggleLocalConsole, this, "Toggle local console");
	Console()->Register("toggle_remote_console", "", CFGFLAG_CLIENT, ConToggleRemoteConsole, this, "Toggle remote console");
	Console()->Register("clear_local_console", "", CFGFLAG_CLIENT, ConClearLocalConsole, this, "Clear local console");
	Console()->Register("clear_remote_console", "", CFGFLAG_CLIENT, ConClearRemoteConsole, this, "Clear remote console");
	Console()->Register("dump_local_console", "", CFGFLAG_CLIENT, ConDumpLocalConsole, this, "Write local console contents to a text file");
	Console()->Register("dump_remote_console", "", CFGFLAG_CLIENT, ConDumpRemoteConsole, this, "Write remote console contents to a text file");

	Console()->Register("console_page_up", "", CFGFLAG_CLIENT, ConConsolePageUp, this, "Previous page in console");
	Console()->Register("console_page_down", "", CFGFLAG_CLIENT, ConConsolePageDown, this, "Next page in console");
	Console()->Register("console_page_top", "", CFGFLAG_CLIENT, ConConsolePageTop, this, "Last page in console");
	Console()->Register("console_page_bottom", "", CFGFLAG_CLIENT, ConConsolePageBottom, this, "First page in console");
	Console()->Chain("console_output_level", ConchainConsoleOutputLevel, this);
}

void CGameConsole::OnInit()
{
	Engine()->SetAdditionalLogger(std::unique_ptr<ILogger>(m_pConsoleLogger));
	// add resize event
	Graphics()->AddWindowResizeListener([this]() {
		m_LocalConsole.UpdateBacklogTextAttributes();
		m_LocalConsole.m_HasSelection = false;
		m_RemoteConsole.UpdateBacklogTextAttributes();
		m_RemoteConsole.m_HasSelection = false;
	});
}

void CGameConsole::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_ONLINE && NewState == IClient::STATE_OFFLINE)
	{
		m_RemoteConsole.m_UserGot = false;
		m_RemoteConsole.m_aUser[0] = '\0';
		m_RemoteConsole.m_Input.Clear();
		m_RemoteConsole.m_UsernameReq = false;
	}
}
