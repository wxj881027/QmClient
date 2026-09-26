#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_EXPORT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_EXPORT_H

#include <base/str.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/qm_chat_avatar.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 聊天导出：字形由主线程用 ITextRender 分帧光栅化成纯 alpha 位图，
// 之后的排版、合成与 PNG 编码全部在后台任务里做，不碰渲染线程。
namespace QmChatExport
{
	constexpr int FONT_MESSAGE = 30;
	constexpr int FONT_NAME = 24;
	constexpr int FONT_TIME = 18;
	constexpr int IMAGE_WIDTH = 1080;
	constexpr int MAX_IMAGE_HEIGHT = 12000;
	constexpr int PAGE_PADDING = 32;
	constexpr int BUBBLE_MAX_WIDTH = 740;
	constexpr int BUBBLE_PADDING = 18;
	constexpr int LINE_HEIGHT = 40;
	constexpr int NAME_HEIGHT = 32;
	constexpr int NAME_GAP = 8;
	constexpr int TIME_HEIGHT = 26;
	constexpr int RECORD_GAP = 26;

	struct SLine
	{
		std::string m_Raw;
		std::string m_Time;
		std::string m_Sender;
		std::string m_Message;
		bool m_Local = false;
		std::shared_ptr<const QmChatAvatar::SSnapshot> m_pAvatar;
	};

	struct SGlyph
	{
		int m_Width = 0;
		int m_Height = 0;
		int m_Advance = 0;
		std::vector<uint8_t> m_vAlpha;
		int m_OffsetY = 0;
	};

	using TGlyphs = std::map<std::pair<int, int>, SGlyph>;

	struct SLabels
	{
		std::string m_Title;
		std::string m_Total;
		std::string m_Messages;
	};

	struct SRecord
	{
		size_t m_LineIndex = 0;
		std::vector<std::string> m_vNameLines;
		std::vector<std::string> m_vMessageLines;
		int m_BubbleWidth = 0;
		int m_Height = 0;
	};

	struct SPage
	{
		std::vector<SRecord> m_vRecords;
		int m_Height = PAGE_PADDING * 2;
	};

	inline bool IsCancelled(const std::atomic<bool> *pCancelled)
	{
		return pCancelled != nullptr && pCancelled->load(std::memory_order_relaxed);
	}

	inline const SGlyph *FindGlyph(const TGlyphs &Glyphs, int FontSize, int Codepoint)
	{
		const auto It = Glyphs.find({FontSize, Codepoint == '\t' ? ' ' : Codepoint});
		if(It != Glyphs.end())
			return &It->second;
		const auto Fallback = Glyphs.find({FontSize, '?'});
		return Fallback == Glyphs.end() ? nullptr : &Fallback->second;
	}

	inline int Advance(const TGlyphs &Glyphs, int FontSize, int Codepoint)
	{
		const SGlyph *pGlyph = FindGlyph(Glyphs, FontSize, Codepoint);
		return pGlyph == nullptr ? std::max(1, FontSize / 2) : std::max(0, pGlyph->m_Advance);
	}

	inline int TextWidth(const std::string &Text, const TGlyphs &Glyphs, int FontSize)
	{
		int Width = 0;
		const char *pCursor = Text.c_str();
		while(*pCursor)
			Width += Advance(Glyphs, FontSize, str_utf8_decode(&pCursor));
		return Width;
	}

	// 只使用主线程准备好的字形度量，逐字换行不重复测量整段文本。
	inline std::vector<std::string> Wrap(const std::string &Text, const TGlyphs &Glyphs, int FontSize, int MaxWidth, const std::atomic<bool> *pCancelled = nullptr)
	{
		std::vector<std::string> vLines;
		std::string Current;
		int Width = 0;
		const char *pCursor = Text.c_str();
		while(*pCursor)
		{
			if(IsCancelled(pCancelled))
				return {};
			const char *pStart = pCursor;
			const int Codepoint = str_utf8_decode(&pCursor);
			if(Codepoint == '\r')
				continue;
			if(Codepoint == '\n')
			{
				vLines.push_back(std::move(Current));
				Current.clear();
				Width = 0;
				continue;
			}
			const int GlyphWidth = Advance(Glyphs, FontSize, Codepoint);
			if(!Current.empty() && Width + GlyphWidth > MaxWidth)
			{
				vLines.push_back(std::move(Current));
				Current.clear();
				Width = 0;
			}
			Current.append(pStart, pCursor - pStart);
			Width += GlyphWidth;
		}
		vLines.push_back(std::move(Current));
		return vLines;
	}

	inline std::vector<SPage> BuildPages(const std::vector<SLine> &vLines, const TGlyphs &Glyphs, const std::atomic<bool> *pCancelled = nullptr)
	{
		std::vector<SPage> vPages;
		SPage Page;
		for(size_t Index = 0; Index < vLines.size(); ++Index)
		{
			if(IsCancelled(pCancelled))
				return {};
			const auto vNames = Wrap(vLines[Index].m_Sender, Glyphs, FONT_NAME, BUBBLE_MAX_WIDTH, pCancelled);
			auto vWrapped = Wrap(vLines[Index].m_Message, Glyphs, FONT_MESSAGE, BUBBLE_MAX_WIDTH - BUBBLE_PADDING * 2, pCancelled);
			if(IsCancelled(pCancelled))
				return {};
			const int FixedHeight = (int)vNames.size() * NAME_HEIGHT + NAME_GAP + BUBBLE_PADDING * 2 + RECORD_GAP + (vLines[Index].m_Time.empty() ? 0 : TIME_HEIGHT);
			if(FixedHeight + PAGE_PADDING * 2 + LINE_HEIGHT > MAX_IMAGE_HEIGHT)
				return {};
			const size_t MaxLines = (MAX_IMAGE_HEIGHT - PAGE_PADDING * 2 - FixedHeight) / LINE_HEIGHT;
			// 单条消息也按正文行切分，不能仅截短图片高度。
			for(size_t Start = 0; Start < vWrapped.size(); Start += MaxLines)
			{
				if(IsCancelled(pCancelled))
					return {};
				const size_t End = std::min(Start + MaxLines, vWrapped.size());
				SRecord Record;
				Record.m_LineIndex = Index;
				Record.m_vNameLines = vNames;
				int Width = 0;
				for(size_t LineIndex = Start; LineIndex < End; ++LineIndex)
				{
					Width = std::max(Width, TextWidth(vWrapped[LineIndex], Glyphs, FONT_MESSAGE));
					Record.m_vMessageLines.push_back(std::move(vWrapped[LineIndex]));
				}
				Record.m_BubbleWidth = std::clamp(Width + BUBBLE_PADDING * 2, 80, BUBBLE_MAX_WIDTH);
				Record.m_Height = FixedHeight + (int)Record.m_vMessageLines.size() * LINE_HEIGHT;
				if(!Page.m_vRecords.empty() && Page.m_Height + Record.m_Height > MAX_IMAGE_HEIGHT)
				{
					vPages.push_back(std::move(Page));
					Page = SPage{};
				}
				Page.m_Height += Record.m_Height;
				Page.m_vRecords.push_back(std::move(Record));
			}
		}
		if(!Page.m_vRecords.empty())
			vPages.push_back(std::move(Page));
		return vPages;
	}

	inline void AppendEscaped(std::string &Output, const std::string &Text)
	{
		for(const char Character : Text)
		{
			switch(Character)
			{
			case '&': Output += "&amp;"; break;
			case '<': Output += "&lt;"; break;
			case '>': Output += "&gt;"; break;
			case '"': Output += "&quot;"; break;
			case '\'': Output += "&#39;"; break;
			default: Output += Character; break;
			}
		}
	}

	// 导出仍保留纯文本副本：只有日志原文，不依赖任何图片或渲染结果。
	inline std::string BuildTxt(const std::vector<SLine> &vLines)
	{
		std::string Txt;
		for(const SLine &Line : vLines)
		{
			Txt += Line.m_Raw;
#if defined(CONF_FAMILY_WINDOWS)
			Txt += "\r\n";
#else
			Txt += '\n';
#endif
		}
		return Txt;
	}

	inline bool AvatarDataUri(const SLine &Line, std::string &Uri)
	{
		auto Pixels = QmChatAvatar::Render(Line.m_pAvatar.get(), Line.m_Sender);
		CImageInfo Image;
		Image.m_Width = QmChatAvatar::SIZE;
		Image.m_Height = QmChatAvatar::SIZE;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = Pixels.data();
		CByteBufferWriter Writer;
		if(!CImageLoader::SavePng(Writer, Image))
			return false;
		std::vector<char> vEncoded((Writer.Size() + 2) / 3 * 4 + 1);
		str_base64(vEncoded.data(), (int)vEncoded.size(), Writer.Data(), (int)Writer.Size());
		Uri = "data:image/png;base64,";
		Uri += vEncoded.data();
		return true;
	}

	inline bool BuildHtml(const std::vector<SLine> &vLines, const SLabels &Labels, std::string &Html, const std::atomic<bool> *pCancelled = nullptr)
	{
		if(IsCancelled(pCancelled))
			return false;
		Html = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>";
		AppendEscaped(Html, Labels.m_Title);
		Html += "</title><style>body{margin:0;background:#1a1a1a;color:#ededed;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}.wrap{max-width:1000px;padding:32px 24px;margin:auto}h1{font-size:24px}.sub{color:#aaa;margin-bottom:32px}.msg{display:flex;align-items:flex-start;gap:18px;margin:26px 0}.msg.local{flex-direction:row-reverse}.avatar{width:80px;height:80px;border-radius:50%;flex:none}.content{display:flex;flex-direction:column;align-items:flex-start;max-width:min(740px,calc(100% - 98px));min-width:0}.local .content{align-items:flex-end}.name{font-size:24px;color:#acacac;margin-bottom:8px;overflow-wrap:anywhere}.bubble{background:#303030;border-radius:16px;padding:18px;font-size:30px;line-height:40px;white-space:pre-wrap;overflow-wrap:anywhere;text-align:left}.local .bubble{background:#204561}.time{font-size:18px;color:#888;margin-top:8px}</style></head><body><main class=\"wrap\"><h1>";
		AppendEscaped(Html, Labels.m_Title);
		Html += "</h1><div class=\"sub\">";
		AppendEscaped(Html, Labels.m_Total);
		Html += " " + std::to_string(vLines.size()) + " ";
		AppendEscaped(Html, Labels.m_Messages);
		Html += "</div>";
		std::map<std::string, std::map<const QmChatAvatar::SSnapshot *, std::string>> AvatarCache;
		for(const SLine &Line : vLines)
		{
			if(IsCancelled(pCancelled))
				return false;
			auto &Uri = AvatarCache[Line.m_Sender][Line.m_pAvatar.get()];
			if(Uri.empty() && !AvatarDataUri(Line, Uri))
				return false;
			Html += Line.m_Local ? "<div class=\"msg local\">" : "<div class=\"msg\">";
			Html += "<img class=\"avatar\" alt=\"\" src=\"" + Uri + "\"><div class=\"content\"><div class=\"name\">";
			AppendEscaped(Html, Line.m_Sender);
			Html += "</div><div class=\"bubble\">";
			AppendEscaped(Html, Line.m_Message);
			Html += "</div>";
			if(!Line.m_Time.empty())
			{
				Html += "<div class=\"time\">";
				AppendEscaped(Html, Line.m_Time);
				Html += "</div>";
			}
			Html += "</div></div>";
		}
		Html += "</main></body></html>\n";
		return !IsCancelled(pCancelled);
	}

	using TColor = std::array<uint8_t, 3>;

	inline void Blend(std::vector<uint8_t> &Pixels, int Height, int X, int Y, TColor Color, unsigned Alpha = 255)
	{
		if(X < 0 || X >= IMAGE_WIDTH || Y < 0 || Y >= Height)
			return;
		const size_t Offset = ((size_t)Y * IMAGE_WIDTH + X) * 4;
		for(size_t Channel = 0; Channel < Color.size(); ++Channel)
			Pixels[Offset + Channel] = (uint8_t)((Color[Channel] * Alpha + Pixels[Offset + Channel] * (255 - Alpha) + 127) / 255);
	}

	inline void RoundedRect(std::vector<uint8_t> &Pixels, int Height, int X, int Y, int Width, int RectHeight, TColor Color)
	{
		constexpr int Radius = 16;
		for(int Row = 0; Row < RectHeight; ++Row)
		{
			for(int Column = 0; Column < Width; ++Column)
			{
				const int Dx = std::max({Radius - Column - 1, Column - (Width - Radius), 0});
				const int Dy = std::max({Radius - Row - 1, Row - (RectHeight - Radius), 0});
				if(Dx * Dx + Dy * Dy <= Radius * Radius)
					Blend(Pixels, Height, X + Column, Y + Row, Color);
			}
		}
	}

	inline void DrawText(std::vector<uint8_t> &Pixels, int Height, const TGlyphs &Glyphs, const std::string &Text, int FontSize, int X, int Y, int MaxWidth, TColor Color)
	{
		const int Right = X + MaxWidth;
		const char *pCursor = Text.c_str();
		while(*pCursor && X < Right)
		{
			const int Codepoint = str_utf8_decode(&pCursor);
			const SGlyph *pGlyph = FindGlyph(Glyphs, FontSize, Codepoint);
			if(pGlyph != nullptr && Codepoint != ' ' && Codepoint != '\t')
			{
				const SGlyph &Glyph = *pGlyph;
				for(int Row = 0; Row < Glyph.m_Height; ++Row)
				{
					for(int Column = 0; Column < Glyph.m_Width && X + Column < Right; ++Column)
					{
						const size_t Offset = (size_t)Row * Glyph.m_Width + Column;
						if(Offset < Glyph.m_vAlpha.size() && Glyph.m_vAlpha[Offset] != 0)
							Blend(Pixels, Height, X + Column, Y + Row + Glyph.m_OffsetY, Color, Glyph.m_vAlpha[Offset]);
					}
				}
			}
			X += Advance(Glyphs, FontSize, Codepoint);
		}
	}

	inline std::vector<uint8_t> RenderPage(const SPage &Page, const std::vector<SLine> &vLines, const TGlyphs &Glyphs, const std::atomic<bool> &Cancelled)
	{
		if(Cancelled.load(std::memory_order_relaxed))
			return {};
		std::vector<uint8_t> Pixels((size_t)IMAGE_WIDTH * Page.m_Height * 4, 26);
		for(size_t Offset = 3; Offset < Pixels.size(); Offset += 4)
			Pixels[Offset] = 255;
		int Y = PAGE_PADDING;
		for(const SRecord &Record : Page.m_vRecords)
		{
			if(Cancelled.load(std::memory_order_relaxed))
				return {};
			const SLine &Line = vLines[Record.m_LineIndex];
			const int AvatarX = Line.m_Local ? IMAGE_WIDTH - 40 - QmChatAvatar::SIZE : 40;
			const int BubbleX = Line.m_Local ? AvatarX - 18 - Record.m_BubbleWidth : AvatarX + QmChatAvatar::SIZE + 18;
			const int BubbleY = Y + (int)Record.m_vNameLines.size() * NAME_HEIGHT + NAME_GAP;
			const int BubbleHeight = BUBBLE_PADDING * 2 + (int)Record.m_vMessageLines.size() * LINE_HEIGHT;
			const auto Avatar = QmChatAvatar::Render(Line.m_pAvatar.get(), Line.m_Sender);
			for(int Row = 0; Row < QmChatAvatar::SIZE; ++Row)
			{
				for(int Column = 0; Column < QmChatAvatar::SIZE; ++Column)
				{
					const size_t Offset = (Row * QmChatAvatar::SIZE + Column) * 4;
					Blend(Pixels, Page.m_Height, AvatarX + Column, Y + Row, {Avatar[Offset], Avatar[Offset + 1], Avatar[Offset + 2]}, Avatar[Offset + 3]);
				}
			}
			int NameY = Y;
			for(const std::string &NameLine : Record.m_vNameLines)
			{
				const int NameWidth = std::min(BUBBLE_MAX_WIDTH, TextWidth(NameLine, Glyphs, FONT_NAME));
				const int NameX = Line.m_Local ? BubbleX + Record.m_BubbleWidth - NameWidth : BubbleX;
				DrawText(Pixels, Page.m_Height, Glyphs, NameLine, FONT_NAME, NameX, NameY, BUBBLE_MAX_WIDTH, {172, 172, 172});
				NameY += NAME_HEIGHT;
			}
			RoundedRect(Pixels, Page.m_Height, BubbleX, BubbleY, Record.m_BubbleWidth, BubbleHeight, Line.m_Local ? TColor{32, 69, 97} : TColor{48, 48, 48});
			int TextY = BubbleY + BUBBLE_PADDING;
			for(const std::string &Text : Record.m_vMessageLines)
			{
				if(Cancelled.load(std::memory_order_relaxed))
					return {};
				DrawText(Pixels, Page.m_Height, Glyphs, Text, FONT_MESSAGE, BubbleX + BUBBLE_PADDING, TextY, Record.m_BubbleWidth - BUBBLE_PADDING * 2, {237, 237, 237});
				TextY += LINE_HEIGHT;
			}
			const int TimeWidth = std::min(BUBBLE_MAX_WIDTH, TextWidth(Line.m_Time, Glyphs, FONT_TIME));
			const int TimeX = Line.m_Local ? BubbleX + Record.m_BubbleWidth - TimeWidth : BubbleX;
			DrawText(Pixels, Page.m_Height, Glyphs, Line.m_Time, FONT_TIME, TimeX, BubbleY + BubbleHeight + 8, BUBBLE_MAX_WIDTH, {136, 136, 136});
			Y += Record.m_Height;
		}
		return Pixels;
	}

	inline bool WriteFile(IStorage *pStorage, const std::string &Filename, const void *pData, size_t Size, const std::atomic<bool> *pCancelled = nullptr)
	{
		if(IsCancelled(pCancelled))
			return false;
		IOHANDLE File = pStorage->OpenFile(Filename.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		bool Written = true;
		const auto *pBytes = static_cast<const uint8_t *>(pData);
		for(size_t Offset = 0; Offset < Size;)
		{
			const size_t ChunkSize = std::min(Size - Offset, (size_t)65536);
			if(IsCancelled(pCancelled) || io_write(File, pBytes + Offset, ChunkSize) != ChunkSize)
			{
				Written = false;
				break;
			}
			Offset += ChunkSize;
		}
		const bool Closed = io_close(File) == 0;
		if(!Written || !Closed)
			pStorage->RemoveFile(Filename.c_str(), IStorage::TYPE_SAVE);
		return Written && Closed;
	}

	// 成功时返回 true 并留下 .txt/.html/分页 .png；任何一步失败或取消都会删掉本次写出的全部文件。
	inline bool Export(IStorage *pStorage, const std::string &BaseFilename, const std::vector<SLine> &vLines, const TGlyphs &Glyphs, const SLabels &Labels, const std::atomic<bool> &Cancelled, std::atomic<int> &CompletedPages)
	{
		CompletedPages.store(0, std::memory_order_relaxed);
		if(Cancelled.load(std::memory_order_relaxed) || vLines.empty() || pStorage == nullptr)
			return false;
		const auto vPages = BuildPages(vLines, Glyphs, &Cancelled);
		if(vPages.empty() || Cancelled.load(std::memory_order_relaxed))
			return false;
		std::string Html;
		if(!BuildHtml(vLines, Labels, Html, &Cancelled))
			return false;
		std::vector<std::string> vWrittenFiles;
		const auto Fail = [&]() {
			for(const std::string &Filename : vWrittenFiles)
				pStorage->RemoveFile(Filename.c_str(), IStorage::TYPE_SAVE);
			CompletedPages.store(0, std::memory_order_relaxed);
			return false;
		};
		const std::string Txt = BuildTxt(vLines);
		if(!WriteFile(pStorage, BaseFilename + ".txt", Txt.data(), Txt.size(), &Cancelled))
			return Fail();
		vWrittenFiles.push_back(BaseFilename + ".txt");
		if(!WriteFile(pStorage, BaseFilename + ".html", Html.data(), Html.size(), &Cancelled))
			return Fail();
		vWrittenFiles.push_back(BaseFilename + ".html");
		for(size_t PageIndex = 0; PageIndex < vPages.size(); ++PageIndex)
		{
			auto Pixels = RenderPage(vPages[PageIndex], vLines, Glyphs, Cancelled);
			if(Pixels.empty())
				return Fail();
			CImageInfo Image;
			Image.m_Width = IMAGE_WIDTH;
			Image.m_Height = vPages[PageIndex].m_Height;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = Pixels.data();
			CByteBufferWriter Writer;
			if(Cancelled.load(std::memory_order_relaxed) || !CImageLoader::SavePng(Writer, Image) || Cancelled.load(std::memory_order_relaxed))
				return Fail();
			char aSuffix[32];
			if(vPages.size() == 1)
				str_copy(aSuffix, ".png");
			else
				str_format(aSuffix, sizeof(aSuffix), "_%03d.png", (int)PageIndex + 1);
			if(!WriteFile(pStorage, BaseFilename + aSuffix, Writer.Data(), Writer.Size(), &Cancelled))
				return Fail();
			vWrittenFiles.push_back(BaseFilename + aSuffix);
			CompletedPages.store((int)PageIndex + 1, std::memory_order_relaxed);
		}
		// 最后一页已完整落盘后到达的取消请求不撤销成功导出。
		return true;
	}
}

#endif
