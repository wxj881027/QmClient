// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiStore.h"

#include "QmCardWiki.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/localization.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	IStorage *s_pStorage = nullptr;
	char s_aLanguage[64] = "english";
	std::unordered_map<std::string, SWikiContent> s_Content;

	std::string TrimCopy(const std::string &Text)
	{
		size_t Begin = 0;
		size_t End = Text.size();
		while(Begin < End && static_cast<unsigned char>(Text[Begin]) <= ' ')
			++Begin;
		while(End > Begin && static_cast<unsigned char>(Text[End - 1]) <= ' ')
			--End;
		return Text.substr(Begin, End - Begin);
	}

	void ResolveLanguage()
	{
		const char *pLanguageFile = g_Config.m_ClLanguagefile;
		if(pLanguageFile == nullptr || pLanguageFile[0] == '\0')
		{
			str_copy(s_aLanguage, "english", sizeof(s_aLanguage));
			return;
		}
		str_copy(s_aLanguage, pLanguageFile, sizeof(s_aLanguage));
		const size_t Length = str_length(s_aLanguage);
		if(Length > 4 && str_comp_nocase(s_aLanguage + Length - 4, ".txt") == 0)
			s_aLanguage[Length - 4] = '\0';
		// 语言名只用于目录，过滤掉路径分隔符等不安全字符。
		for(char *pCursor = s_aLanguage; *pCursor != '\0'; ++pCursor)
		{
			const unsigned char Byte = static_cast<unsigned char>(*pCursor);
			if(std::isalnum(Byte) == 0 && *pCursor != '-' && *pCursor != '_')
				*pCursor = '_';
		}
	}

	bool LoadMarkdown(const std::string &RelativePath, std::string *pOut)
	{
		if(s_pStorage == nullptr)
			return false;
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "wiki/%s/%s.md", s_aLanguage, RelativePath.c_str());
		IOHANDLE File = s_pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL);
		if(File == nullptr)
			return false;
		char *pText = io_read_all_str(File);
		io_close(File);
		if(pText == nullptr)
			return false;
		*pOut = pText;
		free(pText);
		return true;
	}

	std::string ContentKey(const char *pScope, const std::string &RelativePath)
	{
		std::string Key(pScope);
		Key += ':';
		Key += s_aLanguage;
		Key += ':';
		Key += RelativePath;
		return Key;
	}

	SWikiContent ResolveImpl(const char *pScope, const char *pId, const SWikiDocument &Fallback)
	{
		ResolveLanguage();
		const std::string RelativePath = qm_wiki_store::IdToRelativePath(pId);
		const std::string Key = ContentKey(pScope, RelativePath);
		auto It = s_Content.find(Key);
		if(It != s_Content.end())
			return It->second;

		SWikiContent Content;
		Content.m_RelativePath = RelativePath;
		Content.m_HasUserOverride = qm_wiki_store::HasOverride(RelativePath.c_str());
		if(!LoadMarkdown(RelativePath, &Content.m_Markdown))
			Content.m_Markdown = qm_wiki_store::ComposeFromDocument(Fallback);
		else
			Content.m_HasShippedFile = !Content.m_HasUserOverride;

		auto Inserted = s_Content.emplace(Key, Content);
		return Inserted.first->second;
	}
} // namespace

void qm_wiki_store::Init(IStorage *pStorage)
{
	s_pStorage = pStorage;
	s_Content.clear();
}

const char *qm_wiki_store::LanguageDir()
{
	ResolveLanguage();
	return s_aLanguage;
}

std::string qm_wiki_store::IdToRelativePath(const char *pId)
{
	std::string Path;
	if(pId == nullptr)
		return Path;
	for(const char Ch : std::string(pId))
	{
		const unsigned char Byte = static_cast<unsigned char>(Ch);
		if(Ch == ':')
			Path += '/';
		else if(std::isalnum(Byte) != 0 || Ch == '-' || Ch == '_' || Ch == '.' || Ch == '/')
			Path += Ch;
		else
			Path += '_';
	}
	return Path;
}

bool qm_wiki_store::HasOverride(const char *pRelativePath)
{
	if(s_pStorage == nullptr || pRelativePath == nullptr)
		return false;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "wiki/%s/%s.md", LanguageDir(), pRelativePath);
	return s_pStorage->FileExists(aPath, IStorage::TYPE_SAVE);
}

std::string qm_wiki_store::ComposeFromDocument(const SWikiDocument &Document)
{
	std::string Text;
	if(Document.m_pSections == nullptr)
		return Text;
	for(int SectionIndex = 0; SectionIndex < Document.m_SectionCount; ++SectionIndex)
	{
		const SWikiSection &Section = Document.m_pSections[SectionIndex];
		if(Section.m_pHeading != nullptr && Section.m_pHeading[0] != '\0')
		{
			Text += "## ";
			Text += Localize(Section.m_pHeading);
			Text += "\n\n";
		}
		for(int ParagraphIndex = 0; ParagraphIndex < Section.m_ParagraphCount; ++ParagraphIndex)
		{
			const char *pParagraph = Section.m_apParagraphs[ParagraphIndex];
			if(pParagraph == nullptr || pParagraph[0] == '\0')
				continue;
			// 一段里写了多个步骤时（分号分隔），拆成要点更好读；只有一句时保持整段。
			const std::string Paragraph = Localize(pParagraph);
			std::vector<std::string> vParts;
			size_t PartStart = 0;
			for(size_t Index = 0; Index < Paragraph.size(); ++Index)
			{
				const bool AsciiSemicolon = Paragraph[Index] == ';';
				const bool WideSemicolon = Paragraph.compare(Index, 3, "\xef\xbc\x9b") == 0;
				if(!AsciiSemicolon && !WideSemicolon)
					continue;
				vParts.push_back(TrimCopy(Paragraph.substr(PartStart, Index - PartStart)));
				PartStart = Index + (WideSemicolon ? 3 : 1);
				Index = PartStart - 1;
			}
			vParts.push_back(TrimCopy(Paragraph.substr(PartStart)));
			if(vParts.size() >= 3)
			{
				for(const std::string &Part : vParts)
				{
					if(Part.empty())
						continue;
					Text += "- ";
					Text += Part;
					Text += '\n';
				}
				Text += '\n';
			}
			else
			{
				Text += Paragraph;
				Text += "\n\n";
			}
		}
	}
	return Text;
}

SWikiContent qm_wiki_store::ResolveCard(const char *pStableId, const SWikiDocument &Fallback)
{
	return ResolveImpl("card", pStableId, Fallback);
}

SWikiContent qm_wiki_store::ResolveDocument(const char *pDocumentId, const SWikiDocument &Fallback)
{
	return ResolveImpl("doc", pDocumentId, Fallback);
}

bool qm_wiki_store::SaveOverride(const char *pRelativePath, const std::string &Markdown, std::string *pError)
{
	if(s_pStorage == nullptr || pRelativePath == nullptr || pRelativePath[0] == '\0')
	{
		if(pError != nullptr)
			*pError = Localize("Wiki storage is not available");
		return false;
	}
	// 逐级建目录：存储层只做单层 fs_makedir，也不会自动创建父目录，
	// 所以分隔符必须自己补——少了开头的 '/'，第一段目录会被拼成 wiki/<语言><目录>。
	char aDir[IO_MAX_PATH_LENGTH];
	str_copy(aDir, "wiki", sizeof(aDir));
	s_pStorage->CreateFolder(aDir, IStorage::TYPE_SAVE);
	str_format(aDir, sizeof(aDir), "wiki/%s", LanguageDir());
	s_pStorage->CreateFolder(aDir, IStorage::TYPE_SAVE);
	std::string Nested(aDir);
	Nested += '/';
	for(const char Ch : std::string(pRelativePath))
	{
		if(Ch == '/')
		{
			s_pStorage->CreateFolder(Nested.c_str(), IStorage::TYPE_SAVE);
			Nested += '/';
		}
		else
		{
			Nested += Ch;
		}
	}

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "wiki/%s/%s.md", LanguageDir(), pRelativePath);
	IOHANDLE File = s_pStorage->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
	{
		if(pError != nullptr)
			*pError = Localize("Cannot write the wiki file");
		return false;
	}
	const bool Written = io_write(File, Markdown.data(), static_cast<unsigned>(Markdown.size())) == Markdown.size();
	io_close(File);
	if(!Written)
	{
		if(pError != nullptr)
			*pError = Localize("Cannot write the wiki file");
		return false;
	}
	s_Content.clear();
	return true;
}

bool qm_wiki_store::DeleteOverride(const char *pRelativePath)
{
	if(s_pStorage == nullptr || pRelativePath == nullptr)
		return false;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "wiki/%s/%s.md", LanguageDir(), pRelativePath);
	const bool Removed = s_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE);
	s_Content.clear();
	return Removed;
}
