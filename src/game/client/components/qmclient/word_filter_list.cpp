#include "word_filter_list.h"

#include "streamer_word_filter.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/localization.h>
#include <engine/storage.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>

namespace
{

	std::string_view Trim(std::string_view Text)
	{
		const size_t Start = Text.find_first_not_of(" \t\r");
		if(Start == std::string_view::npos)
			return {};
		return Text.substr(Start, Text.find_last_not_of(" \t\r") - Start + 1);
	}

	std::string Quote(std::string_view Text)
	{
		std::string Result = "\"";
		for(char Character : Text)
		{
			if(Character == '\\' || Character == '"')
				Result += '\\';
			Result += Character;
		}
		Result += '"';
		return Result;
	}

	void SetPreparationError(std::string &Error) noexcept
	{
		try
		{
			Error = Localizable("Cannot prepare word list. Previous words remain active.");
		}
		catch(...)
		{
			// 内存仍不足时保留已有诊断，不能从失败出口再次抛出。
		}
	}

}

namespace QmWordFilterList
{
	static bool ParseFileChecked(std::string_view Content, std::vector<std::string> &vWords, std::string &Error, bool &PreparationFailed) noexcept
	{
		PreparationFailed = false;
		try
		{
			if(Content.size() > MAX_FILE_BYTES)
			{
				Error = Localizable("Word list file exceeds 2 MiB.");
				return false;
			}
			const std::string Text(Content);
			if(Content.find('\0') != std::string_view::npos || !str_utf8_check(Text.c_str()))
			{
				Error = Localizable("Word list must be UTF-8 text without NUL characters.");
				return false;
			}
			if(Content.substr(0, 3) == "\xef\xbb\xbf")
				Content.remove_prefix(3);
			std::vector<std::string> vParsed;
			std::unordered_set<std::string> Seen;
			while(!Content.empty())
			{
				const size_t End = Content.find('\n');
				std::string_view Line = Trim(Content.substr(0, End));
				Content = End == std::string_view::npos ? std::string_view{} : Content.substr(End + 1);
				if(Line.empty() || Line.front() == '#')
					continue;
				std::string Word;
				if(Line.front() == '"')
				{
					size_t Position = 1;
					for(; Position < Line.size() && Line[Position] != '"'; ++Position)
					{
						if(Line[Position] == '\\' && Position + 1 < Line.size() && (Line[Position + 1] == '\\' || Line[Position + 1] == '"'))
							++Position;
						Word += Line[Position];
					}
					if(Position == Line.size() || (Trim(Line.substr(Position + 1)) != "," && !Trim(Line.substr(Position + 1)).empty()))
					{
						Error = Localizable("Invalid quoted word. Use one word per line.");
						return false;
					}
				}
				else
					Word = Line;
				if(Word.empty())
					continue;
				for(unsigned char Character : Word)
				{
					if(Character < ' ' || Character == 0x7f)
					{
						Error = Localizable("Word list entries cannot contain control characters.");
						return false;
					}
				}
				if(Seen.insert(Word).second)
					vParsed.push_back(std::move(Word));
				if(vParsed.size() > MAX_FILE_WORDS)
				{
					Error = Localizable("Word list exceeds 100000 unique entries.");
					return false;
				}
			}
			Error.clear();
			vWords = std::move(vParsed);
			return true;
		}
		catch(...)
		{
			PreparationFailed = true;
			SetPreparationError(Error);
			return false;
		}
	}
}

bool QmWordFilterList::ParseFile(std::string_view Content, std::vector<std::string> &vWords, std::string &Error) noexcept
{
	bool PreparationFailed;
	return ParseFileChecked(Content, vWords, Error, PreparationFailed);
}

bool QmWordFilterList::AppendInlineChunk(char *pList, size_t Capacity, const char *pChunk)
{
	const size_t Length = str_length(pList);
	const size_t ChunkLength = str_length(pChunk);
	if(!str_utf8_check(pChunk) || Length >= Capacity || ChunkLength >= Capacity - Length)
		return false;
	str_append(pList, pChunk, Capacity);
	return true;
}

bool QmWordFilterList::AppendInlineHexChunk(char *pList, size_t Capacity, const char *pChunk) noexcept
{
	try
	{
		const size_t Length = str_length(pChunk);
		if(Length % 2 != 0 || Length / 2 >= Capacity)
			return false;
		std::string Decoded;
		Decoded.reserve(Length / 2);
		for(size_t Position = 0; Position < Length; Position += 2)
		{
			int Byte = 0;
			for(size_t Offset = 0; Offset < 2; ++Offset)
			{
				const char Character = pChunk[Position + Offset];
				const int Digit = Character >= '0' && Character <= '9' ? Character - '0' :
						  Character >= 'a' && Character <= 'f' ? Character - 'a' + 10 :
											 -1;
				if(Digit < 0)
					return false;
				Byte = Byte * 16 + Digit;
			}
			if(Byte == 0)
				return false;
			Decoded += static_cast<char>(Byte);
		}
		return AppendInlineChunk(pList, Capacity, Decoded.c_str());
	}
	catch(...)
	{
		return false;
	}
}

bool QmWordFilterList::SerializeInline(const char *pList, std::vector<std::string> &vLines) noexcept
{
	try
	{
		std::string_view Remaining(pList);
		if(Remaining.size() >= INLINE_CAPACITY || !str_utf8_check(pList))
			return false;
		std::vector<std::string> vPrepared;
		if(Remaining.size() <= 2048 && Remaining.find_first_of("\r\n") == std::string_view::npos)
			vPrepared.push_back("qm_block_words_list " + Quote(Remaining));
		else
		{
			// 大列表用十六进制分段保留换行、引号和反斜杠，不改变 cfg 的命令格式。
			vPrepared.emplace_back("qm_block_words_list \"\"");
			constexpr char HEX[] = "0123456789abcdef";
			while(!Remaining.empty())
			{
				size_t Length = std::min(Remaining.size(), size_t{2048});
				while(Length < Remaining.size() && (static_cast<unsigned char>(Remaining[Length]) & 0xc0) == 0x80)
					--Length;
				std::string Line = "qm_block_words_list_append ";
				for(unsigned char Byte : Remaining.substr(0, Length))
				{
					Line += HEX[Byte >> 4];
					Line += HEX[Byte & 15];
				}
				vPrepared.push_back(std::move(Line));
				Remaining.remove_prefix(Length);
			}
		}
		vLines.swap(vPrepared);
		return true;
	}
	catch(...)
	{
		return false;
	}
}

bool QmWordFilterList::CFileList::UpdateFromText(std::string_view Content) noexcept
{
	std::vector<std::string> vWords;
	bool PreparationFailed;
	if(!ParseFileChecked(Content, vWords, m_Error, PreparationFailed))
	{
		if(PreparationFailed)
			m_AttemptedPath.clear();
		return false;
	}
	if(vWords != m_vWords)
	{
		m_vWords = std::move(vWords);
		++m_Revision;
	}
	return true;
}

void QmWordFilterList::CFileList::Refresh(IStorage *pStorage, const char *pPath, bool Force) noexcept
{
	try
	{
		if(!Force && m_AttemptedPath == pPath)
			return;
		m_AttemptedPath = pPath;
		if(m_AttemptedPath.empty())
		{
			UpdateFromText({});
			return;
		}
		std::unique_ptr<void, decltype(&io_close)> File(pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL_OR_ABSOLUTE), &io_close);
		if(!File)
		{
			m_Error = Localizable("Cannot open word list file. Previous words remain active.");
			return;
		}
		const int64_t Length = io_length(File.get());
		if(Length < 0 || static_cast<uint64_t>(Length) > MAX_FILE_BYTES)
		{
			m_Error = Localizable("Word list file exceeds 2 MiB.");
			return;
		}
		std::string Content(static_cast<size_t>(Length), '\0');
		const unsigned Read = io_read(File.get(), Content.data(), static_cast<unsigned>(Content.size()));
		char Extra;
		const bool Changed = io_read(File.get(), &Extra, 1) != 0;
		File.reset();
		if(Read != Content.size() || Changed)
		{
			m_Error = Localizable("Cannot read complete word list file. Previous words remain active.");
			return;
		}
		UpdateFromText(Content);
	}
	catch(...)
	{
		// 资源失败不消耗路径刷新机会；局部文件所有权已先释放。
		m_AttemptedPath.clear();
		SetPreparationError(m_Error);
	}
}

bool QmWordFilterList::CFileList::PublishStreamerWords(bool Force) noexcept
{
	if(!Force && m_PublishedStreamerRevision == m_Revision)
		return true;
	if(!QmStreamerWordFilter::SetAdditionalWords(m_vWords))
		return false;
	m_PublishedStreamerRevision = m_Revision;
	return true;
}
