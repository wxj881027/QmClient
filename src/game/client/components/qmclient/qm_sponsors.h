#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SPONSORS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SPONSORS_H

#include <base/str.h>

#include <engine/shared/json.h>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace qm_sponsors
{
	inline std::vector<std::string> ParseNames(const char *pMarkdown)
	{
		std::vector<std::string> vNames;
		if(!pMarkdown)
			return vNames;

		constexpr size_t MaxBytes = 64 * 1024;
		constexpr size_t MaxNames = 400;
		size_t Length = 0;
		while(Length < MaxBytes && pMarkdown[Length])
			++Length;
		const bool Truncated = Length == MaxBytes && pMarkdown[Length];
		const auto IsBlank = [](char Char) { return Char == ' ' || Char == '\t'; };

		size_t Position = 0;
		if(Length >= 3 && static_cast<unsigned char>(pMarkdown[0]) == 0xEF &&
			static_cast<unsigned char>(pMarkdown[1]) == 0xBB && static_cast<unsigned char>(pMarkdown[2]) == 0xBF)
			Position = 3;

		while(Position < Length && vNames.size() < MaxNames)
		{
			size_t Begin = Position;
			while(Position < Length && pMarkdown[Position] != '\n' && pMarkdown[Position] != '\r')
				++Position;
			size_t End = Position;
			// 字节上限落在行内时舍弃整行，避免截断 UTF-8 姓名。
			if(Position == Length && Truncated)
				break;
			if(Position < Length)
			{
				const bool CarriageReturn = pMarkdown[Position] == '\r';
				++Position;
				if(CarriageReturn && Position < Length && pMarkdown[Position] == '\n')
					++Position;
			}

			while(Begin < End && IsBlank(pMarkdown[Begin]))
				++Begin;
			while(End > Begin && IsBlank(pMarkdown[End - 1]))
				--End;
			if(Begin == End)
				continue;

			if(pMarkdown[Begin] == '-' || pMarkdown[Begin] == '*' || pMarkdown[Begin] == '+')
				++Begin;
			else
			{
				const size_t NumberBegin = Begin;
				while(Begin < End && pMarkdown[Begin] >= '0' && pMarkdown[Begin] <= '9')
					++Begin;
				if(Begin == NumberBegin || Begin == End || pMarkdown[Begin] != '.')
					continue;
				++Begin;
			}
			if(Begin == End || !IsBlank(pMarkdown[Begin]))
				continue;
			while(Begin < End && IsBlank(pMarkdown[Begin]))
				++Begin;
			if(Begin < End)
				vNames.emplace_back(pMarkdown + Begin, End - Begin);
		}
		return vNames;
	}

	class CSnapshot
	{
		std::string m_Markdown;
		std::vector<std::string> m_vNames;
		int m_Version = -1;
		int m_Revision = 0;

	public:
		bool Apply(const json_value *pPayload, bool &Changed)
		{
			Changed = false;
			if(!pPayload || pPayload->type != json_object)
				return false;
			const json_value *pMarkdown = json_object_get(pPayload, "markdown");
			const json_value *pVersion = json_object_get(pPayload, "version");
			if(!pMarkdown || pMarkdown->type != json_string || pMarkdown->u.string.length > 64 * 1024 ||
				!pVersion || pVersion->type != json_integer || pVersion->u.integer < 0 ||
				pVersion->u.integer > std::numeric_limits<int>::max() ||
				static_cast<size_t>(str_length(pMarkdown->u.string.ptr)) != pMarkdown->u.string.length ||
				!str_utf8_check(pMarkdown->u.string.ptr))
				return false;
			const int Version = static_cast<int>(pVersion->u.integer);
			if(Version < m_Version)
				return true;
			const std::string Markdown(pMarkdown->u.string.ptr, pMarkdown->u.string.length);
			if(Version == m_Version && Markdown == m_Markdown)
				return true;
			m_Markdown = Markdown;
			m_vNames = ParseNames(m_Markdown.c_str());
			m_Version = Version;
			++m_Revision;
			Changed = true;
			return true;
		}

		const std::string &Markdown() const { return m_Markdown; }
		const std::vector<std::string> &Names() const { return m_vNames; }
		int Version() const { return m_Version; }
		int Revision() const { return m_Revision; }
	};
}

#endif
